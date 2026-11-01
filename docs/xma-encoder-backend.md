# XMA encoder boundary and measured XNA fixture (XNAPP-262)

CNA ships no XMA encoder. Its stable unavailable diagnostic marker is
`CNA::Content::Pipeline::XmaEncoderUnavailableSentence`:

> **XMA ENCODER EXTERNALLY UNAVAILABLE**

This is the encoder limitation recorded by the completed content-pipeline campaign, whose plan is
now in Git history. It is not a statement that every other pipeline feature is fully compatible.
See [xna-content-pipeline-parity-report.md](xna-content-pipeline-parity-report.md) for the matrix.

## What the blocker establishes

The repository contains an external-process encoder seam, not a built-in XMA bitstream encoder.
On this audit host, FFmpeg 7.1.5 lists xma1/xma2 decoding but no XMA encoder. No conforming
redistributable encoder has been integrated or qualified as a project dependency.

Container documentation and the committed oracle bytes do not establish a complete encoding
algorithm. The older claims that no licensable encoder could exist, or that a particular SDK
licence authorizes a user's use, go beyond what these source/tests establish. CNA does not bundle
Microsoft's SDK encoder; an explicitly attached tool must be supplied under its own applicable
terms. This page does not determine those terms.

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

The built-in default/Best WAV sound-effect route writes PCM for all three target identities. For the measured
Windows/Phone corpus it agrees with XNA; for Xbox the PCM/XMA difference is recorded as
audio_wav_soundeffect_xbox360 in `tools/xna-pipeline-oracle/differential/decisions.json`.
Attaching an encoder does not automatically make that built-in route choose XMA.

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

Executable resolution is explicit option, then environment, then a
CNA_XMA_ENCODER_EXECUTABLE compile-definition default if supplied; no encoder PATH search is
performed. Launcher resolution similarly has a compile-definition fallback. The service contains
those fallbacks, but current CMake files do not wire matching cache options/definitions; passing
an arbitrary -DCNA_XMA_ENCODER_EXECUTABLE cache variable alone does not attach an encoder.
Environment argument text is split on
whitespace, without shell quoting; repeated CLI arguments preserve individual argument boundaries.
The default template is two arguments, {input} and {output}. Substitutions also include {quality}
(low/medium/best), {loopStart} and {loopLength}.

The adapter writes canonical little-endian PCM RIFF WAVE input and expects RIFF WAVE output with a
fmt chunk declaring little-endian tag 0x0166 and a data chunk. This is an adapter protocol, not a
guarantee that every tool's .xma output satisfies it. Use a wrapper for other command/file formats.

Available currently means the configured path exists, not that it is executable, correctly
licensed, able to start or capable of encoding. Encode reports launch/nonzero-exit/parse failures.
The parser checks RIFF/WAVE, chunk bounds, required chunks and the tag; it does not fully validate
XMA2 extension semantics, playable bitstream contents or console acceptance.

The adapter currently copies requested loopStart/loopLength into its result rather than extracting
the encoder's quantized loop region. AudioContent adopts the returned format/data/loop fields.
Do not infer fixture-equivalent loop behavior from a successful stub conversion.

## Identity and incremental-build limitation

XmaEncoderIdentity currently contains the executable's filename and last-write timestamp.
It is not a content hash, full-path identity or common encoder version query, and does not include
launcher/template/environment inputs. Equal filenames/timestamps can collide.

The source comparison also found no compiler component-version consumer of BuildXmaEncoder's
Identity; the service test checks identity differences, not a real incremental rebuild after
replacing an encoder. The older promise of automatic encoder-change invalidation was therefore
too strong. Qualify/integrate that dependency before relying on cached custom-processor output.

## Verification and future qualification

On 2026-11-01, the focused ContentPipeline target builds and all nine XmaEncoderService cases plus
the per-target audio processor case pass on the private runner. Tests use Python stub encoders
and the committed genuine fixture; no real XMA encoding or console playback is qualified.
The independent parser also accepts all three target fixture files.

A future real-encoder integration still needs built-in Xbox routing, measured loop handling,
complete format/target-byte-order handling, dependency fingerprinting and genuine output/playback
comparisons. Only remove the recorded divergence when those comparisons actually pass.
