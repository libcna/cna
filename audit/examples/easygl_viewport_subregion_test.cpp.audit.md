# Audit: examples/easygl_viewport_subregion_test.cpp

## Metadata

- Source file: `examples/easygl_viewport_subregion_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/test executable (hand-rolled `Game` subclass + `printf`-based PASS/FAIL
  harness), registered as CTest `EasyGL_Viewport_Subregion` (`cmake/Tests/EasyGLTests.cmake` line
  ~952), gated on `CNA_GRAPHICS_BACKEND STREQUAL "EASYGL"` and `CNA_BUILD_EXAMPLES`/`CNA_BUILD_TESTS`
- Related production code: `Microsoft::Xna::Framework::Graphics::GraphicsDevice::GetBackBufferData`
  (`src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` lines 1778-1813),
  `CNA::Internal::Backends::EasyGL::EasyGLGraphicsBackend::SetViewport`/`ReadBackbuffer`/`Clear`
  (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` lines 1502-1566, 2034-2053),
  `Microsoft::Xna::Framework::Graphics::BasicEffect` (default `World`/`View`/`Projection` = Identity,
  `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp` lines 41-93)
- XNA/FNA relevance: exercises real `Microsoft::Xna` public API (`GraphicsDevice.Viewport`,
  `Clear`, `DrawUserPrimitives`, `BasicEffect`, `GetBackBufferData`) against FNA's documented
  semantics that `glClear`/backbuffer-clear operations are viewport-independent (only gated by the
  scissor rectangle when enabled) — the file's own header comment (lines 10-16) states this
  explicitly and correctly.
- Main related tests: sibling `examples/easygl_viewport_state_test.cpp` (Task 208) covers
  `Viewport` get/set persistence across `Clear`/`Draw`; this file (Task 880) is the one that proves
  the GPU actually *honors* the viewport rectangle, via pixel readback — same-named siblings exist
  for Vulkan (`vulkan_viewport_subregion_test.cpp`) and Bgfx (`bgfx_viewport_subregion_test.cpp`).

## Purpose

Verifies "Task 880: GraphicsDevice.Viewport GPU wiring — sub-region (split-screen) viewport." The
file's own header comment states the historical defect plainly: "every backend hardcoded its actual
viewport to the full render-target/window size regardless of what Viewport was set to." This test
proves the fix by drawing a full-NDC-range (-1..1) quad under a custom left-half `Viewport` and
confirming via pixel readback that only the left half of the window is touched — the right half must
remain the earlier clear color. Correctly placed under `examples/` with the `easygl_` prefix per
`AUDIT_SCOPE.md`'s backend-integration-test sharding convention (build-gated on
`CNA_GRAPHICS_BACKEND=EASYGL`, no EasyGL-specific symbols in the file itself, matching its Vulkan/Bgfx
siblings).

## Executive Verdict

**Healthy.** This is a rigorous, well-constructed pixel-readback test with a real "prove the negative"
structure (confirms the right half specifically stays the *previous* clear color, not just that the
left half is correct) that directly falsifies the exact historical bug it documents. Every helper
function and constant was traced against the production code it depends on and found consistent. No
correctness defects found; only minor, low-severity observations (F1, F2) below.

## Checklist Results

### API / XNA / FNA parity
`Viewport(int,int,int,int)`, `getViewportProperty()`/`setViewportProperty()`, `Clear(Color)`,
`BasicEffect` with `VertexColorEnabled = true` then `Apply()`, `DrawUserPrimitives(PrimitiveType,
VertexPositionColor*, int, int)`, `GetBackBufferData(const Rectangle*, Color*, int, int)`,
`SetDepthTestEnabled` (NOXNA extension, correctly not `Microsoft::Xna`-namespaced despite living on
`GraphicsDevice` — confirmed `include/.../GraphicsDevice.hpp` line 674 marks it `NOXNA`),
`setBlendStateProperty(BlendState::Opaque)`, `setRasterizerStateProperty(RasterizerState::CullNone)` —
all verified to exist with matching signatures in their respective headers. `BasicEffect`'s default
`World`/`View`/`Projection` = `Matrix::getIdentityProperty()` (verified `BasicEffect.cpp` lines 41-93)
is the load-bearing fact this test's entire design depends on: passing raw NDC `(-1..1)` positions
through an untouched (identity) MVP pipeline means the quad's clip-space position equals its
object-space position, so the only thing that can clip it to a sub-rectangle of the window is the
GPU viewport transform itself — exactly the mechanism under test, correctly isolated from any
matrix-math confound.

