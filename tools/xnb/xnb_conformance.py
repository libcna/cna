#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Independent, specification-based conformance parser for XNB files.

plans/plan_xnapipeline.md ``XNAP-43``.

This program is deliberately a *second implementation* of the XNB container and
of the built-in reader payloads. It shares no code, no headers and no constants
with CNA, and it is written against the format description recorded in
``plans/plan_xnapipeline.md`` section 2 plus the byte evidence in the committed
fixtures -- not by transcribing CNA's C++ readers. Its whole value is that it
can disagree with them.

It also refuses to be lenient. Anything the specification does not permit is an
error, and a file with even one unconsumed trailing byte fails: a writer bug
that leaves a stray byte behind is exactly the kind of defect a permissive
parser hides.

Usage::

    xnb_conformance.py <file-or-directory> [...]      # human-readable report
    xnb_conformance.py --json <file-or-directory>     # machine-readable report
    xnb_conformance.py --expect <manifest.json> <file>

``--expect`` compares the parsed result against an expected-value manifest, the
form ``XNAP-31`` uses to describe what a real XNA 4.0 runtime should observe.

Exit code 0 means every input parsed (and matched every expectation given);
1 means at least one input failed.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys

# --- limits -------------------------------------------------------------------
# Bounds applied to every count read out of a file. They exist so a corrupt or
# hostile count cannot make this program allocate wildly before the truncation
# it implies is detected.
MAX_FILE_BYTES = 256 * 1024 * 1024
MAX_TYPE_READERS = 4096
MAX_SHARED_RESOURCES = 1_000_000
MAX_COLLECTION_ELEMENTS = 10_000_000
MAX_STRING_BYTES = 1 * 1024 * 1024
MAX_BLOB_BYTES = 256 * 1024 * 1024

HEADER_BYTES = 10

# Platform identifiers. Only the first three were ever produced by Microsoft's
# own XNA 4.0 Content Pipeline; the rest come from later implementations and are
# reported as "extended" so a report can never imply XNA 4.0 provenance.
XNA40_PLATFORMS = {"w": "Windows", "m": "Windows Phone 7", "x": "Xbox 360"}
EXTENDED_PLATFORMS = {
    "i": "iOS", "a": "Android", "d": "DesktopGL", "X": "Xbox One",
    "W": "Windows Store", "n": "Nintendo Switch", "u": "Ouya",
    "p": "PlayStation Mobile", "M": "Windows Phone 8", "r": "Raspberry Pi",
    "P": "PlayStation 4", "g": "Windows OpenGL (legacy)", "l": "Linux (legacy)",
}

# SurfaceFormat ordinals, XNA 4.0 numbering.
SURFACE_FORMATS = {
    0: "Color", 1: "Bgr565", 2: "Bgra5551", 3: "Bgra4444", 4: "Dxt1", 5: "Dxt3",
    6: "Dxt5", 7: "NormalizedByte2", 8: "NormalizedByte4", 9: "Rgba1010102",
    10: "Rg32", 11: "Rgba64", 12: "Alpha8", 13: "Single", 14: "Vector2",
    15: "Vector4", 16: "HalfSingle", 17: "HalfVector2", 18: "HalfVector4",
    19: "HdrBlendable",
}
# Container version 4 used an earlier, sparser numbering.
LEGACY_SURFACE_FORMATS = {1: "Color", 28: "Dxt1", 30: "Dxt3", 32: "Dxt5"}

BLOCK_COMPRESSED = {"Dxt1", "Dxt3", "Dxt5"}
BYTES_PER_PIXEL = {
    "Color": 4, "Bgr565": 2, "Bgra5551": 2, "Bgra4444": 2, "NormalizedByte2": 2,
    "NormalizedByte4": 4, "Rgba1010102": 4, "Rg32": 4, "Rgba64": 8, "Alpha8": 1,
    "Single": 4, "Vector2": 8, "Vector4": 16, "HalfSingle": 2, "HalfVector2": 4,
    "HalfVector4": 8, "HdrBlendable": 8,
}


class XnbError(Exception):
    """Raised for any file that does not satisfy the format."""


