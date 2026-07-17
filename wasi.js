// wasi.js — full WASI implementation with VFS-backed filesystem
// v0.4: preopened dirs, nested paths, fd_readdir, fd_prestat
const WASI = function (vfs) {
    this.stdout = '';
    this.stderr = '';
    this.exitCode = 0;
    this.vfs = vfs || new Map();
    this.openFiles = new Map();
    this.nextFd = 3;
    this.args = [];

    // Preopened directories: fd -> path
    // fd 3 = / (root), fd 4 = / (preopen)
    this.preopens = new Map();
    this.preopens.set(3, '/');
    this.openFiles.set(3, { path: '/', data: null, pos: 0, isDir: true, preopen: true });
    this.nextFd = 4;
};

const ESUCCESS = 0, EBADF = 8, ENOENT = 44, EINVAL = 28, EISDIR = 31, ENOTDIR = 54, EACCES = 2;
const FT_REGULAR = 4, FT_DIRECTORY = 3, FT_CHARACTER = 2;
const OFLAGS_CREAT = 1, OFLAGS_DIRECTORY = 2, OFLAGS_EXCL = 4, OFLAGS_TRUNC = 8;
const RIGHTS_FD_READ = 2n, RIGHTS_FD_WRITE = 64n, RIGHTS_FD_SEEK = 4n;

WASI.prototype.fd_write = function (fd, iovs, iovsLen, nwritten) {
    let total = 0;
    const mem = this.memory;
    for (let i = 0; i < iovsLen; i++) {
        const ptr = mem.getUint32(iovs + i * 8, true);
        const len = mem.getUint32(iovs + 4 + i * 8, true);
        const bytes = new Uint8Array(mem.buffer, ptr, len);
        if (fd === 1) this.stdout += new TextDecoder().decode(bytes);
        else if (fd === 2) this.stderr += new TextDecoder().decode(bytes);
        else {
            const file = this.openFiles.get(fd);
            if (file && file.data) {
                if (file.pos + len > file.data.length) {
                    const nd = new Uint8Array(file.pos + len);
                    nd.set(file.data);
                    file.data = nd;
                }
                file.data.set(bytes, file.pos);
                file.pos += len;
            }
        }
        total += len;
    }
    mem.setUint32(nwritten, total, true);
    return ESUCCESS;
};

WASI.prototype.fd_read = function (fd, iovs, iovsLen, nread) {
    const mem = this.memory;
    if (fd === 0) { mem.setUint32(nread, 0, true); return ESUCCESS; }
    const file = this.openFiles.get(fd);
    if (!file) { mem.setUint32(nread, 0, true); return EBADF; }
    if (file.isDir) { mem.setUint32(nread, 0, true); return EISDIR; }
    let total = 0;
    for (let i = 0; i < iovsLen; i++) {
        const ptr = mem.getUint32(iovs + i * 8, true);
        const len = mem.getUint32(iovs + 4 + i * 8, true);
        if (!file.data) break;
        const remaining = file.data.length - file.pos;
        const toRead = Math.min(len, remaining);
        if (toRead <= 0) break;
        new Uint8Array(mem.buffer, ptr, toRead).set(file.data.subarray(file.pos, file.pos + toRead));
        file.pos += toRead;
        total += toRead;
    }
    mem.setUint32(nread, total, true);
    return ESUCCESS;
};

WASI.prototype.fd_seek = function (fd, offset, whence, newoffset) {
    const mem = this.memory;
    const file = this.openFiles.get(fd);
    if (!file) { mem.setBigUint64(newoffset, 0n, true); return EBADF; }
    let np = file.pos;
    if (whence === 0) np = offset;
    else if (whence === 1) np += offset;
    else if (whence === 2) np = (file.data ? file.data.length : 0) + offset;
    if (np < 0) np = 0;
    file.pos = np;
    mem.setBigUint64(newoffset, BigInt(np), true);
    return ESUCCESS;
};

WASI.prototype.fd_close = function (fd) { this.openFiles.delete(fd); return ESUCCESS; };

WASI.prototype.fd_fdstat_get = function (fd, buf) {
    const mem = this.memory;
    const file = this.openFiles.get(fd);
    let ft = FT_CHARACTER;
    if (file && file.isDir) ft = FT_DIRECTORY;
    else if (file) ft = FT_REGULAR;
    mem.setUint8(buf, ft);
    mem.setUint16(buf + 2, 0, true);
    mem.setBigUint64(buf + 8, 0xffffffffffffffffn, true);
    mem.setBigUint64(buf + 16, 0xffffffffffffffffn, true);
    return ESUCCESS;
};

WASI.prototype.fd_fdstat_set_flags = function (fd, flags) { return ESUCCESS; };

