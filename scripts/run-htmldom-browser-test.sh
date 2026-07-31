#!/usr/bin/env bash
# plan_html_dom.md HTMLDOM-72: runs the HTML_DOM backend's smoke test in a real browser.
#
# The HTML_DOM backend renders through actual DOM elements and CSS, so a wasm module loaded under
# `node` proves nothing at all -- SDL_Init(SDL_INIT_VIDEO) throws there before any backend code
# runs. This drives the generated page through headless Chromium instead, so the checks the test
# makes against the produced DOM are real.
#
# Usage: scripts/run-htmldom-browser-test.sh [build-dir]
#        build-dir defaults to cmake-build-htmldom.
#
# Requires: a completed HTML_DOM build (cna_test_htmldom_smoke.html + .js + .wasm), node, and the
# playwright package with a Chromium binary. Exit code 0 = every check passed.
set -euo pipefail

BUILD_DIR="${1:-cmake-build-htmldom}"
PAGE="cna_test_htmldom_smoke.html"

if [[ ! -f "${BUILD_DIR}/${PAGE}" ]]; then
    echo "error: ${BUILD_DIR}/${PAGE} not found -- build the HTML_DOM smoke test first:" >&2
    echo "  emcmake cmake -S . -B ${BUILD_DIR} -DCNA_GRAPHICS_BACKEND=HTML_DOM" >&2
    echo "  cmake --build ${BUILD_DIR} --target cna_test_htmldom_smoke -j4" >&2
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

NODE_PATH="$(npm root -g)" node "$(dirname "$0")/htmldom-browser-test.mjs" \
    "http://127.0.0.1:${PORT}/${PAGE}"
