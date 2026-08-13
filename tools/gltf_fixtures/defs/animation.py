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
from ..manifest import (Defect, Fixture, l3_primitive, mat_from_trs, mat_mul, transform_point,
                        world_positions)
from .common import TRIANGLE_INDICES, TRIANGLE_NORMALS, TRIANGLE_POSITIONS


def _carrier(b: GltfBuilder, *, mesh_name: str,
             targets: list[dict[str, int]] | None = None,
             weights: list[float] | None = None) -> tuple[int, int, int, int]:
    """Adds the shared carrier triangle to ``b``.

    Every fixture below animates *something*; the geometry is never the thing under test, so it is
    built once here rather than restated ten times where a typo could quietly mean two different
    meshes.

    :param b: the builder to add to.
    :param mesh_name: the mesh's name.
    :param targets: optional morph targets, authored as glTF target objects.
    :param weights: optional default morph weights.
    :return: ``(mesh, position, normal, indices)`` -- the mesh index and its accessor indices.
    """
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    primitive: dict = {
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }
    if targets is not None:
        primitive["targets"] = targets
    mesh = b.add_mesh([primitive], name=mesh_name, weights=weights)
    return mesh, position, normal, indices


def _sampler(b: GltfBuilder, *, times: list[float], values: list, value_type: str,
             interpolation: str = "LINEAR") -> dict:
    """Packs one animation sampler's input/output accessors and returns the sampler object.

    :param b: the builder to add the accessors to.
    :param times: the sampler's input (key times, in seconds).
    :param values: the sampler's output, already in glTF's own element form.
    :param value_type: the output accessor type (``SCALAR``, ``VEC3``, ``VEC4``).
    :param interpolation: ``LINEAR``, ``STEP`` or ``CUBICSPLINE``.
    :return: the glTF sampler object.
    """
    return {
        "input": b.add_packed_accessor(usage="animation input (time)", values=times,
                                       accessor_type="SCALAR", component_type=FLOAT),
        "output": b.add_packed_accessor(usage="animation output", values=values,
                                        accessor_type=value_type, component_type=FLOAT),
        "interpolation": interpolation,
    }

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


#: `anim-nonzero-start`'s key times: the first is deliberately NOT 0, which is the case the
#: duration and start-behaviour question exists for. The last is 3.0, so "last key" (3.0) and
#: "span between the keys" (1.5) are different numbers and a manifest can distinguish them.
_NONZERO_START_TIMES = [1.5, 3.0]


