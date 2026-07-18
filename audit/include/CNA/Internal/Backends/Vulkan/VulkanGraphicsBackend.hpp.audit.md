# Audit: include/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp`
- Audit status: AUDITED (scoped-depth review — see below; matches the standard already applied to this audit's
  other largest backend headers: EasyGL, WebGPU, D3D11, D3D12, SdlGpu, Bgfx)
- Subsystem: `backend-vulkan` shard
- File type: C++ header, 1491 lines
- Related implementation: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` (same shard, 8954 lines —
  **the single largest file in this entire audit**, larger than WebGPU's own previously-largest 8805 lines)
- XNA/FNA relevance: implements the full `IGraphicsBackend` contract via native Vulkan (SPIR-V shaders)
- Graphics backend relevance: this is the backend's central class family
- Main related tests: `examples-tests-vulkan` (already audited via mechanical batch earlier this session)

## Purpose

Declares the full Vulkan backend class family: `VulkanGraphicsBackend` itself (device/swapchain/render-pass
lifecycle, `Apply*State`, draw dispatch, pipeline/render-pass caching), plus its resource-backend satellites
(`VulkanTextureBackend`, `VulkanRenderTargetBackend`, `VulkanRenderTargetCubeBackend`, `VulkanTexture3DBackend`,
`VulkanTextureCubeBackend`, `VulkanVertexBufferBackend`, `VulkanIndexBufferBackend`,
`VulkanOcclusionQueryBackend`, `VulkanEffectBackend`, `VulkanSpriteBatchBackend`, `VulkanMRTProxy`), and the
`BlendKeyParams`/`DepthStencilKeyParams`/`PipelineKeyHash` pipeline-cache-key infrastructure.

## Executive Verdict

**Needs attention, scoped-depth review.** Several already-known and newly-confirmed defects are visible at the
header/declaration level and independently verified in the paired `.cpp`: `VulkanSpriteBatchBackend` never
declares a `SetTransformMatrix()` override (confirmed no-op bug, inherits the base class's empty default);
`scissorEnabled_`/`scissorX_`/`scissorY_`/`scissorW_`/`scissorH_` members exist and are correctly populated by
`SetScissorRect()`/`ApplyRasterizerState()`, but (confirmed in the `.cpp`) are silently ignored whenever a
render target is bound. No `RegisterForWindow` call anywhere in this class family, confirming immunity from
the EasyGL-class dangling-window-registry-pointer bug (matching D3D11/D3D12/Bgfx's own confirmed absence).

## Checklist Results

### Confirmed, header-level: `SetTransformMatrix` no-op
`VulkanSpriteBatchBackend` (declared here) has no `SetTransformMatrix()` override anywhere in this header or
its `.cpp` — confirmed via exhaustive grep of the entire Vulkan backend directory (zero matches for the method
name outside `IGraphicsBackend.hpp`'s own base-class declaration, `virtual void SetTransformMatrix(const
Matrix& m) {}`). This is the only backend in this audit confirmed to have this exact gap — every other checked
backend (EasyGL, Bgfx, D3D9, D3D11, WebGPU, SdlGpu, SdlRenderer, Canvas, Dx3, Software, Headless, Ascii)
correctly overrides it. Already recorded in `AUDIT_FINDINGS_INDEX.md`/`AUDIT_CROSS_CUTTING_FINDINGS.md`; this
report formally confirms it from the Vulkan side's own header (previously inferred from D3D11's header comment
citing Vulkan).

### Confirmed clean: window-registry
No `RegisterForWindow` call anywhere in this header or the paired `.cpp` — cannot share the EasyGL-class
dangling-pointer bug, matching D3D11/D3D12/Bgfx's own confirmed absence.

### Architecture — `DepthStencilKeyParams`/`BlendKeyParams`/pipeline-key infrastructure
`DepthStencilKeyParams` (line 635) correctly bundles every field baked into a `VkPipeline` at creation time
(depthCompareOp, front/back `VkStencilOpState`) — stencil reference/compare-mask/write-mask are true Vulkan
dynamic state (`vkCmdSetStencil*`), correctly excluded from the cache key per the class's own accurate
comment. `BlendKeyParams` (line 612) mirrors this for the 6-value Blend/BlendFunction state. `PipelineKeyHash`
(line 627)'s comment explains the `std::pair<uint64_t, uint32_t>` split (blend state given its own uint32_t
half once the existing uint64_t key ran low on free bits) — a reasonable, well-documented evolution.

### Cross-File Observations — confirmed in the `.cpp`, declared here
`scissorEnabled_`/`scissorX_`/`scissorY_`/`scissorW_`/`scissorH_` (private members) are correctly written by
`SetScissorRect()`/`ApplyRasterizerState()`, but — confirmed by reading `RecordCommandBuffer()` in the `.cpp` —
are read ONLY for the backbuffer pass; every render-target pass hardcodes a full-target `VkRect2D`
unconditionally. See the `.cpp` report and `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full writeup. `SetViewport()`'s
own header comment (line ~7975 in the `.cpp`) honestly discloses the equivalent Viewport-when-RT-bound
limitation; no equivalent disclosure exists for Scissor.

## Detailed Findings

None new beyond what's captured above and in the `.cpp` report / `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Cross-File Observations

Declares accessors/state matching every already-audited shader file's own uniform/push-constant expectations
(`FillExtPushConst`, `FillInstancedPushConst`, `FillPbrUboData` all declared here, bodies verified in the
`.cpp`) — no mismatch found between this header's declared state and what the shaders actually consume.

## Missing or Weak Tests

No dedicated test found in this audit exercising `ScissorRectangle` together with a bound `RenderTarget2D`
on this or any backend (see `AUDIT_CROSS_CUTTING_FINDINGS.md`), nor a non-Identity `SpriteBatch` transform
matrix on Vulkan specifically.

## Positive Findings

Confirmed absence of the EasyGL-class window-registry bug; well-documented, genuinely necessary pipeline-cache
key infrastructure (`DepthStencilKeyParams`/`BlendKeyParams`) with accurate, non-stale comments; honest,
in-header disclosure of the Viewport-when-RT-bound architectural limitation (Scissor's own equivalent
limitation is not disclosed, see cross-cutting findings).

## Final Assessment

Confirms and formalizes 2 already-recorded cross-cutting defects (`SetTransformMatrix` no-op,
`SkinnedEffect` Ambient/Emissive gap's root-cause locus) and contributes 1 newly-confirmed defect (Scissor
silently ignored when a render target is bound) at the header/declaration level, all fully verified against
the paired `.cpp`.
