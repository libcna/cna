#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Regenerate the CNA-produced `.xnb` interoperability fixture corpus.

plans/plan_xnapipeline.md XNAP-026.

The corpus exists so that CNA-written `.xnb` files can be handed to a real XNA 4.0 or MonoGame
runtime on a machine that has one, without that machine needing to build CNA first. The source
assets are generated here rather than vendored, so every byte in the corpus is CNA-original and
carries no third-party licence.

Usage:
    generate_cna_xnb_fixtures.py --content-tool <path/to/cna-content> [--output <dir>]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT = REPOSITORY_ROOT / "tests" / "assets" / "xnb" / "cna"


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def write_png(path: pathlib.Path, width: int, height: int) -> None:
    """Writes an RGBA PNG whose every texel is distinct and derived from its coordinates."""
    raw = b""
    for y in range(height):
        raw += b"\x00"
        for x in range(width):
            raw += bytes([(16 * x + 1) & 0xFF, (16 * y + 2) & 0xFF,
                          (16 * (x + y) + 3) & 0xFF, 0xFF])
    data = (b"\x89PNG\r\n\x1a\n"
            + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + png_chunk(b"IDAT", zlib.compress(raw))
            + png_chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def write_wav(path: pathlib.Path, sample_rate: int, channels: int, frames: int) -> None:
    """Writes a 16-bit PCM WAV holding a pure tone, so a listener can verify it played."""
    samples = bytearray()
    for frame in range(frames):
        value = int(20000 * math.sin(2.0 * math.pi * 440.0 * frame / sample_rate))
        for _ in range(channels):
            samples += struct.pack("<h", value)
    block_align = channels * 2
    fmt = struct.pack("<HHIIHH", 1, channels, sample_rate, sample_rate * block_align,
                      block_align, 16)
    data = (b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt) + 8 + len(samples)) + b"WAVE"
            + b"fmt " + struct.pack("<I", len(fmt)) + fmt
            + b"data" + struct.pack("<I", len(samples)) + bytes(samples))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


FIXTURES = [
    {
        "name": "texture_4x4",
        "source": "texture_4x4.png",
        "expectedType": "Texture2D",
        "rootReader": "Microsoft.Xna.Framework.Content.Texture2DReader",
        "platform": "windows",
        "notes": "Every texel is distinct, so a runtime that mixes up rows, columns or channels "
                 "produces a visibly wrong image rather than a plausible one.",
    },
    {
        "name": "texture_desktopgl_hidef",
        "source": "texture_4x4.png",
        "expectedType": "Texture2D",
        "rootReader": "Microsoft.Xna.Framework.Content.Texture2DReader",
        "platform": "desktopgl",
        "profile": "hidef",
        "notes": "The same asset with a different platform byte and the HiDef profile flag set.",
    },
    {
        "name": "sound_mono_22050",
        "source": "sound_mono_22050.wav",
        "expectedType": "SoundEffect",
        "rootReader": "Microsoft.Xna.Framework.Content.SoundEffectReader",
        "platform": "windows",
        "notes": "A 440 Hz tone, 16-bit mono PCM: audibly correct or audibly wrong.",
    },
]


def build(tool: pathlib.Path, source: pathlib.Path, output: pathlib.Path,
          platform: str, profile: str, version: str) -> None:
    """Builds one asset, then moves the artifact into the corpus.

    Each build gets its own staging directory: `cna-content` owns everything in an output
    directory through its manifest, so several independent single-file builds sharing one
    directory would each retire the previous one's artifact.
    """
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cna_xnb_fixture_") as staging:
        staged = pathlib.Path(staging) / output.name
        command = [str(tool), "build", str(source), "-o", str(staged), "--format", "xnb",
                   "--xnb-platform", platform, "--xnb-profile", profile,
                   "--xnb-version", version, "--quiet"]
        subprocess.run(command, check=True)
        output.write_bytes(staged.read_bytes())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--content-tool", required=True, type=pathlib.Path,
                        help="path to the built cna-content executable")
    parser.add_argument("--output", type=pathlib.Path, default=DEFAULT_OUTPUT,
                        help="corpus directory (default: tests/assets/xnb/cna)")
    arguments = parser.parse_args()

    sources = arguments.output / "source"
    artifacts = arguments.output / "windows" / "uncompressed"
    write_png(sources / "texture_4x4.png", 4, 4)
    write_wav(sources / "sound_mono_22050.wav", 22050, 1, 4410)

    index = []
    for fixture in FIXTURES:
        source = sources / fixture["source"]
        artifact = artifacts / (fixture["name"] + ".xnb")
        platform = fixture.get("platform", "windows")
        profile = fixture.get("profile", "reach")
        version = str(fixture.get("version", 5))
        build(arguments.content_tool, source, artifact, platform, profile, version)

        digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
        manifest = {
            "producer": "CNA content pipeline (cna-content --format xnb)",
            "producerPlan": "plans/plan_xnapipeline.md",
            "sourceAsset": f"source/{fixture['source']}",
            "sourceLicense": "CNA-original, generated by scripts/generate_cna_xnb_fixtures.py; "
                             "no third-party asset is vendored here",
            "platform": platform,
            "profile": profile,
            "version": int(version),
            "compressed": False,
            "rootReader": fixture["rootReader"],
            "expectedType": fixture["expectedType"],
            "sizeBytes": artifact.stat().st_size,
            "sha256": digest,
            "notes": fixture["notes"],
        }
        manifest_path = artifact.with_suffix(".xnb.manifest.json")
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        index.append({"file": artifact.name, "sha256": digest,
                      "expectedType": fixture["expectedType"]})
        print(f"wrote {artifact} ({manifest['sizeBytes']} bytes)")

    (arguments.output / "README.md").write_text(
        "# CNA-produced `.xnb` interoperability corpus\n\n"
        "Generated by `scripts/generate_cna_xnb_fixtures.py`; validated by\n"
        "`scripts/xnb_conformance.py`. Every source asset here is CNA-original and generated by\n"
        "that script, so the corpus carries no third-party licence.\n\n"
        "See [`docs/xnb-interoperability.md`](../../../../docs/xnb-interoperability.md) for what\n"
        "to do with these files on a machine that has a real XNA 4.0 or MonoGame runtime.\n\n"
        "```json\n" + json.dumps(index, indent=2) + "\n```\n",
        encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
