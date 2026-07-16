# NEXT.md — CNA Project Handoff

> **Both `plan_dx.md` backends are now closed through Phase DX16 (2026-07-15) — every row except
> the 5 genuinely `needs_human` hardware-gated ones.** `D3D11_Smoke` **147/147 checks** (6/6 CTests
> total), `D3D12_Smoke` **212/212 checks** (1/1 CTest, off-screen only — see below), both through
> Wine+DXVK/vkd3d-proton on a real GPU, verified after every single task landed this session, not
> just at the end.
>
> **This session's work (2026-07-15), continuing from the 2026-07-14 state this banner used to
> describe:**
> - **Phase DX13's last 2 partial rows closed for real**: `DX-117` follow-up — real, device-queried
>   MSAA for `D3D12RenderTargetBackend` (2D) *and* `D3D12RenderTargetCubeBackend` (the latter closed
>   later, see Phase DX16 below); `DX-121` — `SpriteBatch::Begin(effect)` now has its own dedicated
>   CTest proof through the real public XNA API. **Phase DX13 is now 8 of 8 rows closed.**
> - **3 stale `plan_dx.md`/`graphics-backend-feature-matrix.md` rows found and fixed** — a recurring
>   pattern this session: a LATER task's own row explicitly says it closed an EARLIER row's gap, but
>   the earlier row's own status badge was never updated to reflect it. Found and fixed for `DX-29`
>   (D3D11 window resize — already closed by `DX-83`), `DX-102` (D3D12 device/queue/swap-chain —
>   already closed by its own later update + `DX-116`), and `DX-113` (D3D12 tests — one gap already
>   closed by `DX-118`, the other closed for real this session with a genuine GPU-load timing proof,
>   Checks `E4`/`E4pre`/`E4a`). **Before trusting a plan row's ⬜/🟨 status, check whether a later
>   row's own closing note claims to have already closed it.**
> - **Phase DX16 (new, `DX-149`–`DX-155`) opened and fully closed** — found by computing a real
>   percentage of "how much of what EasyGL does can D3D11/D3D12 also do" (34 comparable
>   feature-matrix rows; both backends started at ~79% fully verified / ~97% functionally
>   implemented). All 7 gaps closed: `EnvironmentMapEffect`/`SkinnedEffect` `DirectionalLight1`/`2` +
>   specular tests (both backends), **real new `RenderTargetCube` MSAA support on both D3D11 and
>   D3D12** (`DX-152` — reopened a previously-deliberate scope exclusion, not a silent oversight),
>   `RenderTargetCube` mip-chain regen for a non-zero face, D3D12's own 16-simultaneous-sampler-slot
>   test, and `Model` root-bone-index flexibility (found and corrected a wrong premise in this row's
>   own original text — see `plan_dx.md`'s `DX-155` row for detail: `rootBoneIndex` doesn't actually
>   drive `Model::Draw()`, only `getRootProperty()` reflects it).
>
> **Result: with Phase DX16 also closed, `plan_dx.md` has NOTHING left to do on this Debian machine.**
> Every remaining open row (`DX-27`, `DX-90`, `DX-91`, `DX-110`, `DX-114`) is genuinely
> `needs_human` — either real-Windows-hardware verification, or a real `DXGI_ERROR_DEVICE_REMOVED`
> trigger that cannot be induced under Wine. Full task-by-task detail and this session's complete
> chronological history live in `plan_dx.md` — not duplicated here. Current-state summaries:
> `docs/d3d11-backend.md`, `docs/d3d12-backend.md`, and `docs/graphics-backend-feature-matrix.md`
> (full row-by-row `D3D11`/`D3D12` comparison against every established backend).
>
> This is a brand-new architectural front for the project — read `plan_dx.md`'s own status banner
> before touching it. The pre-existing EasyGL/Vulkan/Bgfx/SDL_Renderer/Headless/Software/WebGPU work
> summarized below is unchanged by this; full history for that lives in `plan_graphics.md`/
> `plan_webgpu.md`/`plan_software.md` and `git log`, not duplicated here.

