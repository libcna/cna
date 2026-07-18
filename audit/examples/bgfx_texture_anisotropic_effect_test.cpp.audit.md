# Audit: examples/bgfx_texture_anisotropic_effect_test.cpp

## Metadata

- Source file: `examples/bgfx_texture_anisotropic_effect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — Task 749, `SamplerState.MaxAnisotropy` cap-query/fallback
  on a 3D stock effect (`DualTextureEffect`), Bgfx backend
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_texture_anisotropic_effect …)` /
  `cna_register_backend_test(NAME Bgfx_TextureAnisotropicEffect …)`, `cmake/Tests/BgfxTests.cmake:96-99`).
- XNA/FNA relevance: direct — `SamplerState.MaxAnisotropy`, `TextureFilter::Anisotropic`,
  `DualTextureEffect`.
- FNA reference: `Graphics/States/SamplerState.cs` (`MaxAnisotropy` property, default `4`),
  `Graphics/Effect/StockEffects/HLSL/DualTextureEffect.fx`.
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`ApplySamplerState`, lines 1890-1939, specifically case `2` at lines 1909-1911 and the
  intentionally-unused third parameter at line 1892); `RasterizerState::CullNone` winding fix
  referenced at line 121.

## Purpose

Verifies that requesting an absurdly over-cap `SamplerState.MaxAnisotropy` (`9999`, "far beyond any
real GPU's limit" per the file's own comment) on a real `DualTextureEffect` 3D draw does not crash or
throw. This is explicitly the test's *entire* pass/fail criterion (`result_ = threw ? 1 : 0`,
line 155) — the sampled pixel's actual color is only ever printed as an `[INFO]`/diagnostic
classification (blended vs. solid-black), never asserted on.

## Executive Verdict

