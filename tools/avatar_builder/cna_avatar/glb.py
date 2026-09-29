# SPDX-License-Identifier: MS-PL
"""Minimal deterministic glTF 2.0 binary (GLB) writer for skinned avatar assets."""
import json
import struct

from . import rig
from .mathutil import quantize

FLOAT, UBYTE, SHORT, USHORT, UINT = 5126, 5121, 5122, 5123, 5125


class GlbBuilder:
    def __init__(self, extras):
        self.gltf = {"asset": {"version": "2.0", "generator": "CNA tools/avatar_builder"},
                     "extras": extras, "scene": 0, "scenes": [{"nodes": []}], "nodes": [],
                     "buffers": [], "bufferViews": [], "accessors": []}
        self.blob = bytearray()
        self.textured_materials = set()

    def _view(self, data, target=None, stride=None):
        while len(self.blob) % 4:
            self.blob.append(0)
        view = {"buffer": 0, "byteOffset": len(self.blob), "byteLength": len(data)}
        if target:
            view["target"] = target
        if stride:
            view["byteStride"] = stride
        self.blob += data
        self.gltf["bufferViews"].append(view)
        return len(self.gltf["bufferViews"]) - 1

    def accessor(self, values, kind, component, count_per=1, normalized=False, target=None, bounds=False, pad=0):
        """pad: bytes of padding after each element (keeps vertex elements 4-byte aligned)."""
        fmt = {FLOAT: "<f", UBYTE: "<B", SHORT: "<h", USHORT: "<H", UINT: "<I"}[component]
        rows = [v if isinstance(v, (tuple, list)) else (v,) for v in values]
        data = b"".join(b"".join(struct.pack(fmt, c) for c in r) + b"\0" * pad for r in rows)
        stride = len(data) // len(rows) if pad else None
        accessor = {"bufferView": self._view(data, target, stride), "componentType": component, "count": len(values),
                    "type": kind}
        if normalized:
            accessor["normalized"] = True
        if bounds:
            rows = [v if isinstance(v, (tuple, list)) else (v,) for v in values]
            # Bounds are the float32 values actually stored.
            rows = [tuple(struct.unpack(fmt, struct.pack(fmt, c))[0] for c in r) for r in rows]
            accessor["min"] = [min(r[i] for r in rows) for i in range(count_per)]
            accessor["max"] = [max(r[i] for r in rows) for i in range(count_per)]
        self.gltf["accessors"].append(accessor)
        return len(self.gltf["accessors"]) - 1

    def add_rig(self, body):
        """Adds the 71 joint nodes (bind translations, identity rotations) and returns the skin index."""
        translations = rig.local_translations(body)
        base = len(self.gltf["nodes"])
        for i in range(rig.BONE_COUNT):
            node = {"name": rig.NAMES[i], "translation": [quantize(c) for c in translations[i]]}
            children = [base + c for c in range(rig.BONE_COUNT) if rig.PARENTS[c] == i]
            if children:
                node["children"] = children
            self.gltf["nodes"].append(node)
        self.gltf["scenes"][0]["nodes"].append(base)
        positions = rig.joint_positions(body)
        inverse = []
        for p in positions:
            # Column-major mat4 of translation(-p).
            inverse.append((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -quantize(p[0]), -quantize(p[1]), -quantize(p[2]), 1))
        ibm = self.accessor(inverse, "MAT4", FLOAT)
        self.gltf.setdefault("skins", []).append({"name": "CNAAvatar71", "joints": list(range(base, base + rig.BONE_COUNT)),
                                                 "skeleton": base, "inverseBindMatrices": ibm})
        self.rig_base = base
        return len(self.gltf["skins"]) - 1

    def add_image(self, name, png):
        self.gltf.setdefault("images", []).append({"name": name, "mimeType": "image/png", "bufferView": self._view(png)})
        self.gltf.setdefault("samplers", [{"magFilter": 9729, "minFilter": 9729, "wrapS": 33071, "wrapT": 33071}])
        self.gltf.setdefault("textures", []).append({"sampler": 0, "source": len(self.gltf["images"]) - 1})
        return len(self.gltf["textures"]) - 1

    def add_material(self, name, tint, color=(1.0, 1.0, 1.0), texture=None, extras=None, specular=None):
        """specular scales the renderer's highlight for this material (cnaSpecular; 1 when absent)."""
        pbr = {"baseColorFactor": [color[0], color[1], color[2], 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.9}
        if texture is not None:
            pbr["baseColorTexture"] = {"index": texture}
        material = {"name": name, "pbrMetallicRoughness": pbr, "extras": dict({"cnaTint": tint}, **(extras or {}))}
        if specular is not None and specular != 1.0:
            material["extras"]["cnaSpecular"] = specular
        self.gltf.setdefault("materials", []).append(material)
        if extras and "cnaFeature" in extras:
            # Face decals are textured at runtime with the expression's atlas tile.
            self.textured_materials.add(len(self.gltf["materials"]) - 1)
        return len(self.gltf["materials"]) - 1

    def add_skinned_mesh(self, name, parts, skin):
        """parts: list of (Mesh, material index)."""
        primitives = []
        for mesh, material in parts:
            mesh.validate()
            textured = "baseColorTexture" in self.gltf["materials"][material]["pbrMetallicRoughness"]
            attributes = {
                "POSITION": self.accessor([tuple(quantize(c) for c in p) for p in mesh.positions], "VEC3", FLOAT, 3,
                                          target=34962, bounds=True),
                # Normalized 16-bit normals, padded to 8 bytes per element.
                "NORMAL": self.accessor([tuple(max(-32767, min(32767, int(round(c * 32767.0)))) for c in n)
                                         for n in mesh.normals], "VEC3", SHORT, normalized=True, target=34962, pad=2),
                "JOINTS_0": self.accessor([j for j, _ in mesh.skin], "VEC4", UBYTE, target=34962),
                "WEIGHTS_0": self.accessor([w for _, w in mesh.skin], "VEC4", UBYTE, normalized=True, target=34962),
            }
            # Only textured parts carry texture coordinates; the rest are flat tinted materials.
            if textured or material in self.textured_materials:
                attributes["TEXCOORD_0"] = self.accessor([tuple(max(0, min(65535, int(round(c * 65535.0)))) for c in uv)
                                                          for uv in mesh.uvs], "VEC2", USHORT, normalized=True, target=34962)
            index_type = USHORT if len(mesh.positions) < 65536 else UINT
            indices = self.accessor(mesh.indices, "SCALAR", index_type, target=34963)
            primitives.append({"attributes": attributes, "indices": indices, "material": material, "mode": 4})
        self.gltf.setdefault("meshes", []).append({"name": name, "primitives": primitives})
        self.gltf["nodes"].append({"name": name, "mesh": len(self.gltf["meshes"]) - 1, "skin": skin})
        self.gltf["scenes"][0]["nodes"].append(len(self.gltf["nodes"]) - 1)

    def add_animation(self, name, times, channels, extras):
        """channels: list of (joint slot, path, (in, value, out) triples, key times or None for `times`)."""
        animation = {"name": name, "channels": [], "samplers": [], "extras": extras}
        time_accessor = self.accessor([quantize(t, 1e-4) for t in times], "SCALAR", FLOAT, 1, bounds=True)
        still_accessor = None
        for slot, path, triples, own_times in channels:
            if len(triples) == 1:
                # A constant channel is one key.
                if still_accessor is None:
                    still_accessor = self.accessor([0.0], "SCALAR", FLOAT, 1, bounds=True)
                kind = "VEC4" if path == "rotation" else "VEC3"
                size = 4 if path == "rotation" else 3
                sampler = {"input": still_accessor,
                           "output": self.accessor([tuple(quantize(c, 1e-6) for c in v) for v in triples[0]], kind,
                                                   FLOAT, size),
                           "interpolation": "CUBICSPLINE"}
                animation["samplers"].append(sampler)
                animation["channels"].append({"sampler": len(animation["samplers"]) - 1,
                                              "target": {"node": self.rig_base + slot, "path": path}})
                continue
            flat = []
            for triple in triples:
                flat.extend(triple)
            kind = "VEC4" if path == "rotation" else "VEC3"
            size = 4 if path == "rotation" else 3
            # A channel may carry its own, denser key times (legs solved again between keys).
            input_accessor = time_accessor if own_times is None else \
                self.accessor([quantize(t, 1e-4) for t in own_times], "SCALAR", FLOAT, 1, bounds=True)
            sampler = {"input": input_accessor,
                       "output": self.accessor([tuple(quantize(c, 1e-6) for c in v) for v in flat], kind, FLOAT, size),
                       "interpolation": "CUBICSPLINE"}
            animation["samplers"].append(sampler)
            animation["channels"].append({"sampler": len(animation["samplers"]) - 1,
                                          "target": {"node": self.rig_base + slot, "path": path}})
        self.gltf.setdefault("animations", []).append(animation)

    def build(self):
        while len(self.blob) % 4:
            self.blob.append(0)
        self.gltf["buffers"] = [{"byteLength": len(self.blob)}]
        text = json.dumps(self.gltf, separators=(",", ":"), ensure_ascii=True).encode("ascii")
        while len(text) % 4:
            text += b" "
        total = 12 + 8 + len(text) + 8 + len(self.blob)
        return (struct.pack("<III", 0x46546C67, 2, total) + struct.pack("<II", len(text), 0x4E4F534A) + text +
                struct.pack("<II", len(self.blob), 0x004E4942) + bytes(self.blob))
