# SPDX-License-Identifier: MS-PL
"""``GltfBuilder``: asset construction, GLB packing, and the L2 expectation records (`GLTF-003`).

One builder produces one corpus asset in both containers -- a text ``.gltf`` with a base64
``data:`` URI buffer and a binary ``.glb`` twin -- from a single authored description, so the two
cannot drift apart. Alongside the asset it accumulates ``accessor_records``: the **decoded** value
of every accessor, derived from the same authored values that were packed into the buffer. That is
the L2 expectation, and deriving it here rather than restating it in a fixture is what makes a
fixture and its expectation a single source of truth.

Determinism is a hard requirement (`GLTF-020`): the JSON key order is fixed by this module rather
than by insertion accident, floats are emitted by Python's own shortest-round-trip repr, and no
value is ever derived from the environment. Regenerating an unchanged tree produces a zero diff.

Nothing here consults CNA. Every value this module emits comes from the fixture's authored data and
the pinned glTF 2.0 specification (``docs/gltf-conformance.md`` §2).
"""

from __future__ import annotations

import base64
import json
import struct
from typing import Any, Iterable, Sequence

# --- component types (§3.6.2.2 accessor-data-types) --------------------------------------------

#: ``5120`` -- signed byte.
BYTE = 5120
#: ``5121`` -- unsigned byte.
UNSIGNED_BYTE = 5121
#: ``5122`` -- signed short.
SHORT = 5122
#: ``5123`` -- unsigned short.
UNSIGNED_SHORT = 5123
#: ``5125`` -- unsigned int.
UNSIGNED_INT = 5125
#: ``5126`` -- 32-bit float.
FLOAT = 5126

#: Component type -> (struct format character, size in bytes, is-integer).
_COMPONENTS: dict[int, tuple[str, int, bool]] = {
    BYTE: ("b", 1, True),
    UNSIGNED_BYTE: ("B", 1, True),
    SHORT: ("h", 2, True),
    UNSIGNED_SHORT: ("H", 2, True),
    UNSIGNED_INT: ("I", 4, True),
    FLOAT: ("f", 4, False),
}

#: Component type -> the specification's own name for it, recorded in the L2 expectation so a
#: manifest is readable without a lookup table.
COMPONENT_TYPE_NAMES: dict[int, str] = {
    BYTE: "BYTE",
    UNSIGNED_BYTE: "UNSIGNED_BYTE",
    SHORT: "SHORT",
    UNSIGNED_SHORT: "UNSIGNED_SHORT",
    UNSIGNED_INT: "UNSIGNED_INT",
    FLOAT: "FLOAT",
}

#: The maximum magnitude each integer component type normalises against (§3.6.2.2). Unsigned types
#: map onto ``[0,1]`` and signed onto ``[-1,1]``, with the signed floor clamped at ``-1``.
_NORMALIZE_DIVISOR: dict[int, float] = {
    BYTE: 127.0,
    UNSIGNED_BYTE: 255.0,
    SHORT: 32767.0,
    UNSIGNED_SHORT: 65535.0,
    UNSIGNED_INT: 4294967295.0,
}

#: Accessor type -> number of components per element (§3.6.2.2).
ACCESSOR_TYPE_COMPONENTS: dict[str, int] = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
    "MAT2": 4,
    "MAT3": 9,
    "MAT4": 16,
}

# --- primitive modes (§3.7.2.1 meshes-overview) ------------------------------------------------

#: ``0`` -- POINTS.
POINTS = 0
#: ``1`` -- LINES.
LINES = 1
#: ``2`` -- LINE_LOOP.
LINE_LOOP = 2
#: ``3`` -- LINE_STRIP.
LINE_STRIP = 3
#: ``4`` -- TRIANGLES, the only mode CNA imports today.
TRIANGLES = 4
#: ``5`` -- TRIANGLE_STRIP.
TRIANGLE_STRIP = 5
#: ``6`` -- TRIANGLE_FAN.
TRIANGLE_FAN = 6