class Cursor:
    """A bounds-checked little-endian reader over one in-memory payload."""

    def __init__(self, data: bytes, origin: str) -> None:
        self.data = data
        self.pos = 0
        self.origin = origin

    def fail(self, message: str) -> None:
        raise XnbError(f"{self.origin}: {message} (at byte {self.pos})")

    def take(self, count: int) -> bytes:
        if count < 0:
            self.fail("negative read length")
        if count > len(self.data) - self.pos:
            self.fail(f"truncated: needs {count} more bytes")
        chunk = self.data[self.pos:self.pos + count]
        self.pos += count
        return chunk

    def u8(self) -> int:
        return self.take(1)[0]

    def i8(self) -> int:
        return struct.unpack("<b", self.take(1))[0]

    def boolean(self) -> bool:
        value = self.u8()
        if value not in (0, 1):
            self.fail(f"a Boolean must be 0 or 1, not {value}")
        return value == 1

    def u16(self) -> int:
        return struct.unpack("<H", self.take(2))[0]

    def i16(self) -> int:
        return struct.unpack("<h", self.take(2))[0]

    def u32(self) -> int:
        return struct.unpack("<I", self.take(4))[0]

    def i32(self) -> int:
        return struct.unpack("<i", self.take(4))[0]

    def u64(self) -> int:
        return struct.unpack("<Q", self.take(8))[0]

    def i64(self) -> int:
        return struct.unpack("<q", self.take(8))[0]

    def f32(self) -> float:
        return struct.unpack("<f", self.take(4))[0]

    def f64(self) -> float:
        return struct.unpack("<d", self.take(8))[0]

    def seven_bit_int(self) -> int:
        """.NET BinaryReader.Read7BitEncodedInt: at most five bytes."""
        value = 0
        for index in range(5):
            byte = self.u8()
            if index == 4 and byte > 0x0F:
                self.fail("too many bytes in a 7-bit encoded integer")
            value |= (byte & 0x7F) << (7 * index)
            if byte & 0x80 == 0:
                return value - (1 << 32) if value >= (1 << 31) else value
        self.fail("unterminated 7-bit encoded integer")
        return 0

    def string(self) -> str:
        length = self.seven_bit_int()
        if length < 0 or length > MAX_STRING_BYTES:
            self.fail(f"string length {length} is out of range")
        raw = self.take(length)
        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError as error:
            self.fail(f"string is not well-formed UTF-8: {error}")
            return ""

    def char(self) -> str:
        """One UTF-8-encoded code unit, as .NET BinaryReader.ReadChar writes it."""
        lead = self.data[self.pos] if self.pos < len(self.data) else self.fail("truncated char")
        if lead < 0x80:
            length = 1
        elif 0xC2 <= lead <= 0xDF:
            length = 2
        elif 0xE0 <= lead <= 0xEF:
            length = 3
        else:
            self.fail(f"0x{lead:02X} does not start a System.Char encoding")
            length = 1
        return self.take(length).decode("utf-8")

    def blob(self) -> bytes:
        length = self.i32()
        if length < 0 or length > MAX_BLOB_BYTES:
            self.fail(f"blob length {length} is out of range")
        return self.take(length)

    def collection_count(self, what: str) -> int:
        count = self.i32()
        if count < 0 or count > MAX_COLLECTION_ELEMENTS:
            self.fail(f"{what} count {count} is out of range")
        return count

    def vector3(self):
        return [self.f32(), self.f32(), self.f32()]

    def matrix(self):
        return [self.f32() for _ in range(16)]

    def rectangle(self):
        return [self.i32(), self.i32(), self.i32(), self.i32()]


def strip_assembly(name: str) -> str:
    """Reduce an assembly-qualified .NET type name to its bare canonical form.

    The grammar this follows is .NET's own::

        type := name [ '[' arg (',' arg)* ']' ] [ ',' assembly-qualifier ]
        arg  := '[' type ']'

    so ``ListReader`1[[System.String, mscorlib, Version=...]]`` reduces to
    ``ListReader`1[[System.String]]``, recursively for nested generics.
    """
    canonical, _ = _parse_type_name(name, 0)
    return canonical


