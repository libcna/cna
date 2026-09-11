#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xnapipeline_parity.md XNAPP-201, XNAPP-202, XNAPP-220: the synthetic media corpus for the
# three importers XNA reads through Windows Media -- Mp3Importer, WmaImporter and WmvImporter.
#
# Every file here is generated from a deterministic synthetic signal (a sine tone, a test pattern)
# by the host's FFmpeg. Nothing is downloaded and nothing is third-party content; the provenance
# file beside the fixtures records the exact command that made each one, so a later session can
# regenerate them with the same FFmpeg.
#
#   $1  output directory (default tests/assets/xna40/media)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
out="${1:-$repo/tests/assets/xna40/media}"
command -v ffmpeg >/dev/null || { echo "make-media-fixtures: ffmpeg not found" >&2; exit 3; }

mkdir -p "$out"
prov="$out/PROVENANCE.md"
{
  echo "# Synthetic media corpus (generated)"
  echo
  echo 'Written by `tools/xna-pipeline-oracle/media/make-media-fixtures.sh`. Every file is'
  echo 'synthesized from a mathematical signal by FFmpeg; none is third-party content, and none'
  echo 'was downloaded. Regenerate with the same FFmpeg to get the same bytes.'
  echo
  echo '```'
  ffmpeg -hide_banner -version 2>/dev/null | head -1
  echo '```'
  echo
  echo "| File | How it was made |"
  echo "|---|---|"
} > "$prov"

# gen <name> <ffmpeg args...>   -- the args are recorded verbatim, with $out folded away.
gen() {
    local name="$1"; shift
    ffmpeg -hide_banner -loglevel error -y "$@" "$out/$name"
    echo "| \`$name\` | \`ffmpeg ${*//$out\//} $name\` |" >> "$prov"
}
note() { echo "| \`$1\` | $2 |" >> "$prov"; }

# --- the shared PCM sources -------------------------------------------------------------------
# A 440 Hz tone, and a two-channel one whose channels differ, so a channel-count or channel-order
# mistake shows up in the decoded samples and not only in the header.
gen tone_mono_44100.wav   -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=0.5" -ac 1 -c:a pcm_s16le
gen tone_stereo_44100.wav -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=0.5" \
                          -f lavfi -i "sine=frequency=660:sample_rate=44100:duration=0.5" \
                          -filter_complex "[0:a][1:a]join=inputs=2:channel_layout=stereo[a]" -map "[a]" -c:a pcm_s16le
gen tone_mono_22050.wav   -f lavfi -i "sine=frequency=440:sample_rate=22050:duration=0.5" -ac 1 -c:a pcm_s16le

# --- MP3 --------------------------------------------------------------------------------------
# -write_xing 0 with -id3v2_version 0 keeps the file to bare MPEG frames, so the first measurement
# is of the audio and not of a tag reader.
gen mp3_mono_44100_128k.mp3   -i "$out/tone_mono_44100.wav"   -c:a libmp3lame -b:a 128k -write_xing 0 -id3v2_version 0
gen mp3_stereo_44100_192k.mp3 -i "$out/tone_stereo_44100.wav" -c:a libmp3lame -b:a 192k -write_xing 0 -id3v2_version 0
gen mp3_mono_22050_64k.mp3    -i "$out/tone_mono_22050.wav"   -c:a libmp3lame -b:a 64k  -write_xing 0 -id3v2_version 0
# The same audio with a Xing/LAME header and an ID3v2 tag, which is what a real .mp3 carries.
gen mp3_mono_44100_tagged.mp3 -i "$out/tone_mono_44100.wav"   -c:a libmp3lame -b:a 128k -id3v2_version 3 \
                              -metadata title=CnaTone -metadata artist=CNA
