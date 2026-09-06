#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xnapipeline_parity.md XNAPP-167: the per-extension texture corpus.

The nine extensions `TextureImporter` declares each get a committed fixture here, so the genuine
XNA importer and CNA's importer read **the same bytes** rather than two encoders' idea of the same
image. Before this existed both sides synthesized their own file and the comparison silently
assumed the two encoders agreed; a JPEG makes that assumption false by construction.

Every fixture is 2x2 and carries the same four pixels -- opaque red, opaque green, opaque blue and
half-transparent white -- so what a format can and cannot carry (alpha in a PPM, precision in a
JPEG) shows up as a difference in the measured pixels and not as a difference in the source.

The malformed set beside them is what each importer must refuse.

Run:  python3 tools/xna-pipeline-oracle/texture/make_texture_fixtures.py
"""
import os
import struct
import subprocess
import sys
import zlib

# Red, green, blue, half-transparent white, as RGBA. The same four the graphics oracle uses.
PIXELS = [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 255, 128)]
WIDTH = HEIGHT = 2


def png(path):
    """A 2x2 RGBA8 PNG, written here rather than by a library so the bytes are fixed forever."""
    raw = b""
    for row in range(HEIGHT):
        raw += b"\x00"  # filter: none
        for column in range(WIDTH):
            raw += bytes(PIXELS[row * WIDTH + column])

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)
    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    write(path, data)


def bmp_body():
    """The BITMAPINFOHEADER and the 32-bit bottom-up pixels a .bmp and a .dib share."""
    body = struct.pack("<IiiHHIIiiII", 40, WIDTH, HEIGHT, 1, 32, 0, WIDTH * HEIGHT * 4, 0, 0, 0, 0)
    for row in range(HEIGHT - 1, -1, -1):          # a BMP's rows run bottom to top
        for column in range(WIDTH):
            r, g, b, a = PIXELS[row * WIDTH + column]
            body += bytes((b, g, r, a))
        # 32-bit rows are already a multiple of four bytes, so no padding is needed.
    return body


def bmp(path):
    body = bmp_body()
    write(path, b"BM" + struct.pack("<IHHI", 14 + len(body), 0, 0, 14 + 40) + body)


def dib(path):
    """A .dib is exactly a .bmp without its fourteen-byte file header."""
    write(path, bmp_body())


def tga(path):
    data = bytes((0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0))
    data += struct.pack("<HHBB", WIDTH, HEIGHT, 32, 0x28)   # top-left origin, eight alpha bits
    for r, g, b, a in PIXELS:
        data += bytes((b, g, r, a))
    write(path, data)


def ppm(path):
    """P6 carries no alpha, which is why its fourth pixel reads back opaque."""
    data = b"P6\n2 2\n255\n"
    for r, g, b, _ in PIXELS:
        data += bytes((r, g, b))
    write(path, data)


def pfm(path):
    """A PFM's rows run bottom to top and each pixel is three little-endian floats."""
    data = b"PF\n2 2\n-1.0\n"
    for row in range(HEIGHT - 1, -1, -1):
        for column in range(WIDTH):
            for channel in PIXELS[row * WIDTH + column][:3]:
                data += struct.pack("<f", channel / 255.0)
    write(path, data)


def hdr(path, width=8):
    """Radiance RGBE, written by the host FFmpeg.

    Radiance's run-length scanlines only exist at widths of 8 and above, so a 2x2 HDR is
    necessarily a flat file -- and D3DX, which is what XNA's importer reads with, cannot read a
    flat scanline: it answers the second pixel first and infinities for the rest (measured, see
    `probe_flat.hdr`). The corpus therefore carries a width-8 image, whose first four pixels are
    the same four every other fixture carries, and keeps the narrow one beside it as the case that
    records the limit.
    """
    row = [p[:3] for p in PIXELS] + [(128, 0, 0), (0, 128, 0), (0, 0, 128), (64, 64, 64)]
    raw = b"".join(bytes(p) for p in (row * HEIGHT)[:width * HEIGHT])
    process = subprocess.run(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (width, HEIGHT), "-i", "pipe:0",
         "-c:v", "hdr", "-f", "image2", "pipe:1"],
        input=raw, stdout=subprocess.PIPE, check=True)
    write(path, process.stdout)


def hdr_flat(path):
    """The 2x2 flat-scanline HDR D3DX misreads; kept so the limit stays measured."""
    raw = b"".join(bytes(p[:3]) for p in PIXELS)
    process = subprocess.run(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (WIDTH, HEIGHT), "-i", "pipe:0",
         "-c:v", "hdr", "-f", "image2", "pipe:1"],
        input=raw, stdout=subprocess.PIPE, check=True)
    write(path, process.stdout)


