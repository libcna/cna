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
PYTHONPATH=tools python3 -m gltf_fixtures --reference-pins            # validate/print external pins
```

or from `tools/` without the `PYTHONPATH` prefix (`cd tools && python3 -m gltf_fixtures --list`).

Python 3.11+, standard library only. No third-party dependency, and none may be added: the corpus
must be regenerable in any environment that can run the build.

## Output

Into `tests/assets/gltf/`, three core files per fixture, any generated external-resource/L5
sidecars, plus one corpus manifest:

| File | Contents |
|---|---|
| `<id>.gltf` | the text-first asset; buffers/images are base64 `data:` URIs by default, with generated external sidecars only for the named source-form fixtures |
| `<id>.glb` | the same asset in the binary container, from the same source of truth |
| `<id>.expected.json` | the inventory record and the spec-derived expectations for every layer |
| `<id>.*.bin` / `<id>.*.png` | an external buffer/image or an L5 golden, generated from the fixture's authored bytes |
| `manifest.json` | the corpus inventory: distinct-asset count, per-group counts, defect ledger, and a SHA-256 for every emitted file |

The corpus is **committed**, not generated at test time. Fixtures are review artefacts, and
`CnaTests` must not need a Python interpreter. `GltfFixtureCorpusTests.cpp` re-verifies every
committed file against the digest recorded in `manifest.json`, so a hand-edit or a stale file fails
the build rather than quietly changing what the suite means.

## Optional Khronos references

`reference-pins.json` is the machine-readable source of truth for `GLTF-013`, `GLTF-014`, and
`GLTF-016`. These are fetch-on-demand development references, never generated-corpus inputs, CI
dependencies, or CNA runtime dependencies. In particular, no Khronos model is committed here.

The pinned Asset Generator revision has root manifests with 28 groups and 219 permutations. An
explicitly downloaded checkout can be projected onto CNA's canonical fixture identities with:

```bash
PYTHONPATH=tools python3 -m gltf_fixtures --asset-generator-map \
  /path/to/glTF-Asset-Generator/Output/Positive/Manifest.json \
  /path/to/glTF-Asset-Generator/Output/Negative/Manifest.json
```

The command reads every upstream `fileName`, rejects a missing/new group or changed group id, and
emits all 219 records with their closest CNA fixtures. `relationship: "overlap"` means semantic
overlap, not byte equivalence or a claim that CNA has run that third-party file. A `gap` is kept
visible rather than mapped approximately. The command performs no network access.

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
re-counts it. The current distinct-asset total is the sum of the owning-group counts. The final
GLTF-399 IDs live in `TARGET_ASSET_IDS_BY_GROUP`; `corpus.py` rejects an unplanned generated ID and
emits every target ID not built yet under `missingAssets`, so current + missing equals the target
per group and globally.

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

For a named external-source fixture, attach a `GltfEmission` to the `Fixture`. External URI
spellings are percent-decoded and checked one-for-one against its flat `sidecars` map; generation
fails on a missing, unreferenced, nested or colliding sidecar instead of committing a broken asset.

## Regenerating the L5 goldens

`plan_gltf.md` **GLTF-149**/**GLTF-167**. The `<id>.vb.bin` / `<id>.ib.bin` sidecars are the
byte-exact vertex and index buffers CNA must produce, packed by `l5.py` from the manifest's
independent L3 values plus its explicit `importPolicy` transformations. For example, a strip's
indices are expanded and a non-topological skin's authored `JOINTS_0` indices are replaced by the
stated `paletteIndex` values; neither transformation is guessed from CNA. They are regenerated by
the ordinary command — there is no separate golden step:

```bash
PYTHONPATH=tools python3 -m gltf_fixtures --out tests/assets/gltf
```

Two properties make that diff reviewable rather than a wall of binary:

* **The generator never reads CNA.** `l5.py` packs from the manifest's own L3 streams and named
  import-policy results, so a golden can disagree with the importer — which is the entire point of
  having one. A golden regenerated because "the test failed" is a golden that proves nothing.
* **A mismatch is reported by field, not by offset.** `GltfBufferOracleEXT`'s `BufferDifference`
  maps the first differing byte through the canonical stride table and names the attribute
  (`"Normal, vertex 7, component 1"`), so a one-byte change points at what moved.

Which strides the corpus covers is itself checked: `select_stride` mirrors `ExtractMesh`'s own
selection rule and **raises** rather than guessing when it meets a shape it does not model, so a
new fixture that would need an unmodelled rule fails generation instead of emitting a golden nobody
verified.

All seven strides the glTF importer can emit have goldens: 20 (`tex-dual-texture-stride`), 24,
32 (`mat-unlit`), 48, 52 (`skin-unlit`), 56 (`skin-vertex-color`) and 68.

A stride-20 golden needed a *textured* fixture, and the corpus had no image support at all until
`png.py` (`GLTF-190`) — a PNG encoder written against `zlib` and `struct`, because the generator
may not take a dependency. Stride 20 is the only stride a texture combination decides: a non-PBR
material carrying both a base-colour and an occlusion map selects `DualTextureEffect`'s layout,
and every other map is sampled by whatever effect the material model already chose.

When a golden legitimately changes — a fixed packing defect, a deliberate layout change — the diff
to review is the `.expected.json` block, which states the stride, the field offsets and the vertex
count in text. The `.bin` files follow from it.

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
