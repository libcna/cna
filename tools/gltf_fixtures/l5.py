# SPDX-License-Identifier: MS-PL
"""L5 golden vertex/index buffers (`GLTF-007`, plan_gltf.md §7.1).

L5 is the layer at which the numbers stop being values and become **bytes**: exactly what CNA hands
to `VertexBuffer::SetData`/`IndexBuffer::SetData`, at the boundary immediately before GPU resource
creation. A golden here catches what no higher layer can -- a field written at the wrong offset, a
stride selected wrongly, a colour channel packed in the wrong order, an index narrowed to the wrong
width -- because every one of those still decodes back to correct-looking values at L3.

A golden buffer is derived from two things, and it is worth being precise about which is which:

* **the values** come from the fixture's own L3 expectation, which is spec-derived and independent
  of CNA; and
* **the placement** comes from CNA's own vertex stride ABI (plan_gltf.md §2.3), which is a CNA
  decision, not a specification one -- including its fill values for a slot whose attribute the
  file omits (normal ``(0,0,1)``, texcoord ``(0,0)``, colour alpha ``255``).

So an L5 golden asserts "the spec-derived values, laid out the way CNA says it lays them out". A
deliberate ABI change therefore requires regenerating these goldens, and that regeneration is the
point at which someone has to look at every renderer's `ApplyLayout` and agree.
"""

from __future__ import annotations

import hashlib
import struct
from typing import Any, Sequence

from .builder import MODE_NAMES, TRIANGLES

#: The vertex stride ABI (plan_gltf.md §2.3) as a byte layout: stride -> [(field, offset, size)].
#: Every renderer's own ApplyLayout switch is a re-statement of this table; the C++ side of this
#: oracle carries an independent copy and a test asserts the two agree.
STRIDE_LAYOUTS: dict[int, list[tuple[str, int, int]]] = {
    20: [("Position", 0, 12), ("TextureCoordinate", 12, 8)],
    24: [("Position", 0, 12), ("Color", 12, 4), ("TextureCoordinate", 16, 8)],
    32: [("Position", 0, 12), ("Normal", 12, 12), ("TextureCoordinate", 24, 8)],
    48: [("Position", 0, 12), ("Normal", 12, 12), ("Tangent", 24, 16),
         ("TextureCoordinate", 40, 8)],
    52: [("Position", 0, 12), ("Normal", 12, 12), ("TextureCoordinate", 24, 8),
         ("BlendWeight", 32, 16), ("BlendIndices", 48, 4)],
    56: [("Position", 0, 12), ("Normal", 12, 12), ("TextureCoordinate", 24, 8),
         ("BlendWeight", 32, 16), ("BlendIndices", 48, 4), ("Color", 52, 4)],
    68: [("Position", 0, 12), ("Normal", 12, 12), ("Tangent", 24, 16),
         ("TextureCoordinate", 40, 8), ("BlendWeight", 48, 16), ("BlendIndices", 64, 4)],
}

#: What ExtractMesh writes into a slot whose attribute the source file does not author. These are
#: CNA's own choices, not the specification's, and they are recorded here rather than assumed.
DEFAULT_NORMAL = (0.0, 0.0, 1.0)
DEFAULT_TEXCOORD = (0.0, 0.0)


def _f32(values: Sequence[float]) -> bytes:
    return b"".join(struct.pack("<f", float(v)) for v in values)


def _color_byte(value: float) -> int:
    """The [0,1] float -> byte round-trip ExtractMesh performs (clamp, scale, round-half-up)."""
    return int(min(max(value, 0.0), 1.0) * 255.0 + 0.5)


