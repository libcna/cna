# NEXT.md — CNA Project Handoff

> **D3D11 graphics backend is now complete per `plan_dx.md` (2026-07-14) — Phase DX1 through DX11
> are ALL closed.** Every task except `DX-90`/`DX-91` (real-Windows hardware, `needs_human`, no such
> machine available here) is done: real device/swap-chain/back-buffer, a shared `D3DCommon` mapping/
> shader/constant-buffer core, all 10 stock HLSL shader variants + custom `ShaderEffect`, real
> vertex/index buffers, textures/render targets (MSAA/MRT/occlusion queries), cached state objects,
> a real `SpriteBatch`, a cross-cutting mutation-tested/DXVK-gated test suite, and now full docs
> (`docs/d3d11-backend.md`, a `D3D11` column in `docs/graphics-backend-feature-matrix.md`, a
> `README.md` build section). **6 CTest binaries, 96+ checks, all passing through Wine+DXVK on a
> real GPU.** The project owner separately authorized Phase DX12 (D3D12), which is now underway:
> `DX-100`'s real spike (2026-07-14) found the D3D12 device/queue/fence/command-list path genuinely
> works locally via Wine+vkd3d-proton (DXR 1.1/SM 6.8 negotiated on this machine's GPU — more
> capable than D3D11's own DXVK path), using real vkd3d-proton DLLs already present locally inside
> this machine's Steam "Proton - Experimental" install (no new system install needed, no sudo). **The
> one real gap**: `CreateSwapChainForHwnd` crashes (`FLIP_DISCARD`) or fails cleanly
> (`DXGI_ERROR_INVALID_CALL` with plain `DISCARD`) under vanilla Debian Wine's own `dxgi.dll` — that
> presentation-path integration normally lives in Proton's own patched Wine+DXGI fork, not upstream
> Wine, and a first attempt at running the spike through Proton's own `wine` binary directly failed
> (prefix/launch-environment mismatch, not investigated further). See `plan_dx.md`'s `DX-100` row for
> the full evidence and its recommended path forward: build `DX-101`–`DX-109` (CMake wiring, device/
> queue/heaps/command-lists/barriers/PSOs/root-signatures/resources) around off-screen,
> swap-chain-free proof (`ID3D12Resource` render target + staging-heap readback) for local Wine-based
> development, deferring swap-chain/`Present()` proof to a real-Windows/CI-VM pass much earlier than
> D3D11's own `DX-90` did. `DX-101` (full D3D12 CMake wiring) is the concrete next step. Below is
> this session's own chronological history (kept for detail, not rewritten):
>
> Phase DX3 (`D3DCommon`), Phase DX5
> (vertex/index buffers + input layout), Phase DX6 (textures + render targets), Phase DX7 (state
> objects), Phase DX8 (all 10 stock shader variants + custom `ShaderEffect`), and Phase DX9
> (`D3D11SpriteBatchBackend`) are now closed.
> `CNA_GRAPHICS_BACKEND=D3D11` is a working CMake option; `D3D11GraphicsBackend` implements every
> `IGraphicsBackend` pure virtual (real device/swap-chain/back-buffer/clear/present/readback, real
> vertex/index buffers + input layout cache, real 2D/cube/3D textures + 2D/cube render targets +
> MSAA + MRT + sampler cache + occlusion queries, real blend/depth-stencil/rasterizer state objects
> + viewport/scissor, and now a real `colored3d` `DrawColoredPrimitives()`/
> `DrawIndexedColoredPrimitives()` draw path; honest "not yet implemented" throws remain for every
> other stride/shader variant and SpriteBatch, rest of Phase DX8/DX9). `ctest -R D3D11` passes 2/2
> (`D3D11_Smoke` + `D3D11_Common`, **67 checks total**, up from 65) running through DXVK 2.6.0 on a
> real GPU (AMD Radeon 780M/RADV) via Wine on this Debian machine. `D3DCommon` (format/state/
> vertex-layout mapping + shader cache + now constant-buffer struct layouts) is real and tested.
> `DX-13-hlsl`/`DX-14-compile`/`DX-15-embed` (HLSL shaders, real DXBC compile, real shader-object
> creation) closed Phase DX3 earlier in this session; `DX-30`/`DX-31`/`DX-32` (real buffers + input
> layout) closed Phase DX5; Phase DX6 (`DX-40`–`DX-47`, textures/render targets/MSAA/MRT/occlusion
> queries, plus a genuine `Clear()`-target-tracking bug fix) closed next; Phase DX7 (`DX-50`–`DX-53`,
> real cached blend/depth-stencil/rasterizer state objects + viewport/scissor, plus a real
> `DepthBias` float→INT rounding fix) closed after that.
>
> **Phase DX8's foundational tasks now closed**: new header-only `D3DConstantBuffers.hpp`
> (`D3DCommon`) defines `D3DPerDrawConstants`/`D3DFogConstants`/`D3DLightingConstants`/
> `D3DBoneConstants`, every field offset `static_assert`-verified against the real HLSL `cbuffer`
> declarations (not guessed). `DrawColoredPrimitives()`/`DrawIndexedColoredPrimitives()` (previously
> honest throws) now do a real `colored3d` draw — cached shader + input layout + two lazily-created
> persistent constant buffers, updated via `Map`/`Unmap` each draw — for stride-16
> (`VertexPositionColor`) only; other strides still throw a clear, named-successor error. **A real,
> independent bug found and fixed getting the first pixel test to pass**: `DX-46`'s
> `SetRenderTargets(nullptr, 0)` never restored the back buffer after a prior MRT bind (only the
> single-target path's tracking triggered the restore), leaving the device context bound to
> render-target views a test's `unique_ptr`s had already destroyed. New `D3D11_Smoke` Check P
> proves it for real: clears to a known blue, reads back a fixed region (confirms blue), draws a
> real NDC-covering triangle with solid vertex color, reads back the *same* region again (confirms
> red) — for both the indexed and non-indexed draw paths. **44/44 smoke checks pass**, up from
> 42/42.
>
> **`DX-62`/`DX-63`/`DX-64` (and `DX-69`'s fog wiring for those 5 variants) also now closed.** New
> `DrawPrimitivesEx()`/`DrawIndexedPrimitivesEx()` overrides give this backend its first real,
> `GpuDrawParams`-driven effect dispatch (mirroring `VulkanGraphicsBackend::DrawPrimitivesEx`'s
> priority chain): `textured3d`/`colored_textured3d` (stride 20/24), `lit_textured3d` (stride 32,
> first real consumer of `D3DLightingConstants` — full Blinn-Phong), and `alpha_test3d` (a new
> dedicated `D3DAlphaTestConstants` struct — its HLSL cbuffer shape genuinely differs from
> `D3DPerDrawConstants`) all draw for real now; `dual_texture3d`/`env_map3d`/`skinned3d` throw named
> "not yet implemented" errors instead of silently falling through to the wrong shader. **Zero bugs
> found on the first real run** — new Checks Q/R/S proved exact texture sampling, exact vertex-color
> tinting, the lit branch genuinely differing from the unlit one, and `alpha_test3d`'s `clip()`
> genuinely discarding (background survives) vs. genuinely drawing (exact color incl. a non-255
> alpha byte) on pass. **51/51 smoke checks pass** (up from 44/44), `D3D11` CTest total now **74/74
> checks** (`D3D11_Smoke` 51 + `D3D11_Common` 23). `DX-69` fog wiring is real for these 5 variants
> but not yet exercised by a dedicated fog-on/off pixel check (Checks Q/R/S all draw with
> `fogEnabled=false`) — an open Phase DX10 gap, not a false claim.
>
> **Phase DX8 is now FULLY CLOSED (2026-07-13).** `DX-65`/`DX-66`/`DX-67` land real `dual_texture3d`
> (two real SRVs/samplers), `env_map3d` (a real `TextureCube` SRV, reflection direction constrained
> by test geometry to land deep inside one distinctly-colored cube face), and `skinned3d` (a
> genuinely-populated `D3DBoneConstants` buffer, confirmed *not* a row/column-major transpose bug).
> `DX-68` lands real `DrawInstancedPrimitivesEx()` for `instanced3d` (new fixed `INSTANCEWORLD0`–`3`
> instanced input layout); `sprite2d` is deliberately left unwired into any draw dispatch — its real
> home is `SpriteBatch` (Phase DX9, not started). `DX-69` (fog) is now real for all 8 fog-capable
> variants. `DX-58` adds a new `D3D11EffectBackend` (runtime `D3DCompile()`, `d3dcompiler` now
> linked into the main backend target for real) mirroring `VulkanEffectBackend`'s own fixed-slot
> uniform convention — independently GPU-proven but not yet wired into an actual `SpriteBatch` draw
> loop. **58/58 smoke checks pass** (up from 51/51), `D3D11` CTest total now **81/81 checks**
> (`D3D11_Smoke` 58 + `D3D11_Common` 23).
>
> **Phase DX9 (SpriteBatch, `DX-70`/`DX-71`/`DX-72`) is now CLOSED (2026-07-13) — resolving both of
> Phase DX8's own deferred dependencies (`sprite2d`'s draw path, `DX-58`'s end-to-end integration)
> in the same task.** New `D3D11SpriteBatchBackend` (`D3D11SpriteBatch.hpp`/`.cpp`) mirrors
> `EasyGLSpriteBatchBackend`'s immediate-flush-per-texture-change quad batcher (D3D11's context is
> already immediate-mode — no reason for `VulkanSpriteBatchBackend`'s frame-end-snapshot design).
> **One real, deliberate improvement over Vulkan's own precedent**: `SetTransformMatrix()`
> genuinely works here (`VulkanSpriteBatchBackend` leaves it a silent no-op) — applied per-vertex on
> the CPU via `Vector2::Transform()`, since `sprite2d.vert.hlsl` has no projection-matrix uniform to
> fold it into GPU-side. `DX-71` reuses `D3D11EffectBackend` (`DX-58`) directly, adding
> `SetViewportSizeEXT()` to fill the `vpSize` slot that class had already reserved. `DX-72` needed
> no new implementation — `D3D11SamplerCache` (`DX-44`) already handles Wrap/Mirror correctly; this
> row is purely verification. **Tested through the real public API**
> (`Microsoft::Xna::Framework::Graphics::SpriteBatch`+`Texture2D`, not the raw backend interface) —
> 6 new checks (Y/Z/AA), all passing on the first real Wine+DXVK run on the real GPU, including two
> deliberately *discriminating* Wrap/Mirror probe pixels (chosen so a broken Clamp-fallback would
> read a different, wrong color). **64/64 smoke checks pass** (up from 58/58), `D3D11` CTest total
> now **87/87 checks** (`D3D11_Smoke` 64 + `D3D11_Common` 23), verified via
> `ctest --test-dir cmake-build-d3d11 -R D3D11`.
>
> **`Phase DX10` (`DX-81`–`DX-85`) also closed 2026-07-14** — added 4 new CTest entries reusing
> the exact backend-agnostic `easygl_blendstate_*`/`easygl_depthstencilstate_*`/
> `easygl_rasterizerstate_*` sources Vulkan already reuses verbatim (`D3D11_BlendState_Opaque`/
> `AlphaBlend`, `D3D11_DepthStencilState_StencilEnable`, `D3D11_RasterizerState_CullMode` — real
> pixel-behavior proof, all 4 passing on the first run); closed `DX-29`'s long-flagged
> never-exercised resize gap for real (Check AB: 64×64→96×80 via the public
> `GraphicsDeviceManager` API, DXVK's own presenter log confirms the new buffer size, correct
> post-resize readback); ran a real mutation-test pass on `DX-61`'s `colored3d` triangle check
> (`DX-84` — first mutation was an honest false negative since that check uses identity matrices,
> second mutation genuinely broke it, verified reverted); hardened `scripts/run-wine-dxvk.sh`
> itself to automatically assert a `DXVK: <version>` marker appears in every run (`DX-85`) — caught
> a real false-positive live (`D3D11_Common` never opens a device, needed its own narrowly-scoped
> `CNA_D3D11_SKIP_DXVK_GATE=1` opt-out, not a weakened gate); and closed `DX-69`'s own
> honestly-flagged "fog wired but never exercised by a dedicated on/off pixel test" gap with a new
> Check AC (exact-color fog-on vs fog-off discrimination). **`D3D11` CTest total now 6 tests, 92
> embedded `D3D11_Smoke`/`D3D11_Common` checks + 10 more assertions across the 4 new state-object
> tests** (`D3D11_Smoke` 69 + `D3D11_Common` 23 + 4 new tests), all verified via a real
> `ctest --test-dir cmake-build-d3d11 -R D3D11` run (6/6 tests pass). Full detail, task-by-task,
> lives in `plan_dx.md` (`DX-1`–`DX-85` all closed; `DX-90`/`DX-91` stay `needs_human`/best-effort).
>
> **`Phase DX11` (docs, `DX-95`–`DX-98`) also closed 2026-07-14 — this closes out `plan_dx.md`'s own
> D3D11 scope entirely.** New `docs/d3d11-backend.md` (mirrors `docs/software-backend.md`/
> `docs/headless-backend.md`'s structure: status, what it's for/isn't, Wine+DXVK dev-loop, writing a
> test, an honest known-limitations list — specular highlights untested, multi-light variants
> untested, mip chains untested, `Model`/`SpriteFont` not separately exercised, the pre-existing
> unrelated `cna_reference_dump`/`cna_demo_2d` link failure found during `DX-81`, plus the
> already-known `DX-90`/`DX-91`/`DX-27`/`DX-21`/`DX-25`-combo-variant gaps). A real `D3D11` column
> added to every applicable table in `docs/graphics-backend-feature-matrix.md`, honestly mixing
> ✅/🟨/⬜ per row rather than blanket-approving anything the underlying code merely supports.
> `README.md` gained a "Tested Compilers" row and a full "Build (Windows cross-compilation — D3D11
> backend)" section mirroring `SDL_RENDERER`'s own. **Phase DX12 (D3D12) is now underway** — `DX-100`
> (the phase's own first task, a Wine+vkd3d-proton dev-loop spike) is closed 🟨: device/queue/fence/
> command-list all real (DXR 1.1/SM 6.8 negotiated), but `CreateSwapChainForHwnd` crashes/fails under
> vanilla Wine's own `dxgi.dll` — see `plan_dx.md`'s `DX-100` row for the full evidence. `DX-101`
> (full D3D12 CMake wiring) is the next concrete step, building around off-screen swap-chain-free
> proof per `DX-100`'s own recommendation.
> **This is a brand-new architectural front for the project — read `plan_dx.md`'s own status banner
> before touching it.** The pre-existing EasyGL/Vulkan/Bgfx/SDL_Renderer/Headless/Software/WebGPU
> work summarized below is unchanged by this; full history for that lives in `plan_graphics.md`/
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
  working native 2D+partial-3D baseline (`plan_webgpu.md`). **The Direct3D 11 backend
  (`plan_dx.md`) is now complete per its own plan** (Phase DX1–DX11 all closed, 2026-07-14) — a
  Windows-only backend, cross-compiled from this Debian machine via MinGW-w64 and verified locally
  through Wine+DXVK; only `DX-90`/`DX-91` (real-Windows hardware) remain `needs_human`. **Direct3D
  12 (`plan_dx.md` Phase DX12) is now underway** — `DX-100`'s Wine+vkd3d-proton spike is closed 🟨
  (device/queue/fence/command-list real; swap-chain creation crashes/fails, see that row); `DX-101`
  (full D3D12 CMake wiring) is next. Everything else
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
| Headless, WebGPU | Not re-verified this session | See `plan_headless.md`/`plan_webgpu.md` for their own last-verified status |

All EasyGL/Vulkan/Bgfx/Software numbers above are carried over from the last session that actually
touched those backends (2026-07-10/11/13) — **not re-verified in this session**, which worked
exclusively on D3D11. The D3D11 numbers are fresh, verified today.

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
  open, both `needs_human`. **D3D12 (Phase DX12) is now underway** — `DX-100`'s spike closed 🟨
  2026-07-14 (device/queue/fence/command-list real via Wine+vkd3d-proton; swap-chain creation
  crashes/fails, see that row's Notes); `DX-101` (CMake wiring) is next, see §8/§9.
- **D3D11 + `CnaTests`**: the full pre-existing GTest suite does not build under this backend —
  ~10 test files call POSIX-only `::setenv()` directly (see §4).
- **D3D12**: still nothing implemented beyond `DX-100`'s own throwaway (unchecked-in) spike — no
  CMake `STRINGS`/option-flag entry yet (`DX-101`, the next concrete step).
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

**No hard blocker — `plan_dx.md`'s D3D11 scope is entirely closed.** Phase DX1 through DX11 are all
✅ except `DX-90`/`DX-91` (real Windows hardware, `needs_human`, no such machine available here).
**Phase DX12 (D3D12) is now underway.** `DX-100` (the Wine+vkd3d-proton dev-loop spike, "do this
first, before committing to the rest of this phase's ordering" per its own row) is closed 🟨: the
D3D12 device/queue/fence/command-list path is real and works locally (feature level `12_1`, DXR 1.1,
Shader Model 6.8 negotiated via vkd3d-proton DLLs already present in this machine's Steam Proton
install — no new system install needed), but `CreateSwapChainForHwnd` crashes (`FLIP_DISCARD`) or
fails cleanly (`DXGI_ERROR_INVALID_CALL` with plain `DISCARD`) under vanilla Debian Wine's own
`dxgi.dll` — see `plan_dx.md`'s `DX-100` row for the full evidence. **The concrete next step is
`DX-101`** (full D3D12 CMake wiring), building `DX-102` onward around off-screen/swap-chain-free
proof per `DX-100`'s own recommendation, and deferring swap-chain/`Present()` proof to a real-
Windows/CI-VM pass much earlier than D3D11's `DX-90` did. Outside the D3D plan, the standing backlog
(Phase 79's 153-sample `../cna-samples` re-audit, Task 945's HLSL→GLSL tooling decision, Task 952's
deferred Bgfx bug) is unchanged — see §8/§9.

**One real, separate, documented problem**: the full `CnaTests` GTest suite does not build under
`CNA_GRAPHICS_BACKEND=D3D11`.

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
  (`cna_test_d3d11_smoke`, `cna_test_d3d11_common`) build and run fine — only the full `CnaTests`
  target is affected.
- **Not a blocker for continued backend development**: Phase DX5+ work doesn't need `CnaTests` to
  build under D3D11 to make progress: this project's own established pattern (Software/Headless
  backends) is standalone example-based CTests (`examples/d3d11_*.cpp`) until a backend is mature
  enough to run the full suite.

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

1. **Phase DX12 (D3D12), continue with `DX-101`** (`plan_dx.md`) — `DX-100`'s spike is closed 🟨
   (2026-07-14): the device/queue/fence/command-list path is real and usable locally via Wine+
   vkd3d-proton (feature level `12_1`, DXR 1.1, SM 6.8 negotiated), but `CreateSwapChainForHwnd`
   crashes/fails under vanilla Wine's own `dxgi.dll` — see that row's Notes for the full evidence
   and its two recommended paths forward. **Next step: `DX-101`** (full D3D12 CMake wiring —
   `CNA_GRAPHICS_BACKEND=D3D12`, `cna_backend_graphics_d3d12` target, factory dispatch), building
   `DX-102` onward around off-screen/swap-chain-free proof (an `ID3D12Resource` render target +
   staging-heap readback, no `IDXGISwapChain`) rather than assuming presentation works, per
   `DX-100`'s own recommendation. Already authorized — no go-ahead needed, but see §9 for what's
   still off-limits.
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
   the 5 combo `Clear*` variants' own dedicated pixel test, `Model`/`SpriteFont` D3D11 coverage, and
   the pre-existing unrelated `cna_reference_dump`/`cna_demo_2d` link failure (`undefined reference
   to Effect::Apply()`, confirmed to pre-date D3D11 entirely).

---

## 9. Do not do yet

- **`plan_dx.md`'s D3D11 scope (Phase DX1–DX11) is entirely closed** (2026-07-14) — nothing left to
  authorize there. **Phase DX12 (D3D12) is authorized** (the project owner explicitly authorized it
  "later if time allows," 2026-07-13) — it is legitimate next work, not gated the way it was before
  that authorization; `plan_dx.md`'s own design-decision-9 boilerplate text ("D3D11 finishing does
  not implicitly authorize D3D12") predates that explicit go-ahead and is now superseded for this
  project specifically — **do not read design decision 9 as still blocking D3D12** without checking
  this file's own current authorization state first. Only `DX-90`/`DX-91` (needs a real Windows
  machine) remain genuinely gated — see the next bullet.
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

Phase DX1 through DX11 of plan_dx.md (D3D11 backend) are ALL closed (2026-07-14) -- do not re-do
this work. Phase DX12 (D3D12) is authorized and underway -- DX-100 (Wine+vkd3d-proton spike) is
closed 🟨 (device/queue/fence/command-list real; swap-chain creation crashes/fails under vanilla
Wine's dxgi.dll, see that row's Notes). Continue with DX-101 (full D3D12 CMake wiring) and proceed
without re-asking; see §9 for what stays off-limits (no merging D3D11/D3D12 into one class; do not
assume swap-chain/Present() support works locally -- build around off-screen/readback proof instead
per DX-100's own recommendation).

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
