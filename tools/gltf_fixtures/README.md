# `tools/gltf_fixtures` — the CNA glTF 2.0 conformance fixture generator

`plan_gltf.md` **GLTF-003**. Emits every synthetic asset in the glTF conformance corpus **and** its
expectation manifest, from one source of truth, so a fixture and the values it is checked against
cannot drift apart.

Companion documentation: [`docs/gltf-conformance.md`](../../docs/gltf-conformance.md).

## Usage

Run from the repository root:

```bash
PYTHONPATH=tools python3 -m gltf_fixtures --out tests/assets/gltf     # regenerate in place
PYTHONPATH=tools python3 -m gltf_fixtures --check tests/assets/gltf   # verify byte-identical
PYTHONPATH=tools python3 -m gltf_fixtures --list                      # print the manifest, no writes
```

or from `tools/` without the `PYTHONPATH` prefix (`cd tools && python3 -m gltf_fixtures --list`).

Python 3.11+, standard library only. No third-party dependency, and none may be added: the corpus
must be regenerable in any environment that can run the build.

## Output

Into `tests/assets/gltf/`, three files per fixture plus one corpus manifest:

| File | Contents |
|---|---|
| `<id>.gltf` | the asset, JSON with a base64 `data:` URI buffer — text-first and diffable |
| `<id>.glb` | the same asset in the binary container, from the same source of truth |
| `<id>.expected.json` | the inventory record and the spec-derived expectations for every layer |
| `manifest.json` | the corpus inventory: distinct-asset count, per-group counts, defect ledger, and a SHA-256 for every emitted file |

The corpus is **committed**, not generated at test time. Fixtures are review artefacts, and
`CnaTests` must not need a Python interpreter. `GltfFixtureCorpusTests.cpp` re-verifies every
committed file against the digest recorded in `manifest.json`, so a hand-edit or a stale file fails
the build rather than quietly changing what the suite means.

## Rules

**Never hand-edit a generated file.** Change the fixture definition, regenerate, commit both.

**Generation is deterministic.** Regenerating an unchanged tree produces a zero diff. Nothing here
may read the clock, the environment, the filesystem, or any unordered iteration order.

**Expectations are spec-derived, never CNA-derived.** The `l1`/`l2`/`l3`/`l4` blocks are computed
from the fixture's own authored values and the pinned specification. They do not change when CNA is
fixed. Where CNA is known to be wrong, the wrong value appears only under `defects[].currentActual`
as dated evidence — see below.

**One rule per file.** A fixture that exercises two semantics cannot localise a failure. Prefer
adding a fixture over widening one.

**Small.** Every synthetic asset stays well under 8 KB.

**One canonical identity.** An asset is defined in exactly one `defs/` module, and that module's
name is its owning group. Other groups may *reference* it (`referencingGroups`), which never
re-counts it. The distinct-asset total is the sum of the owning-group counts, and `corpus.py`
enforces both properties at generation time.

## Layout

```text
tools/gltf_fixtures/
  __init__.py     generator version and the glTF specification pin
  __main__.py     the --out / --check / --list CLI
  builder.py      GltfBuilder: asset construction, GLB packing, and the L2 expectation records
  manifest.py     fixture/defect records, the L4 world-transform oracle, deterministic JSON
  corpus.py       the registry: which fixtures exist, in which owning group
  defs/           one module per owning group; a fixture lives in exactly one of them
```

## Adding a fixture

1. Add a `def <name>() -> Fixture` to the `defs/` module for its owning group, and append it to
   that module's `FIXTURES` list.
2. State the authored values once. `add_packed_accessor` records the decoded L2 expectation from
   the same values it packs; `world_positions` composes the L4 expectation from the node graph.
3. Give it a canonical id from `plan_gltf.md` §24.2. Do not invent a name for an asset the plan
   already names.
4. Regenerate and commit the generator change together with its output.

## Recording a defect

A fixture that reproduces a proven CNA defect carries a `Defect` record with:

* `first_divergent_layer` — the first oracle layer at which reality diverges;
* `divergent_fields` — exactly which fields of that layer are broken. The conformance tests skip
  these and only these, so a defect confined to one field never suppresses checking of the fields
  that are correct;
* `owning_tasks` — the `GLTF-xxx` tasks that will fix it;
* `current_actual` — **what CNA produces today**, as dated evidence. This is never an expectation.
  `GltfKnownDefectTests.cpp` asserts against it so that a remediation task which changes the
  behaviour fails loudly instead of passing silently.

## Fixing a defect

When the owning remediation task lands, `GltfKnownDefectTests.cpp` starts failing — that is the
signal, not a problem. Then, and only then:

1. set the defect's `status` to `"fixed"` and clear its `divergent_fields`;
2. delete the corresponding test from `GltfKnownDefectTests.cpp`;
3. regenerate and commit.

The fixture and its expected values are **not** touched. If a fix requires changing an expectation,
either the expectation was wrong (investigate and say so explicitly) or the fix is wrong.
