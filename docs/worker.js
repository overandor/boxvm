// worker.js — parallel decompression via Web Workers
self.onmessage = async function(e) {
    const { id, entries } = e.data;
    const results = [];
    for (const entry of entries) {
        try {
            const ds = new DecompressionStream('deflate');
            const writer = ds.writable.getWriter();
            const reader = ds.readable.getReader();
            writer.write(new Uint8Array(entry.compData));
            writer.close();
            const chunks = [];
            while (true) {
                const { done, value } = await reader.read();
                if (done) break;
                chunks.push(value);
            }
            let total = 0;
            for (const c of chunks) total += c.length;
            const data = new Uint8Array(total);
            let off = 0;
            for (const c of chunks) { data.set(c, off); off += c.length; }
            results.push({ path: entry.path, data, origSize: entry.origSize });
        } catch (err) {
            results.push({ path: entry.path, data: new Uint8Array(entry.compData), origSize: entry.origSize, error: err.message });
        }
    }
    self.postMessage({ id, results });
};
