# Audit: examples/easygl_alphatest_null_texture_test.cpp

## Metadata

- Source file: `examples/easygl_alphatest_null_texture_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `AlphaTestEffect` null-texture × EasyGL backend pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_alphatest_null_texture …)` /
  `cna_register_backend_test(NAME EasyGL_AlphaTest_NullTexture …)`,
  `cmake/Tests/EasyGLTests.cmake:1138-1140`).
- XNA/FNA relevance: direct, but tests an intentional CNA-specific deviation from FNA's undefined
  behavior (see Purpose) — `Texture=nullptr` is not a documented XNA scenario.
- FNA reference: `AlphaTestEffect.cs`/`AlphaTestEffect.fx` — confirmed no `TextureEnabled` flag
  exists on this effect (unlike `BasicEffect`), and FNA leaves an unbound-texture sample as
  graphics-API-dependent UB.
- Related production code: `AlphaTestEffect.cpp::FillGpuDrawParams()` (`p.textureEnabled =
  (texture_ != nullptr)`, line 317), `EasyGLGraphicsBackend.cpp::EnsureDefaultWhiteTexture()`
  (line 3892) and its unconditional `if (params.texture0) ... else default_white_texture_.bind(...)`
  fallback (verified at lines 4238-4241, the unit-0/primary-texture binding site).

## Purpose

Verifies CNA's own established cross-backend convention — falling back to an internal 1×1 opaque
white texture when no texture is bound, rather than leaving a stale/undefined sampler state — holds
for `AlphaTestEffect` on EasyGL specifically. The file's header comment (lines 16-26) states this
exact test *found no bug on EasyGL* (EasyGL already had the fallback); the real bug it references
was found on the Bgfx sibling test, not this file.

## Executive Verdict

**Healthy.** Verified the white-texture fallback is unconditional in EasyGL's actual unit-0 binding
code (`if (params.texture0) BindGL(); else default_white_texture_.bind(...)`, no `textureEnabled`
gate at all on the *fallback* branch — see Detailed Findings F1) and that the test's two expected
pixel values (`(120,40,40)` with-texture, `(153,102,204)` null-texture) are both exact arithmetic
matches for `TextureColor × DiffuseColor`.

## Checklist Results

### API / XNA / FNA parity
`setTextureProperty(Texture2D*)` (line 114) accepting `nullptr` matches the header's declared
signature (`include/.../AlphaTestEffect.hpp` line 181, `Texture2D* value`) — no FNA equivalent
exists for "clear the texture" since C# `Texture = null` is just a normal reference assignment;
CNA's raw-pointer property correctly supports the same semantic via `nullptr`.

### Behavioral correctness
Re-derived both expected colors directly:
- With texture: `TextureColor(200,100,50)/255 × DiffuseColor(0.6,0.4,0.8)` → `200×0.6=120`,
  `100×0.4=40`, `50×0.8=40` → `(120,40,40)`, exact integers, matching `kExpectedWithTexture`
  (line 68).
- Null texture: white fallback `(255,255,255)/255=1` × same diffuse → `(0.6,0.4,0.8)×255 =
  (153,102,204)`, matching `kExpectedNullTexture` (line 70) exactly (`0.6×255=153`, `0.4×255=102`,
  `0.8×255=204`, all exact).
- Confirmed against the live `AlphaTestEffect.cpp::FillGpuDrawParams()` diffuse formula
  (`diffuseColor_.X * alpha_`, lines 324-327; `alpha_` defaults to `1.0f` here since this test never
  calls `setAlphaProperty`) and EasyGL's shared fragment formula
  (`FragColor=texture(uTexture,vUV)*vc*uDiffuseColor`, e.g. line 2748) — with
  `VertexColorEnabled=false` (default), `vc` collapses to `(1,1,1,1)`, leaving exactly
  `texture × diffuse`, matching both derivations above.

Sub-test 3 (line 161-163) — asserting the null-texture pixel does *not* match the previous draw's
real-texture pixel — is a deliberately strong check: it rules out the specific stale-state-leak
failure mode the file's header comment (lines 16-26) says was found on Bgfx (`if (params.texture0
&& ...)` with no `else` branch at all, silently leaving the *previous* draw's bound texture).
Verified this exact failure mode is structurally impossible in the current EasyGL code, since the
`else` branch unconditionally binds `default_white_texture_` (see F1) — there is no code path where
omitting a texture leaves the prior binding in place.

### Logic
`RasterizerState::CullNone` per draw (line 126, "Task 896 finding"), consistent with the rest of
this shard. The 20-iteration blank-frame retry loop (lines 119-131) is present here (unlike the two
older sibling files flagged in their own reports) — correct, no finding against this file for that.

### C++ correctness
`renderWith(GraphicsDevice&, Texture2D* tex, ...)` (line 111) correctly accepts a raw possibly-null
pointer and forwards it verbatim to `setTextureProperty` (line 114) without dereferencing —
no null-deref risk.

### Testing
Genuinely validates the fallback *and* the absence of state leakage (3 distinct assertions across 2
renders), rather than merely confirming the effect doesn't crash with a null texture — satisfies
the anti-boilerplate bar well.

## Detailed Findings

### F1 — White-texture fallback confirmed unconditional in EasyGL's unit-0 binding path

- Severity: INFO
- Confidence: HIGH
- Category: correctness (verification note)
- Location/symbol: `EasyGLGraphicsBackend.cpp` lines 4234-4242 (`if (p.loc_texture >= 0) { …
  EnsureDefaultWhiteTexture(); … if (params.texture0) params.texture0->BindGL(); else
  default_white_texture_.bind(...); }`)
- Evidence: traced the exact binding site this test exercises; the `else` branch always executes
  when `params.texture0` (populated only if `AlphaTestEffect::texture_ != nullptr`, per
  `FillGpuDrawParams()` line 321-322) is null — no conditional gate on `textureEnabled` guards the
  fallback itself, so there is no path that leaves a stale binding.
- Why it matters: directly substantiates the test's own claim ("This EasyGL test itself found no
  bug — EasyGL already had the correct fallback") with a specific line citation, closing the loop
  for this audit.

## Cross-File Observations

- The file's header comment (lines 16-33) documents a real, already-fixed Bgfx defect (missing
  `else` branch across 7 texture-binding call sites) and a deliberately-deferred, narrower
  `DualTextureEffect` second-texture-slot analog on Bgfx — both are Bgfx-shard concerns, correctly
  out of scope for this EasyGL file's own audit, but worth the Bgfx shard auditor cross-checking
  this narrative independently rather than taking the comment at face value.

## Missing or Weak Tests

None specific to this file — the with-texture/without-texture/no-stale-leak triad is a solid,
complete characterization of the one behavior this file is named for.

## Positive Findings

- The choice of a non-white, non-black `DiffuseColor=(0.6,0.4,0.8)` (line 65) to make the three
  hypotheses (correct white fallback / stale previous texture / black fallback) numerically
  distinct is good test design, explicitly called out and verified correct in this audit.
- Sub-test 3's explicit "not equal to the previous draw's color" assertion is a meaningfully
  different check from sub-test 2's "equals the expected fallback color" — it specifically targets
  the stale-state-leak failure mode by name, not just the end-state color.

## Final Assessment

A well-targeted, correctly-verified test; its central claim (EasyGL's fallback is unconditional and
therefore leak-free) is confirmed directly against the current backend source, not just inferred
from the test passing.
