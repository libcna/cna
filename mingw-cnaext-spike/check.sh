#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plan_modern.md MOD-1719 -- cross-compile check for the engine layer as the D3D renderers see it.
#
# Run from the repository root:
#   ./mingw-cnaext-spike/check.sh
#
# Needs g++-mingw-w64-x86-64 (Debian/Ubuntu: sudo apt install g++-mingw-w64-x86-64) and, for the
# sharp-runtime include paths, a sharp-runtime checkout beside this one. Override either with
# SHARP_RUNTIME=... on the command line.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SHARP_RUNTIME="${SHARP_RUNTIME:-$(cd "$REPO/.." && pwd)/sharp-runtime}"
CXX="${CXX:-x86_64-w64-mingw32-g++}"
LAUNCHER="$(command -v ccache || true)"

if ! command -v "$CXX" >/dev/null; then
    echo "MOD-1719: $CXX not found -- install g++-mingw-w64-x86-64" >&2
    exit 2
fi
if [ ! -d "$SHARP_RUNTIME/modules/core/include" ]; then
    echo "MOD-1719: sharp-runtime not found at $SHARP_RUNTIME -- set SHARP_RUNTIME=..." >&2
    exit 2
fi

INC=(
    -I"$REPO/modules/graphics-ext/include"
    -I"$REPO/modules/graphics/include"
    -I"$REPO/modules/math/include"
    -I"$REPO/modules/core/include"
    -I"$REPO/modules/platform/include"
    -I"$SHARP_RUNTIME/modules/core/include"
    -I"$SHARP_RUNTIME/modules/io/include"
    -I"$SHARP_RUNTIME/modules/uri/include"
    -I"$SHARP_RUNTIME/modules/collections/include"
    -I"$SHARP_RUNTIME/modules/text/include"
    -I"$SHARP_RUNTIME/modules/buffers/include"
    -isystem "$SHARP_RUNTIME/vendor"
)
BASE=(-DCNA_CNAEXT -DSHARP_RUNTIME_HAS_NATIVE_INT128=1 -DXNA5 -DCNA_GL_PROFILE_OPENGLES3)

fail=0
for renderer in DIRECTX11 DIRECTX12 DIRECTX9; do
    echo "== CNA_RENDERER_$renderer =="
    for src in "$REPO"/modules/graphics-ext/src/*.cpp; do
        if ! $LAUNCHER "$CXX" -std=c++23 -fsyntax-only -Wall -Wextra -Wno-unused-parameter \
                "${BASE[@]}" "-DCNA_RENDERER_$renderer" "${INC[@]}" "$src"; then
            echo "FAILED: $(basename "$src") [$renderer]" >&2
            fail=1
        fi
    done
    # The reason this script exists rather than a plain "does it parse": a D3D translation unit
    # includes <windows.h>, whose macros (near, far, GetObject, ...) are the actual portability
    # hazard for an engine layer written on Linux.
    if ! $LAUNCHER "$CXX" -std=c++23 -fsyntax-only -Wall -Wextra \
            "${BASE[@]}" "-DCNA_RENDERER_$renderer" "${INC[@]}" \
            "$REPO/mingw-cnaext-spike/windows_header_collisions.cpp"; then
        echo "FAILED: windows_header_collisions.cpp [$renderer]" >&2
        fail=1
    fi
done

# The MSVC-shaped min/max probe is scored differently, because it does not compile today and the
# reason is outside this repository: sharp-runtime's SharpRuntimeHelper.hpp writes
# std::numeric_limits<T>::max() unparenthesised, which the macro eats. What this repository can be
# held to is that *its own* headers are clean, so the check is "nothing under modules/graphics-ext
# appears in the diagnostics" rather than "it compiles".
echo "== MSVC-shaped min/max =="
minmax_log="$(mktemp)"
trap 'rm -f "$minmax_log"' EXIT
$LAUNCHER "$CXX" -std=c++23 -fsyntax-only "${BASE[@]}" -DCNA_RENDERER_DIRECTX11 "${INC[@]}" \
        "$REPO/mingw-cnaext-spike/msvc_minmax_collisions.cpp" >"$minmax_log" 2>&1 || true
# Score on where the diagnostics *originate*, not on which files appear in the "In file included
# from" chains -- every engine-layer header appears in those chains by construction, so matching
# them would make this check fail no matter what.
offenders="$(grep -oE '^[^:]+:[0-9]+:[0-9]+: error' "$minmax_log" | cut -d: -f1 | sort -u || true)"
if echo "$offenders" | grep -q 'modules/graphics-ext'; then
    echo "FAILED: an engine-layer header is not min/max-macro-clean:" >&2
    echo "$offenders" | grep 'modules/graphics-ext' >&2
    fail=1
else
    if [ -n "$offenders" ]; then
        echo "  engine layer clean; still failing outside it (known, see MOD-1719):"
        echo "$offenders" | sed 's/^/    /'
    else
        echo "  clean everywhere -- sharp-runtime's numeric_limits usage appears to be fixed."
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "MOD-1719: FAILED"
    exit 1
fi
echo "MOD-1719: every engine-layer translation unit cross-compiles for x86_64-w64-mingw32."
