#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xna_sample_xnb_sweep.md XNASWEEP-234: what `MeshHelper.TransformScene` does to a
# normal, in raw IEEE-754 bits.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
out="${1:-$repo/tests/reference/xna40/model}"
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
build="$repo/build/xna-pipeline-oracle/mesh-normalize"
command -v mcs >/dev/null || { echo "run-mesh-normalize-oracle: mcs not found" >&2; exit 3; }
command -v wine >/dev/null || { echo "run-mesh-normalize-oracle: wine not found" >&2; exit 3; }
mkdir -p "$build" "$out"
cp "$refs/Microsoft.Xna.Framework.dll" "$refs/Microsoft.Xna.Framework.Graphics.dll" \
   "$refs/Microsoft.Xna.Framework.Content.Pipeline.dll" "$build/"
mcs -sdk:4 -platform:x86 -target:exe -nologo -out:"$build/MeshNormalizeOracle.exe" \
    -r:"$build/Microsoft.Xna.Framework.dll" -r:"$build/Microsoft.Xna.Framework.Graphics.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.dll" \
    "$here/MeshNormalizeOracle.cs"
rm -rf "$build/out"; mkdir -p "$build/out"
win_out="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/out" 2>/dev/null)"
env -u WAYLAND_DISPLAY DISPLAY="${CNA_XNA40_DISPLAY:-:99}" WINEPREFIX="$prefix" WINEDEBUG=-all \
    wine "$build/MeshNormalizeOracle.exe" "$win_out"
tr -d '\r' < "$build/out/mesh-normalize-oracle.json" > "$out/mesh-normalize-oracle.json"
echo "run-mesh-normalize-oracle: wrote $out/mesh-normalize-oracle.json"
