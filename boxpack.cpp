// boxpack.cpp — pack a directory into a .box container with real zlib + SHA-256
// Usage: boxpack <input_dir> <output.box> [--parallel]
#include "box.h"
#include "boxpool.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <ctime>

static void walkDir(const std::string& dir, const std::string& prefix, std::vector<std::string>& files) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d))) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        std::string full = dir + "/" + name;
        std::string rel = prefix.empty() ? name : prefix + "/" + name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) walkDir(full, rel, files);
        else if (S_ISREG(st.st_mode)) files.push_back(rel);
    }
    closedir(d);
}

static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: boxpack <input_dir> <output.box> [--parallel]\n");
        return 1;
    }
    std::string inDir = argv[1];
    std::string outPath = argv[2];
    bool parallel = argc > 3 && std::string(argv[3]) == "--parallel";
    if (!inDir.empty() && inDir.back() == '/') inDir.pop_back();

    std::vector<std::string> files;
    walkDir(inDir, "", files);
    std::sort(files.begin(), files.end());
    if (files.empty()) { fprintf(stderr, "No files found in %s\n", inDir.c_str()); return 1; }

    std::vector<BoxEntry> entries(files.size());
    uint64_t totalOrig = 0, totalComp = 0;

    if (parallel) {
        WorkStealingPool pool;
        std::vector<std::future<void>> futures;
        for (size_t i = 0; i < files.size(); i++) {
            futures.push_back(pool.submit([&, i] {
                auto data = readFile(inDir + "/" + files[i]);
                entries[i].path = files[i];
                entries[i].origSize = (uint32_t)data.size();
                entries[i].origData = data;
                sha256(data.data(), data.size(), entries[i].sha256);
                entries[i].compData = zlibCompress(data);
                entries[i].compSize = (uint32_t)entries[i].compData.size();
            }));
        }
        for (auto& f : futures) f.get();
        pool.shutdown();
    } else {
        for (size_t i = 0; i < files.size(); i++) {
            auto data = readFile(inDir + "/" + files[i]);
            entries[i].path = files[i];
            entries[i].origSize = (uint32_t)data.size();
            entries[i].origData = data;
            sha256(data.data(), data.size(), entries[i].sha256);
            entries[i].compData = zlibCompress(data);
            entries[i].compSize = (uint32_t)entries[i].compData.size();
        }
    }

    for (auto& e : entries) { totalOrig += e.origSize; totalComp += e.compSize; }

    // Build manifest hash: SHA-256 of all paths + sizes + file hashes
    std::vector<uint8_t> manifestData;
    for (auto& e : entries) {
        manifestData.insert(manifestData.end(), e.path.begin(), e.path.end());
        w32(manifestData, e.origSize);
        manifestData.insert(manifestData.end(), e.sha256, e.sha256 + 32);
    }
    uint8_t manifestHash[32];
    sha256(manifestData.data(), manifestData.size(), manifestHash);

    // Build .box file
    std::vector<uint8_t> out;
    out.insert(out.end(), BOX_MAGIC, BOX_MAGIC + 4);
    w16(out, BOX_VERSION);
    w16(out, FLAG_ZLIB);
    w32(out, (uint32_t)entries.size());
    out.insert(out.end(), manifestHash, manifestHash + 32);

    for (auto& e : entries) {
        w16(out, (uint16_t)e.path.size());
        out.insert(out.end(), e.path.begin(), e.path.end());
        w32(out, e.origSize);
        w32(out, e.compSize);
        out.insert(out.end(), e.sha256, e.sha256 + 32);
        out.insert(out.end(), e.compData.begin(), e.compData.end());
    }

    // Trailer: SHA-256 of entire body (everything after magic)
    uint8_t trailerHash[32];
    sha256(out.data() + 4, out.size() - 4, trailerHash);
    out.insert(out.end(), trailerHash, trailerHash + 32);

    std::ofstream f(outPath, std::ios::binary);
    f.write((const char*)out.data(), out.size());
    f.close();

    double ratio = totalOrig > 0 ? (100.0 * totalComp / totalOrig) : 0;
    fprintf(stderr, "Packed %zu files → %s\n", entries.size(), outPath.c_str());
    fprintf(stderr, "  Original:  %llu bytes\n", (unsigned long long)totalOrig);
    fprintf(stderr, "  Container: %llu bytes (%.1f%%)\n", (unsigned long long)out.size(), ratio);
    fprintf(stderr, "  Manifest:  %s\n", sha256Hex(manifestHash).c_str());
    fprintf(stderr, "  Trailer:   %s\n", sha256Hex(trailerHash).c_str());
    return 0;
}
