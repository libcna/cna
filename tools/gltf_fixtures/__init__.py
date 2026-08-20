# SPDX-License-Identifier: MS-PL
"""Deterministic glTF 2.0 conformance-fixture generator for CNA (``plans/plan_gltf.md`` GLTF-003).

One source of truth per fixture emits **both** the asset and its expectation manifest, so a
fixture and the values it is checked against cannot drift apart.

Every expectation in a generated manifest is derived from the fixture's own authored values and
from the pinned glTF 2.0 specification (``docs/gltf-conformance.md``). No expectation is ever
derived from what CNA currently produces. Where CNA is known to be wrong, the wrong value is
recorded separately under ``defects[].currentActual`` as dated evidence, never as the expectation.

Run ``python3 -m gltf_fixtures --help`` from the repository root for usage.
"""

from __future__ import annotations

#: Bumped whenever the emitted layout changes in a way a consumer must notice.
GENERATOR_VERSION = 1

#: The glTF 2.0 specification snapshot every expectation in this corpus is resolved against.
#: Kept in lockstep with ``docs/gltf-conformance.md`` §2.1 -- change both together.
SPEC_PIN = {
    "repository": "KhronosGroup/glTF",
    "path": "specification/2.0/Specification.adoc",
    "commit": "2b29723d025a995971726f2989697cdc49b1222a",
    "sha256": "55986799907693d3f51b0a474497852c0d6318b85084811fdc05ff0db4b27967",
    "document": "docs/gltf-conformance.md",
}

__all__ = ["GENERATOR_VERSION", "SPEC_PIN"]
