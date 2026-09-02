#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Independently validate a `.xnb` file against the published XNA 4.0 container format.

plans/plan_xnapipeline.md XNAP-026.

This is a deliberately *separate* implementation of the reader side of the format, written from
the published Microsoft "XNA Game Studio 4.0 Compiled (XNB) Content Format" specification and
sharing no code with CNA's own C++ reader. A writer and a reader that were built together can
agree with each other and still both be wrong; this script exists so that CNA's output is checked
against the specification by something that has never seen CNA's reader.

It is not a loader: it decodes structure and field layout, not pixels or audio.

Usage:
    xnb_conformance.py <file.xnb> [<file.xnb> ...]
    xnb_conformance.py --expect-reader Microsoft.Xna.Framework.Content.Texture2DReader file.xnb

Exit code 0 when every file conforms, 1 otherwise.
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass, field
from typing import Any


ACCEPTED_PLATFORMS = set("wxmiadXWnupMrPgl")

# Section 3 of the specification: the XNA 4.0 SurfaceFormat numbering.
SURFACE_FORMATS = [
    "Color", "Bgr565", "Bgra5551", "Bgra4444", "Dxt1", "Dxt3", "Dxt5",
    "NormalizedByte2", "NormalizedByte4", "Rgba1010102", "Rg32", "Rgba64",
    "Alpha8", "Single", "Vector2", "Vector4", "HalfSingle", "HalfVector2",
    "HalfVector4", "HdrBlendable",
]

SURFACE_FORMAT_TEXEL_BYTES = {
    "Color": 4, "Bgr565": 2, "Bgra5551": 2, "Bgra4444": 2,
    "NormalizedByte2": 2, "NormalizedByte4": 4, "Rgba1010102": 4, "Rg32": 4,
    "Rgba64": 8, "Alpha8": 1, "Single": 4, "Vector2": 8, "Vector4": 16,
    "HalfSingle": 2, "HalfVector2": 4, "HalfVector4": 8, "HdrBlendable": 8,
}

BLOCK_FORMAT_BYTES = {"Dxt1": 8, "Dxt3": 16, "Dxt5": 16}


def normalize_reader_name(name: str) -> str:
    """Strip .NET assembly qualifiers, leaving the bare type name and its generic arguments.

    A real XNA `.xnb` qualifies a reader that does not live in `Microsoft.Xna.Framework`, and
    qualifies every generic type argument. Both spellings name the same reader, so structural
    checks compare the normalized form.
    """
    result = []
    depth = 0
    index = 0
    while index < len(name):
        character = name[index]
        if character == "[":
            depth += 1
            result.append(character)
        elif character == "]":
            depth -= 1
            result.append(character)
        elif character == ",":
            # A comma at this level begins an assembly qualifier: skip to the end of this
            # argument, which is the matching close bracket at the same depth.
            scan = index
            level = depth
            while scan < len(name):
                if name[scan] == "[":
                    level += 1
                elif name[scan] == "]":
                    if level == depth:
                        break
                    level -= 1
                scan += 1
            index = scan
            continue
        else:
            result.append(character)
        index += 1
    return "".join(result)


class ConformanceError(Exception):
    """A specific way in which a file departs from the specification."""


