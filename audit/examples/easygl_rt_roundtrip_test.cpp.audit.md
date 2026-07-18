# Audit: examples/easygl_rt_roundtrip_test.cpp

## Metadata

- Source file: `examples/easygl_rt_roundtrip_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `RenderTarget2D` bind/unbind round-trip integration test
- File type: C++ example/integration-test executable (`RtRoundtripTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::GraphicsDevice::SetRenderTarget`/`GetBackBufferData`
  (`GraphicsDevice.cpp:1778-1859`), `CNA::Internal::Backends::EasyGL::EasyGLGraphicsBackend::SetRenderTarget2D`/
  `ReadBackbuffer` (`EasyGLGraphicsBackend.cpp:1502-1555`, `1709-1728`), `EasyGLRenderTargetBackend::BindAsRenderTarget`
  (`EasyGLGraphicsBackend.cpp:660-663`)
- XNA/FNA relevance: `GraphicsDevice.SetRenderTarget(RenderTarget2D)`, `RenderTargetUsage.DiscardContents`,
  `GraphicsDevice.GetBackBufferData` — judged against FNA's `GraphicsDevice.cs` render-target-binding contract
  (viewport/scissor reset to target size, `DiscardContents` implying undefined-until-cleared content).
- Main related tests: this file (Task 180); sibling `easygl_render_target_usage_test.cpp` (Task 177) separately
  covers `DiscardContents` vs `PreserveContents` semantics — this file does not re-test that distinction and
  correctly doesn't need to.

## Purpose

Exercises a specific bind/unbind sequence — backbuffer → RT1 → backbuffer → RT2 → backbuffer — verifying at each
step that `GetBackBufferData` reads from whichever framebuffer is *currently* bound (the RT's FBO while bound, FBO 0
once unbound), and that switching away from and back to the backbuffer never disturbs its own prior content. The
file's own header comment (lines 1-16) states the exact mechanism under test: `currentRtHeight_ != 0` routes reads
to the RT attachment, `== 0` routes to FBO 0. Placement matches `examples-tests-easygl`.

## Executive Verdict

**Healthy.** Every claim in the file's own header comment was independently traced against
`EasyGLGraphicsBackend::ReadBackbuffer`/`SetRenderTarget2D` and `GraphicsDevice::SetRenderTarget` and found to be an
exact, accurate description of the real code path — this is a genuine, correctly-targeted regression test, not
boilerplate.

## Checklist Results

### API / XNA / FNA parity
`RenderTarget2D(device, width, height, mipMap, format, depthFormat, multiSampleCount, usage)` (line 70-73) matches
the real 8-argument constructor at `RenderTarget2D.hpp:43-50` exactly, argument-for-argument, including the
`RenderTargetUsage::DiscardContents` default being passed explicitly. `GetBackBufferData(const Rectangle*, Color*,
int, int)` (line 53) matches `GraphicsDevice.hpp`'s 4-argument overload.

### Behavioral correctness
Traced `GraphicsDevice::SetRenderTarget(RenderTarget2D*)` (`GraphicsDevice.cpp:1821-1859`): binding a target with
`RenderTargetUsage::DiscardContents` triggers an implicit `Clear(Target[|DepthBuffer], Color(0,0,0,255), 1.0f, 0)`
immediately after backend binding — exactly the "auto-Clear(black)" the test's header claims for both `rt1`/`rt2`
(steps 2 and 5). Confirmed `EasyGLRenderTargetBackend::BindAsRenderTarget()` (`EasyGLGraphicsBackend.cpp:660-663`)
itself does *not* clear anything (`fbo_.bind(...)` only) — the auto-clear is entirely a `GraphicsDevice`-level
policy, not a backend-level one, matching the test's framing of it as a `SetRenderTarget`-driven side effect.

Confirmed `EasyGLGraphicsBackend::ReadBackbuffer` (lines 1502-1555): when `currentRtHeight_ == 0` it explicitly
selects `GL_BACK` as the read source and Y-flips using the window's viewport height; when a render target is bound,
neither branch is taken and the already-bound FBO's `COLOR_ATTACHMENT0` is read directly, Y-flipped using the RT's
own height. This is precisely the two-branch behavior the test's header describes, and `SetRenderTarget2D`
(`EasyGLGraphicsBackend.cpp:1709-1728`) is confirmed to set `currentRtHeight_ = rt->GetHeight()` when binding an RT
and reset it to `0` when unbinding — the single piece of backend state the whole test's correctness hinges on.

Step 8 (backbuffer preserved Red) is a genuine test of FBO-0 content permanence: between the initial `Clear(Red)`
on FBO 0 and the final readback, FBO 0 is never bound as a *draw* target again (only RT1/RT2's FBOs are drawn to
and cleared) — `BindDefaultFramebuffer()` (called by `SetRenderTarget2D(nullptr)`, line 1726) merely re-binds FBO 0
without touching its contents, so Red is provably untouched. Correctly reasoned test design.

### Logic
The `eq()` helper (line 57-62) compares against the real `Color::Red`/`Color::Green`/`Color::Blue` XNA constants
directly rather than hardcoded RGB literals — this is a robust pattern: it stays correct even though
`Color::Green == 0xFF008000` (R=0,G=128,B=0, matching real System.Drawing "Green," not naively (0,255,0)/"Lime";
verified against `Color.cpp:106` and FNA's `Color.cs`). Had the test hardcoded `(0,255,0)` instead, it would have
been silently wrong regardless of what `Clear(Color::Green)` actually produces.

### Memory/resource lifetime
`RenderTarget2D rt1`/`rt2` are stack-local to `Initialize()`, destructed when the function returns (after `Exit()`
schedules shutdown but before the object's storage is reclaimed at scope exit) — both are unbound (`SetRenderTarget
(nullptr)` at steps 4/7) before either goes out of scope, so no "destroying a currently-bound render target" hazard
(the exact class of bug `GraphicsDevice.cpp`'s `Dispose(bool)` override guards against for `RenderTarget2D`).

### C++ correctness
No raw-pointer ownership; `readPixel`'s default-argument `x=0, y=0` (line 49) matches every call site's actual
usage (all four `readPixel(dev)` calls at lines 83, 96, 105 rely on the default, reading the top-left texel — fine
for a uniformly-cleared `kSize=4` target).

### Performance
N/A — one-shot `Initialize()`-only test, no hot path.

### Robustness
No malformed-input path exercised or expected; deterministic single-pass sequence.

### Testing
This file is itself a test; see Missing or Weak Tests.

## Detailed Findings

No HIGH/CRITICAL/MEDIUM findings — every mechanism the test's own header claims was independently verified against
the actual `GraphicsDevice`/`EasyGLGraphicsBackend` source and matches exactly.

### F1 — `eq()` never compares the Alpha channel

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `RtRoundtripTest::eq()` (lines 57-62)
- Evidence: `eq()` compares only `getRProperty()`/`getGProperty()`/`getBProperty()`; none of the four `check()`
  calls verify the read-back Alpha channel matches the cleared color's Alpha (`Color::Red`/`Green`/`Blue` all have
  A=255, and the `DiscardContents` auto-clear itself uses `Color(0,0,0,255)`, i.e. opaque black).
- Why it matters: a regression that corrupted only the RT's Alpha channel (e.g. an sRGB/format mapping bug that
  writes 0 or garbage to the 4th byte during `ReadBackbuffer`'s RGBA unpack) would pass this test silently.
- FNA/XNA comparison: N/A (CNA-internal readback path).
- Suggested future action: extend `eq()` (or add a distinct assertion) to also check `getAProperty()`.

## Cross-File Observations

- Shares the "read the currently-bound FBO" mechanism with every other render-target test in this shard's siblings
  (e.g. `easygl_render_target_usage_test.cpp`) — this file is the one that specifically stresses *switching between
  two different bound targets and back to the backbuffer*, a scenario none of the single-target tests cover.
- Confirms (via direct code read, not assumption) that `RenderTargetUsage::DiscardContents`'s auto-clear-to-black is
  implemented once, centrally, in `GraphicsDevice::SetRenderTarget`, not duplicated per-backend — a genuinely clean
  architectural placement worth noting positively for the `xna-graphics` shard's own audit.

## Missing or Weak Tests

- No case exercises the equivalent round-trip through the plural `SetRenderTargets(vector<RenderTargetBinding>)`
  overload (`GraphicsDevice.cpp:1881-1949`), which has its own independent viewport/scissor-reset and
  `DiscardContents` logic duplicated from the singular overload — a divergence there would need a separate test.
- No case exercises a round-trip where the second render target is smaller/larger than the first (would exercise
  `ResetViewportAndScissorForRenderTarget`'s size-dependent behavior more thoroughly than this test's uniform
  `kSize=4` for both RTs).

## Positive Findings

- The file's own header comment is an accurate, verifiable specification of the exact backend mechanism under
  test — confirmed line-by-line against `EasyGLGraphicsBackend.cpp` and `GraphicsDevice.cpp` during this audit.
- Uses symbolic `Color::Red`/`Green`/`Blue` constants for comparison rather than brittle hardcoded RGB triplets,
  correctly tolerant of `Color::Green`'s real (non-(0,255,0)) XNA-accurate value.

## Final Assessment

A precisely-targeted, well-documented regression test whose stated mechanism was independently confirmed accurate
against the real `GraphicsDevice`/EasyGL backend source; only a minor Alpha-channel coverage gap (F1) is worth
noting.
