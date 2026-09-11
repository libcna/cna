#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xnapipeline_parity.md XNAPP-201, XNAPP-202, XNAPP-220: run the genuine XNA 4.0 media
# importers and processors over the synthetic corpus and record what they do.
#
# Compiled with mono's mcs against the XNA Game Studio 4.0 reference assemblies and executed under
# the real .NET Framework 4.0 in a Wine prefix. No Direct3D device is created, so no display is
# needed.
#
#   CNA_XNA40_REFERENCES  directory holding Microsoft.Xna.Framework*.dll
#   CNA_XNA40_WINEPREFIX  Wine prefix with .NET Framework 4.0 (default ~/.wine-cna-xna40)
#   CNA_MEDIA_ORACLE_TIMEOUT  seconds before the run is abandoned (default 900)
#   $1                    output directory (default tests/reference/xna40/media)
#   $2..                  case names to skip, for a call that did not return on an earlier run
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
out="${1:-$repo/tests/reference/xna40/media}"
shift || true
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
build="$repo/build/xna-pipeline-oracle/media"
fixtures="$repo/tests/assets/xna40/media"

for dll in Microsoft.Xna.Framework.dll Microsoft.Xna.Framework.Content.Pipeline.dll \
           Microsoft.Xna.Framework.Content.Pipeline.AudioImporters.dll \
           Microsoft.Xna.Framework.Content.Pipeline.VideoImporters.dll; do
    [ -f "$refs/$dll" ] || { echo "run-media-oracle: missing $refs/$dll" >&2; exit 3; }
done
command -v mcs >/dev/null  || { echo "run-media-oracle: mcs not found" >&2; exit 3; }
command -v wine >/dev/null || { echo "run-media-oracle: wine not found" >&2; exit 3; }
[ -d "$fixtures" ] || { echo "run-media-oracle: no corpus at $fixtures; run make-media-fixtures.sh" >&2; exit 3; }

mkdir -p "$build" "$out"
# The Microsoft assemblies are copied only into the ignored build directory, beside the driver, so
# the CLR finds them without a GAC; nothing Microsoft owns reaches the repository.
cp "$refs/Microsoft.Xna.Framework.dll" "$refs/Microsoft.Xna.Framework.Graphics.dll" \
   "$refs/Microsoft.Xna.Framework.Content.Pipeline.dll" "$refs/Microsoft.Xna.Framework.Video.dll" \
   "$refs/Microsoft.Xna.Framework.Content.Pipeline.AudioImporters.dll" \
   "$refs/Microsoft.Xna.Framework.Content.Pipeline.VideoImporters.dll" "$build/"
native="${CNA_XNA40_NATIVE:-$prefix/drive_c/Program Files/Common Files/Microsoft Shared/XNA/Framework/v4.0/XnaNative.dll}"
[ -f "$native" ] || { echo "run-media-oracle: missing $native" >&2; exit 3; }
cp "$native" "$build/"
media="${CNA_XNA40_MEDIA_HELPER:-$refs/../../../Bin/XnaMediaHelper_1.dll}"
[ -f "$media" ] || { echo "run-media-oracle: missing $media" >&2; exit 3; }
cp "$media" "$build/"

mcs -sdk:4 -platform:x86 -target:exe -nologo -out:"$build/MediaContentOracle.exe" \
    -r:"$build/Microsoft.Xna.Framework.dll" -r:"$build/Microsoft.Xna.Framework.Graphics.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.dll" \
    -r:"$build/Microsoft.Xna.Framework.Video.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.AudioImporters.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.VideoImporters.dll" \
    "$here/MediaContentOracle.cs"

rm -rf "$build/out"; mkdir -p "$build/out"
cp -a "$fixtures/." "$build/fixtures/" 2>/dev/null || { mkdir -p "$build/fixtures"; cp -a "$fixtures/." "$build/fixtures/"; }
win_out="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/out" 2>/dev/null)"
win_fix="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/fixtures" 2>/dev/null)"
status=0
timeout "${CNA_MEDIA_ORACLE_TIMEOUT:-900}" \
  env -u WAYLAND_DISPLAY -u DISPLAY WINEPREFIX="$prefix" WINEDEBUG=-all \
  wine "$build/MediaContentOracle.exe" "$win_out" "$win_fix" "$@" || status=$?
if [ "$status" -ne 0 ]; then
    echo "run-media-oracle: driver exited $status; publishing what it recorded before that" >&2
fi

find "$out" -maxdepth 1 -type f -name '*.json' -delete
for f in "$build"/out/*; do
    [ -f "$f" ] || continue
    tr -d '\r' < "$f" > "$out/$(basename "$f")"
done
echo "run-media-oracle: wrote $(ls "$out" | wc -l) files to $out"
