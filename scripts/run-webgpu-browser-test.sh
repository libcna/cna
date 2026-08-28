#!/usr/bin/env bash
# plans/plan_webgpu.md WEBGPU-122: run the Emscripten WEBGPU build of cna_demo_2d in a real browser.
#
# A wasm module loaded under `node` proves nothing for this renderer: its surface is a browser
# <canvas> reached through navigator.gpu, which only exists in a browser with a WebGPU-capable GPU
# process. This drives the generated page through headless Chrome instead, passing the demo's own
# `--webgpu-2d-validation --smoke N` arguments, and treats a clean wasm exit with no WebGPU error as
# the pass.
#
# Usage: scripts/run-webgpu-browser-test.sh [build-dir] [frames]
#        build-dir defaults to cmake-build-wasm-webgpu; frames defaults to 120.
#
# Requires: a completed WEBGPU Emscripten build of cna_demo_2d (cna_demo_2d.{html,js,wasm,data}),
# python3, and a WebGPU-capable google-chrome. Exit 0 = the demo initialised WebGPU, ran the
# validation scene for `frames` frames, and exited cleanly with no uncaptured WebGPU error.
#
# WebGPU in headless Chrome here needs the real GPU's Vulkan path (SwiftShader has no WebGPU adapter
# in this environment); override the flags with CNA_WEBGPU_CHROME_FLAGS if a different machine needs
# a different combination.
set -euo pipefail

BUILD_DIR="${1:-cmake-build-wasm-webgpu}"
FRAMES="${2:-120}"
# The demo target to drive. cna_demo_2d exercises the SpriteBatch path; cna_house3d_demo exercises
# the 3D BasicEffect path (WEBGPU-121). Both accept `--smoke N` and tear the renderer down on exit.
NAME="${CNA_WEBGPU_DEMO:-cna_demo_2d}"
CHROME="${CNA_WEBGPU_CHROME:-google-chrome}"
PORT="${CNA_WEBGPU_TEST_PORT:-8732}"
DEFAULT_FLAGS="--enable-unsafe-webgpu --enable-features=Vulkan,WebGPU --use-angle=vulkan"
CHROME_FLAGS="${CNA_WEBGPU_CHROME_FLAGS:-$DEFAULT_FLAGS}"

if [[ ! -f "${BUILD_DIR}/${NAME}.js" || ! -f "${BUILD_DIR}/${NAME}.wasm" ]]; then
    echo "error: ${BUILD_DIR}/${NAME}.{js,wasm} not found -- build it first:" >&2
    echo "  emcmake cmake -S . -B ${BUILD_DIR} -DCNA_GRAPHICS_RENDERER=WEBGPU -DCNA_BUILD_EXAMPLES=ON ..." >&2
    echo "  cmake --build ${BUILD_DIR} --target ${NAME} -j\"\$(nproc)\"" >&2
    exit 2
fi

# The steady-state 2D render path is the default smoke. Set CNA_WEBGPU_VALIDATION_SCENE=1 to run the
# deterministic --webgpu-2d-validation scene instead; that scene also resizes the canvas mid-run,
# which is a separate, still-open Emscripten code path (see plans/plan_webgpu.md WEBGPU-120).
if [[ "${CNA_WEBGPU_VALIDATION_SCENE:-0}" == "1" ]]; then
    DEMO_ARGS="'--webgpu-2d-validation', '--smoke', '${FRAMES}'"
else
    DEMO_ARGS="'--smoke', '${FRAMES}'"
fi

# A minimal driver page: presets Module.arguments (the emcc default shell parses none), pipes the
# wasm's stdout/stderr to the console with tags this script greps for, and reports the exit code.
cat > "${BUILD_DIR}/webgpu-smoke.html" <<HTML
<!doctype html><html><head><meta charset="utf-8"></head><body>
<canvas id="canvas" width="800" height="480"></canvas>
<script>
  var Module = {
    canvas: document.getElementById('canvas'),
    arguments: [${DEMO_ARGS}],
    print:    function (t) { console.log('CNAOUT ' + t); },
    printErr: function (t) { console.log('CNAERR ' + t); },
    onAbort:  function (w) { console.log('CNAABORT ' + w); },
    onExit:   function (c) { console.log('CNAEXIT ' + c); },
  };
</script>
<script src="${NAME}.js"></script>
</body></html>
HTML

python3 -m http.server "${PORT}" --directory "${BUILD_DIR}" --bind 127.0.0.1 >/dev/null 2>&1 &
SERVER_PID=$!
PROFILE="$(mktemp -d)"
cleanup() { kill "${SERVER_PID}" 2>/dev/null || true; rm -rf "${PROFILE}"; }
trap cleanup EXIT

for _ in $(seq 1 50); do
    curl -s -o /dev/null "http://127.0.0.1:${PORT}/webgpu-smoke.html" && break
    sleep 0.1
done

LOG="$(mktemp)"
# Real wall-clock, NOT --virtual-time-budget: virtual time races ahead of the real GPU-process
# promises and swallows a late abort (the wgpuSurfacePresent abort was invisible under it). The demo
# renders `frames` frames in real time then calls exit(); this timeout just reaps the still-open tab.
RUN_SECONDS="${CNA_WEBGPU_RUN_SECONDS:-30}"
timeout "${RUN_SECONDS}" "${CHROME}" --headless=new --no-sandbox --disable-gpu-sandbox \
    ${CHROME_FLAGS} \
    --user-data-dir="${PROFILE}" \
    --enable-logging=stderr --v=1 \
    "http://127.0.0.1:${PORT}/webgpu-smoke.html" >"${LOG}" 2>&1 || true

echo "----- browser console (CNA lines) -----"
grep -aE 'CONSOLE.*CNA(OUT|ERR|ABORT|EXIT)' "${LOG}" | sed -E 's/^.*CONSOLE[^"]*"//; s/", source.*$//' || true
echo "---------------------------------------"

# A clean run reaches renderer teardown: the smoke loop ends, Run() returns, and ~WebGPURenderer()
# releases the device, which fires the device-lost callback with reason "Device was destroyed" (the
# only reason we cause ourselves). Emscripten does not call Module.onExit when main() merely returns
# without EXIT_RUNTIME, so that teardown marker -- not CNAEXIT -- is the reliable completion signal.
STATUS=1
HAD_ERROR=0
grep -aqiE 'CNAABORT|Aborted|uncaptured error|validation error|fatal (exception|error)|CNAEXIT [1-9]' "${LOG}" && HAD_ERROR=1
if grep -aqE 'Device was destroyed|CNAEXIT 0' "${LOG}"; then
    if [[ "${HAD_ERROR}" -eq 1 ]]; then
        echo "WEBGPU BROWSER SMOKE: FAIL (ran to teardown but a WebGPU error/abort was reported)"
    else
        echo "WEBGPU BROWSER SMOKE: PASS (${FRAMES} frames, clean teardown, no WebGPU error)"
        STATUS=0
    fi
elif [[ "${HAD_ERROR}" -eq 1 ]]; then
    echo "WEBGPU BROWSER SMOKE: FAIL (wasm aborted or reported a WebGPU error)"
else
    echo "WEBGPU BROWSER SMOKE: FAIL (no clean teardown observed within the time budget)"
fi

rm -f "${LOG}"
exit "${STATUS}"
