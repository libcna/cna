# Audit: examples/rendertarget2d_depth_test.cpp

## Metadata

- Source file: `examples/rendertarget2d_depth_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — genuinely backend-agnostic (`GraphicsDevice`/
  `RenderTarget2D`/`BasicEffect`/`SpriteBatch` XNA API only, no backend-specific includes or
  types) — registered against **three** backends from the single shared source file:
  `cmake/Tests/EasyGLTests.cmake:158-159` (`EasyGL_RenderTarget2D_DepthBuffer`),
  `cmake/Tests/VulkanTests.cmake:376-377` (`Vulkan_...`), `cmake/Tests/BgfxTests.cmake:266-270`
  (`Bgfx_RenderTarget2D_DepthBuffer`).
- XNA/FNA relevance: direct — `RenderTarget2D`'s `DepthFormat` constructor parameter,
  `GraphicsDevice.SetRenderTarget`, `DepthStencilState.Default`.
- FNA reference: `GraphicsDevice.cs` (`SetRenderTarget`/depth-stencil binding),
  `DepthStencilState.cs` (`Default` = depth enable+write, `CompareFunction.LessEqual`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/RenderTarget2D.cpp`,
  `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (`SetRenderTarget`,
  `ResetViewportAndScissorForRenderTarget`), `src/Microsoft/Xna/Framework/Graphics/
  DepthStencilState.cpp`, and each backend's `CreateRenderTarget2D`/`HasRealDepthBuffer`.

## Purpose

Proves that a `RenderTarget2D` constructed with `DepthFormat::Depth24Stencil8` has a **real,
functioning** depth buffer wired into the offscreen draw pipeline, not merely a stored
`DepthStencilFormat` property value (the file's own comment explicitly distinguishes this from
"Task 331's property test"). Method: bind the RT, clear color+depth, draw a near green quad
(z=0.2) then a far red quad (z=0.8) at the same screen position with `DepthStencilState::Default`
(depth test+write, `LessEqual`) — if depth genuinely gates the draw, green must win; if depth
testing is silently bypassed for render-target draws, red (drawn last) overwrites. Unbind, then
sample the RT back onto the backbuffer via `SpriteBatch` (a path the header comment says is
"already pixel-verified working on both EasyGL (Task 87) and Vulkan (Task 148)"), isolating "does
depth actually function inside the RT" as the only new variable.

The header comment also documents a Bgfx-specific retry-until-non-black loop (Task 912), framed as
a workaround for `GetBackBufferData()` only reliably reflecting the first read call per rendered
frame on Bgfx.

## Executive Verdict

**Healthy** — this file does exactly what its header comment claims, the underlying production
wiring was independently confirmed (`DepthStencilState::Default` really is depth-enable+write with
`LessEqual`; `SetRenderTarget`/backend `CreateRenderTarget2D` really do allocate a functioning depth
attachment on all three registered backends), and the Task 912 retry-loop narrative is corroborated
by `git log` down to the exact commit and root cause. No live defect found in this file or the
functionality it directly exercises.

## Checklist Results

### API / XNA / FNA parity
`RenderTarget2D(device, w, h, mipMap, format, depthFormat)` (6-arg constructor, line 89-90) and
`DepthStencilState::Default` (line 116) both map directly to their FNA/XNA equivalents; no
non-XNA API surface is used in this file.

### Behavioral correctness
Independently confirmed `DepthStencilState::Default{"DepthStencilState.Default", true, true}`
(`DepthStencilState.cpp:6`) — the two `bool` constructor args are `depthBufferEnable_`/
`stencilEnable_` set `true`, and the class's own default member initializer sets
`depthBufferFunction_(CompareFunction::LessEqual)` (`DepthStencilState.cpp:13`) — matching the
header comment's "depth test+write enabled, LessEqual" claim exactly, not just asserted-and-trusted.

Traced `GraphicsDevice::SetRenderTarget(RenderTarget2D*)` (`GraphicsDevice.cpp:1821-1858`): calls
`backend_->SetRenderTarget2D(rt->GetRenderTargetBackend())`, and `RenderTarget2D`'s constructor
(`RenderTarget2D.cpp:44-67`) forwards `preferredDepthFormat` straight into
`device.GetBackend().CreateRenderTarget2D(width, height, static_cast<int>(preferredDepthFormat), ...)`
— confirmed present in all three backends actually under test here (`EasyGLGraphicsBackend.cpp:1679`,
`VulkanGraphicsBackend.cpp:7147`, `BgfxGraphicsBackend.cpp:732`), so the depth format genuinely
reaches backend-specific allocation rather than being silently dropped before construction.

### Logic
The retry loop (lines 129-141, up to 20 iterations) reads back the centre pixel after each
`Clear`+`Draw`+`Read`, breaking as soon as any channel exceeds 10. This does not mask a genuine
functional failure: both possible real outcomes (green wins → `(0,255,0)`, or the bug this test
exists to catch → red wins → `(255,0,0)`) are non-black and break the loop on the very first
iteration; only a scenario where *neither* quad is drawn at all (e.g. a totally broken RT bind)
would exhaust all 20 iterations and correctly still fail the final `pass` check (`centPx` stays
near `(0,0,0)`, satisfying `R<=50` but failing `G>=200`).

### Cross-file / documentation-accuracy check (git log corroboration)
The Task 912 narrative was independently verified rather than taken on faith:
- `git log -- examples/rendertarget2d_depth_test.cpp` shows `b1ae5976`/`74c3d723`
  ("`verify(Task P39-335)`: confirm render target depth buffers are functional, find
  format-fidelity gap") as the file's origin, and `59c924b0`/`23e677fe`
  ("`fix(Task 912)`: root-cause Bgfx render-target sampling bug to a known first-read quirk") as
  the commit that added the retry loop and the Bgfx registration.
- The origin commit's own message independently corroborates the "core depth-test functionality
  genuinely works... on both EasyGL and Vulkan" claim, and separately documents 3 real
  **format-fidelity** gaps found at the time (EasyGL hardcoding `DepthComponent24` regardless of
  requested format, Vulkan dropping its `hasDepth` parameter entirely, Bgfx not differentiating
  exact `DepthFormat` values) — explicitly scoped out of this file ("not fixed here, tracked as new
  Task 877 — distinct from 'does it work at all', which it does"). Confirmed via `git log --all |
  grep "Task 877"` that this was subsequently fixed (`412f763b`/`7d883ee5`, "wire
  DepthStencilFormat's exact value into RT depth/stencil attachments"), with a dedicated follow-up
  test (`vulkan_rendertarget_depthformat_fidelity_test.cpp`, out of scope for this batch) — so this
  file's own narrow scope ("does depth work at all with one specific format") was always a
  deliberate, disclosed choice, not an oversight later left stale.
- The Task 912 commit message independently confirms the exact same root-cause story as this
  file's header comment (stale-first-read quirk, not a distinct rendering bug), and states the fix
  was verified not to regress EasyGL/Vulkan (both already pass on the very first loop iteration).

### Testing
This is itself a test file; as a piece of the `xna-graphics`/backend coverage it exercises real
GPU state (depth test) rather than only a stored property, which is exactly the gap it was written
to close (superseding a weaker property-only test, "Task 331," per its own comment).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. Two LOW/INFO observations:

### F1 — Scope is deliberately narrow to one DepthFormat; does not by itself catch a
format-fidelity regression
- Severity: INFO
- Confidence: HIGH
- Category: test-coverage
- Location: whole file (always constructs with `DepthFormat::Depth24Stencil8`, line 90)
- Evidence: confirmed via git history (see above) that a real format-fidelity gap exists
  historically and was fixed by a *different* file (`vulkan_rendertarget_depthformat_fidelity_test.cpp`).
- Why it matters: not a defect in this file — it was always intended to answer "does depth work at
  all," and does so correctly; flagged only so a future reader doesn't mistake this file's PASS as
  proof that `Depth16`/`Depth24`/`DepthFormat::None` are equally faithfully honored on all three
  backends (a distinct, already-tracked concern).

### F2 — No explicit `GraphicsDeviceManager` construction
- Severity: INFO
- Category: architecture/consistency
- Location: whole file (no `gdm_` member, unlike `rendertarget_viewport_scissor_reset_test.cpp`/
  `viewport_reset_after_resize_test.cpp` in this same batch)
- Why it matters: relies on `Game`'s own eager default `GraphicsDevice` construction; consistent
  with several other files in this codebase that never resize/reconfigure the device, so this is
  not a defect, just noted for cross-file consistency awareness.

## Cross-File Observations

- Shares its exact source file across three backend CMake registrations (EasyGL/Vulkan/Bgfx) — one
  of relatively few files in the `examples-tests-generic` shard that is *literally* the same
  compiled binary re-registered three times, rather than merely "backend-agnostic in principle."
  This makes it a genuine cross-backend regression sentinel: a future regression in exactly one
  backend's depth-attachment wiring would show up as exactly one of the three CTest names failing
  with the identical source unchanged.
- Complements `easygl_rendertarget2d_msaa_test.cpp` (MSAA resolve) and
  `rendertarget_viewport_scissor_reset_test.cpp` (this same batch) as the trio of "RenderTarget2D
  really behaves like a real render target, not just a texture with a getter" test family.

## Missing or Weak Tests

None beyond F1 (already tracked elsewhere, not a gap in this file's own stated scope).

## Positive Findings

- Genuinely cross-backend (compiled and registered three times against the same source), and this
  audit confirmed — rather than assumed — that all three backends' `CreateRenderTarget2D` paths
  forward the requested depth format instead of silently dropping it.
- The header comment's historical narrative (Task 335 → Task 877 → Task 912) was checked against
  `git log` and found to be accurate in every particular, including the specific commit hashes,
  the specific three-backend gap list, and the specific root cause of the Bgfx quirk.
- Retry-loop design correctly cannot mask the specific regression this test exists to catch (see
  Logic section above) — a rare case where a "retry until success" pattern was verified not to be
  a silent false-negative risk.

## Final Assessment

A well-targeted, accurately-documented, genuinely cross-backend functional test. Its header
comment's multi-task historical narrative was independently verified against git history rather
than taken at face value, and checks out completely.
