import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const [rendererRoot, baseUrl, asset, cameraJson, output, metadata] = process.argv.slice(2);
const playwrightUrl = pathToFileURL(
    path.join(rendererRoot, "node_modules", "playwright", "index.mjs")
);
const { chromium } = await import(playwrightUrl.href);
const chrome = process.env.CNA_GLTF_CHROME || "/usr/bin/google-chrome";
const browser = await chromium.launch({
    executablePath: chrome,
    headless: true,
    args: ["--use-gl=angle", "--use-angle=swiftshader", "--enable-webgl"]
});

try {
    const page = await browser.newPage({
        viewport: { width: 512, height: 512 },
        deviceScaleFactor: 1
    });
    const browserErrors = [];
    page.on("console", message => {
        if (message.type() === "error") browserErrors.push(message.text());
    });
    page.on("pageerror", error => browserErrors.push(error.stack || error.message));

    const url = `${baseUrl}/reference_harness.html?asset=${encodeURIComponent(asset)}` +
        `&camera=${encodeURIComponent(cameraJson)}`;
    await page.goto(url, { waitUntil: "networkidle" });
    await page.waitForFunction(
        () => globalThis.cnaReferenceResult?.ready === true,
        undefined,
        { timeout: 30000 }
    );
    const result = await page.evaluate(() => globalThis.cnaReferenceResult);
    if (browserErrors.length !== 0) {
        throw new Error(`browser errors: ${browserErrors.join(" | ")}`);
    }
    await page.locator("#canvas").screenshot({ path: output, omitBackground: true });
    fs.writeFileSync(metadata, JSON.stringify(result, null, 2) + "\n");
} finally {
    await browser.close();
}
