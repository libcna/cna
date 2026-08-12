# glTF 2.0 conformance — specification pin and oracle harness

Campaign document: [`plan_gltf.md`](../plan_gltf.md).
This file is the campaign's **normative reference anchor** (`GLTF-002`) and the operating manual for
the numerical oracle harness (`GLTF-003` … `GLTF-006`, `GLTF-041`).

It records *where* the specification is, not *what* it says: no part of the Khronos specification is
reproduced here. Every semantic claim made by a `GLTF-xxx` task must be resolvable against the exact
snapshot pinned below.

---

## 1. Baseline reproduction (`GLTF-001`)

The campaign's forensic baseline is `origin/develop` @ `fb3728267e8f2179d43b96357ff372ae712b7e7f`.
The one-command reproduction of the converter every glTF defect was proven through:

```bash
cmake -S . -B cmake-build-debug -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_RENDERER=STUB \
      -DCNA_BUILD_TESTS=OFF \
      -DCNA_BUILD_EXAMPLES=OFF \
      -DCNA_ENABLE_NET=OFF
cmake --build cmake-build-debug --target cna_tool_gltf_to_cnj -j4     # 525 edges, exit 0
./cmake-build-debug/cna_tool_gltf_to_cnj <fixture>.gltf <outDir> <baseName> [unitScale]
```

The conformance **tests** need a second, test-enabled configuration; it is deliberately a separate
build directory so the converter reproduction above stays byte-for-byte the one the audit ran:

```bash
cmake -S . -B cmake-build-tests -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_RENDERER=STUB \
      -DCNA_BUILD_TESTS=ON \
      -DCNA_BUILD_EXAMPLES=OFF \
      -DCNA_ENABLE_NET=OFF
cmake --build cmake-build-tests --target CnaTests -j4
./cmake-build-tests/CnaTests --gtest_filter='Gltf*'                   # run from the repository root
```

Both build directories are gitignored and survive between sessions; never build the glTF campaign in
a session scratchpad (see `CLAUDE.md`, *Build locations & caching*). Maximum parallelism in this
sandbox is `-j4`.

---

## 2. The specification pin (`GLTF-002`)

### 2.1 What is pinned

| Field | Value |
|---|---|
| Specification | glTF™ 2.0 Specification, The Khronos® 3D Formats Working Group |
| Primary source | `KhronosGroup/glTF`, path `specification/2.0/Specification.adoc` |
| **Pinned commit** | **`2b29723d025a995971726f2989697cdc49b1222a`** (branch `main`) |
| **Pinned document digest** | **SHA-256 `55986799907693d3f51b0a474497852c0d6318b85084811fdc05ff0db4b27967`** (149 830 bytes) |
| Pin taken on | 2026-08-11 |
| Specification licence | CC-BY-4.0 (`SPDX-License-Identifier: CC-BY-4.0`, copyright 2013-2021 The Khronos Group Inc.) |
| Published renderings | `https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html` and `…/glTF-2.0.pdf` (Khronos glTF Registry) |

The AsciiDoc source in the Khronos repository is the pinned artefact rather than a registry HTML
snapshot for three reasons: it is the input the published HTML/PDF are generated from, it carries a
stable content digest, and it is retrievable at an exact immutable revision.

### 2.2 Re-verifying the pin

Any later session can prove it is reading the same specification text:

```bash
curl -sS -o /tmp/Specification.adoc \
  https://raw.githubusercontent.com/KhronosGroup/glTF/2b29723d025a995971726f2989697cdc49b1222a/specification/2.0/Specification.adoc
sha256sum /tmp/Specification.adoc
# 55986799907693d3f51b0a474497852c0d6318b85084811fdc05ff0db4b27967
```

If the digest differs, the file fetched is **not** the pinned snapshot and no citation below is
valid against it.

The specification is **not** vendored into this repository, and CNA has no build-time or run-time
dependency on it. It is a reading reference for humans and for task acceptance arguments only.

### 2.3 JSON schema

The normative JSON schemas that generate the specification's *Properties Reference* (section 5) and
*JSON Schema Reference* (Appendix A) live under `specification/2.0/schema/` in the same pinned
commit. Cite them at the same revision:

```text
https://raw.githubusercontent.com/KhronosGroup/glTF/2b29723d025a995971726f2989697cdc49b1222a/specification/2.0/schema/<file>.schema.json
```

### 2.4 Citation style for `GLTF-xxx` tasks

Cite the **anchor id**, optionally with the section number. Anchor ids are stable across
specification revisions and resolve directly as URL fragments in the published HTML
(`…/glTF-2.0.html#nodes-and-hierarchy`); section *numbers* are generated and can shift when a
section is inserted. Where the two disagree, the anchor wins.

