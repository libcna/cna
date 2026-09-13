#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xna_sample_xnb_sweep.md XNASWEEP-149: run the documented public D3DX9 mesh
# optimisation entry points over the same probe file genuine XNA's MeshHelper.OptimizeForCache was
# measured on, so the two Microsoft black boxes can be compared.
#
# The DLL is the Microsoft redistributable that ships with the June 2010 DirectX SDK, kept outside
# this repository under /rv/tmp/samples/_tools/ and loaded by absolute path so that no Wine builtin
# can stand in for it.  Nothing is copied into the repository and CNA gains no dependency on it.
#
#   $1  probe file   (default build/xna-sample-sweep/optimize/probes.txt)
#   $2  output file  (default build/xna-sample-sweep/optimize/d3dx-answers.txt)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
probes="${1:-$repo/build/xna-sample-sweep/optimize/probes.txt}"
answers="${2:-$repo/build/xna-sample-sweep/optimize/d3dx-answers.txt}"
dll="${CNA_D3DX9_DLL:-/rv/tmp/samples/_tools/directx-sdk-june-2010/extract/DXSDK/Utilities/bin/x86/d3dx9_43.dll}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
build="$repo/build/xna-pipeline-oracle/optimize"

command -v i686-w64-mingw32-gcc >/dev/null || {
    echo "run-d3dx-oracle: i686-w64-mingw32-gcc not found" >&2; exit 3; }
command -v wine >/dev/null || { echo "run-d3dx-oracle: wine not found" >&2; exit 3; }
[ -f "$probes" ] || { echo "run-d3dx-oracle: missing $probes" >&2; exit 3; }
[ -f "$dll" ]    || { echo "run-d3dx-oracle: missing $dll" >&2; exit 3; }

mkdir -p "$build" "$(dirname "$answers")"
ccache=""
command -v ccache >/dev/null && ccache="ccache"
$ccache i686-w64-mingw32-gcc -O2 -municode -o "$build/D3dxOptimizeOracle.exe" \
    "$here/D3dxOptimizeOracle.c" 2>/dev/null \
  || $ccache i686-w64-mingw32-gcc -O2 -o "$build/D3dxOptimizeOracle.exe" "$here/D3dxOptimizeOracle.c"

cp "$probes" "$build/d3dx-probes.txt"
win_dll="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$dll" 2>/dev/null)"
win_probes="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/d3dx-probes.txt" 2>/dev/null)"
win_answers="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/d3dx-answers.txt" 2>/dev/null)"
env -u WAYLAND_DISPLAY DISPLAY="${CNA_XNA40_DISPLAY:-:99}" WINEPREFIX="$prefix" WINEDEBUG=-all \
    wine "$build/D3dxOptimizeOracle.exe" "$win_dll" "$win_probes" "$win_answers"
tr -d '\r' < "$build/d3dx-answers.txt" > "$answers"
echo "run-d3dx-oracle: $(wc -l < "$answers") answers to $answers"
