# Direct3D 12 graphics backend

## Status

The D3D12 backend is a **native Windows Direct3D 12 graphics backend**, verified 2026-07-14 on this
Debian dev machine via Windows cross-compilation + Wine+vkd3d-proton (see "Development environment"
below). It is the **software/logic layer only** — every check below runs **off-screen**, because
swap-chain presentation is a known, real, unresolved gap on this dev loop (see "Known limitations").
Select it with:

```bash
cmake -S . -B cmake-build-d3d12 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
  -DCNA_GRAPHICS_BACKEND=D3D12 \
  -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-d3d12 --target CNA
```

`D3D12` is hard-gated to `CMAKE_SYSTEM_NAME=Windows` at configure time, same as `D3D11`. The
`cna_backend_graphics_d3d12` target links only `d3d12`+`dxgi`+`D3DCommon` — no `dxguid`, no
`d3dcompiler` (`plan_dx.md` `DX-100`'s confirmed minimum).

## What this backend is for (and isn't)

D3D12 is CNA's second native Direct3D backend, built directly on top of `D3D11`'s own experience
(shared `D3DCommon` HLSL/DXBC bytecode, shared format/state mapping tables, shared constant-buffer
struct layouts) rather than developed from scratch — `plan_dx.md` Phase DX12's own intro explicitly
deferred detailed D3D12 design until D3D11's own dev-loop lessons could inform it.

**What it proves**: a real `ID3D12Device` executing CNA's XNA-shaped `IGraphicsBackend` contract
off-screen — real command queues, descriptor heaps, command lists, fences with genuine N=2-frame
back-pressure, a real per-resource barrier-transition tracker, pipeline state objects, root
signatures, vertex/index buffers, 2D and cube textures, all 10 stock HLSL shader variants (the exact
same `hlsl_shaders.hpp` DXBC bytecode `D3D11` compiles and uses — design decision 5's reuse
bootstrap, not a re-derivation), a real `SpriteBatch`, and a real, functionally-proven device-removed
recovery path — all pixel-verified via real off-screen GPU readback, not just "the API call returned
`S_OK`."

**What it is not (yet)**:
- **Not presenting to a window.** Swap-chain creation is implemented for real (production
  `DXGI_SWAP_EFFECT_FLIP_DISCARD`) but crashes under this machine's Wine+vkd3d-proton setup — see
  "Known limitations." Every proof in this backend stays off-screen for this reason, unlike `D3D11`
  which does have a working (Wine-verified) swap chain.
- **Not verified on real Windows.** `plan_dx.md` `DX-114` (the D3D12 equivalent of `D3D11`'s
  `DX-90`) is explicitly `needs_human` — no real Windows machine is available in this dev
  environment.
- Several capabilities `D3D11` has for real do not exist yet for D3D12 at all: a public
  `D3D12RenderTargetBackend`, `Texture3D`, runtime-settable blend/depth-stencil/rasterizer state
  objects (PSO state is currently hardcoded, see below), per-slot `SamplerState`-driven sampling
  (samplers are hardcoded static samplers baked into the root signature), and occlusion queries.
  These are real, scoped gaps, not silently dropped — see "Known limitations."

## Development environment: Wine + vkd3d-proton dev-loop

This backend was built almost entirely without a Windows machine, the same way `D3D11` was, but
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
scripts/run-wine-vkd3d.sh cmake-build-d3d12/examples/d3d12_smoke_test.exe
```

CTest wires this in automatically — `ctest --test-dir cmake-build-d3d12 -R D3D12` runs the D3D12
test through the same wrapper.

## Writing a D3D12 test

Like `D3D11`, D3D12 tests are not ordinary `Game`-subclass examples — this backend has no working
window/`Present()` path to drive one through. All correctness tests live in
`examples/d3d12_smoke_test.cpp` (`D3D12_Smoke` CTest, the single registered D3D12 CTest — Checks A
through V as of `DX-113`, 80/80 passing) and talk to the real `ID3D12Device`/command
queue/list fairly directly. The general off-screen pixel-readback shape:

```cpp
// 1. Create (or reuse) a real D3D12GraphicsBackend/device.
// 2. Bind a minimal off-screen render target via BindOffscreenColorTargetEXT() (a NOXNA helper --
//    a raw ID3D12Resource+RTV the test itself creates and registers with the resource-state
//    tracker) rather than the swap chain, since presentation doesn't work under this dev loop.
// 3. Build known vertex/texture/cubemap data, get/create the right root signature
//    (D3D12RootSignatureCache) and PSO (D3D12PipelineStateCache) for the shader variant under test,
//    populate the correct D3DConstantBuffers struct (shared with D3D11, DX-60/60a) into a
//    persistently-mapped UPLOAD-heap buffer, and record a real draw call
//    (DrawInstanced/DrawIndexedInstanced) on the shared command list.
// 4. Execute + wait synchronously (ExecuteCommandListAndWaitEXT()) -- every draw in this backend
//    today is synchronous, no per-frame pipelining exists yet since Present() isn't real.
// 5. Read back specific pixels via a D3D12_HEAP_TYPE_READBACK buffer + CopyTextureRegion + Map, the
//    off-screen-safe D3D12 equivalent of D3D11's staging-texture readback.
// 6. Assert exact or discriminating-expected colors -- not just "the call returned S_OK." (DX-111's
//    own colored3d landing found a real silent-failure bug this way: a draw call returning S_OK but
//    painting nothing, due to an unset PSO cull-mode default.)
```

Continue the existing check-lettering convention (currently through `V`) rather than starting a new
scheme.

## Known limitations (2026-07-14)

- **Swap-chain presentation does not work on this dev loop — the single most significant open
  gap.** `CreateSwapChainForHwnd`/`FLIP_DISCARD` is implemented for real, matching `D3D11`'s own
  `DX-23` convention, but crashes: a null-pointer read inside Wine's own `dxgi.dll`
  (`d3d12_swapchain_init` → `vkd3d_instance_get_vk_instance(instance=0)`), reproduced twice
  (`DX-100`'s raw spike, then `DX-102`'s dedicated `examples/d3d12_swapchain_diag.cpp` diagnostic
  with a full symbolized backtrace). Root cause: a genuine architecture mismatch between Debian's
  system `dxgi.dll` (which expects Wine's own built-in `winevkd3d` instance state) and
  vkd3d-proton's separately-overridden `d3d12.dll` (which never populates it) — **not a CNA bug**.
  `CreateSwapChainResources()` is only ever reached when a real window is supplied;
  `D3D12_Smoke` always constructs off-screen (`window = nullptr`) specifically to avoid this crash
  path. Real swap-chain/`Present()` verification is `DX-114`'s job, on real Windows hardware.
- **Not verified on real Windows hardware at all.** `DX-114` (MSVC build, real DXGI present/tearing,
  full device-lost recovery trigger, WARP fallback) is `needs_human` — no such machine is available.
- **No runtime-settable blend/depth-stencil/rasterizer state objects.** Unlike `D3D11`'s
  `D3D11StateObjectCache`, D3D12 bakes this state directly into each PSO description. Every PSO
  built so far uses a hardcoded simplification (`depthEnable=false`, `cullMode=None`) rather than a
  real `XNA BlendState`/`DepthStencilState`/`RasterizerState` → PSO-desc-key mapping — a real,
  documented, scoped follow-up task, not an oversight (`DX-113`'s own audit confirmed this is "not
  applicable yet" rather than a coverage gap to close).
- **Samplers are hardcoded static samplers**, not driven by XNA `SamplerState` at all:
  `D3D12RootSignatureCache::MakeDefaultStaticSampler()` always uses
  `D3D12_FILTER_MIN_MAG_MIP_LINEAR` + `D3D12_TEXTURE_ADDRESS_MODE_WRAP`. There is no D3D12
  equivalent of `D3D11SamplerCache` yet, and no `TextureAddressMode::Clamp`/`Mirror`/point-filtering
  support.
- **No `D3D12RenderTargetBackend`.** Off-screen draws use a minimal, test-only
  `BindOffscreenColorTargetEXT()` NOXNA helper (a raw `ID3D12Resource`+RTV the test itself creates),
  not a public, `IRenderTargetBackend`-implementing class. `RenderTarget2D`/`RenderTargetCube`/MRT/
  MSAA construction via the real XNA API do not work against this backend yet.
- **No `Texture3D` support.** `D3D12TextureBackend` (2D) and `D3D12TextureCubeBackend` (cube, added
  when `env_map3d` needed one) are real; `Texture3D` was explicitly triaged out (`DX-109`) and has
  no D3D12 backend at all.
- **`D3D12TextureCubeBackend::GetData()` is a no-op** (the interface's own default) — `SetData()` is
  real; readback is not, a narrower gap than `D3D11`'s own real cube-texture readback.
- **No occlusion query support.** Unlike `D3D11` (`DX-47`), Phase DX12's task list has no
  `ID3D12Query`-based occlusion-query task at all — `CreateOcclusionQuery()` falls through to
  `IGraphicsBackend`'s own silent `nullptr` default.
- **Custom `Effect` via `SpriteBatch::Begin(effect)`** (`D3D11`'s own `DX-71`) has no D3D12
  equivalent — `D3D12SpriteBatchBackend` only draws through the stock `sprite2d` pipeline.
- **`SpriteFont`, `Model`, mip chains beyond level 0, and multi-light/specular/`WeightsPerVertex`
  discrimination** are all unverified against this backend, the same honest gaps `D3D11`'s own docs
  list for the same reasons (they build on already-tested primitives but have no D3D12-specific
  dedicated test).
- **Device-removed recovery is real but its trigger is untestable here.** `RecreateDeviceEXT()`
  (`DX-110`) genuinely tears down and rebuilds every device-lifetime resource, and is functionally
  proven (fresh GPU work round-trips through the recreated device) — but a genuine
  `DXGI_ERROR_DEVICE_REMOVED` cannot be triggered on this dev loop, so the detection *trigger* path
  itself (as opposed to the recovery logic) is unverified. Same honest constraint `D3D11`'s own
  `DX-27`/`DX-90` gap has.
- **`d3dx12.h`** (Microsoft's optional D3D12 helper header, e.g. `D3D12CalcSubresource()`) is absent
  from this machine's MinGW-w64 D3D12 headers (`DX-100`'s own finding) — the whole backend uses raw
  `ID3D12*` calls directly.
- **`CnaTests` (the gtest suite) does not build for D3D12** — same MinGW/Windows-cross-target
  portability gap `D3D11`'s own `DX-15` already found and left out of scope (~10 test files calling
  POSIX-only `::setenv()`).

See `plan_dx.md` for the full task-by-task status (`DX-100` through `DX-115`) and design rationale,
and `docs/graphics-backend-feature-matrix.md` for a row-by-row comparison against the other
established backends (including `D3D11`).
