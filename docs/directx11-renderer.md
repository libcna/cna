# Direct3D 11 graphics renderer

## Status

The D3D11 renderer is a **native Windows Direct3D 11 graphics renderer**, verified 2026-07-14 on this
Debian dev machine via Windows cross-compilation + Wine+DXVK (see "Development environment" below —
real Windows hardware verification is a separate, still-open gate, see "Known limitations"). Select
it with:

```bash
cmake -S . -B cmake-build-d3d11 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/mingw-w64.cmake" \
  -DCNA_GRAPHICS_RENDERER=DIRECTX11 \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_SHARP_RUNTIME_ROOT="$PWD/../sharp-runtimenext"
cmake --build cmake-build-d3d11 -j
```

Add `-DCNA_DIRECTX11_COMPILED_EFFECTS=ON` to execute compiled XNA Effect Framework bytecode through
MojoShader's D3D11 adapter. The option is off by default so the ordinary renderer keeps its
dependency-free build.

Three corrections to that command, all found by running it (`plans/plan_dx.md` `DX-249`):

* the renderer identity is **`DIRECTX11`**, not `D3D11` — `D3D11` is not one of the 49 accepted
  values and fails configuration;
* the toolchain file must be an **absolute** path. The vendored-SDL prebuild runs its own
  sub-configure from a different working directory, and a relative toolchain path fails there with
  `Could not find toolchain file`, several minutes into the configure;
* `CNA_SHARP_RUNTIME_ROOT` must name a sharp-runtime checkout that has the required module set. The
  verified DirectX build uses the sibling `sharp-runtimenext`; the older `sharp-runtime` sibling is
  not interchangeable with it for these measurements.

`D3D11` is hard-gated to `CMAKE_SYSTEM_NAME=Windows` at configure time — attempting it on a native
Linux/macOS configure fails fast with `FATAL_ERROR`, pointing at the MinGW-w64 toolchain file above.
The default build fetches no extra dependency: `d3d11`/`dxgi`/`d3dcompiler` are all provided by the
Windows SDK (MSVC) or MinGW-w64's own headers/import libraries (this dev machine's actual path — see
`plans/plan_dx.md` `DX-1`). The compiled-effect opt-in additionally builds the MojoShader revision
pinned by FNA3D.

## What this renderer is for (and isn't)

XNA 4.0 itself was a thin wrapper over Direct3D 9, and modern Windows XNA/FNA-style games run
closest to their original execution environment on a real Direct3D renderer, not through OpenGL/
Vulkan/bgfx translation layers. `D3D11` is CNA's first **native** Direct3D renderer (as opposed to
`BGFX`, which can already select a D3D11/D3D12 renderer *internally* on Windows, but that's bgfx's
own abstraction, not CNA's) — it gives this project a dependency-free Windows path and direct
control over the exact Direct3D calls made, matching `CLAUDE.md`'s "preserve XNA-style APIs...
using modern C++23 internals" mandate more directly than routing through a third abstraction layer.

**What it proves**: a real `ID3D11Device`/`ID3D11DeviceContext` executing CNA's XNA-shaped
`IGraphicsRenderer` contract. Buffers, all core-XNA texture formats, render targets including
MSAA/MRT, state objects, SpriteBatch/SpriteFont, all stock effects, models/content, runtime HLSL
`ShaderEffect`, queries, presentation and deterministic device recovery are exercised through
public `GraphicsDevice` fixtures with GPU readback. With the compiled-effect option enabled, the
same public path also executes XNA/FNA `.fxb` reflection, passes, primitive/instanced/multi-stream
draws, SpriteBatch, sampler state, and 2D/cube/volume sampling. Renderer-internal cache/device
invariants remain in the deliberately small smoke binary rather than standing in for public
behavior.