#: Mode -> the specification's own name. Indexable by mode value, so ``MODE_NAMES[prim["mode"]]``
#: is total over the seven legal modes and raises on anything else.
MODE_NAMES: dict[int, str] = {
    POINTS: "POINTS",
    LINES: "LINES",
    LINE_LOOP: "LINE_LOOP",
    LINE_STRIP: "LINE_STRIP",
    TRIANGLES: "TRIANGLES",
    TRIANGLE_STRIP: "TRIANGLE_STRIP",
    TRIANGLE_FAN: "TRIANGLE_FAN",
}

#: The ``asset.generator`` string every emitted fixture carries.
GENERATOR_NAME = "CNA gltf_fixtures"

#: The media type of the text container's base64 buffer URI.
_BUFFER_URI_PREFIX = "data:application/octet-stream;base64,"

# GLB container constants (§4.4 glb-file-format-specification).
_GLB_MAGIC = 0x46546C67
_GLB_VERSION = 2
_GLB_CHUNK_JSON = 0x4E4F534A
_GLB_CHUNK_BIN = 0x004E4942

# --- key orders ---------------------------------------------------------------------------------
# Emission order is stated once, here, rather than relying on the order a fixture happens to build
# its dictionaries in. A generated asset is a committed artefact whose diff must mean something.

_TOP_LEVEL_ORDER = ["asset", "extensionsUsed", "extensionsRequired", "extensions", "scene",
                    "scenes", "nodes",
                    "cameras", "meshes", "materials", "textures", "images", "samplers", "skins",
                    "animations", "accessors", "bufferViews", "buffers"]
_NODE_ORDER = ["name", "mesh", "camera", "skin", "extensions", "children", "translation",
               "rotation", "scale", "matrix", "weights"]
_ACCESSOR_ORDER = ["bufferView", "byteOffset", "componentType", "normalized", "count", "type",
                   "max", "min", "sparse"]
_BUFFER_VIEW_ORDER = ["buffer", "byteOffset", "byteLength", "byteStride", "target"]
_PRIMITIVE_ORDER = ["attributes", "indices", "material", "mode", "targets"]
_MESH_ORDER = ["name", "primitives", "weights"]
_SCENE_ORDER = ["name", "nodes"]

#: The punctual-lighting extension, declared automatically by ``add_light``.
_KHR_LIGHTS = "KHR_lights_punctual"


def _ordered(value: dict[str, Any], order: Sequence[str]) -> dict[str, Any]:
    """Reorders ``value``'s known keys per ``order``, keeping any unlisted key in its own order."""
    out = {key: value[key] for key in order if key in value}
    out.update({key: v for key, v in value.items() if key not in out})
    return out


def flatten(values: Iterable[Sequence[float]]) -> list[float]:
    """Flattens a sequence of elements into the component stream an accessor stores.

    :param values: elements, each a sequence of components.
    :return: every component of every element, in order.
    """
    out: list[float] = []
    for element in values:
        out.extend(element)
    return out


def pack(values: Sequence[float], component_type: int) -> bytes:
    """Packs a flat component stream into little-endian accessor bytes.

    :param values: the components, already flattened.
    :param component_type: one of the ``BYTE`` … ``FLOAT`` constants.
    :return: the packed bytes, with no padding of any kind.
    """
    fmt, _size, is_integer = _component(component_type)
    if is_integer:
        return b"".join(struct.pack("<" + fmt, int(v)) for v in values)
    return b"".join(struct.pack("<" + fmt, float(v)) for v in values)


def expand_to_triangles(indices: Sequence[int], mode: int) -> list[list[int]]:
    """Expands an index list into triangles per the mode's own rule (§3.7.2.1).

    :param indices: the resolved index list (a non-indexed primitive's implicit ``[0, count)``).
    :param mode: the primitive mode.
    :return: one ``[a, b, c]`` per triangle; empty for the point and line modes, which describe no
        triangles at all rather than describing them badly.
    """
    if mode not in MODE_NAMES:
        raise ValueError(f"unknown primitive mode {mode!r}")
    idx = list(indices)
    if mode in (POINTS, LINES, LINE_LOOP, LINE_STRIP):
        return []
    if mode == TRIANGLES:
        return [idx[i:i + 3] for i in range(0, len(idx) - len(idx) % 3, 3)]
    if mode == TRIANGLE_STRIP:
        # Odd triangles swap their first two corners so the winding stays consistent.
        out = []
        for i in range(len(idx) - 2):
            out.append([idx[i], idx[i + 1], idx[i + 2]] if i % 2 == 0
                       else [idx[i + 1], idx[i], idx[i + 2]])
        return out
    out = []
    for i in range(1, len(idx) - 1):  # TRIANGLE_FAN
        out.append([idx[0], idx[i], idx[i + 1]])
    return out


