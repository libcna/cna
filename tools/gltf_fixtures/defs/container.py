# SPDX-License-Identifier: MS-PL
"""Container and external-resource fixtures -- owning group ``container`` (plan_gltf.md §24.2).

The first seven assets pin the source shapes in §4.4 and §3.6.1: ordinary and padded GLB BIN
chunks, inline and external buffers, inline and external images, and a percent-decoded filename.
Each still comes from one :class:`GltfBuilder`; only the text container's URI is substituted, so
its GLB twin cannot drift to different geometry. The eighth fixture is the deliberately refused
``extensionsRequired`` case.

Specification: §4.4 ``glb-file-format-specification``, §3.6.1 ``buffers-and-buffer-views``,
§3.9.2 ``image``, §3.12 ``specifying-extensions``.
"""

from __future__ import annotations

from ..builder import TRIANGLES, UNSIGNED_SHORT, GltfBuilder
from ..l5 import unsupported as l5_unsupported
from ..manifest import Fixture, GltfEmission, l3_primitive, world_positions
from ..png import reference_texture
from .common import TRIANGLE_INDICES, TRIANGLE_NORMALS, TRIANGLE_POSITIONS

_TRIANGLE_TANGENTS = [(1.0, 0.0, 0.0, 1.0)] * 3
_TRIANGLE_TEXCOORDS = [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)]
_BUFFER_SPEC = ["glb-file-format-specification", "buffers-and-buffer-views"]
_IMAGE_SPEC = _BUFFER_SPEC + ["image", "texture", "materials"]

#: An extension CNA does not implement. Chosen because cgltf *parses* it -- so the fixture cannot
#: pass by accident through an unknown-extension shortcut -- while CNA honours nothing it declares.
_UNSUPPORTED = "KHR_materials_variants"


def _buffer_triangle(name: str, *, trailing_bytes: int = 0) -> tuple[GltfBuilder, int]:
    """The common non-trivial geometry behind the five buffer/container source fixtures."""
    b = GltfBuilder(name)
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS,
                                   accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    if trailing_bytes:
        # Legal bytes outside every bufferView, used to choose the GLB/base64 padding residue
        # without perturbing any semantic value or inventing a dummy accessor.
        b.pad(trailing_bytes)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="ContainerTriangle")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return b, mesh


