#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-197: the WAV sources the width matrix is measured on.

XNA's own `WavImporter` accepts 8-bit unsigned and 16-bit signed PCM and refuses everything wider
(`audio-content-oracle.json`, `processors/SoundEffectProcessor_pcm_matrix`), so the sources that
matter are those two widths, mono and stereo, at a few rates, with and without a loop. The
waveform is a deterministic ramp rather than a tone: what is being compared is the container's
fields and the payload's bytes, and a ramp makes an off-by-one in either visible.

    python3 tools/xna-pipeline-oracle/audio/make_pcm_width_wavs.py [directory]
"""
from __future__ import annotations

import os
import struct
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))


def ramp(frames, channels, bits):
    """A deterministic sample ramp: one byte pattern per (frame, channel)."""
    data = bytearray()
    for frame in range(frames):
        for channel in range(channels):
            value = (frame * 7 + channel * 53) % 256
            if bits == 8:
                data.append(value)
            else:
                data += struct.pack("<h", (value - 128) * 256)
    return bytes(data)


def wav(path, channels, rate, bits, frames, loop_start=0, loop_length=0):
    align = channels * bits // 8
    payload = ramp(frames, channels, bits)
    fmt = struct.pack("<HHIIHH", 1, channels, rate, rate * align, align, bits)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    if loop_length:
        smpl = struct.pack("<7I", 0, 0, 0, 0, 0, 0, 0) + struct.pack("<II", 1, 0)
        smpl += struct.pack("<6I", 0, 0, loop_start, loop_start + loop_length - 1, 0, 0)
        chunks += b"smpl" + struct.pack("<I", len(smpl)) + smpl
    chunks += b"data" + struct.pack("<I", len(payload)) + payload
    with open(path, "wb") as handle:
        handle.write(b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks)
    return os.path.basename(path)


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    out = argv[0] if argv else os.path.join(REPO, "tests", "assets", "xna40", "media")
    os.makedirs(out, exist_ok=True)
    written = [
        wav(os.path.join(out, "pcm8_mono_22050.wav"), 1, 22050, 8, 441),
        wav(os.path.join(out, "pcm8_stereo_22050.wav"), 2, 22050, 8, 441),
        wav(os.path.join(out, "pcm8_mono_8000.wav"), 1, 8000, 8, 400),
        wav(os.path.join(out, "pcm8_mono_44100.wav"), 1, 44100, 8, 441),
        wav(os.path.join(out, "pcm8_mono_22050_loop.wav"), 1, 22050, 8, 441, 100, 200),
        wav(os.path.join(out, "pcm16_mono_22050_loop.wav"), 1, 22050, 16, 441, 100, 200),
        wav(os.path.join(out, "pcm8_stereo_44100_loop.wav"), 2, 44100, 8, 441, 7, 400),
    ]
    for name in written:
        print("make_pcm_width_wavs: %s" % name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
