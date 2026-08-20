# SPDX-License-Identifier: MS-PL
"""The corpus registry: which fixtures exist, in which owning group, and what they emit.

`plans/plan_gltf.md` §24.1's ownership model is enforced structurally here: a fixture is defined in
exactly one ``defs`` module, that module's name *is* its owning group, and the distinct-asset count
is the sum of the owning-group counts. Referencing a fixture from another group never re-counts it.
"""

from __future__ import annotations

import hashlib
from typing import Any, Callable

from . import GENERATOR_VERSION, SPEC_PIN
from .defs import (accessors, animation, cameras, component_types, container, draco, lights,
                   materials, normals, robustness, scenes, skinning, textures, topology,
                   transforms)
from .manifest import OPEN_DEFECT_STATUSES, Fixture, dumps

#: The final §24.2 inventory, stated once in executable form.  A generated fixture must already
#: have a place in this inventory; adding an unplanned id fails generation instead of silently
#: moving the campaign's finish line.  Conversely, ``corpus_manifest`` emits every not-yet-built
#: entry as ``missingAssets``, so the distance to GLTF-399 cannot be reconstructed differently by
#: the plan, the continuity note and CI again.
#:
#: Later tasks deliberately changed the plan written in 2025: the topology group gained a morphing
#: strip, cameras and lights became separate groups with two additional discriminating assets, and
#: GLTF-316 established that ten (not eleven) animation fixtures are sufficient.  Together with
#: the 13 morph fixtures housed in the same generator module, plus GLTF-218's deliberately
#: discriminating factor-times-texture material witness, the reconciled target was 145 -- and
#: GLTF-463 added the skinned vertex-coloured metallic-roughness witness that stride 80 exists for,
#: making it 146.
TARGET_ASSET_IDS_BY_GROUP: dict[str, tuple[str, ...]] = {
    "container": (
        "glb-basic",
        "glb-bin-chunk-padding",
        "gltf-external-bin",
        "gltf-data-uri-bin",
        "gltf-external-image",
        "gltf-data-uri-image",
        "gltf-uri-percent-encoded",
        "gltf-required-extension-unsupported",
    ),
    "accessors": (
        "accessor-offset",
        "bufferview-offset",
        "bufferview-stride-tight",
        "interleaved-position-normal",
        "interleaved-pos-nrm-uv",
        "interleaved-mixed-widths",
        "stride-padded",
        "two-primitives-one-buffer",
        "sparse-position",
        "sparse-indices",
        "sparse-interleaved-base",
        "accessor-minmax",
        "mat3-padded",
    ),
    "component-types": (
        "u8-idx",
        "u16-idx",
        "u32-idx",
        "non-indexed-triangles",
        "normalized-u8-color",
        "normalized-u16-color",
        "float-color",
        "normalized-i8-normal",
    ),
    "topology": (
        "mode-points",
        "mode-lines",
        "mode-line-loop",
        "mode-line-strip",
        "mode-triangles",
        "mode-triangle-strip",
        "mode-triangle-strip-morph",
        "mode-triangle-fan",
    ),
    "normals": (
        "normal-absent",
        "normal-quantized",
        "tangent-handedness",
        "tangent-absent-generated",
        "normal-nonuniform-scale",
        "tangent-mirrored",
        # GLTF-464: promoted from inline test documents -- both are §3.7.2 conformance statements.
        "tangent-without-normal",
        "morph-normalless-quad",
    ),
    "transforms": (
        "xf-identity",
        "xf-translation",
        "xf-scale-uniform",
        "xf-scale-nonuniform",
        "xf-rot-x90",
        "xf-rot-y90",
        "xf-rot-z90",
        "xf-trs-order",
        "xf-matrix-node",
        "xf-matrix-vs-trs",
        "xf-parent-child",
        "xf-deep-chain",
        "xf-negative-scale",
        "xf-mirror-child",
        "xf-shared-mesh",
        "xf-transform-only",
        "xf-multi-root",
    ),
    "materials": (
        "mat-default",
        "mat-factor-only-gold",
        "mat-basecolor-factor-times-texture",
        "mat-emissive-factor",
        "mat-emissive-strength",
        "mat-vertex-color-pbr",
        "mat-normal-occlusion-scale",
        "mat-alpha-mask-cutoff",
        "mat-unimplemented-extensions",
        "mat-material-variants",
        "mat-unlit",
        "mat-unlit-vertex-color-alpha",
        "mat-specular-glossiness",
        "mat-authored-tangent",
    ),
    "textures": (
        "tex-reference-checkerboard",
        "uv1-material",
        "uv-out-of-range-clamp",
        "uv-out-of-range-wrap",
        "uv-out-of-range-mirror",
        "sampler-trilinear",
        "tex-texture-transform",
        "texture-transform-per-map",
        "texture-shared-two-samplers",
        "tex-dual-texture-stride",
    ),
    "skinning": (
        "skin-armature-ancestor",
        "skin-mesh-node-transform",
        "skin-plus-static-mesh",
        "skin-unlit",
        "skin-vertex-color",
        "skin-vertex-color-pbr",
        "skin-mesh-node-parent-transform",
        "skin-skeleton-hint",
        "skin-unnormalized",
        "skin-73-joints",
        "skin-eight-influences",
        "skin-two-weighted",
        "skin-four-weighted",
        "skin-no-ibm",
        "skin-nonuniform-joint-scale",
        "skin-parented-joints",
        "skin-ushort-joint-indices",
    ),
    "animation": (
        "morph-position-only",
        "morph-position-normal",
        "morph-position-normal-tangent",
        "morph-two-targets",
        "morph-eight-targets",
        "morph-zero-weights",
        "morph-overdriven-weight",
        "morph-normal-only-target",
        "morph-mesh-weights-only",
        "morph-node-weights-zero",
        "morph-asymmetric-deltas",
        "morph-no-base-normals",
        "anim-rigid-node",
        "anim-nonzero-start",
        "anim-translation-scale",
        "anim-step",
        "anim-cubicspline",
        "anim-two-clips",
        "anim-repeated-time",
        "anim-parent-child",
        "anim-weights-path",
        "anim-out-of-scene-target",
        "morph-node-weights-override",
    ),
    "cameras": (
        "camera-perspective",
        "camera-perspective-infinite",
        "camera-perspective-no-aspect",
        "camera-orthographic",
        "camera-animated-node",
    ),
    "lights": (
        "lights-kinds-and-reach",
        "lights-over-budget",
    ),
    "scenes": (
        "scene-default-selection",
        "scene-two-roots",
        "scene-no-scenes",
    ),
    "draco": (
        "draco-triangle",
        "draco-vs-uncompressed-pair",
        "draco-skinned",
        "draco-morph",
    ),
    "robustness": (
        "bad-accessor-out-of-bounds",
        "bad-accessor-count-overflow",
        "bad-index-out-of-range",
        "bad-matrix-and-trs",
        "accessor-count-mismatch",
        "skin-joint-index-out-of-range",
        "skin-joint-index-padding",
        "bad-animation-input-order",
    ),
}