@dataclass
class Cursor:
    """A bounds-checked read cursor over the decompressed body."""

    data: bytes
    offset: int = 0

    def take(self, count: int) -> bytes:
        if count < 0:
            raise ConformanceError(f"negative read of {count} bytes")
        end = self.offset + count
        if end > len(self.data):
            raise ConformanceError(
                f"read of {count} bytes at offset {self.offset} runs past the "
                f"{len(self.data)}-byte body")
        chunk = self.data[self.offset:end]
        self.offset = end
        return chunk

    def byte(self) -> int:
        return self.take(1)[0]

    def boolean(self) -> bool:
        value = self.byte()
        if value not in (0, 1):
            raise ConformanceError(f"boolean byte is {value}, not 0 or 1")
        return value == 1

    def int32(self) -> int:
        return struct.unpack("<i", self.take(4))[0]

    def uint32(self) -> int:
        return struct.unpack("<I", self.take(4))[0]

    def int64(self) -> int:
        return struct.unpack("<q", self.take(8))[0]

    def uint16(self) -> int:
        return struct.unpack("<H", self.take(2))[0]

    def single(self) -> float:
        return struct.unpack("<f", self.take(4))[0]

    def seven_bit(self) -> int:
        """The .NET Read7BitEncodedInt encoding, per the specification's own C listing."""
        result = 0
        shift = 0
        while True:
            if shift > 28:
                raise ConformanceError("7-bit encoded integer exceeds five bytes")
            value = self.byte()
            result |= (value & 0x7F) << shift
            shift += 7
            if not value & 0x80:
                break
        return result

    def string(self) -> str:
        length = self.seven_bit()
        if length < 0:
            raise ConformanceError(f"string declares a negative byte count ({length})")
        raw = self.take(length)
        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError as error:
            raise ConformanceError(f"string is not well-formed UTF-8: {error}") from error

    def char(self) -> str:
        """One UTF-8 encoded character, read by its leading byte's own length."""
        first = self.data[self.offset:self.offset + 1]
        if not first:
            raise ConformanceError("character read past the end of the body")
        lead = first[0]
        if lead < 0x80:
            length = 1
        elif lead >> 5 == 0b110:
            length = 2
        elif lead >> 4 == 0b1110:
            length = 3
        elif lead >> 3 == 0b11110:
            length = 4
        else:
            raise ConformanceError(f"character has an invalid UTF-8 lead byte 0x{lead:02X}")
        return self.take(length).decode("utf-8")


@dataclass
class Xnb:
    """The structural contents of one `.xnb` file."""

    platform: str = ""
    version: int = 0
    hidef: bool = False
    compressed: bool = False
    declared_size: int = 0
    actual_size: int = 0
    readers: list[tuple[str, int]] = field(default_factory=list)
    shared_resource_count: int = 0
    root_reader: str = ""
    body_consumed: int = 0
    body_size: int = 0
    details: dict[str, Any] = field(default_factory=dict)


def parse_header(raw: bytes) -> Xnb:
    if len(raw) < 10:
        raise ConformanceError(f"file is {len(raw)} bytes, shorter than the 10-byte header")
    if raw[0:3] != b"XNB":
        raise ConformanceError(f"magic bytes are {raw[0:3]!r}, not b'XNB'")

    result = Xnb()
    result.platform = chr(raw[3])
    if result.platform not in ACCEPTED_PLATFORMS:
        raise ConformanceError(f"platform identifier {result.platform!r} is not recognized")
    result.version = raw[4]
    if result.version not in (4, 5):
        raise ConformanceError(f"container version is {result.version}, not 4 or 5")

    flags = raw[5]
    result.hidef = bool(flags & 0x01)
    result.compressed = bool(flags & 0x80)
    if flags & ~0xC1:
        raise ConformanceError(f"flags byte 0x{flags:02X} sets bits the format does not define")

    result.declared_size = struct.unpack("<I", raw[6:10])[0]
    result.actual_size = len(raw)
    if result.declared_size != result.actual_size:
        raise ConformanceError(
            f"header declares {result.declared_size} bytes but the file holds "
            f"{result.actual_size}")
    return result


def read_texture_levels(cursor: Cursor, surface: str, levels: int, label: str) -> list[int]:
    sizes = []
    for level in range(levels):
        size = cursor.uint32()
        cursor.take(size)
        sizes.append(size)
        if size == 0:
            raise ConformanceError(f"{label} level {level} declares zero bytes")
    return sizes


