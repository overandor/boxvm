// boxrun.cpp — C++ HTTP + WebSocket server for .box containers
// Usage: boxrun <file.box> [port] [--public [subdomain]]
#include "box.h"
#include "boxpool.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// SHA-1 + base64 for WebSocket handshake (RFC 6455)
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

static std::vector<uint8_t> g_boxData;
static std::string g_boxPath;
static std::string g_vmHtml;
static std::string g_swJs;
static std::string g_workerJs;
static std::string g_wasiJs;
static std::string g_landingHtml;
static std::string g_portalHtml;
static bool g_public = false;
static std::string g_publicSub;
static WorkStealingPool* g_pool = nullptr;

// WebSocket clients (agent connections)
static std::vector<int> g_wsClients;
static std::mutex g_wsMtx;

static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static std::string base64Encode(const uint8_t* data, size_t len) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data, len);
    BIO_flush(b64);
    BUF_MEM* ptr;
    BIO_get_mem_ptr(b64, &ptr);
    std::string out(ptr->data, ptr->length);
    BIO_free_all(b64);
    return out;
}

static std::string wsAcceptKey(const std::string& key) {
    std::string guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = key + guid;
    uint8_t hash[SHA_DIGEST_LENGTH];
    SHA1((const unsigned char*)combined.c_str(), combined.size(), hash);
    return base64Encode(hash, SHA_DIGEST_LENGTH);
}

static void wsSend(int fd, const std::string& msg) {
    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN + text
    size_t len = msg.size();
    if (len < 126) {
        frame.push_back((uint8_t)len);
    } else if (len < 65536) {
        frame.push_back(126);
        frame.push_back((len >> 8) & 0xff);
        frame.push_back(len & 0xff);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--) frame.push_back((len >> (i*8)) & 0xff);
    }
    frame.insert(frame.end(), msg.begin(), msg.end());
    send(fd, (const char*)frame.data(), frame.size(), 0);
}

static void wsBroadcast(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_wsMtx);
    for (auto it = g_wsClients.begin(); it != g_wsClients.end();) {
        if (send(*it, nullptr, 0, 0) < 0) { close(*it); it = g_wsClients.erase(it); }
        else { wsSend(*it, msg); ++it; }
    }
}

static void handleWs(int fd) {
    {
        std::lock_guard<std::mutex> lk(g_wsMtx);
        g_wsClients.push_back(fd);
    }
    uint8_t buf[4096];
    while (true) {
        int n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        // Parse WebSocket frame (simplified: text frames only)
        if (n < 2) continue;
        uint8_t opcode = buf[0] & 0x0f;
        bool masked = buf[1] & 0x80;
        uint64_t plen = buf[1] & 0x7f;
        int headerLen = 2;
        if (plen == 126) { plen = (buf[2] << 8) | buf[3]; headerLen = 4; }
        else if (plen == 127) { plen = 0; for (int i = 0; i < 8; i++) plen = (plen << 8) | buf[2+i]; headerLen = 10; }
        uint8_t mask[4] = {};
        if (masked) { memcpy(mask, buf + headerLen, 4); headerLen += 4; }
        std::string payload((char*)buf + headerLen, (size_t)plen);
        if (masked) for (size_t i = 0; i < payload.size(); i++) payload[i] ^= mask[i % 4];

        if (opcode == 0x8) break; // close
        if (opcode == 0x1) { // text
            // Parse JSON command
            fprintf(stderr, "[WS] agent: %s\n", payload.c_str());
            // Echo back with status
            wsSend(fd, "{\"type\":\"ack\",\"msg\":\"received\"}");
            // Broadcast to other clients
            wsBroadcast("{\"type\":\"agent\",\"data\":" + payload + "}");
        }
    }
    {
        std::lock_guard<std::mutex> lk(g_wsMtx);
        g_wsClients.erase(std::remove(g_wsClients.begin(), g_wsClients.end(), fd), g_wsClients.end());
    }
    close(fd);
}

static void sendResp(int fd, int status, const char* ct, const void* data, size_t len) {
    const char* st = status == 200 ? "OK" : status == 404 ? "Not Found" : "Error";
    char hdr[512];
    int hl = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\n"
        "Cross-Origin-Embedder-Policy: require-corp\r\n"
        "Cross-Origin-Opener-Policy: same-origin\r\n"
        "Connection: close\r\n\r\n", status, st, ct, len);
    send(fd, hdr, hl, 0);
    send(fd, (const char*)data, len, 0);
}