### 2.5 Resolved section map (from the pinned snapshot)

Numbers below were computed from the pinned AsciiDoc's own heading nesting; anchors are the ids
declared in that file.

| § | Title | Anchor |
|---|---|---|
| 2.5 | Versioning | `versioning` |
| 2.6 | File Extensions and Media Types | `file-extensions-and-media-types` |
| 2.8 | URIs | `uris` |
| 3.2 | Asset | `asset` |
| 3.4 | Coordinate System and Units | `coordinate-system-and-units` |
| 3.5 | Scenes | `scenes` |
| 3.5.2 | **Nodes and Hierarchy** | `nodes-and-hierarchy` |
| 3.5.3 | **Transformations** (TRS vs `matrix`, `T * R * S`, global = parent × local) | `transformations` |
| 3.6.1 | Buffers and Buffer Views | `buffers-and-buffer-views` |
| 3.6.1.2 | GLB-stored Buffer | `glb-stored-buffer` |
| 3.6.2 | Accessors | `accessors` |
| 3.6.2.2 | Accessor Data Types | `accessor-data-types` |
| 3.6.2.3 | **Sparse Accessors** | `sparse-accessors` |
| 3.6.2.4 | **Data Alignment** (stride, offsets, alignment) | `data-alignment` |
| 3.6.2.5 | Accessors Bounds (`min`/`max`) | `accessors-bounds` |
| 3.7.2 | Meshes | `meshes` |
| 3.7.2.1 | Meshes — Overview (attribute semantics, `primitive.mode`) | `meshes-overview` |
| 3.7.2.2 | **Morph Targets** | `morph-targets` |
| 3.7.3 | **Skins** | `skins` |
| 3.7.3.2 | Joint Hierarchy | `joint-hierarchy` |
| 3.7.3.3 | Skinned Mesh Attributes (the joint-matrix equation) | `skinned-mesh-attributes` |
| 3.7.4 | Instantiation | `instantiation` |
| 3.8.2 | Textures | `textures` |
| 3.8.3 | Images | `images` |
| 3.8.4 | Samplers (filtering, wrapping) | `samplers` |
| 3.9.2 | Metallic-Roughness Material (channel packing, colour space) | `metallic-roughness-material` |
| 3.9.3 | Additional Textures (normal, occlusion, emissive) | `additional-textures` |
| 3.9.4 | Alpha Coverage (`alphaMode`, `alphaCutoff`) | `alpha-coverage` |
| 3.9.5 | Double Sided | `double-sided` |
| 3.9.6 | Default Material | `default-material` |
| 3.9.7 | Point and Line Materials | `point-and-line-materials` |
| 3.10 | Cameras | `cameras` |
| 3.11 | **Animations** | `animations` |
| 3.12 | Specifying Extensions | `specifying-extensions` |
| 4 | GLB File Format Specification | `glb-file-format-specification` |
| 4.4.2 | GLB Header | `binary-header` |
| 4.4.3 | GLB Chunks | `chunks` |
| 5 | Properties Reference (generated from the schemas) | `properties-reference` |
| 7 | Appendix A — JSON Schema Reference | `appendix-a-json-schema-reference` |
| 8 | Appendix B — BRDF Implementation | `appendix-b-brdf-implementation` |
| 9 | Appendix C — Animation Sampler Interpolation Modes | `appendix-c-interpolation` |
| 9.5 | Cubic Spline Interpolation | `interpolation-cubic` |

### 2.6 Corrections to `plan_gltf.md` §7.3

`plan_gltf.md` §7.3 was written before the pin existed and cites four sections by a number that does
not resolve in the pinned snapshot. The anchors below are authoritative; the plan text is **not**
rewritten (it is a planning record), so resolve its citations through this table.

| `plan_gltf.md` §7.3 citation | Resolves in the pinned snapshot to |
|---|---|
| node transforms and `matrix`/TRS mutual exclusivity — "§3.5 (Nodes and Hierarchy)" | §3.5.2 `nodes-and-hierarchy` **and** §3.5.3 `transformations` — the mutual-exclusivity and `T * R * S` rules are in *Transformations*, not *Nodes and Hierarchy* |
| skins and the joint matrix — "§3.8" | §3.7.3 `skins` (§3.7.3.3 `skinned-mesh-attributes` for the joint-matrix equation). §3.8 in this revision is *Texture Data* |
| animation samplers and CUBICSPLINE — "§3.9, Appendix A" | §3.11 `animations` for the samplers; **Appendix C** §9 `appendix-c-interpolation` for the interpolation modes. Appendix A is the JSON Schema Reference |
| metallic-roughness and channel packing — "§3.9.x `material`, Appendix B" | §3.9.2 `metallic-roughness-material`, §3.9.3 `additional-textures`; **Appendix B** §8 `appendix-b-brdf-implementation` |