def produces_triangles(mode: int) -> bool:
    """Whether a mode describes triangles, and so has a triangle-list equivalent (`GLTF-072`)."""
    return mode in (TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN)


def primitive_count_for_mode(mode: int, index_count: int) -> int:
    """How many primitives an index run of ``mode`` describes (plan_gltf.md §12.3, `GLTF-078`).

    Mirrors ``PrimitiveCountForTopology`` on the C++ side. Both are stated independently on purpose:
    a count that agreed with itself but not with the specification would pass either alone.

    :param mode: the topology the run is in.
    :param index_count: the number of indices in the run.
    :return: the draw-call primitive count, never negative.
    """
    if mode == POINTS:
        return index_count
    if mode == LINES:
        return index_count // 2
    if mode in (LINE_STRIP, LINE_LOOP):
        return max(index_count - 1, 0)
    if mode == TRIANGLES:
        return index_count // 3
    return max(index_count - 2, 0)  # strip / fan, before conversion


def _component(component_type: int) -> tuple[str, int, bool]:
    try:
        return _COMPONENTS[component_type]
    except KeyError:
        raise ValueError(f"unknown componentType {component_type!r}") from None


def _normalize(value: int, component_type: int) -> float:
    """Applies §3.6.2.2's normalized-integer conversion to one component."""
    divisor = _NORMALIZE_DIVISOR.get(component_type)
    if divisor is None:
        raise ValueError(f"componentType {component_type!r} may not be normalized")
    if component_type in (BYTE, SHORT):
        return max(float(value) / divisor, -1.0)
    return float(value) / divisor