### Behavioral correctness
- **`readPixel()`** (lines 36-42): constructs a 1×1 `Rectangle` and calls
  `GetBackBufferData(&reg, &px, 0, 1)`. Traced against `GraphicsDevice::GetBackBufferData` (lines
  1778-1813): when `rect` is non-null it reads `x=rect->X, y=rect->Y, w=rect->Width, h=rect->Height`
  directly (top-left-origin XNA coordinates) and forwards to `backend_->ReadBackbuffer(x,y,w,h,...)`.
  `EasyGLGraphicsBackend::ReadBackbuffer` (lines 1502-1555) flips Y from GL's bottom-left origin using
  `fbH` sourced from `GetViewportSize()` — verified this returns the **logical window/backbuffer
  size** (`getLogicalSize()`, line 1661), *not* the currently-set custom `Viewport`'s height — so
  `readPixel(dev, x, y)`'s absolute window-space coordinates correctly address the physical pixel at
  `(x,y)` in the actual backbuffer regardless of whatever sub-region `Viewport` is active at draw
  time. This is the single most important cross-file fact this test's correctness rests on, and it
  checks out.
- **`colorNear()`** (lines 44-49): per-channel `abs(a-b) <= tol` on R/G/B only (alpha correctly
  ignored, matching this project's own documented convention in `PixelTestGame::ExpectPixel`'s
  comment about ignoring alpha unless a test cares about it explicitly), default `tol=4` — a
  reasonable tolerance for solid, unblended `Color::Red`/`Black`/`Green` against potential
  driver/rounding noise, consistent with the ~98-file tolerance survey referenced in
  `PixelTestGame.hpp`'s own comments.
- **`drawFullScreen()`** (lines 56-67): six vertices forming two triangles spanning the full
  `(-1,-1)`-`(1,1)` NDC quad, `BasicEffect` with vertex color only (no texture/lighting needed for a
  flat-color test) — correctly minimal for the property under test.
- **Scenario 1** (lines 104-112): `Clear(Color::Black)` over the *whole* window (this call is made
  with the *default/full* viewport still active, matching the header comment's stated method:
  "Clear() the WHOLE window black first ... Viewport doesn't gate Clear()"), **then**
  `setViewportProperty(Viewport(0,0,leftHalfW,H))` is set, **then** `drawFullScreen(dev, Color::Red)`
  draws the NDC quad. Verified against production: `EasyGLGraphicsBackend::Clear()`'s own Task-880
  comment (lines 1560-1563) confirms `glClear` is issued with whatever GL clear state is set,
  independent of `glViewport`, and `SetViewport()`'s own Task-880 comment (lines 2038-2044) confirms
  the fix under test — this ordering (clear-whole-window, *then* narrow the viewport, *then* draw) is
  exactly the sequence needed to distinguish "GPU viewport clips draws" from "GPU viewport also clips
  clears," and the test asserts both outcomes correctly: `leftX` pixel is red (drawn, inside the
  narrowed viewport), `rightX` pixel is still black (the earlier whole-window clear, undisturbed by
  either the narrowed viewport or the draw that only affected the left half).
- **Scenario 2** (lines 114-122): restores `fullVp` (the viewport captured *before* any test
  mutation, at line 93) and re-clears+redraws in green, checking **both** halves are now green — a
  genuine "does the fix regress the common/default case" check, not just a one-directional "narrow
  viewport works" claim. This is an important complementary assertion: a broken implementation that
  always clips to *some* fixed sub-rect (rather than genuinely reading the current `Viewport` state)
  would pass scenario 1 but fail scenario 2's right-half check.
- **Scenario 3** (lines 124-132): plain non-pixel getter/setter round-trip
  (`Viewport(10,20,300,200)`) — deliberately narrower than the state test's check 2 (doesn't test
  `MinDepth`/`MaxDepth`, which is already fully covered by `easygl_viewport_state_test.cpp`'s checks
  2 and 7) — correctly scoped as a lightweight sanity check rather than a redundant duplicate.

