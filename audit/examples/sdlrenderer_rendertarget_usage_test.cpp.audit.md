# Audit: examples/sdlrenderer_rendertarget_usage_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_rendertarget_usage_test.cpp` (145 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `RenderTargetUsage::DiscardContents` vs `PreserveContents` test
- Build/CTest registration: `cna_sdl_test(cna_test_sdl_rendertarget_usage …)` /
  `cna_register_backend_test(NAME SDL_Renderer_RenderTargetUsage …)`,
  `cmake/Tests/SdlRendererTests.cmake:265-269`. Header traces to Task 706 (confirmed live: `git log` shows
  `a96f6250 verify(Task 706): DiscardContents vs PreserveContents on SDL_Renderer`).
- XNA/FNA relevance: `RenderTargetUsage` enum (`Microsoft::Xna::Framework::Graphics`),
  `GraphicsDevice.SetRenderTarget`'s auto-clear-on-bind semantics for `DiscardContents`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`SetRenderTarget(RenderTarget2D*)`, lines 1821-1859).

## Purpose

Proves that `RenderTargetUsage::DiscardContents` and `PreserveContents` are genuinely, observably different on
SDL_Renderer, not merely stored-but-ignored enum values. `GraphicsDevice::SetRenderTarget` auto-clears
(`Color(0,0,0,255)`) on every bind when the target's usage is `DiscardContents`, but does nothing extra for
`PreserveContents` — relying on the fact that an `SDL_TEXTUREACCESS_TARGET` texture's backing store is a normal,
persistent GPU texture whose content survives being unbound/rebound unless something explicitly overwrites it.
The test draws a distinctive Green into one target of each usage, unbinds both, **rebinds each without drawing
or clearing anything new**, immediately unbinds again, then samples: `DiscardContents`' content must be gone
(auto-cleared to black on the untouched rebind); `PreserveContents`' content must still be there.

## Executive Verdict

**Healthy.** This is a well-designed differential test: by drawing identically into both targets and only
varying the usage enum, then applying an identical "rebind with no draw" sequence to both, any observed
difference in the final sampled color can only be attributed to the usage-driven auto-clear-on-bind behavior,
not to any other confound. The mechanism it exercises was independently traced to and confirmed against the
current `GraphicsDevice::SetRenderTarget` code.

## Checklist Results

### API / XNA / FNA parity
`RenderTargetUsage::DiscardContents`/`PreserveContents` (lines 93-96) match FNA's own enum values and semantics:
FNA's `GraphicsDevice.SetRenderTarget` clears a `DiscardContents`-usage target on bind (matching Direct3D's own
discard-on-bind driver hint), leaving `PreserveContents` targets untouched — this test's decision to verify the
observable *difference* (not just that both construct/bind without throwing) is a stronger XNA-parity check than
a simple construction test would be.

