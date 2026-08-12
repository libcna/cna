# SPDX-License-Identifier: MS-PL
"""The corpus registry: which fixtures exist, in which owning group, and what they emit.

`plan_gltf.md` §24.1's ownership model is enforced structurally here: a fixture is defined in
exactly one ``defs`` module, that module's name *is* its owning group, and the distinct-asset count
is the sum of the owning-group counts. Referencing a fixture from another group never re-counts it.
"""

from __future__ import annotations

import hashlib
from typing import Any, Callable

from . import GENERATOR_VERSION, SPEC_PIN
from .defs import (accessors, animation, cameras, component_types, container, lights,
                   materials, normals, robustness, scenes, skinning, textures, topology,
                   transforms)
from .manifest import OPEN_DEFECT_STATUSES, Fixture, dumps

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
    for fixture in fixtures:
        files[f"{fixture.id}.gltf"] = fixture.builder.to_gltf_text().encode("utf-8")
        files[f"{fixture.id}.glb"] = fixture.builder.to_glb_bytes()
        files[f"{fixture.id}.expected.json"] = dumps(fixture.expectation()).encode("utf-8")
        # The L5 goldens (GLTF-007): the vertex/index bytes CNA must hand to the GPU layer, as
        # files rather than as JSON, because a byte comparison is the whole point of the layer.
        files.update(fixture.l5_expectation()[1])
    files["manifest.json"] = dumps(corpus_manifest(fixtures, files)).encode("utf-8")
    return files


def corpus_manifest(fixtures: list[Fixture], files: dict[str, bytes]) -> dict[str, Any]:
    """The corpus-level inventory (`GLTF-003`), including the machine-readable asset count."""
    group_counts: dict[str, int] = {}
    for group, _ in _GROUP_MODULES:
        group_counts[group] = sum(1 for f in fixtures if f.owning_group == group)

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
        "promotedAuditFixtures": ordered_promoted,
        "defectLedger": [defects[k] for k in sorted(defects)],
        "assets": [f.inventory() for f in fixtures],
        "files": [
            {"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            for path, data in sorted(files.items())
        ],
    }
