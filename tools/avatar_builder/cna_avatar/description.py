# SPDX-License-Identifier: MS-PL
"""CNA avatar descriptions in Python, mirroring AvatarDescriptionCodec.cpp (docs/avatars.md).

Used by the review tools to name avatars; the C++ codec is the authority. Format 1 and format 2
share bytes 0-42; format 2 adds a facial-hair item id (43-44) and sixteen face-shape bytes (45-60).
"""
import struct
import zlib

SIZE = 1021
SLOTS = ("hair", "top", "bottom", "shoes", "glasses", "hat")
OPTIONAL_SLOTS = ("glasses", "hat")
COLORS = ("skin", "hair", "eyes", "top", "bottom", "shoes", "accessory")
FACE_PARAMETERS = 16
NEUTRAL = 128


class Descriptor:
    def __init__(self, body_type=0, height_mm=1700, build=128, catalog_version=1, colors=None, items=None,
                 facial_hair=0, face=None):
        self.body_type = body_type
        self.height_mm = height_mm
        self.build = build
        self.catalog_version = catalog_version
        self.colors = dict(colors or {name: (200, 200, 200) for name in COLORS})
        self.items = dict(items or {name: 0 for name in SLOTS})
        self.facial_hair = facial_hair
        self.face = list(face or [NEUTRAL] * FACE_PARAMETERS)

    @property
    def format(self):
        return 2 if self.facial_hair or any(v != NEUTRAL for v in self.face) else 1

    def encode(self):
        data = bytearray(SIZE)
        data[0] = self.format
        data[1:4] = b"CNA"
        data[4] = self.body_type
        struct.pack_into("<HBH", data, 5, self.height_mm, self.build, self.catalog_version)
        for index, name in enumerate(COLORS):
            data[10 + index * 3:13 + index * 3] = bytes(self.colors[name])
        for index, name in enumerate(SLOTS):
            struct.pack_into("<H", data, 31 + index * 2, self.items[name])
        if self.format == 2:
            struct.pack_into("<H", data, 43, self.facial_hair)
            data[45:45 + FACE_PARAMETERS] = bytes(self.face)
        struct.pack_into("<I", data, SIZE - 4, zlib.crc32(bytes(data[:SIZE - 4])) & 0xffffffff)
        return bytes(data)

    def hex(self):
        return self.encode().hex()


def decode(data):
    """Descriptor for a CNA format 1 or 2 description, or None (same refusals as the C++ codec,
    except the catalog item checks, which need a manifest)."""
    if len(data) != SIZE or data[0] not in (1, 2) or data[1:4] != b"CNA":
        return None
    if struct.unpack_from("<I", data, SIZE - 4)[0] != zlib.crc32(bytes(data[:SIZE - 4])) & 0xffffffff:
        return None
    reserved = 43 if data[0] == 1 else 45 + FACE_PARAMETERS
    if any(data[reserved:SIZE - 4]):
        return None
    height, build, catalog = struct.unpack_from("<HBH", data, 5)
    colors = {name: tuple(data[10 + i * 3:13 + i * 3]) for i, name in enumerate(COLORS)}
    items = {name: struct.unpack_from("<H", data, 31 + i * 2)[0] for i, name in enumerate(SLOTS)}
    if data[4] > 1 or not 1450 <= height <= 2050 or catalog == 0:
        return None
    if data[0] == 2:
        return Descriptor(data[4], height, build, catalog, colors, items, struct.unpack_from("<H", data, 43)[0],
                          list(data[45:45 + FACE_PARAMETERS]))
    return Descriptor(data[4], height, build, catalog, colors, items)
