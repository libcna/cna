# Audit: examples/easygl_emissive_ambient_composition_test.cpp

## Metadata

- Source file: `examples/easygl_emissive_ambient_composition_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration/regression test (audit_net.md remediation, "sixth round",
  2026-07-18), `examples-tests-easygl` shard
- File type: C++ integration-test executable (`Game` subclass, `main()`), pixel-readback style,
  no `pass_`/`fail_` counters — single `result_` flag, exit code 0/1
- Related production code: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EnsureSkinnedProgram`, `EnsureSkinnedVertexLitProgram`, `EnsureEnvMapped3DProgram`),
  `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp`,
  `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp` (`FillGpuDrawParams`)
- FNA reference: `src/Graphics/Effect/StockEffects/HLSL/Lighting.fxh`'s `ComputeLights()`
  (`result.Diffuse = mul(diffuse, lightDiffuse) * DiffuseColor.rgb + EmissiveColor;`) — this file's
  entire premise is a direct pixel-level regression test against that one line of FNA reference
  behavior.
- Main related tests: `examples/easygl_skinned_effect_bones_test.cpp` (explicitly named in this
  file's own header comment as a test that could NOT discriminate this bug because it uses
  DiffuseColor=(1,0,0), where `x*x == x`); `examples/easygl_environmentmapeffect_amount_zero_test.cpp`
  (also named, as a test that deliberately sidesteps the same confound by pinning DiffuseColor to
  (1,1,1)).

## Purpose

