#!/usr/bin/env bash
# Task 457: cross-renderer smoke-test orchestrator.
#
# Configures (if needed), builds, and runs each graphics renderer's own 3-frame
# smoke test (Tasks 85/88/89/730, CTest label "GraphicsSmoke") sequentially --
# never concurrently, matching this project's own testing discipline -- using
# the persistent per-renderer build directories this project already uses
# locally (cmake-build-debug=OPENGLES, cmake-build-vulkan, cmake-build-bgfx,
# cmake-build-sdl).
#
# A renderer whose build directory does not exist and cannot be freshly
# configured (e.g. missing system dependencies such as the Vulkan SDK) is
# reported as "not available on this machine" and skipped rather than failing
# the whole run -- matching this task's own "every renderer available on the
# machine" framing. A renderer that DOES configure/build but whose smoke test
# fails makes the overall run fail (non-zero exit), so this composes cleanly
# as a CI step if/when this project's CI is extended to call it.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

CNA_SMOKE_TEST_DISPLAY="${CNA_SMOKE_TEST_DISPLAY:-:99}"

RENDERERS=(OPENGLES VULKAN BGFX SDL_RENDERER)
declare -A RENDERER_DIRS=(
    [OPENGLES]="cmake-build-debug"
    [VULKAN]="cmake-build-vulkan"
    [BGFX]="cmake-build-bgfx"
    [SDL_RENDERER]="cmake-build-sdl"
)
declare -A RESULTS

for renderer in "${RENDERERS[@]}"; do
    dir="${RENDERER_DIRS[$renderer]}"
    echo "=== ${renderer} (${dir}) ==="

    if [ ! -d "${dir}" ]; then
        echo "-- ${dir} does not exist, configuring fresh --"
        if ! cmake -S . -B "${dir}" -DCNA_GRAPHICS_RENDERER="${renderer}" \
                -DCNA_BUILD_TESTS=ON -DCNA_TEST_DISPLAY="${CNA_SMOKE_TEST_DISPLAY}"; then
            echo "SKIP: ${renderer} not available on this machine (configure failed)"
            RESULTS[${renderer}]="SKIPPED (configure failed)"
            continue
        fi
    fi

    # `-k` keeps going past the one already-known, unrelated cna_demo_xact
    # Content-copy build error (see NEXT.md/plan_graphics.md) -- that target
    # is irrelevant to the smoke tests below, so a non-zero exit here is not
    # by itself treated as "renderer unavailable"; ctest is the real signal.
    cmake --build "${dir}" -j4 -- -k

    if ctest --test-dir "${dir}" -L GraphicsSmoke --output-on-failure; then
        RESULTS[${renderer}]="PASS"
    else
        RESULTS[${renderer}]="FAIL"
    fi
done

echo
echo "=== Cross-renderer smoke test summary ==="
overall=0
for renderer in "${RENDERERS[@]}"; do
    printf '%-14s %s\n' "${renderer}" "${RESULTS[${renderer}]:-UNKNOWN}"
    if [ "${RESULTS[${renderer}]:-}" = "FAIL" ]; then
        overall=1
    fi
done

exit "${overall}"
