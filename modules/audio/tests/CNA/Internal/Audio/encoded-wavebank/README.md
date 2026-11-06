# SDK xWMA corpus and the WMA Pro qualification limit

All tones, authored XAP projects, encoded outputs and derived PCM in this
folder are CNA-authored **CC0-1.0** material. No SDK executable, SDK media or game
recording is redistributed. Framework/test source retains MS-PL.

Microsoft DirectX SDK June 2010 xWMAEncode.exe and XactBld3.exe, build
9.29.1962.0, encoded the synthetic source and built the genuine Windows WBND
banks. PROVENANCE.json records exact source, SDK and independent CLI commands,
tool versions/hashes and source hashes; each stem's provenance records native
metadata. SHA256SUMS covers every corpus file except itself. The authored XAPs
are retained. The SDK ran outside the lane sandbox, with DISPLAY/WAYLAND unset
and a lane-private Wine prefix that was removed afterwards.

The sources contain 32768 frames at 44100 Hz. ffmpeg lavfi generates amplitude
0.16 sine tones at 311/457 Hz for stereo, and 311/457/613/787/911/1103 Hz in
FL/FR/FC/LFE/BL/BR for the six-channel control. The SDK's default **bitrate** is
48000 bits/s; this is distinct from its selected **sample rate**. Its help lists
44100 Hz 5.1 as native only at 96000/192000 bits/s and explicitly permits
resampling unsupported combinations.

## Qualified WMAv2 stereo

The genuine bank has mini-format 0x3195888b: stereo, 44100 Hz, 2230-byte block
alignment, 6000 bytes/s. Its 6690-byte payload contains three packets. Raw seek
values are [57344,122880,131072] decoded bytes; last/(2 channels*2 bytes)=32768
frames. Native bank Duration and the independent CLI output both equal 32768.
The SDK xWMA and the descriptor reconstructed from the bank decode to identical
PCM (SHA-256 recorded in PROVENANCE.json).

- wmav2.native.xwb is the untouched SDK bank; wmav2.native.s16le contains all
  32768 independent native-channel frames, including the decoder's tail.
- wmav2.xwb changes only Duration to 8192 and LoopRegion to [2048,6144). Every
  encoded payload and seek-table byte is unchanged. wmav2.s16le is the first
  8192 independently decoded frames, with no shift, fitting, padding or scaling.
- wmav2.reference.xwma reconstructs the literal native format/seek/payload for
  the independent system ffmpeg/ffprobe oracle.
- wmav2.invalid.bin preserves payload size and replaces only the final packet
  with 0xff. Independent ffmpeg -err_detect explode -xerror refuses it. Earlier
  valid packets already cover the 8192-frame derivative; this checks that the
  adapter still validates the encoded tail.

Independent decode command (no CNA executable is used to produce expectations):

```sh
ffmpeg -nostdin -v error -threads 1 -i wmav2.reference.xwma \
  -map 0:a:0 -c:a pcm_s16le -f s16le wmav2.native.s16le
ffprobe -v error -show_streams -of json wmav2.reference.xwma
```

Native PCM comparison permits ±2 S16 LSBs for libavcodec/SIMD rounding. Mixer
comparison permits 3/32768, including decode/conversion rounding. No tolerance
expansion or alignment fitting is used. Both source channels must contain signal.
The largest committed fixture is the 131072-byte untrimmed WMAv2 PCM reference.

## Genuine WMA Pro control: refused, not qualified playback

wmapro.native.xwb is the untouched SDK bank. Mini-format 0xb797701b specifies
**48000 Hz, six channels**, 5462-byte packet alignment and 6000 bytes/s. There
are exactly three packets (16386 bytes). Raw dpds values are
[196608,393216,452556]; final/(6*2)=**37713** frames, exactly the authored Duration.
Independent ffprobe's duration_ts is therefore also37713; it is container
metadata, not a decoded sample count. ffmpeg returns 18 frames: 17*2048+849=
**35665** samples/channel. Both the original SDK xWMA and the reconstructed bank
reference yield byte-identical PCM. The 35665-frame result agrees with resampling
32768 source frames from44100 to48000 Hz (35665.306..., rounded by the encoder).
It leaves an exact2048-frame difference from authored metadata.

The SDK header xact3wb.h:459-465 defines Duration in native sample frames.
FAudio FACT.c:1879 passes entry->Duration as PlayLength, :1893-1902 passes raw
packet cumulative bytes, and :1853-1858 disallows encoded streaming subregions.
Its current Pro path at :1803-1806 asserts the high bit is zero; this is not a
native XACT Pro oracle. Its Windows decoder at
FAudio_platform_win32_wmadec.c:213-236 copies available output and zeros an
unfilled request; that implementation alone does not establish Microsoft's Pro
priming or end-of-stream contract.

Microsoft [DirectXTK WaveBankReader.cpp](https://github.com/microsoft/DirectXTK/blob/main/Audio/WaveBankReader.cpp#L1040-L1047)
preserves a normal entry's Duration as metadata. FFmpeg's
[n7.1.2 wmaprodec.c](https://github.com/FFmpeg/FFmpeg/blob/n7.1.2/libavcodec/wmaprodec.c#L403)
initializes a first-frame output skip and suppresses that output at1396-1413.
Its [xwma demuxer](https://github.com/FFmpeg/FFmpeg/blob/n7.1.2/libavformat/xwma.c#L218-L232)
estimates duration from the final dpds value. The first-frame skip is consistent
with the observed2048-frame difference; this source inspection is an inference,
not a Microsoft runtime measurement. The independent binary used here is
7.1.5-0+deb13u1; n7.1.2 is the inspected source reference, not an assertion that
those binaries are identical.

CNA retains **37713** in parsed bank metadata. Under the owner's Phase 3 fallback,
**every WMA Pro entry is refused by name before decoding**, including metadata
trimmed to8192 or with absent Duration. No SoundEffect duration or playable voice
is produced; no silence is appended, samples shifted, or native duration reduced.
There is no positive Pro derivative/PCM golden in this corpus. The Pro bank and
reference exist to reproduce this limitation and test the refusal. A future
native XACT/XAudio2 measurement must settle priming/duration and the explicit
tail policy before the gate opens. XMA/XMA2 remain separately refused.

The approved future 5.1-to-stereo FAudio matrix is tested with independent channel
impulses in pure tests and both real mixers, but those tests do not qualify Pro
codec decoding or six-channel speaker output.