def dds(path):
    """Uncompressed A8R8G8B8, the shape the graphics oracle already measured."""
    data = b"DDS "
    data += struct.pack("<IIIIIII", 124, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000, HEIGHT, WIDTH,
                        WIDTH * 4, 0, 0)
    data += b"\x00" * (11 * 4)                                # reserved
    data += struct.pack("<IIIIIIII", 32, 0x1 | 0x40, 0, 32,
                        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    data += struct.pack("<IIIII", 0x1000, 0, 0, 0, 0)
    for r, g, b, a in PIXELS:
        data += bytes((b, g, r, a))
    write(path, data)


def jpg(path):
    """The one format no two encoders spell alike, so the committed file is the reference.

    Written by the host FFmpeg from the raw pixels at quality 1, and read back by both sides.
    """
    raw = b"".join(bytes(p[:3]) for p in PIXELS)
    process = subprocess.run(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (WIDTH, HEIGHT), "-i", "pipe:0",
         "-q:v", "1", "-f", "mjpeg", "pipe:1"],
        input=raw, stdout=subprocess.PIPE, check=True)
    write(path, process.stdout)


def png_3x2(path):
    """Three by two: the one source whose size ResizeToPowerOfTwo actually changes."""
    pixels = PIXELS + [(0, 0, 0, 255), (128, 128, 128, 64)]
    raw = b""
    for row in range(2):
        raw += b"\x00"
        for column in range(3):
            raw += bytes(pixels[row * 3 + column])

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", 3, 2, 8, 6, 0, 0, 0)
    write(path, b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
          chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def png_4x4(path):
    """Four by four: the smallest size XNA's DXT compression accepts.

    XNA refuses to DXT-compress a texture whose dimensions are not multiples of four -- measured,
    `texture/png_texture_dxt` in the differential corpus -- so proving the rule needs a source on
    each side of it, not only the one that fails (XNAPP-265).
    """
    palette = PIXELS + [(0, 0, 0, 255), (128, 128, 128, 64), (255, 255, 0, 255), (0, 255, 255, 255),
                        (255, 0, 255, 255), (64, 64, 64, 255), (200, 100, 50, 255),
                        (10, 20, 30, 255), (250, 250, 250, 255), (1, 2, 3, 4), (9, 8, 7, 255),
                        (30, 60, 90, 200)]
    raw = b""
    for row in range(4):
        raw += b"\x00"
        for column in range(4):
            raw += bytes(palette[row * 4 + column])

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", 4, 4, 8, 6, 0, 0, 0)
    write(path, b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
          chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def write(path, data):
    with open(path, "wb") as handle:
        handle.write(data)
    RECORD.append((os.path.basename(path), len(data)))


RECORD = []


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", "..", ".."))
    out = os.path.join(repo, "tests/assets/xna40/texture")
    os.makedirs(out, exist_ok=True)

    png(os.path.join(out, "probe.png"))
    bmp(os.path.join(out, "probe.bmp"))
    dib(os.path.join(out, "probe.dib"))
    tga(os.path.join(out, "probe.tga"))
    ppm(os.path.join(out, "probe.ppm"))
    pfm(os.path.join(out, "probe.pfm"))
    hdr(os.path.join(out, "probe.hdr"))
    hdr_flat(os.path.join(out, "probe_flat.hdr"))
    dds(os.path.join(out, "probe.dds"))
    jpg(os.path.join(out, "probe.jpg"))
    png_3x2(os.path.join(out, "probe_3x2.png"))
    png_4x4(os.path.join(out, "probe_4x4.png"))
    # The same PNG bytes under an extension no importer declares: the importer must read the file
    # and not its name.
    png(os.path.join(out, "probe.xyz"))

    # The malformed set. Each is what a reader of that format must refuse.
    write(os.path.join(out, "empty.png"), b"")
    with open(os.path.join(out, "probe.png"), "rb") as handle:
        whole = handle.read()
    write(os.path.join(out, "truncated.png"), whole[:20])
    write(os.path.join(out, "garbage.tga"), b"this is not a targa file\n")
    with open(os.path.join(out, "probe.dds"), "rb") as handle:
        whole = handle.read()
    write(os.path.join(out, "truncated.dds"), whole[:60])

    with open(os.path.join(out, "PROVENANCE.md"), "w", encoding="utf-8") as handle:
        handle.write("# Texture source corpus (generated)\n\n")
        handle.write("Written by `tools/xna-pipeline-oracle/texture/make_texture_fixtures.py`.\n")
        handle.write("Every fixture is 2x2 and carries the same four pixels -- opaque red, opaque\n")
        handle.write("green, opaque blue, half-transparent white -- so a format's own limits show up\n")
        handle.write("in what the importer answers rather than in the source. Nothing here was\n")
        handle.write("downloaded and nothing is third-party content: the bytes of every format but\n")
        handle.write("JPEG are written by the script itself, and the JPEG by the host FFmpeg from\n")
        handle.write("the same raw pixels.\n\n")
        handle.write("| File | Bytes |\n|---|---:|\n")
        for name, size in sorted(RECORD):
            handle.write("| `%s` | %d |\n" % (name, size))
    print("make_texture_fixtures: wrote %d files to %s" % (len(RECORD), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
