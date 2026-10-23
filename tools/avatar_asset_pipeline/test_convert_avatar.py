#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Tests for convert_avatar.py: python3 tools/avatar_asset_pipeline/test_convert_avatar.py

A tiny skinned glTF is built in memory and converted into a temporary directory.
"""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

import pygltflib

sys.path.insert(0, str(Path(__file__).resolve().parent))
import convert_avatar  # noqa: E402

VERTEX_STRIDE = 52


def _skinned_gltf(meshes):
    """meshes: list of (name, [vertex count per primitive]). One joint skins every vertex."""
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

    identity = struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    inverse_bind = accessor(identity, pygltflib.FLOAT, 1, pygltflib.MAT4)
    gltf.nodes.append(pygltflib.Node(name="root"))
    for name, counts in meshes:
        primitives = []
        for count in counts:
            positions = b"".join(struct.pack("<3f", i, 0, 0) for i in range(count))
            indices = b"".join(struct.pack("<H", i % count) for i in range(3))
            primitives.append(pygltflib.Primitive(
                attributes=pygltflib.Attributes(
                    POSITION=accessor(positions, pygltflib.FLOAT, count, pygltflib.VEC3),
                    JOINTS_0=accessor(bytes(4 * count), pygltflib.UNSIGNED_BYTE, count, pygltflib.VEC4),
                    WEIGHTS_0=accessor(struct.pack("<4f", 1, 0, 0, 0) * count, pygltflib.FLOAT, count,
                                       pygltflib.VEC4)),
                indices=accessor(indices, pygltflib.UNSIGNED_SHORT, 3, pygltflib.SCALAR)))
        gltf.meshes.append(pygltflib.Mesh(name=name, primitives=primitives))
        gltf.nodes.append(pygltflib.Node(mesh=len(gltf.meshes) - 1, skin=0))
    gltf.skins.append(pygltflib.Skin(joints=[0], inverseBindMatrices=inverse_bind))
    gltf.scenes.append(pygltflib.Scene(nodes=list(range(len(gltf.nodes)))))
    gltf.buffers.append(pygltflib.Buffer(byteLength=len(blob)))
    gltf.set_binary_blob(bytes(blob))
    return gltf


class ConvertBodyTest(unittest.TestCase):
    def convert(self, meshes):
        work = tempfile.TemporaryDirectory()
        self.addCleanup(work.cleanup)
        body = Path(work.name) / "body.glb"
        _skinned_gltf(meshes).save_binary(str(body))
        out = Path(work.name) / "out"
        convert_avatar.convert_body(body, out)
        import json
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


if __name__ == "__main__":
    unittest.main()
