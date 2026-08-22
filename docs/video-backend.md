# Optional FFmpeg video backend

CNA keeps the XNA `MediaPlayer`, `Video`, `VideoPlayer`, video content readers and their public
headers available in every build. FFmpeg decoding is a separate native module,
`cna_video_ffmpeg` (`CNA::VideoFfmpeg`), selected with `CNA_ENABLE_VIDEO`:

| Value | Configure behavior | Runtime video behavior |
|---|---|---|
| `OFF` | Does not probe or link FFmpeg. | File-backed video construction/playback throws `System::NotSupportedException`. |
| `AUTO` | Enables the backend only when all four required FFmpeg pkg-config modules are present. This is the default. | Decodes through FFmpeg when found; otherwise behaves like `OFF`. |
| `ON` | Requires a supported target and all FFmpeg development packages; configuration fails if either condition is unmet. | Decodes through FFmpeg. |

For a game that does not use video, the dependency-free choice is explicit:

```sh
cmake -S . -B build -DCNA_ENABLE_VIDEO=OFF
cmake --build build
```

That build has no `libavcodec`, `libavformat`, `libavutil` or `libswresample` link edge. In
particular, using `Microsoft::Xna::Framework::Game` or audio-only `MediaPlayer` does not bring
FFmpeg into the final executable.

## Stable no-backend contract

The decoder-free build is link-complete, not a reduced-header profile:

- `Video`'s compiled-asset/metadata constructor remains usable and preserves its supplied
  metadata without touching the file.
- `VideoContentTypeReader` and the built-in XNB reader registration remain available, so video
  metadata can be loaded.
- The raw file constructor and `Video::FromUriEXT` still report
  `System::IO::FileNotFoundException` for a missing path. For an existing path, they report
  `System::NotSupportedException` because probing requires the decoder.
- `VideoPlayer` can be constructed, queried, configured and disposed. `Play(non-null-video)`
  reports `System::NotSupportedException` before changing the player's video or playback state.
- FFmpeg-backed audio-duration probing used by `MediaLibrary` returns zero (the existing
  “unknown duration” sentinel) when the optional backend is absent. Song playback itself still
  uses CNA's selected audio backend and is unaffected.

The C ABI maps the same exception to `CNA_RESULT_NOT_SUPPORTED`. Metadata-only
`cna_video_create_with_metadata` remains successful; file probing and playback report the missing
backend deterministically.

`CNA_VIDEO_AVAILABLE` is defined on CNA targets only when the FFmpeg backend was selected.
`CNA_FFMPEG_AVAILABLE` remains as the legacy equivalent for existing internal/test code.

## Platform boundary

The current support and verification boundary is explicit:

| Target | `AUTO` result | Evidence |
|---|---|---|
| Linux native | FFmpeg when installed, fallback otherwise | Both `ON` and `OFF` configurations are built and their video contracts tested by `MEDIA-233`; the `OFF` executable dependency list is checked for absence of all four FFmpeg libraries. |
| macOS native | FFmpeg when Homebrew/pkg-config finds it, fallback otherwise | Existing native macOS CI/history covers the FFmpeg build; the new optional split was not run on macOS by `MEDIA-233`. |
| Windows/MSVC | Fallback | No target-native FFmpeg integration or decode verification yet (`MEDIA-192`). |
| MinGW | Fallback | No target-native FFmpeg integration or decode verification yet (`MEDIA-193`). |
| Android | Fallback | No target-native FFmpeg integration or decode verification yet (`MEDIA-194`). |
| Emscripten | Fallback | No target-native FFmpeg integration or decode verification yet (`MEDIA-195`). |
| iOS | Fallback | Host macOS FFmpeg is deliberately not used for an iOS cross-build; no target-native integration or decode verification yet. |

Requesting `ON` on a fallback-only toolchain fails at configure time. Adding real target-native
FFmpeg integrations there remains separate work and does not affect availability of the public XNA
types.
