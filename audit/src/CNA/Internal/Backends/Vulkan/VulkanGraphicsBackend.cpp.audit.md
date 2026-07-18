# Audit: src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
- Audit status: AUDITED (scoped-depth review — see below; **the single largest file in this entire audit**,
  8954 lines, larger than WebGPU's own previously-largest 8805 lines; matches the scoped-depth standard already
  applied to EasyGL 4733, WebGPU 8805, D3D11 1846, D3D12 2331, SdlGpu 5105, Bgfx 3443 lines)
- Subsystem: `backend-vulkan` shard
- File type: C++ implementation, 8954 lines
- Related header: `include/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp` (same shard, see that report)
- XNA/FNA relevance: implements the entire Vulkan backend's device lifecycle, draw dispatch, and per-effect
  push-constant/UBO fill functions
- Graphics backend relevance: this is the central Vulkan backend class
- Main related tests: `examples-tests-vulkan` (98 files, already audited via mechanical batch earlier this
  session)

## Purpose

Implements Vulkan instance/device/swapchain/render-pass setup, `Apply*State`, `Clear*`, the deferred
`RecordCommandBuffer()` draw-recording pipeline (2-phase: RT passes then backbuffer pass), and every
`Fill*PushConst`/`Fill*Ubo` per-effect parameter fill function.

## Executive Verdict

**Needs attention, scoped-depth review.** This shard's direct source reading confirmed 2 previously-recorded
findings at the exact implementation site (`SetTransformMatrix` no-op — confirmed via exhaustive grep,
zero overrides anywhere; the `SkinnedEffect` Ambient/Emissive gap's root-cause locus — confirmed
`FillExtPushConst()` itself faithfully forwards whatever `GpuDrawParams` it's given, so the actual drop happens
upstream in `SkinnedEffect::FillGpuDrawParams()`, not in this file) and surfaced 1 new HIGH-severity defect
(Scissor silently non-functional whenever a render target is bound) plus formal confirmation, at the shader-fill
call-site level, that the missing-Y-flip bug affects 3 additional effect-shader families beyond the
already-known `EnvironmentMapEffect` instance (`PbrEffect`, `SkinnedPbrEffect`, `InstancedEffect` — see the
individual shader reports and `AUDIT_CROSS_CUTTING_FINDINGS.md`). The remaining ~8700 lines (full swapchain
recreation, MSAA resolve paths, texture/render-target resource classes' own implementations, device-lost
recovery) were not exhaustively traced line-by-line, consistent with this audit's scoped-depth standard for
files of this size.

## Checklist Results