class GltfBuilder:
    """Accumulates one glTF asset and the L2 expectation of every accessor in it.

    A fixture states its authored values exactly once. Whatever is packed into the buffer is what
    the accessor record reports as decoded, so an expectation cannot silently disagree with the
    bytes it is checked against.
    """

    def __init__(self, name: str) -> None:
        """Starts an empty asset.

        :param name: the fixture's canonical corpus id, used only in error messages.
        """
        self.name = name
        self._buffer = bytearray()
        self._scenes: list[dict[str, Any]] = []
        self._nodes: list[dict[str, Any]] = []
        self._meshes: list[dict[str, Any]] = []
        self._materials: list[dict[str, Any]] = []
        self._images: list[dict[str, Any]] = []
        self._samplers: list[dict[str, Any]] = []
        self._textures: list[dict[str, Any]] = []
        self._skins: list[dict[str, Any]] = []
        self._cameras: list[dict[str, Any]] = []
        self._lights: list[dict[str, Any]] = []
        self._animations: list[dict[str, Any]] = []
        self._accessors: list[dict[str, Any]] = []
        self._buffer_views: list[dict[str, Any]] = []
        self._default_scene: int | None = None
        self._extensions_used: list[str] = []
        self._extensions_required: list[str] = []
        #: The L2 expectation: one decoded record per accessor, in accessor order.
        self.accessor_records: list[dict[str, Any]] = []

    # --- buffer ---------------------------------------------------------------------------------

    def pad(self, count: int) -> int:
        """Appends ``count`` zero bytes, so a following bufferView has a non-zero ``byteOffset``.

        :param count: how many padding bytes to append.
        :return: the buffer length after padding.
        """
        self._buffer.extend(b"\x00" * count)
        return len(self._buffer)

    def append_bytes(self, data: bytes, alignment: int = 1) -> int:
        """Appends raw bytes to the single buffer, aligning the start first.

        :param data: the bytes to append.
        :param alignment: byte alignment the appended block must start on.
        :return: the byte offset the block starts at.
        """
        if alignment > 1 and len(self._buffer) % alignment:
            self._buffer.extend(b"\x00" * (alignment - len(self._buffer) % alignment))
        offset = len(self._buffer)
        self._buffer.extend(data)
        return offset

    def add_buffer_view(self, byte_offset: int, byte_length: int,
                        byte_stride: int | None = None) -> int:
        """Adds a bufferView over the single buffer.

        :param byte_offset: the view's offset into the buffer.
        :param byte_length: the view's length in bytes.
        :param byte_stride: the interleaving stride, or ``None`` for tightly packed data.
        :return: the new bufferView's index.
        """
        view: dict[str, Any] = {"buffer": 0, "byteOffset": byte_offset, "byteLength": byte_length}
        if byte_stride is not None:
            view["byteStride"] = byte_stride
        self._buffer_views.append(view)
        return len(self._buffer_views) - 1

    # --- accessors ------------------------------------------------------------------------------

    def add_accessor(self, *, usage: str, component_type: int, accessor_type: str, count: int,
                     expected: Sequence[float], buffer_view: int | None,
                     byte_offset: int = 0, normalized: bool = False,
                     min_: Sequence[float] | None = None, max_: Sequence[float] | None = None,
                     sparse: dict[str, Any] | None = None,
                     declared_count: int | None = None,
                     base_values_if_sparse_ignored: Sequence[float] | None = None) -> int:
        """Adds an accessor whose bytes some caller has already placed in the buffer.

        Use this when the accessor's storage is the thing under test -- interleaving, a non-zero
        ``byteOffset``, a sparse override, an absent base ``bufferView``. The decoded expectation is
        passed in as ``expected`` because in those cases it is not recoverable from the packed bytes
        alone; it is still spec-derived, never taken from CNA.

        :param usage: what the accessor is for, recorded in the L2 expectation (``POSITION``, …).
        :param component_type: one of the ``BYTE`` … ``FLOAT`` constants.
        :param accessor_type: ``SCALAR``, ``VEC3``, ``MAT4``, …
        :param count: the element count.
        :param expected: the decoded component stream a conforming reader must produce.
        :param buffer_view: the base bufferView index, or ``None`` for a sparse accessor with no
            base view (whose base array reads as all zeros).
        :param byte_offset: the accessor's own offset within the bufferView.
        :param normalized: whether integer components carry the normalized flag.
        :param min_: the authored ``min`` bound, if any.
        :param max_: the authored ``max`` bound, if any.
        :param sparse: the authored ``sparse`` block, if any.
        :param base_values_if_sparse_ignored: for a sparse accessor, the values a decoder that
            ignored the ``sparse`` block would produce. Recorded so the L2 dump can be asserted to
            show the override *applied* (``GLTF-066``) rather than merely to contain plausible
            numbers -- the control that separates "the override ran" from "the base happened to be
            right".
        :param declared_count: the count to WRITE into the file, when it must differ from the
            honest ``count`` the expectation is built from. Only a malformed fixture wants this --
            a count no buffer could back is the lie under test (``GLTF-039``) -- so the two are
            kept separate rather than letting a bad count silently define the expectation.
        :return: the new accessor's index.
        """
        components = ACCESSOR_TYPE_COMPONENTS[accessor_type]
        if len(expected) != count * components:
            raise ValueError(
                f"{self.name}: {usage} declares {count} x {accessor_type} "
                f"({count * components} components) but the expectation carries {len(expected)}")

        accessor: dict[str, Any] = {
            "componentType": component_type,
            "count": count if declared_count is None else declared_count,
            "type": accessor_type}
        if buffer_view is not None:
            accessor["bufferView"] = buffer_view
            if byte_offset:
                accessor["byteOffset"] = byte_offset
        if normalized:
            accessor["normalized"] = True
        if max_ is not None:
            accessor["max"] = list(max_)
        if min_ is not None:
            accessor["min"] = list(min_)
        if sparse is not None:
            accessor["sparse"] = sparse
        self._accessors.append(_ordered(accessor, _ACCESSOR_ORDER))

        view = self._buffer_views[buffer_view] if buffer_view is not None else None
        _fmt, _size, is_integer = _component(component_type)
        values: list[Any] = ([int(v) for v in expected] if is_integer and not normalized
                             else [float(v) for v in expected])
        self.accessor_records.append({
            "index": len(self._accessors) - 1,
            "usage": usage,
            "type": accessor_type,
            "componentType": component_type,
            "componentTypeName": COMPONENT_TYPE_NAMES[component_type],
            "count": count,
            "componentsPerElement": components,
            "normalized": bool(normalized),
            "sparse": sparse is not None,
            "bufferView": buffer_view,
            "byteOffset": byte_offset,
            "bufferViewByteOffset": view["byteOffset"] if view is not None else None,
            "bufferViewByteStride": view.get("byteStride") if view is not None else None,
            "values": values,
        })
        # Recorded only when the file LIES about its count, so a malformed fixture states the
        # number under test while every honest fixture's expectation stays exactly as it was.
        if declared_count is not None and declared_count != count:
            self.accessor_records[-1]["declaredCount"] = declared_count
        if base_values_if_sparse_ignored is not None:
            self.accessor_records[-1]["baseValuesIfSparseIgnored"] = (
                [int(v) for v in base_values_if_sparse_ignored] if is_integer and not normalized
                else [float(v) for v in base_values_if_sparse_ignored])
        return len(self._accessors) - 1

    def add_packed_accessor(self, *, usage: str, values: Sequence[Any], accessor_type: str,
                            component_type: int = FLOAT, with_bounds: bool = False,
                            normalized: bool = False) -> int:
        """Packs ``values`` into a fresh tightly packed bufferView and adds an accessor over it.

        This is the ordinary path: the authored values are packed and the decoded expectation is
        derived from those same values, so the two cannot disagree. A normalized integer accessor
        records the §3.6.2.2 conversion of what was packed, not the raw stored integers.

        :param usage: what the accessor is for, recorded in the L2 expectation.
        :param values: elements -- scalars for ``SCALAR``, sequences otherwise.
        :param accessor_type: ``SCALAR``, ``VEC3``, ``VEC4``, ``MAT4``, …
        :param component_type: one of the ``BYTE`` … ``FLOAT`` constants.
        :param with_bounds: emit the ``min``/``max`` bounds computed per component.
        :param normalized: mark integer components normalized.
        :return: the new accessor's index.
        """
        components = ACCESSOR_TYPE_COMPONENTS[accessor_type]
        if components == 1:
            elements = [[v] for v in values]
        else:
            elements = [list(v) for v in values]
        for element in elements:
            if len(element) != components:
                raise ValueError(
                    f"{self.name}: {usage} is {accessor_type} ({components} components) but an "
                    f"element carries {len(element)}")
        components_flat = flatten(elements)

        _fmt, size, _is_integer = _component(component_type)
        offset = self.append_bytes(pack(components_flat, component_type), alignment=4)
        view = self.add_buffer_view(offset, len(components_flat) * size)

        bounds: dict[str, Any] = {}
        if with_bounds:
            bounds["min_"] = [min(e[c] for e in elements) for c in range(components)]
            bounds["max_"] = [max(e[c] for e in elements) for c in range(components)]

        if normalized:
            expected: list[Any] = [_normalize(int(v), component_type) for v in components_flat]
        else:
            expected = list(components_flat)

        return self.add_accessor(usage=usage, component_type=component_type,
                                 accessor_type=accessor_type, count=len(elements),
                                 expected=expected, buffer_view=view, normalized=normalized,
                                 **bounds)

    # --- scene graph ----------------------------------------------------------------------------

    def add_mesh(self, primitives: Sequence[dict[str, Any]], *, name: str,
                 weights: Sequence[float] | None = None) -> int:
        """Adds a mesh.

        :param primitives: the mesh's primitives, each an authored glTF primitive object.
        :param name: the mesh's name.
        :param weights: the mesh's own default morph weights (§3.7.2.2), if any.
        :return: the new mesh's index.
        """
        mesh: dict[str, Any] = {
            "name": name,
            "primitives": [_ordered(dict(p), _PRIMITIVE_ORDER) for p in primitives],
        }
        if weights is not None:
            mesh["weights"] = list(weights)
        self._meshes.append(_ordered(mesh, _MESH_ORDER))
        return len(self._meshes) - 1

    def add_node(self, *, name: str, mesh: int | None = None, camera: int | None = None,
                 skin: int | None = None, light: int | None = None,
                 weights: Sequence[float] | None = None,
                 children: Sequence[int] | None = None,
                 translation: Sequence[float] | None = None,
                 rotation: Sequence[float] | None = None,
                 scale: Sequence[float] | None = None,
                 matrix: Sequence[float] | None = None) -> int:
        """Adds a node.

        ``matrix`` and the TRS properties are mutually exclusive (§3.5.3); authoring both is
        rejected here rather than left for a reader to resolve.

        :param name: the node's name.
        :param mesh: the mesh this node instances, if any.
        :param camera: the camera this node instances, if any.
        :param skin: the skin this node's mesh is deformed by, if any.
        :param weights: morph weights overriding the mesh's own (§3.7.2.2), if any.
        :param children: child node indices.
        :param translation: the node's translation.
        :param rotation: the node's rotation as an ``(x, y, z, w)`` quaternion.
        :param scale: the node's scale.
        :param matrix: the node's local transform as 16 column-major floats.
        :return: the new node's index.
        """
        if matrix is not None and any(v is not None for v in (translation, rotation, scale)):
            raise ValueError(
                f"{self.name}: node {name!r} authors both 'matrix' and TRS -- §3.5.3 allows one")
        node: dict[str, Any] = {"name": name}
        if mesh is not None:
            node["mesh"] = mesh
        if camera is not None:
            node["camera"] = camera
        if skin is not None:
            node["skin"] = skin
        if light is not None:
            # KHR_lights_punctual attaches a light through the node's own extensions block, not
            # through a top-level property like mesh/camera/skin -- the extension cannot add one.
            node["extensions"] = {_KHR_LIGHTS: {"light": light}}
        if weights is not None:
            node["weights"] = list(weights)
        if children:
            node["children"] = list(children)
        # Numeric literals are stored exactly as the fixture authored them. Coercing them to
        # float here would rewrite an authored integer as "10.0" in the emitted asset, which is a
        # different committed artefact for no gain -- both spellings are the same glTF number.
        if translation is not None:
            node["translation"] = list(translation)
        if rotation is not None:
            node["rotation"] = list(rotation)
        if scale is not None:
            node["scale"] = list(scale)
        if matrix is not None:
            node["matrix"] = list(matrix)
        self._nodes.append(_ordered(node, _NODE_ORDER))
        return len(self._nodes) - 1

    def add_scene(self, nodes: Sequence[int], *, name: str) -> int:
        """Adds a scene.

        :param nodes: the scene's root node indices.
        :param name: the scene's name.
        :return: the new scene's index.
        """
        self._scenes.append(_ordered({"name": name, "nodes": list(nodes)}, _SCENE_ORDER))
        return len(self._scenes) - 1

    def set_default_scene(self, index: int) -> None:
        """Sets the asset's default scene.

        :param index: the scene index ``scene`` is set to.
        """
        if not 0 <= index < len(self._scenes):
            raise ValueError(f"{self.name}: default scene {index} does not exist")
        self._default_scene = index

    def declare_extensions(self, *, used: Sequence[str] = (),
                           required: Sequence[str] = ()) -> None:
        """Declares the asset's ``extensionsUsed`` / ``extensionsRequired`` (§3.12).

        Anything in ``required`` is added to ``used`` if absent, because the specification requires
        every required extension to also be listed as used, and a fixture that violated that would
        be testing its own malformedness rather than the thing it is for.

        :param used: extensions the asset uses; absent ones may be ignored by a reader.
        :param required: extensions without which the asset cannot be interpreted correctly.
        """
        self._extensions_used = list(used)
        self._extensions_required = list(required)
        for name in self._extensions_required:
            if name not in self._extensions_used:
                self._extensions_used.append(name)

    def add_camera(self, camera: dict[str, Any]) -> int:
        """Adds a camera, authored as a glTF camera object.

        :param camera: the camera object (``type`` plus its ``perspective``/``orthographic`` block).
        :return: the new camera's index.
        """
        self._cameras.append(dict(camera))
        return len(self._cameras) - 1

    def add_light(self, light: dict[str, Any]) -> int:
        """Adds a ``KHR_lights_punctual`` light, and declares the extension as used.

        The extension is declared here rather than left to the caller because a light that no
        ``extensionsUsed`` entry announces is a malformed file, and a fixture that had to remember
        to declare it would eventually forget. ``used`` rather than ``required``: a reader that
        ignores the extension still gets correct — if unlit — geometry, which is exactly the
        distinction §3.12 draws.

        :param light: the light object (``type``, ``color``, ``intensity``, ``range``, ``spot``).
        :return: the new light's index within the extension's own ``lights`` array.
        """
        if _KHR_LIGHTS not in self._extensions_used:
            self._extensions_used.append(_KHR_LIGHTS)
        self._lights.append(dict(light))
        return len(self._lights) - 1

    def add_skin(self, skin: dict[str, Any]) -> int:
        """Adds a skin, authored as a glTF skin object.

        :param skin: the skin object (``joints``, ``inverseBindMatrices``, optionally ``skeleton``).
        :return: the new skin's index.
        """
        self._skins.append(dict(skin))
        return len(self._skins) - 1

    def add_material(self, material: dict[str, Any]) -> int:
        """Adds a material, authored as a glTF material object.

        :param material: the material object.
        :return: the new material's index.
        """
        self._materials.append(dict(material))
        return len(self._materials) - 1

    def add_image(self, png_bytes: bytes, *, name: str | None = None) -> int:
        """Adds an image, carried as a base64 ``data:`` URI (plan_gltf.md ``GLTF-190``).

        A `data:` URI rather than a bufferView so the *same* image object works unchanged in both
        containers, and so a reader of the committed `.gltf` can see that the fixture has a texture
        without opening a second file. `GLTF-196`'s other two source shapes -- a bufferView and an
        external file -- are covered by their own scratch-document tests, which can vary one thing
        at a time; a corpus asset cannot.

        :param png_bytes: the encoded PNG.
        :param name: optional image name.
        :return: the new image's index.
        """
        image: dict[str, Any] = {}
        if name is not None:
            image["name"] = name
        image["uri"] = "data:image/png;base64," + base64.b64encode(png_bytes).decode("ascii")
        image["mimeType"] = "image/png"
        self._images.append(image)
        return len(self._images) - 1

    def add_sampler(self, sampler: dict[str, Any]) -> int:
        """Adds a sampler, authored as a glTF sampler object.

        :param sampler: the sampler object (``magFilter``, ``minFilter``, ``wrapS``, ``wrapT``).
        :return: the new sampler's index.
        """
        self._samplers.append(dict(sampler))
        return len(self._samplers) - 1

    def add_texture(self, *, source: int, sampler: int | None = None,
                    name: str | None = None) -> int:
        """Adds a texture pairing an image with an optional sampler.

        :param source: index of the image.
        :param sampler: index of the sampler, or ``None`` for glTF's own defaults (§5.29).
        :param name: optional texture name.
        :return: the new texture's index.
        """
        texture: dict[str, Any] = {}
        if name is not None:
            texture["name"] = name
        if sampler is not None:
            texture["sampler"] = sampler
        texture["source"] = source
        self._textures.append(texture)
        return len(self._textures) - 1

    def add_animation(self, animation: dict[str, Any]) -> int:
        """Adds an animation, authored as a glTF animation object.

        :param animation: the animation object (``samplers`` and ``channels``).
        :return: the new animation's index.
        """
        self._animations.append(dict(animation))
        return len(self._animations) - 1

    # --- emission -------------------------------------------------------------------------------

    @property
    def document(self) -> dict[str, Any]:
        """The glTF JSON document, without any buffer URI -- the container adds that.

        :return: the asset as a plain dict, in the module's fixed key order.
        """
        doc: dict[str, Any] = {
            "asset": {"version": "2.0", "generator": GENERATOR_NAME},
        }
        if self._extensions_used:
            doc["extensionsUsed"] = list(self._extensions_used)
        if self._extensions_required:
            doc["extensionsRequired"] = list(self._extensions_required)
        if self._default_scene is not None:
            doc["scene"] = self._default_scene
        doc["scenes"] = self._scenes
        doc["nodes"] = self._nodes
        if self._lights:
            doc["extensions"] = {_KHR_LIGHTS: {"lights": self._lights}}
        if self._cameras:
            doc["cameras"] = self._cameras
        doc["meshes"] = self._meshes
        if self._materials:
            doc["materials"] = self._materials
        if self._textures:
            doc["textures"] = self._textures
        if self._images:
            doc["images"] = self._images
        if self._samplers:
            doc["samplers"] = self._samplers
        if self._skins:
            doc["skins"] = self._skins
        if self._animations:
            doc["animations"] = self._animations
        doc["accessors"] = self._accessors
        doc["bufferViews"] = [_ordered(v, _BUFFER_VIEW_ORDER) for v in self._buffer_views]
        doc["buffers"] = [{"byteLength": len(self._buffer)}]
        return _ordered(doc, _TOP_LEVEL_ORDER)

    @property
    def buffer_bytes(self) -> bytes:
        """The asset's single buffer, exactly as both containers carry it."""
        return bytes(self._buffer)

    def l1_expectation(self) -> dict[str, Any]:
        """The L1 expectation: the structural counts a reader must report for this asset.

        :return: the ``l1`` block of the fixture's expectation manifest.
        """
        return {
            "assetVersion": "2.0",
            "defaultScene": self._default_scene if self._default_scene is not None else 0,
            "sceneCount": len(self._scenes),
            "nodeCount": len(self._nodes),
            "meshCount": len(self._meshes),
            "materialCount": len(self._materials),
            "textureCount": len(self._textures),
            "imageCount": len(self._images),
            "samplerCount": len(self._samplers),
            "skinCount": len(self._skins),
            "animationCount": len(self._animations),
            "accessorCount": len(self._accessors),
            "bufferViewCount": len(self._buffer_views),
            "bufferCount": 1,
            "bufferByteLength": len(self._buffer),
            "extensionsUsed": list(self._extensions_used),
            "extensionsRequired": list(self._extensions_required),
            "punctualLightCount": len(self._lights),
        }

    def to_gltf_text(self) -> str:
        """Renders the text container, with the buffer as a base64 ``data:`` URI.

        Indented and newline-terminated so a committed fixture stays diffable (`GLTF-003`).

        :return: the complete ``.gltf`` document text.
        """
        doc = self.document
        doc["buffers"] = [{
            "byteLength": len(self._buffer),
            "uri": _BUFFER_URI_PREFIX + base64.b64encode(bytes(self._buffer)).decode("ascii"),
        }]
        return json.dumps(doc, indent=2, ensure_ascii=True, allow_nan=False) + "\n"

    def to_glb_bytes(self) -> bytes:
        """Renders the binary container: the same asset with the buffer as the BIN chunk.

        The JSON chunk is compact and space-padded, the BIN chunk zero-padded, both to a four-byte
        boundary as §4.4.1 requires.

        :return: the complete ``.glb`` file bytes.
        """
        json_bytes = json.dumps(self.document, separators=(",", ":"), ensure_ascii=True,
                                allow_nan=False).encode("utf-8")
        json_bytes += b" " * (-len(json_bytes) % 4)
        bin_bytes = bytes(self._buffer)
        bin_bytes += b"\x00" * (-len(bin_bytes) % 4)

        total = 12 + 8 + len(json_bytes) + 8 + len(bin_bytes)
        out = bytearray()
        out += struct.pack("<III", _GLB_MAGIC, _GLB_VERSION, total)
        out += struct.pack("<II", len(json_bytes), _GLB_CHUNK_JSON)
        out += json_bytes
        out += struct.pack("<II", len(bin_bytes), _GLB_CHUNK_BIN)
        out += bin_bytes
        return bytes(out)