Every other §7.3 citation (`§3.6.2`, `§3.6.2.3`, `§3.6.2.4`, `§3.7.2`, `§3.7.2.1`, `§3.7.2.2`,
`§3.9.4`, `§3.9.2`, `§3.10`) resolves exactly as written.

### 2.7 Extensions

Extension semantics are **not** in the core specification. Cite the extension's own `README.md` at
the same pinned commit:

```text
https://raw.githubusercontent.com/KhronosGroup/glTF/2b29723d025a995971726f2989697cdc49b1222a/extensions/2.0/Khronos/<EXTENSION_NAME>/README.md
```

### 2.8 What this pin does *not* cover

`GLTF-013` … `GLTF-016` pin the sample assets, asset generator, validator and reference renderer
separately. None of those is pinned yet, and no third-party asset has been introduced — every
fixture in the corpus below is CNA-authored and MS-PL.

---

## 3. The oracle harness

### 3.1 The seven layers

`plan_gltf.md` §7.1 defines the ladder. A task may not be closed at layer *N* until layers 1…*N*−1
pass for its fixture. Implemented so far:

| Layer | What it compares | Status | Owning task |
|---|---|---|---|
| **L1** | parser/container structure (`cgltf_data` counts, default scene, `extensionsRequired`) | implemented | `GLTF-004` |
| **L2** | decoded accessor arrays, before any semantic interpretation | implemented — `DumpAccessorEXT` | `GLTF-005`, `GLTF-041` |
| **L3** | semantic mesh streams in mesh-local space (`MeshOut`, field by field) | implemented — `DumpMeshOutEXT` | `GLTF-005` |
| **L4** | world-space vertex positions after node composition | implemented — `EvaluateWorldPositionsEXT` | `GLTF-006` |
| **L5** | byte-exact generated vertex/index buffers | implemented — `CompareVertexBytesEXT` / `CompareIndexBytesEXT` | `GLTF-007` |
| **L6** | effect parameters actually bound for a draw | implemented — `CaptureDrawParamsEXT` | `GLTF-008` |
| **L7** | rendered pixels vs a golden PNG | not implemented — needs a 3D-capable renderer | `GLTF-009` |

The helpers are **test scope only** — they live under `modules/content/tests/CNA/Internal/GltfImport/`
in namespace `CnaTest::GltfOracle`, are compiled only into `CnaTests`, and are not part of the CNA
public API, of `CNA::Internal::GltfImport`, or of the CNAEXT surface. Nothing in `modules/*/src/`
may call them.

### 3.2 The fixture generator (`GLTF-003`)

`tools/gltf_fixtures/` is a deterministic Python generator. One source of truth per fixture emits the
asset **and** its expectation manifest, so the two cannot drift:

```bash
python3 -m gltf_fixtures --out tests/assets/gltf         # regenerate the corpus in place
python3 -m gltf_fixtures --check tests/assets/gltf       # verify the tree is byte-identical
python3 -m gltf_fixtures --list                          # machine-readable inventory, no writes
```

Run from the repository root with `tools/` on `PYTHONPATH`, or from `tools/` without it
(`cd tools && python3 -m gltf_fixtures --list`). Standard library only; no third-party dependency,
and none may be added.

`CnaTests` never runs the generator. It verifies the committed corpus against the SHA-256 digests
`manifest.json` records for every emitted file, so the byte-identity guarantee holds at test time
without a Python interpreter. `--check` is the developer-side equivalent.

Per fixture the generator emits three files into `tests/assets/gltf/`, plus the L5 goldens for a
fixture CNA can import:

| File | Contents |
|---|---|
| `<id>.gltf` | the asset, JSON with base64 `data:` URI buffers — text-first and diffable |
| `<id>.glb` | the same asset in the binary container, from the same source of truth |
| `<id>.expected.json` | the inventory record and the **spec-derived** expectations for every layer the asset validates |
| `<id>.vb.bin` / `<id>.ib.bin` | the L5 golden vertex and index buffers (§3.7). Part 0 of a fixture uses these names; a second part would use `<id>.p1.vb.bin`, so adding one never renames the first |

`tests/assets/gltf/manifest.json` is the corpus-level inventory: the distinct-asset count, and each
asset's `id`, `owningGroup`, `referencingGroups[]`, `validatedLayers[]` and `features[]` (§24.1 of
the plan). One asset has exactly one canonical id and exactly one owning group however many layers or
phases reference it.