def _parse_type_name(text: str, index: int):
    start = index
    while index < len(text) and text[index] not in "[,]":
        index += 1
    base = text[start:index].strip()
    arguments = []
    if index < len(text) and text[index] == "[":
        index += 1
        while True:
            while index < len(text) and text[index] == " ":
                index += 1
            if index >= len(text) or text[index] != "[":
                raise XnbError(f"malformed generic argument list in '{text}'")
            index += 1
            argument, index = _parse_type_name(text, index)
            arguments.append(argument)
            # Whatever follows the argument's own type name is its assembly
            # qualifier; skip to the ']' that closes this argument.
            depth = 0
            while index < len(text):
                character = text[index]
                if character == "[":
                    depth += 1
                elif character == "]":
                    if depth == 0:
                        break
                    depth -= 1
                index += 1
            if index >= len(text):
                raise XnbError(f"unbalanced brackets in '{text}'")
            index += 1
            while index < len(text) and text[index] == " ":
                index += 1
            if index < len(text) and text[index] == ",":
                index += 1
                continue
            if index < len(text) and text[index] == "]":
                index += 1
                break
            raise XnbError(f"expected ',' or ']' after a generic argument in '{text}'")
    if arguments:
        base += "[" + ",".join("[" + argument + "]" for argument in arguments) + "]"
    return base, index


