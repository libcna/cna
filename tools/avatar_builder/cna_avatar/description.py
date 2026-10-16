# SPDX-License-Identifier: MS-PL
"""CNA avatar descriptions in Python, mirroring AvatarDescriptionCodec.cpp (docs/avatars.md).

Used by the review tools to name avatars; the C++ codec is the authority. Formats 1, 2 and 3 share
bytes 0-42; format 2 adds a facial-hair item id (43-44) and sixteen face-shape bytes (45-60); format
3 adds eyebrows (61), ears (63), four accessories with sides (65), four makeup styles (77), five
colours (85), proportions (100) and sixteen more face-shape bytes (101-116).
"""
import struct
import zlib

SIZE = 1021
SLOTS = ("hair", "top", "bottom", "shoes", "glasses", "hat")
OPTIONAL_SLOTS = ("glasses", "hat")
COLORS = ("skin", "hair", "eyes", "top", "bottom", "shoes", "accessory")
EXTENDED_COLORS = ("lips", "eyeShadow", "blush", "jewelry", "gloves")
ACCESSORIES = ("earrings", "wristwear", "gloves", "rings")
MAKEUP = ("eyeShadow", "eyeliner", "blush", "lips")
FACE_PARAMETERS = 32
FORMAT_TWO_FACE = 16
NEUTRAL = 128
# The baseline avatar catalog (BaseCatalogVersion): catalogs below it were development catalogs.
BASE_CATALOG_VERSION = 3
# Item id -> lowest description format naming it, for descriptors built without their own table
# (set from the catalog by the review tools: use_catalog).
ITEM_FORMATS = {}


def use_catalog(manifest):
    """Descriptors created from now on know which of this catalog's items need format 3."""
    ITEM_FORMATS.clear()
    ITEM_FORMATS.update({e["id"]: e.get("format", 1) for key in ("items", "featureItems", "faceStyles")
                         for e in manifest.get(key, [])})


class Descriptor:
    def __init__(self, body_type=0, height_mm=1700, build=128, catalog_version=BASE_CATALOG_VERSION, colors=None, items=None,
                 facial_hair=0, face=None, eyebrows=0, ears=0, accessories=None, sides=None, makeup=None, proportion=NEUTRAL,
                 item_formats=None):
        self.body_type = body_type
        self.height_mm = height_mm
        self.build = build
        self.catalog_version = catalog_version
        self.colors = dict(colors or {name: (200, 200, 200) for name in COLORS})
        for name in EXTENDED_COLORS:
            self.colors.setdefault(name, (0, 0, 0))
        self.items = dict(items or {name: 0 for name in SLOTS})
        self.facial_hair = facial_hair
        face = list(face or [NEUTRAL] * FACE_PARAMETERS)
        self.face = face + [NEUTRAL] * (FACE_PARAMETERS - len(face))
        self.eyebrows = eyebrows
        self.ears = ears
        self.accessories = dict(accessories or {})
        self.sides = dict(sides or {})
        self.makeup = dict(makeup or {})
        self.proportion = proportion
        # Item id -> lowest format naming it (the catalog's "format"); ids absent are format 1.
        self.item_formats = dict(item_formats if item_formats is not None else ITEM_FORMATS)

    def extended(self):
        return bool(self.eyebrows or self.ears or any(self.accessories.values()) or any(self.sides.values()) or
                    any(self.makeup.values()) or self.proportion != NEUTRAL or
                    any(v != NEUTRAL for v in self.face[FORMAT_TWO_FACE:]) or
                    any(self.colors[name] != (0, 0, 0) for name in EXTENDED_COLORS))

    @property
    def format(self):
        if self.extended():
            return 3
        needed = max([self.item_formats.get(i, 1) for i in list(self.items.values()) + [self.facial_hair] if i] + [1])
        face = 2 if self.facial_hair or any(v != NEUTRAL for v in self.face[:FORMAT_TWO_FACE]) else 1
        return max(face, needed)

    def encode(self):
        data = bytearray(SIZE)
        fmt = self.format
        data[0] = fmt
        data[1:4] = b"CNA"
        data[4] = self.body_type
        struct.pack_into("<HBH", data, 5, self.height_mm, self.build, self.catalog_version)
        for index, name in enumerate(COLORS):
            data[10 + index * 3:13 + index * 3] = bytes(self.colors[name])
        for index, name in enumerate(SLOTS):
            struct.pack_into("<H", data, 31 + index * 2, self.items[name])
        if fmt >= 2:
            struct.pack_into("<H", data, 43, self.facial_hair)
            data[45:45 + FORMAT_TWO_FACE] = bytes(self.face[:FORMAT_TWO_FACE])
        if fmt >= 3:
            struct.pack_into("<HH", data, 61, self.eyebrows, self.ears)
            for index, name in enumerate(ACCESSORIES):
                struct.pack_into("<HB", data, 65 + index * 3, self.accessories.get(name, 0), self.sides.get(name, 0))
            for index, name in enumerate(MAKEUP):
                struct.pack_into("<H", data, 77 + index * 2, self.makeup.get(name, 0))
            for index, name in enumerate(EXTENDED_COLORS):
                data[85 + index * 3:88 + index * 3] = bytes(self.colors[name])
            data[100] = self.proportion
            data[101:117] = bytes(self.face[FORMAT_TWO_FACE:])
        struct.pack_into("<I", data, SIZE - 4, zlib.crc32(bytes(data[:SIZE - 4])) & 0xffffffff)
        return bytes(data)

    def hex(self):
        return self.encode().hex()


def decode(data):
    """Descriptor for a CNA format 1, 2 or 3 description, or None (the C++ codec's refusals except
    the catalog checks and the canonical-format check, which need a manifest)."""
    if len(data) != SIZE or data[0] not in (1, 2, 3) or data[1:4] != b"CNA":
        return None
    if struct.unpack_from("<I", data, SIZE - 4)[0] != zlib.crc32(bytes(data[:SIZE - 4])) & 0xffffffff:
        return None
    reserved = {1: 43, 2: 45 + FORMAT_TWO_FACE, 3: 117}[data[0]]
    if any(data[reserved:SIZE - 4]):
        return None
    height, build, catalog = struct.unpack_from("<HBH", data, 5)
    colors = {name: tuple(data[10 + i * 3:13 + i * 3]) for i, name in enumerate(COLORS)}
    items = {name: struct.unpack_from("<H", data, 31 + i * 2)[0] for i, name in enumerate(SLOTS)}
    if data[4] > 1 or not 1450 <= height <= 2050 or catalog < BASE_CATALOG_VERSION:
        return None
    d = Descriptor(data[4], height, build, catalog, colors, items)
    if data[0] >= 2:
        d.facial_hair = struct.unpack_from("<H", data, 43)[0]
        d.face[:FORMAT_TWO_FACE] = list(data[45:45 + FORMAT_TWO_FACE])
    if data[0] == 3:
        d.eyebrows, d.ears = struct.unpack_from("<HH", data, 61)
        for index, name in enumerate(ACCESSORIES):
            d.accessories[name], d.sides[name] = struct.unpack_from("<HB", data, 65 + index * 3)
        for index, name in enumerate(MAKEUP):
            d.makeup[name] = struct.unpack_from("<H", data, 77 + index * 2)[0]
        for index, name in enumerate(EXTENDED_COLORS):
            d.colors[name] = tuple(data[85 + index * 3:88 + index * 3])
        d.proportion = data[100]
        d.face[FORMAT_TWO_FACE:] = list(data[101:117])
    return d
