# plan_gltf.md — CNA glTF 2.0 Correctness Remediation Campaign

Planning date: **2026-08-11**
Baseline: `origin/develop` @ **`fb3728267e8f2179d43b96357ff372ae712b7e7f`**
(`test(examples): add xvfb screenshot demo for EasyGL and SDL_Renderer`)
Planning branch: `claude/gltf-correctness-audit-plan-rxfs1l`
Oracle repository: `openeggbert/cna-gltf-viewer` @ `aaa008dc62bcb1127901ca23b75b4bf356c0ba66` (`develop`), inspected read-only.

> **THIS DOCUMENT IS A PLAN. NOTHING IN IT WAS IMPLEMENTED.**
> No production source, test, asset, shader, CMake, CNAEXT.md, FUTURE.md or sharp-runtime file was
> modified by the session that produced it. The only tracked change is this file.
> It realises `FUTURE.md` **Phase 5 — glTF correctness campaign**, and supersedes the analysis-only
> `gltfissues.md` (2026-07-28) as the campaign's working document. `gltfissues.md` remains valid
> historical evidence and is **not** rewritten.

---

## 1. Executive Summary

CNA's glTF 2.0 support is **structurally incomplete at the scene level and silently lossy at
several data-model levels**. The visible "models collapsed toward the centre" symptom is not a
single bug; this audit reproduced **eight independent, numerically proven defects** on the current
public `develop`, at least four of which each independently produce collapsed or displaced
geometry.

The single most important finding is that **CNA has no glTF node-transform pipeline at all**.
`CNA::Internal::GltfImport::MeshGroup` stores only `{const cgltf_skin*, std::vector<const
cgltf_mesh*>}` — there is no `cgltf_node*` and no matrix anywhere in the import data model. Every
imported mesh primitive is therefore emitted in **mesh-local coordinates with an identity bone
transform**. In a typical authored asset, each part's placement lives entirely in its node's TRS;
stripping that places every part at the origin, superimposed. *That is literally "collapsed toward
the centre."*

The second independent collapse mechanism is in skinning. `BuildSkeleton` walks parent links **only
within the skin's own joint set**, so any transform on the armature/root node *above* the joints is
dropped from the bind pose — while the file-authored `inverseBindMatrices`, which *do* include that
transform, are kept verbatim. The resulting joint matrix is therefore multiplied by the **inverse**
of the dropped ancestor transform. A fixture proved this exactly: an armature translated `[0,100,0]`
yields a skin transform of `translate(0,-100,0)`. For the very common case of an armature carrying a
uniform scale (a centimetre-authored rig, or a Blender axis-conversion node), the same mechanism
multiplies every skinned vertex by the reciprocal of that scale — collapsing the character toward
the origin.

Three further proven defects are silent data corruption rather than omission:

* an index accessor using `accessor.sparse` decodes to **all-zero indices** (the mesh becomes a
  single degenerate point);
* every glTF primitive `mode` other than `TRIANGLES` is silently reinterpreted as a triangle list;
* factor-only metallic-roughness materials (`baseColorFactor` + `metallicFactor` + `roughnessFactor`
  with no maps) are downgraded to a white `BasicEffect` — `baseColorFactor`, `alphaMode`,
  `alphaCutoff` and `doubleSided` are never carried anywhere.

Against this, the audit also **verified as correct** several things that a superficial reading would
have blamed: interleaved `byteStride` decoding, non-zero `bufferView.byteOffset` and
`accessor.byteOffset`, sparse *attribute* accessors, `UNSIGNED_BYTE`/`UNSIGNED_SHORT` index
component types, normalized `UNSIGNED_BYTE` `COLOR_0` round-tripping, and default-scene selection.
The accessor layer is largely sound because it delegates to `cgltf_accessor_unpack_floats`. **The
damage is above the accessor layer, not inside it.** Remediation must not begin by rewriting
accessor decoding.

`cna-gltf-viewer` was inspected and found **free of compensating workarounds**: it converts through
`cna_tool_gltf_to_cnj`, loads the resulting `.cnj`, frames the camera from the generated vertex
sidecars, and draws with `Matrix::Identity`. It has two genuine viewer-owned defects (a global
`RasterizerState::CullNone`, and no fallback lighting for a light-less PBR scene) and one blocking
staleness problem: it still uses the pre-2026-08-10 `CNA_GRAPHICS_BACKEND` / `EASYGL` /
`cna_backend_graphics_easygl` / `CNA_NOXNA` build vocabulary, which no longer exists on `develop`,
so **it cannot currently configure against the baseline at all**.

The plan defines two milestones — **GLTF CORE 2.0 CORRECT** and **GLTF ROBUST** — a seven-layer
numerical oracle hierarchy, a 135-asset conformance corpus, a 24-phase dependency-ordered backlog of
**460 tasks** (`GLTF-001` … `GLTF-460`) organised into **two execution tracks**, and a 19-task
**P0 center-collapse track** that answers the owner's question before any accessor-breadth,
material, animation or extension work begins.

### 1.1 Proven defect ledger (this audit, on `fb37282`)

Every row was reproduced by running the **real** `cna_tool_gltf_to_cnj` built from the baseline and
decoding its binary output. Fixtures are described in §4.3; they are *planned* corpus assets and
were **not** committed by this session.

| # | Defect | Layer | Proof |
|---|---|---|---|
| D1 | glTF node TRS is discarded for every mesh instance | `GLTF-TRANSFORM` | `f1`: node `translation [10,0,0]` → decoded X bounds `[0,1]`, expected `[0,11]` |
| D2 | Parent→child transform composition is discarded | `GLTF-TRANSFORM` | `f2`: parent `scale 2` × child `translation [0,3,0]` → decoded Y `[0,1]`, expected `[6,8]` |
| D3 | `node.matrix` is discarded | `GLTF-TRANSFORM` | `f13`: `matrix = translate(4,5,6)` → decoded X `[0,1]`, expected `[4,5]` |
| D4 | Sparse **index** accessor decodes to all zeros | `GLTF-ACCESSOR` | `f3`: expected `[0,1,2,0,2,3]`, got `[0,0,0,0,0,0]` |
| D5 | Non-`TRIANGLES` primitive modes silently reinterpreted | `GLTF-MESH` | `f4` (`mode 5`): 4 strip indices → 1 triangle, vertex 3 dropped. `f12` (`mode 0`, non-indexed): 4 points → 1 triangle |
| D6 | Rigid (unskinned) node animation silently dropped | `GLTF-ANIMATION` | `f7`: one rotation channel on a mesh node → no `animations` key in the `.cnj`, no warning |
| D7 | Factor-only PBR material downgraded, all material state lost | `GLTF-MATERIAL` | `f8`: gold `baseColorFactor [1,0.72,0.315,0.5]` + `alphaMode BLEND` + `doubleSided` → `BasicEffect`, stride 32, zero material fields emitted |
| D8 | Skin ancestor chain dropped while IBMs are kept | `GLTF-SKIN` | `f9`: armature `translation [0,100,0]` → `bindPoseLocal` translation `(0,0,0)`, `inverseBindGlobal` translation `(0,-100,0)` ⇒ skin transform `translate(0,-100,0)` |

### 1.2 Verified-correct ledger (do not "fix" these)

| Behaviour | Proof |
|---|---|
| Interleaved `bufferView.byteStride = 24`, `bufferView.byteOffset = 16`, `accessor.byteOffset = 12` | `f5`: positions `(0,0,0) (2,0,0) (0,3,0)` exact |
| Sparse **attribute** accessor with absent base `bufferView` | `f6`: positions `(0,0,0) (5,0,0) (0,7,0)` exact |
| `UNSIGNED_BYTE` index component type | `f11`: indices `[0,1,2]` exact |
| Normalized `UNSIGNED_BYTE` `VEC4` `COLOR_0` | `f10`: `(255,0,0,255) (0,255,0,128) (0,0,255,0)` byte-exact |
| Default-scene selection when `scene != 0` | `f14`: only the `scene: 1` mesh imported; decoy excluded |
| Base64 `data:` URI buffers | all 14 fixtures load |

---

## 2. Current CNA glTF Architecture

### 2.1 Owning files (baseline `fb37282`)

| Concern | File |
|---|---|
| Vendored parser | `third_party/cgltf/cgltf.h` (single header, `CGLTF_IMPLEMENTATION` defined in exactly one TU) |
| Shared import core | `modules/content/include/CNA/Internal/GltfImport/GltfImportCore.hpp` (387 lines) |
| Shared import core impl | `modules/content/src/GltfImport/GltfImportCore.cpp` (1409 lines) |
| Offline CLI | `tools/gltf_to_cnj/gltf_to_cnj.cpp` (650 lines), target `cna_tool_gltf_to_cnj`, registered by `cmake/ToolGltfToCnj.cmake` |
| Runtime reader | `modules/content/src/Xna/ContentManager.cpp` — `ReadGltfModel()` (≈1798–2148) and `ModelTypeReader` (`.cnj` path, ≈2150–2900) |
| Model/bone runtime | `modules/graphics/src/Xna/Model.cpp`, `ModelMesh.cpp`, `ModelBone.cpp` |
| Skinning runtime | `modules/graphics/src/Xna/AnimationPlayer.cpp` (`SkinningData`, `AnimationPlayer`) |
| Morph runtime | `modules/graphics/src/Xna/MorphTargetEXT.cpp` |
| PBR effects | `modules/graphics/src/Xna/PbrEffect.cpp`, `SkinnedPbrEffect.cpp` |
| Reference renderer shaders | `modules/renderers/easygl/src/EasyGLRenderer.cpp` (`EnsurePbrProgram`, `EnsurePbrSkinnedProgram`, `ApplyLayout`) |
| Tests | `modules/content/tests/.../GltfImportCoreTests.cpp` (10), `GltfToCnjToolTests.cpp` (20), `RuntimeGltfModelTests.cpp` (9) |

### 2.2 Pipeline diagram — actual, with proven loss points

```text
  .gltf / .glb  ──────────────────────────────────────────────────────────────────┐
        │                                                                          │
        ▼                                                                          │
  ┌────────────────────────────────────────────────────────────┐                   │
  │ cgltf_parse_file + cgltf_load_buffers                      │  GLTF-CONTAINER   │
  │  · GLB chunks, external URIs, base64 data: URIs            │  GLTF-BUFFER      │
  │  ✗ cgltf_validate() is NEVER called                        │  ← L1 gap         │
  │  ✗ data->extensions_required is NEVER checked              │  ← L1 gap         │
  └────────────────────────────────────────────────────────────┘                   │
        │                                                                          │
        ▼                                                                          │
  ┌────────────────────────────────────────────────────────────┐                   │
  │ accessor decoding                                          │  GLTF-ACCESSOR    │
  │  ✔ attributes: cgltf_accessor_unpack_floats                │                   │
  │      (stride-aware, sparse-aware, normalization-aware)     │  ← VERIFIED       │
  │  ✗ indices:    cgltf_accessor_read_index                   │                   │
  │      returns 0 for sparse OR null bufferView, silently     │  ← D4             │
  └────────────────────────────────────────────────────────────┘                   │
        │                                                                          │
        ▼                                                                          │
  ┌────────────────────────────────────────────────────────────┐                   │
  │ ExtractMesh()                                              │  GLTF-MESH        │
  │  · picks a magic vertex stride 20/24/32/48/52/56/68         │  CNA-GPU-PACKING  │
  │  · bakes KHR_texture_transform into the single UV channel   │                   │
  │  · generates tangents when absent (angle-weighted)          │                   │
  │  ✗ prim.type (mode) is NEVER read                           │  ← D5             │
  │  ✗ baseColorFactor / alphaMode / alphaCutoff /              │                   │
  │    doubleSided / normal scale / occlusion strength: LOST    │  ← D7             │
  │  ✗ sampler state: LOST                                      │                   │
  │  ✗ TEXCOORD_1+ beyond the base-colour set: LOST (warned)    │                   │
  └────────────────────────────────────────────────────────────┘                   │
        │                                                                          │
        ▼                                                                          │
  ┌────────────────────────────────────────────────────────────┐                   │
  │ CollectMeshGroups()                    ★ THE STRUCTURAL HOLE ★                  │
  │  struct MeshGroup { const cgltf_skin* skin;                │  GLTF-TRANSFORM   │
  │                     std::vector<const cgltf_mesh*> meshes; }│                   │
  │  ✗ NO cgltf_node*.  NO matrix.  NO instance identity.       │  ← D1 D2 D3       │
  │  → every primitive is emitted in mesh-local space           │                   │
  └────────────────────────────────────────────────────────────┘                   │
        │                                                                          │
        ├──────────────── offline ────────────────┐   ┌───────── runtime ──────────┤
        ▼                                          │   ▼                            │
  ┌──────────────────────────────┐                 │ ┌──────────────────────────┐   │
  │ gltf_to_cnj                  │                 │ │ ReadGltfModel()          │   │
  │  · one .cnj per mesh GROUP   │                 │ │  · groups.front() ONLY   │   │
  │  · ExtractClips only if skin │ ← D6            │ │  · identity Root bone    │   │
  │  · no transforms in .cnj     │                 │ │  · identity mesh bone    │   │
  └──────────────────────────────┘                 │ └──────────────────────────┘   │
        │                                          │   │                            │
        ▼                                          │   │                            │
  ┌──────────────────────────────┐                 │   │                            │
  │ ModelTypeReader (.cnj)       │                 │   │                            │
  │  · identity Root bone        │                 │   │                            │
  │  · identity per-mesh bone    │                 │   │                            │
  │  · primCount = numIndices/3  │ ← D5 amplifier  │   │                            │
  └──────────────────────────────┘                 │   │                            │
        └───────────────────┬──────────────────────┴───┘                            │
                            ▼                                                       │
  ┌────────────────────────────────────────────────────────────┐                    │
  │ Model / ModelBone / ModelMesh / ModelMeshPart              │  CNA-GPU-PACKING   │
  │  Model::Draw: world = absoluteBoneTransform * world  ✔      │                    │
  │  BuildVertexBufferFromRawBytes → VertexBuffer(stride only)  │                    │
  │  Skin:  AnimationPlayer, skin[i] = IBM[i] * world[i]        │  GLTF-SKIN ← D8    │
  │  Morph: CPU re-blend + full VB re-upload                    │  GLTF-MORPH        │
  └────────────────────────────────────────────────────────────┘                    │
                            │                                                       │
                            ▼                                                       │
  ┌────────────────────────────────────────────────────────────┐                    │
  │ Effect layer: BasicEffect / DualTextureEffect /            │  CNA-EFFECT        │
  │ SkinnedEffect / PbrEffect / SkinnedPbrEffect               │                    │
  │  · 3 directional lights + ambient, all disabled by default  │                    │
  │  · PbrEffect.AmbientLightColor default = (0,0,0)            │                    │
  └────────────────────────────────────────────────────────────┘                    │
                            │                                                       │
                            ▼                                                       │
  ┌────────────────────────────────────────────────────────────┐                    │
  │ IGraphicsRenderer (41 renderer identities)                 │  CNA-RENDERER      │
  │  ApplyLayout() dispatches on MAGIC STRIDE, per renderer     │                    │
  │  no VertexDeclaration reaches the renderer boundary         │                    │
  │  no sRGB decode anywhere in the PBR shader                  │                    │
  └────────────────────────────────────────────────────────────┘                    │
                            │                                                       │
                            ▼                                                       ▼
                         pixels                                          cna-gltf-viewer
```

### 2.3 Vertex stride table (the de-facto CNA/glTF ABI)

There is **no single source of truth** for this table. It is written in `ExtractMesh` and re-read by
a `switch (stride)` in every GPU renderer. This is the single largest structural hazard in the GPU
packing layer.

| Stride | Layout (offsets) | Chosen when | Effect |
|---|---|---|---|
| 20 | Pos 0, UV 12 | unskinned, uncoloured, non-PBR, base-colour **and** occlusion maps | `DualTextureEffect` |
| 24 | Pos 0, Color(u8×4) 12, UV 16 | unskinned + `COLOR_0` | `BasicEffect` + `VertexColorEnabled` |
| 32 | Pos 0, Nrm 12, UV 24 | default unskinned | `BasicEffect` |
| 48 | Pos 0, Nrm 12, Tan(4f) 24, UV 40 | unskinned, uncoloured, has normal **or** MR map | `PbrEffect` |
| 52 | Pos 0, Nrm 12, UV 24, Weights(4f) 32, Indices(u8×4) 48 | skinned | `SkinnedEffect` |
| 56 | stride 52 + Color(u8×4) 52 | skinned + `COLOR_0` | `SkinnedEffect` + `VertexColorEnabled` |
| 68 | Pos 0, Nrm 12, Tan(4f) 24, UV 40, Weights 48, Indices 64 | skinned + PBR | `SkinnedPbrEffect` |

Hard caps implied by this table: **4 influences per vertex**, **≤255 joints** (`BlendIndices` is
`uint8`), **≤72 bones per effect** (`SkinnedEffect::MaxBones`), **one UV channel**, **no second
`JOINTS_1`/`WEIGHTS_1` set**, **no tangent slot outside the PBR strides**.

---

## 3. Current State / Evidence

### 3.1 Documentation reconciliation

`CNAEXT.md` §3.2 is the repository's most prominent public claim about glTF. Measured against the
baseline it is **directionally true but materially incomplete**: everything it lists does exist, but
it omits every defect in §1.1 and reads as a completeness statement.

| Claim (`CNAEXT.md` §3.2) | Verdict | Evidence |
|---|---|---|
| "Runtime path: `Content.Load<Model>("character.glb")` works directly" | **PARTIAL** | works, but imports `groups.front()` **only** — a file with a skinned character *and* static scenery loses one of them |
| "Offline path: `tools/gltf_to_cnj` produces `.cnj` + sidecars" | **CURRENT TRUTH** | verified by execution |
| "Imports: geometry" | **KNOWN BROKEN** | D1–D5: no node transforms, no topology, sparse indices zeroed |
| "PBR materials (4 maps + factors)" | **PARTIAL** | maps + metallic/roughness/emissive only; `baseColorFactor`, `alphaMode`, `alphaCutoff`, `doubleSided`, normal scale, occlusion strength all lost (D7). PBR is only selected when a normal or MR **map** exists |
| "skins/skeleton" | **KNOWN BROKEN** | D8: ancestor chain above the joint set dropped |
| "animation (LINEAR/STEP/CUBICSPLINE Hermite)" | **PARTIAL** | correct *for skin joints*; rigid node animation silently dropped (D6) |
| "morph targets (CPU-blended)" | **PARTIAL** | position deltas correct; **normal deltas are never applied on the PBR strides 48/68** (§16.2); tangent deltas never extracted |
| "tangents (angle-weighted generation when absent)" | **CURRENT TRUTH** (documented non-MikkTSpace divergence) | `ComputeTangentsEXT`; only run when `usePbr` |
| "Draco decode (optional)" | **UNVERIFIED** | present, tested for a triangle; no uncompressed-vs-compressed parity test exists; not built here (`libdraco` absent) |
| "`KHR_texture_transform`" | **PARTIAL** | base-colour texture only, baked into the one shared UV channel |
| "`KHR_lights_punctual`" | **PARTIAL** | ≤3 lights, point/spot approximated as directional aimed at the origin, intensity clamped to `[0,1]` |
| "`KHR_materials_emissive_strength`" | **PARTIAL** | applied only when `usePbr` is true |

`gltfissues.md` (2026-07-28) — **STILL VALID**. All nine of its cited source files still exist; its
four headline findings (discarded node transforms, lost `baseColorFactor` / wrong PBR selection,
black unlit PBR, ignored `KHR_materials_transmission`) all reproduce on the baseline. Its
"Recommended Repair Order" is **NOT IMPLEMENTED**; its 12 proposed regression tests **do not exist**.
Its one stale citation (`EasyGLGraphicsBackend.cpp:4121`, and the file has since moved to
`modules/renderers/easygl/src/EasyGLRenderer.cpp`) is deliberately retained as historical, per
`integration/lanes/gltf.md`.

`plan_cnj.md` Phases 12–14 (`CNB-50`…`CNB-109`) — **HISTORICAL PLAN, LARGELY IMPLEMENTED**. Those
phases built the import core, PBR effects, morph targets, Draco and the three named extensions. They
never scoped node transforms, primitive topology, material factors, samplers or colour space, so
their completion is **not** evidence that glTF is correct. This campaign does not reopen them; it
adds the layers they never covered.

`FUTURE.md` Phase 5 — **THIS CAMPAIGN**. Its audit scope list is a subset of §29's phases; its rule
"Do not assume every `cna-gltf-viewer` visual failure is in the parser" is honoured by §6's taxonomy.

`remediation/REMEDIATION_PROGRESS.md` `REMED-NA-016` — **STILL VALID, OUT OF SCOPE THERE, IN SCOPE
HERE**: a UBSan-confirmed misaligned 4-byte `float` load in `cgltf_component_read_float`
(`third_party/cgltf/cgltf.h`), live under `GltfToCnjToolTest.ResolvesSparseAccessorOverride`. Carried
forward as `GLTF-036`/`GLTF-037`.

`known_bugs.md` — contains **zero** glTF entries. That absence is itself a finding: the defects in
§1.1 were never tracked.

### 3.2 Search sweep results

Repository-wide searches for `glTF|gltf|GLB|Gltf|GLTF|Draco|morph|skin|skeleton|animation|PBR|
metallic|roughness` resolve to exactly the files in §2.1 plus documentation. Notably:

* `cgltf_primitive_type` — **0 occurrences** in production code. Confirms D5.
* `cgltf_validate` — **0 occurrences**. No structural validation is ever performed.
* `extensions_required` — **0 occurrences** in production code (only in test fixture JSON).
* `cgltf_sampler`, `mag_filter`, `wrap_s` — **0 occurrences**. Samplers are never read.
* `cgltf_camera` — **0 occurrences**. glTF cameras are never imported.
* `KHR_materials_variants`, `KHR_materials_transmission`, `KHR_materials_unlit`,
  `KHR_materials_ior`, `KHR_materials_specular`, `KHR_materials_sheen`, `KHR_materials_volume`,
  `KHR_materials_clearcoat`, `KHR_texture_basisu`, `EXT_meshopt_compression`,
  `EXT_mesh_gpu_instancing` — parsed by the vendored cgltf, **0 occurrences** in CNA. All are
  `PARSED_BUT_IGNORED`.

---

## 4. `cna-gltf-viewer` Reproduction

### 4.1 What the viewer is

`openeggbert/cna-gltf-viewer` @ `aaa008d` — 926 lines across 4 sources. It:

1. shells out to `$<TARGET_FILE:cna_tool_gltf_to_cnj>` (path baked in at build time) with
   `<input> <outdir> scene <unitScale>`;
2. scans the output directory for every `.cnj` whose text contains `"type": "Model"`;
3. `ContentManager::Load<Model>()`s each of them;
4. frames an orbit camera by re-reading the generated `*_verts.bin` sidecars and taking the AABB of
   the first 12 bytes of every stride;
5. draws each model with `model.Draw(Matrix::Identity, view, projection)`.

**It therefore exercises the offline `.cnj` path, not the runtime `Load<Model>(".glb")` path.** Both
share `GltfImportCore`, so every defect in §1.1 reaches it, but the two paths differ (the offline
path emits *all* mesh groups; the runtime path emits only the first).

### 4.2 Viewer verdict — oracle, not culprit

**No compensating workarounds exist.** There is no per-asset special-casing, no axis flip, no
rescale-until-it-looks-right, no weight renormalisation, no material substitution. Confirmed by
reading all four sources in full.

Three genuinely viewer-owned items:

| ID | Item | Class |
|---|---|---|
| V1 | Build vocabulary is stale: `CNA_GRAPHICS_BACKEND`, `EASYGL`, `cna_backend_graphics_easygl`, `CNA_NOXNA` — none exist on `develop` since the 2026-08-10 renderer-naming normalisation. **The viewer cannot configure against the baseline.** | `VIEWER`, blocking |
| V2 | Global `RasterizerState::CullNone` in `Draw()`. glTF materials are single-sided unless `doubleSided`. Legitimate as a *debug* default, but it must not be the state under which conformance is judged, and it masks winding-order defects. | `VIEWER` |
| V3 | No fallback lighting. A glTF file without `KHR_lights_punctual` leaves `PbrEffect` at ambient `(0,0,0)` with all three directional lights disabled ⇒ every PBR surface renders **pure black**. | `VIEWER` (presentation) + `CNA-EFFECT` (no `EnableDefaultLighting()` on import) |

V2 and V3 are viewer defects because the viewer owns presentation policy. **They are the only two
viewer-side changes this campaign authorises**, plus V1's mechanical build migration. Everything
else is fixed in CNA.

The viewer's camera framing (step 4) reads the *already-wrong* local-space sidecars, so on a
transform-bearing asset it also computes the wrong pivot and radius. That is **not** a viewer bug —
it is D1 propagating. It will self-correct once D1 is fixed, and must not be patched in the viewer.

### 4.3 Reproduction performed by this audit

The viewer itself could not be run (V1 blocks configuration; there is no display in this
environment). The audit instead reproduced against **the exact converter the viewer invokes**:

```
cmake -S . -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_RENDERER=STUB -DCNA_BUILD_TESTS=OFF -DCNA_BUILD_EXAMPLES=OFF \
      -DCNA_ENABLE_NET=OFF
cmake --build cmake-build-debug --target cna_tool_gltf_to_cnj -j4      # 525/525, exit 0
cna_tool_gltf_to_cnj <fixture>.gltf <outdir> scene                     # exit 0 for all 14
```

Fourteen hand-authored minimal fixtures (`f1`…`f14`, base64 `data:` URI buffers, one semantic rule
each) were converted and their `.cnj` + `*_verts.bin` + `*_idx.bin` + `*.skeleton.bin` outputs
decoded byte-for-byte. Results are §1.1 and §1.2. These fixtures are the seed of the conformance
corpus in §24; **they were not committed by this session.**

### 4.4 Known-failing real-world asset

`ChronographWatch.glb` (7 446 368 bytes, SHA-256
`8e875fcd83efb433afed9ef1c18b2c2b2e075e2bf48371cadfd2a3cf529f1aef`), recorded in `gltfissues.md`.
14 nodes, 13 meshes / 19 primitives, 29 materials, 8 textures, 1 animation, no skins/lights/cameras;
uses `KHR_materials_transmission`, `KHR_materials_variants`, `KHR_texture_transform`. Documented
divergences, still reproducible from the code on the baseline:

| Part | CNA/exported local bounds | Correct world bounds |
|---|---|---|
| Backplate | Y `-0.0710 … -0.0066`, Z `-1.9022 … 1.9834` | Y `-1.9022 … 1.9834`, Z `0.0166 … 0.0810` |
| Hour hand | Y `-0.7620 … -0.7315`, Z `-0.2314 … 0.3759` | Y `-0.2314 … 0.3759`, Z `0.7371 … 0.7676` |
| Minute hand | Y `-0.8000 … -0.7684`, Z `-0.2629 … 0.6765` | Y `-0.2629 … 0.6765`, Z `0.7740 … 0.8056` |

The asset is not in either repository and its licence was not reviewed. It is a **reproduction
input**, not a planned corpus asset (`GLTF-018` resolves its licence before any commit).

### 4.5 Renderer-dependence of the failures

Every defect in §1.1 is observable **in the converter's own output on disk, before any draw call**.
None of them can be renderer-specific. The renderer-differential question (§22) is therefore about
*additional* divergence layered on top, not about the reported symptom.

---

## 5. Center-Collapse Hypotheses

The owner's symptom — "geometry deformed / collapsed toward the centre" — has **four proven
sufficient causes** in CNA today. The critical path (§28) determines which one applies to a given
failing asset, in ~4 hours, without touching materials or animation.

### 5.1 Ranked hypotheses

| Rank | Hypothesis | Status | Mechanism | Signature that identifies it |
|---|---|---|---|---|
| **H1** | Node transforms discarded | 🐛 **CONFIRMED** (D1–D3) | every part emitted in mesh-local space with an identity bone ⇒ all parts superimposed at the origin | multi-part asset; each part *individually* correct in shape but all concentric; decoded AABB equals the union of the per-mesh local AABBs |
| **H2** | Skin ancestor chain dropped, IBMs kept | 🐛 **CONFIRMED** (D8) | `skin[i] = IBM[i] · world[i]` where `world[i]` omits everything above the joint set ⇒ vertices multiplied by `inverse(ancestorTransform)`; a scaled armature scales the mesh by `1/s` | skinned asset only; whole character uniformly shrunk/enlarged/rotated/offset by a constant; unskinned parts of the same file are unaffected |
| **H3** | Sparse index accessor zeroed | 🐛 **CONFIRMED** (D4) | all indices become 0 ⇒ every triangle is `(0,0,0)` ⇒ the mesh renders as nothing, or as a degenerate sliver at vertex 0 | affected primitive vanishes entirely or becomes a single point; `*_idx.bin` is all-zero |
| **H4** | Non-`TRIANGLES` topology reinterpreted | 🐛 **CONFIRMED** (D5) | a strip/fan index run decoded as a triangle list produces a chaotic tangle of long thin triangles spanning the mesh, visually "imploded" | asset uses `mode != 4`; silhouette is a spider-web of thin triangles; `indexCount % 3 != 0` truncation drops the tail |
| **H5** | Runtime path imports only `groups.front()` | 🐛 **CONFIRMED** (code) | `ReadGltfModel` takes `groups.front()`; a mixed skinned + static file silently loses one whole group | runtime `Load<Model>(".glb")` only; the viewer's offline path is unaffected |
| **H6** | `MaxBones = 72` / `BlendIndices` `uint8` overflow | 🔬 **INVESTIGATE** | a rig with >72 joints silently truncates the palette; >255 joints wraps the index byte ⇒ vertices snap to arbitrary joints, dragging them toward whichever joint index they alias to | skinned asset with a large rig; localised limbs collapsing to unrelated joints |
| **H7** | Morph default weights applied with stale PBR normals | 🔬 **INVESTIGATE** | `mesh.weights` non-zero at import is applied via `SetMorphWeightsEXT`; for strides 48/68 normal deltas are skipped (§16.2) | morphed PBR asset; shape right, shading wrong — *not* a collapse |
| **H8** | `unitScale` misuse | ✅ **VERIFIED not a cause** | scales positions **and** bone/IBM translations consistently; default 1.0; the viewer passes it through unchanged from `--scale` | — |
| **H9** | Accessor stride / offset / component-type misdecoding | ✅ **VERIFIED not a cause** | delegated to `cgltf_accessor_unpack_floats`, proven exact on `f5`/`f6`/`f10`/`f11` | — |
| **H10** | Matrix row/column-major confusion in `ConvertGltfMatrix` | ✅ **VERIFIED correct as written** | copies basis vectors directly, producing the XNA row-vector transpose of the glTF column-vector matrix; consistent with `Matrix::CreateTranslation` and `Model::Draw` | but it is **unreached** for node matrices today, because no node matrix is ever read (D3) |
| **H11** | Quaternion component order | ✅ **VERIFIED correct** | glTF `(x,y,z,w)` → `Quaternion(X,Y,Z,W)` in `ReadQuatSample`; matches XNA's own member order | — |
| **H12** | Weights not normalised | 🔬 **INVESTIGATE** | glTF requires normalised weights but does not guarantee them; CNA never renormalises. Non-normalised weights summing to <1 shrink the vertex toward the origin — *a genuine collapse mechanism* | skinned asset from a non-conforming exporter; `sum(WEIGHTS_0) != 1` in the decoded vertex buffer |
| **H13** | Renderer vertex-layout mismatch | 🔬 **INVESTIGATE** | `ApplyLayout` dispatches on magic stride; a renderer missing a stride case falls back to **position-only**, leaving normals/UVs/weights unbound (garbage or zero) ⇒ skinned meshes collapse to weight 0 | renderer-specific; disappears on another renderer; `CNA_RENDER_LOG("ApplyLayout: unknown stride=…")` fires |

### 5.2 Cause/evidence/reproducer/test matrix (owner's checklist, answered)

| Candidate cause from the brief | Location | Verdict | Reproducer | Test to add |
|---|---|---|---|---|
| wrong accessor byte offset | `UnpackAccessor` → cgltf | ✅ correct | `f5` | `GLTF-046` |
| wrong bufferView byte offset | cgltf `cgltf_buffer_view_data` | ✅ correct | `f5` | `GLTF-045` |
| wrong byteStride | cgltf `accessor->stride` | ✅ correct | `f5` | `GLTF-047` |
| wrong component size / type | `cgltf_component_read_float` | ✅ correct | `f10`,`f11` | `GLTF-050`…`GLTF-055` |
| normalized integer misread | `cgltf_component_read_float(normalized)` | ✅ correct | `f10` | `GLTF-056` |
| float reinterpretation / alignment | `cgltf.h:2250` | 🔬 UBSan-confirmed misalignment, no observed fault | `REMED-NA-016` | `GLTF-036` |
| incorrect accessor count | `posAcc->count` used for every stream | 🐛 **unvalidated** — a NORMAL accessor with a different `count` is read out of range | none yet | `GLTF-060` |
| incorrect VEC2/3/4 layout | `UnpackAccessor` component check | ✅ correct (throws on mismatch) | — | `GLTF-049` |
| interleaved attribute decoding | cgltf | ✅ correct | `f5` | `GLTF-047` |
| index corruption | `cgltf_accessor_read_index` | 🐛 **CONFIRMED** (D4) | `f3` | `GLTF-063`…`GLTF-066` |
| signed/unsigned conversion | `AppendUint16(static_cast<uint16_t>(v))` | 🐛 **unguarded truncation** when `vertexCount ≤ 65535` but an index value exceeds it | none yet | `GLTF-068` |
| vertex declaration mismatch | magic-stride `ApplyLayout` | 🔬 INVESTIGATE | H13 | `GLTF-155`…`GLTF-160` |
| POSITION stream overwritten | `ExtractMesh` writes Position first, always at offset 0 | ✅ correct | — | `GLTF-150` |
| wrong primitive base offset | `ModelMeshPart(vb, ib, numVertices, primCount, 0, 0)` | ✅ correct (always 0/0, one part per primitive) | — | `GLTF-152` |
| incorrect min/max handling | `accessor.min`/`max` never read | ⚪ not used — no correctness impact, but no validation either | — | `GLTF-061` |
| transform matrix order | `Model::Draw`: `bone * world`; `AnimationPlayer`: `local * parentWorld` | ✅ correct XNA row-vector order | — | `GLTF-104` |
| row-major vs column-major | `ConvertGltfMatrix` | ✅ correct, but unreached (D3) | `f13` | `GLTF-107` |
| parent/child multiplication order | `CopyAbsoluteBoneTransformsTo` | ✅ correct | — | `GLTF-112` |
| duplicate node transform application | n/a — applied **zero** times | 🐛 **CONFIRMED** (D1) | `f1` | `GLTF-113` |
| missing node transform | `CollectMeshGroups` | 🐛 **CONFIRMED** (D1–D3) | `f1`,`f2`,`f13` | `GLTF-103`…`GLTF-125` |
| wrong scale | `ScaleTranslation` | ✅ correct | — | `GLTF-121` |
| coordinate handedness conversion | none performed | ✅ correct — glTF and XNA are both right-handed, +Y up, −Z forward; **no conversion is required** | — | `GLTF-105` |
| quaternion component/order | `ReadQuatSample` | ✅ correct | — | `GLTF-108` |
| quaternion normalisation | CUBICSPLINE renormalises; LINEAR uses `Slerp` | ✅ correct | — | `GLTF-298` |
| skin weights not normalised | never renormalised | 🔬 INVESTIGATE (H12) | none yet | `GLTF-256` |
| wrong `JOINTS_0` decoding | `(int)(joints[i]+0.5f)` then `oldToNew` remap | ✅ correct for ≤255 joints; 🐛 silent `uint8` truncation above | none yet | `GLTF-254` |
| wrong `WEIGHTS_0` decoding | `unpackSemantic(..., 4, ...)` | ✅ correct | `f9` | `GLTF-255` |
| wrong inverse bind matrices | `ConvertGltfMatrix` + `ScaleTranslation` | ✅ correct **as read**; wrong **as used** (D8) | `f9` | `GLTF-246` |
| wrong joint matrix multiplication order | `IBM * world` | ✅ correct XNA order; **missing the mesh-node inverse term** | `f9` | `GLTF-247` |
| skinning applied to unskinned meshes | gated on `jointsAcc && weightsAcc` | ✅ correct | — | `GLTF-259` |
| morph deltas applied incorrectly | `BlendMorphTargetsEXT` | 🐛 normals skipped on strides 48/68 | code | `GLTF-278` |
| zero/default morph weights | `mesh.weights` applied at import | ✅ correct; `node.weights` **ignored** | — | `GLTF-281` |
| malformed GPU vertex buffer packing | `BuildVertexBufferFromRawBytes` | ✅ correct for the 7 known strides | — | `GLTF-149` |
| shader semantic mismatch | per-renderer `ApplyLayout` | 🔬 INVESTIGATE | H13 | `GLTF-374`, `GLTF-379` |
| world/view/projection matrix order | `IEffectMatrices` | ✅ correct | — | `GLTF-266` |

---

## 6. Layered Failure Taxonomy

Every task and every future bug carries **exactly one** primary owner.

| Owner | Scope | Canonical files |
|---|---|---|
| `GLTF-CONTAINER` | `.gltf` JSON, GLB chunk layout, `asset.version`, `extensionsUsed`/`extensionsRequired`, structural validation | `cgltf.h`, `Convert()`, `ReadGltfModel()` |
| `GLTF-BUFFER` | `buffer.uri`, external files, base64 `data:` URIs, GLB `BIN` chunk, `byteLength` | `cgltf_load_buffers`, `ExtractImage` |
| `GLTF-ACCESSOR` | `bufferView` offset/length/stride, `accessor` offset/count/type/componentType/normalized/sparse | `UnpackAccessor`, `cgltf_accessor_read_index` |
| `GLTF-MESH` | primitive assembly, `mode`, indices, attribute→stream mapping, stride selection | `ExtractMesh` |
| `GLTF-TRANSFORM` | node graph, TRS vs `matrix`, world composition, scenes, instancing | `CollectMeshGroups`, `MeshGroup`, `Model`/`ModelBone` |
| `GLTF-MATERIAL` | metallic-roughness factors, alpha modes, double-sided, effect selection | `ExtractMesh` material block, `MeshOut` |
| `GLTF-TEXTURE` | images, samplers, texture views, `texCoord`, `KHR_texture_transform`, colour space | `ExtractImage`, `Find*Image`, `Texture2D::FromStream` |
| `GLTF-SKIN` | `skin.joints`/`skeleton`/`inverseBindMatrices`, `JOINTS_n`/`WEIGHTS_n`, joint matrices | `BuildSkeleton`, `AnimationPlayer` |
| `GLTF-MORPH` | morph targets, `mesh.weights`, `node.weights`, delta application | `ExtractMesh` morph block, `MorphTargetEXT.cpp` |
| `GLTF-ANIMATION` | samplers, channels, paths, interpolation, time domain | `ExtractClips`, `ExtractMorphWeightTrack`, `AnimationPlayer` |
| `GLTF-EXTENSION` | every `KHR_*` / `EXT_*` other than Draco | `ExtractMesh`, `ExtractPunctualLightsEXT` |
| `CNA-GPU-PACKING` | `MeshOut` → `VertexBuffer`/`IndexBuffer`/`ModelMeshPart`, stride ABI, primitive counts | `BuildVertexBufferFromRawBytes`, `ContentManager.cpp` |
| `CNA-EFFECT` | `PbrEffect`/`SkinnedPbrEffect`/`BasicEffect` parameter semantics and defaults | `modules/graphics/src/Xna/*Effect.cpp` |
| `CNA-RENDERER` | per-renderer vertex layout binding, shader semantics, sampler/blend/cull state | `modules/renderers/*/src/*Renderer.cpp` |
| `VIEWER` | `cna-gltf-viewer` presentation policy and build wiring **only** | `openeggbert/cna-gltf-viewer` |

---

## 7. Reference / Oracle Strategy

### 7.1 The seven-layer oracle hierarchy

**A screenshot is the last oracle, never the first.** When a render is wrong, the developer walks
this ladder and reports *the first layer at which reality diverges*. Every task in §29 names the
layer it is verified at.

| Layer | Name | What is compared | Mechanism |
|---|---|---|---|
| **L1** | Parser | JSON/GLB structure: counts, indices, `extensionsRequired`, `cgltf_validate` result | GoogleTest over `cgltf_data` |
| **L2** | Decoded accessor | exact `float`/`uint` arrays per accessor, before any semantic interpretation | new `DumpAccessorEXT` test helper vs a `.expected.json` sidecar |
| **L3** | Semantic mesh | per-primitive positions / normals / tangents / UVs / colours / joints / weights / index list / topology, in **mesh-local** space | `MeshOut` compared field-by-field against a `.expected.json` |
| **L4** | Transformed CPU geometry | world-space vertex positions after node/skin/morph composition | new `EvaluateWorldPositionsEXT` test helper vs `.expected.json` |
| **L5** | GPU packing | byte-exact generated vertex buffer, index buffer, stride, element offsets/formats, `primitiveCount` | `memcmp` against a golden `.vb.bin`/`.ib.bin` |
| **L6** | Effect / shader parameters | every effect parameter actually bound for a draw: matrices, factors, textures, bone palette | `GpuDrawParams` capture via the `HEADLESS`/`STUB` renderer |
| **L7** | Rendered pixels | framebuffer vs a golden PNG, per-renderer, with a stated tolerance | existing `examples/golden/` harness + xvfb |

Rule: **a task may not be closed at layer N until layers 1…N−1 pass for its fixture.**

### 7.2 External reference policy

External glTF implementations are used **only** to generate golden data and for development
comparison. **CNA's runtime must never depend on another glTF library.** Golden data is committed as
plain JSON/PNG with provenance, so the CI has no external dependency.

| Source | Version pin | Used for | Licence |
|---|---|---|---|
| Khronos glTF 2.0 specification | 2.0, registry snapshot recorded in `GLTF-002` | normative semantics; section citations in task rows | Khronos IP |
| `KhronosGroup/glTF-Sample-Assets` | commit pinned in `GLTF-013` | real-world corpus; per-asset licence review | mostly CC-BY / CC0 — **reviewed per asset** |
| `KhronosGroup/glTF-Asset-Generator` | release pinned in `GLTF-014` | conformance permutation assets + their manifest | MIT |
| `KhronosGroup/glTF-Validator` | release pinned in `GLTF-015` | fixture self-validation in the corpus build script | Apache-2.0 |
| `KhronosGroup/glTF-Sample-Renderer` (reference viewer) | tag pinned in `GLTF-016` | L7 reference screenshots for the retake matrix | Apache-2.0 |
| `google/draco` | system `libdraco` | Draco parity fixtures | Apache-2.0 |

Every generated golden file records, in a header comment or sibling `.provenance.json`: source tool,
version/commit, exact command line, and the date. `GLTF-020` makes golden regeneration a single
reproducible script.

### 7.3 Authoritative specification anchors

Cited by section, not pasted:

* node transforms and `matrix`/TRS mutual exclusivity — glTF 2.0 §3.5 (`Nodes and Hierarchy`)
* accessor data layout, alignment, stride — §3.6.2, §3.6.2.4 (`Data Alignment`)
* sparse accessors — §3.6.2.3
* primitive modes — §3.7.2 (`mesh.primitive.mode`)
* attribute semantics and permitted component types — §3.7.2.1
* skins and the joint matrix — §3.8 (including the mesh-node-transform rule)
* morph targets — §3.7.2.2, `mesh.weights` / `node.weights`
* animation samplers and CUBICSPLINE — §3.9, Appendix A (`Animation Sampler Interpolation Modes`)
* metallic-roughness material model and texture channel packing — §3.9.x `material`, Appendix B
* alpha modes — §3.9.4
* colour space of each texture — §3.9.2 (`baseColor`/`emissive` are sRGB; `normal`, `occlusion`,
  `metallicRoughness` are linear)
* cameras — §3.10
* `KHR_lights_punctual`, `KHR_texture_transform`, `KHR_materials_*`, `KHR_draco_mesh_compression`,
  `EXT_meshopt_compression` — their own extension `README.md` in `KhronosGroup/glTF`

---

## 8. Binary Data Model Audit

### 8.1 Effective address — the one equation that matters

For an accessor `a` on `bufferView` `v` of `buffer` `b`, element `i`, component `c`:

```
componentSize = sizeof(a.componentType)
elementSize   = componentSize * numComponents(a.type)          // with the MAT2/MAT3 padding rule
stride        = v.byteStride ? v.byteStride : elementSize      // interleaving
address       = b.data + v.byteOffset + a.byteOffset + i * stride + c * componentSize
```

Constraints CNA must enforce and currently does **not**:

* `v.byteOffset + a.byteOffset + (a.count − 1) * stride + elementSize ≤ v.byteOffset + v.byteLength`
* `v.byteOffset + v.byteLength ≤ b.byteLength`
* `a.byteOffset % componentSize == 0` and `stride % componentSize == 0` (§3.6.2.4)
* `v.byteStride` must be in `[4, 252]` and a multiple of 4 when present
* every arithmetic step above must be overflow-checked in `size_t`

Current state: **none of these is checked**, because `cgltf_validate()` is never called and CNA adds
no checks of its own. A malformed or hostile file reads out of bounds. `GLTF-021`…`GLTF-040` close
this.

### 8.2 MAT2/MAT3 padding

glTF §3.6.2.4 requires each *column* of a `MAT2`/`MAT3` accessor with 1- or 2-byte components to
start on a 4-byte boundary, so the element is larger than `rows*cols*componentSize`. cgltf implements
this correctly (`cgltf_element_read_float` special cases). CNA only ever reads `MAT4` of `FLOAT`
(inverse bind matrices), so it is currently unexposed — but `GLTF-058` adds the fixture so a future
`MAT3` use cannot regress silently.

### 8.3 Component-type matrix (what must be tested, per accessor shape)

| | `SCALAR` | `VEC2` | `VEC3` | `VEC4` | `MAT2` | `MAT3` | `MAT4` |
|---|---|---|---|---|---|---|---|
| `BYTE` (5120) | `GLTF-050` | · | · | norm. only | pad | pad | — |
| `UNSIGNED_BYTE` (5121) | idx `GLTF-064` | · | · | `JOINTS_0`, `COLOR_0` | pad | pad | — |
| `SHORT` (5122) | `GLTF-051` | · | · | norm. only | · | pad | — |
| `UNSIGNED_SHORT` (5123) | idx `GLTF-064` | · | · | `JOINTS_0`, `COLOR_0` | · | pad | — |
| `UNSIGNED_INT` (5125) | idx `GLTF-064` | · | · | · | — | — | — |
| `FLOAT` (5126) | anim input | UV | POSITION/NORMAL | TANGENT/WEIGHTS | · | · | IBM |

`GLTF-050`…`GLTF-059` cover one fixture per non-trivial cell, plus the normalized/non-normalized
distinction for each integer type. Normalized conversion is exactly:

```
signed   : max(v / (2^(bits−1) − 1), −1.0)      // BYTE, SHORT
unsigned : v / (2^bits − 1)                      // UNSIGNED_BYTE, UNSIGNED_SHORT
```

`GLTF-056` asserts the endpoint values (`−128 → −1.0`, `127 → 1.0`, `255 → 1.0`, `0 → 0.0`) exactly.

### 8.4 The `reinterpret_cast` hazard

`ExtractMesh` builds its vertex bytes with `std::memcpy`-based `AppendFloat`/`AppendUint16` — safe.
But `ContentManager.cpp` does
`reinterpret_cast<const std::uint32_t*>(meshOut.indexBytes.data())`, and cgltf's own
`cgltf_component_read_float` performs a misaligned `float` load (`REMED-NA-016`). Neither is known to
fault on the current targets, but both are UB. `GLTF-036`/`GLTF-037`/`GLTF-038` bring the whole path
under ASan+UBSan in CI and replace the casts with `memcpy` on the CNA side; the vendored cgltf
divergence decision is deliberate and recorded, not silently patched.

---

## 9. Accessor Audit

### 9.1 Attribute path — VERIFIED CORRECT

`UnpackAccessor()` wraps `cgltf_accessor_unpack_floats`, which:

* honours `bufferView.byteStride` (interleaving) — proven by `f5`;
* honours `bufferView.byteOffset` and `accessor.byteOffset` — proven by `f5`;
* honours `accessor.normalized` per component type — proven by `f10`;
* resolves `accessor.sparse` including the "no base `bufferView`" case — proven by `f6`;
* validates the declared component count and throws on mismatch (`UnpackAccessor`'s own check).

**Remediation must not rewrite this.** The tasks in Phase 2 add *fixtures and bounds checks* around
it, not a replacement decoder.

### 9.2 Index path — CONFIRMED BROKEN

```cpp
indices.push_back(static_cast<std::uint32_t>(
    prim.indices ? cgltf_accessor_read_index(prim.indices, i) : i));
```

`cgltf_accessor_read_index` (`cgltf.h:2519`) returns **`0`** — with no error channel — when
`accessor->is_sparse` or `accessor->buffer_view == NULL`. Its own upstream comment says
*"This is an error case, but we can't communicate the error with existing interface."* CNA does not
check either condition. Result: `f3` decoded `[0,0,0,0,0,0]` for an expected `[0,1,2,0,2,3]`.

The fix is a CNA-side index reader that mirrors `UnpackAccessor`'s structure (`GLTF-063`): unpack the
index accessor to `uint32` through a sparse-aware path, validating that every value is `< vertexCount`.

### 9.3 Known sparse hazard in the vendored cgltf — CONFIRMED, worked around (`GLTF-062`)

In `cgltf_accessor_unpack_floats`' second (sparse) pass:

```c
for (...; reader_index < sparse->count; reader_index++, index_data += index_stride,
                                         reader_head += accessor->stride)
```

`accessor->stride` is the **base** accessor's stride, which equals `bufferView.byteStride` when the
base data is interleaved. glTF §3.6.2.3 requires the sparse **values** array to be tightly packed
(element size), independent of the base stride. For an accessor that is *both* interleaved *and*
sparse, this reads the override values at the wrong offsets.

`GLTF-062` built that fixture (`sparse-interleaved-base`) and **confirmed the bug**. The decisive
evidence is that cgltf contradicts *itself*: its validator sizes the values bufferView as
`element_size * sparse->count` — tightly — and then its reader walks the same array at
`accessor->stride`. Both cannot be right, and the specification says the validator is.

The resolution is a CNA-side sparse resolver, not a refusal: the file is perfectly legal glTF and
rejecting it would be CNA punishing the asset for its parser's mistake. `ApplySparseOverridesTightly`
re-applies the overrides at `i * elementSize` on top of whatever cgltf wrote, and runs after **every**
`cgltf_accessor_unpack_floats` call in the importer — the attribute path via `UnpackAccessor` and the
flat morph-weight animation output, which reads its accessor outside `UnpackAccessor`. It is a no-op
unless `accessor->stride != elementSize`, so no ordinary asset takes a different path than before.

Two consequences worth stating:

* **The L2 oracle carried the bug too.** `DumpAccessorEXT` delegates decoding to cgltf precisely so
  the oracle is not CNA judging itself — which means that for this one combination the oracle *was*
  the defective component, and would have certified the wrong answer as "expected". The oracle now
  restates §3.6.2.3's addressing independently (its own component reader, including §3.6.2.4's
  `MAT2`/`MAT3` column padding). Both implementations are checked against the Python-generated
  manifest, which is where the truth actually lives.
* **The bug itself is pinned, not just fixed.**
  `GltfAccessorDecodeLock.VendoredParserStillMisreadsSparseValuesAtTheBaseStride` asserts that raw
  `cgltf_accessor_unpack_floats` still misplaces override 1 onto override 2's bytes. A cgltf upgrade
  that fixes the reader fails that test, which is the signal to retire both copies of the workaround
  rather than leave them to rot. Filing upstream is still worth doing and is not blocked by anything
  here.

### 9.4 Cross-accessor consistency — UNCHECKED

`ExtractMesh` uses `posAcc->count` as *the* vertex count and then indexes every other unpacked stream
with it. A file whose `NORMAL` accessor has a smaller `count` causes an out-of-range read of the
`normals` vector. glTF §3.7.2.1 requires all attribute accessors of a primitive to have equal
`count`; CNA never asserts it. `GLTF-060` adds the check with a clear diagnostic.

Similarly `accessor.min`/`max` are never read. They are not required for correctness, but they are a
free L2 cross-check: `GLTF-061` asserts decoded bounds against the declared bounds and warns on
divergence, which would have caught D4 instantly.

---

## 10. Mesh / Vertex / Index Audit

### 10.1 Primitive topology — CONFIRMED BROKEN

`cgltf_primitive_type` has **zero occurrences** in CNA production code. `prim.type` is never read.
All seven glTF modes are decoded as if they were `TRIANGLES`, and all three loaders then compute
`primitiveCount = numIndices / 3`.

| glTF `mode` | Name | CNA today | Proven | XNA equivalent |
|---|---|---|---|---|
| 0 | `POINTS` | triangle list; 4 pts → 1 triangle | `f12` 🐛 | `PrimitiveType::PointList` — **not in XNA 4.0**; CNAEXT or convert |
| 1 | `LINES` | triangle list | — 🐛 | `PrimitiveType::LineList` ✅ |
| 2 | `LINE_LOOP` | triangle list | — 🐛 | none — convert to `LineStrip` + closing segment |
| 3 | `LINE_STRIP` | triangle list | — 🐛 | `PrimitiveType::LineStrip` ✅ |
| 4 | `TRIANGLES` | correct | ✅ | `PrimitiveType::TriangleList` ✅ |
| 5 | `TRIANGLE_STRIP` | triangle list; 4 idx → 1 triangle, vertex 3 lost | `f4` 🐛 | `PrimitiveType::TriangleStrip` ✅ |
| 6 | `TRIANGLE_FAN` | triangle list | — 🐛 | none in XNA 4.0 — convert to a triangle list at import |

**Policy decision (`GLTF-072`):** *never silently reinterpret*. The import must, per mode:

* `TRIANGLES` — pass through;
* `TRIANGLE_STRIP`, `TRIANGLE_FAN` — **convert to a triangle list at import**, preserving winding
  (a strip flips winding on odd triangles; the conversion must emit `(i, i+1, i+2)` for even and
  `(i+1, i, i+2)` for odd), so no renderer needs a new topology and the whole GPU packing layer is
  unchanged;
* `LINES`, `LINE_STRIP` — carry real XNA `PrimitiveType` through `ModelMeshPart`;
* `LINE_LOOP` — convert to `LINE_STRIP` plus the closing segment;
* `POINTS` — CNAEXT `PrimitiveType` on the renderers that support it, otherwise an explicit,
  documented "not supported" error naming the mode. **Never** a silent triangle reinterpretation.

Converting strips/fans at import is chosen deliberately over plumbing new topologies through 41
renderers: it is provable at L3/L5, needs no renderer change, and cannot regress an existing renderer.

**Status (`GLTF-072`, landed).** The three triangle-producing modes are done: `TRIANGLES` passes
through and `TRIANGLE_STRIP`/`TRIANGLE_FAN` convert, winding preserved. The four remaining modes are
**not** a decoding gap — they decode correctly and are rejected only because every loader still
computes `numIndices / 3`. What they need is a draw path: `GLTF-073` (a real `PrimitiveType` on
`ModelMeshPart`), `GLTF-078` (a topology-aware primitive count), `GLTF-076` (`LINE_LOOP`'s closing
segment) and `GLTF-077` (whether a point list is CNAEXT-supported or explicitly refused per
renderer). Until then the rejection names the owning task.

### 10.2 Index width and truncation

`out.use32BitIndices = vertexCount > 65535`. If `vertexCount ≤ 65535` every index is
`static_cast<std::uint16_t>` with no guard. A spec-legal file may declare `UNSIGNED_INT` indices with
values ≥ 65536 while `POSITION.count ≤ 65535` only if those indices are out of range — i.e. the file
is already invalid — but the current code turns "invalid file" into "silently wrong geometry" instead
of a diagnostic. `GLTF-068` validates `index < vertexCount` before packing and errors clearly.

### 10.3 Non-indexed primitives

Handled: `prim.indices == nullptr` synthesises `0..count-1`. Correct, and `f12` proves it runs. But
CNA then **always** materialises an index buffer even for a non-indexed primitive — wasteful, and it
hides the distinction. `GLTF-070` keeps the synthesis (it keeps the GPU layer uniform) but records
the decision explicitly and tests it.

### 10.4 Attribute → stream mapping

| glTF semantic | CNA today | Gap |
|---|---|---|
| `POSITION` | required; offset 0 in every stride | ✅ |
| `NORMAL` | offset 12 on strides 32/48/52/56/68 | dropped on strides 20/24 with **no warning** (`GLTF-085`) |
| `TANGENT` | read only when `usePbr`; generated when absent | ignored for non-PBR meshes (`GLTF-171`) |
| `TEXCOORD_0` | the base-colour texture's `texCoord` set, or 0 | only **one** set is ever baked (`GLTF-181`) |
| `TEXCOORD_1+` | detected as `pbrUv2Mismatch`, warned, **not** used | `GLTF-183`…`GLTF-188` |
| `COLOR_0` | `VEC3`/`VEC4`, any component type, → `Color` bytes | precision loss on `FLOAT`/`USHORT` colours, undocumented (`GLTF-090`) |
| `COLOR_1+` | ignored | correct (XNA has one colour); document (`GLTF-091`) |
| `JOINTS_0` | `uint8` after `oldToNew` remap | `>255` joints truncate silently (`GLTF-254`) |
| `WEIGHTS_0` | 4 floats, never renormalised | `GLTF-256` |
| `JOINTS_1`/`WEIGHTS_1`+ | **ignored entirely, no warning** | `GLTF-257` — >4 influences silently lose the tail |
| custom `_*` attributes | ignored | correct; document (`GLTF-092`) |

### 10.5 Effect-selection rule — the material/topology coupling defect

```cpp
out.usePbr = (!out.colored) && (out.normalImage != nullptr || out.metallicRoughnessImage != nullptr);
```

This makes **texture presence** decide the *shading model*. glTF says `pbrMetallicRoughness` is the
material model whenever the material has it — maps optional. Consequences proven by `f8`: a gold
factor-only material becomes a white `BasicEffect`. Also, `usePbr && colored` is impossible, so any
vertex-coloured PBR mesh silently loses PBR.

`GLTF-215` replaces the rule with: **a primitive whose material has `pbrMetallicRoughness` (or has no
material at all — glTF's default material *is* metallic-roughness) imports as PBR**, with
`BasicEffect`/`DualTextureEffect` reserved for explicitly-chosen legacy paths. This is the single
largest material-layer change and is sequenced after the transform work so its visual effect is
attributable.

---

## 11. Coordinate-System and Transform Audit

### 11.1 Conventions — stated once, authoritatively

| Property | glTF 2.0 | CNA / XNA 4.0 | Conversion needed |
|---|---|---|---|
| Handedness | right-handed | right-handed | **none** |
| Up axis | +Y | +Y | **none** |
| Forward | −Z | −Z (`Matrix::CreateLookAt` looks down −Z) | **none** |
| Units | metres (normative) | unitless | optional `unitScale` |
| Matrix memory layout | flat 16 floats, **column-major** | `Matrix` M11…M44, **row-major** | transpose-equivalent |
| Vector convention | column vectors, `v' = M · v` | row vectors, `v' = v · M` | the transpose cancels the layout difference |
| Composition order | `world = parent · local` | `world = local * parentWorld` | equivalent under the above |
| TRS composition | `M = T · R · S` | `M = S * R * T` | equivalent |
| Quaternion order | `(x, y, z, w)` | `Quaternion(X, Y, Z, W)` | **none** |
| Texture origin | UV (0,0) = **top-left** of the image | XNA UV (0,0) = **top-left** | **none** |
| NDC depth range | −1…1 (OpenGL) | 0…1 (Direct3D) | owned by the renderer, not the importer |

**Critical consequence: no handedness or axis conversion belongs anywhere in the CNA glTF importer.**
`ConvertGltfMatrix` performs *layout* conversion only, and is correct as written. Any future task
that proposes an axis flip must first fail a fixture from §24, not a screenshot. `GLTF-105` records
this as a permanent, testable invariant so a later session cannot "fix" a symptom by flipping Z.

### 11.2 Node local transform — the equations

glTF §3.5. Either `matrix` **or** any of `translation`/`rotation`/`scale` may be present, never both.

```
local(node) = node.matrix                                  if node.matrix is present
            = T(translation) · R(rotation) · S(scale)       otherwise (defaults T=0, R=identity, S=1)
```

In XNA row-vector form, the same transform is:

```
Local = Matrix::CreateScale(S) * Matrix::CreateFromQuaternion(R) * Matrix::CreateTranslation(T)
World(node) = Local(node) * World(parent)      // parent-of-root ⇒ Identity
```

`cgltf_node_transform_local` / `cgltf_node_transform_world` already implement the column-major form
correctly and are used today for lights and bone bind poses. The gap is that **no mesh instance ever
consults them.**

### 11.3 Normal transformation

For a world matrix `W` with a non-uniform or mirroring linear part `A` (upper-left 3×3):

```
normal'   = normalize( normal   * transpose(inverse(A)) )        // row-vector form
tangent'  = normalize( tangent.xyz * A )                          // tangents transform as directions
tangent.w = tangent.w * sign(det(A))                              // handedness flips under mirroring
```

If the node transform is baked into positions at import (§11.5 option B), normals **must** get the
inverse-transpose and tangents **must** get the determinant sign flip, or a mirrored/non-uniformly
scaled node renders with inverted lighting. `GLTF-118`/`GLTF-119`/`GLTF-176` cover exactly this, with
a `negative-scale` fixture.

### 11.4 Transform fixture ladder (L4 oracles)

Each fixture is one node-graph shape with an exactly computable world-space vertex set.

| Fixture | Shape | Expected (single vertex at local `(1,0,0)`) |
|---|---|---|
| `xf-identity` | one node, no transform | `(1,0,0)` |
| `xf-translation` | `T=[3,4,5]` | `(4,4,5)` |
| `xf-scale-uniform` | `S=[2,2,2]` | `(2,0,0)` |
| `xf-scale-nonuniform` | `S=[2,3,4]` | `(2,0,0)` (and the full triangle proves Y/Z) |
| `xf-rot-x90` | `R=quat(x=√½,w=√½)` | `(1,0,0)` (invariant — plus a `(0,1,0)` vertex → `(0,0,1)`) |
| `xf-rot-y90` | `R=quat(y=√½,w=√½)` | `(0,0,−1)` |
| `xf-rot-z90` | `R=quat(z=√½,w=√½)` | `(0,1,0)` |
| `xf-trs-order` | `T=[1,0,0] R=z90 S=[2,2,2]` | `(1,2,0)` — proves `S` then `R` then `T` |
| `xf-matrix-node` | `matrix` = translate(4,5,6) | `(5,5,6)` |
| `xf-matrix-vs-trs` | same transform authored both ways, two nodes | identical outputs |
| `xf-parent-child` | parent `S=2`, child `T=[0,3,0]` | `(2,6,0)` |
| `xf-deep-chain` | 5 nested nodes, each `T=[1,0,0]` | `(6,0,0)` |
| `xf-negative-scale` | `S=[−1,1,1]` | `(−1,0,0)`; winding and `tangent.w` must flip |
| `xf-mirror-child` | parent `S=[−1,1,1]`, child `R=y90` | exactly computed in the manifest |
| `xf-shared-mesh` | one mesh, two nodes at `T=[0,0,0]` / `[10,0,0]` | two instances, X ∈ `[0,11]` |
| `xf-transform-only` | a node with a transform and no mesh, with a mesh child | child placed by the parent |
| `xf-multi-root` | three root nodes in the scene | all three present |

### 11.5 Architecture policy for Phase 5 (`GLTF-103`)

**Option A — a real `ModelBone` node hierarchy — is the presumptive final architecture.**
`GLTF-103` is therefore not an open A-vs-B choice: it is a task to *prove Option A preserves XNA
compatibility and adopt it*, and it may fall back to Option B only by demonstrating a concrete,
written blocker.

**Option A — real bone hierarchy (PRESUMPTIVE FINAL ARCHITECTURE).** `MeshGroup` becomes a list of
*instances* (`{const cgltf_node*, const cgltf_mesh*, Matrix world, Matrix local, int
parentBoneIndex}`); the importer builds a real `ModelBone` tree mirroring the glTF node graph, and
each `ModelMesh`'s `ParentBone` is its node's bone. `Model::Draw` then already composes correctly
(`Model.cpp` is verified correct). `.cnj` gains a `"bones"` array with parent indices and
transforms, and a per-mesh `"parentBone"` index.

CNA already ships every primitive this needs — `Model`, `ModelBone`, `ModelMesh::ParentBone`,
`CopyAbsoluteBoneTransformsTo` — and `Model::Draw`'s composition was **verified correct** by this
audit. Building the node graph at import is therefore the architecturally natural move, not a new
subsystem.

Option A is the only architecture that preserves all seven of:

1. mesh instancing (a shared mesh drawn from N nodes, one `VertexBuffer`);
2. a real scene node graph;
3. rigid (non-joint) node animation — **D6 cannot be fixed without it**;
4. correct `node.weights` per instance;
5. cameras and lights attached to nodes, with their node transforms;
6. hierarchy visibility to game code via `CopyAbsoluteBoneTransformsTo`;
7. natural parity between the offline `.cnj` path and the direct runtime path.

Its cost is a versioned, additive `.cnj` format change (`GLTF-129`) that both loaders must adopt
together (`GLTF-130`). Under this policy those two tasks are **mandatory, not conditional**.

**Option B — bake world transforms into vertices — is a temporary emergency fallback only.**
Positions premultiplied by `world`, normals by `transpose(inverse(A))`, `tangent.w` by
`sign(det A)`, winding flipped on negative determinant.

* Only merit: no format change, smallest diff.
* Cost: destroys every one of the seven properties above. It duplicates a shared mesh per node (N×
  geometry memory), makes rigid node animation **impossible**, and permanently discards the
  hierarchy.

Option B is **not** an equal long-term alternative and must never be recorded as the final
architecture. It may be adopted only if `GLTF-103` demonstrates a concrete blocker to Option A, and
then only as an explicitly time-boxed interim step that carries its own removal task.

`GLTF-103` decides on evidence from the L2/L3/L4 oracle harness (`GLTF-004`, `GLTF-005`,
`GLTF-006`) — it does **not** wait for the `GLTF-011` verdict, which is written after the
center-collapse fixes land (§28).

---

## 12. GPU Packing Audit

### 12.1 The magic-stride ABI

`MeshOut::stride` is an integer that must be interpreted identically by `ExtractMesh`,
`BuildVertexBufferFromRawBytes`, the `.cnj` reader, and a `switch (stride)` in **every GPU
renderer's** `ApplyLayout`. There is no shared declaration. A renderer that lacks a case falls
through to a **position-only fallback** which leaves normals, UVs, weights and joint indices
unbound — reading whatever the previous draw left in those attribute slots.

`GLTF-155`…`GLTF-162` address this: a single canonical stride→`VertexDeclaration` table in
`modules/graphics`, a static assertion per stride, a renderer conformance test that every enabled
renderer accepts all seven strides, and replacement of the silent fallback with a loud diagnostic.

### 12.2 Byte-level packing invariants to assert (L5)

For each of the seven strides, `GLTF-149`…`GLTF-154` assert, byte-for-byte:

* `vertexBytes.size() == vertexCount * stride` exactly (no padding, no tail);
* Position is 3 `float32` LE at offset 0 for every stride;
* every declared element offset matches the table in §2.3;
* `VertexBuffer::VertexCount == vertexCount` and its declared stride matches;
* `IndexBuffer::IndexCount == indices.size()` and its element size matches `use32BitIndices`;
* `ModelMeshPart` `VertexOffset == 0`, `StartIndex == 0`, and
  `PrimitiveCount == expectedPrimitiveCount(mode, indexCount)` — **not** unconditionally `/3`;
* a colour byte is `round(clamp(f,0,1)*255)` (`ToByteColorChannel`), asserted at the endpoints.

### 12.3 Primitive count

All three loaders compute `primCount = numIndices / 3`. After §10.1's topology work this becomes:

```
TriangleList  : indexCount / 3
TriangleStrip : max(indexCount − 2, 0)      // only if strips are ever carried through
LineList      : indexCount / 2
LineStrip     : max(indexCount − 1, 0)
PointList     : indexCount
```

`GLTF-152` centralises this in one helper used by all three loaders.

### 12.4 Buffer lifetime

`ModelResources` owns `vbs`, `ibs`, `partOwners`, `meshOwners`, `boneOwners`, `textureOwners`,
`effectOwners`, `morphOwners` in a `shared_ptr<void>` attached to the `Model`. Copies of the `Model`
share it. This is sound. `GLTF-436`…`GLTF-440` add the stress coverage (repeated load/unload, device
loss, `ContentManager::Unload`) rather than changing it.

---

## 13. Materials / PBR

### 13.1 Field-by-field status

| glTF field | Spec default | CNA import | CNA effect | Status |
|---|---|---|---|---|
| `pbrMetallicRoughness.baseColorFactor` | `[1,1,1,1]` | **not read** | `PbrEffect::DiffuseColor` + `Alpha` exist and are wired to the shader | 🐛 `GLTF-216` |
| `baseColorTexture` | — | ✅ (honours its own `texCoord`) | `PbrEffect::Texture` | ✅ |
| `baseColorTexture.texCoord` | 0 | ✅ used to choose the single baked UV set | — | ✅ |
| `metallicFactor` | 1.0 | ✅ (only when `usePbr`) | `MetallicFactor` | ⚠ gated |
| `roughnessFactor` | 1.0 | ✅ (only when `usePbr`) | `RoughnessFactor` | ⚠ gated |
| `metallicRoughnessTexture` | — | ✅ | `MetallicRoughnessMap`; shader reads **G = roughness, B = metallic** ✅ | ✅ |
| `normalTexture` | — | ✅ | `NormalMap`; flat-normal fallback when unbound (EasyGL) | ✅ |
| `normalTexture.scale` | 1.0 | **not read** | no parameter exists | 🐛 `GLTF-224` |
| `occlusionTexture` | — | ✅ | `OcclusionMap`; shader reads **R** ✅ | ✅ |
| `occlusionTexture.strength` | 1.0 | **not read** | no parameter exists | 🐛 `GLTF-225` |
| `emissiveTexture` | — | ✅ | `EmissiveMap` | ✅ |
| `emissiveFactor` | `[0,0,0]` | ✅ (only when `usePbr`) | `EmissiveFactor` | ⚠ gated |
| `alphaMode` | `OPAQUE` | **not read** | `AlphaTestEffect` exists; `PbrEffect` has `uAlphaTest` but it is never configured | 🐛 `GLTF-228` |
| `alphaCutoff` | 0.5 | **not read** | — | 🐛 `GLTF-229` |
| `doubleSided` | `false` | **not read** | `RasterizerState` is caller-owned | 🐛 `GLTF-231` |
| no material at all | default metallic-roughness material | falls to `BasicEffect` white | — | 🐛 `GLTF-217` |

### 13.2 Channel semantics — verified

The EasyGL PBR fragment shader reads:

```glsl
float roughness = clamp(mr.g * uRoughnessFactor, 0.045, 1.0);
float metallic  = clamp(mr.b * uMetallicFactor,  0.0,   1.0);
float occlusion = texture(uOcclusionMap, ...).r;
```

That is exactly glTF's packing (occlusion **R**, roughness **G**, metallic **B**). ✅ `GLTF-233`
locks it with a fixture whose three channels carry three distinct values, and `GLTF-234` runs the
same fixture on every renderer so a renderer that swapped G/B is caught.

The BRDF itself (GGX D, Smith-Schlick-GGX G with `k=(r+1)²/8`, Schlick F, `F0 = mix(0.04, albedo,
metallic)`) matches glTF Appendix B. `GLTF-235` adds analytic spot-checks at normal incidence rather
than an image diff.

### 13.3 Colour space — CONFIRMED BROKEN

glTF §3.9.2: `baseColorTexture` and `emissiveTexture` are **sRGB-encoded**; `normalTexture`,
`occlusionTexture` and `metallicRoughnessTexture` are **linear**. Lighting must be computed in
linear space and the result encoded back to sRGB for display.

CNA today: `Texture2D::FromStream` produces plain `SurfaceFormat::Color` (RGBA8 UNORM) for every
image; the PBR shader samples all five maps raw; there is no linear→sRGB encode on output. Every
sRGB-authored base-colour and emissive texture is therefore treated as if it were linear, and the lit
result is written without encoding — a double error that partly cancels visually but is quantitatively
wrong everywhere.

Three implementation options, decided in `GLTF-209`:

* **A** — hardware sRGB texture formats (`SurfaceFormat::ColorSRgb`-equivalent) plus an sRGB
  framebuffer. Correct and free at runtime, but needs a new `SurfaceFormat` on 41 renderers.
* **B** — shader-side decode of the two sRGB maps and encode on output, gated by a per-map flag in
  `GpuDrawParams`. Renderer-local, no format change, small per-pixel cost.
* **C** — CPU-side decode at import (bake sRGB→linear into the texture bytes). Precision-lossy at 8
  bits; **not recommended**, recorded only so a later session does not rediscover it.

Option B is the plan's provisional recommendation because it is testable at L6/L7 per renderer
without touching the `SurfaceFormat` enum. `GLTF-210`…`GLTF-213` implement, test and document it.

### 13.4 Lighting policy

`PbrEffect` defaults to `AmbientLightColor = (0,0,0)` with all three `DirectionalLight`s disabled,
and neither loader calls `EnableDefaultLighting()`. A glTF file without `KHR_lights_punctual`
therefore renders **pure black** through `PbrEffect`. This is correct XNA behaviour (XNA's own
`BasicEffect` starts unlit) and correct-ish glTF behaviour (glTF core defines no lights and expects
the *viewer* to supply IBL), so:

* the **CNA** decision (`GLTF-242`) is to leave effect defaults alone and instead expose the
  information — the importer records how many punctual lights it found, so an application can decide;
* the **viewer** decision (V3, `GLTF-424`) is that `cna-gltf-viewer` calls
  `EnableDefaultLighting()` when the imported scene contributed zero lights. That is presentation
  policy and is legitimately viewer-owned.

Image-based lighting is explicitly **out of scope for GLTF CORE 2.0 CORRECT**; `CNAEXT.md` already
tracks `setImageBasedLightEXT` as future work (`N43`). `GLTF-243` records the boundary so nobody
treats "not IBL-accurate" as a conformance failure.

---

## 14. Textures / Samplers / Color Space

### 14.1 Image sources

`ExtractImage` handles all three glTF image storage forms: embedded `bufferView`, external URI (with
`cgltf_decode_uri` percent-decoding), and base64 `data:` URI. ✅ Verified by reading; PNG and JPEG
both flow through `stb_image` inside `Texture2D::FromStream`.

Gaps:

* the external-URI path resolves against `gltfDir` with **no containment check** — a
  `../../../etc/passwd` URI escapes the asset directory (`GLTF-198`, security-relevant);
* `image.mimeType` is trusted over content sniffing when present, and the `data:` URI branch guesses
  `png` for anything that is not `image/jpeg` (`GLTF-199`);
* `KHR_texture_basisu` / `EXT_texture_webp` images are `PARSED_BUT_IGNORED` — the `image` will have
  no `uri`/`buffer_view` and `ExtractImage` returns `nullopt`, so the texture silently disappears
  (`GLTF-200`).

### 14.2 Samplers — CONFIRMED MISSING

`cgltf_sampler` has **zero occurrences** in CNA. Every imported texture is drawn with whatever
`SamplerState` the device happens to have, which `SamplerStateCollection` defaults to
`LinearWrap`. For an asset authored with `CLAMP_TO_EDGE` and UVs outside `[0,1]` — which
`KHR_texture_transform` routinely produces — this is a visible, large error.

| glTF sampler | Value | CNA `SamplerState` mapping (proposed, `GLTF-203`) |
|---|---|---|
| `magFilter` 9728 `NEAREST` | | `Filter = Point` |
| `magFilter` 9729 `LINEAR` | | `Filter = Linear` |
| `minFilter` 9728 `NEAREST` | | `Point`, no mip |
| `minFilter` 9729 `LINEAR` | | `Linear`, no mip |
| `minFilter` 9984 `NEAREST_MIPMAP_NEAREST` | | `Point` + `MipMapPoint` |
| `minFilter` 9985 `LINEAR_MIPMAP_NEAREST` | | `Linear` min/mag + point mip |
| `minFilter` 9986 `NEAREST_MIPMAP_LINEAR` | | point min/mag + linear mip |
| `minFilter` 9987 `LINEAR_MIPMAP_LINEAR` | | `Filter = Linear` (trilinear) |
| `wrapS`/`wrapT` 10497 `REPEAT` | | `TextureAddressMode::Wrap` |
| `wrapS`/`wrapT` 33071 `CLAMP_TO_EDGE` | | `TextureAddressMode::Clamp` |
| `wrapS`/`wrapT` 33648 `MIRRORED_REPEAT` | | `TextureAddressMode::Mirror` |
| sampler absent | | glTF says "repeat, auto filter" ⇒ `LinearWrap` (today's accidental default) |

XNA's `SamplerState` cannot express independent min/mag filters for the four mixed combinations;
`GLTF-204` records the approximation table explicitly rather than leaving it implicit. Mip generation
is a separate decision (`GLTF-206`): glTF assets assume mipmaps exist for the mipmapped min filters,
and CNA's `Texture2D::FromStream` currently produces a single level.

Carrying sampler state per texture slot also requires a home in the model: `ModelMeshPart` has no
sampler property. `GLTF-207` proposes a CNAEXT per-part sampler array attached alongside the existing
`Tag` convention, mirroring `MorphTargetDataEXT`'s precedent, rather than a new public XNA API.

### 14.3 UV orientation

Both glTF and XNA place UV `(0,0)` at the **top-left**. No flip is required, and none is performed.
`GLTF-190` adds an asymmetric checkerboard fixture (a distinct colour per quadrant plus a numeral)
so a future flip regression is unmissable at L7 and, more importantly, detectable at L3 by comparing
decoded UVs.

---

## 15. Skinning

### 15.1 The equations

glTF §3.8, column-vector form:

```
jointMatrix(j) = inverse(globalTransform(meshNode)) · globalTransform(joint_j) · inverseBindMatrix(j)
skinnedPosition = Σ_i  WEIGHTS_0[i] · jointMatrix(JOINTS_0[i]) · position
```

Three rules that are easy to miss and that CNA gets wrong:

1. `globalTransform(joint_j)` is the joint's **full scene-root-relative** transform — every ancestor,
   including nodes that are not themselves joints, **and including every ancestor above
   `skin.skeleton`**. See the boxed rule below.
2. The mesh node's own transform must be **cancelled**, not applied. glTF further says a skinned
   mesh's node transform *should* be ignored entirely; the `inverse(globalTransform(meshNode))` term
   is what makes that true.
3. A joint's **scene-node identity** and its **skin palette index** are different things (§15.1.2).
   Conflating them is how a correct palette reorder silently corrupts the scene hierarchy.

#### 15.1.1 `skin.skeleton` is a root *hint*, never a traversal stop

> ⛔ **`skin.skeleton` must never truncate the ancestry used to compute `globalTransform(joint)`.**
>
> `skin.skeleton` names the declared skeleton root — a semantic hint useful for locating and naming
> the rig. It is **not** a licence to stop walking scene ancestors. Every transform on every
> ancestor above `skin.skeleton`, and every ancestor that is not itself a joint, still contributes
> to `globalTransform(joint)` exactly as the equation above requires.
>
> **D8 is precisely the failure that "stop the walk early" produces.** An implementation that walks
> up only until it reaches `skin.skeleton` — or only within the joint set, as CNA does today —
> reproduces D8 in a new disguise. `f9` proves the cost: one dropped ancestor translation of
> `[0,100,0]` displaces every skinned vertex by exactly `−100` in Y.

#### 15.1.2 Scene-node identity ≠ skin palette identity

Two independent index spaces must be modelled explicitly and never merged:

| Space | Meaning | Ordering constraint | Consumers |
|---|---|---|---|
| **`sceneNodeIndex`** | the node's stable identity in the glTF scene graph and in CNA's `ModelBone` tree | glTF's own node order / the imported bone tree; **stable across loads** | `Model::Bones`, `ModelMesh::ParentBone`, `CopyAbsoluteBoneTransformsTo`, rigid node animation, cameras/lights |
| **`paletteIndex`** | the joint's slot in one skin's GPU bone palette | whatever the shader palette requires (today: breadth-first, parent-before-child) | `SkinningData`, `AnimationPlayer::GetSkinTransforms`, `BlendIndices`, `uBones[]` |

Rules:

* the **scene hierarchy must not be reordered** to satisfy palette ordering — palette ordering is a
  skin-local implementation detail;
* palette reordering and its `oldToNew` remap stay **internal** to the skin;
* `JOINTS_0` decoding targets **`paletteIndex`**, via the skin's own remap;
* rigid animation, scene hierarchy, camera/light attachment and game-facing bone lookup all use
  **`sceneNodeIndex`**;
* the mapping `sceneNodeIndex ↔ paletteIndex` is explicit, per skin, and testable in both
  directions.

This costs nothing today — `BuildSkeleton`'s breadth-first reorder and `oldToNew` remap are already
correct *as a palette operation* (`RuntimeGltfModelTest.LoadsSkinnedAnimatedModelDirectlyFromGltfWithReversedJointOrder` proves it). The rule exists so that Phase 5's new scene hierarchy is not
retro-fitted to the palette order once both exist.

CNA's XNA row-vector equivalent, as currently implemented in `AnimationPlayer::RecomputeTransforms`:

```
world[i]  = bindPoseLocal[i] * world[parent[i]]        // parent[i] < i, topologically ordered
skin[i]   = InverseBindPose[i] * world[i]
```

The multiplication order is **correct**. The inputs are not:

* `world[root]` = the root joint's *node-local* transform only — the ancestor chain is missing
  (**D8**);
* there is no `inverse(meshNodeWorld)` term at all;
* `skin.skeleton` is parsed by cgltf and **never read** by CNA.

### 15.2 The proof (fixture `f9`)

| Quantity | Correct | CNA |
|---|---|---|
| `Armature` node | `translation [0,100,0]` | — (not a joint, so not walked) |
| `globalTransform(Joint0)` | `translate(0,100,0)` | `identity` |
| `inverseBindMatrix(0)` (from the file) | `translate(0,−100,0)` | `translate(0,−100,0)` ✅ read correctly |
| `jointMatrix(0)` | `identity` | `translate(0,−100,0)` |
| vertex `(1,0,0)` after skinning | `(1,0,0)` | `(1,−100,0)` |

Decoded from `scene.skeleton.bin`: `bindPoseLocal` translation `(0,0,0)`, `inverseBindGlobal`
translation `(0,−100,0)`. **Exactly as predicted.**

Generalising: if the joint set's common ancestor carries transform `A`, every skinned vertex is
transformed by `A⁻¹`. For `A = scale(s)`, the character is scaled by `1/s`. Blender's glTF exporter
routinely emits an armature node carrying the scene's unit scale and/or axis conversion — which is
why "the character collapses toward the centre" is the expected symptom of D8 on real content.

### 15.3 Additional skinning gaps

| Gap | Detail | Task |
|---|---|---|
| `skin.skeleton` ignored | the declared skeleton root is parsed by cgltf and never read. It is a naming/locating hint — **it must not scope or truncate the ancestor walk** (§15.1.1) | `GLTF-249` |
| Missing `inverseBindMatrices` | spec says treat as identity; CNA leaves `Matrix::Identity` ✅ correct, untested | `GLTF-250` |
| `JOINTS_1`/`WEIGHTS_1` | ignored with no warning; >4 influences silently truncated | `GLTF-257` |
| Weight renormalisation | never performed; a non-conforming exporter shrinks vertices toward the origin | `GLTF-256` |
| `MaxBones = 72` | rigs above 72 joints silently exceed the palette | `GLTF-261` |
| `BlendIndices` `uint8` | rigs above 255 joints wrap the index | `GLTF-254` |
| Palette ordering | `BuildSkeleton` reorders joints breadth-first and remaps `JOINTS_0` via `oldToNew` ✅ correct **as a palette operation**, and `RuntimeGltfModelTest.LoadsSkinnedAnimatedModelDirectlyFromGltfWithReversedJointOrder` covers it. The risk is Phase 5: the new scene hierarchy must not inherit palette order (§15.1.2) | ✅ / `GLTF-252` |
| Multiple skins per file | one `MeshGroup` per skin; the offline tool emits one `.cnj` each ✅, the runtime path keeps only the first 🐛 | `GLTF-137` |
| Non-uniform joint scale | never tested; normals need the inverse-transpose in the skinning shader | `GLTF-268` |
| Bind pose never applied | `SkinnedEffect` defaults to 72 identity bones; the viewer never calls `SetBoneTransforms`, so a skinned model renders **unskinned** | `GLTF-262`, V-adjacent `GLTF-425` |

### 15.4 Skinning fixture ladder (L4, exactly computable)

| Fixture | Setup | Expected world position of local `(1,0,0)` |
|---|---|---|
| `skin-one-identity` | 1 joint, identity, IBM identity, w=1 | `(1,0,0)` |
| `skin-one-translated` | 1 joint `T=[0,2,0]`, IBM `T=[0,−2,0]`, w=1 | `(1,0,0)` |
| `skin-one-posed` | as above but joint animated to `T=[0,5,0]` at t=1 | `(1,3,0)` |
| `skin-two-weighted` | joints at `T=[0,0,0]`/`T=[0,10,0]`, w=`[0.5,0.5]` | `(1,5,0)` |
| `skin-four-weighted` | 4 joints, w=`[0.4,0.3,0.2,0.1]` | manifest-computed |
| `skin-unnormalized` | w=`[0.5,0.25,0,0]` (sums to 0.75) | manifest states both the raw and the renormalised result; the policy chosen in `GLTF-256` decides which is asserted |
| `skin-parented-joints` | joint B child of joint A, both transformed | manifest-computed |
| `skin-armature-ancestor` | **`f9`** — joints under a transformed non-joint node | `(1,0,0)`; today `(1,−100,0)` |
| `skin-mesh-node-transform` | skinned mesh node with `T=[0,0,50]`, identity joint and IBM | joint matrix `T(0,0,−50)` — the mesh node transform must be cancelled **exactly once**, and must not be re-applied by the node bone Phase 5 gives that same node (`GLTF-260`) |
| `skin-no-ibm` | `inverseBindMatrices` absent | identity IBMs |
| `skin-nonuniform-joint-scale` | joint `S=[1,2,1]` | positions **and** normals asserted |
| `skin-73-joints` | 73 joints | must not silently truncate |
| `skin-256-joints` | 256 joints | must not wrap the index byte |
| `skin-plus-static-mesh` | a skinned mesh **and** an ordinary static mesh, skinned node first | both meshes imported; the prop at its own node's `T=[0,0,−20]` (`GLTF-137`) |
| `skin-skeleton-hint` | `skin.skeleton` names a node with a transform-bearing ancestor **above** it, and which is not the joints' common ancestor | every joint matrix identity; a walk truncated at the declared root yields `T(0,−100,0)` (`GLTF-249`) |

---

## 16. Morph Targets

### 16.1 The equation

glTF §3.7.2.2:

```
morphedPosition = basePosition + Σ_t  weight[t] · positionDelta[t]
morphedNormal   = normalize( baseNormal  + Σ_t weight[t] · normalDelta[t] )
morphedTangent  = normalize( baseTangent.xyz + Σ_t weight[t] · tangentDelta[t] )   // .w preserved
```

Weight source precedence: `node.weights` overrides `mesh.weights`; an animation `weights` channel
overrides both at playback time.

### 16.2 Status

CNA implements **CPU morphing**: `BlendMorphTargetsEXT` re-blends the whole vertex byte array and
`SetMorphWeightsEXT` re-uploads it. Interaction with skinning is clean (morph writes positions and
normals in the same vertex buffer the skinning shader then reads), and interaction with node
transforms is currently vacuous because there are none.

| Aspect | Status |
|---|---|
| POSITION deltas | ✅ extracted, unit-scaled, blended |
| NORMAL deltas | ✅ extracted; **🐛 applied only for strides 32/52/56** — `hasNormalSlot` excludes 48 and 68, yet those layouts also carry Normal at offset 12. Every **PBR** morph target therefore keeps stale normals. `GLTF-278` |
| TANGENT deltas | 🐛 never extracted at all, though strides 48/68 have a tangent slot at offset 24. `GLTF-279` |
| `mesh.weights` | ✅ read and applied at import when non-zero |
| `node.weights` | 🐛 never read — glTF says it overrides `mesh.weights`. `GLTF-281` |
| animation of weights | ✅ `ExtractMorphWeightTrack` + `EvaluateMorphWeightsEXT`, LINEAR/STEP/CUBICSPLINE |
| multiple targets | ✅ unbounded target count |
| target count mismatch | ⚠ `morphPositionDeltas[ti]` is zero-filled for a target with no POSITION delta ✅; a channel whose output size disagrees throws ✅ |
| mesh referenced by >1 node | ⚠ documented simplification: the **first** node found wins, so two instances cannot have different weights. `GLTF-282` |
| renormalisation of blended normals | ✅ performed |
| GPU morphing | not implemented; CPU-only is a deliberate, documented tradeoff |
| performance | full vertex-buffer re-upload per weight change. `GLTF-285`, `GLTF-441` |

### 16.3 Morph fixture ladder (L3/L4)

The morph group **owns** 13 assets — every name is written out in full so the §24.2 inventory is
machine-verifiable rather than inferred from a compressed token:

`morph-position-single`, `morph-position-two-targets`, `morph-normal-delta-basic` (stride 32),
`morph-normal-delta-pbr` (stride 48 — **fails today**), `morph-tangent-delta` (fails today),
`morph-mesh-weights-nonzero`, `morph-node-weights-override` (fails today),
`morph-weights-animated-linear`, `morph-weights-animated-step`, `morph-weights-animated-cubic`,
`morph-target-without-position`, `morph-plus-skin`, `morph-shared-mesh-two-nodes`.

The three `morph-weights-animated-*` assets are owned here and *referenced* by Phase 14
(§17.2's `anim-weights-*`); `morph-plus-skin` is owned here and referenced by `GLTF-269`/`GLTF-286`.
Per §24.1 a reference never re-counts.

---

## 17. Animation

### 17.1 Status

| Aspect | Status |
|---|---|
| `animation.samplers` / `channels` | ✅ parsed |
| target path `translation`/`rotation`/`scale` | ✅ — **but only when the target node is a skin joint** |
| target path `weights` | ✅ via a separate `ExtractMorphWeightTrack` path |
| **rigid (non-joint) node targets** | ✔ **imported** by `ExtractSceneNodeClips` (`GLTF-293`), which resolves channels against the scene graph; the clip targets the node's own `ModelBone`. Not yet serialised to the `.cnj` — `GLTF-294`. Was: silently skipped, and the offline tool only called `ExtractClips` at all when `hasSkin`. |
| `LINEAR` | ✅ `Vector3::Lerp` / `Quaternion::Slerp` |
| `STEP` | ✅ hold-last-value, covered by an existing test |
| `CUBICSPLINE` | ✅ real Hermite with the spec's `Δt` tangent scaling, covered by an existing test; rotations renormalised |
| union-time resampling | ⚠ bone channels are resampled onto the union of their own three channels' times. For a CUBICSPLINE bone channel this **bakes a piecewise-linear approximation** at those sample points — exact at keyframes, approximate between them. Documented in the header; not tested for error magnitude. `GLTF-297` |
| clip duration | ⚠ `maxTime` is the max of the **last input sample** across channels; a channel whose first sample is > 0 is not accounted for (glTF clips may start after t=0). `GLTF-299` |
| looping | ✅ `AnimationPlayer` floor-mod on ticks |
| multiple animations | ✅ one `ClipOut` each; `.cnj` writes one file per clip |
| animation of a camera / light node | 🐛 dropped (same non-joint rule) | `GLTF-296` |
| time domain | seconds throughout, `System::TimeSpan` at the runtime boundary ✅ |

### 17.2 Pose-at-time fixtures (L4)

For each interpolation mode, a single node with a known channel, asserted at `t = 0`, `t = mid`,
`t = end`, `t > end` (clamp), and `t` after a loop wrap:

* `anim-linear-translation` — keys `t=0 → [0,0,0]`, `t=2 → [10,0,0]`; assert `t=1 → [5,0,0]`.
* `anim-step-translation` — same keys, `STEP`; assert `t=1.999 → [0,0,0]`, `t=2 → [10,0,0]`.
* `anim-cubic-translation` — keys with non-trivial tangents; the manifest carries the Hermite result
  computed independently, to 1e-6.
* `anim-rotation-slerp` — identity → 180° about Y; assert `t=mid` is the 90° quaternion.
* `anim-rotation-shortest-path` — the sibling twin of the above with one keyframe quaternion negated;
  assert the **shortest path** was taken by requiring the same pose as `anim-rotation-slerp`.
* `anim-scale-linear`, `anim-multi-channel-one-node` (T+R+S on one node with different key times —
  exercises the union resampling), `anim-two-nodes`, `anim-nonzero-start` (first key at `t=1.5`),
  `anim-rigid-node` (`f7`), `anim-weights-*` (see §16.3).

---

## 18. Cameras / Lights / Scenes

### 18.1 Cameras — NOT IMPLEMENTED

`cgltf_camera` has zero occurrences in CNA. glTF `perspective` (`yfov`, `aspectRatio`, `znear`,
optional `zfar` ⇒ infinite projection) and `orthographic` (`xmag`, `ymag`, `znear`, `zfar`) are never
imported, and no camera node reaches the model.

`GLTF-317`…`GLTF-324` add a CNAEXT camera list on the imported `Model` (attached via the established
`Tag`/owned-resources convention, not a new public XNA type), with:

```
perspective:  Matrix::CreatePerspectiveFieldOfView(yfov, aspectRatio ?: viewportAspect, znear, zfar)
              zfar absent ⇒ infinite far plane (a dedicated CNAEXT builder, since XNA has no overload)
orthographic: Matrix::CreateOrthographic(2*xmag, 2*ymag, znear, zfar)
view        : inverse(worldTransform(cameraNode))     // camera looks down its own −Z
```

`GLTF-323` is explicit that **the viewer must keep its own orbit camera as the default** and only
offer imported cameras as an alternative view — otherwise a broken imported camera would be
indistinguishable from broken geometry.

### 18.2 Lights

`KHR_lights_punctual` is `PARTIAL`: at most 3 lights, all reduced to directional, point/spot aimed
from the light's world position at the scene **origin**, `color * intensity` clamped to `[0,1]`,
extras beyond 3 silently dropped. Range and cone angles are ignored. All of that is a legitimate
consequence of XNA's fixed 3-directional-light model — but it is currently undocumented at the API
surface and unwarned at import.

`GLTF-325`…`GLTF-331`: surface the count of dropped/approximated lights as an import diagnostic,
test the clamp and the ordering, and record the approximation table in `docs/`. Real point/spot
support is explicitly **GLTF ROBUST**, not core.

### 18.3 Scenes

Default-scene selection is ✅ correct (`f14`). Gaps: a file with **no** `scenes` array falls back to
"every mesh in the file" (reasonable, untested — `GLTF-332`); a node appearing in two scenes is
visited once (correct); the runtime path's single-`MeshGroup` limit (`GLTF-137`) means "the default
scene" is not fully imported for mixed skinned/static content.

---

## 19. Extensions

Inventory of every extension the vendored cgltf parses, classified against CNA's actual use.
`extensionsUsed` is advisory; **`extensionsRequired` is normative and CNA never checks it**
(`GLTF-333`) — a file that *requires* an unsupported extension imports silently and wrongly today.

| Extension | Classification | Evidence / note | Task |
|---|---|---|---|
| `KHR_draco_mesh_compression` | **IMPLEMENTED_UNVERIFIED** | decode path exists behind `CNA_DRACO_AVAILABLE`; one triangle test; **no uncompressed-vs-compressed parity test**; not built in this environment | `GLTF-353`… |
| `KHR_texture_transform` | **PARTIAL** | base-colour texture only, baked into the single shared UV channel; per-map transforms lost | `GLTF-184` |
| `KHR_lights_punctual` | **PARTIAL** | ≤3, directional-only approximation | `GLTF-325` |
| `KHR_materials_emissive_strength` | **PARTIAL** | applied only when `usePbr` | `GLTF-222` |
| `KHR_materials_unlit` | **PARSED_BUT_IGNORED** | trivially mappable to `BasicEffect` with lighting off — a genuine quick win | `GLTF-337` |
| `KHR_materials_transmission` | **PARSED_BUT_IGNORED** | causes the `ChronographWatch` opaque-glass defect | `GLTF-339` |
| `KHR_materials_variants` | **PARSED_BUT_IGNORED** | `ChronographWatch` has 4 variants | `GLTF-341` |
| `KHR_materials_ior` | **PARSED_BUT_IGNORED** | affects `F0`; small, well-defined shader change | `GLTF-343` |
| `KHR_materials_specular` | **PARSED_BUT_IGNORED** | | `GLTF-344` |
| `KHR_materials_clearcoat` | **PARSED_BUT_IGNORED** | second specular lobe; large | `GLTF-345` |
| `KHR_materials_sheen` | **PARSED_BUT_IGNORED** | | `GLTF-346` |
| `KHR_materials_volume` | **PARSED_BUT_IGNORED** | depends on transmission | `GLTF-347` |
| `KHR_materials_iridescence` | **NOT_DESIRED** (for now) | | `GLTF-348` |
| `KHR_materials_anisotropy` | **NOT_DESIRED** (for now) | | `GLTF-348` |
| `KHR_materials_dispersion` | **NOT_DESIRED** (for now) | | `GLTF-348` |
| `KHR_materials_pbrSpecularGlossiness` | **UNSUPPORTED** (archived by Khronos) | detect and convert to metallic-roughness, or reject clearly | `GLTF-349` |
| `KHR_texture_basisu` | **UNSUPPORTED** | image silently vanishes today | `GLTF-200`, `GLTF-350` |
| `EXT_texture_webp` | **UNSUPPORTED** | ditto | `GLTF-350` |
| `EXT_meshopt_compression` | **UNSUPPORTED** | cgltf parses the extension but decoding requires a caller-supplied hook CNA does not provide ⇒ buffer data is absent | `GLTF-351` |
| `EXT_mesh_gpu_instancing` | **UNSUPPORTED** | CNA *has* `DrawInstancedPrimitives`, so this is a natural later fit | `GLTF-352` |

**Priority rule:** no extension work begins before **GLTF CORE 2.0 CORRECT**, with two exceptions
already load-bearing in CNA's advertised support — `KHR_texture_transform` and `KHR_lights_punctual`
must not regress, and `KHR_draco_mesh_compression` must reach parity because it is a *geometry* path.

---

## 20. Draco / Compression

Draco decoding lives entirely inside `ExtractMesh` behind `#ifdef CNA_DRACO_AVAILABLE`, via
`DecodeDracoPrimitiveEXT` + `FindDracoUniqueId` + `UnpackDracoAttribute`. Design points that are
**correct** and must be preserved:

* attribute transforms are left at the decoder default, so values read back already dequantised — the
  same semantic level `cgltf_accessor_unpack_floats` produces for a regular accessor;
* the decoded point count is validated against the declared `POSITION.count`;
* connectivity comes from the decoded mesh's own face list, not `prim.indices`;
* `unpackSemantic` makes every downstream call site source-agnostic, so Draco and non-Draco share one
  code path — **there is no separate transform or material path.** ✅ This is exactly the property the
  brief demands, and it must be locked by a test rather than assumed.

Gaps:

* **no parity test**: `GLTF-353` builds the same mesh twice (uncompressed and Draco) and asserts that
  every decoded semantic stream — POSITION, NORMAL, TANGENT, TEXCOORD, COLOR, JOINTS, WEIGHTS,
  indices — matches within a stated quantisation tolerance, at **L3**, not L7;
* the `!CNA_DRACO_AVAILABLE` error path is tested, the *available* path only for a triangle;
* Draco with skinning, with morph targets, and with a non-`TRIANGLES` mode are all untested;
* `FindDracoUniqueId`'s pointer-arithmetic reinterpretation of cgltf's placeholder accessor index is
  correct but fragile against a cgltf upgrade — `GLTF-359` pins it with a direct unit test.

`EXT_meshopt_compression` is a separate, larger problem (§19) and is **GLTF ROBUST**.

---

## 21. Effect / Shader Integration

### 21.1 Contract table (what must agree, and where it is asserted)

| Quantity | Importer writes | Effect exposes | Shader reads | Assert at |
|---|---|---|---|---|
| World / View / Projection | `ModelBone` chain → `Model::Draw` | `IEffectMatrices` | `uWVP`, `uWorld` | L6 `GLTF-266` |
| Normal matrix | — | derived by the effect | `uNormalMatrix` | L6 `GLTF-267` — must be `transpose(inverse(world3x3))`, verified under non-uniform scale |
| Base colour factor | *(missing)* → `GLTF-216` | `PbrEffect::DiffuseColor` + `Alpha` | `uDiffuseColor` | L6 `GLTF-218` |
| Metallic / roughness | `MeshOut` factors | `MetallicFactor` / `RoughnessFactor` | `uMetallicFactor` / `uRoughnessFactor` | L6 `GLTF-220` |
| Emissive | factor × strength | `EmissiveFactor` | `uEmissiveColor` | L6 `GLTF-223` |
| MR texture channels | image bytes | `MetallicRoughnessMap` | `.g` roughness, `.b` metallic | L3+L7 `GLTF-233` |
| Occlusion channel | image bytes | `OcclusionMap` | `.r` | L7 `GLTF-226` |
| Normal map | image bytes | `NormalMap` | `rgb*2−1` in TBN | L7 `GLTF-227` |
| Tangent handedness | `tangent.w` | vertex stream | `vBitangentSign`, `B = cross(N,T)*sign` | L5+L7 `GLTF-175` |
| Bone palette | `SkinningData` → game code | `SetBoneTransforms` | `uBones[72]` | L6 `GLTF-263` |
| Influences per vertex | always 4 | `WeightsPerVertex` | `uWeightsPerVertex` | L6 `GLTF-258` |
| Alpha mode / cutoff | *(missing)* → `GLTF-228` | `uAlphaTest` exists, unconfigured | `_at` discard | L6+L7 `GLTF-230` |
| Double-sided | *(missing)* → `GLTF-231` | `RasterizerState` (caller-owned) | fixed-function | L7 `GLTF-232` |

### 21.2 Rule

> **A field with the same name is not the same convention until a test says so.**

Every row above gets a `GpuDrawParams`-capture test on the `HEADLESS`/`STUB` renderer, so the effect
boundary is validated numerically before any pixel is compared.

---

## 22. Renderer Differential Validation

### 22.1 What is and is not renderer-specific

Every defect in §1.1 is present **in the converter output on disk**, before a draw call. They are all
shared-importer defects and must be fixed **once**, in `GltfImportCore` / `ContentManager` /
`modules/graphics`. Fixing any of them per renderer is explicitly forbidden.

Genuinely renderer-owned surfaces, which must be fixed at the renderer boundary:

* `ApplyLayout`'s magic-stride table and its silent position-only fallback (§12.1);
* the PBR fragment shader's channel reads and its colour-space handling (§13.3);
* the flat-normal fallback for an unbound `NormalMap` (EasyGL has one — `GLTF-384` checks whether
  every PBR-capable renderer does);
* render-target V-flip (`cnaSampleUV`), cull winding, and depth range.

### 22.2 Validation matrix

| Renderer | Role | Layers validated |
|---|---|---|
| `STUB` | pure CPU oracle; no GPU needed; used by the whole L1–L5 test suite | L1–L5 |
| `HEADLESS` | `GpuDrawParams` capture without a window | L1–L6 |
| `OPENGLES3` (EasyGL) | the viewer's renderer; reference GPU path | L1–L7 |
| `VULKAN` | second mature GPU path, independent shader source | L1–L7 |
| `DIRECTX11` | third path, different vertex-layout code, Windows-only | L1–L7 where CI allows |
| `SOFTWARE` | CPU rasteriser cross-check for winding/depth | L5–L7 |

`GLTF-383`…`GLTF-398` run the **same** corpus through each and compare L5/L6 byte-for-byte across
renderers (those layers are renderer-independent by construction, so any divergence is itself a bug)
and L7 with a per-renderer tolerance.

---

## 23. `cna-gltf-viewer` Policy

> ## ⛔ NEVER COMPENSATE FOR A CNA glTF DEFECT IN `cna-gltf-viewer`.
>
> Specifically forbidden, in the viewer or in any sample:
>
> * scaling, translating or rotating the model "until it looks right";
> * reordering, transposing or inverting matrices only in the viewer;
> * flipping an axis or a UV only in the viewer;
> * renormalising skin weights or regenerating normals/tangents only in the viewer;
> * substituting or patching materials only in the viewer;
> * special-casing a known asset by name, hash or size;
> * post-processing the generated `.cnj` or its sidecars.
>
> If the viewer reveals a defect, **fix CNA**. The viewer is a reproducer and an oracle.

Viewer changes this campaign authorises — and no others:

| ID | Change | Justification |
|---|---|---|
| V1 / `GLTF-421` | migrate the build to `CNA_GRAPHICS_RENDERER`, the current renderer identities and the current target names; drop `CNA_NOXNA` | mechanical; the viewer cannot configure against `develop` without it |
| V2 / `GLTF-423` | replace the unconditional `RasterizerState::CullNone` with the state the material asks for, once `doubleSided` is imported; keep `CullNone` behind an explicit `--no-cull` debug flag | presentation policy, viewer-owned |
| V3 / `GLTF-424` | call `EnableDefaultLighting()` when the imported scene contributed zero lights, and say so on stdout | presentation policy, viewer-owned |
| `GLTF-425` | drive `AnimationPlayer` / `SetBoneTransforms` so a skinned model renders **skinned** rather than in the identity-bone pose, and add clip selection | the viewer today cannot exercise skinning at all |
| `GLTF-426` | add `--dump-oracle <dir>` writing the L2–L5 JSON for the loaded asset | makes the viewer a first-class diagnostic, not just a picture |
| `GLTF-427` | after `GLTF-103`, switch camera framing to the **world-space** bounds exposed by the model rather than re-parsing vertex sidecars | removes the viewer's private duplicate of the bounds computation |

A standing audit task (`GLTF-428`) greps the viewer for compensating logic at the end of the campaign
and fails the release gate if any is found.

---

## 24. Test Asset / Conformance Corpus

### 24.1 Principles

* **One rule per file.** A fixture that exercises two semantics cannot localise a failure.
* **Every asset ships a machine-checkable manifest.** `<name>.expected.json` carries the expected
  values for every layer the asset is meant to validate (L2 accessor arrays, L3 semantic streams, L4
  world positions, L5 buffer bytes, L6 effect parameters, L7 golden PNG hash + tolerance).
* **Generated, not hand-edited.** A committed Python generator (`tools/gltf_fixtures/`) emits every
  synthetic asset **and** its manifest from one source of truth, so a fixture and its expectation
  cannot drift.
* **Text-first.** `.gltf` + base64 `data:` URI keeps fixtures diffable; each also gets a `.glb`
  twin via the same generator, so the container path is covered without doubling the authoring.
* **Self-validated.** The generator runs `glTF-Validator` over every emitted file, so a fixture can
  never encode CNA's bug as the spec.
* **Small.** Every synthetic fixture is < 8 KB. Large real-world assets are referenced, licence-
  reviewed, and fetched by a script — not committed blindly.
* **One canonical ID, one owning group, many referencing groups.** Every asset has exactly one
  canonical name and is **listed and counted in exactly one owning group** in §24.2. Other phases
  freely *reference* it — `sparse-indices` is owned by the accessor group and referenced by
  `GLTF-063`; `morph-plus-skin` is owned by the morph group and referenced by `GLTF-269`;
  `anim-weights-*` are owned by the morph group and referenced by Phase 14. **Referencing never
  re-counts.** The distinct-asset total is therefore exactly the sum of the owning-group counts.
* **The manifest is the authority, not this document.** The generator emits a machine-readable
  inventory (`id`, `owningGroup`, `referencingGroups[]`, `validatedLayers[]`, `features[]`), and
  `GLTF-399` asserts in CI that the emitted distinct-asset count equals the number stated here. If
  the corpus grows, the number in this document is updated from the manifest — never the reverse.

### 24.2 Planned corpus — 135 distinct synthetic assets

Counts below are **owning-group** counts per §24.1; no asset is listed twice, so the column sums to
the distinct-asset total. Each asset additionally ships a `.glb` twin (`GLTF-400`), which is the
same asset in another container, not another asset.

> **Count correction (`GLTF-011`).** P0-D raised the skinning group from 13 to 14 (and the total
> from 135 to 136) when it *generated* `skin-mesh-node-transform`. That asset was new to the
> corpus but **not** to this plan — §15.4's ladder already listed it among its 13. All five
> cross-referenced ladders were re-counted and the counts here now match them exactly: transforms
> 17 (§11.4), skinning 13 (§15.4), morph 13 (§16.3), animation 10 (§17.2, excluding the
> morph-owned `anim-weights-*`), and the remaining groups enumerated in full below. The generated
> corpus stands at **43** assets today (`GLTF-072` completed the topology group's seven;
> `GLTF-021`/`GLTF-023` added the first container and robustness fixtures; `GLTF-267` generated
> §11.4's `xf-scale-nonuniform`, the corpus's first non-uniform node scale; `GLTF-062` generated
> `sparse-interleaved-base`, which was planned in the accessor group's 13 all along and turned out
> to be the only asset in the corpus that can observe §9.3's vendored-parser bug at all;
> `GLTF-095` generated `skin-eight-influences`, which §15.4 had not foreseen and which is
> added to the skinning group's own ladder rather than counted against another group;
> `GLTF-254` generated the `skin-joint-index-out-of-range`/`skin-joint-index-padding` pair,
> which raises the robustness group from 6 to 8 — a refusal that is conditional needs an
> asset on each side of the condition, or the condition itself is untested; `GLTF-173`
> generated `normal-absent`, opening the normals group, which §24.2 had planned at 6 all
> along and which no fixture had occupied);
> `GLTF-399` completes the rest.
>
> **A later, deliberate count change (`GLTF-137`).** The skinning group is **14** and the total
> **136** — and that is not the correction above being undone. The correction removed a bump made
> by *generating* an asset §15.4 had already planned. This one adds a row to §15.4 itself:
> `skin-plus-static-mesh`, a file with a skinned mesh and a static one, which no ladder foresaw and
> which `GLTF-137` needs because it is the smallest file whose second mesh group the loader used to
> drop. The ladder and the column still agree, which is the invariant §24.1 actually asks for.

| Group | Count | Assets |
|---|---|---|
| Container / buffer | 8 | `glb-basic`, `glb-bin-chunk-padding`, `gltf-external-bin`, `gltf-data-uri-bin`, `gltf-external-image`, `gltf-data-uri-image`, `gltf-uri-percent-encoded`, `gltf-required-extension-unsupported` |
| bufferView / accessor | 13 | `accessor-offset`, `bufferview-offset`, `bufferview-stride-tight`, `interleaved-position-normal`, `interleaved-pos-nrm-uv`, `interleaved-mixed-widths`, `stride-padded`, `two-primitives-one-buffer`, `sparse-position`, `sparse-indices`, `sparse-interleaved-base`, `accessor-minmax`, `mat3-padded` |
| Component types | 8 | `u8-idx`, `u16-idx`, `u32-idx`, `normalized-u8-color`, `normalized-u16-color`, `float-color`, `normalized-i8-normal`, `u16-joints` |
| Topology | 7 | `mode-points`, `mode-lines`, `mode-line-loop`, `mode-line-strip`, `mode-triangles`, `mode-triangle-strip`, `mode-triangle-fan` |
| Transforms | 17 | the `xf-*` ladder in §11.4 |
| Normals / tangents | 6 | `tangent-authored`, `tangent-handedness`, `tangent-absent-generated`, `normal-absent`, `normal-nonuniform-scale`, `tangent-mirrored` |
| UV / textures / samplers | 10 | `uv0-checker`, `uv1-material`, `uv-out-of-range-clamp`, `uv-out-of-range-wrap`, `uv-out-of-range-mirror`, `sampler-nearest`, `sampler-trilinear`, `texture-transform-basecolor`, `texture-transform-per-map`, `texture-shared-two-samplers` |
| Materials / PBR | 12 | `mat-default` (no material), `mat-factor-only-gold`, `mat-basecolor-factor-times-texture`, `mat-metallic-roughness-channels`, `mat-normal-occlusion-scale`, `mat-emissive-factor`, `mat-emissive-strength`, `mat-vertex-color-pbr`, `alpha-opaque`, `alpha-mask`, `alpha-blend`, `double-sided` |
| Skinning | 16 | the `skin-*` ladder in §15.4, which `GLTF-095` extends with `skin-eight-influences` (two `JOINTS_n`/`WEIGHTS_n` sets — no ladder foresaw one, and the truncation decision needs an asset that authors more than four influences to mean anything) |
| Morph | 13 | the `morph-*` ladder in §16.3, which **owns** `morph-weights-animated-linear/step/cubic` and `morph-plus-skin` |
| Animation | 11 | the `anim-*` ladder in §17.2, excluding the `anim-weights-*` fixtures owned by the morph group, plus `morph-node-weights-override` (the node/mesh weight-override rule, `GLTF-281`/`GLTF-282`; it lives here rather than in the morph group because what it asserts is the *override and per-instance* rules, not morph blending) |
| Scenes | 3 | `scene-default-selection`, `scene-two-roots`, `scene-no-scenes` |
| Cameras / lights | 5 | `camera-perspective`, `camera-perspective-infinite`, `camera-orthographic`, `camera-animated-node`, `lights-punctual-three` |
| Draco parity | 4 | `draco-triangle`, `draco-vs-uncompressed-pair`, `draco-skinned`, `draco-morph` |
| Robustness / malformed | 8 | `bad-accessor-out-of-bounds`, `accessor-count-mismatch` (moved here from the accessor group, whose 14 it was planned in — it is a malformed-input fixture by construction and the robustness group is where the rejection tests look), `bad-index-out-of-range`, `bad-buffer-truncated`, `bad-glb-chunk-length`, `bad-matrix-and-trs`, `bad-version-1.0`, `skin-joint-index-out-of-range`, `skin-joint-index-padding` (`GLTF-254`'s conditional refusal needs an asset on each side of the condition) |

**Total: 8 + 13 + 8 + 7 + 17 + 6 + 10 + 12 + 16 + 13 + 11 + 3 + 5 + 4 + 8 = 141 distinct assets.**

> **Materials group, twice adjusted (`GLTF-241`, `GLTF-224`/`GLTF-225`).** `mat-vertex-color-pbr`
> was added (12 → 13) because no planned fixture carried `COLOR_0` together with a PBR material.
> The separately-planned `mat-normal-scale` and `mat-occlusion-strength` then became the single
> `mat-normal-occlusion-scale` (13 → 12): neither scalar needs a texture to be asserted, so two
> assets would have differed only in which one they authored — while **one** asset authoring both
> to *different* values (0.35 and 0.8) additionally catches a swap, which neither of the two could.
> The "declared vs absent" half is covered by `mat-factor-only-gold`, which declares neither, and
> that is the case that caught cgltf's zeroed-view trap.

> The former combined "Scenes / cameras / lights" row was split in two by `GLTF-317`, which gave
> the camera fixtures their own owning group: scene *selection* (§3.5) and cameras (§3.10) are
> different concerns and the generator groups by concern. **No asset moved and the total is
> unchanged** — 3 + 4 is the 7 that row carried.

Reuse happens along two axes and neither changes that total. An asset is reused **across oracle
layers** — the same `alpha-mask` file is an L3, L6 and L7 fixture — and **across phases**, where a
task in another group consumes an asset owned elsewhere (§24.1). Both are recorded in the manifest
as `validatedLayers[]` and `referencingGroups[]`; only `owningGroup` counts.

Fourteen assets (`f1`…`f14`) already exist as this audit's throwaway fixtures and are promoted, not
re-invented: `xf-shared-mesh` (f1), `xf-parent-child` (f2), `sparse-indices` (f3),
`mode-triangle-strip` (f4), `interleaved-position-normal` (f5), `sparse-position` (f6),
`anim-rigid-node` (f7), `mat-factor-only-gold` (f8), `skin-armature-ancestor` (f9),
`normalized-u8-color` (f10), `u8-idx` (f11), `mode-points` (f12), `xf-matrix-node` (f13),
`scene-default-selection` (f14).

### 24.3 Existing tracked glTF assets

`git ls-files` finds **no** tracked `.gltf`/`.glb` binaries in CNA. Every current glTF test builds
its fixture as an inline JSON string literal inside the test `.cpp` (e.g.
`GltfToCnjToolTests.cpp` embeds ~30 of them). That is why the corpus is a *new* deliverable rather
than a curation of existing assets. `cna-gltf-viewer` tracks no assets either
(`tests/VerifyConversion.cmake` generates a minimal triangle at test time).

Consequence for licensing: **the campaign introduces no third-party asset without review.**
`GLTF-018` defines the review procedure (per-asset licence + attribution in
`THIRD_PARTY_NOTICES.md`), and `GLTF-019` chooses between committing a small curated subset of
Khronos sample assets and fetching them on demand in a developer-only script. Every synthetic fixture
is CNA-authored and MS-PL, so the core corpus carries no third-party obligation at all.

---

## 25. Public API Compatibility

The default is **no new public glTF API**. Almost every fix belongs inside
`CNA::Internal::GltfImport`, `ContentManager`, or `modules/graphics` internals. Where new surface is
genuinely required, it follows CNA's established CNAEXT conventions (`Tag`-attached data, mirroring
`SkinningData` and `MorphTargetDataEXT`) rather than inventing new XNA-shaped types.

| Proposed surface | Current problem | Shape | Compatibility | Migration | Test |
|---|---|---|---|---|---|
| `.cnj` `"bones"` + per-mesh `"parentBone"` | node hierarchy has nowhere to live | additive JSON fields, `cnjVersion` bumped | old `.cnj` files still load (fields optional) | none — regenerate assets | `GLTF-129` |
| `ModelMeshPart` primitive type | topology cannot be expressed | real XNA `PrimitiveType` on the part (XNA already has one on `GraphicsDevice::DrawIndexedPrimitives`) | additive | none | `GLTF-073` |
| `PbrEffect::NormalScale`, `OcclusionStrength` | glTF fields have no home | new properties on an existing CNAEXT effect | additive | none | `GLTF-224`/`GLTF-225` |
| `PbrEffect` alpha-mode state | `alphaMode`/`alphaCutoff` have no home | CNAEXT `AlphaModeEXT` enum + `AlphaCutoff` (already sketched in `CNAEXT.md` as `PbrMaterial`) | additive | none | `GLTF-228` |
| Per-part sampler state | glTF samplers have no home | CNAEXT `SamplerStateArrayEXT` attached to the part, XNA has no equivalent | additive | none | `GLTF-207` |
| `Model` imported-camera list | glTF cameras have no home | CNAEXT `GltfCameraEXT` list in owned resources | additive | none | `GLTF-320` |
| Import diagnostics | losses are silent | CNAEXT `GltfImportReportEXT` (warnings + dropped-feature counts) returned alongside the model | additive | none | `GLTF-034` |
| `SkinnedEffect::MaxBones` above 72 | large rigs truncate | **investigate only** — raising it changes a real XNA constant and every renderer's uniform array | breaking if changed | decide in `GLTF-261` | `GLTF-261` |

Every one of these is gated behind `GLTF-025` (an explicit API-change review checkpoint) and must be
reflected in `CNAEXT.md` by the Phase 23 documentation tasks (`GLTF-448`, `GLTF-456`) — **which this
campaign performs only in its implementation phase, not now.**

---

## 26. Performance / Lifetime

Correctness first. These are recorded so they are not rediscovered as "bugs", and are scheduled last.

| Hazard | Location | Impact | Task |
|---|---|---|---|
| Whole-file re-parse per `Load<Model>` | `ReadGltfModel` | a GLB is parsed and its buffers loaded on every call; no `cgltf_data` cache | `GLTF-433` |
| Offline path re-parses per mesh group | `Convert()` → `ConvertGroup()` per group | acceptable (CLI), documented | `GLTF-434` |
| Every accessor fully unpacked to `float` | `UnpackAccessor` | a `u16` position stream is expanded 2× in RAM before repacking | `GLTF-435` |
| Full vertex-buffer re-upload per morph weight change | `SetMorphWeightsEXT` | O(vertices) per frame for an animated morph | `GLTF-441` |
| `MorphTargetDataEXT::BaseVertexBytes` duplicates the whole VB in RAM | `MorphTargetEXT.hpp` | 2× memory for every morphed mesh | `GLTF-442` |
| Texture decoded once per `cgltf_image*` per load | `textureCache` | correct within a load; no cross-load cache | `GLTF-437` |
| Occlusion image decoded, halved and re-encoded to PNG, then decoded again | `RemapOcclusionImageForDualTextureEXT` | three full codec passes per occlusion texture | `GLTF-443` |
| Shared mesh duplicated per instance under transform-baking (Option B) | §11.5 | N× geometry memory — a direct argument for Option A | `GLTF-103` |
| `Model::sharedDrawBoneMatrices_` is a **static** vector | `Model.cpp` | not thread-safe; two `Model::Draw` calls on different threads race | `GLTF-444` |
| Shader recompilation | per-renderer `Ensure*Program` | already cached per program; no glTF-specific hazard | ✅ |

---

## 27. Milestones

### 27.1 `GLTF CORE 2.0 CORRECT`

CNA may claim trustworthy core glTF 2.0 support when **all** of the following hold, each backed by
permanent automated tests at the stated oracle layer:

| # | Requirement | Layer |
|---|---|---|
| 1 | `.gltf` and `.glb` load equivalently; GLB chunk layout, padding and the `BIN` chunk are validated | L1 |
| 2 | `buffer`s resolve from external files, base64 `data:` URIs and the GLB `BIN` chunk; `byteLength` is enforced | L1 |
| 3 | Every `bufferView` offset/length/stride and `accessor` offset/count/type/componentType/normalized combination in §8.3 decodes exactly, with bounds and alignment enforced | L2 |
| 4 | `accessor.sparse` is correct for **attributes and indices** | L2 |
| 5 | All seven primitive `mode`s are either supported or explicitly rejected — **never silently reinterpreted** | L3 |
| 6 | Indices decode exactly for `UNSIGNED_BYTE`/`SHORT`/`INT`, with range validation; non-indexed primitives work | L3 |
| 7 | `POSITION`, `NORMAL`, `TANGENT`, `TEXCOORD_0/1`, `COLOR_0`, `JOINTS_0`, `WEIGHTS_0` decode exactly, with correct defaults for absent attributes | L3 |
| 8 | Node `matrix` and TRS both compose correctly through arbitrary hierarchies, including negative scale and mirroring; every fixture in §11.4 passes numerically | L4 |
| 9 | Scenes, default-scene selection, multiple roots, transform-only nodes and **shared meshes instanced by multiple nodes** all import correctly | L4 |
| 10 | Generated vertex/index buffers are byte-exact against goldens for all supported strides; `primitiveCount` is correct per topology | L5 |
| 11 | Images load from all three sources; **samplers** map to `SamplerState`; colour space is correct for all five map roles | L6/L7 |
| 12 | Metallic-roughness PBR is complete: `baseColorFactor`, metallic, roughness, all four maps and their factors, normal scale, occlusion strength, emissive and emissive strength | L6 |
| 13 | `alphaMode` `OPAQUE`/`MASK`/`BLEND` with `alphaCutoff`, and `doubleSided`, are honoured | L6/L7 |
| 14 | Skinning matches glTF's joint-matrix equation exactly, including the full joint ancestor chain and the mesh-node cancellation; every fixture in §15.4 passes numerically | L4 |
| 15 | Morph targets apply position, normal **and** tangent deltas on every stride that has the slot; `mesh.weights` and `node.weights` are both honoured | L4 |
| 16 | `LINEAR`, `STEP` and `CUBICSPLINE` animation of `translation`/`rotation`/`scale`/`weights` is exact at keyframes and at midpoints, for **joint and non-joint nodes alike** | L4 |
| 17 | An unsupported **required** extension produces a clear, deterministic error instead of a silent mis-import | L1 |
| 18 | Every loss the importer does perform is reported through an import diagnostic, never silently | L1 |
| 19 | The whole corpus passes identically on `STUB`, `HEADLESS`, `OPENGLES3` and `VULKAN` at L5 and L6 | L5/L6 |
| 20 | `cna-gltf-viewer` renders the §29 Phase 21 retake matrix correctly against reference output, with **no** compensating logic | L7 |

Explicitly **not** required for this milestone: image-based lighting, tone mapping, transmission,
clearcoat/sheen/volume/iridescence, material variants, Basis/WebP textures, meshopt, GPU instancing,
GPU morphing, >4 skin influences, >72 bones.

### 27.2 `GLTF ROBUST`

| # | Requirement |
|---|---|
| 1 | Draco parity proven at L3 for position/normal/tangent/UV/colour/joints/weights/indices, including skinned and morphed Draco primitives |
| 2 | `EXT_meshopt_compression` supported or explicitly and cleanly rejected |
| 3 | `KHR_materials_unlit`, `KHR_materials_ior`, `KHR_materials_specular`, `KHR_materials_transmission` (with a documented approximation boundary), `KHR_materials_variants` |
| 4 | `KHR_texture_transform` per **map**, and ≥2 real UV channels end-to-end |
| 5 | `KHR_texture_basisu` / `EXT_texture_webp` supported or cleanly rejected — never a silently missing texture |
| 6 | Point and spot lights with real falloff and cone angles, beyond the 3-directional approximation |
| 7 | Imported cameras (perspective incl. infinite far, and orthographic) |
| 8 | Every malformed-input fixture produces a deterministic, actionable error; **zero** ASan/UBSan findings across the whole corpus |
| 9 | Large real-world assets (≥ 50 MB, ≥ 200 k triangles, ≥ 150 joints) load within a stated time and memory budget |
| 10 | Lifetime stress passes: repeated load/unload, `ContentManager::Unload`, simulated device loss, concurrent `Model::Draw` |
| 11 | The performance hazards in §26 are measured and either fixed or documented with numbers |
| 12 | The retake matrix passes on ≥ 4 renderers including one Direct3D path |

---

## 28. Critical Path — DEFORMATION / CENTER-COLLAPSE

### 28.1 Two execution tracks

The 24 phases of §29 express **dependency order, not schedule order**. Read literally as a schedule
they would put the entire Phase 1–4 conformance campaign — container validation, the whole accessor
component-type matrix, vertex-semantics breadth — ahead of Phase 5, and so delay the already-proven
fundamental geometry defect behind work that does not block it. That is the wrong sequencing for the
owner's actual problem.

The campaign therefore runs as two tracks:

| Track | Contents | Entry | Blocks |
|---|---|---|---|
| **TRACK A — P0 CENTER-COLLAPSE** | the 19 tasks in §28.2, each marked `[P0]` in §29 | none beyond its own dependency columns | nothing else may claim a milestone before it completes |
| **TRACK B — FULL CONFORMANCE** | every remaining task, in the Phase 1–23 order of §29 | its own phase entry conditions | Track A tasks are **not** blocked by Track B phase entry conditions |

**Track A's phase entry conditions are waived for its own 19 tasks**, and only for those. A phase
entry condition such as Phase 5's *"Entry: Phase 4"* means "Phase 4 must be complete before the rest
of Phase 5 runs" — it does **not** gate `GLTF-103`, `GLTF-113`, `GLTF-114` or `GLTF-115`, whose real
prerequisites are the dependency columns in §29 and nothing more. The same waiver applies to Phase 2
(`GLTF-041`), Phase 3 (`GLTF-063`, `GLTF-071`) and Phase 12 (`GLTF-245`, `GLTF-247`, `GLTF-248`,
`GLTF-260`).

Track A is **closed under its own dependencies**: the transitive closure of its 19 tasks contains
nothing outside the set. That is a machine-checkable property of the `Deps` columns, and it is why
`GLTF-002` (the spec pin `GLTF-003` needs) and `GLTF-041` (the L2 attribute-decode lock `GLTF-063`
builds on) are members rather than silent external prerequisites.

This is safe because every Track A task's dependency column already names its true prerequisites, and
the audit proved the accessor breadth Track B adds is **not** implicated in any of D1–D5 or D8: the
attribute decode path was numerically verified correct (§1.2, §9.1).

### 28.2 Track A — the P0 sequence

**19 tasks, closed under their own dependencies. Depends on no material, texture, animation or
extension work.** Its output is a written verdict naming the first divergent layer for the owner's
failing asset, with numbers.

```
GLTF-001  record baseline SHA + build the converter               (done in this audit; re-verify)
   │
GLTF-002  pin the glTF 2.0 specification revision
   │
GLTF-003  tools/gltf_fixtures/ generator — asset AND manifest from one source
   │
GLTF-004  promote f1…f14 into the generator with full manifests
   │
GLTF-005  DumpAccessorEXT / DumpMeshOutEXT test-only dumpers      (L2 / L3)
   │
GLTF-006  EvaluateWorldPositionsEXT expected-world-position helper (L4)
   │
GLTF-007  golden vertex/index buffer comparator                   (L5)
   │
GLTF-041  lock the VERIFIED-correct attribute decode path with L2 fixtures
   │       (so index-path work cannot be blamed on, or regress, the attribute path)
   │
   ├── GLTF-063  index decoding: sparse + null-bufferView + range validation   ← D4
   │
   ├── GLTF-071  read prim.type; classify; never reinterpret                   ← D5
   │
   ├── GLTF-103  ADOPT Option A (real ModelBone node hierarchy) unless a
   │   │         concrete blocker is proven                                    ← D1 D2 D3
   │   │         deps: GLTF-004, GLTF-005, GLTF-006   — NOT GLTF-011
   │   └── GLTF-113  MeshGroup → mesh *instances* carrying cgltf_node* + world matrix
   │          └── GLTF-114  world transform reaches ModelBone on BOTH loaders
   │                 └── GLTF-115  xf-* ladder passes at L4
   │                        │
   └── GLTF-245  BuildSkeleton walks the FULL scene ancestry above the joint set ← D8
          │      (skin.skeleton is a hint, never a traversal stop — §15.1.1)
          └── GLTF-247  add the inverse(meshNodeWorld) term to the joint matrix
                 └── GLTF-248  skin-armature-ancestor + skin-mesh-node-transform at L4
                        └── GLTF-260  the node bone Phase 5 gives the skinned mesh node
                                      must NOT re-apply that transform  (needs GLTF-114)
                                      ← D1–D3 remediation changes D8's assumptions
   │
GLTF-011  center-collapse verdict report: for each failing asset, the first
          divergent layer, decoded vs expected positions, generated VB bytes,
          transform matrices, and the owning task ID
          deps: GLTF-007, GLTF-063, GLTF-071, GLTF-115, GLTF-248, GLTF-260
```

`GLTF-260` is inside the critical path, not after it. Fixing D1–D3 gives the skinned mesh's node a
real `ModelBone` for the first time — which is exactly the transform glTF requires to be *cancelled*
for a skinned mesh (§15.1). Without `GLTF-260`, the D1–D3 remediation can silently re-introduce the
mesh-node transform that `GLTF-247` just cancelled, and skinning would be double-transformed rather
than fixed. **Skinning is not complete at `GLTF-248`.**

### 28.3 Track A deliverables, per failing asset

1. a **minimal failing asset** (either a corpus fixture that already reproduces it, or a reduction of
   the real asset);
2. **decoded CPU positions** (L3) vs **expected CPU positions**;
3. **expected world-space positions** (L4) vs what CNA produces;
4. the **generated vertex buffer bytes** (L5);
5. every **transform matrix** in the chain, printed;
6. the **first divergent layer** and the single owning task ID.

Track B's Phases 8–23 do not begin before `GLTF-011` is written. Track B's Phases 1–4 and 6–7 may
proceed in parallel with Track A, since neither blocks the other.

---

## 29. Complete glTF Task Backlog

**460 tasks, `GLTF-001` … `GLTF-460`, across 24 dependency-ordered phases (Phase 0 … Phase 23).**

Status legend — `⬜ TODO` (new work) · `🐛 CONFIRMED` (defect proven during this audit) ·
`🔬 INVESTIGATE` (unknown; the task's first job is to establish the fact) ·
`✅ VERIFIED` (prerequisite proven correct during this audit; the task only locks it with a test) ·
`✔ DONE` (implemented and verified against the task's own acceptance criteria; the row names the
closing commit) · `⛔ BLOCKED` (design settled, but the environment cannot execute the acceptance
criterion; the row states exactly what is missing and what would unblock it).

**Implementation progress.** **The P0 center-collapse track (§28) is complete** — five batches,
`GLTF-001` … `GLTF-011`, closed by the verdict report in
**`docs/gltf-center-collapse-verdict.md`**, which is the document to read first. See also
`docs/gltf-conformance.md` for the harness itself.

**P0-A — oracle foundation**: `GLTF-001` … `GLTF-006` and `GLTF-041`. It established the
specification pin, the permanent fixture generator, the promoted `f1`…`f14` corpus, the L2/L3/L4
numerical oracles, and the executable D1–D8 known-defect ledger. **No D1–D8 production fix was
implemented** — the defects remained reproducible by design.

**P0-B — independent geometry corruption**: `GLTF-007`, `GLTF-063`, `GLTF-071`. The first batch that
changes glTF behaviour.

* `GLTF-063` (`ef3bc2cbf`) replaced `cgltf_accessor_read_index` with a CNA-side sparse-aware,
  bounds-checked index reader and added the `index < POSITION.count` validation. **D4 is fixed**:
  `sparse-indices` decodes `[0,1,2,0,2,3]` at L2, L3 and byte-exact at L5, from the unchanged
  fixture and the unchanged expectation.
* `GLTF-071` (`07312274e`) reads and classifies `mesh.primitive.mode`, carries it on `MeshOut`, and
  rejects every topology CNA cannot yet honour with its mode named. **D5 is partially remediated**:
  the silent triangle-list reinterpretation is gone, but `GLTF-072` still owns the conversions, so
  `mode-triangle-strip` and `mode-points` do not import at all today. **`GLTF-072` is not started.**
* `GLTF-007` (`414686088`) added the permanent L5 byte-level VB/IB oracle and the golden buffers for
  13 of the 15 fixtures.

The defect ledger gained a three-state lifecycle (`known-failing` → `partially-remediated` →
`fixed`) with per-layer divergent fields, and a defect record is now never deleted: a remediated one
stays as the regression witness with its original measurement under `priorActual`.

**P0-C — the node hierarchy**: `GLTF-103`, `GLTF-113`, `GLTF-114`, `GLTF-115`, `GLTF-129`,
`GLTF-130`, `GLTF-252` (`d344718`, `e063864`). Option A adopted with no blocker found:
`BuildSceneGraph` flattens the default scene parent-before-child with composed world transforms and
both loaders mirror it as real `ModelBone`s. **D1, D2 and D3 are fixed.** Vertex positions stay
mesh-local, so instancing survives — `xf-shared-mesh`'s two placements share one mesh.

**P0-D — the skin coordinate spaces**: `GLTF-245`, `GLTF-247`, `GLTF-248`, `GLTF-260` (`e9336a1`).
**D8 is fixed.** `BuildSkeleton` walks the full scene ancestry (`skin.skeleton` is a hint, never a
traversal stop) and the `inverse(globalTransform(meshNode))` term cancels the skinned mesh node's
placement — both carried on a per-root prefix rather than baked into the bind pose, so an animated
root joint cannot undo them. `skin-armature-ancestor`'s joint matrix went from `translate(0,−100,0)`
to exactly identity; the new `skin-mesh-node-transform` proves the cancellation happens **exactly
once** (`T(0,0,−50)`, distinguishable from both `0` and `−100`).

**P0-E — the verdict**: `GLTF-011`. `docs/gltf-center-collapse-verdict.md`. Names both collapse
mechanisms, their first divergent layer, their before/after numbers and their locking fixtures; and
records that P0-D corrected the **L4 oracle itself** — both oracles placed a skinned mesh by its own
node, which §3.7.3 says the joints do — against the specification rather than the implementation, so
the expectation never became a golden bug.

**P0-F — topology conversion**: `GLTF-072`, `GLTF-074`, `GLTF-075`. The last geometric defect from
the P0 audit. All seven `mode-*` fixtures exist and decode to their manifest lists; a
`TRIANGLE_STRIP` and a `TRIANGLE_FAN` are converted to equivalent triangle lists at import with
winding preserved, so no renderer changed. The four topologies that describe no triangles stay
rejected pending a draw path (`GLTF-073`/`GLTF-077`/`GLTF-078`), which is why D5 remains
`partially-remediated`.

Landed alongside it: `tools/gltf_fixtures/builder.py`, the corpus generator's core module, which
`.gitignore`'s unanchored `build*` rule had silently swallowed — the generator did not run on a
clean checkout at all, so `GLTF-003`'s byte-identical-regeneration guarantee was unverifiable.
Reconstructed against the committed corpus (which carries a SHA-256 per file), with a test that
fails if any generator module goes missing again.

**P0-G — rigid node animation**: `GLTF-293`. `ExtractSceneNodeClips` resolves animation channels
against the scene graph rather than a skin's joint set, so `anim-rigid-node` produces a real clip on
the animated node's own bone. Both mechanisms that dropped it are gone; what remains is
serialisation, which `GLTF-294` owns, so **D6 is `partially-remediated`** rather than fixed and the
converter reports the clip instead of discarding it.

**P0-H — the PBR selection rule**: `GLTF-215`, `GLTF-216`, `GLTF-217`, `GLTF-219`, `GLTF-221`.
What selects PBR is now the material **model** the file declares, not which texture maps it
happens to carry, so a factor-only metallic-roughness material reaches `PbrEffect` and every
authored factor — base colour, metallic, roughness, emissive — survives to the effect. Stride 32
no longer occurs in the corpus, and the `DualTextureEffect` glTF path is superseded by `PbrEffect`'s
real occlusion map. **D7 is `partially-remediated`**: the alpha and sidedness state (`alphaMode`,
`alphaCutoff`, `doubleSided`) still has no `MeshOut` field and no effect parameter, and is owned by
`GLTF-228`/`GLTF-229`/`GLTF-231` behind the `GLTF-025` API-change gate.

**P0-I — container validation**: `GLTF-021`, `GLTF-022`, `GLTF-023`, `GLTF-024`. `cgltf_validate()`
had zero occurrences in production code and `extensionsRequired` was never read, so a malformed file
decoded whatever was adjacent in memory and a file declaring an unimplemented required extension
imported silently and wrongly. Both now reject with a named cause; an unimplemented *used* extension
warns instead, which is the one genuinely advisory case. This also unblocks `GLTF-025`.

**P0-J — the line and point draw path**: `GLTF-073`, `GLTF-076`, `GLTF-077`, `GLTF-078`.
**D5 is `fixed`.** All seven glTF modes import: the three triangle modes as a triangle list
(converted where needed, winding preserved), a `LINE_LOOP` as a `LINE_STRIP` carrying the closing
segment glTF leaves implicit in the mode, and the rest as themselves — each reaching its
`ModelMeshPart` with a real `PrimitiveType` and a §12.3 primitive count.

**P0-K — the material's alpha and sidedness state**: `GLTF-228`, `GLTF-229`, `GLTF-231`.
**D7 is `fixed`** — not one authored material property is lost any more. One boundary is
deliberate and recorded: `doubleSided` is *carried*, not *applied*; `GLTF-230` owns making culling
follow it, at L7.

**P0-L — rigid clip serialisation and playback**: `GLTF-294`. **D6 is `fixed`.**

**All eight defects from the forensic audit are now `fixed`.** `GltfKnownDefectTests.cpp` contains
no known-defect tests at all — only the ledger test that asserts the bookkeeping in both directions,
which is what will catch the next defect being recorded without one. Every remediated defect keeps
its record as a regression witness with the audit's original measurement under `priorActual`. No renderer and
no `cna-gltf-viewer` work has been started.

**Lighting fallback policy (CNAEXT), decided with `GLTF-215`.** Sending every untextured primitive
through `PbrEffect` exposed a gap the old rule hid: `PbrEffect::AmbientLightColor` defaults to
`(0,0,0)` with all three directional slots disabled, and glTF does not require a scene to declare
any light — most authored assets do not, because lighting is normally the viewer's business. A
light-less file would therefore render black: a faithful reading of "no lights" and a useless one
for anybody importing a model to look at it.

The policy: **a file that declares no light at all gets the effect's own `EnableDefaultLighting()`
rig** — the same three-light arrangement real XNA applies and that `BasicEffect` users already
expect. It is one-sided by construction (`if (lights.empty())`), so it can never override or dim an
asset that authored its own lights. `GltfLightingPolicy.AFileThatDeclaresNoLightGetsTheDefaultLightingRig`
pins it; the authored-lights half is covered by the existing `KHR_lights_punctual` tool test, which
needs a 3D renderer. This is a CNA import decision, not a specification rule, and is recorded here
so it reads as a choice rather than an accident.

Every phase declares its **primary owner** from §6; a task whose owner differs names it inline.
Dependencies are the *minimum* set — a task also inherits its phase's entry condition, **except for
the 19 Track A (P0 center-collapse) tasks listed in §28.2, whose phase entry conditions are waived**;
for those the dependency column is the complete prerequisite set.

The dependency graph over all 460 tasks is **acyclic** and is machine-checkable from the `Deps`
column of these tables.

**Track A tasks are marked `[P0]` in their title.**

---

### Phase 0 — Baseline, oracle harness, corpus generator · owner: `GLTF-CONTAINER` / tooling
*Entry: none. Exit: the seven-layer harness runs green on the promoted `f1…f14` fixtures. `GLTF-001`
… `GLTF-007` are Track A's entry condition (§28.1); `GLTF-011` is Track A's terminus.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-001 | **[P0]** Record the campaign baseline and reproduce the converter build | ✔ | — | Baseline `fb37282`; `cmake -DCNA_GRAPHICS_RENDERER=STUB -DCNA_BUILD_TESTS=OFF -DCNA_BUILD_EXAMPLES=OFF -DCNA_ENABLE_NET=OFF` then `--target cna_tool_gltf_to_cnj` → 525/525, exit 0. **Accept:** a documented one-command reproduction that a fresh session can re-run. **Done** (`f4c3f6cb6`): re-verified on this branch — configure exit 0, 525 build edges, tool runs. Recorded in `docs/gltf-conformance.md` §1 together with the separate test-enabled configuration the conformance suites need. |
| GLTF-002 | **[P0]** Pin the glTF 2.0 specification revision used by this campaign | ✔ | — | Record the Khronos registry snapshot date/hash in `docs/gltf-conformance.md`. **Accept:** every later task's spec citation resolves against that pin. **Done** (`f4c3f6cb6`): `KhronosGroup/glTF@2b29723d025a995971726f2989697cdc49b1222a`, `specification/2.0/Specification.adoc`, SHA-256 `55986799907693d3f51b0a474497852c0d6318b85084811fdc05ff0db4b27967`. `docs/gltf-conformance.md` §2 carries the re-verification command, the resolved section/anchor map, and corrections for the four §7.3 citations whose section numbers do not resolve against the pin. |
| GLTF-003 | **[P0]** Create `tools/gltf_fixtures/` — one generator emitting asset **and** manifest | ✔ | GLTF-002 | Python generator, `.gltf` + `.glb` twin + `<name>.expected.json` per fixture, from one source of truth. Emits the §24.1 inventory record per asset (`id`, `owningGroup`, `referencingGroups[]`, `validatedLayers[]`, `features[]`). **Accept:** `python -m gltf_fixtures --out <dir>` regenerates the whole corpus byte-identically and emits a manifest whose distinct-asset count is machine-readable. **Done** (`35ddc1ce5`): `tools/gltf_fixtures/`, stdlib-only Python. Regeneration is byte-identical across independent runs; `manifest.json` carries the machine-readable distinct-asset count, the per-group counts and a SHA-256 per emitted file. |
| GLTF-004 | **[P0]** Promote `f1…f14` into the generator with full manifests | ✔ | GLTF-003 | The 14 audit fixtures in §1.1/§1.2, each with its expected L3/L4 values. **Accept:** 8 of 14 fail against current CNA with exactly the deltas in §1.1; 6 pass. **Done** (`35ddc1ce5`, `b681f1327`): 15 assets (the 14 promoted plus `xf-identity`). All eight defects reproduce exactly as §1.1 recorded; the five verified-correct fixtures pass. The split is **9 fixtures failing, 5 passing**, not the 8/6 written above: D5 owns two fixtures (`f4` `mode-triangle-strip` and `f12` `mode-points`), so the eight *defects* of §1.1 are exposed by nine *fixtures*, and §1.2 lists five verified-correct fixtures (`f5` `f6` `f10` `f11` `f14`) — the sixth §1.2 row, base64 `data:` URI buffers, is a property of all fourteen rather than a fixture of its own. One sharpening: `mat-factor-only-gold` now authors non-default metallic/roughness/emissive factors, which showed those are lost too — `ExtractMesh` assigns them only inside its `usePbr` guard, confirming §1.1's "zero material fields emitted" precisely. |
| GLTF-005 | **[P0]** `DumpAccessorEXT` / `DumpMeshOutEXT` test-only dumpers (L2/L3) | ✔ | GLTF-003 | Test-scope helpers serialising decoded accessor arrays and every `MeshOut` field to JSON. Not public API. **Accept:** dumps round-trip and diff cleanly against a manifest. **Done** (`195bda140`): `modules/content/tests/CNA/Internal/GltfImport/GltfOracleEXT.{hpp,cpp}`, namespace `CnaTest::GltfOracle`. Dumps round-trip and are byte-stable; a single-component perturbation of an L2 or L3 manifest value fails with the field and component index named. |
| GLTF-006 | **[P0]** `EvaluateWorldPositionsEXT` expected-world-position helper (L4) | ✔ | GLTF-005 | Composes node/skin/morph state into world-space vertex positions for comparison. **Accept:** returns the manifest values for `xf-identity`. **Done** (`195bda140`): returns the manifest values for `xf-identity`, cross-checks its own composition against `cgltf_node_transform_world` on every fixture, and is proved not to alter production output. Exposes the D1–D3 divergence without fixing it. |
| GLTF-007 | **[P0]** Golden vertex/index buffer comparator (L5) | ✔ | GLTF-005 | `memcmp` against `<name>.vb.bin`/`.ib.bin` with a readable first-difference report (offset, field, expected, actual). **Accept:** a one-byte perturbation is reported at the right offset and field name. **Done** (`414686088`): `GltfBufferOracleEXT.{hpp,cpp}` + `tools/gltf_fixtures/l5.py`. A perturbed byte is reported as `<fixture> VB differs at byte 45 (vertex 1, Normal +1)`; every byte of a vertex is swept and must resolve to the field whose span contains it. 13 of 15 fixtures carry goldens (strides 32/24/52, 16-bit index path); the two `GLTF-071` rejects record `l5.supported = false` naming `GLTF-072`. The stride ABI is stated independently in the generator and in C++ and a test asserts they agree. **Scope held:** the PBR/dual-texture strides (20/48/68) raise an explicit "not implemented" in the packer rather than emitting an unchecked golden (`GLTF-149`+), and `primitiveCount` is asserted for `TRIANGLES` only (`GLTF-078`). See `docs/gltf-conformance.md` §4. |
| GLTF-008 | `GpuDrawParams` capture harness on `HEADLESS`/`STUB` (L6) | ✔ | GLTF-005 | Record every effect parameter actually bound for each draw. **Accept:** a `PbrEffect` draw yields all 12 §21.1 quantities. **Landed:** `CaptureDrawParamsEXT` (`GltfDrawParamsOracleEXT.{hpp,cpp}`, test scope) binds `absoluteBoneTransform * world`, view and projection exactly as `Model::Draw` does, then calls the **same** virtual `Effect::FillGpuDrawParams()` `GraphicsDevice::DrawIndexedPrimitives` calls — no renderer, no reimplemented effect. Every §21.1 row whose "Assert at" column says L6 is asserted against a value another layer established independently: the world matrix against the manifest's **L4** `worldMatrixColumnMajor`, every material factor against its **L3** `material` block, the bone palette against `AnimationPlayer::GetSkinTransforms()`. Two rows are honest boundaries rather than coverage, both recorded in `docs/gltf-conformance.md` §5.2: tangent handedness is a vertex-stream fact with no effect parameter (L5 owns it), and the alpha state is *carried, not applied*, so the capture records both halves and a dedicated test pins the gap so `GLTF-230` cannot close it silently. The texture rows are captured as **bindings** — which slot is filled — since channel semantics are L3/L7. |
| GLTF-009 | L7 image-oracle harness for the corpus | ⛔ | GLTF-008 | Reuse `examples/golden/` + xvfb; fixed camera/light rig per fixture; per-renderer tolerance. **Accept:** deterministic PNGs across two runs on `OPENGLES3`. **Blocked on the renderer, not on design** (`docs/gltf-conformance.md` §5.3): the conformance suite runs on `STUB`, which has no 3D pipeline — the same capability gap that already fails the repository's `GraphicsDeviceCapability`/`TextureCube` suites and the reason `ModelMesh::Draw` has an `Ensure3DSupported()` gate. No image can be produced, and a golden captured from a renderer that draws nothing would be a golden bug of exactly the kind the verdict report §5 warns about. Everything below L7 is renderer-independent by construction and is unaffected. |
| GLTF-010 | Wire L1–L7 into one `ctest` label `gltf-conformance` | ✔ | GLTF-009 | **Accept:** `ctest -L gltf-conformance` runs the whole ladder and names the failing layer. **Landed** for every rung that exists (L1–L6 plus an L0 corpus rung, the defect ledger and the offline `.cnj` converter); the L7 entry appears when `GLTF-009` unblocks, and the label's shape already accommodates it. Each rung is its **own** CTest entry, registered lowest-first, so CTest's own result line names the divergent layer — `CnaGltfConformanceL4 ... Failed` — without reading a log, and higher rungs still run so a single invocation shows how far the damage reached (`GLTF-402`). The rung list lives once, in `cmake/UnitTests.cmake`; `GltfConformanceLadder` parses that exact list and asserts the partition is total **both** ways, so a new `Gltf*` suite matching no rung fails the run instead of quietly sitting outside the label, and a rung naming a deleted suite fails instead of quietly running zero tests. |
| GLTF-011 | **[P0] Write the center-collapse verdict report** | ✔ | GLTF-007, GLTF-063, GLTF-071, GLTF-115, GLTF-248, GLTF-260 | The Track A terminus (§28.2). For every asset the owner reports as deformed: minimal reproducer, decoded vs expected positions, generated VB bytes, transform chain, **first divergent layer**, owning task ID. Depends on the fixes, **never the other way round** — `GLTF-103` must not depend on this task. **Accept:** `docs/gltf-center-collapse-verdict.md` exists and every listed asset has a named owning task. **Landed:** `docs/gltf-center-collapse-verdict.md`. Two independent collapse mechanisms named, both first divergent at **L4** and both closed: node transforms discarded (`GLTF-113` under `GLTF-103`, locked by `GLTF-115`) and the skin ancestry/mesh-space asymmetry (`GLTF-245`+`GLTF-247`, closed by `GLTF-260`). D4 fixed at L3; D5 partially remediated with `GLTF-072` still owning conversion; D6 and D7 recorded as open and explicitly **not** collapse mechanisms. Records three things beyond the acceptance criteria: that L5 is byte-identical across the D1–D3 fix (so a byte oracle alone could never have found the collapse), that the **L4 oracle itself was wrong** and was corrected against the specification rather than the implementation (§5 — the golden-bug hazard), and the §24.2 count correction below. |
| GLTF-012 | Stand up a `known_bugs.md` glTF section | ⬜ | GLTF-004 | `known_bugs.md` currently has **zero** glTF entries. Add D1–D8 with their fixtures. **Accept:** each of D1–D8 has an entry pointing at its fixture and task. |
| GLTF-013 | Pin `glTF-Sample-Assets` for reference use | ⬜ | GLTF-002 | Commit pin only; no assets committed yet. **Accept:** pin recorded with its licence summary. |
| GLTF-014 | Pin `glTF-Asset-Generator` and map its manifest to the corpus | ⬜ | GLTF-013 | **Accept:** the permutation manifest is machine-readable by the corpus runner. |
| GLTF-015 | Integrate `glTF-Validator` into the generator | ⬜ | GLTF-003 | Every emitted fixture is validated at generation time. **Accept:** a deliberately malformed fixture fails generation unless it lives in the `bad-*` group. |
| GLTF-016 | Pin the Khronos reference renderer for L7 comparison | ⬜ | GLTF-009 | Development/reference only — **never** a CNA runtime dependency. **Accept:** documented capture procedure and version pin. |
| GLTF-017 | Prove the harness is renderer-independent at L1–L5 | ⬜ | GLTF-010 | Run L1–L5 on `STUB` and `HEADLESS`; results must be identical. **Accept:** byte-identical L2–L5 output. |
| GLTF-018 | Third-party asset licence review procedure | ⬜ | GLTF-013 | Per-asset licence + attribution in `THIRD_PARTY_NOTICES.md` before any commit. **Accept:** written procedure; **no asset committed without it.** |
| GLTF-019 | Decide: commit a curated Khronos subset vs fetch-on-demand | ⬜ | GLTF-018 | Weigh repository size against CI reproducibility. **Accept:** decision recorded with rationale. |
| GLTF-020 | One reproducible `regenerate-gltf-goldens` script | ⬜ | GLTF-007, GLTF-009 | **Accept:** running it on an unchanged tree produces a zero diff. |

---

### Phase 1 — Container, buffers, URIs, structural validation · owner: `GLTF-CONTAINER` / `GLTF-BUFFER`
*Entry: Phase 0. Exit: every malformed-input fixture yields a deterministic, actionable error.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-021 | Call `cgltf_validate()` on both load paths | ✔ | GLTF-010 | **0 occurrences** in production code today. **Accept:** validation runs and its failure is surfaced as `ContentLoadException` / a CLI error naming the violated constraint. **Landed:** `ValidateGltfEXT` runs on both load paths, immediately after `cgltf_load_buffers` — deliberately after, because the sparse index-bound check needs buffer data to run at all. A failure surfaces as `ContentLoadException` on the runtime path and a named CLI error on the offline one, saying which class of constraint was violated rather than only a numeric code. `bad-accessor-out-of-bounds` proves it fires: a POSITION accessor reaching 12 bytes past its own `bufferView`. |
| GLTF-022 | Decide the policy for validation failures (reject vs warn) | ✔ | GLTF-021 | Some real assets fail strict validation. **Accept:** a documented severity table; hard rejection only for constraints that make decoding unsafe. **Decided, from what cgltf actually checks rather than from taste.** Its entire check set is decoding-safety constraints: an accessor past its `bufferView`, a `bufferView` past its buffer, a sparse index outside the accessor's range, attribute counts disagreeing within a primitive, an undefined component or primitive type. There is no cosmetic violation in that set to downgrade, so the severity table has one row: **`cgltf_validate` failure rejects**. The genuinely advisory case is a different check — an unimplemented `extensionsUsed` entry — and that warns (`GLTF-024`), which is what makes the distinction real rather than nominal. |
| GLTF-023 | Enforce `extensionsRequired` | ✔ | GLTF-021 | Never checked; an unsupported *required* extension imports silently and wrongly. **Accept:** `gltf-required-extension-unsupported` fixture errors with the extension name. **Landed:** an `extensionsRequired` entry CNA does not implement rejects the import with the extension named. `IsGltfExtensionSupportedEXT` draws the line at **implemented**, not **noticed**: `KHR_materials_unlit` and `KHR_materials_pbrSpecularGlossiness` are both read by `ExtractMesh` to keep such a material off the metallic-roughness path (`GLTF-215`), yet an unlit surface still goes through a lit effect and the specular-glossiness parameters are dropped — so a file *requiring* either is rejected. `KHR_draco_mesh_compression` is supported only in a build with libdraco, so the answer is build-dependent rather than a fixed list. Fixture: `gltf-required-extension-unsupported`. |
| GLTF-024 | Report `extensionsUsed` entries CNA ignores | ✔ | GLTF-023 | **Accept:** the import report lists every ignored extension. **Landed:** every unimplemented `extensionsUsed` entry produces a warning naming it, on both paths — the runtime one through `CNA::Logger::Warn`, the offline one in the tool's own warning list. The import continues, because `extensionsUsed` is advisory by definition; but it is never silent, since "loaded fine" and "loaded as authored" are different claims. |
| GLTF-025 | Public/CNAEXT API-change review checkpoint | ✔ | GLTF-024 | Gate for every §25 row. **Accept:** each proposed member has problem, shape, compatibility, migration and test recorded before implementation. **Landed:** `docs/gltf-api-change-review.md`. Each member is recorded by exact name, type, default and semantics rather than by intent, because that is what a reviewer can disagree with. Nothing in §25 was rejected outright; the gate's effect was on **shape**: the topology property reuses the existing `PrimitiveType` rather than a glTF-flavoured one; `AlphaModeEXT` is declared **once** in `Microsoft::Xna::Framework::Graphics` and `CNAEXT.md` §5.5's separately-sketched `PbrMaterial::AlphaMode` is bound to that same type rather than becoming a second enum with a conversion function; and `doubleSided` is admitted as **carried state only**, with applying it to the rasterizer explicitly assigned to `GLTF-230` — the difference between a flag that works and a flag that merely exists. `GLTF-224`/`GLTF-225`/`GLTF-207`/`GLTF-320`/`GLTF-034` are reviewed and deferred with reasons; `GLTF-261` stays `investigate only` as the one breaking row. |
| GLTF-026 | GLB container fixtures: chunk order, padding, alignment | ⬜ | GLTF-003 | `glb-basic`, `glb-bin-chunk-padding`. **Accept:** L1 assertions on chunk headers; `.glb` and `.gltf` twins produce identical L3 output. |
| GLTF-027 | Malformed GLB: bad magic, bad chunk length, truncated | ⬜ | GLTF-026 | `bad-glb-chunk-length`. **Accept:** deterministic error, no read past the buffer under ASan. |
| GLTF-028 | External `.bin` buffer resolution | ⬜ | GLTF-026 | `gltf-external-bin`. **Accept:** relative URI resolves against the `.gltf` directory; a missing file errors clearly. |
| GLTF-029 | Base64 `data:` URI buffers | ✅ | GLTF-026 | All 14 audit fixtures use them. **Accept:** locked by test, including odd padding lengths. |
| GLTF-030 | Percent-encoded URI handling | ⬜ | GLTF-028 | `cgltf_decode_uri` is used for images; verify for buffers too. **Accept:** `gltf-uri-percent-encoded` loads. |
| GLTF-031 | `buffer.byteLength` enforcement | ⬜ | GLTF-028 | **Accept:** `bad-buffer-truncated` errors instead of reading short. |
| GLTF-032 | Path containment for external URIs | ✔ | GLTF-028 | `gltfDir / uri` with no containment check — `../../..` escapes the asset directory. Security-relevant. **Accept:** a traversal URI is rejected with a clear message. **Landed as `ResolveExternalUriEXT` + `ValidateExternalUriContainmentEXT`.** The sweep runs between `cgltf_parse_file` and `cgltf_load_buffers` on **both** entry points, because that call resolves external buffer URIs itself and offers no hook to veto one — there is nowhere else to stand. Four distinct refusals with four distinct messages (scheme, absolute/drive-absolute, lexical escape, symlink escape); percent-decoding runs *before* containment so `%2e%2e%2f` is seen for what it is; containment is compared component-wise so `/asset-evil` does not read as inside `/asset`. |
| GLTF-033 | Reject non-`2.0` `asset.version` on both paths | ✅ | GLTF-021 | Both paths already check. **Accept:** `bad-version-1.0` errors; locked by test. |
| GLTF-034 | `GltfImportReportEXT` — structured import diagnostics | ⬜ | GLTF-025 | Today losses are silent or printed to stdout by the CLI only. **Accept:** warnings, dropped-feature counts and approximations are reachable programmatically on **both** load paths. |
| GLTF-035 | Route every existing silent drop through the report | ⬜ | GLTF-034 | Non-joint animation channels, >3 lights, ignored UV sets, ignored extensions, unsupported modes. **Accept:** each has a report entry with a fixture. |
| GLTF-036 | Bring the glTF path under ASan+UBSan in CI | ✔ | GLTF-010 | `REMED-NA-016`: UBSan-confirmed misaligned `float` load in `cgltf_component_read_float`, live under `ResolvesSparseAccessorOverride`. **Accept:** the whole corpus runs clean, or every finding is triaged with a recorded disposition. **Both: the finding was root-caused and the selection now runs clean.** REMED-NA-016 is neither a cgltf bug nor a CNA decode bug — it is a **malformed fixture CNA accepted**. §3.6.2.4 makes an accessor's effective offset a multiple of its component size; `cgltf_validate` does not check it; cgltf reads a component with a raw `*(const float*)` cast. `kSparseAccessorGltf` put a sparse `VEC3<float>` values bufferView at byte offset **62**, two off a float boundary, so the parser performed a misaligned load. Fixed at both ends: the fixture moved to 64, and `ValidateGltfEXT` now **refuses** any file violating §3.6.2.4 (base accessor, sparse indices, sparse values) or §3.6.2.1's multiple-of-4 `byteStride` — rejection rather than a warning, because CNA cannot make the vendored parser safe on such a file and the alternative (replacing cgltf's element reader) is what `GLTF-041` exists to prevent. `.github/workflows/gltf-sanitizers-ci.yml` keeps it that way: STUB renderer, no display, `--gtest_filter='Gltf*'` plus the conformance label, `detect_leaks=1` and UBSan `exitcode=1`, no known-failure allowlist. Current state: **190 glTF tests, 0 ASan errors, 0 UBSan reports, 0 leaks.** **Out-of-scope findings, recorded rather than silently left behind:** a full unfiltered run of the same binary reports **58 UBSan findings and 7 leak reports, none of them on the glTF path** — signed-integer overflow in `Vector3`/`Vector4`/`Matrix`/`Quaternion` `GetHashCode()` (raw float bit patterns summed as `int`), member access on misaligned/invalid `this` in `gamer-services` and `net`, and a leaked `NetworkSession` in the gamer-services dispatcher harness. They belong to those subsystems' own plans, not this one, and are why the CI job is glTF-scoped: a full-suite sanitizer job could not be green today and would need an allowlist, which this job deliberately does not have. |
| GLTF-037 | Replace CNA-side `reinterpret_cast` on index bytes with `memcpy` | ⬜ | GLTF-036 | `ContentManager.cpp` casts `indexBytes.data()` to `uint32_t*`. **Accept:** no CNA-authored misaligned load remains; behaviour unchanged at L5. |
| GLTF-038 | Decide the vendored-cgltf divergence policy | ⬜ | GLTF-036 | Patching `third_party/cgltf/cgltf.h` diverges from upstream. **Accept:** a written rule (report upstream; patch only on an observed fault), consistent with `REMED-NA-016`'s disposition. |
| GLTF-039 | Overflow-check every offset/count computation | ⬜ | GLTF-021 | `count * stride`, `offset + length` in `size_t`. **Accept:** a fixture with counts near `SIZE_MAX/stride` errors instead of wrapping. |
| GLTF-040 | Fuzz corpus seed for the container/buffer layer | ⬜ | GLTF-036 | Seed from the `bad-*` group. **Accept:** a short fuzz run produces no crash or sanitiser finding. |

---

### Phase 2 — bufferView / accessor correctness · owner: `GLTF-ACCESSOR`
*Entry: Phase 1 — **waived for the Track A task `GLTF-041`** (§28.1). Exit: every §8.3 cell has a
fixture; every address computation is bounds-checked.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-041 | **[P0]** Lock the attribute decode path with L2 fixtures | ✔ | GLTF-005 | `UnpackAccessor` → `cgltf_accessor_unpack_floats` proven correct on `f5`/`f6`/`f10`/`f11`. **Accept:** L2 dumps match manifests; **no rewrite of this path.** **Done** (`8f39e225c`): `GltfAccessorDecodeLockTests.cpp`. Locks `f5`/`f6`/`f9`/`f10`/`f11` at L2 **and** requires the production `ExtractMesh` output to agree with the L2 dump component for component, so a replacement decoder fails even if it satisfied the L2 half alone. No rewrite of this path. |
| GLTF-042 | Bounds-check the effective accessor address | ✔ | GLTF-021 | §8.1 inequality is never enforced. **Accept:** `bad-accessor-out-of-bounds` errors; ASan clean. **Landed with `GLTF-021`:** `ValidateGltfEXT` runs on both load paths and `bad-accessor-out-of-bounds` — a POSITION accessor reaching 12 bytes past its own `bufferView` — is refused with a diagnostic naming the violated constraint, asserted by `GltfContainerValidation`. The **ASan half of the acceptance is not claimed here**: bringing the glTF path under ASan+UBSan is `GLTF-036`'s own task and it records a still-open UBSan finding in `cgltf_component_read_float`. |
| GLTF-043 | Enforce `accessor.byteOffset % componentSize == 0` | ⬜ | GLTF-042 | §3.6.2.4. **Accept:** a misaligned fixture errors. |
| GLTF-044 | Enforce `byteStride` range and multiple-of-4 | ⬜ | GLTF-042 | **Accept:** an out-of-range stride errors. |
| GLTF-045 | `bufferView.byteOffset` fixture | ✅ | GLTF-041 | `f5` uses offset 16. **Accept:** locked at L2. |
| GLTF-046 | `accessor.byteOffset` fixture | ✅ | GLTF-041 | `f5` uses offset 12 for NORMAL. **Accept:** locked at L2. |
| GLTF-047 | Interleaved `byteStride` fixtures | ✅ | GLTF-041 | `interleaved-position-normal`, `interleaved-pos-nrm-uv`, `interleaved-mixed-widths`, `stride-padded`. **Accept:** every effective attribute address proven at L2. |
| GLTF-048 | Two primitives sharing one bufferView | ⬜ | GLTF-047 | `two-primitives-one-buffer`. **Accept:** independent decode, no aliasing. |
| GLTF-049 | Component-count mismatch is an error, not a silent misread | ✔ | GLTF-041 | `UnpackAccessor` throws. **Accept:** locked by test with the exact message. **Locked with the message, not just the throw** — a `VEC2` `POSITION` is refused naming the attribute, the count it declares and the count expected, because "invalid accessor" tells an author nothing about which one to fix. Without the check, a vector sized for three components is filled from a stream of two and every vertex after the first reads from the wrong offset — `D4`'s class of silent misread, from the other direction. |
| GLTF-050 | `BYTE` (5120) fixtures, normalized and not | ⬜ | GLTF-041 | **Accept:** `−128 → −1.0` exactly (the `max(v/127,−1)` rule). |
| GLTF-051 | `SHORT` (5122) fixtures | ⬜ | GLTF-041 | **Accept:** endpoint values exact. |
| GLTF-052 | `UNSIGNED_BYTE` (5121) attribute fixtures | ✅ | GLTF-041 | `f10`. **Accept:** locked. |
| GLTF-053 | `UNSIGNED_SHORT` (5123) attribute fixtures | ⬜ | GLTF-041 | `u16-joints`, `normalized-u16-color`. **Accept:** exact. |
| GLTF-054 | `UNSIGNED_INT` (5125) accessor fixtures | ⬜ | GLTF-041 | Indices only per spec. **Accept:** exact. |
| GLTF-055 | `FLOAT` (5126) fixtures for every shape | ✅ | GLTF-041 | **Accept:** locked. |
| GLTF-056 | Normalized-integer conversion endpoints | ✔ | GLTF-050, GLTF-053 | §8.3 formulas. **Accept:** `0`, mid, and max map exactly for all four integer types, signed and unsigned. **Writing the test found the SECOND confirmed defect in the vendored parser.** §3.6.2.2's signed conversions are `max(c / 127, −1)` and `max(c / 32767, −1)`; `cgltf_component_read_float` divides and returns with **no clamp for either type**, so `-128` decodes to **−1.0079** and `-32768` to **−1.00003** — outside the unit range the normalization exists to produce. The clamp is not decoration: a two's-complement integer's negative range is one wider than its positive one, which is exactly why the specification writes a `max` there. Clamped CNA-side in `UnpackAccessor` and restated independently in the oracle, both pointing at this row; only the three unsigned divisors were previously tested, and only for `UNSIGNED_BYTE`. The other error this row exists to catch — dividing by 256 or 65536 instead of 255 or 65535 — is under one percent and invisible anywhere but an endpoint. |
| GLTF-057 | Non-normalized integer attributes stay integral | ✔ | GLTF-052 | `JOINTS_0` must **not** be normalized. **Accept:** a `JOINTS_0` value of 200 decodes to 200.0, not 0.784. **Already locked** by `NonNormalizedJointIndicesStayIntegral`, which asserts the flag is false and every decoded joint index is integral — a fractional value being the only way normalization could have been applied. `GLTF-056`'s new endpoint tests are its counterpart: together they pin that the flag is honoured in both directions. |
| GLTF-058 | `MAT2`/`MAT3` column-padding fixture | ⬜ | GLTF-041 | Unexposed today (only `MAT4` `FLOAT` is used) but must not regress. **Accept:** `mat3-padded` decodes per §3.6.2.4. |
| GLTF-059 | `MAT4` inverse-bind-matrix accessor fixture | ✅ | GLTF-041 | `f9` proved read correctness. **Accept:** locked at L2. |
| GLTF-060 | Cross-attribute `count` consistency check | ✔ | GLTF-042 | `posAcc->count` drives indexing of every other stream; a shorter `NORMAL` accessor reads out of range. **Accept:** `accessor-count-mismatch` errors with a clear message; ASan clean. **Landed, and the fixture corrected the premise.** `cgltf_validate` *does* catch the disagreement (code 4), so both loaders already rejected such a file before extraction was reached — the task's assumption that it slipped through was wrong. The check is still not redundant: `ExtractMesh` is called **directly, without validation**, by the L3 oracle itself, and had no check of its own, so `POSITION`'s count drove the loop that indexes every other decoded stream and a short `NORMAL` was read past the end of its vector. The check turns that undefined behaviour into a named error for any caller. It runs over **every** attribute the primitive declares, not only the ones the chosen stride uses: an attribute CNA ignores today is still evidence the file is malformed. `accessor-count-mismatch` therefore records **two** refusals, and the rejection test now asserts both — a defence-in-depth check that quietly stopped working would otherwise be invisible behind the layer in front of it. |
| GLTF-061 | Cross-check decoded bounds against `accessor.min`/`max` | ⬜ | GLTF-041 | Never read today. Would have caught D4 instantly. **Accept:** `accessor-minmax` warns on divergence via the import report. |
| GLTF-062 | Sparse + interleaved base accessor | ✔ | GLTF-047 | **CONFIRMED cgltf bug** (§9.3): the reader walks the sparse **values** array at `accessor->stride` while cgltf's own validator sizes it tightly, so every override after the first is misplaced when the base view is interleaved. `sparse-interleaved-base` decodes correctly through `ApplySparseOverridesTightly`, applied after every unpack site in the importer; the oracle restates the same rule independently; `VendoredParserStillMisreadsSparseValuesAtTheBaseStride` pins the underlying defect so a cgltf upgrade retires the workaround. |

---

### Phase 3 — Indices and primitive topology · owner: `GLTF-MESH`
*Entry: Phase 2 — **waived for the Track A tasks `GLTF-063` and `GLTF-071`** (§28.1). Exit: no
primitive mode is ever silently reinterpreted; indices decode exactly.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-063 | **[P0] Sparse-safe, bounds-checked index reader** | ✔ | GLTF-041 | `cgltf_accessor_read_index` returns `0` for sparse or null-`bufferView` with no error channel; `f3` decoded `[0,0,0,0,0,0]` for expected `[0,1,2,0,2,3]`. Replace with a CNA reader mirroring `UnpackAccessor`. **Accept:** `sparse-indices` decodes exactly; **critical path.** **Done** (`ef3bc2cbf`): `UnpackIndexAccessor` in `GltfImportCore.cpp`. `sparse-indices` decodes `[0,1,2,0,2,3]` exactly, at L2, L3 and (since `GLTF-007`) byte-exact at L5. Resolves sparse overrides per §3.6.2.3 including the zero-initialised base array and an independent sparse-index component type; requires SCALAR, non-normalized and one of the three unsigned types; bounds-checks every read against the owning bufferView with overflow-checked span arithmetic; reads components with `memcpy`. **D4 → `fixed`**, its record kept as the regression witness. The attribute path is untouched (`GLTF-041` still green). |
| GLTF-064 | Index component-type fixtures | ✅/⬜ | GLTF-063 | `u8-idx` ✅ (`f11`), `u16-idx` ✅, `u32-idx` ⬜. **Accept:** all three exact at L3. **Partly** (`ef3bc2cbf`): `GltfIndexDecodeTests.cpp` asserts all three decode exactly, but `u32` only on a hand-authored in-test accessor. The **corpus fixture** the row asks for does not exist yet, so the row stays open. |
| GLTF-065 | Null-`bufferView` index accessor is an explicit error | ✔ | GLTF-063 | Today it silently yields zeros. **Accept:** clear error. **Done** (`ef3bc2cbf`): an index accessor with neither a bufferView nor sparse data throws naming both; one with no bufferView *and* sparse data is legal per §3.6.2.3 and decodes from the zero-initialised base array, which is tested separately. |
| GLTF-066 | Sparse index accessor L2 dump | ⬜ | GLTF-063 | **Accept:** the L2 dump shows the override applied. |
| GLTF-067 | Non-indexed primitives synthesise `0..count-1` | ✅/⬜ | GLTF-063 | ~~`f12` proves it runs.~~ **Accept:** locked at L3. **Note** (`ef3bc2cbf`, `07312274e`): `f12` (`mode-points`) can no longer prove this — `GLTF-071` rejects its `POINTS` mode before the implicit range is synthesised at all. `GltfIndexDecode.NonIndexedPrimitiveSynthesisesTheImplicitRange` covers it on a hand-authored non-indexed `TRIANGLES` primitive instead. A non-indexed **corpus** fixture with an importable topology is still owed. |
| GLTF-068 | Validate `index < vertexCount` before packing | ✅/⬜ | GLTF-063 | `static_cast<uint16_t>` truncates silently when `vertexCount ≤ 65535`. **Accept:** `bad-index-out-of-range` errors with the offending value. **Partly** (`ef3bc2cbf`): the production check landed with `GLTF-063` — every decoded index is proved `< POSITION.count` before `ComputeTangentsEXT` or the packing loop sees it, and the error names the offending value, its position and the vertex count. Asserted on a hand-authored accessor; the `bad-index-out-of-range` **corpus fixture** the acceptance names does not exist yet, so the row stays open. |
| GLTF-069 | 16- vs 32-bit index selection rule | ⬜ | GLTF-068 | `use32BitIndices = vertexCount > 65535`. **Accept:** documented and tested at both sides of the boundary. |
| GLTF-070 | Record the "always materialise an index buffer" decision | ⬜ | GLTF-067 | Keeps the GPU layer uniform. **Accept:** decision documented and tested. |
| GLTF-071 | **[P0] Read `prim.type`; never silently reinterpret** | ✔ | GLTF-063 | `cgltf_primitive_type` has **0 occurrences**; `f4` (strip) and `f12` (points) both decoded as triangle lists. **Accept:** every mode is classified; **critical path.** **Done** (`07312274e`): `PrimitiveTopology` + `ClassifyPrimitiveTopology`/`PrimitiveTopologyName`/`PrimitiveTopologyMode`/`IsPrimitiveTopologySupported`, carried on `MeshOut::topology`. All seven modes classify by number and by specification name; a mode outside 0…6 is rejected rather than assumed. `TRIANGLES` imports byte-identically to before, including the very common no-`mode`-key case. Every other topology is rejected by `ExtractMesh` with its mode named by number and name, and **no index list reaches the `numIndices / 3` path at all**. **D5 → `partially-remediated`**: silent corruption is gone, but the row's own scope ends at classification — `GLTF-072` still owns conversion, so `f4`/`f12` do not import. |
| GLTF-072 | Implement the §10.1 per-mode policy | ✔ | GLTF-071 | Pass through `TRIANGLES`; convert `TRIANGLE_STRIP`/`TRIANGLE_FAN` to a triangle list at import; carry `LINES`/`LINE_STRIP`; convert `LINE_LOOP`; `POINTS` via CNAEXT or an explicit error. **Accept:** all seven `mode-*` fixtures decode to the manifest's triangle/line/point list. **Landed:** all seven `mode-*` fixtures now exist (the topology group is complete at 7) and each decodes to its manifest list. `ConvertToTriangleList` rewrites a strip or fan at import with winding preserved — a strip's odd triangle emits `(i+1, i, i+2)` — and `IsPrimitiveTopologySupported` widens to exactly the three triangle-producing modes. `MeshOut` gained `sourceTopology` alongside `topology`, so a conversion is visible rather than lossy: `topology` is always `Triangles` on anything `ExtractMesh` returns, while `sourceTopology` still names what the file declared (`GLTF-082`). The vertex buffer is untouched by a conversion — only the index list is rewritten — which is exactly why no renderer changed. **Scope boundary:** the four topologies that describe *no* triangles stay rejected, and deliberately so. They decode correctly; what they lack is a draw path, since every loader still computes `numIndices / 3`. Importing them now would move the original defect from the import layer to the draw layer rather than fix it, so `GLTF-073` (a real `PrimitiveType` on `ModelMeshPart`), `GLTF-078` (a topology-aware count) and `GLTF-077` (the points decision) own them, and the rejection message names them. D5 stays `partially-remediated` for that reason, with `GLTF-072` in `closedTasks`. |
| GLTF-073 | Carry primitive type on `ModelMeshPart` | ✔ | GLTF-072, GLTF-025 | XNA already has `PrimitiveType` at the device level. **Accept:** a line-mode fixture draws as lines on `OPENGLES3`. **Landed** behind the `GLTF-025` gate (`docs/gltf-api-change-review.md` §1.1), which chose the shape: a CNAEXT `getPrimitiveTypeEXTProperty`/`setPrimitiveTypeEXTProperty` pair reusing the **existing** `PrimitiveType` rather than a glTF-flavoured enum, defaulting to `TriangleList`. It is CNAEXT even though the type is XNA's own, because real XNA passes the topology to `DrawIndexedPrimitives` and every XNA part is implicitly a triangle list. `ModelMesh::Draw` now draws the part's own topology, both loaders set it, and the `.cnj` carries a `"primitiveTopology"` field written only when it is not the default — so an ordinary triangle-list asset's `.cnj` is unchanged and a pre-`GLTF-073` file still loads as the triangle list it could only ever have been. |
| GLTF-074 | Strip→list conversion preserves winding | ✔ | GLTF-072 | Odd triangles in a strip have reversed winding. **Accept:** `mode-triangle-strip` L3 index list equals the manifest exactly, including the swap on odd triangles. **Landed with `GLTF-072`.** `GltfConformanceL3` asserts the converted list against the manifest's own `triangles` expansion — derived independently of `importPolicy.indices`, so a generator bug that got the rule wrong in both places still fails — and `GltfPrimitiveTopology.StripConversionSwapsTheOddTrianglesCorners` pins the rule on a five-index run where the alternation has to continue rather than merely apply once. |
| GLTF-075 | Fan→list conversion | ✔ | GLTF-072 | **Accept:** `mode-triangle-fan` matches the manifest. **Landed with `GLTF-072`.** `mode-triangle-fan` is generated and imports to `[0,1,2]`,`[0,2,3]`. The two rules are asserted to *differ* on identical input, so neither fixture can pass under the other's conversion. |
| GLTF-076 | `LINE_LOOP` → `LINE_STRIP` + closing segment | ✔ | GLTF-072 | **Accept:** manifest match. **Landed:** `CloseLineLoop` appends the first index again, so `mode-line-loop`'s `[0,1,2]` imports as `[0,1,2,0]` with `MeshOut::topology` = `LineStrip` and a primitive count of 3 against `mode-line-strip`'s 2. The closing segment is implicit in the mode, so dropping the mode lost a whole segment before it lost the topology — the two fixtures author the identical index list and differ only by it. |
| GLTF-077 | Decide `POINTS` support per renderer | ✔ | GLTF-072 | **Accept:** either a CNAEXT point-list path or a documented, per-renderer explicit rejection — never a triangle. **Decided: a point list imports as `PrimitiveType::PointListEXT` and support is answered per renderer at draw time, not by refusing the data.** Import and draw are different questions, and conflating them is what made the original defect: a renderer that cannot draw points is not a reason to discard a file's geometry. `mode-points` imports with 4 points and 0 triangles; whether a given renderer honours `PointListEXT` is its own capability answer. |
| GLTF-078 | Primitive-count helper for every topology | ✔ | GLTF-072 | All three loaders hardcode `numIndices / 3`. **Accept:** one shared helper; §12.3 table asserted at L5. **Landed:** `PrimitiveCountForTopology` implements §12.3's table once, and the loaders' hardcoded `numIndices / 3` is gone from the glTF paths. Stated independently in C++ and in the generator on purpose — a count that agreed with itself but not with the specification would pass either alone. A run too short for one primitive yields `0`, never a negative count, which is where `n-1` and `n-2` could have underflowed. The legacy `SkinnedModel` `.cnj` reader keeps its own `/3`: that format has no topology field and only ever held triangles. |
| GLTF-079 | Degenerate/empty primitive handling | ⬜ | GLTF-072 | Zero indices, or an index count not divisible by the topology's stride. **Accept:** deterministic behaviour, reported. |
| GLTF-080 | Topology + Draco interaction | ✔ | GLTF-072 | Draco always decodes to triangles. **Accept:** a Draco primitive declaring a non-triangle mode is rejected or documented. **Established, and refused.** Draco's mesh encoder is a *triangle* encoder — a decoded `draco::Mesh` carries a face list and nothing else — so a primitive declaring `KHR_draco_mesh_compression` together with a line or point mode is a contradiction the file cannot mean. It is refused, **before decoding**, so the diagnostic names the contradiction rather than some later symptom of it, and **independently of whether the build has libdraco**: the file is self-contradictory either way. Asserted as a **partition** rather than a list — the three triangle modes are exactly what Draco can encode and the four others exactly what it cannot, and the two sets are asserted to be all seven — so the rule has no gap for a mode to fall through. `ProducesTriangles` moved into the header as part of this: two rules now share that partition (`GLTF-072`'s conversion and this refusal), and a file-local copy is how the two would drift. No fixture: the corpus has no Draco asset and building one needs libdraco at *generation* time. |
| GLTF-081 | Topology + morph interaction | ⬜ | GLTF-072 | Deltas are per-vertex, so topology conversion must not reorder vertices. **Accept:** asserted for `mode-triangle-strip` + morph. |
| GLTF-082 | Report every topology conversion in the import report | ⬜ | GLTF-035, GLTF-072 | **Accept:** conversions are visible, not silent. |

---

### Phase 4 — Vertex semantics · owner: `GLTF-MESH`
*Entry: Phase 3. Exit: every supported semantic decodes exactly, with documented defaults and losses.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-083 | `POSITION` is required and always at offset 0 | ✔ | GLTF-041 | **Accept:** locked at L3/L5; a primitive without `POSITION` errors (already does). **Both halves locked.** The refusal matters beyond conformance: `POSITION`'s accessor count is what drives the packing loop, so a primitive without one has no vertex count at all and would otherwise produce a silently empty mesh. The offset half asserts the bytes *at* offset 0 are the authored positions, which is the other side of `GLTF-150`'s table claim — together they stop the table and the packer drifting apart. |
| GLTF-084 | `NORMAL` decode and default | ⬜ | GLTF-083 | Absent normals currently emit `(0,0,1)` — a **fabricated** default, not glTF's "compute flat normals". **Accept:** decision recorded (§8 of Phase 8) and tested. |
| GLTF-085 | Warn when `NORMAL` is dropped by stride selection | ✔ | GLTF-084, GLTF-035 | Strides 20 and 24 have no normal slot; the attribute is silently discarded. **Accept:** import report entry. **Landed with `GLTF-241`**, which is where the case stopped being hypothetical: a vertex-coloured PBR primitive lands on stride 24 and so loses its normals *and* its PBR material at once. `MeshOut::droppedNormalForStrideEXT` is set at extraction and `ContentManager` reports it per primitive, naming the stride that has no slot; `GltfKnownDefectTests` asserts the flag on `mat-vertex-color-pbr`. |
| GLTF-086 | `TANGENT` decode for non-PBR meshes | ✔ | GLTF-083 | Read only when `usePbr`. **Accept:** decision recorded; either carried or reported. **Decided: reported, because it cannot be carried** (`docs/gltf-api-change-review.md` §1.10). Only strides 48 and 68 have a tangent slot, and those are exactly the PBR layouts — unlike the material factors `GLTF-219` ungated, which `MeshOut` could hold even with no effect to consume them, there is literally nowhere to put a tangent in a stride-32 vertex. `MeshOut::droppedTangentForStrideEXT` names it and both loaders log it, matching `GLTF-241`'s treatment of the dropped normal. Worth reporting rather than shrugging at: a file that authored tangents did so for a reason, usually a normal map the material also declares. |
| GLTF-087 | `TEXCOORD_0` decode | ✅ | GLTF-041 | **Accept:** locked at L3. |
| GLTF-088 | `TEXCOORD_n` selection by the base-colour texture's `texCoord` | ✅ | GLTF-087 | `UsesTexcoordSetSelectedByMaterial` already covers it. **Accept:** extended to the corpus. |
| GLTF-089 | `COLOR_0` as `VEC3` and `VEC4` | ✔ | GLTF-041 | Missing alpha defaults to 255. **Accept:** both shapes locked at L5 byte level. **Locked against each other**, not merely each against a literal: the two shapes must agree byte-for-byte on the three channels they share, so the `VEC3` path cannot quietly diverge from the `VEC4` one. The implied alpha is asserted as **255, not 0** — 0 being exactly what a zero-initialised buffer supplies, and the value that would make every vertex of such a mesh fully transparent. |
| GLTF-090 | Document `COLOR_0` precision loss to 8-bit | ⬜ | GLTF-089 | `FLOAT`/`UNSIGNED_SHORT` colours are quantised to `Color` bytes. **Accept:** documented and asserted at the endpoints. |
| GLTF-091 | `COLOR_1+` ignored — document | ⬜ | GLTF-089 | XNA has one colour channel. **Accept:** import report entry. |
| GLTF-092 | Custom `_*` attributes ignored — document | ⬜ | GLTF-083 | **Accept:** documented; no error. |
| GLTF-093 | `JOINTS_0` component types | ✅/🐛 | GLTF-057 | `UNSIGNED_BYTE` and `UNSIGNED_SHORT` decode correctly; the **`uint8` `BlendIndices` pack** truncates above 255. **Accept:** decode locked; truncation handled by `GLTF-254`. |
| GLTF-094 | `WEIGHTS_0` component types | ✅ | GLTF-041 | `f9` proves `FLOAT`; normalized `u8`/`u16` weights need a fixture. **Accept:** all three exact at L5. |
| GLTF-095 | `JOINTS_1`/`WEIGHTS_1` detection and reporting | ✔ | GLTF-035 | Silently ignored — a >4-influence mesh loses its tail. **Accept:** import report entry; `GLTF-257` decides support. **Landed as a measurement, not a count.** `MeshOut::extraInfluenceSetsEXT` says how many sets were authored and `worstDroppedInfluenceEXT` says the largest share of a single vertex's influence that truncation discarded — measured **before** `GLTF-256`'s renormalisation, because that is the only number distinguishing a fifth influence weighted 0.002 (exporter noise) from one weighted 0.4 (a visibly different pose). Both loaders report it. New fixture `skin-eight-influences`, whose three vertices lose 40%, 1% and nothing, so a report that merely counted sets could not pass. |
| GLTF-096 | Attribute-set index parsing (`_0`, `_1`, …) | ✅ | GLTF-088 | cgltf's `attribute.index`. **Accept:** locked. |
| GLTF-097 | Vertex-count consistency across all streams | ✔ | GLTF-060 | See `GLTF-060`. **Accept:** shared assertion. **Landed as exactly that — one assertion, not a pair of special cases.** `GLTF-060`'s check is driven by the primitive's own attribute list, so it covers every declared stream including ones CNA does not yet read; there was never a second place to add it. Tested in both directions: the malformed fixture is refused with a diagnostic naming the attribute and the constraint, **and** every primitive of every other corpus fixture extracts cleanly — which is what stops the check being satisfied by a version that refuses anything with more than one attribute. |
| GLTF-098 | Deterministic vertex ordering | ✔ | GLTF-083 | `ExtractMesh` emits vertices in accessor order. **Accept:** locked at L5 — required for morph delta indexing. **Locked, and it has the most dangerous failure mode in the file.** A morph target's delta array is indexed *by a vertex's position in the buffer*; nothing in the morph path carries an identity for a vertex. So any reordering during extraction — a cache-friendly sort, a dedupe pass, a Draco round-trip that renumbers — applies every delta to the wrong vertex, and **no test in the morph path could see it**, because the deltas would still be the right values on the wrong vertices. The fixture's positions descend in X, so a sort is visible immediately. |
| GLTF-099 | Stride selection decision table as data, not nested ternaries | ⬜ | GLTF-072 | The current one-line ternary chain is unreadable and duplicated implicitly in renderers. **Accept:** a table-driven selector with a unit test per row of §2.3. |
| GLTF-100 | Reject unrepresentable attribute combinations loudly | ⬜ | GLTF-099 | e.g. `usePbr && colored` is currently impossible and silently downgrades. **Accept:** the combination is either supported or reported. |
| GLTF-101 | L3 semantic-mesh manifest for every corpus asset | ⬜ | GLTF-005 | **Accept:** `MeshOut` field-by-field comparison for all 135 assets. |
| GLTF-102 | Attribute fuzz: random valid permutations | ⬜ | GLTF-040 | **Accept:** no crash, no sanitiser finding, no silent drop without a report entry. |

---

### Phase 5 — Coordinate conventions and node transforms · owner: `GLTF-TRANSFORM` · **CRITICAL PATH**
*Entry: Phase 4 — **waived for the Track A tasks `GLTF-103`, `GLTF-113`, `GLTF-114`, `GLTF-115`**,
whose only prerequisites are their own dependency columns (§28.1). Exit: every `xf-*` fixture passes
numerically at L4.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-103 | **[P0] ADOPT the real `ModelBone` node hierarchy (§11.5 Option A)** | ✔ | GLTF-004, GLTF-005, GLTF-006 | **Not an open A-vs-B choice.** Option A is the presumptive final architecture (§11.5): prove it preserves XNA compatibility and adopt it. CNA already has `Model`, `ModelBone`, `ModelMesh::ParentBone` and a verified-correct `CopyAbsoluteBoneTransformsTo`. Option B (baking) may be adopted **only** on a demonstrated concrete blocker, **only** as an explicitly time-boxed interim step, and then it must carry its own removal task — it is never recorded as the final architecture. Makes `GLTF-129`/`GLTF-130` mandatory. **Dependency note:** this task deliberately does **not** depend on `GLTF-011`; the verdict is written after these fixes land, and the reverse dependency would be a cycle. **Accept:** a written decision naming the adopted architecture, the seven properties of §11.5 it preserves, and — if Option B is chosen — the concrete blocker, the time box and the removal task. **Critical path.** **Landed:** Option A adopted; no blocker found. `BuildSceneGraph` flattens the default scene parent-before-child with composed world transforms, and both loaders mirror it as `ModelBone`s. Vertex positions stay mesh-local, so instancing survives — `xf-shared-mesh`'s two placements share one mesh. `GLTF-129`/`GLTF-130` landed with it, as §11.5 requires. |
| GLTF-104 | Lock the XNA transform-order invariants | ✔ | GLTF-006 | `Model::Draw` uses `bone * world`; `AnimationPlayer` uses `local * parentWorld`. Both correct. **Accept:** unit tests pin them so a later change is caught. **Locked in `GltfConventionsTests.cpp`**, with operands chosen so a swap is unmistakable rather than merely wrong: `Model::Draw` binds a bone translated 5 through a world scaled 2, giving 10 one way and 5 the other; the scene graph places a child offset 1 under a parent scaled 2 and translated 100, giving 102 one way and 201 the other. A parent at the origin — the obvious fixture to reach for — cannot tell the two orders apart at all. |
| GLTF-105 | **Record "no axis or handedness conversion" as a testable invariant** | ✔ | GLTF-104 | glTF and XNA are both right-handed, +Y up, −Z forward; UV origin is top-left in both. **Accept:** `docs/gltf-conventions.md` states it, and a test asserts a +Z-facing glTF normal stays +Z. Any future axis flip must first fail a fixture. **Both landed.** `docs/gltf-conventions.md` is the one-place statement — the agreements table, the single conversion that *is* performed and why it is not an exception, and the two load-bearing multiplication orders. The test asserts positions, normals **and** UVs pass through unchanged, with every authored component distinct so a swapped or negated axis cannot land on the value it should have had and pass by coincidence. |
| GLTF-106 | Lock `ConvertGltfMatrix` | ✔ | GLTF-105 | Copies basis vectors, producing the XNA row-vector transpose of the glTF column-vector matrix. Correct — but unreached today. **Accept:** direct unit test over a non-trivial affine matrix. **Locked behaviourally, through `BuildSceneGraph`, rather than by exporting the helper for a test** — the same coverage without an API addition the `GLTF-025` gate would have to review. "Non-trivial" is load-bearing and the corpus could not supply it: `xf-matrix-node` is translation-only, and a translation-only matrix **cannot** tell a basis-vector copy from a naive full transpose, because transposing an identity rotation block gives the identity back. The test uses a rotation-plus-non-uniform-scale-plus-translation matrix, where the wrong transpose puts the translation in the fourth *column* instead of the fourth *row*. A second test cross-checks the `matrix` and TRS paths against each other, which catches a transpose present in exactly one of them — an error each path's own test would miss, because each is self-consistent. |
| GLTF-107 | `node.matrix` decoding | ✔ | GLTF-106 | `f13`/`xf-matrix-node`: `matrix = translate(4,5,6)` discarded. **Accept:** L4 world position `(5,5,6)`. **Landed with `GLTF-113`/`GLTF-114`** (defect D3, recorded `fixed` in the ledger): `BuildSceneGraph` reads each node's local transform through `cgltf_node_transform_local`, which applies the specification's own "matrix, or else TRS" exclusivity rule, so both authorings compose alike. `xf-matrix-node`'s world X range is the spec-derived `[4,5]` and `GltfConformanceL4` asserts it in full. |
| GLTF-108 | Quaternion component order | ✔ | GLTF-105 | glTF `(x,y,z,w)` → `Quaternion(X,Y,Z,W)`. **Accept:** locked by `xf-rot-*`. **Locked directly instead** — `xf-rot-*` is still ungenerated, and the invariant does not need a corpus asset to be pinned. 90° about +X, which maps +Y to +Z: under the classic `[w,x,y,z]` misread the same four numbers are a rotation about +Y that leaves +Y alone, so the test cannot pass by coincidence. An identity rotation, the obvious thing to author, is indistinguishable under both readings. |
| GLTF-109 | TRS composition order | ⬜ | GLTF-108 | `S` then `R` then `T`. **Accept:** `xf-trs-order` yields `(1,2,0)`. |
| GLTF-110 | `matrix` and TRS mutual exclusivity | ⬜ | GLTF-107 | §3.5. **Accept:** `bad-matrix-and-trs` is reported; `xf-matrix-vs-trs` proves the two authorings agree. |
| GLTF-111 | Default TRS values | ⬜ | GLTF-109 | `T=0`, `R=identity`, `S=1`. **Accept:** `xf-identity`. |
| GLTF-112 | World-transform propagation, applied exactly once | ✅/⬜ | GLTF-109 | `CopyAbsoluteBoneTransformsTo` is correct; the input is missing. **Accept:** `xf-deep-chain` yields `(6,0,0)` — not `(1,0,0)` and not `(11,0,0)`. |
| GLTF-113 | **[P0] `MeshGroup` → mesh instances carrying `cgltf_node*` + world matrix** | ✔ | GLTF-103 | `struct MeshGroup { const cgltf_skin*; std::vector<const cgltf_mesh*>; }` has no node and no matrix (D1–D3). The per-instance identity introduced here is the **`sceneNodeIndex`** space of §15.1.2 and must not be ordered to suit any skin's GPU palette. **Accept:** the import data model carries per-instance node identity and transform. **Critical path.** **Landed:** `MeshGroup::meshes` → `MeshGroup::instances` (`MeshInstanceOut`: node, mesh, `sceneNodeIndex`, world transform, `skinned`). Group ordering and the fallback for a file whose scenes reference no mesh are unchanged. |
| GLTF-114 | **[P0] World transform reaches the model on BOTH loaders** | ✔ | GLTF-113 | `ReadGltfModel` and the `.cnj` `ModelTypeReader` both create identity root and identity per-mesh bones. **Accept:** `f1` yields X ∈ `[0,11]`; `f2` yields Y ∈ `[6,8]`. **Critical path.** **Landed:** One `ModelBone` per scene node on both loaders, bone index == `SceneGraphOut` index, mesh parented to its instancing node's bone. A **skinned** instance is parented to the identity root instead — glTF ignores a skinned mesh's own node transform — so this deliberately does not touch D8. |
| GLTF-115 | **[P0] The whole `xf-*` ladder passes at L4** | ✔ | GLTF-114 | All 17 fixtures in §11.4. **Accept:** every expected world position matches to 1e-6. **Critical path.** **Landed:** `GltfConformanceL4.CnaWorldPositionsMatchTheExpectedGeometry` now asserts every corpus fixture with no suppression; D1/D2/D3 are `fixed` in the ledger with the audit's measurements preserved under `priorActual`, and their inverted known-defect tests are deleted. |
| GLTF-116 | Negative scale: winding | ⬜ | GLTF-115 | `xf-negative-scale`. **Accept:** triangle winding is flipped so front faces stay front-facing; asserted at L5 (index order) or L7 (cull test). |
| GLTF-117 | Mirrored transform through a hierarchy | ⬜ | GLTF-116 | `xf-mirror-child`. **Accept:** manifest match. |
| GLTF-118 | Normal transformation under non-uniform scale | ⬜ | GLTF-115 | `normal * transpose(inverse(A))`. **Accept:** `xf-scale-nonuniform` normals match the manifest; matters for Option B and for the shader's `uNormalMatrix` either way. |
| GLTF-119 | Tangent transformation and `w` sign under mirroring | ⬜ | GLTF-118 | `tangent.w *= sign(det A)`. **Accept:** `tangent-mirrored` matches. |
| GLTF-120 | Nodes without a mesh (transform-only) | ⬜ | GLTF-114 | `xf-transform-only`. **Accept:** the child mesh is placed by the parent. |
| GLTF-121 | `unitScale` interaction with node transforms | ✅/⬜ | GLTF-114 | Currently scales positions and bone/IBM translations consistently ✅, but cannot scale the (dropped) node translations. **Accept:** after `GLTF-114`, `unitScale` scales the composed world translation too; existing `UnitScaleApplies…` test still passes. |
| GLTF-122 | Multiple root nodes | ⬜ | GLTF-114 | `xf-multi-root`. **Accept:** all three roots imported and placed. |
| GLTF-123 | Node visited from two scenes | ✔ | GLTF-114 | `CollectSceneReachableNodes` de-duplicates. **Accept:** locked. **Locked — and writing it corrected the row's premise.** §3.5.3 makes the node hierarchy a disjoint union of strict *trees*, so a node cannot have two **parents** at all; cgltf rejects that document outright, and it is not the shape this row is about. What a file legitimately can do is name the same node twice among one scene's roots, and list it as a root of several scenes. Both are de-duplicated, asserted at the graph *and* the extraction layer — a graph that de-duplicates but an extractor that does not would still import every mesh twice, drawing geometry on top of itself. |
| GLTF-124 | Cycle / malformed hierarchy guard | ⬜ | GLTF-114 | **Accept:** a cyclic `children` graph errors rather than hanging. |
| GLTF-125 | Deep-hierarchy depth limit | ⬜ | GLTF-124 | The traversal is iterative (stack-based) ✅. **Accept:** a 10 000-deep chain does not overflow. |
| GLTF-126 | Bone naming and index stability | ⬜ | GLTF-113 | Nodes without a `name` need deterministic generated names. **Accept:** the same file produces the same bone names/indices on every run. |
| GLTF-127 | `Model::Root` is the scene root | ⬜ | GLTF-114 | **Accept:** `getRootProperty()` is the synthetic scene root; `CopyAbsoluteBoneTransformsTo` composes correctly. |
| GLTF-128 | World-space model bounds exposed to callers | ⬜ | GLTF-114 | The viewer currently recomputes bounds from vertex sidecars. **Accept:** a CNAEXT bounds accessor; `GLTF-427` consumes it. |
| GLTF-129 | `.cnj` format: `"bones"` + per-mesh `"parentBone"` | ✔ | GLTF-103, GLTF-025 | Additive, `cnjVersion` bumped. **Accept:** old `.cnj` files still load; new ones round-trip at L4. **Landed:** `cnjVersion` 2 adds `"bones"` (name/parent/local transform) and per-mesh `"parentBone"`. The version ceiling is **per type** (`ValidateCnjEnvelope(..., maxVersion)`) — only `Model` accepts 2 — so "an unknown future version is rejected" stays true for every other `.cnj` type. A version-1 Model still loads through the same reader and keeps its per-mesh child-bone behaviour. |
| GLTF-130 | `.cnj` round-trip equals the runtime path at L4 | ✔ | GLTF-129 | The two loaders must not diverge. **Accept:** every corpus fixture yields identical L4 output via both paths. **Landed:** Both loaders build the same tree from the same `SceneGraphOut`; the offline path round-trips it through `"bones"`/`"parentBone"`. Asserted by the shared L4 suite, which runs over the corpus rather than per loader. |
| GLTF-131 | Transform fixtures in `.glb` form too | ⬜ | GLTF-115, GLTF-026 | **Accept:** `.gltf` and `.glb` twins agree at L4. |
| GLTF-132 | Document the transform pipeline in `docs/` | ⬜ | GLTF-115 | **Accept:** `docs/gltf-conventions.md` carries §11's tables and equations. |

---

### Phase 6 — Scenes, instancing, model shape · owner: `GLTF-TRANSFORM`
*Entry: Phase 5. Exit: a shared mesh instanced by N nodes produces N correctly-placed instances.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-133 | Default-scene selection | ✔ | GLTF-114 | `f14` proved `scene: 1` is honoured and the decoy excluded. **Accept:** locked as `scene-default-selection`. **Locked twice over:** the corpus fixture, plus a scratch document whose decoy carries its own mesh rather than being an empty node — so defaulting to scene 0, the obvious shortcut, loads entirely the wrong content instead of merely the wrong node count. |
| GLTF-134 | No `scenes` array at all | ⬜ | GLTF-133 | Falls back to "every mesh in the file". **Accept:** `scene-no-scenes` behaviour documented and tested. |
| GLTF-135 | Nodes outside any scene are excluded | ✔ | GLTF-133 | `OnlyImportsNodesReachableFromTheDefaultScene` exists. **Accept:** extended to the corpus. **The existing test is a `GltfToCnjToolTest`, which this configuration SKIPS** — so the rule was effectively unasserted here. Now locked directly at graph and extraction level: an exporter routinely leaves helpers, proxies and hidden geometry in the `nodes` pool without putting them in a scene, and importing those drags back in exactly what the author took out. |
| GLTF-136 | Shared mesh instanced by multiple nodes | ✔ | GLTF-114 | `f1`: today two identity-transform duplicates. **Accept:** `xf-shared-mesh` yields two instances at the right places, ideally sharing one `VertexBuffer`. **Landed with `GLTF-113`/`GLTF-114`** (defect D1, `fixed`): the two instances are placed 10 units apart and the union spans the spec-derived X range `[0,11]`, asserted by `GltfConformanceL4`. **The "ideally" half is deliberately not claimed:** the loader still extracts and uploads a separate `VertexBuffer` per instance, so a mesh instanced *n* times occupies *n* copies. That is a memory and load-time cost, not a correctness one — the bytes are identical, which `GLTF-007`'s L5 goldens would catch if they ever were not — and de-duplicating it means keying uploads by `(mesh, primitive)` inside the import loop. Recorded here rather than silently satisfied by the correctness half. |
| GLTF-137 | Runtime path imports **all** mesh groups, not `groups.front()` | ✔ | GLTF-114 | `ReadGltfModel` documents the single-group limit; a mixed skinned + static file silently loses a group. **Accept:** one `Model` carries every group, or the limitation is removed by the node-hierarchy work. **Landed:** the loader builds the list of importable instances once — every group's, each paired with the skeleton that poses it — instead of taking `groups.front()`, so a file with a character and a prop keeps both. `skin-plus-static-mesh` is the smallest file that exhibited the drop and is new to §15.4 (see the count note in §24.2); the skinned node is authored first, so the **static** prop is what used to vanish, and the L6 capture asserts both parts are drawable, keep their own skinned/unskinned nature, and that the prop lands at its own node's placement as stated at L4. **One limit survives and is now reported instead of hidden:** `Model::Tag` holds a single `SkinningData`, so a *second* skin's meshes are still not imported — posing them with another rig's palette would be a fresh silent corruption in place of the old silent drop, the same reasoning `GLTF-295` records for rigid clips. The unskinned group is never affected. |
| GLTF-138 | Grouping by skin becomes a detail, not the model shape | ⬜ | GLTF-137 | With a real node hierarchy, "one `.cnj` per skin" is no longer necessary. **Accept:** decision recorded; the offline tool's output shape is settled. |
| GLTF-139 | `ModelMesh` ↔ glTF mesh ↔ primitive mapping | ⬜ | GLTF-114 | Today one `ModelMesh` per **primitive**. XNA's shape is one `ModelMesh` per mesh with one `ModelMeshPart` per primitive. **Accept:** the mapping is chosen, documented and tested. |
| GLTF-140 | Deterministic mesh/part ordering | ⬜ | GLTF-139 | **Accept:** stable across runs; required for golden L5 comparison. |
| GLTF-141 | Mesh/node/part naming | ⬜ | GLTF-126 | **Accept:** names traceable back to the glTF node and mesh. |
| GLTF-142 | Empty mesh / empty primitive handling | ⬜ | GLTF-139 | **Accept:** deterministic, reported, no crash. |
| GLTF-143 | A file with meshes but no scene node referencing them | ✔ | GLTF-134 | Existing fallback. **Accept:** locked. **Locked with the distinction that makes it consistent with `GLTF-135` rather than contradicting it:** what is excluded is a node **no scene reaches**, not a mesh no node uses. A separate test proves the fallback does *not* fire for a file that has scenes — otherwise an excluded orphan's mesh would come straight back in through the other door, and both rows would still look green. |
| GLTF-144 | Camera and light nodes must not become mesh instances | ⬜ | GLTF-114 | **Accept:** asserted for `lights-punctual-three` and `camera-perspective`. |
| GLTF-145 | Node-graph import report | ⬜ | GLTF-034 | Node count, instance count, depth, shared-mesh count. **Accept:** report fields populated. |
| GLTF-146 | `EXT_mesh_gpu_instancing` interaction placeholder | ⬜ | GLTF-136 | CNA already has `DrawInstancedPrimitives`. **Accept:** documented as GLTF ROBUST (`GLTF-352`), not silently ignored. |
| GLTF-147 | Very large node counts | ⬜ | GLTF-125 | **Accept:** a 10 000-node scene imports within a stated budget. |
| GLTF-148 | Scene-level L4 regression for the whole corpus | ⬜ | GLTF-115 | **Accept:** every fixture's full world-space vertex set matches its manifest. |

---

### Phase 7 — GPU packing and the stride ABI · owner: `CNA-GPU-PACKING`
*Entry: Phase 6. Exit: every generated buffer is byte-exact against a golden, on every renderer.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-149 | Golden vertex-buffer bytes for all seven strides | ⬜ | GLTF-007 | **Accept:** `memcmp` clean; a one-byte change is reported at the right field. |
| GLTF-150 | Position at offset 0 for every stride | ✔ | GLTF-149 | **Accept:** static assertion + L5 test. **Locked with `GLTF-155`** — `EveryStrideTheGltfImporterCanEmitIsInTheCanonicalTable` walks all eight strides the importer can produce and asserts Position at offset 0 on each, per stride rather than assumed once; `GLTF-083` adds the L5 half, that the bytes there really are the authored positions. |
| GLTF-151 | Element offsets match §2.3 exactly | ⬜ | GLTF-149 | **Accept:** one assertion per element of each stride. |
| GLTF-152 | `ModelMeshPart` counts and offsets | ✔ | GLTF-078 | `PrimitiveCount = numIndices/3` unconditionally in all three loaders. **Accept:** the §12.3 table, asserted per topology; `VertexOffset`/`StartIndex` remain 0 and are tested. **Landed:** the count half by `GLTF-078`'s topology-aware helper, asserted per topology against each fixture's own `importPolicy.primitiveCount`. The offsets had no test at all and now do, over every part of every corpus fixture — together with the fact that makes a zero offset *correct* rather than merely true: the loader gives each part its own buffers, so `NumVertices` spans the whole vertex buffer. A future change that packs several parts into one buffer has to set the offsets rather than inherit a silent 0. |
| GLTF-153 | `IndexBuffer` element size matches `use32BitIndices` | ⬜ | GLTF-149 | **Accept:** L5 assertion both sides of 65535. |
| GLTF-154 | Colour byte quantisation rule | ✔ | GLTF-089 | `round(clamp(f,0,1)*255)`. **Accept:** endpoints asserted. **Locked on a FLOAT `COLOR_0`, which is the only shape that can test it** — a normalized-integer colour is already a byte and round-trips under any rule, which is why the existing `normalized-u8-color` coverage proved nothing about the rounding. Truncation and rounding agree at 0 and 1 and differ everywhere between, so mid values carry the test: 0.5 → 128 (not 127), 0.002 → 1 (not 0), 0.998 → 254. The **clamp** gets its own test: unclamped, 1.5 narrows to 126, so a bright colour would arrive as mid grey and read as a material bug rather than an out-of-range input. |
| GLTF-155 | **One canonical stride → `VertexDeclaration` table** | ✔ | GLTF-151 | Today the table lives in `ExtractMesh` and is re-implemented as a `switch (stride)` in every renderer, with no shared source of truth. **Accept:** a single declaration in `modules/graphics`, consumed by the importer and available to renderers. **The table is `CNA/Internal/Graphics/VertexDeclarationFidelity.hpp`'s `InferredLayoutForStride` — header-only, in `modules/graphics`, already consumed by renderers.** What this task added is the importer side: its unlisted-stride refusal is now a *query against the table* rather than a literal list, and a test asserts the two sets are one — every stride the importer can emit is in the table, and an unlisted stride comes back `known == false` rather than with a guessed layout. That coupling is what `GLTF-278` proved is needed: a restated ABI goes stale silently. The per-stride **upload** branches necessarily stay explicit — each selects a distinct C++ vertex struct, which a data table cannot do — and complete per-renderer declaration *translation* remains `plan_postaudit.md`'s, as that header's own documentation states. |
| GLTF-156 | Static assertions tying the table to the vertex structs | ⬜ | GLTF-155 | `VertexPositionNormalTextureSkinned` etc. **Accept:** changing a struct field order fails the build, not a test. |
| GLTF-157 | Replace the silent position-only `ApplyLayout` fallback | 🐛 | GLTF-155 | An unknown stride leaves normals/UVs/weights **unbound** — reading stale attribute state. **Accept:** a loud error or a mandatory declaration path; `CNA_RENDER_LOG` is not sufficient. **The importer's half is done; the renderer's half is not.** A silent fallback of exactly this shape was found on the *import* side: `BuildVertexBufferFromRawBytes` had no `else`, so an unlisted stride fell out of the `if` chain and the freshly constructed, **empty** `VertexBuffer` was returned as though it had been filled — the mesh then drew from whatever that buffer object happened to contain. It now throws, naming the stride and whether the canonical table knows it. **Still open:** `EasyGLRenderer::ApplyLayout`'s own position-only fallback. That file cannot be compiled in this environment (it needs the sibling `../easy-gl` and `../meta-gl` checkouts), so a change there could be written but not verified, and an unverified renderer change is not what this row is asking for. |
| GLTF-158 | Renderer stride-conformance test | ⬜ | GLTF-157 | **Accept:** every enabled renderer accepts all seven strides and binds every element; run in CI for `STUB`, `HEADLESS`, `OPENGLES3`, `VULKAN`. |
| GLTF-159 | Pass a real `VertexDeclaration` to the renderer boundary | 🔬 | GLTF-155 | `ApplyLayout` already has a generic declaration path; the glTF loader never populates one. **Accept:** decision recorded; if adopted, magic strides become an internal detail. |
| GLTF-160 | Audit every renderer's stride table against §2.3 | ⬜ | GLTF-158 | **Accept:** a per-renderer conformance matrix in `docs/`. |
| GLTF-161 | `BuildVertexBufferFromRawBytes` covers every stride | ✅/⬜ | GLTF-155 | Correct for the seven known strides. **Accept:** locked; an unknown stride errors. |
| GLTF-162 | `VertexBuffer::SetDataRaw` contract | ⬜ | GLTF-161 | Used by morph re-upload. **Accept:** stride/count contract tested. |
| GLTF-163 | 32-bit index support on every renderer | 🔬 | GLTF-153 | Some legacy renderers may not support `ThirtyTwoBits`. **Accept:** capability matrix; a clear error rather than silent truncation. |
| GLTF-164 | Large-mesh boundary fixture (65 535 / 65 536 vertices) | ⬜ | GLTF-069 | Generated, not committed as a large asset. **Accept:** both sides decode and draw. |
| GLTF-165 | Buffer creation failure handling | ⬜ | GLTF-161 | **Accept:** an allocation failure surfaces as `ContentLoadException`, not a crash. |
| GLTF-166 | Zero-vertex / zero-index primitive | ⬜ | GLTF-079 | **Accept:** no zero-size buffer creation; deterministic behaviour. |
| GLTF-167 | L5 golden regeneration procedure | ⬜ | GLTF-020 | **Accept:** goldens regenerate reproducibly and their diff is reviewable. |
| GLTF-168 | Cross-renderer L5 equality | ⬜ | GLTF-158 | L5 is renderer-independent by construction. **Accept:** byte-identical across `STUB`/`HEADLESS`/`OPENGLES3`/`VULKAN`; any divergence is a bug. |

---

### Phase 8 — Normals and tangents · owner: `GLTF-MESH`
*Entry: Phase 7. Exit: shading basis is correct under every transform in the corpus.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-169 | Authored `TANGENT` is used verbatim when present | ✅ | GLTF-086 | **Accept:** `tangent-authored` matches at L5, `w` included. |
| GLTF-170 | Tangent generation when absent | ✅ | GLTF-169 | `ComputeTangentsEXT`: angle-weighted, Gram-Schmidt, documented non-MikkTSpace divergence. **Accept:** `tangent-absent-generated` matches the manifest; the divergence stays documented. |
| GLTF-171 | Tangent generation is gated on `usePbr` | ✔ | GLTF-170, GLTF-215 | After the PBR-selection fix, far more meshes need tangents. **Accept:** generation is driven by the *effect's* need, not by map presence. **Landed with `GLTF-215`:** the gate is still spelled `usePbr`, and that is now exactly the acceptance rather than a violation of it — `usePbr` **is** the effect selection since `GLTF-215` replaced the map-presence rule with the material model the file declares, and strides 48/68 are the only layouts with a Tangent slot at all, so generating for anything else would write into another field. The consequence is the one the task predicted: `mat-factor-only-gold` carries no map of any kind and still gets a tangent stream, which its byte-exact stride-48 L5 golden proves. |
| GLTF-172 | Degenerate-UV triangles contribute nothing | ✅ | GLTF-170 | `fabs(denom) < 1e-12` skip. **Accept:** no NaN/Inf in any generated tangent, asserted over the corpus. |
| GLTF-173 | Missing `NORMAL`: decide generate vs default | ✔ | GLTF-084 | Today a fabricated `(0,0,1)` is written. glTF says a primitive without `NORMAL` uses flat shading. **Accept:** decision recorded; `normal-absent` matches it. **Decided: generate**, because §3.7.2.1 makes it a MUST and the old default was not a default at all — it asserted every surface faces +Z, so a model lit from any other direction was uniformly and silently wrong. CNA computes the **area-weighted vertex normal**, which for a mesh whose author already split its edges *is* the flat normal, exactly. Where a vertex is shared between faces of different orientation, true flat shading needs it duplicated once per face — that changes the vertex count and every per-vertex stream including morph deltas, which this single-vertex-array extraction cannot express — so those vertices get the average and are **counted** in `smoothedNormalVertexCountEXT`, reported by both loaders. New fixture `normal-absent` is deliberately **tilted out of the XY plane**: every other normal-less asset in the corpus is planar and CCW, so its own face normal is exactly `(0,0,1)` and none of them could ever have told the two behaviours apart — which is also why the corpus regenerated byte-identically. |
| GLTF-174 | Missing `NORMAL` + tangent generation | ⬜ | GLTF-173 | `ComputeTangentsEXT` falls back to `(0,0,1)` for the normal. **Accept:** consistent with `GLTF-173`. |
| GLTF-175 | `tangent.w` handedness end-to-end | ⬜ | GLTF-169 | Importer → stride 48/68 → `vBitangentSign` → `B = cross(N,T)*sign`. **Accept:** `tangent-handedness` renders the two halves of a normal-mapped quad with opposite, correct lighting at L7. |
| GLTF-176 | Normal/tangent transformation under node transforms | ⬜ | GLTF-118, GLTF-119 | Only relevant if `GLTF-103` chooses baking; the shader's `uNormalMatrix` needs it either way. **Accept:** `xf-scale-nonuniform` and `tangent-mirrored` correct at L4/L7. |
| GLTF-177 | Normal renormalisation policy | ⬜ | GLTF-173 | glTF says normals are unit-length; morph and skin blending break that. **Accept:** normalisation points documented and tested. |
| GLTF-178 | Normal/tangent L3 manifests for the corpus | ⬜ | GLTF-101 | **Accept:** every fixture's normals and tangents compared numerically. |
| GLTF-179 | MikkTSpace parity investigation | 🔬 | GLTF-170 | Measure the divergence from reference MikkTSpace on a seam-heavy fixture. **Accept:** the error is quantified and either accepted with numbers or scheduled. |
| GLTF-180 | Document the tangent algorithm | ⬜ | GLTF-179 | **Accept:** `docs/gltf-conformance.md` states the algorithm and its bound. |

---

### Phase 9 — UV sets and texture transform · owner: `GLTF-TEXTURE`
*Entry: Phase 8. Exit: ≥2 real UV channels, or an explicit, reported limitation.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-181 | **Decide: real second UV channel vs documented single-channel limit** | ✔ | GLTF-025 | `PbrEffect` samples every map from one shared UV set; `pbrUv2Mismatch` warns but does not fix. **Accept:** decision recorded with its vertex-stride and shader cost. **Decided: the single-channel limit stands, and is now reported on BOTH paths** (`docs/gltf-api-change-review.md` §1.9). The costs the task asked to be weighed, stated as numbers: a second `vec2` takes the unskinned PBR stride from 48 to **56** and the skinned one from 68 to **76** — and **56 is already taken**, by the skinned+coloured layout, so this is not an additive change but a collision in the exact dispatch space `VertexDeclarationFidelity.hpp` exists to police; resolving it means renumbering an existing layout or abandoning stride-keyed dispatch. The shader needs a second attribute, a second varying, and per-map selection across five maps, in both PBR programs, on every renderer. Against that, no corpus asset uses a second UV set and CNA already detects when one is present. **The real defect found here was the reporting, not the limit:** `pbrUv2Mismatch` was surfaced only by the offline tool, so the runtime path — the one most content takes — was silently wrong on exactly the same file. It now warns by name. |
| GLTF-182 | Second UV channel in the vertex layout (if adopted) | ⬜ | GLTF-181, GLTF-155 | New strides + renderer layout entries. **Accept:** `uv1-material` renders correctly at L7. |
| GLTF-183 | Per-map `texCoord` selection reaches the shader | ⬜ | GLTF-182 | **Accept:** L6 shows each map bound to its own UV set. |
| GLTF-184 | `KHR_texture_transform` per map, not just base colour | ✔ | GLTF-183 | Today only the base-colour transform is baked into the shared UVs. **Accept:** `texture-transform-per-map` correct at L7. **Decided: reported, not carried — and the *why* differs from `GLTF-181`'s in a way that matters.** The transform is applied by **baking it into the UV data**, and there is exactly one UV channel to bake into, so exactly one transform can be honoured. `MeshOut::unbakedTextureTransformsEXT` now names every map declaring a different one, on both loaders. Unlike `GLTF-181` — which needs a vertex *attribute*, hence a new stride, and the stride it needs is already taken — this one needs only a per-map **uniform**: the shared UV transformed in the shader before each sample. No ABI, no `VertexDeclaration`; a shader and uniform change in every renderer with a PBR program. That is a strictly smaller future task and is recorded as such rather than lumped in with the UV-channel one. Four tests cover both directions, including the control that a *shared* transform — the common authoring pattern — stays silent. |
| GLTF-185 | `KHR_texture_transform` maths lock | ✅ | GLTF-184 | Scale → rotate → translate, matching the spec's reference formula; `ExtractMeshAppliesTextureTransformAndEmissiveStrength` exists. **Accept:** extended with a rotation-bearing fixture. |
| GLTF-186 | Baked vs shader-side texture transform decision | ⬜ | GLTF-184 | Baking into UVs is destructive when two maps share a `texCoord` with different transforms. **Accept:** decision recorded. |
| GLTF-187 | `KHR_texture_transform.texcoord` override | ✅ | GLTF-185 | Already honoured for base colour. **Accept:** locked, and extended per map. |
| GLTF-188 | Retire or repurpose `pbrUv2Mismatch` | ⬜ | GLTF-184 | Once per-map UVs work, the warning becomes noise. **Accept:** removed or narrowed, with its test updated. |
| GLTF-189 | UV wrap behaviour with out-of-range coordinates | ⬜ | GLTF-203 | `uv-out-of-range-clamp/wrap/mirror`. **Accept:** each matches its sampler at L7. |
| GLTF-190 | Asymmetric checkerboard reference texture | ⬜ | GLTF-009 | Distinct colour per quadrant plus a numeral, so a flip or a swapped set is unmissable. **Accept:** used by every UV fixture; flips detectable at **L3** by decoded UVs, not only at L7. |
| GLTF-191 | UV origin invariant | ✔ | GLTF-105 | Both glTF and XNA use top-left. **Accept:** asserted; no flip anywhere in the importer. **Asserted with `GLTF-105`**, on authored UVs that are asymmetric in V (0.75, 0.5, 0.0625) so a mirror cannot land back on itself. This is also the assertion `GLTF-192`'s render-target V-flip must not be allowed to leak into: that flip belongs to the sampler at draw time, not to the asset's own coordinates. |
| GLTF-192 | Render-target V-flip must not leak into asset UVs | 🔬 | GLTF-191 | `cnaSampleUV(vUV, uRtFlipV.*)` exists for render targets. **Accept:** a fixture proves a non-render-target draw is unaffected. |

---

### Phase 10 — Textures, images, samplers, colour space · owner: `GLTF-TEXTURE`
*Entry: Phase 9. Exit: samplers are honoured and every map is sampled in the right colour space.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-193 | Embedded `bufferView` images | ✅ | GLTF-026 | **Accept:** locked at L6 (texture dimensions + a sampled pixel). |
| GLTF-194 | External URI images | ✅ | GLTF-028 | **Accept:** locked; a missing file errors clearly. |
| GLTF-195 | `data:` URI images | ✅ | GLTF-029 | **Accept:** locked. |
| GLTF-196 | PNG and JPEG decode | ✅ | GLTF-193 | Via `stb_image` in `Texture2D::FromStream`. **Accept:** both locked with a known-pixel fixture. |
| GLTF-197 | Image cache keyed by `cgltf_image*` | ✅ | GLTF-193 | Correct within one load. **Accept:** locked; a shared image decodes once. |
| GLTF-198 | Path containment for image URIs | ✔ | GLTF-032 | Same traversal exposure as buffers. **Accept:** rejected with a clear message. **Landed with `GLTF-032`** — one resolver, two gates: the up-front sweep covers `data->images` alongside `data->buffers`, and `ExtractImage` resolves through the same function rather than joining the URI itself, so a caller reaching it without the sweep is still contained. `ExtractImage`'s hand-rolled `cgltf_decode_uri` + `gltfDir / uri` is gone. |
| GLTF-199 | MIME-type vs content sniffing | ⬜ | GLTF-196 | The `data:` branch guesses `png` for anything not `image/jpeg`. **Accept:** content-sniffed, with the MIME type as a hint only. |
| GLTF-200 | Unsupported image formats must not vanish silently | ✔ | GLTF-035 | The real shape is one level up from the row's own description: the texture's `image` is null (the pixels hang off `KHR_texture_basisu`/`EXT_texture_webp` instead), so `ExtractImage` was never reached at all and every downstream check simply read "no texture on this slot". **Landed:** one `ImageForTexture` helper behind all five map finders, feeding `MeshOut::unsupportedTextureSourcesEXT` — one entry per map, naming the map and the extension — which both loaders log. It also catches the second shape, a plain `source` whose `mimeType` is `image/ktx2`/`image/webp`/`image/basis`, at import where the map's name is still known rather than at a decode failure that cannot say which map it was. |
| GLTF-201 | Zero-byte / corrupt image handling | ⬜ | GLTF-196 | **Accept:** deterministic error, no crash under ASan. |
| GLTF-202 | **Import `cgltf_sampler`** | ✔ | GLTF-025 | `cgltf_sampler`, `mag_filter`, `wrap_s` all have **0 occurrences**. **Accept:** sampler data reaches the model. **Landed:** `SamplerOut` per texture slot on `MeshOut`, read from the texture each material view names, and carried to a new CNAEXT `ModelMeshPart::SamplerStatesEXT`. **This overrules `GLTF-207`'s own sketch**, which proposed hanging the array off `ModelMeshPart::Tag`: `Tag` already *is* `MorphTargetDataEXT` for a morphing part, so a morphing part with clamped textures could not have expressed both — the same one-object collision §1.5 records for rigid clips. A plain property, as `GLTF-073` already established for `PrimitiveTypeEXT`. Per **slot** rather than per material, because glTF attaches a sampler to a texture and a material may legitimately clamp its base colour while repeating its normal map. Recorded as `docs/gltf-api-change-review.md` §1.8. |
| GLTF-203 | Map glTF filters/wraps to `SamplerState` | ✔ | GLTF-202 | The §14.2 table. **Accept:** every row tested; `uv-out-of-range-*` correct at L7. **Landed, and it corrected §14.2.** The section claims XNA "cannot express independent min/mag filters for the four mixed combinations" — it can: the nine `TextureFilter` values, `MinLinearMagPointMipLinear` and its three siblings among them, cover all eight min×mag×mip combinations **exactly**, so those four need no approximation at all. The one real approximation is elsewhere: glTF's `NEAREST`/`LINEAR` minFilters mean *no mipmapping*, which XNA expresses through the texture's level count rather than through `TextureFilter`, so the mip mode is arbitrary there; `SamplerOut::minFilterHasNoMipStage` makes that visible instead of implied and it becomes observable when `GLTF-206` generates levels. Every row is tested directly — `MapGltfSamplerEXT` takes raw glTF enum values, so the whole table is exercised without a file, which no realistic number of fixtures could match. **Not proved:** the end-to-end path, because **no corpus asset carries a texture at all** (`GLTF-190`'s reference image is what the texture group waits on); and the `uv-out-of-range-*` L7 half, blocked on `GLTF-009`. |
| GLTF-204 | Document the min/mag approximation | ⬜ | GLTF-203 | XNA cannot express independent min/mag filters. **Accept:** approximation table in `docs/`, surfaced in the import report. |
| GLTF-205 | Default sampler when absent | ⬜ | GLTF-203 | glTF: repeat + auto filter. **Accept:** `LinearWrap`, tested. |
| GLTF-206 | Mipmap generation policy | 🔬 | GLTF-203 | `Texture2D::FromStream` produces one level; the mipmapped min filters assume more. **Accept:** decision recorded and implemented or explicitly deferred with a report entry. |
| GLTF-207 | Where sampler state lives on the model | ⬜ | GLTF-202, GLTF-025 | `ModelMeshPart` has no sampler property. Proposed CNAEXT `SamplerStateArrayEXT` attached like `MorphTargetDataEXT`. **Accept:** design reviewed and tested. |
| GLTF-208 | Sampler state actually applied at draw time | ⬜ | GLTF-207 | **Accept:** L6 capture shows the right `SamplerState` per slot. |
| GLTF-209 | **Colour-space decision** | ✔ | GLTF-025 | §13.3 options A/B/C. Today every map is RGBA8 UNORM sampled raw, with no output encode. **Accept:** decision recorded with rationale; provisional recommendation is B (shader-side). **Decided: B**, recorded in full as `docs/gltf-api-change-review.md` §1.7. A was rejected on blast radius — a new `SurfaceFormat` across 41 renderers to fix a glTF defect is the wrong order of work — and C on precision plus the fact that it would make an imported texture disagree with the file it came from. B is renderer-local, adoptable one renderer at a time, and observable at L6 without a renderer at all. The decision records three things the shape had to get right: **three flags rather than one**, because "what this texture contains" (×2) and "where this fragment is going" are different kinds of statement and an sRGB render target needs the third switched off without the first two changing; **no flag at all for the three linear maps**, because §3.9.2 leaves no choice there and an invented choice is a thing someone eventually sets wrong; and **factors are never transferred**, only texture samples, since the factors are already linear and `KHR_materials_emissive_strength` may push emissive above 1. |
| GLTF-210 | sRGB decode for base colour and emissive | ✔ | GLTF-209 | **Accept:** a known-sRGB fixture produces the manifest's linear values at L6/L7. **Landed:** `GpuDrawParams::pbrBaseColorTextureIsSrgb`/`pbrEmissiveTextureIsSrgb` carry the declaration; both PBR effects expose it as CNAEXT properties defaulting to glTF's own rule; `EasyGLRenderer`'s two PBR shaders decode the **samples** and multiply the linear factors afterwards — though see the verification boundary below. The transfer itself is stated once in `CNA/Internal/Graphics/SrgbTransfer.hpp` — as a macro of string literals, because a renderer builds shader source by literal concatenation and a `const char*` cannot join that — and the C++ functions beside it are the same arithmetic, so the GLSL and the CPU cannot drift into two slightly different curves. `SrgbTransferTests` pins endpoints, mid-grey (the 2.3× error the fix removes, stated as a number), both knees **branch-against-branch** rather than by finite difference, a 256-level round trip, and the no-clamp property `KHR_materials_emissive_strength` depends on. The L6/L7 half of the acceptance is split: L6 asserts the declaration reaches every PBR draw, and the pixel-level half waits on `GLTF-009`. **Verification boundary:** `EasyGLRenderer.cpp` was **not compiled** — the EasyGL family needs sibling `../easy-gl` and `../meta-gl` checkouts this tree does not have, so no renderer-configured build can be configured and the STUB build does not compile that file. The one non-obvious construct, splicing a literal macro into shader-source concatenation, has its own test (the first version used a `const char*` and would not have compiled); the rest is string literals, an `int` member and a `set_uniform` overload the same file already uses. A session with the siblings should build `OPENGLES3` once to close it. |
| GLTF-211 | Linear handling for normal / occlusion / metallic-roughness | ✔ | GLTF-209 | **Accept:** asserted unchanged by the decode path. **Landed as an absence, deliberately:** these three maps get **no flag at all**. §3.9.2 declares them linear unconditionally, so there is nothing to configure, and adding a flag would invent a choice someone would eventually set wrong. The decode in the shader touches the base-colour and emissive samples by name and nothing else, so the three are unchanged by construction rather than by a guard that could be inverted. Recorded in `docs/gltf-api-change-review.md` §1.7. |
| GLTF-212 | Linear → sRGB encode on output | ✔ | GLTF-209 | **Accept:** a flat-lit fixture's framebuffer values match the manifest. **Landed:** `pbrEncodeOutputToSrgb`, on by default and settable per effect — unlike the two decode flags this is a real policy choice, since an application drawing into an sRGB target or running its own tone map must switch it off. Encoding happens **last** and touches **RGB only** (§3.9.4 makes alpha coverage, never colour), and the fog mix now happens in linear space with `uFogColor` decoded first, since mixing an encoded colour into a linear result would tint the fade as it thickens. **A boundary is named rather than hidden:** because the encode lives in the PBR shader, a scene mixing `PbrEffect` with the stock XNA effects writes differently encoded colour into one render target. That is the price of option B over A; it is not a regression, since the stock effects behave exactly as before and XNA content never had colour management to lose. The proper fix is a colour-managed render target, which belongs with the `SurfaceFormat` work. The framebuffer-value half of the acceptance waits on `GLTF-009`. |
| GLTF-213 | Colour space across every PBR-capable renderer | ⬜ | GLTF-212 | **Accept:** identical L7 within tolerance on `OPENGLES3` and `VULKAN`. |
| GLTF-214 | Vertex-colour colour space | ✔ | GLTF-089 | glTF vertex colours are **linear**; `BasicEffect` treats them as-is. **Accept:** documented and tested. **Established: they are linear, they stay linear, and that is now stated.** §3.7.2.1 declares `COLOR_0` a **linear** value — unlike `baseColorTexture` and `emissiveTexture`, which §3.9.2 declares sRGB-encoded — so `GLTF-210`'s decode must not touch it. Applying the transfer to a vertex colour would darken it by the same 2.3× mid-grey factor the decode exists to *correct* on a texture. The finding is that this is already right, and right **by construction** rather than by luck: the decode is applied in the shader to two named texture samples, and a vertex colour is neither. What was missing was anything saying so — which is exactly what a future "apply the transfer everywhere" simplification would have needed to run into. The test additionally asserts the fixture has a mid-range component, since at 0 and 1 the sRGB transfer is the identity and a fixture of only those could not tell a decoded vertex colour from an untouched one. |

---

### Phase 11 — Materials and PBR · owner: `GLTF-MATERIAL`
*Entry: Phase 10. Exit: every §13.1 row is either implemented and tested, or explicitly rejected.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-215 | **Replace the map-presence PBR-selection rule** | ✔ | GLTF-011 | `usePbr = !colored && (normalImage \|\| metallicRoughnessImage)`; `f8` proved a gold factor-only material becomes a white `BasicEffect`. glTF's default material *is* metallic-roughness. **Accept:** a primitive whose material has `pbrMetallicRoughness`, or has no material, imports as PBR. **Landed:** the rule is now the material **model** the file declares, not which maps it carries. Metallic-roughness is glTF's default in two distinct ways, and both are honoured: a primitive with no material at all gets the default material, and a material that merely omits the optional `pbrMetallicRoughness` object still uses that model with default factors. Only `KHR_materials_pbrSpecularGlossiness` and `KHR_materials_unlit` — neither of which CNA's PBR shaders implement — are excluded, alongside the existing `!colored` limit (no PBR shader reads a vertex Color stream). **Consequences, all deliberate:** stride 32 no longer occurs anywhere in the corpus — an unskinned uncoloured primitive is 48 and a skinned one 68 — and the `DualTextureEffect` glTF path is **superseded**: a base-colour + occlusion material now reaches `PbrEffect`'s real `OcclusionMap` instead of CNB-72/73's halved occlusion-as-lightmap approximation, so the CNB-88 brightness compensation is no longer applied to it. `DualTextureEffect` itself is untouched and still reachable through every other content path; a test pins the replacement so it cannot be undone silently. |
| GLTF-216 | **Carry `baseColorFactor` end-to-end** | ✔ | GLTF-215 | Not read anywhere; `MeshOut` has no field. `PbrEffect::DiffuseColor` + `Alpha` already exist and reach the shader. **Accept:** `mat-factor-only-gold` shows `(1,0.72,0.315)` and alpha `0.5` at L6. **Landed:** `MeshOut::baseColorFactor` (CNAEXT) is read from the material and carried to `PbrEffect`/`SkinnedPbrEffect`'s `DiffuseColor` (RGB) and `Alpha` (A) on **both** loaders. The `.cnj` needed no schema addition — it emits `diffuseColor`/`alpha`, which the reader already consumed for every effect. `mat-factor-only-gold` carries `(1, 0.72, 0.315)` with alpha `0.5` at L3, asserted against the fixture's own spec-derived expectation. The L6 half of the acceptance criterion waits on `GLTF-008`. |
| GLTF-217 | Primitive with no material at all | ✔ | GLTF-215 | Falls to white `BasicEffect`. **Accept:** `mat-default` imports as the glTF default metallic-roughness material. **Landed with `GLTF-215`:** `prim.material == nullptr` selects PBR, which is what glTF's default material means. Every materialless corpus fixture now imports at stride 48 (or 68 skinned) with the default factors. |
| GLTF-218 | `baseColorFactor` × `baseColorTexture` | ⬜ | GLTF-216 | The two multiply. **Accept:** `mat-basecolor-factor-times-texture` matches at L7. |
| GLTF-219 | `metallicFactor` / `roughnessFactor` ungated | ✔ | GLTF-215 | Read only when `usePbr` today. **Accept:** always read for a metallic-roughness material. **Landed:** both are read for any material carrying `pbrMetallicRoughness`, outside the old `if (usePbr)` guard that was the second half of D7. |
| GLTF-220 | Factors reach the shader | ✔ | GLTF-219 | **Accept:** L6 capture matches the file. **Landed with `GLTF-008`:** `MaterialFactorsReachTheBoundEffect` compares every PBR draw's `diffuseColor`, `pbrMetallicFactor`, `pbrRoughnessFactor` and `emissiveColor` against the manifest's own spec-derived L3 material block, across the corpus. This is the assertion D7 would have failed for the whole of the audit. |
| GLTF-221 | `emissiveFactor` ungated | ✔ | GLTF-215 | **Accept:** `mat-emissive-factor` correct at L6. **Landed:** read for any material, with `KHR_materials_emissive_strength` still applied in the spec's own order. |
| GLTF-222 | `KHR_materials_emissive_strength` ungated | ✔ | GLTF-221 | Applied only when `usePbr`. **Accept:** `mat-emissive-strength` correct, HDR values > 1 preserved. **Landed:** `GLTF-219`/`GLTF-221` moved the whole factor block out from behind the `usePbr` guard, so the multiplier is read for any material; `mat-emissive-strength` now proves it end to end. The fixture states all three numbers separately — the authored `[0.4,0.2,0.1]`, the strength `5`, and the product `[2.0,1.0,0.5]` — because a fixture that gave only the product could not tell "strength applied" from "a factor authored at that value in the first place". The L6 assertion names the two failure shapes apart: an emissive equal to the authored factor means the strength was dropped, and one at or below 1 means it was clamped, which would make carrying the extension pointless. |
| GLTF-223 | Emissive applied after the texture, per spec order | ✅ | GLTF-222 | **Accept:** locked. |
| GLTF-224 | `normalTexture.scale` | ✔ | GLTF-025 | Never read; no effect parameter exists. **Accept:** new `PbrEffect::NormalScale`; `mat-normal-scale` correct at L7. **Landed** as CNAEXT `NormalScaleEXT` on both PBR effects, `GpuDrawParams::pbrNormalScale`, and an EasyGL shader that scales the sampled tangent-space normal's **X and Y only** — scaling Z as well would merely rescale the vector, which normalization then undoes, so the perturbation would not change at all. **A trap the fixture caught:** cgltf sets a texture view's `scale` to 1 only when it *parses* that view, so a material with no `normalTexture` object leaves it zeroed by `calloc`. Reading it straight through gave 0 — "flatten the map completely" — for every material that simply does not mention one, the exact opposite of glTF's default. An undeclared view now falls back to 1 explicitly, and a test pins that case separately from the authored one. Proven at L6; L7 waits on `GLTF-009` and on the corpus's first texture (`GLTF-190`). |
| GLTF-225 | `occlusionTexture.strength` | ✔ | GLTF-025 | Never read. **Accept:** new `PbrEffect::OcclusionStrength`; `mat-occlusion-strength` correct at L7. **Landed** alongside `GLTF-224`, applied with §3.9.3's own formula `1 + strength * (sampled - 1)`. That form matters: at `strength = 0` the result is **1** — no occlusion at all, whatever the map holds — where the plausible wrong formula (multiplying by the strength) would darken everything to black. The same undeclared-view trap applies and is handled the same way. |
| GLTF-226 | Occlusion channel is `.r` | ✅ | GLTF-225 | Shader reads `.r`. **Accept:** locked with a three-distinct-channel fixture. |
| GLTF-227 | Normal-map decode `rgb*2−1` in TBN | ✅ | GLTF-175 | **Accept:** locked at L7. |
| GLTF-228 | **`alphaMode`** | ✔ | GLTF-025 | Never read; `PbrEffect` has `uAlphaTest` but nothing configures it. **Accept:** CNAEXT `AlphaModeEXT` (`Opaque`/`Mask`/`Blend`); all three fixtures correct at L7. **Landed** behind the gate (`docs/gltf-api-change-review.md` §1.3), which chose the shape: `AlphaModeEXT` (`Opaque`/`Mask`/`Blend`) declared **once**, in `Microsoft::Xna::Framework::Graphics` rather than on an effect, so `CNAEXT.md` §5.5's separately-sketched `PbrMaterial::AlphaMode` binds to the same type instead of becoming a second enum with a conversion function. Carried on `MeshOut`, through the `.cnj` (written only when not `OPAQUE`, so an ordinary material's `.cnj` is unchanged) and onto both PBR effects. The L7 half of the acceptance criterion waits on `GLTF-009`. |
| GLTF-229 | `alphaCutoff` | ✔ | GLTF-228 | Default 0.5. **Accept:** `alpha-mask` cuts exactly at the manifest threshold. **Landed:** default `0.5`, carried end-to-end alongside `alphaMode`. Kept even when the mode is not `Mask`, because a material that switches modes must not lose the threshold it authored. |
| GLTF-230 | `BLEND` needs blend state and draw ordering | ⬜ | GLTF-228 | **Accept:** `alpha-blend` composites correctly; ordering policy documented (CNA does not sort by default). |
| GLTF-231 | `doubleSided` | ✔ | GLTF-025 | Never read. **Accept:** `double-sided` renders both faces; a single-sided fixture culls back faces. **Landed as carried state, deliberately not as applied state** (gate §1.4). The flag survives import, the `.cnj` and both effects. **Applying** it is a `RasterizerState::CullMode` change, which in XNA is per-draw device state the application sets — having `Model::Draw` mutate it as a side effect would surprise every XNA caller and collides with `GLTF-230`'s blend-state and draw-ordering work, whose acceptance is an L7 comparison. Recording that boundary is what stops the flag looking implemented while nothing honours it. |
| GLTF-232 | `doubleSided` interacts with negative-scale winding | ⬜ | GLTF-231, GLTF-116 | **Accept:** a mirrored single-sided fixture still shows its front face. |
| GLTF-233 | Metallic-roughness channel semantics | ✅ | GLTF-220 | Shader reads `.g` roughness, `.b` metallic — correct per spec. **Accept:** locked with a three-distinct-value fixture. |
| GLTF-234 | Channel semantics on every renderer | ⬜ | GLTF-233 | **Accept:** the same fixture passes on `OPENGLES3` and `VULKAN`; a swapped G/B would fail. |
| GLTF-235 | BRDF analytic spot-checks | ⬜ | GLTF-233 | GGX D, Smith-Schlick-GGX G (`k=(r+1)²/8`), Schlick F, `F0 = mix(0.04, albedo, metallic)` — matches Appendix B. **Accept:** normal-incidence and grazing values within tolerance, not an image diff. |
| GLTF-236 | Material data model on the model | ⬜ | GLTF-025 | `MeshOut` carries loose fields; `CNAEXT.md` already sketches `PbrMaterial`. **Accept:** a coherent carrier for every §13.1 field on both load paths. |
| GLTF-237 | `.cnj` material serialisation | ⬜ | GLTF-236, GLTF-129 | **Accept:** every field round-trips; offline and runtime paths agree at L6. |
| GLTF-238 | Material sharing / de-duplication | ⬜ | GLTF-236 | Two primitives with the same material should share one `Effect`. **Accept:** effect count matches the distinct material count. |
| GLTF-239 | `DualTextureEffect` occlusion-as-lightmap path | ✅ | GLTF-215 | `RemapOcclusionImageForDualTextureEXT` halves RGB for the `0.5 = neutral` convention. **Accept:** kept and tested, but no longer chosen for a genuine PBR material. |
| GLTF-240 | `BasicEffect`/`SkinnedEffect` remain reachable | ⬜ | GLTF-215 | For genuinely non-PBR content. **Accept:** selection policy documented and tested. |
| GLTF-241 | Vertex-coloured PBR | ✔ | GLTF-215 | `usePbr && colored` is currently impossible. **Accept:** either supported (new stride + shader) or reported — not silently downgraded. **Landed as the second outcome: reported.** Supporting it needs a vertex layout holding a Colour *alongside* a Tangent, which no CNA stride has — and the obvious packing (12+12+16+8+4) collides with the existing stride 52, exactly the reinterpretation hazard `VertexDeclarationFidelity` exists to stop — plus a shader variant on every renderer. That is the same blast-radius argument that ruled out colour-space option A. **The fixture found the loss runs deeper than the material:** stride 24 has no Normal slot either, so a coloured primitive's authored NORMAL is discarded too and it cannot be lit *at all*, not merely lit without PBR. `MeshOut` now names both losses (`unsupportedMaterialModelEXT`, `droppedNormalForStrideEXT`) and both loaders log them. `mat-vertex-color-pbr` records the divergence in the corpus ledger as `partially-remediated` with `GLTF-238` remaining — which immediately made the ledger demand an executable test for it, the bookkeeping working as designed on the first non-audit defect it was given. The material's factors do still reach `MeshOut` (`GLTF-219`/`GLTF-221`), so the importer understood the material perfectly and the vertex layout is what could not carry it. |
| GLTF-242 | Lighting-default policy for CNA | ⬜ | GLTF-215 | `PbrEffect` defaults to zero ambient with all lights disabled ⇒ black. Correct XNA behaviour. **Accept:** effect defaults unchanged; the import report states how many lights were contributed. |
| GLTF-243 | Record the IBL / tone-mapping boundary | ⬜ | GLTF-242 | **Accept:** documented that "not IBL-accurate" is **not** a CORE conformance failure. |
| GLTF-244 | Full material L6/L7 regression over the corpus | ⬜ | GLTF-237 | **Accept:** all 12 `mat-*`/`alpha-*`/`double-sided` fixtures green on two renderers. |

---

### Phase 12 — Skinning · owner: `GLTF-SKIN` · **CRITICAL PATH**
*Entry: Phase 5 (transforms) — **waived for the Track A tasks `GLTF-245`, `GLTF-247`, `GLTF-248`,
`GLTF-260`** (§28.1); `GLTF-260` still genuinely needs `GLTF-114`. Exit: every `skin-*` fixture
passes numerically at L4 **and** `GLTF-260` proves no double application.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-245 | **[P0] `BuildSkeleton` must walk the full scene ancestry** | ✔ | GLTF-114 | Parent links are resolved **only within the joint set**; `f9` proved an armature `translation [0,100,0]` is dropped from `bindPoseLocal` while the file's IBM keeps it ⇒ skin transform `translate(0,−100,0)`. `globalTransform(joint)` must include **every** scene ancestor, joint or not, **and every ancestor above `skin.skeleton`** (§15.1.1) — no early stop. The palette this produces is the `paletteIndex` space of §15.1.2 and must not reorder the scene hierarchy. **Accept:** `skin-armature-ancestor` places the vertex at `(1,0,0)`; a test asserts an ancestor transform above `skin.skeleton` still contributes. **Critical path.** **Landed:** `BuildSkeleton` gained a four-argument overload taking the scene graph and the skinned mesh node's world transform. A root joint's full scene ancestry — every ancestor, joint or not, and regardless of `skin.skeleton` — is resolved from `SceneGraphOut`, falling back to `cgltf_node_transform_world` for a joint parented outside the default scene rather than dropping the chain. |
| GLTF-246 | Lock IBM reading | ✅ | GLTF-059 | `ConvertGltfMatrix` + `ScaleTranslation` read IBMs correctly (`f9`). **Accept:** locked at L2. |
| GLTF-247 | **[P0] Add the `inverse(meshNodeWorld)` term** | ✔ | GLTF-245 | glTF §3.8: the mesh node's own transform must be cancelled. Absent entirely today. **Accept:** `skin-mesh-node-transform` (mesh node `T=[0,0,50]`) places the vertex at `(1,0,0)`. **Critical path.** **Landed:** The `inverse(globalTransform(meshNode))` term is composed onto the same prefix. Both terms ride on `BoneOut::parentWorldPrefix` / `SkinningData::SkeletonRootPrefix` rather than being folded into the bind pose, so an animated root joint substitutes only its own local transform and cannot undo them. `AnimationPlayer` composes `world(root) = local * prefix`; an empty prefix array reads as all-identity, so a pre-existing skeleton is unaffected. |
| GLTF-248 | **[P0] The whole `skin-*` ladder passes at L4** | ✔ | GLTF-247 | The 13 fixtures in §15.4. **Accept:** every expected world position matches to 1e-6. **Skinning is not complete here** — `GLTF-260` closes the track. **Critical path.** **Landed:** `skin-armature-ancestor` now yields a joint matrix of exactly identity, was `translate(0,−100,0)`. `GltfSkinSpaces` asserts the joint matrix and the resulting skinned vertex through the real loader; D8 is `fixed` in the ledger with the audit's measurement under `priorActual` and its inverted known-defect test deleted. |
| GLTF-249 | Honour `skin.skeleton` **as a root hint, never as a traversal stop** | ✔ | GLTF-245 | Parsed by cgltf, never read. §15.1.1: it names the declared skeleton root — useful for locating and naming the rig — but **must not truncate the ancestry** used for `globalTransform(joint)`. An implementation that walks up only until `skin.skeleton` recreates D8 in a new disguise. **Accept:** `skin.skeleton` is honoured as the declared/semantic root **while** joint global transforms are still derived from the complete scene-node ancestry the glTF skinning equation requires; a fixture in which a transform-bearing ancestor sits **above** `skin.skeleton` still produces the correct world position, and a fixture where `skin.skeleton` differs from the natural common ancestor is covered. No required ancestor transform may be dropped because an ancestor lies above `skin.skeleton` or is not itself a joint. **Landed:** the traversal-stop half was already right — `GLTF-245` made the ancestry walk scene-driven and the code says in as many words that `skin.skeleton` does not truncate it — but nothing proved it, and the **hint half did not exist at all**: `skin->skeleton` was read by nobody, so an application had no way to find the rig root the file declares. It is now recorded on `SkeletonResult` and carried to `SkinningData::SkeletonRootNodeIndexEXT`/`SkeletonRootNameEXT` as a `sceneNodeIndex` (§15.1.2), so it indexes `Model::Bones` like any other node. It is **data, never control flow**, and both header comments say so. `skin-skeleton-hint` makes each half fail separately: `Outer` carries `T(0,100,0)` and is the *parent* of the declared root, so a truncated walk drops it and reproduces D8's exact 100-unit displacement; and the declared root is deliberately **not** the joints' common ancestor (`Mid` is), so substituting one for the other is visible. The manifest states the **wrong** answer by name under `truncatedAtDeclaredRoot`, so the test asserts against both poles rather than merely away from one. |
| GLTF-250 | Missing `inverseBindMatrices` ⇒ identity | ✅ | GLTF-246 | Spec-correct today, untested. **Accept:** `skin-no-ibm` locked. |
| GLTF-251 | Joint matrix formula documented in one place | ⬜ | GLTF-247 | §15.1 in both column- and row-vector form. **Accept:** `docs/gltf-conventions.md`, referenced from the code. |
| GLTF-252 | Joint palette ordering — **keep it separate from scene-node identity** | ✅/⬜ | GLTF-245, GLTF-113 | Breadth-first with an `oldToNew` remap; `LoadsSkinnedAnimatedModelDirectlyFromGltfWithReversedJointOrder` covers it, and it is correct **as a palette operation**. §15.1.2 makes the two index spaces explicit: `sceneNodeIndex` (stable scene/`ModelBone` identity, used by the hierarchy, rigid animation, cameras/lights and game code) versus `paletteIndex` (skin-local GPU palette slot, used by `SkinningData`, `BlendIndices` and `uBones[]`). **Accept:** the existing reorder is locked; the scene hierarchy introduced by `GLTF-113` is **not** reordered to match palette order; `JOINTS_0` remapping targets `paletteIndex`; the per-skin `sceneNodeIndex ↔ paletteIndex` mapping is explicit and tested in both directions. No public API is introduced unless a test proves one is required. |
| GLTF-253 | `AnimationPlayer` composition order | ✅ | GLTF-252 | `world[i] = local[i] * world[parent]`, `skin[i] = IBM[i] * world[i]` — correct XNA row-vector order. **Accept:** locked. |
| GLTF-254 | `BlendIndices` `uint8` truncation | ✔ | GLTF-093 | Silent above 255 joints. **Accept:** `skin-256-joints` errors or uses a wider index; never wraps. **The 256-joint case is unreachable** — `GLTF-261` refuses any skin past 72 joints in `BuildSkeleton`, so no remapped index can approach 255 and `skin-256-joints` would only re-test `GLTF-261`'s own boundary. A runtime guard now couples the two limits explicitly rather than leaving the `uint8` cast trusting a constant declared far away. **The real residue was something else**, found while checking this: an out-of-range `JOINTS_0` value fell back to joint **0** unconditionally, binding the vertex to the root and dragging it there — the same collapse as `H6`/`H12`, arriving from a malformed file. Now refused, **but only when the influence carries weight**: a zero-weighted slot with a garbage index is the universal exporter padding pattern and contributes nothing to the skin equation, so refusing it would reject real assets to prevent nothing. Two fixtures pin exactly that boundary — `skin-joint-index-out-of-range` (weight 0.4, refused) and `skin-joint-index-padding` (same indices, zero weight, imports). |
| GLTF-255 | `WEIGHTS_0` decode | ✅ | GLTF-094 | **Accept:** locked at L5 for `FLOAT` and normalized integer forms. |
| GLTF-256 | **Weight renormalisation policy** | ✔ | GLTF-255 | Never renormalised. Weights summing to < 1 shrink vertices toward the origin — an independent collapse mechanism (H12). **Accept:** `skin-unnormalized` behaves per a written policy; the report flags non-conforming input. **Decided: renormalise, and report.** Refusing would be useless — a slightly-off sum is what quantised exporters routinely emit — and leaving it is H12: the skin equation is a weighted sum of joint matrices, so weights summing to 0.75 apply 0.75 of the vertex's transform, dragging it three-quarters of the way toward a joint at the origin. **The two exceptions matter as much as the rule**, and `skin-unnormalized` authors all three cases in three vertices so they can fail separately: a sum within `1e-4` of 1 is float error and is left untouched *and uncounted*; a sum of **zero** means the vertex is unweighted, and `0/0` is not a normalisation, so it is counted and left alone rather than assigned to an arbitrary joint. `MeshOut` carries the renormalised count, the zero-weight count and the **worst deviation** — the last because a count alone cannot tell exporter quantisation from a broken file. **L2 holds the authored weights and L3 the renormalised ones**, which is exactly the distinction between the two layers: L2 is what the accessor decodes to, L3 is what the importer understood after its own documented policy ran. |
| GLTF-257 | `JOINTS_1`/`WEIGHTS_1` — support or report | ✔ | GLTF-095 | Silently ignored. **Accept:** >4 influences are either supported or reported; never silently truncated. **Decided: reported, because it cannot be supported** — the same blast-radius argument as the second UV channel (`GLTF-181`) and vertex-coloured PBR (`GLTF-241`). XNA's `BlendIndices`/`BlendWeight` are four-component by definition; carrying eight would need a new stride, a new `VertexDeclaration`, and a skinning shader change in **every** renderer, to serve a case one file in a thousand authors. **The finding that makes truncation survivable is `GLTF-256`'s renormalisation**: without it a vertex whose set-0 weights sum to 0.6 applies three-fifths of its transform and is dragged toward the origin — `H12` arriving by a second road. With it, the retained four are rescaled to 1, so the result is a *coarser* skin, not a collapsed one. `skin-eight-influences` pins both halves: vertex 0 sums to 0.6 as authored and to exactly 1 as imported. |
| GLTF-258 | Influence count reaches the shader | ✔ | GLTF-255 | `WeightsPerVertex` / `uWeightsPerVertex`. **Accept:** L6 capture. **Landed with `GLTF-008`:** asserted as four on every skinned corpus draw by `SkinnedDrawBindsThePaletteAndFourInfluencesPerVertex`. |
| GLTF-259 | Skinning is applied only to skinned primitives | ✅ | GLTF-245 | Gated on `JOINTS_0 && WEIGHTS_0`. **Accept:** locked. |
| GLTF-260 | **[P0] A skinned mesh's node transform must not be applied twice** | ✔ | GLTF-247, GLTF-114, GLTF-248 | Remediating D1–D3 changes the assumptions D8's fix rests on: Phase 5 gives the skinned mesh's node a real `ModelBone` for the first time, and that is precisely the transform glTF requires to be *cancelled* for a skinned mesh (§15.1). Without this task the node-hierarchy work can silently re-apply what `GLTF-247` just cancelled, leaving skinning double-transformed rather than fixed. **Accept, both halves required:** (a) the mesh-space cancellation term `inverse(globalTransform(meshNode))` is present and effective — `skin-mesh-node-transform` (mesh node `T=[0,0,50]`) places the vertex at `(1,0,0)`; and (b) the new real node hierarchy does **not** re-apply that same transform — the identical fixture yields `(1,0,0)`, not `(1,0,50)`, with the node bone present and non-identity, asserted at L4 through **both** loaders. **Critical path — skinning is not complete without it.** **Landed:** New fixture `skin-mesh-node-transform` (mesh node `T=[0,0,50]`, identity joint and IBM) makes the three outcomes distinguishable: no cancellation → identity, cancelled once → `T(0,0,−50)`, cancelled twice → `−100`. Both halves asserted — the cancellation exists and is applied exactly once, **and** the node's bone still exists carrying its transform while the mesh stays parented to the identity root, so the hierarchy cannot silently re-apply it. |
| GLTF-261 | `MaxBones = 72` | ✔ | GLTF-025 | Rigs above 72 joints silently exceed the palette. Raising it changes a real XNA constant and every renderer's uniform array. **Accept:** `skin-73-joints` errors clearly, or the limit is raised deliberately with the cost recorded. **Decided: error clearly, do not raise the limit.** The palette is 72 `mat4`s — a real XNA constant every renderer's uniform array is sized by — so raising it is not a local change. `BuildSkeleton` now refuses an over-limit rig, naming the skin, its joint count and the limit, instead of letting it surface later as `SetBoneTransforms`' generic "exceeds MaxBones", which says nothing about which asset caused it. **Refusing rather than truncating is the whole point:** truncation leaves the joints past the limit at the identity, so every vertex bound to them collapses toward the origin — the audit's H6, and exactly the silent wrongness this campaign removes. `skin-73-joints` sits **one** past the boundary, with every vertex bound to the *last* joint (the one a truncating reader drops), and the test asserts 72 is still accepted, because an off-by-one in the check is as much a failure as no check. Uncovered along the way: the L3 oracle let a refused skeleton throw through its whole sweep, so one malformed fixture took every other fixture's L3 with it; it is total now, as its own design principle already claimed. |
| GLTF-262 | Bind pose must be applied when no clip is playing | ✔ | GLTF-253 | `SkinnedEffect` defaults to 72 identity bones, so an unanimated skinned model renders **unskinned**. **Accept:** loading a skinned model yields a usable bind-pose palette without game-code setup, or the requirement is documented and the viewer satisfies it (`GLTF-425`). **Landed:** the first branch, not the second. An identity palette is not "no skinning" — it means every joint matrix is the identity, so the mesh is posed in joint space and glTF's own `inverse(globalTransform(meshNode))` cancellation (`GLTF-247`) never applies; an unposed skinned model rendered **wrong**, not merely still, and `GLTF-008`'s L6 capture is what turned that from a suspicion into a measurement. Both loaders now call the new `ApplyBindPoseBoneTransformsEXT` once after building the model, which computes the palette exactly the way an application would — an `AnimationPlayer` over the model's own `SkinningData` with no clip started — rather than deriving a second notion of "bind pose" that could disagree with playback at `t = 0`. Any later `SetBoneTransforms` simply overwrites it. Approved through the gate as `docs/gltf-api-change-review.md` §1.6, which records why real XNA's sample gets away without this (its content pipeline bakes the mesh into skeleton space, so *there* the bind pose really is the identity palette — glTF's is not). |
| GLTF-263 | Bone palette upload | ✔ | GLTF-262 | `SetBoneTransforms` → `uBones[72]`. **Accept:** L6 capture matches `GetSkinTransforms()`. **Landed with `GLTF-008`:** the test performs the application's own step — `AnimationPlayer::GetSkinTransforms()` into `SkinnedPbrEffect::SetBoneTransforms()` — and then asserts the captured palette equals it entry for entry, in `paletteIndex` order (§15.1.2). A companion test measures the *unpopulated* case, which is XNA's own `MaxBones` identity default rather than an empty palette, and proves the fixture's real palette differs from it — so the main assertion cannot be passing on a coincidence. |
| GLTF-264 | Skinned normals and tangents | ⬜ | GLTF-248 | The skinned shader transforms Position, Normal and Tangent by the palette. **Accept:** `skin-nonuniform-joint-scale` normals correct at L4/L7 (inverse-transpose where required). |
| GLTF-265 | Multiple skins in one file | ✅/🐛 | GLTF-137 | Offline: one `.cnj` per skin ✅ (`ImportsAllSkinsAsSeparateModels`). Runtime: only the first 🐛. **Accept:** both paths import every skin. |
| GLTF-266 | World/View/Projection order for skinned draws | ✔ | GLTF-104 | **Accept:** L6 capture. **Landed with `GLTF-008`:** `BoundWorldMatrixMatchesTheExpectedNodeWorld` asserts the bound world equals the manifest's **L4** node world matrix times the application's own, over every single-instance corpus fixture — compared against L4 rather than against a second walk of the bone hierarchy, so it is a conformance test and not a consistency one. `ViewAndProjectionReachEveryDrawUnaltered` pins the other two against a camera that is nothing like the identity in any of its matrices. |
| GLTF-267 | `uNormalMatrix` is `transpose(inverse(world3x3))` | ✔ | GLTF-264 | **Accept:** verified under non-uniform scale at L6. **Landed with `GLTF-008`:** the L6 capture records the normal matrix using the identical cofactor/inverse-determinant expression `EasyGLRenderer::BindDrawParams()` uploads, so the assertion is over the number a renderer actually sends; `NormalMatrixIsTheInverseTransposeOfTheWorldUpper3x3` checks it across the whole corpus against XNA's own `Matrix::Invert` + `Transpose`. The corpus had **no** non-uniform scale at all, so §11.4's planned `xf-scale-nonuniform` was generated to supply the case that can fail: `scale = [2,3,4]` with normals along `(0.6,0.8,0)`, where the world 3x3 and its inverse transpose send the same normal in visibly different directions. A separate test asserts that separation numerically, so the fixture cannot decay into one that merely agrees. |
| GLTF-268 | Skinned + PBR combined path | ⬜ | GLTF-264 | Stride 68, `SkinnedPbrEffect`. **Accept:** a skinned PBR fixture correct at L5/L6/L7. |
| GLTF-269 | Skinning + morph interaction | ⬜ | GLTF-278 | Morph writes into the same buffer the skinning shader reads. **Accept:** `morph-plus-skin` correct at L4. |
| GLTF-270 | Skinning + node transforms | ⬜ | GLTF-260 | **Accept:** a skinned mesh under a transformed parent is correct at L4. |
| GLTF-271 | Skinning + Draco | ⬜ | GLTF-353 | **Accept:** `draco-skinned` matches its uncompressed twin at L3. |
| GLTF-272 | Skeleton `.cnj` sidecar round-trip | ⬜ | GLTF-248 | **Accept:** both loaders produce identical `SkinningData`. |
| GLTF-273 | Skinning import report | ⬜ | GLTF-034 | Joint count, influences, renormalisation, truncation. **Accept:** populated. |
| GLTF-274 | Full skinning L4 regression | ⬜ | GLTF-248 | **Accept:** all 13 `skin-*` fixtures green on both loaders. |

---

### Phase 13 — Morph targets · owner: `GLTF-MORPH`
*Entry: Phase 12. Exit: deltas apply on every stride that has the slot; both weight sources honoured.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-275 | POSITION delta extraction and blending | ✅ | GLTF-101 | Extracted, unit-scaled, blended. **Accept:** `morph-position-single`, `morph-position-two-targets` exact at L4. |
| GLTF-276 | Targets without a POSITION delta | ✅ | GLTF-275 | Zero-filled. **Accept:** `morph-target-without-position` locked. |
| GLTF-277 | NORMAL delta extraction | ✅ | GLTF-275 | Extracted, **not** unit-scaled (correct — deltas of a unit vector). **Accept:** locked at L3. |
| GLTF-278 | **NORMAL deltas must apply on strides 48 and 68** | ✔ | GLTF-277 | `BlendMorphTargetsEXT`'s `hasNormalSlot = (stride==32 \|\| 52 \|\| 56)` excludes 48 and 68, yet both carry Normal at offset 12. Every **PBR** morph target keeps stale normals. **Accept:** `morph-normal-delta-pbr` correct at L4/L7. **Landed:** the literal list was written when 32/52/56 were the only strides a mesh with normals could have; `GLTF-215` changed which effect a metallic-roughness material selects and with it the strides an ordinary glTF mesh gets, and the list did not follow — so every PBR morph target's positions moved while its normals stayed put. Restating an ABI is what allowed the drift, so the predicate is now a **query against the canonical stride table** (`CNA::Internal::Graphics::InferredLayoutForStride`) that the renderers themselves dispatch on, and the write offset comes from the same answer instead of a hardcoded 12. Tests sweep every entry of that table: the five strides with a Normal slot must all apply a delta identically, and the two without one (20/24) must leave every byte past the position untouched — a blend that "helpfully" wrote a normal at offset 12 there would land on a TextureCoordinate or a Color. Both tests were confirmed to fail against the old rule before the fix was kept. The dedicated `morph-normal-delta-pbr` fixture is still owed by `GLTF-277`'s group; the stride sweep is what makes the defect impossible to reintroduce in the meantime. |
| GLTF-279 | TANGENT deltas | ✔ | GLTF-278 | Never extracted, though strides 48/68 have a tangent slot at offset 24. **Accept:** `morph-tangent-delta` correct, `.w` preserved. **Landed.** The old scope cut predated `PbrEffect`'s real tangent-space normal mapping, so a morphed PBR surface kept its **rest-pose tangent basis** while its positions and normals moved — normal mapping then lit the deformed surface with the undeformed basis. Deltas are stored as `Vector3`, not `Vector4`, and that is the whole design: glTF morphs the tangent **direction** only, because `w` is the UV winding's handedness and interpolating it would pass through 0, which is not a handedness at all. Storing three components makes preserving `w` structural rather than a rule someone has to remember, and a test asserts it survives in **both** signs and at a half weight. The tangent offset comes from the same canonical stride table `GLTF-278` moved the normal offset to, so a stride with no tangent slot is left entirely untouched — asserted, because writing a tangent at offset 24 of a stride-32 vertex would land on its TextureCoordinate. |
| GLTF-280 | `mesh.weights` as the default pose | ✅ | GLTF-275 | Applied at import when non-zero. **Accept:** `morph-mesh-weights-nonzero` locked. |
| GLTF-281 | `node.weights` overrides `mesh.weights` | ✔ | GLTF-280 | Never read. **Accept:** `morph-node-weights-override` correct. **Landed.** §3.7.2.2 gives the instancing node the final say, and **overrides** is the operative word: a node declaring `[1,0]` for a mesh whose own weights are `[0,1]` must start at `[1,0]`, not `[1,1]`, and a node array shorter than the target count leaves the remainder at **zero** rather than at the mesh's value. `node.weights` was read by nobody, so a mesh instanced by several nodes wore every node's expression at once. The fixture (`morph-node-weights-override`) is still owed by `GLTF-280`'s group, which has no morph assets in the corpus yet; the override rule itself is exercised by the runtime loader passing each instance's own node. |
| GLTF-282 | A mesh instanced by two nodes with different weights | ✔ | GLTF-281, GLTF-136 | `ExtractMorphWeightTrack` takes the **first** node found; `MorphTargetDataEXT` is per-part, so two instances cannot differ. **Accept:** supported after Phase 6, or reported. **Landed as supported, and the precondition turned out to already hold.** `MorphTargetDataEXT` is per **part**, so two instances can only differ if the importer builds a part per instance — and it does: the runtime loader extracts each mesh instance separately, which `GLTF-137` made true for every group rather than just the first. With `GLTF-281` passing each instance's own node, the two differ by construction. `morph-node-weights-override` proves it: one mesh with `weights: [0.5]`, instanced by nodes declaring `[1]` and `[0]`, must produce 1 and 0. The test names all three ways it could go wrong — both instances the same (node weights ignored), an instance keeping the mesh's 0.5 (not overridden), or a weight above 1 (merged rather than replaced). |
| GLTF-283 | Blended-normal renormalisation | ✅ | GLTF-278 | Performed. **Accept:** locked. |
| GLTF-284 | Weight-vector length validation | ✅ | GLTF-275 | `BlendMorphTargetsEXT` throws on a mismatch. **Accept:** locked with the exact message. |
| GLTF-285 | Record the CPU-morphing design decision | ⬜ | GLTF-278 | Full VB re-blend + re-upload; works on every renderer. **Accept:** documented with its cost; GPU morphing explicitly GLTF ROBUST. |
| GLTF-286 | Morph + skin | ⬜ | GLTF-269 | **Accept:** `morph-plus-skin` correct at L4. |
| GLTF-287 | Morph + node transforms | ⬜ | GLTF-114 | Deltas are in mesh-local space and must be applied **before** the node transform. **Accept:** asserted. |
| GLTF-288 | Morph + Draco | ⬜ | GLTF-353 | **Accept:** `draco-morph` matches its uncompressed twin. |
| GLTF-289 | Morph sidecar `.cnj` round-trip | ✅/⬜ | GLTF-275 | `BuildMorphBytes` + the `.cnj` reader exist and are tested. **Accept:** extended to tangent deltas. |
| GLTF-290 | Large target counts | ⬜ | GLTF-275 | `kMaxSaneTargetCount = 100000` in the `.cnj` reader; the glTF path has no equivalent bound. **Accept:** consistent limits on both paths. |
| GLTF-291 | Morph import report | ⬜ | GLTF-034 | Target count, missing delta kinds, applied default weights. **Accept:** populated. |
| GLTF-292 | Full morph L4 regression | ⬜ | GLTF-278 | **Accept:** all 12 `morph-*` fixtures green on both loaders. |

---

### Phase 14 — Animation · owner: `GLTF-ANIMATION`
*Entry: Phase 13. Exit: every path × interpolation combination is exact at keyframes and midpoints.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-293 | **Import animation of non-joint (rigid) nodes** | ✔ | GLTF-114 | `ExtractClips` skips any channel whose target is not in `skel.nodeToNewIndex`, and the offline tool calls it only when `hasSkin`. `f7` produced a `.cnj` with **no** `animations` key and no warning. **Accept:** `anim-rigid-node` produces a playable clip targeting the mesh node's bone. **Landed:** `ExtractSceneNodeClips` resolves channels against `SceneGraphOut` instead of a joint set, so `anim-rigid-node` yields one clip `Spin` with one track on the `SpinningMesh` node's own bone — identity at `t=0`, a quarter turn about +Z at `t=1`, scale filled from the node's bind pose rather than zero. Both original mechanisms are gone: extraction is no longer gated on a skin, and a non-joint target resolves instead of being skipped. `ClipOut` gained `targetSpace` (`ClipTargetSpace::JointPalette` \| `SceneNode`) so §15.1.2's two index spaces can never be silently interchanged; it defaults to `JointPalette`, leaving every existing consumer's meaning unchanged. Channel gathering and keyframe resampling are now one shared code path for both extractors — the two differing only in how a target node resolves — since D6 existed precisely because they were never separated. **Scope boundary:** the clip is **not** serialised to the `.cnj` yet. A scene-node track's `boneIndex` is a `sceneNodeIndex`, and the clip schema has no field distinguishing it from a palette slot; writing one would let a reader apply it as a palette index — a fresh silent corruption in place of the old one. `GLTF-294` adds the field and the unified playback path, so the converter **reports** the clip by name and track count instead of dropping it. **D6 → `partially-remediated`.** Its ledger entry also named the wrong owner (`GLTF-284`, the morph weight-vector validation task); corrected to `GLTF-293`/`GLTF-294`. |
| GLTF-294 | Unify joint and node animation on the bone hierarchy | ✔ | GLTF-293, GLTF-103 | With a real `ModelBone` tree, both are bone tracks. **Accept:** one code path; `.cnj` carries clips for both. **Half done by `GLTF-293`:** channel gathering and resampling are already one shared path, and `ClipOut::targetSpace` names the space each clip is in. What is left is the serialisation and playback half — a `targetSpace` field in the `.cnj` clip schema, a reader that honours it, and a way to drive `ModelBone` transforms from a `SceneNode` clip. **This is what keeps D6 open.** **Landed:** one writer serialises both clip kinds — they differ only by a `"targetSpace"` field in the file, not by a second serialiser, since keeping two would let them drift. `AnimationClipEXT` gained `TargetSpace`, a rigid clip reaches an unskinned model's `Tag` as `ModelAnimationsEXT`, and `ApplyClipToBonesEXT` poses `Model::Bones` from it — **refusing** a joint-palette clip outright, because applying palette indices to bones would pose the wrong ones with no symptom but wrong motion. **D6 is `fixed`.** **Boundary recorded, not resolved:** `Model::Tag` holds one object and a skinned model already uses it for `SkinningData`, so a file with **both** a skin and rigid node animation has nowhere to put the rigid clips; the importer reports that by name and `GLTF-295` owns it. |
| GLTF-295 | Call `ExtractClips` unconditionally | ✔ | GLTF-293 | `gltf_to_cnj` gates it on `hasSkin`. **Accept:** a skinless animated file produces clips. **Landed with `GLTF-293`/`GLTF-294`:** `ExtractSceneNodeClips` runs unconditionally on both paths and `anim-rigid-node` — a file with no skin at all — produces and round-trips its clip, which is exactly the stated acceptance. The tool's own comment had gone stale in the meantime, still saying the clip was extracted but not serialised; `GLTF-294` added the `targetSpace` field that made serialising it safe, and the comment now says so. The **separate** collision this task's own body describes — a file with *both* a skin and rigid node animation, where `Model::Tag` has room for only one object — is unchanged and still reported by name rather than dropped; it is tracked as a named limitation in `docs/gltf-api-change-review.md` §1.5, not as part of this acceptance. |
| GLTF-296 | Animation of camera and light nodes | ✔ | GLTF-294 | Same non-joint drop. **Accept:** imported or reported. **Landed as imported, and it needed no camera-specific path at all** — §3.11 animates *nodes*, and a camera node is an ordinary node, so `GLTF-293`'s `ExtractSceneNodeClips` already resolves it against the scene graph. `camera-animated-node` proves that rather than assuming it, and the clip's track is asserted to drive the **camera's own** bone rather than merely some bone. **The consequence worth pinning:** `ModelCameraEXT::WorldTransform` is a snapshot taken at import, so posing the model moves the camera's *bone* and not that matrix. A consumer reading the stored one every frame would render an animated camera as a stationary one. The field now says so, and a test asserts both halves — the live transform follows the bone, and the stored one deliberately does not — with a guard that the two actually differ at `t = 1`, so the test cannot pass by their coinciding. Lights are the same shape and the same code path; they have no fixture yet because the corpus has no `lights-punctual-*` asset (`GLTF-326`'s group). |
| GLTF-297 | Quantify the union-time resampling error | 🔬 | GLTF-294 | Bone channels are resampled onto the union of their own three channels' times, baking a piecewise-linear approximation of CUBICSPLINE between keys. **Accept:** the error is measured on a fixture and either accepted with numbers or replaced by lazy evaluation (as the morph path already does). |
| GLTF-298 | Rotation interpolation | ✅ | GLTF-294 | `Slerp` for LINEAR; component-wise Hermite + renormalisation for CUBICSPLINE. **Accept:** `anim-rotation-slerp` exact at the midpoint, and a negated-quaternion twin yields the same pose (shortest path). |
| GLTF-299 | Clip duration when the first key is > 0 | ✔ | GLTF-294 | `maxTime` only tracks the last input sample. **Accept:** `anim-nonzero-start` has the correct duration and start behaviour. **Landed — and the answer is that both behaviours were already right, which nothing proved.** glTF animations live on an absolute timeline anchored at 0, so each question has exactly one spec-conformant answer: the duration is the **last** input sample, not the span between the first and last keys, and the pose below the first key is that key's, held (Appendix C's clamping) — not the node's rest pose. `anim-nonzero-start` is the first fixture that can tell those apart: its first key is at `t = 1.5` and its last at `3.0`, so "last sample" and "key span" are different numbers, and its node's rest rotation differs from its first key, so "holds the first key" and "falls back to the rest pose" are different poses. Both rules are stated in the fixture's own manifest, with the reason, so the decision lives with the data rather than in a test's expectations. A duration that collapsed to the key span would shift every authored time by the offset — a whole-clip error no single pose comparison would localise, which is why it gets its own assertion. |
| GLTF-300 | LINEAR translation/scale | ✅ | GLTF-294 | **Accept:** `anim-linear-translation` yields `[5,0,0]` at `t=1`. |
| GLTF-301 | STEP interpolation | ✅ | GLTF-294 | Existing test covers a foreign resample time. **Accept:** `anim-step-translation` locked at `t=1.999` and `t=2`. |
| GLTF-302 | CUBICSPLINE tangent packing and `Δt` scaling | ✅ | GLTF-294 | `HermiteEvaluate` uses the spec's interval scaling; `EvaluatesCubicSplineWithRealHermiteBasis` exists. **Accept:** `anim-cubic-translation` matches an independently computed manifest to 1e-6. |
| GLTF-303 | Multi-channel single node with disjoint key times | ⬜ | GLTF-297 | **Accept:** `anim-multi-channel-one-node` correct at every union sample and between them. |
| GLTF-304 | Bind pose as the fallback for an unanimated component | ✅ | GLTF-294 | `EvaluateVec3Channel` falls back to the decomposed bind pose. **Accept:** locked. |
| GLTF-305 | Multiple animations per file | ✅ | GLTF-294 | One `ClipOut` each; one `.cnj` per clip. **Accept:** locked. |
| GLTF-306 | Clip naming and lookup | ⬜ | GLTF-305 | Unnamed animations become `ClipN`. **Accept:** deterministic and documented. |
| GLTF-307 | Playback clamping and looping | ✅ | GLTF-305 | `AnimationPlayer` clamps or floor-mods on ticks. **Accept:** locked at `t>end` and across a loop boundary. |
| GLTF-308 | Loop-boundary continuity | ⬜ | GLTF-307 | **Accept:** pose at `t=duration` equals pose at `t=0` for a fixture authored to loop. |
| GLTF-309 | Time domain and units | ✅ | GLTF-294 | Seconds throughout, `System::TimeSpan` at the boundary. **Accept:** locked. |
| GLTF-310 | Animation targeting a node not in the default scene | ⬜ | GLTF-133 | **Accept:** ignored and reported. |
| GLTF-311 | Empty / single-keyframe channels | ✅ | GLTF-294 | Handled by `FindBracket` and the size-1 paths. **Accept:** locked. |
| GLTF-312 | `unitScale` applies to translation channels and tangents | ✅ | GLTF-121 | Already applied to both. **Accept:** locked. |
| GLTF-313 | Sampler input must be strictly increasing | ⬜ | GLTF-294 | `FindBracket` assumes sorted input. **Accept:** unsorted input is rejected or sorted, deterministically. |
| GLTF-314 | Animation `.cnj` round-trip | ⬜ | GLTF-294 | **Accept:** both loaders produce identical `AnimationClipEXT`. |
| GLTF-315 | Animation import report | ⬜ | GLTF-034 | Skipped channels, unsupported paths, resampling. **Accept:** populated; the existing stdout warning becomes a report entry. |
| GLTF-316 | Full animation L4 regression | ⬜ | GLTF-302 | **Accept:** all 12 `anim-*` fixtures green on both loaders. |

---

### Phase 15 — Cameras, lights, scenes · owner: `GLTF-TRANSFORM` / `GLTF-EXTENSION`
*Entry: Phase 14. Exit: imported cameras and lights are available and correct, or explicitly absent.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-317 | **Import glTF cameras** | ✔ | GLTF-025, GLTF-114 | `cgltf_camera` has **0 occurrences**. **Accept:** camera nodes reach the model. **Landed:** `ExtractCamerasEXT` walks the **scene graph**, not `data->cameras` — a camera only counts if a node in the default scene instances it, and one camera instanced by several nodes is several placements, both of which walking the camera array would get wrong. Carried to a new CNAEXT `Model::CamerasEXT` holding `ModelCameraEXT`. **A property, not `Tag`** — the third time that collision has come up: `Tag` holds one object and `SkinningData` and `ModelAnimationsEXT` already contend for it, so a skinned model with cameras would have had to choose. Projection and placement stay **apart**, as §3.10 keeps them, because an application animating the camera node must recompute the view without touching the projection. |
| GLTF-318 | Perspective projection mapping | ✔ | GLTF-317 | `CreatePerspectiveFieldOfView(yfov, aspectRatio ?: viewportAspect, znear, zfar)`. **Accept:** `camera-perspective` matrix matches the manifest to 1e-6. **Landed**, matching to 1e-6 against a matrix the fixture computes from the specification's own formula rather than from CNA. An absent `aspectRatio` means "the viewport's" (§3.10.3), which the importer cannot know: it uses 1 and carries `yfov`/`znear` recoverably so an application with a real viewport can rebuild the projection without re-reading the file. |
| GLTF-319 | Infinite perspective (`zfar` absent) | ✔ | GLTF-318 | XNA has no such overload. **Accept:** a CNAEXT builder; `camera-perspective-infinite` matches. **Landed** as `CreateInfinitePerspectiveFieldOfViewEXT`, built from the finite matrix's **limits** as `zfar → ∞` (`M33 → −1`, `M43 → −znear`) rather than from a large finite `zfar` — which is not equivalent, because a large `zfar` perturbs *every* depth value, not only the ones near the horizon, turning an exact depth comparison into an approximate one. It throws `std::invalid_argument` on the same preconditions as `Matrix`'s finite overload and with the same exception type, since a caller switching between the two must not have to catch two different things. The two fixtures share every parameter but `zfar`, so a test isolates exactly the terms that may differ: `M11`/`M22` must be identical and the two `z` terms must not be. |
| GLTF-320 | Orthographic projection mapping | ✔ | GLTF-317 | `CreateOrthographic(2*xmag, 2*ymag, znear, zfar)`. **Accept:** `camera-orthographic` matches. **Landed.** `xmag`/`ymag` are **half** extents, so the volume is twice each — the classic silent factor of two, which gets its own test rather than only being implied by the matrix comparison. The fixture is deliberately non-square (3 × 1.5) so a swapped axis is caught too. |
| GLTF-321 | Camera view matrix from the node transform | ✔ | GLTF-317 | `view = inverse(worldTransform(cameraNode))`; the camera looks down its own −Z. **Accept:** manifest match. **Landed.** A glTF camera looks down its own −Z with +Y up, which is **XNA's own convention**, so no basis change is applied — and the manifest states that rule explicitly, so introducing one later fails a test rather than looking subtly wrong on screen. The node's world transform is carried on `ModelCameraEXT` and the view is its inverse, kept as a derivation rather than a stored second matrix that could drift from it. |
| GLTF-322 | `aspectRatio` absent ⇒ use the viewport | ⬜ | GLTF-318 | **Accept:** documented and tested. |
| GLTF-323 | The viewer keeps its own camera as default | ⬜ | GLTF-317 | Otherwise a broken imported camera is indistinguishable from broken geometry. **Accept:** imported cameras are an explicit alternative view only. |
| GLTF-324 | Camera carrier on the model | ⬜ | GLTF-025 | CNAEXT `GltfCameraEXT` list in owned resources. **Accept:** reviewed and tested. |
| GLTF-325 | `KHR_lights_punctual` current behaviour locked | ✅ | GLTF-101 | ≤3 lights, directional-only, point/spot aimed at the origin, `color*intensity` clamped to `[0,1]`. Two tests already exist. **Accept:** locked as the documented approximation. |
| GLTF-326 | Report dropped and approximated lights | ✔ | GLTF-035 | Extras beyond 3 are silently dropped. **Accept:** report entries. **Landed as `LightReportEXT`, covering four losses rather than the one the row names.** Beyond the >3 drop: point lights approximated as directional, spot lights approximated the same way *and* losing their cone, and — the one nobody had written down — `color * intensity` clamped to `[0,1]`. glTF intensity is **photometric and unbounded** (lux for a directional light, candela for the others) while `DiffuseColor` is a colour, so an authored 683 imports as plain white. Point and spot are counted apart because the approximation is materially worse for one than the other. The one behavioural change: the extraction loop no longer *stops* at three, because it has to keep walking to count what it drops — “3 of 6 imported” is actionable and “3 imported” is not. A control test asserts a file already inside XNA's lighting model reports nothing at all, so the diagnostic cannot degrade into noise every import prints. |
| GLTF-327 | Light `range` and cone angles | ⬜ | GLTF-325 | Ignored. **Accept:** documented as GLTF ROBUST. |
| GLTF-328 | Light node transform | ✅ | GLTF-325 | `cgltf_node_transform_world` is already used correctly here. **Accept:** locked. |
| GLTF-329 | Light ordering determinism | ⬜ | GLTF-325 | Node-array order restricted to the default scene. **Accept:** deterministic and documented. |
| GLTF-330 | Intensity clamping documented | ⬜ | GLTF-325 | glTF's photometric units have no XNA mapping. **Accept:** approximation table in `docs/`. |
| GLTF-331 | Real point/spot lights | ⬜ | GLTF-327 | No CNA stock effect supports them. **Accept:** scoped as GLTF ROBUST with a design sketch. |
| GLTF-332 | Scene fixtures regression | ⬜ | GLTF-148 | **Accept:** all 7 `scene-*`/`camera-*`/`lights-*` fixtures green. |

---

### Phase 16 — Extensions · owner: `GLTF-EXTENSION`
*Entry: **GLTF CORE 2.0 CORRECT**. Exit: every §19 row is implemented or explicitly and loudly rejected.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-333 | Enforce `extensionsRequired` (implementation) | ✔ | GLTF-023 | **Accept:** an unsupported required extension errors with its name. **Landed with `GLTF-023`:** both load paths refuse a file whose `extensionsRequired` names an extension `IsGltfExtensionSupportedEXT` does not implement, and the message names the extension. `gltf-required-extension-unsupported` is the witness. `GLTF-024`'s separate rule holds alongside it: an extension that is merely *used* is ignored with a warning, never an error — the severity difference is the point, and both are asserted by `GltfContainerValidation`. |
| GLTF-334 | Central extension-support registry | ⬜ | GLTF-333 | One table mapping extension name → classification → handler. **Accept:** §19's table is generated from it, not maintained by hand. |
| GLTF-335 | Extension conformance test template | ⬜ | GLTF-334 | **Accept:** adding an extension requires a fixture and a classification. |
| GLTF-336 | `KHR_texture_transform` completion | ✔ | GLTF-184 | **Accept:** per-map transforms; classification becomes IMPLEMENTED_AND_TESTED. **Classification: IMPLEMENTED_WITH_A_NAMED_LIMIT.** The extension *is* implemented — offset, rotation and scale are applied with the spec's own reference formula, and `has_texcoord` overrides the view's UV set — for the one transform that can be baked into a single UV channel. The residue is `GLTF-184`'s: a second, different transform on another map cannot be baked and is now named rather than dropped. Claiming IMPLEMENTED_AND_TESTED outright would assert per-map transforms work, which they do not; claiming NOT_IMPLEMENTED would be false for every single-transform material, which is nearly all of them. |
| GLTF-337 | `KHR_materials_unlit` | ⬜ | GLTF-215 | Maps cleanly to `BasicEffect` with lighting off — a genuine quick win. **Accept:** fixture correct at L7. |
| GLTF-338 | `KHR_materials_unlit` + vertex colour / alpha | ⬜ | GLTF-337 | **Accept:** tested. |
| GLTF-339 | `KHR_materials_transmission` | ✔ | GLTF-230 | Ignored; causes the `ChronographWatch` opaque-glass defect. **Accept:** either a real transmission pass, or a **documented** alpha-blend approximation that is explicitly not physical — never silent. **Decided: the alpha-blend approximation.** A real pass samples the framebuffer behind the surface and blurs it by roughness, which needs a second pass and a scene-colour target no CNA stock effect has. So `alpha = 1 - transmissionFactor`, **multiplied into** the material's own alpha rather than replacing it (a surface that is both partly transparent and partly transmissive should end up more transparent than either alone), with `alphaMode` forced to `Blend`. The four ways it is not physical are named in the source, the header and every report: no refraction, no roughness blur, tinted glass **darkens** rather than tints what is behind it, and specular — which a transmissive surface keeps at full strength — fades out with the alpha. A transmission **texture** is flagged separately, because flattening a varying surface to one factor is materially worse than the uniform case. A factor of 0 leaves the material completely alone, so a neutrally-declared extension is not turned into a `BLEND` regression. And the extension is **still not claimed** in `IsGltfExtensionSupportedEXT`: an approximation is not an implementation, so a file that *requires* transmission is still refused by `GLTF-023` rather than loading with its glass drawn as tinted alpha. |
| GLTF-340 | Transmission render ordering | ⬜ | GLTF-339 | **Accept:** the dial is visible through the glass in the retake. |
| GLTF-341 | `KHR_materials_variants` import | ⬜ | GLTF-236 | 4 variants in `ChronographWatch`. **Accept:** variants imported; default mapping unchanged. |
| GLTF-342 | Variant selection API | ⬜ | GLTF-341, GLTF-025 | **Accept:** CNAEXT selection; the viewer can switch. |
| GLTF-343 | `KHR_materials_ior` | ⬜ | GLTF-235 | Affects `F0`. **Accept:** shader change + analytic check. |
| GLTF-344 | `KHR_materials_specular` | ⬜ | GLTF-343 | **Accept:** fixture correct. |
| GLTF-345 | `KHR_materials_clearcoat` | ⬜ | GLTF-343 | Second specular lobe; large. **Accept:** implemented or explicitly deferred with a report entry. |
| GLTF-346 | `KHR_materials_sheen` | ⬜ | GLTF-343 | **Accept:** as above. |
| GLTF-347 | `KHR_materials_volume` | ⬜ | GLTF-339 | Depends on transmission. **Accept:** as above. |
| GLTF-348 | Classify iridescence / anisotropy / dispersion as NOT_DESIRED | ⬜ | GLTF-334 | **Accept:** recorded with rationale; required-use is rejected loudly. |
| GLTF-349 | `KHR_materials_pbrSpecularGlossiness` | ⬜ | GLTF-334 | Archived by Khronos but present in older assets. **Accept:** converted to metallic-roughness, or rejected clearly. |
| GLTF-350 | `KHR_texture_basisu` / `EXT_texture_webp` | ✔ | GLTF-200 | Images silently vanish today. **Accept:** supported or explicitly rejected. **Decided: explicitly reported, and — where the file demands them — rejected.** Both extensions are specified so a texture MAY keep a plain PNG/JPEG `source` as a fallback for readers without the codec, so there are three outcomes and `GLTF-200` implements all three: the fallback is used when present and nothing is reported, the loss is named per map when it is not, and a file listing either in `extensionsRequired` is refused outright by `GLTF-023`. Neither is listed in `IsGltfExtensionSupportedEXT`, and a test guards that — claiming support would turn that refusal into a silent load with the textures missing, which is worse than today, not better. Adding a KTX2 or WebP decoder is a codec dependency, not a glTF conformance question. |
| GLTF-351 | `EXT_meshopt_compression` | ⬜ | GLTF-334 | cgltf parses it but decoding needs a caller-supplied hook CNA does not provide ⇒ buffer data is absent. **Accept:** supported or explicitly rejected; **never** silently empty geometry. |
| GLTF-352 | `EXT_mesh_gpu_instancing` | ⬜ | GLTF-146 | CNA already has `DrawInstancedPrimitives`. **Accept:** design sketch; GLTF ROBUST. |

---

### Phase 17 — Draco and compression · owner: `GLTF-EXTENSION`
*Entry: Phase 16. Exit: Draco output is semantically identical to its uncompressed twin at L3.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-353 | **Draco parity fixture pair** | ⬜ | GLTF-101 | The same mesh, uncompressed and Draco-compressed. **Accept:** POSITION, NORMAL, TANGENT, TEXCOORD, COLOR, JOINTS, WEIGHTS and the index list all match within a stated quantisation tolerance, **at L3**. |
| GLTF-354 | Lock the shared-code-path property | ✅ | GLTF-353 | `unpackSemantic` makes every call site source-agnostic — Draco has **no** separate transform or material path. **Accept:** a test asserts the two paths produce identical `MeshOut` apart from quantisation. |
| GLTF-355 | Draco point-count validation | ✅ | GLTF-353 | Already validated against the declared `POSITION.count`. **Accept:** locked. |
| GLTF-356 | Draco connectivity from the decoded face list | ✅ | GLTF-353 | Not from `prim.indices`, which has no backing data. **Accept:** locked. |
| GLTF-357 | Dequantisation semantics | ✅ | GLTF-353 | `SetSkipAttributeTransform` is never called, so values read back already dequantised — the same level `cgltf_accessor_unpack_floats` gives. **Accept:** locked and documented. |
| GLTF-358 | `!CNA_DRACO_AVAILABLE` error path | ✅ | GLTF-353 | Already tested. **Accept:** locked. |
| GLTF-359 | Pin `FindDracoUniqueId`'s pointer arithmetic | ⬜ | GLTF-353 | Correct but fragile against a cgltf upgrade. **Accept:** a direct unit test over a known attribute mapping. |
| GLTF-360 | Draco + skinning | ⬜ | GLTF-271 | **Accept:** `draco-skinned` matches its twin. |
| GLTF-361 | Draco + morph | ⬜ | GLTF-288 | **Accept:** `draco-morph` matches its twin. |
| GLTF-362 | Draco + non-triangle mode | 🔬 | GLTF-080 | Draco decodes to triangles. **Accept:** rejected or documented. |
| GLTF-363 | Draco in CI | ⬜ | GLTF-353 | `libdraco` was **absent** in this audit's environment, so the Draco path was not exercised. **Accept:** a CI job with `libdraco` present; the whole corpus runs both with and without it. |
| GLTF-364 | Draco malformed-input robustness | ⬜ | GLTF-040 | **Accept:** a corrupt Draco buffer errors cleanly; ASan/UBSan clean. |

---

### Phase 18 — Effect and shader boundary · owner: `CNA-EFFECT`
*Entry: Phase 11 + Phase 12. Exit: every §21.1 row is asserted numerically at L6.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-365 | L6 capture for every stock effect | ⬜ | GLTF-008 | **Accept:** `BasicEffect`, `DualTextureEffect`, `SkinnedEffect`, `PbrEffect`, `SkinnedPbrEffect` all capturable. |
| GLTF-366 | Matrices agree end-to-end | ✅ | GLTF-266 | **Accept:** L6. |
| GLTF-367 | Normal matrix under non-uniform scale | ⬜ | GLTF-267 | **Accept:** L6 value equals `transpose(inverse(world3x3))`. |
| GLTF-368 | `DiffuseColor` / `Alpha` carry `baseColorFactor` | ⬜ | GLTF-216 | **Accept:** L6. |
| GLTF-369 | `PbrEffect` does not premultiply alpha into RGB | ✅ | GLTF-368 | Deliberate and correct for glTF. **Accept:** locked with a comment-backed test. |
| GLTF-370 | Metallic / roughness / emissive parameters | ⬜ | GLTF-220 | **Accept:** L6. |
| GLTF-371 | Normal scale and occlusion strength parameters | ⬜ | GLTF-224, GLTF-225 | **Accept:** L6. |
| GLTF-372 | Alpha-test parameters actually configured | ⬜ | GLTF-229 | `uAlphaTest` exists but nothing sets it. **Accept:** L6 shows the cutoff. |
| GLTF-373 | Texture slot → uniform mapping | ⬜ | GLTF-365 | **Accept:** each of the five maps binds to its intended unit on every renderer. |
| GLTF-374 | Unbound-map fallbacks | 🔬 | GLTF-373 | EasyGL has a flat-normal fallback (`CNB-58`) and white for the rest — correct. **Accept:** every PBR-capable renderer has the same fallbacks; a missing flat-normal fallback tilts every normal and must be caught. |
| GLTF-375 | Bone palette and influence count | ✅ | GLTF-263 | **Accept:** L6. |
| GLTF-376 | Light parameters | ✅ | GLTF-325 | **Accept:** L6 shows disabled lights as zero colour, matching the shader's expectation. |
| GLTF-377 | Fog parameters do not leak into glTF draws | ⬜ | GLTF-365 | `vFogFactor` is in the PBR shader. **Accept:** fog off by default; asserted. |
| GLTF-378 | Shader source review against the spec BRDF | ✅ | GLTF-235 | Matches Appendix B. **Accept:** documented, with the deliberate `roughness` clamp to `[0.045,1]` noted. |
| GLTF-379 | Cross-renderer shader semantic audit | ⬜ | GLTF-373 | **Accept:** a per-renderer matrix of the §21.1 rows. |
| GLTF-380 | Effect parameter naming vs convention | ⬜ | GLTF-379 | "A field with the same name is not the same convention until a test says so." **Accept:** every shared name has a test. |
| GLTF-381 | `ShaderEffect` interaction | ⬜ | GLTF-365 | A custom `ShaderEffect` must still receive correct glTF vertex data. **Accept:** a fixture with a custom shader over a glTF mesh. |
| GLTF-382 | Effect-boundary regression over the corpus | ⬜ | GLTF-365 | **Accept:** L6 green for every fixture on `HEADLESS`. |

---

### Phase 19 — Renderer differential validation · owner: `CNA-RENDERER`
*Entry: Phase 18. Exit: L5/L6 identical across renderers; L7 within tolerance.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-383 | Run the whole corpus on `STUB` and `HEADLESS` | ⬜ | GLTF-382 | **Accept:** L1–L6 green, no GPU required. |
| GLTF-384 | Run the corpus on `OPENGLES3` (EasyGL) | ⬜ | GLTF-383 | The viewer's renderer. **Accept:** L1–L7 green. |
| GLTF-385 | Run the corpus on `VULKAN` | ⬜ | GLTF-383 | Independent shader source. **Accept:** L1–L7 green. |
| GLTF-386 | Run the corpus on `DIRECTX11` where CI allows | ⬜ | GLTF-383 | Different vertex-layout code. **Accept:** L1–L7 green or the gap is documented. |
| GLTF-387 | Run the corpus on `SOFTWARE` | ⬜ | GLTF-383 | CPU rasteriser cross-check for winding and depth. **Accept:** L5–L7 green. |
| GLTF-388 | Assert L5 equality across renderers | ⬜ | GLTF-168 | L5 is renderer-independent by construction. **Accept:** byte-identical; any divergence is a bug. |
| GLTF-389 | Assert L6 equality across renderers | ⬜ | GLTF-382 | **Accept:** identical parameter values. |
| GLTF-390 | Per-renderer L7 tolerance policy | ⬜ | GLTF-009 | **Accept:** a documented tolerance per renderer with its justification. |
| GLTF-391 | Classify every divergence found | ⬜ | GLTF-390 | Shared-importer vs renderer-owned, per §6. **Accept:** each has exactly one owning task. |
| GLTF-392 | **Forbid per-renderer fixes for shared defects** | ⬜ | GLTF-391 | **Accept:** a review rule; a shared-importer fix appearing in a renderer directory fails review. |
| GLTF-393 | Renderer capability matrix for glTF features | ⬜ | GLTF-391 | 32-bit indices, point/line topology, MRT, sRGB. **Accept:** `docs/graphics-renderer-feature-matrix.md` extended. |
| GLTF-394 | Unsupported-feature behaviour per renderer | ⬜ | GLTF-393 | **Accept:** a clear "not supported" message, never a silent wrong draw. |
| GLTF-395 | Winding and cull agreement across renderers | ⬜ | GLTF-232 | **Accept:** the single-sided fixture culls the same face everywhere. |
| GLTF-396 | Depth-range and clip-space differences | ⬜ | GLTF-395 | Owned by the renderer, not the importer. **Accept:** documented; the corpus is unaffected. |
| GLTF-397 | Render-target V-flip does not affect glTF UVs | ⬜ | GLTF-192 | **Accept:** asserted per renderer. |
| GLTF-398 | Renderer-differential CI job | ⬜ | GLTF-390 | **Accept:** at least `STUB`, `HEADLESS` and `OPENGLES3` run per commit. |

---

### Phase 20 — Conformance corpus and CI gate · owner: tooling
*Entry: Phase 19. Exit: the corpus is complete, generated, licensed and gating.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-399 | Complete the 135-asset synthetic corpus | ⬜ | GLTF-003 | §24.2's owning-group inventory (8+14+8+7+17+6+10+12+13+13+10+7+4+6 = **135** distinct assets, each with a `.glb` twin). **Accept:** every owning group's assets exist, are generated and are validated; **CI asserts the generator's manifest reports exactly 135 distinct assets**, so the number in §24.2 and the corpus cannot drift apart. |
| GLTF-400 | `.glb` twin for every synthetic asset | ⬜ | GLTF-399 | **Accept:** twins agree at L3/L4. |
| GLTF-401 | Manifest completeness audit | ⬜ | GLTF-399 | **Accept:** every asset declares exactly one `owningGroup`, its `referencingGroups[]`, the layers it validates and the expected values for each; the sum of owning-group counts equals the reported distinct-asset total, checked mechanically rather than by reading. |
| GLTF-402 | Corpus runner reports the first divergent layer | ⬜ | GLTF-010 | **Accept:** a failure names the layer, the fixture, the field and the delta. |
| GLTF-403 | Corpus coverage matrix vs the §27.1 checklist | ⬜ | GLTF-401 | **Accept:** every CORE requirement maps to ≥1 fixture. |
| GLTF-404 | Fixture-count and runtime budget | ⬜ | GLTF-402 | **Accept:** the full corpus runs within a stated CI time budget. |
| GLTF-405 | Real-world asset subset decision executed | ⬜ | GLTF-019 | **Accept:** either a committed, licence-reviewed subset or a fetch script; `THIRD_PARTY_NOTICES.md` updated. |
| GLTF-406 | Licence review for every committed asset | ⬜ | GLTF-018 | **Accept:** no asset committed without a recorded licence. |
| GLTF-407 | `ChronographWatch` acceptance criteria | ⬜ | GLTF-405 | §4.4's world-bounds table plus material/animation/transmission criteria. **Accept:** either the asset is licensed and committed, or the criteria are checked against a licence-clean equivalent. |
| GLTF-408 | Malformed-input group behaviour | ⬜ | GLTF-040 | **Accept:** all 6 `bad-*` fixtures produce deterministic, actionable errors. |
| GLTF-409 | Sanitiser run over the whole corpus | ⬜ | GLTF-036 | **Accept:** zero unresolved ASan/UBSan findings, or each triaged. |
| GLTF-410 | Golden regeneration is reviewable | ⬜ | GLTF-167 | **Accept:** a golden change shows a readable diff and requires justification. |
| GLTF-411 | Cross-check a subset against the reference renderer | ⬜ | GLTF-016 | **Accept:** documented capture and comparison for ≥10 assets. |
| GLTF-412 | Numerical oracles precede screenshots in every gate | ⬜ | GLTF-402 | **Accept:** the CI job fails at the earliest divergent layer, not at L7. |
| GLTF-413 | Test-name → requirement traceability | ⬜ | GLTF-403 | **Accept:** each §27.1 row lists its tests. |
| GLTF-414 | Retire or migrate inline JSON test fixtures | ⬜ | GLTF-399 | ~30 fixtures live as string literals inside `GltfToCnjToolTests.cpp`. **Accept:** migrated to the generator or explicitly kept, with rationale. |
| GLTF-415 | Existing 39 glTF tests still pass | ⬜ | GLTF-399 | **Accept:** no regression in `GltfImportCoreTests`, `GltfToCnjToolTests`, `RuntimeGltfModelTests`. |
| GLTF-416 | Corpus documentation | ⬜ | GLTF-403 | **Accept:** `docs/gltf-conformance.md` lists every fixture and what it proves. |
| GLTF-417 | Corpus contribution guide | ⬜ | GLTF-416 | **Accept:** adding a fixture is a documented, one-command process. |
| GLTF-418 | Corpus determinism | ⬜ | GLTF-399 | **Accept:** two generator runs produce byte-identical output. |
| GLTF-419 | Corpus size budget | ⬜ | GLTF-399 | **Accept:** total synthetic corpus under a stated size; every fixture < 8 KB. |
| GLTF-420 | Make `gltf-conformance` a required CI gate | ⬜ | GLTF-398 | **Accept:** the label runs per commit and blocks on failure. |

---

### Phase 21 — `cna-gltf-viewer` retake · owner: `VIEWER`
*Entry: **GLTF CORE 2.0 CORRECT**. Exit: the retake matrix renders correctly with no compensating logic.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-421 | **Migrate the viewer build to current CNA vocabulary** | 🐛 | GLTF-001 | The viewer uses `CNA_GRAPHICS_BACKEND`, `EASYGL`, `cna_backend_graphics_easygl` and `CNA_NOXNA` — none exist on `develop` since 2026-08-10. **It cannot configure against the baseline.** **Accept:** it configures and builds against `develop` with `CNA_GRAPHICS_RENDERER=OPENGLES3`. **Blocked, and the blocker is now precise rather than "not in this tree":** the viewer is `openeggbert/cna-gltf-viewer`, a **separate repository**. Two independent obstacles, either of which alone is decisive — the work is a push to a repository outside this branch's authorized scope, and `OPENGLES3` needs the sibling `../easy-gl` and `../meta-gl` checkouts, which this environment does not have, so the acceptance criterion ("configures **and builds**") could not be verified even if the edit were made. |
| GLTF-422 | Viewer builds against the remediated CNA | ⬜ | GLTF-421 | **Accept:** green build, existing CTest cases pass. |
| GLTF-423 | Replace the unconditional `CullNone` | ⬜ | GLTF-231 | V2. **Accept:** the material's `doubleSided` drives cull state; `CullNone` survives only behind an explicit `--no-cull` debug flag. |
| GLTF-424 | Fallback lighting for a light-less scene | ⬜ | GLTF-242 | V3. **Accept:** `EnableDefaultLighting()` when the import contributed zero lights, announced on stdout; PBR surfaces are no longer black. |
| GLTF-425 | Drive `AnimationPlayer` and `SetBoneTransforms` | ⬜ | GLTF-262 | The viewer never sets bones, so a skinned model renders in the identity-bone pose. **Accept:** bind pose by default, plus clip selection and playback. |
| GLTF-426 | `--dump-oracle <dir>` | ⬜ | GLTF-005 | Writes the L2–L5 JSON for the loaded asset. **Accept:** the dumps diff cleanly against a corpus manifest. |
| GLTF-427 | Camera framing from world-space model bounds | ⬜ | GLTF-128 | Removes the viewer's private duplicate of the bounds computation and its dependence on the sidecar layout. **Accept:** framing matches the model's own bounds. |
| GLTF-428 | **Compensating-logic audit** | ⬜ | GLTF-427 | Grep and review for scaling/axis/matrix/weight/material hacks and per-asset special cases. **Accept:** none found; this gates the release. |
| GLTF-429 | Retake matrix execution | ⬜ | GLTF-420 | §30's 13-row matrix. **Accept:** every row correct against reference output. |
| GLTF-430 | Runtime-path parity in the viewer | ⬜ | GLTF-137 | The viewer uses the offline `.cnj` path; add a `--direct` mode using `Load<Model>(".glb")`. **Accept:** both paths render identically. |
| GLTF-431 | Viewer diagnostics overlay | ⬜ | GLTF-034 | Surface `GltfImportReportEXT`. **Accept:** losses and approximations are visible in the viewer. |
| GLTF-432 | Viewer documentation refresh | ⬜ | GLTF-429 | `README.md`/`plan.md` still describe the pre-remediation behaviour. **Accept:** accurate. |

---

### Phase 22 — Performance and lifetime · owner: `CNA-GPU-PACKING`
*Entry: **GLTF CORE 2.0 CORRECT**. Exit: §26's hazards are measured and fixed or documented with numbers.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-433 | Parse-and-load cost per `Load<Model>` | ⬜ | GLTF-420 | A GLB is fully re-parsed on every call. **Accept:** measured; a cache added or the cost documented. |
| GLTF-434 | Offline per-group re-parse | ⬜ | GLTF-433 | **Accept:** measured; acceptable for a CLI, documented. |
| GLTF-435 | Accessor unpack allocation cost | ⬜ | GLTF-433 | Every accessor is expanded to `float` before repacking. **Accept:** measured; reduced or documented. |
| GLTF-436 | Repeated load/unload stress | ⬜ | GLTF-433 | **Accept:** no leak under ASan/LSan over 1000 cycles. |
| GLTF-437 | Cross-load texture caching | ⬜ | GLTF-436 | Correct within a load; nothing across loads. **Accept:** measured; decision recorded. |
| GLTF-438 | `ContentManager::Unload` correctness | ⬜ | GLTF-436 | **Accept:** all owned resources released; no dangling `Effect`/`Texture2D`. |
| GLTF-439 | Device-loss behaviour | ⬜ | GLTF-438 | `GraphicsDevice::DebugSimulateContextLoss()` exists. **Accept:** a loaded glTF model survives or fails deterministically. |
| GLTF-440 | Model copy semantics | ⬜ | GLTF-438 | `ownedResources_` is a `shared_ptr<void>` shared by copies. **Accept:** documented and tested. |
| GLTF-441 | Morph re-upload cost | ⬜ | GLTF-285 | Full VB re-upload per weight change. **Accept:** measured; a dirty-range or GPU path scoped as ROBUST. |
| GLTF-442 | `BaseVertexBytes` memory duplication | ⬜ | GLTF-441 | 2× memory per morphed mesh. **Accept:** measured; decision recorded. |
| GLTF-443 | Occlusion remap codec cost | ⬜ | GLTF-433 | Decode → halve → PNG-encode → decode again. **Accept:** measured; short-circuited where possible. |
| GLTF-444 | `Model::sharedDrawBoneMatrices_` is static | ✔ | GLTF-440 | Not thread-safe; two `Model::Draw` calls on different threads race. **Accept:** made safe or the single-thread constraint documented and asserted. **Made safe: `static` → `static thread_local`.** A deliberate, behaviour-preserving deviation from FNA, which shares one buffer process-wide. The buffer is pure scratch — `CopyAbsoluteBoneTransformsTo` overwrites it in full at the top of every `Draw` and nothing reads it afterwards — so one per thread is observably identical for any single-threaded caller. What it removes is not a wrong matrix but a **use-after-free**: one thread's `resize` reallocates while another holds a reference into the same vector. **The test earned its keep the hard way.** Its first draft read the transforms back via `CopyAbsoluteBoneTransformsTo`, which reads `bones_` directly and never touches the scratch at all — it passed just as happily with the shared static restored. Rewritten to assert the effect's bound `World`, the only value `Draw` pushes *through* the buffer, it aborts with `double free or corruption` on the unfixed build and passes 5/5 on the fixed one. |

---

### Phase 23 — Documentation and release gate · owner: campaign
*Entry: Phases 0–22. Exit: the milestones can be claimed truthfully.*

| ID | Title | St | Deps | Scope, evidence → acceptance |
|---|---|---|---|---|
| GLTF-445 | `docs/gltf-conventions.md` | ⬜ | GLTF-132 | §11's tables, §15.1's joint matrix, §16.1's morph equation, §17's animation semantics. **Accept:** written and cross-referenced from the code. |
| GLTF-446 | `docs/gltf-conformance.md` | ⬜ | GLTF-416 | Corpus, oracle ladder, spec pin, tolerances. **Accept:** written. |
| GLTF-447 | `docs/gltf-limitations.md` | ⬜ | GLTF-334 | Every documented approximation and unsupported feature, with its report entry. **Accept:** written; matches the registry. |
| GLTF-448 | **Rewrite `CNAEXT.md` §3.2 to match reality** | ⬜ | GLTF-447 | The current text reads as a completeness claim (§3.1). **Accept:** each capability is marked implemented/partial/unsupported with its evidence. |
| GLTF-449 | Update `FUTURE.md` Phase 5 status | ⬜ | GLTF-448 | It currently says "glTF is **not** corrected." **Accept:** updated only when the milestone is actually met. |
| GLTF-450 | Retire `gltfissues.md` into the campaign record | ⬜ | GLTF-448 | Keep it as historical evidence; add a pointer to this plan. **Accept:** no contradiction between the two. |
| GLTF-451 | Reconcile `plan_cnj.md` Phases 12–14 | ⬜ | GLTF-448 | Mark which `CNB-*` claims this campaign superseded or corrected. **Accept:** no stale "complete" claim survives. |
| GLTF-452 | Update `known_bugs.md` | ⬜ | GLTF-012 | **Accept:** D1–D8 closed with their fixing task and test. |
| GLTF-453 | Update `AUDIT.md` glTF API coverage | ⬜ | GLTF-415 | Per `CLAUDE.md`, an API is not "complete" until its tests are. **Accept:** accurate. |
| GLTF-454 | Update `README.md` glTF claims | ⬜ | GLTF-448 | **Accept:** accurate. |
| GLTF-455 | Migration notes for `.cnj` consumers | ⬜ | GLTF-129 | **Accept:** the `cnjVersion` bump and new fields documented in `cnj.md`. |
| GLTF-456 | Doxygen coverage for new public/CNAEXT members | ⬜ | GLTF-025 | `CLAUDE.md` requires a full block comment on every public member. **Accept:** Doxygen clean. |
| GLTF-457 | `CHECKLIST.md` deviations table updated | ⬜ | GLTF-447 | Every intentional divergence from glTF/XNA recorded. **Accept:** complete. |
| GLTF-458 | Declare **GLTF CORE 2.0 CORRECT** | ⬜ | GLTF-429 | **Accept:** all 20 §27.1 rows green with named tests; the declaration cites them. |
| GLTF-459 | Declare **GLTF ROBUST** | ⬜ | GLTF-458 | **Accept:** all 12 §27.2 rows green. |
| GLTF-460 | Campaign retrospective | ⬜ | GLTF-459 | What the audit found vs what the fixes cost; what oracle layer caught what. **Accept:** written, so the next subsystem campaign can reuse the method. |

---

## 30. Final Acceptance Gates

### 30.1 Gate A — Center-collapse answered (end of the §28.2 Track A sequence)

| # | Gate |
|---|---|
| A1 | `docs/gltf-center-collapse-verdict.md` exists and names the **first divergent layer** for every asset the owner reported |
| A2 | A minimal failing fixture exists for each, in the generated corpus |
| A3 | Decoded CPU positions, expected CPU positions, generated vertex-buffer bytes and the full transform chain are recorded for each |
| A4 | Each has exactly one owning task ID from §29 |
| A5 | `f1`, `f2`, `f13` (node transforms), `f3` (sparse indices), `f4`/`f12` (topology) and `f9` (skin ancestor chain) all pass |
| A6 | **`GLTF-260` passes both halves**: the mesh-space cancellation exists **and** the new real node hierarchy does not re-apply the skinned mesh node's transform. Skinning is **not** accepted at `GLTF-248` alone |
| A7 | All 19 Track A tasks of §28.2 are closed, and `GLTF-103` recorded the adopted architecture (§11.5 Option A unless a concrete blocker was proven, time-boxed, and given a removal task) |
| A8 | The dependency graph remains acyclic — in particular `GLTF-103` does not depend, directly or transitively, on `GLTF-011` |

### 30.2 Gate B — `GLTF CORE 2.0 CORRECT`

All 20 rows of §27.1, each backed by a named automated test at its stated oracle layer, plus:

| # | Gate |
|---|---|
| B1 | `ctest -L gltf-conformance` green on `STUB`, `HEADLESS`, `OPENGLES3` and `VULKAN` |
| B2 | L5 and L6 byte-identical across those four renderers |
| B3 | Zero unresolved ASan/UBSan findings across the corpus |
| B4 | Every silent loss replaced by a `GltfImportReportEXT` entry — **grep proves no silent `continue`/`nullptr` drop remains in the import path** |
| B5 | `CNAEXT.md` §3.2, `docs/gltf-limitations.md` and the extension registry agree with each other and with the code |
| B6 | The 39 pre-existing glTF tests still pass |
| B7 | No third-party asset committed without a recorded licence |

### 30.3 Gate C — Viewer retake matrix

`cna-gltf-viewer`, unchanged except for `GLTF-421`…`GLTF-427`, renders each row correctly against
reference output, with **zero** compensating logic (`GLTF-428`):

| # | Row | What it proves |
|---|---|---|
| 1 | Static untextured mesh | Phases 2–5 |
| 2 | Textured mesh | Phases 9–10 |
| 3 | Full PBR model (all four maps + factors) | Phase 11 |
| 4 | Hierarchical multi-part model | Phase 5 — **the center-collapse retake** |
| 5 | Skinned model in bind pose | Phase 12 |
| 6 | Animated skinned model | Phases 12 + 14 |
| 7 | Rigid (unskinned) node animation | `GLTF-293` |
| 8 | Morph-target model | Phase 13 |
| 9 | `.glb` container | Phase 1 |
| 10 | `.gltf` with external `.bin` and external images | Phase 1 |
| 11 | Interleaved-buffer model | Phase 2 |
| 12 | Sparse-accessor model (attributes **and** indices) | Phase 2 + `GLTF-063` |
| 13 | Draco-compressed model | Phase 17 |
| 14 | Large real-world model (≥ 50 MB) | Phase 22 |

### 30.4 Gate D — `GLTF ROBUST`

All 12 rows of §27.2, plus Gate C passing on ≥ 4 renderers including one Direct3D path.

### 30.5 Standing campaign rules

1. **Never compensate for a CNA defect in `cna-gltf-viewer`** (§23).
2. **Never fix a shared-importer defect inside a renderer** (`GLTF-392`).
3. **Never silently reinterpret data.** Convert deliberately, or reject explicitly, and always report.
4. **Numerical oracles before screenshots.** A task is not done because a render looks right.
5. **A fixture and its expectation are generated from one source of truth** — they cannot drift.
6. **An API is not complete until its tests are** (`CLAUDE.md`).
7. **No third-party asset is committed without a licence review** (`GLTF-018`).
8. **Task ↔ commit traceability, not one commit per task.** `CLAUDE.md`'s repository-wide
   "one task = one commit" rule is written for its own single-task plans; a 460-task campaign
   applying it literally would produce 460 commits, enormous history noise, many intermediate
   states that do not build, and painful cherry-picks. This campaign therefore adopts the following
   as its explicit, scoped exception — traceability is preserved exactly, fragmentation is not
   forced:
   * every implementation commit **references one or more `GLTF-NNN` IDs** in its message;
   * every closed `GLTF-NNN` task **identifies the single commit that closed it**;
   * a commit must remain **coherent and, where practical, independently buildable and testable**;
   * **unrelated tasks must not be bundled merely for convenience** — grouping is legitimate only
     when the tasks form one coherent change (e.g. `GLTF-104`/`GLTF-105`/`GLTF-106`, which together
     lock the transform-convention invariants), never when it is just batching;
   * a task that changes behaviour lands with its test in the same commit.

---

## Appendix — Audit reproduction record

| Item | Value |
|---|---|
| Baseline | `origin/develop` @ `fb3728267e8f2179d43b96357ff372ae712b7e7f` |
| Planning branch | `claude/gltf-correctness-audit-plan-rxfs1l` |
| Viewer | `openeggbert/cna-gltf-viewer` @ `aaa008dc62bcb1127901ca23b75b4bf356c0ba66` (`develop`), read-only |
| sharp-runtime | `81624983c1e5388cb17e325480fdc2631a5cc653` (build dependency only) |
| Configure | `-DCNA_GRAPHICS_RENDERER=STUB -DCNA_BUILD_TESTS=OFF -DCNA_BUILD_EXAMPLES=OFF -DCNA_ENABLE_NET=OFF` |
| Build | `cmake --build cmake-build-debug --target cna_tool_gltf_to_cnj -j4` → **525/525, exit 0** |
| Draco | `libdraco` **absent** — the Draco path was not exercised (`GLTF-363`) |
| Fixtures | 14 hand-authored minimal `.gltf` files, base64 `data:` URI buffers, one semantic rule each |
| Fixture results | **8 defects confirmed**, **6 behaviours verified correct** (§1.1, §1.2) |
| Files changed by the planning session | `plan_gltf.md` **only** |
| Implementation performed | **none** |
