# XMA encoder boundary and measured XNA fixture (XNAPP-262)

CNA ships no XMA encoder. Its stable unavailable diagnostic marker is
`CNA::Content::Pipeline::XmaEncoderUnavailableSentence`:

> **XMA ENCODER EXTERNALLY UNAVAILABLE**

It is not a statement that every other pipeline feature is fully compatible. See
[xna-content-pipeline-parity-report.md](xna-content-pipeline-parity-report.md) for the matrix.

## What the blocker establishes

The repository contains an external-process encoder seam, not a built-in XMA bitstream encoder.
As the runtime's unavailable reason states, XMA is Microsoft's own Xbox 360 codec with no public
specification sufficient to implement a conforming encoder, no encoder licensable to the project,
and none in FFmpeg, which decodes xma1 and xma2 and encodes neither. Container documentation and
the committed oracle bytes do not establish a complete encoding algorithm either.

CNA does not bundle Microsoft's SDK encoder; an explicitly attached tool must be supplied under its
own applicable terms. This page does not determine those terms.

## Measured fixture, not a general encoding formula

The committed differential corpus contains one 44100 Hz mono 16-bit source with 22050 frames,
built by genuine XNA for three targets:

| Field | Windows | Windows Phone | Xbox 360 |
|---|---|---|---|
| Format tag | 1 (PCM) | 1 (PCM) | 0x0166 (XMA2, read big-endian) |
| Sample rate | 44100 | 44100 | 44032 |
| Average bytes/second | 88200 | 88200 | 12200 |
| Block align / bits | 2 / 16 | 2 / 16 | 2 / 16 |
| Format extension | none | none | 34 bytes |
| Payload | 44100 bytes | 44100 bytes | 6144 bytes |
| Loop start / length | 0 / 22050 | 0 / 22050 | 384 / 22016 |
| Duration | 500 ms | 500 ms | 500 ms |

The Xbox fixture is
`tests/reference/xna40/differential/audio_wav_soundeffect_xbox360.xnb`.
Its format block is big-endian: a little-endian misreading of tag bytes gives 0x6601.
The XMA2 extension fields are:

| Field | Width | Value |
|---|---|---|
| NumStreams | WORD | 1 |
| ChannelMask | DWORD | 1 |
| SamplesEncoded | DWORD | 22528 |
| BytesPerBlock | DWORD | 32768 |
| PlayBegin | DWORD | 0 |
| PlayLength | DWORD | 22050 |
| LoopBegin | DWORD | 384 |
| LoopLength | DWORD | 22016 |
| LoopCount | BYTE | 0 |
| EncoderVersion | BYTE | 4 |
| BlockCount | WORD | 1 |

The XmaEncoderService fixture test asserts the big-endian fields and Windows PCM comparison.
The independent `tools/xnb/xnb_conformance.py` parser validates the XNB/container object graph.

These observed values align several fields to 128 samples while PlayLength retains the source
length. One source does not establish a universal sample-rate-rounding rule, exact padding or
loop-quantization algorithm, or a maximum authored-loop displacement for all encoder settings.
Preserve the observations and expand the genuine corpus before implementing those rules.
Microsoft's [XAudio2 buffer reference](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/ns-xaudio2-xaudio2_buffer)
documents XMA buffer alignment constraints, separately from this fixture's measured metadata.

## Current CNA routes

The built-in WAV sound-effect route (`CNA.SoundEffectProcessor`) writes PCM for Windows and Windows
Phone, which agrees with XNA for the measured corpus. For an Xbox 360 build it writes XMA when an
encoder is attached: `SetBuildXmaEncoder` installs an
`XboxSoundEffectEncoder` that the processor uses for that target, and the XNB writer emits the
encoder's XMA2WAVEFORMATEX with every field, extension included, big-endian as in the genuine
fixture. Without an encoder the Xbox build stays PCM, and that difference remains recorded as
audio_wav_soundeffect_xbox360 in `tools/xna-pipeline-oracle/differential/decisions.json`. A
`.cnb` build refuses an XMA-encoded sound effect: CNB stores PCM for CNA's own runtime.