### Logic
The three-scenario structure (narrow→verify isolation, restore→verify full coverage, plain
round-trip) forms a genuinely falsifiable test of the specific claim in the header comment ("BOTH
halves would show red" pre-fix) — a regression to the pre-880 behavior would make `rightX` in
scenario 1 read red instead of black, which the test explicitly checks
(`colorNear(readPixel(dev, rightX, midY), Color::Black)`, line 111) rather than only checking the
positive (left-half-is-red) case. This is the strongest possible structure for this kind of
GPU-wiring regression test given the constraints of a pixel-readback approach.

### C++ correctness
`static const Vector3 kTL/kBL/kBR/kTR` (lines 51-54) are simple immutable value constants with no
static-init-order risk (no cross-TU dependency, no dependency on any other static's initialization).
`const VertexPositionColor q[6]` (lines 62-65) constructed from these plus a per-call `col` parameter
— correct, no aliasing/lifetime issues (array is stack-local, fully consumed synchronously by
`DrawUserPrimitives` before `drawFullScreen()` returns). No raw-pointer lifetime concerns anywhere in
the file.

### Memory/resource lifetime
Same pattern as its Task-208 sibling: `unique_ptr<GraphicsDeviceManager>` member, single `Draw()`
latch via `done_`, `Exit()` called once. No leaks or double-free risk.

### Performance
N/A — single-frame diagnostic test, not a hot path; three `Clear`+`Draw`+readback cycles is a
deliberately-small, appropriate cost for a CTest-registered correctness check.

### Thread safety
N/A — single-threaded `Game` loop.

### Architecture
Clean: only public `Microsoft::Xna` API surface used, zero EasyGL-specific symbols, matching its
Vulkan/Bgfx siblings' structure closely enough that the three files could plausibly share more code
(see F2) but the current duplication is small and each file is independently readable, which has real
value for a per-backend regression suite.

### Maintainability
Well-commented — the file's header comment (lines 1-18) is unusually good at stating the bug, the
method, and the expected pre-fix-vs-post-fix outcome up front, which materially helped verify this
audit's claims against the production code. `readPixel`/`colorNear`/`drawFullScreen` are small,
well-named free functions with a single clear responsibility each. No dead code, no
`TODO`/`FIXME`/stub markers.

### Portability
Environment (`SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}`) set by the CMake test registration
itself; no platform-conditional code in the file.

### Robustness
Same gap as the Task-208 sibling: no `CNA::Examples::ProbeGpuDisplayAvailable()` headless preflight
before constructing the real `Game` (see F1) — same systemic, non-unique-to-this-file pattern.

### Testing
This *is* the test. As analyzed under Behavioral correctness/Logic above, its three scenarios
constitute a genuinely rigorous, falsifiable regression test for the specific historical defect it
documents — one of the stronger-evidenced test files audited in this shard so far, not "compiles and
doesn't crash" filler.

### Cross-file consistency
Fully consistent with `EasyGLGraphicsBackend.cpp`'s own "Task 880" comments on `SetViewport`,
`SetScissorRect`, and `Clear` — all three describe the identical historical bug (viewport/scissor
hardcoded to full window/RT size) this test's header comment also describes, from three independent
vantage points (this test's black-box behavior, and two backend methods' implementation comments)
that corroborate each other rather than merely repeating one source.

## Detailed Findings

### F1 — No headless-display preflight guard before constructing the real `Game`

- Severity: LOW
- Confidence: HIGH
- Category: robustness
- Location/symbol: `main()` (lines 147-152).
- Evidence/why it matters: identical to the finding recorded for
  `examples/easygl_viewport_state_test.cpp.audit.md` (F1) — no
  `CNA::Examples::ProbeGpuDisplayAvailable()` call before `Run()`, so a headless environment with no
  display and no Xvfb would surface as an uncaught `std::runtime_error` crash rather than a clean
  `SKIPPED` (exit code 77) result. Confirmed systemic (~22/797 example files use the guard); not
  specific to this file's authorship.
- Suggested future action: none required for this audit; a mechanical sweep adding the guard to all
  pre-Task-470 hand-rolled `main()`s would be the natural fix if ever prioritized.

### F2 — Cross-backend triplet (`easygl_`/`vulkan_`/`bgfx_viewport_subregion_test.cpp`) is hand-duplicated rather than shared

- Severity: LOW
- Confidence: MEDIUM (structure strongly suggests duplication; did not byte-diff the three files)
- Category: maintainability
- Location/symbol: whole file, vs. `examples/vulkan_viewport_subregion_test.cpp` and
  `examples/bgfx_viewport_subregion_test.cpp` (confirmed to exist via directory listing, not opened
  in this batch since they belong to other shards).
- Evidence: identical CTest name pattern (`{Backend}_Viewport_Subregion`), identical Task-880
  provenance comment structure referenced from this file's own header, and an architecture (public
  API only, no backend-specific symbols) that would let all three be the *same* source file linked
  three times, the way `cna_easygl_test`'s macro already parameterizes the executable/backend
  pairing for other shared test sources in this codebase's CMake.
- Why it matters: purely a maintenance-cost observation — three independently-maintained copies of
  the same test logic risk drifting out of sync (e.g., a tolerance or sample-point tweak applied to
  one but not the others) with no compiler-enforced link between them. Not a correctness defect in
  this file today.
- Suggested future action (not implemented by this audit): if a future task revisits cross-backend
  example duplication broadly, this triplet (and likely others sharing the same backend-agnostic
  API-only shape) is a reasonable consolidation candidate — flagged for
  `AUDIT_CROSS_CUTTING_FINDINGS.md` rather than acted on here, since deduplicating test sources is
  out of scope for an audit-only pass.

## Cross-File Observations

- This file and `easygl_viewport_state_test.cpp` together give viewport handling unusually strong,
  complementary EasyGL coverage: C++-side persistence (state test) plus actual GPU-side clipping
  effect (this file), with no detected gap or redundant overlap between them.
- `GetViewportSize()`'s naming is genuinely confusing in context — despite its name, it returns the
  full window/logical framebuffer size, not the currently-active (possibly sub-region) `Viewport`
  rectangle; this test's correctness depends on that distinction holding, and it does, but the name
  itself is a latent readability trap for a future maintainer skimming `EasyGLGraphicsBackend.cpp` —
  worth a note in the `backend-easygl` shard's own audit (`IGraphicsBackend.hpp`'s abstract method of
  the same name, line 503, has the identical naming tension) rather than fixed here.

