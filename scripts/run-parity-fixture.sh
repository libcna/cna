#!/usr/bin/env bash
# WEBGPU-207: run ONE shared parity fixture under BOTH renderers and diff the
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
#   easygl-build   default cmake-build-opengl33 (must be configured OPENGL33)
#   webgpu-build   default cmake-build-webgpu  (configured -DCNA_GRAPHICS_RENDERER=WEBGPU)
#   tolerance      default 2 -- a shared fixture renders a scene designed to be reproducible, so the
#                  cross-renderer tolerance here is TIGHT on purpose. Raise it only for a fixture
#                  whose own header documents why, never to get a run green.
#
# DISPLAY is inherited from the caller (normally tools/platform/run_gpu_tests_private.sh), with
# CNA_PARITY_DISPLAY as an explicit test-infrastructure override. There is deliberately no fixed
# fallback display: a parity run must never land on the owner's desktop by accident.
# Exit 0 = both fixtures passed their own assertions AND the frames match.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIXTURE="${1:?usage: run-parity-fixture.sh <fixture> [easygl-build] [webgpu-build] [tolerance]}"
EASYGL_BUILD="${2:-cmake-build-opengl33}"
WEBGPU_BUILD="${3:-cmake-build-webgpu}"
TOLERANCE="${4:-2}"
DISPLAY_VALUE="${CNA_PARITY_DISPLAY:-${DISPLAY:-}}"

build_path() {
    case "$1" in
        /*) printf '%s' "$1" ;;
        *) printf '%s/%s' "$REPO_ROOT" "$1" ;;
    esac
}

require_renderer() {
    local build_root="$1" expected="$2" cache="$1/CMakeCache.txt" actual
    [[ -f "$cache" ]] || {
        echo "error: missing CMake cache '$cache'" >&2
        exit 2
    }
    actual="$(sed -n 's/^CNA_GRAPHICS_RENDERER:STRING=//p' "$cache" | tail -1)"
    [[ "$actual" == "$expected" ]] || {
        echo "error: '$build_root' selects ${actual:-unknown}, expected $expected" >&2
        exit 2
    }
}

if [[ -z "${DISPLAY_VALUE}" ]]; then
    echo "error: no private GPU display; run through tools/platform/run_gpu_tests_private.sh --exec" >&2
    exit 2
fi

EASYGL_ROOT="$(build_path "$EASYGL_BUILD")"
WEBGPU_ROOT="$(build_path "$WEBGPU_BUILD")"
require_renderer "$EASYGL_ROOT" OPENGL33
require_renderer "$WEBGPU_ROOT" WEBGPU

EASYGL_BIN="${EASYGL_ROOT}/cna_parity_${FIXTURE}_easygl"
WEBGPU_BIN="${WEBGPU_ROOT}/cna_parity_${FIXTURE}_webgpu"
COMPARE="${CNA_PARITY_COMPARE:-${EASYGL_ROOT}/cna_diag_compare}"

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