def mip_dimensions(width: int, height: int, depth: int, level: int):
    for _ in range(level):
        width = max(1, width // 2)
        height = max(1, height // 2)
        depth = max(1, depth // 2)
    return width, height, depth


def level_byte_size(surface: str, width: int, height: int, depth: int) -> int:
    if surface in BLOCK_COMPRESSED:
        blocks = ((width + 3) // 4) * ((height + 3) // 4)
        return blocks * (8 if surface == "Dxt1" else 16) * depth
    if surface not in BYTES_PER_PIXEL:
        raise XnbError(f"no byte size is defined for SurfaceFormat {surface}")
    return width * height * depth * BYTES_PER_PIXEL[surface]


class Reader:
    """Decodes one object graph, dispatching through the file's reader table."""

    def __init__(self, cursor: Cursor, table, version: int, shared_count: int) -> None:
        self.cursor = cursor
        self.table = table
        self.version = version
        self.shared_count = shared_count

    def reader_reference(self, expected=None, optional=False):
        index = self.cursor.seven_bit_int()
        if index == 0:
            if not optional:
                self.cursor.fail("a null object appeared where a value was required")
            return None
        if index < 0 or index > len(self.table):
            self.cursor.fail(
                f"type-reader index {index} is outside the {len(self.table)}-entry table")
        entry = self.table[index - 1]
        if expected is not None and entry["canonical"] != expected:
            self.cursor.fail(
                f"expected reader '{expected}' but the object dispatches to "
                f"'{entry['canonical']}'")
        return entry

    def shared_reference(self, required: bool):
        index = self.cursor.seven_bit_int()
        if index < 0 or index > self.shared_count:
            self.cursor.fail(
                f"shared-resource reference {index} is outside the "
                f"{self.shared_count}-entry table")
        if required and index == 0:
            self.cursor.fail("a required shared-resource reference is null")
        return index

    # -- payload decoders, one per built-in reader ------------------------------

    def texture(self, kind: str):
        raw = self.cursor.i32()
        table = SURFACE_FORMATS if self.version >= 5 else LEGACY_SURFACE_FORMATS
        if raw not in table:
            self.cursor.fail(
                f"SurfaceFormat {raw} is not defined for container version {self.version}")
        surface = table[raw]
        width = self.cursor.i32()
        if kind == "TextureCube":
            height, depth, faces = width, 1, 6
        elif kind == "Texture3D":
            height = self.cursor.i32()
            depth = self.cursor.i32()
            faces = 1
        else:
            height = self.cursor.i32()
            depth, faces = 1, 1
        mips = self.cursor.i32()
        if width <= 0 or height <= 0 or depth <= 0 or mips <= 0:
            self.cursor.fail("texture dimensions and mip count must be positive")

        levels = []
        for _ in range(faces):
            for level in range(mips):
                lw, lh, ld = mip_dimensions(width, height, depth, level)
                payload = self.cursor.blob()
                expected = level_byte_size(surface, lw, lh, ld)
                if len(payload) != expected:
                    self.cursor.fail(
                        f"mip level {level} carries {len(payload)} bytes but {lw}x{lh}x{ld} "
                        f"{surface} needs {expected}")
                levels.append(payload)
        return {
            "kind": kind, "surfaceFormat": surface, "width": width, "height": height,
            "depth": depth, "faceCount": faces, "mipCount": mips,
            "levelByteSizes": [len(level) for level in levels],
            "levelDigests": [_digest(level) for level in levels],
        }

    def value_list(self, expected_reader: str, element):
        self.reader_reference(expected_reader)
        count = self.cursor.collection_count(expected_reader)
        return [element() for _ in range(count)]

    def sprite_font(self):
        self.reader_reference("Microsoft.Xna.Framework.Content.Texture2DReader")
        atlas = self.texture("Texture2D")
        glyphs = self.value_list(
            "Microsoft.Xna.Framework.Content.ListReader`1"
            "[[Microsoft.Xna.Framework.Rectangle]]", self.cursor.rectangle)
        cropping = self.value_list(
            "Microsoft.Xna.Framework.Content.ListReader`1"
            "[[Microsoft.Xna.Framework.Rectangle]]", self.cursor.rectangle)
        characters = self.value_list(
            "Microsoft.Xna.Framework.Content.ListReader`1[[System.Char]]", self.cursor.char)
        line_spacing = self.cursor.i32()
        spacing = self.cursor.f32()
        kerning = self.value_list(
            "Microsoft.Xna.Framework.Content.ListReader`1"
            "[[Microsoft.Xna.Framework.Vector3]]", self.cursor.vector3)
        default_character = self.cursor.char() if self.cursor.boolean() else None
        if not (len(glyphs) == len(cropping) == len(characters) == len(kerning)):
            self.cursor.fail("SpriteFont glyph, cropping, character and kerning counts differ")
        return {
            "atlas": atlas, "glyphs": glyphs, "cropping": cropping,
            "characters": characters, "lineSpacing": line_spacing, "spacing": spacing,
            "kerning": kerning, "defaultCharacter": default_character,
        }

    def sound_effect(self):
        format_length = self.cursor.u32()
        if format_length < 16 or format_length > MAX_STRING_BYTES:
            self.cursor.fail(f"WAVEFORMATEX block length {format_length} is out of range")
        result = {
            "formatTag": self.cursor.u16(), "channels": self.cursor.u16(),
            "sampleRate": self.cursor.u32(), "averageBytesPerSecond": self.cursor.u32(),
            "blockAlign": self.cursor.u16(), "bitsPerSample": self.cursor.u16(),
        }
        if format_length > 16:
            declared = self.cursor.u16()
            remaining = format_length - 18
            if remaining < 0:
                self.cursor.fail("WAVEFORMATEX block is too small for its cbSize field")
            extension = self.cursor.take(remaining)
            if declared > len(extension):
                self.cursor.fail("WAVEFORMATEX cbSize exceeds its own block")
            result["extensionByteCount"] = len(extension)
        else:
            result["extensionByteCount"] = 0
        samples = self.cursor.blob()
        result["sampleByteCount"] = len(samples)
        result["sampleDigest"] = _digest(samples)
        result["loopStart"] = self.cursor.i32()
        result["loopLength"] = self.cursor.i32()
        result["durationMs"] = self.cursor.u32()
        return result

    def song(self):
        media = self.cursor.string()
        self.reader_reference("Microsoft.Xna.Framework.Content.Int32Reader")
        return {"mediaPath": media, "durationMs": self.cursor.i32()}

    def video(self):
        self.reader_reference("Microsoft.Xna.Framework.Content.StringReader")
        media = self.cursor.string()

        def boxed_int():
            self.reader_reference("Microsoft.Xna.Framework.Content.Int32Reader")
            return self.cursor.i32()

        duration = boxed_int()
        width = boxed_int()
        height = boxed_int()
        self.reader_reference("Microsoft.Xna.Framework.Content.SingleReader")
        fps = self.cursor.f32()
        soundtrack = boxed_int()
        return {"mediaPath": media, "durationMs": duration, "width": width,
                "height": height, "framesPerSecond": fps, "soundtrackType": soundtrack}

    def curve(self):
        pre = self.cursor.i32()
        post = self.cursor.i32()
        count = self.cursor.collection_count("Curve keys")
        keys = []
        for _ in range(count):
            keys.append({
                "position": self.cursor.f32(), "value": self.cursor.f32(),
                "tangentIn": self.cursor.f32(), "tangentOut": self.cursor.f32(),
                "continuity": self.cursor.i32(),
            })
        return {"preLoop": pre, "postLoop": post, "keys": keys}

    def vertex_declaration(self):
        stride = self.cursor.i32()
        count = self.cursor.collection_count("vertex elements")
        if stride <= 0:
            self.cursor.fail("a vertex declaration's stride must be positive")
        elements = []
        for _ in range(count):
            elements.append({
                "offset": self.cursor.i32(), "format": self.cursor.i32(),
                "usage": self.cursor.i32(), "usageIndex": self.cursor.i32(),
            })
        return {"stride": stride, "elements": elements}

    def bone_reference(self, bone_count: int) -> int:
        raw = self.cursor.u8() if bone_count < 255 else self.cursor.u32()
        if raw > bone_count:
            self.cursor.fail(f"bone reference {raw} is outside the {bone_count}-bone table")
        return -1 if raw == 0 else raw - 1

    def model(self):
        bone_count = self.cursor.u32()
        if bone_count > MAX_COLLECTION_ELEMENTS:
            self.cursor.fail(f"bone count {bone_count} is out of range")
        bones = []
        for _ in range(bone_count):
            self.reader_reference("Microsoft.Xna.Framework.Content.StringReader")
            bones.append({"name": self.cursor.string(), "transform": self.cursor.matrix()})
        for bone in bones:
            bone["parent"] = self.bone_reference(bone_count)
            children = self.cursor.u32()
            if children > MAX_COLLECTION_ELEMENTS:
                self.cursor.fail("bone child count is out of range")
            bone["children"] = [self.bone_reference(bone_count) for _ in range(children)]

        meshes = []
        for _ in range(self.cursor.collection_count("meshes")):
            self.reader_reference("Microsoft.Xna.Framework.Content.StringReader")
            mesh = {
                "name": self.cursor.string(),
                "parentBone": self.bone_reference(bone_count),
                "boundingSphere": [self.cursor.f32() for _ in range(4)],
            }
            mesh["tag"] = self.reader_reference(optional=True)
            if mesh["tag"] is not None:
                self.cursor.fail("this parser does not decode a non-null mesh Tag")
            parts = []
            for _ in range(self.cursor.collection_count("mesh parts")):
                part = {
                    "vertexOffset": self.cursor.i32(), "numVertices": self.cursor.i32(),
                    "startIndex": self.cursor.i32(), "primitiveCount": self.cursor.i32(),
                }
                if self.reader_reference(optional=True) is not None:
                    self.cursor.fail("this parser does not decode a non-null mesh-part Tag")
                part["vertexBuffer"] = self.shared_reference(required=True)
                part["indexBuffer"] = self.shared_reference(required=True)
                part["effect"] = self.shared_reference(required=True)
                parts.append(part)
            mesh["parts"] = parts
            meshes.append(mesh)

        root = self.bone_reference(bone_count)
        if self.reader_reference(optional=True) is not None:
            self.cursor.fail("this parser does not decode a non-null Model Tag")
        return {"bones": bones, "meshes": meshes, "rootBone": root}

    def shared_resource(self, entry):
        name = entry["canonical"]
        if name == "Microsoft.Xna.Framework.Content.VertexBufferReader":
            declaration = self.vertex_declaration()
            count = self.cursor.u32()
            payload = self.cursor.take(count * declaration["stride"])
            return {"reader": name, "declaration": declaration, "vertexCount": count,
                    "byteCount": len(payload), "digest": _digest(payload)}
        if name == "Microsoft.Xna.Framework.Content.IndexBufferReader":
            sixteen = self.cursor.boolean()
            payload = self.cursor.blob()
            width = 2 if sixteen else 4
            if len(payload) % width != 0:
                self.cursor.fail("index payload is not a whole number of indices")
            return {"reader": name, "indexElementSize": width,
                    "indexCount": len(payload) // width, "digest": _digest(payload)}
        if name == "Microsoft.Xna.Framework.Content.BasicEffectReader":
            return {"reader": name, "texture": self.cursor.string(),
                    "diffuse": self.cursor.vector3(), "emissive": self.cursor.vector3(),
                    "specular": self.cursor.vector3(),
                    "specularPower": self.cursor.f32(), "alpha": self.cursor.f32(),
                    "vertexColorEnabled": self.cursor.boolean()}
        if name == "Microsoft.Xna.Framework.Content.AlphaTestEffectReader":
            return {"reader": name, "texture": self.cursor.string(),
                    "alphaFunction": self.cursor.i32(), "referenceAlpha": self.cursor.u32(),
                    "diffuse": self.cursor.vector3(), "alpha": self.cursor.f32(),
                    "vertexColorEnabled": self.cursor.boolean()}
        if name == "Microsoft.Xna.Framework.Content.DualTextureEffectReader":
            return {"reader": name, "texture": self.cursor.string(),
                    "texture2": self.cursor.string(), "diffuse": self.cursor.vector3(),
                    "alpha": self.cursor.f32(),
                    "vertexColorEnabled": self.cursor.boolean()}
        if name == "Microsoft.Xna.Framework.Content.EnvironmentMapEffectReader":
            return {"reader": name, "texture": self.cursor.string(),
                    "environmentMap": self.cursor.string(),
                    "environmentMapAmount": self.cursor.f32(),
                    "environmentMapSpecular": self.cursor.vector3(),
                    "fresnelFactor": self.cursor.f32(), "diffuse": self.cursor.vector3(),
                    "emissive": self.cursor.vector3(), "alpha": self.cursor.f32()}
        if name == "Microsoft.Xna.Framework.Content.SkinnedEffectReader":
            return {"reader": name, "texture": self.cursor.string(),
                    "weightsPerVertex": self.cursor.i32(),
                    "diffuse": self.cursor.vector3(), "emissive": self.cursor.vector3(),
                    "specular": self.cursor.vector3(),
                    "specularPower": self.cursor.f32(), "alpha": self.cursor.f32()}
        raise XnbError(f"{self.cursor.origin}: no decoder for shared resource '{name}'")

    def root(self, entry):
        name = entry["canonical"]
        simple = {
            "Microsoft.Xna.Framework.Content.BooleanReader": self.cursor.boolean,
            "Microsoft.Xna.Framework.Content.ByteReader": self.cursor.u8,
            "Microsoft.Xna.Framework.Content.SByteReader": self.cursor.i8,
            "Microsoft.Xna.Framework.Content.Int16Reader": self.cursor.i16,
            "Microsoft.Xna.Framework.Content.UInt16Reader": self.cursor.u16,
            "Microsoft.Xna.Framework.Content.Int32Reader": self.cursor.i32,
            "Microsoft.Xna.Framework.Content.UInt32Reader": self.cursor.u32,
            "Microsoft.Xna.Framework.Content.Int64Reader": self.cursor.i64,
            "Microsoft.Xna.Framework.Content.UInt64Reader": self.cursor.u64,
            "Microsoft.Xna.Framework.Content.SingleReader": self.cursor.f32,
            "Microsoft.Xna.Framework.Content.DoubleReader": self.cursor.f64,
            "Microsoft.Xna.Framework.Content.CharReader": self.cursor.char,
            "Microsoft.Xna.Framework.Content.StringReader": self.cursor.string,
            "Microsoft.Xna.Framework.Content.RectangleReader": self.cursor.rectangle,
            "Microsoft.Xna.Framework.Content.Vector3Reader": self.cursor.vector3,
            "Microsoft.Xna.Framework.Content.MatrixReader": self.cursor.matrix,
        }
        if name in simple:
            return simple[name]()
        if name == "Microsoft.Xna.Framework.Content.Texture2DReader":
            return self.texture("Texture2D")
        if name == "Microsoft.Xna.Framework.Content.Texture3DReader":
            return self.texture("Texture3D")
        if name == "Microsoft.Xna.Framework.Content.TextureCubeReader":
            return self.texture("TextureCube")
        if name == "Microsoft.Xna.Framework.Content.SpriteFontReader":
            return self.sprite_font()
        if name == "Microsoft.Xna.Framework.Content.SoundEffectReader":
            return self.sound_effect()
        if name == "Microsoft.Xna.Framework.Content.SongReader":
            return self.song()
        if name == "Microsoft.Xna.Framework.Content.VideoReader":
            return self.video()
        if name == "Microsoft.Xna.Framework.Content.CurveReader":
            return self.curve()
        if name == "Microsoft.Xna.Framework.Content.ModelReader":
            return self.model()
        if name == "Microsoft.Xna.Framework.Content.EffectReader":
            payload = self.cursor.blob()
            return {"bytecodeByteCount": len(payload), "digest": _digest(payload)}
        if name.startswith("Microsoft.Xna.Framework.Content.ListReader`1[["):
            return self.generic_list(name)
        raise XnbError(f"{self.cursor.origin}: no decoder for root reader '{name}'")

    def generic_list(self, name: str):
        argument = name[len("Microsoft.Xna.Framework.Content.ListReader`1[["):-2]
        count = self.cursor.collection_count("list")
        reference_types = {"System.String"}
        items = []
        for _ in range(count):
            if argument in reference_types:
                self.reader_reference(
                    "Microsoft.Xna.Framework.Content." +
                    argument.rsplit(".", 1)[-1] + "Reader")
            items.append(self.value_of(argument))
        return items

    def value_of(self, type_name: str):
        decoders = {
            "System.String": self.cursor.string,
            "System.Char": self.cursor.char,
            "System.Int32": self.cursor.i32,
            "System.Single": self.cursor.f32,
            "Microsoft.Xna.Framework.Rectangle": self.cursor.rectangle,
            "Microsoft.Xna.Framework.Vector3": self.cursor.vector3,
        }
        if type_name not in decoders:
            raise XnbError(f"{self.cursor.origin}: no decoder for element type '{type_name}'")
        return decoders[type_name]()


def _digest(data: bytes) -> str:
    """FNV-1a 64-bit of a payload, so a report can compare bytes without carrying them."""
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


def parse(path: str) -> dict:
    """Parses one `.xnb` file completely and returns a structured report."""
    size = os.path.getsize(path)
    if size > MAX_FILE_BYTES:
        raise XnbError(f"{path}: {size} bytes exceeds this parser's ceiling")
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < HEADER_BYTES:
        raise XnbError(f"{path}: truncated before its {HEADER_BYTES}-byte header")
    if data[0:3] != b"XNB":
        raise XnbError(f"{path}: does not start with the 'XNB' magic")

    platform = chr(data[3])
    if platform in XNA40_PLATFORMS:
        platform_kind = "xna40"
        platform_name = XNA40_PLATFORMS[platform]
    elif platform in EXTENDED_PLATFORMS:
        platform_kind = "extended"
        platform_name = EXTENDED_PLATFORMS[platform]
    else:
        raise XnbError(f"{path}: unrecognized platform identifier '{platform}'")

    version = data[4]
    if version not in (4, 5):
        raise XnbError(f"{path}: container version {version} is neither 4 nor 5")
    flags = data[5]
    if flags & 0x3E:
        raise XnbError(f"{path}: header flags 0x{flags:02X} set an undefined bit")
    if (flags & 0x80) and (flags & 0x40):
        raise XnbError(f"{path}: header flags claim both LZX and LZ4 compression")
    total = struct.unpack_from("<i", data, 6)[0]
    if total != len(data):
        raise XnbError(f"{path}: header declares {total} bytes but the file holds {len(data)}")

    report = {
        "path": path,
        "platform": platform,
        "platformName": platform_name,
        "platformKind": platform_kind,
        "version": version,
        "graphicsProfile": "HiDef" if flags & 0x01 else "Reach",
        "compression": "lzx" if flags & 0x80 else ("lz4" if flags & 0x40 else "none"),
        "totalLength": total,
    }
    if report["compression"] != "none":
        report["status"] = "container-only"
        report["note"] = ("compressed payloads are not decompressed by this parser; the "
                          "container header was validated")
        return report

    cursor = Cursor(data[HEADER_BYTES:], path)
    reader_count = cursor.seven_bit_int()
    if reader_count < 0 or reader_count > MAX_TYPE_READERS:
        raise XnbError(f"{path}: type-reader count {reader_count} is out of range")
    table = []
    for _ in range(reader_count):
        raw = cursor.string()
        table.append({"name": raw, "canonical": strip_assembly(raw),
                      "version": cursor.i32()})
    for entry in table:
        if entry["version"] != 0:
            raise XnbError(
                f"{path}: reader '{entry['canonical']}' declares version {entry['version']}; "
                "every built-in XNA 4.0 reader is version 0")
    report["typeReaders"] = [entry["canonical"] for entry in table]
    report["typeReaderNames"] = [entry["name"] for entry in table]

    shared_count = cursor.seven_bit_int()
    if shared_count < 0 or shared_count > MAX_SHARED_RESOURCES:
        raise XnbError(f"{path}: shared-resource count {shared_count} is out of range")
    report["sharedResourceCount"] = shared_count

    reader = Reader(cursor, table, version, shared_count)
    root_entry = reader.reader_reference()
    report["rootReader"] = root_entry["canonical"]
    report["root"] = reader.root(root_entry)

    shared = []
    for _ in range(shared_count):
        entry = reader.reader_reference()
        shared.append(reader.shared_resource(entry))
    report["sharedResources"] = shared

    if cursor.pos != len(cursor.data):
        raise XnbError(
            f"{path}: {len(cursor.data) - cursor.pos} byte(s) remain after the object graph")
    report["status"] = "ok"
    return report


def compare(report: dict, expected: dict, path: str) -> list:
    """Compares a parsed report against an expected-value manifest."""
    problems = []

    def walk(actual, wanted, where):
        if isinstance(wanted, dict):
            if not isinstance(actual, dict):
                problems.append(f"{where}: expected an object, parsed {type(actual).__name__}")
                return
            for key, value in wanted.items():
                if key not in actual:
                    problems.append(f"{where}.{key}: absent from the parsed file")
                    continue
                walk(actual[key], value, f"{where}.{key}")
        elif isinstance(wanted, list):
            if not isinstance(actual, list) or len(actual) != len(wanted):
                problems.append(
                    f"{where}: expected {len(wanted)} element(s), parsed "
                    f"{len(actual) if isinstance(actual, list) else type(actual).__name__}")
                return
            for index, value in enumerate(wanted):
                walk(actual[index], value, f"{where}[{index}]")
        elif isinstance(wanted, float) or isinstance(actual, float):
            if abs(float(actual) - float(wanted)) > 1e-6:
                problems.append(f"{where}: expected {wanted}, parsed {actual}")
        elif actual != wanted:
            problems.append(f"{where}: expected {wanted!r}, parsed {actual!r}")

    walk(report, expected, os.path.basename(path))
    return problems


def collect(paths):
    files = []
    for path in paths:
        if os.path.isdir(path):
            for root, _, names in os.walk(path):
                for name in sorted(names):
                    if name.lower().endswith(".xnb"):
                        files.append(os.path.join(root, name))
        else:
            files.append(path)
    return files


def main(argv) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("inputs", nargs="+", help="`.xnb` files or directories")
    parser.add_argument("--json", action="store_true", help="emit a machine-readable report")
    parser.add_argument("--expect", help="expected-value manifest to compare against")
    arguments = parser.parse_args(argv)

    files = collect(arguments.inputs)
    if arguments.expect and len(files) != 1:
        print("--expect applies to exactly one file", file=sys.stderr)
        return 2

    expected = None
    if arguments.expect:
        with open(arguments.expect, "r", encoding="utf-8") as handle:
            expected = json.load(handle)

    reports = []
    failures = 0
    for path in files:
        try:
            report = parse(path)
            if expected is not None:
                problems = compare(report, expected, path)
                if problems:
                    report["status"] = "expectation-mismatch"
                    report["problems"] = problems
                    failures += 1
        except XnbError as error:
            report = {"path": path, "status": "error", "error": str(error)}
            failures += 1
        reports.append(report)

    if arguments.json:
        json.dump(reports, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    else:
        for report in reports:
            if report["status"] == "error":
                print(f"FAIL {report['path']}\n     {report['error']}")
            elif report["status"] == "expectation-mismatch":
                print(f"FAIL {report['path']}")
                for problem in report["problems"]:
                    print(f"     {problem}")
            else:
                print(f"{report['status'].upper():14} {report['path']}  "
                      f"platform={report['platform']} ({report['platformKind']}) "
                      f"v{report['version']} {report['compression']} "
                      f"root={report.get('rootReader', '-')}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