The corpus is **committed**, not generated at test time: fixtures are diffable review artefacts, and
`CnaTests` must not require a Python interpreter. `GltfFixtureCorpusTests.cpp` asserts that the
committed tree matches the manifest; `tools/gltf_fixtures/README.md` documents the regeneration
contract.

### 3.3 Expected truth vs current CNA output

This is the invariant that makes the harness trustworthy while D1–D8 are still unfixed.

Each `<id>.expected.json` keeps three things strictly apart:

* `l1` / `l2` / `l3` / `l4` — **spec-derived expectations**. Never CNA's output. They are computed
  by the generator from the fixture's own authored values and the pinned specification, and they do
  not change when CNA is fixed. (`l5` is the one layer that is not purely spec-derived; see §3.7.)
* `defects[]` — one record per proven defect the fixture exposes, each naming the defect id
  (`D1`…`D8`), the layer it first diverges at, the remediation tasks that own it, and
  `currentActual`: **what CNA produces today**, recorded as evidence.
* `status` per defect — see the lifecycle below.

A known-defect test therefore asserts two things at once:

1. the spec expectation is still **not** met (the defect is still present); and
2. the divergence is **exactly** the recorded one (CNA is broken in the documented way, not a new
   way).

When an owning remediation task lands, assertion 2 fails loudly — with the fixture and the task
named — and the implementer updates the fixture's defect record, **without touching the fixture or
its spec expectation**.

#### The defect lifecycle

| `status` | Meaning | Effect on the suites |
|---|---|---|
| `known-failing` | no owning task has landed; CNA is wrong in exactly the recorded way | the fields in `divergentFieldsByLayer` are skipped by `GltfConformance*`; a `GltfKnownDefect` test asserts the divergence |
| `partially-remediated` | one owning task landed and changed the behaviour; `closedTasks` names it and `remainingTasks` names what is left | same suppression, but the known-defect test now asserts the **new** behaviour |
| `fixed` | every owning task landed | `divergentFieldsByLayer` is empty, so the layer is asserted in full and the record becomes a regression witness. Its known-defect test is deleted |

A defect record is **never deleted**. When a defect is fixed, `currentActual` becomes what CNA
produces today and the measurement it replaced moves to `priorActual`, dated with the baseline
commit it was taken on. `GltfKnownDefectTests.cpp` asserts this bookkeeping in both directions: an
open defect must have a test there, and a remediated one must not.

`divergentFieldsByLayer` is authoritative and is what the conformance suites consult. A defect
usually breaks exactly one layer, but not always: a primitive CNA rejects outright has no semantic
mesh at L3 **and** no world geometry at L4, and a record that declared only the first would leave
the second failing as if it were a conformance regression.

Never weaken an expectation to make the current implementation green. If a fixture contradicts the
forensic audit, stop and investigate the contradiction.

#### Current state

| Defect | Status | Closed by | Remaining |
|---|---|---|---|
| **D1, D2, D3** | **`fixed`** | **`GLTF-103` → `GLTF-113` → `GLTF-114` → `GLTF-115`** | — |
| **D4** | **`fixed`** | **`GLTF-063`** | — |
| **D5** | **`fixed`** | **`GLTF-071`** → **`GLTF-072`** → **`GLTF-073`** / **`GLTF-076`** / **`GLTF-078`** | — |
| **D6** | **`fixed`** | **`GLTF-293`** → **`GLTF-294`** | — |
| **D7** | **`fixed`** | **`GLTF-215`** → `GLTF-216`/`217`/`219`/`221` → `GLTF-228`/`229`/`231` | — |
| **D8** | **`fixed`** | **`GLTF-245` → `GLTF-247` → `GLTF-248` → `GLTF-260`** | — |

**D8 was closed in its own batch, deliberately after the node-transform work rather than with it.**
`GLTF-114` only parked a skinned mesh on the identity root — the conservative half of glTF's rule
that a skinned mesh's own node transform is ignored, which is what stopped it introducing a second,
opposite error on top of D8. The joint ancestry and the `inverse(globalTransform(meshNode))`
cancellation were then resolved on their own fixtures: `skin-armature-ancestor` for the ancestry
above the joint set, and `skin-mesh-node-transform` for the cancellation, which is authored so that
omitting it, applying it once and applying it twice are three numerically distinct outcomes.

Both terms ride on a per-root prefix (`BoneOut::parentWorldPrefix`, `SkinningData::
SkeletonRootPrefix`) rather than being folded into the bind pose. Folding them in would have been
undone the moment an animation clip replaced a root joint's local transform; kept separate, an
animated root joint substitutes only its own local transform exactly like any other bone.

