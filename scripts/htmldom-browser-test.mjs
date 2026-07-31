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

    console.log(`\nbrowser verdict: ${result.passed}/${result.expected} checks passed`);
    exitCode = result.code === 0 && !sawFailLine ? 0 : 1;
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