def select_stride(primitive: dict[str, Any]) -> int:
    """The stride ExtractMesh selects for a primitive, for the cases the corpus can express.

    Deliberately partial: the PBR and dual-texture branches depend on which texture *maps* a
    material carries, and no corpus fixture carries any, so encoding a guess for them here would be
    a golden nobody had checked. A fixture that grows a map must extend this and say so.
    """
    material = primitive.get("material") or {}
    if any(material.get(key) for key in ("hasBaseColorTexture", "hasNormalTexture",
                                          "hasMetallicRoughnessTexture", "hasOcclusionTexture",
                                          "hasEmissiveTexture")):
        raise NotImplementedError(
            f"{primitive.get('meshName')!r}: the L5 golden packer does not yet cover the PBR / "
            "dual-texture stride branches (48/20/68), whose selection and tangent generation both "
            "depend on which texture maps a material carries. Extend tools/gltf_fixtures/l5.py "
            "together with the fixture that needs them.")
    skinned = bool(primitive.get("joints")) and bool(primitive.get("weights"))
    colored = bool(primitive.get("colors"))
    if skinned:
        return 56 if colored else 52
    return 24 if colored else 32


def pack_vertex_buffer(primitive: dict[str, Any], stride: int) -> bytes:
    """Packs one primitive's L3 streams into CNA's vertex layout for ``stride``."""
    layout = STRIDE_LAYOUTS.get(stride)
    if layout is None:
        raise ValueError(f"stride {stride} is not in the vertex stride ABI table")

    count = int(primitive["vertexCount"])
    positions = primitive["positions"]
    normals = primitive.get("normals") or []
    texcoords = primitive.get("texcoords") or []
    colors = primitive.get("colors") or []
    weights = primitive.get("weights") or []
    joints = primitive.get("joints") or []
    if len(positions) != count:
        raise ValueError("the L3 position count disagrees with vertexCount")

    out = bytearray()
    for v in range(count):
        vertex = bytearray(stride)
        for name, offset, size in layout:
            if name == "Position":
                field = _f32(positions[v])
            elif name == "Normal":
                field = _f32(normals[v] if v < len(normals) else DEFAULT_NORMAL)
            elif name == "TextureCoordinate":
                field = _f32(texcoords[v] if v < len(texcoords) else DEFAULT_TEXCOORD)
            elif name == "Color":
                rgba = list(colors[v]) if v < len(colors) else [0.0, 0.0, 0.0, 1.0]
                # A VEC3 COLOR_0 has no alpha; the specification's default is fully opaque.
                while len(rgba) < 4:
                    rgba.append(1.0)
                field = bytes(_color_byte(c) for c in rgba[:4])
            elif name == "BlendWeight":
                field = _f32(weights[v] if v < len(weights) else (0.0, 0.0, 0.0, 0.0))
            elif name == "BlendIndices":
                field = bytes(int(j) & 0xFF for j in (joints[v] if v < len(joints) else (0, 0, 0, 0)))
            elif name == "Tangent":
                raise NotImplementedError(
                    "tangent generation is not reproduced by the L5 golden packer; see "
                    "select_stride's own note")
            else:  # pragma: no cover - a typo in STRIDE_LAYOUTS
                raise ValueError(f"unknown vertex field {name!r}")
            if len(field) != size:
                raise ValueError(f"{name}: packed {len(field)} bytes, the layout reserves {size}")
            vertex[offset:offset + size] = field
        out += vertex
    return bytes(out)


def pack_index_buffer(indices: Sequence[int], vertex_count: int) -> tuple[bytes, int]:
    """Packs an index list the way ExtractMesh does, returning the bytes and the element size.

    The width is chosen from the **vertex count**, not from the largest index: that is CNA's own
    rule, and it is exactly why every index must be proved in range before it is narrowed
    (`GLTF-063`).
    """
    element_size = 4 if vertex_count > 65535 else 2
    fmt = "<I" if element_size == 4 else "<H"
    for index in indices:
        if not 0 <= index < vertex_count:
            raise ValueError(f"index {index} is outside the primitive's {vertex_count} vertices")
    return b"".join(struct.pack(fmt, int(i)) for i in indices), element_size


