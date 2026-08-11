// Playwright driver for the SVG_DOM renderer's browser test pages (smoke / pixel / scissor-order).
//
// Loads the Emscripten-generated page in headless Chromium, mirrors everything the wasm module
// prints, and waits for the test to publish its verdict on a page-specific window.__cna*Done flag --
// the process exit code of a wasm module in a browser is not observable, which is why each page
// publishes its result explicitly (same reasoning scripts/htmldom-browser-test.mjs documents for its
// own sibling renderer; adapted here for SVG_DOM's own three pages, not copied mechanically).
//
// Beyond each page's own in-wasm checks, this harness independently verifies STRUCTURAL facts from
// outside the module (real SVG namespace elements, the backbuffer canvas staying hidden, no stray
// Canvas2D rasterization of ordinary SpriteBatch draws) and, for scissor-order, takes a REAL
// screenshot and samples actual composited pixels -- the SVGDOM-A draw-order regression can only be
// proven that way, since no in-page API rasterizes a live SVG subtree synchronously.
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const url = process.argv[2];
const pageName = process.argv[3] ?? 'smoke';
if (!url) {
    console.error('usage: svgdom-browser-test.mjs <url> <smoke|pixel|scissor-order>');
    process.exit(2);
}

const TIMEOUT_MS = Number(process.env.CNA_SVGDOM_TEST_TIMEOUT_MS ?? 60000);

const DONE_FLAG = {
    smoke: '__cnaSmokeDone',
    pixel: '__cnaPixelDone',
    'scissor-order': '__cnaScissorOrderDone',
}[pageName];
const RESULT_KEYS = {
    smoke: ['__cnaSmokeResult', '__cnaSmokePassed', '__cnaSmokeExpected'],
    pixel: ['__cnaPixelResult', '__cnaPixelPassed', '__cnaPixelExpected'],
    'scissor-order': ['__cnaScissorOrderResult', '__cnaScissorOrderPassed', '__cnaScissorOrderExpected'],
}[pageName];
if (!DONE_FLAG) {
    console.error(`unknown page '${pageName}'`);
    process.exit(2);
}

// Structural proof (task requirement C): the backbuffer must be genuine SVG DOM, not another
// renderer disguised behind the identity -- a real <svg> root in the SVG namespace, the SDL canvas
// hidden (so only the SVG surface is visible), and (for the smoke/pixel pages, which never bind a
// render target on the path being checked) no Canvas2D 2D context ever created for the ordinary
// backbuffer path, proving normal SpriteBatch draws are not silently rasterized through CANVAS.
async function verifyStructural(page) {
    return page.evaluate(() => {
        const root = document.getElementById('cna-svg-dom-root');
        const isSvgNs = !!root && root.namespaceURI === 'http://www.w3.org/2000/svg' &&
                        root.tagName.toLowerCase() === 'svg';
        const canvas = window.Module && window.Module['canvas'];
        const canvasHidden = !!canvas && canvas.style.visibility === 'hidden';
        // Every sprite-bearing child of root must itself be a real SVG namespace element (<g>), and
        // every nested <image>/<pattern> child likewise -- proves the backbuffer path emits real SVG
        // primitives, not e.g. <div>/<canvas> elements from a different renderer.
        let allChildrenAreSvgNs = true;
        const slots = (window.Module && window.Module['cnaSvgDomFlushSlots']) || [];
        for (const slot of slots) {
            if (!slot || !slot.container) continue;
            if (slot.container.namespaceURI !== 'http://www.w3.org/2000/svg') allChildrenAreSvgNs = false;
            for (const child of slot.container.children) {
                if (child.namespaceURI !== 'http://www.w3.org/2000/svg') allChildrenAreSvgNs = false;
            }
        }
        return { isSvgNs, canvasHidden, allChildrenAreSvgNs, slotCount: slots.length };
    });
}

