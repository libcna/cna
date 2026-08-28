#!/usr/bin/env bash
# plans/plan_webgpu.md WEBGPU-123: cross-backend pixel parity for the WEBGPU renderer.
#
# Renders the shared, renderer-agnostic cross_renderer_diagnostic_scene (one unlit vertex-colour
# triangle -> 64x64 RGBA8) with the Emscripten WEBGPU build in a real browser, extracts the dump out
# of MEMFS, and diffs it against another backend's dump of the SAME scene with cna_diag_compare. The
# reference dump is produced the ordinary way -- e.g. a native SOFTWARE build's cna_diag_software,
# which is the independently-implemented CPU rasterizer and needs no GPU or display:
#
#   cmake -S . -B cmake-build-debug -G Ninja -DCNA_GRAPHICS_RENDERER=SOFTWARE -DCNA_PLATFORM=HEADLESS \
#         -DCNA_BUILD_TESTS=ON -DCNA_BUILD_EXAMPLES=ON ...
#   cmake --build cmake-build-debug --target cna_diag_software cna_diag_compare
#   cmake-build-debug/cna_diag_software software_dump.rgba
#
# Usage: scripts/run-webgpu-parity-test.sh <reference-dump> [wasm-build-dir] [compare-tool] [tolerance]
#        wasm-build-dir defaults to cmake-build-wasm-webgpu; compare-tool to
#        cmake-build-debug/cna_diag_compare; tolerance to 40 (cna_diag_compare's own default).
#
# Requires: an Emscripten WEBGPU build of cna_diag_webgpu, python3, a WebGPU-capable google-chrome,
# and cna_diag_compare. Exit 0 = the WebGPU dump matches the reference within tolerance.
set -euo pipefail

REFERENCE="${1:?usage: run-webgpu-parity-test.sh <reference-dump> [wasm-build-dir] [compare-tool] [tolerance]}"
BUILD_DIR="${2:-cmake-build-wasm-webgpu}"
COMPARE="${3:-cmake-build-debug/cna_diag_compare}"
TOLERANCE="${4:-40}"
CHROME="${CNA_WEBGPU_CHROME:-google-chrome}"
PORT="${CNA_WEBGPU_TEST_PORT:-8733}"
FLAGS="${CNA_WEBGPU_CHROME_FLAGS:---enable-unsafe-webgpu --enable-features=Vulkan,WebGPU --use-angle=vulkan}"

for f in "${REFERENCE}" "${BUILD_DIR}/cna_diag_webgpu.js" "${COMPARE}"; do
    [[ -e "$f" ]] || { echo "error: missing '$f'" >&2; exit 2; }
done

# Driver page: run the scene (writes /dump.rgba into MEMFS), then poll FS and print the bytes as
# base64. cna_diag_webgpu is linked with -sEXPORTED_RUNTIME_METHODS=FS so Module.FS is reachable
# here; emscripten does not call onExit on a normal main() return, hence the poll rather than a hook.
cat > "${BUILD_DIR}/webgpu-diag.html" <<'HTML'
<!doctype html><html><head><meta charset="utf-8"></head><body>
<canvas id="canvas" width="64" height="64"></canvas>
<script>
  var Module = {
    canvas: document.getElementById('canvas'),
    arguments: ['/dump.rgba'],
    print:    function (t) { console.log('CNAOUT ' + t); },
    printErr: function (t) { console.log('CNAERR ' + t); },
  };
  function tryDump(n) {
    try {
      if (Module.FS && Module.FS.analyzePath('/dump.rgba').exists) {
        var data = Module.FS.readFile('/dump.rgba');
        var bin = '';
        for (var i = 0; i < data.length; i++) bin += String.fromCharCode(data[i]);
        console.log('CNADUMP ' + data.length + ' ' + btoa(bin));
        return;
      }
    } catch (e) { console.log('CNADUMPERR ' + e); return; }
    if (n > 0) setTimeout(function () { tryDump(n - 1); }, 250);
    else console.log('CNADUMPERR timeout');
  }
  setTimeout(function () { tryDump(40); }, 1000);
</script>
<script src="cna_diag_webgpu.js"></script>
</body></html>
HTML

python3 -m http.server "${PORT}" --directory "${BUILD_DIR}" --bind 127.0.0.1 >/dev/null 2>&1 &
SERVER_PID=$!
PROFILE="$(mktemp -d)"
LOG="$(mktemp)"
WEBGPU_DUMP="$(mktemp --suffix=.rgba)"
cleanup() { kill "${SERVER_PID}" 2>/dev/null || true; rm -rf "${PROFILE}" "${LOG}" "${WEBGPU_DUMP}" "${BUILD_DIR}/webgpu-diag.html"; }
trap cleanup EXIT

for _ in $(seq 1 50); do
    curl -s -o /dev/null "http://127.0.0.1:${PORT}/webgpu-diag.html" && break
    sleep 0.1
done

timeout 40 "${CHROME}" --headless=new --no-sandbox --disable-gpu-sandbox \
    ${FLAGS} --user-data-dir="${PROFILE}" --enable-logging=stderr --v=1 \
    "http://127.0.0.1:${PORT}/webgpu-diag.html" >"${LOG}" 2>&1 || true

if ! grep -aqE 'CNADUMP [0-9]+ ' "${LOG}"; then
    echo "WEBGPU PARITY: FAIL (no dump extracted from the browser)"
    grep -aoE 'CNAOUT [^"]*|CNADUMPERR [^"]*' "${LOG}" | head
    exit 1
fi
grep -aoE 'CNADUMP [0-9]+ [A-Za-z0-9+/=]+' "${LOG}" | head -1 | awk '{print $3}' | base64 -d > "${WEBGPU_DUMP}"
echo "extracted WebGPU dump: $(stat -c%s "${WEBGPU_DUMP}") bytes"

echo "----- cna_diag_compare (reference vs WEBGPU) -----"
if "${COMPARE}" "${REFERENCE}" "${WEBGPU_DUMP}" "${TOLERANCE}"; then
    echo "WEBGPU PARITY: PASS (matches ${REFERENCE##*/} within tolerance ${TOLERANCE})"
    exit 0
else
    echo "WEBGPU PARITY: FAIL (exceeds tolerance ${TOLERANCE} vs ${REFERENCE##*/})"
    exit 1
fi
