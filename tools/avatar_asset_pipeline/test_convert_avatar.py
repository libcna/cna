#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Tests for convert_avatar.py: python3 tools/avatar_asset_pipeline/test_convert_avatar.py

A tiny skinned glTF is built in memory and converted into a temporary directory.
"""

import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

import pygltflib
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import convert_avatar  # noqa: E402

VERTEX_STRIDE = 52


def _png(color):
    data = io.BytesIO()
    Image.new("RGBA", (2, 2), color).save(data, format="PNG")
    return data.getvalue()


def _skinned_gltf(meshes, materials=()):
    """meshes: list of (name, [vertex count per primitive]) or (name, [(count, material)]).
    materials: (baseColorFactor or None, PNG bytes or None) per glTF material. One joint skins
    every vertex."""
    blob = bytearray()
    gltf = pygltflib.GLTF2(asset=pygltflib.Asset(version="2.0"))

    def accessor(data, component_type, count, kind):
        while len(blob) % 4:
            blob.append(0)
        gltf.bufferViews.append(pygltflib.BufferView(buffer=0, byteOffset=len(blob), byteLength=len(data)))
        blob.extend(data)
        gltf.accessors.append(pygltflib.Accessor(bufferView=len(gltf.bufferViews) - 1,
                                                 componentType=component_type, count=count, type=kind))
        return len(gltf.accessors) - 1

    for factor, png in materials:
        pbr = pygltflib.PbrMetallicRoughness(baseColorFactor=factor)
        if png is not None:
            while len(blob) % 4:
                blob.append(0)
            gltf.bufferViews.append(pygltflib.BufferView(buffer=0, byteOffset=len(blob), byteLength=len(png)))
            blob.extend(png)
            gltf.images.append(pygltflib.Image(bufferView=len(gltf.bufferViews) - 1, mimeType="image/png"))
            gltf.textures.append(pygltflib.Texture(source=len(gltf.images) - 1))
            pbr.baseColorTexture = pygltflib.TextureInfo(index=len(gltf.textures) - 1)
        gltf.materials.append(pygltflib.Material(pbrMetallicRoughness=pbr))

    identity = struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    inverse_bind = accessor(identity, pygltflib.FLOAT, 1, pygltflib.MAT4)
    gltf.nodes.append(pygltflib.Node(name="root"))
    for name, counts in meshes:
        primitives = []
        for entry in counts:
            count, material = entry if isinstance(entry, tuple) else (entry, None)
            positions = b"".join(struct.pack("<3f", i, 0, 0) for i in range(count))
            indices = b"".join(struct.pack("<H", i % count) for i in range(3))
            primitives.append(pygltflib.Primitive(
                attributes=pygltflib.Attributes(
                    POSITION=accessor(positions, pygltflib.FLOAT, count, pygltflib.VEC3),
                    JOINTS_0=accessor(bytes(4 * count), pygltflib.UNSIGNED_BYTE, count, pygltflib.VEC4),
                    WEIGHTS_0=accessor(struct.pack("<4f", 1, 0, 0, 0) * count, pygltflib.FLOAT, count,
                                       pygltflib.VEC4)),
                indices=accessor(indices, pygltflib.UNSIGNED_SHORT, 3, pygltflib.SCALAR),
                material=material))
        gltf.meshes.append(pygltflib.Mesh(name=name, primitives=primitives))
        gltf.nodes.append(pygltflib.Node(mesh=len(gltf.meshes) - 1, skin=0))
    gltf.skins.append(pygltflib.Skin(joints=[0], inverseBindMatrices=inverse_bind))
    gltf.scenes.append(pygltflib.Scene(nodes=list(range(len(gltf.nodes)))))
    gltf.buffers.append(pygltflib.Buffer(byteLength=len(blob)))
    gltf.set_binary_blob(bytes(blob))
    return gltf


class ConvertBodyTest(unittest.TestCase):
    def convert(self, meshes, materials=()):
        work = tempfile.TemporaryDirectory()
        self.addCleanup(work.cleanup)
        body = Path(work.name) / "body.glb"
        _skinned_gltf(meshes, materials).save_binary(str(body))
        out = Path(work.name) / "out"
        convert_avatar.convert_body(body, out)
        manifest = json.loads((out / "avatar.skinnedmodel.json").read_text())
        return out, manifest["parts"]

    def test_every_primitive_of_a_named_mesh_keeps_its_own_files(self):
        out, parts = self.convert([("BLUPI", [5, 4])])
        self.assertEqual(len(parts), 2)
        self.assertEqual(len({part["vertices"] for part in parts}), 2)
        self.assertEqual([(out / part["vertices"]).stat().st_size for part in parts],
                         [5 * VERTEX_STRIDE, 4 * VERTEX_STRIDE])

    def test_names_stay_unique_across_meshes_and_single_primitive_names_are_kept(self):
        out, parts = self.convert([("Body", [3]), ("Body", [4]), (None, [3, 3])])
        names = [part["name"] for part in parts]
        self.assertEqual(names[0], "Body")
        self.assertEqual(len(set(names)), len(names))
        self.assertEqual(len({part["vertices"] for part in parts}), len(parts))

    # galaxy-eggbert UB-002: a part keeps its material's base colour and texture.
    def test_parts_keep_their_base_colour_and_texture(self):
        out, parts = self.convert(
            [("BLUPI", [(3, 0), (3, 1)]), ("Plain", [3])],
            [([1.0, 0.8, 0.0, 1.0], None), ([0.5, 0.5, 0.5, 0.25], _png((10, 20, 30, 255)))])
        self.assertEqual(parts[0]["diffuseColor"], [1.0, 0.8, 0.0, 1.0])
        self.assertEqual(parts[1]["diffuseColor"], [0.5, 0.5, 0.5, 0.25])
        self.assertEqual(parts[2]["diffuseColor"], [1.0, 1.0, 1.0, 1.0])
        with Image.open(out / parts[0]["texture"]) as white:
            self.assertEqual(white.convert("RGBA").getpixel((0, 0)), (255, 255, 255, 255))
        with Image.open(out / parts[1]["texture"]) as face:
            self.assertEqual(face.size, (2, 2))
            self.assertEqual(face.convert("RGBA").getpixel((1, 1)), (10, 20, 30, 255))


if __name__ == "__main__":
    unittest.main()
