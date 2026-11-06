# Optional encoded XACT WaveBank decoding

`CNA_ENABLE_WAVEBANK_CODECS=OFF|AUTO|ON` selects the audio-internal FFmpeg entry
adapter independently of `CNA_ENABLE_VIDEO`. Both default to AUTO. The consumers
share one discovery of libavcodec, libavformat, libavutil and libswresample; the
historical `CNA_FFMPEG_AVAILABLE` variable continues to mean video availability.

OFF uses a decoder-free adapter which names the unavailable codec. AUTO enables
the adapter on eligible native toolchains when all four development modules are
found. ON refuses configuration when they are missing or the target integration
is unsupported. Linux and desktop macOS use the existing video platform envelope;
Windows/MSVC, MinGW, Android, Emscripten and iOS do not currently have target-native
FFmpeg discovery. A particular codec omitted by the system FFmpeg build is also
refused by name. Both options must be OFF for a runtime without FFmpeg edges.
Distribution FFmpeg libraries remain system dependencies; CNA does not bundle
them. The installed C++ dependency scan includes the private audio archive edges.

The qualified WMAv2 route reconstructs each entry's native xWMA format and packet cumulative
decoded-byte table. The original mini-format high bit selects WMAv2 versus WMA
Pro, and packed byte-rate/block-alignment indices are validated before use.
Encoded bytes are never reinterpreted as PCM. Malformed metadata, truncated or
invalid packets and short decoded output fail with bank, wave index, codec and
reason in the diagnostic; the entry is not cached as a silent playing sound.

Decoded output is interleaved S16 at the authored native sample rate. Mono and
stereo stay in their source layout. WMA Pro is currently refused as described below.
The owner-selected future 5.1 WMA Pro policy applies
FAudio's measured XAudio2 default 6-to-2 matrix at the shared adapter boundary,
before either SDL3_mixer or CnaMixer. In FL, FR, FC, LFE, BL, BR order:

| Output | FL | FR | FC | LFE | BL | BR |
|---|---:|---:|---:|---:|---:|---:|
| Left | .294545442 | 0 | .208181813 | .090909094 | .251818180 | .154545456 |
| Right | 0 | .294545442 | .208181813 | .090909094 | .154545456 | .251818180 |

Source: local FAudio `src/matrix_defaults.inl`:159-160, selected by
`FAudio.c`:1384-1387 (`FAUDIO_INTERNAL_MATRIX_DEFAULTS[5][1]`). The matrix includes
LFE and surround crossfeed. Each output is accumulated in single precision,
rounded to the nearest S16 integer and clamped to [-32768,32767]. No resampling
or change in frame count is part of downmixing. This is an explicit stereo-output
policy, not a claim that CNA gained multichannel speaker output or G06 spatial
audio fidelity.

Resident banks decode/cache only the requested entry. Streaming constructors keep
the same retained source and read/decode the requested entry lazily, including an
embedded bank offset. This is full-entry lazy decoding, not packet-scheduled
bounded-memory streaming. Encoded input and native decoded output are each
limited to 256 MiB per entry. The limit is checked before large entry reads and
as output is decoded, including entries with no authored duration. Invalid
streaming metadata extents are checked against the source before prefix
allocation. Authored decoded-frame duration and loop regions are preserved;
declared duration may trim real trailing padding but never pad short output.
Internal seeks use decoded frames, not encoded bytes or interleaved scalar counts.

WMA Pro entries remain **refused by name**, even when FFmpeg is present. The
SDK's six-channel 44100 Hz source at its fallback 48000 bit/s bitrate becomes
48000 Hz Pro. Native Duration and dpds specify37713 frames; independent FFmpeg
returns35665, matching the resampled source length but2048 short of the bank's
authored timeline. Parsed duration remains37713; CNA neither silently truncates
that metadata nor pads/shifts PCM. No playable effect/duration is constructed.
FAudio FACT.c:1879 forwards authored Duration, while its Pro selector currently
asserts unsupported at :1803-1806; it cannot settle Microsoft's native priming
contract. See the [corpus investigation](../modules/audio/tests/CNA/Internal/Audio/encoded-wavebank/README.md)
for packet facts, independent tool versions/hashes and the unresolved native
measurement. A shorter metadata derivative cannot bypass the qualification gate.

XMA/XMA2 entries remain **refused by name**. The shared FFmpeg pipeline contains
the modern XMA2 descriptor/decode boundary, but its dispatch is deliberately
unqualified until a lawful native fixture validates block metadata, priming and
PCM. Legacy XMA1 metadata is not guessed. No Xbox SDK or game-derived recording
is redistributed. The desktop Refresh `CheckWaveBankHeader` byte-order gate is
preserved; this feature does not introduce big-endian DNBW bank acceptance.

WMAv2 playback tests use SDK-encoded CNA-owned tones and an independently executed system
ffmpeg/ffprobe oracle; their fixture directory records tool versions, commands,
licences and hashes. Pure metadata, matrix and unavailable-adapter tests do not
need an audio device. The GAP-02 manual-device targets exercise actual decoded
PCM, pause/stop, finite loops, storage offsets and disposal through both real
mixers without a window. Encoded streaming subregions need native qualification;
the positive streaming loop contract uses whole-wave regions.
