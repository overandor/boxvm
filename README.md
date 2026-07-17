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
- **File search** — instant fuzzy search across all container files
- **Drag & drop upload** — drop .box files anywhere on the portal to load
- **File preview modal** — double-click any file to preview text or hex content
- **Toast notifications** — non-intrusive status messages for all actions
- **Context menu** — right-click files for preview, hash, copy, download
- **Copy hex to clipboard** — one-click copy of hex dump from inspector
- **Export stats as JSON** — download full container statistics as JSON
- **Terminal history** — arrow up/down to recall previous commands
- **Keyboard shortcuts** — Ctrl+1-7 tab switch, Ctrl+F search, Esc close, ? for help
- **Progress bar** — animated loading progress during container operations
- **Tab badges** — file count badge on Files tab
- **Integrity badge** — visual pass/fail badge for SHA-256 verification
- **Entropy gauge** — circular gauge showing average entropy
- **Entropy heatmap** — color-coded grid visualization of per-file entropy
- **Byte histogram** — aggregated 256-bin byte frequency distribution chart
- **File size distribution** — sorted bar chart of all file sizes
- **Per-file compression bars** — visual compression ratio per file
- **Manifest viewer** — raw container manifest with per-file SHA-256 hashes
- **Compression efficiency grade** — A+ to F letter grade based on compression ratio
- **Duplicate file detector** — identifies potential duplicate files by size + entropy
- **Decompression timing** — performance.now() timing for decompression operations
- **File hash viewer** — view SHA-256 hash of any file via context menu
- **Container diff** — before/after entropy variance comparison for layout optimization
- **Resizable panels** — drag to resize sidebar and right panel widths
- **File type counts** — aggregated type counts in stats panel
- **Container summary card** — gradient card with key metrics at a glance
- **File type distribution donut chart** — canvas-based chart in LLM tab
- **Quick stats topbar** — live file count, size, threads in top bar
- **Keyboard help** — press ? to see available shortcuts
- **50+ terminal commands** — ls, cat, grep, find, tree, head, tail, wc, sort, uniq, rev, nl, od, base64, uuid, random, calc, benchmark, profile, echo, env, date, whoami, and more
- **7 color themes** — default, midnight, dracula, solarized, monokai, github, nord
- **Command palette** — Ctrl+K fuzzy search for all commands and actions
- **File sorting** — sort by name, size, entropy, type, modification time
- **File filtering** — filter by type (wasm, html, js, css, json, md, binary)
- **File pinning** — pin important files to top of list
- **Recent files** — track recently accessed files
- **30+ developer tools** — CRC32, Hamming distance, Levenshtein, JSON formatter/minifier, JS beautifier, CSS cleaner, binary↔text, endian swapper, bit counter, magic byte detector, entropy mapper, container checksum
- **10+ export formats** — JSON, CSV, Markdown, HTML, YAML, XML, LaTeX, SQL, JSONL, Properties, INI, TOML, Dot graph
- **10+ security scans** — hash all files, secret scanning, MIME checks, null byte detection, entropy flags, path traversal detection, BOM checks, control char scans, long line detection, Unicode audits, whitespace audit, comment extraction
- **10+ visualizations** — entropy distributions, correlation matrices, radar charts, diversity indices, scatter plots, box plots, percentile analysis, Z-score outliers, Shannon diversity, Simpson index
- **Collaboration features** — activity timeline, file annotations, container health metrics, risk assessment, sharing menu
- **Settings panel** — toggles for line numbers, auto-verify, animations, terminal font size slider
- **Notification dots** — visual indicators on tabs with content
- **Collapsible sections** — click headers to collapse/expand analysis sections
- **Terminal info bar** — live file count, command count, session duration
- **Extended keyboard shortcuts** — Ctrl+Shift+E export, Ctrl+Shift+S security scan, Ctrl+Shift+R report, Ctrl+Shift+T theme
- **Resizable panels** — drag handles to resize sidebar and right panel

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
- [x] v0.7.1 — 300+ portal features (50+ terminal commands, 30+ dev tools, 10+ export formats, 10+ security scans, 10+ visualizations, 7 themes, command palette, collaboration)
- [ ] v0.8 — WASI threads (SharedArrayBuffer, parallel execution)
- [ ] v0.9 — Agent framework (in-container SDK, IPC, sub-containers)
- [ ] v1.0 — Production (TLS, auth, IndexedDB, checkpointing, benchmarks)
- [ ] v2.0 — ML optimization (ONNX Runtime, KV cache, gradient compression)

## License

MIT
