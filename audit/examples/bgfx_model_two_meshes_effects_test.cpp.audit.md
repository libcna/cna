# Audit: examples/bgfx_model_two_meshes_effects_test.cpp

## Metadata

- Source file: `examples/bgfx_model_two_meshes_effects_test.cpp` (192 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `Model`/`ModelMeshCollection` per-mesh-Effect pixel
  test (Task 812, Phase 72 Model row group)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_model_two_meshes_effects …)` /
  `cna_register_backend_test(NAME Bgfx_Model_TwoMeshesEffects …)`,
  `cmake/Tests/BgfxTests.cmake:833-838`).
- XNA/FNA relevance: direct — `Model::Draw` iterating `ModelMeshCollection`, each `ModelMesh`
  owning its own `ModelEffectCollection`/`Effect`.
- FNA reference: `src/Graphics/Model.cs` (`Draw`'s `foreach (ModelMesh mesh in Meshes) { foreach
  (Effect effect in mesh.Effects) {...} mesh.Draw(); }`), `src/Graphics/ModelMeshPart.cs` (`Effect`
  setter's auto-sync into `parent.Effects`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/Model.cpp` (`Draw()`),
  `ModelMesh.cpp` (`Draw()`), `ModelMeshPart.cpp` (`setEffectProperty()`).

## Purpose

Bgfx-specific adaptation (Task 812) of `examples/easygl_model_two_meshes_effects_test.cpp`
(Task 438). Builds one `Model` with a single root bone and **two** `ModelMesh` instances — a
Red left-half quad and a Blue right-half quad, each bound to its own `BasicEffect` instance with
`VertexColorEnabled = true` — and asserts each mesh renders its own effect's colour at its own
screen location. This proves `Model::Draw` genuinely iterates every mesh in
`ModelMeshCollection` and dispatches each mesh's *own* `Effect`, rather than only drawing the
first/last mesh or accidentally sharing one effect instance across both.

## Executive Verdict

**Healthy** — both checks are real, spatially-disjoint pixel assertions that would fail under the
specific failure modes the file claims to guard against (single-mesh-only draw, or effect
cross-contamination), and the underlying `Model::Draw`/`ModelMesh::Draw` mechanics were
independently traced and confirmed correct against both FNA and the current CNA source.

## Checklist Results

### API / XNA / FNA parity
Both meshes share a single root `ModelBone(0, "root")` (line 90) and leave `ParentBone` null on
both `ModelMeshPart`s — the file's own comment (lines 16-18) correctly cites this project's
established "Task 431" null-`ParentBone`-defaults-to-bone-0 CNA behavior (see the sibling
`bgfx_model_hierarchy_child_mesh_test.cpp` audit for the FNA-vs-CNA divergence this implies:
real FNA's `Model.cs` `Draw()` has no such null guard). Since both meshes use the identical
(identity) bone here, this divergence is not actually exercised/relevant to this specific test —
correctly scoped, no over-claiming.

### Behavioral correctness
Confirmed via `Model.cpp`'s `Draw()`: the effect loop iterates `mesh->getEffectsProperty()`
(the `ModelEffectCollection`, populated by `ModelMeshPart::setEffectProperty`'s auto-sync, not the
raw per-part `Effect*` field directly) to push `World`/`View`/`Projection`, then a **separate**
call, `mesh->Draw()`, iterates `meshParts_` and uses each part's own `getEffectProperty()` to
actually issue the draw call (`ModelMesh.cpp` lines 41-66). This test's construction order
(`ModelMeshPart partLeft(...)` → `ModelMesh meshLeft(&dev, "Left", {&partLeft})` → **then**
`partLeft.setEffectProperty(&fxLeft)`, lines 92-98) again correctly sequences the owning-mesh
assignment before the effect is set, so `fxLeft`'s registration into `meshLeft`'s
`ModelEffectCollection` (via the `parent_ != nullptr` gate in `ModelMeshPart::setEffectProperty`)
genuinely happens — same verified pattern as the sibling hierarchy test in this batch.
Two entirely separate `BasicEffect` instances (`fxLeft`, `fxRight`) with independently-set
`VertexColorEnabled` and independently-registered vertex/index buffers (`vbLeft_`/`ibLeft_` vs.
`vbRight_`/`ibRight_`, set up once in `Initialize()`, lines 115-144) genuinely rules out "same
Effect object reused for both meshes" as a way to accidentally pass.

### Logic
Same Task-406-driven per-check `RunCheck` retry-loop restructuring as this batch's other Model
tests (redo `Clear()`+rebuild+`Draw()`+read every iteration, break on first non-background pixel,
lines 74-111) — correctly applied here too.

### C++ correctness
`vbLeft_`/`vbRight_`/`ibLeft_`/`ibRight_` are member `unique_ptr`s populated once in
`Initialize()` and read (not recreated) inside every `RunCheck` iteration (lines 92, 96) — unlike
the sibling hierarchy test, which recreates its vertex buffers fresh per iteration. Both are
valid designs (this file's buffers are static per-mesh geometry, no per-iteration state to reset),
no lifetime issue: the buffers outlive every `ModelMeshPart`/`ModelMesh`/`Model` built inside
`RunCheck`, which are all local to that single call.

### Robustness
Sample points (`kSize/4, kSize/2` and `3*kSize/4, kSize/2`, lines 157/164) are comfortably inside
each half's own region (`[-1,0]` vs `[0,1]` in NDC, mapping to `[0,32]` vs `[32,64]` in the 64px
backbuffer) with no risk of straddling the shared boundary at `x=32`.

### Testing
The two checks are precisely the right pair to distinguish "only mesh N drawn" (either check alone
failing) from "effects swapped/shared" (both checks failing symmetrically opposite, e.g. left
region reads Blue) from "genuinely correct" (both PASS) — good discriminating power for a 2-check
test.

## Detailed Findings

None — no correctness or test-validity defects found.

## Cross-File Observations

- Effectively a spatial variant of `bgfx_model_hierarchy_child_mesh_test.cpp` (same batch):
  that file varies the mesh **transform** (via `ParentBone`) while holding effects/geometry
  otherwise similar; this file varies the mesh **effect binding** while holding the bone/transform
  fixed at identity for both meshes. Together they give reasonably orthogonal coverage of
  `Model::Draw`'s two independent per-mesh dispatch axes (bone resolution vs. effect resolution)
  without either file needing to cover both axes itself.
- Uses the identical `CullNone`/`DepthStencilState` Bgfx-specific substitutions as the rest of this
  batch's Model tests, consistent with the shard-wide Task 896/752-755 precedent.

## Missing or Weak Tests

None identified for this file's stated scope.

## Positive Findings

- Genuinely independent per-mesh vertex/index buffers and `BasicEffect` instances (not the same
  object reused with a mutated property between draws) make this a strong, not-easily-gamed test
  of "each mesh gets its own effect," rather than a weaker "same effect, different colour
  property" version that would leave open the possibility of one mesh's state leaking into the
  other's draw call.

## Final Assessment

A correctly-designed two-mesh/two-effect Model test with real spatial-discrimination power;
verified against both the FNA reference semantics and the current CNA `Model`/`ModelMesh`/
`ModelMeshPart` implementation. No defects found.
