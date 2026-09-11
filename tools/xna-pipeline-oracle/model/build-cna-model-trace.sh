#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xna_sample_xnb_sweep.md XNASWEEP-170: build CnaModelTrace against the libraries
# cmake-build-debug/ already holds, into the shared build-probe/ directory. It is a diagnostic
# tool, not part of any CMake target, and nothing it builds is production.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SR="${CNA_SHARP_RUNTIME_ROOT:-/rv/data/development/github.com/openeggbert/sharp-runtimenext}"
BUILD="${CNA_BUILD_DIR:-$ROOT/cmake-build-debug}"
export CCACHE_DIR="${CCACHE_DIR:-/rv/cnaccache}" CCACHE_BASEDIR="${CCACHE_BASEDIR:-/rv}"
mkdir -p "$ROOT/build-probe"
INCLUDES=()
for d in "$ROOT"/modules/*/include; do INCLUDES+=(-I"$d"); done
for d in "$SR"/modules/*/include; do INCLUDES+=(-I"$d"); done
LIBS=()
for l in "$BUILD"/modules/*/lib*.a "$BUILD"/SHARP_RUNTIME/lib*.a "$BUILD"/lib*.a; do
    [ -f "$l" ] && LIBS+=("$l")
done
ccache g++ -std=c++23 -O1 -g "${INCLUDES[@]}" \
    -o "$ROOT/build-probe/xnasweep170-trace" \
    "$ROOT/tools/xna-pipeline-oracle/model/CnaModelTrace.cpp" \
    -Wl,--start-group "${LIBS[@]}" -Wl,--end-group -lz -lpthread -ldl
echo "built build-probe/xnasweep170-trace"