**Healthy** — the file's own header comment makes a specific, falsifiable claim about current Bgfx
backend behavior ("the requested `MaxAnisotropy` level itself is unused in
`BgfxGraphicsBackend::ApplySamplerState`") which this audit independently confirmed true by reading
the actual switch statement, rather than merely trusting the comment (see Checklist below). The test's
narrow crash-safety assertion is implemented correctly and unambiguously.

## Checklist Results

### API / XNA / FNA parity
`SamplerState.MaxAnisotropy` (int property, default `4` in FNA), `TextureFilter::Anisotropic`, and
`DualTextureEffect` are all real XNA 4.0 API surface; this test's use of
`aniso.setMaxAnisotropyProperty(9999)` (line 99) is a valid API call regardless of backend support
depth — XNA itself does not specify client-side validation/clamping of this value (FNA passes it
through to the underlying driver via `FNA3D`/`PipelineCache`, letting the GPU driver clamp it), so
CNA accepting an out-of-range value without throwing is consistent with XNA's own permissive contract,
not a deviation.

### Behavioral correctness
Independently confirmed the file's central claim by reading `ApplySamplerState` directly
(`BgfxGraphicsBackend.cpp` lines 1890-1892, 1909-1911): the function signature is
`ApplySamplerState(int slot, int filter, int addressU, int addressV, int /*maxAnisotropy*/)` — the
`maxAnisotropy` parameter is explicitly named-out (unused) in the signature itself, and case `2`
(`Anisotropic`) only ever sets `BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC`, with no
numeric level ever threaded through to bgfx. This is **not stale documentation** (unlike the sibling
`easygl_texture_anisotropic_effect_test.cpp` audited previously in this project, whose header comment
was found to describe a since-fixed EasyGL limitation) — it is a live, currently-accurate description
of Bgfx's actual current behavior.

### Logic
`try`/`catch(const std::exception&)` (lines 93-131) correctly wraps the entire draw sequence
(`SamplerState` construction, `DualTextureEffect` setup/`Apply()`, `DrawUserPrimitives`,
`GetBackBufferData`); `threw`/`result_` are set consistently on both paths, and `Exit()` is called
unconditionally after the `if(!threw){...}` block (line 156), so both branches always terminate the
test.

### Robustness
The `isBlended`/`isBlack` classification (lines 133-137) is explicitly advisory only
(`[INFO]`, never affecting `result_`) — a deliberate, well-reasoned design given that the *actual*
anisotropic-quality outcome is legitimately driver-dependent (as the header comment states) and would
be fragile to assert on precisely. This mirrors the equivalent EasyGL test's identical design choice.

### C++ correctness
`RasterizerState::CullNone` (line 121) is applied with an inline comment attributing the need to
"Task 896 finding" (CCW/back-facing winding under CNA's real default `RasterizerState`) — independently
plausible given the vertex winding of the quad at lines 111-118 (`(-1,1)→(-1,-1)→(1,-1)`, then
`(-1,1)→(1,-1)→(1,1)`, both CCW in standard screen-space Y-down NDC), consistent with the same
attribution used identically across every sibling test in this batch that draws a full-screen quad via
`DrawUserPrimitives`.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Test verifies "doesn't crash," not "the requested cap is actually clamped/queried against a real device limit"

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: entire file; `result_` is set solely from `threw` (line 155)
- Evidence: no assertion anywhere in this file inspects what numeric anisotropy level (if any)
  actually reached the GPU sampler; the file's own header comment explicitly frames this as
  intentional scope ("this test verifies the 'caps and fallback' half literally... A true visual
  anisotropic-quality pixel test is inherently driver-dependent and fragile to assert precisely").
- Why it matters: this is a real, honestly-disclosed scope limitation, not a hidden gap — flagged here
  per the checklist's testing-coverage section, matching this audit's identical finding on the sibling
  EasyGL anisotropic test (`easygl_texture_anisotropic_effect_test.cpp.audit.md`, F2), where an
  equivalent value-level follow-up test (`easygl_anisotropic_gl_state_test.cpp`) already exists. No
  equivalent Bgfx-specific value-level test was found in this shard — reasonable, since Bgfx's own
  production code (confirmed above) does not thread the numeric value anywhere a test could observe it
  short of reading bgfx's internal renderer state directly.
- Related files: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`ApplySamplerState`).
- Suggested future action: none required from this audit — the scope is honestly and correctly
  documented; a deeper value-level test would first require Bgfx's backend to actually consume the
  `maxAnisotropy` parameter, which is a production-code task, not a test-authoring gap.

## Cross-File Observations

- Unlike its EasyGL sibling (`easygl_texture_anisotropic_effect_test.cpp`, audited separately and found
  to carry ~two-thirds stale header commentary describing a pre-Task-918/924 backend state), this
  file's header comment was independently checked against the current Bgfx production source and found
  fully accurate — no staleness found here.
- Shares the `ApplySamplerState` production function with `bgfx_texture_address_mode_mirror_test.cpp`
  and `bgfx_texturefilter_split_minmag_test.cpp` (both in this same batch) — this file exercises the
  `case 2` (Anisotropic) branch specifically, while the others exercise the address-mode and
  split-filter branches of the same switch/if-chain.
- The `RasterizerState::CullNone` Task-896 attribution is used identically (same comment wording
  pattern) across every quad-drawing test in this batch (`bgfx_texture_filter_point_vs_linear_test.cpp`,
  `bgfx_texture_mip_filter_effect_test.cpp`, `bgfx_texturefilter_split_minmag_test.cpp`), consistent
  cross-file attribution rather than a one-off comment.

## Missing or Weak Tests

See F1 — a value-level (rather than crash-only) anisotropic test is not present for Bgfx, but this
reflects a genuine production-code gap (the value is never threaded through) rather than an oversight
in this specific test file's own design.

## Positive Findings

- The file's central factual claim about current Bgfx backend behavior (`MaxAnisotropy` unused) was
  independently verified against the live source, not merely trusted — and found accurate, unlike a
  sibling EasyGL test in this project that made a similar-looking but since-falsified claim.
- Exception-safety logic (`try`/`catch`, unconditional `Exit()`) is correct and unambiguous on every
  path.
- The advisory-only `[INFO]` classification of the sampled pixel is a reasonable, driver-agnostic
  design choice that avoids a flaky assertion on an inherently environment-dependent outcome.

## Final Assessment

A narrowly-scoped, honestly-documented crash-safety test whose central claim about Bgfx's current
(non-)handling of `MaxAnisotropy` was independently confirmed accurate by reading the production
source directly. No correctness issues found; the one noted gap (F1) is an already-acknowledged,
reasonable scope boundary rather than a defect.
