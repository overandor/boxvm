#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>
#include <openssl/sha.h>

// ===== .box Container Format v2 =====
// [MAGIC:4] "BOX2"
// [VERSION:u16] 2
// [FLAGS:u16] bit0=zlib
// [ENTRY_COUNT:u32]
// [MANIFEST_SHA256:32]  (hash of all entry metadata)
// For each entry:
//   [PATH_LEN:u16]
//   [PATH:PATH_LEN]
//   [ORIG_SIZE:u32]
//   [COMP_SIZE:u32]
//   [SHA256:32]           (hash of original data)
//   [DATA:COMP_SIZE]      (real zlib deflate)
// [TRAILER_SHA256:32]     (hash of entire container body)

static const char BOX_MAGIC[4] = {'B','O','X','2'};
static const uint16_t BOX_VERSION = 2;
static const uint16_t FLAG_ZLIB = 0x0001;

struct BoxEntry {
    std::string path;
    uint32_t origSize = 0;
    uint32_t compSize = 0;
    uint8_t sha256[32] = {};
    std::vector<uint8_t> compData;
    std::vector<uint8_t> origData;
};

// LE read/write
inline void w16(std::vector<uint8_t>&o, uint16_t v){o.push_back(v&0xff);o.push_back(v>>8);}
inline void w32(std::vector<uint8_t>&o, uint32_t v){o.push_back(v&0xff);o.push_back((v>>8)&0xff);o.push_back((v>>16)&0xff);o.push_back((v>>24)&0xff);}
inline uint16_t r16(const uint8_t*&p){uint16_t v=p[0]|(p[1]<<8);p+=2;return v;}
inline uint32_t r32(const uint8_t*&p){uint32_t v=p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24);p+=4;return v;}

// Real zlib compression (deflate)
inline std::vector<uint8_t> zlibCompress(const std::vector<uint8_t>& in) {
    uLong bound = compressBound(in.size());
    std::vector<uint8_t> out(bound);
    uLongf outLen = bound;
    if (compress2(out.data(), &outLen, in.data(), in.size(), Z_BEST_COMPRESSION) != Z_OK) {
        // Fallback: store raw
        out = in;
        return out;
    }
    out.resize(outLen);
    return out;
}

// Real zlib decompression
inline std::vector<uint8_t> zlibDecompress(const uint8_t* data, size_t size, uint32_t origSize) {
    std::vector<uint8_t> out(origSize);
    uLongf outLen = origSize;
    if (uncompress(out.data(), &outLen, data, size) != Z_OK) {
        // Maybe stored raw
        out.assign(data, data + size);
        return out;
    }
    out.resize(outLen);
    return out;
}

// Real SHA-256 via OpenSSL
inline void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    SHA256(data, len, out);
}

inline std::string sha256Hex(const uint8_t in[32]) {
    static const char hex[] = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 32; i++) { s += hex[in[i]>>4]; s += hex[in[i]&0xf]; }
    return s;
}

// MIME types
inline const char* mimeFor(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    auto ext = path.substr(dot + 1);
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "js") return "application/javascript";
    if (ext == "css") return "text/css";
    if (ext == "json") return "application/json";
    if (ext == "wasm") return "application/wasm";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "webp") return "image/webp";
    if (ext == "woff2") return "font/woff2";
    if (ext == "woff") return "font/woff";
    if (ext == "ttf") return "font/ttf";
    if (ext == "ico") return "image/x-icon";
    if (ext == "txt") return "text/plain; charset=utf-8";
    if (ext == "dat") return "application/octet-stream";
    if (ext == "xml") return "application/xml";
    if (ext == "pdf") return "application/pdf";
    return "application/octet-stream";
}
