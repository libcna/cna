# Audit: examples/bgfx_blendstate_separate_functions_test.cpp

## Metadata

- Source file: `examples/bgfx_blendstate_separate_functions_test.cpp` (157 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `BlendState.ColorBlendFunction`/`AlphaBlendFunction`
  independence pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_blendstate_separate_functions …)` /
  `cna_register_backend_test(NAME Bgfx_BlendState_SeparateFunctions …)`,
  `cmake/Tests/BgfxTests.cmake:556-558`).
- XNA/FNA relevance: direct — `Microsoft.Xna.Framework.Graphics.BlendState.ColorBlendFunction`/
  `AlphaBlendFunction`, `Microsoft.Xna.Framework.Graphics.BlendFunction`.
- FNA reference: `src/Graphics/States/BlendState.cs` (`ColorBlendFunction`/`AlphaBlendFunction`
  properties, independently settable), `BlendFunction` enum semantics (`Add`/`Subtract`/
  `ReverseSubtract`/`Max`/`Min`).
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp:1558-1596`
  (`XnaBlendFunctionToBgfxEquation`, `ApplyBlendState`).

## Purpose

Builds two custom `BlendState`s with all 4 blend *factors* fixed to `One` (isolating the *function*
under test) and opposite `ColorBlendFunction`/`AlphaBlendFunction` pairs (Check A:
`Color=Subtract,Alpha=Add`; Check B: `Color=Add,Alpha=Subtract`), then asserts the rendered RGB output
follows each check's own `ColorBlendFunction` regardless of what `AlphaBlendFunction` is set to.
Per the file's own header comment (lines 4-7), this is a genuinely discriminating regression test for
a real, previously-fixed Bgfx-specific bug: `BgfxGraphicsBackend::ApplyBlendState` used to hardcode
`BGFX_STATE_BLEND_EQUATION_ADD` regardless of the requested function, which Check A's
`ColorBlendFunction=Subtract` directly exposes.

## Executive Verdict

**Needs attention** — the production fix this test guards (Task 923, confirmed present and correct in
current `BgfxGraphicsBackend.cpp`) is real, and Check A genuinely discriminates the specific historical
bug it targets. However, the file's own stated purpose — *"verify ColorBlendFunction and
AlphaBlendFunction operate independently"* — is only half pixel-verified: neither check ever reads
back the alpha channel, so `AlphaBlendFunction`'s own correctness is asserted only by inference, never
independently observed (see F1). This mirrors a limitation this project's own commit history (Task
923) already documented and deliberately worked around by *deleting* a similar alpha-observing test
rather than shipping an unreliable one — but that caveat is not carried into this file's own header
comment, which reads as if both halves are pixel-verified.

## Checklist Results

### API / XNA / FNA parity
`BlendFunction::{Add,Subtract,ReverseSubtract,Max,Min}` (ordinals 0-4 per `BlendFunction.hpp`) map
1:1 to `XnaBlendFunctionToBgfxEquation` (`BgfxGraphicsBackend.cpp:1560-1569`:
`case 1→SUB, case 2→REVSUB, case 3→MAX, case 4→MIN, default→ADD`) — independently confirmed
correct and complete (all 5 enum values handled, not just the 2 this test exercises).
`setColorBlendFunctionProperty`/`setAlphaBlendFunctionProperty` (lines 60-61) match `BlendState.hpp`'s
real setter naming.

### Behavioral correctness
Re-derived Check A: `ColorBlendFunction=Subtract`, factors all `One` → `R = 200-50 = 150`,
`G = 50-200 = -150` clamps to `0` — matches `aOk` (`subA.R∈[135,165]`, `subA.G≤20`, lines 117-118).
Check B: `ColorBlendFunction=Add` → `R=200+50=250`, `G=50+200=250` — matches `bOk`
(`addB.R≥235`, `addB.G≥235`, lines 122-123). Both re-derivations are internally consistent with the
shared `MakeState()` helper (lines 53-63) and the fixed `Background/Source` colors (lines 23-24).

### Logic
Confirmed via direct read of `BgfxGraphicsBackend.cpp:1572-1596` that, prior to Task 923,
`ApplyBlendState` ignored its `colorBlendFunc`/`alphaBlendFunc` parameters entirely (ADD was always
implicit); the current code correctly threads both through `BGFX_STATE_BLEND_EQUATION_SEPARATE`
(line 1592-1594). This is independently corroborated by `plans/plan_graphics.md` row 923's own
`git stash`-revert verification note (*"Check A failed exactly as predicted (`(250,250,0)` instead
of `(150,0,0)`) … Check B coincidentally still passed"*), which this audit treats as strong,
concrete evidence the fix and this test's Check A genuinely correspond — not just a plausible-sounding
claim.

### Robustness
`DrawAndSample()` (lines 71-105) correctly does a fresh `Clear()`+`setBlendStateProperty`+draw+read
each retry iteration (up to 20), matching this shard's established safe pattern for Bgfx's "first read
per rendered frame" quirk — confirmed there is no code path here that re-reads without re-drawing
first.

