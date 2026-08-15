#!/usr/bin/env bash
# Task 457: cross-renderer smoke-test orchestrator.
#
# Configures (if needed), builds, and runs each graphics renderer's own 3-frame
# smoke test (Tasks 85/88/89/730, CTest label "GraphicsSmoke") sequentially --
# never concurrently, matching this project's own testing discipline -- using
# the persistent per-renderer build directories this project already uses
# locally (cmake-build-debug=OPENGLES3, cmake-build-vulkan, cmake-build-bgfx,
# cmake-build-sdl).
#
# A renderer whose build directory does not exist and cannot be freshly
# configured (e.g. missing system dependencies such as the Vulkan SDK) is
# reported as "not available on this machine" and skipped rather than failing
# the whole run -- matching this task's own "every renderer available on the
# machine" framing. A renderer that DOES configure/build but whose smoke test
# fails makes the overall run fail (non-zero exit), so this composes cleanly
# as a CI step if/when this project's CI is extended to call it.
#
# plan_runtimerenderer.md RTR-P9-22: a MULTI-RENDERER mode. Passing a semicolon-separated renderer
# list runs every one of them from ONE build directory, selecting each at runtime through
# CNA_GRAPHICS_RENDERER rather than reconfiguring and rebuilding per renderer:
#
#     scripts/run-all-renderer-smoke-tests.sh --multi "HEADLESS;SOFTWARE;STUB"
#
# That is where a multi-renderer build actually pays for itself: N renderers cost one build instead
# of N. The default per-renderer mode is unchanged, and remains the right choice when the renderers
# cannot share a binary (see docs/runtime-renderer-selection.md's combination table).
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

CNA_SMOKE_TEST_DISPLAY="${CNA_SMOKE_TEST_DISPLAY:-:99}"

MULTI_LIST=""
if [ "${1:-}" = "--multi" ]; then
    MULTI_LIST="${2:-}"
    if [ -z "${MULTI_LIST}" ]; then
        echo "usage: $0 --multi \"RENDERER;RENDERER;...\"" >&2
        exit 2
    fi
fi

RENDERERS=(OPENGLES3 VULKAN BGFX SDL_RENDERER)
declare -A RENDERER_DIRS=(
    [OPENGLES3]="cmake-build-debug"
    [VULKAN]="cmake-build-vulkan"
    [BGFX]="cmake-build-bgfx"
    [SDL_RENDERER]="cmake-build-sdl"
)
declare -A RESULTS

if [ -n "${MULTI_LIST}" ]; then
    # One build, every renderer selected at runtime.
    IFS=';' read -r -a MULTI_RENDERERS <<< "${MULTI_LIST}"
    dir="cmake-build-multi"
    default="${MULTI_RENDERERS[0]}"

    echo "=== multi-renderer build (${dir}): ${MULTI_LIST} ==="
    if ! cmake -S . -B "${dir}" -DCNA_GRAPHICS_RENDERER="${default}" \
            -DCNA_GRAPHICS_RENDERERS="${MULTI_LIST}" \
            -DCNA_BUILD_TESTS=ON -DCNA_TEST_DISPLAY="${CNA_SMOKE_TEST_DISPLAY}"; then
        echo "FAIL: the requested renderer combination does not configure."
        echo "      See docs/runtime-renderer-selection.md for which combinations are refused and why."
        exit 1
    fi
    cmake --build "${dir}" -j4 -- -k

    for renderer in "${MULTI_RENDERERS[@]}"; do
        echo "=== ${renderer} (selected at runtime from the ${dir} build) ==="

        # Which label carries this renderer's smoke test. The GL/Vulkan/bgfx family registers
        # "GraphicsSmoke"; the CPU renderers register under their own name (Stub_Smoke is labelled
        # "Stub"). Try both rather than assuming, because `ctest -L <nothing matched>` exits 0 --
        # so a wrong label would report PASS for a renderer that ran no test at all.
        # Labels are spelled in the renderers' own capitalisation ("Headless", "Stub"), not the
        # uppercase identity, so the match is case-insensitive and ignores underscores.
        label=""
        renderer_key="$(printf '%s' "${renderer}" | tr -d '_' | tr '[:upper:]' '[:lower:]')"
        while IFS= read -r candidate; do
            candidate_key="$(printf '%s' "${candidate}" | tr -d '_' | tr '[:upper:]' '[:lower:]')"
            if [ "${candidate_key}" = "graphicssmoke" ] || [ "${candidate_key}" = "${renderer_key}" ]; then
                if [ "$(ctest --test-dir "${dir}" -N -L "^${candidate}$" 2>/dev/null \
                        | grep -c 'Test *#')" -gt 0 ]; then
                    label="${candidate}"
                    break
                fi
            fi
        done < <(ctest --test-dir "${dir}" --print-labels 2>/dev/null | sed -n 's/^  //p')

        if [ -z "${label}" ]; then
            echo "SKIP: no smoke test registered for ${renderer} in this build"
            RESULTS[${renderer}]="SKIPPED (no smoke test)"
            continue
        fi

        echo "-- using ctest label '${label}' --"
        if CNA_GRAPHICS_RENDERER="${renderer}" \
                ctest --test-dir "${dir}" -L "${label}" --output-on-failure; then
            RESULTS[${renderer}]="PASS"
        else
            RESULTS[${renderer}]="FAIL"
        fi
    done

    echo
    echo "=== Cross-renderer smoke test summary (one build, ${#MULTI_RENDERERS[@]} renderers) ==="
    overall=0
    for renderer in "${MULTI_RENDERERS[@]}"; do
        printf '%-14s %s\n' "${renderer}" "${RESULTS[${renderer}]:-UNKNOWN}"
        if [ "${RESULTS[${renderer}]:-}" = "FAIL" ]; then
            overall=1
        fi
    done
    exit "${overall}"
fi

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
