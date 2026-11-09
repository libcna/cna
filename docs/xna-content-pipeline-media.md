# XNA 4.0 media source parity — MP3, WMA and WMV

This page separates retained Microsoft XNA measurements from current CNA implementation policy.
The black-box oracle used the genuine Game Studio 4.0 assemblies under the recorded Wine prefix:
[media-content-oracle.json](../tests/reference/xna40/media/media-content-oracle.json),
produced by `tools/xna-pipeline-oracle/media/MediaContentOracle.cs` without inspecting IL.
It contains **56 named records**, including failed/unavailable operations; they are not 56
successful parity tests. The JSON records CLR version and pipeline assembly identity, but does
not by itself pin every helper/codec/fixture byte or reproduce the host environment.

The work is XNAPP-201/202/220/136/043/332. Current native routes are in
[content-pipeline.md](content-pipeline.md), XNB output scope in
[xnb-interoperability.md](xnb-interoperability.md), and unresolved issues in
[KNOWN_ISSUES.md](../KNOWN_ISSUES.md). Missing Windows Media facilities under Wine do not define valid-file
refusal behaviour on native Windows.

## 1. What the three importers declare

| Importer | Extension | DisplayName | DefaultProcessor | CacheImportedData | Output |
|---|---|---|---|---|---|
| `Mp3Importer` | `.mp3` | MP3 Audio File - XNA Framework | `SongProcessor` | false | `AudioContent` |
| `WmaImporter` | `.wma` | WMA Audio File - XNA Framework | `SongProcessor` | false | `AudioContent` |
| `WmvImporter` | `.wmv` | WMV Video File - XNA Framework | `VideoProcessor` | false | `VideoContent` |

## 2. `Mp3Importer` — measured

In the recorded direct Mp3Importer calls, Import answers AudioContent whose `FileType` is `AudioFileType.Mp3`, whose `FileName` is the
path it was given, whose `Identity` is **null**, and which adds **no dependency** to the importer
context and writes nothing to its logger. The canonical build engine separately tracks its
primary source; this is not a claim that compiled MP3 content has no build dependency.

The `Format` it answers is **not** the source's own format. It is the format of the PCM the
decoder will produce, and across every MPEG version and every source rate the corpus carries it is
the same shape:

| Source | Reported channels | Reported sample rate | Bits | BlockAlign | AverageBytesPerSecond | Duration |
|---|---:|---:|---:|---:|---:|---:|
| mono 44100 CBR 128k | 1 | **44100** | 16 | 2 | 88200 | 548 ms |
| stereo 44100 CBR 192k | 2 | **44100** | 16 | 4 | 176400 | 548 ms |
| mono 44100 VBR | 1 | **44100** | 16 | 2 | 88200 | 548 ms |
| mono 44100 CBR 128k, Xing + ID3v2 | 1 | **44100** | 16 | 2 | 88200 | 574 ms |
| mono 48000 (MPEG-1) | 1 | **44100** | 16 | 2 | 88200 | 528 ms |
| mono 32000 (MPEG-1) | 1 | **44100** | 16 | 2 | 88200 | 540 ms |
| mono 22050 (MPEG-2) | 1 | **44100** | 16 | 2 | 88200 | 574 ms |
| stereo 22050 (MPEG-2) | 2 | **44100** | 16 | 4 | 176400 | 574 ms |
| mono 24000 (MPEG-2) | 1 | **44100** | 16 | 2 | 88200 | 552 ms |
| mono 16000 (MPEG-2) | 1 | **44100** | 16 | 2 | 88200 | 576 ms |
| mono 8000 (MPEG-2.5) | 1 | **44100** | 16 | 2 | 88200 | 648 ms |
| mono 44100, two seconds | 1 | **44100** | 16 | 2 | 88200 | 2037 ms |

These twelve fixtures establish:

1. **The channel count survives, the sample rate does not.** Every source, from 8000 Hz to
   48000 Hz and across all three MPEG versions, is reported as 44100 Hz 16-bit PCM; only mono
   versus stereo comes through. `WavImporter` reports the source's own rate, so this normalization
   belongs to the MP3 route, not to `AudioContent`.
2. Duration is recorded in whole milliseconds and includes delay/padding in these files
   (the half-second tone reports 548 ms). The changed reported PCM rate does not change that
   recorded duration. This is a fixture observation, not a universal duration formula for all
   MP3 containers, headers or malformed streams.
3. **`LoopStart` and `LoopLength` are both 0.** A WAV that names no loop answers `LoopStart = 0`
   and `LoopLength =` the whole sound; an MP3 answers zero for both.

The `NativeWaveFormat` is the eighteen-byte `WAVEFORMATEX` — `wFormatTag = 1`, the channel count,
44100, the byte rate, the block align, 16 bits and a `cbSize` of 0 — the same shape and the same
included `cbSize` the WAV route answers for PCM.

### Refusals

