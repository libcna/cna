# XNA 4.0 media source parity — MP3, WMA and WMV

> What genuine Microsoft XNA Game Studio 4.0 does with the three source formats it reads through
> Windows Media, measured black-box under Wine by
> `tools/xna-pipeline-oracle/media/run-media-oracle.sh` into
> `tests/reference/xna40/media/media-content-oracle.json`. Task rows:
> `plans/plan_xnapipeline_parity.md` `XNAPP-201`, `XNAPP-202`, `XNAPP-220`, `XNAPP-136`, `XNAPP-043`.
>
> Nothing here was read out of Microsoft's code. Every line is either an attribute the assemblies
> publish, a value a public property answered, or the text of an exception a public call threw.

## 1. What the three importers declare

| Importer | Extension | DisplayName | DefaultProcessor | CacheImportedData | Output |
|---|---|---|---|---|---|
| `Mp3Importer` | `.mp3` | MP3 Audio File - XNA Framework | `SongProcessor` | false | `AudioContent` |
| `WmaImporter` | `.wma` | WMA Audio File - XNA Framework | `SongProcessor` | false | `AudioContent` |
| `WmvImporter` | `.wmv` | WMV Video File - XNA Framework | `VideoProcessor` | false | `VideoContent` |

## 2. `Mp3Importer` — measured

`Import` answers an `AudioContent` whose `FileType` is `AudioFileType.Mp3`, whose `FileName` is the
path it was given, whose `Identity` is **null**, and which adds **no dependency** to the importer
context and writes nothing to its logger.

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

Three things this settles:

1. **The channel count survives, the sample rate does not.** Every source, from 8000 Hz to
   48000 Hz and across all three MPEG versions, is reported as 44100 Hz 16-bit PCM; only mono
   versus stereo comes through. `WavImporter` reports the source's own rate, so this normalization
   belongs to the MP3 route, not to `AudioContent`.
2. **`Duration` is the stream's own wall-clock length**, truncated to whole milliseconds — the same
   truncation `AudioContent.Duration` applies to a WAV. It is derived from the source's frame count
   at the source's rate, so it is unaffected by the reported 44100, and it includes the encoder
   delay and padding frames the file actually carries (a half-second tone is 548 ms, not 500 ms).
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
| an MP3 given to `WmaImporter` | `InvalidContentException: Could not read the audio data from file "{name}".` |

The missing-file message carries an **unformatted `{0}`** — the same defect `WavImporter` has, and
the video route repeats it (§4). The importers are **content-driven, not extension-driven**: a WAV
renamed `.mp3` is refused, and an MP3 renamed `.wav` is refused, each by the importer whose format
it is not. The one exception is `WmaImporter` reading an MP3, which gets far enough to open the
file and fails on the audio instead.

## 3. `WmaImporter` — declared, not measurable here

Every WMA source in the corpus — wmav2 and wmav1, mono and stereo, 22050 and 44100 — is refused
with `Failed to open file {name}. Ensure the file is a valid audio file and is not DRM protected.`,
and a missing one with the same unformatted `Could not locate audio file "{0}".` This is the
environment, not XNA: see §6.

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

So the **importer checks existence and the constructor does not**: the same missing file is a
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

## 5. `SongProcessor` — the encoder's own refusal

Handed an `AudioContent` imported from an MP3, `SongProcessor.Process` answers

```text
InvalidContentException: Could not convert audio file {name} to WindowsMedia format.
```

which is the sentence XNA gives when its Windows Media encoder cannot produce the song. It is the
message CNA gives for the same failure.

## 6. Environment boundary — what could not be measured, and exactly why

The oracle runs the genuine assemblies under Wine 10.0. The XNA pipeline reaches Windows Media
through `XnaMediaHelper_1.dll`, and how far that gets depends on what the prefix provides:

| Route | State | Component that decides it |
|---|---|---|
| MP3 header, format, duration | **measured** | needs `l3codecx.ax`, the DirectShow MPEG Layer-3 decoder, in the prefix (`winetricks l3codecx`); with Wine's own stub the call is `InvalidContentException: Could not read the audio data from file "{name}".` |
| MP3 sample data (`AudioContent.Data`) | **not measurable** | reading the samples still answers `Could not read the audio data`; adding `winetricks directshow` and `winetricks mf` changes nothing |
| WMA, any variant | **not measurable** | the file is not opened at all; the genuine Windows Media Format runtime is not in the prefix |
| WMV, 320x240 | **not measurable** | `Video file … is invalid` |
| WMV, 64x48 | **not measurable** | `SEHException: External component has thrown an exception` — a native fault inside the helper |
| Media Foundation generally | **absent** | Wine 10.0 aborts on `mfplat.dll.MFCreateVideoMediaType`, which is unimplemented |

`winetricks wmp9` was tried and **reverted**: it leaves `XnaMediaHelper_1.dll` unable to load at all
(`DllNotFoundException … Module not found`), which is worse than the stub. The prefix was restored
from a full copy of `system32` plus its three registry hives, and the audio oracle
(`tools/xna-pipeline-oracle/audio/`) reproduces its committed measurements byte for byte after the
restore, which is how the restore was verified.

To reproduce the MP3 measurements in a fresh prefix:

```bash
WINEPREFIX=~/.wine-cna-xna40 winetricks -q l3codecx
bash tools/xna-pipeline-oracle/media/make-media-fixtures.sh
bash tools/xna-pipeline-oracle/media/run-media-oracle.sh
```

## 7. What CNA does with the parts XNA could not be asked about here

Where the measurement exists, CNA reproduces it. Where the environment refused, CNA implements the
format itself and says so in the parity map rather than claiming a measured behaviour:

* the **MP3 sample data** is decoded by CNA's own build-time decoder to the 44100 Hz 16-bit PCM
  shape the measured `Format` describes, because that is the format XNA reported it would produce;
* **WMA** and **WMV** are read by the same build-time decoder, and the fields CNA fills are the ones
  the official documentation names for each property;
* every **refusal text** above is reproduced exactly, including the unformatted `{0}` and the
  difference between the importer's `FileNotFoundException` and the constructor's
  `InvalidContentException` for the same missing file.
