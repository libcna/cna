#!/usr/bin/env node
// Runs a built Emscripten test page in a real headless browser and reports its result.
//
// Two subsystems use this, and the name is historical rather than a scope:
//
//   * plans/plan_pixijs.md PIXIJS-85 -- the PixiJS renderer is Emscripten-only and its draw path is
//     EM_JS, so its pixel behaviour can only be verified in a browser with a real WebGL context.
//     `node` alone has no DOM, and SDL_Init(SDL_INIT_VIDEO) fails there before any renderer code
//     runs (the same boundary docs/canvas-backend.md records for CANVAS). Every browser result
//     quoted in plans/plan_pixijs.md comes from this script.
//   * plans/plan_cabi.md CABI-37/CABI-39 -- the C ABI's own browser probe
//     (modules/c-api/wasm/browser_probe.html), registered as the ctest CApi_WasmBrowserProbe. It
//     reuses this runner rather than adding a second one, so both subsystems agree on how a page
//     is served, what counts as a pass, and what "Playwright is available" means.
//
// Either way the run is reproducible rather than a one-off hand-driven session. A page reports by
// printing `[FAIL] ...` lines and a final `=== <passed>/<expected> PASS ===`; a run that dies early
// never prints the summary at all, which is what makes its absence a failure.
//
// Usage:
//   node scripts/run_pixijs_browser_tests.mjs <build-dir> [page.html ...]
//   node scripts/run_pixijs_browser_tests.mjs --check-playwright
//
// `--check-playwright` runs no page: it exits 0 when Playwright resolves here and 1 when it does
// not, so a build system can gate a browser test on the same resolution the run itself uses
// instead of on a search of its own that may be narrower.
//
// Requires the `playwright` npm package and a Chromium build; point PLAYWRIGHT_CHROMIUM at an
// executable to override Playwright's own bundled one, and NODE_PATH at the module root holding
// Playwright when it is a global install. Exit code 0 = every page passed.

import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';
import { createRequire } from 'node:module';

const MIME = {
    '.html': 'text/html',
    '.js': 'text/javascript',
    '.mjs': 'text/javascript',
    '.wasm': 'application/wasm',
    '.data': 'application/octet-stream',
};

function loadPlaywright() {
    const require = createRequire(import.meta.url);
    for (const base of [process.env.NODE_PATH, '/opt/node22/lib/node_modules', '/usr/lib/node_modules']) {
        if (!base) continue;
        try {
            return require(join(base, 'playwright'));
        } catch {
            /* try the next candidate */
        }
    }
    return require('playwright');
}

async function serve(root) {
    const server = createServer(async (request, response) => {
        const path = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
        const file = join(root, path === '/' ? '/index.html' : path);
        try {
            const body = await readFile(file);
            response.writeHead(200, {
                'Content-Type': MIME[extname(file)] ?? 'application/octet-stream',
                // SharedArrayBuffer is not used here, but the headers keep a pthread-enabled build
                // loadable through the same server without a second mode.
                'Cross-Origin-Opener-Policy': 'same-origin',
                'Cross-Origin-Embedder-Policy': 'require-corp',
            });
            response.end(body);
        } catch {
            response.writeHead(404);
            response.end('not found');
        }
    });
    await new Promise((done) => server.listen(0, '127.0.0.1', done));
    return server;
}

async function runPage(browser, origin, page_name, timeoutMs) {
    const page = await browser.newPage();
    const lines = [];
    page.on('console', (message) => lines.push(message.text()));
    page.on('pageerror', (error) => lines.push(`[pageerror] ${error.message}`));

    // The page's own printf summary is the authority, not an exit code: Emscripten's HTML shell
    // installs its own Module object, so a Module.onExit hook injected from here is overwritten
    // before main() ever runs. `=== <passed>/<expected> PASS ===` is printed by every CNA smoke
    // test in this family and is unambiguous -- a run that dies early never prints it at all.
    const SUMMARY = /===\s*(\d+)\/(\d+)\s+PASS\s*===/;
    await page.goto(`${origin}/${page_name}`, { waitUntil: 'domcontentloaded' });
    const deadline = Date.now() + timeoutMs;
    let summary = null;
    while (summary === null && Date.now() < deadline) {
        await page.waitForTimeout(100);
        for (const line of lines) {
            const match = SUMMARY.exec(line);
            if (match) { summary = match; break; }
        }
    }
    await page.close();

    const failed = lines.filter((line) =>
        line.startsWith('[FAIL]') || line.startsWith('[pageerror]'));
    const passed = summary ? Number(summary[1]) : 0;
    const expected = summary ? Number(summary[2]) : 0;
    const ok = summary !== null && passed === expected && failed.length === 0;
    return { ok, passed, expected, lines };
}

async function main() {
    const [buildDir, ...requested] = process.argv.slice(2);

    // Lets a build system ask "can this machine run these pages?" using the *same* resolution the
    // run itself uses. A gate with its own separate search can be narrower than the runner and
    // report a skip where the test would have passed, which is exactly what happened to
    // CApi_WasmBrowserProbe (plans/plan_cabi.md CABI-39).
    if (buildDir === '--check-playwright') {
        try {
            loadPlaywright();
            return 0;
        } catch {
            return 1;
        }
    }

    if (!buildDir) {
        console.error('usage: run_pixijs_browser_tests.mjs <build-dir> [page.html ...]');
        console.error('       run_pixijs_browser_tests.mjs --check-playwright');
        return 2;
    }
    const root = resolve(buildDir);
    const pages = requested.length > 0 ? requested : ['cna_test_pixijs_smoke.html'];
    for (const page_name of pages) {
        const file_name = page_name.split('?', 1)[0];
        if (!existsSync(join(root, file_name))) {
            console.error(`missing built page: ${join(root, file_name)}`);
            return 2;
        }
    }

    const { chromium } = loadPlaywright();
    const server = await serve(root);
    const origin = `http://127.0.0.1:${server.address().port}`;
    const browser = await chromium.launch({
        executablePath: process.env.PLAYWRIGHT_CHROMIUM || undefined,
        args: ['--use-gl=swiftshader', '--enable-unsafe-swiftshader', '--no-sandbox'],
    });

    let failures = 0;
    try {
        for (const page_name of pages) {
            const { ok, passed, expected, lines } = await runPage(browser, origin, page_name, 180_000);
            console.log(`--- ${page_name} ---`);
            for (const line of lines) console.log(line);
            console.log(`--- ${page_name}: ${ok ? 'PASS' : 'FAIL'} (${passed}/${expected})`);
            if (!ok) failures += 1;
        }
    } finally {
        await browser.close();
        server.close();
    }
    return failures === 0 ? 0 : 1;
}

process.exit(await main());
