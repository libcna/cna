#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xnapipeline_parity.md XNAPP-098: run the genuine XNA 4.0 graphics content object model
# over the measurements in GraphicsContentOracle.cs and record what it does.
#
# Compiled with mono's mcs on Linux against the XNA Game Studio 4.0 reference assemblies and
# executed under the real .NET Framework 4.0 in a Wine prefix (the pipeline assembly is MSIL, but
# the serializer's behaviour is what is being measured, so the Microsoft runtime runs it).
#
#   CNA_XNA40_REFERENCES  directory holding Microsoft.Xna.Framework*.dll (XNA GS 4.0 References/Windows/x86)
#   CNA_XNA40_WINEPREFIX  Wine prefix with .NET Framework 4.0 (default ~/.wine-cna-xna40)
#   $1                    output directory (default tests/reference/xna40/graphics)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
out="${1:-$repo/tests/reference/xna40/graphics}"
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
build="$repo/build/xna-pipeline-oracle/graphics"

for dll in Microsoft.Xna.Framework.dll Microsoft.Xna.Framework.Graphics.dll Microsoft.Xna.Framework.Content.Pipeline.dll; do
    [ -f "$refs/$dll" ] || { echo "run-graphics-oracle: missing $refs/$dll" >&2; exit 3; }
done
command -v mcs >/dev/null || { echo "run-graphics-oracle: mcs not found" >&2; exit 3; }
command -v wine >/dev/null || { echo "run-graphics-oracle: wine not found" >&2; exit 3; }

mkdir -p "$build" "$out"
# The Microsoft assemblies are copied only into the ignored build directory, beside the driver,
# so the CLR finds them without a GAC; nothing Microsoft owns reaches the repository.
cp "$refs/Microsoft.Xna.Framework.dll" "$refs/Microsoft.Xna.Framework.Graphics.dll" \
   "$refs/Microsoft.Xna.Framework.Content.Pipeline.dll" \
   "$refs/Microsoft.Xna.Framework.Content.Pipeline.EffectImporter.dll" \
   "$refs/Microsoft.Xna.Framework.Content.Pipeline.TextureImporter.dll" "$build/"
# The pipeline's native helper (texture resampling, format conversion, DXT) lives beside the
# managed assembly in the installed framework; without it those operations report
# "Specified method is not supported", which is an environment artifact, not XNA behaviour.
native="${CNA_XNA40_NATIVE:-$prefix/drive_c/Program Files/Common Files/Microsoft Shared/XNA/Framework/v4.0/XnaNative.dll}"
[ -f "$native" ] || { echo "run-graphics-oracle: missing $native" >&2; exit 3; }
cp "$native" "$build/"
mcs -sdk:4 -platform:x86 -target:exe -nologo -out:"$build/GraphicsContentOracle.exe" \
    -r:"$build/Microsoft.Xna.Framework.dll" -r:"$build/Microsoft.Xna.Framework.Graphics.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.EffectImporter.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.TextureImporter.dll" \
    -r:System.Drawing -r:System.Xml -r:System.Core \
    "$here/GraphicsContentOracle.cs"

win_out="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/out" 2>/dev/null)"
rm -rf "$build/out"; mkdir -p "$build/out"
# The pipeline's texture paths (resampling, DXT, mipmaps) create a Direct3D device, which under
# Wine needs an X display: a virtual one is fine (Xvfb :99 by default).
env -u WAYLAND_DISPLAY DISPLAY="${CNA_XNA40_DISPLAY:-:99}" WINEPREFIX="$prefix" WINEDEBUG=-all wine "$build/GraphicsContentOracle.exe" "$win_out"

# Publish: strip CRLF so the fixtures are byte-stable across hosts, keep everything else verbatim.
find "$out" -maxdepth 1 -type f \( -name '*.json' -o -name '*.bin' \) -delete
for f in "$build"/out/*; do
    # The importer probes keep their fixtures in out/work; only the measurements are published.
    [ -f "$f" ] || continue
    tr -d '\r' < "$f" > "$out/$(basename "$f")"
done
echo "run-graphics-oracle: wrote $(ls "$out" | wc -l) files to $out"
