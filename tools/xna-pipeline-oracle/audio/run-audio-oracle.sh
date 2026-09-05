#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xnapipeline_parity.md XNAPP-160, XNAPP-161: run the genuine XNA 4.0 framework over the packing
# measurements in AudioContentOracle.cs and record what it does.
#
# Compiled with mono's mcs on Linux against the XNA Game Studio 4.0 reference assemblies and
# executed under the real .NET Framework 4.0 in a Wine prefix. No Direct3D device is created here,
# so unlike the graphics oracle this one needs no display.
#
#   CNA_XNA40_REFERENCES  directory holding Microsoft.Xna.Framework*.dll (XNA GS 4.0 References/Windows/x86)
#   CNA_XNA40_WINEPREFIX  Wine prefix with .NET Framework 4.0 (default ~/.wine-cna-xna40)
#   $1                    output directory (default tests/reference/xna40/audio)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
out="${1:-$repo/tests/reference/xna40/audio}"
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
build="$repo/build/xna-pipeline-oracle/audio"

for dll in Microsoft.Xna.Framework.dll Microsoft.Xna.Framework.Content.Pipeline.dll; do
    [ -f "$refs/$dll" ] || { echo "run-audio-oracle: missing $refs/$dll" >&2; exit 3; }
done
command -v mcs >/dev/null || { echo "run-audio-oracle: mcs not found" >&2; exit 3; }
command -v wine >/dev/null || { echo "run-audio-oracle: wine not found" >&2; exit 3; }

mkdir -p "$build" "$out"
# The Microsoft assemblies are copied only into the ignored build directory, beside the driver,
# so the CLR finds them without a GAC; nothing Microsoft owns reaches the repository.
cp "$refs/Microsoft.Xna.Framework.dll" "$refs/Microsoft.Xna.Framework.Graphics.dll" \
   "$refs/Microsoft.Xna.Framework.Content.Pipeline.dll" "$refs/Microsoft.Xna.Framework.Video.dll" "$build/"
# The pipeline's native helper carries the audio conversions; without it every ConvertFormat
# reports "Specified method is not supported", which is an environment artifact, not XNA behaviour.
native="${CNA_XNA40_NATIVE:-$prefix/drive_c/Program Files/Common Files/Microsoft Shared/XNA/Framework/v4.0/XnaNative.dll}"
[ -f "$native" ] || { echo "run-audio-oracle: missing $native" >&2; exit 3; }
cp "$native" "$build/"
# The audio side has a second native helper, in the Game Studio bin rather than the shared
# framework directory; without it every AudioContent reports a missing DLL.
media="${CNA_XNA40_MEDIA_HELPER:-$refs/../../../Bin/XnaMediaHelper_1.dll}"
[ -f "$media" ] || { echo "run-audio-oracle: missing $media" >&2; exit 3; }
cp "$media" "$build/"
mcs -sdk:4 -platform:x86 -target:exe -nologo -out:"$build/AudioContentOracle.exe" \
    -r:"$build/Microsoft.Xna.Framework.dll" -r:"$build/Microsoft.Xna.Framework.Graphics.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.dll" \
    -r:"$build/Microsoft.Xna.Framework.Video.dll" \
    "$here/AudioContentOracle.cs"

win_out="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/out" 2>/dev/null)"
rm -rf "$build/out"; mkdir -p "$build/out"
env -u WAYLAND_DISPLAY -u DISPLAY WINEPREFIX="$prefix" WINEDEBUG=-all wine "$build/AudioContentOracle.exe" "$win_out"

# Publish: strip CRLF so the fixtures are byte-stable across hosts, keep everything else verbatim.
find "$out" -maxdepth 1 -type f -name '*.json' -delete
for f in "$build"/out/*; do
    # The driver keeps its own scratch WAVs in out/work; only the measurements are published.
    [ -f "$f" ] || continue
    tr -d '\r' < "$f" > "$out/$(basename "$f")"
done
echo "run-audio-oracle: wrote $(ls "$out" | wc -l) files to $out"
