# SPDX-License-Identifier: MS-PL
"""Tiny GLB reader for validation and previews of the generated catalog."""
import json
import struct

SIZES = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
FORMATS = {5126: "f", 5121: "B", 5123: "H", 5125: "I"}


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
    count = a["count"] * size
    values = struct.unpack_from("<%d%s" % (count, fmt), blob, view["byteOffset"] + a.get("byteOffset", 0))
    if a.get("normalized") and fmt == "B":
        values = [v / 255.0 for v in values]
    return [tuple(values[i * size:(i + 1) * size]) for i in range(a["count"])] if size > 1 else list(values)


def image_bytes(gltf, blob, image):
    view = gltf["bufferViews"][gltf["images"][image]["bufferView"]]
    return blob[view["byteOffset"]:view["byteOffset"] + view["byteLength"]]
