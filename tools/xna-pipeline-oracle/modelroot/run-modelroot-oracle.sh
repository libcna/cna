#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xna_sample_xnb_sweep.md XNASWEEP-162: run the genuine FbxImporter *and* ModelProcessor
# over a directory of FBX files and print every bone's transform at round-trip precision.
#
#   $1  fixture directory
#   $2  output file
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
fixtures="${1:?usage: run-modelroot-oracle.sh <fixtures> <out.txt>}"
out="${2:?usage: run-modelroot-oracle.sh <fixtures> <out.txt>}"
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
build="$repo/build/xna-pipeline-oracle/modelroot"
mkdir -p "$build" "$(dirname "$out")"
for dll in Microsoft.Xna.Framework.dll Microsoft.Xna.Framework.Graphics.dll \
           Microsoft.Xna.Framework.Content.Pipeline.dll \
           Microsoft.Xna.Framework.Content.Pipeline.FBXImporter.dll \
           Microsoft.Xna.Framework.Content.Pipeline.XImporter.dll; do
    cp "$refs/$dll" "$build/"
done
native="${CNA_XNA40_NATIVE:-$prefix/drive_c/Program Files/Common Files/Microsoft Shared/XNA/Framework/v4.0/XnaNative.dll}"
[ -f "$native" ] && cp "$native" "$build/"
mcs -sdk:4 -platform:x86 -target:exe -nologo -out:"$build/ModelRootOracle.exe" \
    -r:"$build/Microsoft.Xna.Framework.dll" -r:"$build/Microsoft.Xna.Framework.Graphics.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.FBXImporter.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.XImporter.dll" \
    "$here/ModelRootOracle.cs"
rm -rf "$build/fixtures"; mkdir -p "$build/fixtures"
cp "$fixtures"/*.fbx "$build/fixtures/" 2>/dev/null || true
cp "$fixtures"/*.x "$build/fixtures/" 2>/dev/null || true
win_in="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/fixtures" 2>/dev/null)"
win_out="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/out.txt" 2>/dev/null)"
env -u WAYLAND_DISPLAY DISPLAY="${CNA_XNA40_DISPLAY:-:99}" WINEPREFIX="$prefix" WINEDEBUG=-all \
    wine "$build/ModelRootOracle.exe" "$win_in" "$win_out"
tr -d '\r' < "$build/out.txt" > "$out"
echo "run-modelroot-oracle: $(wc -l < "$out") lines to $out"
