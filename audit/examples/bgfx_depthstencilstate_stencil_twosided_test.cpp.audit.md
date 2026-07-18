# Audit: examples/bgfx_depthstencilstate_stencil_twosided_test.cpp

## Metadata

- Source file: `examples/bgfx_depthstencilstate_stencil_twosided_test.cpp` (240 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `DepthStencilState.TwoSidedStencilMode` /
  `CounterClockwiseStencilFunction`/`Fail`/`DepthBufferFail`/`Pass` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_depthstencilstate_stencil_twosided …)` /
  `cna_register_backend_test(NAME Bgfx_DepthStencilState_StencilTwoSided …)`,
  `cmake/Tests/BgfxTests.cmake:625-627`).
- XNA/FNA relevance: direct — `DepthStencilState.TwoSidedStencilMode`,
  `CounterClockwiseStencilFunction`/`CounterClockwiseStencilFail`/
  `CounterClockwiseStencilDepthBufferFail`/`CounterClockwiseStencilPass`.
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`RebuildStencilState`, lines 1723-1765 — the front/back bgfx-slot swap for
  `TwoSidedStencilMode`).

## Purpose

A genuinely differential 2-check test: draws a single BACK-FACING triangle (reversed winding,
`RasterizerState.CullMode=None` throughout so it actually rasterizes) with identical front-face and
CCW-face `DepthStencilState` property values across both checks, toggling *only*
`TwoSidedStencilMode` between them, and expects *opposite* final stencil outcomes. Check 0
(`TwoSidedStencilMode=true`): the CCW-configured op (`NotEqual`→fails→`Increment`) should apply to
the back-facing triangle, landing the stencil buffer at `0x06`. Check 1
(`TwoSidedStencilMode=false`, contrast): the front-configured op (`Equal`→passes→`Decrement`) should
apply to *all* faces including this back-facing one, landing at `0x04`. Both checks then read back
the *same* `ReferenceStencil=0x06` — check 0 expects a match (GREEN), check 1 expects a mismatch
(BACKGROUND), which is what actually proves `TwoSidedStencilMode` (not some other variable) changed
the outcome.

## Executive Verdict

**Healthy** — this audit independently traced the full stencil-buffer arithmetic for both checks and
cross-checked it against `BgfxGraphicsBackend::RebuildStencilState`'s actual front/back-slot swap
logic (lines 1723-1765); both the test's math and its dependency on that specific, documented
production workaround are correct and consistent.

## Checklist Results

### API / XNA / FNA parity
`setTwoSidedStencilModeProperty`, `setCounterClockwiseStencilFunctionProperty`,
`setCounterClockwiseStencilFailProperty`, `setCounterClockwiseStencilPassProperty`,
`setCounterClockwiseStencilDepthBufferFailProperty` (lines 103-128) all match
`DepthStencilState.hpp`'s public surface (lines 149-202 of that header) exactly, including the
"CounterClockwise" naming XNA itself uses for the back-face stencil slot set (not "back" — matching
FNA's own `DepthStencilState.cs` property names, which this audit spot-checked against the header's
own doc comments referencing exactly this naming).

### Behavioral correctness
Re-derived the full state machine:
- Stamp (`MakeStampState`, lines 97-108): front and CCW both `Always`/`Replace`, `ref=0x05` — the
  front-facing stamp quad (`DrawQuadFront`) writes stencil=0x05 across the whole screen regardless
  of which slot bgfx's rasterizer would apply (both are configured identically for the stamp).
- Op (`MakeOpState(twoSided)`, lines 110-130): front config is `Equal(0x05,0x05)`=true→`Decrement`
  on pass (0x05→0x04); CCW config is `NotEqual(0x05,0x05)`=false→**fails**→`Increment` on fail
  (0x05→0x06). Drawn via `DrawQuadBack` (deliberately reversed winding, lines 84-95).
- Production cross-check: `RebuildStencilState()` (`BgfxGraphicsBackend.cpp:1723-1765`), when
  `twoSidedStencilModeCached_` is true, deliberately assigns
  `stencilFront_ = BuildBgfxStencil(ccwStencilFuncCached_, …)` and
  `stencilBack_  = BuildBgfxStencil(stencilFuncCached_, …)` — i.e. it **swaps** XNA's own
  front/CCW configs into bgfx's front/back slots. The adjacent comment (lines 1729-1741) explains
  why: this backend never sets `BGFX_STATE_FRONT_CCW`, so bgfx's raw `glFrontFace` default (`GL_CW`
  is front) is the *opposite* sense from what "XNA front-facing" would naively map to, and this swap
  is the empirically-verified compensation. Given that swap, a raw back-facing (reversed-winding)
  triangle lands on whichever bgfx slot now holds the *CCW*-configured op — exactly the outcome the
  test expects for `TwoSidedStencilMode=true` (0x05→0x06 via the CCW op). Matches.
- When `TwoSidedStencilMode=false` (line 1751-1758): `stencilFront_ = stencilBack_ =
  BuildBgfxStencil(stencilFuncCached_, …)` (front-only config, no swap) — every face, front or
  back, uses the single front-configured op → 0x05→0x04. Matches the test's check 1 expectation.
- Read-back (`MakeReadBackState`, lines 132-142): both checks query the same
  `ReferenceStencil=0x06`/`Equal`. Check 0: `Equal(0x06, 0x06)`=true→GREEN (matches
  `expectGreen=true`). Check 1: `Equal(0x06, 0x04)`=false→BACKGROUND (matches `expectGreen=false`).

### Logic
The test's own header comment (lines 12-16) explicitly calls out the same "same-outcome-for-every-
check can't distinguish a real feature from a bypassed stencil test" pitfall this project
previously found on Vulkan (Task 870) — and structures itself as one genuinely differential pair
specifically to avoid it. This audit agrees the design achieves that: the two checks' expected
outcomes are opposite, and the only variable changed between them is `TwoSidedStencilMode`.

### C++ correctness
`RunCheck(GraphicsDevice&, bool twoSided)` (lines 156-189) constructs a fresh `RasterizerState` with
`CullMode::None` every iteration (lines 164-166) — correctly re-applied per retry-loop iteration,
not just once, so a `Clear()`-triggered device-state reset (if any) can't silently revert to a
culling default that would drop the back-facing triangle before the retry-until-settled logic even
gets a chance to observe real output.

### Robustness
Relying on a raw-winding hardware distinction (front vs. back face) while `CullMode::None` is active
is architecturally correct: culling and front/back-facing determination are independent GPU
pipeline stages (culling suppresses rasterization of one winding sense entirely, but a separate-
stencil-op pipeline still needs to know, per-fragment, which winding produced it, regardless of
whether that winding would otherwise have been culled) — this test setting `CullMode::None` doesn't
undermine the front/back split it's trying to observe.

### Testing
Fully covers `TwoSidedStencilMode` toggling and all 4 `CounterClockwiseStencil*` properties (via the
CCW config used in the op state) in a single differential pair. Complements
`bgfx_depthstencilstate_stencil_ops_test.cpp`'s front-facing-only coverage in this same batch.

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM findings.

## Cross-File Observations

- This is the one file in the batch whose correctness depends on the least-obvious piece of Bgfx
  backend internals (the front/back stencil-slot swap in `RebuildStencilState`) — this audit traced
  that logic directly rather than trusting the test's own comment at face value, and confirms it
  matches. Git history (`6b16bfdd`/`29f090a9`, "fix(Task 763): swap `TwoSidedStencilMode` front/back
  slots on Bgfx") corroborates that this swap was a deliberate, tested fix rather than an
  accidental artifact.
- Shares the `RasterizerState::CullNone`/retry-until-settled idioms with the rest of this batch,
  though here `CullMode::None` is load-bearing for the test's own subject matter (making a
  back-facing triangle actually rasterize), not merely a workaround for an unrelated default-cull
  quirk as in the other files.

## Missing or Weak Tests

None identified — the file achieves the minimum necessary differential structure to prove
`TwoSidedStencilMode` (not some other variable) is responsible for the observed outcome.

## Positive Findings

- Explicitly designed around a previously-identified real bug class in this project (test-outcome-
  invariant-under-the-feature-being-off, Task 870) rather than independently re-discovering the same
  mistake.
- The single differential pair is minimal but sufficient — it does not over-test with redundant
  checks, and both checks are load-bearing for the conclusion.

## Final Assessment

A precisely-targeted, correctly-derived test of `TwoSidedStencilMode` whose validity this audit
verified down to the specific bgfx front/back stencil-slot swap it depends on in
`BgfxGraphicsBackend::RebuildStencilState`. No defects found.
