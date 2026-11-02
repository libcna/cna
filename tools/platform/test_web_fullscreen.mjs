#!/usr/bin/env node
// SPDX-License-Identifier: MS-PL
// CNA-FIX-D26: real DOM fullscreen must retain a browser-sized drawable and XNA-sized backbuffer.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {extname, join, resolve} from 'node:path';

const require = createRequire(import.meta.url);
function loadPlaywright() {
    for (const base of [process.env.NODE_PATH, '/opt/node22/lib/node_modules', '/usr/lib/node_modules']) {
        if (!base) continue;
        try { return require(join(base, 'playwright')); } catch { /* next installation */ }
    }
    return require('playwright');
}

const root = resolve(process.argv[2] ?? '');
if (!process.argv[2]) throw new Error('usage: test_web_fullscreen.mjs <C-API-Wasm-artifact-directory>');
const html = `<!doctype html><meta charset="utf-8"><link rel="icon" href="data:,">
<style>html,body{margin:0;padding:0;}button{height:56px;}canvas{display:block;width:100vw;height:calc(100vh - 56px);}</style>
<button id="fullscreen">Fullscreen</button><canvas id="canvas"></canvas>
<script type="module">
const query = new URLSearchParams(location.search);
const width = Number(query.get('width')), height = Number(query.get('height'));
const canvas = document.querySelector('canvas');
canvas.width = width; canvas.height = height;
const cna = await (await import('/cna_c_api.mjs')).default({canvas});
function check(name, code) { if (code !== 0) throw new Error(name + ': ' + code); }
const viewportOut = cna._malloc(32);
const effectOut = cna._malloc(8), vertices = cna._malloc(192), primitives = cna._malloc(48);
const quad = [[-1,-1],[1,-1],[1,1],[-1,-1],[1,1],[-1,1]];
const corners = quad.concat([quad[2],quad[1],quad[0],quad[5],quad[4],quad[3]]);
corners.forEach(([x,y], i) => {
    cna.HEAPF32[(vertices >> 2) + 4*i] = x;
    cna.HEAPF32[(vertices >> 2) + 4*i + 1] = y;
    cna.HEAPF32[(vertices >> 2) + 4*i + 2] = 0;
    cna.HEAPU8.set([24,48,96,255], vertices + 16*i + 12);
});
cna.HEAPU8.fill(0, primitives, primitives + 48);
cna.HEAPU32[primitives >> 2] = 48; cna.HEAPU32[(primitives >> 2) + 1] = 1;
cna.HEAPU32[(primitives >> 2) + 3] = 1;
cna.HEAPU32[(primitives >> 2) + 4] = vertices;
cna.HEAP32[(primitives >> 2) + 9] = 12; cna.HEAP32[(primitives >> 2) + 10] = 4;
let effect = 0n;
let logicalSize;
const draw = cna.addFunction((game) => {
    check('getDevice', cna._cna_game_get_graphics_device(game, viewportOut));
    const device = cna.getValue(viewportOut, 'i64');
    check('clear', cna._cna_graphics_device_clear_rgba(device, 0, 0, 0, 1));
    check('getViewport', cna._cna_graphics_device_get_viewport(device, viewportOut + 8));
    logicalSize = [cna.HEAP32[(viewportOut + 16) >> 2], cna.HEAP32[(viewportOut + 20) >> 2]];
    if (effect === 0n) {
        check('effect', cna._cna_basic_effect_create(device, effectOut));
        effect = cna.getValue(effectOut, 'i64');
        check('vertexColor', cna._cna_basic_effect_set_vertex_color_enabled(effect, 1));
    }
    check('applyEffect', cna._cna_effect_apply(effect));
    check('draw', cna._cna_graphics_device_draw_user_primitives(device, primitives));
    return 0;
}, 'ijiii');
const callbacks = cna._malloc(32);
cna.HEAPU8.fill(0, callbacks, callbacks + 32);
cna.HEAPU32[callbacks >> 2] = 32; cna.HEAPU32[(callbacks >> 2) + 1] = 1;
cna.HEAPU32[(callbacks >> 2) + 4] = draw;
const info = cna._malloc(48);
cna.HEAPU8.fill(0, info, info + 48);
cna.HEAPU32[info >> 2] = 48; cna.HEAPU32[(info >> 2) + 1] = 1;
cna.setValue(info + 16, 166667n, 'i64'); cna.HEAPU32[(info + 40) >> 2] = callbacks;
const handles = cna._malloc(16);
check('createGame', cna._cna_game_create(info, handles)); const game = cna.getValue(handles, 'i64');
check('createManager', cna._cna_graphics_device_manager_create(game, handles + 8));
const manager = cna.getValue(handles + 8, 'i64');
check('width', cna._cna_graphics_device_manager_set_preferred_back_buffer_width(manager, width));
check('height', cna._cna_graphics_device_manager_set_preferred_back_buffer_height(manager, height));
check('presentation', cna._cna_graphics_device_manager_set_preferred_presentation_mode_ext(manager, 0));
const running = cna._malloc(1);
let frames = 0, capture;
function span(pixels) {
    const hits = [];
    for (let i = 0; i < pixels.length; i += 4)
        if (pixels[i] === 24 && pixels[i+1] === 48 && pixels[i+2] === 96) hits.push(i / 4);
    return {first: hits[0] ?? -1, last: hits.at(-1) ?? -1, count: hits.length};
}
function snapshot() {
    const css = canvas.getBoundingClientRect(), gl = canvas.getContext('webgl2');
    const previous = gl.getParameter(gl.FRAMEBUFFER_BINDING);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    const row = new Uint8Array(canvas.width * 4), column = new Uint8Array(canvas.height * 4);
    // Read the actual presented canvas in the same task as Present, before its buffer is discarded.
    gl.readPixels(0, Math.floor(canvas.height / 2), canvas.width, 1, gl.RGBA, gl.UNSIGNED_BYTE, row);
    gl.readPixels(Math.floor(canvas.width / 2), 0, 1, canvas.height, gl.RGBA, gl.UNSIGNED_BYTE, column);
    gl.bindFramebuffer(gl.FRAMEBUFFER, previous);
    return {frames, fullscreen: document.fullscreenElement?.id ?? null,
        canvas: [canvas.width, canvas.height], css: [css.width, css.height],
        drawing: [gl.drawingBufferWidth, gl.drawingBufferHeight], logicalSize,
        row: span(row), column: span(column), error: gl.getError()};
}
function tick() {
    check('frame', cna._cna_game_run_frame_ext(game, running)); frames++;
    if (capture) { const resolve = capture; capture = null; resolve(snapshot()); }
    requestAnimationFrame(tick);
}
document.querySelector('button').onclick = () => {
    check('fullscreen', cna._cna_graphics_device_manager_set_is_full_screen(manager, 1));
    check('apply', cna._cna_graphics_device_manager_apply_changes(manager));
};
window.probe = {capture: () => new Promise(resolve => {capture = resolve;}), frames: () => frames};
requestAnimationFrame(tick);
</script>`;

