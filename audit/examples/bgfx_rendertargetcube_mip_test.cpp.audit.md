# Audit: examples/bgfx_rendertargetcube_mip_test.cpp

## Metadata

- Source file: `examples/bgfx_rendertargetcube_mip_test.cpp` (214 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `RenderTargetCube` mip-chain generation test
- File type: standalone `Game`-subclass executable, CTest-registered as `Bgfx_RenderTargetCube_MipChain`
  (`cmake/Tests/BgfxTests.cmake:498-501`)
- XNA/FNA relevance: direct — `RenderTargetCube(device, size, mipMap, ...)`, first-ever Bgfx test to
  sample a `RenderTargetCube` via `EnvironmentMapEffect` at all (per its own header comment).
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` —
  `BgfxRenderTargetCubeBackend` ctor (800-835, `hasMips=mipMap` at `bgfx::createTextureCube` line 819),
  `BindAsRenderTargetFace()` (845-866), `SetRenderTargetCubeFace()` (887-899), `ReadBackbuffer()`
  (303-325, confirms the `bgfx::frame()` call the test's per-face workaround depends on).

## Purpose

Confirms `RenderTargetCube`'s mip-generation *mechanism* doesn't corrupt level 0 or crash — explicitly
scoped (per its own header, lines 8-13, mirroring the Vulkan port's identical decision) to **not**
assert on coarser mip levels' actual content, since a prior task found that technique
non-discriminating for cube-face content. Phase 1 fills all 6 faces solid blue via `SpriteBatch`
(triggering bgfx's own per-face auto-mip-regeneration on unbind); Phase 2 samples the now-unbound,
mip-complete cube back via `EnvironmentMapEffect` and checks the centre pixel is blue
(`R<=50, G<=50, B>=200`).

The header comment also documents two prerequisite bug fixes this test's own methodology required
(Task 874's `IBgfxSamplable`-style `dynamic_cast` fix for cube sampling, and a `SetRenderTargetCubeFace`
override to update `currentRtWidth_`/`currentRtHeight_`) plus **one deliberately-unfixed, deeper
architectural finding**: rendering into all 6 cube faces within a single un-advanced bgfx frame only
actually renders into whichever face was bound *last*, because all 6 faces share one hardcoded bgfx
view id and `bgfx::setViewFrameBuffer()` is per-view-per-frame, not per-submit-call. The test works
around this with a per-face dummy `GetBackBufferData()` call (lines 137-144) to force a `bgfx::frame()`
boundary between each face's fill.

## Executive Verdict

**Mostly healthy** — the mip-generation mechanism itself is correctly wired and the test's own
documented workaround for the separately-tracked Task 910 view-id limitation is verified to actually
force the frame boundary it claims to. One minor, non-blocking observation (F1): this file (like the
combined SkinnedEffect test in this same batch) omits an explicit `GraphicsDeviceManager`, relying on
`Game`'s own default-constructed `GraphicsDevice` — established, not unique to this file, and provably
harmless here since all pixel sampling uses the runtime viewport size rather than a hardcoded assumption.

## Checklist Results

### API / XNA / FNA parity
`RenderTargetCube(device, kCubeSize, /*mipMap=*/true, SurfaceFormat::Color, DepthFormat::None)`
(line 95-96) matches FNA's constructor shape. `EnvironmentMapEffect`'s `EnvironmentMapAmount=1`/
`EnvironmentMapSpecular=0` combination (lines 160-161) isolates a pure reflection sample, matching this
same pattern verified independently in the sibling `bgfx_rendertargetcube_depthformat_test.cpp`.

### Behavioral correctness
Confirmed `bgfx::createTextureCube(size, mipMap, 1, RGBA8, msaaFlag|...)` (line 819-821) really does
pass `mipMap` (here `true`) as `hasMips`, and the constructor's own comment (lines 814-818) correctly
cites that `BindAsRenderTargetFace()`'s `bgfx::Attachment::init()` call defaults to
`BGFX_RESOLVE_AUTO_GEN_MIPS` for the colour/cube-face attachment (confirmed at
`BgfxGraphicsBackend.cpp:849`, `atts[0].init(cubeTex, bgfx::Access::Write, static_cast<uint16_t>(face))`
— the `_resolve` parameter is omitted, defaulting per bgfx's own API to auto-gen), so mip regeneration
genuinely happens on unbind, not merely "storage is allocated but undefined," matching the file's own
scope claim.

### Logic
The Task 910 "found, not fixed" view-id-sharing bug is real: confirmed `BgfxRenderTargetCubeBackend`
allocates exactly one `viewId_` in its constructor (`Detail::AllocateRtViewId()`, line 804), shared by
all 6 calls to `BindAsRenderTargetFace(face)` (845-866) regardless of which face — matching the header's
claim that "all 6 faces share view id [N]." The test's own dummy-read workaround
(`device.GetBackBufferData(&dummyReg, &dummy, 0, 1)`, lines 142-144, once per face inside the Phase 1
loop) was independently confirmed to actually force a `bgfx::frame()` call: `ReadBackbuffer()`
(`BgfxGraphicsBackend.cpp:303-325`) calls `bgfx::frame()` at line 321 before its screenshot-callback
wait. This is a genuine, verified-not-just-claimed workaround, not a placebo.

### C++ correctness
No ownership issues — `rtc_`/`whiteTex_`/`sb_`/`blueTex_` are all `unique_ptr` members created once in
`Initialize()` and outlive the single `Draw()` call that uses them.

### Robustness
`SetRenderTargetCubeFace()`'s override (`BgfxGraphicsBackend.cpp:887-899`) was independently confirmed
to set `currentRtWidth_`/`currentRtHeight_` from `rt->GetSize()` (897-898) — the exact fix the header
comment (lines 25-31) describes as closing "the identical bug shape Task 901 already fixed for 2D
RenderTarget2D." Without it, `EnsureViewState()`'s viewport-sizing logic (verified separately in this
batch's `bgfx_rendertarget2d_msaa_test.cpp` report) would fall back to the full window size for any
`SpriteBatch` draw into a cube face — this file's Phase 1 fill loop uses exactly that path
(`sb_->Draw(*blueTex_, ...)`, line 132), so this fix is a genuine hard prerequisite, not incidental.

### Testing
Deliberately narrow scope (level-0 content + no-crash only, not per-mip-level content) is explicitly
disclosed and justified by cross-reference to the Vulkan port's own prior abandoned attempt — an honest,
appropriately-scoped test rather than an overclaimed one.

## Detailed Findings

No MEDIUM/HIGH/CRITICAL findings. One LOW/INFO observation:

### F1 — No explicit `GraphicsDeviceManager`; relies on `Game`'s default-constructed device (800×480)
- Severity: LOW
- Confidence: HIGH
- Category: maintainability / consistency
- Location/symbol: `class BgfxRenderTargetCubeMipTest : public Game` (lines 80-207) — no `gdm_` member,
  no constructor
- Evidence: `Game::getGraphicsDeviceProperty()` (`Game.cpp:172-185`) falls back to the default-constructed
  `GraphicsDevice_` member (`Game.cpp:99`) whenever no `IGraphicsDeviceService` was registered, which in
  turn defaults to `GraphicsDeviceManager::DefaultBackBufferWidth/Height` = 800×480
  (`GraphicsDeviceManager.hpp:56-58`), rather than the small (32-320px) windows most sibling tests in
  this shard explicitly request via `GraphicsDeviceManager::setPreferredBackBufferWidthProperty()`.
- Why it matters: not a correctness bug — this file reads `device.getViewportProperty()` for its sample
  point (`W/2, H/2`, lines 112-114) rather than hardcoding a window size, so it is fully agnostic to
  whatever default applies. Confirmed via the EasyGL/Vulkan sibling ports
  (`easygl_skinnedeffect_combined_test.cpp`, `vulkan_rendertargetcube_mip_test.cpp`) that this is an
  established, intentional convention for this specific test family (both omit `GraphicsDeviceManager`
  too), not a Bgfx-specific oversight. Recorded here only because a window ~25× larger in area than
  the sibling MSAA/depth-format tests in this same shard is a minor, avoidable inefficiency for a CI
  test suite, not because it risks incorrect behavior.
- Suggested follow-up: none required; noted for completeness only.

## Cross-File Observations

- Shares the Task 910 "all 6 faces share one view id" limitation with
  `bgfx_rendertargetcube_msaa_test.cpp` (same shard, same workaround pattern, explicitly credited by
  that file's own header as originating here) — this audit verified the underlying shared-view-id
  architecture once (via this file) and treated the sibling file's identical claim as corroborated.
- `BindAsRenderTargetFace()`'s FBO-recreate-per-bind pattern (also flagged as the leading suspect for the
  separately-audited `bgfx_rendertargetcube_depthformat_test.cpp`'s Task 952 depth-attachment bug) is
  exercised by this file too, but only with `DepthFormat::None` — so this file's own Phase 1/Phase 2
  sequence would not surface that specific bug even if this test's methodology were extended to depth
  testing.

## Missing or Weak Tests

None beyond the explicitly-disclosed and justified per-mip-level content scope limitation already
covered above.

## Positive Findings

- The header comment's three-tier structure (mechanism fix / hard-prerequisite fix / found-but-not-fixed
  architectural gap) is a genuinely useful pattern for future readers, and every one of its three claims
  was independently verified against current source rather than taken on faith.
- The dummy-backbuffer-read-as-frame-boundary workaround is a real, verified mechanism (traced to
  `bgfx::frame()` inside `ReadBackbuffer()`), not just an assumption that "it probably helps."

## Final Assessment

A solid, appropriately-scoped mip-mechanism test whose own documentation of a deeper, deliberately
unfixed Task 910 view-id-sharing limitation is accurate and independently corroborated by this audit.
No correctness defects found in the file itself.
