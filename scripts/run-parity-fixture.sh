#!/usr/bin/env bash
# plans/plan_webgpu.md WEBGPU-207: run ONE shared parity fixture under BOTH renderers and diff the
# two frames.
#
# The fixture's own programmatic assertions (see modules/graphics/examples/parity/ParityFixture.hpp)
# are the primary oracle and run as ordinary CTests in each build -- `ctest -R Parity`. This script
# adds the second layer the plan asks for: the SAME scene's raw frame from each renderer, compared
# pixel-for-pixel by cna_diag_compare, which is the only way a difference the fixture's assertions
# do not happen to sample still gets seen.
#
# Usage:
#   scripts/run-parity-fixture.sh <fixture> [easygl-build] [webgpu-build] [tolerance]
#
#   fixture        a name from CNA_PARITY_FIXTURES (e.g. vertex_semantics)
#   easygl-build   default cmake-build-debug   (configured -DCNA_GRAPHICS_RENDERER=OPENGL33)
#   webgpu-build   default cmake-build-webgpu  (configured -DCNA_GRAPHICS_RENDERER=WEBGPU)
#   tolerance      default 2 -- a shared fixture renders a scene designed to be reproducible, so the
#                  cross-renderer tolerance here is TIGHT on purpose. Raise it only for a fixture
#                  whose own header documents why, never to get a run green.
#
# DISPLAY is taken from CNA_PARITY_DISPLAY (default :131, the GPU-backed virtual display the WebGPU
# CTests use). Exit 0 = both fixtures passed their own assertions AND the frames match.
set -euo pipefail

FIXTURE="${1:?usage: run-parity-fixture.sh <fixture> [easygl-build] [webgpu-build] [tolerance]}"
EASYGL_BUILD="${2:-cmake-build-debug}"
WEBGPU_BUILD="${3:-cmake-build-webgpu}"
TOLERANCE="${4:-2}"
DISPLAY_VALUE="${CNA_PARITY_DISPLAY:-:131}"

EASYGL_BIN="${EASYGL_BUILD}/cna_parity_${FIXTURE}_easygl"
WEBGPU_BIN="${WEBGPU_BUILD}/cna_parity_${FIXTURE}_webgpu"
COMPARE="${CNA_PARITY_COMPARE:-${EASYGL_BUILD}/cna_diag_compare}"

for f in "${EASYGL_BIN}" "${WEBGPU_BIN}" "${COMPARE}"; do
    [[ -x "$f" ]] || { echo "error: missing or non-executable '$f'" >&2; exit 2; }
done

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

run_leg() {
    local name="$1" bin="$2" dump="$3"
    local log="${WORK}/${name}.log"
    echo "=== ${name} ==="
    if ! env SDL_VIDEODRIVER=x11 DISPLAY="${DISPLAY_VALUE}" "${bin}" "${dump}" >"${log}" 2>&1; then
        cat "${log}"
        echo "FAIL: ${name} fixture assertions did not pass" >&2
        exit 1
    fi
    cat "${log}"
}

# "[dump] wrote N bytes (WxH RGBA8) to ..." -- the fixture states its own frame size, so this
# script never has to know any fixture's resolution.
leg_size() {
    sed -n 's/.*(\([0-9]*x[0-9]*\) RGBA8).*/\1/p' "${WORK}/$1.log" | tail -1
}

EASYGL_DUMP="${WORK}/easygl.rgba"
WEBGPU_DUMP="${WORK}/webgpu.rgba"

run_leg easygl "${EASYGL_BIN}" "${EASYGL_DUMP}"
run_leg webgpu "${WEBGPU_BIN}" "${WEBGPU_DUMP}"

EASYGL_SIZE="$(leg_size easygl)"
WEBGPU_SIZE="$(leg_size webgpu)"

if [[ -z "${EASYGL_SIZE}" || "${EASYGL_SIZE}" != "${WEBGPU_SIZE}" ]]; then
    echo "FAIL: frame sizes disagree (easygl='${EASYGL_SIZE}' webgpu='${WEBGPU_SIZE}')" >&2
    exit 1
fi

echo "=== cna_diag_compare (${EASYGL_SIZE}, tolerance ${TOLERANCE}) ==="
"${COMPARE}" "${EASYGL_DUMP}" "${WEBGPU_DUMP}" "${TOLERANCE}" "${EASYGL_SIZE}"
