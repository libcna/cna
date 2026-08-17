# SPDX-License-Identifier: MS-PL
"""Camera fixtures -- owning group ``cameras`` (plan_gltf.md §24.2).

``cgltf_camera`` had **zero occurrences** in CNA: a file's cameras were dropped entirely, so an
asset that had been framed by its author arrived with no framing at all.

Each fixture states its expected projection matrix in the manifest, computed from the specification's
own formulas rather than from CNA -- so a wrong mapping is a numeric mismatch against the
specification, not against another reading of the same code.

Specification: §3.10 ``cameras``, §3.5.3 ``transformations``.
"""

from __future__ import annotations

import math

from ..builder import FLOAT, TRIANGLES, UNSIGNED_SHORT, GltfBuilder
from ..manifest import Fixture, l3_primitive, world_positions
from .common import TRIANGLE_INDICES, TRIANGLE_NORMALS, TRIANGLE_POSITIONS

_SPEC = ["cameras", "transformations"]

#: A field of view, aspect, and clip range with no round numbers in the resulting matrix, so a
#: transposed or half-applied term cannot coincide with the right answer.
_YFOV = 0.7853981633974483          # 45 degrees, exactly representable as pi/4
_ASPECT = 1.6
_ZNEAR = 0.25
_ZFAR = 120.0

#: The camera node's placement. Off every axis so a dropped component is visible.
_CAMERA_TRANSLATION = [2.0, 3.0, 9.0]


def _carrier(builder: GltfBuilder, mesh_name: str) -> int:
    """A triangle for the camera to look at, so the scene is not camera-only."""
    position = builder.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                           accessor_type="VEC3", with_bounds=True)
    normal = builder.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS,
                                         accessor_type="VEC3")
    indices = builder.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                          accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    return builder.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name=mesh_name)


def _perspective_matrix(yfov: float, aspect: float, znear: float, zfar: float | None) -> list[float]:
    """XNA's right-handed perspective projection, column-major.

    ``zfar = None`` is glTF's infinite case (§3.10.3), which is the finite matrix with its two z
    terms at their limits as ``zfar -> infinity``: ``M33 -> -1`` and ``M43 -> -znear``. Written as
    the limits rather than as a large finite zfar, because a large zfar perturbs *every* depth
    value, not only the far ones.
    """
    y_scale = 1.0 / math.tan(yfov * 0.5)
    x_scale = y_scale / aspect
    if zfar is None:
        m33, m43 = -1.0, -znear
    else:
        m33 = zfar / (znear - zfar)
        m43 = (znear * zfar) / (znear - zfar)
    # Column-major: XNA's row-major M11..M44 transposed.
    return [x_scale, 0.0, 0.0, 0.0,
            0.0, y_scale, 0.0, 0.0,
            0.0, 0.0, m33, -1.0,
            0.0, 0.0, m43, 0.0]


def _orthographic_matrix(xmag: float, ymag: float, znear: float, zfar: float) -> list[float]:
    """XNA's right-handed orthographic projection over the FULL extents, column-major.

    §3.10.2's ``xmag``/``ymag`` are HALF extents, so the volume is twice each -- the classic silent
    factor of two, stated here so the manifest can catch it.
    """
    width = xmag * 2.0
    height = ymag * 2.0
    return [2.0 / width, 0.0, 0.0, 0.0,
            0.0, 2.0 / height, 0.0, 0.0,
            0.0, 0.0, 1.0 / (znear - zfar), 0.0,
            0.0, 0.0, znear / (znear - zfar), 1.0]