> **Separate, unrelated track — `plan_graphics.md` Phase 78 (DEFERRED.md item #11, HLSL→GLSL sample
> shader conversion) is now FULLY COMPLETE, as of 2026-07-16 (EasyGL only).** This is completely
> independent of the D3D work above — it unblocks samples catalogued in `plan_samples.md`
> (`../cna-samples`' own 153-sample re-audit), not `plan_dx.md`. **Task 945 decided** (project
> owner, 2026-07-16): manual line-by-line HLSL→GLSL porting, no `SPIRV-Cross`/`dxc` pipeline — every
> HLSL construct hit across every shader ported turned out to be a mechanical 1:1 substitution.
> **Task 947 is now 13/13 — every sample originally blocked purely by DEFERRED.md #11 has its
> shader(s) ported and pixel-verified**: `NetRumble`, `PerPixelLighting`, `VertexLighting`,
> `DistortionSample`, `NonPhotoRealistic`, `ShadowMapping`, `NormalMapping`, `BillboardSample`,
> `ShatterEffect`, `Particles3D`, `XmlParticles`, `ShipGame`, `InstancedModel` (`BloomSample`, the
> 14th sample under the same DEFERRED.md #11 umbrella, was already closed earlier via Task 946).
> Along the way, 4 new backend capabilities were added and closed, all EasyGL-only, all additive
> (Vulkan/Bgfx/SDL_Renderer untouched): **Task 1079** (wires `ShaderEffect` into `GraphicsDevice`'s
> 3D draw path, not just `SpriteBatch`), **Task 1080** (genuinely custom vertex layouts for that
> path, not just the 5 fixed byte-strides), **Task 1081** (`TextureCube` sampling for custom
> shaders), **Task 1082** (real GPU hardware instancing — `glVertexAttribDivisor`-driven per-instance
> vertex streams). **What remains is explicitly NOT `cna_graphics` scope**: the actual sample ports
> (`.cpp`/`.hpp`/`Content/` under `../cna-samples/samples/<Name>/`) for these 13 (now-unblocked)
> samples still need to be written in the sibling `../cna-samples` repo, tracked in that repo's own
> plan file, not here or in `plan_graphics.md`/`plan_samples.md`. `plan_samples.md` also still has
> ~88 other `⬜` rows unrelated to this shader-conversion track (re-verification passes, other
> DEFERRED.md items, etc.) — untouched by this work, standing backlog. Full detail: `plan_graphics.md`
> Task 947's own row (chronological per-shader history, discriminating-power mutation testing for
> every one) and Tasks 1079–1082's own rows; `plan_samples.md` for the per-sample CNA-gap tracking.

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
  working native 2D+partial-3D baseline (`plan_webgpu.md`). **Both Direct3D backends (`plan_dx.md`)
  are now closed through Phase DX16, as of 2026-07-15 — nothing left to do on this Debian machine.**
  Direct3D 11 (Phase DX1–DX11) is fully closed — a Windows-only backend, cross-compiled from this
  Debian machine via MinGW-w64 and verified locally through Wine+DXVK, including a working swap
  chain/`Present()`; only `DX-27`/`DX-90`/`DX-91` (real-Windows hardware / a real device-removed
  trigger) remain `needs_human`. Direct3D 12 (Phase DX12–DX16) is also fully closed — same dev-loop
  approach but via Wine+vkd3d-proton, with its own routine CTest suite still off-screen only (real
  swap-chain creation and `Present()` work too, but only through a separate manual Proton-managed
  diagnostic, not the routine CTest — Proton's own bootstrap is too heavy for a normal CTest run
  here); only `DX-110`/`DX-114` (real-Windows hardware / a real device-removed trigger) remain
  `needs_human`. `D3D11_Smoke` **147/147**, `D3D12_Smoke` **212/212** checks, both real GPU-facing
  proof. With every remaining open row genuinely `needs_human`, there is no further available
  Debian-side work on `plan_dx.md` barring new instructions. **`plan_graphics.md` Phase 78
  (DEFERRED.md #11, HLSL→GLSL sample shader conversion) is now also fully closed on the CNA side
  (2026-07-16, EasyGL only) — Task 945 decided (manual porting), Task 947 13/13, Tasks 1079–1082
  (4 new backend capabilities) all closed — see the banner at the top of this file.** What's left:
  Phase 79's 153-sample `../cna-samples` re-audit standing queue (`plan_samples.md`, most rows
  unrelated to shaders), the actual sample-porting work for those 13 now-unblocked samples (a
  different repo, out of `cna_graphics` scope), and Task 952's deferred Bgfx bug — see §5/§8/§9.
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
| `cmake-build-debug` | EasyGL | Clean — rebuilt and `ctest -R "EasyGL_"` re-verified 2026-07-16 (Phase 78 sample shader-conversion work, unrelated to the D3D session this file otherwise documents) |
| `cmake-build-vulkan` | Vulkan | Clean as of 2026-07-10 (not rebuilt this session) |
| `cmake-build-bgfx` | Bgfx | Clean as of 2026-07-10 (not rebuilt this session) |
| `cmake-build-sdl` | SDL_Renderer | Clean as of 2026-07-10 (not rebuilt this session) |
| `cmake-build-android` | SDL_Renderer (NDK) | Blocked — Task 920 (sibling `sharp-runtime` NDK build regressions) |
| `cmake-build-d3d11` | D3D11 (Windows cross-compile, MinGW-w64) | **Verified clean 2026-07-15**: `CNA` and `cna_backend_graphics_d3dcommon`/`cna_backend_graphics_d3d11` build clean. 6 CTests all pass through Wine+DXVK: `D3D11_Smoke` **147/147 checks** (real `RenderTargetCube` MSAA landed this session, `DX-152`), `D3D11_Common` 23/23, plus 4 state-object CTests. `plan_dx.md` Phases DX1–DX16 all closed on this backend — only `DX-27`/`DX-90`/`DX-91` (real Windows hardware / a real device-removed trigger) remain, `needs_human`. |
| `cmake-build-d3d12` | D3D12 (Windows cross-compile, MinGW-w64) | **Verified clean 2026-07-15**: `CNA` and both D3D12 backend targets build clean. `D3D12_Smoke` CTest (off-screen only — routine CTest harness still uses `run-wine-vkd3d.sh`'s plain-Wine prefix, real swap-chain/`Present()` verified separately via `scripts/run-proton-vkd3d.sh`'s manual diagnostic, not the routine CTest): **212/212 checks**, checks lettered A through VV. All 10/10 stock shader variants, real render targets/MRT with real MSAA (both `RenderTarget2D` — `DX-117` follow-up — and `RenderTargetCube` — `DX-152`, new this session), real mip-chain generation (both RT types, including a non-zero cube face — `DX-153`), runtime-settable blend/depth-stencil/rasterizer state, per-slot `SamplerState` (now proven across all 16 slots simultaneously — `DX-154`), real occlusion queries, custom `ShaderEffect` (including a dedicated `SpriteBatch::Begin(effect)` CTest proof — `DX-121`), `Texture3D`, `TextureCube::GetData()`, `SpriteFont`/`Model` (including root-bone-index flexibility — `DX-155`) through a windowless `GraphicsDevice`, and a real, GPU-load-timed fence back-pressure proof (`DX-113` follow-up). `plan_dx.md` Phases DX12–DX16 all closed on this backend — only `DX-110`/`DX-114` (real Windows hardware / a real device-removed trigger) remain, `needs_human`. |

