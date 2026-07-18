# Audit: examples/sdlrenderer_samplerstate_perdraw_switch_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_samplerstate_perdraw_switch_test.cpp` (140 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 703
  (CMake registration: `cmake/Tests/SdlRendererTests.cmake:248`)
- XNA/FNA relevance: verifies per-`Begin()` `SamplerState` changes take live effect on SDL_Renderer, matching
  FNA's general contract that each `SpriteBatch::Begin()` call re-applies its own sampler state rather than
  inheriting a stale one.
- Related production code: `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`SdlSpriteBatchBackend::SetSamplerFilter`, `scaleMode` member; `Draw()` overloads' `SDL_SetTextureScaleMode`
  calls at lines 134, 154, 201).
- Git provenance: `ecb74811`/`84d8ae89` "verify(Task 703): per-draw SamplerState changes take effect on next
  Begin" — confirmed real commits.

## Purpose

Draws the same 2x1 (Red|Blue) texture stretched across the viewport three times in sequence, each via its own
`Begin()`/`Draw()`/`End()` cycle, alternating `SamplerState`: `PointClamp` → `LinearClamp` → `PointClamp`
again. Samples the exact Red/Blue texel boundary each time and asserts pure-color / blended-color / pure-color
respectively — specifically designed (per the header comment) to "rule out a one-directional/sticky bug where
switching TO Linear works but switching BACK to Point does not."

## Executive Verdict

**Healthy.** Independently confirmed the mechanism this test exercises: `SetSamplerFilter` (called once per
`SpriteBatch::Begin()`, per `SpriteBatch.cpp:119`) stores into a single `scaleMode` member on
`SdlSpriteBatchBackend`, and **every** `Draw()` overload re-applies it via `SDL_SetTextureScaleMode(nativeTex,
scaleMode)` immediately before rendering (confirmed at all three call sites: lines 134, 154, 201) — so the test
correctly targets a mechanism that is genuinely re-applied per draw, not cached at construction/first-use.

## Checklist Results

### API / XNA / FNA parity
`SamplerState::PointClamp` = `{Filter=Point, AddressU=AddressV=Clamp}`, `SamplerState::LinearClamp` =
`{Filter=Linear, ...}` (confirmed `SamplerState.cpp:8,10`) — the two presets used are real, existing XNA
presets, correctly referenced via `const_cast<SamplerState*>(&SamplerState::PointClamp)` (the `const_cast` is
necessary and harmless since `Begin()`'s `SamplerState*` parameter is non-const but the preset itself is never
mutated by any of these draws).

### Behavioral correctness
Traced `DrawWithSamplerAndSampleBoundary` (lines 66-84): stretches `Rectangle(0,0,2,1)` (the full 2-texel
source) across `Rectangle(0,0,W,H)` (`W=64,H=8` from the constructor), so the Red/Blue boundary sits at exactly
`x=W/2=32`; the readback region `(32,4,1,1)` samples exactly at that boundary — correctly targeting the one
pixel where Point vs. Linear filtering produces visibly different results.

- 1st draw (`PointClamp`): `SetSamplerFilter(1)` → default branch → `SDL_SCALEMODE_NEAREST` — boundary should
  read a pure color (no interpolation at a nearest-neighbor sample exactly on a texel edge, given SDL's
  standard rounding). `IsPure` (lines 42-47) checks `R>=230 & B<=25` (pure red) or `B>=230 & R<=25` (pure blue)
  — correctly excludes the blended mid-range.
- 2nd draw (`LinearClamp`): `SetSamplerFilter(0)` → `SDL_SCALEMODE_LINEAR` — boundary should read a genuine
  blend. `IsBlended` (lines 50-54) checks both `R` and `B` in `[90,165]`, correctly centered around the
  expected ~127/128 50/50 blend value.
- 3rd draw (`PointClamp` again): re-verifies the switch is not one-directional/sticky.

### Logic
Each `DrawWithSamplerAndSampleBoundary` call performs a full independent `Clear()`/`Begin()`/`Draw()`/`End()`
cycle (lines 73-78), so there is no risk of stale render-target contents leaking between the three samples —
each readback genuinely reflects only its own draw.

### Memory/resource lifetime
`sb_`, `tex_` created once in `Initialize()`, reused (not recreated) across all three draws — appropriate,
since the test is specifically about `Begin()`-time sampler-state re-application, not texture lifecycle.

### Testing
All three checks are genuinely discriminating: a hypothetical "Linear works but switching back to Point
doesn't" bug (the specific failure mode the file's header comment calls out) would pass check 2 but fail check
3, which this test would catch — a real, non-trivial regression-guard, not a redundant re-test of check 1.

## Detailed Findings

None. No defects found; the mechanism under test was independently confirmed correct via direct inspection of
all three `Draw()` overloads' `SDL_SetTextureScaleMode` call sites.

## Cross-File Observations

- Complements `sdlrenderer_samplerstate_filter_audit_test.cpp` (which proves the *mapping* from `TextureFilter`
  ordinal to `SDL_ScaleMode` is correct for all 9 values) by proving the *timing/liveness* of that mapping
  across repeated `Begin()` calls — together the two files cover orthogonal risks (wrong mapping vs. stale
  application) for the same underlying `scaleMode` mechanism.
- Only tests `PointClamp`/`LinearClamp` (both `AddressMode::Clamp`), so does not intersect with the
  `SetSamplerAddressMode` no-op gap independently found in this batch's audit of
  `sdlrenderer_samplerstate_default_test.cpp` — not applicable here since no address-mode divergence is being
  tested.

## Missing or Weak Tests

None specific to this file — the 3-cycle alternation design is a deliberately minimal but sufficient way to
rule out the specific sticky-bug failure mode it targets.

## Positive Findings

- The "switch back" (3rd draw) check is a genuinely valuable addition beyond a simpler 2-draw
  "does-Linear-work" test — it specifically targets a class of bug (one-directional state caching) that a
  naive 2-sample test would miss entirely.
- Full per-draw `Clear()`/`Begin()`/`End()` isolation between the three samples avoids any risk of
  cross-contamination between the three assertions.

## Final Assessment

A well-targeted, correctly-implemented regression test for a specific, plausible bug class (sticky/one-way
sampler-state application). No corrective action needed.
