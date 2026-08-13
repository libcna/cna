# Direct3D 12 graphics renderer

## Status

The D3D12 renderer is a **native Windows Direct3D 12 graphics renderer**, verified 2026-07-14 on this
Debian dev machine via Windows cross-compilation + Wine+vkd3d-proton (see "Development environment"
below). The **routine CTest suite runs off-screen** (real GPU proof, but not through a live window) —
presentation through a real window/swap chain is separately proven, but only via a manual diagnostic
(`modules/renderers/directx12/examples/directx12_swapchain_diag.cpp`, through a Proton-managed launch), not the routine CTest, since
that launch is too heavy for a normal CI run on this dev loop. See "Known limitations" for the exact
boundary. Select it with:

```bash
cmake -S . -B cmake-build-d3d12 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
  -DCNA_GRAPHICS_RENDERER=D3D12 \
  -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-d3d12 --target CnaTests
```

`D3D12` is hard-gated to `CMAKE_SYSTEM_NAME=Windows` at configure time, same as `D3D11`. The
`cna_renderer_directx12` target links only `d3d12`+`dxgi`+`D3DCommon` — no `dxguid`, no
`d3dcompiler` (`plan_dx.md` `DX-100`'s confirmed minimum).

## What this renderer is for (and isn't)

D3D12 is CNA's second native Direct3D renderer, built directly on top of `D3D11`'s own experience
(shared `D3DCommon` HLSL/DXBC bytecode, shared format/state mapping tables, shared constant-buffer
struct layouts) rather than developed from scratch — `plan_dx.md` Phase DX12's own intro explicitly
deferred detailed D3D12 design until D3D11's own dev-loop lessons could inform it.

**What it proves**: a real `ID3D12Device` executing CNA's XNA-shaped `IGraphicsRenderer` contract —
real command queues, descriptor heaps, command lists, fences with genuine N=2-frame back-pressure, a
real per-resource barrier-transition tracker, pipeline state objects (including real runtime-settable
`BlendState`/`DepthStencilState`/`RasterizerState`), root signatures, real per-slot dynamic
`SamplerState`, vertex/index buffers, 2D/cube/3D textures, a real `RenderTarget2D`/`RenderTargetCube`/
MRT implementation with real mip-chain generation and device-queried MSAA (2D leg only, resolved via
`ResolveSubresource()` on unbind), real occlusion queries, all 10 stock HLSL shader
variants plus the `AlphaTestEffect.VertexColorEnabled`-specific `alpha_test_colored3d` sibling (the
exact same `hlsl_shaders.hpp` DXBC bytecode `D3D11` compiles and uses — design decision 5's reuse
bootstrap, not a re-derivation), a real `SpriteBatch` with runtime-compiled custom-`ShaderEffect`
support, and a real, functionally-proven device-removed recovery path — all pixel-verified via real
GPU readback (off-screen for the routine CTest suite; through a live window for the separate Proton
diagnostic, see "Known limitations"), not just "the API call returned `S_OK`."

**What it is not (yet)**:
- **Not routinely presenting to a window in CTest.** Swap-chain *creation* genuinely works (a
  properly Proton-managed launch, `scripts/run-proton-vkd3d.sh`) and `Present()`/back-buffer
  rendering are real and proven through a live window (`DX-116`) — but only via a manual diagnostic
  (`modules/renderers/directx12/examples/directx12_swapchain_diag.cpp`), not the routine `DirectX12_Smoke` CTest, since Proton's own
  bootstrap launch is too heavy/slow for a normal CTest run on this dev loop. See "Known
  limitations" for the full plain-Wine-vs-Proton distinction.
- **Not verified on real Windows.** `plan_dx.md` `DX-114` (the D3D12 equivalent of `D3D11`'s
  `DX-90`) is explicitly `needs_human` — no real Windows machine is available in this dev
  environment.
- **`D3D12TextureCubeRenderer::GetData()`** was a no-op — now real (`DX-123`).
- Runtime-settable blend/depth-stencil/rasterizer state objects, per-slot `SamplerState`, occlusion
  queries, `Texture3D`, custom `ShaderEffect` compilation, and `SpriteBatch::Begin(effect)`'s own
  integration on top of it are all real and independently CTest-proven now (`DX-118`/`DX-119`/
  `DX-120`/`DX-122`/`DX-121`) — see "Known limitations" for what's still genuinely open.

## Development environment: Wine + vkd3d-proton dev-loop

This renderer was built almost entirely without a Windows machine, the same way `D3D11` was, but
using a different Windows-D3D-to-Vulkan translation layer:

```text
Debian (this repo's actual dev machine)
└── Windows cross-build (cmake/toolchains/mingw-w64.cmake, same toolchain D3D11/SDL_RENDERER use)
    └── D3D12
         ├── compile: MinGW-w64 (x86_64-w64-mingw32-{gcc,g++})
         ├── local dev-loop test: Wine + vkd3d-proton (D3D12 calls → real Vulkan calls on the real GPU)
         └── final verification: a real Windows machine (still open, DX-114)
```

D3D12 needs `vkd3d-proton` (Direct3D 12→Vulkan translation), not `DXVK` (D3D9/10/11→Vulkan) —
`D3D11`'s own Wine prefix/DLL overrides do not carry over, hence a **separate, dedicated Wine
prefix**:

1. Install the cross toolchain (same as `D3D11`): `sudo apt install mingw-w64`.
2. Obtain `vkd3d-proton`'s `d3d12.dll`/`d3d12core.dll`. This dev machine already had them locally
   via its Steam "Proton - Experimental" install (`plan_dx.md` `DX-100`'s own spike) — no new
   install or `sudo` needed, the same no-elevated-changes bar DXVK's own setup met. If Steam/Proton
   isn't available, `vkd3d-proton` ships prebuilt releases on GitHub that can be dropped in the same
   way.
3. Initialize a dedicated Wine prefix and register the DLLs as native overrides:
   ```bash
   WINEPREFIX=~/.wine-cna-d3d12 wineboot --init
   # copy vkd3d-proton's d3d12.dll/d3d12core.dll into system32/syswow64, then:
   WINEPREFIX=~/.wine-cna-d3d12 wine reg add 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' /v d3d12 /d native /f
   WINEPREFIX=~/.wine-cna-d3d12 wine reg add 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' /v d3d12core /d native /f
   ```
   See `plan_dx.md` `DX-100`'s own row for the exact steps this machine used.
4. Configure and build as shown above.
5. Run any built `.exe` through `scripts/run-wine-vkd3d.sh` — mirrors `D3D11`'s own
   `scripts/run-wine-dxvk.sh`/`DX-85` gate exactly, but for vkd3d-proton: sets `WINEPREFIX`
   (override with `CNA_D3D12_WINEPREFIX`), execs `wine "$@"`, and asserts a real
   `vkd3d-proton - applicationVersion: <version>` log line actually appeared, failing loudly (exit
   3) if it didn't. A binary that legitimately never creates a D3D12 device should set
   `CNA_D3D12_SKIP_VKD3D_GATE=1` to opt out.

```bash
scripts/run-wine-vkd3d.sh cmake-build-d3d12/examples/directx12_smoke_test.exe
```

CTest wires this in automatically — `ctest --test-dir cmake-build-d3d12 -R D3D12` runs the D3D12
test through the same wrapper.

## Writing a D3D12 test

Like `D3D11`, D3D12 tests are not ordinary `Game`-subclass examples — the routine CTest suite has no
Proton-managed window/`Present()` path to drive one through (Proton's own bootstrap launch is too
heavy for a normal CTest run, see "Known limitations"). All correctness tests live in
`modules/renderers/directx12/examples/directx12_smoke_test.cpp` (`DirectX12_Smoke` CTest, the single registered D3D12 CTest — checks
lettered A through VV as of `DX-113`/`DX-117`/`DX-121`/`DX-136`/`DX-144`/`DX-149`–`DX-155`,
**212/212 passing**) and talk to the real
`ID3D12Device`/command queue/list fairly directly. The general off-screen pixel-readback shape:

```cpp
// 1. Create (or reuse) a real DirectX12Renderer/device.
// 2. Bind a minimal off-screen render target via BindOffscreenColorTargetEXT() (a CNAEXT helper --
//    a raw ID3D12Resource+RTV the test itself creates and registers with the resource-state
//    tracker) or a real D3D12RenderTargetRenderer (DX-117) rather than the swap chain, since the
//    routine CTest suite doesn't use the heavy Proton-managed launch presentation needs.
// 3. Build known vertex/texture/cubemap data, get/create the right root signature
//    (D3D12RootSignatureCache) and PSO (D3D12PipelineStateCache) for the shader variant under test,
//    populate the correct D3DConstantBuffers struct (shared with D3D11, DX-60/60a) into a
//    persistently-mapped UPLOAD-heap buffer, and record a real draw call
//    (DrawInstanced/DrawIndexedInstanced) on the shared command list.
// 4. Execute + wait synchronously (ExecuteCommandListAndWaitEXT()) -- every draw in this renderer
//    today is synchronous, no per-frame pipelining exists yet.
// 5. Read back specific pixels via a D3D12_HEAP_TYPE_READBACK buffer + CopyTextureRegion + Map, the
//    off-screen-safe D3D12 equivalent of D3D11's staging-texture readback.
// 6. Assert exact or discriminating-expected colors -- not just "the call returned S_OK." (DX-111's
//    own colored3d landing found a real silent-failure bug this way: a draw call returning S_OK but
//    painting nothing, due to an unset PSO cull-mode default.)
```

Continue the existing check-lettering convention (currently through double letters, `AA`–`NN`)
rather than starting a new scheme.

## Known limitations (2026-07-14, re-audited against `plan_dx.md`'s actual `DX-100`–`DX-148` row
status — most of this section's earlier revisions predated Phase DX13/DX14/DX15 landing and were
significantly stale; re-derived from source, not copy-edited)

- **Swap-chain presentation under *plain* Wine does not work — but under a properly Proton-managed
  launch, it does.** `CreateSwapChainForHwnd`/`FLIP_DISCARD` crashes under plain Wine: a null-pointer
  read inside Wine's own `dxgi.dll` (`d3d12_swapchain_init` → `vkd3d_instance_get_vk_instance
  (instance=0)`), reproduced twice (`DX-100`'s raw spike, then `DX-102`'s dedicated
  `modules/renderers/directx12/examples/directx12_swapchain_diag.cpp` diagnostic with a full symbolized backtrace) — a genuine
  architecture mismatch between Debian's system `dxgi.dll` and vkd3d-proton's separately-overridden
  `d3d12.dll`, **not a CNA bug**. `DX-102` later found the real fix: a properly Proton-managed launch
  (`scripts/run-proton-vkd3d.sh`) gives vkd3d-proton the matched DLL pair it expects, and swap-chain
  *creation* genuinely succeeds. `DX-116` then closed real `Present()`/back-buffer rendering on top
  of that — a real 10-frame `Clear()`+`Present()` loop through a live window, verified twice from a
  clean log, zero crashes. **The routine `DirectX12_Smoke` CTest still constructs off-screen** (Proton's
  own bootstrap is too heavy/slow for a normal CTest run), so this is a real, permanent split: use
  `DirectX12_Smoke`/plain Wine for routine pixel-correctness work, `run-proton-vkd3d.sh` for anything
  that genuinely needs a live window. Full tearing/vsync/exclusive-fullscreen policy-branch
  verification and window resize (`D3D11`'s own `DX-29` equivalent) remain open, real, scoped gaps.
- **Not verified on real Windows hardware at all.** `DX-114` (MSVC build, real DXGI present/tearing,
  full device-lost recovery trigger, WARP fallback) is `needs_human` — no such machine is available.
- **MSAA render targets** are now real for both `D3D12RenderTargetRenderer` (`DX-117` follow-up,
  device-queried via `CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS)`, resolved via a
  real `ResolveSubresource()` on unbind, Checks OO0/OO1) and `D3D12RenderTargetCubeRenderer`
  (`DX-152` — a previously deliberate scope exclusion, since reopened: `D3D12_SRV_DIMENSION_TEXTURECUBE`
  has no multisampled variant, so the MSAA color resource is RTV-only and a separate single-sample
  resource is resolved into on unbind, face-scoped, Checks SS0/SS1). `D3D12RenderTargetRenderer`/
  `D3D12RenderTargetCubeRenderer` are otherwise real (`DX-117`, including real MRT and, as of
  `DX-144`, real mip-chain generation for both 2D and cube — a CPU box-filter downsample cascade,
  not D3D11's driver-level `GenerateMips()`; `RenderTargetCube` only regenerates the active face's
  own chain on unbind, the same honest single-active-face scope `D3D11RenderTargetCubeRenderer`'s own
  test coverage already has). Per-target MSAA-resolve-on-unbind for `N>1` MRT (`D3D11`'s own
  `DX-143` equivalent) was not attempted for D3D12 — a real, scoped follow-up.
- **Device-removed recovery is real but its trigger is untestable here.** `RecreateDeviceEXT()`
  (`DX-110`) genuinely tears down and rebuilds every device-lifetime resource, and is functionally
  proven (fresh GPU work round-trips through the recreated device) — but a genuine
  `DXGI_ERROR_DEVICE_REMOVED` cannot be triggered on this dev loop, so the detection *trigger* path
  itself (as opposed to the recovery logic) is unverified. Same honest constraint `D3D11`'s own
  `DX-27`/`DX-90` gap has.
- **`AlphaTestEffect.VertexColorEnabled`** now has a real dedicated shader variant
  (`alpha_test_colored3d`, stride 24) and dedicated tests (`DX-136`) — previously a real gap
  (`alpha_test3d` had no vertex-color attribute at all), now closed on both D3D11 and D3D12.
- **`SpriteBatch::Begin(effect)`** (`D3D11`'s own `DX-71`) is now fully real and independently
  CTest-proven (`DX-121`, Checks NN0/NN1) — the runtime `D3DCompile()` custom-`ShaderEffect` path
  (Checks BB1–BB4) and the `SpriteBatch`-specific integration on top of it were closed separately;
  `PresentationParameters::HeadlessEXT` removed the windowed-`GraphicsDevice` blocker that
  originally kept the integration itself from being independently proven.
- **The following are all real and closed now, despite earlier revisions of this section claiming
  otherwise** — re-verify against `plan_dx.md`'s own row status before trusting any *other* specific
  claim in this file, since this whole section was significantly stale before this re-audit:
  runtime-settable `BlendState`/`DepthStencilState`/`RasterizerState` → PSO objects (`DX-118`,
  replacing the old hardcoded `depthEnable=false`/`cullMode=None` PSO defaults); real per-slot
  dynamic `SamplerState` across 16 slots, a real `D3D12SamplerCache` (`DX-119`, replacing the old
  single hardcoded static sampler); a real public `D3D12RenderTargetRenderer`/
  `D3D12RenderTargetCubeRenderer` implementing `IRenderTargetRenderer` for real, including MRT
  (`DX-117`, replacing the old test-only `BindOffscreenColorTargetEXT()`-only story); real
  `D3D12Texture3DRenderer` (`DX-122`); real `D3D12TextureCubeRenderer::GetData()` readback (`DX-123`,
  no longer a no-op); real `D3D12OcclusionQueryRenderer` (`DX-120`, `CreateOcclusionQuery()` no longer
  falls through to a silent `nullptr`).
- **`d3dx12.h`** (Microsoft's optional D3D12 helper header, e.g. `D3D12CalcSubresource()`) is absent
  from this machine's MinGW-w64 D3D12 headers (`DX-100`'s own finding) — the whole renderer uses raw
  `ID3D12*` calls directly.
- **`CnaTests` (the gtest suite) now builds and links for D3D12**, same as `D3D11` — the
  MinGW/Windows-cross-target `::setenv()` portability gap `DX-15` found and fixed applied equally to
  both renderers (the fix is in the shared `tests/` sources, not renderer-specific). Confirmed running
  under plain Wine for test groups that don't create a live `GraphicsDevice` window (7/7 passed).
  Test groups that do create one hit this same page's own already-documented plain-Wine swap-chain
  limitation (`DX-100`/`DX-102`) and need the Proton launch path, not plain `wine`, to run — no new
  gap, just this pre-existing one now reachable through `CnaTests` too.

See `plan_dx.md` for the full task-by-task status (`DX-100` through `DX-148`) and design rationale,
and `docs/graphics-renderer-feature-matrix.md` for a row-by-row comparison against the other
established renderers (including `D3D11`).
