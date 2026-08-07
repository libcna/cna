#!/usr/bin/env bash
# Task 457: cross-backend smoke-test orchestrator.
#
# Configures (if needed), builds, and runs each graphics backend's own 3-frame
# smoke test (Tasks 85/88/89/730, CTest label "GraphicsSmoke") sequentially --
# never concurrently, matching this project's own testing discipline -- using
# the persistent per-backend build directories this project already uses
# locally (cmake-build-debug=OPENGLES, cmake-build-vulkan, cmake-build-bgfx,
# cmake-build-sdl).
#
# A backend whose build directory does not exist and cannot be freshly
# configured (e.g. missing system dependencies such as the Vulkan SDK) is
# reported as "not available on this machine" and skipped rather than failing
# the whole run -- matching this task's own "every backend available on the
# machine" framing. A backend that DOES configure/build but whose smoke test
# fails makes the overall run fail (non-zero exit), so this composes cleanly
# as a CI step if/when this project's CI is extended to call it.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

CNA_SMOKE_TEST_DISPLAY="${CNA_SMOKE_TEST_DISPLAY:-:99}"

BACKENDS=(OPENGLES VULKAN BGFX SDL_RENDERER)
declare -A BACKEND_DIRS=(
    [OPENGLES]="cmake-build-debug"
    [VULKAN]="cmake-build-vulkan"
    [BGFX]="cmake-build-bgfx"
    [SDL_RENDERER]="cmake-build-sdl"
)
declare -A RESULTS

for backend in "${BACKENDS[@]}"; do
    dir="${BACKEND_DIRS[$backend]}"
    echo "=== ${backend} (${dir}) ==="

    if [ ! -d "${dir}" ]; then
        echo "-- ${dir} does not exist, configuring fresh --"
        if ! cmake -S . -B "${dir}" -DCNA_GRAPHICS_BACKEND="${backend}" \
                -DCNA_BUILD_TESTS=ON -DCNA_TEST_DISPLAY="${CNA_SMOKE_TEST_DISPLAY}"; then
            echo "SKIP: ${backend} not available on this machine (configure failed)"
            RESULTS[${backend}]="SKIPPED (configure failed)"
            continue
        fi
    fi

    # `-k` keeps going past the one already-known, unrelated cna_demo_xact
    # Content-copy build error (see NEXT.md/plan_graphics.md) -- that target
    # is irrelevant to the smoke tests below, so a non-zero exit here is not
    # by itself treated as "backend unavailable"; ctest is the real signal.
    cmake --build "${dir}" -j4 -- -k

    if ctest --test-dir "${dir}" -L GraphicsSmoke --output-on-failure; then
        RESULTS[${backend}]="PASS"
    else
        RESULTS[${backend}]="FAIL"
    fi
done

echo
echo "=== Cross-backend smoke test summary ==="
overall=0
for backend in "${BACKENDS[@]}"; do
    printf '%-14s %s\n' "${backend}" "${RESULTS[${backend}]:-UNKNOWN}"
    if [ "${RESULTS[${backend}]:-}" = "FAIL" ]; then
        overall=1
    fi
done

exit "${overall}"
