#!/usr/bin/env bash
# Runs one of the SVG_DOM renderer's browser test pages in a real browser.
#
# The SVG_DOM renderer renders through actual SVG namespace DOM elements, so a wasm module loaded
# under `node` proves nothing (SDL_Init(SDL_INIT_VIDEO) throws there before any renderer code runs).
# This drives the generated page through headless Chromium instead, mirroring
# scripts/run-htmldom-browser-test.sh's own shape for the sibling HTML_DOM renderer (adapted here,
# not copied mechanically -- SVG_DOM's own pages/checks are different).
#
# Usage: scripts/run-svgdom-browser-test.sh [build-dir] [page]
#        build-dir defaults to cmake-build-svgdom.
#        page is one of: smoke (default), pixel, scissor-order.
#
# Requires: a completed SVG_DOM build of the requested page's target, node, and the playwright
# package with a Chromium binary. Exit code 0 = every check passed.
set -euo pipefail

BUILD_DIR="${1:-cmake-build-svgdom}"
PAGE_NAME="${2:-smoke}"

case "${PAGE_NAME}" in
    smoke)          TARGET="cna_test_svgdom_smoke" ;;
    pixel)          TARGET="cna_test_svgdom_pixel_verification" ;;
    scissor-order)  TARGET="cna_test_svgdom_scissor_order" ;;
    *)
        echo "error: unknown page '${PAGE_NAME}' -- expected smoke|pixel|scissor-order" >&2
        exit 2
        ;;
esac
PAGE="${TARGET}.html"
# module-examples campaign: this renderer's examples build inside its own owning module directory,
# not at the CMake build-dir root.
PAGE_DIR="${BUILD_DIR}/modules/renderers/svg-dom/examples"

if [[ ! -f "${PAGE_DIR}/${PAGE}" ]]; then
    echo "error: ${PAGE_DIR}/${PAGE} not found -- build it first:" >&2
    echo "  emcmake cmake -S . -B ${BUILD_DIR} -DCNA_GRAPHICS_RENDERER=SVG_DOM" >&2
    echo "  cmake --build ${BUILD_DIR} --target ${TARGET} -j4" >&2
    exit 2
fi

# The wasm module cannot be fetched over file:// (cross-origin), so the build directory is served.
PORT="${CNA_SVGDOM_TEST_PORT:-8741}"
python3 -m http.server "${PORT}" --directory "${PAGE_DIR}" --bind 127.0.0.1 >/dev/null 2>&1 &
SERVER_PID=$!
trap 'kill "${SERVER_PID}" 2>/dev/null || true' EXIT

for _ in $(seq 1 50); do
    if curl -s -o /dev/null "http://127.0.0.1:${PORT}/${PAGE}"; then break; fi
    sleep 0.1
done

NODE_PATH="$(npm root -g)" node "$(dirname "$0")/svgdom-browser-test.mjs" \
    "http://127.0.0.1:${PORT}/${PAGE}" "${PAGE_NAME}"
