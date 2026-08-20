#!/usr/bin/env bash
# plans/plan_html_dom.md HTMLDOM-72/HTMLDOM-112: runs one of the HTML_DOM renderer's browser test pages in
# a real browser.
#
# The HTML_DOM renderer renders through actual DOM elements and CSS, so a wasm module loaded under
# `node` proves nothing at all -- SDL_Init(SDL_INIT_VIDEO) throws there before any renderer code
# runs. This drives the generated page through headless Chromium instead, so the checks the test
# makes against the produced DOM are real.
#
# Usage: scripts/run-htmldom-browser-test.sh [build-dir] [page]
#        build-dir defaults to cmake-build-htmldom.
#        page is one of: smoke (default), pixel, stress, dispose, host-integration, memory.
#
# To build and run all six pages in one command, use scripts/run-htmldom-test-suite.sh instead.
#
# Requires: a completed HTML_DOM build of the requested page's target, node, and the playwright
# package with a Chromium binary. Exit code 0 = every check passed.
set -euo pipefail

BUILD_DIR="${1:-cmake-build-htmldom}"
PAGE_NAME="${2:-smoke}"

# plans/plan_html_dom.md HTMLDOM-115: host-integration reuses the smoke page's own built .html (no
# separate C++ binary -- both host-page-integration behaviours it exercises are part of the
# EXISTING constructor flow every htmldom page already goes through), but is driven by a different
# harness script (htmldom-host-integration-test.mjs, not htmldom-browser-test.mjs below), since it
# needs to rewrite the page's own HTML response before the browser parses it -- see that script's
# own top-of-file comment for why.
HARNESS="htmldom-browser-test.mjs"
case "${PAGE_NAME}" in
    smoke)            TARGET="cna_test_htmldom_smoke" ;;
    pixel)            TARGET="cna_test_htmldom_pixel_verification" ;;
    stress)           TARGET="cna_test_htmldom_stress" ;;
    dispose)          TARGET="cna_test_htmldom_dispose" ;;
    memory)           TARGET="cna_test_htmldom_memory" ;;
    host-integration) TARGET="cna_test_htmldom_smoke"; HARNESS="htmldom-host-integration-test.mjs" ;;
    *)
        echo "error: unknown page '${PAGE_NAME}' -- expected smoke|pixel|stress|dispose|memory|host-integration" >&2
        exit 2
        ;;
esac
PAGE="${TARGET}.html"

if [[ ! -f "${BUILD_DIR}/${PAGE}" ]]; then
    echo "error: ${BUILD_DIR}/${PAGE} not found -- build it first:" >&2
    echo "  emcmake cmake -S . -B ${BUILD_DIR} -DCNA_GRAPHICS_RENDERER=HTML_DOM" >&2
    echo "  cmake --build ${BUILD_DIR} --target ${TARGET} -j3" >&2
    exit 2
fi

# The wasm module cannot be fetched over file:// (cross-origin), so the build directory is served.
PORT="${CNA_HTMLDOM_TEST_PORT:-8731}"
python3 -m http.server "${PORT}" --directory "${BUILD_DIR}" --bind 127.0.0.1 >/dev/null 2>&1 &
SERVER_PID=$!
trap 'kill "${SERVER_PID}" 2>/dev/null || true' EXIT

for _ in $(seq 1 50); do
    if curl -s -o /dev/null "http://127.0.0.1:${PORT}/${PAGE}"; then break; fi
    sleep 0.1
done

NODE_PATH="$(npm root -g)" node "$(dirname "$0")/${HARNESS}" \
    "http://127.0.0.1:${PORT}/${PAGE}"