def expected_level_bytes(surface: str, width: int, height: int, depth: int = 1) -> int:
    if surface in BLOCK_FORMAT_BYTES:
        blocks = ((width + 3) // 4) * ((height + 3) // 4)
        return blocks * BLOCK_FORMAT_BYTES[surface] * depth
    return width * height * depth * SURFACE_FORMAT_TEXEL_BYTES[surface]


def check_texture2d(cursor: Cursor, result: Xnb, cube: bool = False,
                    volume: bool = False) -> None:
    format_value = cursor.int32()
    if not 0 <= format_value < len(SURFACE_FORMATS):
        raise ConformanceError(f"surface format {format_value} is not an XNA 4.0 value")
    surface = SURFACE_FORMATS[format_value]

    width = cursor.uint32()
    height = width if cube else cursor.uint32()
    depth = cursor.uint32() if volume else 1
    mips = cursor.uint32()
    if width == 0 or height == 0 or depth == 0 or mips == 0:
        raise ConformanceError("a texture dimension or its mip count is zero")

    faces = 6 if cube else 1
    for face in range(faces):
        for level in range(mips):
            size = cursor.uint32()
            level_width = max(1, width >> level)
            level_height = max(1, height >> level)
            level_depth = max(1, depth >> level)
            expected = expected_level_bytes(surface, level_width, level_height, level_depth)
            if size != expected:
                raise ConformanceError(
                    f"face {face} level {level} declares {size} bytes, but {level_width}x"
                    f"{level_height}x{level_depth} in {surface} occupies {expected}")
            cursor.take(size)

    result.details.update(
        surfaceFormat=surface, width=width, height=height, depth=depth, mipCount=mips,
        faceCount=faces)


def check_sound_effect(cursor: Cursor, result: Xnb) -> None:
    format_size = cursor.uint32()
    if format_size < 16:
        raise ConformanceError(f"WAVEFORMATEX block is {format_size} bytes, fewer than 16")
    start = cursor.offset
    tag = cursor.uint16()
    channels = cursor.uint16()
    sample_rate = cursor.uint32()
    average_bytes = cursor.uint32()
    block_align = cursor.uint16()
    bits = cursor.uint16()
    cursor.take(format_size - (cursor.offset - start))

    data_size = cursor.uint32()
    cursor.take(data_size)
    loop_start = cursor.int32()
    loop_length = cursor.int32()
    duration = cursor.int32()

    if channels == 0:
        raise ConformanceError("WAVEFORMATEX declares zero channels")
    if sample_rate == 0:
        raise ConformanceError("WAVEFORMATEX declares a zero sample rate")
    if tag == 1:
        expected_align = channels * bits // 8
        if block_align != expected_align:
            raise ConformanceError(
                f"PCM block alignment is {block_align}, expected {expected_align}")
        if average_bytes != sample_rate * block_align:
            raise ConformanceError(
                f"PCM average bytes per second is {average_bytes}, expected "
                f"{sample_rate * block_align}")
        if data_size % block_align:
            raise ConformanceError(
                f"PCM data size {data_size} is not a whole number of {block_align}-byte frames")
    if loop_start < 0 or loop_length < 0:
        raise ConformanceError("the loop region has a negative start or length")
    if tag == 1 and loop_start + loop_length > data_size:
        # Only PCM states its loop region in bytes of the stored data. A compressed format's
        # points are expressed against its decoded frames, which this checker does not decode.
        raise ConformanceError("the PCM loop region runs outside the sample data")
    if duration < 0:
        raise ConformanceError(f"duration is negative ({duration})")

    result.details.update(
        formatTag=tag, channels=channels, sampleRate=sample_rate, blockAlign=block_align,
        bitsPerSample=bits, dataSize=data_size, loopStart=loop_start, loopLength=loop_length,
        durationMs=duration)


def check_curve(cursor: Cursor, result: Xnb) -> None:
    pre = cursor.int32()
    post = cursor.int32()
    if not 0 <= pre <= 4 or not 0 <= post <= 4:
        raise ConformanceError(f"curve loop types ({pre}, {post}) are not CurveLoopType values")
    keys = cursor.uint32()
    for index in range(keys):
        cursor.single()
        cursor.single()
        cursor.single()
        cursor.single()
        continuity = cursor.int32()
        if continuity not in (0, 1):
            raise ConformanceError(
                f"key {index} continuity is {continuity}, not CurveContinuity 0 or 1")
    result.details.update(preLoop=pre, postLoop=post, keyCount=keys)


def dispatched(cursor: Cursor, result: Xnb, expected_reader: str) -> None:
    """Consume one polymorphic object header and require the named reader."""
    type_id = cursor.seven_bit()
    if type_id == 0:
        raise ConformanceError(f"expected a {expected_reader} object, found the null reference")
    if type_id > len(result.readers):
        raise ConformanceError(
            f"object type identifier {type_id} exceeds the {len(result.readers)}-entry table")
    actual = result.readers[type_id - 1][0]
    if actual != expected_reader:
        raise ConformanceError(f"expected {expected_reader}, found {actual}")


def check_sprite_font(cursor: Cursor, result: Xnb) -> None:
    dispatched(cursor, result, "Microsoft.Xna.Framework.Content.Texture2DReader")
    check_texture2d(cursor, result)

    def rectangle_list(label: str) -> int:
        dispatched(
            cursor, result,
            "Microsoft.Xna.Framework.Content.ListReader`1"
            "[[Microsoft.Xna.Framework.Rectangle]]")
        count = cursor.uint32()
        for _ in range(count):
            cursor.int32()
            cursor.int32()
            cursor.int32()
            cursor.int32()
        return count

    glyphs = rectangle_list("glyphs")
    cropping = rectangle_list("cropping")

    dispatched(cursor, result,
               "Microsoft.Xna.Framework.Content.ListReader`1[[System.Char]]")
    characters = cursor.uint32()
    previous = None
    for _ in range(characters):
        character = cursor.char()
        if previous is not None and character <= previous:
            raise ConformanceError("the character map is not strictly ascending")
        previous = character

    cursor.int32()   # line spacing
    cursor.single()  # spacing

    dispatched(cursor, result,
               "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3]]")
    kerning = cursor.uint32()
    for _ in range(kerning):
        cursor.single()
        cursor.single()
        cursor.single()

    if cursor.boolean():
        cursor.char()

    if not glyphs == cropping == characters == kerning:
        raise ConformanceError(
            f"the parallel lists differ in length ({glyphs}, {cropping}, {characters}, {kerning})")
    result.details.update(glyphCount=glyphs)


def check_song(cursor: Cursor, result: Xnb) -> None:
    name = cursor.string()
    if not name:
        raise ConformanceError("the streaming file name is empty")
    dispatched(cursor, result, "Microsoft.Xna.Framework.Content.Int32Reader")
    result.details.update(streamingFile=name, durationMs=cursor.int32())


def check_video(cursor: Cursor, result: Xnb) -> None:
    dispatched(cursor, result, "Microsoft.Xna.Framework.Content.StringReader")
    name = cursor.string()
    fields = {}
    for label in ("durationMs", "width", "height"):
        dispatched(cursor, result, "Microsoft.Xna.Framework.Content.Int32Reader")
        fields[label] = cursor.int32()
    dispatched(cursor, result, "Microsoft.Xna.Framework.Content.SingleReader")
    fields["framesPerSecond"] = cursor.single()
    dispatched(cursor, result, "Microsoft.Xna.Framework.Content.Int32Reader")
    fields["soundtrackType"] = cursor.int32()
    result.details.update(streamingFile=name, **fields)


# Value-type payload readers, keyed by the reader that declares them. Used for the elements of a
# collection root; a reference-typed element dispatches through the type table instead.
VALUE_ELEMENT_READERS = {
    "Microsoft.Xna.Framework.Content.ByteReader": lambda c: c.take(1),
    "Microsoft.Xna.Framework.Content.SByteReader": lambda c: c.take(1),
    "Microsoft.Xna.Framework.Content.Int16Reader": lambda c: c.take(2),
    "Microsoft.Xna.Framework.Content.UInt16Reader": lambda c: c.take(2),
    "Microsoft.Xna.Framework.Content.Int32Reader": lambda c: c.int32(),
    "Microsoft.Xna.Framework.Content.UInt32Reader": lambda c: c.uint32(),
    "Microsoft.Xna.Framework.Content.Int64Reader": lambda c: c.int64(),
    "Microsoft.Xna.Framework.Content.UInt64Reader": lambda c: c.take(8),
    "Microsoft.Xna.Framework.Content.SingleReader": lambda c: c.single(),
    "Microsoft.Xna.Framework.Content.DoubleReader": lambda c: c.take(8),
    "Microsoft.Xna.Framework.Content.BooleanReader": lambda c: c.boolean(),
    "Microsoft.Xna.Framework.Content.CharReader": lambda c: c.char(),
    "Microsoft.Xna.Framework.Content.Vector2Reader": lambda c: c.take(8),
    "Microsoft.Xna.Framework.Content.Vector3Reader": lambda c: c.take(12),
    "Microsoft.Xna.Framework.Content.Vector4Reader": lambda c: c.take(16),
    "Microsoft.Xna.Framework.Content.MatrixReader": lambda c: c.take(64),
    "Microsoft.Xna.Framework.Content.QuaternionReader": lambda c: c.take(16),
    "Microsoft.Xna.Framework.Content.ColorReader": lambda c: c.take(4),
    "Microsoft.Xna.Framework.Content.PointReader": lambda c: c.take(8),
    "Microsoft.Xna.Framework.Content.RectangleReader": lambda c: c.take(16),
    "Microsoft.Xna.Framework.Content.PlaneReader": lambda c: c.take(16),
    "Microsoft.Xna.Framework.Content.BoundingBoxReader": lambda c: c.take(24),
    "Microsoft.Xna.Framework.Content.BoundingSphereReader": lambda c: c.take(16),
    "Microsoft.Xna.Framework.Content.RayReader": lambda c: c.take(24),
    "Microsoft.Xna.Framework.Content.TimeSpanReader": lambda c: c.int64(),
    "Microsoft.Xna.Framework.Content.DateTimeReader": lambda c: c.take(8),
    "Microsoft.Xna.Framework.Content.DecimalReader": lambda c: c.take(16),
}

# The element reader a closed generic collection's own type argument implies.
ELEMENT_READER_FOR_TYPE = {
    "System.Byte": "ByteReader", "System.SByte": "SByteReader",
    "System.Int16": "Int16Reader", "System.UInt16": "UInt16Reader",
    "System.Int32": "Int32Reader", "System.UInt32": "UInt32Reader",
    "System.Int64": "Int64Reader", "System.UInt64": "UInt64Reader",
    "System.Single": "SingleReader", "System.Double": "DoubleReader",
    "System.Boolean": "BooleanReader", "System.Char": "CharReader",
    "System.String": "StringReader",
    "Microsoft.Xna.Framework.Vector2": "Vector2Reader",
    "Microsoft.Xna.Framework.Vector3": "Vector3Reader",
    "Microsoft.Xna.Framework.Vector4": "Vector4Reader",
    "Microsoft.Xna.Framework.Matrix": "MatrixReader",
    "Microsoft.Xna.Framework.Quaternion": "QuaternionReader",
    "Microsoft.Xna.Framework.Color": "ColorReader",
    "Microsoft.Xna.Framework.Point": "PointReader",
    "Microsoft.Xna.Framework.Rectangle": "RectangleReader",
    "Microsoft.Xna.Framework.Plane": "PlaneReader",
    "Microsoft.Xna.Framework.BoundingBox": "BoundingBoxReader",
    "Microsoft.Xna.Framework.BoundingSphere": "BoundingSphereReader",
    "Microsoft.Xna.Framework.Ray": "RayReader",
}

REFERENCE_ELEMENT_TYPES = {"System.String"}


def collection_element_type(reader_name: str) -> str:
    start = reader_name.find("[[")
    end = reader_name.rfind("]]")
    if start < 0 or end < 0:
        raise ConformanceError(f"'{reader_name}' is not a closed generic reader name")
    return reader_name[start + 2:end]


def check_collection(cursor: Cursor, result: Xnb) -> None:
    element_type = collection_element_type(result.root_reader)
    short = ELEMENT_READER_FOR_TYPE.get(element_type)
    if short is None:
        raise ConformanceError(
            f"this checker does not know the element type '{element_type}'")
    element_reader = "Microsoft.Xna.Framework.Content." + short

    count = cursor.uint32()
    for index in range(count):
        if element_type in REFERENCE_ELEMENT_TYPES:
            dispatched(cursor, result, element_reader)
            cursor.string()
        else:
            VALUE_ELEMENT_READERS[element_reader](cursor)
    result.details.update(elementType=element_type, count=count)


ROOT_CHECKS = {
    "Microsoft.Xna.Framework.Content.Texture2DReader":
        lambda c, r: check_texture2d(c, r),
    "Microsoft.Xna.Framework.Content.Texture3DReader":
        lambda c, r: check_texture2d(c, r, volume=True),
    "Microsoft.Xna.Framework.Content.TextureCubeReader":
        lambda c, r: check_texture2d(c, r, cube=True),
    "Microsoft.Xna.Framework.Content.SoundEffectReader": check_sound_effect,
    "Microsoft.Xna.Framework.Content.CurveReader": check_curve,
    "Microsoft.Xna.Framework.Content.SpriteFontReader": check_sprite_font,
    "Microsoft.Xna.Framework.Content.SongReader": check_song,
    "Microsoft.Xna.Framework.Content.VideoReader": check_video,
}


def check_file(path: str, expect_reader: str | None) -> Xnb:
    with open(path, "rb") as handle:
        raw = handle.read()

    result = parse_header(raw)
    if result.compressed:
        raise ConformanceError(
            "the file is compressed; this checker validates uncompressed containers only")

    body = raw[10:]
    result.body_size = len(body)
    cursor = Cursor(body)

    count = cursor.seven_bit()
    if count == 0:
        raise ConformanceError("the type-reader table is empty, so no root object can dispatch")
    for index in range(count):
        name = cursor.string()
        version = cursor.int32()
        if not name:
            raise ConformanceError(f"type-reader entry {index} has an empty name")
        if version != 0:
            raise ConformanceError(
                f"type-reader '{name}' declares version {version}; every built-in XNA 4.0 reader "
                "is version 0")
        result.readers.append((normalize_reader_name(name), version))

    if len({name for name, _ in result.readers}) != len(result.readers):
        raise ConformanceError("the type-reader table repeats a reader name")

    result.shared_resource_count = cursor.seven_bit()
    if result.shared_resource_count < 0:
        raise ConformanceError("the shared-resource count is negative")

    root_id = cursor.seven_bit()
    if root_id == 0:
        raise ConformanceError("the root asset is the null reference")
    if root_id > len(result.readers):
        raise ConformanceError(
            f"root type identifier {root_id} exceeds the {len(result.readers)}-entry table")
    result.root_reader = result.readers[root_id - 1][0]

    if expect_reader is not None and result.root_reader != expect_reader:
        raise ConformanceError(
            f"root reader is {result.root_reader}, expected {expect_reader}")

    check = ROOT_CHECKS.get(result.root_reader)
    if check is None and (
            result.root_reader.startswith("Microsoft.Xna.Framework.Content.ListReader`1[[") or
            result.root_reader.startswith("Microsoft.Xna.Framework.Content.ArrayReader`1[[")):
        check = check_collection
    if check is None:
        raise ConformanceError(
            f"this checker does not know how to validate a {result.root_reader} payload")
    check(cursor, result)

    result.body_consumed = cursor.offset
    if result.shared_resource_count == 0 and cursor.offset != len(body):
        raise ConformanceError(
            f"the root payload consumed {cursor.offset} of {len(body)} body bytes; "
            f"{len(body) - cursor.offset} are unaccounted for")

    unused = {name for name, _ in result.readers}
    if len(result.readers) > 1 and result.root_reader in unused and len(unused) == 1:
        raise ConformanceError("the type-reader table repeats the root reader")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", help=".xnb files to validate")
    parser.add_argument("--expect-reader", default=None,
                        help="require this root ContentTypeReader name")
    parser.add_argument("--quiet", action="store_true", help="print only failures")
    arguments = parser.parse_args()

    failures = 0
    for path in arguments.files:
        try:
            result = check_file(path, arguments.expect_reader)
        except (ConformanceError, OSError) as error:
            print(f"FAIL {path}: {error}", file=sys.stderr)
            failures += 1
            continue
        if not arguments.quiet:
            def short(name: str) -> str:
                head = name.split("`", 1)[0]
                return head.rsplit(".", 1)[-1] + ("`..." if "`" in name else "")

            readers = ", ".join(short(name) for name, _ in result.readers)
            print(f"OK   {path}: {short(result.root_reader)}, platform "
                  f"'{result.platform}', version {result.version}, "
                  f"{'HiDef' if result.hidef else 'Reach'}, {result.actual_size} bytes, "
                  f"readers [{readers}], {result.details}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
