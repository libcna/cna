# Audit: examples/easygl_model_two_meshes_effects_test.cpp

## Metadata

- Source file: `examples/easygl_model_two_meshes_effects_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `Model`/`ModelMesh`/`ModelMeshPart` multi-mesh,
  multi-effect pixel test
- File type: `Game`-derived executable (raw `Game`, not `PixelTestGame`), CTest-registered as
  `cna_test_easygl_model_two_meshes_effects` / `EasyGL_Model_TwoMeshesEffects`
  (`cmake/Tests/EasyGLTests.cmake:749-751`)
- XNA/FNA relevance: direct — exercises `Microsoft::Xna::Framework::Graphics::Model::Draw`,
  `ModelMesh::Draw`, `ModelMeshPart::setEffectProperty`, `BasicEffect`
- Production sources cross-checked: `src/Microsoft/Xna/Framework/Graphics/Model.cpp`,
  `ModelMesh.cpp`, `ModelMeshPart.cpp`, `ModelBone.cpp`/`.hpp`

## Purpose

Builds a `Model` with two `ModelMesh`es (`"Left"`, `"Right"`), each with one `ModelMeshPart` and its
own distinct `BasicEffect` instance (`fxLeft`/`fxRight`), and asserts that `Model::Draw` renders each
mesh with its *own* effect (Red left half, Blue right half) rather than sharing/reusing a single
effect or only drawing one of the two meshes. It is the two-effect extension of an established
single-mesh `Model.Draw` fixture (referenced as "Task 144").

## Executive Verdict

**Healthy.** Every line of the constructed scene was traced against `Model.cpp`/`ModelMesh.cpp`/
`ModelMeshPart.cpp` and the expected two-colour result follows directly and unambiguously from that
code; the test genuinely exercises per-mesh effect dispatch, not just "compiles and renders
something."

## Checklist Results

### API / XNA / FNA parity
Uses `Model`'s 3-argument constructor (`Model(GraphicsDevice*, vector<ModelBone*>, vector<ModelMesh*>)`,
`Model.cpp:15-23`), `ModelMeshPart`'s 6-argument constructor (vb, ib, numVertices, primitiveCount,
startIndex, vertexOffset — matches `ModelMeshPart.cpp:9-18`'s parameter order exactly), and
`ModelMeshPart::setEffectProperty`. No FNA source exists for a literal "two-mesh" sample, but the
individual API surface used (`Model`, `ModelMesh`, `ModelMeshPart`, `ModelBone`) is standard XNA
`Microsoft.Xna.Framework.Graphics` and matches FNA's `Model.Draw` foreach-mesh/foreach-effect
iteration pattern.

### Behavioral correctness
Traced construction order precisely: `ModelMeshPart partLeft(...)` is built first, then
`ModelMesh meshLeft(&device, "Left", { &partLeft })` runs its constructor
(`ModelMesh.cpp:20-28`), which sets `partLeft.parent_ = &meshLeft` **before**
`partLeft.setEffectProperty(&fxLeft)` is called at line 127 of the test. This ordering matters:
`ModelMeshPart::setEffectProperty` (`ModelMeshPart.cpp:27-57`) only registers the effect into the
owning mesh's `ModelEffectCollection` when `parent_ != nullptr`, so calling `setEffectProperty`
*after* mesh construction (as this test does) is required for `mesh->getEffectsProperty()` to be
non-empty when `Model::Draw` iterates it — the test gets this ordering right.

`Model model(&device, { &root }, { &meshLeft, &meshRight })` uses the 3-arg ctor, which sets
`root_ = bones_.bones_[0]` (`Model.cpp:21-22`) and leaves every mesh's `parentBone_` as its default
`nullptr` (never touched by this ctor). `Model::Draw` (`Model.cpp:103-134`) computes
`boneIdx = mesh->getParentBoneProperty() ? …->getIndexProperty() : 0` — the test's own header
comment correctly cites this as "Task 431's audit finding" (a documented, intentional CNA behavior:
a null `ParentBone` defaults to bone index 0 rather than crashing, unlike FNA's `Model.cs:119`,
which dereferences `mesh.ParentBone.Index` unconditionally and would throw `NullReferenceException`
on a null `ParentBone`). Since `ModelBone::transform_` default-initializes to
`Matrix::getIdentityProperty()` (`ModelBone.hpp:73`) and `parent_` defaults to `nullptr`
(`ModelBone.hpp:74`), `CopyAbsoluteBoneTransformsTo` (`Model.cpp:60-81`) computes
`sharedDrawBoneMatrices_[0] = Identity` for the single root bone — so both meshes render with
`World = Identity * Identity = Identity`, matching the test's flat, unrotated NDC quads exactly.

`ModelMesh::Draw()` (`ModelMesh.cpp:41-66`) iterates `meshParts_`, skips a part if
`effect == nullptr || primitiveCount <= 0` (neither true here — 2 triangles, real effect pointers),
binds the mesh's own vertex/index buffer, and issues `DrawIndexedPrimitives` per effect pass. Since
`fxLeft`/`fxRight` are distinct `BasicEffect` instances with `VertexColorEnabled = true`, the two
meshes' independently-set world/view/projection and diffuse colour genuinely diverge — the resulting
pixels are not a coincidental match.

### Logic
`RasterizerState::CullNone` is set once, globally, before either `model.Draw()` call — correctly
noted as a carry-over from "Task 896's finding" (this project's own established convention that
these hand-authored CCW screen-space quads are back-facing under the real default
`RasterizerState.CullCounterClockwise`). Sample points (`W/4, H/2` and `3*W/4, H/2`) fall solidly
inside the left ([-1,0]→[0,W/2]) and right ([0,1]→[W/2,W]) NDC halves respectively, well clear of the
shared boundary at `W/2` — no off-by-one risk.

### Memory/resource lifetime
`fxLeft`/`fxRight`, `root`, `partLeft`/`partRight`, `meshLeft`/`meshRight` are all stack locals
scoped to the single `Draw()` call, and `Model`/`ModelMesh`/`ModelMeshPart` store only non-owning raw
pointers to them (`Model.cpp`/`ModelMesh.cpp`/`ModelMeshPart.cpp` never take ownership) — all
consumed synchronously within the same call, so lifetime is correct without any dangling-pointer
risk. `vbLeft_`/`vbRight_`/`ibLeft_`/`ibRight_` are member `unique_ptr`s created once in
`Initialize()`, outliving the single `Draw()` call that uses them.

### C++ correctness
`colourMatch` casts `getRProperty()` etc. (an unsigned byte-returning getter, per project convention)
to `int` before subtracting — correct, avoids unsigned-underflow wraparound that a raw
`byte - byte` subtraction could produce.

### Architecture
Exercises the real `Model`/`ModelMesh`/`ModelMeshPart` object graph end-to-end (not a mock), which is
the appropriate way to validate `Model::Draw`'s mesh/effect iteration contract.

### Testing
Effectively a regression test for exactly one thing: that `Model::Draw` applies each mesh's own
`Effect` rather than a single shared one. It does not exercise a real multi-bone hierarchy (only one
root bone, `ModelBone(0, "root")`, is used, and both meshes' `ParentBone` stays null/defaults to
index 0) — so the interaction between *distinct* parent bones and per-mesh world transforms (the
`sharedDrawBoneMatrices_[boneIdx] * world` term for `boneIdx != 0`) remains untested by this file,
though that is arguably out of this file's stated scope (which the header comment describes
correctly as being about per-mesh effects, not bone hierarchies).

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — Test scope does not exercise non-zero `ParentBone` indices

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `ModelTwoMeshesEffectsTest::Draw` (lines 123-136); `Model::Draw`
  (`Model.cpp:125-126`)
- Evidence: both meshes use `ModelMeshPart`/`ModelMesh` with `parentBone_` left at its default
  `nullptr`, and the `Model` only has one bone (`root`, index 0) — the `boneIdx != 0` branch of
  `Model::Draw`'s `sharedDrawBoneMatrices_[boneIdx] * world` computation is never taken by this file.
- Why it matters: a regression that swapped `sharedDrawBoneMatrices_[boneIdx]` for the wrong index
  (e.g. always 0 regardless of `boneIdx`) would not be caught by this test, only by a genuinely
  multi-bone fixture.
- Related files: a hypothetical `easygl_model_multi_bone_hierarchy_test.cpp` (not confirmed to exist
  in this shard) would be the natural place to close this gap.

## Cross-File Observations

- The null-`ParentBone`-defaults-to-bone-0 behavior (`Model.cpp:125-126`) is a documented,
  intentional deviation from FNA's `Model.cs:119` (`mesh.ParentBone.Index`, unconditional, would
  throw `NullReferenceException` on null) — already flagged internally per the test's own "Task 431"
  reference, and correctly exercised (not newly discovered) by this file.
- `Model::Draw`'s `dynamic_cast<IEffectMatrices*>(effect)` throwing `std::runtime_error` on failure
  (`Model.cpp:121-123`) is a reasonable C++ analogue of FNA's `InvalidOperationException` on the same
  `as IEffectMatrices` failure (`Model.cs:115-119`) — not exercised by this file (both effects here
  are `BasicEffect`, which does implement `IEffectMatrices`), but consistent with the project's
  documented "exception type may differ, behavior equivalent" convention.

## Missing or Weak Tests

- No fixture in this file (or, so far as checked, its immediate siblings) exercises a `Model` with a
  genuine multi-bone hierarchy and non-zero per-mesh `ParentBone` indices together with multiple
  effects — see F1.

## Positive Findings

- Correct, load-bearing ordering of `ModelMeshPart` construction → `ModelMesh` construction →
  `setEffectProperty()` — get this backwards (call `setEffectProperty` before the mesh exists) and
  the effect would silently never register in `ModelEffectCollection`, which this test's author
  evidently understood and got right.
- Clear, decomposed pass/fail reporting (`check()` helper prints a labelled PASS/FAIL line per
  assertion) makes a future failure immediately diagnosable without needing to re-read the test.

## Final Assessment

A small, correctly-targeted test whose expected result is exactly derivable from the real
`Model`/`ModelMesh`/`ModelMeshPart` source it exercises; the only gap is scope (single flat bone
hierarchy), not correctness.
