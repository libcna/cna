# Audit: examples/bgfx_setrendertarget_null_restore_test.cpp

## Metadata

- Source file: `examples/bgfx_setrendertarget_null_restore_test.cpp` (167 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `GraphicsDevice.SetRenderTarget(nullptr)` restore-backbuffer
  pixel test
- File type: standalone `Game`-subclass executable, CTest-registered as `Bgfx_SetRenderTarget_NullRestore`
  (`cmake/Tests/BgfxTests.cmake:121-124`)
- XNA/FNA relevance: direct — `GraphicsDevice.SetRenderTarget(null)` restoring the real backbuffer as
  the active render target (a well-known XNA idiom).
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` —
  `BgfxGraphicsBackend::SetRenderTarget2D()` (740-763, specifically the `else` branch at 756-762),
  `EnsureViewState()` (1325-1382).
- Superseded prior test: `examples/bgfx_render_target_usage_test.cpp` (Task 179), whose own header
  comment ("Pixel-level verification is not available in the Bgfx backend") this file's header
  explicitly calls stale (lines 5-10).

## Purpose

Binds a 16×16 `RenderTarget2D`, clears it red, calls `SetRenderTarget(nullptr)`, clears the *real*
64×64 backbuffer green, draws a full-viewport blue quad, then reads back two points: the centre (must
be blue — not the RT's red, not the backbuffer's own green clear) and a corner strictly outside the
RT's 16×16 bounds but inside the real 64×64 backbuffer (must also be blue — proves the viewport/ortho
projection genuinely reset to the full window size after unbind, not left at the RT's smaller
dimensions). Each sample point gets its own fully independent `RunCheck()` pass (bind-RT-once, then
retry only the backbuffer clear+draw+read cycle), consistent with this shard's established Task 406
Bgfx-quirk workaround.

The header also documents a negative result from a discriminating-power investigation (lines 21-31):
deliberately sabotaging both of `SetRenderTarget2D(nullptr)`'s explicit resets
(`spriteViewId`/`currentViewId_`, and `currentRtWidth_`/`currentRtHeight_`) did not break this test,
traced to bgfx's own per-frame view-framebuffer reset semantics making a stale binding from an earlier
frame unobservable across the multi-frame retry loop this Bgfx-quirk workaround requires. The comment
is explicit that "real production behavior is still genuinely, freshly pixel-verified here," just that a
clean revert-and-refail proof could not be established — an honest disclosure of a test-design
limitation rather than a claim of full mutation coverage.

## Executive Verdict

**Healthy** — both checks are correctly targeted at real production code paths, verified against current
source, and the file's own disclosed limitation (can't cleanly prove a revert-and-refail) is accurately
characterized rather than overclaimed.

## Checklist Results

### API / XNA / FNA parity
`device.SetRenderTarget(rt_.get())` / `device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr))`
(lines 84, 86) match FNA's `SetRenderTarget(RenderTarget2D)` / `SetRenderTarget(null)` overload pair
exactly, including the explicit `static_cast<RenderTarget2D*>(nullptr)` needed in C++ to disambiguate
which overload a bare `nullptr` should resolve to — a correct, idiomatic adaptation for the language,
not a deviation from XNA behavior.

### Behavioral correctness
Verified `SetRenderTarget2D(nullptr)`'s `else` branch (`BgfxGraphicsBackend.cpp:756-762`):
```
bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
currentViewId_ = 0;
spriteViewId = 0;
currentRtWidth_ = currentRtHeight_ = 0;
```
— confirms the real reset the file's header describes. `EnsureViewState()`'s viewport-sizing logic
(`1354-1355`, independently verified in this batch's `bgfx_rendertarget2d_msaa_test.cpp` report) uses
`cachedWidth`/`cachedHeight` (the real window size) whenever `spriteViewId == 0` — exactly the state
this reset produces — so the centre/corner checks are both targeting the real fixed production
mechanism, not a coincidence of this specific test's timing.

### Logic
The corner sample point `(kSize-2, kSize-2)` = `(62, 62)` (line 139) is correctly outside the RT's own
16×16 bounds while still inside the real 64×64 backbuffer — the comment's own justification ("confirms
the viewport/ortho genuinely reset to the full window, not left at the RT's dimensions") is a sound,
independently-verifiable claim: if `currentRtWidth_`/`currentRtHeight_` were *not* reset to 0 (and
`EnsureViewState()` incorrectly kept using the stale 16×16 RT size for the viewport/ortho even while
`spriteViewId == 0`, a plausible variant of the exact bug class this whole test family was created to
catch), the blue quad would only be drawn/visible within the RT's own 16×16 region, and this corner
would read the green clear instead of blue.

### Robustness
The `rtBoundOnce_` guard (line 68, checked at line 82) is a deliberate, well-justified design choice: the
comment (lines 76-81) explains that repeatedly rebinding the same `RenderTarget2D` across many separate
rendered frames (needed for the retry loop) triggers an unrelated, pre-existing Xvfb/software-GL
`glReadPixels(GL_INVALID_OPERATION)` limitation already documented elsewhere in this project
(`Bgfx_RenderTarget2D_DepthBuffer`/`MsaaResolve` baseline) — correctly avoiding tripping over an
unrelated, already-known environment limitation rather than either ignoring it (silent flakiness) or
mysteriously working around it without explanation.

### Testing
Both checks are read via fully independent `RunCheck()` passes (Task 406 pattern), each retrying only
the backbuffer-affecting steps (green clear + blue draw + read), never re-binding/re-clearing the RT
itself after the first call — correctly scoped to avoid the Xvfb limitation described above while still
retrying the part of the pipeline that genuinely needs Bgfx's known first-read-only quirk accounted for.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM/LOW findings — this file's assertions were independently verified against the
real `SetRenderTarget2D`/`EnsureViewState` production code and match exactly what the file claims and
tests.

## Cross-File Observations

- Directly builds on the `EnsureViewState()` RT-sizing fix independently verified in this batch's
  `bgfx_rendertarget2d_msaa_test.cpp` report (Task 878/879) — this file specifically targets the
  "does unbinding correctly *reset* that same logic to the full window" half of that fix, a
  complementary (not redundant) check to the MSAA test's "does binding correctly *apply* the RT's own
  size" half.
- The file's own disclosed discriminating-power limitation (can't sabotage-and-refail across the
  multi-frame retry loop) is a rare and useful piece of honest test-design self-critique in this shard —
  worth cross-referencing against `easygl_basiceffect_specular_test.cpp`'s similar self-disclosed
  weak-check pattern (a prior audited file in this project), though that file's issue was a stale
  numeric constant rather than an inherent test-design limitation like this one.

## Missing or Weak Tests

The header's own disclosed limitation (no clean mutation-testing proof of the specific resets under
test) is the one honest gap here; it does not reduce confidence that the *current* production behavior
is correctly verified, only that a regression reintroducing the bug might theoretically also survive
detection if it manifested in a way only visible within a single un-flushed frame. Not actionable without
restructuring the retry mechanism itself (which would reintroduce the Xvfb limitation this file already
correctly avoids).

## Positive Findings

- Explicitly and correctly identifies and retires a stale claim in a sibling file's own header comment
  (`bgfx_render_target_usage_test.cpp`'s "pixel-level verification is not available"), replacing it with
  real assertions rather than leaving the stale claim to mislead future readers.
- The `rtBoundOnce_` guard is a well-reasoned, well-documented avoidance of an unrelated known
  environment limitation, not a workaround dressed up as a fix.

## Final Assessment

A well-designed, accurately self-documented test with no correctness defects found; its one disclosed
limitation is honestly characterized as a test-design constraint, not silently omitted.
