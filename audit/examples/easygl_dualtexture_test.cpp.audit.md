# Audit: examples/easygl_dualtexture_test.cpp

## Metadata

- Source file: `examples/easygl_dualtexture_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test (`Game` subclass + `main()`), registered as CTest
  `EasyGL_DualTexture` (`cmake/Tests/EasyGLTests.cmake:1179-1181`, `cna_test_easygl_dualtexture`)
- Related production code: `Microsoft::Xna::Framework::Graphics::DualTextureEffect`
  (`include/.../DualTextureEffect.hpp`, `src/.../DualTextureEffect.cpp`),
  `CNA::Internal::Backends::EasyGL::EasyGLGraphicsBackend::EnsureDualTextured3DProgram()`
  (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:3009-3070`)
- XNA/FNA relevance: exercises `DualTextureEffect` (`Microsoft::Xna::Framework::Graphics`), a real
  XNA 4.0 stock effect. FNA reference: `Graphics/Effect/StockEffects/DualTextureEffect.cs` +
  `HLSL/DualTextureEffect.fx` (`PSDualTexture`).
- Main related tests: sibling files in this batch (`easygl_dualtextureeffect_doubling_test.cpp`,
  `..._combined_test.cpp`, etc.) plus `tests/Microsoft/Xna/Framework/Graphics/DualTextureEffectTests.cpp`
  (CPU-side `FillGpuDrawParams()` unit tests).

## Purpose

`DualTextureTest` (a `Microsoft::Xna::Framework::Game` subclass) draws a full-screen NDC quad four
times with `DualTextureEffect`, varying `Texture`/`Texture2`/`DiffuseColor`, and reads back the
center pixel via `GetBackBufferData` to confirm both texture slots and the diffuse multiplier are
actually wired into the render path (as opposed to only one slot mattering, or the multiply being a
no-op). Placement under `examples/` with the `easygl_` prefix and CMake registration under
`EasyGLTests.cmake` is correct and consistent with this project's ~570-file backend-integration-test
convention documented in `AUDIT_SCOPE.md`.

## Executive Verdict

