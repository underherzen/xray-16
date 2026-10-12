// Dev probe: exercise the XRFS write path without running the engine.
// Usage: node fsprobe.js <game_layout_dir>

const path = require('node:path');
const gameDir = process.argv[2];
process.argv = [process.argv[0], process.argv[1]];

globalThis.XRFS_CONFIG = {
    baseDir: path.resolve(gameDir),
    indexFile: path.resolve(gameDir, 'files.json'),
};
globalThis.screen = { width: 1920, height: 1080 };

const Module = require('./xr_3da.js');
Module.noInitialRun = true;
Module.onRuntimeInitialized = () => {
    const FS = Module.FS;
    const steps = [
        ['lookupPath parent', '/cop/_appdata_/logs/probe.log', () => { const l = FS.lookupPath('/cop/_appdata_/logs/probe.log', { parent: true }); return l.path + ' node=' + (l.node && l.node.name); }],
        ['lookupPath noent', '/cop/_appdata_/logs/probe.log', () => { const l = FS.lookupPath('/cop/_appdata_/logs/probe.log', { noent_okay: true }); return l.path + ' node=' + (l.node && l.node.name); }],
        ['mknod direct', '/cop/_appdata_/logs/probe3.log', () => { const n = FS.mknod('/cop/_appdata_/logs/probe3.log', 0o100666, 0); return n.name; }],
        ['open w', '/cop/_appdata_/logs/probe4.log', () => { const s = FS.open('/cop/_appdata_/logs/probe4.log', 'w'); return 'fd=' + s.fd; }],
        ['write new', '/cop/_appdata_/logs/probe.log', () => { FS.writeFile('/cop/_appdata_/logs/probe.log', 'hello'); return Buffer.from(FS.readFile('/cop/_appdata_/logs/probe.log')).toString(); }],
    ];
    for (const [what, p, fn] of steps) {
        try {
            console.log('OK  ', what, p, '->', JSON.stringify(fn()));
        } catch (e) {
            console.log('FAIL', what, p, '->', e.errno !== undefined ? 'errno ' + e.errno : (e.message || e));
        }
    }
    process.exit(0);
};
