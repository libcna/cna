# XMA: the audit, the measurement, and the seam

`plans/plan_xnapipeline_parity.md` `XNAPP-262`.

XMA is the Xbox 360's audio codec. It is the one thing in the Content Pipeline parity plan CNA
cannot produce, and this document is why, what was measured about it anyway, and where an encoder
attaches if one ever becomes available.

The single sentence the rest of the project quotes is:

> **XMA ENCODER EXTERNALLY UNAVAILABLE**

It is a fixed string (`CNA::Content::Pipeline::XmaEncoderUnavailableSentence`) rather than prose, so
the parity matrix, the plan, the command-line help and the runtime diagnostic all say the same
thing, and a test can check that they do.

---

## 1. The audit

Four things had to be true for this to be an external blocker rather than work. All four are.

| Condition | Finding |
|---|---|
| No public specification sufficient to implement a conforming encoder | XMA is a Microsoft codec derived from WMA Pro. Microsoft published the *container* — `XMA2WAVEFORMATEX` is documented, and §2 below measures it independently — and never the bitstream. The open-source work that exists (`ffmpeg`'s `xmadec`, and the several `.xma`-to-`.wav` tools) is decoding, reverse-engineered from streams; nothing describes the encoder's rate control, block layout or subframe packing well enough to write one that a console would play. |
| No legally usable or licensable encoder | The encoder ships inside the Xbox 360 XDK and XNA Game Studio, both under licences that permit use by a registered developer on their own machine and not redistribution. There is no separately licensable XMA encoder library. |
| No encoder in FFmpeg or in any other dependency this project may take | Measured on this machine, FFmpeg 7.1.5: `ffmpeg -codecs` lists `xma1` and `xma2` with the flags `D.AIL.` — decode only — and `ffmpeg -h encoder=xma2` answers *"Codec 'xma2' is known to FFmpeg, but no encoders for it are available."* `libavcodec/codec_id.h` declares `AV_CODEC_ID_XMA1` and `AV_CODEC_ID_XMA2` and libavcodec registers no encoder for either. No other permitted dependency has one. |
| Microsoft's own encoder cannot be redistributed or used as a CNA dependency | Committing or shipping it is forbidden by both its licence and this project's provenance rules. What *is* allowed, and what §4 provides, is for a user who has legally installed it to attach it themselves — the same arrangement `--fx-compiler` already has with Microsoft's `fxc`. |

The finding is therefore **EXTERNAL_BLOCKED**, and it is the only one in the plan.

---

## 2. What was measured anyway

The blocker is the encoder. Everything around it was measurable, because genuine XNA built the
corpus's sound effect for all three targets and those files are committed.

Three builds of one 44100 Hz mono 16-bit source, 22050 frames:

| | Windows (`w`) | Windows Phone (`m`) | Xbox 360 (`x`) |
|---|---|---|---|
| format tag | 1 (PCM) | 1 (PCM) | **0x0166 (XMA2)** |
| sample rate | 44100 | 44100 | **44032** |
| average bytes/second | 88200 | 88200 | 12200 |
| block align | 2 | 2 | 2 |
| bits per sample | 16 | 16 | 16 |
| format extension | none | none | **34 bytes** |
| payload | 44100 bytes | 44100 bytes | 6144 bytes |
| loop start / length | 0 / 22050 | 0 / 22050 | **384 / 22016** |
| duration | 500 ms | 500 ms | 500 ms |

Two rules come straight off that table.

**Byte order.** The Xbox 360 is big-endian and XNA byte-swaps the entire format block for it. The
tag reads `0x0166` only when it is read big-endian; read the file's bytes little-endian and it is
`0x6601`, which is what a reader that assumes one order sees. The 34-byte extension is swapped
field by field, not as a blob — every `WORD` and `DWORD` of the `XMA2WAVEFORMATEX` individually.
Decomposed from the committed file, in declaration order:

| Field | Width | Value here |
|---|---|---|
| `NumStreams` | `WORD` | 1 |
| `ChannelMask` | `DWORD` | 1 (`SPEAKER_FRONT_LEFT`) |
| `SamplesEncoded` | `DWORD` | 22528 |
| `BytesPerBlock` | `DWORD` | 32768 |
| `PlayBegin` | `DWORD` | 0 |
| `PlayLength` | `DWORD` | 22050 |
| `LoopBegin` | `DWORD` | 384 |
| `LoopLength` | `DWORD` | 22016 |
| `LoopCount` | `BYTE` | 0 |
| `EncoderVersion` | `BYTE` | 4 |
| `BlockCount` | `WORD` | 1 |

`XmaEncoderService.TheGenuineXboxSoundEffectDecomposesAsABigEndianXma2WaveFormatEx` asserts every
one of those, so this table is a test rather than a claim.

**Loop and duration semantics.** An XMA stream is built out of 128-sample subframes, and every
frame count in the format follows from that:

* the **sample rate** is rounded down to a multiple of 128 — 44100 becomes 44032, which is 344×128;
* `SamplesEncoded` (22528 = 176×128) is the padded length the encoder actually wrote, which is
  *more* than the source's 22050 frames;
* `PlayLength` keeps the source's exact 22050, so the playable region is not quantized;
* the **loop region is** — `LoopBegin` 384 is 3×128 and `LoopLength` 22016 is 172×128, where the
  PCM builds of the same source loop 0..22050 exactly. A loop point moves by up to 127 frames;
* the **duration is unchanged** at 500 ms, because XNA computes it from `PlayLength` and the
  source's own rate rather than from the encoded byte count.

The XNB's own `loopStart` and `loopLength` fields carry `LoopBegin` and `LoopLength`, so a game
reading the asset sees the quantized region and not the authored one.

**Container.** The `.xnb` around it is otherwise ordinary and is validated independently by
`tools/xnb/xnb_conformance.py`, which shares no code with CNA's writer or reader: the header, the
type-reader table (`SoundEffectReader`, with the Compact Framework's own `mscorlib` identity — see
`XNAPP-261`), the format block, the payload length and the loop fields all parse and agree.

---

## 3. What CNA does today

The built-in `.wav` route writes **PCM for all three targets**. For Windows and Windows Phone that
is byte-identical to XNA; for the Xbox 360 it is a recorded divergence, listed in
`tools/xna-pipeline-oracle/differential/decisions.json` as `audio_wav_soundeffect_xbox360` with the
paths it explains, and in the parity matrix as `.wav`'s `xbox360: XMA ENCODER EXTERNALLY
UNAVAILABLE`. It is not hidden: `XnaAudioProcessors.EveryTargetAndProfileAnswersWhatXnaAnswers`
asserts the difference, so if CNA ever gains an encoder that test fails and says so.

`AudioContent::ConvertFormat(ConversionFormat::Xma, …)` — XNA's own API for asking for XMA —
refuses with a message beginning with the sentence above, unless an encoder is attached.

---

## 4. The seam

`CNA::Content::Pipeline::XmaEncoderService` is the interface. It is deliberately the same shape as
`EffectCompilerService`, which solves the identical problem for `fxc`: a Microsoft tool that cannot
be vendored, discovered by option then environment then CMake, optionally run through a launcher,
and folded into the build fingerprint by its own reported identity.

```
--xma-encoder <path>              the program                (CNA_XMA_ENCODER)
--xma-encoder-launcher <program>  something to run it through (CNA_XMA_ENCODER_LAUNCHER), e.g. wine
--xma-encoder-arg <argument>      one argument, repeatable    (CNA_XMA_ENCODER_ARGS)
```

There is deliberately **no search of `PATH`**: an encoder for this codec is something a user
attaches on purpose, and picking one up by name would make a build depend on what happens to be
installed.

**The contract.** The program is handed a canonical RIFF WAVE file and must write a RIFF file whose
`fmt ` chunk is the `XMA2WAVEFORMATEX` (format tag `0x0166`, little-endian, as a `.xma` carries it)
and whose `data` chunk is the encoded payload. That is exactly what a `.xma` file is, so an encoder
that already writes one needs no adaptation. Everything after that is CNA's: the byte order for the
target, the loop fields, the duration, the container.

The argument template is what adapts CNA to an encoder whose command line differs. Each
`--xma-encoder-arg` is one argument, with `{input}`, `{output}`, `{quality}`, `{loopStart}` and
`{loopLength}` substituted; the default is `{input} {output}`.

**Fingerprint.** `XmaEncoderIdentity` carries the encoder's name and, since no encoder for this
codec answers a common version probe, its file's own last-write time. Two encoders are two
identities, so attaching or changing one rebuilds rather than reusing an artifact the other
produced.

**What is tested without an encoder.** `XmaEncoderServiceTests.cpp` drives the whole seam with a
stub the test writes: the contract, the argument template and the quality reaching it, the format
block becoming the content's format, an output that is not a `.xma` being refused by name, an
encoder that fails being reported in its own words, the two refusal classes being distinguishable
(*no encoder* against *this path is wrong*), and two encoders having two fingerprints. None of that
needs a real encoder to be correct, and all of it is what an attached one would rely on.

---

## 5. If this ever changes

Attaching an encoder is a command-line option and no code change. What would then need doing, in
order:

1. give the Xbox 360 branch of the `.wav` route the encoder, so a build converts rather than
   passing PCM through;
2. quantize the loop region the way §2 measures, and keep `PlayLength` exact;
3. byte-swap the `XMA2WAVEFORMATEX` field by field for the `x` target;
4. remove `audio_wav_soundeffect_xbox360` from `decisions.json` and let the differential compare it;
5. change `.wav`'s `xbox360` row from `XMA ENCODER EXTERNALLY UNAVAILABLE` to `SUPPORTED`.

Steps 2 and 3 are already measured; only step 1 depends on the encoder.
