# Direct3D 12 graphics renderer

## Status

The D3D12 renderer is a **native Windows Direct3D 12 graphics renderer**, verified on this Debian
development machine through Windows cross-compilation and Wine+vkd3d-proton. Most CTests use the
real GPU through an off-screen public `GraphicsDevice`; three resize/presentation fixtures use a
Proton-managed real-window launch. Select it with:

```bash
cmake -S . -B cmake-build-d3d12 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/mingw-w64.cmake" \
  -DCNA_GRAPHICS_RENDERER=DIRECTX12 \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_SHARP_RUNTIME_ROOT="$PWD/../sharp-runtimenext"
cmake --build cmake-build-d3d12
```

The renderer identity is **`DIRECTX12`**, not `D3D12`, and the toolchain file must be an absolute
path — both corrected by `plans/plan_dx.md` `DX-249`, which found the documented command does not
work. `--target CnaTests` was also wrong: that target does not build on any Windows toolchain (see
`.github/workflows/d3d-windows-ci.yml`'s own header), and it is not what produces this renderer's
test executables.

### Running the tests off-screen

`CNA_FORCE_HEADLESS_DEVICE_EXT=DIRECTX12` makes `GraphicsDevice` create this renderer's device as
if `PresentationParameters::HeadlessEXT` had been set — no window, no swap chain, rendering into the
implicit off-screen back buffer (`DX-241`) and reading it back through `GetBackBufferData`
(`DX-205`). That is what lets the renderer-neutral `Game`-harness corpus run here at all, since
`CreateSwapChainForHwnd` faults inside vanilla Wine's `dxgi.dll`; `ctest -L DIRECTX12` sets it for
every registered fixture (`DX-235`). It is a comma-separated renderer-name list, so it cannot
accidentally put a renderer that has no headless mode into one.

`D3D12` is hard-gated to `CMAKE_SYSTEM_NAME=Windows` at configure time, same as `D3D11`. The
`cna_renderer_directx12` target links only `d3d12`+`dxgi`+`D3DCommon` — no `dxguid`, no
`d3dcompiler` (`plans/plan_dx.md` `DX-100`'s confirmed minimum).

## What this renderer is for (and isn't)

D3D12 is CNA's second native Direct3D renderer, built directly on top of `D3D11`'s own experience
(shared `D3DCommon` HLSL/DXBC bytecode, shared format/state mapping tables, shared constant-buffer
struct layouts) rather than developed from scratch — `plans/plan_dx.md` Phase DX12's own intro explicitly
deferred detailed D3D12 design until D3D11's own dev-loop lessons could inform it.

**What it proves**: a real `ID3D12Device` executes CNA's public XNA-shaped graphics contract.
The shared corpus covers buffers, every core-XNA texture format, 2D/cube render targets including
MSAA and MRT, state objects, SpriteBatch/SpriteFont, all stock effects, models/content, runtime HLSL
`ShaderEffect`, queries, presentation and deterministic device recovery. These paths are verified by
GPU readback rather than only successful API return values. D3D12-specific descriptor and command
invariants remain in the deliberately small smoke executable.

**Measured state, 2026-09-09 (`plans/plan_dx.md` Phase DX17).** `ctest -L DIRECTX12` passes
**264/264**, with no CTest skips, and `D3D12_Smoke` passes **25/25** retained internal checks. The
shared registration inventory contains **265 declarations: 262 renderer-neutral fixtures, two
reasoned D3D11-native exceptions and one D3D12-native exception**. This result used the fixed
`sharp-runtimenext` checkout, Wine+vkd3d-proton and private virtual Xwayland `:4`; no physical
display was used.

The renderer has two command-lifetime modes today: ordinary draw/clear/upload operations still
submit and wait synchronously, while the three real-window resize fixtures use the Proton wrapper.
The allocated two-frame fence/allocator machinery is not yet production frame pipelining; replacing
the per-call waits is explicitly `DX-237`, followed by upload staging in `DX-238`.

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
   via its Steam "Proton - Experimental" install (`plans/plan_dx.md` `DX-100`'s own spike) — no new
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
   See `plans/plan_dx.md` `DX-100`'s own row for the exact steps this machine used.
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

CTest wires this in automatically — `ctest --test-dir cmake-build-d3d12 -L DIRECTX12` runs the
complete D3D12 label through either the routine Wine wrapper or the explicitly selected Proton
window wrapper.

## Writing a D3D12 test

Renderer-neutral behavior belongs in a public `Game`/`GraphicsDevice` fixture and is declared once
with `cna_d3d_parity_fixture()` in `cmake/DirectXParityTests.cmake`; it then runs on D3D11 and D3D12.
A one-renderer exception requires a human-readable reason. Mark a fixture `DIRECTX12_PROTON` only
when it genuinely requires a real window; the default wrapper supplies the off-screen public device.
Use a native D3D12 executable only for an invariant that cannot be observed through the public API,
such as descriptor-heap generation or resource-state tracking. A native pixel diagnostic follows
this shape:

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

`D3D12_Smoke` keeps its local check naming, but new public conformance proof must not be added there
instead of the shared renderer-neutral fixture.

## Known limitations (2026-09-09)

- **CPU-visible operations remain synchronization boundaries.** `DX-237` records clears, draws and
  resolves per frame, while `DX-238` stages buffer and texture uploads through persistently mapped
  frame rings. CPU texture/back-buffer readback, CPU mip generation, resize/recreation and teardown
  still submit pending work and wait deliberately. Occlusion queries are asynchronous since
  `DX-240`: public `Begin()`/`End()` span any number of frame-list draws and `IsComplete()` polls the
  resolve fence without forcing a submit or wait.
- **Plain Wine cannot create the swap chain used by this vkd3d-proton build.** The matched
  `scripts/run-proton-vkd3d.sh` launch is required for windowed tests. This is not an untested-only
  path: `BackbufferResize`, `RealWindowResize` and `ViewportResetAfterResize` are registered
  Proton CTests and pass on the private virtual display. The remaining native present/tearing,
  exclusive-fullscreen, WARP and real-driver coverage belongs to `DX-114`.
- **A genuine device-removal trigger is not available in this environment.** `DX-244` proves the
  complete public two-phase loss/restore path for 16 cycles, including resources, loaded content and
  events; `DX-114` must still prove that a native `DXGI_ERROR_DEVICE_REMOVED` reaches that path.
- **Compiled XNA `Effect` bytecode is unsupported.** `SupportsCompiledEffects()` is false and
  `CreateCompiledEffect()` returns null. Runtime-source `ShaderEffect` and all stock effects are
  working paths. `plans/plan_dx.md` `DX-248` records the unassigned D3D12 ownership decision;
  the implementation itself belongs in `plans/plan_fx.md`, not DX17.
- **Native Windows execution remains a separate gate.** The Wine+vkd3d-proton results prove CNA's
  renderer behavior but do not substitute for the MSVC, WARP and vendor-driver evidence required by
  `DX-114` and `DX-246`.

See `plans/plan_dx.md` for the authoritative task-by-task status and design rationale, and
`docs/graphics-renderer-feature-matrix.md` for the cross-renderer comparison.
