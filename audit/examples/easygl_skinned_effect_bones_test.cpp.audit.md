# Audit: examples/easygl_skinned_effect_bones_test.cpp

## Metadata

- Source file: `examples/easygl_skinned_effect_bones_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `SkinnedEffect` bone-count pixel integration test
  (Task 193, per the file's own header)
- File type: C++ example/integration test
- Related production code: `include/Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp` /
  `src/.../SkinnedEffect.cpp` (`SetBoneTransforms`, `setWeightsPerVertexProperty`),
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp::EnsureSkinnedProgram()`
  (the actual GPU skinning-matrix-blend shader, lines 3272-3427)
- XNA/FNA relevance: `SkinnedEffect` is a real XNA 4.0 stock effect
  (`FNA-XNA/FNA/src/Graphics/Effect/StockEffects/SkinnedEffect.cs`); this test exercises its
  `SetBoneTransforms`/`WeightsPerVertex` bone-blending contract end-to-end through a real GPU draw.
- Main related tests: standalone — `SkinnedBonesTest` is itself the test; sibling coverage in this
  same batch: `easygl_skinnedeffect_combined_test.cpp` (single-draw-call 3-quad composition of
  this same bone-blend logic), `easygl_skinnedeffect_identity_bones_test.cpp` (default-palette
  variant of sub-test (a) below).

## Purpose

Verifies 3 bone-blending scenarios purely through pixel readback: (a) 1 bone, identity (no-op),
(b) 1 bone, a translation, (c) 2 bones blended 50/50. Each renders a quad whose on-screen position
after the bone transform is checked at 3 fixed pixel columns (left/centre/right), distinguishing
"moved" from "didn't move" and "blended" from "one bone dominating".

## Executive Verdict

**Mostly healthy.** The core bone-blend math is correctly derived and matches
`EnsureSkinnedProgram()`'s real shader (`skinMat = sum_i(uBones[indices[i]] * weights[i])`,
confirmed against lines 3300-3302 of `EasyGLGraphicsBackend.cpp`), and sub-test (c) is genuinely
discriminating (its own comment explains *why* the right pixel must stay green — proving bone 0's
full-weight translation was NOT applied alone). Two minor, real code-quality nits found (missing
`<array>` include, unused function parameters), neither of which affects correctness.

## Checklist Results

### API / XNA / FNA parity
`SkinnedEffect::SetBoneTransforms(std::vector<Matrix>)` and `setWeightsPerVertexProperty(int)`
match the real XNA `SkinnedEffect` API surface (bone palette + weights-per-vertex are genuine XNA
4.0 members, confirmed present in `SkinnedEffect.hpp`). Test usage is correct XNA-facing API use,
not a NOXNA extension.

### Behavioral correctness
Traced all 3 sub-tests against `EnsureSkinnedProgram()`'s vertex shader (`EasyGLGraphicsBackend.cpp`
lines 3298-3304):
```
mat4 skinMat=uBones[aBoneIndices.x]*aBoneWeights.x;
if(uWeightsPerVertex>=2) skinMat+=uBones[aBoneIndices.y]*aBoneWeights.y;
```
- **(a)** `weightsPerVertex=1`, bone[0]=identity, quad authored at NDC x:[-0.5,0.5] ->
  `skinMat=Identity` -> quad stays put. Test checks centre=red (inside), left/right=green
  (outside) — correct three-point check for "didn't move."
- **(b)** `weightsPerVertex=1`, bone[0]=Translate(+0.5,0,0), quad authored at x:[-1,0] -> shifts to
  x:[-0.5,0.5]. Checks centre=red (now covered) and left=green (moved away from its old position)
  — correct.
- **(c)** `weightsPerVertex=2`, bone[0]=Translate(+1,0,0) w=0.5, bone[1]=Identity w=0.5 ->
  `skinMat` (linear combination, not a full matrix blend/slerp — matches the shader's literal
  weighted-sum-of-matrices formula, which is also what real XNA/FNA's `Skin()` HLSL function
  does) -> net translation `0.5*1 + 0.5*0 = 0.5`, matching test's own derivation (file comment
  lines 16-19). Right-pixel=green is the key discriminating assertion: if bone 0's translation
  were mistakenly applied at full weight (a plausible off-by-one/weight-ignored bug), the quad
  would land at x:[0,1] and the right pixel (NDC ~+0.75) would incorrectly read red. This is a
  genuinely meaningful check, not just "does it render."

### Logic
`check()`'s per-sub-test pass/fail booleans use `R > G` (loose ordering, not exact color match) —
matches this shard's established convention of using textured/lit "red-dominant" predicates rather
than an exact-value comparison when real lighting math (`setAmbientLightColorProperty`,
`setDiffuseColorProperty`) is involved and not trivial to hand-derive to the byte. Reasonable given
the comment at line 148-149 pre-computes the expected result algebraically
(`litRGB = (emissive + ambient*diffuse) * diffuse = (1,0,0)*(1,0,0) = red`) — this is BasicEffect/
SkinnedEffect's ambient-folded-into-emissive convention, matches
`SkinnedEffect::FillGpuDrawParams()`'s real pre-fold behavior (confirmed at
`SkinnedEffect.cpp` line 336-338).

