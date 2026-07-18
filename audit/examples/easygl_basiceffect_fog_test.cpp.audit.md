# Audit: examples/easygl_basiceffect_fog_test.cpp

## Metadata

- Source file: `examples/easygl_basiceffect_fog_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `BasicEffect` fog pixel integration test
- File type: C++ example/integration-test executable (`FogTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::BasicEffect::FillGpuDrawParams()`
  (`BasicEffect.cpp:135-140`, fog fields), `CNA::Internal::Backends::EasyGL::EasyGLGraphicsBackend`'s fog GLSL
  (`vFogFactor` computation and `mix(uFogColor,FragColor.rgb,vFogFactor)`, multiple shader-string sites e.g.
  `EasyGLGraphicsBackend.cpp:2597-2627`)
- XNA/FNA relevance: `BasicEffect.FogEnabled`/`.FogColor`/`.FogStart`/`.FogEnd`, judged against
  `FNA/src/Graphics/Effect/StockEffects/EffectHelpers.cs::SetFogVector()` and
  `FNA/src/Graphics/Effect/StockEffects/HLSL/Common.fxh::ComputeFogFactor()`/`ApplyFog()`.
- Main related tests: this file (Task 195, formula corrected under Task 1111 per its own header).

## Purpose

Verifies `BasicEffect`'s linear-fog rendering across three cases: (a) fog disabled → no color change; (b) fog at
50% (Z at the midpoint between `FogStart`/`FogEnd`) → color mixed halfway with `FogColor`; (c) fog at the boundary
that this test's own sign convention makes "fully fogged." The file's header (lines 1-33) contains an unusually
long, explicit worked derivation of the real formula and a documented history of a previous, subtly-wrong formula
that happened to coincidentally pass at this test's own Z=0 midpoint. Placement matches `examples-tests-easygl`.

## Executive Verdict

**Healthy** — independently re-derived the fog math from FNA's actual `SetFogVector`/`ComputeFogFactor`/`ApplyFog`
source and confirmed both (1) the test's own three expected outcomes and (2) EasyGL's actual GLSL `vFogFactor`
formula are algebraically equivalent to FNA's reference formula, including the file's own claimed (and initially
counter-intuitive) sign convention. This is one of the most rigorously self-documented, independently-verifiable
test files in this batch.

## Checklist Results

### API / XNA / FNA parity
`setFogEnabledProperty`, `setFogColorProperty`, `setFogStartProperty`, `setFogEndProperty` are real XNA members
used correctly (note: FNA's `FogColor` is a `Vector3`, matching `fx.setFogColorProperty(Vector3(1.0f, 0.0f, 0.0f))`
usage at line 149/177 — correct type).

### Behavioral correctness — full FNA-formula re-derivation
Traced `FNA/src/Graphics/Effect/StockEffects/EffectHelpers.cs::SetFogVector()` (lines 117-142): with
`World=View=Identity` (as this test explicitly sets via `setupBase()`, lines 114-119), `worldView` is Identity, so
`fogVector = (M13*scale, M23*scale, M33*scale, (M43+FogStart)*scale) = (0, 0, scale, FogStart*scale)` where
`scale = 1/(FogStart-FogEnd)`. `Common.fxh::ComputeFogFactor()` (lines 9-12) computes
`fogFactor = saturate(dot(position, FogVector))` where `position` is the raw object-space vertex position — with
the fogVector above this reduces to `fogFactor = saturate(scale*(z + FogStart))`. `ApplyFog()` (line 17) then does
`color.rgb = lerp(color.rgb, FogColor*color.a, fogFactor)` — i.e. `fogFactor=1` → pure `FogColor`, `fogFactor=0` →
unmodified scene color.

Plugged in this test's actual values (`FogStart=-0.9, FogEnd=0.9`, so `scale = 1/(-0.9-0.9) = -1/1.8`):
- **Case (b), Z=0**: `fogFactor = saturate(-0.5556*(0+(-0.9))) = saturate(0.5) = 0.5` → `mix(blue,red,0.5) =
  (128,0,128)` — exactly matches the test's own comment (line 29) and its `kMix(128,0,128,255)` assertion
  (line 164), independently confirmed.
- **Case (c), Z=-0.9 (=FogStart)**: `fogFactor = saturate(-0.5556*(-0.9+(-0.9))) = saturate(-0.5556*-1.8) =
  saturate(1.0) = 1.0` → full `FogColor` (red) — matches the test's comment (line 31-32) and its `isRed` assertion
  (lines 188-191).
- Also checked the boundary the test does *not* directly render but discusses (`Z=FogEnd=0.9`):
  `fogFactor = saturate(-0.5556*(0.9-0.9)) = 0` → fully unfogged. This confirms the file's own explicitly-flagged,
  counter-intuitive claim (lines 18-22): with `View=Identity`, `Z=FogStart` is the **fully-fogged** boundary and
  `Z=FogEnd` is the **unfogged** boundary — the reverse of what the property names might suggest in a normal
  camera-space setup. This is a real, correctly-identified and correctly-tested XNA/FNA behavior, not a test
  author's misunderstanding.

Then verified EasyGL's own shader-side convention is the algebraic mirror image and still equivalent: EasyGL's
`vFogFactor` (e.g. `EasyGLGraphicsBackend.cpp:2610`) is defined as
`clamp((aPos.z+uFogEnd)/(uFogEnd-uFogStart), 0, 1)` and used as `mix(uFogColor, FragColor.rgb, vFogFactor)` — note
the mix operands are *swapped* relative to FNA's `ApplyFog` (`FragColor` second, not first), meaning EasyGL's
`vFogFactor` is "fraction of *original* color," i.e. `vFogFactor = 1 - fnaFogFactor`. Algebraically confirmed:
`1 - saturate((z+FogStart)/(FogStart-FogEnd)) = (z+FogEnd)/(FogEnd-FogStart)` (derivation: `1 -
(z+FogStart)/(FogStart-FogEnd) = [(FogStart-FogEnd)-(z+FogStart)]/(FogStart-FogEnd) = -(z+FogEnd)/(FogStart-FogEnd)
= (z+FogEnd)/(FogEnd-FogStart)`) — which is *exactly* EasyGL's GLSL expression. The shader formula is genuinely,
algebraically equivalent to the FNA reference, not merely "close enough" or coincidentally matching at one point —
this was the specific defect (Task 1111, per the file's own header) that a previous formula only coincidentally
passed at Z=0 and is now provably fixed.

### Logic
Three independent scenes (`(a)`/`(b)`/`(c)`), each with `dev.Clear`/fresh `BasicEffect`/`setupBase()` (Identity
World/View/Projection, `VertexColorEnabled=true`)/`Apply()`/`RasterizerState::CullNone` (same "Task 896" pattern as
every sibling file in this batch)/`DrawUserPrimitives`/readback. `static VertexPositionColor quad[6]` (line 86,
defined out-of-line at line 208) is reused/mutated via `makeQuad()` between cases — correct, no aliasing hazard
since each case fully overwrites all 6 vertices before its own draw.

### Memory/resource lifetime
No dynamic allocation; `quad` is a static array with a single definition (line 208, matching its declaration) — no
ODR risk, correctly defined exactly once outside the class.

### C++ correctness
Comparison logic (`isBlue`/`isMix`/`isRed`, lines 135-137, 160-164, 188-190) uses per-channel threshold checks
rather than a shared `colourMatch()`/`matches()` helper (unlike every sibling file in this batch) — a minor
style inconsistency, not a correctness issue; each threshold was independently checked against the derived expected
values and found appropriately loose (e.g. case (b)'s `±30` tolerance around `128` comfortably covers realistic
GPU blend-precision noise without being so loose it would mask a real formula regression, since a wrong formula at
Z=0 - e.g. the old, since-fixed one - would have historically produced the *same* 0.5 factor at this specific
midpoint per the file's own header note (lines 6-7), meaning this specific tolerance check alone would not have
caught the old bug; case (c)'s boundary-value check is what actually would have failed under the old formula).

### Performance
N/A — three single-frame draws, no hot path.

### Robustness
No malformed-input path; deterministic geometry/effect state per case.

### Testing
This file is itself a test. See Missing or Weak Tests.

## Detailed Findings

No HIGH/CRITICAL findings. No MEDIUM findings — this file's math and cross-referencing hold up thoroughly under
independent re-derivation.

### F1 — Case (b)'s specific Z=0 sample is, per the file's own header, insufficient on its own to distinguish the old (buggy) formula from the new (correct) one

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage / self-documented limitation
- Location/symbol: file header, lines 6-7 ("it happened to give the same answer at this test's own Z=0 midpoint but
  was wrong everywhere else"); case (b), lines 141-166
- Evidence: the file's own comment explicitly states the *previous*, since-fixed formula gave the same result as
  the correct one specifically at Z=0 — meaning case (b) alone is not a regression guard against reintroducing that
  specific historical bug; only case (c) (`Z=FogStart=-0.9`, away from the coincidental-match point) actually
  discriminates between the two formulas. This is self-aware and correctly documented by the author, not an
  oversight — recorded as INFO/LOW since the test file as a whole (cases (b)+(c) together) is a real regression
  guard, but flagged so a future editor doesn't mistakenly treat case (b) in isolation as sufficient fog-formula
  coverage.
- Why it matters: minor — the test suite as a whole is sound; this is a note about which specific assertion carries
  the discriminating weight, useful context for anyone editing this file later.
- FNA/XNA comparison: N/A.
- Suggested future action: none required; the file's own header already documents this precisely.

## Cross-File Observations

- Shares the "Task 896 finding" `RasterizerState::CullNone` comment/pattern with every full-screen-quad test in
  this batch.
- Unlike `easygl_basiceffect_combined_test.cpp`/`easygl_basiceffect_emissive_test.cpp`/
  `easygl_basiceffect_multilight_emissive_test.cpp`, this file has **no** blank-frame retry loop — each case draws
  and reads back exactly once. This is the same asymmetry flagged as Finding F2 in the `combined_test.cpp` report:
  if the retry loop elsewhere is masking a real, still-reproducing race, this file (and
  `easygl_basiceffect_combinations_test.cpp`, `easygl_basiceffect_default_lighting_test.cpp`,
  `easygl_basiceffect_golden_test.cpp`, `easygl_backbuffer_resize_test.cpp`, none of which retry) would be expected
  to show the same flakiness and don't appear to have needed the workaround — worth resolving which files actually
  need it, resolved in this batch centrally at the `combined_test.cpp` report rather than repeated per-file here.

## Missing or Weak Tests

- No case combines fog with a textured/lit `BasicEffect` (all three cases here use plain `VertexPositionColor`,
  `LightingEnabled` implicitly `false`, no texture) — a reasonable scope limit for a fog-focused test, but worth
  flagging that fog×lighting×texture triple interaction has no dedicated pixel test anywhere in this batch.
- No case tests `FogStart == FogEnd` (`Common.fxh`/`EffectHelpers.SetFogVector`'s own documented "degenerate case:
  force everything to 100% fogged," `EffectHelpers.cs:119-122`) — an explicit FNA edge case with no CNA/EasyGL
  regression test found in this batch.

## Positive Findings

- Exceptionally well-documented, self-checking derivation — the file's header doesn't just assert expected values,
  it walks through the actual FNA formula, explains the historical bug, and explains why case (b) alone wouldn't
  have caught it. This is far above the baseline for "does this test justify its own numbers."
- Independently re-derived every formula and boundary case in this file against the real FNA source
  (`EffectHelpers.cs`, `Common.fxh`) during this audit and confirmed the EasyGL shader implementation is genuinely,
  algebraically equivalent — not a coincidental match.

## Final Assessment

A rigorously self-documented, independently-verified fog test whose expected values and boundary-case reasoning
check out exactly against both the FNA reference formula and the real EasyGL shader implementation; the only note
worth carrying forward is that case (b) alone is insufficient to guard the specific historical bug this file
describes (F1), which the file's own comments already make clear.