#: Owning groups in a fixed order, so the emitted manifest is deterministic. The order follows
#: the oracle ladder -- container and accessor concerns first, then semantics, then composition.
_GROUP_MODULES: list[tuple[str, Any]] = [
    ("container", container),
    ("accessors", accessors),
    ("component-types", component_types),
    ("topology", topology),
    ("normals", normals),
    ("transforms", transforms),
    ("materials", materials),
    ("textures", textures),
    ("skinning", skinning),
    ("animation", animation),
    ("cameras", cameras),
    ("lights", lights),
    ("scenes", scenes),
    ("draco", draco),
    ("robustness", robustness),
]


def all_fixtures() -> list[Fixture]:
    """Builds every fixture, in a stable group-then-definition order."""
    built: list[Fixture] = []
    seen: dict[str, str] = {}
    for group, module in _GROUP_MODULES:
        factories: list[Callable[[], Fixture]] = module.FIXTURES
        for factory in factories:
            fixture = factory()
            if fixture.owning_group != group:
                raise ValueError(
                    f"{fixture.id}: declared owningGroup {fixture.owning_group!r} but is defined "
                    f"in the {group!r} module -- a fixture's owning group is where it lives")
            if fixture.id in seen:
                raise ValueError(
                    f"duplicate fixture id {fixture.id!r} (already owned by {seen[fixture.id]!r}) "
                    "-- one asset has exactly one canonical identity")
            seen[fixture.id] = group
            built.append(fixture)
    return built


def emit(fixtures: list[Fixture]) -> dict[str, bytes]:
    """Renders the whole corpus to an in-memory ``relative path -> bytes`` map.

    Writing and checking both go through this, so ``--check`` cannot drift from ``--out``.
    """
    files: dict[str, bytes] = {}

    def add(path: str, data: bytes, owner: str) -> None:
        if not path or path in (".", "..") or "/" in path or "\\" in path:
            raise ValueError(
                f"{owner}: emitted path {path!r} is not a flat corpus filename")
        if path in files:
            raise ValueError(
                f"{owner}: emitted path {path!r} collides with another fixture output")
        files[path] = data

    for fixture in fixtures:
        emission = fixture.gltf_emission
        add(f"{fixture.id}.gltf", fixture.builder.to_gltf_text(
            buffer_uri=emission.buffer_uri if emission is not None else None,
            image_uri_overrides=(emission.image_uri_overrides
                                 if emission is not None else None)).encode("utf-8"), fixture.id)
        add(f"{fixture.id}.glb", fixture.builder.to_glb_bytes(), fixture.id)
        add(f"{fixture.id}.expected.json", dumps(fixture.expectation()).encode("utf-8"), fixture.id)
        if emission is not None:
            for path, data in sorted(emission.sidecars.items()):
                add(path, data, fixture.id)
        # The L5 goldens (GLTF-007): the vertex/index bytes CNA must hand to the GPU layer, as
        # files rather than as JSON, because a byte comparison is the whole point of the layer.
        for path, data in fixture.l5_expectation()[1].items():
            add(path, data, fixture.id)
    files["manifest.json"] = dumps(corpus_manifest(fixtures, files)).encode("utf-8")
    return files