### 3.4 Promoted audit fixtures (`GLTF-004`)

The fourteen throwaway fixtures the forensic audit used (`f1`…`f14`, `plan_gltf.md` §1.1/§1.2) are
promoted to permanent generated fixtures under their canonical corpus names (§24.2):

| Audit | Canonical id | Owning group | Proves |
|---|---|---|---|
| `f1` | `xf-shared-mesh` | Transforms | **D1** node TRS discarded |
| `f2` | `xf-parent-child` | Transforms | **D2** parent→child composition discarded |
| `f13` | `xf-matrix-node` | Transforms | **D3** `node.matrix` discarded |
| `f3` | `sparse-indices` | bufferView / accessor | **D4** sparse *index* accessor decodes to zeros |
| `f4` | `mode-triangle-strip` | Topology | **D5** non-`TRIANGLES` mode reinterpreted |
| `f12` | `mode-points` | Topology | **D5** non-indexed `POINTS` reinterpreted |
| `f7` | `anim-rigid-node` | Animation | **D6** rigid node animation dropped |
| `f8` | `mat-factor-only-gold` | Materials / PBR | **D7** factor-only PBR material lost |
| `f9` | `skin-armature-ancestor` | Skinning | **D8** skin ancestor chain dropped |
| `f5` | `interleaved-position-normal` | bufferView / accessor | ✅ interleaving + both byte offsets |
| `f6` | `sparse-position` | bufferView / accessor | ✅ sparse *attribute* accessor, null base view |
| `f11` | `u8-idx` | Component types | ✅ `UNSIGNED_BYTE` indices |
| `f10` | `normalized-u8-color` | Component types | ✅ normalized `UNSIGNED_BYTE` `COLOR_0` |
| `f14` | `scene-default-selection` | Scenes / cameras / lights | ✅ default-scene selection when `scene != 0` |

`xf-identity` (Transforms) is generated alongside them as the L4 ladder's zero point and as the
generator's own self-test.

### 3.5 What `GLTF-041` locks

The audit proved the **attribute** decode path correct: `UnpackAccessor()` →
`cgltf_accessor_unpack_floats` honours `bufferView.byteOffset`, `accessor.byteOffset`,
`byteStride`/interleaving, `normalized` integer conversion, and sparse *attribute* accessors
including the null-base-`bufferView` case.

`GltfAccessorDecodeLockTests.cpp` locks exactly those behaviours with permanent L2 tests, and
cross-checks the L2 dump against the L3 `MeshOut` that the **production** `ExtractMesh` (and hence
the production `UnpackAccessor`) produced from the same accessors. A rewrite of that path breaks
these tests.

**Do not rewrite this path.** Phase 2 adds fixtures and bounds checks *around* the decoder, never a
replacement decoder. The index path was a different, genuinely broken path; `GLTF-063` replaced it
(§3.7) without touching this one, which is exactly what `GLTF-041` exists to guarantee.

### 3.6 Running the harness

```bash
./cmake-build-tests/CnaTests --gtest_filter='Gltf*'
```

Run from the repository root — the fixtures are opened at `tests/assets/gltf/` relative to the
working directory, which is what CTest is configured to use. The suites are:

| Suite | What it asserts |
|---|---|
| `GltfFixtureCorpus` | the committed corpus matches the generator (per-file SHA-256), the ownership model holds, and every `.glb` twin is a valid container |
| `GltfConformanceL1` … `L5` | the spec-derived expectation, field by field, minus the fields a still-open defect breaks |
| `GltfKnownDefect` | each still-open defect is present in exactly the recorded way, and the remediated ones are no longer asserted here |
| `GltfOracleEXT` | the oracle helpers themselves — stability, round-tripping, agreement with cgltf, and that using them does not alter production output |
| `GltfBufferOracle` | the L5 comparator proving itself: a perturbed byte is reported at the right offset, vertex and field (`GLTF-007`) |
| `GltfAccessorDecodeLock` | the verified-correct attribute decode path (`GLTF-041`) |
| `GltfIndexDecode` | the sparse-safe, bounds-checked index reader and D4's regression witness (`GLTF-063`) |
| `GltfPrimitiveTopology` | the seven-mode classification table, the never-reinterpret policy (`GLTF-071`), and the strip/fan → triangle-list conversion with its winding rule (`GLTF-072`) |
| `GltfContainerValidation` | structural validation, `extensionsRequired` enforcement and the ignored-extension report, and the severity difference between them (`GLTF-021` … `GLTF-024`) |
| `GltfRigidAnimation` | rigid (non-joint) node animation: resolved against the scene graph (`GLTF-293`), serialised, read back and posed (`GLTF-294`), and the absolute-timeline rules for a clip whose first key is not at 0 (`GLTF-299`) |
| `GltfMaterialState` | every authored material property reaching the effect, and glTF's defaults when none is declared (`GLTF-215` … `GLTF-231`) |
| `GltfDrawTopology` | each primitive mode reaching its `ModelMeshPart` as a real `PrimitiveType` with a §12.3 count (`GLTF-073`/`GLTF-078`) |
| `GltfLightingPolicy` | the default-lighting fallback for a file that declares no light (`GLTF-215`) |
| `GltfConformanceL6` / `GltfDrawParamsOracleL6` | the parameter block a draw binds: world/view/projection, the normal matrix, the material factors, the bone palette and influence count, and the alpha state's carried-vs-applied boundary (`GLTF-008`) |
| `GltfConformanceLadder` | that every `Gltf*` suite belongs to exactly one rung of the `gltf-conformance` label (`GLTF-010`) |

