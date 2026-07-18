# Audit: examples/rendertarget_viewport_scissor_reset_test.cpp

## Metadata

- Source file: `examples/rendertarget_viewport_scissor_reset_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — backend-agnostic (`GraphicsDevice`/`RenderTarget2D`/
  `BasicEffect`/`RasterizerState` XNA API only). Registered against **two** backends from the same
  shared source: `cmake/Tests/EasyGLTests.cmake:176-177`
  (`EasyGL_RenderTarget_ViewportScissorReset`), `cmake/Tests/VulkanTests.cmake:461-464`
  (`Vulkan_RenderTarget_ViewportScissorReset`). Not registered for Bgfx (unlike the sibling
  `rendertarget2d_depth_test.cpp` and `viewport_reset_after_resize_test.cpp` in this same batch).
- XNA/FNA relevance: direct — `GraphicsDevice.SetRenderTarget`/`SetRenderTargets` resetting
  `Viewport`/`ScissorRectangle`.
- FNA reference: `GraphicsDevice.cs` `SetRenderTargets` (sets `Viewport`/`ScissorRectangle` to the
  new target's/backbuffer's size unconditionally on every call).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`SetRenderTarget(RenderTarget2D*)` lines 1821-1859, `ResetViewportAndScissorForRenderTarget`
  lines 1815-1819, `setScissorRectangleProperty` lines 1728-1733).

## Purpose

Proves two things via a single scenario: (1) `SetRenderTarget`/`SetRenderTargets({})` genuinely
reset `Viewport` and `ScissorRectangle` to the new target's size (or the backbuffer's, when
unbinding) — matching FNA's unconditional `SetRenderTargets` reset behavior — and (2) that the
`ScissorRectangle` reset has a **real GPU-level effect**, not just a stale property value, by
drawing full-screen both before and after the RT bind/unbind cycle while `ScissorTestEnable=true`
with the same right-half scissor rect still nominally active. The file's own header comment states
CNA's `SetRenderTarget`/`SetRenderTargets` "never touched Viewport or ScissorRectangle at all
before this task" (Task 338) — a game's previously-set custom Viewport/Scissor would incorrectly
survive a render-target switch.

## Executive Verdict

**Healthy** — the production fix this test asserts is genuinely present and correctly wired end to
end (C++ property *and* backend GPU scissor state), independently confirmed by reading
`GraphicsDevice.cpp` rather than trusting the header comment's narrative. The test's own
methodology (scissor-clip sanity check before the RT switch, then re-check after) is a sound way to
distinguish "property says X" from "GPU actually behaves like X."

## Checklist Results

### API / XNA / FNA parity
`RasterizerState::CullNone`/`setScissorTestEnableProperty(true)` (lines 112-113),
`setScissorRectangleProperty`/`getScissorRectangleProperty` (lines 117, 130, 138) all map directly
to FNA's `GraphicsDevice.ScissorRectangle`/`RasterizerState.ScissorTestEnable`.

### Behavioral correctness
Traced `GraphicsDevice::SetRenderTarget(RenderTarget2D*)` (`GraphicsDevice.cpp:1821-1858`):
unconditionally calls `ResetViewportAndScissorForRenderTarget(rt->getWidthProperty(),
rt->getHeightProperty())` when binding, or with the backbuffer's `PresentationParameters` size when
unbinding (`else` branch, lines 1839-1841) — `ResetViewportAndScissorForRenderTarget`
(`GraphicsDevice.cpp:1815-1819`) calls **both** `setViewportProperty(Viewport(0,0,w,h))` **and**
`setScissorRectangleProperty(Rectangle(0,0,w,h))` through their real property setters (not direct
field writes), so the GPU-side backend state is pushed too. This exactly matches the test's step 3
check (`scissorWhileBound` == `(0,0,16,16)` while the 16×16 RT is bound, line 128-136) and step 3's
second half (`scissorAfterUnbind` == `(0,0,W,H)`, lines 138-141).

Confirmed `setScissorRectangleProperty` (`GraphicsDevice.cpp:1728-1733`) forwards to
`backend_->SetScissorRect(...)` on every call, including the ones made internally by
`ResetViewportAndScissorForRenderTarget` — meaning the test's step 4 GPU-level draw check (lines
143-149) is not merely confirming a C++ property value coincidentally matches what a later
independent scissor-set call would have produced; it is confirming the *same* reset call-path that
also updates the C++ property is the one that reaches the backend.

### Logic
The scenario deliberately does *no* draws between bind and unbind (lines 128-131) — "the point is
the bind/unbind cycle itself, not what happens inside" per the header comment — isolating exactly
one variable (does the reset happen at all) rather than conflating it with RT-content correctness
(already covered by `rendertarget2d_depth_test.cpp` in this same batch).

### C++ correctness
`colorNear` (lines 56-61) uses an integer tolerance of 4 for a 0-255 channel comparison — reasonable
for anti-aliasing/blend edge noise; no correctness issue.

### Testing
Five `check()` assertions (lines 122-149), each independently meaningful (not a single
all-or-nothing check): 2 pre-switch sanity checks, 2 property-value checks (while-bound and
after-unbind), 2 post-switch GPU-level checks. Good structure for isolating exactly which layer
(property vs. GPU) a hypothetical regression would live in.

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — Does not cover the plural `SetRenderTargets`/`RenderTargetCube` reset paths in this same file
- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location: whole file; contrast with `GraphicsDevice::SetRenderTargets` (`GraphicsDevice.cpp:
  1881-1927`, unbind branch at 1919-1923) and `SetRenderTarget(RenderTargetCube*, CubeMapFace)`
  (`GraphicsDevice.cpp:1861-1879`), both of which have their **own** separate call to
  `ResetViewportAndScissorForRenderTarget` — textually duplicated, not shared with the singular
  `RenderTarget2D` overload this test exercises.
- Why it matters: a hypothetical future edit that fixed/regressed only the singular
  `SetRenderTarget(RenderTarget2D*)` overload (the one this file exercises) would not be caught for
  the plural `SetRenderTargets`/`RenderTargetCube` overloads, since the reset logic is duplicated
  rather than shared through one code path. This audit did not check whether some other file in a
  different shard (e.g. an MRT-focused test) already covers this — flagged here as a coverage gap
  local to this file, not a confirmed production defect (the plural-overload code was read directly
  above and looks correct on its own).
- Suggested follow-up (not implemented by this audit): confirm (via the `xna-graphics`/MRT test
  shards, out of scope here) whether `SetRenderTargets`'s and `SetRenderTarget(RenderTargetCube*)`'s
  identical reset behavior is independently tested anywhere, since this file only proves it for the
  most common singular-`RenderTarget2D` path.

## Cross-File Observations

- `GraphicsDevice::SetRenderTarget`, `SetRenderTargets`, and `SetRenderTarget(RenderTargetCube*)`
  each re-implement the exact same "call `ResetViewportAndScissorForRenderTarget` with either the
  target's size or the backbuffer's" logic independently (3 call sites, `GraphicsDevice.cpp` lines
  ~1836-1841, ~1873-1878, ~1919-1923) rather than funneling through one shared internal helper that
  takes "new width/height" — not a bug (all three are currently consistent), but a duplication risk:
  a future fix applied to one call site could easily be missed in the other two. Worth flagging for
  the `xna-graphics` shard's own audit of `GraphicsDevice.cpp` as a maintainability observation.
- Reuses the same `readPixel`/`colorNear`/`drawFullScreen` helper pattern seen across this batch's
  sibling files (e.g. `rendertarget2d_depth_test.cpp`'s `DrawFullQuad`), and correctly applies the
  "Task 896" `RasterizerState::CullNone` convention (line 112) established elsewhere in this
  codebase for these specific CCW/CW-sensitive full-screen quads.

## Missing or Weak Tests

See F1 — the plural `SetRenderTargets`/`RenderTargetCube` reset paths are not exercised by this
specific file, though the underlying code was independently confirmed correct by direct reading.

## Positive Findings

- Real, verified GPU-level proof (not just property values) that the scissor reset actually
  reaches the backend — confirmed by tracing the exact call chain from `SetRenderTarget` through
  `ResetViewportAndScissorForRenderTarget` to `backend_->SetScissorRect`.
- Clean before/after symmetry (steps 1 and 4 use the identical `drawFullScreen`+pixel-read pattern),
  making a regression easy to localize to "the reset itself" versus "scissor testing in general is
  broken."
- Correctly isolates the bind/unbind-with-no-draws scenario from RT-content-correctness questions,
  avoiding conflating two different concerns in one test.

## Final Assessment

A precise, well-targeted regression test for Task 338's fix, with its central claim (GPU-level
scissor reset, not just a property) independently verified against the actual `GraphicsDevice.cpp`
call chain rather than trusted from the header comment. Its only weakness is scope: it doesn't
extend the same proof to the plural `SetRenderTargets`/`RenderTargetCube` overloads that duplicate
the identical reset logic.