| Source | Exception and message |
|---|---|
| a file that is not there | `FileNotFoundException: Could not locate audio file "{0}".` |
| zero bytes, truncated, text, or a WAV named `.mp3` | `InvalidContentException: Failed to open file {name}. Ensure the file is a valid audio file and is not DRM protected.` |
| an MP3 named `.wav`, given to `WavImporter` | the same `Failed to open file …` sentence |
| an MP3 given to WmaImporter | Import returns AudioContent labelled Wma with MP3-derived metadata; Data access then throws Could not read the audio data from file... in this Wine environment |

The missing-file message carries an **unformatted `{0}`** — the same defect `WavImporter` has, and
the video route repeats it (§4). The format-specific refusal witnesses include a WAV
renamed .mp3 and an MP3 renamed .wav. The WmaImporter/MP3 record has a different call boundary:
metadata import succeeds before sample access fails. Do not relabel that result as an Import
exception or infer a universal extension-sniffing rule from these cases.

## 3. Valid WMA files — blocked in the recorded prefix

Every WMA source in the corpus — wmav2 and wmav1, mono and stereo, 22050 and 44100 — is refused
with `Failed to open file {name}. Ensure the file is a valid audio file and is not DRM protected.`,
and a missing one with the same unformatted Could not locate audio file "{0}".
The valid-file failures are attributed to the prefix's missing Windows
Media Format facilities (§6); it does not establish a valid-WMA refusal contract on native Windows.
CNA reads supported WMA files through FFmpeg instead of reproducing those host failures.

Current decoder code also contains a WMA priming policy informed by the separately recorded
Platformer source/prebuilt-XNB comparison (XNAPP-332): it allows FFmpeg to discard decoder priming,
unlike MP3's retained delay/padding. That narrower output comparison is different evidence from
successfully calling the WmaImporter oracle over this whole WMA matrix.

## 4. `VideoContent` and `WmvImporter` — measured where the environment allows

`VideoContent(string filename)` is **eager**: it opens and probes the file inside the constructor,
so every refusal below comes from construction and not from a later property read.

| Case | Result |
|---|---|
| `new VideoContent(null)` | `InvalidContentException: Video file  is invalid. Please make sure that the video is not DRM protected and is a valid single-pass CBR encoded video file.` |
| `new VideoContent("")` | the same sentence, with the same empty name |
| `new VideoContent(<missing path>)` | the same sentence — **not** a `FileNotFoundException` |
| `new VideoContent(<a WAV>)` | the same sentence |
| `WmvImporter.Import(<missing path>)` | `FileNotFoundException: Could not locate video file "{0}".` |
| `WmvImporter.Import(<zero bytes / truncated / an MP3>)` | the `Video file … is invalid` sentence |

The importer performs a separate existence check, while the constructor maps probe failure to
its invalid-video exception: the same missing file is a
`FileNotFoundException` through `WmvImporter` and an `InvalidContentException` through
`VideoContent` directly. A null filename is **not** an `ArgumentNullException`; it reaches the
message as an empty name.

`VideoProcessor`:

| Case | Result |
|---|---|
| default `VideoSoundtrackType` | `Music` |
| `Process(null, context)` | `ArgumentNullException`, parameter name `input` |

`VideoSoundtrackType` is `Microsoft.Xna.Framework.Media.VideoSoundtrackType`, the runtime enum, with
`Music = 0`, `Dialog = 1`, `MusicAndDialog = 2`.

## 5. SongProcessor — recorded host failure versus current CNA encoding

The stored songprocessor/mp3 call failed under Wine with:

```text
InvalidContentException: Could not convert audio file {name} to WindowsMedia format.
```

That is an observed encoder failure, not a requirement that valid songs always fail. Current
XNA-shaped CNA SongProcessor writes a .wma beside the requested output, registers it through
AddOutputFile, and returns SongContent with its filename/duration. BuildTimeMedia uses FFmpeg's
WMAV2 encoder when available. CNA's quality ladder is Best=192000, Medium=128000 and Low=96000
bits/s; it is an implemented policy, not a measured reproduction of Microsoft's encoder.
The existing tests decode CNA's result and check its shape/rate/quality choices, not byte identity
with a successful genuine Windows Media encode.

This differs from the native convention-only Song route: that route keeps/deploys the supplied
streaming media instead of promising a WMA transcode. Project importer/processor selection can
choose compressed SoundEffect input or WAV Song input; see the native guide.

## 6. Environment boundary — what could not be measured, and exactly why

The retained investigation ran the genuine assemblies under Wine 10.0. The XNA pipeline reaches Windows Media
through `XnaMediaHelper_1.dll`, and how far that gets depends on what the prefix provides:

| Route | State | Component that decides it |
|---|---|---|
| MP3 header, format, duration | **measured** | needs `l3codecx.ax`, the DirectShow MPEG Layer-3 decoder, in the prefix (`winetricks l3codecx`); with Wine's own stub the call is `InvalidContentException: Could not read the audio data from file "{name}".` |
| MP3 sample data (`AudioContent.Data`) | **not measurable** | reading the samples still answers `Could not read the audio data`; adding `winetricks directshow` and `winetricks mf` changes nothing |
| WMA, any variant | **not measurable** | the file is not opened at all; the genuine Windows Media Format runtime is not in the prefix |
| WMV, 320x240 | **not measurable** | `Video file … is invalid` |
| WMV, 64x48 | **not measurable** | `SEHException: External component has thrown an exception` — a native fault inside the helper |
| Media Foundation generally | **absent** | Wine 10.0 aborts on `mfplat.dll.MFCreateVideoMediaType`, which is unimplemented |

