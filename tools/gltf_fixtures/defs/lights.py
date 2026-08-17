# SPDX-License-Identifier: MS-PL
"""``KHR_lights_punctual`` fixtures -- owning group ``lights`` (plan_gltf.md §24.2).

Importing a glTF light rig into XNA is **lossy by construction**, and every fixture here exists to
pin one of the losses rather than to prove a mapping. XNA's stock effects light with exactly three
directional lights: no point term, no spot term, no falloff, no cone, and a ``DiffuseColor`` bounded
to ``[0,1]`` where glTF's intensity is photometric and unbounded.

None of that is an error, and none of it looks like one on screen -- a scene lit by six ranged point
lights imports as three directionals aimed at the origin and simply renders differently. So the
assertions here are about what the importer **says**, not only about what it produces.

Specification: §3.10 ``cameras`` (for the node convention), ``KHR_lights_punctual``.
"""

from __future__ import annotations

from ..builder import TRIANGLES, UNSIGNED_SHORT, GltfBuilder
from ..manifest import Fixture, l3_primitive, world_positions
from .common import TRIANGLE_INDICES, TRIANGLE_NORMALS, TRIANGLE_POSITIONS

_SPEC = ["lights-punctual", "nodes-and-hierarchy"]


def _carrier(b: GltfBuilder, mesh_name: str) -> int:
    """A triangle for the lights to fall on, so the scene is not lights-only."""
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    return b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name=mesh_name)