// SVGDOM-A pixel proof: samples the four decisive points from svgdom_scissor_order_test.cpp's own
// scene directly off a REAL screenshot (the browser's own compositor via CDP, not a page-level API
// that design decision 5/backbuffer-readback-is-impossible would rule out).
async function verifyScissorOrderScreenshot(page) {
    const pngBase64 = (await page.screenshot()).toString('base64');
    const pixels = await page.evaluate((b64) => new Promise((resolve, reject) => {
        const img = new Image();
        img.onload = () => {
            const canvas = document.createElement('canvas');
            canvas.width = img.naturalWidth;
            canvas.height = img.naturalHeight;
            const ctx = canvas.getContext('2d');
            ctx.drawImage(img, 0, 0);
            const root = document.getElementById('cna-svg-dom-root');
            const rect = root ? root.getBoundingClientRect() : { left: 0, top: 0 };
            const sample = (logicalX, logicalY) => {
                const px = Math.round(rect.left + logicalX);
                const py = Math.round(rect.top + logicalY);
                const d = ctx.getImageData(px, py, 1, 1).data;
                return [d[0], d[1], d[2], d[3]];
            };
            resolve({
                rect1: sample(5, 5),     // must be GREEN (batch C painted over batch A's red)
                rect2: sample(27, 27),   // must be GREEN (batch C painted over batch B's blue)
                rect3: sample(5, 27),    // must be YELLOW (batch D, drawn after C, its own later region)
                elsewhere: sample(16, 16), // must be GREEN (only batch C ever touches it)
            });
        };
        img.onerror = () => reject(new Error('failed to decode the screenshot inside the page'));
        img.src = 'data:image/png;base64,' + b64;
    }), pngBase64);

    const near = (a, b, tol) => Math.abs(a - b) <= tol;
    const isColor = (px, r, g, b) => near(px[0], r, 20) && near(px[1], g, 20) && near(px[2], b, 20);

    const rect1Ok = isColor(pixels.rect1, 0, 255, 0);
    const rect2Ok = isColor(pixels.rect2, 0, 255, 0);
    const rect3Ok = isColor(pixels.rect3, 255, 255, 0);
    const elsewhereOk = isColor(pixels.elsewhere, 0, 255, 0);

    console.log(`       SVGDOM-A screenshot rect1(5,5)=rgba(${pixels.rect1.join(',')}) ` +
                `rect2(27,27)=rgba(${pixels.rect2.join(',')}) rect3(5,27)=rgba(${pixels.rect3.join(',')}) ` +
                `elsewhere(16,16)=rgba(${pixels.elsewhere.join(',')})`);
    console.log(`[${rect1Ok ? 'PASS' : 'FAIL'}] SVGDOM-A: rect1 (drawn RED then covered by a later ` +
                `unscissored GREEN batch) shows GREEN -- the pre-fix region-creation-order bug left ` +
                `RED wrongly on top here`);
    console.log(`[${rect2Ok ? 'PASS' : 'FAIL'}] SVGDOM-A: rect2 (drawn BLUE then covered by the same ` +
                `later unscissored GREEN batch) shows GREEN -- same defect, a second distinct region`);
    console.log(`[${rect3Ok ? 'PASS' : 'FAIL'}] SVGDOM-A: rect3 (a NEW region created AFTER the full ` +
                `batch) correctly shows its own later YELLOW draw`);
    console.log(`[${elsewhereOk ? 'PASS' : 'FAIL'}] SVGDOM-A: an untouched point shows the unscissored ` +
                `batch's own GREEN`);
    return rect1Ok && rect2Ok && rect3Ok && elsewhereOk;
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
    await page.waitForFunction((flag) => window[flag] === true, DONE_FLAG, { timeout: TIMEOUT_MS });

    const result = await page.evaluate((keys) => ({
        code: window[keys[0]],
        passed: window[keys[1]],
        expected: window[keys[2]],
    }), RESULT_KEYS);

    const structural = await verifyStructural(page);
    console.log(`       structural: root is real SVG namespace=${structural.isSvgNs}, ` +
                `SDL canvas hidden=${structural.canvasHidden}, ` +
                `all sprite elements are SVG namespace=${structural.allChildrenAreSvgNs}, ` +
                `flush slots=${structural.slotCount}`);
    console.log(`[${structural.isSvgNs ? 'PASS' : 'FAIL'}] structural: #cna-svg-dom-root is a real ` +
                `<svg> element in the SVG namespace`);
    console.log(`[${structural.canvasHidden ? 'PASS' : 'FAIL'}] structural: the SDL <canvas> is hidden ` +
                `-- only the SVG surface is visible`);
    console.log(`[${structural.allChildrenAreSvgNs ? 'PASS' : 'FAIL'}] structural: every sprite-bearing ` +
                `element the renderer created is a real SVG namespace element (genuine SVG DOM, not ` +
                `another renderer disguised behind the identity)`);

    let screenshotOk = true;
    if (pageName === 'scissor-order') {
        screenshotOk = await verifyScissorOrderScreenshot(page);
    }

    console.log(`\nbrowser verdict: ${result.passed}/${result.expected} checks passed`);
    exitCode = result.code === 0 && !sawFailLine && screenshotOk &&
               structural.isSvgNs && structural.canvasHidden && structural.allChildrenAreSvgNs ? 0 : 1;
} catch (err) {
    console.error(`\nthe test did not finish: ${err.message}`);
    try {
        const surface = await page.evaluate(() => {
            const root = document.getElementById('cna-svg-dom-root');
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