**Measured state, 2026-09-09 (`plans/plan_dx.md` Phase DX17 and `plans/plan_fx.md` `FX-063`).** With
`CNA_DIRECTX11_COMPILED_EFFECTS=ON`, `ctest -L DIRECTX11` passes **267/267**, with no CTest skips,
the 18-test compiled-effect suite passes without a GoogleTest skip, and `DirectX11_Smoke` passes
**21/21** retained internal checks.
The shared registration inventory contains **265 declarations: 262 renderer-neutral fixtures, two
reasoned D3D11-native exceptions and one D3D12-native exception**. The D3D11 label adds its smoke and
DXVK gate to the applicable inventory. This result used the fixed `sharp-runtimenext` checkout,
Wine+DXVK 2.6 and private virtual Xwayland `:4`; no physical display was used.

Two XNA-specific conversions are measured rather than guessed. XNA's stock 3D path follows D3D9
integer pixel centers while SpriteBatch follows half-integer centers (`DX-253`), so D3DCommon
applies the viewport-derived correction only to stock/instanced 3D matrices. XNA `DepthBias` is a
normalized depth offset (`DX-256`), converted to the active D16/D24 native integer units. Both
contracts pass on D3D11, D3D12 and EasyGL. `DX-244` additionally proves 16 consecutive loss/restore
cycles preserve the same public texture, vertex/index buffers, render target and loaded model.

**What it is not yet**: verified against a genuine device-removal event or a vendor Windows driver.
Wine proves the renderer logic, not native-driver parity. Real present/tearing behavior, WARP,
MSVC execution and the native removal trigger remain in `DX-90`/`DX-91`; the deterministic recovery
path itself is implemented and tested.

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

CTest wires this in automatically — `ctest --test-dir cmake-build-d3d11 -L DIRECTX11` runs the
complete D3D11 label through the appropriate DXVK wrapper.

## Writing a D3D11 test

Renderer-neutral behavior belongs in a public `Game`/`GraphicsDevice` fixture and is declared once
with `cna_d3d_parity_fixture()` in `cmake/DirectXParityTests.cmake`; it then runs on D3D11 and D3D12.
A one-renderer exception requires a human-readable reason. Use a native D3D11 executable only for
an invariant that cannot be observed through the public API, such as input-layout or state-object
cache identity. `DirectX11_Smoke` is intentionally limited to those internals and
`DirectX11_Common` owns pure mapping-table checks. A native pixel diagnostic should follow this
shape:

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

## Known limitations (2026-09-09)

- **Native Windows remains a separate gate.** `DX-90` covers MSVC execution, real DXGI
  present/tearing, WARP and a genuine device-removal trigger; `DX-91` is optional multi-vendor
  coverage. The device/resource reconstruction behind that trigger is no longer missing:
  `DX-244` executes it deterministically for 16 cycles, including events and long-lived resources.
- **The D3D11 debug-layer-missing fallback path (`DX-21`) is unexercised.** This dev machine's
  Wine+DXVK setup always satisfies `D3D11_CREATE_DEVICE_DEBUG`, so the
  `DXGI_ERROR_SDK_COMPONENT_MISSING` → retry-without-debug-layer branch has never actually run.
- **Compiled XNA `Effect` bytecode is an opt-in.** With
  `CNA_DIRECTX11_COMPILED_EFFECTS=ON`, `SupportsCompiledEffects()` is true and the complete
  `FX-063` public-path suite passes. With it off, the renderer keeps the explicit unsupported
  capability/refusal and does not build MojoShader. D3D12's separate implementation owner is
  `plans/plan_fx.md` `FX-134`. Runtime-source `ShaderEffect` and every stock effect remain separate
  working paths.
- **`SetDataOptions` is implemented and differentially proven.** D3D11 maps `Discard` and
  `NoOverwrite` to the matching D3D11 map modes. The renderer-neutral 62-frame dynamic-buffer
  oracle passes all 187 checks on D3D11 and EasyGL and distinguishes all three options without
  state leakage; `plans/plan_dx.md` `DX-238` records the cross-renderer evidence.

See `plans/plan_dx.md` for the authoritative task-by-task status and design rationale, and
`docs/graphics-renderer-feature-matrix.md` for a row-by-row comparison against the other established
renderers.
