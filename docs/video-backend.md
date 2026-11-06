# Optional FFmpeg video decoding

The XNA MediaPlayer, Video and VideoPlayer declarations remain available independently of
FFmpeg. Runtime decoding is supplied by `modules/video-ffmpeg` (target `cna_video_ffmpeg`,
alias `CNA::VideoFfmpeg`); the decoder-free implementation lives in `modules/media`.
XNB metadata readers live in the content module.

## Selection and dependencies

`CNA_ENABLE_VIDEO` is a three-state option, resolved by `modules/CMakeLists.txt`:

| Value | Configure/runtime behavior |
|---|---|
| OFF | No video FFmpeg link edge; decoder-dependent video operations report NotSupportedException. Independent WaveBank codecs may still require FFmpeg. |
| AUTO (default) | Enables FFmpeg when the toolchain is eligible and all four pkg-config modules are found; otherwise uses the unavailable decoder. |
| ON | Requires an eligible toolchain and all four packages; configure fails rather than silently falling back. |

The packages are libavcodec, libavformat, libavutil and libswresample. CNA's frame conversion does
not require libswscale development headers. The backend target owns the native FFmpeg dependency
and public XNA headers expose none of its types.

Build-time MP3/WMA/WMV importing is a separate module/option, `CNA_ENABLE_MEDIA_PIPELINE`, but its
availability currently reuses the FFmpeg detection controlled by CNA_ENABLE_VIDEO. With VIDEO=OFF,
MEDIA_PIPELINE=AUTO disables those importers and MEDIA_PIPELINE=ON fails configuration. It is not
currently possible to enable that build-time decoder independently through these options.

For a decoder-free configuration in the usual debug tree:

```sh
cmake -S . -B cmake-build-debug -DCNA_ENABLE_VIDEO=OFF -DCNA_ENABLE_WAVEBANK_CODECS=OFF -DCNA_ENABLE_MEDIA_PIPELINE=OFF
cmake --build cmake-build-debug --parallel 8
```

Those settings remove CNA's own FFmpeg edges. Merely using Game or audio-only MediaPlayer in an
FFmpeg-enabled configuration does not guarantee a dependency-free executable: the shared runtime
contains the composed modules, and static link closure depends on what is retained. Inspect the
actual consumer artifact when absence of those libraries matters.

## Decoder-free contract

- The compiled-asset/metadata Video constructor preserves supplied metadata without probing a file.
  VideoContentTypeReader and built-in XNB reader registration remain available.
- Raw local-file construction checks file existence before requiring a decoder: a missing/non-file
  path gives FileNotFoundException; an existing file gives NotSupportedException.
- FromUriEXT resolves local paths/file URIs before the same check. Non-file absolute URIs are
  independently refused with InvalidOperationException; not every URI failure means missing FFmpeg.
- VideoPlayer can be constructed, queried, configured and disposed. Play validates disposal and
  non-null input, then requires the decoder before changing the association/playback state.
- The unavailable AudioDurationProbe returns zero for unknown duration. Song playback still uses
  the selected audio implementation; this is not a promise that every encoded song format works.

The native C API translates NotSupportedException to CNA_RESULT_NOT_SUPPORTED.
cna_video_create_with_metadata remains a metadata route; file probing/playback can require the
decoder. See [c-api/MEDIA.md](c-api/MEDIA.md) for handle/frame lifetime and the precise C contract.

CNA_VIDEO_AVAILABLE is defined on the shared build-configuration surface when FFmpeg is selected;
CNA_FFMPEG_AVAILABLE remains the equivalent legacy internal/test definition.

## Enabled decoding boundary

VideoDecoder refuses a decoded frame whose dimensions differ from those established at Open,
with std::runtime_error. This is covered by a mid-stream resolution-change fixture; the converters
and XNA Video width/height assume a fixed size.

Codec/container/audio-stream coverage depends on the linked FFmpeg build and CNA's converter
support. Enabling the option is not a guarantee for every codec or file, network URI playback,
hardware decoding or a particular renderer's upload behavior.

## Toolchains and evidence

| Target | Current configure policy |
|---|---|
| Linux native | Eligible for pkg-config FFmpeg detection. |
| macOS native | Eligible; requires target-compatible FFmpeg/pkg-config paths. |
| Windows/MSVC, MinGW, Android, Emscripten, iOS | AUTO falls back; ON fails. No target-native FFmpeg integration is wired here. |

The gate is a CMake policy, not a universal upstream FFmpeg platform limitation. Other cross-builds
must likewise supply target-compatible pkg-config resolution; eligibility alone does not prove it.

MEDIA-233 recorded Linux ON/OFF builds, refusal tests and an OFF-artifact dependency inspection.
Older macOS FFmpeg evidence predates this optional split. On 2026-11-01 the current Linux
multi-renderer Media target builds and all 23 availability/decoder tests pass through the private
runner, including corrupt/truncated input and resolution-change cases. This audit does not repeat
the OFF dependency inspection or qualify another native/mobile/browser decoder integration.
