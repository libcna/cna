# WebGPU Backend Implementation Plan

> WebGPU is an authorized, experimental workstream. Its first goal, a **verified native 2D backend
> on Linux desktop**, was reached 2026-07-12 (`WEBGPU-124`–`WEBGPU-131`, all ✅ — see Phase 56.1).
> This does not mean feature parity with Vulkan, Android support, or browser WebGPU: those remain
> future work (Phase 57 onward), now open per `WEBGPU-131`'s own acceptance criteria — and, as of
> 2026-07-12, genuinely started: `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` (a
> `VertexPositionColor`, stride-16, unlit/untextured 3D draw with real depth testing) work
> end-to-end, verified by the `WebGPU_Colored3D` CTest. Full BasicEffect/texture/lighting dispatch
> (`WEBGPU-66`) and the other 8 WGSL shader variants remain open — see the Active execution order
> below.
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.

> **History:** this content originally lived inline in `plan_graphics.md` as Phases 56–69,
> numbered `501`–`661`, then renumbered to `10001`+ (2026-07-02) to free up the low task-ID
> range for further core-plan growth. Moved to this dedicated file and renumbered again to
> `WEBGPU-1`–`WEBGPU-123` (2026-07-07) to fully separate the parked WebGPU work from the active
> `plan_graphics.md` backlog — no task content changed in either move, only the numbering and
> file location.
>
> **Verified native 2D baseline (2026-07-12, `WEBGPU-124`–`WEBGPU-131`):** a clean Linux x86_64
> CMake configuration with an explicit `CNA_WEBGPU_ROOT` compiles `WebGPUGraphicsBackend.cpp`
> against the pinned `wgpu-native v29.0.1.1` headers, produces `libcna_backend_graphics_webgpu.a`,
> and copies the native runtime. `cna_demo_2d --smoke 120`/`--webgpu-2d-validation` and a second,
> independent application (`../mobile-eggbert`) both initialize, clear, upload/sample textures,
> draw animated multi-sprite `SpriteBatch` scenes with tint/alpha/rotation/flip/sampler variants,
> resize, and present cleanly on a host desktop session with no WebGPU validation error, device
> loss or loader failure. Automated (non-manual-screenshot) tests and GPU readback remain open —
> see `WEBGPU-88`–`99` below.
>
> **Platform scope until expanded by a completed task:** Linux desktop (X11 and Wayland), x86_64,
> using an explicit extracted `wgpu-native` package. Windows and macOS are code paths only, not
> validation claims. Android is blocked by the absence of an Android package/build route, and
> browser/Emscripten WebGPU is a separate future workstream.

## Active execution order — do this one task at a time

1. ~~`WEBGPU-126`~~ – ~~`WEBGPU-131`~~ — 2D baseline established 2026-07-12, all ✅ (Phase 56.1).
2. ~~`WEBGPU-88`~~ – ~~`WEBGPU-91`~~ — automated CTest coverage and reusable GPU readback, all ✅
   2026-07-12 (`WebGPU_Clear_Readback`).
3. `WEBGPU-92` — 🟨 partial (sampler address-mode pixel assertions still open — everything else is
   now covered, including partial-alpha blend since `WEBGPU-132`). `WEBGPU-99` — buffer disposal/
   `SetDataOptions`/vertex-format tests, not yet started.
4. 3D backlog (Phases 57–66) is now underway, all landed 2026-07-12:
   - First vertical slice — `WEBGPU-11`/`13`/`14` (UBO + bind group), `WEBGPU-19`
     (`colored3d.wgsl`), `WEBGPU-32` (pipeline, with genuine depth-test verification),
     `WEBGPU-64`/`65`/`67`/`69` (`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`, real draw
     dispatch) — `WebGPU_Colored3D` CTest, 4/4, git-stash-verified. Found+fixed a real, separate
     gap along the way: `ApplyDepthStencilState()` was entirely unimplemented (`GraphicsDevice.
     DepthStencilState` had zero effect on this backend), now handles the depth portion
     (`WEBGPU-39` partial).
   - `WEBGPU-66` (`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`, real `GpuDrawParams` dispatch) —
     stride-16 case only, reusing the same `colored3d.wgsl`/pipeline: a `BasicEffect`'s real
     `DiffuseColor`/`VertexColorEnabled` now reach the shader instead of the
     `DrawColoredPrimitives` fallback's hardcoded white/true. `WebGPU_DrawPrimitivesEx` CTest, 3/3,
     git-stash-verified.
   - `WEBGPU-15`/`17`/`20`/`33` (`textured3d.wgsl`, stride 20, second bind group for sampler+
     texture) and the matching `WEBGPU-66` dispatch extension — a real `Texture2D` bound through
     `BasicEffect.Texture` now samples correctly and `DiffuseColor` genuinely multiplies it (not
     just passed through unmultiplied). `WebGPU_Textured3D` CTest, 3/3, git-stash-verified.
   - Next: `WEBGPU-21`/`22` (`colored_textured3d.wgsl` stride 24, `lit_textured3d.wgsl` stride 32
     with real lighting) and their `WEBGPU-33` pipeline dispatch, or the remaining `WEBGPU-66` scope
     (alpha test/dual texture/env map/skinned dispatch).

For every task: use a clean build directory, pass `CNA_WEBGPU_ROOT` and
`CNA_WEBGPU_AUTO_DOWNLOAD=OFF`, record the exact command and result in the task note, and do not
mark it ✅ from source inspection alone.

---

## Phase 56 — WebGPU backend: infrastructure and CMake setup