## Missing or Weak Tests

- No check exists (here or in the state test) for a sub-region `Viewport` combined with a bound
  custom `RenderTarget2D` (as opposed to the default backbuffer) — the `EasyGLGraphicsBackend`
  source comments (`SetViewport`/`SetScissorRect`, lines 2038-2044/2011-2016) explicitly call out
  `currentRtHeight_`-based Y-flip logic as behaving differently when an RT is bound, but this test
  only exercises the default-framebuffer path. A `Viewport` + custom `RenderTarget2D` combination
  test would close a real, specifically-named-in-the-source-comments gap.
- No check for an out-of-range/degenerate sub-region `Viewport` (e.g., partially or fully outside
  the window bounds) — reasonable to omit at this test's level given `SetViewport`'s own `w<=0||h<=0`
  guard is a backend-implementation-detail edge case better suited to a backend-focused test.

## Positive Findings

- The scenario-2 "restore full viewport, verify full green coverage" check is a genuinely strong,
  easy-to-miss test design choice — it specifically rules out a broken fix that clips to some fixed
  region regardless of the actual `Viewport` state, not just a fix that fails to clip at all.
- `readPixel`'s absolute-window-coordinate design was verified correct against `GetViewportSize()`'s
  actual (non-obvious, confusingly-named) behavior — the test author clearly understood this
  distinction, since sampling still works correctly for both scenarios regardless of which `Viewport`
  is currently active.
- Header comment (lines 1-18) is exemplary: states the bug, the test method, and the expected
  pre-/post-fix outcomes explicitly enough to independently corroborate against the production code
  without needing to run the binary.

## Final Assessment

A rigorous, well-designed regression test that correctly isolates and falsifies the specific
historical GPU-viewport-wiring defect it documents, with a "prove both directions" structure
(narrow-viewport isolation AND full-viewport-restore coverage) that is stronger than a simpler
single-direction pixel check would have been. No correctness defects found; the two findings
recorded (F1, F2) are minor, systemic-pattern observations rather than authorship-specific mistakes.
