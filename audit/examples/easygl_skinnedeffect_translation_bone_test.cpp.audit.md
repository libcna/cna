# Audit: examples/easygl_skinnedeffect_translation_bone_test.cpp

## Metadata

- Source file: `examples/easygl_skinnedeffect_translation_bone_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `SkinnedEffect` single-bone translation test
- File type: hand-rolled `Game`-derived executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_skinnedeffect_translation_bone …)` /
  `cna_register_backend_test(NAME EasyGL_SkinnedEffect_TranslationBone …)`,
  `cmake/Tests/EasyGLTests.cmake:660-663`).
- XNA/FNA relevance: direct — `SkinnedEffect.SetBoneTransforms`, real XNA 4.0 API. FNA source:
  `Graphics/Effect/StockEffects/HLSL/SkinnedEffect.fx`'s `Skin(vin, boneCount)`.
- Production code exercised: `SkinnedEffect::SetBoneTransforms`/`setWeightsPerVertexProperty`
  (`src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp`), `EasyGLVertexBufferBackend::ApplyLayout`
  stride-52 case (`EasyGLGraphicsBackend.cpp` lines 2277-2302), `EnsureSkinnedVertexLitProgram()`'s
  vertex-stage skin matrix build (lines 3489-3493, since `EnableDefaultLighting()` + the real XNA
  `PreferPerPixelLighting=false` default routes here).

## Purpose

Task 407's formalization of the pre-existing Task 123 skinning integration test into this shard's
per-task naming convention. All 6 vertices are 100%-weighted to bone 0 (`w0=1`, others 0; `i0=0`), and
bone 0 is a genuine, non-Identity `Matrix::CreateTranslation(0.5f, 0, 0)` — so `skinMat = 1 * Bones[0] =
Translate(+0.5,0,0)`, and the quad (authored covering NDC x:-1..0) should visibly shift to NDC x:-0.5..0.5
after the vertex shader applies it. Contrasts directly with the (not in this batch) identity-bone
baseline test the header comment references (Task 406).

## Executive Verdict

**Healthy.** The test's own arithmetic (`skinMat = Bones[0]`, translation `+0.5` in X, NDC-to-screen
mapping) is correct, matches `ApplyLayout`'s real stride-52 attribute layout byte-for-byte, and the
three-sample-point (`left`/`center`/`right`) design gives genuine positional discrimination rather than
a single ambiguous sample.

## Checklist Results

### API / XNA / FNA parity
`fx.SetBoneTransforms(std::vector<Matrix>{Matrix::CreateTranslation(0.5f,0,0)})` and
`fx.setWeightsPerVertexProperty(1)` match FNA's `SkinnedEffect.SetBoneTransforms(Matrix[])`/
(no direct `WeightsPerVertex` setter call needed here since `1` is the class's own default, but
setting it explicitly is harmless and self-documenting). `MaxBones=72` bound (`SkinnedEffect.hpp` line
28, `SkinnedEffect.cpp` FNA-reference cross-check already independently confirmed in this shard's
other reports) — a 1-element vector is well within range, and `SetBoneTransforms`
(`SkinnedEffect.cpp` lines 294-302) correctly rejects empty vectors and over-`MaxBones` vectors, neither
of which this test exercises (not a defect — those paths belong to a dedicated
argument-validation test, not this positional test).

### Behavioral correctness
`SkinnedGpuVertex`'s field layout (`px,py,pz,nx,ny,nz,u,v,w0..w3,i0..i3`, `static_assert(sizeof==52)`,
lines 41-49) matches `ApplyLayout`'s stride-52 case exactly: position at offset 0 (3 floats), normal at
12 (3 floats), UV at 24 (2 floats), weights at 32 (4 floats), indices at 48 (4 unsigned bytes) — verified
field-by-field against `EasyGLGraphicsBackend.cpp` lines 2292-2301. All 6 vertices set `w0=1,
w1=w2=w3=0, i0=0` (indices 0-3 unused since weight is 0), so `skinMat=uBones[0]*1.0` exactly, matching
the test's own stated intent.

The screen-space math: quad authored at NDC x∈[-1,0], y∈[-1,1] (lines 101-108); bone 0 translates
`+0.5` in X; `WVP = World*View*Projection`, all three Identity (lines 86-88), so clip-space = the raw
vertex position after skinning, i.e. the shifted quad occupies NDC x∈[-0.5,0.5]. Sample points at
`W/8` (NDC≈-0.75, outside), `W/2` (NDC≈0.0, inside), `7W/8` (NDC≈+0.75, outside) — correctly placed
relative to both the pre-shift (`[-1,0]`) and post-shift (`[-0.5,0.5]`) quad extents, so a shader that
silently ignored the bone transform (rendering the quad at its *un*-shifted position) would make the
`left` sample land inside the (unshifted) quad instead of green background — the test's own three-point
design would catch that failure mode, not just a wrong center pixel.