A discriminating pixel regression test for how `EmissiveColor` composes with lit `DiffuseColor`
across all three EasyGL shader paths (`SkinnedEffect` per-pixel lighting, `SkinnedEffect` vertex
lighting, `EnvironmentMapEffect`) that had a documented composition bug: instead of FNA's `result =
lightSum * DiffuseColor + EmissiveColor` (additive), the buggy EasyGL shaders computed
`(EmissiveColor + lightSum) * DiffuseColor`. Since `FillGpuDrawParams()` pre-folds ambient into
emissive (`emissive + ambient*diffuse`), the buggy multiplicative form evaluated the ambient
contribution as `ambient*diffuse²` — a quadratic suppression that crushed dark materials. The test
deliberately pins `DiffuseColor=(0.25, 0.5, 0.75)` — three values strictly between 0 and 1, all
different — specifically because `x² ≠ x` at every one of those values, and zeroes all three
directional lights so the ambient-folded emissive term is the only contributor, isolating the
composition bug from every other shading term.

## Executive Verdict

**Healthy** — an exceptionally well-designed, self-documenting regression test. Its header
comment gives a full derivation of both the correct and buggy expected pixel values
((64,128,191) vs. (16,64,143), a 48-64 level per-channel gap, far outside its own ±20 tolerance),
and independent verification against the actual current EasyGL shader source confirms both the
formula the test expects and the fact that all three shader paths it targets have in fact been
fixed to match FNA.

## Checklist Results

### API / XNA / FNA parity
`SkinnedEffect`/`EnvironmentMapEffect` properties used (`setDiffuseColorProperty`,
`setEmissiveColorProperty`, `setAmbientLightColorProperty`, `DirectionalLight0/1/2`,
`setPreferPerPixelLightingProperty`, `SetBoneTransforms`, `setWeightsPerVertexProperty`,
`setEnvironmentMapAmountProperty`, `setEnvironmentMapSpecularProperty`, `setFresnelFactorProperty`)
all match FNA's `SkinnedEffect`/`EnvironmentMapEffect` public surface.

### Behavioral correctness
Independently re-derived and confirmed the test's own math:
- `zeroAllLights()` (lines 141-153) sets each `DirectionalLight{0,1,2}`'s diffuse/specular to
  `(0,0,0)` (not just "disabled") and direction to `(0,0,-1)` — zeroing the *magnitude*, not
  relying on an `Enabled=false` gate, which is the more robust choice since it isolates the
  composition formula regardless of how "disabled" is implemented downstream.
- Traced `SkinnedEffect::FillGpuDrawParams()`/`EnvironmentMapEffect::FillGpuDrawParams()`: both
  compute `emissiveColor[n] = (emissiveColor_.n + ambientLightColor_.n * diffuseColor_.n) *
  alpha_` (confirmed at `SkinnedEffect.cpp` lines 336-338 and `EnvironmentMapEffect.cpp`, same
  pattern) — with `emissiveColor_=(0,0,0)`, `ambientLightColor_=(1,1,1)`, `diffuseColor_=kDiffuse`,
  `alpha_=1` (default), this yields exactly `kDiffuse` as the GPU-side "emissive" parameter,
  matching the test's own derivation (comment lines 31-34).
- Traced the actual current shader formulas in `EasyGLGraphicsBackend.cpp`:
  `EnsureSkinnedProgram()`'s fragment shader (line 3380): `litRGB =
  lightSum*uDiffuseColor.rgb+uEmissiveColor;` — additive, matches FNA, matches this test's
  "CORRECT" expectation.
  `EnsureSkinnedVertexLitProgram()`'s vertex shader (grep-confirmed): `vLitRGB =
  lightSum*uDiffuseColor.rgb+uEmissiveColor;` — same, additive, correct.
  `EnsureEnvMapped3DProgram()`'s fragment shader (line 3229): `litRGB =
  lightSum*uDiffuseColor.rgb+uEmissiveColor;` — same, additive, correct.
  All three shader paths this file targets have the fixed (additive) formula, not the buggy
  (multiplicative) one — the "sixth round" fix this file was written to confirm is genuinely
  present in the current source, not just claimed.
- With `lightSum=0` (all lights zeroed) and `litRGB = 0*kDiffuse + kDiffuse = kDiffuse`, and
  `texColor=(1,1,1,1)` (a white 1×1 texture, `whiteTex_`, set in `LoadContent()`), `baseColor =
  litRGB*texColor.rgb = kDiffuse = (0.25,0.5,0.75) → (64,128,191)` in 8-bit — exactly the test's
  `kExpected`. Independently re-derived; matches.
- `DrawSkinned` binds all vertices 100% to bone 0 (identity) via a packed 52-byte
  `SkinnedGpuVertex` (`w0=1, w1=w2=w3=0`, `i0..i3=0`), correctly making skinning a no-op so only
  the lighting composition affects the result — a deliberate, correct isolation technique, matching
  the same approach used in `easygl_skinned_effect_bones_test.cpp` (per this file's own comment,
  lines 172-174).

### Logic
`colourMatch()`/`closeTo()` (tol=20 default) correctly implemented as a per-channel absolute
difference bound; `check()` (lines 112-129) prints both the actual buggy-would-be value and the
expected value on failure, which is genuinely useful diagnostic output distinguishing "some other
bug" from "the exact regression this test targets."

### Memory/resource lifetime
`whiteTex_` is a `Texture2D` member (not a pointer), default-constructed then assigned in
`LoadContent()` (line 248) — `Texture2D`'s move/copy-assignment semantics are relied upon here;
no ownership issue found (consistent with how `Texture2D` is used as a value type elsewhere in
this shard, e.g. `easygl_env_map_test.cpp`). `makeSolidCube()` returns `std::unique_ptr<TextureCube>`
with clear ownership, held in a local `greenCube` inside `DrawEnvMap()` — correctly scoped, no leak.

### C++ correctness
`static_assert(sizeof(SkinnedGpuVertex) == 52, ...)` (line 84) is a real, load-bearing compile-time
check — the struct's field layout (`float px,py,pz,nx,ny,nz,u,v,w0,w1,w2,w3; uint8_t
i0,i1,i2,i3;`) sums to exactly 48+4=52 bytes assuming no padding (all `float`s first, then 4
`uint8_t` at the end, naturally aligned) — verified by hand: 3+3+2+4=12 floats × 4 bytes = 48,
plus 4 bytes of `uint8_t` = 52, matches. Correct and a good defensive check against silent ABI
drift if the struct were ever reordered.

### Performance
N/A — a one-shot `Draw()` call (`done_` flag gates re-entry, line 254), three draws total.

### Thread safety
N/A.

### Architecture
Correctly uses public XNA-facing API only; the `SkinnedGpuVertex` packed struct is a
test-local convention matching the backend's expected raw GPU layout (via `VertexBuffer::SetDataRaw`,
line 198), consistent with the same technique used in the sibling `easygl_skinned_effect_bones_test.cpp`.

### Maintainability
Exceptionally well-commented — the header (lines 1-39) is a small self-contained postmortem: cites
the exact FNA reference line, explains why two *other* existing tests could not have caught this
bug (with concrete reasoning, not just assertion), and gives a full numeric derivation of both the
correct and incorrect expected outputs. This is exactly the kind of documentation this project's
own `CLAUDE.md`/audit standard asks test authors to produce, done to an unusually high standard.

### Portability
N/A.

### Robustness
N/A — happy-path regression test by design.

### Testing
This file is itself a test. See Missing or Weak Tests.

### Cross-file consistency
Fully consistent with `SkinnedEffect.cpp`/`EnvironmentMapEffect.cpp`'s `FillGpuDrawParams()` and
the three EasyGL shader programs it targets — every claim in the header comment about the current
(fixed) and historical (buggy) formula was independently verified against the current source, not
merely trusted.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — this file is a high-quality, verified-accurate regression test.

### F1 — `DrawEnvMap`'s call to `dev.DrawUserPrimitives` versus `DrawSkinned`'s `SetVertexBuffer`+`DrawPrimitives` is an inconsistency worth a one-line note, not a defect

- Severity: INFO
- Confidence: HIGH
- Category: maintainability
- Location/symbol: `DrawSkinned()` (lines 197-200, uses `VertexBuffer`+`SetDataRaw`+
  `SetVertexBuffer`+`DrawPrimitives`) vs. `DrawEnvMap()` (line 238, uses
  `dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2)` directly on a
  `VertexPositionNormalTexture[]` stack array)
- Evidence: the file's own comment (lines 172-174) explains the reason for `DrawSkinned`'s more
  verbose path: `DrawUserPrimitives` does not carry the skinned vertex channels (weights/indices),
  so it renders nothing for a skinned draw — this is a documented, deliberate difference, not an
  oversight, since `EnvironmentMapEffect`'s vertex type (`VertexPositionNormalTexture`) has no such
  extra channels and is compatible with `DrawUserPrimitives`.
- Why it matters: purely a readability note for a future maintainer skimming the file — the
  asymmetry is already explained inline, so no action needed.

## Cross-File Observations

- This file names and reasons about two sibling tests explicitly
  (`easygl_skinned_effect_bones_test.cpp`, `easygl_environmentmapeffect_amount_zero_test.cpp`) —
  cross-checked both claims:
  - `easygl_skinned_effect_bones_test.cpp`'s use of DiffuseColor=(1,0,0) is confirmed unable to
    discriminate the bug (both `x` and `x²` equal `x` at 0 and 1) — the claim in this file's
    header is accurate.
  - `easygl_environmentmapeffect_amount_zero_test.cpp` does pin DiffuseColor to its default (1,1,1)
    and its own header comment independently confirms the same reasoning — see that file's own
    audit report for details; the cross-reference between the two files is accurate and mutually
    consistent.
- `easygl_env_map_test.cpp` (also in this batch) does **not** pin `DiffuseColor=(1,1,1)` for
  every sub-test's emissive assertions the same defensive way — see that file's own audit report
  (F1) for a related but distinct finding about that file's stale header-comment formula.

## Missing or Weak Tests

- Only `SkinnedEffect` and `EnvironmentMapEffect` are covered; the header comment's own bug
  description also implicates `BasicEffect`'s ambient/emissive composition path indirectly (since
  the fix comment at `EasyGLGraphicsBackend.cpp` line 3371 references
  "`EnsureLit3DProgram`/`EnsureLit3DVertexLitProgram` in this same file already did correctly" as
  the reference the other two were brought into line with) — no direct test in this file (or
  found by name in this shard) re-confirms `BasicEffect`'s own composition with this same
  discriminating (non-0/1) DiffuseColor technique, though it is asserted to have already been
  correct before this fix.

## Positive Findings

- Exceptional self-documentation: explains not just what is tested but why two specific *other*
  tests in the suite could not have caught the bug, with worked numeric reasoning.
- Deliberately chooses non-saturated, mutually-distinct component values
  (`DiffuseColor=(0.25,0.5,0.75)`) specifically to make a squared-vs-linear formula bug
  unmistakable — a materially better test design than using saturated 0/1 values, and the file
  explicitly credits this same lesson to a "Task 383" precedent.
- All three targeted shader formulas were independently verified (not just trusted) against the
  current `EasyGLGraphicsBackend.cpp` source and found to match both the test's expectations and
  FNA's `Lighting.fxh` reference behavior.

## Final Assessment

One of the strongest test files reviewed in this batch: it identifies a real, subtle, previously-
shipped bug class (quadratic ambient suppression via double diffuse-multiplication), explains why
existing coverage missed it, derives concrete numeric expectations, and — on independent
verification against the current shader source — is confirmed to be testing against code that has
in fact been fixed to match the derivation. No corrective action needed for this file itself.