### Memory/resource lifetime
`VertexBuffer vb(dev, 6)` constructed fresh, locally, inside `drawAndCheck()` for each sub-test —
no reuse-after-scope-exit risk, standard RAII, no leak.

### C++ correctness
`makeQuad()` returns `std::array<SkinnedGpuVertex, 6>` (line 89-102) but the file's `#include`
list is only `<cstdint>`, `<cstdio>`, `<memory>`, `<vector>` (lines 38-41) — **`<array>` is never
included**. This will typically still compile because `<vector>`/other transitively-included
standard headers commonly pull in `<array>` on libstdc++/libc++, but it is a real missing-include
per the C++ standard and is not portable/guaranteed across standard library implementations or
future toolchain versions. `static_assert(sizeof(SkinnedGpuVertex) == 52)` (line 56) correctly
pins the layout used across all 3 sub-tests and matches `EasyGLGraphicsBackend.cpp`'s generically
handled stride-52 case (confirmed at that file's line 2277).

### Performance
N/A at this scale (3 draws, 6 vertices each).

### Thread safety
N/A — single-threaded `Game`-loop test.

### Architecture
Correct XNA-facing API usage layered over the EasyGL backend; no backend-specific code leaks into
the test beyond the documented `RasterizerState::CullNone` workaround (see Cross-File
Observations).

### Maintainability
`drawAndCheck(GraphicsDevice&, const SkinnedGpuVertex*, SkinnedEffect& fx, int W, int H)`
(line 104-114) declares `fx`, `W`, and `H` parameters but **never uses any of them** in its body —
the function only touches `dev` and `verts`. This is dead parameter surface (harmless — the
effect's `Apply()` is correctly called by the caller *before* `drawAndCheck()` runs, so omitting
`fx` from the signature entirely would change nothing), but it's a real, checkable maintainability
nit: a future reader might reasonably assume `fx`/`W`/`H` are used for something (e.g. per-call
uniform re-application) and be misled.

### Portability
See the missing `<array>` include above — `LOW` portability risk, `HIGH` confidence (grep-verified
absence, not inferred).

### Robustness
N/A — no external input, deterministic single-frame test.

### Testing
This file is itself the test.

### Cross-file consistency
`dev.setRasterizerStateProperty(RasterizerState::CullNone);` inside `drawAndCheck()` (line 112) is
annotated "Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
RasterizerState — needs CullNone" — same documented fix applied consistently across every sibling
`SkinnedEffect` test in this batch (`combined`, `fog`, `golden`, `identity_bones`, `multilight`),
confirming this isn't a one-off local workaround but a project-wide, tracked convention.

## Detailed Findings

- **LOW / HIGH confidence** — Missing `#include <array>` despite using `std::array` as
  `makeQuad()`'s return type (line 89, 98-101). Relies on transitive inclusion via `<vector>` or
  another already-included header; works today on this project's toolchain but is not a portable
  guarantee.
  - Category: C++ correctness / portability
  - Location: lines 38-41 (include list), 89 (`std::array<SkinnedGpuVertex, 6> makeQuad(...)`)
  - Why it matters: a standard-library update or a different compiler/STL combination could break
    this silently; trivial one-line fix if ever touched again.
- **LOW / HIGH confidence** — `drawAndCheck()`'s `fx`, `W`, `H` parameters are unused inside the
  function body.
  - Category: maintainability
  - Location: lines 104-114
  - Why it matters: purely cosmetic — no functional impact since the effect is already applied by
    the caller before this function runs — but is dead signature surface that could mislead a
    future reader into thinking per-call effect re-application happens here.

## Cross-File Observations

Shares the exact `SkinnedGpuVertex` (stride-52) layout, the same `RasterizerState::CullNone`
Task-896 workaround comment, and the same weighted-bone-sum verification approach as every other
`SkinnedEffect` test audited in this batch — a consistent, well-established test pattern across
the shard.

## Missing or Weak Tests

No 4th sub-test exercises `weightsPerVertex=4` (all 4 weight/index slots) even though the shader
(`EnsureSkinnedProgram()`) has a distinct `if(uWeightsPerVertex>=4)` branch summing indices z/w —
that branch is exercised by other tests in the broader suite (per the file's own Task numbering
context, e.g. Task 409's combined test uses `weightsPerVertex=2` only as well), but not by this
specific file. Not a defect in this file, just a coverage note for the shard as a whole.

## Positive Findings

- Sub-test (c)'s right-pixel=green assertion is a genuinely well-designed discriminating check —
  it specifically catches a "weight ignored, full bone-0 translation applied" class of bug that a
  naive centre-pixel-only check would miss entirely.
- Consistent application of the documented `RasterizerState::CullNone` fix, cross-referenced by
  Task number, showing real engineering discipline in tracking a project-wide gotcha.

## Final Assessment

A correct, meaningfully-discriminating bone-blend test with two minor, non-functional code-quality
nits (missing include, unused parameters). No behavioral defects found.