def lights_kinds_and_reach() -> Fixture:
    """One light of each kind, two of them bounded in reach. Owns **GLTF-327**.

    The three glTF light types collapse onto XNA's single one, but they do not collapse *equally*,
    and the fixture separates the losses so a report cannot conflate them:

    * the **directional** light maps exactly -- it is the one kind XNA has, so it is the control
      that proves the other two are being counted for a real reason;
    * the **point** light loses its position, becoming a directional light aimed from where it
      stood toward the scene origin;
    * the **spot** light loses that *and* its cone, which is a materially worse approximation --
      a focused beam becomes full-scene illumination.

    Two of them additionally declare a finite ``range``. Range and cone both bound a light's
    **reach**, which a directional light has no concept of, so a lamp the author scoped to one room
    lights everything. That error grows with distance from the light -- which is to say it is
    smallest exactly where an author is looking while placing it.

    Intensities are deliberately in gamut (``1.0``) so this fixture isolates kind-and-reach from
    ``lights-over-budget``'s clamping.
    """
    b = GltfBuilder("lights-kinds-and-reach")
    mesh = _carrier(b, "LitTri")

    directional = b.add_light({"name": "Key", "type": "directional",
                               "color": [1.0, 0.95, 0.9], "intensity": 1.0})
    point = b.add_light({"name": "Lamp", "type": "point", "color": [0.2, 0.4, 1.0],
                         "intensity": 1.0, "range": 12.5})
    spot = b.add_light({"name": "Beam", "type": "spot", "color": [1.0, 0.5, 0.0],
                        "intensity": 1.0, "range": 30.0,
                        "spot": {"innerConeAngle": 0.4, "outerConeAngle": 0.8}})

    subject = b.add_node(name="LitSubject", mesh=mesh)
    key_node = b.add_node(name="KeyNode", light=directional)
    lamp_node = b.add_node(name="LampNode", light=point, translation=[0.0, 6.0, 0.0])
    beam_node = b.add_node(name="BeamNode", light=spot, translation=[4.0, 4.0, 4.0])
    b.add_scene([subject, key_node, lamp_node, beam_node], name="Scene")
    b.set_default_scene(0)

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["lights"] = {
        "declaredCount": 3,
        "importedCount": 3,
        "droppedCount": 0,
        "approximatedPointCount": 1,
        "approximatedSpotCount": 1,
        "clampedIntensityCount": 0,
        "ignoredRangeCount": 2,
        "ignoredConeAngleCount": 1,
        "entries": [
            {"light": directional, "node": key_node, "nodeName": "KeyNode", "type": "directional",
             "exact": True,
             "note": "The one kind XNA has. Its node has no rotation, so it travels along the "
                     "node's own -Z, which is glTF's convention and XNA's alike."},
            {"light": point, "node": lamp_node, "nodeName": "LampNode", "type": "point",
             "exact": False, "range": 12.5,
             "note": "Becomes a directional light aimed from its world position at the scene "
                     "origin -- from (0,6,0), that is straight down."},
            {"light": spot, "node": beam_node, "nodeName": "BeamNode", "type": "spot",
             "exact": False, "range": 30.0, "innerConeAngle": 0.4, "outerConeAngle": 0.8,
             "note": "Approximated the same way as the point light AND loses its cone, which is "
                     "why the two are counted apart: a focused beam becoming full-scene "
                     "illumination is a materially worse approximation than losing a position."},
        ],
        "reachRule": "range and cone angles both bound a light's REACH, and a directional light "
                     "has no bounds at all -- so a lamp the author scoped to one room lights the "
                     "whole scene. The error grows with distance from the light, which is where "
                     "an author placing it is least likely to see it. Ignored, and counted "
                     "(GLTF-327).",
    }
    return Fixture(
        id="lights-kinds-and-reach", audit_fixture=None, owning_group="lights",
        description="One directional, one point and one spot light, two of them with a finite "
                    "range and the spot with cone angles. The directional light is the control "
                    "that maps exactly; the other two pin what is lost, counted apart because "
                    "losing a cone is worse than losing a position.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["KHR_lights_punctual", "directional light", "point light", "spot light",
                  "light range ignored", "cone angles ignored"],
        spec_anchors=_SPEC,
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="LitTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def lights_over_budget() -> Fixture:
    """Five lights where XNA binds three, one of them photometrically bright. Owns **GLTF-329**.

    Two independent things are pinned here, and neither is visible in the imported result:

    **Which three survive.** XNA has ``DirectionalLight0..2`` and nothing else, so two of these
    five are dropped. *Which* two must not depend on a hash order or on the extension's own
    ``lights`` array -- it is the **node array order, restricted to the default scene**, because
    that is the only ordering the file itself defines over light *placements*. The extension's
    array is an ordering over light *definitions*, which is a different thing entirely: the
    fixture's node order and light-definition order are deliberately reversed so a reader using
    the wrong one imports a visibly different three.

    **What an unbounded intensity becomes.** glTF intensity is photometric -- lux for a
    directional light -- and ``DiffuseColor`` is a ``[0,1]`` colour. The third light is authored at
    ``683``, the lumens-per-watt figure that makes a real photometric value ordinary rather than
    absurd, and it imports as plain white. That is not a bug and it is not fixable inside XNA's
    lighting model; it is a thing an author comparing two renders must be *told*.
    """
    b = GltfBuilder("lights-over-budget")
    mesh = _carrier(b, "OverlitTri")

    # Light DEFINITIONS in the reverse of the node order below, so a reader that walked this array
    # instead of the node array keeps a different three -- and the colours make which three obvious.
    fifth = b.add_light({"name": "Fifth", "type": "directional",
                         "color": [0.5, 0.5, 0.5], "intensity": 1.0})
    fourth = b.add_light({"name": "Fourth", "type": "directional",
                         "color": [0.0, 0.0, 1.0], "intensity": 1.0})
    third = b.add_light({"name": "Third", "type": "directional",
                        "color": [1.0, 1.0, 1.0], "intensity": 683.0})
    second = b.add_light({"name": "Second", "type": "directional",
                          "color": [0.0, 1.0, 0.0], "intensity": 1.0})
    first = b.add_light({"name": "First", "type": "directional",
                         "color": [1.0, 0.0, 0.0], "intensity": 1.0})

    subject = b.add_node(name="OverlitSubject", mesh=mesh)
    n1 = b.add_node(name="First", light=first)
    n2 = b.add_node(name="Second", light=second)
    n3 = b.add_node(name="Third", light=third)
    n4 = b.add_node(name="Fourth", light=fourth)
    n5 = b.add_node(name="Fifth", light=fifth)
    b.add_scene([subject, n1, n2, n3, n4, n5], name="Scene")
    b.set_default_scene(0)

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["lights"] = {
        "declaredCount": 5,
        "importedCount": 3,
        "droppedCount": 2,
        "approximatedPointCount": 0,
        "approximatedSpotCount": 0,
        "clampedIntensityCount": 1,
        "worstPreClampChannel": 683.0,
        "ignoredRangeCount": 0,
        "ignoredConeAngleCount": 0,
        "orderRule": "Node array order, restricted to the default scene -- the only ordering the "
                     "file defines over light PLACEMENTS. The extension's own lights array orders "
                     "light DEFINITIONS, which is a different thing; it is authored in reverse "
                     "here so a reader using it keeps a visibly different three.",
        "importedDiffuseColors": [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [1.0, 1.0, 1.0]],
        "droppedNodeNames": ["Fourth", "Fifth"],
        "entries": [
            {"node": n1, "nodeName": "First", "light": first, "imported": True,
             "diffuseColor": [1.0, 0.0, 0.0]},
            {"node": n2, "nodeName": "Second", "light": second, "imported": True,
             "diffuseColor": [0.0, 1.0, 0.0]},
            {"node": n3, "nodeName": "Third", "light": third, "imported": True,
             "intensity": 683.0, "preClamp": [683.0, 683.0, 683.0],
             "diffuseColor": [1.0, 1.0, 1.0],
             "note": "683 lux is an ordinary photometric value, not an absurd one -- it is the "
                     "lumens-per-watt constant. It imports as plain white, which is not fixable "
                     "inside XNA's lighting model and must therefore be reported."},
            {"node": n4, "nodeName": "Fourth", "light": fourth, "imported": False},
            {"node": n5, "nodeName": "Fifth", "light": fifth, "imported": False},
        ],
        "walkRule": "Extraction does NOT stop at three: it keeps walking to count what it drops, "
                    "because '3 of 5 imported' is actionable and '3 imported' is not.",
    }
    return Fixture(
        id="lights-over-budget", audit_fixture=None, owning_group="lights",
        description="Five directional lights where XNA binds three, with the light-definition "
                    "array authored in reverse of the node order so the tie-break is observable, "
                    "and one light at 683 lux so the photometric clamp is too.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["KHR_lights_punctual", "more lights than XNA can bind", "light ordering",
                  "photometric intensity clamped"],
        spec_anchors=_SPEC,
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="OverlitTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


FIXTURES = [lights_kinds_and_reach, lights_over_budget]