Explicit AudioContent.ConvertFormat(ConversionFormat::Xma, ...) uses the process-wide
BuildXmaEncoder service. Without an available service it throws InvalidContentException including
the unavailable reason. A named nonexistent path has a path-specific reason rather than the
no-encoder marker. Failed encoding includes the attached service identity and diagnostics.

SetBuildXmaEncoder installs the service; null restores the unavailable default. Set it before the
build and do not replace it during a build. The `cna_content_compiler` CLI installs the selected
external service before source discovery.

## External-process contract

```text
--xma-encoder <path>              CNA_XMA_ENCODER
--xma-encoder-launcher <program>  CNA_XMA_ENCODER_LAUNCHER
--xma-encoder-arg <argument>      repeatable; CNA_XMA_ENCODER_ARGS is the fallback
```

Executable resolution is explicit option, then environment, then the configure-time default:
the `CNA_XMA_ENCODER_EXECUTABLE` and `CNA_XMA_ENCODER_LAUNCHER` cache entries, which
`modules/content-pipeline/CMakeLists.txt` turns into compile definitions when set. No encoder PATH
search is performed.
Environment argument text is split on
whitespace, without shell quoting; repeated CLI arguments preserve individual argument boundaries.
The default template is two arguments, {input} and {output}. Substitutions also include {quality}
(low/medium/best), {loopStart} and {loopLength}.

The adapter writes canonical little-endian PCM RIFF WAVE input and expects RIFF WAVE output with a
fmt chunk declaring little-endian tag 0x0166 and a data chunk. This is an adapter protocol, not a
guarantee that every tool's .xma output satisfies it. Use a wrapper for other command/file formats.

Available currently means the configured path exists, not that it is executable, correctly
licensed, able to start or capable of encoding. Encode reports launch/nonzero-exit/parse failures.
The parser checks RIFF/WAVE, chunk bounds, required chunks and the tag, and then the XMA2
extension: present (cbSize at least 34), its channel count the source's, NumStreams within it,
BytesPerBlock and the data in whole 2048-byte packets, the data within BlockCount x BytesPerBlock,
and both the play region and the loop inside SamplesEncoded (the fixture's loop, 384 + 22016,
runs past its PlayLength of 22050 but not past SamplesEncoded). It does not decode the bitstream
or qualify console acceptance.

The loop the result carries is the encoder's own LoopBegin/LoopLength, read from that extension:
XMA quantizes the loop (the fixture asked for 0/22050 and carries 384/22016), so the requested
values would describe a loop the stream does not have. AudioContent adopts the returned
format/data/loop fields.

## Identity and incremental builds

XmaEncoderIdentity is the executable's filename plus a fingerprint of what decides the output: the
SHA-256 of the program's bytes, the launcher, and the argument template. The same program
copied elsewhere fingerprints the same; a changed program or a differently run one does not. There
is no version query, because no command is common to every encoder.

With an encoder attached, `CNA.SoundEffectProcessor`'s component version is
`3+xbox:<identity>`, so the incremental manifest rebuilds sound effects when the encoder changes
(conservatively for every target, not only Xbox). Custom processors that call
`AudioContent::ConvertFormat(Xma, ...)` must fold the identity into their own version themselves.

## Tests and future qualification

`XmaEncoderService.*` and `XnbOutputContentPipelineTest.AnAttachedXboxEncoderMakesTheXboxWavRouteWriteXma`
cover the extension checks, the measured loop, the fingerprint, the processor version, the CMake
options and the Xbox routing with Python stub encoders, a fake encoder and the committed genuine
fixture; the independent parser accepts all three target fixture files. A real encoder's output and
console playback are unqualified; remove the recorded divergence only when genuine output/playback
comparisons pass.
