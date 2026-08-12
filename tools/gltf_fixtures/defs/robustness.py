# SPDX-License-Identifier: MS-PL
"""Malformed-input fixtures -- owning group ``robustness`` (plan_gltf.md §24.2).

Each asset here violates a structural constraint whose violation would make decoding read outside
the file's own buffers. They exist to prove the validation pass actually runs: without them,
`cgltf_validate` could be removed again and every other test would stay green.

Specification: §3.6.2 ``accessors``, §3.6.2.4 ``data-alignment``.
"""

from __future__ import annotations

from ..builder import FLOAT, TRIANGLES, UNSIGNED_SHORT, GltfBuilder, flatten, pack
from ..l5 import unsupported as l5_unsupported
from ..manifest import Fixture
from .common import TRIANGLE_INDICES, TRIANGLE_POSITIONS


def bad_accessor_out_of_bounds() -> Fixture:
    """A POSITION accessor declaring more elements than its ``bufferView`` can hold.

    Proves **`GLTF-021`**. The accessor says three `VEC3` floats (36 bytes) live in a bufferView
    that is only 24 bytes long, so decoding it reads 12 bytes past the end of the view -- and, for
    the last vertex, past the buffer itself. This is precisely the class of constraint
    `cgltf_validate` exists to catch, and precisely why its failure is a rejection rather than a
    warning: there is no partially-correct import to salvage.
    """
    b = GltfBuilder("bad-accessor-out-of-bounds")
    # Packed honestly, then described dishonestly: the bufferView is deliberately declared two
    # vertices long while the accessor claims three. Authoring the lie in the *description* rather
    # than the data keeps the bytes themselves readable.
    packed = pack(flatten(TRIANGLE_POSITIONS), FLOAT)
    offset = b.append_bytes(packed, alignment=4)
    short_view = b.add_buffer_view(offset, len(packed) - 12)
    position = b.add_accessor(
        usage="POSITION", component_type=FLOAT, accessor_type="VEC3", count=3,
        expected=flatten(TRIANGLE_POSITIONS), buffer_view=short_view,
        min_=[0.0, 0.0, 0.0], max_=[1.0, 1.0, 0.0])
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="TruncatedTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="bad-accessor-out-of-bounds", audit_fixture=None, owning_group="robustness",
        description="A POSITION accessor whose declared element count reaches 12 bytes past the "
                    "end of its bufferView. Decoding it would read outside the file's own buffer, "
                    "so it must be rejected at validation rather than imported with whatever "
                    "happened to be in adjacent memory.",
        builder=b, validated_layers=["L1"],
        features=["accessor beyond bufferView", "structural validation", "import rejection"],
        spec_anchors=["accessors", "data-alignment"],
        # No L3 expectation: there is no correct semantic mesh for a file this malformed, and
        # inventing one would imply a conforming reader could produce something.
        l3={"primitives": []},
        l4={},
        l5=l5_unsupported("The asset is rejected at validation, so no buffers are produced.",
                          ["GLTF-021"]),
        rejection={
            "stage": "validation",
            "task": "GLTF-021",
            "errorContains": ["validation", "bad-accessor-out-of-bounds"],
            "note": "cgltf_validate's own check set is entirely of this kind, which is what makes "
                    "'reject on failure' the whole severity policy rather than one branch of it.",
        },
    )


FIXTURES = [bad_accessor_out_of_bounds]
