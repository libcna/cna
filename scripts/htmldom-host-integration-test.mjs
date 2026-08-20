// plans/plan_html_dom.md HTMLDOM-115: Playwright driver exercising the two host-page-integration
// behaviours that can ONLY be observed by manipulating the DOM BEFORE the Emscripten module ever
// loads -- CNA_HtmlDom_EnsureRoot's own "capture the canvas's pre-existing visibility" and "detect
// an existing conflicting #cna-dom-root" logic both run once, synchronously, inside the very first
// renderer construction, which happens extremely early (as part of the module's own bootstrap
// script executing, before this harness's own JS could ever run first via a normal page-load
// hook). `page.route()` HTML-response interception is the one thing that reliably wins that race:
// it rewrites the page's OWN HTML source text before the browser ever parses it, so the injected
// markup is part of the document from its very first parsed byte -- not dependent on any
// CDP-level "runs before other scripts" timing subtlety, which measurably was NOT early enough
// (confirmed by comparing both approaches directly while writing this harness).
//
// Reuses cna_test_htmldom_smoke.html as the page under test -- no new C++ binary needed, since both
// behaviours are exercised by the EXISTING constructor flow every htmldom page already goes
// through; only the harness driving it is new.
//
// Usage: htmldom-host-integration-test.mjs <smoke-page-url>
// Exit code 0 = both scenarios behaved correctly.
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const url = process.argv[2];
if (!url) {
    console.error('usage: htmldom-host-integration-test.mjs <smoke-page-url>');
    process.exit(2);
}

const TIMEOUT_MS = Number(process.env.CNA_HTMLDOM_TEST_TIMEOUT_MS ?? 60000);

async function runVisibilityPreservationCheck(browser) {
    const page = await browser.newPage();
    try {
        // Sets the STATIC canvas element's own inline style directly in the HTML source -- present
        // from the very first parsed byte, so CNA_HtmlDom_EnsureRoot's capture (which runs during
        // the module's own synchronous bootstrap) unambiguously observes THIS value, not the CSS
        // default. A distinctive, non-default value unlikely to coincide with anything else.
        await page.route('**/*.html', async (route) => {
            const response = await route.fetch();
            let body = await response.text();
            body = body.replace('id="canvas"', 'id="canvas" style="visibility:collapse"');
            await route.fulfill({ response, body });
        });
        await page.goto(url, { waitUntil: 'domcontentloaded' });
        await page.waitForFunction(
            () => typeof Module !== 'undefined' && 'cnaDomOriginalCanvasVisibility' in Module,
            null, { timeout: TIMEOUT_MS });

        const captured = await page.evaluate(() => Module['cnaDomOriginalCanvasVisibility']);
        const ok = captured === 'collapse';
        console.log(`       HTMLDOM-115 visibility capture: expected "collapse", got ` +
                    `${JSON.stringify(captured)}`);
        console.log(`[${ok ? 'PASS' : 'FAIL'}] HTMLDOM-115: CNA_HtmlDom_EnsureRoot captures the ` +
                    `canvas's REAL pre-existing visibility value (here, a host page that had ` +
                    `already set a distinctive non-default one) rather than assuming it was unset`);
        return ok;
    } finally {
        await page.close();
    }
}

async function runIdCollisionCheck(browser) {
    const page = await browser.newPage();
    const consoleLines = [];
    page.on('console', (msg) => consoleLines.push(msg.text()));
    try {
        // Injects a conflicting, host-page-owned #cna-dom-root as literally the first child of
        // <body>, present before the module's own bootstrap <script> (later in <body>, or in
        // <head>) ever runs -- see this file's own top-of-file comment for why route()-based
        // source rewriting, not addInitScript, is what makes this reliable.
        await page.route('**/*.html', async (route) => {
            const response = await route.fetch();
            let body = await response.text();
            const conflictMarkup =
                '<div id="cna-dom-root" data-host-page-owned="true"></div>';
            body = body.replace('<body>', '<body>' + conflictMarkup);
            await route.fulfill({ response, body });
        });
        await page.goto(url, { waitUntil: 'domcontentloaded' });
        // No real #cna-dom-viewport can ever appear here (the renderer refuses to proceed), so
        // waiting for the surface would hang -- give the module a fixed window to attempt (and
        // fail) its own init instead.
        await page.waitForTimeout(2000);

        const state = await page.evaluate(() => ({
            conflictStillOwnedByHost: document.getElementById('cna-dom-root')
                ?.getAttribute('data-host-page-owned') === 'true',
            viewportCreated: !!document.getElementById('cna-dom-viewport'),
            moduleRoot: typeof Module !== 'undefined' ? (Module['cnaDomRoot'] || null) : null,
        }));
        const sawRefusal = consoleLines.some((l) => l.includes('refusing to proceed'));
        const ok = state.conflictStillOwnedByHost && !state.viewportCreated &&
                   state.moduleRoot === null && sawRefusal;
        console.log(`       HTMLDOM-115 ID collision: conflictStillOwnedByHost=` +
                    `${state.conflictStillOwnedByHost} viewportCreated=${state.viewportCreated} ` +
                    `moduleRoot=${JSON.stringify(state.moduleRoot)} sawRefusalMessage=${sawRefusal}`);
        console.log(`[${ok ? 'PASS' : 'FAIL'}] HTMLDOM-115: a pre-existing, host-page-owned element ` +
                    `with id "cna-dom-root" is never silently adopted or replaced -- the renderer ` +
                    `logs a clear refusal and does not create its own DOM surface at all, rather ` +
                    `than corrupting or conflating itself with the host's own unrelated element`);
        return ok;
    } finally {
        await page.close();
    }
}

const browser = await chromium.launch({ headless: true });
let exitCode = 1;
try {
    const results = await Promise.all([
        runVisibilityPreservationCheck(browser),
        runIdCollisionCheck(browser),
    ]);
    const passed = results.filter(Boolean).length;
    console.log(`\nhost-integration verdict: ${passed}/${results.length} checks passed`);
    exitCode = results.every(Boolean) ? 0 : 1;
} catch (err) {
    console.error(`\nthe host-integration test did not finish: ${err.message}`);
    exitCode = 1;
} finally {
    await browser.close();
}

process.exit(exitCode);
