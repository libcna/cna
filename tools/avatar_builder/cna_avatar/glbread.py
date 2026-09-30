# SPDX-License-Identifier: MS-PL
"""Tiny GLB reader for validation and previews of the generated catalog."""
import json
import struct

SIZES = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
FORMATS = {5126: "f", 5121: "B", 5122: "h", 5123: "H", 5125: "I"}
BYTES = {"f": 4, "B": 1, "h": 2, "H": 2, "I": 4}


def read(data):
    magic, version, total = struct.unpack_from("<III", data, 0)
    assert magic == 0x46546C67 and version == 2 and total == len(data)
    json_length, json_type = struct.unpack_from("<II", data, 12)
    assert json_type == 0x4E4F534A
    gltf = json.loads(data[20:20 + json_length])
    offset = 20 + json_length
    bin_length, bin_type = struct.unpack_from("<II", data, offset)
    assert bin_type == 0x004E4942
    blob = data[offset + 8:offset + 8 + bin_length]
    return gltf, blob


def accessor(gltf, blob, index):
    a = gltf["accessors"][index]
    view = gltf["bufferViews"][a["bufferView"]]
    size = SIZES[a["type"]]
    fmt = FORMATS[a["componentType"]]
    element = size * BYTES[fmt]
    stride = view.get("byteStride", element)
    start = view["byteOffset"] + a.get("byteOffset", 0)
    rows = [struct.unpack_from("<%d%s" % (size, fmt), blob, start + i * stride) for i in range(a["count"])]
    if a.get("normalized"):
        scale = {"B": 255.0, "h": 32767.0, "H": 65535.0}[fmt]
        rows = [tuple(max(-1.0, v / scale) for v in r) for r in rows]
    return rows if size > 1 else [r[0] for r in rows]


def image_bytes(gltf, blob, image):
    view = gltf["bufferViews"][gltf["images"][image]["bufferView"]]
    return blob[view["byteOffset"]:view["byteOffset"] + view["byteLength"]]