**Mostly healthy** — the four sub-cases correctly exercise `DualTextureEffect`'s two-texture-multiply
and diffuse-multiply behavior and each check is independently justified. However, the file's own
header comment (lines 4-5) states the shader formula as `texture(uTexture) * texture(uTexture2) *
uDiffuseColor`, omitting the real shader's `color.rgb *= 2` doubling factor
(`EnsureDualTextured3DProgram()` line 3051, matching FNA's `PSDualTexture`) — and because every one
of this test's four color combinations uses pure 0/1-saturated channel values, none of its
assertions can actually distinguish "doubling implemented" from "doubling silently missing" (see F1).
This is not a new discovery — the project's own later `easygl_dualtextureeffect_doubling_test.cpp`
(Task 383) explicitly documents that this exact test (named "Task 191" in its own header) was one of
several that "missed [the doubling factor] entirely... since all of them used pure 0/1-saturated
texture values" — so the gap is already known and deliberately left in place by a purpose-built
sibling test, but this file's own comment was never corrected to reflect that.

## Checklist Results

### API / XNA / FNA parity
`DualTextureEffect::setTextureProperty`/`setTexture2Property`/`setDiffuseColorProperty`/`Apply()` are
exercised with the correct XNA property names (mapped to CNA's `getX`/`setX` convention). No
API-shape issues.

### Behavioral correctness
Verified by hand for all 4 sub-cases against the actual EasyGL fragment shader
(`base=texture(uTexture); base.rgb*=2; FragColor=base*texture(uTexture2)*uDiffuseColor;`, clamped to
`[0,1]` on output):
- (a) tex1=white(1,1,1), tex2=blue(0,0,1): `base=(2,2,2)`, `FragColor=(0,0,2)→clamp→(0,0,1)`=blue. Matches.
- (b) tex1=red(1,0,0), tex2=white: `base=(2,0,0)`, `FragColor=(2,0,0)→clamp→(1,0,0)`=red. Matches.
- (c) tex1=white,tex2=white,diffuse=green(0,1,0): `base=(2,2,2)`, `FragColor=(0,2,0)→clamp→(0,1,0)`=green. Matches.
- (d) tex1=yellow(1,1,0)×tex2=cyan(0,1,1): `base=(2,2,0)`, `FragColor=(0,2,0)→clamp→(0,1,0)`=green. Matches.
All four pass with the real shader, confirming the test's assertions are correct in the sense that
they don't produce false failures — but see F1 for what they fail to discriminate.

### Logic
`colourMatch` (line 37-42) compares only R/G/B with a generous `tol=40` (of 255) — reasonable for a
solid-color full-screen quad test where MSAA/driver blending isn't a factor here (no partial-coverage
edges are sampled — only the exact viewport center pixel is read). `Task 896 finding` comment (line
113-114) correctly documents why `RasterizerState::CullNone` is required: the hand-authored NDC quad
winding is back-facing under CNA's real default cull state.

### Memory/resource lifetime
Five `Texture2D` locals (`texWhite`, `texRed`, `texBlue`, `texYellow`, `texCyan`) are stack-scoped for
the whole `Draw()` call and referenced only by raw pointer from `DualTextureEffect::setTextureProperty`
(non-owning, per the effect's own documented contract) — correct, no dangling-pointer risk since the
`DualTextureEffect` instances are also stack-scoped within the same or a shorter lifetime, each
constructed and destroyed inside its own `{ }` sub-block before `readCenter` returns.

### C++ correctness
No `<cstdlib>`/`<cmath>` include despite using `std::abs(int)` (only `<cstdio>`, `<memory>` are
included, line 31-32) — relies on `std::abs` being transitively visible through another header, most
likely by an indirect standard-library include. This is not guaranteed by the C++ standard, so it is
theoretically non-portable across a stricter standard-library implementation, but very unlikely to
actually break here since every sibling test in this batch (`_alpha_test.cpp`, `_combined_test.cpp`,
`_doubling_test.cpp`, `_null_texture0_test.cpp`, `_null_texture2_test.cpp`) *does* include
`<cstdlib>` explicitly for the identical `std::abs` usage — this file is the outlier. `LOW`
severity/maintainability, see F2.

### Performance
N/A — single-frame test, no hot-path concerns.

### Thread safety
N/A — single-threaded `Game` loop.

### Architecture
Correct layering: the test only calls public `Microsoft::Xna::Framework` API
(`DualTextureEffect`, `GraphicsDevice`, `Texture2D`) — no direct backend/EasyGL symbol usage, matching
the project's XNA-facing example convention.

### Maintainability
Small, focused, well-commented file (186 lines). The stale/incomplete shader-formula comment (line
4-5) is the only maintainability issue found — see F1.

### Portability
No platform-specific code.

### Robustness
N/A — this is itself a correctness-verification test, not production code; no input validation
concerns apply.

### Testing
This file *is* a test. Coverage of `DualTextureEffect`: `Texture`/`Texture2`/`DiffuseColor` are each
independently exercised, and case (d) is designed to prove simultaneous (not just one-slot)
multiplication. It does **not** cover `Alpha`, `FogEnabled`/`FogColor`/`FogStart`/`FogEnd`,
`VertexColorEnabled`, or the doubling factor's actual magnitude — all of which are (correctly) covered
by the other 7 files in this same batch instead, so there is no real coverage gap at the suite level,
only within this one file (as expected by its own narrower scope).

### Cross-file consistency
Consistent with `DualTextureEffect::FillGpuDrawParams()` (`DualTextureEffect.cpp:248-275`), which sets
`p.dualTexture = true` and forwards `texture0`/`texture1`/`diffuseColor`, and with
`EasyGLGraphicsBackend::SelectProgram()` (`EasyGLGraphicsBackend.cpp:3948-3958`), which routes any
`params.dualTexture` draw with `stride==20` (this test's `VertexPositionTexture`, not
`VertexPositionColorTexture`) to `EnsureDualTextured3DProgram()` — the exact shader this report
verified the math against.

## Detailed Findings

### F1 — File's own header comment omits the shader's `*2` doubling factor, and no sub-case can detect its absence

- Severity: LOW
- Confidence: HIGH (verified by hand-computing all 4 sub-cases with and without the `*2` factor —
  every one clamps to the same visible output either way)
- Category: maintainability / test-coverage documentation
- Location/symbol: file header comment lines 4-5; all four sub-tests in `Draw()` (lines 117-165)
- Evidence: real shader (`EasyGLGraphicsBackend.cpp:3050-3052`) is
  `base.rgb*=2.0; FragColor=base*texture(uTexture2,vUV)*uDiffuseColor;`, but this file's comment says
  simply `FragColor = texture(uTexture, vUV) * texture(uTexture2, vUV) * uDiffuseColor`. Every color
  used in this file (`kWhite`, `kRed`, `kBlue`, `kGreen`, `kYellow`, `kCyan`) has each RGB channel at
  exactly 0 or 1, so `channel*2` is either `0` (unaffected by doubling) or `≥1` (clamped to `1`
  regardless of whether the true multiplier is 1 or 2) — the doubling factor is provably invisible to
  every assertion in this file.
- Why it matters: a reader trusting this file's comment as "the" formula reference would draw an
  incorrect conclusion about `DualTextureEffect`'s actual pixel math; a maintainer regressing the `*2`
  factor (already once genuinely missing across all 3 backends per Task 383's own commit message)
  would see this test still pass. This is already known and mitigated by a dedicated sibling test
  (`easygl_dualtextureeffect_doubling_test.cpp`) which explicitly cites this file (as "Task 191") as
  one of the tests that "missed [doubling] entirely" — so the actual regression risk is closed at the
  suite level, but the stale comment in this specific file was never corrected to point a future
  reader at the doubling test for the complete formula.
- FNA/XNA comparison: FNA's `PSDualTexture` (`DualTextureEffect.fx:95-106`) is
  `color.rgb *= 2; color *= overlay * pin.Diffuse;` — the file's comment should say the same.
- Related files: `examples/easygl_dualtextureeffect_doubling_test.cpp` (already correctly documents
  and tests the `*2` factor with non-saturated colors).
- Suggested action (not implemented by this audit): update the header comment to include `* 2` and a
  one-line pointer to the doubling test, so a reader isn't misled about the actual formula.

### F2 — Missing explicit `<cstdlib>` include for `std::abs`

- Severity: LOW
- Confidence: HIGH
- Category: C++ correctness / portability
- Location/symbol: `colourMatch()` (line 37-42), calls `std::abs(int)`; includes are only `<cstdio>`,
  `<memory>` (lines 31-32)
- Evidence: every sibling file in this batch that uses the identical `std::abs` idiom
  (`easygl_dualtextureeffect_alpha_test.cpp`, `..._combined_test.cpp`, `..._doubling_test.cpp`,
  `..._fog_test.cpp`, `..._null_texture0_test.cpp`, `..._null_texture2_test.cpp`) includes
  `<cstdlib>` explicitly; this file does not.
- Why it matters: relies on `<cstdio>` or a transitively-included XNA header pulling in `<cstdlib>`
  on this toolchain/standard-library combination — works today (the CMake test is registered and
  presumably passing), but is not guaranteed portable to a different standard library.
- FNA/XNA comparison: N/A.
- Related files: none beyond the sibling test files cited above.
- Suggested action (not implemented by this audit): add `#include <cstdlib>`.

## Cross-File Observations

- This file, `easygl_dualtextureeffect_doubling_test.cpp`, `..._combined_test.cpp`, and
  `..._golden_test.cpp` collectively give `DualTextureEffect` real, cross-checked pixel coverage of
  its core multiply formula (including the doubling factor and a real multi-texel texture) — a
  stronger suite than a single file taken alone would suggest.

## Missing or Weak Tests

None beyond F1's documentation gap — the doubling factor itself is already covered by a sibling file,
so no *additional* test is needed here.

## Positive Findings

- Test case (d) (`yellow×cyan→green`) is a well-chosen discriminating case: it is the one combination
  in this file where a genuinely wrong "only one texture matters" implementation could not
  accidentally pass, since neither texture alone produces green.
- `Task 896 finding` comment about `RasterizerState::CullNone` is a good example of a self-documenting
  fix left in place for future readers.

## Final Assessment

A correctly-behaving, narrowly-scoped integration test whose only real issue is a stale/incomplete
formula comment that could mislead a future reader about `DualTextureEffect`'s actual math — already
functionally mitigated by a dedicated sibling test, but worth a one-line comment fix.
