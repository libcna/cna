# Audit: examples/easygl_scissor_test.cpp

## Metadata

- Source file: `examples/easygl_scissor_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend scissor-rectangle behavior test
- File type: C++ example/integration-test executable (`ScissorTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::GraphicsDevice::setRasterizerStateProperty`/
  `setScissorRectangleProperty` (`GraphicsDevice.cpp:1713-1733`), `CNA::Internal::Backends::EasyGL::
  EasyGLGraphicsBackend::SetScissorRect` (`EasyGLGraphicsBackend.cpp:2007-2026`)
- XNA/FNA relevance: `RasterizerState.ScissorTestEnable`, `GraphicsDevice.ScissorRectangle` — judged against FNA's
  documented contract that `ScissorTestEnable` (part of `RasterizerState`) gates whether `ScissorRectangle` (a
  separate `GraphicsDevice` property) has any effect.
- Main related tests: this file (Task 209) is this behavior's sole dedicated test in this shard.

## Purpose

Verifies four independent aspects of scissor-rectangle behavior: (1) `ScissorTestEnable=false` (default) does not
clip a full-viewport draw even when a scissor rect is separately set; (2) `ScissorTestEnable=true` with a
right-half scissor rect clips the draw to only the right half; (3) toggling `ScissorTestEnable` back to `false`
restores full-viewport drawing; (4) `GraphicsDevice.ScissorRectangle`'s getter round-trips whatever was last set,
independent of whether the test is enabled. Placement matches `examples-tests-easygl`.

## Executive Verdict

**Healthy** — all four checks are correctly targeted, the `RasterizerState`/`ScissorRectangle` separation this
file exercises was independently confirmed against the real `GraphicsDevice`/EasyGL source, and the Y-axis
flip/origin-conversion the backend performs was traced and found consistent with the test's top-left-origin
sample-point choices.

## Checklist Results

### API / XNA / FNA parity
`RasterizerState::CullNone` copied and mutated via `withScissor.setScissorTestEnableProperty(true)` (lines 101-103)
— confirmed `RasterizerState` is a value type whose `ScissorTestEnable` is a real, independent field from
`GraphicsDevice.ScissorRectangle` (a *separate* `GraphicsDevice` property, not part of `RasterizerState` itself) —
this file correctly models the real XNA architecture: `ScissorTestEnable` is the on/off gate (part of
`RasterizerState`, applied via `setRasterizerStateProperty`), while the rectangle's *value* lives independently on
`GraphicsDevice` (`setScissorRectangleProperty`/`getScissorRectangleProperty`) and persists across
`RasterizerState` changes — exactly the separation check 4 (lines 143-149) proves by setting/reading the rectangle
without ever touching `RasterizerState` at that point.

### Behavioral correctness
Traced `GraphicsDevice::setRasterizerStateProperty` (`GraphicsDevice.cpp:1715-1725`): forwards
`getScissorTestEnableProperty()` into `backend_->ApplyRasterizerState(...)` as one of several parameters — the
scissor *enable* flag is applied as part of the rasterizer-state bundle. Separately, `setScissorRectangleProperty`
(lines 1728-1733) calls `backend_->SetScissorRect(...)` independently. Confirmed
`EasyGLGraphicsBackend::SetScissorRect` (lines 2007-2026) itself never enables/disables the GL scissor test — its
own comment (line 2024-2025) states this explicitly: "Do NOT enable/disable scissor test here — that is controlled
exclusively by `ApplyRasterizerState`." This is the exact architectural split the test's four checks are designed
to validate, and it holds up under direct code inspection.

Check 1 (`ScissorTestEnable=false`, scissor rect set to right-half but ignored): `noScissor` is `CullNone`'s copy
with `ScissorTestEnable` left at its default (`false`, since `CullNone`'s stored value never set it true) — applied
via `setRasterizerStateProperty(noScissor)` (line 110), then the rect is set anyway (line 111) — since GL's scissor
test remains disabled (per the code trace above), the rect assignment is inert, correctly matching the "left pixel
is red (not clipped)"/"right pixel is red (not clipped)" expectations (lines 115-118).

Check 2 (`ScissorTestEnable=true`, right-half rect): `withScissor` enables the test; `rightHalf = Rectangle(midX, 0,
W-midX, H)` (line 106) is correctly computed to cover exactly the right half regardless of odd/even `W`. GL's
scissor origin is bottom-left, but since this test only samples *horizontally* distinct points at the *same*
vertical center (`midY`, both `leftX`/`rightX` share `screenY=midY`), the backend's Y-flip (`SetScissorRect`,
line 2011: `device.set_scissor(x, fbH - y - h, w, h)`) does not affect which half is clipped for a full-height
scissor rect (`h=H` covers the entire vertical extent regardless of the flip) — correctly reasoned test design that
sidesteps needing to account for the Y-flip at all.

Check 3 confirms disabling the test again (setting `noScissor`, without resetting the rectangle) restores
full-viewport rendering — proving `ScissorTestEnable`, not the rectangle's presence, is the actual gate,
independently corroborating the architectural split traced above.

Check 4's round-trip (`getScissorRectangleProperty()` after `setScissorRectangleProperty(testRect)`, lines 144-149)
is a plain store/retrieve check on `GraphicsDevice::scissorRectangle_` — confirmed the getter (`GraphicsDevice.cpp:
1727`) returns the stored value directly, no backend round-trip involved, so this specifically tests the
`GraphicsDevice`-side storage, not the GPU-side scissor state — an appropriately scoped, correctly-named check.

### Logic
`drawFullScreen()` (lines 56-67) constructs a fresh `BasicEffect` per call rather than reusing one — a minor,
harmless inefficiency (three `BasicEffect` constructions across the three draw phases) with no correctness impact
given this is a single-shot test.

### Memory/resource lifetime
No dynamic allocation of note; `gdm_` (`std::unique_ptr<GraphicsDeviceManager>`) standard ownership.

### C++ correctness
`colorNear()` (lines 42-47) compares only R/G/B (not Alpha), consistent with the pattern seen in
`easygl_rt_roundtrip_test.cpp`'s `eq()` — see Cross-File Observations.

### Performance
N/A — single-frame, four-phase test.

### Robustness
No malformed-input path exercised.

### Testing
This file is itself a test; see Missing or Weak Tests.

## Detailed Findings

No HIGH/CRITICAL/MEDIUM findings — the `RasterizerState.ScissorTestEnable` / `GraphicsDevice.ScissorRectangle`
architectural split this file is designed to validate was independently confirmed correct at the code level for
all four checks.

### F1 — `colorNear()` never compares the Alpha channel

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `colorNear()` (lines 42-47)
- Evidence: identical pattern to `easygl_rt_roundtrip_test.cpp`'s `eq()` — only R/G/B are compared; none of the six
  `check()` calls that use `colorNear()` (lines 115-140) verify the read-back Alpha channel.
- Why it matters: a regression that specifically corrupted Alpha during a clipped-vs-unclipped scissor readback
  (e.g. a discard/clip path that zeroed Alpha instead of leaving the underlying cleared-black value's Alpha=255
  intact) would pass silently.
- FNA/XNA comparison: N/A.
- Suggested future action: extend `colorNear()` to include Alpha, or add a dedicated Alpha-channel check.

## Cross-File Observations

- Shares the R/G/B-only comparison helper pattern (`colorNear()` here, `eq()` in `easygl_rt_roundtrip_test.cpp`) —
  the same Alpha-channel coverage gap recurs across this shard's readback-based tests, worth flagging once at the
  shard level rather than as an isolated one-off.
- Uses the same "Task 896 finding" `RasterizerState::CullNone` requirement pattern seen throughout this batch,
  though here it's foundational to the test's design (both `noScissor` and `withScissor` are built from
  `CullNone`) rather than an incidental fix applied to an existing quad.

## Missing or Weak Tests

- No case exercises a scissor rectangle that does *not* span the full viewport height (this test's `rightHalf`
  always uses `H` for height) — would exercise the backend's Y-flip conversion (`SetScissorRect`'s `fbH - y - h`)
  more thoroughly, since a partial-height rect is where a Y-flip bug would actually become visible via a wrongly
  clipped top/bottom edge.
- No case exercises scissor rectangle interaction with a bound `RenderTarget2D` (only the default backbuffer is
  used here) — `SetScissorRect`'s own logic (`EasyGLGraphicsBackend.cpp:2017-2022`) explicitly branches on
  `currentRtHeight_` for the Y-flip height source, and that branch is entirely untested by this file.
- No case tests an out-of-bounds or degenerate (zero-width/height) scissor rectangle — `SetScissorRect`'s own guard
  (`if (w <= 0 || h <= 0) return;`, line 2010) is a real, deliberate no-op path with no test coverage found in this
  file.

## Positive Findings

- Correctly and precisely validates the real XNA architectural split between `RasterizerState.ScissorTestEnable`
  (the gate) and `GraphicsDevice.ScissorRectangle` (the independently-persisting value) — a genuinely
  well-targeted test, not a superficial "does scissor exist" smoke test.
- The choice to sample only at a shared vertical center for the left/right comparison correctly sidesteps needing
  to reason about the backend's Y-flip, keeping the test's own logic simple without sacrificing correctness.

## Final Assessment

A precisely-targeted, architecturally-correct scissor-behavior test whose four checks were each independently
traced to the real `GraphicsDevice`/EasyGL source and confirmed sound; the only gaps are an Alpha-channel blind
spot (F1, shared with a sibling file) and untested partial-height/render-target/degenerate-rect scissor scenarios.
