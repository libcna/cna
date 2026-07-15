# NEXT.md — CNA Project Handoff

> **2026-07-15: new `CANVAS` (HTML Canvas 2D) backend — all 8 phases (`plan_canvas.md` C1-C8)
> implemented and pushed to `feature/canvas` (worktree `../cnacanvas`, branched from `develop`).**
> Emscripten-only, 2D-only (like `SDL_RENDERER`) — `SpriteBatch` (rotation/origin/flip/tint/
> transform), textures/render targets, `BlendState`/`SamplerState` mapping, and `SpriteFont` are all
> implemented and structurally reviewed. **Real milestone**: this required a genuine
> `emcmake`/`emcc` 6.0.2 configure+build — the first one ever successfully completed in this
> project's history (confirmed via `$HOME/emsdk/emsdk_env.sh`); `CnaTests` links and a
> backend-agnostic GTest suite genuinely passes under `node CnaTests.js`. Along the way, fixed a
> real, pre-existing `sharp-runtime` bug blocking *any* Emscripten build (`FileSystemWatcher.hpp`
> declared 3 inotify fields unconditionally but only used them under `__linux__`, which `emcc`
> doesn't define) — owner-approved, committed in `sharp-runtime` (not pushed), and a real,
> pre-existing `GraphicsBackendCompileDefinitionTests.cpp` gap this configure exposed
> (`CNA_BACKEND_CANVAS` missing from its backend-count `#ifdef` chain, now fixed). **Not yet
> pixel-verified**: this dev loop has no real browser DOM at all (`SDL_Init(SDL_INIT_VIDEO)` itself
> throws under Emscripten/`node`) — see `docs/canvas-backend.md`'s "Known findings that revise the
> original DRAFT plan" (two judgment calls flagged for owner review: `AlphaBlend`/`NonPremultiplied`
> needing no premultiply conversion on this backend, and `Wrap`/`Mirror` addressing being
> implemented but unverified) and its 10-item manual browser verification checklist. Full
> task-by-task detail lives in `plan_canvas.md`, not duplicated here. Open question for the project
> owner: what to do next with `feature/canvas` (PR, merge to `develop`, or something else) — not yet
> answered as of this update.
>
> **Both `plan_dx.md` backends' full software/logic layer are done and Wine-verified as of
> 2026-07-14.** **D3D11**: Phase DX1 through DX11 are ALL closed — every task except `DX-90`/`DX-91`
> (real-Windows hardware, `needs_human`, no such machine available here). 6 CTest binaries, 96+
> checks, all passing through Wine+DXVK on a real GPU, including a working (Wine-verified) swap
> chain/`Present()`. **D3D12**: Phase DX12 (`DX-100` through `DX-113` and `DX-115`) is now ALSO fully
> closed — every task except `DX-114` (real-Windows hardware, `needs_human`, same constraint as
> D3D11's `DX-90`). `D3D12_Smoke` CTest: 80/80 checks, all 10 stock shader variants (same DXBC as
> D3D11) + a real `SpriteBatch` + device-removed recovery, all pixel-verified **off-screen**.
> **UPDATE 2026-07-14 (later same day): D3D12's swap-chain crash is fixed locally.** A properly
> Proton-managed launch (`scripts/run-proton-vkd3d.sh`, new — drives this machine's local Steam
> "Proton - Experimental" through its own `proton run` entry point + a dedicated Proton-managed
> prefix, instead of overlaying vkd3d-proton's DLLs onto a foreign system-Wine prefix) makes
> `CreateSwapChainForHwnd` genuinely succeed (`IsSwapChainAvailableEXT() = true`, reproduced twice
> including from a fresh prefix — see `plan_dx.md`'s `DX-102` row for the full evidence and the two
> dead ends that didn't work). `Present()`/back-buffer binding are still real, separate,
> `NotYetImplemented()` follow-up work — this only closes swap-chain *creation*, not presentation
> end-to-end, and does not substitute for `DX-114`'s real-hardware tearing/present-mode/device-lost
> verification. D3D12 also still lacks a public render-target backend,
> `Texture3D`, runtime-settable blend/depth-stencil/rasterizer state (PSOs hardcode
> `depthEnable=false`/`cullMode=None`), per-slot `SamplerState` (hardcoded static WRAP/linear
> samplers), and occlusion queries — real, scoped, honestly-documented follow-up work.
>
> Full task-by-task detail and this session's complete chronological history live in `plan_dx.md`
> (`DX-1`–`DX-115`) — not duplicated here. Current-state summaries: `docs/d3d11-backend.md`,
> `docs/d3d12-backend.md`, and `docs/graphics-backend-feature-matrix.md` (full row-by-row
> `D3D11`/`D3D12` comparison against every established backend).
>
> **With `DX-114` the only open item and it `needs_human` (real Windows hardware), there is no
> further available Debian-side work on `plan_dx.md` barring new instructions.** This is a
> brand-new architectural front for the project — read `plan_dx.md`'s own status banner before
> touching it. The pre-existing EasyGL/Vulkan/Bgfx/SDL_Renderer/Headless/Software/WebGPU work
> summarized below is unchanged by this; full history for that lives in `plan_graphics.md`/
> `plan_webgpu.md`/`plan_software.md` and `git log`, not duplicated here.

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on SDL3 with a pluggable graphics backend layer. It is a
framework/runtime — not a game — designed so XNA/FNA game code can be ported to C++ with minimal
API-surface changes.

- **Main goal:** full XNA 4.0 API coverage with pixel-accurate behavior, verified against the
  authoritative FNA reference source (`/rv/data/library/github.com/FNA-XNA/FNA/src`), backed by
  unit tests (`CnaTests`) and GPU pixel-readback integration tests (`ctest`).
- **Current phase:** the established Linux/desktop backends (EasyGL/Vulkan/Bgfx/SDL_Renderer) and
  the CI-oriented ones (Headless/Software) are mature — Phase 55 already declared a qualified
  **~90% XNA/FNA compatibility milestone** for `Microsoft::Xna::Framework::Graphics`. WebGPU has a
  working native 2D+partial-3D baseline (`plan_webgpu.md`). **Both Direct3D backends
  (`plan_dx.md`) now have their full software/logic layer complete and Wine-verified, as of
  2026-07-14.** Direct3D 11 (Phase DX1–DX11) is fully closed — a Windows-only backend, cross-compiled
  from this Debian machine via MinGW-w64 and verified locally through Wine+DXVK, including a working
  swap chain/`Present()`; only `DX-90`/`DX-91` (real-Windows hardware) remain `needs_human`. Direct3D
  12 (Phase DX12, `DX-100`–`DX-113`/`DX-115`) is also fully closed — same dev-loop approach but via
  Wine+vkd3d-proton, **off-screen only**: its swap chain genuinely crashes under this dev loop (a
  real `dxgi.dll`/vkd3d-proton mismatch, not a CNA bug), so only `DX-114` (real-Windows hardware)
  remains `needs_human` there too. With `DX-114` `needs_human`, there is no further available
  Debian-side work on `plan_dx.md` barring new instructions. Everything else
  (Phase 79's 153-sample `../cna-samples` re-audit, Task 945's HLSL→GLSL tooling decision, Task
  952's deferred Bgfx bug) is unchanged standing backlog — see §5/§8/§9.
- **Key architectural decisions:**
  - Backend selection is **compile-time** via the `CNA_GRAPHICS_BACKEND` CMake option
    (`EASYGL` | `VULKAN` | `BGFX` | `SDL_RENDERER` | `WEBGPU` | `HEADLESS` | `SOFTWARE` | `D3D11`).
    EasyGL is primary and most heavily tested. `SDL_Renderer` is 2D-only by design. `D3D11` is
    Windows-only, hard-`FATAL_ERROR`-gated at configure time on any other `CMAKE_SYSTEM_NAME`.
  - The `sharp-runtime` sibling repo (`../sharp-runtime/`) provides all `System.*` types and
    primitive type aliases (`bytecs`, `Single`, `String`, …) used on the XNA API surface.
  - Vertex layout dispatch is **stride-keyed**: EasyGL/Vulkan/Bgfx/Software/WebGPU/D3D11 all select
    their GPU vertex layout from the raw byte stride of the bound buffer (16/20/24/32/52), not from
    `VertexDeclaration` contents.
  - `Texture3D` and `TextureCube` inherit `GraphicsResource` directly, **not** `Texture` — unlike
    FNA (Task 863, documented architectural gap, not fixed).
  - `GraphicsDevice` stores state objects (`BlendState`/`DepthStencilState`/`RasterizerState`) **by
    value**, unlike FNA's reference-type aliasing (Task 869, deliberate, not fixed).
  - Vulkan and Bgfx both **batch an entire frame's draws into one deferred render pass/view**, whose
    clear always applies once, before all of that frame's draws, regardless of call order within
    that frame — load-bearing for how pixel tests must be written on these two backends (see §6).
  - XNA compiled `.fx` bytecode: not yet implemented (`Effect`'s bytecode constructor throws
    `NotImplementedException`, Phase 74). Custom shaders go through `ShaderEffect` (hand-written
    GLSL/HLSL/SPIR-V/WGSL per backend).
  - **D3D11-specific**: three independent resource-lifetime groups (device / swap-chain / window-size
    views — `plan_dx.md` design decision 11); COM ownership is `Microsoft::WRL::ComPtr<T>`
    throughout; a shared `D3DCommon` static library (`cna_backend_graphics_d3dcommon`) holds
    format/state/vertex-layout mapping tables reusable by a future D3D12 backend without merging
    the two backends' fundamentally different device models.

---

## 2. Current status

### Build status

| Build dir | Backend | Status |
|---|---|---|
| `cmake-build-debug` | EasyGL | Clean as of 2026-07-10 (not rebuilt this session) |
| `cmake-build-vulkan` | Vulkan | Clean as of 2026-07-10 (not rebuilt this session) |
| `cmake-build-bgfx` | Bgfx | Clean as of 2026-07-10 (not rebuilt this session) |
| `cmake-build-sdl` | SDL_Renderer | Clean as of 2026-07-10 (not rebuilt this session) |
| `cmake-build-android` | SDL_Renderer (NDK) | Blocked — Task 920 (sibling `sharp-runtime` NDK build regressions) |
| `cmake-build-d3d11` | D3D11 (Windows cross-compile, MinGW-w64) | **Verified clean 2026-07-13**: `CNA` and `cna_backend_graphics_d3dcommon`/`cna_backend_graphics_d3d11` build clean; `cna_test_d3d11_smoke`/`cna_test_d3d11_common` build, link, and run correctly under Wine+DXVK. **`CnaTests` itself does NOT build** for this backend — genuinely blocked (see §4), not silently skipped. |
| `cmake-build-d3d12` | D3D12 (Windows cross-compile, MinGW-w64) | **Updated 2026-07-14 (DX-113 test-suite formalization closed)**: `CNA` and both D3D12 backend targets build clean. Device-lifetime resources (DX-102-105), resource-barrier state tracking, a PSO cache, a root-signature cache (DX-106-108), real `D3D12VertexBufferBackend`/`D3D12IndexBufferBackend`/`D3D12TextureBackend`/`D3D12TextureCubeBackend` (DX-109, buffers+2D+cube textures — render targets/3D textures still honestly deferred, 🟨) plus a real, tested `RecreateDeviceEXT()` device-removed recovery path (DX-110), and `Clear()`/`DrawColoredPrimitives()`/`DrawIndexedColoredPrimitives()`/`DrawPrimitivesEx()`/`DrawIndexedPrimitivesEx()`/`DrawInstancedPrimitivesEx()` are REAL for **all 10 of 10 stock variants** (`colored3d`/`textured3d`/`colored_textured3d`/`lit_textured3d`/`alpha_test3d`/`dual_texture3d`/`env_map3d`/`sprite2d`/`skinned3d`/`instanced3d`, DX-111 ✅) via a new `BindOffscreenColorTargetEXT()` NOXNA helper standing in for the still-broken swap chain, plus a real `D3D12SpriteBatchBackend` (DX-112 ✅). DX-113 audited that coverage against D3D11's own DX-80–85 methodology, added a dedicated fog on/off test and untracked-resource-throw tests, and mutation-verified Check M's discriminating power (🟨 — real, but 2 honest gaps: no state-object cache exists yet to test against, and fence back-pressure isn't measured under real GPU load). `D3D12_Smoke` CTest: **80/80 checks pass** through Wine+vkd3d-proton (off-screen only, unchanged — the CTest harness still uses `run-wine-vkd3d.sh`'s system-Wine prefix). **UPDATE 2026-07-14: swap-chain *creation* now genuinely works**, via a properly Proton-managed launch (`scripts/run-proton-vkd3d.sh`, new) — `cna_diag_d3d12_swapchain` reports `IsSwapChainAvailableEXT() = true`, reproduced twice including from a fresh prefix (`plan_dx.md` `DX-102`'s row has the full evidence). `Present()`/back-buffer rendering are still real, separate `NotYetImplemented()` follow-up work. `DX-115` (docs) is now also closed — Phase DX12 is fully done except `DX-114`, which stays `needs_human`. **FINAL UPDATE 2026-07-14: Phase DX13 (`DX-116`–`DX-123`) is 6 of 8 rows closed, 2 partial (🟨)** — real `Present()`/back-buffer rendering, render targets/MRT, runtime-settable state objects, per-slot samplers, occlusion queries, custom `ShaderEffect`, `Texture3D`, and `TextureCube::GetData()` readback are all real now. Still partial: `DX-117` (render targets have **no MSAA and no mip-chain generation**) and `DX-121` (the `SpriteBatch::Begin(effect)` wiring is real code but not independently CTest-proven). `D3D12_Smoke` CTest: **125/125 checks** at the end of that phase (169/169 today). See `plan_dx.md`'s Phase DX13 section for full detail; Phase DX14/DX15 are the active work now. |

The `cna_demo_xact` example fails to build on every backend (missing `examples/demo_xact/Content`
directory in this checkout) — cosmetic, pre-existing, not a CNA bug, do not chase it (§9).

### Test status

| Backend | `CnaTests` (gtest) | `ctest` (integration/pixel) |
|---|---|---|
| EasyGL | 4371/4373 pass (2 hardware skips), as of 2026-07-11 | 190/192 pass — 2 pre-existing failures (`EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`) |
| Vulkan | 4371/4373 pass (2 hardware skips), as of 2026-07-11 | 127/128 pass — 1 pre-existing failure (`Vulkan_DepthBias`) |
| Bgfx | 4375/4377 pass (2 hardware skips), as of 2026-07-11 | 104/106 pass — 2 pre-existing failures (`Bgfx_RenderTarget2D_MsaaResolve`, `Bgfx_RenderTargetCube_DepthFormat`, DEFERRED — Task 952) |
| Software | 4371/4373 pass, as of 2026-07-13 | 6 CTests, 29/29 checks |
| D3D11 | **Does not build** (see §4) | **6/6 pass, 92 `D3D11_Smoke`/`D3D11_Common` checks + 10 more assertions across 4 new state-object tests** (`D3D11_Smoke` 69, `D3D11_Common` 23, `D3D11_BlendState_Opaque`/`AlphaBlend`, `D3D11_DepthStencilState_StencilEnable`, `D3D11_RasterizerState_CullMode`), verified 2026-07-14 via `ctest --test-dir cmake-build-d3d11 -R D3D11` |
| D3D12 | Not applicable yet (no `CnaTests` target for this backend) | **1/1 pass, 80/80 checks** (`D3D12_Smoke`: off-screen device/queue/heaps/command-lists/fence + resource-barrier tracking (incl. untracked-resource throw, DX-113) + root-signature cache + pipeline-state-object cache + real vertex/index/texture/cube-texture round-trips + device-removed recreation + real, pixel-verified `colored3d`/`textured3d`/`colored_textured3d`/`lit_textured3d`/`alpha_test3d`/`dual_texture3d`/`env_map3d`/`skinned3d`/`instanced3d` draws via `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`/`DrawInstancedPrimitivesEx` + fog on/off (DX-113) + a real `D3D12SpriteBatchBackend` quad-draw/flip proof — all 10/10 stock shader variants), mutation-verified (Check M), verified 2026-07-14 via `ctest --test-dir cmake-build-d3d12 -R D3D12` |
| Headless, WebGPU | Not re-verified this session | See `plan_headless.md`/`plan_webgpu.md` for their own last-verified status |

All EasyGL/Vulkan/Bgfx/Software numbers above are carried over from the last session that actually
touched those backends (2026-07-10/11/13) — **not re-verified in this session**, which worked
exclusively on D3D11/D3D12. The D3D11 numbers are unchanged from earlier this session; the D3D12
numbers are fresh, verified just now.

### Recently implemented

- **D3D11 graphics backend** (2026-07-13, this session, `plan_dx.md`): real device/swap-chain/
  back-buffer/`Clear()`/`Present()`/`ReadBackbuffer()`, a shared `D3DCommon` format/state/vertex-
  layout mapping core, all 10 stock shader variants ported to HLSL and compiler-verified to real
  DXBC (`DX-13-hlsl`/`DX-14-compile`), and a shader cache that creates real
  `ID3D11VertexShader`/`ID3D11PixelShader` objects for all of them (`DX-15-embed`) — Phase DX3 is
  now fully closed. Real vertex/index buffers (both 16-bit and 32-bit) and a cached
  `ID3D11InputLayout` path (`DX-30`/`DX-31`/`DX-32`) close Phase DX5. Real 2D/cube/3D textures, 2D/
  cube render targets (with real device-queried MSAA and a fix so `Clear()` targets whatever's
  actually bound instead of always the back buffer), MRT, a sampler cache, and occlusion queries
  (`DX-40`–`DX-47`) close Phase DX6 — each round-trip/creation-proven via new `D3D11_Smoke` checks.
  Real blend/depth-stencil/rasterizer state objects (cached, from the raw XNA ordinals
  `Apply*State()` already carries) plus `SetBlendFactor()`/`SetReferenceStencil()`/`SetViewport()`/
  `SetScissorRect()` (`DX-50`–`DX-53`) close Phase DX7, including a real, documented
  `DepthBias` float→D3D11-`INT` unit-conversion finding (round, not truncate). **Phase DX8's
  foundational tasks (`DX-60`/`DX-60a`/`DX-61`) now close too — this backend's first real, pixel-
  verified 3D triangle**: new `D3DConstantBuffers.hpp` (`D3DCommon`) defines the GPU-packed
  constant-buffer structs (offsets `static_assert`-verified against the real HLSL), and
  `DrawColoredPrimitives()`/`DrawIndexedColoredPrimitives()` now do a real `colored3d` draw for
  stride-16 (`VertexPositionColor`). Found+fixed a real independent bug along the way: `DX-46`'s
  `SetRenderTargets(nullptr, 0)` never restored the back buffer after a prior MRT bind. **`DX-62`/
  `DX-63`/`DX-64` (+ `DX-69` partial) also close**: new `DrawPrimitivesEx()`/
  `DrawIndexedPrimitivesEx()` overrides give real, `GpuDrawParams`-driven `textured3d`/
  `colored_textured3d`/`lit_textured3d`/`alpha_test3d` draws (fog wired for all 5 real variants so
  far); `dual_texture3d`/`env_map3d`/`skinned3d` throw named not-yet-implemented errors instead of
  silently misrendering. Zero bugs found getting these to pass. See §3 for the itemized list and
  §4/§5 for what's still open.
- **Software backend Phase S9** (`SOFTWARE-80`–`84`, 2026-07-13, `plan_software.md`): real bilinear
  texture sampling, real backface culling, real near-plane polygon clipping, `DualTextureEffect`/
  `EnvironmentMapEffect`/`SkinnedEffect` support, and a cross-backend diagnostic tool confirming
  `SOFTWARE` matches `EASYGL` within a max per-channel pixel diff of 1.
- Full history before that (WebGPU 3D shader work, `SkinnedEffect`/`EnvironmentMapEffect` multi-
  light fixes, the Bgfx render-target crash cluster fix, the XNA culling/depth-occlusion compat
  audits, skeletal animation playback) is in `plan_graphics.md`/`plan_webgpu.md` and `git log` —
  not repeated here to keep this file current and short.

### Known working examples

Representative, still-current demos/examples (not re-verified this session): `cna_demo_2d`,
`cna_demo_avatar` (+ sub-demos), `cna_house3d_demo`, and ~180 EasyGL / ~120 Vulkan / ~90 Bgfx
registered `ctest` pixel-verification examples under `examples/`. New this session:
`examples/d3d11_smoke_test.cpp` (real device/Clear/Present/readback proof) and
`examples/d3d11_common_test.cpp` (pure-function format/state/vertex-layout table checks).

### Does NOT work yet

- **D3D11**: **Every stock 3D shader variant now has a real, pixel-verified draw pipeline**
  (`colored3d`/`textured3d`/`colored_textured3d`/`lit_textured3d`/`alpha_test3d`/`dual_texture3d`/
  `env_map3d`/`skinned3d`/`instanced3d` — Phase DX8, `DX-60`–`DX-69`, fully closed 2026-07-13),
  reachable via real `DrawPrimitivesEx()`/`DrawIndexedPrimitivesEx()`/`DrawInstancedPrimitivesEx()`
  overrides driven by actual `GpuDrawParams` (textures/vertex-color/lighting/alpha-test/fog/
  two-texture/cube-map/bone-skinning/per-instance-world all wired) — `colored3d` via the legacy
  `DrawColoredPrimitives()` path still carries no `GpuDrawParams` (hardcoded white/no-fog, Task 364
  parity with every other backend's identical legacy path). **`SpriteBatch` is now real too**
  (`D3D11SpriteBatchBackend`, Phase DX9, `DX-70`/`DX-71`/`DX-72`, closed 2026-07-13) — quad
  batching through the stock `sprite2d` pipeline, `SpriteEffects` flip, a genuinely-working
  `SetTransformMatrix()` (unlike Vulkan's own silent no-op), real `TextureAddressMode::Wrap`/
  `Mirror`, and custom `Effect`-via-`SpriteBatch::Begin(effect)` reusing `D3D11EffectBackend`
  (`DX-58`) — all GPU-proven through the actual public `SpriteBatch`/`Texture2D` API. Vertex/index
  buffers and cached input layouts are real (`DX-30`/`DX-31`/`DX-32`, Phase DX5 closed); textures,
  cube/3D textures, 2D/cube render targets (incl. real MSAA), MRT, sampler cache, and occlusion
  queries are real (`DX-40`–`DX-47`, Phase DX6 closed); blend/depth-stencil/rasterizer state
  objects + viewport/scissor are real (`DX-50`–`DX-53`, Phase DX7 closed). `DX-69` fog wiring is
  real for all 8 fog-capable variants (`sprite2d`/`instanced3d` genuinely have no fog cbuffer to
  wire) and is now exercised by a dedicated fog-on/off pixel test (Check AC, Phase DX10). **Phase
  DX10 (`DX-81`–`DX-85`) closed 2026-07-14**: 4 new state-object CTest entries (blend/depth-stencil/
  rasterizer, reusing the same backend-agnostic sources Vulkan reuses), a real backbuffer-resize
  test (`DX-83`, closes `DX-29`'s long-flagged never-exercised gap), a mutation-tested proof of
  discriminating power for the first landed pixel test (`DX-84`), and an automated DXVK-engagement
  gate now built into `scripts/run-wine-dxvk.sh` itself (`DX-85`) so a silent WineD3D fallback can
  never again pass as a green D3D11 CTest run. MRT's per-target MSAA-resolve/mip-regen-on-unbind is
  still only wired for the single-target case (`DX-43`), not yet for N>1 (`DX-46`'s own honest
  scope note, unchanged this session). The 5 combo `Clear*` variants and device-lost recovery are
  implemented but still not yet exercised by any test (fullscreen toggle likewise, per `DX-83`'s
  own scope note — real fullscreen-transition behavior needs `DX-90`). **Phase DX11 (docs) also
  closed 2026-07-14** — `docs/d3d11-backend.md`, a `D3D11` column in
  `docs/graphics-backend-feature-matrix.md`, and a `README.md` build section all now exist. **D3D11
  is complete per `plan_dx.md`'s own scope** — only `DX-90`/`DX-91` (real Windows hardware) remain
  open, both `needs_human`. **D3D12 (Phase DX12) is now ALSO fully closed** (`DX-100` through
  `DX-113` and `DX-115`) — real device/queue/heaps/command-lists/fence, resource-barrier tracking, a
  root-signature cache, a pipeline-state-object cache, real vertex/index buffers + 2D/cube textures,
  a real tested device-removed recreation path, all 10/10 stock shader variants drawing for real
  off-screen, a real `D3D12SpriteBatchBackend`, an audited+mutation-tested test suite, and full docs
  (`docs/d3d12-backend.md`, a `D3D12` column in `docs/graphics-backend-feature-matrix.md`, a
  `README.md` build section). Only `DX-114` (real Windows hardware) remains, `needs_human` — swap
  chain/`Present()` is D3D12's own additional real gap (genuinely crashes under this dev loop's
  Wine+vkd3d-proton, not a CNA bug), so unlike D3D11 no D3D12 proof is on-screen. See §8/§9.
- **D3D11 + `CnaTests`**: the full pre-existing GTest suite does not build under this backend —
  ~10 test files call POSIX-only `::setenv()` directly (see §4). Same gap applies to D3D12.
- **D3D12**: all 10/10 stock shader variants (`colored3d`/`textured3d`/`colored_textured3d`/
  `lit_textured3d`/`alpha_test3d`/`dual_texture3d`/`env_map3d`/`skinned3d`/`sprite2d`/`instanced3d`)
  draw for real off-screen via `DrawColoredPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`/
  `DrawInstancedPrimitivesEx`, plus a real `D3D12SpriteBatchBackend`; `D3D12_Smoke` 80/80 checks,
  mutation-verified. Still genuinely missing (not silently dropped, see `docs/d3d12-backend.md`): a
  public render-target backend, `Texture3D`, runtime-settable blend/depth-stencil/rasterizer state
  (PSOs hardcode `depthEnable=false`/`cullMode=None`), per-slot `SamplerState` (hardcoded static
  WRAP/linear samplers), occlusion queries, and custom `Effect` via `SpriteBatch::Begin(effect)`.
  `Present()`/swap-chain presentation still honestly throw/crash — real verification is `DX-114`'s
  job on real Windows hardware.
- XNA compiled `.fx` bytecode (`Effect` constructor throws `NotImplementedException`) — Phase 74.
- `Texture3D`/`TextureCube` cannot be bound as a shader sampler through the generic `EffectParameter`
  path — Task 863, needs an architecture decision (see §5).
- Android cross-compile — blocked on sibling `sharp-runtime` NDK build regressions (Task 920).

---

## 3. Recent changes

Most recent first. Full detail (exact code, discriminating-power verification) is in `plan_dx.md`
(D3D11) / `plan_graphics.md`/`plan_software.md` (everything else) — this section is a short index.

| Commit(s) | Summary |
|---|---|
| `b3289ac6` + this commit | **Phase DX15: 15 of 18 rows closed** (`DX-136`/`DX-137`/`DX-144` remain open for real reasons — see `plan_dx.md`'s Phase DX15 intro; `DX-136` in particular is blocked on a cross-backend shader gap, `alpha_test3d` has no vertex-color attribute at all). The last 3 rows (`DX-132` D3D12 `SpriteFont`, `DX-148` D3D12 `Model`, `DX-140`'s `FromStream`/`SaveAsPng` half) shared one blocker: they need a real `GraphicsDevice`, which forced a real window → a real swap chain → this dev loop's D3D12 crash path. Fixed by **`PresentationParameters::HeadlessEXT`** (NOXNA): a runtime opt-in for a genuinely windowless device that skips SDL video init entirely (so it needs no display server, and works in CI). Defaults false → production behavior byte-identical. All 3 rows then landed in the **routine plain-Wine** D3D12 CTest. **Real crash bug found**: `SetDepthTestEnabled`/`SetDepthWriteEnabled`/`SetBlendEnabled` were still `NotYetImplemented()` **throws** on D3D12, so any game calling `GraphicsDevice::SetDepthTestEnabled()` crashed — caught by `DX-148`'s Model test, the first thing to ever drive the shared `GraphicsDevice` path against D3D12. `D3D12_Smoke` 159→**169/169**. |
| `18a70bba`+ (DX-115) | **Phase DX12 fully closed — `DX-115` (docs)**: new `docs/d3d12-backend.md`, a `D3D12` column across all 7 applicable `docs/graphics-backend-feature-matrix.md` tables, and a `README.md` build section, mirroring D3D11's own `DX-95`–`DX-97`. Confirmed exact current numbers via a live run: `D3D12_Smoke` 80/80 checks. Only `DX-114` (real Windows hardware) remains open in `plan_dx.md`, `needs_human`. |
| `724b95f6` | **`DX-113` (test-suite formalization) closed** — audited the 74 checks `DX-102`–`111` already built, closed 2 real gaps (dedicated fog on/off test, `D3D12ResourceStateTracker`'s untracked-resource throw contract), mutation-tested Check M (`VertexColorEnabled` flip → exactly M1 failed, M2 untouched, reverted+reconfirmed). `D3D12_Smoke` 80/80 checks. Two gaps left honestly open (state-object testing not yet applicable; fence stall not independently measured under real GPU load). |
| `1996141f` | **`DX-111` closed for real — `env_map3d` lands, 10 of 10 stock variants real.** New `D3D12TextureCubeBackend` (6-face `ID3D12Resource`, `GetData()` left at the interface default no-op). Reused `dual_texture3d`'s `(3,2,2)` root-signature/PSO shape. Two unrelated real bugs found and fixed: the RTV descriptor heap's fixed 8-slot capacity was genuinely exhausted by the growing test suite (raised to 32), and `RecreateDeviceEXT()` never reset `boneConstantBuffer_`/`skinnedExtraConstantBuffer_`/`instancedPso_`. `D3D12_Smoke` grew 72→74/74 checks (Check U). |
| `f2fd9e77` | **`DX-111` extended — `skinned3d`/`instanced3d` land, 9 of 10 stock variants real.** `skinned3d` reused existing caches unchanged (`(3,1,1)` shape); `instanced3d` needed its own hand-built PSO/2-stream input layout + new `DrawInstancedPrimitivesEx()`. No new bugs. `D3D12_Smoke` grew 68→72/72 checks. |
| `aa2efab3` | **`DX-111` extended again (🟨, 7 of 10 stock variants) — `dual_texture3d` and `sprite2d` now also real and pixel-verified; `DX-112` (SpriteBatch) closed too.** Two real, non-obvious dev-loop bugs found and fixed, neither a CNA logic mistake caught by inspection, both only surfacing from an actual wrong pixel-readback result: (1) `dual_texture3d`'s 2 SRVs via ONE `D3D12_DESCRIPTOR_TABLE` range with `NumDescriptors=2` (populated by a fresh per-draw `CopyDescriptorsSimple` into a contiguous bump-allocated heap-slot pair) sampled as all-zero on the real GPU even though every CPU-side descriptor write was independently verified correct — fixed by switching `D3D12RootSignatureCache` to N SEPARATE single-descriptor tables (one root param per texture register), simpler code, not just a workaround, documented in that file's own doc comment for real-Windows re-verification later; (2) the first `sprite2d` pixel test assumed `UV = px/width` for its readback coordinates, but D3D rasterizes at pixel CENTERS (`UV = (px+0.5)/width`) — a genuine GPU sampling behavior, not a SpriteBatch bug — fixed by widening the test texture so each color spans 2 texels instead of 1. New `D3D12SpriteBatchBackend` (`D3D12SpriteBatch.hpp`/`.cpp`) mirrors `D3D11SpriteBatchBackend`'s exact quad-building formula, reuses `alpha_test3d`'s own `(1,1,1)` root signature, but needs its own hand-built PSO/input-layout (sprite2d's 32-byte vertex collides with `VertexPositionNormalTexture`'s existing stride-32 meaning, same real collision D3D11's own `DX-70` fork already found). Honest, documented gaps: `SpriteBatch::Begin(effect)` throws (no D3D12 custom-effect backend exists yet); sampler filter/address-mode setters are stored but not yet behaviorally real (no D3D12 dynamic-sampler-state system exists yet). `D3D12_Smoke` grew 61→**68/68 checks** (Checks Q/R). Still deferred: `env_map3d`/`skinned3d`/`instanced3d` (3 of 10). |
| `e6100176` | **`DX-111` extended (🟨, 5 of 10 stock variants) — `textured3d`/`colored_textured3d`/`lit_textured3d`/`alpha_test3d` now also real and pixel-verified, on top of `colored3d` below.** New `D3D12GraphicsBackend::DrawPrimitivesEx()`/`DrawIndexedPrimitivesEx()` overrides (`DrawPrimitivesExImpl`) replicate D3D11's own priority-chain dispatch (alpha-test > lit-textured (stride 32) > colored/textured/colored_textured bundle by stride); `dual_texture3d`/`env_map3d`/`skinned3d` explicitly throw named "not yet implemented" rather than silently drawing the wrong shader. Real SRV descriptor-table texture binding: a `D3D12TextureBackend`'s own SRV (already allocated in the shared CBV/SRV/UAV heap at texture-creation time, `DX-109`) is bound directly as a 1-descriptor table base — no separate copy step. Root-signature shapes: `alpha_test3d` needs its own `(1,1,1)` (single combined `PerDraw@b0`, no separate `FogParams`); `textured3d`/`colored_textured3d`/`lit_textured3d` all share one `(2,1,1)` shape and therefore one cached root-signature object. Landing `lit_textured3d` for real **corrected a stale speculative doc-comment claim** in `D3D12RootSignatureCache.hpp` (it had guessed `(2,0,0)` for `lit_textured3d`, written before anyone had actually read `lit_textured3d.frag.hlsl`'s real `t0`/`s0` texture binding). No new bugs — all 5 new checks (N/O/P) passed the first real Wine+vkd3d-proton run. `D3D12_Smoke` grew 51→**61/61 checks**: exact texture sampling + exact vertex-color tinting (`textured3d`/`colored_textured3d`), lit-vs-unlit-vs-background discrimination (`lit_textured3d`), and genuine `clip()` discard/pass (`alpha_test3d`) — same rigor D3D11's own Checks Q/R/S used. Still deferred: `dual_texture3d`/`env_map3d`/`skinned3d`/`sprite2d`/`instanced3d` (5 of 10) and `DX-112` (SpriteBatch, needs `sprite2d` first). |
| `0922f3aa` | **`DX-111` closed (🟨, `colored3d` only) — the first real triangle this D3D12 backend has drawn, pixel-verified off-screen.** New `BindOffscreenColorTargetEXT()`/`UnbindOffscreenColorTargetEXT()` NOXNA helpers on `D3D12GraphicsBackend` bind a caller-owned+tracked `ID3D12Resource`+RTV as a `Clear()`/draw target, standing in for the still-broken swap chain and the still-deferred public `D3D12RenderTargetBackend`. `Clear()` is now real (`ClearRenderTargetView` via the barrier tracker). `DrawColoredPrimitives()`/`DrawIndexedColoredPrimitives()` get/create a real `(2,0,0)` root signature + `colored3d`/stride-16 PSO, populate real `D3DPerDrawConstants`/`D3DFogConstants` (byte-identical to D3D11's) into new persistently-mapped UPLOAD-heap constant buffers (`GetOrCreatePerDrawConstantBufferEXT`/`GetOrCreateFogConstantBufferEXT`), and record a real `OMSetRenderTargets`/viewport/vertex+index-buffer/root-CBV/`DrawInstanced`(or indexed) sequence. `RecreateDeviceEXT()` extended to also reset the root-signature/PSO caches and the constant buffers (all tied to the old device). **Real bug found and fixed**: the PSO's default state (depth test on with no DSV bound, `CullCounterClockwiseFace` default) silently painted nothing on the first real run — no debug layer exists on this dev loop to report why; root-caused by bisecting (disabling depth had no effect; `CullMode::None` fixed it — the test triangle's real winding was back-face-culled after D3D's NDC→screen-space Y-flip), now a documented, hardcoded simplification (no D3D12 rasterizer-state-cache equivalent to D3D11's `DX-52` exists yet). New `D3D12_Smoke` Check M (before/after-`Clear()` pixel-region readback, same discipline as D3D11's Check P, for both the non-indexed and indexed draw paths) — **51/51 checks pass** (up from 48/48). Only `colored3d` landed; the other 9 stock variants and `DX-112` (SpriteBatch) are honest, explicit follow-up work. |
| `c6c0d80e` | **`DX-109`/`DX-110` closed (🟨 honest partial)**: new `D3D12Buffers.hpp`/`.cpp` (real `D3D12VertexBufferBackend`/`D3D12IndexBufferBackend`, DEFAULT-heap resource + fresh UPLOAD-heap staging per call + `CopyBufferRegion`, `CreateIndexBuffer32()` explicitly overridden and tested) and `D3D12Textures.hpp`/`.cpp` (real `D3D12TextureBackend`, row-pitch-aligned staging buffer + `CopyTextureRegion`, `d3dx12.h` absent so subresource indices computed directly). Both byte-exact round-trip proven via a real `D3D12_HEAP_TYPE_READBACK` buffer. Render targets/cube/3D textures deliberately triaged out (honest 🟨). `D3D12GraphicsBackend` grew a shared `D3D12ResourceStateTracker` member, `CheckDeviceRemovedEXT()` (detection-only, mirrors D3D11's `DX-27`), and `RecreateDeviceEXT()` (a real, tested full device-lifetime-group recreation). A real bug was caught in this task's own test (device-pointer-inequality is not sound proof of recreation — fixed to assert functional proof instead). New `D3D12_Smoke` Checks J/K/L — 48/48 checks pass (up from 31/31), 3 consecutive runs all green. |
| `60d42acf` | **`DX-106`/`DX-107`/`DX-108` closed**: new `D3D12ResourceStateTracker.hpp`/`.cpp` (per-resource `D3D12_RESOURCE_STATES` map, emits a transition barrier only on a genuine state change, proven via 7 new Checks against a real throwaway committed buffer), `D3D12RootSignatureCache.hpp`/`.cpp` (keyed by `(numCbvs, numSrvs, numSamplers)` binding shape, root CBVs + static-sampler SRV table), `D3D12PipelineStateCache.hpp`/`.cpp` (one PSO per full shader/stride/blend/depth/rasterizer/format tuple — **the first real `ID3D12PipelineState` this backend has created**, for `colored3d`/stride-16). `D3DVertexFormatHelper` grew `InputElementsForStrideD3D12()` (D3D12 bakes the input layout into the PSO desc directly, no `ID3D11InputLayout` equivalent). New `D3D12_Smoke` Checks G/H/I — 31/31 checks pass (up from 18/18). Stencil/scissor deliberately not yet part of the PSO key — honest, documented gap. |
| `18265d75` | **Phase DX11 fully closed (`DX-95`–`DX-98`)**: new `docs/d3d11-backend.md` (mirrors `docs/software-backend.md`/`docs/headless-backend.md`'s structure — status, what it's for/isn't, Wine+DXVK dev-loop setup, writing a test, an honest known-limitations list). A real `D3D11` column added to every applicable table in `docs/graphics-backend-feature-matrix.md` (2D SpriteBatch/SpriteFont, Stock Effects, RenderTarget/MSAA/mip/depth, Texture2D/Texture3D/TextureCube, GraphicsDevice state objects, OcclusionQuery, Model), honestly mixed ✅/🟨/⬜ per row rather than blanket-approved. New "Tested Compilers" row + "Build (Windows cross-compilation — D3D11 backend)" section in `README.md`. This closes `plan_dx.md`'s entire D3D11 scope — only `DX-90`/`DX-91` (real Windows hardware) remain, both `needs_human`. |
| `911440f2` | **Phase DX10 fully closed (`DX-81`–`DX-85`)**: 4 new CTest entries reusing the exact backend-agnostic `easygl_blendstate_*`/`easygl_depthstencilstate_*`/`easygl_rasterizerstate_*` sources Vulkan already reuses verbatim (`D3D11_BlendState_Opaque`/`AlphaBlend`, `D3D11_DepthStencilState_StencilEnable`, `D3D11_RasterizerState_CullMode`) — real pixel-behavior proof (blend math, stencil gating, winding-order culling), all 4 passing on the first run. New Check AB (`DX-83`) closes `DX-29`'s long-flagged never-exercised resize gap: real 64×64→96×80 resize via `GraphicsDeviceManager`, DXVK's own presenter log confirms the new buffer size, correct post-resize readback. `DX-84`: real mutation-test pass on `DX-61`'s `colored3d` check — a first mutation (reversed WVP order) was an honest false negative since that check uses identity matrices, a second (`VertexColorEnabled` flipped off) genuinely broke it, verified reverted. `DX-85`: `scripts/run-wine-dxvk.sh` now automatically greps every run's output for a `DXVK: <version>` marker and fails loudly (exit 3) if absent, even overriding a 0 exit code from the wrapped program — caught a real false-positive live (`D3D11_Common` never opens a device) and added a distinct, narrowly-scoped `CNA_D3D11_SKIP_DXVK_GATE=1` opt-out for it via CTest `ENVIRONMENT`. New Check AC closes `DX-69`'s own honestly-flagged fog-on/off gap (exact-color discrimination). `D3D11` CTest total now 6 tests, `D3D11_Smoke` 69 + `D3D11_Common` 23 + 4 new tests, all passing. |
| `5bde8ab5` | **Phase DX9 fully closed (`DX-70`/`DX-71`/`DX-72`)**: new `D3D11SpriteBatchBackend` (`D3D11SpriteBatch.hpp`/`.cpp`) — real quad batching feeding the stock `sprite2d` pipeline (destination/source-rect/origin/rotation/`SpriteEffects`-flip, matching `EasyGLSpriteBatchBackend`'s formula), a genuinely-working `SetTransformMatrix()` (CPU-side `Vector2::Transform()` per vertex — a real improvement over `VulkanSpriteBatchBackend`'s own silent no-op), custom `Effect` support reusing `D3D11EffectBackend` (`DX-58`) via a new `SetViewportSizeEXT()` that fills the `vpSize` slot that class had already reserved, and real `TextureAddressMode::Wrap`/`Mirror` (no new implementation needed — `D3D11SamplerCache`, `DX-44`, already handled it; this row is verification). Tested through the **real public `SpriteBatch`/`Texture2D` API**, not the raw backend interface — 6 new `D3D11_Smoke` Checks Y/Z/AA, all passing on the first real Wine+DXVK run, including two deliberately discriminating Wrap/Mirror probe pixels. 64/64 smoke checks pass (up from 58/58), `D3D11` CTest total now 87/87. |
| `6c591a0b` | **Phase DX8 fully closed (`DX-65`/`DX-66`/`DX-67`/`DX-68`/`DX-69`/`DX-58`)**: `dual_texture3d` (two real SRVs/samplers, fog at `register(b2)`), `env_map3d` (new `D3DEnvMapPerDrawConstants`/`D3DEnvMapConstants`, real `TextureCube` SRV via new `GetSrvForTextureCubeEXT()`, reflection direction geometrically constrained to land inside one distinct cube face), `skinned3d` (new `D3DSkinnedExtraConstants`, `D3DBoneConstants` genuinely populated via `memcpy` from `GpuDrawParams::boneTransforms` — confirmed not a transpose bug), `instanced3d` (real `DrawInstancedPrimitivesEx()`, new fixed `INSTANCEWORLD0`–`3` instanced input layout) all draw for real now; `sprite2d` deliberately left unwired (real home is `SpriteBatch`, Phase DX9). `DX-69` fog now wired for all 8 fog-capable variants. `DX-58`: new `D3D11EffectBackend` (runtime `D3DCompile()`, `d3dcompiler` now linked into the main backend target for real), mirrors `VulkanEffectBackend`'s fixed-slot uniform convention — independently GPU-proven, not yet consumed by a `SpriteBatch` draw loop. 5 new `D3D11_Smoke` Checks T/U/V/W/X, all passing on the first real run — 58/58 checks (up from 51/51), `D3D11` CTest total now 81/81. |
| `d55c5fab` | **`DX-62`/`DX-63`/`DX-64` (+ `DX-69` partial)**: new `DrawPrimitivesEx()`/`DrawIndexedPrimitivesEx()` overrides (sharing one `DrawPrimitivesExImpl` helper) give real `GpuDrawParams`-driven draws for `textured3d`/`colored_textured3d` (stride 20/24), `lit_textured3d` (stride 32, first real `D3DLightingConstants` consumer, full Blinn-Phong), and `alpha_test3d` (new `D3DAlphaTestConstants` struct); `dual_texture3d`/`env_map3d`/`skinned3d` throw named not-yet-implemented errors. New `D3D11_Smoke` Checks Q/R/S — exact texture sampling, exact vertex-color tinting, lit-vs-unlit plausibility, and `clip()` discard/pass both proven for real — 51/51 checks pass (up from 44/44), `D3D11` CTest total now 74/74. Zero bugs found. |
| `dc9fa753` | **Phase DX8 foundational tasks (`DX-60`/`DX-60a`/`DX-61`)**: new `D3DConstantBuffers.hpp` (`D3DPerDrawConstants`/`D3DFogConstants`/`D3DLightingConstants`/`D3DBoneConstants`, offsets `static_assert`-verified against the real HLSL). `DrawColoredPrimitives()`/`DrawIndexedColoredPrimitives()` now do a real `colored3d` draw (stride-16 only) instead of throwing. Found+fixed a real gap: `SetRenderTargets(nullptr, 0)` never restored the back buffer after a prior MRT bind. New `D3D11_Smoke` Check P — the backend's first real, pixel-verified 3D triangle — 44/44 checks pass (up from 42/42), `D3D11` CTest total now 67/67. |
| `1076cd77` | **Phase DX7 (`DX-50`–`DX-53`)**: new `D3D11StateObjectCache.hpp`/`.cpp` (`D3D11BlendStateCache`/`D3D11DepthStencilStateCache`/`D3D11RasterizerStateCache`, cached real `ID3D11BlendState`/`ID3D11DepthStencilState`/`ID3D11RasterizerState` objects) plus a new `D3DStateMapping::StencilOperationToD3D11`. Implemented `ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`/`SetBlendFactor`/`SetReferenceStencil`/`SetViewport`/`SetScissorRect` for real (previously all no-ops). Real, documented finding: XNA `DepthBias` float uses the same "r"-scaled convention as this project's Vulkan/EasyGL backends (Task 767) — D3D11's own `DepthBias` field is `INT`, so this rounds rather than truncates. New `D3D11_Smoke` Check O — 42/42 checks pass (up from 29/29), `D3D11` CTest total now 65/65. Phase DX7 fully closed. |
| `afc17218` | **Phase DX6 (`DX-40`–`DX-47`)**: new `D3D11Textures.hpp`/`.cpp` (2D/cube/3D textures), `D3D11RenderTargets.hpp`/`.cpp` (2D/cube render targets, real device-queried MSAA), `D3D11SamplerCache.hpp`/`.cpp`, `D3D11OcclusionQuery.hpp`/`.cpp`, plus one real MRT `OMSetRenderTargets` call in `D3D11GraphicsBackend::SetRenderTargets()`. Found+fixed a real gap: `Clear()`/`ClearColorAndDepth`/etc. were hardcoded to the back buffer's own RTV/DSV since Phase DX4 — now routed through new `currentColorRTVs_`/`currentDSV_` tracking so a bound custom render target actually gets cleared. New `D3D11_Smoke` Checks H–N — 29/29 checks pass (up from 18/18), `D3D11` CTest total now 52/52. Phase DX6 fully closed. |
| `2c7e0cd3` | **Phase DX5 (`DX-30`/`DX-31`/`DX-32`)**: new `D3D11Buffers.hpp`/`.cpp` (real `D3D11_USAGE_DYNAMIC` vertex/16-bit-and-32-bit-index buffers, `Map`/`Unmap`-updated, `SetDataOptions` mapped to `D3D11_MAP_WRITE_*`) and `D3D11InputLayoutCache.hpp`/`.cpp` (cached, real `CreateInputLayout()` per (shader variant, stride)). Found+fixed a real gap: `D3D11GraphicsBackend` never overrode `CreateIndexBuffer32`, silently inheriting a 16-bit-only default. New `D3D11_Smoke` Checks E/F/G — 18/18 checks pass (up from 13/13), `D3D11` CTest total now 41/41. Phase DX5 fully closed. |
| `daa742fc` | **`DX-15-embed`**: new `D3DShaderCache.hpp`/`.cpp` (`D3DShaderVariant` + `CreateVertexShaderForVariant`/`CreatePixelShaderForVariant`/`GetVertexShaderBytecode`/`GetPixelShaderBytecode`), `D3D11GraphicsBackend::GetDeviceEXT()` accessor, and a new `D3D11_Smoke` Check D creating real `ID3D11VertexShader`/`ID3D11PixelShader` objects for all 10 `DX-13-hlsl` variants through a live device — 13/13 checks pass (up from 3/3), `D3D11` CTest total now 36/36. Phase DX3 fully closed. |
| `18a70bba` | **`DX-14-compile`**: `hlsl_compiler_tool.cpp` (MinGW-built `D3DCompile()`-calling `.exe`) + `compile_shaders_hlsl.py`, run through `scripts/run-wine-dxvk.sh` — all 20 `DX-13-hlsl` shaders compiled cleanly to real DXBC, embedded in `hlsl_shaders.hpp`. |
| `aa62de83` | **`DX-13-hlsl`**: all 20 HLSL shader files (10 stock variants × vertex/pixel) hand-ported line-by-line from the Vulkan GLSL source into `D3DCommon/shaders/*.hlsl`. |
| `9f5e13f4`/`6d9cec89` | **D3DCommon shared core** (`plan_dx.md` `DX-11-fmt`/`DX-12-state`/`DX-16-vtx`): `D3DFormatMapping` (full `SurfaceFormat`/`DepthFormat`→`DXGI_FORMAT`), `D3DStateMapping` (`Blend`/`BlendFunction`/`CompareFunction`/`CullMode`/`FillMode`/`TextureAddressMode`/`TextureFilter`→`D3D11_*`, with the D3D11/D3D12 enum-identity claim actually verified against both real SDK headers, not assumed), `D3DVertexFormatHelper` (stride-keyed `D3D11_INPUT_ELEMENT_DESC` arrays). New `D3D11_Common` CTest, 23/23 checks, mutation-verified discriminating (temporarily broke `CullModeToD3D11`, confirmed the test caught it, reverted). |
| `492a1a26`/`482057ec` | **D3D11GraphicsBackend** (`plan_dx.md` `DX-10`–`DX-29`, `DX-80`): real device creation (feature-level fallback array, graceful debug-layer degradation), factory chain + tearing query, modern flip-model swap chain, back-buffer RTV/depth-stencil, `Clear()`/`Present()`/`ReadBackbuffer()` (RowPitch-aware), device-lost detection scaffolding. `CNA_GRAPHICS_BACKEND=D3D11` CMake wiring (D3D11-only, no D3D12 scaffolding). New `D3D11_Smoke` CTest (3/3, real DXVK-verified pixel round-trip). Along the way, fixed 4 pre-existing MinGW-portability bugs unrelated to D3D11 itself (2 in sibling `sharp-runtime`: `NOMINMAX` redefinition, 2× `-Werror=unused-parameter`; 2 in `cna_graphics`: `ContentManager.cpp`'s `Video`/FFmpeg guard missing `__MINGW32__`, `cna_net_two_process_harness` built unconditionally despite its own consumer test being excluded on `WIN32`). |
| `f45c8a22` | `ContentManager.cpp`'s `Video`/FFmpeg content-reader registration guard now also excludes `__MINGW32__`, matching `CMakeLists.txt`'s own `CNA_FFMPEG_AVAILABLE` computation (previously only excluded `__EMSCRIPTEN__`/`__ANDROID__`, leaving a MinGW build with an unresolved `Media::Video::Video()` reference). |
| `9662bb97`…`7f8273f4` | **`plan_dx.md` created and iterated** (2026-07-13): D3D11/D3D12 backend plan, refined across 3 rounds of the project owner's own technical review (device-creation robustness, link-library scoping, three-way resource-lifetime split), then Phase DX1 (Wine+DXVK dev-loop setup, `dxvk-wine64` installed, `scripts/run-wine-dxvk.sh` added, `programs.md` §9) executed and closed with real, DXVK-verified results before any backend code was written. |
| `946b8765` and earlier | Software backend Phase S9 close, WebGPU 3D shader work, `SkinnedEffect`/`EnvironmentMapEffect` fixes, Bgfx render-target crash-cluster fix, XNA culling/depth-occlusion audits — see `plan_graphics.md`/`plan_software.md`/`plan_webgpu.md` and `git log --oneline` for full detail, not repeated here. |

---

## 4. Current blocker / main problem

**No hard blocker — `plan_dx.md`'s scope is entirely closed for both backends' software/logic
layer.** D3D11: Phase DX1 through DX11 are all ✅ except `DX-90`/`DX-91` (real Windows hardware,
`needs_human`). D3D12: Phase DX12 (`DX-100` through `DX-113`/`DX-115`) is all ✅ except `DX-114`
(real Windows hardware, `needs_human`) — no such machine is available in this dev environment for
either gate. **D3D12's swap-chain crash is fixed** (2026-07-14): launching through a real,
correctly-bootstrapped Proton runtime (`scripts/run-proton-vkd3d.sh`, not the isolated hand-built
prefix `DX-100`/`DX-102` originally used) makes `CreateSwapChainForHwnd` return real `S_OK` —
confirms the earlier diagnosis was right (vkd3d-proton needs its matched `dxgi.dll`, not just
`d3d12.dll`), not a CNA bug. **`DX-116` (real `Present()`/back-buffer rendering) is also closed**:
a real 10-frame `Clear()`+`Present()` loop through that same Proton launch succeeds with no
throw/crash, reproduced twice — D3D12 can now genuinely put pixels on a real screen, kept as a
manual diagnostic (not a CTest, Proton's bootstrap is too heavy for routine runs). **`DX-117`
(real `D3D12RenderTargetBackend`/`D3D12RenderTargetCubeBackend` + MRT) is also closed (🟨)**:
`CreateRenderTarget2D`/`SetRenderTarget2D`/`SetRenderTargets`/`CreateRenderTargetCube` are all real
now through the public `IGraphicsBackend` API — bind+`Clear()`+draw+unbind and a genuine 2-target
MRT clear are all exact-color pixel-verified. **`DX-118` (real `BlendState`/`DepthStencilState`/
`RasterizerState` → PSO state) is also closed**: `ApplyBlendState`/`ApplyDepthStencilState`/
`ApplyRasterizerState` are now real, feeding tracked state into every PSO-key construction (3 real
draw-path call sites) instead of the old hardcoded `depthEnable=false`/`cullMode=None` literals —
additive blend, cull-mode culling/un-culling, and real per-pixel depth-test gating (via a real bound
DSV, both draw orderings + a depth-disabled control) are all exact-pixel-verified. A real,
pre-existing, functionally-inert mislabeling bug was found in `D3D12PipelineStateDesc.hpp`'s own
default-value comments (documented in `plan_dx.md`'s `DX-118` row, not fixed — regression risk
avoided by leaving it alone -- fixed in a same-day follow-up commit instead). **`DX-119` (real
per-slot `SamplerState`) is also closed**: a real `D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER` heap +
`D3D12SamplerCache` + dynamic per-texture-slot sampler descriptor tables (replacing the old
hardcoded static LINEAR/WRAP sampler), proven with a genuine `TextureAddressMode::Wrap`-vs-`Clamp`
discriminating pixel probe (same geometry/UVs, opposite sampled color, purely from the
`SamplerState` change) plus cache identity/distinctness proof. **`DX-120`
(`D3D12OcclusionQueryBackend`) is also closed**, closing Phase DX15's own `DX-147` D3D12 half too:
a real `ID3D12QueryHeap`+readback buffer, proven with an exact `PixelCount()=4096` for a
full-viewport visible triangle and exactly `0` for the same query reused around off-screen (clipped)
geometry. **A real, non-obvious bug was found and fixed while landing this**: `BeginQuery`/
`EndQuery` must share one command-list submission with the draw(s) they bracket (a Vulkan/
vkd3d-proton constraint), which this backend's own per-draw-call self-submission architecture
didn't satisfy — fixed via a new `D3D12GraphicsBackend::SetActiveOcclusionQueryEXT()` that every
draw-recording method now checks and brackets its own recording with. **`DX-121`
(`D3D12EffectBackend`) is also closed**: real runtime `D3DCompile()` builds a real PSO+constant
buffer, pixel-verified with the exact expected color, plus a real broken-HLSL compile-failure
check. `D3D12SpriteBatchBackend`'s own `SpriteBatch::Begin(effect)` wiring was implemented for
real too (reusing the same shared root signature), but is honestly **not** independently
CTest-proven — it needs a real `GraphicsDevice`, whose constructor unconditionally creates a real
window for any non-Headless/Software backend, the same crash-prone path `DX-100`/`DX-102` already
found for D3D12 outside a Proton-managed launch (confirmed by reading `GraphicsDevice.cpp`
directly). **`DX-122` (`D3D12Texture3DBackend`) and `DX-123` (`D3D12TextureCubeBackend::GetData()`
real readback) are also closed — Phase DX13 (`DX-116`–`DX-123`) is now FULLY complete.** `DX-122`:
a real byte-exact sub-volume upload/readback round-trip (2×2×2 sub-cube at an off-center `(1,1,0)`
offset within a 4×4×2 volume, different solid color per Z slice, genuinely exercising X/Y/Z offset
math and per-slice pitch). `DX-123`: real per-face, per-sub-rect readback (two different faces at
two different offsets, each byte-exact; a genuinely untouched region reads real zero-initialized
GPU content, not stale CPU-buffer poison — a live-readback proof, not just "some data came back").
`D3D12_Smoke` CTest (off-screen only): **130/130 checks**, all 10/10 stock shader variants +
`SpriteBatch` + device-removed recovery + render targets/MRT + real state objects + real per-slot
samplers + real occlusion queries + real custom `ShaderEffect` + real `Texture3D` + real
`TextureCube` readback. **Phase DX13: 6 of 8 rows closed, 2 partial** (`DX-117` render-target MSAA/mip-chains, `DX-121` SpriteBatch custom-effect wiring — see `plan_dx.md`). Phase DX15 progress (2026-07-14, first
chunk): `DX-131` (`SpriteBatch` rotation/scale/crop-rect, both D3D11 and D3D12 — `D3D11_Smoke`
72/72, `D3D12_Smoke` +3 checks) and `DX-133` (D3D12 `SpriteBatch` sampler wiring — a real,
previously-inert bug fixed: `SetSamplerFilter`/`SetSamplerAddressMode` now genuinely drive slot 0's
sampler via `ApplySamplerState`, not silently ignored) are both closed. `DX-132` (D3D12 `SpriteFont`
test) hit a genuine architectural blocker, documented not forced: `GraphicsDevice`'s constructor
creates a real window for every backend except `HEADLESS`/`SOFTWARE`, and a real windowed
`GraphicsDevice` under D3D12 on this dev loop would hit the same swap-chain crash `DX-100`/`DX-102`
already root-caused — left open, see `plan_dx.md`'s own `DX-132` row for the real options. **Phase
DX14 (D3D11 verification hardening) and the rest of Phase DX15** were added 2026-07-14 and are
authorized and actively in progress — see `plan_dx.md` for the full task list and ordering
rationale.

**Phase DX15 progress (2026-07-14, second chunk): `DX-134`/`DX-135`/`DX-137` closed real, `DX-136`
investigated and found genuinely unimplementable without cross-backend shader work.** `DX-134`
(`EnvironmentMapEffect` base-lerp alpha): confirmed the term IS implemented in the shared HLSL,
added an `envMapAmount=0.0` check to both backends proving the lerp is a real graduated blend, not
an on/off gate. `DX-135` (`SkinnedEffect.WeightsPerVertex`): a real, non-obvious math property
found empirically (not just theorized) while debugging a failed first attempt — a single active
bone's weight always cancels out via the GPU's own homogeneous divide, so the discriminating test
needed two genuinely-blended bones, not a single weighted one; both backends' tests now pass for
real. `DX-136` (`AlphaTestEffect.VertexColorEnabled`): investigated and found `alpha_test3d`'s own
vertex shader has **no color attribute at all**, inherited identically from the original Vulkan
GLSL port — a real, shared, cross-backend gap needing new vertex-attribute/shader work, correctly
left open rather than forced. `DX-137` (fog for non-`colored3d` variants): closed for `textured3d`
as a representative variant (found and fixed a real test-fixture bug along the way — the fog
fixture needs its own vertex buffer at Z=fogEnd, not the shared Z=0 one); the other 6 of 7 variants
remain honestly open. `D3D11_Smoke` **77/77 checks**, `D3D12_Smoke` **135/135 checks**, both
verified via a real `ctest` run with no regression.

**Phase DX15 progress (2026-07-14, third chunk): `DX-138`/`DX-139` closed real, `DX-148` investigated
and found the identical architectural blocker `DX-132` already documented.** `DX-138` (D3D12
multi-light/`EmissiveColor`): 4 new checks (`EE1`–`EE4`), each an EXACT expected RGB derived by hand
from `lit_textured3d.frag.hlsl`'s real math — `DirectionalLight1` alone → exact `(255,0,0)`,
re-disabling it → exact `(0,0,0)` (proves the first result was real, not a leaked default),
`DirectionalLight2` alone → exact `(0,255,0)`, `EmissiveColor` alone with every light off → exact
`(0,0,255)` (a genuinely constant, light-independent term). `DX-139` (D3D12 specular): 2 new checks
(`FF1`/`FF2`), using a real methodology that avoids `DX-125`'s own D3D11 "specular zeroed for
CPU-determinism" gap instead of repeating it — geometry chosen so the Blinn-Phong half-vector
exactly equals the surface normal (`dot(H,N)=1`), collapsing `pow(1,SpecularPower)=1` regardless of
the power value, giving an exact expected white `(255,255,255)` vs. black `(0,0,0)` with
`SpecularColor` zeroed. All 6 new checks passed on the first real Wine+vkd3d-proton run. `DX-148`
(D3D12 `Model` test): confirmed directly from source that `ModelMesh::Draw()` needs a real
`GraphicsDevice*` (`SetVertexBuffer`/`DrawIndexedPrimitives`/`Effect::Apply()`), and
`GraphicsDevice.cpp`'s `createOrAttachWindow()` has no no-window special case for `D3D12` (only
`HEADLESS`/`SOFTWARE`) — constructing one would hit the exact same swap-chain crash `DX-132` already
found; manually bypassing `GraphicsDevice`/`Effect::Apply()` was considered and rejected since that
would just re-test `VertexBuffer`/`IndexBuffer` draws under a `Model`-shaped label, not a genuine
proof of `ModelMesh::Draw()`'s own code path. Left open rather than forced. `D3D11_Smoke` still
77/77 (untouched), `D3D12_Smoke` **135→141/141 checks**, verified via a real `ctest` run with no
regression.

**Phase DX15 progress (2026-07-14, fourth chunk): `DX-141`/`DX-142` closed real, `DX-140` closed 🟨
(NPOT done, `FromStream`/`SaveAsPng` deferred to the same real blocker `DX-132`/`DX-148` already
found).** `DX-140` (`Texture2D.FromStream`/`SaveAsPng`/NPOT): both `FromStream()` and every real
`Texture2D` constructor need a `GraphicsDevice&` (confirmed from source) — for D3D12 that's the
identical windowed-`GraphicsDevice` swap-chain-crash blocker, so those two stay honestly open;
**NPOT** (testable at the raw-backend level, no `GraphicsDevice` needed) closed for real on both
backends — a genuine `5×3` non-power-of-two texture (20 bytes/row, doesn't divide evenly into
`D3D12_TEXTURE_DATA_PITCH_ALIGNMENT`), solid color to isolate the upload path from unrelated
bilinear-sampling concerns, sampled via a real `textured3d` draw with an exact-color readback.
`DX-141` (D3D12 mip level > 0): rather than fighting the stock shaders' driver-dependent implicit-LOD
`Sample()` selection, reads mip level 1 back directly via `CopyTextureRegion` (the same discipline
`DX-122`/`DX-123`'s own `GetData()` already established) — proves both the level-1 upload is exact
AND level 0 stays genuinely unaffected. `DX-142` (D3D11 all-16-sampler-slots): binds all 16 slots
simultaneously with 16 distinct configurations, then re-verifies every slot's bound object *after*
all 16 were applied — confirms no later slot's bind clobbered an earlier one (the specific
off-by-one/aliasing risk this row was written to catch). All new checks passed on the first real
run. `D3D11_Smoke` 77→**81/81 checks**, `D3D12_Smoke` 141→**147/147 checks**, both verified via a
real `ctest` run with no regression. Outside the D3D plan, the standing backlog (Phase 79's
153-sample `../cna-samples` re-audit, Task 945's HLSL→GLSL tooling decision, Task 952's deferred
Bgfx bug) is unchanged — see §8/§9.

**One real, separate, documented problem**: the full `CnaTests` GTest suite does not build under
`CNA_GRAPHICS_BACKEND=D3D11` (and, by the same root cause, `D3D12`).

- **Symptom**: `cmake --build cmake-build-d3d11 --target CnaTests` fails with
  `error: '::setenv' has not been declared; did you mean 'getenv'?` across roughly 10 test files.
- **Failing command**: `cmake --build cmake-build-d3d11 --target CnaTests -j4`
- **Affected files**: mostly `tests/Microsoft/Xna/Framework/Audio/*Tests.cpp` (confirmed via
  `grep -rl '::setenv' tests/`) — POSIX-only `::setenv()`/`::unsetenv()` calls with no Windows
  fallback (`_putenv_s`/`SetEnvironmentVariable`).
- **Suspected cause**: these test files were written and only ever built against
  Linux/EasyGL/Vulkan/Bgfx; nobody has linked a MinGW cross-target beyond `SDL_RENDERER` (which has
  much lighter test coverage needs) recently enough to catch this.
- **What's already been tried**: nothing yet — found and explicitly left unfixed this session
  (deliberately out of scope; see §9). `CNA` itself and standalone test executables
  (`cna_test_d3d11_smoke`, `cna_test_d3d11_common`, `d3d12_smoke_test`) build and run fine — only the
  full `CnaTests` target is affected.
- **Not a blocker for continued backend development**: this project's own established pattern
  (Software/Headless backends) is standalone example-based CTests (`examples/d3d11_*.cpp`/
  `examples/d3d12_*.cpp`) until a backend is mature enough to run the full suite.

---

## 5. Known bugs and limitations

| Status | Description | Task |
|---|---|---|
| Incomplete, real gap | `CnaTests` does not build under `CNA_GRAPHICS_BACKEND=D3D11` — ~10 test files call POSIX-only `::setenv()` directly. See §4. | — |
| Incomplete, honestly unexercised | D3D11's 5 combo `Clear*` variants (`ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil`), window resize (`EnsureSwapChainSize()`), and device-lost detection (`CheckDeviceRemoved()`) are all implemented but never exercised by any test. | — |
| Needs verification (real hardware, not Wine) | D3D11's debug-layer-missing fallback (`DXGI_ERROR_SDK_COMPONENT_MISSING` retry) and the `E_INVALIDARG`/drop-feature-level-11_1 fallback never fired on this machine (DXVK always provides what's needed) — genuinely untested code paths. Needs a real Windows machine without the D3D11 SDK debug layer installed, or a driver that rejects an explicit 11_1 request. | — |
| **DEFERRED (2026-07-11)** — investigated 3 times, not fixed, explicitly paused by the project owner | A `Depth24Stencil8`-attached `RenderTargetCube` face produces no colour output at all on Bgfx. See `plan_graphics.md`'s Task 952 entry for the full investigation trail. **Do not resume without explicit instruction** — see §9. | 952 |
| Confirmed bug, environment limitation | `Bgfx_RenderTarget2D_MsaaResolve`: this sandbox's bgfx OpenGL path negotiates only a legacy GL 2.1 context under which MSAA-flagged framebuffer textures don't sub-pixel resolve. | — |
| Confirmed bug | `EasyGL_MRT_TwoAttachments`: a basic 2-target MRT setup doesn't render correctly. Off-limits for opportunistic fixing (§9). | 145 |
| Confirmed bug | `Vulkan_DepthBias` fails; pre-existing, not investigated further. | — |
| Confirmed bug, not fixed | `GraphicsDevice.ReferenceStencil`'s independent-override semantics have zero backend connection on EasyGL/Bgfx (Vulkan already fixed). | 872 |
| Confirmed bug, found+reverted, needs its own task | `BgfxGraphicsBackend::DrawIndexedPrimitivesEx`'s non-wireframe path silently discards `GpuDrawParams::startIndex`/`baseVertex`. Not visible in any current sample/test. | 954 |
| Needs architecture decision | `Texture3D`/`TextureCube` inherit `GraphicsResource`, not `Texture` — no shader-sampling bind path via the generic `EffectParameter` route. Two named fix options, neither picked. | 863 |
| Needs architecture decision | `GraphicsDevice` state objects use C++ value semantics; FNA uses reference semantics. Project-wide implication. | 869 |
| Needs project-owner decision | HLSL→GLSL conversion approach for Phase 78 (manual port vs. `dxc`+`SPIRV-Cross`). | 945 |
| Incomplete, cross-repo | Android NDK cross-compile blocked by build regressions in sibling `sharp-runtime`. | 920 |
| Known, cosmetic | `cna_demo_xact` example fails to build (`examples/demo_xact/Content` doesn't exist in this checkout). | — |
| Known characteristic, not a bug | WebGPU's surface prefers an sRGB swapchain format, so raw `GetBackBufferData()` readback returns gamma-encoded, not linear, bytes. Every WebGPU 3D readback test works around it with pure 0/1 extreme expected colours. | — |
| **Needs re-verification** | This file previously claimed "this sandbox has no real X server on `:0`" (Linux-backend testing needs `Xvfb :99`). This session's D3D11/Wine work directly observed `DISPLAY=:0` as a real, usable desktop session with real GPU access (used for DXVK). Whether this affects Linux-native backend (EasyGL/Vulkan/Bgfx) testing conventions was **not re-checked this session** (D3D11 work never touched those backends) — verify before assuming either claim next time a Linux backend is touched. | — |

---

## 6. Architecture notes

### Main modules

| Layer | Location | Notes |
|---|---|---|
| XNA public API | `include/Microsoft/Xna/Framework/…` | Must match XNA 4.0 / FNA exactly |
| Backend contracts | `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` | One abstract interface, 8 concrete implementations (EasyGL/Vulkan/Bgfx/SDL_Renderer/WebGPU/Headless/Software/D3D11) |
| EasyGL backend | `src/CNA/Internal/Backends/EasyGL/` | Primary; OpenGL ES 3.2 via the `easy-gl` wrapper (sibling repo) |
| Vulkan backend | `src/CNA/Internal/Backends/Vulkan/` | Defers a whole frame's draws into one command buffer |
| Bgfx backend | `src/CNA/Internal/Backends/Bgfx/` | Similar per-frame/per-view batching to Vulkan |
| SDL_Renderer backend | `src/CNA/Internal/Backends/SdlRenderer/` | 2D-only; every 3D method throws |
| WebGPU backend | `src/CNA/Internal/Backends/WebGPU/` | Native 2D + partial 3D baseline, `plan_webgpu.md` |
| Headless backend | `src/CNA/Internal/Backends/Headless/` | No GPU/window, CI-oriented, `plan_headless.md` |
| Software backend | `src/CNA/Internal/Backends/Software/` | CPU rasterizer, `plan_software.md` |
| **D3D11 backend** | `src/CNA/Internal/Backends/D3D11/`, `include/CNA/Internal/Backends/D3D11/` | **New.** Windows-only, MinGW-w64 cross-compiled. `D3D11GraphicsBackend` — real device/swap-chain/back-buffer/clear/present/readback; everything else an honest "not yet implemented" throw. `plan_dx.md`. |
| **D3DCommon** | `src/CNA/Internal/Backends/D3DCommon/`, `include/CNA/Internal/Backends/D3DCommon/` | **New.** Shared format/state/vertex-layout mapping tables (`cna_backend_graphics_d3dcommon` static lib), consumed by D3D11 today and a future D3D12 backend. |
| CNA utilities | `include/CNA/`, `src/CNA/` | `NOXNA` helpers, logging, math |
| sharp-runtime | `../sharp-runtime/` (sibling repo) | `System.*` types, primitive aliases |
| Content pipeline | `src/Microsoft/Xna/Framework/Content/ContentManager.cpp` | Single large file, one `ContentTypeReader` subclass per asset type |

### Critical invariants (do not break these)

- **`NOXNA` macro** tags every non-XNA extension in public headers.
- **C# properties** → `getXProperty()` / `setXProperty()` — never public fields on the XNA surface.
- **Type aliases** from `SharpRuntime/SharpRuntimeHelper.hpp` (`bytecs`, `Single`, `String`, …) —
  never raw `uint8_t`/`float`/`std::string` directly on XNA API surfaces.
- **Backend selection is compile-time** — no runtime branch between backends in the same binary.
- **Stride-keyed vertex layout** — only strides 16/20/24/32/52 work correctly for 3D, on every
  backend including D3D11.
- **Doxygen required** on every public `.hpp` member. **SPDX header** on every `.hpp`/`.cpp`.
- **`Texture3D`/`TextureCube` inherit `GraphicsResource`, not `Texture`** — known deviation from
  FNA (Task 863).
- **Vulkan/Bgfx clear semantics**: a frame's clear always applies once, before all of that frame's
  draws, regardless of call order — a new pixel test proving a mid-frame `Clear()` change must
  split the sequence across separate real frames (see §5/`examples/*_clear_stencil_test.cpp`).
- **`Effect`/`EffectTechnique`/`EffectPass`/`EffectParameterCollection`** — must use
  `vector<unique_ptr<T>>`, not `vector<T>` by value, if any code caches a raw pointer/reference
  across an `Add()` call.
- **D3D11-specific**: three independent resource-lifetime groups — device (created once, torn down
  only on device-removed recovery), swap chain (created once, resized via `ResizeBuffers`, never
  recreated by a plain resize), window-size views (recreated on every resize). A plain resize must
  never re-run the device-lifetime tearing-capability query or recreate the swap-chain object.
  **COM ownership is `Microsoft::WRL::ComPtr<T>` throughout** — no bare `Release()` call sites; a
  `ComPtr` being re-populated (not first-time constructed) must use `ReleaseAndGetAddressOf()`, not
  `GetAddressOf()`. **No `D3D12` scaffolding exists** (not even the CMake `STRINGS` entry) —
  `plan_dx.md` design decision 9 deliberately keeps it that way until Phase DX12 is authorized.

### FNA reference

Authoritative behavioral reference: `/rv/data/library/github.com/FNA-XNA/FNA/src`. Document
intentional CNA/FNA divergence in the commit/PR description and in `plan_graphics.md`/`plan_dx.md`
— not as a source comment explaining the deviation's rationale.

---

## 7. Useful commands

```bash
# Configure (pick one backend per build dir) -- Linux-native backends
cmake -B cmake-build-debug  -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN -DCNA_BUILD_TESTS=ON
cmake -B cmake-build-bgfx   -DCNA_GRAPHICS_BACKEND=BGFX   -DCNA_BUILD_TESTS=ON

# Configure -- D3D11 (Windows cross-compile via MinGW-w64; needs mingw-w64 + dxvk-wine64 installed,
# see programs.md §8/§9)
cmake -S . -B cmake-build-d3d11 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_BACKEND=D3D11 -DCNA_BUILD_TESTS=ON

# Build the CNA library
cmake --build cmake-build-debug --target CNA -j$(nproc)
cmake --build cmake-build-d3d11 --target CNA -j$(nproc)

# Build and run all unit tests (Linux-native backend build dirs only -- CnaTests does not build
# under D3D11 yet, see §4)
cmake --build cmake-build-debug --target CnaTests -j$(nproc)
SDL_VIDEODRIVER=x11 DISPLAY=:99 ./cmake-build-debug/CnaTests

# Run one gtest suite
SDL_VIDEODRIVER=x11 DISPLAY=:99 ./cmake-build-debug/CnaTests --gtest_filter="Texture2DTest.*"

# Full ctest run for one backend (run one backend's suite at a time -- see §9)
ctest --test-dir cmake-build-debug -R "^EasyGL_" --timeout 60
ctest --test-dir cmake-build-vulkan -R "^Vulkan_" --timeout 60
ctest --test-dir cmake-build-bgfx  -R "^Bgfx_"   --timeout 60
ctest --test-dir cmake-build-d3d11 -R "^D3D11" --output-on-failure

# Run a D3D11 test binary directly (via the Wine+DXVK wrapper, not the .exe directly)
scripts/run-wine-dxvk.sh cmake-build-d3d11/cna_test_d3d11_smoke.exe
scripts/run-wine-dxvk.sh cmake-build-d3d11/cna_test_d3d11_common.exe

# Verify DXVK (not WineD3D) is actually what ran -- see programs.md §9
DXVK_LOG_PATH=/tmp/dxvk-logs DXVK_LOG_LEVEL=info \
  scripts/run-wine-dxvk.sh cmake-build-d3d11/cna_test_d3d11_smoke.exe
head -5 /tmp/dxvk-logs/*.log   # should start with "DXVK: 2.6.0" and a real GPU name, not be empty

# Reproduce the CnaTests-under-D3D11 build failure (see §4)
cmake --build cmake-build-d3d11 --target CnaTests -j4
```

**Environment note (Linux-native backends):** the last-verified convention was that this sandbox
has no real X server on `:0` and every GPU/window-creating binary needs
`SDL_VIDEODRIVER=x11 DISPLAY=:99` (a virtual `Xvfb` display). **This was not re-checked this
session** and this session's own D3D11/Wine work directly observed `DISPLAY=:0` as a real, usable
desktop session with real GPU access — re-verify before assuming either claim (see §5's last row).

---

## 8. Next smallest tasks

1. **`plan_dx.md` Phases DX1–DX13 AND DX15 are all fully closed. Phase DX14 (D3D11 verification
   hardening) is the only remaining Debian-side work.**
   `D3D11_Smoke` **93/93** + `D3D11_Common` 23/23; `D3D12_Smoke` **169/169** — both fully green.
   Phase DX15 closed all 18 EasyGL-parity rows, including the three that needed a real
   `GraphicsDevice` (D3D12 `SpriteFont`/`Model`/`Texture2D::FromStream`), unblocked by the new
   `PresentationParameters::HeadlessEXT` windowless-device mode. The only rows still open anywhere
   in the plan are `DX-90`/`DX-91` (D3D11 real-Windows checklist) and `DX-114` (D3D12 real-Windows
   checklist) — both `needs_human`, requiring an actual Windows machine with a real GPU, not
   available here. **Next available work: Phase DX14** (`DX-124`–`DX-130`: D3D11 multi-light,
   specular, mip>0, SpriteFont, Model, RenderTargetCube, combo `Clear*` verification tests) — see
   `plan_dx.md`. Note several of these now have a proven D3D12 counterpart to copy the methodology
   from, since Phase DX15 already closed the D3D12 legs.
1a. **2026-07-14: `.github/workflows/d3d-windows-ci.yml` added** (project-owner-approved exception
    to this project's general no-CI-automation stance, scoped narrowly to D3D11/D3D12) — a
    `workflow_dispatch` job that builds `CNA`+backend with native MSVC on `windows-latest` (this
    project's first-ever native-MSVC configure), runs the smoke/common CTests for real (no Wine),
    and recompiles all 20 HLSL shaders against a real `D3DCOMPILER_47.dll`. Two `CMakeLists.txt`
    fixes were needed to make native MSVC configure at all (FFmpeg's `CNA_FFMPEG_AVAILABLE` gate
    only excluded `MINGW`, not native-Windows `WIN32` generally; the D3D11/D3D12 CTest `COMMAND`s
    were hardcoded to the Wine wrapper scripts, now `CMAKE_CROSSCOMPILING`-gated) — both verified
    not to regress the existing MinGW `cmake-build-d3d11`/`cmake-build-d3d12` builds/CTest suites.
    **This does NOT close `DX-90`/`DX-114`** — it only covers the "MSVC-compile/unit-test/shader-
    generation-check" subset those rows' own text already anticipated CI could provide, not the
    real swap-chain/tearing/device-lost/driver-parity items. **Honestly unvalidated**: this
    environment has no `gh` CLI authentication (no token available, `gh auth status` confirms not
    logged in, no way to obtain one here), so the workflow could not actually be triggered/iterated
    against a real GitHub Actions run before pushing — it's a careful, locally-reasoned first
    attempt, not a proven-green result. **Next step**: project owner runs `gh workflow run
    d3d-windows-ci.yml` (or triggers via the GitHub UI) and shares the log if it fails, so the next
    session can iterate with real failure data.
2. **Decide Task 945** (manual HLSL→GLSL port vs. `dxc`+`SPIRV-Cross` tooling for Phase 78, the
   *unrelated* `../cna-samples` shader-conversion track) — Task 946's data point is in (manual
   porting scaled fine for BloomSample's 3 shaders). Needs project-owner input, do not decide
   unilaterally.
3. **Phase 79 standing queue** (Tasks 957–1076, `plan_graphics.md`): a full re-audit of all 153
   `../cna-samples`-catalogued samples, one task per sample. Start with Task 1006 or any `⬜` row —
   do not touch `⛔` rows (structural/permanent, no CNA action possible).
4. Task 952 (`RenderTargetCube` depth-gating bug on Bgfx) remains **DEFERRED**, not a next task —
   see §9.
5. Lower priority, real and open but not urgent: D3D11's own honestly-flagged residual gaps (not
   completion blockers, `plan_dx.md`/`docs/d3d11-backend.md` "Known limitations") — specular-
   highlight pixel test, multi-light (`DirectionalLight1`/`2`)/`EmissiveColor` discriminating tests,
   the 5 combo `Clear*` variants' own dedicated pixel test, and `Model`/`SpriteFont` D3D11 coverage.
6. **Fixed 2026-07-14 (post-plan_dx.md session): both `cna_reference_dump` and `cna_demo_2d`'s
   D3D11/D3D12 build failures.** `cna_reference_dump`'s `undefined reference to Effect::Apply()`
   link failure: a genuine circular dependency between `cna_backend_graphics_d3d11`/`_d3d12` (whose
   `SpriteBatch`'s custom-`Effect` path calls `Effect::Apply()`) and `CNA` itself (which defines
   it) — MinGW's single-pass archive resolution never revisited `libCNA.a`. Fixed in
   `CMakeLists.txt` by declaring the cycle explicitly (`target_link_libraries(${BACKEND_TARGET}
   PRIVATE CNA)` for `D3D11`/`D3D12`); CMake's documented static-library-cycle handling then repeats
   the archives on the link line. `cna_demo_2d`'s separate `SDL3/SDL.h`-not-found compile failure
   (found while investigating the above — its `Game1.cpp` called raw SDL directly for
   minimize/restore/resize with no XNA equivalent to reach for instead, and never linked
   `SDL3::SDL3` itself; native Linux builds never noticed since a system-wide SDL3 install covered
   it): fixed at the root by adding two real `GameWindow` NOXNA extension methods,
   `MinimizeEXT()`/`RestoreEXT()` (mirroring the existing `IsBorderlessEXT` pattern), and switching
   the resize calls to the existing XNA `EndScreenDeviceChange()` API — `Game1.cpp` no longer
   includes `<SDL3/SDL.h>` at all, so no new SDL3 link dependency was needed. 3 new `GameWindowTest`
   cases added. Verified: real `cna_reference_dump.exe`/`cna_demo_2d.exe` links succeed under both
   D3D11 and D3D12; full `D3D11`(6/6)/`D3D12`(1/1, 80/80 checks) CTest and the EasyGL `CnaTests`
   `GameWindowTest.*` suite (14/14) all re-verified with no regression.

---

## 9. Do not do yet

- **`plan_dx.md` is entirely closed for both D3D11 (Phase DX1–DX11) and D3D12 (Phase DX12,
  `DX-100`–`DX-113`/`DX-115`)** (2026-07-14) — nothing left to authorize or implement there on this
  Debian machine. Only `DX-90`/`DX-91` (D3D11) and `DX-114` (D3D12) remain, all `needs_human` —
  a real Windows machine with a real GPU. `plan_dx.md`'s own design-decision-9 boilerplate text
  ("D3D11 finishing does not implicitly authorize D3D12") is now moot — the project owner already
  authorized and this session already completed D3D12's own software/logic layer in full.
- **When working Phase DX12, do not merge D3D11 and D3D12 into one shared device/backend class**
  "for less duplication" — `plan_dx.md` design decision 4 already scoped what's genuinely shared
  (`D3DCommon`); forcing the actual device/command/resource logic to share code across two
  structurally different APIs is exactly the kind of premature abstraction `CLAUDE.md` warns
  against.
- **Do not assume D3D12 swap-chain/`Present()` support works locally under Wine "since D3D11's
  DXVK path worked"** — `DX-100`'s real spike found the opposite for presentation specifically
  (`CreateSwapChainForHwnd` crashes/fails under vanilla Wine's `dxgi.dll` + vkd3d-proton, even
  though the device/queue/command-list path itself is genuinely solid). Build `DX-102` onward
  around off-screen/readback proof and re-verify swap-chain support explicitly before relying on
  it, rather than inheriting D3D11's own assumption by analogy.
- **Do not opportunistically fix the `CnaTests`-under-D3D11 `::setenv` portability gap** (§4) — a
  real, separately-scoped, ~10-file problem, deliberately left open this session rather than
  expanding scope.
- **Do not resume Task 952** (Bgfx `RenderTargetCube` depth-gating bug) without explicit
  instruction — explicitly marked **DEFERRED** by the project owner after 2 full investigation
  rounds found no root cause.
- **Do not attempt Task 863 or Task 869** (the two architecture-decision items in §5) without the
  project owner picking a direction first.
- **Do not start Phase 78's sample-porting tasks (943/944/946/947) in `../cna-samples`** without
  explicit direction — confirmed mostly out of `cna_graphics` scope.
- **Do not chase `cna_demo_xact`'s build failure** — missing example asset directory, not a CNA bug.
- **Do not attempt `EasyGL_MRT_TwoAttachments`** opportunistically — pre-existing, off-limits
  without a dedicated task.
- **Do not run more than one backend's `ctest`/`CnaTests` suite concurrently** — causes spurious
  `Subprocess aborted` failures from resource contention, not real bugs.
- **Do not bundle multiple task numbers into one commit** — one task per commit, staged by explicit
  filename (never `git add -A`/`.`).
- **No broad refactors** of `GraphicsDevice::Clear`, `IGraphicsBackend`, or the Vulkan/Bgfx render
  pass creation code beyond what's already scoped in `plan_graphics.md`.
- **Lesson from a prior session, still worth applying**: in an actively-authorized session, finishing
  one coherent task is a checkpoint to update this file/commit and move to the next authorized task
  — not a reason to stop and wait, and not license to silently expand scope past what was actually
  authorized either. Both directions of this mistake have happened in this project's history; stay
  precisely within the authorized boundary from §9's own "do not start X" items, but don't stall
  needlessly within it.

---

## 10. Resume prompt

```
Read NEXT.md first, in full, before touching any code.

Pick exactly one task from §8 "Next smallest tasks" (default to the first one unless told
otherwise). Inspect only the files that task names -- do not go exploring unrelated modules, and do
not refactor anything you find along the way that isn't directly required for this task.

plan_dx.md (D3D11 + D3D12 backends) is now fully closed for everything this Debian dev environment
can prove: Phase DX1 through DX11 (D3D11) and Phase DX12 DX-100 through DX-113/DX-115 (D3D12) are
ALL closed (2026-07-14) -- do not re-do this work. The only open rows in the whole plan are
DX-90/DX-91 (D3D11 real-Windows checklist) and DX-114 (D3D12 real-Windows checklist, which
additionally needs to verify the swap chain -- known to crash under this dev loop's
Wine+vkd3d-proton, root-caused, not a CNA bug) -- all three are needs_human and require an actual
Windows machine with a real GPU, not available here. Do not attempt them; if such a machine becomes
available, start with DX-114 or DX-90 per plan_dx.md's own checklists. See §9 for what stays
off-limits generally (no merging D3D11/D3D12 into one class, etc.).

Make one small, verified improvement:
1. Investigate/reproduce the issue first (run the exact failing command from §4/§8).
2. Implement the smallest correct fix.
3. Write or extend a discriminating test that would fail without your fix (verify via git stash or
   a targeted mutation, per this project's established methodology).
4. Run the relevant build/test command from §7 for every backend your change touches.
5. Update the relevant plan_*.md file with the task's closure detail, then update NEXT.md (this
   file): move the task out of §8, add a one-line entry to §3, and refresh §2/§4/§5 if your fix
   changed current test-pass counts or closed a known bug.
6. Commit (staged by explicit filename, one task per commit) and push if asked, following this
   repo's existing commit-message style (git log --oneline).

Do not start a second task in the same session unless the first is fully closed, tested, committed,
and NEXT.md is updated.
```