def anim_nonzero_start() -> Fixture:
    """A clip whose first keyframe is at ``t = 1.5``. Owns **GLTF-299**.

    glTF animations live on an absolute timeline anchored at 0 and Appendix C clamps a sampler
    below its first key to that key's value, so two questions have exactly one spec-conformant
    answer each and the manifest states both:

    * the clip's duration is the **last** input sample (3.0) -- not the span between the first and
      last keys (1.5), which would silently reinterpret the timeline as clip-relative and make
      every authored time wrong by the offset;
    * the pose anywhere in ``[0, 1.5]`` is the **first key's**, held -- not the node's rest pose,
      and not an extrapolation backwards along the curve.

    The rest pose is authored as a distinct third rotation precisely so "holds the first key" and
    "falls back to the node's own transform" cannot be confused: they are different quaternions.
    """
    b = GltfBuilder("anim-nonzero-start")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="LateStartTri")

    # Rest pose: the identity rotation. First key: a quarter turn. Last key: back to identity.
    # So the rest pose and the first key differ, and the first and last keys differ.
    rest_rotation = [0.0, 0.0, 0.0, 1.0]
    key_rotations = [(0.0, 0.0, _HALF_SQRT2, _HALF_SQRT2), (0.0, 0.0, 0.0, 1.0)]

    node = b.add_node(name="LateStartMesh", mesh=mesh, rotation=rest_rotation)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    times = b.add_packed_accessor(usage="animation input (time)", values=_NONZERO_START_TIMES,
                                  accessor_type="SCALAR", component_type=FLOAT)
    rotations = b.add_packed_accessor(usage="animation output (rotation)", values=key_rotations,
                                      accessor_type="VEC4", component_type=FLOAT)
    b.add_animation({
        "name": "LateSpin",
        "samplers": [{"input": times, "output": rotations, "interpolation": "LINEAR"}],
        "channels": [{"sampler": 0, "target": {"node": node, "path": "rotation"}}],
    })

    poses = []
    for t, q in zip(_NONZERO_START_TIMES, key_rotations):
        matrix = mat_from_trs(None, q, None)
        poses.append({
            "time": t,
            "nodeLocalColumnMajor": matrix,
            "worldPositions": [transform_point(matrix, p) for p in TRIANGLE_POSITIONS],
        })

    first_key_matrix = mat_from_trs(None, key_rotations[0], None)
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["animation"] = {
        "animationCount": 1,
        "clipNames": ["LateSpin"],
        "duration": _NONZERO_START_TIMES[-1],
        "firstKeyTime": _NONZERO_START_TIMES[0],
        "durationRule": "the last input sample across every channel, on glTF's own absolute "
                        "timeline anchored at 0 -- NOT the span between the first and last keys, "
                        "which would be 1.5 here and would shift every authored time by the "
                        "offset.",
        "restPoseRotation": list(rest_rotation),
        "posesBeforeFirstKey": [
            {"time": 0.0, "nodeLocalColumnMajor": first_key_matrix},
            {"time": 0.75, "nodeLocalColumnMajor": first_key_matrix},
            {"time": _NONZERO_START_TIMES[0], "nodeLocalColumnMajor": first_key_matrix},
        ],
        "startRule": "Appendix C clamps a sampler below its first key to that key's value, so the "
                     "pose anywhere in [0, 1.5] is the FIRST KEY's -- not the node's rest pose, "
                     "which is a different rotation here on purpose.",
        "channels": [{
            "animation": 0,
            "channel": 0,
            "targetNode": node,
            "targetNodeName": "LateStartMesh",
            "path": "rotation",
            "interpolation": "LINEAR",
            "targetsSkinJoint": False,
            "times": list(_NONZERO_START_TIMES),
            "values": [list(q) for q in key_rotations],
        }],
        "posesAtKeyTimes": poses,
    }
    return Fixture(
        id="anim-nonzero-start", audit_fixture=None, owning_group="animation",
        description="A LINEAR rotation channel whose first keyframe is at t=1.5, on a node whose "
                    "rest rotation differs from that first key. Separates the clip's duration "
                    "(the last sample, 3.0) from the key span (1.5), and 'holds the first key "
                    "before it' from 'falls back to the rest pose'.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["animation with a non-zero first key time", "clip duration", "pre-first-key "
                  "clamping", "rotation path", "no skin"],
        spec_anchors=["animations", "interpolation-cubic", "transformations"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="LateStartTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def morph_node_weights_override() -> Fixture:
    """One mesh with a morph target, instanced by two nodes with different ``node.weights``.

    Owns **GLTF-281** and **GLTF-282**. §3.7.2.2 gives the instancing node the final say, and
    *override* is the operative word: node A declaring ``[1]`` and node B declaring ``[0]`` for a
    mesh whose own weights are ``[0.5]`` must start at 1 and 0 respectively -- not at 1.5 and 0.5,
    and not both at the mesh's own value.

    Two nodes rather than one is the second half: ``MorphTargetDataEXT`` is per **part**, so two
    instances can only differ if the importer builds a part per instance. It does (each mesh
    instance is extracted separately), which is what makes ``GLTF-282`` supportable at all rather
    than a limitation to report.

    The target moves the triangle's first vertex 4 units along +X, so the two instances' poses are
    exactly computable and far apart.
    """
    b = GltfBuilder("morph-node-weights-override")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    target_deltas = [(4.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)]
    target_position = b.add_packed_accessor(usage="morph POSITION delta", values=target_deltas,
                                            accessor_type="VEC3", with_bounds=True)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "targets": [{"POSITION": target_position}],
        "mode": TRIANGLES,
    }], name="MorphTri", weights=[0.5])

    node_full = b.add_node(name="FullyMorphed", mesh=mesh, weights=[1.0])
    node_rest = b.add_node(name="AtRest", mesh=mesh, weights=[0.0],
                           translation=[0.0, 0.0, -10.0])
    b.add_scene([node_full, node_rest], name="Scene")
    b.set_default_scene(0)

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["morph"] = {
        "targetCount": 1,
        "meshWeights": [0.5],
        "instances": [
            {"node": node_full, "nodeName": "FullyMorphed", "nodeWeights": [1.0],
             "expectedWeights": [1.0]},
            {"node": node_rest, "nodeName": "AtRest", "nodeWeights": [0.0],
             "expectedWeights": [0.0]},
        ],
        "rule": "node.weights OVERRIDES mesh.weights (§3.7.2.2) -- it does not merge with it and "
                "does not fill in only the entries it names. 1 and 0, never 1.5 and 0.5, and never "
                "both at the mesh's own 0.5.",
        "perInstanceRule": "MorphTargetDataEXT is per PART, so two instances can only differ if the "
                           "importer builds a part per instance. It does, which is what makes "
                           "GLTF-282 supportable rather than a limitation to report.",
    }
    return Fixture(
        id="morph-node-weights-override", audit_fixture=None, owning_group="animation",
        description="One morph-target mesh instanced by two nodes whose node.weights are 1 and 0, "
                    "over a mesh.weights of 0.5. Proves the override rule and that two instances "
                    "of one mesh can hold different weights at all.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["mesh.weights", "node.weights override", "one mesh, two instances",
                  "morph target POSITION delta"],
        spec_anchors=["morph-targets", "meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="MorphTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def anim_translation_scale() -> Fixture:
    """Translation and scale channels keyed at **disjoint** times. Owns **GLTF-316**'s resampling.

    A ``ClipOut`` track carries one keyframe per time, holding all three of T/R/S at once, but glTF
    keys each path on its own sampler. Reconciling the two means baking onto the union of every
    channel's times, which is exact at each source key and an interpolation everywhere else -- and
    a ``ClipOut`` cannot say it happened, which is why ``AnimationReportEXT`` counts it
    (``GLTF-315``).

    Disjoint times are what make the union observable: translation is keyed at 0 and 2, scale at 1
    and 3, so the track has **four** keys where neither source channel has more than two. Overlap
    would produce a union the same size as its inputs and prove nothing.

    The node's rest rotation is a quarter turn -- a value no channel touches -- so "the missing
    component comes from the bind pose" is separable from "the missing component is identity".
    """
    b = GltfBuilder("anim-translation-scale")
    mesh, _, _, _ = _carrier(b, mesh_name="SlidingTri")
    node = b.add_node(name="Slider", mesh=mesh, rotation=list(_KEY_ROTATIONS[1]))
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    b.add_animation({
        "name": "SlideAndSwell",
        "samplers": [
            _sampler(b, times=[0.0, 2.0], values=[(0.0, 0.0, 0.0), (4.0, 0.0, 0.0)],
                     value_type="VEC3"),
            _sampler(b, times=[1.0, 3.0], values=[(1.0, 1.0, 1.0), (3.0, 3.0, 3.0)],
                     value_type="VEC3"),
        ],
        "channels": [
            {"sampler": 0, "target": {"node": node, "path": "translation"}},
            {"sampler": 1, "target": {"node": node, "path": "scale"}},
        ],
    })

    # Every value below is the LINEAR reading of the two channels at the union times, with each
    # channel clamped to its own first/last key outside its own range (Appendix C).
    expected = [
        {"time": 0.0, "translation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
        {"time": 1.0, "translation": [2.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
        {"time": 2.0, "translation": [4.0, 0.0, 0.0], "scale": [2.0, 2.0, 2.0]},
        {"time": 3.0, "translation": [4.0, 0.0, 0.0], "scale": [3.0, 3.0, 3.0]},
    ]
    for key in expected:
        key["rotation"] = list(_KEY_ROTATIONS[1])
        key["nodeLocalColumnMajor"] = mat_from_trs(key["translation"], key["rotation"],
                                                   key["scale"])

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["animation"] = {
        "animationCount": 1,
        "clipNames": ["SlideAndSwell"],
        "duration": 3.0,
        "trackCount": 1,
        "sourceChannelKeyCounts": [2, 2],
        "expectedTrackKeyCount": 4,
        "resampled": True,
        "resamplingRule": "A track holds T, R and S together at one time, so channels keyed apart "
                          "are baked onto the union of their times -- four keys here, from two "
                          "channels of two. Exact at every source key, interpolated between them.",
        "restRotation": list(_KEY_ROTATIONS[1]),
        "bindPoseFillRule": "The rotation no channel drives comes from the NODE'S OWN rest pose, "
                            "not from identity -- which is why the rest rotation here is a quarter "
                            "turn rather than identity.",
        "expectedKeys": expected,
    }
    return Fixture(
        id="anim-translation-scale", audit_fixture=None, owning_group="animation",
        description="One node driven by a translation channel keyed at t=0,2 and a scale channel "
                    "keyed at t=1,3. The union of the two is four keys where neither channel has "
                    "more than two, so resampling is observable; the untouched rotation must come "
                    "from the node's non-identity rest pose.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["translation path", "scale path", "channels keyed at disjoint times",
                  "union resampling", "bind-pose fill of an undriven component"],
        spec_anchors=["animations", "transformations"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SlidingTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def anim_step() -> Fixture:
    """A ``STEP`` translation channel. Locks **GLTF-301** into the corpus.

    STEP holds the sample at the *start* of the half-open interval it is in, so at exactly
    ``times[i+1]`` the next sample is already in force. Every interior key of a STEP channel lands
    on a bracket boundary once the track is resampled onto its own times, so the whole channel is
    that one case repeated -- a three-sample channel read the naive way yields ``0, 0, 20`` instead
    of ``0, 10, 20``, with the last value right only because the end clamp takes a different path.

    Three samples rather than two: with two, the first is the start clamp and the second the end
    clamp, and neither exercises the boundary rule at all.
    """
    b = GltfBuilder("anim-step")
    mesh, _, _, _ = _carrier(b, mesh_name="SteppingTri")
    node = b.add_node(name="Stepper", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    times = [0.0, 1.0, 2.0]
    values = [(0.0, 0.0, 0.0), (10.0, 0.0, 0.0), (20.0, 0.0, 0.0)]
    b.add_animation({
        "name": "Teleport",
        "samplers": [_sampler(b, times=times, values=values, value_type="VEC3",
                              interpolation="STEP")],
        "channels": [{"sampler": 0, "target": {"node": node, "path": "translation"}}],
    })

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["animation"] = {
        "animationCount": 1,
        "clipNames": ["Teleport"],
        "duration": 2.0,
        "interpolation": "STEP",
        "expectedKeys": [
            {"time": t, "translation": list(v), "nodeLocalColumnMajor": mat_from_trs(v, None, None)}
            for t, v in zip(times, values)
        ],
        "boundaryRule": "§3.6's intervals are half-open: times[i]'s value applies on "
                        "[times[i], times[i+1]), so AT times[i+1] the next sample is already in "
                        "force. Reading STEP as 'the value at the bracket's lower index' gives "
                        "0, 0, 20 here -- the last one right by accident, via the end clamp.",
        "midIntervalSamples": [
            {"time": 0.5, "translation": [0.0, 0.0, 0.0]},
            {"time": 1.5, "translation": [10.0, 0.0, 0.0]},
        ],
    }
    return Fixture(
        id="anim-step", audit_fixture=None, owning_group="animation",
        description="A three-sample STEP translation channel. Every interior key lands exactly on "
                    "a bracket boundary once resampled, which is the one case that separates a "
                    "correct STEP reader from one that always returns the bracket's lower sample.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["STEP interpolation", "translation path", "half-open interval boundary"],
        spec_anchors=["animations", "interpolation-step"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SteppingTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def anim_cubicspline() -> Fixture:
    """A ``CUBICSPLINE`` translation channel, resampled at an interior time by a second channel.

    CUBICSPLINE stores each key as an ``[in-tangent, value, out-tangent]`` triplet, so its output
    accessor has three times the input's count and every read must stride past the tangents. With
    both tangents zero the Hermite basis reduces to smoothstep, whose midpoint is exactly half --
    ``5.0`` of a 10-unit span, against the ``5.0`` a linear reader would also produce. That is
    deliberate: the midpoint is where the two agree, so the fixture states the **quarter** point
    too (``2.5`` linear versus ``1.5625`` Hermite), where they do not.

    A LINEAR scale channel keyed at ``t = 0.5`` is what forces the interior evaluation to happen
    during *extraction* rather than only at playback: without it the union of key times is the
    translation channel's own two, and the Hermite basis is never entered.
    """
    b = GltfBuilder("anim-cubicspline")
    mesh, _, _, _ = _carrier(b, mesh_name="EasedTri")
    node = b.add_node(name="Eased", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    zero = (0.0, 0.0, 0.0)
    b.add_animation({
        "name": "Ease",
        "samplers": [
            # Two keys, six elements: in-tangent, value, out-tangent for each.
            _sampler(b, times=[0.0, 1.0],
                     values=[zero, zero, zero, zero, (10.0, 0.0, 0.0), zero],
                     value_type="VEC3", interpolation="CUBICSPLINE"),
            _sampler(b, times=[0.0, 0.5, 1.0],
                     values=[(1.0, 1.0, 1.0), (1.0, 1.0, 1.0), (1.0, 1.0, 1.0)],
                     value_type="VEC3"),
        ],
        "channels": [
            {"sampler": 0, "target": {"node": node, "path": "translation"}},
            {"sampler": 1, "target": {"node": node, "path": "scale"}},
        ],
    })

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["animation"] = {
        "animationCount": 1,
        "clipNames": ["Ease"],
        "duration": 1.0,
        "interpolation": "CUBICSPLINE",
        "outputTripletRule": "A CUBICSPLINE output holds [in-tangent, value, out-tangent] per key, "
                             "so its count is 3x the input's and the VALUE is the middle third. A "
                             "reader that strides one element per key returns the tangents.",
        "expectedKeys": [
            {"time": 0.0, "translation": [0.0, 0.0, 0.0]},
            # h01(0.5) = -2(0.125) + 3(0.25) = 0.5, and both tangents are zero.
            {"time": 0.5, "translation": [5.0, 0.0, 0.0]},
            {"time": 1.0, "translation": [10.0, 0.0, 0.0]},
        ],
        "quarterPoint": {
            "time": 0.25,
            "hermite": [1.5625, 0.0, 0.0],
            "linearWouldBe": [2.5, 0.0, 0.0],
            "note": "The midpoint is where smoothstep and lerp agree, so it cannot tell them "
                    "apart. h01(0.25) = -2(0.015625) + 3(0.0625) = 0.15625.",
        },
    }
    return Fixture(
        id="anim-cubicspline", audit_fixture=None, owning_group="animation",
        description="A two-key CUBICSPLINE translation channel with zero tangents, plus a LINEAR "
                    "scale channel keyed at t=0.5 so the spline is actually evaluated at an "
                    "interior time during extraction rather than only at playback.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["CUBICSPLINE interpolation", "in/out tangent triplets", "Hermite basis",
                  "interior resampling"],
        spec_anchors=["animations", "interpolation-cubic"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="EasedTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def anim_two_clips() -> Fixture:
    """Two animations in one file, the second **unnamed**. Owns **GLTF-305**/**GLTF-306**.

    Two animations is the ordinary case -- "Idle" and "Walk" -- and merging them would give one
    clip whose second half is a different animation, which still plays and reads as a badly
    authored loop rather than as an import bug.

    The second animation carries no ``name``, so it becomes ``Clip1``: its **index**, which is the
    only identifier the file itself provides. The first keeps its authored name, which is the
    control -- a generator that renamed everything uniformly would satisfy "the names are
    deterministic" without preserving anything an application can look a clip up by.
    """
    b = GltfBuilder("anim-two-clips")
    mesh, _, _, _ = _carrier(b, mesh_name="DualClipTri")
    node = b.add_node(name="Actor", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    b.add_animation({
        "name": "Walk",
        "samplers": [_sampler(b, times=[0.0, 2.0], values=[(0.0, 0.0, 0.0), (2.0, 0.0, 0.0)],
                              value_type="VEC3")],
        "channels": [{"sampler": 0, "target": {"node": node, "path": "translation"}}],
    })
    # No "name" key at all -- not an empty string, which is a different thing an exporter writes.
    b.add_animation({
        "samplers": [_sampler(b, times=[0.0, 0.5], values=list(_KEY_ROTATIONS), value_type="VEC4")],
        "channels": [{"sampler": 0, "target": {"node": node, "path": "rotation"}}],
    })

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["animation"] = {
        "animationCount": 2,
        "clipNames": ["Walk", "Clip1"],
        "clipDurations": [2.0, 0.5],
        "namingRule": "An unnamed animation becomes 'Clip<index>' -- the only identifier the file "
                      "provides. It must be stable across extractions, because AnimationPlayer and "
                      "every .cnj consumer look a clip up BY NAME: a name that varied between runs "
                      "would make a saved game referencing Clip1 find a different animation.",
        "separationRule": "Two animations are two clips with their own durations, never one clip "
                          "of 2.0s whose second half is the other animation.",
    }
    return Fixture(
        id="anim-two-clips", audit_fixture=None, owning_group="animation",
        description="One file with two animations driving the same node -- a named 'Walk' of 2.0s "
                    "and an unnamed 0.5s one that must become 'Clip1'. Separates 'clips stay "
                    "separate' from 'names survive', and the authored name is the control against "
                    "a generator that renamed everything.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["multiple animations", "unnamed animation", "generated clip name",
                  "per-clip duration"],
        spec_anchors=["animations"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="DualClipTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def anim_repeated_time() -> Fixture:
    """A channel with two samples at the **same** time -- an authored hard cut. Owns **GLTF-313**.

    §3.11 requires sampler input to be strictly increasing, so a repeated time is technically
    malformed, but it is what an exporter emits for an instantaneous jump and the file plays
    correctly: ``FindBracket``'s zero-length span yields the earlier sample, so nothing reads out
    of range. Refusing it would reject working assets, which is why CNA **tolerates and counts**
    equal times while refusing decreasing ones (``bad-animation-input-order``).

    What tolerance costs is stated rather than glossed: a track holds one keyframe per time, so the
    union-with-dedup keeps the value in force *at* the cut (``+5``) and the post-cut value
    (``-5``) is not representable. The clip plays a ramp up and a ramp down where the file
    describes a jump. That is an approximation with a name and a count, not a silent one.
    """
    b = GltfBuilder("anim-repeated-time")
    mesh, _, _, _ = _carrier(b, mesh_name="CutTri")
    node = b.add_node(name="Cut", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    times = [0.0, 1.0, 1.0, 2.0]
    values = [(0.0, 0.0, 0.0), (5.0, 0.0, 0.0), (-5.0, 0.0, 0.0), (0.0, 0.0, 0.0)]
    b.add_animation({
        "name": "HardCut",
        "samplers": [_sampler(b, times=times, values=values, value_type="VEC3")],
        "channels": [{"sampler": 0, "target": {"node": node, "path": "translation"}}],
    })

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["animation"] = {
        "animationCount": 1,
        "clipNames": ["HardCut"],
        "duration": 2.0,
        "authoredTimes": list(times),
        "duplicateAdjacentTimes": 1,
        "policy": "tolerated-and-counted",
        "policyRule": "Equal adjacent input times are kept: FindBracket's zero-length span already "
                      "yields the earlier sample, so nothing reads out of range, and refusing them "
                      "would reject assets that play correctly. A DECREASING step is refused "
                      "instead -- see bad-animation-input-order.",
        "expectedKeys": [
            {"time": 0.0, "translation": [0.0, 0.0, 0.0]},
            {"time": 1.0, "translation": [5.0, 0.0, 0.0]},
            {"time": 2.0, "translation": [0.0, 0.0, 0.0]},
        ],
        "approximation": "Three keys, not four: a track holds one keyframe per time, so the value "
                         "in force AT the cut (+5) survives and the post-cut value (-5) is not "
                         "representable. AnimationReportEXT::duplicateInputTimeCount is what makes "
                         "that visible rather than a silently smoothed jump.",
    }
    return Fixture(
        id="anim-repeated-time", audit_fixture=None, owning_group="animation",
        description="A translation channel with two samples at t=1.0 -- an authored hard cut. "
                    "Tolerated and counted rather than refused, and the fixture states what the "
                    "tolerance costs: the post-cut value is not representable in a resampled "
                    "track, so the jump arrives as a ramp.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["repeated sampler input time", "hard cut", "input monotonicity policy"],
        spec_anchors=["animations"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="CutTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def anim_parent_child() -> Fixture:
    """A parent **and** its child, each driven by its own channel, in one clip.

    One clip with two tracks is the shape a jointed mechanism has -- a turntable carrying an arm --
    and it is where the two halves of the import meet: the tracks must land on the right *bones*
    (the scene-node indices of two different nodes) and the poses must **compose**, parent before
    child, or the arm swings in world space instead of with the table.

    The child carries the mesh and the parent does not, so a reader that placed the geometry by the
    animated node alone -- rather than by the composed chain -- would put it in the wrong place at
    every time but ``t = 0``.
    """
    b = GltfBuilder("anim-parent-child")
    mesh, _, _, _ = _carrier(b, mesh_name="ArmTri")
    child = b.add_node(name="Arm", mesh=mesh, translation=[2.0, 0.0, 0.0])
    parent = b.add_node(name="Turntable", children=[child])
    b.add_scene([parent], name="Scene")
    b.set_default_scene(0)

    b.add_animation({
        "name": "Rotate",
        "samplers": [
            _sampler(b, times=list(_KEY_TIMES), values=list(_KEY_ROTATIONS), value_type="VEC4"),
            _sampler(b, times=list(_KEY_TIMES),
                     values=[(2.0, 0.0, 0.0), (2.0, 0.0, 3.0)], value_type="VEC3"),
        ],
        "channels": [
            {"sampler": 0, "target": {"node": parent, "path": "rotation"}},
            {"sampler": 1, "target": {"node": child, "path": "translation"}},
        ],
    })

    poses = []
    for i, t in enumerate(_KEY_TIMES):
        parent_local = mat_from_trs(None, _KEY_ROTATIONS[i], None)
        child_local = mat_from_trs([(2.0, 0.0, 0.0), (2.0, 0.0, 3.0)][i], None, None)
        world = mat_mul(parent_local, child_local)
        poses.append({
            "time": t,
            "parentLocalColumnMajor": parent_local,
            "childLocalColumnMajor": child_local,
            "childWorldColumnMajor": world,
            "worldPositions": [transform_point(world, p) for p in TRIANGLE_POSITIONS],
        })

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["animation"] = {
        "animationCount": 1,
        "clipNames": ["Rotate"],
        "duration": _KEY_TIMES[-1],
        "trackCount": 2,
        "animatedNodes": [{"node": parent, "name": "Turntable", "path": "rotation"},
                          {"node": child, "name": "Arm", "path": "translation"}],
        "compositionRule": "The mesh hangs off the CHILD and the parent carries no geometry, so a "
                           "reader that placed it by the animated node alone rather than by the "
                           "composed chain would be wrong everywhere except t=0.",
        "posesAtKeyTimes": poses,
    }
    return Fixture(
        id="anim-parent-child", audit_fixture=None, owning_group="animation",
        description="A rotation channel on a parent and a translation channel on its mesh-bearing "
                    "child, in one animation. Two tracks in one clip, landing on two different "
                    "nodes' bones, whose poses must compose parent-before-child.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["two channels, two nodes", "parent/child composition", "animated hierarchy"],
        spec_anchors=["animations", "nodes-and-hierarchy", "transformations"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="ArmTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def anim_weights_path() -> Fixture:
    """A ``weights`` channel beside a ``rotation`` channel. Owns **GLTF-315**'s unsupported path.

    ``weights`` is a legal fourth animation path (§3.11) and CNA cannot drive it: morph weights are
    applied on the CPU at import (`GLTF-289`), so there is no per-frame weight to key. Skipping it
    is the right answer; skipping it *silently* is not, because the file then imports, plays its
    rotation, and simply never morphs -- which reads as a broken morph target rather than as an
    unimported channel.

    The rotation channel beside it is what makes the assertion sharp: the clip is **not** empty, so
    "the unsupported channel was skipped" cannot be confused with "the animation was dropped".
    """
    b = GltfBuilder("anim-weights-path")
    target_deltas = [(0.0, 0.0, 2.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)]
    target_position = b.add_packed_accessor(usage="morph POSITION delta", values=target_deltas,
                                            accessor_type="VEC3", with_bounds=True)
    mesh, _, _, _ = _carrier(b, mesh_name="MorphedTri",
                             targets=[{"POSITION": target_position}], weights=[0.0])
    node = b.add_node(name="Morphing", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    b.add_animation({
        "name": "SpinAndMorph",
        "samplers": [
            _sampler(b, times=list(_KEY_TIMES), values=list(_KEY_ROTATIONS), value_type="VEC4"),
            # One weight per target per key: SCALAR, count = keys x targets.
            _sampler(b, times=list(_KEY_TIMES), values=[0.0, 1.0], value_type="SCALAR"),
        ],
        "channels": [
            {"sampler": 0, "target": {"node": node, "path": "rotation"}},
            {"sampler": 1, "target": {"node": node, "path": "weights"}},
        ],
    })

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["animation"] = {
        "animationCount": 1,
        "clipNames": ["SpinAndMorph"],
        "duration": _KEY_TIMES[-1],
        "channelCount": 2,
        "importedChannelCount": 1,
        "skippedUnsupportedPathChannels": 1,
        "unsupportedPaths": ["weights"],
        "rule": "'weights' is a legal fourth animation path and CNA cannot drive it: morph weights "
                "are applied on the CPU at import (GLTF-289), so there is no per-frame weight to "
                "key. Skipped -- and REPORTED, because a silent skip leaves a file that plays its "
                "rotation and never morphs, which reads as a broken morph target.",
        "notEmptyRule": "The clip still has one track from the rotation channel, so 'the "
                        "unsupported channel was skipped' is distinguishable from 'the animation "
                        "was dropped'.",
    }
    return Fixture(
        id="anim-weights-path", audit_fixture=None, owning_group="animation",
        description="An animation with a rotation channel CNA imports and a morph-target 'weights' "
                    "channel it cannot drive. The weights channel is skipped and counted; the clip "
                    "survives with its rotation track, so a skip is never mistaken for a drop.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["weights animation path", "unsupported channel path", "morph target",
                  "partial channel import"],
        spec_anchors=["animations", "morph-targets"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="MorphedTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def anim_out_of_scene_target() -> Fixture:
    """A channel targeting a node that is **not in the default scene**. Owns **GLTF-310**.

    glTF scopes an animation to nothing: ``animations`` is a top-level array, and a channel may
    name any node in the file, including one only a non-default scene contains. CNA imports the
    default scene alone (`GLTF-133`), so such a channel drives a bone that does not exist.

    Ignoring it is the only available answer -- there is nothing to apply it to -- so the whole
    task is the *report*. A second channel drives an in-scene node, which is what makes the two
    outcomes separable: one track survives, one channel is skipped, and the file is neither
    rejected nor silently half-imported.
    """
    b = GltfBuilder("anim-out-of-scene-target")
    mesh, _, _, _ = _carrier(b, mesh_name="InSceneTri")
    in_scene = b.add_node(name="InScene", mesh=mesh)
    # Nothing in the default scene reaches this node -- it is a root of scene 1 only.
    off_scene = b.add_node(name="OffScene", translation=[0.0, 0.0, -8.0])
    b.add_scene([in_scene], name="Default")
    b.add_scene([off_scene], name="Other")
    b.set_default_scene(0)

    b.add_animation({
        "name": "SpinBoth",
        "samplers": [
            _sampler(b, times=list(_KEY_TIMES), values=list(_KEY_ROTATIONS), value_type="VEC4"),
            _sampler(b, times=list(_KEY_TIMES),
                     values=[(0.0, 0.0, -8.0), (0.0, 4.0, -8.0)], value_type="VEC3"),
        ],
        "channels": [
            {"sampler": 0, "target": {"node": in_scene, "path": "rotation"}},
            {"sampler": 1, "target": {"node": off_scene, "path": "translation"}},
        ],
    })

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["animation"] = {
        "animationCount": 1,
        "clipNames": ["SpinBoth"],
        "duration": _KEY_TIMES[-1],
        "channelCount": 2,
        "importedChannelCount": 1,
        "skippedOutOfSceneChannels": 1,
        "expectedTrackCount": 1,
        "animatedInSceneNode": {"node": in_scene, "name": "InScene", "path": "rotation"},
        "skippedTarget": {"node": off_scene, "name": "OffScene", "path": "translation",
                          "inScene": 1},
        "rule": "animations is a TOP-LEVEL array scoped to nothing, so a channel may name any node "
                "in the file. CNA imports the default scene alone (GLTF-133), so a channel naming "
                "a node only another scene contains drives a bone that does not exist. Ignored -- "
                "there is nothing else available -- and reported, which is the whole task.",
        "separabilityRule": "The second channel drives an in-scene node, so 'one channel was "
                            "skipped' cannot be confused with 'the animation was dropped' or with "
                            "'the file was rejected'.",
    }
    return Fixture(
        id="anim-out-of-scene-target", audit_fixture=None, owning_group="animation",
        description="A two-channel animation where one channel targets a node in the default scene "
                    "and the other targets a node only the second scene contains. The out-of-scene "
                    "channel is ignored and reported; the in-scene one still produces its track.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["channel targeting a node outside the default scene", "two scenes",
                  "partial channel import", "default scene selection"],
        spec_anchors=["animations", "scenes"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="InSceneTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )



# --- The morph-target family (`GLTF-292`) --------------------------------------------------------
#
# One builder for a family of small, deliberately-differing fixtures. Each states what it authors
# and the blended result its own weights produce, computed here from §3.7.2.2's own equation --
# `final = base + sum(weight_t * delta_t)` -- rather than read back from CNA. That is the point of
# a fixture: the expectation is derived from the specification, and the loader is the thing under
# test.
#
# A tilted base normal is used throughout rather than `(0,0,1)`: a normal delta added to a normal
# that is already the answer proves nothing, which is the mistake `normal-absent` was written to
# stop repeating.
_MORPH_BASE_NORMALS = [(0.6, 0.0, 0.8), (0.6, 0.0, 0.8), (0.6, 0.0, 0.8)]
#: A tangent basis authored so the fixtures that carry tangent deltas can state a handedness that
#: must survive: `w = -1` is the mirrored case, and blending must never touch it (`GLTF-287`).
_MORPH_BASE_TANGENTS = [(1.0, 0.0, 0.0, -1.0)] * 3


def _blend(base, targets, weights):
    """§3.7.2.2's own equation: `final = base + sum(weight_t * delta_t)`, component-wise."""
    out = []
    for v, point in enumerate(base):
        blended = list(point)
        for t, deltas in enumerate(targets):
            if not deltas:
                continue
            for c in range(len(blended)):
                blended[c] += weights[t] * deltas[v][c]
        out.append(tuple(blended))
    return out


def _morph_fixture(*, name, targets, mesh_weights, description, features, docstring,
                   node_weights=None, with_normals=True, with_tangents=False,
                   spec_anchors=("morph-targets", "meshes-overview")):
    """Builds one member of the morph family.

    :param targets: one entry per morph target, each a dict with any of ``POSITION``/``NORMAL``/
        ``TANGENT`` mapped to a per-vertex delta list. A target with no ``POSITION`` is legal and
        is one of the cases this family exists to cover.
    :param mesh_weights: the mesh's own ``weights``.
    :param node_weights: the instancing node's ``weights``, or ``None`` to leave the mesh's in
        force -- §3.7.2.2 gives the node the final say, and "absent" is not "zero".
    """
    b = GltfBuilder(name)
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    attributes = {"POSITION": position}
    if with_normals:
        attributes["NORMAL"] = b.add_packed_accessor(usage="NORMAL", values=_MORPH_BASE_NORMALS,
                                                      accessor_type="VEC3")
    if with_tangents:
        attributes["TANGENT"] = b.add_packed_accessor(usage="TANGENT", values=_MORPH_BASE_TANGENTS,
                                                       accessor_type="VEC4")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)

    target_blocks = []
    for i, target in enumerate(targets):
        block = {}
        for semantic, deltas in target.items():
            accessor_type = "VEC3"
            block[semantic] = b.add_packed_accessor(
                usage=f"morph {semantic} delta {i}", values=deltas, accessor_type=accessor_type,
                with_bounds=(semantic == "POSITION"))
        target_blocks.append(block)

    mesh = b.add_mesh([{
        "attributes": attributes,
        "indices": indices,
        "targets": target_blocks,
        "mode": TRIANGLES,
    }], name=name.replace("-", "") + "Mesh", weights=list(mesh_weights))
    node = (b.add_node(name="MeshNode", mesh=mesh, weights=list(node_weights))
            if node_weights is not None else b.add_node(name="MeshNode", mesh=mesh))
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    effective = list(node_weights if node_weights is not None else mesh_weights)
    position_targets = [t.get("POSITION") for t in targets]
    normal_targets = [t.get("NORMAL") for t in targets]
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["morph"] = {
        "targetCount": len(targets),
        "meshWeights": list(mesh_weights),
        "effectiveWeights": effective,
        "targetsWithoutPositions": sum(1 for t in targets if "POSITION" not in t),
        "targetsWithoutNormals": sum(1 for t in targets if "NORMAL" not in t),
        "targetsWithoutTangents": sum(1 for t in targets if "TANGENT" not in t),
        # The blended pose at the effective weights, from §3.7.2.2's equation rather than from CNA.
        "blendedPositions": [list(p) for p in _blend(TRIANGLE_POSITIONS, position_targets, effective)],
        "blendedNormalsBeforeRenormalisation":
            [list(n) for n in _blend(_MORPH_BASE_NORMALS, normal_targets, effective)]
            if with_normals else [],
        "blendRule": "final = base + sum(weight_t * delta_t). Normals are renormalised afterwards "
                     "because a weighted sum of unit vectors is not unit length; positions are "
                     "not, and tangent handedness (w) is never blended at all.",
    }
    return Fixture(
        id=name, audit_fixture=None, owning_group="animation",
        description=description, builder=b, validated_layers=["L1", "L2", "L3"],
        features=list(features),
        spec_anchors=list(spec_anchors),
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name=name.replace("-", "") + "Mesh", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS,
            normals=_MORPH_BASE_NORMALS if with_normals else None,
            tangents=_MORPH_BASE_TANGENTS if with_tangents else None,
            indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def morph_position_only() -> Fixture:
    """One target carrying only ``POSITION`` deltas -- the simplest morph there is."""
    return _morph_fixture(
        name="morph-position-only",
        targets=[{"POSITION": [(2.0, 0.0, 0.0), (0.0, 3.0, 0.0), (0.0, 0.0, 4.0)]}],
        mesh_weights=[1.0],
        description="One morph target with POSITION deltas only, at full weight. Each vertex moves "
                    "along a different axis, so a delta applied to the wrong vertex is a different "
                    "shape rather than a scaled one.",
        features=["one morph target", "POSITION deltas only"],
        docstring="")


def morph_position_normal() -> Fixture:
    """Position and normal deltas together, with the renormalisation that follows."""
    return _morph_fixture(
        name="morph-position-normal",
        targets=[{"POSITION": [(1.0, 0.0, 0.0)] * 3,
                  "NORMAL": [(-0.6, 0.8, 0.0)] * 3}],
        mesh_weights=[0.5],
        description="A target carrying POSITION and NORMAL deltas at weight 0.5. The blended normal "
                    "is deliberately not unit length before renormalisation, which is what makes "
                    "the renormalisation step observable rather than a no-op.",
        features=["POSITION and NORMAL deltas", "normal renormalisation"],
        docstring="")


def morph_position_normal_tangent() -> Fixture:
    """All three delta semantics, including the one the `.cnj` sidecar drops (`GLTF-289`)."""
    return _morph_fixture(
        name="morph-position-normal-tangent",
        targets=[{"POSITION": [(0.0, 0.0, 1.0)] * 3,
                  "NORMAL": [(-0.6, 0.8, 0.0)] * 3,
                  "TANGENT": [(0.0, 1.0, 0.0)] * 3}],
        mesh_weights=[1.0],
        with_tangents=True,
        description="A target carrying POSITION, NORMAL and TANGENT deltas. This is the fixture "
                    "GLTF-289's open residue is measured on: tangent deltas import correctly on "
                    "the runtime path and are NOT written to the .cnj sidecar, so the offline path "
                    "morphs positions and normals and keeps its rest-pose tangents. The base "
                    "tangent's handedness is -1, which blending must never touch.",
        features=["POSITION, NORMAL and TANGENT deltas", "tangent handedness preserved",
                  "GLTF-289 residue"],
        docstring="")


def morph_two_targets() -> Fixture:
    """Two targets whose contributions must add rather than overwrite."""
    return _morph_fixture(
        name="morph-two-targets",
        targets=[{"POSITION": [(1.0, 0.0, 0.0)] * 3},
                 {"POSITION": [(0.0, 2.0, 0.0)] * 3}],
        mesh_weights=[0.25, 0.75],
        description="Two targets at 0.25 and 0.75, moving along different axes. The weights sum to "
                    "1 but the result is neither target: an implementation that took the last "
                    "target, or the heaviest, produces a point on an axis rather than off both.",
        features=["two morph targets", "weighted accumulation"],
        docstring="")


def morph_eight_targets() -> Fixture:
    """Eight targets at once -- the count a facial rig actually uses."""
    return _morph_fixture(
        name="morph-eight-targets",
        targets=[{"POSITION": [(float(i + 1) * 0.125, 0.0, 0.0)] * 3} for i in range(8)],
        mesh_weights=[0.1, 0.0, 0.2, 0.0, 0.3, 0.0, 0.4, 0.0],
        description="Eight targets with alternating zero weights, each contributing a different "
                    "amount along +X. Half the targets contribute nothing, so an implementation "
                    "that iterated targets instead of weights -- or stopped at the first zero -- "
                    "lands somewhere else entirely.",
        features=["eight morph targets", "interleaved zero weights"],
        docstring="")


def morph_zero_weights() -> Fixture:
    """Every weight zero: the rest pose, which must be the base pose exactly."""
    return _morph_fixture(
        name="morph-zero-weights",
        targets=[{"POSITION": [(5.0, 5.0, 5.0)] * 3}],
        mesh_weights=[0.0],
        description="One target with large deltas and a weight of zero. The blended pose must be "
                    "the base pose EXACTLY -- not approximately -- which is the control for every "
                    "other fixture in this family: if this one moves, the blend is applying "
                    "something it was not asked for.",
        features=["zero weight", "rest pose is exact"],
        docstring="")


def morph_overdriven_weight() -> Fixture:
    """A weight above 1 and one below 0 -- both legal, and both routinely clamped by mistake."""
    return _morph_fixture(
        name="morph-overdriven-weight",
        targets=[{"POSITION": [(1.0, 0.0, 0.0)] * 3},
                 {"POSITION": [(0.0, 1.0, 0.0)] * 3}],
        mesh_weights=[1.75, -0.5],
        description="Weights of 1.75 and -0.5. §3.7.2.2 does not bound morph weights, and an "
                    "animator overdriving a shape past 1 or pulling it below 0 is ordinary "
                    "practice -- so a reader that clamps to [0,1] produces a pose the file did not "
                    "ask for, silently.",
        features=["weight above 1", "negative weight", "no clamping"],
        docstring="")


def morph_normal_only_target() -> Fixture:
    """A target with no ``POSITION`` at all -- legal, and a natural place to index off the end."""
    return _morph_fixture(
        name="morph-normal-only-target",
        targets=[{"NORMAL": [(-0.6, 0.8, 0.0)] * 3}],
        mesh_weights=[1.0],
        description="A target carrying only NORMAL deltas. §3.7.2.2 allows any subset of "
                    "POSITION/NORMAL/TANGENT per target, so the position array must be left "
                    "untouched rather than indexed into an absent delta list -- which is the "
                    "shape of the bug this fixture exists to catch.",
        features=["target without POSITION", "partial target semantics"],
        docstring="")


def morph_mesh_weights_only() -> Fixture:
    """``mesh.weights`` with no ``node.weights`` -- the mesh's own value must survive."""
    return _morph_fixture(
        name="morph-mesh-weights-only",
        targets=[{"POSITION": [(3.0, 0.0, 0.0)] * 3}],
        mesh_weights=[0.5],
        node_weights=None,
        description="The mesh declares weights [0.5] and the instancing node declares none. §3.7.2.2 "
                    "gives the node the final say only when it HAS one: absent is not zero, and an "
                    "importer that defaulted the node's weights to zero would render every such "
                    "mesh at rest.",
        features=["mesh.weights without node.weights", "absent is not zero"],
        docstring="")


def morph_node_weights_zero() -> Fixture:
    """The converse: a node that explicitly says zero must override a non-zero mesh weight."""
    return _morph_fixture(
        name="morph-node-weights-zero",
        targets=[{"POSITION": [(3.0, 0.0, 0.0)] * 3}],
        mesh_weights=[1.0],
        node_weights=[0.0],
        description="The mesh declares [1.0] and the node declares [0.0]. The pair with "
                    "morph-mesh-weights-only is what separates 'the node overrides' from 'the node "
                    "is ignored when it looks like a default' -- an explicit zero is a decision, "
                    "and both fixtures fail if an implementation confuses the two.",
        features=["node.weights overrides mesh.weights", "explicit zero"],
        docstring="")


def morph_asymmetric_deltas() -> Fixture:
    """Per-vertex deltas that differ, so a per-vertex indexing error is visible."""
    return _morph_fixture(
        name="morph-asymmetric-deltas",
        targets=[{"POSITION": [(1.0, 0.0, 0.0), (0.0, 2.0, 0.0), (0.0, 0.0, 3.0)],
                  "NORMAL": [(0.4, 0.0, -0.3), (-0.6, 0.8, 0.0), (0.0, -0.8, 0.6)]}],
        mesh_weights=[1.0],
        description="Every vertex gets a different POSITION and NORMAL delta. Most morph fixtures "
                    "give every vertex the same delta, which is exactly the case a swapped or "
                    "repeated index survives -- this one does not let it.",
        features=["per-vertex distinct deltas", "index-order sensitivity"],
        docstring="")


def morph_no_normals_in_base() -> Fixture:
    """A morphed primitive whose base has no ``NORMAL`` -- the generated basis must still blend."""
    return _morph_fixture(
        name="morph-no-base-normals",
        targets=[{"POSITION": [(0.0, 0.0, 2.0)] * 3}],
        mesh_weights=[1.0],
        with_normals=False,
        description="A morphed primitive authoring no NORMAL at all. GLTF-173 computes one, and "
                    "the morph path then blends against a normal CNA produced rather than one the "
                    "file wrote -- a combination neither feature's own fixtures cover.",
        features=["morph without authored normals", "generated normal basis"],
        docstring="")


FIXTURES = [morph_position_only, morph_position_normal, morph_position_normal_tangent,
            morph_two_targets, morph_eight_targets, morph_zero_weights, morph_overdriven_weight,
            morph_normal_only_target, morph_mesh_weights_only, morph_node_weights_zero,
            morph_asymmetric_deltas, morph_no_normals_in_base,
            anim_rigid_node, anim_nonzero_start, anim_translation_scale, anim_step,
            anim_cubicspline, anim_two_clips, anim_repeated_time, anim_parent_child,
            anim_weights_path, anim_out_of_scene_target, morph_node_weights_override]