The `cna_demo_xact` example fails to build on every backend (missing `examples/demo_xact/Content`
directory in this checkout) — cosmetic, pre-existing, not a CNA bug, do not chase it (§9).

### Test status

| Backend | `CnaTests` (gtest) | `ctest` (integration/pixel) |
|---|---|---|
| EasyGL | 4371/4373 pass (2 hardware skips), as of 2026-07-11 (`CnaTests` not re-run 2026-07-16, only `ctest`) | **231/233 pass, as of 2026-07-16** — same 2 pre-existing failures (`EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`); the higher total vs. the 2026-07-11 figure is Phase 78's own new shader/capability-proof tests, not regressions |
| Vulkan | 4371/4373 pass (2 hardware skips), as of 2026-07-11 | 127/128 pass — 1 pre-existing failure (`Vulkan_DepthBias`) |
| Bgfx | 4375/4377 pass (2 hardware skips), as of 2026-07-11 | 104/106 pass — 2 pre-existing failures (`Bgfx_RenderTarget2D_MsaaResolve`, `Bgfx_RenderTargetCube_DepthFormat`, DEFERRED — Task 952) |
| Software | 4371/4373 pass, as of 2026-07-13 | 6 CTests, 29/29 checks |
| D3D11 | **Builds, links, and runs** (fixed 2026-07-14, `DX-15`) — full-suite pass count not yet re-measured (thousands of tests, long wall-clock under Wine); a filtered run (`CnaInput*`/`AlphaTestReferenceScalingTest`, 30 tests, through a live D3D11 device) passed 30/30 | **6/6 pass, `D3D11_Smoke` 147/147 + `D3D11_Common` 23/23 + 4 state-object CTests**, verified 2026-07-15 via `ctest --test-dir cmake-build-d3d11 -R D3D11` |
| D3D12 | **Builds and links** (fixed 2026-07-14, `DX-15`) — groups not creating a live `GraphicsDevice` window run and pass under plain Wine (confirmed); groups that need a real window use `scripts/run-proton-vkd3d.sh`'s separate manual diagnostic instead | **1/1 pass, 212/212 checks** (`D3D12_Smoke`, off-screen: all 10/10 stock shader variants, real render targets/MRT/MSAA (both 2D and cube) with mip-chain generation, real state objects, per-slot samplers (all 16 simultaneously), real occlusion queries, custom `ShaderEffect` incl. `SpriteBatch::Begin(effect)`, `Texture3D`, `TextureCube::GetData()`, `SpriteFont`/`Model` incl. root-bone-index flexibility through a windowless `GraphicsDevice`, and a real GPU-load-timed fence back-pressure proof), verified 2026-07-15 via `ctest --test-dir cmake-build-d3d12 -R D3D12` |
| Headless, WebGPU | Not re-verified this session | See `plan_headless.md`/`plan_webgpu.md` for their own last-verified status |

