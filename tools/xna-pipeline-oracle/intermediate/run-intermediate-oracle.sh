#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xnapipeline_parity.md XNAPP-074: run the genuine XNA 4.0 IntermediateSerializer
# over the CNA-authored corpus in IntermediateOracle.cs and record what it writes and accepts.
#
# Compiled with mono's mcs on Linux against the XNA Game Studio 4.0 reference assemblies and
# executed under the real .NET Framework 4.0 in a Wine prefix (the pipeline assembly is MSIL, but
# the serializer's behaviour is what is being measured, so the Microsoft runtime runs it).
#
#   CNA_XNA40_REFERENCES  directory holding Microsoft.Xna.Framework*.dll (XNA GS 4.0 References/Windows/x86)
#   CNA_XNA40_WINEPREFIX  Wine prefix with .NET Framework 4.0 (default ~/.wine-cna-xna40)
#   $1                    output directory (default tests/reference/xna40/intermediate)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
out="${1:-$repo/tests/reference/xna40/intermediate}"
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
build="$repo/build/xna-pipeline-oracle/intermediate"

for dll in Microsoft.Xna.Framework.dll Microsoft.Xna.Framework.Graphics.dll Microsoft.Xna.Framework.Content.Pipeline.dll; do
    [ -f "$refs/$dll" ] || { echo "run-intermediate-oracle: missing $refs/$dll" >&2; exit 3; }
done
command -v mcs >/dev/null || { echo "run-intermediate-oracle: mcs not found" >&2; exit 3; }
command -v wine >/dev/null || { echo "run-intermediate-oracle: wine not found" >&2; exit 3; }

mkdir -p "$build" "$out"
# The Microsoft assemblies are copied only into the ignored build directory, beside the driver,
# so the CLR finds them without a GAC; nothing Microsoft owns reaches the repository.
cp "$refs/Microsoft.Xna.Framework.dll" "$refs/Microsoft.Xna.Framework.Graphics.dll" \
   "$refs/Microsoft.Xna.Framework.Content.Pipeline.dll" "$build/"
mcs -sdk:4 -platform:x86 -target:exe -nologo -out:"$build/IntermediateOracle.exe" \
    -r:"$build/Microsoft.Xna.Framework.dll" -r:"$build/Microsoft.Xna.Framework.Graphics.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.dll" -r:System.Xml \
    "$here/IntermediateOracle.cs"

win_out="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/out" 2>/dev/null)"
rm -rf "$build/out"; mkdir -p "$build/out"
env -u WAYLAND_DISPLAY -u DISPLAY WINEPREFIX="$prefix" WINEDEBUG=-all wine "$build/IntermediateOracle.exe" "$win_out"

# Publish: strip CRLF so the fixtures are byte-stable across hosts, keep everything else verbatim.
find "$out" -maxdepth 1 -type f \( -name '*.xml' -o -name '*.txt' -o -name 'manifest.json' \) -delete
for f in "$build"/out/*; do
    tr -d '\r' < "$f" > "$out/$(basename "$f")"
done
echo "run-intermediate-oracle: wrote $(ls "$out" | wc -l) files to $out"
