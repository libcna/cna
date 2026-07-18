# Audit: examples/easygl_dualtextureeffect_null_texture0_test.cpp

## Metadata

- Source file: `examples/easygl_dualtextureeffect_null_texture0_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test, registered as CTest
  `EasyGL_DualTextureEffect_NullTexture0` (`cmake/Tests/EasyGLTests.cmake:1156-1158`,
  `cna_test_easygl_dualtextureeffect_null_texture0`)
- Related production code: `DualTextureEffect::FillGpuDrawParams()` (`DualTextureEffect.cpp:257`,
  `if (texture_) p.texture0 = &texture_->GetBackend();` — leaves `p.texture0` at its default/null
  when `texture_` is null), `EasyGLGraphicsBackend::BindDrawParams()` texture-unit-0 binding path
  (`EasyGLGraphicsBackend.cpp:4233-...`, falls back to `EnsureDefaultWhiteTexture()` /
  `default_white_texture_` when `params.texture0` is null).
- XNA/FNA relevance: FNA's real `DualTextureEffect` has no `TextureEnabled` flag and every shader
  variant unconditionally samples both `Texture` and `Texture2` — a null `Texture2D` in real XNA
  would throw a `NullReferenceException` inside `GraphicsDevice.Textures[0] = null` handling or bind a
  "no texture" GPU slot depending on the underlying D3D device state. CNA's documented,
  intentionally-chosen deviation (per this file's own comment, "CNA's established cross-effect
  convention") is to substitute an opaque white 1×1 texture rather than leave the sampler undefined —
  a reasonable, explicitly-labeled `NOXNA`-adjacent behavior choice for a null texture slot.
- Main related tests: `easygl_dualtextureeffect_null_texture2_test.cpp` (the symmetric slot-1 case,
  same batch).

## Purpose

`DualTextureNullTexture0Test` verifies that setting `DualTextureEffect::Texture = nullptr` correctly
falls back to an opaque-white substitute (not a stale previous binding, not black) by drawing a real,
distinctive `Texture` first to establish "previous GPU state," then switching to `Texture=null` and
confirming the read-back pixel matches the white-fallback hypothesis and explicitly does **not**
match the first draw's leftover color. Its own header comment states this is a
**"verify-only, zero bugs expected"** test — i.e. it was written to empirically confirm a
source-read conclusion (that all 3 backends already correctly implement this), not written in
response to a bug. Correct placement/registration.

## Executive Verdict

**Healthy.** The test's own 3-hypothesis design (`correct white-fallback` vs. `stale-previous-texture`
vs. `black-fallback`) gives it real discriminating power, its predicted numeric outcome matches the
actual `EnsureDualTextured3DProgram()` shader exactly, and its "verify-only" framing is honestly
representented (it does not overclaim to have found a bug it did not find).

## Checklist Results

### API / XNA / FNA parity
`setTextureProperty(nullptr)` — verified `DualTextureEffect::setTextureProperty(Texture2D*)`
(`DualTextureEffect.cpp:155`) accepts a raw pointer and stores it directly with no null check
(`texture_ = v;`), which is consistent with the header's documented, intentional design rather than an
oversight — a null `Texture2D*` is a supported, first-class state for this setter, exactly matching
what `FillGpuDrawParams()` (line 257) already guards for (`if (texture_) ...`).

### Behavioral correctness
Verified by hand: second draw has `Texture=null` (falls back to white, `(1,1,1,1)`), `Texture2=
kTex2=(80,40,120)`. Shader: `base=white=(1,1,1,1)`; `base.rgb*=2→(2,2,2,1)`;
`FragColor=base*texture2(80/255,40/255,120/255)*diffuse(default white)`:
`R=2*0.3137=0.6274→160`, `G=2*0.1569=0.3138→80`, `B=2*0.4706=0.9412→240` — exactly matches the test's
asserted `Color(160,80,240,255)` (line 146-148). The `!colourMatch(got, kDistinctivePrev)` second
assertion (line 149-151) is a genuinely useful, distinct check from the primary one — a
same-value-coincidence (e.g. if the fallback color happened to equal the stale color) is explicitly
guarded against by design, not merely implied.

### Logic
Two-draw structure (`dev.Clear(kBlack)` between draws, lines 126-143) correctly establishes and then
overwrites "previous GPU binding" state — this is a real, meaningful way to test for a stale
texture-unit-binding bug (e.g. a backend that only rebinds unit 0 conditionally on
`params.texture0 != nullptr` without an else-branch would leave the *previous* draw's texture bound).
Verified against the actual `BindDrawParams()` texture-0 binding code
(`EasyGLGraphicsBackend.cpp:4233` onward) that an `else` branch binding
`default_white_texture_` does exist for this exact case (consistent with the header's "Task 379"
claim).

### Memory/resource lifetime
`texPrev`/`tex2` are stack-local, non-owning-pointer-referenced; both `DualTextureEffect fx` instances
are scoped to their own `{ }` block and destroyed before the next one is constructed — correct, no
dangling-pointer risk.

### C++ correctness
Correctly includes `<cstdlib>` for `std::abs`.

### Performance / Thread safety / Portability
N/A — single-frame test, no platform-specific code.

### Architecture
Correct XNA-only public API usage (`Texture` set to `nullptr` via the public setter, no direct backend
symbol access).

### Maintainability
Comment (lines 1-25) is explicit about being a "verify-only" test and states its own predicted numeric
outcome before the code — verified this prediction is exactly right.

### Robustness
The test itself demonstrates a sound robustness property of the production code: a null texture slot
does not propagate to a crash, a stale binding, or a black/undefined pixel — a well-chosen input-
validation-adjacent behavioral check for a common "user forgot to set a texture" scenario.

### Testing
This file is itself a test for `DualTextureEffect.Texture`'s null-handling. Coverage is precise and
sufficient for its stated scope (slot 0 only; slot 1 is the sibling file's job).

### Cross-file consistency
Symmetric with `easygl_dualtextureeffect_null_texture2_test.cpp` (same batch) — verified both files
use the same 3-hypothesis structure, the same tolerance (`tol=20`), and the same
`(160,80,240)` expected fallback value for the analogous swapped-role scenario (texture/texture2
swapped, and the math is genuinely symmetric under that swap since the shader multiplies the two
sampled colors commutatively). Consistent with `DualTextureEffect::FillGpuDrawParams()`'s null guard
and `EasyGLGraphicsBackend::BindDrawParams()`'s white-texture fallback.

## Detailed Findings

No HIGH/MEDIUM/LOW findings — a clean, accurate, appropriately-scoped verification test.

## Cross-File Observations

- This file's header comment references "Vulkan" and "Bgfx" already correctly implementing the same
  white-fallback for slot 0 (unlike slot 1, where the sibling `_null_texture2_test.cpp`'s header
  documents a real Bgfx-only bug that *was* found and fixed) — worth confirming during the
  `examples-tests-vulkan`/`examples-tests-bgfx` shard audits that their own null-texture0 tests (if
  any) corroborate this "verify-only, no bug" claim independently.

## Missing or Weak Tests

None for its stated scope.

## Positive Findings

- The 3-hypothesis test design (correct/stale/black) is a strong, well-reasoned pattern reused
  consistently across this project's null-texture tests, giving each one real discriminating power
  rather than a single-hypothesis assertion that could pass for the wrong reason.
- Honest "verify-only, zero bugs expected" framing that turned out to be accurate on inspection —
  the test doesn't inflate its own significance.

## Final Assessment

A correctly-implemented, honestly-scoped verification test that empirically confirms a source-level
conclusion about `DualTextureEffect`'s null-`Texture` fallback behavior; no defect found in either the
test or the production code path it exercises.