All EasyGL/Vulkan/Bgfx/Software numbers above are carried over from the last session that actually
touched those backends (2026-07-10/11/13) — **not re-verified this session**, which worked
exclusively on D3D11/D3D12 again. Both D3D11 and D3D12 numbers are fresh, verified 2026-07-15.

### Recently implemented

- **`plan_dx.md` Phase DX13 completion + new Phase DX16** (2026-07-15, this session): closed
  Phase DX13's last 2 partial rows — `DX-117` follow-up (real, device-queried `RenderTarget2D` MSAA
  for D3D12, mirroring D3D11's own `DX-45`) and `DX-121` (`SpriteBatch::Begin(effect)` dedicated
  CTest proof). Fixed 3 stale plan rows found via cross-referencing (`DX-29`/`DX-102`/`DX-113`) —
  each was a case where a LATER task's own row said it closed an EARLIER row's gap, but the earlier
  row's status badge was never updated. Opened and closed a brand-new **Phase DX16** (`DX-149`–
  `DX-155`) from a real percentage audit ("how much of what EasyGL does can D3D11/D3D12 also do" —
  34 comparable feature-matrix rows, both backends started at ~79%/~97%): `EnvironmentMapEffect`/
  `SkinnedEffect` `DirectionalLight1`/`2` + specular tests, **real new `RenderTargetCube` MSAA on
  both backends** (`DX-152`, a genuine new feature reopening a prior deliberate scope exclusion),
  `RenderTargetCube` mip-chain regen for a non-zero face, D3D12's 16-simultaneous-sampler-slot test,
  and `Model` root-bone-index flexibility (corrected this row's own wrong premise about what
  `rootBoneIndex` actually drives, by reading `Model.cpp` first). `D3D11_Smoke` grew 135→**147/147**,
  `D3D12_Smoke` grew 191→**212/212** — every task individually verified via a real `ctest` run and
  committed/pushed separately. **`plan_dx.md` now has nothing left except `needs_human` rows.** See
  §3 for the itemized commit list.
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