def corpus_manifest(fixtures: list[Fixture], files: dict[str, bytes]) -> dict[str, Any]:
    """The corpus-level inventory (`GLTF-003`), including the machine-readable asset count."""
    group_counts: dict[str, int] = {group: 0 for group in TARGET_ASSET_IDS_BY_GROUP}
    for group, _ in _GROUP_MODULES:
        group_counts[group] = sum(1 for f in fixtures if f.owning_group == group)

    target_owner: dict[str, str] = {}
    for group, ids in TARGET_ASSET_IDS_BY_GROUP.items():
        for fixture_id in ids:
            if fixture_id in target_owner:
                raise ValueError(
                    f"target fixture {fixture_id!r} is listed by both {target_owner[fixture_id]!r} "
                    f"and {group!r} -- one canonical id has exactly one owning group")
            target_owner[fixture_id] = group

    generated_ids = {fixture.id for fixture in fixtures}
    for fixture in fixtures:
        expected_owner = target_owner.get(fixture.id)
        if expected_owner is None:
            raise ValueError(
                f"{fixture.id}: generated but absent from the final GLTF-399 target inventory -- "
                "add it deliberately instead of silently moving the finish line")
        if expected_owner != fixture.owning_group:
            raise ValueError(
                f"{fixture.id}: target inventory assigns {expected_owner!r}, generated fixture "
                f"declares {fixture.owning_group!r}")

    target_counts = {group: len(ids) for group, ids in TARGET_ASSET_IDS_BY_GROUP.items()}
    missing_assets = [
        {"id": fixture_id, "owningGroup": group}
        for group, ids in TARGET_ASSET_IDS_BY_GROUP.items()
        for fixture_id in ids
        if fixture_id not in generated_ids
    ]

    promoted = {f.audit_fixture: f.id for f in fixtures if f.audit_fixture}
    ordered_promoted = {k: promoted[k] for k in sorted(promoted, key=lambda s: int(s[1:]))}

    defects: dict[str, dict[str, Any]] = {}
    for fixture in fixtures:
        for defect in fixture.defects:
            record = defects.setdefault(defect.id, {
                "id": defect.id,
                "summary": defect.summary,
                "owner": defect.owner,
                "owningTasks": list(defect.owning_tasks),
                "closedTasks": list(defect.closed_tasks),
                "remainingTasks": list(defect.remaining_tasks),
                "firstDivergentLayer": defect.first_divergent_layer,
                "status": defect.status,
                "fixtures": [],
            })
            # One defect reproduced by several fixtures (D5 owns two) must tell the same story in
            # each of them, or the ledger's own status would depend on iteration order.
            if record["status"] != defect.status or record["closedTasks"] != list(defect.closed_tasks):
                raise ValueError(
                    f"{defect.id}: fixtures disagree about its remediation state "
                    f"({fixture.id} says {defect.status!r}/{defect.closed_tasks}, an earlier "
                    f"fixture said {record['status']!r}/{record['closedTasks']})")
            record["fixtures"].append(fixture.id)
            if defect.divergent_by_layer():
                record["_divergentSomewhere"] = True

    for record in defects.values():
        divergent = record.pop("_divergentSomewhere", False)
        # A partially-remediated defect that no longer bites ANY of its fixtures is fixed, and
        # leaving it open would suppress nothing while still demanding a known-defect test.
        # Per-fixture the rule is looser (see Defect.record): a defect spanning several fixtures
        # may be resolved on some and not others, which is exactly what "partial" means.
        if record["status"] in OPEN_DEFECT_STATUSES and not divergent:
            raise ValueError(
                f"{record['id']}: status is {record['status']!r} but no fixture declares a "
                "divergent field any more -- mark it 'fixed' and delete its known-defect test")

    return {
        "generator": {
            "tool": "tools/gltf_fixtures",
            "version": GENERATOR_VERSION,
            "task": "GLTF-003",
            "regenerate": "PYTHONPATH=tools python3 -m gltf_fixtures --out tests/assets/gltf",
        },
        "specPin": dict(SPEC_PIN),
        "distinctAssetCount": len(fixtures),
        "owningGroupCounts": group_counts,
        "targetDistinctAssetCount": len(target_owner),
        "targetOwningGroupCounts": target_counts,
        "missingAssets": missing_assets,
        "promotedAuditFixtures": ordered_promoted,
        "defectLedger": [defects[k] for k in sorted(defects)],
        "assets": [f.inventory() for f in fixtures],
        "files": [
            {"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            for path, data in sorted(files.items())
        ],
    }
