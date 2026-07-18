# Audit: examples/easygl_texture_address_mode_mirror_test.cpp

## Metadata

- Source file: `examples/easygl_texture_address_mode_mirror_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `SpriteBatch` `TextureAddressMode::Mirror` pixel
  test
- File type: C++ example/integration-test executable (`TextureAddressModeMirrorTest : Game`, `main()`)
- Related production code: same call chain as `easygl_texture_address_mode_test.cpp` —
  `SpriteBatch::Begin`/`SpriteBatch.cpp:119-121`, `EasyGLSpriteBatchBackend::SetSamplerFilter`/
  `SetSamplerAddressMode`/`FlushBatch` (`EasyGLGraphicsBackend.cpp:1065-1145`),
  `EasyGLGraphicsBackend::ApplySamplerState` (lines 2055-2139, specifically the `Mirror→MirroredRepeat` branch)
- XNA/FNA relevance: `TextureAddressMode.Mirror`, `SamplerState` (custom instance, since FNA has no
  `PointMirror` static preset). Judged against `FNA/src/Graphics/States/SamplerState.cs` (confirms only
  `AnisotropicClamp/Wrap`, `LinearClamp/Wrap`, `PointClamp/Wrap` exist as presets — no `*Mirror` preset — so
  this file's custom-`SamplerState` approach is the only correct way to test Mirror against a static-preset
  API surface).
- Main related tests: this file (Task 737); direct sibling of `easygl_texture_address_mode_test.cpp` (Task
  269, Wrap/Clamp via the same `SpriteBatch` code path) and
  `easygl_texture_address_mode_mirror_effect_test.cpp` (Task 296, Mirror via the independent
  `DrawUserPrimitives`+`Effect` code path).

## Purpose

Closes a real coverage gap left by Task 269's original `SpriteBatch` sampler test: that test independently
verified `Wrap` and `Clamp` are honored by `EasyGL`'s `SpriteBatch` sampler-state fix, but never `Mirror`, even
though `Mirror` goes through the exact same `SetSamplerFilter`/`SetSamplerAddressMode` code path. Draws a 2×1
(Red|Blue) texture with `sourceRectangle` twice the texture width (U spans `[0,2]`) and samples at `U=1.6`, a
point deliberately chosen so Mirror's answer differs from *both* Wrap's and Clamp's (unlike `U=1.25`, where
Mirror and Clamp coincidentally agree). Placement matches the shard convention.

## Executive Verdict

**Healthy** — the `U=1.6` sample point was independently re-derived and confirmed to be a genuinely
discriminating choice (Mirror gives Red while both Wrap and Clamp give Blue at this exact point, unlike the
`U=1.25` point used by the base test where Clamp and Mirror coincidentally agree), and this file goes further
than its base-mode sibling by actively asserting all three modes' results in a single run rather than
asserting Mirror alone and merely printing the others for context — a stronger self-verifying design.

## Checklist Results

### API / XNA / FNA parity
Confirmed via `FNA/src/Graphics/States/SamplerState.cs` that no `PointMirror` (or any `*Mirror`) static preset
exists in real XNA/FNA — only `Clamp`/`Wrap` combinations. The test's approach of constructing a local
`SamplerState pointMirror` with `setFilterProperty(TextureFilter::Point)`,
`setAddressUProperty(TextureAddressMode::Mirror)`, `setAddressVProperty(TextureAddressMode::Mirror)` (lines
100-103) is therefore the only correct way to exercise `Mirror` through the public/static-preset-shaped API
surface, not a workaround for a missing CNA feature.

### Behavioral correctness — independently re-derived all three expected colors
Traced the identical `SpriteBatch::Begin → SetSamplerFilter/SetSamplerAddressMode → FlushBatch →
ApplySamplerState` call chain already verified in the base `_test.cpp` sibling's report — confirmed `Mirror`
(enum value `2`) maps to `TextureWrapMode::MirroredRepeat` at `EasyGLGraphicsBackend.cpp:2131`, the specific
GL wrap mode this test is a regression guard for.

Re-derived the mirror-mapping math for `u=1.6` from GL's own `GL_MIRRORED_REPEAT` definition (period-2
triangle wave): for `u∈[1,2)`, the mirrored coordinate is `2-u`. At `u=1.6`: `2-1.6=0.4`, which falls in the
texel-0 half `[0,0.5)` under point sampling → **Red**. This matches the test's `mirrorPass` check
(`mirrorPixel.R==255 && mirrorPixel.B==0`, line 109).

For **Wrap** at the same `u=1.6`: `fract(1.6)=0.6`, falling in `[0.5,1.0)` → texel 1 → **Blue** — matches
`wrapIsBlue` (line 110). For **Clamp**: raw `u=1.6` clamps to `1.0`, which (per the base test's independently
re-derived texel-center analysis) resolves to texel 1 → **Blue** — matches `clampIsBlue` (line 111).

Cross-checked the test's own comment claim that `U=1.25` (the base test's sample point) would have Mirror and
Clamp coincidentally agree: at `u=1.25`, Mirror gives `2-1.25=0.75` (texel 1, Blue) and Clamp also gives texel
1 (Blue) — confirmed these do coincide at `1.25`, meaning `U=1.25` genuinely would **not** discriminate Mirror
from Clamp, validating this file's choice of `U=1.6` instead as the actually-necessary discriminating point
rather than an arbitrary alternative.

### Logic
`SampleAtUOnePointSix(SamplerState*)` (lines 75-91) is called three times — once each for `pointMirror`,
`SamplerState::PointWrap`, `SamplerState::PointClamp` (lines 105-107) — with a fresh `Clear` before each draw
(line 82), correctly isolating each of the three sub-results from the others.

### Memory/resource lifetime
`const_cast<SamplerState*>(&SamplerState::PointWrap/PointClamp)` (lines 106-107) — same API-ergonomics wart
already noted in the base sibling's report (read-only usage confirmed safe via the same `SpriteBatch::Begin`
value-copy semantics); not re-scored as a new finding here.

### C++ correctness
The three boolean checks (`mirrorPass`, `wrapIsBlue`, `clampIsBlue`, lines 109-111) are combined with a
logical AND for the final `result_` (line 124) — meaning this test fails not only if Mirror is wrong, but
also if the "control" Wrap/Clamp results are unexpectedly *not* Blue at this sample point. This is a stronger
design than a Mirror-only assertion: it would catch a scenario where some unrelated regression made *all three*
modes read Red at `U=1.6` (which would otherwise risk a false "Mirror pass" if only `mirrorPass` were checked
in isolation, since the test's actual claim is that Mirror is *distinguishable* from the other two at this
point, not merely that it happens to equal Red).

### Performance
N/A — three single-frame draws, no hot path.

### Robustness
No invalid-input path exercised; correct scope for a positive-path sampler-state test.

### Testing
This file is itself a test; see Missing or Weak Tests.

## Detailed Findings

No HIGH/CRITICAL findings.

## Cross-File Observations

- Shares the V-axis-never-exercised gap (see `easygl_texture_address_mode_test.cpp.audit.md` F1) — this
  texture is also only 1 texel tall with a `sourceRectangle` height matching the texture, so `AddressV=Mirror`
  is set but never actually produces a different result from `AddressV=Wrap`/`Clamp` in this test. Not
  re-scored as a new finding; referenced from the base test's report where it is recorded in full.
- Directly extends `easygl_texture_address_mode_test.cpp` (Task 269) by closing its "Mirror untested" gap for
  the same `SpriteBatch` code path — confirmed via this file's own header claim and independently verified the
  claim is accurate (the base test asserts only `wrapPass`/`clampPass`, never anything Mirror-related).
- Compared to `easygl_texture_address_mode_mirror_effect_test.cpp` (Task 296, the `DrawUserPrimitives`+`Effect`
  Mirror test): that file samples at `U=1.6` too and asserts the identical three-way Red/Blue/Blue split via a
  different code path (`GraphicsDevice::SamplerStates[]` → `applySamplerStatesToBackend()` →
  `ApplySamplerState`, rather than `SpriteBatch`'s `SetSamplerFilter`/`SetSamplerAddressMode` →
  `FlushBatch`'s own `ApplySamplerState` call) — genuinely independent coverage of the same GL-level mapping
  through two different call paths, not duplicated testing.

## Missing or Weak Tests

- V-axis addressing is never exercised (shared gap, see above).
- No case verifies the mirror pattern's *second* reflection (e.g., `u∈[2,3)` reflecting back to `[0,1)`
  matching the *original*, non-mirrored orientation) — this test only samples within the first mirrored
  repeat (`u∈[1,2)`); a `u>2` sample would additionally confirm the full periodic mirror pattern rather than
  just its first reflection.

## Positive Findings

- The choice of `U=1.6` over the base test's `U=1.25` is independently verified to be the *necessary* choice,
  not an arbitrary one — `U=1.25` genuinely would not discriminate Mirror from Clamp, confirmed by direct
  recomputation.
- Asserting all three modes' results together (`mirrorPass && wrapIsBlue && clampIsBlue`) rather than Mirror
  in isolation is a stronger, more genuinely self-verifying test design than a single-assertion check would be.
- Diagnostic printfs for the two "context" modes (lines 116-119) clearly label them as "not asserted here"
  while the actual `result_` computation (line 124) *does* fold them into the pass/fail decision — the
  printf wording is a minor under-statement of what the code actually checks, but does not affect correctness
  (the values are checked, just not solely relied upon for the headline PASS/FAIL label per row).

## Final Assessment

A well-targeted, independently-verified closure of a real gap in the original `SpriteBatch` sampler-state
regression test: its sample point was proven (not assumed) to be the specific point that discriminates Mirror
from both Wrap and Clamp, and its three-way assertion is a stronger design than a single-mode check. The only
gap is the V-axis blind spot shared across this entire test family.