- **D3D11 and D3D12 are both complete per `plan_dx.md`'s own scope as of 2026-07-15 (Phase
  DX1–DX16, both backends).** Every stock 3D shader variant, `SpriteBatch` (incl. custom
  `Effect`/`Begin(effect)`), vertex/index buffers, textures (2D/cube/3D), render targets (2D/cube,
  both now with real MSAA and mip-chain generation), MRT, blend/depth-stencil/rasterizer state,
  per-slot `SamplerState` (D3D11: proven across all 16 slots; D3D12: same, `DX-154`), occlusion
  queries, `Model`/`SpriteFont` (D3D12 via a windowless `GraphicsDevice`), and device-removed
  recovery are all real and pixel-verified on both backends. `D3D11_Smoke` **147/147**,
  `D3D12_Smoke` **212/212**. D3D12's own routine CTest suite is still off-screen only — real
  swap-chain creation and `Present()` work too, but only through `scripts/run-proton-vkd3d.sh`'s
  separate manual Proton-managed diagnostic (Proton's own bootstrap is too heavy for a normal
  CTest run on this dev loop), not the routine `D3D12_Smoke` CTest. **Only genuinely `needs_human`
  rows remain open anywhere in `plan_dx.md`**: `DX-27`/`DX-90`/`DX-91` (D3D11) and `DX-110`/`DX-114`
  (D3D12) — real Windows hardware, or a real `DXGI_ERROR_DEVICE_REMOVED` trigger neither backend
  can induce under Wine. See `plan_dx.md`, `docs/d3d11-backend.md`, `docs/d3d12-backend.md` for
  full detail — not duplicated here.
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
| many, see `plan_graphics.md` (2026-07-16) | **`plan_graphics.md` Phase 78 (HLSL→GLSL sample shader conversion, DEFERRED.md #11) fully closed — unrelated to the D3D work below, see this file's own top banner.** Task 945 decided (manual line-by-line porting). Task 947 went 0→**13/13**: `NetRumble` (`Clouds.fx` + the bloom trio), `PerPixelLighting`/`VertexLighting` (5 effect/technique combinations), `DistortionSample` (`Distort.fx` + `Distorters.fx`, 5 techniques), `NonPhotoRealistic` (`CartoonEffect.Fx` + `PostprocessEffect.Fx`, 8 techniques), `ShadowMapping`, `NormalMapping`, `BillboardSample`, `ShatterEffect`, `Particles3D`/`XmlParticles`, `ShipGame` (4 distinct shaders: `AnimSprite.fx`/`Blur.fx`/`NormalMapping.fx`/`Particle.fx`, incl. real GPU point sprites), `InstancedModel` (`InstancedModel.fx`, incl. real GPU hardware instancing). 4 new EasyGL-only backend capabilities landed along the way as their own tasks: **1079** (`ShaderEffect` into the 3D draw path), **1080** (custom vertex layouts for that path), **1081** (`TextureCube` sampling for custom shaders), **1082** (real GPU hardware instancing via `glVertexAttribDivisor`). `ctest -R "EasyGL_"` grew from ~190 to **231/233** across the whole campaign, same 2 pre-existing unrelated failures throughout, every task individually mutation-tested and committed separately. Full chronological detail (exact expected pixel values, discriminating-power mutation testing per shader) is in `plan_graphics.md`'s own Task 947/1079–1082 rows, not duplicated here. `plan_samples.md` updated per-sample (13 rows now say "No longer CNA-blocked"). **Not done**: the actual sample ports themselves in `../cna-samples` — out of `cna_graphics` scope. |
| `9fb9cd09`…`35a656ba` (2026-07-15) | **Phase DX13 completed + new Phase DX16 opened and fully closed — `plan_dx.md` now has nothing left except `needs_human` rows.** `DX-117` follow-up (`9fb9cd09`): real, device-queried `RenderTarget2D` MSAA for D3D12 (`ClampMultiSampleCount`/`ResolveMsaaEXT`, mirroring D3D11's `DX-45`), `D3D12_Smoke` 191→193/193. `DX-29` doc fix (`28d74b90`): D3D11 window resize was already closed by `DX-83`, only the row badge was stale. `DX-113` follow-up (`d7aa55c3`): closed its 2 remaining honest gaps — the state-object pixel-proof gap was already closed by `DX-118` (just needed the cross-reference fixed), and a new real GPU-load-timed fence back-pressure proof (Checks `E4`/`E4pre`/`E4a`, ~90ms load vs ~1µs control across 4 runs) plus a `DX-102` doc fix, `D3D12_Smoke` 193→196/196. **New Phase DX16** (`0e66133e`…`35a656ba`, `DX-149`–`DX-155`): opened from a real percentage audit (34 comparable EasyGL/D3D11/D3D12 feature-matrix rows, both backends started ~79%/~97%) — `EnvironmentMapEffect`/`SkinnedEffect` `DirectionalLight1`/`2` + specular tests (both backends, reusing `DX-124`/`DX-138`/`DX-125`/`DX-139`'s own methodologies), **real new `RenderTargetCube` MSAA on both D3D11 and D3D12** (`DX-152`, `fe8f3555` — reopened a previously-deliberate scope exclusion; neither backend's `TextureCube` SRV can ever be multisampled, so the MSAA resource is RTV-only with a separate resolve resource, same pattern as `DX-117`'s own 2D leg), `RenderTargetCube` mip-chain regen for a non-zero face (`DX-153`, confirming D3D11's whole-resource `GenerateMips()` and D3D12's `activeFace_`-scoped cascade are architecturally different mechanisms that both work), D3D12's 16-simultaneous-sampler-slot test (`DX-154`, porting D3D11's own `DX-142`), and `Model` root-bone-index flexibility (`DX-155`, `35a656ba` — corrected this row's own wrong premise: `rootBoneIndex` doesn't drive `Model::Draw()`, only `getRootProperty()` reflects it, found by reading `Model.cpp` before writing the test). `D3D11_Smoke` 135→**147/147**; `D3D12_Smoke` 191→**212/212**. Every task individually built, tested via real `ctest`, and committed/pushed separately (one task = one commit, per this project's own convention). |
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

**No hard blocker — `plan_dx.md`'s scope is entirely closed for both backends, through Phase DX16
(2026-07-15).** D3D11: every phase is ✅ except `DX-27`/`DX-90`/`DX-91` (real Windows hardware, or a
real device-removed trigger, both `needs_human`). D3D12: every phase is ✅ except `DX-110`/`DX-114`
(same two `needs_human` constraints) — no such machine is available in this dev environment for
any of the 5 remaining gates. The historical narrative below (through `DX-142`) documents how Phase
DX12/DX13 closed on 2026-07-14; **Phase DX13's own last 2 partial rows (`DX-117` MSAA follow-up,
`DX-121` SpriteBatch-effect CTest proof) and the entirely new Phase DX16 (`DX-149`–`DX-155`) closed
2026-07-15** — see §3's own top row for that detail, not repeated here. **D3D12's swap-chain crash is fixed** (2026-07-14): launching through a real,
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