WASI.prototype._resolve = function (dirfd, path) {
    const base = this.preopens.get(dirfd) || '/';
    let p = path.replace(/^\//, '').replace(/\/+$/, '');
    let b = base.replace(/^\//, '').replace(/\/+$/, '');
    return b ? b + '/' + p : p;
};

WASI.prototype._listDir = function (dirPath) {
    const prefix = dirPath ? dirPath.replace(/\/+$/, '') + '/' : '';
    const seen = new Set();
    const entries = [];
    for (const [k] of this.vfs) {
        if (!k.startsWith(prefix)) continue;
        const rest = k.slice(prefix.length);
        if (!rest) continue;
        const slash = rest.indexOf('/');
        if (slash === -1) {
            if (!seen.has(rest)) { seen.add(rest); entries.push({ name: rest, type: FT_REGULAR }); }
        } else {
            const d = rest.slice(0, slash);
            if (!seen.has(d)) { seen.add(d); entries.push({ name: d, type: FT_DIRECTORY }); }
        }
    }
    return entries;
};

WASI.prototype.path_open = function (dirfd, dirflags, pathPtr, pathLen, oflags, rb, ri, fdFlags, openedFd) {
    const mem = this.memory;
    const rawPath = new TextDecoder().decode(new Uint8Array(mem.buffer, pathPtr, pathLen));
    const path = rawPath.replace(/^\//, '').replace(/\/+$/, '');
    const resolved = this._resolve(dirfd, path);

    // Check if it's a regular file in VFS
    if (this.vfs.has(resolved)) {
        const fd = this.nextFd++;
        this.openFiles.set(fd, { path: resolved, data: this.vfs.get(resolved), pos: 0, isDir: false });
        mem.setUint32(openedFd, fd, true);
        return ESUCCESS;
    }

    // Check if it's a directory (has children in VFS)
    const children = this._listDir(resolved);
    if (children.length > 0 || resolved === '') {
        const fd = this.nextFd++;
        this.openFiles.set(fd, { path: resolved, data: null, pos: 0, isDir: true });
        mem.setUint32(openedFd, fd, true);
        return ESUCCESS;
    }

    mem.setUint32(openedFd, 0, true);
    return ENOENT;
};

WASI.prototype.fd_readdir = function (fd, buf, bufLen, cookie, bufused) {
    const mem = this.memory;
    const file = this.openFiles.get(fd);
    if (!file || !file.isDir) { mem.setUint32(bufused, 0, true); return EBADF; }
    const entries = this._listDir(file.path);
    let offset = 0;
    for (let i = cookie; i < entries.length; i++) {
        const e = entries[i];
        const nb = new TextEncoder().encode(e.name);
        const sz = 24 + nb.length;
        if (offset + sz > bufLen) break;
        mem.setBigUint64(buf + offset, BigInt(i + 1), true);
        mem.setBigUint64(buf + offset + 8, BigInt(i + 1), true);
        mem.setUint32(buf + offset + 16, nb.length, true);
        mem.setUint8(buf + offset + 20, e.type);
        new Uint8Array(mem.buffer, buf + offset + 24, nb.length).set(nb);
        offset += sz;
    }
    mem.setUint32(bufused, offset, true);
    return ESUCCESS;
};

WASI.prototype.path_filestat_get = function (dirfd, flags, pathPtr, pathLen, buf) {
    const mem = this.memory;
    const rawPath = new TextDecoder().decode(new Uint8Array(mem.buffer, pathPtr, pathLen));
    const path = rawPath.replace(/^\//, '').replace(/\/+$/, '');
    const resolved = this._resolve(dirfd, path);

    if (this.vfs.has(resolved)) {
        const data = this.vfs.get(resolved);
        mem.setBigUint64(buf, 0n, true);
        mem.setBigUint64(buf + 8, 1n, true);
        mem.setUint8(buf + 16, FT_REGULAR);
        mem.setBigUint64(buf + 24, 1n, true);
        mem.setBigUint64(buf + 32, BigInt(data.length), true);
        mem.setBigUint64(buf + 40, 0n, true);
        mem.setBigUint64(buf + 48, 0n, true);
        mem.setBigUint64(buf + 56, 0n, true);
        return ESUCCESS;
    }

    // Check if directory
    const children = this._listDir(resolved);
    if (children.length > 0 || resolved === '') {
        mem.setBigUint64(buf, 0n, true);
        mem.setBigUint64(buf + 8, 1n, true);
        mem.setUint8(buf + 16, FT_DIRECTORY);
        mem.setBigUint64(buf + 24, 1n, true);
        mem.setBigUint64(buf + 32, 0n, true);
        mem.setBigUint64(buf + 40, 0n, true);
        mem.setBigUint64(buf + 48, 0n, true);
        mem.setBigUint64(buf + 56, 0n, true);
        return ESUCCESS;
    }

    return ENOENT;
};

WASI.prototype.fd_prestat_get = function (fd, buf) {
    const mem = this.memory;
    if (!this.preopens.has(fd)) { return EBADF; }
    // prestat: u8 type (0=dir), u32 nameLen
    const preopenPath = this.preopens.get(fd);
    const nameLen = new TextEncoder().encode(preopenPath).length;
    mem.setUint8(buf, 0); // prestat_dir
    mem.setUint32(buf + 4, nameLen, true);
    return ESUCCESS;
};

WASI.prototype.fd_prestat_dir_name = function (fd, pathPtr, pathLen) {
    const mem = this.memory;
    if (!this.preopens.has(fd)) { return EBADF; }
    const preopenPath = this.preopens.get(fd);
    const nb = new TextEncoder().encode(preopenPath);
    const writeLen = Math.min(nb.length, pathLen);
    new Uint8Array(mem.buffer, pathPtr, writeLen).set(nb.subarray(0, writeLen));
    return ESUCCESS;
};

WASI.prototype.fd_filestat_get = function (fd, buf) {
    const mem = this.memory;
    const file = this.openFiles.get(fd);
    if (!file) return EBADF;
    mem.setBigUint64(buf, 0n, true);
    mem.setBigUint64(buf + 8, 1n, true);
    mem.setUint8(buf + 16, file.isDir ? FT_DIRECTORY : FT_REGULAR);
    mem.setBigUint64(buf + 24, 1n, true);
    mem.setBigUint64(buf + 32, file.data ? BigInt(file.data.length) : 0n, true);
    mem.setBigUint64(buf + 40, 0n, true);
    mem.setBigUint64(buf + 48, 0n, true);
    mem.setBigUint64(buf + 56, 0n, true);
    return ESUCCESS;
};

WASI.prototype.args_get = function (argv, buf) {
    const mem = this.memory;
    let bo = buf;
    for (let i = 0; i < this.args.length; i++) {
        mem.setUint32(argv + i * 4, bo, true);
        const enc = new TextEncoder().encode(this.args[i] + '\0');
        new Uint8Array(mem.buffer, bo, enc.length).set(enc);
        bo += enc.length;
    }
    return ESUCCESS;
};

WASI.prototype.args_sizes_get = function (argc, bufSize) {
    const mem = this.memory;
    mem.setUint32(argc, this.args.length, true);
    let t = 0;
    for (const a of this.args) t += a.length + 1;
    mem.setUint32(bufSize, t, true);
    return ESUCCESS;
};

WASI.prototype.environ_get = function () { return ESUCCESS; };
WASI.prototype.environ_sizes_get = function (count, bufSize) {
    this.memory.setUint32(count, 0, true);
    this.memory.setUint32(bufSize, 0, true);
    return ESUCCESS;
};

WASI.prototype.proc_exit = function (code) { this.exitCode = code; throw new Error('exit:' + code); };

WASI.prototype.clock_time_get = function (cid, prec, time) {
    this.memory.setBigUint64(time, BigInt(Date.now()) * 1000000n, true);
    return ESUCCESS;
};

WASI.prototype.clock_res_get = function (cid, res) {
    this.memory.setBigUint64(res, 1000000n, true);
    return ESUCCESS;
};

WASI.prototype.random_get = function (buf, len) {
    crypto.getRandomValues(new Uint8Array(this.memory.buffer, buf, len));
    return ESUCCESS;
};

WASI.prototype.importObject = function () {
    const s = this;
    return {
        wasi_snapshot_preview1: {
            fd_write: (...a) => s.fd_write(...a),
            fd_read: (...a) => s.fd_read(...a),
            fd_seek: (...a) => s.fd_seek(...a),
            fd_close: (...a) => s.fd_close(...a),
            fd_fdstat_get: (...a) => s.fd_fdstat_get(...a),
            fd_fdstat_set_flags: (...a) => s.fd_fdstat_set_flags(...a),
            fd_readdir: (...a) => s.fd_readdir(...a),
            fd_prestat_get: (...a) => s.fd_prestat_get(...a),
            fd_prestat_dir_name: (...a) => s.fd_prestat_dir_name(...a),
            fd_filestat_get: (...a) => s.fd_filestat_get(...a),
            path_open: (...a) => s.path_open(...a),
            path_filestat_get: (...a) => s.path_filestat_get(...a),
            args_get: (...a) => s.args_get(...a),
            args_sizes_get: (...a) => s.args_sizes_get(...a),
            environ_get: (...a) => s.environ_get(...a),
            environ_sizes_get: (...a) => s.environ_sizes_get(...a),
            proc_exit: (...a) => s.proc_exit(...a),
            clock_time_get: (...a) => s.clock_time_get(...a),
            clock_res_get: (...a) => s.clock_res_get(...a),
            random_get: (...a) => s.random_get(...a),
        }
    };
};

WASI.prototype.execute = async function (wasmBytes, args, vfs) {
    if (vfs) this.vfs = vfs;
    this.args = args || [];
    const mod = await WebAssembly.compile(wasmBytes);
    const inst = await WebAssembly.instantiate(mod, this.importObject());
    this.memory = inst.exports.memory;
    if (typeof inst.exports._start === 'function') {
        try { inst.exports._start(); } catch (e) {
            if (!e.message || !e.message.startsWith('exit:')) throw e;
        }
    }
    return { stdout: this.stdout, stderr: this.stderr, exitCode: this.exitCode };
};

if (typeof module !== 'undefined') module.exports = WASI;
if (typeof self !== 'undefined') self.WASI = WASI;
