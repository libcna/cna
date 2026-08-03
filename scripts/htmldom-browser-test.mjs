// plan_html_dom.md HTMLDOM-72: Playwright driver for the HTML_DOM backend's smoke test.
//
// Loads the Emscripten-generated page in headless Chromium, mirrors everything the wasm module
// prints, and waits for the test to publish its verdict on window.__cnaSmokeResult. Exits 0 only
// when every check passed -- the process exit code of a wasm module in a browser is not observable,
// which is why the test publishes the result explicitly.
import { createRequire } from 'node:module';

// ESM resolution ignores NODE_PATH, and playwright is commonly installed globally rather than in
// this repo, so it is resolved through CommonJS require -- which does honour NODE_PATH (set by
// run-htmldom-browser-test.sh) -- instead of a bare import.
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const url = process.argv[2];
if (!url) {
    console.error('usage: htmldom-browser-test.mjs <url>');
    process.exit(2);
}

const TIMEOUT_MS = Number(process.env.CNA_HTMLDOM_TEST_TIMEOUT_MS ?? 60000);

// plan_html_dom.md HTMLDOM-101: the smoke page's own EM_JS checks can only ever read DOM STATE
// (clip-path style strings, element counts) -- they cannot prove anything about actual COMPOSITED
// pixels, because design decision 11 means no in-page JS API can rasterize a live DOM subtree. A
// real screenshot, taken from OUT HERE by Playwright (which drives the browser's own compositor via
// CDP, not a page-level API), can. htmldom_smoke_test.cpp's very last draw (frame 10, right before
// publishing its result) leaves exactly this scenario live in the DOM: a sprite whose destination
// footprint straddles a scissor rect's own edge, so part of it must be visibly painted and part must
// not be -- proving the HTMLDOM-101 fix (the scissor region container actually has a real, non-zero
// box) beyond what any DOM-state-only check could show.
async function verifySmokeScreenshotPixels(page) {
    const pngBase64 = (await page.screenshot()).toString('base64');
    const pixels = await page.evaluate((b64) => new Promise((resolve, reject) => {
        const img = new Image();
        img.onload = () => {
            const canvas = document.createElement('canvas');
            canvas.width = img.naturalWidth;
            canvas.height = img.naturalHeight;
            const ctx = canvas.getContext('2d');
            ctx.drawImage(img, 0, 0);
            const root = document.getElementById('cna-dom-root');
            const rect = root ? root.getBoundingClientRect() : { left: 0, top: 0 };
            const sample = (logicalX, logicalY) => {
                const px = Math.round(rect.left + logicalX);
                const py = Math.round(rect.top + logicalY);
                const d = ctx.getImageData(px, py, 1, 1).data;
                return [d[0], d[1], d[2], d[3]];
            };
            // Rect is (60,60,20,20) -- logical x/y in [60,80); sprite footprint is (55,55,40,40) --
            // logical x/y in [55,95). (70,70) is inside both, 10px from every edge. (90,90) is inside
            // the sprite's own unclipped footprint but 10px outside the clip rect's far edge.
            resolve({ inside: sample(70, 70), outside: sample(90, 90) });
        };
        img.onerror = () => reject(new Error('failed to decode the screenshot inside the page'));
        img.src = 'data:image/png;base64,' + b64;
    }), pngBase64);

    const near = (a, b, tol) => Math.abs(a - b) <= tol;
    // Source texel (0,0) is pure red (LoadContent's own texture layout), drawn with Color::White (no
    // tint), so the sprite's unclipped footprint is solid (255,0,0). Outside the clip, nothing else
    // draws at (90,90): the surface's own Clear(CornflowerBlue) colour (100,149,237) should show.
    const insideOk = near(pixels.inside[0], 255, 12) && near(pixels.inside[1], 0, 12) &&
                      near(pixels.inside[2], 0, 12);
    const outsideOk = near(pixels.outside[0], 100, 12) && near(pixels.outside[1], 149, 12) &&
                       near(pixels.outside[2], 237, 12);

    console.log(`       HTMLDOM-101 screenshot pixel inside sprite+scissor rect at logical (70,70): ` +
                `rgba(${pixels.inside.join(',')})`);
    console.log(`[${insideOk ? 'PASS' : 'FAIL'}] HTMLDOM-101: a real screenshot pixel inside both the ` +
                `sprite's footprint and its scissor rect shows the sprite's own colour`);
    console.log(`       HTMLDOM-101 screenshot pixel outside the scissor rect (inside the sprite's ` +
                `unclipped footprint) at logical (90,90): rgba(${pixels.outside.join(',')})`);
    console.log(`[${outsideOk ? 'PASS' : 'FAIL'}] HTMLDOM-101: a real screenshot pixel inside the ` +
                `sprite's unclipped footprint but outside its scissor rect shows the surface's own ` +
                `clear colour instead -- clip-path genuinely removed it from the composited page, ` +
                `not merely from an inline style string`);
    return insideOk && outsideOk;
}

const browser = await chromium.launch({ headless: true });
const page = await browser.newPage();

let sawFailLine = false;
page.on('console', (msg) => {
    const text = msg.text();
    if (text.includes('[FAIL]')) sawFailLine = true;
    console.log(text);
});
page.on('pageerror', (err) => {
    sawFailLine = true;
    console.log(`[browser error] ${err.message}`);
});

let exitCode = 1;
try {
    await page.goto(url, { waitUntil: 'domcontentloaded' });
    await page.waitForFunction('window.__cnaSmokeDone === true', null, { timeout: TIMEOUT_MS });

    const result = await page.evaluate(() => ({
        code: window.__cnaSmokeResult,
        passed: window.__cnaSmokePassed,
        expected: window.__cnaSmokeExpected,
    }));

    // Only the smoke page leaves the HTMLDOM-101 straddling-sprite scenario live in its final DOM
    // state; pixel/stress/dispose pages have no such scenario to screenshot.
    let screenshotOk = true;
    if (url.includes('cna_test_htmldom_smoke')) {
        screenshotOk = await verifySmokeScreenshotPixels(page);
    }

    console.log(`\nbrowser verdict: ${result.passed}/${result.expected} checks passed`);
    exitCode = result.code === 0 && !sawFailLine && screenshotOk ? 0 : 1;
} catch (err) {
    console.error(`\nthe smoke test did not finish: ${err.message}`);
    // Dump whatever the page did build, so a hang is diagnosable rather than just a timeout.
    try {
        const surface = await page.evaluate(() => {
            const root = document.getElementById('cna-dom-root');
            return root
                ? { children: root.children.length, style: root.getAttribute('style') }
                : null;
        });
        console.error(`DOM surface at failure: ${JSON.stringify(surface)}`);
    } catch {
        // The page never got far enough to inspect; the original error is the useful one.
    }
    exitCode = 1;
} finally {
    await browser.close();
}

process.exit(exitCode);
