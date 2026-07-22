# REMEDIATION_INDEX.md — Fast Lookup

**104 remediation tasks + 15 accepted no-action items**, consolidated from 686 raw per-file findings
plus the audit's 6 synthesis documents. Full detail for every ID is in `MASTER_REMEDIATION_PLAN.md`.

## Counts

### By priority

| Priority | Count | Meaning |
|---|---|---|
| **P0** | **11** | Security, memory corruption, UB, and the two build/test blockers |
| **P1** | **21** | Test/CI reliability, HIGH correctness, broad cross-backend defects, major FNA divergences |
| **P2** | **44** | MEDIUM correctness, missing backend features, lifecycle, API surface |
| **P3** | **28** | LOW, performance, maintainability, non-urgent architecture |

### By audit severity

| Severity | Count | Note |
|---|---|---|
| **CRITICAL** | **1** | `REMED-CONTENT-001` — the only CRITICAL in the entire audit |
| **HIGH** | 31 | |
| **MEDIUM** | 55 | |
| **LOW** | 17 | |

Severity and priority deliberately diverge in 9 tasks. The clearest cases: `REMED-BUILD-001` and
`REMED-BUILD-002` are HIGH but scheduled **P0** for leverage; `REMED-CONTENT-006` is MEDIUM but
scheduled **P0** because it is a reachable stack-exhaustion DoS; `REMED-GFX-031`/`-032`/`-048`/`-050`
are MEDIUM but scheduled **P3** because they are isolated and low-blast-radius.

### By owner lane

| Owner | Tasks | Note |
|---|---|---|
| **GRAPHICS** | 51 | Largest lane by far. Internally serialized for shader work — see `REMEDIATION_DEPENDENCIES.md`. |
| **BUILD_TEST_CI** | 19 | `BUILD` (9) + `TEST` (7) + `DOCS` (3) |
| **CORE** | 13 | |
| **NET** | 7 | Includes GamerServices |
| **CONTENT** | 5 | Includes Storage and XNB |
| **MEDIA** | 4 | |
| **DEVICES** | 3 | |
| **AUDIO** | 2 | Lane barely justified — see below |
| **INPUT** | 0 | **Lane not created** — see below |

**On the AUDIO and INPUT lanes.** The audit swept `Microsoft.Xna.Framework.Audio` (31 files, described
as "the most thoroughly self-audited subsystem encountered") and `Input` (44 files, plus a full
member-level xn65 cross-check that found zero discrepancies across `Buttons`' 32 bit values, `Keys`'
160 entries, and every dead-zone/clamp formula). Between them they produced **two LOW findings and one
tagging nit.** Standing up dedicated lanes for these would be ceremony, not capacity. `AUDIO` is kept
as an ID namespace for its two tasks; `INPUT` is not created at all, and its single finding is folded
into `REMED-CORE-012`. This is a real result about the codebase, not an omission.

## P0 — do these first

| ID | Title | Sev | Owner | PS |
|---|---|---|---|---|
| `REMED-BUILD-001` | `gtest_discover_tests` missing `WORKING_DIRECTORY` (~220 tests) | HIGH | BUILD | YES |
| `REMED-BUILD-002` | `cna_demo_xact` POST_BUILD copy aborts every build | HIGH | BUILD | YES |
| `REMED-CONTENT-001` | Malformed Texture2D `.xnb` crashes Vulkan + WebGPU | **CRITICAL** | CONTENT | COND |
| `REMED-CONTENT-002` | `fs::path` containment bypass — 3 sites, 1 root cause | HIGH | CONTENT | COND |
| `REMED-CONTENT-003` | TextureCube reader missing byte-count validation (OOB read) | HIGH | CONTENT | YES |
| `REMED-CONTENT-006` | `XnbTypeName` unbounded recursion + 2 dead `XnbReadLimits` | MEDIUM | CONTENT | YES |
| `REMED-GFX-001` | EasyGL `RegisterForWindow` dangling registry (UAF) | HIGH | GRAPHICS | COND |
| `REMED-GFX-002` | `SpriteFont`/`SpriteBatch` `end()` iterator deref (UB) | HIGH | GRAPHICS | COND |
| `REMED-GFX-003` | `SpriteEffects` undersized table (OOB stack read) | HIGH | GRAPHICS | NO |
| `REMED-NET-001` | ENet host-only broadcasts accepted from any peer | HIGH | NET | COND |
| `REMED-DEVICES-001` | `FileDialog`/`MessageBox` mutex-scoping UAF | HIGH | DEVICES | YES |
| `REMED-MEDIA-001` | `AudioTagParser` integer-overflow bounds checks (32-bit) | HIGH | MEDIA | YES |

