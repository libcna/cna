# Audit: examples/easygl_texture2d_anisotropic_singlelevel_test.cpp

## Metadata

- Source file: `examples/easygl_texture2d_anisotropic_singlelevel_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration/pixel-readback regression test
- File type: C++ executable test (`Game` subclass, no gtest), 136 lines
- XNA/FNA relevance: exercises `Texture2D::CreateFromPixels`, `SamplerState`/`TextureFilter::Anisotropic`,
  `BasicEffect` texturing — all real XNA 4.0 API surface
- FNA reference: N/A directly (this is a CNA/EasyGL backend GPU-state bug, not an XNA API-shape question) —
  the `TextureFilter` enum values themselves (`Anisotropic` etc.) were spot-checked against
  `Microsoft::Xna::Framework::Graphics::TextureFilter.hpp` and match FNA's `TextureFilter.cs` member set
- Production code under test: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:432-476`
  (`EasyGLTextureBackend` constructor/`recreate_gl_resource`), `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp:811-828`
  (`CreateFromPixels`), `include/CNA/Internal/Graphics/ImageData.hpp:16` (`mipLevels` default)
- Direct sibling: `examples/easygl_texture_anisotropic_effect_test.cpp` (Task 299) — this file's own header
  comment states it is that test's "direct sibling," which found the underlying bug but deliberately did not fail
  on it

## Purpose

Task 924 (split from Task 867): asserts that an ordinary, single-mip-level `Texture2D` (created via
`Texture2D::CreateFromPixels`, "the overwhelmingly common case for real game textures" per the header comment)
renders correctly — i.e. produces a real magnified color blend, not solid black — when sampled with
`TextureFilter::Anisotropic`, a mipmap-requiring filter mode. The root cause it targets: EasyGL never set
`GL_TEXTURE_MAX_LEVEL`, so OpenGL's own default (`1000`) made every texture appear as an incomplete mipmap chain
under any `*_MIPMAP_*` minification filter unless levels 0 through 1000 (or to 1×1) were populated — true even for
a genuinely single-level texture.

## Executive Verdict

**Healthy — this is a real, verified regression test for an actual, previously-shipped bug and its fix.**
Independently confirmed both halves of the claim: (1) the production fix
(`EasyGLTextureBackend`'s constructor clamping `GL_TEXTURE_MAX_LEVEL` to `mipLevels_-1`) is present and correctly
reapplied on context-loss recovery, and (2) `Texture2D::CreateFromPixels` (the constructor this test deliberately
uses, matching its own stated "common case" framing) genuinely produces a single-level `ImageData` (`mipLevels`
defaults to `1` and is never overridden by `CreateFromPixels`), so this test exercises exactly the code path the
bug lived in.

## Checklist Results

### API / XNA / FNA parity
`TextureFilter::Anisotropic`, `TextureAddressMode::Clamp`, `SurfaceFormat` (implicit `Color` via
`CreateFromPixels`), `RasterizerState::CullNone`, `BasicEffect` texture properties (`setTextureProperty`,
`setTextureEnabledProperty`, `setWorldProperty`/`setViewProperty`/`setProjectionProperty`, `Apply()`) all match
their respective XNA-facing header declarations — no parity issues found.

### Behavioral correctness
Traced the actual fix end-to-end:
1. `Texture2D::CreateFromPixels` (`Texture2D.cpp:811-828`) builds an `ImageData` and **never sets
   `data.mipLevels`**, leaving it at `ImageData.hpp:16`'s default of `1` (explicitly commented "Task 924: real mip
   level count backends should allocate for").
2. `EasyGLTextureBackend`'s constructor (`EasyGLGraphicsBackend.cpp:432-446`) computes
   `mipLevels_(data.mipLevels > 0 ? data.mipLevels : 1)` — for this test's texture, `mipLevels_ = 1` — then calls
   `texture.set_parameter(..., TextureParameter::MaxLevel, mipLevels_ - 1)`, i.e. `MaxLevel = 0`. The constructor's
   own comment (line 439-442) explicitly documents this exact scenario: "otherwise a mipmap-requiring
   TextureFilter (e.g. Anisotropic) treats this as an incomplete mipmap chain ... and renders solid black, even
   for an ordinary single-level ... texture."
3. `recreate_gl_resource()` (context-loss recovery path, `EasyGLGraphicsBackend.cpp:458-476`) reapplies the same
   `MaxLevel` clamp — confirmed this fix is not a one-time constructor artifact that would regress after a
   simulated context loss.
This test's own pass/fail condition (`isBlack = R<=10 && G<=10 && B<=10` at the mid-screen texel boundary,
lines 105-110) directly targets the documented failure symptom ("renders solid black"), and the fix targets
exactly the code path this test's fixture (`CreateFromPixels`, single-level) exercises.