#### The `gltf-conformance` CTest label (`GLTF-010`)

```bash
ctest -L gltf-conformance          # the whole ladder
ctest -L gltf-conformance -R L4    # one rung
```

Each rung is its **own** CTest entry, registered lowest-first, so CTest's own result line names the
divergent layer — `CnaGltfConformanceL4 ... Failed` — without anyone reading a log. Higher rungs
still run after a failure, because whether a wrong world matrix also corrupted the bound effect
parameters is worth knowing in the same run.

| Entry | Covers |
|---|---|
| `CnaGltfConformanceL0` | the corpus and the oracle helpers themselves — if this fails nothing above it means anything |
| `CnaGltfConformanceL1` … `L6` | the six implemented ladder rungs |
| `CnaGltfConformanceLedger` | the defect ledger: every measured defect is either still declared open or closed by a named task |
| `CnaGltfConformanceTool` | the offline `.cnj` converter, the second of the two loaders |

The rung list lives once, in `cmake/UnitTests.cmake` (`CNA_GLTF_CONFORMANCE_RUNGS`).
`GltfConformanceLadder` parses that exact list and asserts the partition is total in both
directions: a new `Gltf*` suite matching no rung fails the run rather than quietly sitting outside
the label, and a rung naming a suite that no longer exists fails rather than quietly running zero
tests.

There is no `L7` entry. It appears when a renderer with a real 3D pipeline is configured; see
§5.3.

---

## 4. The L5 golden buffers (`GLTF-007`)

### 4.1 What an L5 golden is derived from

L5 compares the bytes CNA hands to `VertexBuffer::SetData` / `IndexBuffer::SetData` — that is,
`MeshOut::vertexBytes` and `MeshOut::indexBytes` — against a committed golden. No GPU and no
renderer is involved.

Unlike L1–L4, an L5 golden is **not** purely spec-derived, and the distinction matters:

* the **values** come from the fixture's own spec-derived L3 expectation;
* the **placement** comes from CNA's own vertex stride ABI (`plan_gltf.md` §2.3) — a CNA decision,
  not a Khronos one — including the fill values `ExtractMesh` writes into a slot whose attribute the
  file omits: normal `(0,0,1)`, texture coordinate `(0,0)`, colour alpha `255`.

So a golden asserts *the spec-derived values, laid out the way CNA says it lays them out*. A
deliberate ABI change therefore requires regenerating these goldens, and that regeneration is the
point at which someone has to look at every renderer's `ApplyLayout` and agree.

The layout table is stated twice on purpose — in `tools/gltf_fixtures/l5.py` and in
`GltfBufferOracleEXT.cpp` — and `GltfBufferOracle.TheCppLayoutTableAgreesWithTheOneTheGeneratorPackedWith`
asserts they agree. A table stated once cannot catch its own drift.

### 4.2 Reading a failure

A difference names the fixture, the buffer, the byte, the element and the field:

```text
u8-idx VB differs at byte 45 (vertex 1, Normal +1): expected 0x00, actual 0xFF
```

A size difference is reported at the first byte past the shorter buffer. A byte in inter-field
padding says so rather than naming a field it does not belong to, and an unknown stride says
`<unknown stride layout>` rather than guessing.

### 4.3 Coverage today

21 of the 23 fixtures carry a golden, covering strides 48, 24 and 68, all seven primitive topologies
with their own §12.3 primitive counts, the 16-bit index path and the `vertexCount > 65535`
width-selection rule. The two without one are the fixtures the importer must **refuse**
(`GLTF-021`/`GLTF-023`); their manifests record `l5.supported = false` with a reason and the owning
task, so the layer is visibly absent rather than quietly unasserted.