### Confirmed, HIGH severity: Scissor silently non-functional when a render target is bound
`RecordCommandBuffer()`'s RT-pass loop (Phase 1, lines ~6709-6752) hardcodes `VkRect2D rtSc{ {0,0}, {rtW,
rtH} }` before every `vkCmdSetScissor` call, never reading `scissorEnabled_`/`scissorX_`/`scissorY_`/
`scissorW_`/`scissorH_` at all. The backbuffer pass (Phase 2, lines ~6790-6796), by contrast, correctly checks
`scissorEnabled_` and applies the real stored rect. `GraphicsDevice.ScissorRectangle` is therefore completely
inert whenever a `RenderTarget2D`/`RenderTargetCube` is bound, with **no disclosure anywhere near the scissor
code** — unlike the paired Viewport-when-RT-bound limitation, which `SetViewport()`'s own header comment
explicitly and honestly discloses ("RT passes stay hardcoded to each RT's own full size... cannot recover what
Viewport was active"). See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full writeup.

### Confirmed, header-level clean: window-registry
No `RegisterForWindow` call anywhere in this file (confirmed via grep) — matches D3D11/D3D12/Bgfx's own
confirmed absence; cannot share the EasyGL-class dangling-window-registry-pointer bug.

### Confirmed: `SetTransformMatrix` no-op
Exhaustive grep of the entire file for `SetTransformMatrix` returns zero matches — `VulkanSpriteBatchBackend`
never overrides it, silently inheriting the base class's empty default. Formally confirms the already-recorded
HIGH-severity finding (previously inferred from D3D11's own header comment citing this bug) directly from its
source location.

### Confirmed: `SkinnedEffect` Ambient/Emissive gap's root-cause locus
`FillExtPushConst()` (line 3575) copies `p.ambientColor` into the push constant unconditionally and correctly
— it does not itself drop the field. This confirms the actual bug is entirely upstream, in
`SkinnedEffect::FillGpuDrawParams()` (not yet audited as of this shard; tracked under the `xna-graphics` shard,
Task #4), which must be leaving `ambientColor` at its zero-initialized default for the skinned draw path
specifically. `emissiveColor` has no corresponding field in any of the 4 Vulkan skinned-shader UBOs at all
(confirmed via full read of `skinned3d`/`skinned3d_color`/`skinned3d_vertexlit`/
`skinned3d_vertexlit_color`'s `FogParams` blocks) — a structural absence, not just an unset value.

### Confirmed: missing-Y-flip bug affects 3 additional effect families (formal call-site confirmation)
`DrawPrimitivesEx()` (lines ~7355-7386) computes `wvp = world * view * projection` identically for every
non-alpha-test, non-env-map 3D draw (lit-textured/skinned/pbr all funnel through the same
`FillExtPushConst(d.pushConst, wvp, params)` call), and `FillInstancedPushConst()` (line 3619) computes
`vp = view * proj` the same way with no baked-in flip — confirming, at the C++ call-site level, that the
Y-flip is a pure per-shader convention with no compensating flip anywhere in the matrix-composition or
push-constant-fill code. This means the `pbr3d`/`pbr3d_skinned`/`instanced3d` shaders' own omission of the
flip (see their individual reports) is a genuine defect, not something silently corrected by the C++ side.

### DualTextureEffect formula
`FillInstancedPushConst`/`FillExtPushConst`/`FillPbrUboData` all read cleanly; `dual_texture3d.frag.glsl`'s
`tex1.rgb *= 2.0;` doubling (confirmed correct against FNA's own DualTextureEffect convention) is applied
entirely shader-side, nothing to check on the C++ fill side for it.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
No issues found in the areas read.

## Detailed Findings

1 new HIGH-severity finding (Scissor non-functional when RT-bound) plus formal confirmation of 3
already-recorded/newly-expanded cross-cutting defects at their exact implementation site.

## Cross-File Observations

The RT-pass vs. backbuffer-pass asymmetry (Scissor honored only for the latter) parallels, but is more severe
than, the already-disclosed Viewport-when-RT-bound limitation — same root architectural cause (a deferred,
per-frame-global draw-recording model that cannot recover per-RT state), but only one half of the pair is
disclosed in-code.

## Missing or Weak Tests

No dedicated test found in this audit exercising `ScissorRectangle` together with a bound `RenderTarget2D` on
any backend — this specific combination may be broadly under-tested project-wide, not unique to Vulkan's own
bug.

## Positive Findings

Confirmed absence of the EasyGL-class window-registry bug; `FillExtPushConst()`/`FillInstancedPushConst()`/
`FillPbrUboData()` are all internally consistent and correctly implement their documented layouts; the
backbuffer-pass Scissor/Viewport handling is itself correct, isolating the defect specifically to the
RT-bound case.

## Final Assessment

1 new HIGH-severity defect (Scissor non-functional when RT-bound, undisclosed) plus formal, source-level
confirmation of `SetTransformMatrix`'s no-op bug, the `SkinnedEffect` Ambient/Emissive gap's true root-cause
locus, and the missing-Y-flip bug's expansion to 3 additional effect families. The untraced ~8700 lines
(swapchain recreation, MSAA resolve, resource-class internals, device-lost recovery) remain a gap for a
future, more exhaustive pass.