**RESOLVED 2026-07-14 (`DX-15`)**: the full `CnaTests` GTest suite previously did not build under
`CNA_GRAPHICS_BACKEND=D3D11`/`D3D12` (`error: '::setenv' has not been declared; did you mean
'getenv'?'` across roughly 10 test files, mostly `tests/Microsoft/Xna/Framework/Audio/*Tests.cpp`,
plus `FrameworkDispatcherTests.cpp`/`Graphics/Texture2DTests.cpp`). Fixed by replacing every
`::setenv()` call site with `System::Environment::SetEnvironmentVariable(name, value)`
(sharp-runtime, already cross-platform), `#include`ing `System/Environment.hpp` where missing.
`cmake --build cmake-build-d3d11 --target CnaTests` and the same for `cmake-build-d3d12` both now
succeed cleanly, and a filtered real run genuinely executes and passes through a live device under
Wine (D3D11: `CnaInput*`/`AlphaTestReferenceScalingTest`, 30/30 passed; D3D12: same filter, real
`D3D12` device init confirmed). `CNA` itself and the standalone test executables
(`cna_test_d3d11_smoke`, `cna_test_d3d11_common`, `d3d12_smoke_test`) were already building fine —
only the full `CnaTests` target was ever affected.

---

## 5. Known bugs and limitations

| Status | Description | Task |
|---|---|---|
| Genuinely `needs_human` | D3D11's device-lost detection (`CheckDeviceRemoved()`) is implemented but its real trigger (`DXGI_ERROR_DEVICE_REMOVED`) cannot be induced under Wine — same constraint as D3D12's `DX-110`. The 5 combo `Clear*` variants (`DX-130`, Phase DX14) and window resize (`DX-83`) are now both real, pixel-verified tests, not open gaps. | 27 |
| Needs verification (real hardware, not Wine) | D3D11's debug-layer-missing fallback (`DXGI_ERROR_SDK_COMPONENT_MISSING` retry) and the `E_INVALIDARG`/drop-feature-level-11_1 fallback never fired on this machine (DXVK always provides what's needed) — genuinely untested code paths. Needs a real Windows machine without the D3D11 SDK debug layer installed, or a driver that rejects an explicit 11_1 request. | — |
| **DEFERRED (2026-07-11)** — investigated 3 times, not fixed, explicitly paused by the project owner | A `Depth24Stencil8`-attached `RenderTargetCube` face produces no colour output at all on Bgfx. See `plan_graphics.md`'s Task 952 entry for the full investigation trail. **Do not resume without explicit instruction** — see §9. | 952 |
| Confirmed bug, environment limitation | `Bgfx_RenderTarget2D_MsaaResolve`: this sandbox's bgfx OpenGL path negotiates only a legacy GL 2.1 context under which MSAA-flagged framebuffer textures don't sub-pixel resolve. | — |
| Confirmed bug | `EasyGL_MRT_TwoAttachments`: a basic 2-target MRT setup doesn't render correctly. Off-limits for opportunistic fixing (§9). | 145 |
| Confirmed bug | `Vulkan_DepthBias` fails; pre-existing, not investigated further. | — |
| Confirmed bug, not fixed | `GraphicsDevice.ReferenceStencil`'s independent-override semantics have zero backend connection on EasyGL/Bgfx (Vulkan already fixed). | 872 |
| Confirmed bug, found+reverted, needs its own task | `BgfxGraphicsBackend::DrawIndexedPrimitivesEx`'s non-wireframe path silently discards `GpuDrawParams::startIndex`/`baseVertex`. Not visible in any current sample/test. | 954 |
| Needs architecture decision | `Texture3D`/`TextureCube` inherit `GraphicsResource`, not `Texture` — no shader-sampling bind path via the generic `EffectParameter` route. Two named fix options, neither picked. | 863 |
| Needs architecture decision | `GraphicsDevice` state objects use C++ value semantics; FNA uses reference semantics. Project-wide implication. | 869 |
| **RESOLVED 2026-07-16** | HLSL→GLSL conversion approach for Phase 78: project owner decided manual line-by-line porting, no `dxc`+`SPIRV-Cross` pipeline. Phase 78 (Task 947) subsequently went 13/13 — see this file's own top banner. | 945 |
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