> The native implementation is pinned to **wgpu-native v29.0.1.1** and uses WGSL. `CNA_WEBGPU_ROOT`
> is the supported reproducible input; automatic download is convenience-only until it has a
> checksum and CI/runtime coverage. Push constants are unavailable in WebGPU, so future 3D work
> must use uniform buffers.

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-1 | Add `CNA_GRAPHICS_BACKEND=WEBGPU` CMake option; locate headers + libs; define `CNA_BACKEND_WEBGPU` | 🟨 | Linux configure, backend archive creation and final executable linkage/runtime loading are all verified (`WEBGPU-124`/`WEBGPU-128`, and cross-repo via `WEBGPU-130`). Package integrity/checksums and non-Linux paths remain unverified. `vendor/wgpu-native` is not a portable vendored dependency. |
| WEBGPU-2 | Create `include/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.hpp` — class skeleton, all IGraphicsBackend sub-interfaces declared | 🟨 | Core backend, Texture2D, vertex/index buffers and SpriteBatch classes exist and are runtime-verified (`WEBGPU-124`–`WEBGPU-130`); remaining render-target/cube/3D/effect/query classes are still open (Phase 57 onward). |
| WEBGPU-3 | Create `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp` — initial functional baseline and explicit unsupported 3D paths | ✅ | Compiles against the pinned header (`WEBGPU-124`) and its 2D functional runtime baseline is verified end-to-end (`WEBGPU-125`–`WEBGPU-130`); explicit unsupported-3D throw paths (`ThrowUnsupported3DDraw`) remain until Phase 57 onward lands. |
| WEBGPU-4 | SDL3 surface creation | ✅ | X11/Wayland branches runtime-verified on Linux desktop (`WEBGPU-125`, `WEBGPU-127`'s resize/minimize/restore coverage, `WEBGPU-130`'s independent second application). Win32/Metal are code paths only, not validation claims; Android is not a supported build route. |
| WEBGPU-5 | Instance, adapter, device and queue initialization | ✅ | Runtime-verified 2026-07-12 (`WEBGPU-125`); callback/timeout behaviour hardened and verified by `WEBGPU-127`'s `AllowProcessEvents` polling fix. |
| WEBGPU-6 | Surface configuration, resize and backbuffer acquisition | ✅ | Runtime-verified: `WEBGPU-125` (baseline present/clear), `WEBGPU-127` (resize 800×600↔960×540, minimize/restore, zero-size handling). |
| WEBGPU-7 | Command encoding and queue submission per frame | ✅ | Runtime-verified by every `WEBGPU-125`–`130` run (120–180+ frames each, no WebGPU validation errors). |
| WEBGPU-8 | Main render pass with colour/depth/stencil attachments | 🟨 | Colour and depth attachments are both now runtime-verified (`WEBGPU-125`/`126`/`130` for colour; `WebGPU_Colored3D`'s depth-order checks for depth, 2026-07-12 — see `WEBGPU-32`). Stencil attachment remains unexercised by any real draw. |
| WEBGPU-9 | Colour/depth/stencil clear state | 🟨 | Colour and depth clear are both now runtime-verified (`WebGPU_Colored3D`'s `ClearOptions::Target\|DepthBuffer` calls, 2026-07-12). Stencil clear remains unexercised by any real draw. |
| WEBGPU-10 | Present and recoverable acquisition handling | ✅ | The v29 status API is handled, and both normal present and surface-loss/outdated recovery are runtime-verified (`WEBGPU-127`, `WEBGPU-130`). |

---

## Phase 56.1 — Recovery and verified native 2D vertical slice (active)

| # | Task | Status | Acceptance criteria |
| --- | --- | --- | --- |
| WEBGPU-124 | Align the backend with the pinned `wgpu-native v29.0.1.1` C API, beginning with surface-acquisition status handling. | ✅ | Verified 2026-07-12: fresh offline CMake configure, generated CMake compile of `WebGPUGraphicsBackend.cpp`, static backend archive and copied `libwgpu_native.so`; no compatibility aliases/workarounds. |
| WEBGPU-125 | Build and run a minimal native window that initializes the backend, clears, presents at least 60 frames, then exits cleanly. | ✅ | Verified 2026-07-12: clean offline build with `-DCNA_WEBGPU_ROOT=$PWD/vendor/wgpu-native -DCNA_WEBGPU_AUTO_DOWNLOAD=OFF`, then `timeout 60s ./cna_demo_2d --smoke 120` on a host desktop session. The run completed 120 frames in 2.10 s with exit code 0 and no uncaptured WebGPU error, device-loss report or loader failure. The earlier X11-close `XInput BadWindow` remains a lifecycle observation for `WEBGPU-127`, not a failure of this smoke gate. |
| WEBGPU-126 | Validate the existing 2D slice: texture upload, source rectangles, tint/alpha, rotation, flip, linear/point sampling, wrap/clamp/mirror, logical presentation and resize. | ✅ | Verified 2026-07-12 with `cna_demo_2d --webgpu-2d-validation --smoke 120`: it uploads `player.png`, submits deterministic source-rectangle/tint/alpha/rotation/both-flip draws, exercises Linear/Point + Clamp/Wrap/Mirror samplers (with UVs extending outside [0,1]), and resizes 800×600→960×540→800×600. The clean native run emitted no WebGPU error. Manual desktop screenshot review confirmed the expected crops, semi-transparent tinted rotation, both flips and sampler variants without artefacts. |
| WEBGPU-127 | Harden lifecycle and failure paths discovered in the first run: request timeout/polling strategy, surface loss/outdated recovery, zero-size windows and destruction order. | ✅ | Verified 2026-07-12: replaced unbounded spontaneous adapter/device callbacks with `AllowProcessEvents` polling and a 10 s timeout (the v29 package's `wgpuInstanceWaitAny` panics as unimplemented); minimized/nulled surfaces now unconfigure and release their depth attachment before restore/reconfigure. `cna_demo_2d --webgpu-2d-validation --smoke 180` exercised resize 800×600→960×540, minimization, restoration to 800×600, regular surface reconfiguration and teardown with exit 0 and no WebGPU error. |
| WEBGPU-128 | Make package discovery and runtime deployment reproducible for the verified Linux target. | ✅ | Verified 2026-07-12 with a clean `/tmp/cna-webgpu-128` configure using `CNA_WEBGPU_ROOT=$PWD/vendor/wgpu-native` and `CNA_WEBGPU_AUTO_DOWNLOAD=OFF`. The final `cna_demo_2d` copies `libwgpu_native.so` beside itself, records `NEEDED libwgpu_native.so` plus `$ORIGIN` first in RUNPATH, and `ldd` resolved the copied sibling runtime before a successful 120-frame run. Auto-download integrity/checksum policy and unvalidated platform packages remain deferred. |
| WEBGPU-129 | Add a backend-specific native smoke-test target and CTest registration that can run only when a display/GPU is available. | ✅ | Verified 2026-07-12: fresh WebGPU configure with `CNA_BUILD_TESTS=ON` registers `WebGPU_Native2D_Smoke`, a CTest wrapper around `cna_demo_2d --smoke 120`. It passed on the host desktop in 2.30 s; with `DISPLAY` and `WAYLAND_DISPLAY` removed it reported SKIPPED with a clear reason. |
| WEBGPU-130 | Integrate `../mobile-eggbert` as the first real desktop 2D application smoke test. | ✅ | Verified 2026-07-12 (commit `dcdb648` in `../mobile-eggbert`, pushed to origin/develop): its CMake targets `../cna_graphics`, keeps `SDL_RENDERER` as the desktop default (matching the repo's own last real committed default, `de40814`) with `WEBGPU` selectable only via explicit `-DCNA_GRAPHICS_BACKEND=WEBGPU`, recognizes `cna_backend_graphics_webgpu` in the GNU linker group, and copies `libwgpu_native.so` beside `WindowsPhoneSpeedyBlupi` with `$ORIGIN` first in RUNPATH. Also fixed an incidental, previously-disabled `worlds/` directory POST_BUILD copy step (referenced an undefined CMake variable) found while verifying this task — a genuinely fresh build directory would not have loaded any level otherwise. On a real desktop session (`DISPLAY=:0`, not the sandbox's Xvfb `:99` — `wgpu-native` needs a real GPU context): a clean WebGPU build ran, reached its main menu automatically ~5s after launch with pixel-correct `SpriteBatch` rendering (title, animated character, player-select panel, Setup gear, Play button), and an `xdotool`-driven click on the Play button correctly triggered the `InitPlay`→`StartMission(1)` sequence — an animated "constructing Blupi" cutscene with a filling progress bar and a cross-fade transition, all rendering correctly frame-by-frame with no WebGPU validation errors. That sequence fades back to the main menu afterward; an identical byte-for-byte-comparable run against a freshly built `EASYGL` binary reproduced the exact same fade-back behavior, confirming it is a pre-existing, backend-independent `Game1`/`InputPad` state-machine behavior in `../mobile-eggbert` itself (its `SetPhase`/fade-transition logic, `Game1.cpp` — see the comment block at the top of that file), not a WebGPU rendering regression. Out of scope to fix here (touches sibling-repo gameplay logic, not a `cna_graphics` graphics-backend concern) — flagged for whoever next works on `../mobile-eggbert`. No Android change was made. |
| WEBGPU-131 | Establish the native 2D test baseline before 3D work. | ✅ | `WEBGPU-124`–`WEBGPU-130` are all now ✅. Manual test evidence and known limitations are recorded in `docs/webgpu-backend.md` (revised 2026-07-12 alongside this task — see its "Verified native 2D baseline" and "Important limitations" sections). The native 2D SpriteBatch path (texture upload, source rects, tint/alpha/rotation/flips, Linear/Point + Clamp/Wrap/Mirror sampling, logical presentation/resize, lifecycle recovery) is verified on Linux desktop against both the synthetic `cna_demo_2d --webgpu-2d-validation` scene and a real, independently-built game (`../mobile-eggbert`). 3D/effects/render-targets/readback/MRT/browser remain unimplemented — Phase 57 onward, tracked below. |
| WEBGPU-132 | `SpriteBatch` partial alpha blending **FIXED** — real bug, found via `WEBGPU-91`/`92`'s new pixel-asserted readback test, not caught by `WEBGPU-126`'s manual screenshot review (a too-bright translucent overlay reads as "looks about right" by eye against a busy background — exactly why pixel assertions matter more than visual review). | ✅ | Verified 2026-07-12: the sprite pipeline's blend factors (`WGPUBlendFactor_One`/`OneMinusSrcAlpha`, a *premultiplied*-alpha equation) didn't match the sprite WGSL fragment shader's actual output (`textureSample(...) * input.color`, *straight*/non-premultiplied — same as Vulkan's `sprite2d.frag.glsl`), so any tint/texture alpha strictly between 0 and 255 was silently ignored for colour (a 50%-alpha sprite rendered at full brightness; only alpha=0 or alpha=255 ever looked correct, since both happen to be blend-equation-independent edge cases). **Fixed** by matching Vulkan's own, correct pairing: `srcFactor = SrcAlpha` (not `One`) for the colour channel, `alpha.dstFactor = Zero` (not `OneMinusSrcAlpha`, matching Vulkan's `srcAlphaBlendFactor=ONE, dstAlphaBlendFactor=ZERO`) — no shader change. New Check G in `WebGPU_Clear_Readback` (a 50%-alpha sprite over black must land strictly between black and full colour), verified genuinely discriminating via `git stash` (reads back full brightness (255) with the bug reverted, ~188 with the fix — the exact value depends on whether the chosen swapchain format blends in linear or sRGB-encoded space, both legitimate, so the check asserts direction/magnitude via a wide tolerance band, not an exact value). Re-verified `WebGPU_Native2D_Smoke` and the `--webgpu-2d-validation` scene (screenshot-reviewed) still render correctly (mostly-opaque content, so no expected visual difference from this fix in that particular scene). |

---

## Phase 57 — WebGPU backend: uniform buffer system (replaces push constants)

> **Recommended concrete entry point (investigated 2026-07-12, not yet implemented — read this
> before starting Phase 57/58/59/63):** the natural first vertical slice is
> `WebGPUGraphicsBackend::DrawColoredPrimitives()`/`DrawIndexedColoredPrimitives()` (already
> declared in the header, currently calling `ThrowUnsupported3DDraw`), because
> `GraphicsDevice::DrawUserPrimitives()`/`DrawUserIndexedPrimitives()` already call them directly
> (`GraphicsDevice.cpp:674` and its indexed sibling) for any `VertexPositionColor` (stride-16) draw
> with an effect applied only for its World/View/Projection matrices — no `BasicEffect`
> lighting/texture dispatch (`GpuDrawParams`/`DrawPrimitivesEx`, Phase 63's harder path) required.
> This is exactly `WEBGPU-93`'s own target ("3D coloured-quad pixel test, stride 16").
>
> Findings from reading the Vulkan reference implementation (`VulkanGraphicsBackend::
> DrawColoredPrimitives`, `FillExtPushConst`, `colored3d.vert.glsl`/`.frag.glsl`) that the next
> session should reuse rather than re-derive:
> - The 128-byte/32-float UBO layout `WEBGPU-11` asks for is **not** a new design — it's
>   `FillExtPushConst()`'s existing byte-for-byte layout (`VulkanGraphicsBackend.cpp:3459`):
>   `[0..15]` MVP (column-major mat4), `[16..19]` diffuseColor, `[20..23]` ambientColor+
>   lightingEnabled, `[24..27]` light0Dir+textureEnabled, `[28..31]` light0Diffuse+
>   vertexColorEnabled. Reuse this exact layout so the same UBO/shader-input shape works for
>   `DrawColoredPrimitives` now and `BasicEffect`'s full `DrawPrimitivesEx` dispatch later (Vulkan's
>   own `DrawColoredPrimitives` fills the *same* 32-float layout directly, bypassing
>   `GpuDrawParams`, with `diffuseColor=white` and `vertexColorEnabled=1`, everything else zeroed —
>   copy that approach, not a bespoke smaller struct).
> - WGSL's `mat4x4f` + 4×`vec4f` gives the identical 128-byte/16-byte-aligned layout in a
>   `@group(0) @binding(0) var<uniform>` block — no manual padding needed.
> - **No Vulkan-style NDC Y-flip is needed.** Vulkan's `colored3d.vert.glsl` negates `pos.y`
>   because Vulkan's clip space is Y-down by convention; WebGPU (like D3D/Metal, which is what real
>   XNA's own math already assumes) is Y-up, matching the *already-verified* `QueueSprite()` 2D
>   path's own `1.0 - 2.0*py/H` formula (no separate flip there either). A plain
>   `position = mvp * vec4f(pos, 1.0)` should be directly correct. Depth range `[0,1]` also already
>   matches XNA/WebGPU with no remap, same as Vulkan's own comment says.
> - **Real lifetime hazard to design around:** `GraphicsDevice::DrawUserPrimitives()` creates its
>   vertex buffer as a function-local `unique_ptr<IVertexBufferBackend>` that is destroyed right
>   after `DrawColoredPrimitives()` returns. Since this backend's frame model defers all rendering
>   to `EnsureFrameRendered()` (matching Vulkan/Bgfx's own "whole frame batched into one pass"
>   design — see `NEXT.md`'s architecture notes), the draw **cannot** hold onto the caller's
>   `WebGPUVertexBufferBackend`/its `WGPUBuffer` — it will be gone by render time. Vulkan's own
>   `DrawColoredPrimitives` sidesteps this by `memcpy`-ing the vertex bytes into its own
>   `Pending3DDraw::vbData` *immediately*, at call time, not by holding a reference. `WebGPUVertexBufferBackend`
>   has no CPU-readable accessor today (`Buffer()` only returns the opaque `WGPUBuffer` handle) —
>   add a small CPU shadow copy (`std::vector<std::uint8_t>`, populated inside
>   `SetDataWithOptions()`) so `DrawColoredPrimitives` can copy it out synchronously, mirroring
>   Vulkan's approach, before queuing a `ColoredDrawCommand` for `EnsureFrameRendered()` to actually
>   build a real `WGPUBuffer` + issue the draw from later (parallel to how `spriteCommands_`/
>   `RenderSprites()` already work for 2D).
> - Depth-stencil state: a real pipeline-cache keyed at minimum by `(depthTestEnabled_,
>   depthWriteEnabled_)` is enough for a first slice (WebGPU pipeline state is largely immutable —
>   see `docs/webgpu-backend.md`'s architecture notes — so this can't be fully dynamic like
>   Vulkan's `VkDynamicState`); `CullMode`/`FillMode::WireFrame` can stay unhandled for the very
>   first slice (tracked separately as `WEBGPU-41`/`WEBGPU-79`/`WEBGPU-115`), documented as a known
>   gap rather than silently ignored.

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-11 | Design `GpuUniforms` struct (128 bytes = 32 floats) matching Vulkan push constant layout; upload via `wgpuQueueWriteBuffer` | ✅ | Verified 2026-07-12: implemented as the anonymous-namespace `FillColoredUniforms()` helper, byte-for-byte matching `VulkanGraphicsBackend::FillExtPushConst()`'s 32-float layout (`[0..15]` MVP, `[16..19]` diffuseColor, `[20..23]` ambient+lightingEnabled, `[24..27]` light0Dir+textureEnabled, `[28..31]` light0Diffuse+vertexColorEnabled), uploaded via `wgpuQueueWriteBuffer`. Runtime-verified by `WebGPU_Colored3D`. |
| WEBGPU-12 | Create `WGPUBuffer` (uniform, size=128) per frame (or ring buffer of 3); map on CPU side via `wgpuBufferGetMappedRange` | 🟨 | A correct, simpler variant is implemented and verified: one fresh 128-byte `WGPUBuffer` (`WGPUBufferUsage_Uniform\|CopyDst`) per *draw call* (not per frame, not a ring buffer), written via `wgpuQueueWriteBuffer` and released once the frame's command buffer is submitted (`WebGPUGraphicsBackend::RenderColoredDraws()`/`pendingBufferReleases_`). Correct and GPU-validated with zero errors across all `WebGPU_Colored3D` checks, but not the per-frame/ring-buffer design this row describes — revisit if per-draw buffer churn becomes a real perf concern once more 3D draws land. |
| WEBGPU-13 | `WGPUBindGroupLayout` for slot 0 binding 0 (uniform buffer) — shared across all 3D pipelines | ✅ | Verified 2026-07-12: `coloredBindGroupLayout_`, one `WGPUBufferBindingType_Uniform` entry at binding 0, visible to both vertex and fragment stages. Currently colored3D-pipeline-specific, not yet literally shared with a second 3D pipeline family (none exist yet) — same layout shape is intended to be reused once Phase 58's other WGSL shaders land. |
| WEBGPU-14 | `WGPUBindGroup` creation and per-draw update for MVP matrix | ✅ | Verified 2026-07-12: a fresh `WGPUBindGroup` is created and bound per draw in `RenderColoredDraws()`, pointing at that draw's own uniform buffer. |
| WEBGPU-15 | `WGPUBindGroupLayout` for slot 1 binding 0 (texture sampler) — for textured pipelines | ✅ | Verified 2026-07-12: `texturedBindGroupLayout_` (group 1: binding 0 sampler, binding 1 texture — mirrors the SpriteBatch bind group layout shape exactly), used alongside `coloredBindGroupLayout_` (group 0, the UBO) by the new `textured3d.wgsl` pipeline (`WEBGPU-20`/`33`). |
| WEBGPU-16 | `WGPUSampler` creation mapping `SamplerState` (filter, address mode) → WGPU descriptor | 🟨 | SpriteBatch sampler cache maps linear/point and wrap/clamp/mirror; full per-slot 3D SamplerState mapping remains open. |
| WEBGPU-17 | `WGPUPipelineLayout` combining UBO bind group layout + texture bind group layout | ✅ | Verified 2026-07-12: `texturedPipelineLayout_` combines `coloredBindGroupLayout_` (group 0, UBO) + `texturedBindGroupLayout_` (group 1, sampler+texture) — `WebGPU_Textured3D` proves both groups bind and are read correctly by the same draw (UBO's `DiffuseColor` multiplies the sampled texture colour). `coloredPipelineLayout_` (UBO-only, no texture group) remains separately used by `colored3d.wgsl`. |

---

## Phase 58 — WebGPU backend: WGSL shaders

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-18 | Write `sprite2d.wgsl` — 2D sprite vertex + fragment shader (pos + UV + RGBA tint); embed as C++ string literal | ✅ | Embedded WGSL compiles and is runtime-verified: `WEBGPU-126`'s validation scene and `WEBGPU-130`'s independent `../mobile-eggbert` application both render correctly through it with no WebGPU validation errors. |
| WEBGPU-19 | Write `colored3d.wgsl` — 3D vertex shader (float3 pos + ubyte4 color), flat fragment; UBO for MVP | ✅ | Verified 2026-07-12: embedded WGSL in `CreateColoredResources()`, compiles and is runtime-verified by `WebGPU_Colored3D` (4/4 checks, including a genuine depth-test proof — see `WEBGPU-32`/`WEBGPU-64`/`65`). No fog (unlike Vulkan's `colored3d.vert.glsl`, which layers a separate fog UBO on top) — not tracked as its own WEBGPU-N task, deliberately deferred. |
| WEBGPU-20 | Write `textured3d.wgsl` — 3D vertex (float3 pos + float2 UV); texture2D sampler in fragment | ✅ | Verified 2026-07-12: embedded WGSL in `CreateTexturedResources()`, `@group(1)` sampler+texture, reuses `@group(0)` UBO from `colored3d.wgsl`. `WebGPU_Textured3D` CTest (3/3): a solid-colour texture samples correctly, `DiffuseColor` genuinely multiplies the sampled colour (not just passed through), and the indexed draw path works too. No fog, same deliberate deferral as `colored3d.wgsl`. |
| WEBGPU-21 | Write `colored_textured3d.wgsl` — float3 + ubyte4 color + float2 UV; multiply tex×color in fragment | ⬜ | stride=24 |
| WEBGPU-22 | Write `lit_textured3d.wgsl` — float3 pos + float3 normal + float2 UV; Blinn-Phong lighting in fragment | ⬜ | stride=32 |
| WEBGPU-23 | Write `alpha_test3d.wgsl` — per-pixel alpha discard matching XNA AlphaTestEffect semantics | ⬜ | |
| WEBGPU-24 | Write `dual_texture3d.wgsl` — two texture samplers, multiply/blend in fragment | ⬜ | |
| WEBGPU-25 | Write `env_map3d.wgsl` — cube map sampler + reflection vector from normal | ⬜ | |
| WEBGPU-26 | Write `skinned3d.wgsl` — bone palette as uniform array (max 72 mat4); blend 4 weights+indices | ⬜ | |
| WEBGPU-27 | Write `instanced3d.wgsl` — per-instance mat4 world transform in second vertex buffer binding | ⬜ | |
| WEBGPU-28 | Compile-time validation: embed all WGSL as `constexpr const char*` in `webgpu_shaders.hpp`; validate via `wgpuDeviceCreateShaderModule` at startup | ⬜ | Catch WGSL errors early |

---

## Phase 59 — WebGPU backend: render pipeline creation

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-29 | `WGPURenderPipelineDescriptor` builder helper: vertex state, primitive state, depth-stencil state, multisample state, fragment state | 🟨 | The concrete SpriteBatch descriptor is runtime-verified (`WEBGPU-126`/`130`); a reusable all-pipeline builder for the 3D families in Phase 59 onward remains open. |
| WEBGPU-30 | Pipeline cache: `std::unordered_map<uint64_t, WGPURenderPipeline>` with MakeKey(topo, depth, blend, cull, stride, wireframe, msaa) | ⬜ | Mirror Vulkan MakeKey / GetOrCreate* |
| WEBGPU-31 | `GetOrCreatePipeline2D()` — sprite pipeline (stride=24, Sprite2DVertex layout, no depth) | ✅ | Opaque and premultiplied-alpha variants are runtime-verified: `WEBGPU-126`'s validation scene and `WEBGPU-130`'s independent `../mobile-eggbert` application both render correctly through this pipeline. |
| WEBGPU-32 | `GetOrCreatePipelineColored3D()` — stride=16, VPC layout | ✅ | Verified 2026-07-12: cached by `(topology, depthFunc, depthTest, depthWrite)`, `Float32x3` position + `Unorm8x4` color at stride 16. `WebGPU_Colored3D`'s two depth-order checks (far-then-near and near-then-far both correctly resolve to the near quad, not "last draw wins") prove genuine `WGPUCompareFunction` depth comparison, not just "a pipeline was created." |
| WEBGPU-33 | `GetOrCreatePipelineExt3D()` — stride 20/24/32 dispatch matching Vulkan | 🟨 | Stride 20 done as `GetOrCreatePipelineTextured3D()` (cached by `(topology, depthFunc, depthTest, depthWrite)`, same shape as `WEBGPU-32`), verified 2026-07-12 by `WebGPU_Textured3D`. Strides 24 (`colored_textured3d.wgsl`, `WEBGPU-21`) and 32 (`lit_textured3d.wgsl`, `WEBGPU-22`) remain open — no shader exists for either yet. |
| WEBGPU-34 | `GetOrCreatePipelineAlphaTest3D()` — alpha discard variant | ⬜ | |
| WEBGPU-35 | `GetOrCreatePipelineDualTex3D()` — two-texture variant | ⬜ | |
| WEBGPU-36 | `GetOrCreatePipelineEnvMap3D()` — cube map variant | ⬜ | |
| WEBGPU-37 | `GetOrCreatePipelineSkinned3D()` — bone palette variant | ⬜ | |
| WEBGPU-38 | `GetOrCreatePipelineInstanced3D()` — per-instance binding variant | ⬜ | |
| WEBGPU-39 | Depth-stencil: `WGPUDepthStencilState` mapping `DepthFormat` + `CompareFunction` + `StencilOperation` | 🟨 | `CompareFunction`→`WGPUCompareFunction` (`ToWGPUCompareFunction()`, mirrors Vulkan's `ToVkCompareOp()` ordinal mapping) and `DepthBufferEnable`/`DepthBufferWriteEnable` are implemented and runtime-verified via `WebGPUGraphicsBackend::ApplyDepthStencilState()` (found missing entirely — see `WEBGPU-83`'s row — while verifying `WEBGPU-32`'s depth-order pixel test). `StencilOperation`/stencil state is still not wired into any pipeline (`WEBGPU-83` remains open for that half). |
| WEBGPU-40 | Blend state: `WGPUBlendState` mapping `BlendFunction` + `BlendFactor` (Opaque, AlphaBlend, Additive, NonPremultiplied) | 🟨 | Opaque and premultiplied-alpha SpriteBatch pipelines exist; complete XNA BlendState mapping remains open. |
| WEBGPU-41 | Rasterizer: `WGPUPrimitiveState` mapping `CullMode`, `FillMode` (WireFrame via `topology=LineStrip` fallback or unsupported) | ⬜ | |

---

## Phase 60 — WebGPU backend: vertex and index buffers

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-42 | `WebGPUVertexBufferBackend`: create `WGPUBuffer` (vertex, size=capacity×stride) with `COPY_DST` usage | 🟨 | Lazily sized VERTEX\|COPY_DST code and capacity validation compile; runtime validation remains open. |
| WEBGPU-43 | `SetData()`: upload via `wgpuQueueWriteBuffer(queue, buffer, 0, data, byteSize)` | 🟨 | Upload code exists; no runtime validation yet. |
| WEBGPU-44 | `SetDataWithOptions()`: `Discard` = reallocate buffer; `NoOverwrite` = `wgpuQueueWriteBuffer` at offset | ⬜ | |
| WEBGPU-45 | `WebGPUIndexBufferBackend`: 16-bit and 32-bit index buffers via `WGPUIndexFormat` | 🟨 | Both backend classes exist; draw dispatch and runtime validation remain open. |
| WEBGPU-46 | `SetData16()` / `SetData32()`: `wgpuQueueWriteBuffer` | 🟨 | Both upload paths exist; no normal build/runtime validation yet. |
| WEBGPU-47 | Disposed guard in all SetData methods (throw `ObjectDisposedException`) | ⬜ | Match Task 240 pattern |
| WEBGPU-48 | `SetVertexBuffer(wgpuRenderPassSetVertexBuffer)` + `SetIndexBuffer(wgpuRenderPassSetIndexBuffer)` in draw dispatch | ⬜ | |

---

## Phase 61 — WebGPU backend: textures

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-49 | `WebGPUTextureBackend`: `WGPUTexture` (2D, RGBA8Unorm, COPY_DST + TEXTURE_BINDING) + `WGPUTextureView` | ✅ | RGBA8Unorm Texture2D + view are runtime-verified: `WEBGPU-126` (`player.png`) and `WEBGPU-130` (`../mobile-eggbert`'s own UI/background textures) both sample correctly with no WebGPU validation errors. |
| WEBGPU-50 | `SetData()`: `wgpuQueueWriteTexture()` with `WGPUImageCopyTexture` + `WGPUTextureDataLayout` | ✅ | Level-upload is runtime-verified by the same two texture uploads above. |
| WEBGPU-51 | `Texture2D::GetData()`: staged MAP_READ copy with aligned rows and asynchronous map/poll completion | ⬜ | `WEBGPU-91` (2026-07-12) implemented this exact staged-copy/row-alignment/async-map pattern for the *backbuffer* (`WEBGPU-55`); `Texture2D::GetData()` itself (reading an arbitrary `Texture2D`, not the swapchain) is a distinct, still-unimplemented method — reuse the same pattern, do not re-derive it. |
| WEBGPU-52 | Mip levels: generate via `wgpuCommandEncoderCopyTextureToTexture` per level or leave as mip=1 (document) | 🟨 | Requested mip count is allocated and explicit level uploads work; automatic mip generation is not implemented. |
| WEBGPU-53 | `WebGPURenderTargetBackend`: `WGPUTexture` (RENDER_ATTACHMENT + TEXTURE_BINDING) + depth texture | ⬜ | |
| WEBGPU-54 | `SetRenderTarget(rt)` / `SetRenderTarget(nullptr)`: switch render pass target between RT and swapchain view | ⬜ | |
| WEBGPU-55 | `GetBackBufferData()`: readback via MAP_READ buffer + `wgpuCommandEncoderCopyTextureToBuffer` | ✅ | This *is* `WEBGPU-91`'s implementation (`WebGPUGraphicsBackend::ReadBackbuffer()`/`CaptureReadback()`) — verified 2026-07-12 via `WebGPU_Clear_Readback`, see `WEBGPU-91`'s own row for the full detail. |
| WEBGPU-56 | `WebGPUTextureCubeBackend`: `WGPUTexture` (dimension=2D, arrayLayerCount=6, CUBE_COMPATIBLE) | ⬜ | |
| WEBGPU-57 | `WebGPUTexture3DBackend`: `WGPUTexture` (dimension=3D) | ⬜ | |
| WEBGPU-58 | MSAA: `WGPUTexture` with `sampleCount=4`; resolve in render pass via `resolveTarget` | ⬜ | |

---

## Phase 62 — WebGPU backend: 2D rendering (SpriteBatch)

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-59 | `WebGPUSpriteBatchBackend`: dynamic vertex buffer ring (3 frames) for sprite quads | 🟨 | The growable dynamic SpriteBatch vertex buffer is runtime-verified (`WEBGPU-126`/`130`, including a real game's animated multi-sprite scenes); the three-frame ring/fencing optimization itself remains open (current single-buffer growth strategy works correctly, just unoptimized). |
| WEBGPU-60 | Upload sprite quads via `wgpuQueueWriteBuffer` per batch | ✅ | Runtime-verified by every `WEBGPU-126`/`130` frame (flattened sprite upload → draw, no validation errors). |
| WEBGPU-61 | Per-batch draw: set pipeline, bind groups (UBO + texture), vertex buffer, draw | ✅ | Runtime-verified the same way — `WEBGPU-130`'s `../mobile-eggbert` run alone issues many real per-frame SpriteBatch draws (title, UI panels, animated character, progress bar, fade overlay) with no WebGPU validation errors. |
| WEBGPU-63 | Verify SpriteBatch sort modes: Immediate, Deferred, Texture, FrontToBack, BackToFront. | ⬜ | Queue ordering is primarily shared `SpriteBatch` behaviour; validate the WebGPU submission path in `WEBGPU-126` rather than reimplementing it. |

---

## Phase 63 — WebGPU backend: 3D draw dispatch

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-64 | `DrawPrimitives()`: bind colored3d pipeline + UBO + vertex buffer + `wgpuRenderPassEncoderDraw` | ✅ | No literal `DrawPrimitives()` method exists on `IGraphicsBackend` (checked 2026-07-12) — this row's described capability *is* `DrawColoredPrimitives()`, implemented and verified (`WEBGPU-91`'s vertical slice, see `WEBGPU-32`). `WebGPU_Colored3D`'s Check A (`DrawUserPrimitives`, non-indexed) exercises exactly this path. |
| WEBGPU-65 | `DrawIndexedPrimitives()`: bind index buffer + `wgpuRenderPassEncoderDrawIndexed` | ✅ | Same clarification as `WEBGPU-64` — this is `DrawIndexedColoredPrimitives()`. `WebGPU_Colored3D`'s Check B (`DrawUserIndexedPrimitives`, 4 verts + 6 indices) verifies it specifically, not just the non-indexed path. |
| WEBGPU-66 | `DrawPrimitivesEx()`: dispatch by `GpuDrawParams` (stride, textureEnabled, lightingEnabled, dualTexture, skinned, instanced) | 🟨 | Verified 2026-07-12 for stride 16 (`VertexPositionColor`, via `colored3d.wgsl`) and stride 20 (`VertexPositionTexture`, via `textured3d.wgsl`, requires `params.texture0 != nullptr`): both forward the caller's real `GpuDrawParams` (`DiffuseColor`, `VertexColorEnabled`, texture binding) via `FillExtUniforms()` (mirrors Vulkan's `FillExtPushConst()` field-for-field), instead of the `DrawColoredPrimitives` fallback's hardcoded white/true/no-texture. `WebGPU_DrawPrimitivesEx`/`WebGPU_Textured3D` CTests prove this concretely (non-white `DiffuseColor` reaching the shader; `DiffuseColor` genuinely multiplying the sampled texture colour, not just being ignored). Stride 24 (`colored_textured3d.wgsl`) and 32 (`lit_textured3d.wgsl`, real lighting) remain open, as do other effects (alpha test/dual texture/env map/skinned) — all still fall back to `DrawColoredPrimitives()` exactly as the interface default did. |
| WEBGPU-67 | `DrawUserPrimitives()`: transient `WGPUBuffer` (COPY_DST + VERTEX, mappedAtCreation=false); upload + draw + release | ✅ | `RenderColoredDraws()` creates a transient `WGPUBuffer` (`Vertex\|CopyDst`) per draw, `wgpuQueueWriteBuffer`s the CPU shadow-copy into it, draws, and releases it after the frame's command buffer is submitted (not `mappedAtCreation` — uses the same queue-write pattern as every other buffer upload in this backend, an equally valid approach). |
| WEBGPU-68 | `DrawInstancedPrimitivesEx()`: second vertex buffer binding (per-instance mat4 world transforms) | ⬜ | |
| WEBGPU-69 | PrimitiveType mapping: TriangleList→`WGPUPrimitiveTopology_TriangleList`, TriangleStrip→Strip, LineList→LineList, LineStrip→LineStrip, PointList→PointList | ✅ | All 5 `PrimitiveType` values map via the existing `ToTopology()` (unchanged); now genuinely exercised by real 3D draw dispatch (`WEBGPU-64`/`65`), not just present in source. `WebGPU_Colored3D` only exercises `TriangleList` directly, but the pipeline cache is keyed by topology so the other 4 follow the same, now-proven code path. |
| WEBGPU-70 | `vertexStart` / `startIndex` / `baseVertex` support in draw calls | ⬜ | `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` always draw from vertex/index 0 (`wgpuRenderPassEncoderDraw(pass, vertexCount, 1, 0, 0)` / `DrawIndexed(..., 0, 0, 0)`) — matches every real call site today (`GraphicsDevice.cpp`'s two `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` callers always build a dedicated, zero-based temporary buffer per call), but a genuine sub-range draw would silently ignore a non-zero start/base. Real gap, not yet needed by any current caller. |

---

## Phase 64 — WebGPU backend: Effects

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-71 | `WebGPUEffectBackend`: `BasicEffect` wires to `FillGpuDrawParams` → UBO upload | ⬜ | |
| WEBGPU-72 | `AlphaTestEffect`: UBO alpha test params (function, reference) | ⬜ | |
| WEBGPU-73 | `DualTextureEffect`: second texture bind group | ⬜ | |
| WEBGPU-74 | `EnvironmentMapEffect`: cube map bind group + reflection UBO params | ⬜ | |
| WEBGPU-75 | `SkinnedEffect`: bone palette as large UBO (72 × mat4 = 4608 bytes) in separate bind group | ⬜ | WebGPU min UBO size: 65536 bytes — fits |
| WEBGPU-76 | `ShaderEffect` (custom WGSL): `wgpuDeviceCreateShaderModule` from user-provided WGSL source string | ⬜ | NOXNA extension |

---

## Phase 65 — WebGPU backend: state management

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-77 | `SetDepthTestEnabled()` / `SetDepthWriteEnabled()`: bake into pipeline key | ⬜ | WebGPU requires pipeline rebuild on change |
| WEBGPU-78 | `SetBlendState()`: map `BlendState` preset → `WGPUBlendState` | ⬜ | |
| WEBGPU-79 | `SetRasterizerState()`: `CullMode` → `WGPUCullMode`; `FillMode::WireFrame` unsupported (log warning) | ⬜ | WebGPU has no polygon mode |
| WEBGPU-80 | `SetScissorRectangle()`: `wgpuRenderPassEncoderSetScissorRect` | ⬜ | |
| WEBGPU-81 | `SetViewport()`: `wgpuRenderPassEncoderSetViewport` | ⬜ | |
| WEBGPU-82 | `SetSamplerState()`: per-slot `WGPUSampler` cache (filter + address mode key) | 🟨 | 18-entry SpriteBatch sampler cache implemented; full graphics-device per-slot sampler state remains open. |
| WEBGPU-83 | `SetDepthStencilState()`: stencil ops → `WGPUStencilFaceState` | ⬜ | |
| WEBGPU-84 | `OcclusionQuery`: `WGPUQuerySet` (type=Occlusion) + `wgpuRenderPassEncoderBeginOcclusionQuery` | ⬜ | |

---

## Phase 66 — WebGPU backend: Multiple Render Targets

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-85 | MRT render pass: `WGPURenderPassDescriptor` with array of `WGPURenderPassColorAttachment` (up to 4) | ⬜ | |
| WEBGPU-86 | `GetOrCreateMRTRenderPipeline(colorAttachmentCount)`: pipeline with matching `targetCount` in fragment state | ⬜ | |
| WEBGPU-87 | `SetRenderTargets(vector<RenderTarget2D*>)`: configure MRT pass descriptor | ⬜ | |

---

## Phase 67 — WebGPU backend: automated validation after the native 2D gate

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-88 | Establish CMake/CTest registration for native WebGPU tests. | ✅ | Delivered by `WEBGPU-129` (`WebGPU_Native2D_Smoke`) and extended 2026-07-12 by `WEBGPU-91`'s `WebGPU_Clear_Readback` — both registered via the same `CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "WEBGPU"` block, real display/GPU required (no synthetic skip path needed beyond the existing `WebGPUNativeSmokeTest.cmake` one). |
| WEBGPU-89 | No-readback smoke: initialize, clear to a known colour, present 60 frames, release. | ✅ | This is exactly `WEBGPU_Native2D_Smoke` (`cna_demo_2d --smoke 120`), verified 2026-07-12 as part of the `ctest -R "^WebGPU_"` run (2.19s, pass) — restating this row's own acceptance criterion, not new work. |
| WEBGPU-90 | No-readback SpriteBatch scene: RGBA texture, source rectangle, tint, rotation and sampler variants. | ✅ | This is `cna_demo_2d --webgpu-2d-validation`, verified by `WEBGPU-126` and reconfirmed 2026-07-12 (screenshot review after the `WEBGPU-91` `Present()`/`EnsureFrameRendered()` refactor, still renders identically — crops/tint/rotation/flips/sampler variants all correct, no WebGPU error). |
| WEBGPU-91 | Implement reusable GPU readback with correct row alignment, asynchronous map/poll completion and timeout/error propagation. | ✅ | Verified 2026-07-12: `WebGPUGraphicsBackend::ReadBackbuffer()` implemented via `wgpuCommandEncoderCopyTextureToBuffer` into a 256-byte-row-aligned `MapRead`\|`CopyDst` buffer, `wgpuBufferMapAsync` + the existing `WaitForCompletion` polling helper (`WEBGPU-127`'s pattern), BGRA↔RGBA swizzle matching Vulkan's own `ReadBackbuffer`. Required refactoring `Present()` into a shared `EnsureFrameRendered()` (acquire-if-needed + render-if-`framePending_`) so `GetBackBufferData()` can force an on-demand render of whatever `Clear()`/`SpriteBatch` work is queued so far in the *current* `Draw()` call, matching the Vulkan/Bgfx backends' own on-demand-submit readback semantics (confirmed necessary: `bgfx_graphicsdevice_clear_stencil_test.cpp` reads back the same-frame result of a `Clear()`+draw with no intervening `Present()`). New `WebGPU_Clear_Readback` CTest (`examples/webgpu_clear_readback_test.cpp`), verified genuinely discriminating via `git stash` (reverting the backend changes makes the test abort with the base class's `"ReadBackbuffer: not implemented"` exception instead of passing). Re-verified `WebGPU_Native2D_Smoke` and the `--webgpu-2d-validation` scene (screenshot-reviewed) still pass unchanged after the `Present()` refactor. |
| WEBGPU-92 | Pixel-asserted 2D tests: clear and white 1×1 sprite, then alpha/source-rectangle/sampler cases. | 🟨 | `WebGPU_Clear_Readback` (`WEBGPU-91`/`132`) pixel-asserts: solid-colour `Clear()` (twice, proving no stale cache), a colour-tinted `SpriteBatch` quad inside/outside its destination rectangle, `alpha=0` leaving the destination unmodified, `sourceRectangle` cropping (2×1 red\|blue texture, right-texel-only crop samples blue), and 50%-alpha landing strictly between black and full colour (`WEBGPU-132` — this last check is also what caught and proved the `WEBGPU-132` blend-factor bug). **Still missing** for full closure: sampler address-mode (wrap/clamp/mirror) pixel assertions — those remain visually-verified only (`WEBGPU-126`), not pixel-asserted. |
| WEBGPU-93 | 3D coloured-quad pixel test (stride 16). | ⬜ | Schedule only after `WEBGPU-19`, `WEBGPU-32`, `WEBGPU-64` and `WEBGPU-77`–`WEBGPU-83`. |
| WEBGPU-94 | 3D textured-quad pixel test (stride 20). | ⬜ | Schedule only after the matching pipeline and draw dispatch exist. |
| WEBGPU-95 | 3D coloured+textured pixel test (stride 24). | ⬜ | Schedule only after the matching pipeline and draw dispatch exist. |
| WEBGPU-96 | Lit-textured, alpha-test and dual-texture effect tests. | ⬜ | Split into independently runnable cases when those effects land. |
| WEBGPU-97 | Environment-map and skinned-effect tests. | ⬜ | Split into independently runnable cases when those effects land. |
| WEBGPU-98 | Instancing, render-target, MSAA and occlusion-query tests. | ⬜ | Split by feature; do not add placeholder tests before implementation. |
| WEBGPU-99 | Buffer disposal, `SetDataOptions` and vertex-format mapping tests. | ⬜ | Schedule with `WEBGPU-44`, `WEBGPU-47` and `WEBGPU-116`. |

---

## Phase 68 — WebGPU backend: advanced and parity

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-106 | `SetStringMarkerEXT()`: no-op (WebGPU has no debug labels in wgpu-native C API yet) | ⬜ | Document deviation |
| WEBGPU-107 | `DebugSimulateContextLoss()`: destroy and recreate device (wgpu-native supports `wgpuDeviceDestroy`) | ⬜ | |
| WEBGPU-108 | `PresentationInterval` → vsync: `wgpuSurfaceConfigure.presentMode` (Fifo=VSync, Immediate=no VSync, Mailbox=adaptive) | 🟨 | Selection code exists but has no runtime validation. |
| WEBGPU-109 | `IsFullScreen` via `SDL_SetWindowFullscreen` — same as other backends | ⬜ | |
| WEBGPU-110 | `BackBufferWidth/Height` changes: reconfigure swap chain via `wgpuSurfaceConfigure` | ✅ | Runtime-verified by `WEBGPU-127`'s resize sequence (800×600→960×540→800×600) and `WEBGPU-130`'s independent second application, both with clean reconfiguration and no WebGPU validation error. |
| WEBGPU-111 | DXT1/DXT3/DXT5 compressed texture upload: `WGPUTextureFormat_BC1RGBAUnorm` etc. | ⬜ | Requires `wgpuAdapterHasFeature(BC_texture_compression)` |
| WEBGPU-112 | Texture3D: `WGPUTextureDimension_3D` + layered upload | ⬜ | |
| WEBGPU-113 | TextureCube: `WGPUTexture` arrayLayerCount=6 + `WGPUTextureViewDimension_Cube` | ⬜ | |
| WEBGPU-114 | RenderTargetCube: `WGPUTexture` cube + per-face `WGPUTextureView` as render attachment | ⬜ | |
| WEBGPU-115 | `FillMode::WireFrame`: document as unsupported in WebGPU (no polygon mode); add to deviations doc | ⬜ | |
| WEBGPU-116 | WebGPU vertex format helper: `WGPUVertexFormat WebGPUVertexFormatFromVEF(VertexElementFormat)` (mirror Task 248) | ⬜ | |

---

## Phase 69 — WebGPU: documentation and future (Emscripten/WASM)

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-117 | `docs/webgpu-backend.md`: architecture, deviations from Vulkan, WGSL shader map, UBO layout | 🟨 | Document exists but must be revised after `WEBGPU-124`–`WEBGPU-131` with only verified claims and exact test commands. |
| WEBGPU-118 | `docs/webgpu-vs-vulkan-deviations.md`: push constants → UBO, no wireframe, async→sync strategy | ⬜ | |
| WEBGPU-119 | Emscripten target: configure CNA for `emcc` build with `-sUSE_WEBGPU=1`; WebGPU backend routes to browser `navigator.gpu` | ⬜ | True browser WASM target |
| WEBGPU-120 | Emscripten: SDL3 Emscripten port + WebGPU surface via `emscripten_webgpu_get_device()` | ⬜ | |
| WEBGPU-121 | Emscripten: verify all 9 WGSL shader pairs compile in browser via `createShaderModule` | ⬜ | |
| WEBGPU-122 | Emscripten: run 2D smoke test in headless Chrome via `--headless=new --enable-features=WebGPU` | ⬜ | CI-friendly |
| WEBGPU-123 | Cross-backend pixel comparison: same scene rendered on EasyGL/Vulkan/Bgfx/WebGPU — assert pixel-level parity | ⬜ | |
