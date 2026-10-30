#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# The focused SDLGPU smoke set that CI runs on a host whose default renderer is SDLGPU but where
# the full suite does not run: macOS (SDL's Metal backend, apple-ci.yml) and native Windows (SDL's
# Direct3D 12 backend, d3d-windows-ci.yml). RRC-051: after the retired METAL and DIRECTX11 had been
# those hosts' native renderers, CI built SDLGPU -- now their default -- on neither of them.
#
# Every program creates a real SDLGPU device on the host's own backend, draws, reads the result
# back and checks pixels or contracts:
#   cna_test_sdlgpu_smoke   device, window and swapchain lifecycle; 60 cleared frames; buffers
#   cna_test_sdlgpu_2d      Texture2D upload and SpriteBatch drawing, read back
#   the conformance programs: ClearOptions on every target type, +X/+Y orientation of the effect
#   families, a decoded PNG drawn exactly, XNA's seven SpriteBatch.Draw overloads, the multisample
#   clamp, and the disposed-resource refusals.
#
# Unlike CTest, which reports exit code 77 as a skip, this requires exit code 0 from every program:
# a host where SDLGPU cannot create its device fails the job instead of passing it vacuously.
#
# Usage: scripts/ci/run-sdlgpu-smoke.sh --targets   prints the CMake targets to build
#        scripts/ci/run-sdlgpu-smoke.sh <build-dir> runs them from that single-config build tree
set -uo pipefail

programs=(
    cna_test_sdlgpu_smoke
    cna_test_sdlgpu_2d
    cna_conformance_clear_options_matrix_sdlgpu
    cna_conformance_effect_orientation_sdlgpu
    cna_conformance_texture2d_fromstream_sdlgpu
    cna_conformance_spritebatch_draw_overloads_sdlgpu
    cna_conformance_multisample_count_clamp_sdlgpu
    cna_conformance_disposed_guards_sdlgpu
)

if [ "${1:-}" = "--targets" ]; then
    echo "${programs[*]}"
    exit 0
fi

build="${1:?usage: run-sdlgpu-smoke.sh <build-dir> | --targets}"
suffix=""
case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) suffix=".exe" ;;
esac

failed=()
for program in "${programs[@]}"; do
    executable="${build}/${program}${suffix}"
    echo "=== ${program}"
    if [ ! -x "${executable}" ]; then
        echo "${executable} was not built" >&2
        failed+=("${program} (not built)")
        continue
    fi
    "${executable}"
    status=$?
    if [ "${status}" -ne 0 ]; then
        if [ "${status}" -eq 77 ]; then
            echo "${program} skipped (exit 77): it found no SDLGPU device or display to run on" >&2
        fi
        failed+=("${program} (exit ${status})")
    fi
done

if [ "${#failed[@]}" -ne 0 ]; then
    echo "SDLGPU smoke failed: ${failed[*]}" >&2
    exit 1
fi
echo "SDLGPU smoke: all ${#programs[@]} programs passed"