A converted topology's golden holds the **converted** index list, not the authored one — a strip's
golden is the triangle list `GLTF-072` rewrites it into, and a `LINE_LOOP`'s carries the closing
index `GLTF-076` appends. The `l5` block records both (`sourceTopology` and `topology`) plus the
resulting `primitiveCount`, so the two are never confused, and
`GltfConformanceL5.AConvertedTopologyProducesTheSameBufferAsAnExplicitTriangleList` asserts the
property that justifies converting at all: `mode-triangle-strip` and `mode-triangles` author the
same quad by different routes and must produce byte-identical index buffers, while
`mode-triangle-fan` — the same four indices under the other rule — must not.

The PBR strides (48/68) arrived with `GLTF-215`, and their tangent stream is byte-exact for a
reason worth stating: no corpus fixture authors UVs, and `ComputeTangentsEXT` reads a missing UV as
`(0,0)`, so every triangle's UV determinant is zero, the `|denom| < 1e-12` guard skips it, and every
accumulator stays exactly zero. The orthogonalised tangent therefore falls to its own `(1,0,0)`
fallback, with handedness `+1`. That is arithmetic rather than an approximation, so the golden needs
no reimplementation of the angle-weighted algorithm.

Deliberately still not covered: a primitive that **does** author UVs without a `TANGENT`, where real
angle-weighted generation runs — the packer refuses rather than guessing, and `GLTF-149`+ extends it
together with the fixture that needs it. The dual-texture stride (20) is likewise uncovered, since
no corpus fixture carries a texture at all. The `primitiveCount` assertion covers only `TRIANGLES`,
since that is the only topology that reaches L5 — a strip or fan is already converted to one by then
(`GLTF-072`); `GLTF-078` replaces the loaders' hardcoded `numIndices / 3` with a topology-aware
helper and this is where that replacement is held to the same answer.

---

## 5. The L6 draw-parameter capture (`GLTF-008`)

### 5.1 What L6 measures

L3 says what the importer understood the file to mean. L5 says which bytes it packed. Neither says
whether those facts reach a shader — and that gap is where **D7** lived for the whole of the audit:
`mat-factor-only-gold` decoded perfectly at L3 and still rendered opaque white, because nothing
assigned its factors to an effect.

`CaptureDrawParamsEXT` (`GltfDrawParamsOracleEXT.{hpp,cpp}`, test scope only) closes it. Per drawn
`ModelMeshPart` it records the `GpuDrawParams` block a renderer would receive, having first bound
`absoluteBoneTransform * world`, the view and the projection exactly as `Model::Draw` binds them. It
calls the same virtual `Effect::FillGpuDrawParams()` that `GraphicsDevice::DrawIndexedPrimitives`
calls, on the same effect instance — no renderer, no device draw, no reimplementation of an effect.

What it deliberately does **not** capture is `GraphicsDevice`'s own additions to the block *after*
that call: vertex stream bindings, `vertexStart`, `startIndex`. Those describe the buffers, not the
material, and none of them is a `plan_gltf.md` §21.1 quantity. L5 already owns the buffers, byte for
byte.

### 5.2 The §21.1 contract at L6

| §21.1 row | Where it is asserted | Against what |
|---|---|---|
| World / View / Projection | `BoundWorldMatrixMatchesTheExpectedNodeWorld`, `ViewAndProjectionReachEveryDrawUnaltered` | the manifest's **L4** `worldMatrixColumnMajor`, times the application world |
| Normal matrix | `NormalMatrixIsTheInverseTransposeOfTheWorldUpper3x3`, `NonUniformScaleSeparatesTheNormalMatrixFromTheWorldMatrix` | XNA's own `Matrix::Invert` + `Transpose` |
| Base colour factor | `MaterialFactorsReachTheBoundEffect` | the manifest's **L3** `material.baseColorFactor` |
| Metallic / roughness | `MaterialFactorsReachTheBoundEffect` | L3 `material.metallicFactor` / `roughnessFactor` |
| Emissive | `MaterialFactorsReachTheBoundEffect` | L3 `material.emissiveFactor` |
| MR / occlusion / normal / emissive maps | `APbrDrawYieldsEverySection211QuantityItCanCarry` | *binding only* — which slot is filled. Channel semantics stay L3/L7 |
| Tangent handedness | — | **L5**, in the vertex bytes; not an effect parameter |
| Bone palette | `SkinnedDrawBindsThePaletteAndFourInfluencesPerVertex` | `AnimationPlayer::GetSkinTransforms()`, entry for entry |
| Influences per vertex | `SkinnedDrawBindsThePaletteAndFourInfluencesPerVertex` | CNA always packs four |
| Alpha mode / cutoff | `AlphaStateIsCarriedOnTheEffectButNotYetInTheParameterBlock` | L3 `material.alphaMode` / `alphaCutoff` |
| Double-sided | same | L3 `material.doubleSided` — *carried*, see below |