### Behavioral correctness
Traced `GraphicsDevice::SetRenderTarget(RenderTarget2D*)` (lines 1821-1859): the auto-clear block (lines
1843-1858) is gated by `renderTarget->getRenderTargetUsageProperty() == RenderTargetUsage::DiscardContents` —
confirmed this is a real conditional on the *usage* property, not always-on. Both `rtDiscard`/`rtPreserve` are
constructed with `DepthFormat::None` (lines 93-96), so the `depthFormatRequested`/`HasRealDepthBuffer` branch
inside that block resolves to a color-only clear (`ClearOptions::Target`, no `DepthBuffer`) for either — the
test's differential design correctly isolates the *usage*-driven behavior from any depth-related masking
(distinguishing this test's isolation quality from the depth-decision test's own, separately-flagged issue —
see that file's audit report, F1 — since this file never asks for a depth-only clear).

### Logic
The sequence (draw Green into each once → unbind both → rebind each with **no draw** → unbind again → sample)
is precisely the right shape to isolate "does *rebinding alone* trigger a clear" from "does drawing after
binding clear it" — a subtler and more correct test than one that rebinds-and-immediately-draws-something-else,
which would conflate the two.

### Memory/resource lifetime
`RenderTarget2D rtDiscard`/`rtPreserve` (lines 93-96) are stack locals, alive through the whole `Draw()` body;
`SampleRenderTarget` (lines 59-70) takes them by non-const reference and reads via `SpriteBatch::Draw`, never
disposing — correct, since both are sampled twice in sequence (implicitly, via the two `SampleRenderTarget`
calls at lines 115, 120) and must remain valid.

### C++ correctness
No unsafe casts. `SampleRenderTarget`'s helper correctly reuses `rt.getWidthProperty()`/`getHeightProperty()`
(line 64) as the source rectangle size rather than hardcoding `rtSize`, making it reusable for either target
without duplication.

### Performance
N/A — single-frame test with 2 tiny (8×8) render targets.

### Thread safety
N/A.

### Architecture
Clean differential-test structure: one shared helper (`SampleRenderTarget`) used identically for both targets,
minimizing the chance the two code paths diverge in some untested way.

### Maintainability
145 lines; proportionate; the "distinctive Green" / "untouched rebind" framing in the header comment maps
directly and clearly onto the code that implements it.

### Portability
N/A — SDL_Renderer-specific, CMake-gated.

### Robustness
The tolerance thresholds (`<=20` for "gone," `>=230` for "still there," lines 116, 121) are appropriately strict
in opposite directions — a genuinely ambiguous result (e.g. a half-cleared buffer) would fail both checks rather
than silently passing either.

### Testing
This file is the dedicated `RenderTargetUsage` differential test; construction-only coverage of the
`RenderTargetUsage` property itself lives in `sdlrenderer_rendertarget2d_construction_test.cpp` (same shard,
audited separately in this batch) — a sensible split between "is the value stored" and "does the value have a
real effect."

### Cross-file consistency
Uses `PresentationMode::NativeBackBuffer` (line 134) consistent with every other file in this shard requiring
exact-pixel readback. `dev.Clear(Color(0,255,0,255))` calls at lines 100, 104 (used to paint each RT Green)
route through the single-argument `Clear(const Color&)` overload, which itself depends on the
depth/stencil-masking logic flagged as currently regressed in `sdlrenderer_rendertarget_depth_decision_test.cpp`'s
audit report (F1) — but since both `rtDiscard`/`rtPreserve` have `DepthFormat::None` and neither test expects a
throw from this call, that masking behaves exactly as intended here and does not affect this file's correctness
(noted for completeness, not a finding against this file).

## Detailed Findings

None. No CRITICAL/HIGH/MEDIUM/LOW findings in this file.

## Cross-File Observations

- Shares the `HasRealDepthBuffer`/auto-clear-on-bind mechanism with
  `sdlrenderer_rendertarget_depth_decision_test.cpp`, but exercises a different axis of it (usage-gated clearing
  vs. depth-format-gated clearing) — the two files are complementary, not redundant.

## Missing or Weak Tests

None identified for this file's stated scope. A theoretical addition (not required) would be a third case
verifying that a `PreserveContents` target that IS drawn into again after an untouched rebind correctly shows
the *new* content (proving the persistent-texture assumption holds under actual redraw, not just idle rebind) —
low incremental value given `SDL_TEXTUREACCESS_TARGET`'s persistence is a basic SDL guarantee already implicitly
exercised by every other render-target test in this shard that draws into a target more than once.

## Positive Findings

- Genuinely strong differential design: identical setup/rebind sequence for both usages, varying only the one
  property under test.
- Correctly isolates "rebind alone" from "rebind + draw" as the specific trigger being tested.
- Independently traced and confirmed against the exact `GraphicsDevice::SetRenderTarget` conditional this test
  depends on.

## Final Assessment

A well-designed, accurate differential test. No defects found in the test or the production code path it
exercises.