An attempt to install wmp9 into the prefix broke helper loading and was reverted, so do not modify a
working oracle prefix that way; none of this is a native-Windows qualification.

The committed fixtures are synthetic FFmpeg output with
[provenance](../tests/assets/xna40/media/PROVENANCE.json). Read/compare those bytes for ordinary
verification. Regenerating through make-media-fixtures.sh uses the host's FFmpeg and can change
encoded bytes; do it deliberately in a separate destination, with tool versions recorded.

To rerun the optional Microsoft oracle, use legally available assemblies and a separately
prepared prefix (including its decoder/helper prerequisites), a new output directory, and the
private runner:

```bash
media_oracle_output=$(mktemp -d /tmp/cna-media-oracle.XXXXXX)
tools/platform/run_gpu_tests_private.sh --exec env \
  CNA_XNA40_REFERENCES=/path/to/xna40/references \
  CNA_XNA40_WINEPREFIX=/path/to/prepared-xna40-prefix \
  bash tools/xna-pipeline-oracle/media/run-media-oracle.sh "$media_oracle_output"
```

CNA_XNA40_NATIVE and CNA_XNA40_MEDIA_HELPER can select the matching native libraries.
The driver compiles/copies dependencies into the ignored build/xna-pipeline-oracle/media directory;
the script clears DISPLAY/WAYLAND_DISPLAY for its Wine call. No prefix installation or fresh
Microsoft oracle was run during this documentation audit.

**Runner status and completeness:** run-media-oracle.sh publishes available partial records, then
returns the driver's status. Failures remain failures after publication, including timeout status
124 and signal termination. It replaces JSON files in the chosen output directory; use a fresh
destination. A successful exit still requires checking case identities/counts, explicit SKIPPED
records and diagnostics before accepting complete evidence.

`python3 tools/xna-pipeline-oracle/media/test_media_oracle_runner.py` exercises the real wrapper
in isolated temporary trees with synthetic compiler/Wine commands. Seven cases cover success,
ordinary failure with and without reports, timeout, signal termination, compiler failure and path
translation failure (CNA-FIX-L01). These tests do not execute Microsoft media code or qualify the
prefix's decoders.

## 7. Current CNA policy and remaining gaps

Build-time media is controlled by CNA_ENABLE_MEDIA_PIPELINE=AUTO|ON|OFF in
modules/content-pipeline/CMakeLists.txt. AUTO uses available FFmpeg libraries, ON requires them,
and OFF omits this build-time decoder/encoder service. This is independent of the optional runtime
Video backend. Disabled services report their absence instead of claiming a valid file is corrupt.

- The XNA-shaped MP3 route eagerly decodes through FFmpeg to 44100 Hz PCM16 with source channels,
  retains delay/padding policy and truncates duration to milliseconds. The twelve recorded format
  witnesses are checked directly; duration assertions allow **30 ms**, so this is not exact
  equality for all MP3s. The Wine record has no usable PCM oracle; CNA's decoded sample bytes are
  not thereby Microsoft-decoder parity.
- Valid supported WMA/WMV inputs are decoded/probed by CNA even though the historical Wine matrix
  could not produce positive XNA results. WMA's 44100 Hz output policy and valid-video field tests
  establish current CNA behaviour over named inputs, not full measured native-XNA equivalence.
- Missing/malformed-input tests retain the measured exception text and importer-versus-constructor
  distinctions where exercised. Availability failures can add an actionable missing-service reason.
- **Unresolved WmaImporter/MP3 call-stage difference:** the retained XNA/Wine record returns
  metadata labelled Wma and fails on Data; CNA's format guard rejects the MP3 during Import.
  XnaWmaImporter.RefusalsMatchXna covers missing/empty/truncated WMA, not that cross-format case.
  Native-Windows measurements with a working WMA positive control are needed before interpreting
  the Wine-specific result as the general XNA contract.

Streaming Song/Video metadata does not prove that a runtime/device can decode or play every source,
nor does constructing a VideoContent decode a video frame. Native XNB target/byte-order restrictions
remain in the interoperability guide; ordinary C++ processor tests do not qualify console playback.

## 8. Verification

The focused media/audio/processor cases in `CnaContentPipelineTests` and `CnaContentTests` (run
privately, for example on OPENGLES3 with dummy audio) include actual WMA encode/decode checks; the
`cna_content_tool` CLI cases cover processor-selected audio reading, asset-relative song paths and
convention-route defaults. The twelve MP3 metadata rows of §2 correspond to records in the
56-record JSON and to current assertions. The synthetic runner probe tests failure propagation, not
Microsoft media execution. None of these is a native-Windows, sanitizer or runtime-playback
qualification.
