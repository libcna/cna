# Direct3D 11 graphics renderer

## Status

The D3D11 renderer is a **native Windows Direct3D 11 graphics renderer**, verified 2026-07-14 on this
Debian dev machine via Windows cross-compilation + Wine+DXVK (see "Development environment" below —
real Windows hardware verification is a separate, still-open gate, see "Known limitations"). Select
it with:

```bash
cmake -S . -B cmake-build-d3d11 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
  -DCNA_GRAPHICS_RENDERER=D3D11 \
  -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-d3d11 -j
```

`D3D11` is hard-gated to `CMAKE_SYSTEM_NAME=Windows` at configure time — attempting it on a native
Linux/macOS configure fails fast with `FATAL_ERROR`, pointing at the MinGW-w64 toolchain file above.
No extra CMake dependency is fetched: `d3d11`/`dxgi`/`d3dcompiler` are all provided by the Windows
SDK (MSVC) or MinGW-w64's own headers/import libraries (this dev machine's actual path — see
`plans/plan_dx.md` `DX-1`).

## What this renderer is for (and isn't)

XNA 4.0 itself was a thin wrapper over Direct3D 9, and modern Windows XNA/FNA-style games run
closest to their original execution environment on a real Direct3D renderer, not through OpenGL/
Vulkan/bgfx translation layers. `D3D11` is CNA's first **native** Direct3D renderer (as opposed to
`BGFX`, which can already select a D3D11/D3D12 renderer *internally* on Windows, but that's bgfx's
own abstraction, not CNA's) — it gives this project a dependency-free Windows path and direct
control over the exact Direct3D calls made, matching `CLAUDE.md`'s "preserve XNA-style APIs...
using modern C++23 internals" mandate more directly than routing through a third abstraction layer.

**What it proves**: a real `ID3D11Device`/`ID3D11DeviceContext` executing CNA's XNA-shaped
`IGraphicsRenderer` contract — real buffers, textures, render targets (including MSAA/MRT), state
objects, all 10 stock HLSL shader variants (colored/textured/lit/alpha-test/dual-texture/env-map/
skinned/sprite/instanced), a real SpriteBatch, and a runtime-`D3DCompile()`-backed custom
`ShaderEffect` path — all pixel-verified via real GPU readback, not just "the API call returned
`S_OK`."

**What it is not (yet)**: verified on real Windows. Every check above ran through Wine+DXVK on this
Debian machine's real GPU (an AMD Radeon 780M/RADV, translated by DXVK 2.6.0) — this proves the
renderer's *logic* (call sequencing, resource lifetime, HLSL correctness, pixel math), the same
"Wine proves the logic, not real-hardware parity" bar this project already established for
`SDL_RENDERER`. Real DXGI present/tearing behavior, real device-lost recovery, WARP fallback, and
MSVC-vs-MinGW ABI parity are still open — see `plans/plan_dx.md` `DX-90`/`DX-91`.

## Development environment: Wine + DXVK dev-loop

This renderer was built almost entirely without a Windows machine:

```text
Debian (this repo's actual dev machine)
└── Windows cross-build (cmake/toolchains/mingw-w64.cmake, already used by SDL_RENDERER)
    └── D3D11
         ├── compile: MinGW-w64 (x86_64-w64-mingw32-{gcc,g++})
         ├── local dev-loop test: Wine + DXVK (D3D11 calls → real Vulkan calls on the real GPU)
         └── final verification: a real Windows machine (still open, DX-90/DX-91)
```

To reproduce this locally:

1. Install the cross toolchain: `sudo apt install mingw-w64` (same package `SDL_RENDERER`'s own
   Windows cross-build already uses).
2. Install Wine + DXVK: `sudo apt-get install -y dxvk-wine64` (pulls in `dxvk`/`dxvk-wine32:i386`
   too on Debian). Full install commands: `programs.md` §10.
3. Initialize a dedicated Wine prefix and install DXVK into it:
   ```bash
   WINEPREFIX=~/.wine-cna-d3d11 wineboot --init
   WINEPREFIX=~/.wine-cna-d3d11 dxvk-setup install
   ```
