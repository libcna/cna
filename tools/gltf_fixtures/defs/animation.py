# SPDX-License-Identifier: MS-PL
"""Animation fixtures -- owning group ``animation`` (plan_gltf.md §17.2, §24.2).

Proves **D6**: an animation channel targeting an ordinary mesh node -- rigid, unskinned motion, the
most common kind of animation in an authored scene -- is silently dropped. ``ExtractClips``
resolves every channel target against the skeleton's joint set, so a channel whose target is not a
joint has nowhere to go and is discarded without a warning.

D6 cannot be fixed without a real node hierarchy, which is why the plan makes it a consequence of
the Phase 5 architecture decision rather than an independent animation defect.

Specification: §3.11 ``animations``, §3.5.3 ``transformations``.
"""

from __future__ import annotations

import math

from ..builder import FLOAT, TRIANGLES, UNSIGNED_SHORT, GltfBuilder
from ..manifest import Defect, Fixture, l3_primitive, mat_from_trs, transform_point, world_positions
from .common import TRIANGLE_INDICES, TRIANGLE_NORMALS, TRIANGLE_POSITIONS

_HALF_SQRT2 = math.sqrt(0.5)
#: Rest pose and a quarter turn about +Z. The quarter turn maps the triangle's +X vertex onto +Y,
#: so the pose at t=1 is exactly computable and visibly different from the rest pose.
_KEY_TIMES = [0.0, 1.0]
_KEY_ROTATIONS = [(0.0, 0.0, 0.0, 1.0), (0.0, 0.0, _HALF_SQRT2, _HALF_SQRT2)]


def anim_rigid_node() -> Fixture:
    """f7 -- one rotation channel on an unskinned mesh node. Proves **D6**."""
    b = GltfBuilder("anim-rigid-node")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="RigidTri")
    # Authored with TRS rather than 'matrix': §3.5.3 forbids 'matrix' on an animated node.
    node = b.add_node(name="SpinningMesh", mesh=mesh, rotation=list(_KEY_ROTATIONS[0]))
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    times = b.add_packed_accessor(usage="animation input (time)", values=_KEY_TIMES,
                                  accessor_type="SCALAR", component_type=FLOAT)
    rotations = b.add_packed_accessor(usage="animation output (rotation)", values=_KEY_ROTATIONS,
                                      accessor_type="VEC4", component_type=FLOAT)
    b.add_animation({
        "name": "Spin",
        "samplers": [{"input": times, "output": rotations, "interpolation": "LINEAR"}],
        "channels": [{"sampler": 0, "target": {"node": node, "path": "rotation"}}],
    })

    poses = []
    for t, q in zip(_KEY_TIMES, _KEY_ROTATIONS):
        matrix = mat_from_trs(None, q, None)
        poses.append({
            "time": t,
            "nodeLocalColumnMajor": matrix,
            "worldPositions": [transform_point(matrix, p) for p in TRIANGLE_POSITIONS],
        })

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["animation"] = {
        "animationCount": 1,
        "clipNames": ["Spin"],
        "duration": _KEY_TIMES[-1],
        "channels": [{
            "animation": 0,
            "channel": 0,
            "targetNode": node,
            "targetNodeName": "SpinningMesh",
            "path": "rotation",
            "interpolation": "LINEAR",
            "targetsSkinJoint": False,
            "times": list(_KEY_TIMES),
            "values": [list(q) for q in _KEY_ROTATIONS],
        }],
        "posesAtKeyTimes": poses,
    }
    return Fixture(
        id="anim-rigid-node", audit_fixture="f7", owning_group="animation",
        description="A single LINEAR rotation channel driving an unskinned mesh node through a "
                    "quarter turn about +Z. There is no skin anywhere in the file, so the channel "
                    "targets a plain scene node -- the ordinary case for a door, a turntable or a "
                    "clock hand.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["animation.channel targeting a non-joint node", "rotation path",
                  "LINEAR interpolation", "no skin"],
        spec_anchors=["animations", "transformations"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="RigidTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
        defects=[Defect(
            id="D6", owner="GLTF-ANIMATION", first_divergent_layer="L4",
            summary="Rigid (unskinned) node animation was silently dropped: ExtractClips resolves "
                    "every channel target against the skin's joint set, so a channel targeting an "
                    "ordinary mesh node matched nothing and was discarded with no warning, and the "
                    "converter called ExtractClips only for a skinned group at all. GLTF-293 adds "
                    "ExtractSceneNodeClips, which resolves channels against the scene graph and "
                    "produces a clip whose tracks target the animated node's own ModelBone. "
                    "GLTF-294 still owns carrying such a clip through the .cnj and playing it, so "
                    "the animation is imported and reported but not yet serialised.",
            # The owning-task list said GLTF-284 until GLTF-293 landed. GLTF-284 is the morph
            # weight-vector validation task and has nothing to do with rigid animation; the real
            # owner is GLTF-293 (plan_gltf.md §29 Phase 14), with GLTF-294 unifying playback.
            owning_tasks=["GLTF-103", "GLTF-113", "GLTF-114", "GLTF-293", "GLTF-294"],
            closed_tasks=["GLTF-103", "GLTF-113", "GLTF-114", "GLTF-293", "GLTF-294"],
            remaining_tasks=[],
            status="fixed",
            divergent_fields=[],
            current_actual={
                "importedClipCount": 1,
                "importedTrackCount": 1,
                "clipTargetSpace": "SceneNode",
                "warningEmitted": True,
                "clipExtractionGatedOnSkin": False,
                "serialisedToCnj": True,
                "playable": True,
                "note": "ExtractSceneNodeClips resolves the rotation channel against the scene "
                        "graph and yields one clip, 'Spin', with one track on the SpinningMesh "
                        "node's own bone -- identity at t=0 and a quarter turn about +Z at t=1, "
                        "with the scale components filled from the node's bind pose rather than "
                        "from zero. Both original mechanisms are gone: extraction is no longer "
                        "gated on a skin, and a non-joint target resolves instead of being "
                        "skipped. GLTF-294 then gave the clip a target space it declares in the "
                        ".cnj, a ModelAnimationsEXT container on the unskinned model's Tag, and "
                        "ApplyClipToBonesEXT to pose the bones from it -- which refuses a "
                        "joint-palette clip outright, because applying palette indices to "
                        "Model::Bones would pose the wrong bones with no symptom but wrong "
                        "motion. One boundary is recorded rather than resolved: Model::Tag holds "
                        "one object, so a file with BOTH a skin and rigid node animation has "
                        "nowhere to put the rigid clips and the importer reports that by name "
                        "(GLTF-295).",
            },
            prior_actual={
                "importedClipCount": 0,
                "warningEmitted": False,
                "clipExtractionGatedOnSkin": True,
                "clipCountIfExtractClipsWereCalled": 1,
                "trackCountIfExtractClipsWereCalled": 0,
                "measuredOn": "fb3728267e8f2179d43b96357ff372ae712b7e7f",
                "note": "What the forensic audit measured: the converter called ExtractClips only "
                        "for a skinned group, so with no skin the .cnj had no 'animations' key at "
                        "all. Even when called, ExtractClips resolved the channel target against "
                        "the joint set and skipped it, producing a clip with zero tracks.",
            },
        )],
    )


FIXTURES = [anim_rigid_node]
