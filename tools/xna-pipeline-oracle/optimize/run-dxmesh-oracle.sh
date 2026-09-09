#!/usr/bin/env bash
# plans/plan_xna_sample_xnb_sweep.md XNASWEEP-149.
#
# Build DxMeshOptimizeOracle against Microsoft's open-source DirectXMesh (MIT) and run it over a
# probe file.  DirectXMesh is a Windows library, so it is cross-compiled with MinGW and run under
# Wine, exactly as D3dxOptimizeOracle.c is.  The checkout is the shared one under ~/deps, pinned to
# the revision THIRD_PARTY_NOTICES.md records.
#
# usage: run-dxmesh-oracle.sh <legacy|geometric> <probes.txt> <out.txt> [vertexCache] [restart]
set -euo pipefail

MODE="${1:?mode}"   # legacy | firstother | geometric
PROBES="${2:?probes.txt}"
OUT="${3:?out.txt}"
CACHE="${4:-}"
RESTART="${5:-}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
DXMESH="${DXMESH_ROOT:-$HOME/deps/DirectXMesh}"
DXMATH="${DXMATH_ROOT:-$HOME/deps/DirectXMath}"
PINNED_REV="bd17eb215d463d98f2b3a13082ce13979219314f"     # tag oct2025

if [ ! -d "$DXMESH" ] || [ ! -d "$DXMATH" ]; then
    echo "run-dxmesh-oracle.sh: clone microsoft/DirectXMesh and microsoft/DirectXMath into ~/deps" >&2
    exit 3
fi
REV="$(git -C "$DXMESH" rev-parse HEAD)"
if [ "$REV" != "$PINNED_REV" ]; then
    echo "run-dxmesh-oracle.sh: DirectXMesh is at $REV, expected $PINNED_REV (tag oct2025)" >&2
    echo "  git -C $DXMESH checkout $PINNED_REV" >&2
    exit 3
fi

BUILD="$ROOT/build/xna-sample-sweep/optimize/dxmesh"
mkdir -p "$BUILD/compat"
printf '#include <windows.h>\n' > "$BUILD/compat/Windows.h"

export CCACHE_DIR="${CCACHE_DIR:-/rv/cnaccache}"
export CCACHE_BASEDIR="${CCACHE_BASEDIR:-/rv}"
CXX=(i686-w64-mingw32-g++)
command -v ccache >/dev/null && CXX=(ccache "${CXX[@]}")

FLAGS=(-std=c++17 -O2 -msse2 -mfpmath=sse -D_WIN32_WINNT=0x0601
       -I"$BUILD/compat" -I"$DXMESH/DirectXMesh" -I"$DXMATH/Inc" -w)

for unit in DirectXMeshOptimizeTVC DirectXMeshUtil DirectXMeshAdjacency; do
    if [ ! -f "$BUILD/$unit.o" ] || [ "$DXMESH/DirectXMesh/$unit.cpp" -nt "$BUILD/$unit.o" ]; then
        "${CXX[@]}" "${FLAGS[@]}" -c -o "$BUILD/$unit.o" "$DXMESH/DirectXMesh/$unit.cpp"
    fi
done
DRIVER="$ROOT/tools/xna-pipeline-oracle/optimize/DxMeshOptimizeOracle.cpp"
if [ ! -f "$BUILD/driver.o" ] || [ "$DRIVER" -nt "$BUILD/driver.o" ]; then
    "${CXX[@]}" "${FLAGS[@]}" -c -o "$BUILD/driver.o" "$DRIVER"
fi
if [ ! -f "$BUILD/DxMeshOptimizeOracle.exe" ] || [ "$BUILD/driver.o" -nt "$BUILD/DxMeshOptimizeOracle.exe" ]; then
    i686-w64-mingw32-g++ -static -o "$BUILD/DxMeshOptimizeOracle.exe" \
        "$BUILD/driver.o" "$BUILD/DirectXMeshOptimizeTVC.o" "$BUILD/DirectXMeshUtil.o" \
        "$BUILD/DirectXMeshAdjacency.o"
fi

unset WAYLAND_DISPLAY
export WINEDEBUG="${WINEDEBUG:-fixme-all,err-all}"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-cna-xna40}"
wine "$BUILD/DxMeshOptimizeOracle.exe" "$MODE" "$PROBES" "$OUT" ${CACHE:+$CACHE} ${RESTART:+$RESTART}