4. Configure and build as shown above.
5. Run any built `.exe` through `scripts/run-wine-dxvk.sh` — this wrapper sets `WINEPREFIX`
   (override with `CNA_D3D11_WINEPREFIX`), execs `wine "$@"` (**not** `wine64` — this Debian's Wine
   10.0 packaging has no separate `wine64` binary; `wine` auto-detects PE32 vs. PE32+), and — as of
   `plans/plan_dx.md` `DX-85` — automatically asserts a `DXVK: <version>` marker actually appeared in the
   run's log output, failing loudly (exit 3) if the run silently fell back to `WineD3D` instead of
   DXVK. A binary that legitimately never opens a D3D11 device (e.g. a pure-mapping-table unit test)
   should set `CNA_D3D11_SKIP_DXVK_GATE=1` to opt out of this check.

```bash
scripts/run-wine-dxvk.sh cmake-build-d3d11/examples/directx11_smoke_test.exe
```

CTest wires this in automatically — `ctest --test-dir cmake-build-d3d11 -R D3D11` runs every D3D11
test through the same wrapper.

## Writing a D3D11 test

D3D11 tests are **not** ordinary `Game`-subclass examples like most other renderers' tests — this
renderer is still missing the full `IShaderRenderer`/`Effect`-driven high-level draw path for every
XNA-level entry point (custom `ShaderEffect` and the public `SpriteBatch`/`Texture2D` API are
real and tested; some lower-level XNA convenience paths are not yet exercised — see "Known
limitations"). Most D3D11 correctness tests instead talk to the real `ID3D11Device`/
`ID3D11DeviceContext` fairly directly, going through `DirectX11Renderer::GetDeviceEXT()` (a
`CNAEXT` accessor added specifically so tests/`D3DCommon` callers can reach the real device without
duplicating its creation path) and `D3DCommon`'s shader/input-layout/constant-buffer helpers. See
`modules/renderers/directx11/examples/directx11_smoke_test.cpp` (`DirectX11_Smoke` CTest, the primary GPU-facing pixel-correctness
suite — Checks A through AC as of `DX-85`) and `modules/renderers/directx11/examples/directx11_common_test.cpp` (`DirectX11_Common`
CTest, pure-function format/state/vertex-layout mapping-table checks, no GPU/device needed) for the
two established patterns. The general pixel-readback shape:

```cpp
// 1. Create (or reuse) a real device via DirectX11Renderer::GetDeviceEXT(), or construct one
//    directly the same way DX-20 does.
// 2. Bind an offscreen render target (D3D11RenderTargetRenderer, DX-43) rather than the swap chain,
//    so the test doesn't disturb window presentation.
// 3. Build known vertex/texture/cubemap data, get real shader objects (D3DShaderCache, DX-15-embed)
//    and a real input layout (D3D11InputLayoutCache, DX-32), populate the correct D3DConstantBuffers
//    struct (DX-60/60a) for the variant under test, and issue a real Draw()/DrawIndexed()/
//    DrawInstanced() call.
// 4. Read back specific pixels via the same staging-texture + Map(READ) technique DX-28's
//    ReadBackbuffer() established (RowPitch-aware — never assume tightly-packed rows).
// 5. Assert exact or discriminating-expected colors -- not just "the call returned S_OK."
```

`DirectX11_Common`'s pure-function tests (no device/GPU) are the right home for anything that's a real
mapping-table/logic check rather than a rendering-correctness one (format/state enum mapping,
vertex-stride inference, cbuffer `static_assert` layout checks already caught at compile time).

## Known limitations (2026-07-14)

- **Not verified on real Windows hardware** — `plans/plan_dx.md` `DX-90` (MSVC build, real DXGI
  present/tearing, full device-lost recovery, WARP fallback, debug-layer-missing fallback on a
  machine that should have it) and `DX-91` (Intel/AMD/NVIDIA driver-specific spot checks) are both
  explicitly `needs_human`/best-effort — no such machine is available in this dev environment. Every
  claim above is proven through Wine+DXVK on one real AMD/RADV GPU, not multi-vendor real-hardware
  parity.
- **Device-lost/removed recovery is detection-only.** `DX-27`'s `CheckDeviceRemoved()` logic exists
  and is wired into `Present()`/`EnsureSwapChainSize()`'s failure paths, but has never actually
  fired — no real device removal occurred during Wine+DXVK testing. Full recovery (recreating all
  three resource-lifetime groups per design decision 11) is unverified.
- **The D3D11 debug-layer-missing fallback path (`DX-21`) is unexercised.** This dev machine's
  Wine+DXVK setup always satisfies `D3D11_CREATE_DEVICE_DEBUG`, so the
  `DXGI_ERROR_SDK_COMPONENT_MISSING` → retry-without-debug-layer branch has never actually run.
- **The 5 combo `Clear*` variants** (`ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/
  `ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil`) are implemented
  (real `ClearDepthStencilView` calls with the right flag combinations) but not yet exercised by a
  dedicated pixel test — only plain `Clear(r,g,b,a)` has a dedicated round-trip check (`DX-25`).
- **Specular highlights are not pixel-verified.** `lit_textured3d`'s lit-branch pixel test
  deliberately zeroes specular for determinism (a CPU-side, non-GPU-replicated Blinn-Phong
  comparison would otherwise need floating-point-tolerant matching); the specular term itself is
  implemented in the HLSL but has no dedicated discriminating pixel test yet.
- **`SkinnedEffect`/`EnvironmentMapEffect`'s `DirectionalLight1`/`DirectionalLight2`/
  `EmissiveColor`** are wired through the same `D3DLightingConstants` buffer `lit_textured3d` uses,
  but have no variant-specific dedicated pixel test distinguishing multi-light contributions from a
  single-light case.
- **Mip-chain generation/sampling and per-instance `DepthStencilFormat` fidelity** are not
  separately pixel-tested (texture upload/readback is tested at mip level 0 only).
- **`Model`/`SpriteFont`** have not been separately exercised against this renderer this session —
  they build on already-tested `Texture2D`/`SpriteBatch`/`VertexBuffer` primitives, but have no
  D3D11-specific test coverage yet.
- **`cna_reference_dump`'s `undefined reference to Effect::Apply()` link failure is fixed** (found
  during `plans/plan_dx.md` `DX-81`'s coverage audit; root cause was a genuine, honest circular
  dependency — `D3D11SpriteBatch.cpp` (in `cna_renderer_directx11`) calls back into
  `Effect::Apply()` (defined in `CNA` itself), and MinGW's single-pass archive resolution never
  revisited `libCNA.a` once `libcna_renderer_directx11.a` created the need. Fixed by declaring
  the cycle explicitly in `CMakeLists.txt` (`target_link_libraries(${RENDERER_TARGET} PRIVATE CNA)`
  for `D3D11`/`D3D12`) — CMake's documented static-library-cycle support then repeats the archives
  on the final link line automatically. `cna_demo_2d`'s separate `SDL3/SDL.h`-not-found compile
  failure (found while verifying the fix above; `Game1.cpp` called raw SDL directly for
  minimize/restore/resize with no XNA equivalent, and never linked `SDL3::SDL3` itself — native
  Linux builds never noticed since a system-wide SDL3 install covered it there) **is also fixed**,
  at the root rather than by adding a link dependency: two new `GameWindow` CNAEXT methods,
  `MinimizeEXT()`/`RestoreEXT()` (mirroring the existing `IsBorderlessEXT` pattern), plus switching
  the resize call to the existing XNA `EndScreenDeviceChange()` API — `Game1.cpp` no longer includes
  `<SDL3/SDL.h>` at all. Verified via real `cna_reference_dump.exe`/`cna_demo_2d.exe` links under
  both D3D11 and D3D12, the full `D3D11`/`D3D12` CTest suites, and the EasyGL `GameWindowTest.*`
  suite (14/14, including 3 new cases) — no regression anywhere.
- **Direct3D 12 does not exist as a renderer.** `plans/plan_dx.md` Phase DX12 is written up in full but
  requires its own separate authorization (design decision 9) — `CNA_GRAPHICS_RENDERER=D3D12` is not
  a recognized CMake value.

See `plans/plan_dx.md` for the full task-by-task status (`DX-1` through `DX-98`) and design rationale, and
`docs/graphics-renderer-feature-matrix.md` for a row-by-row comparison against the other established
renderers.
