#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xnapipeline_parity.md XNAPP-265 (§23): run Microsoft's own BuildContent task over the
# committed corpus and record what it produced.
#
# The driver is compiled with mono's mcs against the XNA Game Studio 4.0 Refresh reference
# assemblies and MSBuild's own Framework/Utilities, and executed under the real .NET Framework in a
# Wine prefix -- BuildContent is the task an XNA project's MSBuild run invokes, so measuring it is
# measuring the build rather than a component.
#
#   CNA_XNA40_REFERENCES  directory holding the pipeline assemblies (XNA GS 4.0 References/Windows/x86)
#   CNA_XNA40_WINEPREFIX  Wine prefix with .NET Framework 4.0 (default ~/.wine-cna-xna40)
#   CNA_XNA40_DISPLAY     X display for the pipeline's D3D9 device (default :99)
#   $1                    output directory (default tests/reference/xna40/differential)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
out="${1:-$repo/tests/reference/xna40/differential}"
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
build="$repo/build/xna-pipeline-oracle/differential"

command -v mcs >/dev/null || { echo "run-differential-oracle: mcs not found" >&2; exit 3; }
command -v wine >/dev/null || { echo "run-differential-oracle: wine not found" >&2; exit 3; }
[ -f "$refs/Microsoft.Xna.Framework.Content.Pipeline.dll" ] || {
    echo "run-differential-oracle: missing the pipeline assembly under $refs" >&2; exit 3; }

msbuild_framework="/usr/lib/mono/4.5/Microsoft.Build.Framework.dll"
msbuild_utilities="/usr/lib/mono/4.5/Microsoft.Build.Utilities.v4.0.dll"
for dll in "$msbuild_framework" "$msbuild_utilities"; do
    [ -f "$dll" ] || { echo "run-differential-oracle: missing $dll (mono-devel)" >&2; exit 3; }
done

rm -rf "$build"
mkdir -p "$build/run" "$out"
# The Microsoft assemblies are copied only into the ignored build directory, beside the driver, so
# the CLR finds them without a GAC; nothing Microsoft owns reaches the repository.
for dll in "$refs"/Microsoft.Xna.Framework*.dll; do cp "$dll" "$build/run/"; done
native="${CNA_XNA40_NATIVE:-$prefix/drive_c/Program Files/Common Files/Microsoft Shared/XNA/Framework/v4.0/XnaNative.dll}"
[ -f "$native" ] || { echo "run-differential-oracle: missing $native" >&2; exit 3; }
cp "$native" "$build/run/"
# The audio and video importers are the only ones that need this second native helper, and without
# it every one of them fails with `DllNotFoundException: XnaMediaHelper_1.dll` -- an environment
# artefact that reads exactly like a refusal from XNA, which is why it is checked for rather than
# left to be discovered in the results.
media_helper="${CNA_XNA40_MEDIA_HELPER:-$(dirname "$refs")/../../Bin/XnaMediaHelper_1.dll}"
if [ ! -f "$media_helper" ]; then
    media_helper="$(find "$(cd "$refs/../../../.." && pwd)" -iname 'XnaMediaHelper_1.dll' 2>/dev/null | head -1)"
fi
[ -f "$media_helper" ] || {
    echo "run-differential-oracle: missing XnaMediaHelper_1.dll; the audio and video cases would" >&2
    echo "  record a DllNotFoundException that looks like an XNA refusal and is not one." >&2
    exit 3
}
cp "$media_helper" "$build/run/"

# The corpus's sources, copied where a Wine path can name them.
mkdir -p "$build/run/sources"
cp -a "$repo/tests/assets/xna40/." "$build/run/sources/"

mcs -sdk:4 -platform:x86 -target:exe -nologo -out:"$build/run/DifferentialOracle.exe" \
    -r:"$msbuild_framework" -r:"$msbuild_utilities" \
    -r:"$build/run/Microsoft.Xna.Framework.dll" \
    -r:"$build/run/Microsoft.Xna.Framework.Graphics.dll" \
    -r:"$build/run/Microsoft.Xna.Framework.Content.Pipeline.dll" \
    "$here/DifferentialOracle.cs"

cp "$here/corpus.json" "$build/run/corpus.json"
win() { env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$1" 2>/dev/null; }
win_corpus="$(win "$build/run/corpus.json")"
win_sources="$(win "$build/run/sources")"
win_results="$(win "$build/run/results")"
win_assemblies="$(win "$build/run")"
mkdir -p "$build/run/results"

# The pipeline's texture paths create a real Direct3D 9 device, which under Wine needs an X display;
# a virtual one is fine.
env -u WAYLAND_DISPLAY DISPLAY="${CNA_XNA40_DISPLAY:-:99}" WINEPREFIX="$prefix" WINEDEBUG=-all \
    wine "$build/run/DifferentialOracle.exe" \
    "$win_corpus" "$win_sources" "$win_results" "$win_assemblies"

# Publish: strip CRLF from the measurements so they are byte-stable across hosts; the .xnb files
# are binary and are copied verbatim.
find "$out" -maxdepth 1 -type f \( -name '*.json' -o -name '*.xnb' \) -delete
tr -d '\r' < "$build/run/results/differential-oracle.json" > "$out/differential-oracle.json"
for f in "$build"/run/results/*.xnb; do [ -f "$f" ] || continue; cp "$f" "$out/$(basename "$f")"; done
echo "run-differential-oracle: wrote $(ls "$out" | wc -l) files to $out"