# Variable bitrate, whose duration cannot be read off the first frame's bitrate.
gen mp3_mono_44100_vbr.mp3    -i "$out/tone_mono_44100.wav"   -c:a libmp3lame -q:a 4 -id3v2_version 0
# One file per MPEG version, so what the importer reports as the sample rate can be told apart
# from what the source carries: 48000 and 32000 are MPEG-1, 24000 and 16000 MPEG-2, 8000 MPEG-2.5.
gen mp3_mono_48000_128k.mp3   -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=0.5" -ac 1 -c:a libmp3lame -b:a 128k -write_xing 0 -id3v2_version 0
gen mp3_mono_32000_128k.mp3   -f lavfi -i "sine=frequency=440:sample_rate=32000:duration=0.5" -ac 1 -c:a libmp3lame -b:a 128k -write_xing 0 -id3v2_version 0
gen mp3_mono_24000_64k.mp3    -f lavfi -i "sine=frequency=440:sample_rate=24000:duration=0.5" -ac 1 -c:a libmp3lame -b:a 64k  -write_xing 0 -id3v2_version 0
gen mp3_mono_16000_64k.mp3    -f lavfi -i "sine=frequency=440:sample_rate=16000:duration=0.5" -ac 1 -c:a libmp3lame -b:a 64k  -write_xing 0 -id3v2_version 0
gen mp3_mono_8000_32k.mp3     -f lavfi -i "sine=frequency=440:sample_rate=8000:duration=0.5"  -ac 1 -c:a libmp3lame -b:a 32k  -write_xing 0 -id3v2_version 0
gen mp3_stereo_22050_96k.mp3  -f lavfi -i "sine=frequency=440:sample_rate=22050:duration=0.5" -f lavfi -i "sine=frequency=660:sample_rate=22050:duration=0.5" \
                              -filter_complex "[0:a][1:a]join=inputs=2:channel_layout=stereo[a]" -map "[a]" -c:a libmp3lame -b:a 96k -write_xing 0 -id3v2_version 0
# A two-second one, so a duration read cannot pass by matching a constant.
gen mp3_mono_44100_2s.mp3     -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=2.0" -ac 1 -c:a libmp3lame -b:a 128k -write_xing 0 -id3v2_version 0

# --- WMA --------------------------------------------------------------------------------------
gen wma_mono_44100.wma    -i "$out/tone_mono_44100.wav"   -c:a wmav2 -b:a 128k
gen wma_stereo_44100.wma  -i "$out/tone_stereo_44100.wav" -c:a wmav2 -b:a 192k
gen wma_mono_22050.wma    -i "$out/tone_mono_22050.wav"   -c:a wmav2 -b:a 64k
gen wma_v1_mono_44100.wma -i "$out/tone_mono_44100.wav"   -c:a wmav1 -b:a 128k

# --- WMV --------------------------------------------------------------------------------------
# testsrc2 is deterministic. 64x48 keeps the corpus small; the second is 320x240 so a dimension
# read cannot pass by accident on an almost-square frame.
gen wmv_64x48_15fps_silent.wmv   -f lavfi -i "testsrc2=size=64x48:rate=15:duration=1" -c:v wmv2 -b:v 200k -an
gen wmv_320x240_30fps_stereo.wmv -f lavfi -i "testsrc2=size=320x240:rate=30:duration=1" \
                                 -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=1" \
                                 -c:v wmv2 -b:v 300k -c:a wmav2 -b:a 128k -ac 2
gen wmv_v1_64x48.wmv -f lavfi -i "testsrc2=size=64x48:rate=10:duration=1" -c:v wmv1 -b:v 200k -an

# --- malformed / refusal corpus ----------------------------------------------------------------
: > "$out/empty.mp3"; note empty.mp3 "zero bytes"
: > "$out/empty.wma"; note empty.wma "zero bytes"
: > "$out/empty.wmv"; note empty.wmv "zero bytes"
head -c 400 "$out/mp3_mono_44100_128k.mp3"    > "$out/truncated.mp3"
note truncated.mp3 "first 400 bytes of \`mp3_mono_44100_128k.mp3\`"
head -c 400 "$out/wma_mono_44100.wma"         > "$out/truncated.wma"
note truncated.wma "first 400 bytes of \`wma_mono_44100.wma\`"
head -c 600 "$out/wmv_64x48_15fps_silent.wmv" > "$out/truncated.wmv"
note truncated.wmv "first 600 bytes of \`wmv_64x48_15fps_silent.wmv\`"
cp "$out/tone_mono_44100.wav" "$out/actually_wav.mp3"
note actually_wav.mp3 "\`tone_mono_44100.wav\` renamed: bytes that contradict the extension"
cp "$out/mp3_mono_44100_128k.mp3" "$out/actually_mp3.wav"
note actually_mp3.wav "\`mp3_mono_44100_128k.mp3\` renamed"
printf 'not media at all\n' > "$out/garbage.mp3"
note garbage.mp3 "seventeen bytes of text"

echo "make-media-fixtures: wrote $(ls "$out" | wc -l) files"
