#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xnapipeline_parity.md XNAPP-216, XNAPP-220: run the genuine XNA 4.0 modelling
# importers over the synthetic corpus and record the NodeContent graph each answers.
#
# XImporter and FBXImporter are mixed-mode assemblies with native loaders inside (D3DX's `.x`
# reader and the FBX SDK), so unlike the audio oracle these need whatever those loaders need. The
# run is given a display for the same reason the graphics oracle is.
#
#   CNA_XNA40_REFERENCES  directory holding Microsoft.Xna.Framework*.dll
#   CNA_XNA40_WINEPREFIX  Wine prefix with .NET Framework 4.0 (default ~/.wine-cna-xna40)
#   CNA_XNA40_DISPLAY     X display to use (default :99)
#   $1                    output directory (default tests/reference/xna40/model)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
out="${1:-$repo/tests/reference/xna40/model}"
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
build="$repo/build/xna-pipeline-oracle/model"
fixtures="$repo/tests/assets/xna40/model"

for dll in Microsoft.Xna.Framework.dll Microsoft.Xna.Framework.Content.Pipeline.dll \
           Microsoft.Xna.Framework.Content.Pipeline.XImporter.dll \
           Microsoft.Xna.Framework.Content.Pipeline.FBXImporter.dll; do
    [ -f "$refs/$dll" ] || { echo "run-model-oracle: missing $refs/$dll" >&2; exit 3; }
done
command -v mcs >/dev/null  || { echo "run-model-oracle: mcs not found" >&2; exit 3; }
command -v wine >/dev/null || { echo "run-model-oracle: wine not found" >&2; exit 3; }
[ -d "$fixtures" ] || { echo "run-model-oracle: no corpus at $fixtures" >&2; exit 3; }

mkdir -p "$build" "$out"
# The Microsoft assemblies are copied only into the ignored build directory, beside the driver;
# nothing Microsoft owns reaches the repository.
cp "$refs/Microsoft.Xna.Framework.dll" "$refs/Microsoft.Xna.Framework.Graphics.dll" \
   "$refs/Microsoft.Xna.Framework.Content.Pipeline.dll" \
   "$refs/Microsoft.Xna.Framework.Content.Pipeline.XImporter.dll" \
   "$refs/Microsoft.Xna.Framework.Content.Pipeline.FBXImporter.dll" "$build/"
native="${CNA_XNA40_NATIVE:-$prefix/drive_c/Program Files/Common Files/Microsoft Shared/XNA/Framework/v4.0/XnaNative.dll}"
[ -f "$native" ] && cp "$native" "$build/"

mcs -sdk:4 -platform:x86 -target:exe -nologo -out:"$build/ModelImportOracle.exe" \
    -r:"$build/Microsoft.Xna.Framework.dll" -r:"$build/Microsoft.Xna.Framework.Graphics.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.XImporter.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.FBXImporter.dll" \
    "$here/ModelImportOracle.cs"

rm -rf "$build/out" "$build/fixtures"; mkdir -p "$build/out" "$build/fixtures"
cp -a "$fixtures/." "$build/fixtures/"
win_out="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/out" 2>/dev/null)"
win_fix="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/fixtures" 2>/dev/null)"
status=0
timeout "${CNA_MODEL_ORACLE_TIMEOUT:-900}" \
  env -u WAYLAND_DISPLAY DISPLAY="${CNA_XNA40_DISPLAY:-:99}" WINEPREFIX="$prefix" WINEDEBUG=-all \
  wine "$build/ModelImportOracle.exe" "$win_out" "$win_fix" || status=$?
if [ "$status" -ne 0 ]; then
    echo "run-model-oracle: driver exited $status; publishing what it recorded before that" >&2
fi

find "$out" -maxdepth 1 -type f -name '*.json' -delete
for f in "$build"/out/*; do
    [ -f "$f" ] || continue
    tr -d '\r' < "$f" > "$out/$(basename "$f")"
done
echo "run-model-oracle: wrote $(ls "$out" | wc -l) files to $out"