def primitive_count(mode: int, index_count: int) -> int:
    """The draw-call primitive count for a topology (plan_gltf.md §12.3).

    ``mode`` is the topology the buffer is in *after* import, not the one the file declared. A
    strip or fan is converted to a triangle list by then (`GLTF-072`), so a triangle-list count is
    the right answer for all three triangle modes; the line and point topologies do not reach L5
    at all until they have a draw path (`GLTF-073`/`GLTF-077`/`GLTF-078`).
    """
    if mode != TRIANGLES:
        raise NotImplementedError(
            f"the L5 golden only covers buffers already converted to TRIANGLES; {MODE_NAMES[mode]} "
            "arrives with its draw path (GLTF-073/GLTF-077/GLTF-078)")
    if index_count % 3 != 0:
        raise ValueError(f"a triangle list needs a multiple of 3 indices, got {index_count}")
    return index_count // 3


def buffers(fixture_id: str, primitives: Sequence[dict[str, Any]],
            strides: Sequence[int] | None = None) -> tuple[dict[str, Any], dict[str, bytes]]:
    """Builds a fixture's ``l5`` expectation and the golden files it references.

    Part 0's files are ``<id>.vb.bin``/``<id>.ib.bin`` and part *N*'s are ``<id>.pN.vb.bin``, so a
    fixture growing a second primitive never renames the first one's golden.

    :returns: the ``l5`` block, and a ``relative filename -> bytes`` map to emit alongside it.
    """
    parts: list[dict[str, Any]] = []
    files: dict[str, bytes] = {}
    for i, primitive in enumerate(primitives):
        stride = strides[i] if strides is not None else select_stride(primitive)
        vertex_bytes = pack_vertex_buffer(primitive, stride)
        # The buffer holds what CNA emits, which for a strip or fan is the CONVERTED triangle list
        # (GLTF-072), not the authored index run. l3_primitive states that separately under
        # importPolicy precisely so this layer does not have to re-derive it -- and so a manifest
        # reader can see that the two differ.
        policy = primitive.get("importPolicy") or {}
        imported_indices = policy.get("indices", primitive["indices"])
        imported_mode = policy.get("topologyMode", primitive["mode"])
        index_bytes, element_size = pack_index_buffer(imported_indices,
                                                       int(primitive["vertexCount"]))
        suffix = "" if i == 0 else f".p{i}"
        vb_name = f"{fixture_id}{suffix}.vb.bin"
        ib_name = f"{fixture_id}{suffix}.ib.bin"
        files[vb_name] = vertex_bytes
        files[ib_name] = index_bytes
        parts.append({
            "mesh": primitive["mesh"],
            "primitive": primitive["primitive"],
            "stride": stride,
            "vertexCount": int(primitive["vertexCount"]),
            "fields": [{"name": n, "offset": o, "size": s} for n, o, s in STRIDE_LAYOUTS[stride]],
            "vertexBufferFile": vb_name,
            "vertexBufferBytes": len(vertex_bytes),
            "vertexBufferSha256": hashlib.sha256(vertex_bytes).hexdigest(),
            "indexElementSize": element_size,
            "indexCount": len(imported_indices),
            "indexBufferFile": ib_name,
            "indexBufferBytes": len(index_bytes),
            "indexBufferSha256": hashlib.sha256(index_bytes).hexdigest(),
            "sourceTopology": MODE_NAMES[primitive["mode"]],
            "topology": MODE_NAMES[imported_mode],
            "primitiveCount": primitive_count(imported_mode, len(imported_indices)),
        })
    return {"supported": True, "parts": parts}, files


def unsupported(reason: str, blocked_by: Sequence[str]) -> dict[str, Any]:
    """The ``l5`` block for a fixture CNA's import path does not currently produce buffers for."""
    return {"supported": False, "reason": reason, "blockedBy": list(blocked_by), "parts": []}
