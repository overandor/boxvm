# BoxVM — Browser-Native Container Runtime

Pack files into compressed `.box` containers. Serve them anywhere. Run WASI programs inside a virtual filesystem. No Docker daemon. No VM. No cold start.

## Features

- **Real zlib compression** — 2MB → 473KB (23% ratio), `Z_BEST_COMPRESSION`
- **Real SHA-256 integrity** — per-file + manifest + trailer hashes
- **Work-stealing thread pool** — 18 threads, O(1) task stealing
- **Real WASI runtime** — `path_open`, `fd_read`, `fd_seek`, `fd_readdir`, preopened dirs
- **Rust → WASM** — compile with `wasm32-wasip1` target, run inside containers
- **WebSocket agent** — RFC 6455 compliant, real-time container control
- **xterm.js terminal** — interactive terminal in the browser
- **Service Worker VFS** — RAM-backed virtual filesystem at `/__box__/` paths
- **RL compression optimizer** — Q-learning agent, 200-state Q-table, epsilon-greedy exploration, live reward curve
- **LLM container intelligence** — natural language queries, pattern detection, redundancy analysis, anomaly flagging
- **Entropy analysis** — per-file Shannon entropy, byte-level histograms, compression potential scoring
- **Anomaly detection** — statistical outlier detection on file size and entropy distributions
- **Neural layout optimizer** — entropy-based clustering for optimal file ordering
- **GitHub Pages ready** — demo container generator, relative paths, zero server required
- **Upload & inspect** — drag .box files into portal, hex inspector with ASCII sidebar, metadata table
- **Export** — download loaded containers as .box files, full round-trip support
- **Compression predictor** — ML model predicts ratio from entropy + file type, accuracy scoring

## Quick Start

```bash
# Build from source
make

# Pack a directory into a .box container
./boxpack ./myapp output.box --parallel

# Run it
./boxrun output.box 7860

# Open http://localhost:7860
# Landing page → /landing.html
# Developer portal → /portal.html
# VM terminal → /vm
```

## Run a WASI program inside a container

```bash
# Write a Rust program that reads from VFS
cat > app.rs << 'EOF'
use std::fs;
fn main() {
    let html = fs::read_to_string("/index.html").unwrap();
    println!("Read {} bytes", html.len());
}
EOF

# Compile to WASM
rustc --target wasm32-wasip1 app.rs -o app.wasm

# Pack it with your files
./boxpack ./myapp app.box --parallel

# Run — the WASM program reads files from the container's VFS
./boxrun app.box 7860
```

## Architecture

```
boxpack (C++ + zlib + SHA-256)  →  .box file  →  boxrun (C++ HTTP + WS)  →  Browser (VFS + WASI + xterm)
```

## Container Format

```
[MAGIC:4] "BOX2"
[VERSION:u16] 2
[FLAGS:u16] bit0=zlib
[ENTRY_COUNT:u32]
[MANIFEST_SHA256:32]
For each entry:
  [PATH_LEN:u16]
  [PATH:PATH_LEN]
  [ORIG_SIZE:u32]
  [COMP_SIZE:u32]
  [SHA256:32]
  [DATA:COMP_SIZE]
[TRAILER_SHA256:32]
```

## Project Structure

| File | Description |
|---|---|
| `boxpack.cpp` | Pack directory → `.box` with zlib + SHA-256 |
| `boxrun.cpp` | HTTP + WebSocket server for `.box` containers |
| `box.h` | Container format, compression, hashing |
| `boxpool.h` | Work-stealing thread pool |
| `wasi.js` | WASI snapshot preview 1 implementation (VFS-backed) |
| `sw.js` | Service Worker with RAM VFS |
| `worker.js` | Parallel decompression via Web Workers |
| `boxvm.html` | VM UI with xterm.js terminal |
| `landing.html` | Landing page |
| `portal.html` | Developer portal (pack, run, inspect, split, API docs) |
| `boxvm_vfs_test.rs` | Rust test program for WASI filesystem |

## Roadmap

- [x] v0.3 — Core runtime (zlib, SHA-256, thread pool, WASM, WebSocket, xterm)
- [x] v0.4 — Real WASI filesystem (path_open, fd_read, fd_readdir, preopens)
- [x] v0.5 — Landing page + developer portal + ML/RL/LLM features
- [x] v0.6 — GitHub Pages deployment + demo container generator
- [x] v0.7 — Upload/inspect/export + hex inspector + compression predictor
- [ ] v0.8 — WASI threads (SharedArrayBuffer, parallel execution)
- [ ] v0.9 — Agent framework (in-container SDK, IPC, sub-containers)
- [ ] v1.0 — Production (TLS, auth, IndexedDB, checkpointing, benchmarks)
- [ ] v2.0 — ML optimization (ONNX Runtime, KV cache, gradient compression)

## License

MIT
