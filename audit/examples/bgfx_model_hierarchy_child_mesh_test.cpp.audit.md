# Audit: examples/bgfx_model_hierarchy_child_mesh_test.cpp

## Metadata

- Source file: `examples/bgfx_model_hierarchy_child_mesh_test.cpp` (205 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `Model`/`ModelBone` hierarchy transform-propagation
  pixel test (Task 813, closing the Phase 72 "Model" row group 812-813)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_model_hierarchy_child_mesh …)` /
  `cna_register_backend_test(NAME Bgfx_Model_HierarchyChildMesh …)`,
  `cmake/Tests/BgfxTests.cmake:854-858`).
- XNA/FNA relevance: direct — `Model::Draw`, `ModelBone::AddChild`/`Parent`,
  `Model::CopyAbsoluteBoneTransformsTo`, `ModelMesh.ParentBone`.
- FNA reference: `src/Graphics/Model.cs` (`Draw`, `CopyAbsoluteBoneTransformsTo`),
  `src/Graphics/ModelBone.cs`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/Model.cpp` (`Draw()` lines
  103-134, `CopyAbsoluteBoneTransformsTo()` lines 60-81), `ModelBone.cpp` (`AddChild()`),
  `ModelMeshPart.cpp` (`setEffectProperty()`'s auto-sync into `ModelMesh::effects_`).

## Purpose

Bgfx-specific adaptation (per the file's own header, Task 813) of
`examples/easygl_model_hierarchy_child_mesh_test.cpp` (Task 439). Builds a two-bone hierarchy —
root bone (identity transform) with one child bone translated `+0.6` on X, wired via
`ModelBone::AddChild` — and two same-shaped small quads: `meshRoot` (Red, `ParentBone` left
`nullptr`) and `meshChild` (Blue, `ParentBone` explicitly the child bone). It asserts (a) the root
mesh renders at NDC origin (its bone's identity transform) and (b) the child mesh renders shifted
right by the child bone's own translation — proving `Model::Draw` resolves *each* mesh's own
`ParentBone`'s absolute transform rather than always using bone 0 or the model's root bone.

## Executive Verdict

**Healthy** — both checks are genuine, correctly-targeted pixel assertions, and this audit
independently traced the full effect-registration chain (`ModelMeshPart::setEffectProperty` →
`ModelMesh::effects_` → `Model::Draw`'s `IEffectMatrices::setWorldProperty` call) to confirm the
test's construction order actually exercises the code path it claims to.

## Checklist Results

### API / XNA / FNA parity
`Model`'s 4-argument constructor (`bones, meshes, meshParentBones, rootBoneIndex`) used here
(`Model model(&dev, { &root, &child }, { &meshRoot, &meshChild }, { nullptr, &child });`,
line 127) is correctly marked `NOXNA` in `Model.hpp` — real XNA/FNA never exposes a public
multi-arg `Model` constructor to user code (it is populated internally by the content pipeline's
`ModelReader`), so this test-only construction path is properly out-of-band from the XNA surface.
`ModelBone::AddChild` (`root.AddChild(&child)`, line 114) matches `ModelBone.cs`'s
parent/child wiring semantics used by `CopyAbsoluteBoneTransformsTo`.

### Behavioral correctness
Traced `Model::Draw()` (`Model.cpp` lines 103-134) end-to-end for this fixture:
`CopyAbsoluteBoneTransformsTo` (lines 60-81) computes `sharedDrawBoneMatrices_[1] =
child.Transform * sharedDrawBoneMatrices_[0]` (root's own transform is Identity, so
`sharedDrawBoneMatrices_[1] = Translate(0.6,0,0)`), then the mesh loop
(lines 113-133) does `boneIdx = mesh->getParentBoneProperty() ? …->getIndexProperty() : 0`
(line 125-126) — `meshRoot`'s null `ParentBone` resolves to bone 0 (Identity), `meshChild`'s
explicit child-bone `ParentBone` resolves to bone 1 (the 0.6 translation) — exactly the two
values the test's own two checks assert on (`RunCheck(dev, 0.0f, 0.0f)` vs.
`RunCheck(dev, 0.6f, 0.0f)`, lines 170/177).
**Verified the test's effect-registration ordering is actually correct, not accidentally
degenerate**: `ModelMeshPart::setEffectProperty()` (`ModelMeshPart.cpp` lines 27-57) only
auto-adds the effect into the owning `ModelMesh`'s `ModelEffectCollection` (which `Model::Draw`
iterates to actually push `World`/`View`/`Projection` into the `BasicEffect` via
`IEffectMatrices`) if `parent_ != nullptr` at the time `setEffectProperty` is called. This file's
construction order is `ModelMeshPart partRoot(...)` → `ModelMesh meshRoot(&dev, "Root",
{&partRoot})` (sets `partRoot.parent_ = &meshRoot` inside the `ModelMesh` ctor) → **then**
`partRoot.setEffectProperty(&fxRoot)` (lines 116-118, same pattern for `partChild`/`fxChild` at
120-122) — i.e. the part's `parent_` is already non-null when the effect is set, so the
`World`/`View`/`Projection` push genuinely happens. Had the order been reversed (effect set before
the owning mesh's construction), `effects_` would stay empty and the test would pass or fail for
the wrong reason (World transform never applied, but the vertex positions being in local NDC space
could coincidentally still produce a visually-plausible-looking result for the root check).

### Logic
`RunCheck`'s retry loop (lines 90-144) redoes `Clear()`+full scene rebuild+`Draw()`+read every
iteration and breaks on the first non-background pixel — matches this shard's established
Task 406 (`GetBackBufferData` "first read per rendered frame") workaround pattern used
consistently elsewhere in this shard (verified via `bgfx::readTexture()`'s async
`targetFrame`-based completion contract in `BgfxGraphicsBackend.cpp`).

### C++ correctness
`ModelBone root(0, "root"); ModelBone child(1, "child");` and both `BasicEffect`/`VertexBuffer`
objects are stack-allocated locals recreated fresh every `RunCheck` iteration (lines 101-122) —
no dangling pointers into `Model`/`ModelMesh`, which only hold raw non-owning pointers to these
locals for the lifetime of the single `Draw()` call within the same iteration.

### Robustness
`colourMatch`'s `tol=40` (line 59) is generous enough to absorb the same
Blinn-Phong/interpolation-adjacent precision noise seen elsewhere in this shard, without being so
loose it would mask a completely wrong bone resolution (e.g. child rendering at the root's
position would leave the shifted sample point at background Green, `colourMatch(Green, Blue)`
fails outright — no partial-credit ambiguity).

### Testing
Two checks fully cover the row's stated intent (defaults-to-bone-0 for null `ParentBone`, and
correct resolution of a non-null `ParentBone`'s *own* absolute transform). No boundary/error-path
coverage is attempted (e.g. an out-of-range mesh-parent-bone index), but that is exercised by
`Model`'s constructor-level `std::out_of_range` throw elsewhere, not this pixel test's job.

## Detailed Findings

None — no correctness, lifetime, or test-validity defects found after tracing the full
bone-resolution and effect-registration chain against both the FNA reference and the current CNA
production source.

## Cross-File Observations

- Shares the exact `RunCheck` retry-loop idiom, `CullNone` workaround (Task 896 finding: NDC quad
  winding is back-facing under CNA's confirmed real default `RasterizerState`
  (`RasterizerState.cpp` line 11: `cullMode_(CullMode::CullCounterClockwiseFace)`), and
  `DepthStencilState`-based depth-disable substitution (`SetDepthTestEnabled(false)` throwing on
  Bgfx) with `bgfx_model_two_meshes_effects_test.cpp` (same batch) — consistent, not duplicated
  by accident; both are legitimate Bgfx-specific adaptations of the same EasyGL/Vulkan-era test
  family.
- `Model::Draw()`'s `boneIdx = mesh->getParentBoneProperty() ? … : 0` null-defaulting (line
  125-126) is a CNA-side defensive addition beyond FNA's own `Model.cs` (`Draw()` line 121:
  `sharedDrawBoneMatrices[mesh.ParentBone.Index]`, which has no null guard — real FNA would throw
  a `NullReferenceException` if `ParentBone` were ever null, which never happens in practice since
  `ModelReader` always assigns it). This is a reasonable, documented (per the file's own comment
  citing "Task 431's finding") C++ hardening, not a parity bug — worth noting as an intentional
  CNA-vs-FNA behavioral divergence for anyone diffing this method against the C# source line by
  line.

## Missing or Weak Tests

None identified beyond the boundary-case note above (out-of-range constructor arguments), which
is more naturally `Model`'s own unit-test responsibility than this pixel-integration test's.

## Positive Findings

- This is one of the few files in this shard where the audit fully traced a subtle
  multi-object construction-order dependency (`ModelMeshPart::setEffectProperty`'s
  `parent_ != nullptr` gate) and confirmed the test actually gets it right, rather than merely
  assuming "it compiles, so the object graph must be wired correctly."
- Correctly uses `NOXNA`-tagged constructors only for test scaffolding, never for anything user-
  facing in the `Microsoft::Xna` namespace.

## Final Assessment

A solid, correctly-targeted two-check hierarchy test whose pixel assertions are backed by an
independently-verified trace through `Model::Draw`, `CopyAbsoluteBoneTransformsTo`, and the
effect-registration auto-sync logic. No defects found.