Every comparison is against a value **another layer already established independently**, never
against a second walk of the same code. That is what makes a green L6 mean "the value survived the
whole trip" rather than "two copies of the same mistake agree".

Two entries in the table are honest boundaries rather than coverage:

* **Tangent handedness** is a vertex-stream fact. It has no effect parameter, so it cannot be an L6
  assertion; L5's byte-exact goldens own it and `GLTF-175` extends it to L7.
* **Alpha state** is *carried, not applied* — `docs/gltf-api-change-review.md` §1.3/§1.4's own
  decision. The capture records both halves: what the effect carries (`BLEND`, cutoff `0.5`,
  double-sided) **and** what the GPU block would apply (`alphaTest` still at its `{0,0,1,1}`
  never-discard default). `AlphaStateIsCarriedOnTheEffectButNotYetInTheParameterBlock` pins that
  gap deliberately, so it cannot be crossed silently in either direction: when `GLTF-230` wires the
  blend and cutoff state, that test fails and has to be updated on purpose.

### 5.3 Why L7 is not implemented

`GLTF-009`'s acceptance is *deterministic PNGs across two runs on `OPENGLES3`*. It is not blocked by
design work; it is blocked by the renderer the conformance suite runs on. `STUB` has no 3D pipeline
at all — that is exactly why the repository's `GraphicsDeviceCapability`, `TextureCube` and
`XnbBuiltInReader` suites already fail there, and why `ModelMesh::Draw`'s own
`Ensure3DSupported()` gate exists. No image can be produced, so no image can be compared, and a
"golden" captured from a renderer that draws nothing would be a golden bug of exactly the kind
`docs/gltf-center-collapse-verdict.md` §5 warns about.

The layers below it are unaffected: L1–L6 are renderer-independent by construction (they read the
file, the importer's output and the effect's own parameter block), which is what `GLTF-017` asserts
directly. When a GL-capable configuration is available, `GLTF-009` adds the L7 rung and
`cmake/UnitTests.cmake` gains a `CnaGltfConformanceL7` entry beside the others — the label's shape
already accommodates it.

---

## 6. The tangent generation algorithm (`GLTF-180`)

§3.7.2.1 says a client "should" compute tangents when a normal-mapped material needs them and the
file authors none. It does not say **how**, and the algorithm decides what the surface looks like,
so CNA's is written down here rather than left to be read out of `ComputeTangentsEXT`.

### What it computes

Per triangle, the tangent is the direction in which **U increases** across the triangle's surface,
derived from the two edge vectors and their UV deltas:

```
E1 = P1 − P0                 dUV1 = UV1 − UV0
E2 = P2 − P0                 dUV2 = UV2 − UV0

r  = 1 / (dUV1.x · dUV2.y − dUV2.x · dUV1.y)
T  = (E1 · dUV2.y − E2 · dUV1.y) · r
B  = (E2 · dUV1.x − E1 · dUV2.x) · r
```

Each triangle's `T` is accumulated onto its three vertices **weighted by the corner's own angle**,
which is what makes a shared vertex's tangent depend on how much of each face actually meets there
rather than on how many faces do. The accumulated tangent is then orthonormalised against the
vertex's normal (Gram-Schmidt) and its handedness taken from the sign of `dot(cross(N, T), B)`.

### Its bound, stated plainly

This is **not MikkTSpace**. MikkTSpace is the de-facto standard most authoring tools bake normal
maps against, and matching it exactly requires its own vertex-splitting and welding rules, which
change the vertex count. The difference shows on surfaces where a normal map was baked against a
tangent basis that splits where CNA's averages — a hard UV seam, most visibly.

Three consequences worth being explicit about:

* **An authored `TANGENT` is always preferred**, and is passed through byte-exact. Generation is
  the fallback, never an override, so a file exported from a MikkTSpace-based tool is unaffected.
* **A degenerate UV triangle** (zero-area in UV space, so `r` would be infinite) contributes
  nothing rather than a NaN, and a vertex left with no usable accumulation falls back to `+X` with
  a `+1` handedness — a valid frame that is simply arbitrary, which is the only honest answer when
  the UVs carry no direction to derive one from.
* **Matching MikkTSpace is `GLTF-179`**, and it is an investigation rather than a defect: the
  generated basis is a valid tangent frame, it is simply not the same one the map was baked
  against. Which of the two a given asset needs is a property of that asset's authoring pipeline.

