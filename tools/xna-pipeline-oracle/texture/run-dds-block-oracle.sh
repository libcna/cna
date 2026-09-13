#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xna_sample_xnb_sweep.md XNASWEEP-224: run the genuine TextureImporter over
# block-compressed `.dds` files whose dimensions are not a whole number of blocks.
#
#   CNA_XNA40_REFERENCES  directory holding Microsoft.Xna.Framework*.dll
#   CNA_XNA40_WINEPREFIX  Wine prefix with .NET Framework 4.0 (default ~/.wine-cna-xna40)
#   $1                    output directory (default build/xna-pipeline-oracle/dds-block)
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
out="${1:-$repo/tests/reference/xna40/texture}"
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
build="$repo/build/xna-pipeline-oracle/dds-block-build"
for dll in Microsoft.Xna.Framework.dll Microsoft.Xna.Framework.Graphics.dll \
           Microsoft.Xna.Framework.Content.Pipeline.dll \
           Microsoft.Xna.Framework.Content.Pipeline.TextureImporter.dll; do
    [ -f "$refs/$dll" ] || { echo "run-dds-block-oracle: missing $refs/$dll" >&2; exit 3; }
done
command -v mcs >/dev/null || { echo "run-dds-block-oracle: mcs not found" >&2; exit 3; }
command -v wine >/dev/null || { echo "run-dds-block-oracle: wine not found" >&2; exit 3; }
mkdir -p "$build" "$out"
cp "$refs/Microsoft.Xna.Framework.dll" "$refs/Microsoft.Xna.Framework.Graphics.dll" \
   "$refs/Microsoft.Xna.Framework.Content.Pipeline.dll" \
   "$refs/Microsoft.Xna.Framework.Content.Pipeline.TextureImporter.dll" "$build/"
mcs -sdk:4 -platform:x86 -target:exe -nologo -out:"$build/DdsBlockOracle.exe" \
    -r:"$build/Microsoft.Xna.Framework.dll" -r:"$build/Microsoft.Xna.Framework.Graphics.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.TextureImporter.dll" \
    "$here/DdsBlockOracle.cs"
win_out="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/out" 2>/dev/null)"
rm -rf "$build/out"; mkdir -p "$build/out"
win_out="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/out" 2>/dev/null)"
win_fix="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$repo/tests/assets/xna40/texture" 2>/dev/null)"
env -u WAYLAND_DISPLAY DISPLAY="${CNA_XNA40_DISPLAY:-:99}" WINEPREFIX="$prefix" WINEDEBUG=-all \
    wine "$build/DdsBlockOracle.exe" "$win_out" "$win_fix"
tr -d '\r' < "$build/out/dds-block-oracle.json" > "$out/dds-block-oracle.json"
echo "run-dds-block-oracle: wrote $out/dds-block-oracle.json"