const mime = {'.mjs': 'text/javascript', '.js': 'text/javascript', '.wasm': 'application/wasm'};
const server = createServer(async (request, response) => {
    try {
        const path = new URL(request.url, 'http://localhost').pathname;
        const body = path === '/' ? html : await readFile(join(root, path));
        response.writeHead(200, {'Content-Type': path === '/' ? 'text/html' : mime[extname(path)] ?? 'application/octet-stream',
            'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp'});
        response.end(body);
    } catch { response.writeHead(404); response.end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const {chromium} = loadPlaywright();
let browser;
let passed = 0;
function verifyImage(label, state, width, height) {
    assert.equal(state.error, 0, label + ': GL error');
    assert.deepEqual(state.canvas, state.drawing, label + ': canvas/drawable disagree');
    assert.deepEqual(state.logicalSize, [width, height], label + ': logical viewport changed');
    const scale = Math.min(state.canvas[0] / width, state.canvas[1] / height);
    const w = Math.round(width * scale), h = Math.round(height * scale);
    const x = Math.round((state.canvas[0] - w) / 2), y = Math.round((state.canvas[1] - h) / 2);
    assert.deepEqual(state.row, {first: x, last: x + w - 1, count: w}, label + ': horizontal presentation pixels');
    assert.deepEqual(state.column, {first: y, last: y + h - 1, count: h}, label + ': vertical presentation pixels');
    passed++;
    console.log('[PASS] ' + label + ' ' + JSON.stringify(state));
}
try {
    browser = await chromium.launch({executablePath: process.env.PLAYWRIGHT_CHROMIUM || undefined,
        args: ['--use-gl=swiftshader', '--enable-unsafe-swiftshader', '--no-sandbox']});
    for (const dpr of [1, 2]) for (const [width, height] of [[480, 800], [800, 480]]) {
        const page = await browser.newPage({viewport: {width: 800, height: 600},
            screen: {width: 800, height: 600}, deviceScaleFactor: dpr});
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.goto('http://127.0.0.1:' + server.address().port + '/?width=' + width + '&height=' + height);
        await page.waitForFunction(() => window.probe?.frames() > 5);
        const initial = await page.evaluate(() => window.probe.capture());
        const scale = initial.canvas[0] / width;
        const label = width + 'x' + height + ' DPR=' + dpr;
        verifyImage(label + ' windowed', initial, width, height);
        for (let cycle = 0; cycle < 2; cycle++) {
            const before = await page.evaluate(() => window.probe.frames());
            await page.click('#fullscreen');
            await page.waitForFunction(() => document.fullscreenElement?.id === 'canvas');
            await page.waitForFunction(frame => window.probe.frames() > frame + 5, before);
            const fullscreen = await page.evaluate(() => window.probe.capture());
            assert.deepEqual(fullscreen.canvas, fullscreen.css.map(value => Math.round(value * scale)),
                label + ': fullscreen canvas must match its visible physical rectangle');
            verifyImage(label + ' fullscreen ' + cycle, fullscreen, width, height);
            await page.evaluate(() => document.exitFullscreen());
            await page.waitForFunction(() => !document.fullscreenElement);
            const exit = await page.evaluate(() => window.probe.frames());
            await page.waitForFunction(frame => window.probe.frames() > frame + 5, exit);
            const windowed = await page.evaluate(() => window.probe.capture());
            assert.deepEqual(windowed.canvas, initial.canvas, label + ': windowed drawing size was not restored');
            verifyImage(label + ' restored ' + cycle, windowed, width, height);
        }
        assert.deepEqual(errors, [], label + ': browser errors');
        await page.close();
    }
    console.log('=== ' + passed + '/20 PASS ===');
} finally {
    await browser?.close();
    server.close();
}