static void handleClient(int fd) {
    char buf[8192];
    int n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(fd); return; }
    buf[n] = 0;
    std::string req(buf, n);
    size_t sp1 = req.find(' ');
    size_t sp2 = req.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) { close(fd); return; }
    std::string method = req.substr(0, sp1);
    std::string path = req.substr(sp1 + 1, sp2 - sp1 - 1);

    // WebSocket upgrade
    if (path == "/ws" && req.find("Upgrade: websocket") != std::string::npos) {
        size_t keyPos = req.find("Sec-WebSocket-Key: ");
        if (keyPos != std::string::npos) {
            size_t eol = req.find("\r\n", keyPos);
            std::string key = req.substr(keyPos + 19, eol - keyPos - 19);
            std::string accept = wsAcceptKey(key);
            std::string resp = "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
            send(fd, resp.c_str(), resp.size(), 0);
            handleWs(fd);
            return;
        }
    }

    if (path == "/" || path == "/index.html" || path == "/landing.html") {
        sendResp(fd, 200, "text/html; charset=utf-8", g_landingHtml.data(), g_landingHtml.size());
    } else if (path == "/portal.html") {
        sendResp(fd, 200, "text/html; charset=utf-8", g_portalHtml.data(), g_portalHtml.size());
    } else if (path == "/vm") {
        sendResp(fd, 200, "text/html; charset=utf-8", g_vmHtml.data(), g_vmHtml.size());
    } else if (path == "/sw.js") {
        sendResp(fd, 200, "application/javascript", g_swJs.data(), g_swJs.size());
    } else if (path == "/worker.js") {
        sendResp(fd, 200, "application/javascript", g_workerJs.data(), g_workerJs.size());
    } else if (path == "/wasi.js") {
        sendResp(fd, 200, "application/javascript", g_wasiJs.data(), g_wasiJs.size());
    } else if (path == "/api/box") {
        sendResp(fd, 200, "application/octet-stream", g_boxData.data(), g_boxData.size());
    } else if (path == "/api/size") {
        char json[64];
        int jl = snprintf(json, sizeof(json), "{\"size\":%zu}", g_boxData.size());
        sendResp(fd, 200, "application/json", json, jl);
    } else if (path == "/api/public" && g_public) {
        char json[256];
        int jl = snprintf(json, sizeof(json), "{\"url\":\"https://boxvm.dev/%s\",\"subdomain\":\"%s\"}", g_publicSub.c_str(), g_publicSub.c_str());
        sendResp(fd, 200, "application/json", json, jl);
    } else if (path == "/api/split" && method == "POST") {
        // Agent split request — broadcast to WS clients
        wsBroadcast("{\"type\":\"split\",\"data\":\"" + std::to_string(n) + "\"}");
        const char* ok = "{\"ok\":true}";
        sendResp(fd, 200, "application/json", ok, strlen(ok));
    } else {
        const char* nf = "{\"error\":\"not found\"}";
        sendResp(fd, 404, "application/json", nf, strlen(nf));
    }
    close(fd);
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: boxrun <file.box> [port] [--public [subdomain]]\n"); return 1; }
    g_boxPath = argv[1];
    int port = 7860;
    srand((unsigned)time(nullptr));
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--public") {
            g_public = true;
            if (i + 1 < argc && argv[i+1][0] != '-') g_publicSub = argv[++i];
            else { char r[9]; for (int j=0;j<8;j++) r[j]="abcdefghijklmnopqrstuvwxyz0123456789"[rand()%36]; r[8]=0; g_publicSub=r; }
        } else if (arg[0] >= '0' && arg[0] <= '9') port = atoi(arg.c_str());
    }

    g_boxData = readFile(g_boxPath);
    if (g_boxData.empty()) { fprintf(stderr, "Cannot read %s\n", g_boxPath.c_str()); return 1; }

    char exeDir[4096] = ".";
#ifdef __APPLE__
    uint32_t sz = sizeof(exeDir);
    if (_NSGetExecutablePath(exeDir, &sz) == 0) { char* s = strrchr(exeDir, '/'); if (s) *s = 0; }
#endif
    auto loadFile = [](const char* dir, const char* name) -> std::string {
        std::string p = std::string(dir) + "/" + name;
        std::ifstream f(p, std::ios::binary);
        if (!f) { fprintf(stderr, "Warning: %s not found\n", p.c_str()); return ""; }
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    g_landingHtml = loadFile(exeDir, "landing.html");
    g_portalHtml = loadFile(exeDir, "portal.html");
    g_vmHtml = loadFile(exeDir, "boxvm.html");
    g_swJs = loadFile(exeDir, "sw.js");
    g_workerJs = loadFile(exeDir, "worker.js");
    g_wasiJs = loadFile(exeDir, "wasi.js");

    WorkStealingPool pool(std::thread::hardware_concurrency());
    g_pool = &pool;

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) { fprintf(stderr, "Cannot bind port %d\n", port); return 1; }
    listen(srv, 64);

    fprintf(stderr, "\n  BoxVM Runtime v2\n  Container: %s (%.1f KB)\n  Open: http://localhost:%d\n", g_boxPath.c_str(), g_boxData.size() / 1024.0, port);
    if (g_public) fprintf(stderr, "  Public: https://boxvm.dev/%s\n", g_publicSub.c_str());
    fprintf(stderr, "  Threads: %zu (work-stealing)\n  WebSocket: ws://localhost:%d/ws\n\n", std::thread::hardware_concurrency(), port);

    while (true) {
        int client = accept(srv, nullptr, nullptr);
        if (client < 0) continue;
        pool.submit([client] { handleClient(client); });
    }
}