### Logic
The 2×1 texture (`Red | Green`) stretched across a full-screen quad and sampled at the exact `u=0.5` texel
boundary (`W/2, H/2`) is a reasonable choice for "is anything non-black actually being sampled here" — it does not
attempt to verify the *specific* anisotropic blend value (which is backend/driver-dependent and not meaningfully
assertable), only that the sample is not the specific "incomplete mipmap" black-fallback symptom. This matches the
test's own narrow, correctly-scoped claim (see header comment: "the same TextureFilter::Anisotropic + single-level
Texture2D combination must now produce a real magnified blend ... not solid black").

### Robustness / Cross-file consistency
The `RasterizerState::CullNone` requirement (line 100-102, "Task 896 finding: this quad's winding is CCW/back-facing
under CNA's real default RasterizerState") was checked against `RasterizerState.cpp:11`, which confirms the default
constructor's `cullMode_` is `CullMode::CullCounterClockwiseFace` — matching FNA/XNA's own documented default. This
is consistent with the test's own comment and is a recurring, previously-documented pattern in this shard (the sibling
`easygl_texture_anisotropic_effect_test.cpp` and other EasyGL quad-based tests use the identical workaround) rather
than something newly introduced or unexplained here.

### Testing
Confirmed the "deliberately did not fail on this bug" framing of the sibling test
(`easygl_texture_anisotropic_effect_test.cpp`, Task 299): that file's own header comment states its scope is "does
an extreme MaxAnisotropy value crash," not the solid-black rendering symptom, and its body explicitly logs an
`[INFO]` note about the black result without failing the test on it. This file is the correctly-scoped follow-up
that actually asserts the rendering-correctness claim the sibling test intentionally deferred — a legitimate,
well-organized split across two focused tests rather than duplication.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — this file is a correct, verified regression test for a real, previously-fixed
bug.

### F1 — Test only proves "not solid black," not that the sampled value is a genuine, correctly-weighted blend of the two source texels

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `Texture2DAnisotropicSingleLevelTest::Draw` (lines 105-123)
- Evidence: the pass condition only checks `R,G,B <= 10` for FAIL (solid black); any other non-black value
  (including, hypothetically, a wrongly-blended or even a single wrong-but-nonzero color) would report PASS.
- Why it matters: this is an intentional, correctly-scoped narrowing (per the header comment's own stated goal —
  proving the specific "incomplete mipmap → solid black" regression is fixed, not asserting exact anisotropic
  filter output, which is legitimately driver/GPU-dependent and not a meaningful cross-platform assertion target)
  rather than an oversight — recorded for completeness since a test with a broader name could be read as claiming
  more.
- FNA/XNA comparison: N/A — anisotropic filtering's exact sampled value is not part of the XNA behavioral contract
  (XNA/FNA delegate the actual filtering math to the underlying graphics API/driver).
- Suggested action: none needed; the test's narrow claim is appropriate and clearly documented in its own header.

## Cross-File Observations

- This file, its direct sibling `easygl_texture_anisotropic_effect_test.cpp` (Task 299), and the `RasterizerState::CullNone`
  workaround they share are a good example of well-organized, cross-referenced test provenance in this shard — each
  file's header comment accurately describes what the other one does and does not cover, and both claims were
  independently verified here rather than taken at face value.
- The `GL_TEXTURE_MAX_LEVEL` fix this test guards (`EasyGLGraphicsBackend.cpp:439-446`) is EasyGL-backend-specific;
  worth checking during any Vulkan/Bgfx/D3D backend audit whether an equivalent mip-level-count-vs-sampler-filter
  mismatch exists there too (out of scope for this file's own audit).

## Missing or Weak Tests

- No test in this file (or, as far as this batch reveals) verifies the inverse case — a genuinely multi-level
  (`mipMap=true`) `Texture2D` sampled with `Anisotropic` still renders correctly after this same `MaxLevel` clamp
  is applied with a `mipLevels_ > 1` value (i.e. that the fix doesn't only special-case the single-level case). This
  would strengthen confidence that the clamp is a correct general formula (`mipLevels_-1`) rather than a
  single-level-specific patch.

## Positive Findings

- A genuine, verified regression test: independently traced the full fix from `ImageData.mipLevels`'s default,
  through `Texture2D::CreateFromPixels`'s omission of that field, to `EasyGLTextureBackend`'s `GL_TEXTURE_MAX_LEVEL`
  clamp and its reapplication on context-loss recovery — all three pieces line up exactly with what this test
  exercises and asserts.
- Correctly and narrowly scoped to the specific historical failure symptom (solid black), with an accurate,
  cross-referenced relationship to its sibling test.
- Uses a real end-to-end render + `GetBackBufferData` readback, not a mock.

## Final Assessment

A well-targeted, independently-verified regression test for a real, previously-shipped EasyGL bug
(`GL_TEXTURE_MAX_LEVEL` defaulting to `1000` and starving single-level textures under mipmap-requiring filters).
The fix it guards was traced end-to-end and confirmed correct and consistently reapplied across the
context-recovery path; the test's own claim is appropriately narrow and clearly documented.
