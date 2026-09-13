#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xnapipeline_parity.md XNAPP-281: the three output families whose fixtures cannot be
# committed with the rest, loaded in a genuine XNA 4.0 runtime.
#
# Every fixture in `tests/assets/xnb/cna/windows/` is written by `cna_xnb_interop_fixtures`, which
# is hermetic: it needs nothing but CNA. These three are not, each for its own reason.
#
#   * **Effect** -- its payload is Direct3D 9 bytecode, and producing that needs Microsoft's legacy
#     `fxc`, which cannot be committed and is not on every machine.
#   * **Song** and **Video** -- an XNB of either is a header plus an *external* media file, so the
#     fixture is two files whose relationship the build makes; a generated corpus of single files
#     cannot express it.
#
# So these are built rather than shipped: CNA's own committed sources, through CNA's importers,
# processors and writers, and then the same harness the committed corpus runs through.
#
#   CNA_CONTENT           the cna-content binary (default cmake-build-debug/cna-content)
#   CNA_FXC               fxc.exe (default: the June 2010 SDK's, where the samples tree has it)
#   CNA_FXC_LAUNCHER      how to run it (default wine)
#   CNA_XNA40_WINEPREFIX  Wine prefix with .NET 4.0 and the XNA runtime
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
content="${CNA_CONTENT:-$repo/cmake-build-debug/cna-content}"
fxc="${CNA_FXC:-/rv/tmp/samples/_tools/directx-sdk-june-2010/extract/DXSDK/Utilities/bin/x86/fxc.exe}"
launcher="${CNA_FXC_LAUNCHER:-wine}"
out="$repo/build/xna-interop-built"

[ -x "$content" ] || { echo "run-built-families-interop: no cna-content at $content" >&2; exit 77; }
[ -f "$fxc" ]     || { echo "run-built-families-interop: no fxc at $fxc" >&2; exit 77; }

rm -rf "$out"; mkdir -p "$out"
CNA_FXC="$fxc" CNA_FXC_LAUNCHER="$launcher" \
    "$content" build "$repo/tests/assets/xna40/source/fx_minimal.fx" \
    -o "$out/effect_fx_minimal.xnb" --format xnb --quiet
"$content" build "$repo/tests/assets/xna40/media/mp3_mono_44100_128k.mp3" \
    -o "$out/song_mp3.xnb" --format xnb --quiet
"$content" build "$repo/tests/assets/xna40/media/wmv_64x48_15fps_silent.wmv" \
    -o "$out/video_wmv.xnb" --format xnb --quiet

# What each source declares, and where a number came from.
#   Effect: `fx_minimal.fx` itself -- one technique `Only`, one pass `Single`, one matrix parameter.
#   Song:   548 ms, which is what XNA's own importer measured for this file
#           (tests/reference/xna40/media/media-content-oracle.json, mp3/mp3_mono_44100_128k.mp3,
#           durationTicks=5480000) -- not the 500 ms the encoder was asked for.
#   Video:  64x48 at 15 fps, which is what the file was generated as.
cat > "$out/effect_fx_minimal.expected.json" <<'JSON'
{"rootReader":"Microsoft.Xna.Framework.Content.EffectReader",
 "root":{"techniqueCount":1,"parameterCount":1,"techniqueName":"Only","passName":"Single",
         "parameterName":"WorldViewProjection"}}
JSON
cat > "$out/song_mp3.expected.json" <<'JSON'
{"rootReader":"Microsoft.Xna.Framework.Content.SongReader","root":{"durationMs":548}}
JSON
cat > "$out/video_wmv.expected.json" <<'JSON'
{"rootReader":"Microsoft.Xna.Framework.Content.VideoReader",
 "root":{"width":64,"height":48,"framesPerSecond":15}}
JSON
cat > "$out/fixtures.json" <<'JSON'
{"producer":"cna-content, from CNA's own committed sources",
 "targetPlatform":"windows","containerVersion":5,"graphicsProfile":"Reach","compression":"none",
 "readerNameStyle":"xna40",
 "note":"Built rather than committed. An Effect's payload is Direct3D 9 bytecode and needs Microsoft's legacy fxc; a Song and a Video are each a header plus an external media file, which a corpus of single generated files cannot express. The sources, the importers, the processors and the writers are all CNA's.",
 "fixtures":[
  {"name":"effect_fx_minimal","file":"effect_fx_minimal.xnb","expectation":"effect_fx_minimal.expected.json",
   "purpose":"Effect compiled from CNA's own fx_minimal.fx: one technique, one pass, shader model 2.0, one matrix parameter. A correct runtime must load it as an Effect and report those names."},
  {"name":"song_mp3","file":"song_mp3.xnb","expectation":"song_mp3.expected.json",
   "purpose":"Song built from an MP3, whose XNB is a name and a duration and whose media file is deployed beside it. A correct runtime must report the duration XNA's own importer measured for the same file, 548 ms."},
  {"name":"video_wmv","file":"video_wmv.xnb","expectation":"video_wmv.expected.json",
   "purpose":"Video built from a WMV, whose XNB is metadata and whose media file is deployed beside it. A correct runtime must report Width 64, Height 48 and FramesPerSecond 15."}]}
JSON

exec "$here/run-interop-harness.sh" "$out"
