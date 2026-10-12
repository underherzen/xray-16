// Dev harness: run the wasm engine under node with the XRFS backend reading
// straight from the game layout directory (no HTTP involved). Remaining
// browser globals the SDL2 port touches are stubbed below.
//
// Usage: node run_node.js <game_layout_dir> [engine args...]
//   node run_node.js ~/dev/cop -nosplash

const path = require('node:path');
const gameDir = process.argv[2];
if (!gameDir) {
    console.error('usage: node run_node.js <game_layout_dir> [engine args...]');
    process.exit(2);
}
// Drop our own args so the engine sees only its flags.
process.argv = [process.argv[0], process.argv[1], ...process.argv.slice(3)];

globalThis.XRFS_CONFIG = {
    baseDir: path.resolve(gameDir),
    indexFile: path.resolve(gameDir, 'files.json'),
};

// The SDL2 Emscripten port queries window.screen during SDL_Init(video).
globalThis.screen = { width: 1920, height: 1080, availWidth: 1920, availHeight: 1040 };

require('./xr_3da.js');

// The glue classifies the environment synchronously while requiring, so by
// now it has already picked the node path; DOM globals the SDL port touches
// during video-mode enumeration can be installed afterwards, before the
// async wasm start gets to main().
globalThis.window = globalThis;
globalThis.innerWidth = 1920;
globalThis.innerHeight = 1080;
