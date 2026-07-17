// sw.js — Service Worker with RAM VFS
const vfs = new Map();
self.addEventListener('install', e => { self.skipWaiting(); });
self.addEventListener('activate', e => { e.waitUntil(self.clients.claim()); });

self.addEventListener('message', e => {
    const msg = e.data;
    if (msg.type === 'LOAD') {
        for (const f of msg.files) vfs.set(f.path, f.data);
        if (e.ports[0]) e.ports[0].postMessage({ ok: true, count: vfs.size });
        self.clients.matchAll().then(cs => cs.forEach(c => c.postMessage({ type: 'VFS_READY', count: vfs.size })));
    } else if (msg.type === 'SPLIT') {
        const sub = new Map();
        for (const f of msg.files) sub.set(f.path, vfs.get(f.path) || f.data);
        for (const [k, v] of sub) vfs.set(msg.prefix + '/' + k, v);
        if (e.ports[0]) e.ports[0].postMessage({ ok: true, prefix: msg.prefix, count: sub.size });
    } else if (msg.type === 'ADD_FILE') {
        vfs.set(msg.path, msg.data);
        if (e.ports[0]) e.ports[0].postMessage({ ok: true });
    } else if (msg.type === 'REMOVE_FILE') {
        vfs.delete(msg.path);
        if (e.ports[0]) e.ports[0].postMessage({ ok: true });
    } else if (msg.type === 'LIST') {
        if (e.ports[0]) e.ports[0].postMessage({ ok: true, files: Array.from(vfs.keys()) });
    } else if (msg.type === 'GET_VFS') {
        const serialized = {};
        for (const [k, v] of vfs) serialized[k] = v;
        if (e.ports[0]) e.ports[0].postMessage({ ok: true, vfs: serialized });
    }
});

function mimeFor(path) {
    const ext = path.split('.').pop();
    const m = {html:'text/html;charset=utf-8',js:'application/javascript',css:'text/css',json:'application/json',wasm:'application/wasm',png:'image/png',jpg:'image/jpeg',jpeg:'image/jpeg',gif:'image/gif',svg:'image/svg+xml',webp:'image/webp',woff2:'font/woff2',woff:'font/woff',ttf:'font/ttf',ico:'image/x-icon',txt:'text/plain;charset=utf-8',dat:'application/octet-stream',xml:'application/xml',pdf:'application/pdf'};
    return m[ext] || 'application/octet-stream';
}

self.addEventListener('fetch', e => {
    const url = new URL(e.request.url);
    if (!url.pathname.startsWith('/__box__/')) return;
    let path = url.pathname.replace('/__box__/', '');
    if (!vfs.has(path)) {
        if (vfs.has(path + '/index.html')) path = path + '/index.html';
        else if (vfs.has('index.html')) path = 'index.html';
        else { e.respondWith(new Response('Not found: ' + path, { status: 404 })); return; }
    }
    e.respondWith(new Response(vfs.get(path), {
        headers: { 'Content-Type': mimeFor(path), 'Access-Control-Allow-Origin': '*', 'Cache-Control': 'no-store' }
    }));
});