def _buffer_fixture(*, fixture_id: str, description: str, features: list[str],
                    emission: GltfEmission, trailing_bytes: int = 0,
                    buffer_sidecar: str | None = None) -> Fixture:
    """Builds one source-form fixture with full L1-L5 geometry expectations."""
    b, mesh = _buffer_triangle(fixture_id, trailing_bytes=trailing_bytes)
    if buffer_sidecar is not None:
        if emission.sidecars:
            raise ValueError(f"{fixture_id}: buffer sidecar would overwrite explicit sidecars")
        # The sidecar is the exact buffer owned by THIS builder, not a reconstructed copy. That is
        # the single-source-of-truth property the GLB twin already has by construction.
        emission.sidecars[buffer_sidecar] = b.buffer_bytes
    return Fixture(
        id=fixture_id, audit_fixture=None, owning_group="container",
        description=description, builder=b,
        validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=features, spec_anchors=_BUFFER_SPEC, gltf_emission=emission,
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="ContainerTriangle", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS,
            indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def glb_basic() -> Fixture:
    """The smallest named ordinary JSON+BIN GLB witness for **`GLTF-026`**."""
    # 78 semantic bytes plus two legal trailing bytes makes the BIN payload itself aligned: this
    # is the zero-padding control for `glb-bin-chunk-padding` below.
    return _buffer_fixture(
        fixture_id="glb-basic", trailing_bytes=2, emission=GltfEmission(),
        description="An ordinary GLB JSON+BIN asset whose 80-byte payload needs no BIN padding. "
                    "It is the zero-padding control for the named non-aligned chunk fixture.",
        features=["GLB JSON chunk", "GLB BIN chunk", "zero BIN padding", "data URI twin"],
    )


def glb_bin_chunk_padding() -> Fixture:
    """A 78-byte BIN payload whose GLB chunk has exactly two zero pad bytes (`GLTF-026`)."""
    return _buffer_fixture(
        fixture_id="glb-bin-chunk-padding", emission=GltfEmission(),
        description="A GLB whose semantic BIN payload is 78 bytes, forcing exactly two zero pad "
                    "bytes in the four-byte-aligned chunk while buffer.byteLength remains 78.",
        features=["GLB BIN padding", "two zero pad bytes", "buffer.byteLength excludes padding"],
    )


def gltf_external_bin() -> Fixture:
    """A text glTF resolving buffer zero from a committed relative `.bin` (`GLTF-028`)."""
    sidecar = "gltf-external-bin.geometry.bin"
    return _buffer_fixture(
        fixture_id="gltf-external-bin", trailing_bytes=1,
        emission=GltfEmission(buffer_uri=sidecar), buffer_sidecar=sidecar,
        description="A text glTF whose 79-byte geometry buffer is a relative .bin sidecar. The "
                    "sidecar is generated and hashed with the asset, while its GLB twin carries "
                    "the same bytes in the BIN chunk.",
        features=["external .bin", "relative buffer URI", "generated sidecar", "GLB BIN twin"],
    )


def gltf_data_uri_bin() -> Fixture:
    """A named inline-buffer source witness, including a two-pad base64 tail (`GLTF-029`)."""
    return _buffer_fixture(
        fixture_id="gltf-data-uri-bin", trailing_bytes=1, emission=GltfEmission(),
        description="A text glTF carrying the same 79-byte geometry buffer inline as a base64 "
                    "data URI. Its payload residue requires the decoder to consume a trailing "
                    "double equals padding sequence without losing or inventing a byte.",
        features=["buffer data URI", "base64 double padding", "inline geometry", "GLB BIN twin"],
    )


def _image_fixture(*, fixture_id: str, external: bool) -> Fixture:
    """Builds the external/data-URI image pair from byte-identical geometry and PNG bytes."""
    b = GltfBuilder(fixture_id)
    png = reference_texture()
    image = b.add_image(png, name="ContainerReference")
    texture = b.add_texture(source=image, name="ContainerTexture")
    material = b.add_material({
        "name": "ContainerTextured",
        "pbrMetallicRoughness": {"baseColorTexture": {"index": texture}},
    })
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS,
                                   accessor_type="VEC3")
    tangent = b.add_packed_accessor(usage="TANGENT", values=_TRIANGLE_TANGENTS,
                                    accessor_type="VEC4")
    texcoord = b.add_packed_accessor(usage="TEXCOORD_0", values=_TRIANGLE_TEXCOORDS,
                                     accessor_type="VEC2")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal, "TANGENT": tangent,
                       "TEXCOORD_0": texcoord},
        "indices": indices,
        "material": material,
        "mode": TRIANGLES,
    }], name="ContainerTexturedTriangle")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    if external:
        sidecar = "gltf-external-image.texture.png"
        emission = GltfEmission(image_uri_overrides={image: sidecar}, sidecars={sidecar: png})
        source_description = "a relative PNG sidecar"
        source_features = ["external image", "relative image URI", "generated PNG sidecar"]
    else:
        emission = GltfEmission()
        source_description = "an inline base64 PNG data URI"
        source_features = ["image data URI", "inline PNG"]

    expected_material = {
        "index": material,
        "name": "ContainerTextured",
        "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
        "metallicFactor": 1.0,
        "roughnessFactor": 1.0,
        "emissiveFactor": [0.0, 0.0, 0.0],
        "alphaMode": "OPAQUE",
        "alphaCutoff": 0.5,
        "doubleSided": False,
        "hasBaseColorTexture": True,
        "hasNormalTexture": False,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
        "hasEmissiveTexture": False,
    }
    return Fixture(
        id=fixture_id, audit_fixture=None, owning_group="container",
        description=f"A textured triangle whose base-colour image is {source_description}. The "
                    "external and data-URI fixtures share exact generated PNG content; each GLB "
                    "twin retains the builder's self-contained image URI.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=source_features + ["base-colour texture", "GLB self-contained image twin"],
        spec_anchors=_IMAGE_SPEC, gltf_emission=emission,
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="ContainerTexturedTriangle", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS,
            tangents=_TRIANGLE_TANGENTS, texcoords=_TRIANGLE_TEXCOORDS,
            indices=TRIANGLE_INDICES, material=expected_material)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def gltf_external_image() -> Fixture:
    """A material image read from a generated relative sidecar (`GLTF-194`)."""
    return _image_fixture(fixture_id="gltf-external-image", external=True)


def gltf_data_uri_image() -> Fixture:
    """The exact image content carried inline as a data URI (`GLTF-195`)."""
    return _image_fixture(fixture_id="gltf-data-uri-image", external=False)


def gltf_uri_percent_encoded() -> Fixture:
    """An encoded buffer URI resolving to a sidecar whose real name contains a space (`GLTF-030`)."""
    encoded = "gltf-uri-percent-encoded%20geometry.bin"
    decoded = "gltf-uri-percent-encoded geometry.bin"
    return _buffer_fixture(
        fixture_id="gltf-uri-percent-encoded", trailing_bytes=1,
        emission=GltfEmission(buffer_uri=encoded), buffer_sidecar=decoded,
        description="A text glTF whose buffer URI contains %20 while the generated sidecar's "
                    "actual filename contains a space. Only URI decoding before filesystem "
                    "resolution can load it; no file with the encoded spelling is emitted.",
        features=["percent-encoded buffer URI", "decoded sidecar filename", "URI %20"],
    )


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


FIXTURES = [
    glb_basic,
    glb_bin_chunk_padding,
    gltf_external_bin,
    gltf_data_uri_bin,
    gltf_external_image,
    gltf_data_uri_image,
    gltf_uri_percent_encoded,
    gltf_required_extension_unsupported,
]