### Logic
`RasterizerState::CullNone` (line 82) is set with an explicit comment (lines 79-81) explaining this is
required because of a specific prior finding (Task 896, "once GraphicsDevice's real default
RasterizerState is pushed to every backend, this quad's winding is culled unless explicitly disabled")
— a genuine, traceable rationale rather than an unexplained defensive line, and consistent with the
identical comment/fix pattern in this shard's other skinning tests.

### Testing
Single-bone, single-frame, no-lighting-precision assertion (checks color dominance, not exact RGB
values) — an appropriately narrow test for "does a non-Identity bone transform actually move the
vertex," distinct in scope from `easygl_skinnedeffect_twobone_blend_test.cpp`'s multi-bone blend and
`easygl_skinnedeffect_weightspervertex_test.cpp`'s garbage-slot-ignoring test. No `Draw()`
retry-until-nonblack loop here (unlike the specular/preferperpixellighting tests in this shard) — since
the pass condition depends on `left`/`right` being green (background) rather than nonzero, a stray
black first frame would fail `leftOk`/`rightOk` too (both would read `(0,0,0)`, and
`leftPx.getGProperty() > leftPx.getRProperty()` is `0 > 0 = false`) — this is a latent single-frame
flakiness risk this file does not defend against that its sibling files (which use the retry loop) do.
See F1.

## Detailed Findings

### F1 — No retry-until-rendered guard against a black first frame, unlike sibling files in this shard

- Severity: LOW
- Confidence: MEDIUM
- Category: robustness / test flakiness
- Location/symbol: `Draw()` (lines 66-147) — single `dev.Clear()`/`DrawPrimitives()`/readback, no loop
- Evidence: `easygl_skinnedeffect_specular_test.cpp` and
  `easygl_skinnedeffect_preferperpixellighting_test.cpp` (this same shard) both wrap their draw/readback
  in an up-to-20-iteration loop specifically to skip a transient all-black frame
  (`if (got.R != 0 || got.G != 0 || got.B != 0) break;`). This file has no equivalent — if the very
  first `Draw()` call happens to read back before the swapchain/backbuffer is actually populated (the
  scenario the sibling files' own comments say motivated their loop), `leftPx`/`centPx`/`rightPx` would
  all read `(0,0,0)`, `leftOk`/`rightOk` would both be `false` (green-dominance check fails on all-zero),
  and the test would report `[FAIL]` — a possible source of environment-dependent flakiness this file
  does not share the sibling files' defense against.
- Why it matters: not a logic bug in this file's own math, but an inconsistency in defensive coding
  across near-identical sibling test files in the same shard, which increases the chance of an
  intermittent, non-reproducible CI failure that would be misdiagnosed as a real regression.
- FNA/XNA comparison: N/A (test infrastructure, not XNA behavior).
- Suggested future action (not implemented by this audit): adopt the same retry-until-nonblack (or
  until-not-all-background-green) loop convention already established by this shard's other files, for
  consistency and flake resistance.

## Cross-File Observations

- Shares this shard's broader Identity-`World` convention (`fx.setWorldProperty(Matrix::getIdentityProperty())`,
  line 86), so — like every other file in this batch — it cannot exercise or detect the missing
  world-space normal transform documented as F1/F2 in this shard's specular/preferperpixellighting
  reports. Low practical relevance *to this specific file*, since its own pass/fail condition
  (`centPx.getRProperty() > centPx.getGProperty() && centPx.getRProperty() > 50`) only checks red
  dominance, not an exact lit value, so it would very likely still pass even if the normal-transform bug
  were fixed or made worse — flagged here for completeness, not as a defect this file needs to address.
- `SkinnedGpuVertex`'s stride-52 layout is independently re-declared (not shared via a common header)
  in at least 5 files across this shard (`_specular_`, `_preferperpixellighting_`, `_translation_bone_`,
  `_twobone_blend_`, `_weightspervertex_`) — each with its own `static_assert(sizeof==52)`. Consistent
  today (all verified byte-identical), but a maintainability risk flagged once already by the
  production code's own comment (`ApplyLayout`'s stride-52 case, `EasyGLGraphicsBackend.cpp` lines
  2278-2290) about the layout being "independently duplicated... If that struct's field order or any
  individual offset ever changes... all 3 copies below must be updated together" — the same risk
  applies to these test files' own copies, times five, though a silent drift here would very likely
  surface as an immediate, loud rendering failure (wrong attribute reads garbage) rather than a subtle
  wrong-pixel bug, so the practical risk is lower than the production-code case.

## Missing or Weak Tests

- See F1 (no retry-until-rendered guard).
- No test in this file (reasonably, given its narrow scope) exercises a bone index other than 0, or a
  weight other than exactly 1.0/0.0 — covered instead by the two-bone/weights-per-vertex sibling files.

## Positive Findings

- Three-sample-point design (left/center/right) gives genuine positional proof, not just "center pixel
  looks textured" — actively distinguishes "shader ignored the bone transform" from "shader applied it
  correctly," a meaningfully stronger check than a single-point assertion would be.
- Clear, traceable rationale comment for the `RasterizerState::CullNone` requirement, tied to a specific
  prior finding (Task 896) rather than an unexplained defensive line.

## Final Assessment

A correct, well-targeted test for single-bone translation; its only real gap relative to sibling files
in the same shard is the missing retry-until-rendered guard (F1, LOW), a flakiness-hardening
inconsistency rather than a logic defect.