### Testing
See F1 below — the stated dual claim ("ColorBlendFunction *and* AlphaBlendFunction operate
independently") is asymmetrically verified.

## Detailed Findings

### F1 — `AlphaBlendFunction`'s own correctness is never independently pixel-verified; the test's stated dual claim is only half-observed, and this asymmetry is not disclosed in the file's own header comment

- Severity: MEDIUM
- Confidence: HIGH (confirmed by reading the entire file — no alpha-channel readback exists in either
  `DrawAndSample()` call or anywhere else in the file)
- Category: test-coverage / correctness-of-test
- Location/symbol: `DrawAndSample()` (lines 71-105, reads only `got` via
  `dev.GetBackBufferData(&reg, &got, 0, 1)` and the checks derived from it read only `.getRProperty()`/
  `.getGProperty()`); header comment line 3 (*"verify ColorBlendFunction and AlphaBlendFunction operate
  independently"*); Check A/B assertions (lines 117-118, 122-123, both RGB-only).
- Evidence: both checks set `AlphaBlendFunction` to *opposite* values (`Add` in Check A, `Subtract` in
  Check B) specifically so that — per the header's own framing — "one leaked into the other's
  computation" would be observable. But neither check, nor any other code path in this file, ever
  reads the backbuffer's alpha channel or otherwise makes the *effect* of `AlphaBlendFunction`
  observable. What is actually proven is narrower: that `ColorBlendFunction`'s own equation correctly
  determines the RGB result *regardless of* whatever `AlphaBlendFunction` happens to be set to (i.e.,
  alpha's function has no *side effect* on color's result) — a real and useful property, but not the
  same as proving `AlphaBlendFunction` itself is correctly wired to a genuinely separate equation on
  the alpha channel. A regression that silently ignored `alphaBlendFunc` entirely (e.g., reverted
  `XnaBlendFunctionToBgfxEquation`'s second call at `BgfxGraphicsBackend.cpp:1594` back to always
  passing `BGFX_STATE_BLEND_EQUATION_ADD` for alpha specifically, while leaving the color half fixed)
  would not be caught by either check here, since neither ever looks at the alpha channel.
- Why it matters: a reader of this file's header comment (or the CTest name
  `Bgfx_BlendState_SeparateFunctions`) would reasonably conclude both halves of "separate functions"
  are pixel-verified; only the color half is. This project's own commit history for the closely
  related Task 923 (`plans/plan_graphics.md` row 923) explicitly acknowledges this exact limitation for the
  blend-*factor* analogue of this same problem (*"this project has no established, verified way to
  read the alpha channel back from the backbuffer directly"*) and, notably, chose to **delete** an
  attempted alpha-observing test there rather than ship one that reported the same result whether the
  underlying fix was applied or reverted — treating a non-discriminating "coverage" test as worse than
  no test. This file's header comment does not carry forward that same honesty about its own,
  analogous alpha-function gap.
- FNA/XNA comparison: N/A — this is a test-authoring/documentation-accuracy question about coverage
  claims, not an XNA/FNA behavioral question. The underlying `BlendFunction` semantics this test
  exercises for the color channel were independently confirmed correct.
- Related files: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp:1592-1594` (the
  `alphaBlendFunc`-half of the fix, confirmed present but not independently pixel-verified by any test
  found in this shard); `plans/plan_graphics.md` row 923 (documents the closely analogous, already-known
  alpha-channel-readback limitation for the *factor* case).
- Suggested future action (not implemented by this audit): either soften the header comment to
  precisely state what is actually verified (`ColorBlendFunction` is genuinely independent of
  whatever `AlphaBlendFunction` is set to; `AlphaBlendFunction`'s own equation is not independently
  observed here, matching the project's existing, disclosed limitation for blend factors), or
  investigate whether this project's `Blend::DestinationAlpha`-into-color-channel indirect-observation
  technique (as attempted, then abandoned, for Task 923's factor case) could be made reliable enough
  here to close the gap for real.

## Cross-File Observations

- The color-channel-only assertions here are consistent with every other `BlendState` file in this
  batch (none read back alpha), but this file is the only one whose *stated purpose* explicitly
  promises to verify a second, alpha-side property it never actually observes — the other files (e.g.
  `Additive`, `AlphaBlend`) only ever claim to verify color-channel properties, which they do
  correctly.
- Directly builds on, and its Check A genuinely discriminates, the Task 923 production fix also
  referenced by this shard's `bgfx_blendstate_opaque_test.cpp` and `bgfx_blendstate_additive_test.cpp`
  reports (the widened, all-4-factor "Opaque fast path" check).

## Missing or Weak Tests

See F1 — no test in this shard (confirmed by this file's own header comment referencing the
already-abandoned alpha-factor attempt) currently gives independent pixel evidence that
`AlphaBlendFunction`/`alphaSrcBlend`/`alphaDstBlend` are correctly wired on the alpha channel
specifically, as opposed to inferred from the production code's textual symmetry with the
already-verified color-channel path.

## Positive Findings

- Check A is a genuine, git-history-corroborated regression test for a real, previously-shipped Bgfx
  bug (the hardcoded-Add equation) — independently confirmed via both static code reading and the
  project's own documented `git stash` revert-and-rebuild verification.
- The `MakeState()` helper (lines 53-63) cleanly isolates the *function* under test from the *factor*
  values by fixing all 4 factors to `One`, which is the correct technique to avoid the two concerns
  interacting and confounding the result.
- The `[INFO]` diagnostic (lines 132-134) correctly names the exact defect class ("one leaked into the
  other's computation") a failure would indicate for the property that *is* actually verified.

## Final Assessment

A real, valuable regression test for a genuine historical bug, let down by a header comment that
promises more coverage (full alpha/color independence) than the test actually delivers (color-channel
independence from alpha's function value only). The gap is the same well-understood, already-disclosed
alpha-channel-readback limitation this project has hit before — the fix here is to be honest about it
in this file too, not necessarily to solve it.