## P1

| ID | Title | Sev | Owner | PS |
|---|---|---|---|---|
| `REMED-BUILD-003` | `WILL_FAIL` never adopted project-wide | MEDIUM | BUILD | NO |
| `REMED-BUILD-004` | CI label filters never run the general suite | HIGH | BUILD | YES |
| `REMED-TEST-001` | 3 test files assert confirmed defects as correct | HIGH | TEST | NO |
| `REMED-CORE-001` | `Logger::ToSDLPriority()` mistags Fatal/Error/Warn | HIGH | CORE | YES |
| `REMED-CORE-004` | `Color::PackFromVector4()` unclamped cast (UB) | MEDIUM | CORE | YES |
| `REMED-CORE-006` | `Game::UnloadContent()` dead hook | HIGH | CORE | COND |
| `REMED-CORE-007` | `GraphicsDeviceManager` device-event forwarding gap | HIGH | CORE | COND |
| `REMED-CONTENT-004` | Texture3D reader round-trip returns zeros | MEDIUM | CONTENT | YES |
| `REMED-GFX-004` | `RenderTargetCube` missing `Dispose(bool)` (UAF risk) | MEDIUM | GRAPHICS | YES |
| `REMED-GFX-005` | **Fog formula mirrored — Bgfx, Vulkan, all 15 D3DCommon shaders** | HIGH | GRAPHICS | NO |
| `REMED-GFX-006` | **SkinnedEffect world-normal transform missing on every backend** | HIGH | GRAPHICS | NO |
| `REMED-GFX-007` | EnvironmentMapEffect emissive re-multiply (5 groups) | HIGH | GRAPHICS | NO |
| `REMED-GFX-008` | SkinnedEffect Ambient/Emissive misconsumed (Vulkan, D3D11/12) | HIGH | GRAPHICS | NO |
| `REMED-GFX-009` | SdlGpu fog entirely unimplemented | HIGH | GRAPHICS | NO |
| `REMED-GFX-011` | Vulkan Y-flip missing in 4 effect families | HIGH | GRAPHICS | NO |
| `REMED-GFX-012` | Vulkan `SpriteBatch` transform dropped | HIGH | GRAPHICS | YES |
| `REMED-GFX-058` | Vulkan test shard centre-pixel-only (47 of 71) — structurally blind to mirrors | MEDIUM | GRAPHICS | YES |
| `REMED-GFX-013` | Vulkan scissor inert when RT bound — **DONE** (per-draw scissor capture) | HIGH | GRAPHICS | COND |
| `REMED-GFX-062` | Vulkan `Viewport` full-RT-hardcoded in RT passes — **DONE** (per-draw viewport capture) | MEDIUM | GRAPHICS | COND |
| `REMED-GFX-063` | Bgfx custom `Viewport` ignored in RT passes — **DONE** (setViewRect on RT views) | MEDIUM | GRAPHICS | COND |
| `REMED-GFX-065` | Bgfx per-view rect can't represent 2+ viewports on one view in a frame (affects backbuffer too) | LOW | GRAPHICS | NEW |
| `REMED-GFX-066` | Bgfx per-draw `bgfx::setScissor` on FBO/render-target views — **NOT A DEFECT** (false finding; RT scissor clips correctly on bgfx-GL + Vulkan; regression added) | MEDIUM | GRAPHICS | DONE |
| `REMED-GFX-067` | Bgfx RenderTarget2D sampled back through `SpriteBatch` reads **vertically mirrored** — the "size dependency" was FALSE (uniform across all sizes; earlier flaky size-correlated observations were Y-blind/position-only content + first-read screenshot-pipeline latency). Root cause: on originBottomLeft renderers (OpenGL) an FBO color attachment stores its texels **bottom-up**, so sampling with the ordinary top-down V mirrors it; ordinary Texture2D (top-down) and the backbuffer readback (screenshot `_yflip`) are unaffected; bgfx-Vulkan (originBottomLeft=false) already correct. **DONE** — `SubmitSprite` flips the sampled V of render-target sources when `caps->originBottomLeft` (new `IBgfxSamplable::IsRenderTargetColorSource()`); `Bgfx_RenderTarget_Orientation` 72/72 on bgfx-OpenGL AND bgfx-Vulkan, GFX-063/066 stay green, zero shader diff. Spawned **GFX-078** | MEDIUM | GRAPHICS | DONE |
| `REMED-GFX-078` | Bgfx generic 3D-effect texture binding (`DrawPrimitivesEx*`/`FillGpuDrawParams` path) uses `static_cast<const BgfxTextureBackend&>(*params.textureN)` on every effect sampler slot (texture0/1, PBR normal/MR/emissive/occlusion, dual-texture, env-map 2D). A `RenderTarget2D` set as an effect texture (`effect.Texture = renderTarget`) is a `BgfxRenderTargetBackend` — an unrelated sibling of `BgfxTextureBackend` — so this static_cast is **UB** reading the wrong pooled handle (the exact bug class Task 873 fixed for `SpriteBatch` and Task 907 for cubes, never fixed for the 3D 2D-texture path). Means the ONLY working RT-sample consumer on Bgfx is `SpriteBatch` (why GFX-067's fix is scoped there). Surfaced by GFX-067 Phase 27. **DONE** — new `BindSamplerSlot()` resolves every slot's handle through `IBgfxSamplable` (all 27 casts removed) and records render-target color sources per slot into `u_rtFlipV`; the 10 2D-sampling fragment shaders V-flip flagged slots only when `caps->originBottomLeft` (generic-effect counterpart of GFX-067's SpriteBatch flip, per-slot so DualTextureEffect's two layers flip independently). UB proven by `-fsanitize=vptr` on the exact cast shape; `Bgfx_RenderTarget_EffectTexture` 13/32 → 32/32 on bgfx-OpenGL AND bgfx-Vulkan; ordinary Texture2D byte-identical; GFX-067/063/066 + all effect families green; 40 regenerated shader arrays (10 fs × 4 profiles), all else byte-identical. Cube path already safe (Task 907). | MEDIUM | GRAPHICS | DONE |
| `REMED-GFX-064` | SdlGpu + D3D12 never wire `GraphicsDevice.Viewport` (base no-op `SetViewport`; full-target hardcoded) — **DONE** (SdlGpu per-draw capture; D3D12 store+consume) | MEDIUM | GRAPHICS | DONE |
| `REMED-GFX-068` | SdlGpu per-pass scissor reset to full-**backbuffer** on RT unbind (deferred-model, analog of Vulkan GFX-013) — **DONE** (per-draw scissor capture, mirrors GFX-064 viewport) | LOW | GRAPHICS | DONE |
| `REMED-GFX-069` | SdlGpu never applies `BlendState.BlendFactor` (constant blend color): pipeline maps `Blend.BlendFactor`/`InverseBlendFactor`→`SDL_GPU_BLENDFACTOR_CONSTANT_COLOR` but no `SetBlendFactor` override / `SDL_SetGPUBlendConstants` call, so constant-color blends used SDL's driver-default constant (observed `(1,1,1,1)`/white on llvmpipe) — **DONE** (per-draw BlendFactor capture, mirrors GFX-064 viewport / GFX-068 scissor; `0/8 → 8/8`) | LOW | GRAPHICS | DONE |
| `REMED-GFX-070` | Vulkan applies `vkCmdSetBlendConstants` only ONCE per frame (at backbuffer-pass begin, from the live member) although pipelines declare `VK_DYNAMIC_STATE_BLEND_CONSTANTS`: RT-pass constant-color blends get an undefined/stale constant (also a real `VUID-vkCmdDraw-None-07835` for blend-enabled RT draws) and multiple `BlendFactor` values in one frame collapse to last-wins — the Vulkan analog of GFX-069 (viewport/scissor were made per-draw by GFX-062/013 but blend constants were not). **DONE** (per-draw/per-batch BlendFactor capture in `Pending3DDraw`/`BatchSnapshot`, replayed via `vkCmdSetBlendConstants` in `draw3DFor`/`drawSpritesFor`; `0/8 → 7/7`, zero validation errors) | LOW | GRAPHICS | DONE |
| `REMED-GFX-071` | Vulkan 2D **sprite** pipeline (`GetOrCreatePipeline2D`/`2DMsaa`) hardcodes `SRC_ALPHA`/`ONE_MINUS_SRC_ALPHA` and takes no blend params, so `SpriteBatch` **ignores the custom BlendState entirely** — `Additive`/`NonPremultiplied`/`BlendFactor`/separate factors/functions are all silent no-ops for sprites (the 3D path was fixed by Task 868 and honors them). Discovered during GFX-070 Phase 16: the per-batch blend-constant capture is correctly wired but inert because the sprite pipeline never selects `CONSTANT_COLOR`. Separate/broader than the blend constant. FIXED (`2db09690`): batch's BlendState captured by value into `BatchSnapshot`, plumbed into the 2D pipeline cache key + `FillBlendAttachmentState`; GFX-070 constant now live for sprites; `Vulkan_SpriteBatch_BlendState` 2/17→17/17. | MEDIUM | GRAPHICS | DONE |
| `REMED-GFX-016` | EasyGL MRT second attachment never drawn | HIGH | GRAPHICS | NO |
| `REMED-GFX-017` | Bgfx default cull mode culls nothing | HIGH | GRAPHICS | YES |
| `REMED-GFX-018` | Bgfx `Clear` ignores `ClearOptions` | HIGH | GRAPHICS | YES |
| `REMED-GFX-019` | WebGPU `SpriteBatch` clip space backbuffer-relative: `QueueSprite()` baked NDC from `ComputeLogicalViewport()`+`physicalWidth_/physicalHeight_` with no bound-render-target branch, so `SpriteBatch.Draw()` into a differently-sized RenderTarget2D/cube face mis-mapped its destination rectangle (half-scaled toward the origin for a 2×-wider backbuffer) — the only backbuffer-hardcoded backend of 6. **DONE** (identity-viewport `currentRenderTarget_`/cube-face branch using the target's own size, mirrors SdlGpu; `WebGPU_SpriteBatch_RenderTarget` `12/23 → 23/23`, EasyGL parity 6/6, zero validation errors, zero shader diff). Spawned **GFX-072** | HIGH | GRAPHICS | DONE |
| `REMED-GFX-072` | SpriteBatch clip space ignored a custom `GraphicsDevice.Viewport` sub-region: the sprite NDC/ortho divided by the full target, so a custom sub-viewport **squished** sprites (WebGPU/Vulkan/SdlGpu/D3D12) or was **ignored** (EasyGL/Bgfx). XNA/FNA build the SpriteBatch ortho from `Viewport.Width/Height` (viewport-local; D3D11/D3D9 already correct via live viewport). **DONE** — fixed all 6 affected backends (7 SpriteBatch backends total; D3D12 found as a 7th by the survey); every backend + the unmodified D3D11 oracle now render the canonical scene to the byte-identical footprint `x[24,40] y[15,25] n=187`. Runtime-verified Vulkan/SdlGpu/EasyGL/Bgfx/WebGPU (Xvfb :99) + D3D11 oracle (Wine+DXVK); D3D12 build-verified. Spawned **GFX-073**, **GFX-074** | MEDIUM | GRAPHICS | DONE |
| `REMED-GFX-073` | Software (CPU-raster) backend SpriteBatch ignored `GraphicsDevice.Viewport`: sprites rasterized at raw framebuffer pixels and `SetViewport` was a no-op, so a custom viewport neither positioned nor clipped 2D. Not the GFX-072 mis-scale (Software never divides by a target size). **DONE** — `SetViewport` now stores the viewport; SpriteBatch adds the viewport origin after `transformMatrix` (viewport-local composition) and clips to the viewport rect via a new `RasterClipRect` on `RasterizeTriangleShaded`; the 3D path passes the full-framebuffer clip so it stays byte-identical, and the default full viewport is byte-identical. New `Software_SpriteBatch_CustomViewport` `5/17 → 17/17`, canonical scene renders to the GFX-072 byte-identical footprint `x[24,40] y[15,25] n=187`; full Software shard 7/7; UBSan clean; zero shader diff. Correcting the original note: the Software **3D** path does NOT consume a custom viewport either (it maps NDC over the full framebuffer via `GetViewportSize`, no X/Y offset / W/H sub-scale / clip / depth-range) — spawned **GFX-079**. `SetScissorRect` is also a no-op — spawned **GFX-080**. test `d7521c8f`, fix `280f7bdd` | LOW | GRAPHICS | DONE |
| `REMED-GFX-079` | Software (CPU-raster) backend **3D** path (`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`) ignores a custom `GraphicsDevice.Viewport`: `ClipVertexToRasterVertex` receives `GetViewportSize()` (= the full current framebuffer, NOT the stored viewport) and applies no `Viewport.X/Y` offset, no `Width/Height` sub-scaling, no viewport clip, and no `MinDepth/MaxDepth` depth-range remap. Correct for the default full-target viewport; a custom sub-viewport is silently ignored. Sibling of GFX-073 (2D); its stored-viewport state can be reused. Surfaced + source-verified during GFX-073; NOT fixed (separable, own transform + clip + depth-range work) | LOW | GRAPHICS | NEW |
| `REMED-GFX-080` | Software (CPU-raster) backend `SoftwareGraphicsBackend::SetScissorRect(...)` is a no-op (`{}`), so `GraphicsDevice.ScissorRectangle` (with `RasterizerState.ScissorTestEnable`) never clips either the 2D sprite or the 3D raster path — the whole target is always writable. Independent of GFX-073's viewport clip (a separate raster-state gap); a centralized `effective clip = target ∩ Viewport ∩ Scissor` could fold it in. Surfaced + source-verified during GFX-073; NOT fixed | LOW | GRAPHICS | NEW |
| `REMED-GFX-074` | Vulkan deferred RenderTarget2D lifetime/flush bug (generic to the target identity, not SpriteBatch-only): deferred work into an RT is only replayed at Present, so `RenderTarget2D::GetData` before Present returned all-zeros (backend never overrode `GetData`), and destroying the RT before Present left a dangling `VulkanRTSource*` in `activeBatches_`/`pending3D_`/`clearedRTs_` → use-after-free at the next Present. **DONE** — purge-on-destroy (`PurgeDeferredWorkForTarget`, 2D + cube face proxies) + a target-scoped `RenderTargetsOnly` flush driving `RenderTarget2D::GetData` (matches WebGPU/SdlGpu's flush-inside-GetData contract). ASan: pre-fix UAF reproduced at `RecordCommandBuffer`, post-fix clean; zero VUIDs; new `Vulkan_RenderTarget_GetDataLifetime` 13/13, GFX-072 RT variant 8/8; no shader diff. Spawned **GFX-075** | MEDIUM | GRAPHICS | DONE |
| `REMED-GFX-075` | Vulkan deferred-resource lifetime (same UAF class as GFX-074, different reference path): destroying a Texture2D/TextureCube/RT-as-sampler, a custom `Effect`, or an `OcclusionQuery` after queuing a deferred draw but before Present dangles the `VkImageView` baked into the captured descriptor set / the effect pointer+`VkPipeline` / the query pointer — none covered by GFX-074's destination-keyed purge, and those backends do no equivalent purge. Vertex/index/instance data is safe (copied at enqueue). **DONE** — generic **frame-fence-tagged retirement queue** (`RetireResources`/`ProcessRetiredResources`): destructors retire handles tagged with `frameGeneration_` (no per-destroy `vkDeviceWaitIdle`), freed only after the consuming frame's fence signals (`gen+MaxFramesInFlight<frameGeneration_`) — covers CPU-record + GPU-exec windows. Effect captured **by value** into `BatchSnapshot` at End(); OcclusionQuery detached via `PurgeDeferredQuery` (draw preserved); MRT proxy retired as an object; `texSamplerDescSets_` evicted per dying view. Previously-issued draws never dropped. ASan: pre-fix dangling-imageView VUID + SEGV reproduced, post-fix clean; new `Vulkan_DeferredResourceLifetime` 8/8, GFX-074 13/13, full Vulkan ctest 5772 passed (5 pre-existing/env); no shader diff. Spawned **GFX-076** | MEDIUM | GRAPHICS | DONE |
| `REMED-GFX-076` | Vulkan per-frame effect descriptor-set caches (`dualTex/envMap/litTextured/fogTex3D/skinned/pbr/pbrSkinned DescSets_`) key on a hash of raw `VkImageView` pointers and persist across frames with no free path until teardown. GFX-075's retirement queue fixes the UAF (a dying view stays alive through the deferred-consume window), but once that view is finally freed its handle value can be reused by a new texture whose hash collides → a stale cached descriptor set samples the wrong (destroyed) image. Unlike `texSamplerDescSets_` (now evicted per-view), these are hash-keyed and clearing them mid-run leaks their pool sets. Fix: rekey/generation-stamp or add a per-view eviction+free path. Surfaced by the GFX-075 Phase-29 sweep; source-identified. **DONE** — chose the **per-view eviction+free path** (mirrors `texSamplerDescSets_`, not a rekey): each entry now records the sampled `VkImageView`s it was written against, so `EvictSampledViewFromCaches` drops (and fence-retires to its own pool) every effect-cache entry a dying view participates in — closing the reuse-alias window AND bounding memory. Effect pools gained `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` + a per-pool fenced free path; the two cube destructors (`TextureCube`/`RenderTargetCube`), which evicted from NO cache before, now evict their cube view. Already-issued deferred draws untouched (set captured by value + retirement fence). New `Vulkan_EffectDescriptorCacheIdentity` **14/14** (pre-fix **7/14**: entries survive their destroyed resource + cache grows 15→79 over 64 temp textures), ASan clean (leaks identical to GFX-075 baseline), validation clean; GFX-075 8/8, GFX-074 13/13. No shader diff. test `ad02dcc8`, fix `6c3b0a48` | LOW | GRAPHICS | DONE |
| `REMED-GFX-077` | `BlendState.ColorWriteChannels` (and `ColorWriteChannels1/2/3`) + `MultiSampleMask` are silent no-ops on **every** backend: `IGraphicsBackend::ApplyBlendState` takes only the 6 factor/function values, so the colour-attachment `colorWriteMask` is hardcoded RGBA in both the 2D sprite and 3D pipelines (Vulkan and, by inspection, the others). Per-channel write masking cannot work anywhere. Surfaced by the GFX-071 Phase-29 sweep. Fix needs an `ApplyBlendState` signature change (touches ~12 backends) — deferred until an interface change is in scope. Source-identified, NOT fixed. | MEDIUM | GRAPHICS | NEW |
| `REMED-GFX-022` | `EffectParameter` Matrix semantics inverted | HIGH | GRAPHICS | COND |
| `REMED-GFX-043` | `DrawUserPrimitives` declaration never reaches backend | HIGH | GRAPHICS | COND |
| `REMED-NET-002` | `NetworkSessionProperties` unchecked iterator arithmetic | MEDIUM | NET | YES |
| `REMED-MEDIA-002` | `MediaLibrary` object-graph SEGFAULT (6+ backends) | HIGH | MEDIA | YES |

## P2 (44) and P3 (28)

Listed in `MASTER_REMEDIATION_PLAN.md` in ID order under their priority headings. Rather than
duplicating them here, the useful cross-cuts:

### Tasks requiring verification before implementation (11)

These rest on static analysis that was never executed, or on an unresolved question. **Reproduce
first. A finding that fails to reproduce is a valid, recordable outcome — record it, do not force a fix.**

`REMED-CONTENT-004` · `REMED-CONTENT-006` · `REMED-MEDIA-001` · `REMED-MEDIA-003` ·
`REMED-GFX-017` · `REMED-GFX-020` · `REMED-GFX-021` · `REMED-GFX-036` · `REMED-GFX-041` ·
`REMED-GFX-051` · `REMED-CORE-010` · `REMED-TEST-003` · `REMED-TEST-007` · `REMED-GFX-043`

### Tasks needing a project-owner decision before implementation (4)

| ID | Decision required |
|---|---|
| `REMED-GFX-035` | Which GLSL dialect is the contract: auto-upgrade in SdlGpu, document a stricter requirement, or fix only the fixtures? |
| `REMED-CORE-011` | Implement `CNA::Runtime` or delete it? |
| `REMED-BUILD-007` | Licensing question: is `CNA::Internal::Net`'s MIT deliberate? |
| `REMED-BUILD-002` | Does `XactFileGen.hpp` already generate the demo content, making the copy step obsolete? |

### Security-impacting tasks (12)

`REMED-CONTENT-001` (crash-DoS, 2 backends) · `REMED-CONTENT-002` (path traversal: **data loss** +
arbitrary read) · `REMED-CONTENT-003` (OOB heap read / memory disclosure) · `REMED-CONTENT-006`
(stack-exhaustion DoS) · `REMED-NET-001` (remote forgery, no MITM) · `REMED-NET-003` (roster DoS) ·
`REMED-GFX-001` (UAF) · `REMED-GFX-002` (UB) · `REMED-GFX-003` (OOB read) · `REMED-DEVICES-001` (UAF) ·
`REMED-MEDIA-001` (OOB read, 32-bit) · `REMED-GFX-004` (UAF risk)

Indirect: `REMED-BUILD-001` and `REMED-BUILD-004` — both adversarial-input fuzz harnesses
(`XnbContainerFuzzTest`, `LzxDecoderFuzzTest`) are in the currently-unrun set.

### Memory/resource-safety tasks (11)

`REMED-GFX-001` · `REMED-DEVICES-001` · `REMED-CONTENT-001` (confirmed **stack corruption**) ·
`REMED-CONTENT-003` · `REMED-CONTENT-006` · `REMED-MEDIA-001` · `REMED-MEDIA-002` · `REMED-GFX-004` ·
`REMED-GFX-028` · `REMED-GFX-029` · `REMED-DEVICES-002` · `REMED-NET-007`

## By affected backend

A backend's count is not a quality ranking — the most-tested backends surface the most defects. EasyGL
(218 test files) and Vulkan (70) are the two most heavily exercised, and unsurprisingly appear most.

| Backend | Tasks | Notable |
|---|---|---|
| **Vulkan** | 9 | Widest single-backend defect count: Y-flip (4 families), SpriteBatch transform, RT scissor, ambient/emissive, fog, skinned normal, + the Texture2D crash |
| **D3D11 / D3D12** | 8 | Mostly via shared `D3DCommon` — one fix closes both. D3D12 additionally: stencil/scissor inert, occlusion overwrite, 1-CTest coverage gap |
| **EasyGL** | 6 | Default Linux/Emscripten backend. UAF, MRT, skinned normal, PBR normal variant |
| **Bgfx** | 7 | Cull mode, ClearOptions, fog, skinned normal, env-map, 2 unannotated failing tests |
| **SdlGpu** | 7 | Fog absent entirely, GLSL dialect, constructor leak, skinned normal, env-map, cube mips, depth bias |
| **WebGPU** | 4 | Texture2D crash (non-catchable panic), SpriteBatch clip space, skinned normal, env-map |
| **D3D9** | 3 (+`GFX-060` post-audit) | Object-space fog in custom shaders, PBR skinned normal. Vendored stock effects immune by construction. **`REMED-GFX-060`** (DONE): draw paths dropped `DrawPrimitives`/`DrawIndexedPrimitives` vertex offsets (15 sites, the D3D9 counterpart of GFX-020) — runtime-verified on Wine+DXVK9 |
| **SdlRenderer** | 3 | Fullscreen crash, stale test expectations, depth-decision ambiguity |
| **Software** | 3 | Depth write/function inert, rotation formula (shared with Dx3), Texture3D |
| **Dx3** | 2 | Rotation defect, resize destroys-before-replace |
| **Headless** | 3 | primitiveCount, Texture3D, WireFrame capability |
| **Ascii / Canvas** | 0 | No MEDIUM+ findings. Both confirmed clean of the `RegisterForWindow` bug |
| **ALL / shared** | 30+ | XNA-facing layer and `IGraphicsBackend` — the highest-leverage fixes |

## By audit source document

| Source | Tasks traced |
|---|---|
| `AUDIT_FINDINGS_INDEX.md` | 71 |
| `AUDIT_CROSS_CUTTING_FINDINGS.md` | 78 |
| `AUDIT_GRAPHICS_BACKEND_MATRIX.md` | 22 |
| `AUDIT_FINAL_REPORT.md` | 19 |
| Per-file `*.audit.md` reports only (**not in any synthesis doc**) | **3** |

Those last three matter: `REMED-CONTENT-006` (stack-exhaustion DoS + two dead security controls),
`REMED-GFX-043` (`DrawUserPrimitives` declaration dropped), and part of `REMED-TEST-005` were
recovered **only** by the exhaustive per-file sweep run while building this plan. They are a gap in
the audit's synthesis layer, not in its per-file work — the evidence was correctly recorded, it just
never propagated upward. See `REMEDIATION_TRACEABILITY.md` § Synthesis gap.