# Build and run CnaTests under D3D11/D3D12 (fixed 2026-07-14, see §4)
cmake --build cmake-build-d3d11 --target CnaTests -j4
cmake --build cmake-build-d3d12 --target CnaTests -j4
WINEPREFIX=~/.wine-cna-d3d11 wine cmake-build-d3d11/CnaTests.exe --gtest_filter="CnaInput*"
```

**Environment note (Linux-native backends):** the last-verified convention was that this sandbox
has no real X server on `:0` and every GPU/window-creating binary needs
`SDL_VIDEODRIVER=x11 DISPLAY=:99` (a virtual `Xvfb` display). **This was not re-checked this
session** and this session's own D3D11/Wine work directly observed `DISPLAY=:0` as a real, usable
desktop session with real GPU access — re-verify before assuming either claim (see §5's last row).

---

## 8. Next smallest tasks

1. **`plan_dx.md` is fully closed for BOTH backends — Phases DX1 through DX16, no exceptions except
   `needs_human` rows. There is NO further available Debian-side work on `plan_dx.md`.**
   `D3D11_Smoke` **147/147** + `D3D11_Common` 23/23; `D3D12_Smoke` **212/212** — both fully green,
   verified 2026-07-15. The only rows still open anywhere in the plan are `DX-27`/`DX-90`/`DX-91`
   (D3D11) and `DX-110`/`DX-114` (D3D12) — all `needs_human`, requiring either an actual Windows
   machine with a real GPU, or a real `DXGI_ERROR_DEVICE_REMOVED` trigger neither backend can induce
   under Wine. **Do not look for "next smallest tasks" inside `plan_dx.md` — there aren't any.** If
   the project owner wants more D3D work, it needs a new instruction (a new phase, or access to real
   Windows hardware for the `needs_human` rows). The actual next-smallest-tasks are items 2–4 below,
   all pre-existing, unrelated backlog outside `plan_dx.md`.
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
2. ~~Decide Task 945~~ — **RESOLVED 2026-07-16**: manual HLSL→GLSL porting, no `dxc`+`SPIRV-Cross`.
   Phase 78 (Task 947, the *unrelated* `../cna-samples` shader-conversion track) subsequently went
   0→**13/13**, plus 4 new backend capabilities (Tasks 1079–1082) — see this file's own top banner
   and `plan_graphics.md`. Nothing left to decide here.
3. **`plan_samples.md` standing queue** (formerly Phase 79, `plan_graphics.md` Tasks 957–1076,
   moved+renumbered `SAMPLE-1`–`SAMPLE-120` on 2026-07-16): a full re-audit of all 153
   `../cna-samples`-catalogued samples, one row per sample. **13 rows** (`SAMPLE-32`/`33`/`34`/`35`/
   `36`/`38`/`39`/`40`/`42`/`43`/`45`/`62`/`66`) now say "No longer CNA-blocked" thanks to Phase 78
   above — their own CNA-side shader gap is closed, but they're still `⬜` in `plan_samples.md`
   because **the actual sample port itself** (`.cpp`/`.hpp`/`Content/` under
   `../cna-samples/samples/<Name>/`) hasn't been written yet — that's a different repo, out of
   `cna_graphics` scope, tracked in `../cna-samples`'s own plan file (not opened this session).
   The other ~88 `⬜` rows in `plan_samples.md` are unrelated to shaders (re-verification passes,
   other DEFERRED.md items) — pick any of those, or any of the 13 above if the sibling repo's own
   plan calls for it. Do not touch `⛔` rows (structural/permanent, no CNA action possible).
4. Task 952 (`RenderTargetCube` depth-gating bug on Bgfx) remains **DEFERRED**, not a next task —
   see §9.
5. ~~D3D11's own honestly-flagged residual gaps (specular, multi-light, combo `Clear*`, `Model`/
   `SpriteFont` coverage)~~ — **all closed**, Phase DX14 (`DX-124`–`DX-130`, 2026-07-14) then Phase
   DX16 (`DX-149`–`DX-151`, 2026-07-15). Nothing left here.
6. ~~`cna_reference_dump`/`cna_demo_2d` D3D11/D3D12 build failures~~ — **fixed 2026-07-14**
   (circular-dependency link fix + `GameWindow` `MinimizeEXT()`/`RestoreEXT()`), historical, not a
   next task.

---

## 9. Do not do yet

- **`plan_dx.md` is entirely closed for both D3D11 and D3D12, through Phase DX16** (2026-07-15) —
  nothing left to authorize or implement there on this Debian machine. Only `DX-27`/`DX-90`/`DX-91`
  (D3D11) and `DX-110`/`DX-114` (D3D12) remain, all `needs_human` — a real Windows machine with a
  real GPU, or a real device-removed trigger neither backend can induce under Wine. **Do not open a
  "Phase DX17" or similar speculatively** — if the project owner wants more D3D work, they'll say so
  (e.g. the same way Phase DX16 itself started from an explicit percentage-audit request). Don't
  invent new gaps to close just because the plan file is open-ended in principle.
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
- **Do not resume Task 952** (Bgfx `RenderTargetCube` depth-gating bug) without explicit
  instruction — explicitly marked **DEFERRED** by the project owner after 2 full investigation
  rounds found no root cause.
- **Do not attempt Task 863 or Task 869** (the two architecture-decision items in §5) without the
  project owner picking a direction first.
- **Phase 78's `cna_graphics`-side shader-conversion work (Tasks 945/946/947/1079–1082) is DONE**
  (2026-07-16, explicit project-owner direction) — do not re-port any of the 13+1 already-closed
  shaders, and do not re-litigate Task 945's own decision (manual porting). What's still off-limits
  without new instruction: writing the actual sample ports themselves in the sibling `../cna-samples`
  repo — that's a different repo with its own plan file (not opened this session), genuinely outside
  `cna_graphics` scope, not merely "not yet started."
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

plan_dx.md (D3D11 + D3D12 backends) is now FULLY closed for everything this Debian dev environment
can prove: every phase (DX1 through DX16) is closed on both backends (2026-07-15) -- do not re-do
this work, and do not go looking inside plan_dx.md for "one more gap" to close. The only open rows
in the whole plan are DX-27/DX-90/DX-91 (D3D11) and DX-110/DX-114 (D3D12) -- all needs_human,
requiring either a real Windows machine with a real GPU, or a real DXGI_ERROR_DEVICE_REMOVED
trigger neither backend can induce under Wine. Do not attempt them; if such a machine becomes
available, start with DX-90/DX-114 per plan_dx.md's own checklists. Do not open a speculative new
D3D phase on your own initiative -- Phase DX16 itself only happened because the project owner asked
a specific question (a real EasyGL-parity percentage) that produced a concrete task list; don't
invent an equivalent prompt yourself.

plan_graphics.md's Phase 78 (DEFERRED.md #11, HLSL->GLSL sample shader conversion) is ALSO now fully
closed on the CNA side (2026-07-16, EasyGL only) -- Task 945 decided (manual porting), Task 947 went
0->13/13, and 4 new backend capabilities (Tasks 1079-1082) landed along the way. This is a completely
separate track from the D3D work above -- see this file's own top banner for the full list of what
closed. Do not re-port any of those shaders. What's left in this track: (a) the actual sample ports
themselves for those 13 samples, in the sibling ../cna-samples repo -- a different repo with its own
plan file, out of cna_graphics scope, not started this session; (b) plan_samples.md's other ~88 open
rows, unrelated to shaders (re-verification passes, other DEFERRED.md items) -- pick any of those.

Pick exactly one task from §8 "Next smallest tasks" (default to the first non-D3D item, since item 1
is now just a "nothing left here" notice, and item 2 is also just a "resolved" notice -- see item 3
onward). Inspect only the files that task names --
do not go exploring unrelated modules, and do not refactor anything you find along the way that
isn't directly required for this task. See §9 for what stays off-limits generally.

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
