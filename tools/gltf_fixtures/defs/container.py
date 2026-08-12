# SPDX-License-Identifier: MS-PL
"""Container-level fixtures -- owning group ``container`` (plan_gltf.md §24.2).

These assets are structurally fine and are nevertheless **not importable**, which is the point: a
reader that only ever asks "did it parse?" imports them happily and silently produces something the
author already said would be wrong.

Specification: §3.12 ``specifying-extensions``.
"""

from __future__ import annotations

from ..builder import TRIANGLES, UNSIGNED_SHORT, GltfBuilder
from ..l5 import unsupported as l5_unsupported
from ..manifest import Fixture, l3_primitive
from .common import TRIANGLE_INDICES, TRIANGLE_NORMALS, TRIANGLE_POSITIONS

#: An extension CNA does not implement. Chosen because cgltf *parses* it -- so the fixture cannot
#: pass by accident through an unknown-extension shortcut -- while CNA honours nothing it declares.
_UNSUPPORTED = "KHR_materials_variants"


def gltf_required_extension_unsupported() -> Fixture:
    """``extensionsRequired`` naming an extension CNA does not implement. Proves **`GLTF-023`**.

    The asset is otherwise perfectly ordinary: one triangle, one node, valid buffers. Nothing about
    it fails structural validation, and that is exactly why it matters -- the only thing standing
    between it and a silently wrong import is the `extensionsRequired` check itself.
    """
    b = GltfBuilder("gltf-required-extension-unsupported")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="RequiresExtension")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    b.declare_extensions(required=[_UNSUPPORTED])
    return Fixture(
        id="gltf-required-extension-unsupported", audit_fixture=None, owning_group="container",
        description="A structurally valid asset whose extensionsRequired names an extension CNA "
                    "does not implement. It must be rejected with that extension named: the file "
                    "itself declares it cannot be interpreted correctly without it, so importing "
                    "it would produce geometry or shading its author already said would be wrong.",
        builder=b, validated_layers=["L1", "L3"],
        features=["extensionsRequired", "unsupported extension", "import rejection"],
        spec_anchors=["specifying-extensions"],
        # The primitive is spelled out because the fixture is only meaningful if a conforming
        # reader *could* have imported it -- the rejection is about the declaration, not the mesh.
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="RequiresExtension", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4={},
        l5=l5_unsupported(
            "The asset is rejected at validation, so no vertex or index buffer is produced. A "
            "golden would only arrive if CNA implemented the required extension.",
            ["GLTF-023"]),
        rejection={
            "stage": "validation",
            "task": "GLTF-023",
            "errorContains": [_UNSUPPORTED, "extensionsRequired"],
            "note": "The diagnostic must name the extension, because 'this file needs something "
                    "you do not have' is only actionable if it says which something.",
        },
    )


FIXTURES = [gltf_required_extension_unsupported]
