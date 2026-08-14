# SPDX-License-Identifier: MS-PL
"""Pinned external glTF references and the Asset Generator manifest projection.

The generated CNA corpus remains self-contained. These references are development-only inputs:
the pin file records immutable upstream identities, while this module can read an explicitly
downloaded Khronos Asset Generator manifest and project every upstream permutation onto the
closest CNA synthetic fixtures. It never downloads anything and is not used by CNA at runtime.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Iterable


REFERENCE_PINS_PATH = Path(__file__).with_name("reference-pins.json")


class ReferenceMapError(ValueError):
    """The committed pin map or an explicitly supplied upstream manifest is inconsistent."""


def _read_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as source:
        return json.load(source)


def _is_commit(value: Any) -> bool:
    return (isinstance(value, str) and len(value) == 40
            and all(character in "0123456789abcdef" for character in value))


def load_reference_pins(fixture_ids: Iterable[str]) -> dict[str, Any]:
    """Loads and validates the committed reference pins against the current CNA corpus."""
    document = _read_json(REFERENCE_PINS_PATH)
    if not isinstance(document, dict) or document.get("schemaVersion") != 1:
        raise ReferenceMapError("reference-pins.json must be a schemaVersion 1 object")

    sources = document.get("sources")
    if not isinstance(sources, list) or len(sources) != 3:
        raise ReferenceMapError("reference-pins.json must name exactly three external sources")

    expected_sources = {
        "sample-assets": ("GLTF-013", "https://github.com/KhronosGroup/glTF-Sample-Assets"),
        "asset-generator": ("GLTF-014", "https://github.com/KhronosGroup/glTF-Asset-Generator"),
        "sample-renderer": ("GLTF-016", "https://github.com/KhronosGroup/glTF-Sample-Renderer"),
    }
    seen_sources: set[str] = set()
    for source in sources:
        if not isinstance(source, dict):
            raise ReferenceMapError("each sources[] entry must be an object")
        source_id = source.get("id")
        if source_id not in expected_sources or source_id in seen_sources:
            raise ReferenceMapError(f"unknown or duplicate reference source {source_id!r}")
        seen_sources.add(source_id)
        expected_task, expected_repository = expected_sources[source_id]
        if source.get("task") != expected_task or source.get("repository") != expected_repository:
            raise ReferenceMapError(f"{source_id}: task or repository does not match its pin")
        if not _is_commit(source.get("revision")):
            raise ReferenceMapError(f"{source_id}: revision is not a lowercase 40-hex commit")
        if source.get("runtimeDependency") is not False or source.get("ciDependency") is not False:
            raise ReferenceMapError(f"{source_id}: external references must remain optional")
        license_record = source.get("license")
        if (not isinstance(license_record, dict)
                or not isinstance(license_record.get("summary"), str)
                or not license_record["summary"]):
            raise ReferenceMapError(f"{source_id}: missing licence summary")

    manifest_map = document.get("assetGeneratorManifest")
    if not isinstance(manifest_map, dict):
        raise ReferenceMapError("assetGeneratorManifest must be an object")
    if manifest_map.get("mappingScope") != "group-semantic-overlap":
        raise ReferenceMapError("assetGeneratorManifest.mappingScope is not recognised")
    expected_paths = {
        "positive": "Output/Positive/Manifest.json",
        "negative": "Output/Negative/Manifest.json",
    }
    if manifest_map.get("paths") != expected_paths:
        raise ReferenceMapError("Asset Generator root manifest paths changed")
    expected_digests = {
        "positive": "100ccab87d7f9a072532ccc3f3cd998e234365c03404b080d1fef96db8096330",
        "negative": "6502a9724d1ec90ff6e55ae8db99b1c8185927df14d9f6831e275fe27555ec94",
    }
    if manifest_map.get("sha256") != expected_digests:
        raise ReferenceMapError("Asset Generator root manifest digests changed")

    current_ids = set(fixture_ids)
    mappings = manifest_map.get("groupMappings")
    if not isinstance(mappings, list) or not mappings:
        raise ReferenceMapError("assetGeneratorManifest.groupMappings must be non-empty")
    seen_groups: set[tuple[str, str]] = set()
    seen_upstream_ids: set[int] = set()
    for mapping in mappings:
        if not isinstance(mapping, dict):
            raise ReferenceMapError("each Asset Generator group mapping must be an object")
        suite = mapping.get("suite")
        folder = mapping.get("folder")
        upstream_id = mapping.get("id")
        relationship = mapping.get("relationship")
        mapped_ids = mapping.get("cnaFixtureIds")
        key = (suite, folder)
        if suite not in expected_paths or not isinstance(folder, str) or not folder:
            raise ReferenceMapError(f"invalid Asset Generator group identity {key!r}")
        if key in seen_groups or not isinstance(upstream_id, int) or upstream_id in seen_upstream_ids:
            raise ReferenceMapError(f"duplicate Asset Generator group identity {key!r}")
        seen_groups.add(key)
        seen_upstream_ids.add(upstream_id)
        if relationship not in ("overlap", "gap") or not isinstance(mapped_ids, list):
            raise ReferenceMapError(f"{suite}/{folder}: invalid relationship or fixture list")
        if relationship == "overlap" and not mapped_ids:
            raise ReferenceMapError(f"{suite}/{folder}: overlap must name a CNA fixture")
        if relationship == "gap" and mapped_ids:
            raise ReferenceMapError(f"{suite}/{folder}: a gap cannot claim a CNA fixture")
        for fixture_id in mapped_ids:
            if not isinstance(fixture_id, str) or fixture_id not in current_ids:
                raise ReferenceMapError(
                    f"{suite}/{folder}: unknown CNA fixture id {fixture_id!r}")

    return document


def project_asset_generator(positive_manifest: Path, negative_manifest: Path,
                            fixture_ids: Iterable[str]) -> dict[str, Any]:
    """Returns a machine-readable CNA projection of both pinned upstream root manifests.

    Every upstream model is retained by file name. A mapping describes semantic overlap, not byte
    equivalence and not a claim that CNA passes the third-party file; the two projects use
    deliberately different fixtures and oracles.
    """
    pins = load_reference_pins(fixture_ids)
    manifest_map = pins["assetGeneratorManifest"]
    mappings = {
        (mapping["suite"], mapping["folder"]): mapping
        for mapping in manifest_map["groupMappings"]
    }
    inputs = {
        "positive": positive_manifest,
        "negative": negative_manifest,
    }
    expected_digests = manifest_map["sha256"]

    groups: list[dict[str, Any]] = []
    actual_keys: set[tuple[str, str]] = set()
    upstream_files: set[str] = set()
    permutation_count = 0
    overlap_count = 0
    gap_count = 0
    for suite, path in inputs.items():
        actual_digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual_digest != expected_digests[suite]:
            raise ReferenceMapError(
                f"{path}: SHA-256 {actual_digest} does not match pinned {expected_digests[suite]}")
        upstream = _read_json(path)
        if not isinstance(upstream, list):
            raise ReferenceMapError(f"{path}: root manifest must be a JSON array")
        for group in upstream:
            if not isinstance(group, dict):
                raise ReferenceMapError(f"{path}: each root manifest entry must be an object")
            folder = group.get("folder")
            upstream_id = group.get("id")
            models = group.get("models")
            key = (suite, folder)
            mapping = mappings.get(key)
            if mapping is None:
                raise ReferenceMapError(
                    f"{path}: upstream group {suite}/{folder} has no committed CNA mapping")
            if key in actual_keys:
                raise ReferenceMapError(f"{path}: duplicate upstream group {suite}/{folder}")
            actual_keys.add(key)
            if upstream_id != mapping["id"]:
                raise ReferenceMapError(
                    f"{suite}/{folder}: upstream id {upstream_id!r} != pinned {mapping['id']}")
            if not isinstance(models, list) or not models:
                raise ReferenceMapError(f"{suite}/{folder}: models must be a non-empty array")

            projected_models: list[dict[str, Any]] = []
            for model in models:
                if not isinstance(model, dict) or not isinstance(model.get("fileName"), str):
                    raise ReferenceMapError(f"{suite}/{folder}: model has no fileName")
                file_name = model["fileName"]
                source_path = f"Output/{suite.capitalize()}/{folder}/{file_name}"
                if source_path in upstream_files:
                    raise ReferenceMapError(f"duplicate upstream model path {source_path}")
                upstream_files.add(source_path)
                projected_models.append({
                    "fileName": file_name,
                    "sourcePath": source_path,
                    "loadable": model.get("loadable"),
                })

            relationship = mapping["relationship"]
            if relationship == "overlap":
                overlap_count += len(projected_models)
            else:
                gap_count += len(projected_models)
            permutation_count += len(projected_models)
            groups.append({
                "suite": suite,
                "folder": folder,
                "id": upstream_id,
                "relationship": relationship,
                "cnaFixtureIds": mapping["cnaFixtureIds"],
                "note": mapping["note"],
                "models": projected_models,
            })

    missing = sorted(set(mappings) - actual_keys)
    if missing:
        rendered = ", ".join(f"{suite}/{folder}" for suite, folder in missing)
        raise ReferenceMapError(
            "the supplied manifests do not match the pinned revision; missing groups: " + rendered)

    asset_generator = next(source for source in pins["sources"]
                           if source["id"] == "asset-generator")
    return {
        "schemaVersion": 1,
        "source": {
            "repository": asset_generator["repository"],
            "revision": asset_generator["revision"],
            "manifestPaths": manifest_map["paths"],
            "manifestSha256": manifest_map["sha256"],
        },
        "mappingScope": manifest_map["mappingScope"],
        "summary": {
            "groupCount": len(groups),
            "permutationCount": permutation_count,
            "overlapPermutationCount": overlap_count,
            "gapPermutationCount": gap_count,
        },
        "groups": groups,
    }