def _camera_fixture(*, fixture_id: str, camera: dict, projection: list[float],
                    is_perspective: bool, infinite_far: bool, description: str,
                    features: list[str]) -> Fixture:
    b = GltfBuilder(fixture_id)
    mesh = _carrier(b, "Subject")
    camera_index = b.add_camera(camera)
    mesh_node = b.add_node(name="Subject", mesh=mesh)
    camera_node = b.add_node(name="CameraNode", camera=camera_index,
                             translation=_CAMERA_TRANSLATION)
    b.add_scene([mesh_node, camera_node], name="Scene")
    b.set_default_scene(0)

    # The camera node's world transform, column-major. The VIEW matrix is its inverse (§3.10.1 /
    # GLTF-321): a glTF camera looks down its own -Z with +Y up, which is XNA's own convention, so
    # no basis change is involved -- and stating that here means a fixture would catch one being
    # introduced.
    world = [1.0, 0.0, 0.0, 0.0,
             0.0, 1.0, 0.0, 0.0,
             0.0, 0.0, 1.0, 0.0,
             _CAMERA_TRANSLATION[0], _CAMERA_TRANSLATION[1], _CAMERA_TRANSLATION[2], 1.0]
    view = [1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            -_CAMERA_TRANSLATION[0], -_CAMERA_TRANSLATION[1], -_CAMERA_TRANSLATION[2], 1.0]

    # plan_gltf.md GLTF-322. §3.10.3 makes aspectRatio optional and means "the viewport's" when it
    # is absent -- something an importer cannot know. The decision is stated here in both forms:
    # what was authored, and what the importer must therefore assume. A consumer that cannot tell
    # the two apart either stretches a deliberately framed shot or letterboxes a deliberately
    # viewport-relative one, and both look like a projection bug rather than a missing field.
    source = camera.get("perspective", camera.get("orthographic", {}))
    authored_aspect = "aspectRatio" in source
    entry = {
        "camera": camera_index,
        "node": camera_node,
        "nodeName": "CameraNode",
        "isPerspective": is_perspective,
        "hasInfiniteFarPlane": infinite_far,
        "projectionColumnMajor": projection,
    }
    if is_perspective:
        entry.update({
            "hasAuthoredAspectRatio": authored_aspect,
            "aspectRatio": source["aspectRatio"] if authored_aspect else 1.0,
            "aspectRatioRule": "An absent aspectRatio means 'use the viewport's' (§3.10.3), which "
                               "an importer has no viewport to read. 1 is assumed AND the "
                               "assumption is recorded, so a consumer can rebuild the projection "
                               "at its own aspect instead of guessing whether it should.",
            "fieldOfView": source["yfov"],
            "nearPlaneDistance": source["znear"],
            "farPlaneDistance": source.get("zfar", 0.0),
            "rebuildRule": "yfov/znear/zfar are carried rather than left to be recovered from the "
                           "projection: inverting a matrix to get back a value the file stated "
                           "outright is work no consumer should do, and is not possible at all "
                           "for the infinite variant without knowing it is the infinite variant.",
        })

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["cameras"] = {
        "count": 1,
        "entries": [{
            **entry,
            "worldTransformColumnMajor": world,
            "viewMatrixColumnMajor": view,
            "viewRule": "view = inverse(worldTransform(cameraNode)). A glTF camera looks down its "
                        "own -Z with +Y up, which is XNA's own convention, so no basis change is "
                        "applied -- stated here so introducing one would fail.",
        }],
    }
    return Fixture(
        id=fixture_id, audit_fixture=None, owning_group="cameras",
        description=description, builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=features, spec_anchors=_SPEC,
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="Subject", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def camera_perspective() -> Fixture:
    """A perspective camera with every optional field present. Owns **GLTF-318**."""
    return _camera_fixture(
        fixture_id="camera-perspective",
        camera={"name": "MainCam", "type": "perspective",
                "perspective": {"aspectRatio": _ASPECT, "yfov": _YFOV,
                                "zfar": _ZFAR, "znear": _ZNEAR}},
        projection=_perspective_matrix(_YFOV, _ASPECT, _ZNEAR, _ZFAR),
        is_perspective=True, infinite_far=False,
        description="A perspective camera declaring aspectRatio, yfov, znear and zfar, on a node "
                    "translated off every axis. The projection and the node placement are stated "
                    "separately in the manifest because glTF keeps them separate.",
        features=["camera.perspective", "aspectRatio declared", "zfar declared",
                  "camera node transform"])


def camera_perspective_infinite() -> Fixture:
    """A perspective camera with no ``zfar`` -- an infinite far plane. Owns **GLTF-319**.

    XNA has no overload for this, and substituting a large finite ``zfar`` is *not* equivalent: it
    changes every depth value rather than only the ones near the horizon. The manifest states the
    limit matrix, so an implementation that reached for a big number instead produces a numeric
    mismatch rather than something that merely looks similar.
    """
    return _camera_fixture(
        fixture_id="camera-perspective-infinite",
        camera={"name": "InfiniteCam", "type": "perspective",
                "perspective": {"aspectRatio": _ASPECT, "yfov": _YFOV, "znear": _ZNEAR}},
        projection=_perspective_matrix(_YFOV, _ASPECT, _ZNEAR, None),
        is_perspective=True, infinite_far=True,
        description="A perspective camera omitting zfar, which §3.10.3 makes an infinite far "
                    "plane. The expected matrix is the finite one's limit -- M33 = -1 and "
                    "M43 = -znear -- not a large finite zfar, which would perturb every depth.",
        features=["camera.perspective", "zfar absent", "infinite far plane"])


def camera_orthographic() -> Fixture:
    """An orthographic camera. Owns **GLTF-320**.

    ``xmag``/``ymag`` are HALF extents, so the volume is twice each. Authored deliberately
    non-square (3 by 1.5) so halving or doubling the wrong axis is visible, and so a mapping that
    used the magnitudes directly as the width and height is off by exactly two.
    """
    xmag, ymag = 3.0, 1.5
    return _camera_fixture(
        fixture_id="camera-orthographic",
        camera={"name": "OrthoCam", "type": "orthographic",
                "orthographic": {"xmag": xmag, "ymag": ymag, "zfar": _ZFAR, "znear": _ZNEAR}},
        projection=_orthographic_matrix(xmag, ymag, _ZNEAR, _ZFAR),
        is_perspective=False, infinite_far=False,
        description="An orthographic camera with xmag 3 and ymag 1.5. Those are half extents, so "
                    "the projection covers 6 by 3 -- a mapping that used them directly is off by "
                    "exactly a factor of two, and the non-square authoring makes a swapped axis "
                    "visible too.",
        features=["camera.orthographic", "xmag/ymag half extents"])


def camera_animated_node() -> Fixture:
    """A camera whose node is animated. Owns **GLTF-296**.

    §3.11 animates *nodes*, and a camera node is an ordinary node -- so a channel targeting one is
    exactly the rigid animation ``GLTF-293`` imports, with no camera-specific handling anywhere.
    This fixture proves that rather than assuming it, and pins the consequence that is easy to get
    wrong: ``ModelCameraEXT::WorldTransform`` is the pose **at import time**, and the camera's live
    placement is its bone's absolute transform. A consumer that read the stored matrix every frame
    would render an animated camera as a stationary one.

    The node starts at the identity rotation and turns a quarter turn about +Y by ``t = 1``, so the
    two poses are exactly computable and visibly different.
    """
    b = GltfBuilder("camera-animated-node")
    mesh = _carrier(b, "Subject")
    camera_index = b.add_camera({"name": "MovingCam", "type": "perspective",
                                 "perspective": {"aspectRatio": _ASPECT, "yfov": _YFOV,
                                                 "zfar": _ZFAR, "znear": _ZNEAR}})
    mesh_node = b.add_node(name="Subject", mesh=mesh)
    # Authored with TRS rather than 'matrix': §3.5.3 forbids 'matrix' on an animated node.
    camera_node = b.add_node(name="CameraNode", camera=camera_index,
                             translation=_CAMERA_TRANSLATION, rotation=[0.0, 0.0, 0.0, 1.0])
    b.add_scene([mesh_node, camera_node], name="Scene")
    b.set_default_scene(0)

    half_sqrt2 = math.sqrt(0.5)
    key_times = [0.0, 1.0]
    key_rotations = [(0.0, 0.0, 0.0, 1.0), (0.0, half_sqrt2, 0.0, half_sqrt2)]
    times = b.add_packed_accessor(usage="animation input (time)", values=key_times,
                                  accessor_type="SCALAR", component_type=FLOAT, with_bounds=True)
    rotations = b.add_packed_accessor(usage="animation output (rotation)", values=key_rotations,
                                      accessor_type="VEC4", component_type=FLOAT)
    b.add_animation({
        "name": "CameraSpin",
        "samplers": [{"input": times, "output": rotations, "interpolation": "LINEAR"}],
        "channels": [{"sampler": 0, "target": {"node": camera_node, "path": "rotation"}}],
    })

    world_at_rest = [1.0, 0.0, 0.0, 0.0,
                     0.0, 1.0, 0.0, 0.0,
                     0.0, 0.0, 1.0, 0.0,
                     _CAMERA_TRANSLATION[0], _CAMERA_TRANSLATION[1], _CAMERA_TRANSLATION[2], 1.0]
    # A quarter turn about +Y sends +X to -Z and +Z to +X, with the translation unchanged.
    world_at_end = [0.0, 0.0, -1.0, 0.0,
                    0.0, 1.0, 0.0, 0.0,
                    1.0, 0.0, 0.0, 0.0,
                    _CAMERA_TRANSLATION[0], _CAMERA_TRANSLATION[1], _CAMERA_TRANSLATION[2], 1.0]

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["cameras"] = {
        "count": 1,
        "entries": [{
            "camera": camera_index,
            "node": camera_node,
            "nodeName": "CameraNode",
            "isPerspective": True,
            "hasInfiniteFarPlane": False,
            "projectionColumnMajor": _perspective_matrix(_YFOV, _ASPECT, _ZNEAR, _ZFAR),
            "worldTransformColumnMajor": world_at_rest,
            "viewMatrixColumnMajor": [1.0, 0.0, 0.0, 0.0,
                                      0.0, 1.0, 0.0, 0.0,
                                      0.0, 0.0, 1.0, 0.0,
                                      -_CAMERA_TRANSLATION[0], -_CAMERA_TRANSLATION[1],
                                      -_CAMERA_TRANSLATION[2], 1.0],
            "animated": True,
            "clipName": "CameraSpin",
            "worldTransformAtEndColumnMajor": world_at_end,
            "liveTransformRule": "ModelCameraEXT::WorldTransform is the pose AT IMPORT TIME. The "
                                 "camera's live placement is the absolute transform of the bone "
                                 "SceneNodeIndex names, so a consumer that read the stored matrix "
                                 "every frame would render an animated camera as a stationary one.",
        }],
    }
    l4["animation"] = {
        "animationCount": 1,
        "clipNames": ["CameraSpin"],
        "duration": key_times[-1],
        "targetIsCameraNode": True,
        "channels": [{
            "animation": 0,
            "channel": 0,
            "targetNode": camera_node,
            "targetNodeName": "CameraNode",
            "path": "rotation",
            "interpolation": "LINEAR",
            "targetsSkinJoint": False,
            "times": list(key_times),
            "values": [list(q) for q in key_rotations],
        }],
    }
    return Fixture(
        id="camera-animated-node", audit_fixture=None, owning_group="cameras",
        description="A camera node driven by a LINEAR rotation channel through a quarter turn "
                    "about +Y. A camera node is an ordinary node, so this needs no camera-specific "
                    "animation path -- and the fixture pins the consequence: the camera's stored "
                    "WorldTransform is its import-time pose, not its live one.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["animation targeting a camera node", "rotation path", "no skin", "camera"],
        spec_anchors=_SPEC + ["animations"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="Subject", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def camera_perspective_no_aspect() -> Fixture:
    """A perspective camera with **no** ``aspectRatio``. Owns **GLTF-322**.

    ``aspectRatio`` is the one camera field whose absence means "ask the runtime", and an importer
    has no runtime to ask. It assumes 1 -- and the whole task is that it *records* having assumed,
    because the alternative is a consumer that cannot distinguish an author who framed a square
    shot from one who deliberately left the framing to the viewport, and would either stretch the
    first or letterbox the second.

    Every other parameter matches ``camera-perspective`` exactly, so the two differ in precisely
    one field and the expected projections differ in precisely the terms that depend on it: the
    ``x`` scale changes and nothing else does.
    """
    return _camera_fixture(
        fixture_id="camera-perspective-no-aspect",
        camera={"name": "ViewportCam", "type": "perspective",
                "perspective": {"yfov": _YFOV, "zfar": _ZFAR, "znear": _ZNEAR}},
        projection=_perspective_matrix(_YFOV, 1.0, _ZNEAR, _ZFAR),
        is_perspective=True, infinite_far=False,
        description="A perspective camera omitting aspectRatio, which §3.10.3 makes 'use the "
                    "viewport's'. The importer assumes 1 and records that it assumed, so a "
                    "consumer can rebuild the projection at its real aspect. Identical to "
                    "camera-perspective in every other field.",
        features=["camera.perspective", "aspectRatio absent", "viewport-relative framing",
                  "assumed value recorded"])


FIXTURES = [camera_perspective, camera_perspective_infinite, camera_perspective_no_aspect,
            camera_orthographic, camera_animated_node]
