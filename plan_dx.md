# Direct3D 11 / Direct3D 12 Graphics Backends — Implementation Plan

> **Status: Phase DX1 + Phase DX2 + Phase DX4's core + `DX-80` all closed 2026-07-13**, authorized
> and completed the same day. `D3D11GraphicsBackend` is real code now, not just a plan:
> `include/CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.hpp`/`.cpp` implement every
> `IGraphicsBackend` pure virtual (real device/swap-chain/back-buffer/clear/present/readback; honest
> "not yet implemented" throws for buffers/textures/draws/SpriteBatch, each naming its own future
> phase). **Headline proof, not a simulated/assumed result**: `ctest -R D3D11` → `D3D11_Smoke`
> passes 3/3 checks running through DXVK 2.6.0 on a real GPU (`AMD Radeon 780M`, RADV driver) —
> feature level negotiated to `11_1`, tearing capable, debug layer enabled, and — the actual point —
> `Clear()`+`GetBackBufferData()` round-trips the *exact* clear color for two different colors/
> regions in the same run. `CNA_GRAPHICS_BACKEND=D3D11` is a real, working CMake option
> (D3D11-only — no `D3D12` scaffolding, per design decision 9). Along the way, building `CNA`/a real
> test executable under this cross-target surfaced and fixed 4 genuine pre-existing MinGW-portability
> bugs unrelated to D3D11 itself (2 in sibling `sharp-runtime`, 2 in `cna_graphics` — see `DX-15`'s
> own row for specifics); the full `CnaTests` suite still does not build under MinGW (a much larger,
> deliberately out-of-scope `::setenv`-portability gap across ~10 test files — also `DX-15`).
> Untouched/known gaps, honestly recorded row-by-row rather than silently claimed: the 5 combo
> `Clear*` variants, window resize (`DX-29`), device-lost recovery (`DX-27`'s detection code exists
> but was never triggered), and the debug-layer-missing fallback path (`DX-21`) are all real,
> implemented-but-unexercised gaps. See `programs.md` §9 for the DXVK install commands this all
> built on.
>
> **Phase DX3's mapping tables also closed 2026-07-13** (`DX-11-fmt`/`DX-12-state`/`DX-16-vtx`):
> `D3DCommon` is a real shared static library now (`cna_backend_graphics_d3dcommon`), with the full
> `SurfaceFormat`/`DepthFormat`→`DXGI_FORMAT` table, the `Blend`/`BlendFunction`/`CompareFunction`/
> `CullMode`/`FillMode`/`TextureAddressMode`/`TextureFilter`→`D3D11_*` table (its own
> "D3D11/D3D12 values are identical" claim independently verified against both real SDK headers on
> this machine, not assumed), and the stride-keyed `D3D11_INPUT_ELEMENT_DESC` vertex-layout helper.
> A new `D3D11_Common` CTest (23/23 checks, pure-function, no GPU needed) verifies all of it,
> mutation-tested for real discriminating power. **`DX-13-hlsl`/`DX-14-compile`/`DX-15-embed` (the
> actual HLSL shader porting + offline compile pipeline) are NOT started** — genuinely substantial
> shader-math work, deliberately not attempted opportunistically alongside the smaller mapping
> tables. That's the concrete next step for this plan. **Phase DX5 onward (vertex/index buffers,
> textures, Phase DX8's stock effects, SpriteBatch) is NOT yet authorized** — get an explicit
> go-ahead before continuing past where this session's authorization actually reached.
> **Direct3D 11 is the actual near-term target; Direct3D 12 is written up in full but deliberately
> deferred** (Phase DX12) — see "Why D3D11 first, D3D12 later" below.
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.

---

## Why these backends, and why D3D11 first

XNA 4.0 itself was a thin, XNA-flavoured wrapper over Direct3D 9 (Windows) / Direct3D 9 (Xbox 360),
and FNA (this project's own authoritative reference, `/rv/data/library/github.com/FNA-XNA/FNA`)
runs on top of a modern Direct3D on Windows via MojoShader/FAudio-era tooling in its own history.
A native Direct3D backend is therefore not "one more backend for coverage's sake" — it is the
closest thing CNA can have to an XNA-authentic Windows reference implementation, useful for:

- Cross-checking CNA's own rendering behavior against something closer to XNA's original Windows
  execution environment than EasyGL/Vulkan/Bgfx/WebGPU/SDL_Renderer.
- A dependency-free Windows path: CNA already reaches D3D11/D3D12 *indirectly* today, since `BGFX`
  can select a D3D11 or D3D12 renderer internally on Windows — but that's bgfx's own abstraction,
  not CNA's. A native backend removes the bgfx dependency for Windows users who want it, and gives
  this project full control over the exact Direct3D calls being made (matches `CLAUDE.md`'s own
  "preserve XNA-style APIs... using modern C++23 internals" mandate more directly than routing
  through a third abstraction layer).
- Broad hardware reach: even old/integrated Windows GPUs have mature, stable D3D11 drivers.

**D3D11 is recommended first, D3D12 explicitly later, for concrete reasons — not just "easier":**

- D3D11 keeps an XNA-shaped immediate-context/device model (`ID3D11Device`/`ID3D11DeviceContext`)
  close to how `IGraphicsBackend` already works for every existing backend — no command
  lists/queues, no descriptor heaps, no manual barriers, no explicit frame-in-flight
  synchronization, no device-lost recovery machinery to design from scratch.
  D3D12 needs all of the above (command queues, descriptor heaps, resource barriers, pipeline
  state objects, multi-frame-in-flight synchronization, device-removed recovery) — real,
  substantial engineering effort with limited additional value *right now*, since `VULKAN` already
  gives this project an explicit, modern, low-level GPU backend, and `BGFX` can already select
  D3D12 internally on Windows if someone specifically wants it.
- D3D12 only earns its own native backend if/when the project wants: maximum Windows performance,
  DXR ray tracing, fine-grained modern-D3D feature access, a direct Vulkan-vs-D3D12 comparison
  point, or independence from bgfx for advanced Windows rendering specifically. None of those are
  active project goals today — flagged here as the real trigger condition for Phase DX12, not
  assumed to already apply.

## Development environment: Debian, not Windows, for ~90% of the work

This project has **already proven** that a Direct3D-adjacent backend can be developed on Linux and
verified on real Windows only at the end — see `README.md`'s own "Tested Compilers" table:
`SDL_RENDERER` is "✅ verified building + full test suite under Wine" when cross-compiled with the
existing `cmake/toolchains/mingw-w64.cmake` toolchain file. D3D11/D3D12 extend that exact, already-
working pattern one step further:

```text
Debian 13 (this repo's actual dev machine)
│
├── Linux native builds (existing, unaffected)
│   ├── EASYGL / VULKAN / BGFX / WEBGPU / SDL_RENDERER / HEADLESS / SOFTWARE
│
└── Windows cross-build (cmake/toolchains/mingw-w64.cmake, already exists)
    └── D3D11 now / D3D12 only after future authorization
         ├── compile: MinGW-w64 (x86_64-w64-mingw32-{gcc,g++})
         ├── local dev-loop test: Wine + DXVK (D3D11→Vulkan) / Wine + vkd3d-proton (D3D12→Vulkan)
         └── final verification: Windows CI runner or a real/VM Windows machine
```

DXVK translates D3D11 calls to Vulkan and runs under Wine, which means the *backend's own logic*
(device/swap-chain setup, resource lifecycle, draw calls, state translation, shader execution,
pixel-readback correctness) can be developed and pixel-tested on this Debian machine, in the same
CLion/CMake workflow already used for every other backend — without ever touching a Windows
machine for the bulk of the work. D3D12's equivalent path (Wine + vkd3d-proton) is **less mature
and not yet proven in this project** — treated as a real risk to validate early in Phase DX12, not
assumed to work by analogy with D3D11's DXVK path.

What Wine+DXVK **cannot** prove, and must be verified on real Windows/Windows CI before either
backend is called done (mirrors this project's existing "Wine proves the logic, not real-hardware
parity" discipline already documented for `SDL_RENDERER`):

- Real DXGI swap-chain behavior (present modes, tearing flags, fullscreen transitions).
- Device-lost/device-removed handling on a real driver.
- Actual Intel/AMD/NVIDIA driver quirks and the D3D11 debug layer's real validation warnings.
- WARP software-rasterizer fallback behavior.
- MSVC-vs-MinGW ABI/toolchain differences (this project already builds with MSVC 2022, clang-cl,
  and MinGW-w64 for `SDL_RENDERER` — see `README.md`; D3D11/D3D12 should eventually match on at
  least MinGW-w64 + MSVC).

---

## Design decisions (recorded before implementation, not left implicit)

1. **Two separate compile-time backends, not one dial.** `CNA_GRAPHICS_BACKEND=D3D11` and
   `CNA_GRAPHICS_BACKEND=D3D12` are two distinct values, each producing its own static library
   target (`cna_backend_graphics_d3d11` / `cna_backend_graphics_d3d12`), exactly matching the
   existing `SDL_RENDERER`/`EASYGL`/`BGFX`/`VULKAN`/`WEBGPU`/`HEADLESS`/`SOFTWARE` pattern in
   `CMakeLists.txt` (`CNA_GRAPHICS_BACKEND` cache variable + one `elseif()` block per backend).
   This is **not** a `HEADLESS`-style single-binary runtime mode dial — D3D11 and D3D12 are
   different APIs with different device/resource/command models; there is no sane single binary
   that "is" both.
2. **Windows-only, hard-gated at configure time.** Unlike `BGFX`'s existing platform check (a
   `message(WARNING ...)` for "primarily tested on Linux" — bgfx *can* still attempt other
   platforms), `D3D11`/`D3D12` genuinely cannot build anywhere but Windows (native or MinGW/MSVC
   cross-compile) — `d3d11.h`/`d3d12.h`/`dxgi.h` do not exist elsewhere. CMake must reject the
   combination with `message(FATAL_ERROR ...)` when `CNA_GRAPHICS_BACKEND` is `D3D11`/`D3D12` and
   `CMAKE_SYSTEM_NAME` is not `Windows`, with an error message that tells the user to either target
   Windows directly or cross-compile via `cmake/toolchains/mingw-w64.cmake` (which already exists
   and is already proven for `SDL_RENDERER`).
3. **No external package dependency beyond the OS/toolchain-provided Direct3D libraries — and only
   the libraries actually needed at link time, not a preemptive blanket set.** Unlike `VULKAN`
   (`find_package(Vulkan REQUIRED)`) or `BGFX` (`FetchContent` of `bgfx.cmake`), D3D11/D3D12 need no
   extra fetched dependency — every import library either backend could conceivably need (`d3d11`,
   `dxgi`, `dxguid`, `d3dcompiler`, plus `d3d12` for D3D12) is provided by both MSVC's Windows SDK
   and MinGW-w64's own packaged headers/libs; `target_link_libraries()` names them directly, no
   `find_package`/`FetchContent` step needed. **But do not link all of them into the main runtime
   backend target by default** — `DX-1` should empirically confirm the actual minimum linkable set
   before `DX-12` writes it into CMake, not assume the full list:
   - `dxguid` mainly provides out-of-line storage for certain GUID symbols referenced as external
     objects in older-style code; with modern headers and `__uuidof(...)` it is frequently
     unnecessary. Do not link it into `cna_backend_graphics_d3d11` preemptively — `DX-1` determines,
     on this project's actual toolchain, whether anything it uses genuinely needs it.
   - `d3dcompiler` is **not** a runtime dependency of the main backend at all, matching this plan's
     own "offline-compiled bytecode, not runtime `D3DCompile()`" stance (design decision 5) — linking
     it into `cna_backend_graphics_d3d11` by default would silently reintroduce the exact
     `d3dcompiler_47.dll` runtime dependency design decision 5 explicitly avoids. It belongs only on
     whatever target actually calls `D3DCompile()` — the offline shader-compile tool
     (`DX-14-compile`) and/or the later runtime custom-`ShaderEffect` path (`DX-58`) — never on
     `cna_backend_graphics_d3d11`/`cna_backend_graphics_d3d12` themselves.
   - Likely real minimum for the main backend target: `d3d11`, `dxgi` (D3D11); `d3d12`, `dxgi`
     (D3D12) — confirm this empirically in `DX-1`/`DX-100` rather than treating it as decided here.
   (MinGW-w64's D3D11 header completeness should still be spot-checked early — see `DX-1` — since
   some newer/rarely-used interfaces are occasionally thinner than the real Windows SDK's.)
4. **A genuinely shared `D3DCommon` core, scoped to what is actually common — not a shared device
   or backend base class.** D3D11 (`ID3D11Device`/immediate `ID3D11DeviceContext`) and D3D12
   (command queues/lists, descriptor heaps, explicit barriers, pipeline state objects, fences) have
   fundamentally different resource and command-submission models, so there is **no** shared
   `D3DGraphicsDevice` base class and no attempt to unify their draw-call paths. What genuinely is
   shared, and lives in `include/CNA/Internal/Backends/D3DCommon/` +
   `src/CNA/Internal/Backends/D3DCommon/` (a small static library, `cna_backend_graphics_d3dcommon`,
   linked by both `cna_backend_graphics_d3d11` and `cna_backend_graphics_d3d12` — one layer more
   specific than the existing backend-agnostic `cna_backend_graphics_common` INTERFACE library):
   - `DXGI_FORMAT` mapping tables for XNA `SurfaceFormat`/`DepthFormat` (`DX-11`).
   - XNA state-enum → `D3D*_BLEND`/`D3D*_COMPARISON_FUNC`/`D3D*_CULL_MODE`/`D3D*_FILL_MODE`/
     `D3D*_TEXTURE_ADDRESS_MODE`/`D3D*_FILTER` mapping tables (`DX-12`). Historically the D3D11 and
     D3D12 enum values for most of these are numerically identical (D3D12 reused D3D11's constants
     for many state enums) — **verify this directly against the actual SDK headers before relying
     on it** (`DX-12`'s own acceptance criterion), do not assume by reputation.
   - HLSL shader **sources** (one `.hlsl` file per stock-effect variant) plus the offline-compile
     tooling that turns them into embeddable bytecode (`DX-13`–`DX-15`) — this directly mirrors the
     project's own already-proven Vulkan precedent (`src/CNA/Internal/Backends/Vulkan/shaders/
     compile_shaders.py` → `spirv_shaders.hpp`, GLSL→SPIR-V, checked-in generated header, no
     runtime shader compiler dependency). D3D12 genuinely can consume Shader Model 5 DXBC bytecode
     compiled from the same HLSL source and the same offline compiler (`fxc`/`D3DCompile`) as D3D11
     — so **one shared HLSL source tree and one shared compile step can bootstrap both backends**,
     stated carefully: this is exactly right and sufficient for D3D11, but for D3D12 it is a
     **compatible starting point, not a promise of the final shader system** — DXIL/`dxc`, newer
     shader models, a modern root-signature-driven binding workflow, and (eventually) ray-tracing
     shaders are all legitimate future D3D12-specific upgrades this plan should not be read as
     ruling out. See Phase DX12's own notes (`DX-107`/`DX-111`).
   - The stride-keyed vertex-format-inference convention this project already uses on
     WebGPU/Software (`DX-16`) — reused, not reinvented.
5. **HLSL shader strategy: offline-compiled bytecode, not runtime `D3DCompile`.** Mirrors the
   ChatGPT-conversation research this plan is based on (see the project owner's own notes) and this
   project's existing Vulkan precedent exactly: `.hlsl` sources compiled to `.cso`-equivalent DXBC
   bytecode ahead of time (CI or a manual `compile_shaders_hlsl.py` step, run on a real Windows
   machine or via `fxc`/`d3dcompiler_47.dll` under Wine if that proves reliable — `DX-14` decides
   which), embedded as a checked-in generated C++ header
   (`src/CNA/Internal/Backends/D3DCommon/shaders/hlsl_shaders.hpp`, mirroring `spirv_shaders.hpp`
   byte-for-byte in spirit). Runtime `D3DCompile()` is explicitly **not** the v1 path — it adds a
   `d3dcompiler_47.dll` runtime dependency and, per the project owner's own research notes, has
   known extra friction under Wine/DXVK specifically. A runtime-compile path for hand-authored
   custom `ShaderEffect` HLSL sources (mirroring `IEffectBackend::CompileProgram()`'s contract) is a
   separate, later, explicitly optional task (`DX-58`) — do not conflate it with the stock-effect
   shader set.
6. **Full effect parity is the actual target for D3D11, not a reduced v1 subset.** Unlike
   `SOFTWARE`'s deliberately narrow first version (no lighting/fog in v1, see `plan_software.md`
   design decision 6), D3D11 is meant to be a faithful, full-fidelity Windows reference backend —
   its `GpuDrawParams` consumption should aim at the same feature depth EasyGL/Vulkan/Bgfx already
   have (per-light diffuse+specular lighting, fog, `AlphaTestEffect`/`DualTextureEffect`/
   `EnvironmentMapEffect`/`SkinnedEffect`, not just `BasicEffect`'s unlit subset). The 10 existing
   Vulkan GLSL shader pairs (`colored3d`, `textured3d`, `colored_textured3d`, `lit_textured3d`,
   `alpha_test3d`, `dual_texture3d`, `env_map3d`, `skinned3d`, `sprite2d`, `instanced3d` — see
   `src/CNA/Internal/Backends/Vulkan/shaders/`) are the direct 1:1 HLSL port target list, not a
   reference to reinvent from scratch. It is still fine to land these incrementally (Phase DX8 is
   ordered cheapest/most-foundational first), just not to declare the backend "done" at a
   `BasicEffect`-only subset the way Software's v1 legitimately did.
7. **Native window handle via SDL3's Win32 property, not a new windowing abstraction.** SDL3
   already exposes the real `HWND` through
   `SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER,
   NULL)` (confirmed present in the vendored `third_party/SDL/include/SDL3/SDL_video.h`) — this is
   the DXGI swap chain's `CreateSwapChainForHwnd` target. No new SDL subsystem or windowing code is
   needed; this is a same-shape lookup to what `VulkanGraphicsBackend` already does for
   `SDL_Vulkan_CreateSurface`.
8. **`D3D11`/`D3D12` follow the existing `#ifdef CNA_BACKEND_HEADLESS`-style per-backend guard
   convention wherever `GraphicsDevice.cpp` branches by backend** — e.g. the existing
   `#ifdef CNA_BACKEND_EASYGL`/`CNA_BACKEND_VULKAN` sites. Since these two backends *do* need a
   real window (unlike `HEADLESS`/`SOFTWARE`), most such sites need no new branch at all — call this
   out explicitly per site rather than assuming.
9. **D3D12 tasks in this plan (Phase DX12) are written in full but explicitly NOT authorized —
   including its CMake scaffolding, not just its implementation.** Same convention as
   `plan_software.md`'s Phase S9: a concrete, scoped-out task list the project owner can approve
   later, one task or one batch at a time — not a vague "someday" note. Do not start Phase DX12
   without an explicit go-ahead, even after Phase DX1–DX11 (D3D11) is fully done. This is deliberately
   stricter than it might need to be: Phase DX2 does **not** pre-add `"D3D12"` to
   `CNA_GRAPHICS_BACKEND`'s `STRINGS`/option flags "since it's cheap" — `-DCNA_GRAPHICS_BACKEND=D3D12`
   should not even be a recognized value until `DX-101` (Phase DX12's own first task) explicitly adds
   it, so there is no inert-but-present D3D12 scaffolding sitting in the tree ahead of authorization.
10. **COM object lifetime: one decided convention before Phase DX2 lands, not per-file
    improvisation.** `Microsoft::WRL::ComPtr<T>` is the obvious default on real Windows/MSVC, but
    its availability and ergonomics under MinGW-w64 — this project's actual *primary* Debian
    dev-loop compiler (Phase DX1) — are not something to assume without checking (`DX-6`). Options:
    `Microsoft::WRL::ComPtr` (Windows-only, MinGW support varies by version/headers actually
    present); `wil::com_ptr` (an extra vendored dependency); a small project-local `CNA::ComPtr<T>`
    (mirrors this project's own "add a minimal stub rather than a new dependency" philosophy, see
    `CLAUDE.md`'s SharpRuntime-extension rule); or raw `IUnknown::Release()` wrapped in whatever
    RAII pattern this codebase already uses elsewhere. **Resolved by `DX-6` (2026-07-13):
    `Microsoft::WRL::ComPtr<T>`** — `wrl/client.h` is present in this MinGW-w64 install and a real
    spike (device creation, `.As()`, `GetParent`, and specifically the `ReleaseAndGetAddressOf()`
    re-population pattern below) compiled, linked against just `d3d11`/`dxgi`, and ran correctly
    end-to-end under Wine on this machine — no project-local type needed. Whichever is chosen,
    **every** `ID3D11*`/`IDXGI*`/`ID3D12*` interface pointer
    anywhere in `D3D11GraphicsBackend`/`D3D12GraphicsBackend`/`D3DCommon` uses it — no bare
    `Release()` call sites. Without this, leaks on resize, double-releases, and forgotten releases
    on a partially-failed initialization are the expected failure mode for a hand-rolled D3D
    backend, not a hypothetical risk. See `DX-6`'s own acceptance checklist for the exact method
    surface required if the project-local option is chosen (move semantics, `GetAddressOf()` vs.
    `ReleaseAndGetAddressOf()`, `Attach`/`Detach`, a `QueryInterface` helper) — each maps to a
    specific, real bug class in hand-rolled COM wrappers, not an optional nice-to-have.
11. **Three independent resource-lifetime groups, not two — the swap chain object itself is its own
    group, separate from both the device and the window-size-dependent views.** A plain resize must
    touch only the narrowest group that actually needs it:
    - **Device lifetime** (`DX-20`/`DX-21`/`DX-22`): `ID3D11Device`, `ID3D11DeviceContext`, the
      `IDXGIFactory2` chain, and `allowTearingSupported_` (the OS/driver *capability*, design
      decision 13) — created once, torn down only on device-removed recovery (`DX-27`).
    - **Swap-chain lifetime** (`DX-23`): the `IDXGISwapChain1` object itself — created once at
      startup (and recreated on device-removed recovery), but a plain resize **reuses the same
      object** via `IDXGISwapChain1::ResizeBuffers(...)`; it does not call
      `CreateSwapChainForHwnd` again.
    - **Window-size lifetime** (`DX-24`): back-buffer `ID3D11RenderTargetView`, depth-stencil
      texture/`ID3D11DepthStencilView`, viewport — unbound and released before `ResizeBuffers`,
      recreated after it, on every resize (`DX-29`) *and* on device-removed recovery.
    Device-removed recovery (`DX-27`/`DX-90`) tears down and recreates all three groups; a plain
    resize (`DX-29`) touches only the third group, plus a `ResizeBuffers` call on the second — it
    must never re-run the first group's factory/tearing-capability query, and it must never destroy
    and recreate the `IDXGISwapChain1` object itself. A flat, undivided "create everything" function
    (or a two-group split that conflates the swap-chain object with its window-size-dependent
    *views*, an earlier draft of this plan's own mistake) is exactly what makes resize and
    device-loss recovery fragile in a hand-rolled D3D backend.
12. **D3D11 device creation degrades gracefully on debug-layer/feature-level *negotiation* — but the
    accepted minimum feature level is a hard, explicit policy, not silently whatever came back.** A
    build that only ever ran under Wine+DXVK (which does not require the real D3D11 SDK debug layer
    to be installed) must not implicitly assume every real Windows machine has it — `DX-21` retries
    device creation without `D3D11_CREATE_DEVICE_DEBUG` on `DXGI_ERROR_SDK_COMPONENT_MISSING` and
    logs `"D3D11 debug layer unavailable; retrying without it."` rather than failing outright.
    Likewise `DX-20` *requests* feature levels as a fallback list (`11_1`→`11_0`→`10_1`→`10_0`, with
    a retry-without-11_1 path for drivers that reject an explicit 11.1 request with `E_INVALIDARG`)
    rather than a single hardcoded `D3D_FEATURE_LEVEL_11_0` request. **But acceptance is not the
    same as negotiation**: Phase DX8's stock shader set is compiled as Shader Model 5
    (`vs_5_0`/`ps_5_0`, design decision 5), which requires feature level 11.0+ — generating
    `vs_4_0`/`ps_4_0` fallback variants to support a 10.x device is explicitly out of scope, not
    worth the added shader-variant complexity for this project. So: `DX-20` negotiates broadly, but
    construction must **reject** (a clear, specific diagnostic error — e.g. "GPU reports feature
    level 10.1; CNA's D3D11 backend requires 11.0+") anything the negotiation returns below
    `D3D_FEATURE_LEVEL_11_0`, **at construction time**, not as a confusing, deferred
    `CreateVertexShader` failure once Phase DX8 first tries to use the device.
13. **Tearing is two separate booleans — an OS/driver *capability* and a CNA *policy* — never
    conflated into one flag.** `allowTearingSupported_` (from `DX-22`'s `IDXGIFactory5::
    CheckFeatureSupport` query) records only what the system can do; a second, independent
    `allowTearingRequested_` records whether CNA actually wants tearing-capable presentation for
    the current `PresentationParameters`/`GraphicsDeviceManager` configuration. Both the swap-chain
    creation flags (`DX-23`) and the per-`Present()` flag computation (`DX-26`) gate on the
    conjunction of both — a swap chain should not unconditionally become tearing-capable just
    because the hardware supports it, if CNA/the game never asked for that mode.
14. **Constant-buffer layout is a single, explicit, project-wide policy — not decided per shader.**
    Every GPU-side constant-buffer POD struct (`DX-60`/`DX-60a`) commits to one matrix-layout
    convention (either `row_major` declared consistently in every HLSL `cbuffer` plus a matching
    C++-side layout, or CPU-side transposition before upload — pick one, document it once in
    `D3DCommon`, and never mix the two across shader variants) and must satisfy
    `sizeof(StructName) % 16 == 0` (`static_assert`ed) — D3D11 requires a constant buffer's
    `ByteWidth` to be a multiple of 16 bytes, so this is a hard correctness requirement, not a
    style preference.

---

## Active execution order — do this one phase at a time

1. **Phase DX1** (Windows cross-build dev loop) unblocks everything else — nothing in this plan can
   be built, let alone tested, without a working MinGW-w64 + Wine + DXVK loop on this Debian
   machine. `cmake/toolchains/mingw-w64.cmake` already exists and is already proven for
   `SDL_RENDERER`; this phase is mostly about confirming it also covers D3D11's extra headers/libs
   and standing up the Wine/DXVK side, which is new.
2. **Phase DX2** (CMake integration + skeleton) depends only on Phase DX1's toolchain being
   confirmed working — it wires `D3D11` only into `CNA_GRAPHICS_BACKEND` exactly like every other
   backend (no `D3D12` scaffolding this early, see that phase's own intro and design decision 9),
   with a real (if minimal) `D3D11GraphicsBackend` that at least
   compiles and links against `CnaTests`.
3. **Phase DX3** (`D3DCommon` shared core) should land early, before the D3D11-specific phases that
   consume it (format/state mapping tables, shader compile pipeline, vertex-format helper) — but
   individual pieces can be built just-in-time per consuming task rather than all up front, mirroring
   how `plan_software.md` let Phase S2/S3 interleave.
4. **Phase DX4** (device, swap chain, back buffer) is the heart of the backend — get a real
   `Clear()`/`Present()`/`GetBackBufferData()` round trip pixel-verified (mirrors `SOFTWARE-10`
   through `SOFTWARE-14`'s own "prove the framebuffer is real" bar) before anything else.
5. **Phases DX5–DX9** (buffers, textures/render targets, state objects, shaders/effects,
   SpriteBatch) build on Phase DX4's device/swap-chain foundation and can interleave somewhat, but
   Phase DX8 (shaders/effects) needs Phase DX5/DX6/DX7 far enough along to actually issue a real
   draw call.
6. **Phase DX10** (tests) — per this project's own convention (`CLAUDE.md`), add test coverage in
   the same task that implements each capability, not bolted on afterward. This phase names the
   cross-cutting suites, not "when to start testing."
7. **Phase DX11** (docs) — write `docs/d3d11-backend.md` as capabilities land, not all at the end,
   mirroring `plan_software.md`'s own Phase S8 discipline.
8. **Phase DX12** (D3D12) — do not start without explicit authorization (design decision 9).

For every task: build the affected target(s) (Windows cross-build, since this backend cannot build
on Linux natively — see design decision 2), run the relevant tests under Wine+DXVK, and do not mark
a task ✅ without both. Tasks whose acceptance criteria genuinely require real Windows/Windows CI
(flagged individually) cannot be marked ✅ from Wine-only verification — mark 🟨 and say so plainly.

---

## Phase DX1 — Windows cross-build dev loop (shared prerequisite)

| # | Task | Status | Notes |
|---|---|---|---|
| DX-1 | Confirm `cmake/toolchains/mingw-w64.cmake` (already used for `SDL_RENDERER`, see `README.md`) resolves `<d3d11.h>`/`<dxgi.h>` (and `<d3dcompiler.h>` separately, for the shader-tooling target only — design decision 3) from the `mingw-w64` apt package on this machine, and **empirically determine the actual minimum link-library set** — do not assume `dxguid` is needed just because it's available | ✅ | **Closed 2026-07-13.** Confirmed via a real throwaway spike (`D3D11CreateDevice` with the `DX-20` feature-level fallback array, `IDXGIDevice`→`IDXGIAdapter`→`IDXGIFactory2`→`IDXGIFactory5` chain, `IDXGIFactory5::CheckFeatureSupport` tearing query, `DXGI_SWAP_CHAIN_DESC1` construction — i.e. real production code shapes, not a trivial include-only check). This machine already has `mingw-w64`/`g++-mingw-w64-x86-64` installed (per `README.md`'s existing `SDL_RENDERER` precedent) with `d3d11.h`/`dxgi.h`/`dxgi1_5.h`/`d3dcompiler.h` all present under `/usr/x86_64-w64-mingw32/include`, and `libd3d11.a`/`libdxgi.a`/`libdxguid.a`/`libd3dcompiler.a` all present under `/usr/x86_64-w64-mingw32/lib`. **Confirmed minimum link set: `d3d11` + `dxgi` only** — the spike (using `__uuidof`, `QueryInterface`, `GetParent`, `IID_PPV_ARGS`) compiled and linked cleanly (`x86_64-w64-mingw32-g++ -std=c++23 spike.cpp -o spike.exe -ld3d11 -ldxgi`) with **no `dxguid` needed at all** — confirms this plan's own suspicion (design decision 3) that modern MinGW-w64 headers resolve these GUIDs without the separate library. `d3dcompiler` was verified to link and work in complete isolation (a separate `D3DCompile()`-calling spike linked only `-ld3dcompiler`), confirming it's safe to keep off the main backend target per design decision 3. The resulting `.exe` was then run under Wine (this machine's real desktop `:0` session, no DXVK installed yet — vanilla `WineD3D`) and every call in the spike genuinely succeeded at runtime (`D3D11CreateDevice hr=0`, the full DXGI factory chain, and `CheckFeatureSupport` reporting `allowTearing=1`) — a stronger result than DX-1 strictly required, and a preview of `DX-4`'s own check once DXVK is installed. |
| DX-2 | Install and configure a dedicated Wine prefix + DXVK for D3D11 testing (`WINEPREFIX=~/.wine-cna-d3d11`, DXVK's `d3d11.dll`/`dxgi.dll` installed into it), following the project owner's own researched steps | ✅ | **Closed 2026-07-13** — the project owner installed `dxvk-wine64` (`sudo apt-get install -y dxvk-wine64`, which pulled in the `dxvk` meta-package and `dxvk-wine32:i386` too). The `dxvk` meta-package ships `dxvk-setup(1)`, a Debian-specific convenience tool this plan didn't originally know about — used instead of manually symlinking DLLs: `WINEPREFIX=~/.wine-cna-d3d11 wineboot --init` then `WINEPREFIX=~/.wine-cna-d3d11 dxvk-setup install`. Verified for real: `system32/d3d11.dll`/`dxgi.dll` are now symlinks straight to `/usr/lib/dxvk/wine64/{d3d11,dxgi}.dll.so`, and the registry's `HKCU\Software\Wine\DllOverrides` shows `d3d11`/`dxgi` = `native` (Debian's DXVK integration loads the `.so` directly as a "native" module rather than the typical Windows-release DXVK zip's override-to-builtin approach — a real, worth-documenting packaging difference). Full install commands recorded in `programs.md` §10, not just here. |
| DX-3 | `scripts/run-wine-dxvk.sh` wrapper (mirrors the existing `scripts/run-all-backend-smoke-tests.sh` convention): sets `WINEPREFIX`, optional `DXVK_HUD`/`DXVK_LOG_LEVEL`, execs `wine "$1"` (**not** `wine64` — see Notes) | ✅ | **Closed 2026-07-13.** `scripts/run-wine-dxvk.sh` written and verified: `CNA_D3D11_WINEPREFIX` (defaults to `~/.wine-cna-d3d11`), fails fast with a clear message if the prefix isn't initialized yet, honors caller-set `DXVK_LOG_PATH`/`DXVK_LOG_LEVEL`/`DXVK_HUD`, `exec wine "$@"`. Confirmed real environment difference: **there is no separate `wine64` command** on this Debian's Wine 10.0 packaging — only `wine`, which auto-detects PE32 vs. PE32+ (`wine64: command not found`, but `wine spike.exe` ran the PE32+ binary correctly) — the script and this plan's own wording were both corrected to `wine`. |
| DX-4 | Prove the loop end-to-end with a minimal non-CNA smoke program: create a bare `ID3D11Device`+swap chain, clear to a known color, run under `wine` with DXVK installed, confirm no crash/error in `DXVK_LOG_LEVEL=info` output **and confirm DXVK is actually the thing that ran** (see Notes) | ✅ | **Closed 2026-07-13, all 3 verification methods from this task's own Notes passed.** Ran `DX-1`'s own spike binary through `scripts/run-wine-dxvk.sh` (`DX-3`) against the now-configured `~/.wine-cna-d3d11` prefix: (1) DXVK log files genuinely created (`spike_d3d11.log`, `spike_dxgi.log` in `DXVK_LOG_PATH`); (2) log content unambiguously identifies real DXVK — `"DXVK: 2.6.0"`, `"Build: x86_64 gcc 14.0.0"`, and a **real GPU**, not a software fallback: `"AMD Radeon 780M (RADV PHOENIX)"` via the `radv 25.0.7` Vulkan driver (`llvmpipe` was explicitly skipped: `"warn: Skipping CPU adapter: llvmpipe"`); (3) feature-level negotiation genuinely worked end-to-end — `"Using feature level D3D_FEATURE_LEVEL_11_1"` (max supported `12_1`), comfortably clearing this plan's own `DX-20`/design-decision-12 minimum of `11_0`. Every application-level call in the spike (`D3D11CreateDevice`, `IDXGIDevice`→`IDXGIFactory5` chain, `CheckFeatureSupport` tearing query, which reported `allowTearing=1`) returned `hr=0`. This is a strictly stronger result than `DX-1`'s own earlier vanilla-Wine run of the same binary (which used `WineD3D`, not DXVK) — direct proof this environment's Wine+DXVK loop is real and load-bearing, not just "ran without crashing." |
| DX-5 | Document the CLion CMake-profile setup the project owner described (separate `Windows-D3D11-MinGW` profile, custom "run" step invoking `scripts/run-wine-dxvk.sh`) | ✅ | **Closed 2026-07-13** (documentation-only, no new CMake logic — Phase DX2 itself is still unauthorized, so there's no `D3D11` target to actually build yet; this records the profile shape for whoever sets it up once Phase DX2 lands). **CLion CMake profile** (Settings → Build, Execution, Deployment → CMake → `+`): name it `Windows-D3D11-MinGW`; leave the Toolchain as the default Linux one (the cross-compilation happens via the CMake toolchain file, not CLion's own toolchain selector); CMake options: `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake -DCNA_GRAPHICS_BACKEND=D3D11 -DCNA_BUILD_TESTS=ON`; build directory e.g. `cmake-build-d3d11-mingw`. **Run configuration**: CLion cannot directly execute a cross-compiled Windows `.exe`, so add a separate *Shell Script* run configuration (not a plain CMake Application one) pointing at `scripts/run-wine-dxvk.sh` (`DX-3`) with the built target's `.exe` path as its argument (e.g. `$CMakeCurrentBuildDir$/<target>.exe`) — this is exactly the wrapper `DX-3` built and `DX-4` proved works. |
| DX-6 | Decide the COM object lifetime convention (design decision 10) before any Phase DX2 code lands: spike whether `Microsoft::WRL::ComPtr<T>` builds cleanly against this project's actual MinGW-w64 toolchain (`cmake/toolchains/mingw-w64.cmake`); if not (or if it's awkward), implement a small project-local `CNA::ComPtr<T>` in `D3DCommon` instead | ✅ | **Closed 2026-07-13 — `Microsoft::WRL::ComPtr<T>` chosen; no project-local `CNA::ComPtr<T>` needed.** `wrl/client.h` is present in this MinGW-w64 install (`/usr/x86_64-w64-mingw32/include/wrl/client.h`). A real spike (`ComPtr<ID3D11Device>`/`ComPtr<ID3D11DeviceContext>` via `D3D11CreateDevice(..., device.GetAddressOf(), ..., context.GetAddressOf())`, `device.As(&dxgiDevice)`, `dxgiDevice->GetParent(IID_PPV_ARGS(&adapter))`, and — specifically exercising this task's own flagged leak pattern — re-creating the device into an *already-populated* `ComPtr` via `device.ReleaseAndGetAddressOf()`) compiled and linked cleanly against just `d3d11`/`dxgi` (no `dxguid`, consistent with `DX-1`'s finding), and **ran correctly end-to-end under Wine** on this machine: `D3D11CreateDevice hr=0`, `.As(IDXGIDevice) hr=0`, `GetParent(adapter) hr=0`, and the `ReleaseAndGetAddressOf()`-based re-creation also `hr=0` — confirming this exact leak-prone pattern behaves correctly through `WRL::ComPtr`. One real, generic (non-`WRL`-specific) build wrinkle found along the way: a plain dynamically-linked MinGW build needs `libgcc_s_seh-1.dll`/`libstdc++-6.dll` present at runtime (`STATUS_DLL_NOT_FOUND` under Wine otherwise) — **already solved** by this project's own existing `CMakeLists.txt` (`target_link_options(CnaTests PRIVATE -static-libgcc -static-libstdc++)` + `cna_copy_mingw_runtime(CnaTests)`), so no new build-system work is implied, just confirms the existing mechanism is the right one to keep relying on. `DX-6a`'s custom-`ComPtr` unit tests are therefore **not needed** — `WRL::ComPtr` is Microsoft's own, already-tested type. |
| DX-6a | ~~If a project-local `CNA::ComPtr<T>` is built (`DX-6`), add dedicated unit tests...~~ | ✅ | **Closed as not-applicable, 2026-07-13** — `DX-6` chose `Microsoft::WRL::ComPtr<T>`, which needs no project-local test suite (already tested upstream, exactly the skip condition this row's own original Notes anticipated). |

---

## Phase DX2 — CMake integration and skeleton

This phase is **D3D11-only** — no `D3D12` CMake wiring, option flag, or `STRINGS` entry lands here,
not even as inert scaffolding. Adding `D3D12` to `CNA_GRAPHICS_BACKEND` early would let a curious
`-DCNA_GRAPHICS_BACKEND=D3D12` configure "successfully" into a target with no real implementation
behind it, muddying design decision 9's "nothing D3D12 happens without explicit authorization" line.
All of D3D12's CMake plumbing — the `STRINGS`/option-flag addition, the `FATAL_ERROR` guard
extension, the `cna_backend_graphics_d3d12` target, and its factory dispatch — is Phase DX12's own
first task (`DX-101`), together with the rest of that phase.

| # | Task | Status | Notes |
|---|---|---|---|
| DX-10 | Add `"D3D11"` (only) to `CNA_GRAPHICS_BACKEND`'s CMake `STRINGS` property and a matching `CNA_BACKEND_D3D11` option flag, following the exact existing pattern (`CMakeLists.txt` lines ~94–139) | ✅ | **Closed 2026-07-13.** `D3D12` deliberately not added (design decision 9) — `CNA_GRAPHICS_BACKEND=D3D12` is still not a recognized value. |
| DX-11 | `FATAL_ERROR` guard: reject `CNA_GRAPHICS_BACKEND` = `D3D11` when `CMAKE_SYSTEM_NAME` is not `Windows`, with a message pointing at `cmake/toolchains/mingw-w64.cmake` (design decision 2) | ✅ | **Closed 2026-07-13.** Not yet independently re-verified by actually attempting a non-Windows configure (would need to temporarily fake `CMAKE_SYSTEM_NAME`, low value) — the guard's condition is simple and directly mirrors the existing `BGFX` check's own proven pattern. |
| DX-12 | `cna_backend_graphics_d3dcommon` static library target + `cna_backend_graphics_d3d11` target (`elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D11")` block, mirroring every existing backend's own block) | ✅ | **Closed 2026-07-13.** `cna_backend_graphics_d3d11` links exactly `d3d11 dxgi` — `DX-1`'s confirmed minimum set — plus `SDL3::SDL3` (needed for `SDL_GetWindowSizeInPixels`/`SDL_GetPointerProperty`/`SDL_Log`, not originally itemized in this row but obviously required, matching every other windowed backend's own link line). No `dxguid`, no `d3dcompiler`, no `cna_backend_graphics_d3dcommon` yet (design decision 4's shared core has no consumer until Phase DX3 — not created prematurely). Real build proof: `cmake --build cmake-build-d3d11 --target cna_backend_graphics_d3d11` succeeds cleanly. |
| DX-13 | `include/CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.hpp` + `src/CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.cpp`: a class implementing every `IGraphicsBackend` pure virtual — real where Phase DX2 can make it real (construction/teardown), honest stubs elsewhere until later phases replace them (mirrors `HEADLESS-3`/`SOFTWARE-3`'s own bar: `CnaTests` must link cleanly against it even before most methods are real) | ✅ | **Closed 2026-07-13, and substantially more real than the bar this row asked for** — Phase DX4's device/swap-chain/back-buffer/clear/present/readback work landed in the same pass (see `DX-20`–`DX-29` below) rather than being deferred as stubs, since the two phases turned out cheap to do together once `D3D11CreateDevice` was reachable. Every `ID3D11*`/`IDXGI*` member uses `Microsoft::WRL::ComPtr<T>` (`DX-6`'s resolved choice) from the first line. Stubs that remain honest "not yet implemented" `throw`s, each naming its own future phase: `CreateTexture` (DX6), `CreateSpriteBatch` (DX9), `CreateVertexBuffer`/`CreateIndexBuffer16` (DX5), `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` (DX8). `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` are real no-ops (not throws) since `GraphicsDevice` applies default state unconditionally on construction — matches this row's own "must not crash normal construction" bar; real state-object application is Phase DX7's job. |
| DX-14 | `CreateGraphicsBackend()` factory dispatch for `D3D11` | ✅ | **Closed 2026-07-13.** One-line `return std::make_unique<D3D11::D3D11GraphicsBackend>(args);` — no `D3D12` branch (`DX-10`'s own scoping). |
| DX-15 | First real build: Windows cross-build via `cmake/toolchains/mingw-w64.cmake -DCNA_GRAPHICS_BACKEND=D3D11`, confirm `CNA` and `CnaTests` targets both link | 🟨 | **`CNA` closed 2026-07-13** — `cmake --build cmake-build-d3d11 --target CNA` succeeds cleanly (full XNA API surface, not just the backend). A small standalone executable (`examples/d3d11_smoke_test.cpp`, see `DX-80`) also builds, links, and *runs correctly* end-to-end through Wine+DXVK. **`CnaTests` itself does NOT build yet** — genuinely blocked, not silently skipped. Attempting it surfaced 4 real, pre-existing gaps, all unrelated to D3D11 itself (nobody has actually linked this cross-target recently): (1) sibling `sharp-runtime`'s `Console.cpp`/`HttpClientHandler.cpp` unconditionally `#define NOMINMAX`, colliding with MinGW-w64's own `<bits/os_defines.h>` under `-Werror` — **fixed** (guarded with `#ifndef`, matching `Environment.cpp`'s own already-correct sibling pattern in the same repo); (2) `sharp-runtime`'s `Process.cpp` has two `-Werror=unused-parameter` hits in its Windows-unsupported stub bodies — **fixed** (`(void)param;`); (3) `cna_graphics`' own `ContentManager.cpp` guards `Video`/FFmpeg support with `!defined(__EMSCRIPTEN__) && !defined(__ANDROID__)`, missing `!defined(__MINGW32__)` even though `CMakeLists.txt`'s own `CNA_FFMPEG_AVAILABLE` computation already excludes `MINGW` — **fixed** (guard extended); (4) `cna_net_two_process_harness` (a POSIX-only helper executable, `<sys/resource.h>`) was built unconditionally under `CNA_ENABLE_NET AND CNA_BUILD_TESTS` even though its own consumer test is already excluded on `WIN32` — **fixed** (matching exclusion added to the executable's own guard). **Found but deliberately NOT fixed, out of this session's scope**: roughly 10 test files (`grep -rl '::setenv' tests/` — mostly `Microsoft/Xna/Framework/Audio/*Tests.cpp`) call POSIX-only `::setenv()` directly with no Windows equivalent (`_putenv_s`/`SetEnvironmentVariable`) — a real, pre-existing, much larger portability gap than the 4 above, genuinely out of scope for "prove D3D11 Phase DX2/DX4 works" and worth its own separate, explicitly-scoped task rather than an opportunistic fix here. |

---

## Phase DX3 — `D3DCommon` shared core

Everything in this phase lives under `include/CNA/Internal/Backends/D3DCommon/` +
`src/CNA/Internal/Backends/D3DCommon/`, linked by both D3D11 and D3D12 (design decision 4). Nothing
here depends on which of the two consumes it first — build each piece just-in-time for whichever
Phase DX4–DX9 task actually needs it, per the "Active execution order" note above.

| # | Task | Status | Notes |
|---|---|---|---|
| DX-11-fmt | `D3DFormatMapping.hpp`/`.cpp`: XNA `SurfaceFormat`/`DepthFormat` → `DXGI_FORMAT` (e.g. `Color`→`DXGI_FORMAT_R8G8B8A8_UNORM`, `Depth24Stencil8`→`DXGI_FORMAT_D24_UNORM_S8_UINT`) | ✅ | **Closed 2026-07-13.** Full `SurfaceFormat` enum covered (27 values) plus `DepthFormat` (4 values, `Depth24` correctly falling back to the same `DXGI_FORMAT_D24_UNORM_S8_UINT` as `Depth24Stencil8` since D3D11 has no pure-24-bit-depth format — matches Vulkan's own documented fallback). Cross-checked against Vulkan's own hardcoded format choices where Vulkan actually has one (`VK_FORMAT_R8G8B8A8_UNORM` for `Color`, the same `D24_UNORM_S8_UINT`-family depth fallback) — Vulkan itself doesn't implement a full per-`SurfaceFormat` table (it hardcodes `Color`/RGBA8 almost everywhere), so most of this table's non-`Color` entries are this project's own first real `SurfaceFormat`→native-format mapping, not a re-derivation of an existing one. 6 dedicated pixel/logic checks in `D3D11_Common` (`examples/d3d11_common_test.cpp`), verified genuinely discriminating via a live mutation test (temporarily broke `CullModeToD3D11`, confirmed the exact expected check failed 22/23, reverted, reconfirmed 23/23). |
| DX-12-state | `D3DStateMapping.hpp`/`.cpp`: XNA `Blend`/`BlendFunction`/`CompareFunction`/`CullMode`/`FillMode`/`TextureAddressMode`/`TextureFilter` → `D3D11_*`/`D3D12_*` equivalents | ✅ | **Closed 2026-07-13 — the "verify, don't assume" instruction was followed for real.** Directly inspected both `/usr/x86_64-w64-mingw32/include/d3d11.h` and `d3d12.h` on this machine: `D3D11_BLEND`/`D3D12_BLEND` (D3D12 is a strict superset, every D3D11 value matches numerically), `D3D11_BLEND_OP`/`D3D12_BLEND_OP`, `D3D11_COMPARISON_FUNC`/`D3D12_COMPARISON_FUNC`, `D3D11_CULL_MODE`/`D3D12_CULL_MODE`, `D3D11_FILL_MODE`/`D3D12_FILL_MODE`, `D3D11_TEXTURE_ADDRESS_MODE`/`D3D12_TEXTURE_ADDRESS_MODE`, and `D3D11_FILTER`/`D3D12_FILTER` (including its non-trivial bitmask-encoded values) **all confirmed numerically identical** — the header's own doc comment states this as a verified fact with the exact verification method, not an assumption. `CullMode` mapping specifically derived (not guessed) from this project's own Vulkan precedent: D3D11's native `FrontCounterClockwise=FALSE` default already matches D3D's own clockwise-is-front convention (no override needed, unlike Vulkan which must explicitly set `frontFace=CLOCKWISE`), so `CullClockwiseFace`→`D3D11_CULL_FRONT`/`CullCounterClockwiseFace`→`D3D11_CULL_BACK` follows the identical logic Vulkan's own "Task 870 empirical finding" already established. 13 dedicated checks in `D3D11_Common`, including the specific `CullMode` pair (the one genuinely non-obvious mapping in this whole table) — mutation-tested as described in `DX-11-fmt`'s row. |
| DX-13-hlsl | Port the 10 existing Vulkan GLSL shader pairs to HLSL source (`colored3d`, `textured3d`, `colored_textured3d`, `lit_textured3d`, `alpha_test3d`, `dual_texture3d`, `env_map3d`, `skinned3d`, `sprite2d`, `instanced3d`) into `src/CNA/Internal/Backends/D3DCommon/shaders/*.hlsl`, one file per stage per variant | ⬜ | **Not started.** Genuinely substantial, careful shader-math-porting work (10 shader pairs, line-by-line cross-checked against the GLSL source per `CLAUDE.md`'s own port-verification discipline) — deliberately not attempted opportunistically alongside the smaller mapping-table tasks above. Next concrete step for this plan. |
| DX-14-compile | `compile_shaders_hlsl.py` (mirrors `compile_shaders.py`'s role exactly): offline HLSL→DXBC compile step (design decision 5), generating `hlsl_shaders.hpp` with byte arrays, checked in like `spirv_shaders.hpp` | ⬜ | **Not started** — blocked behind `DX-13-hlsl` (nothing to compile yet). One real finding worth recording now: `D3DCompile()` itself is only reachable at *runtime*, via `d3dcompiler.dll`, which — like the rest of this backend — needs `wine`/DXVK-equivalent execution to actually run on this Debian machine (MinGW only provides the header/import-lib for *linking*, not a native Linux implementation) — so the "offline compile tool" will itself be a small Windows `.exe`, built via this same MinGW toolchain and run via `scripts/run-wine-dxvk.sh`, not a native Linux/Python tool shelling out to `fxc`. |
| DX-15-embed | Wire `hlsl_shaders.hpp`'s byte arrays into D3D11's `CreateVertexShader`/`CreatePixelShader` calls (Phase DX8 consumes this directly) | ⬜ | Not started — blocked behind `DX-14-compile`. |
| DX-16-vtx | `D3DVertexFormatHelper.hpp`: stride-keyed vertex layout inference (16/20/24/32/52-byte strides), mirroring `VulkanVertexFormatHelper.hpp`'s own convention, emitting a `D3D11_INPUT_ELEMENT_DESC[]`/`D3D12_INPUT_ELEMENT_DESC[]` array per stride | ✅ | **Closed 2026-07-13.** All 5 established strides implemented, byte offsets read directly from `VertexPositionColor`/`VertexPositionTexture`/`VertexPositionColorTexture`/`VertexPositionNormalTexture`/`VertexPositionNormalTextureSkinned`'s own real `getVertexDeclarationStatic()` C++ source (not re-derived/guessed), using this project's established HLSL semantic-name convention (`POSITION`/`COLOR`/`TEXCOORD`/`NORMAL`/`BLENDWEIGHT`/`BLENDINDICES`). `D3D12_INPUT_ELEMENT_DESC` variant not yet written — `D3D11_INPUT_ELEMENT_DESC`'s layout is identical in shape (design decision 4's own verified claim covers the enum *values* used inside it, e.g. `DXGI_FORMAT`, which are shared as-is; the struct itself would need its own small D3D12 overload when Phase DX12 needs it, not written speculatively now). 4 dedicated checks in `D3D11_Common` (strides 16/24/52 plus an unrecognized-stride negative case), mutation-verified alongside `DX-11-fmt`/`DX-12-state`'s own checks. |

---

## Phase DX4 — D3D11 device, swap chain, back buffer

Split into three independent resource-lifetime groups per design decision 11 — **device**
(`DX-20`–`DX-22`), **swap chain** (`DX-23`), and **window-size views** (`DX-24`) — specifically so
a plain resize (`DX-29`) touches only the narrowest group it actually needs, and device-removed
recovery (`DX-27`) is the only path that touches all three.

| # | Task | Status | Notes |
|---|---|---|---|
| DX-20 | Device-lifetime resource: `ID3D11Device`/`ID3D11DeviceContext` creation via `D3D11CreateDevice` (**not** the combined `...AndSwapChain` entry point — device creation is deliberately separated from swap-chain creation, design decision 11), requesting feature levels as a fallback array `{11_1, 11_0, 10_1, 10_0}`; if the call returns `E_INVALIDARG` retry once with `11_1` dropped from the array (some drivers reject an explicit 11.1 request outright), `D3D11_SDK_VERSION` | ✅ | **Closed 2026-07-13.** Real end-to-end proof via `examples/d3d11_smoke_test.cpp` (`D3D11_Smoke` CTest) running through DXVK 2.6.0 on a real GPU (AMD Radeon 780M/RADV): negotiated feature level `0xb100` = `D3D_FEATURE_LEVEL_11_1` (the *first* array entry succeeded directly — the `E_INVALIDARG`/drop-11_1 fallback branch exists in code but was never actually exercised on this machine/driver, an honest gap, not a false claim). |
| DX-21 | Device-lifetime resource: debug layer as best-effort — attempt the `DX-20` call with `flags \| D3D11_CREATE_DEVICE_DEBUG` first; if it returns `DXGI_ERROR_SDK_COMPONENT_MISSING`, retry the *exact same* call with that flag cleared and log `"D3D11 debug layer unavailable; retrying without it."` (design decision 12) — the debug layer must never be a hard requirement for the backend to construct | 🟨 | The direct-success path is real and proven (`D3D11_Smoke` reports `debug layer = enabled` — DXVK's Wine integration provides whatever `D3D11_CREATE_DEVICE_DEBUG` needs on this machine, no fallback triggered). **The actual `DXGI_ERROR_SDK_COMPONENT_MISSING` retry-without-debug-layer branch has never fired** — this machine's environment happens to always satisfy the debug-layer request, so this path is implemented but genuinely untested. `DX-90`'s real-Windows checklist (a machine without the D3D11 SDK debug layer installed) is the real test for this, exactly as this row's own Notes already anticipated. |
| DX-22 | Device-lifetime resource: obtain the modern DXGI factory chain and query tearing support **before** the swap chain is created — done once, at device-creation time, **never repeated on a plain resize**: `IDXGIDevice` (`QueryInterface` off the `ID3D11Device`) → `IDXGIAdapter` (`GetParent`) → `IDXGIFactory2` (`GetParent`) → `QueryInterface<IDXGIFactory5>` → `CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, ...)`; store the result as `allowTearingSupported_` — a *capability*, distinct from the separate `allowTearingRequested_` *policy* flag (design decision 13) | ✅ | **Closed 2026-07-13.** `D3D11_Smoke` reports `tearing = capable` — the full factory chain and `IDXGIFactory5::CheckFeatureSupport` genuinely succeeded under DXVK/RADV on this machine. |
| DX-23 | Swap-chain-lifetime resource: the `IDXGISwapChain1` object itself — modern creation via a `DXGI_SWAP_CHAIN_DESC1` (`Format = DXGI_FORMAT_R8G8B8A8_UNORM`, mapping to XNA `SurfaceFormat::Color` via `DX-11-fmt`'s own table; `BufferCount=2`, `SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD`, `SampleDesc.Count=1`, `Flags = (allowTearingSupported_ && allowTearingRequested_) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0` — design decision 13) sized from `PresentationParameters`, then `IDXGIFactory2::CreateSwapChainForHwnd` targeting the real `HWND` from `SDL_PROP_WINDOW_WIN32_HWND_POINTER` (design decision 7). Created once at startup (and on device-removed recovery); **a plain resize (`DX-29`) reuses this exact object via `ResizeBuffers`, it is never recreated** | ✅ | **Closed 2026-07-13.** `CreateSwapChainForHwnd` genuinely succeeded (DXVK's own log confirms a real presenter was created: `Format: VK_FORMAT_B8G8R8A8_SRGB`, `Present mode: VK_PRESENT_MODE_FIFO_KHR`, `Buffer size: 64x64` — DXVK's internally-chosen Vulkan surface format is its own translation of the requested `DXGI_FORMAT_R8G8B8A8_UNORM`, not something CNA controls or needs to match exactly). Resize itself (`DX-29`) not yet exercised — see that row. |
| DX-24 | Window-size-lifetime resources: back-buffer `ID3D11RenderTargetView` + default depth-stencil `ID3D11Texture2D`/`ID3D11DepthStencilView`, sized to match the swap chain — recreated on every resize (`DX-29`) | ✅ | **Closed 2026-07-13.** Proven indirectly but solidly: `Clear()`+`Present()`+`ReadBackbuffer()` all succeeded, which is only possible if the RTV/DSV/viewport binding actually worked. |
| DX-25 | Real `Clear(r,g,b,a)` (`ClearRenderTargetView`) and depth/stencil-inclusive `Clear*` variants (`ClearDepthStencilView`) | 🟨 | Plain `Clear(r,g,b,a)` is real and proven (`D3D11_Smoke`'s own core check, exact color round-trip twice with different colors). The other 5 combo variants (`ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil`) are implemented identically (real `ClearDepthStencilView` calls, correct flag combinations) but **not yet exercised by any test** — an honest, real gap, not a false completeness claim. |
| DX-26 | Real `Present()`: sync interval and tearing are **backend state**, not a direct D3D11 "set swap interval" API — there is no such entry point. `SetSwapInterval`/`SetPresentationMode` just update stored fields (`vsyncEnabled_`; `allowTearingRequested_`, design decision 13; `exclusiveFullscreen_` from whatever presentation-mode plumbing already tracks fullscreen state); `Present()` itself computes `const bool mayTear = allowTearingSupported_ && allowTearingRequested_ && !vsyncEnabled_ && !exclusiveFullscreen_;`, `UINT syncInterval = vsyncEnabled_ ? 1 : 0`, `UINT flags = mayTear ? DXGI_PRESENT_ALLOW_TEARING : 0`, then calls `swapChain->Present(syncInterval, flags)` | ✅ | **`Present()` itself closed 2026-07-13** — genuinely called and succeeded (DXVK's own presenter initialization log line only appears once a real `Present()` call reaches it; `D3D11_Smoke` exits 0 with no `CheckDeviceRemoved` log firing). The `mayTear`/`vsyncEnabled_` policy branches themselves were exercised only at their default values (`vsyncEnabled_=true` from `args.swapInterval=1`'s default) — the tearing-enabled/no-vsync branch specifically hasn't been independently exercised by a dedicated test yet. |
| DX-27 | Device-lost/removed **detection**, starting here in Phase DX4 — not deferred to `DX-90`: check the `HRESULT` returned by `Present()` (and any other call that can surface it) for `DXGI_ERROR_DEVICE_REMOVED`/`DXGI_ERROR_DEVICE_RESET`; on either, call `device->GetDeviceRemovedReason()` and log/report the reason | 🟨 | Code is real and in place (`CheckDeviceRemoved()`, called from both `Present()` and `EnsureSwapChainSize()`'s `ResizeBuffers` failure path). **Never actually triggered** — no real device removal occurred during this session's testing, so the detection *logic* itself remains unverified against a real `DXGI_ERROR_DEVICE_REMOVED`. Matches this row's own Notes: `DX-90` (real hardware) is the genuine test for this, not Wine+DXVK. |
| DX-28 | `ReadBackbuffer()`/`GetBackBufferData()`: real GPU→CPU readback via a staging `ID3D11Texture2D` (`D3D11_USAGE_STAGING` + `CopyResource` + `Map`) — this backend's first genuine pixel-correctness proof, same bar `SOFTWARE-13` set. **Must read via `D3D11_MAPPED_SUBRESOURCE::RowPitch`, one row at a time** — never assume the mapped rows are tightly packed (`RowPitch` can exceed `width * bytesPerPixel` due to driver-side row alignment) | ✅ | **Closed 2026-07-13 — this backend's actual reason to exist, proven for real.** `D3D11_Smoke`'s two checks: `Clear(20,40,60,255)` → `GetBackBufferData()` over a 4×4 region at origin reads back the exact color for every pixel; a second `Clear(200,100,50,255)` → readback over a *different* 4×4 region at `(10,10)` also matches exactly (proves it's a genuine live read, not a cached/stale first-call value). `RowPitch`-correctness itself not yet independently isolated by a test that deliberately spans a row-alignment boundary — the current 4-pixel-wide regions are too small to have exercised a case where `RowPitch != width*4`; a real gap worth a dedicated `DX-81` test later, not a false claim now. |
| DX-29 | Window resize handling — touches only the window-size-view group plus a `ResizeBuffers` call on the existing swap chain (design decision 11), nothing else: (1) unbind the current RTV/DSV (`OMSetRenderTargets(0, nullptr, nullptr)`); (2) release **every** reference to the old back buffer — `DX-24`'s RTV, the back-buffer texture reference, depth texture, DSV, **and** any SRV or staging/readback-cache resource (`DX-28`) that was ever bound off the old back buffer; (3) `context->Flush()` to help ensure the immediate context isn't still holding an implicit reference before resizing; (4) call `swapChain->ResizeBuffers(...)` on `DX-23`'s **existing** swap-chain object; (5) recreate `DX-24`'s RTV/DSV/viewport sized to the new dimensions | ⬜ | Implemented (`EnsureSwapChainSize()`, called lazily from `Present()`) but **never exercised** — `D3D11_Smoke`'s window was never resized during its short (~1 frame) lifetime. A real, open gap — `DX-83`'s resize test is the next concrete step to close this. |

---

## Phase DX5 — Vertex/index buffers

| # | Task | Status | Notes |
|---|---|---|---|
| DX-30 | `D3D11VertexBufferBackend`: `ID3D11Buffer` with `D3D11_BIND_VERTEX_BUFFER`, `SetData`/`SetDataWithOptions` via `Map`/`Unmap` (dynamic) or `UpdateSubresource` (default usage), matching `SetDataOptions::Discard`/`NoOverwrite` semantics | ⬜ | |
| DX-31 | `D3D11IndexBufferBackend`: 16-bit (`DXGI_FORMAT_R16_UINT`) and 32-bit (`DXGI_FORMAT_R32_UINT`), same buffer-update strategy | ⬜ | |
| DX-32 | Wire `DX-16-vtx`'s stride-keyed `D3D11_INPUT_ELEMENT_DESC` inference into actual `ID3D11InputLayout` creation, cached per (shader, stride) pair | ⬜ | |

---

## Phase DX6 — Textures and render targets

| # | Task | Status | Notes |
|---|---|---|---|
| DX-40 | `D3D11TextureBackend`: `ID3D11Texture2D` + `ID3D11ShaderResourceView`, `UpdatePixels`/`UpdatePixelsLevel`, mip level support | ⬜ | |
| DX-41 | `D3D11TextureCubeBackend`: 6-face `ID3D11Texture2D` array with `D3D11_RESOURCE_MISC_TEXTURECUBE` | ⬜ | Needed for `EnvironmentMapEffect` (design decision 6). |
| DX-42 | `D3D11Texture3DBackend`: `ID3D11Texture3D` | ⬜ | |
| DX-43 | `D3D11RenderTargetBackend`/`D3D11RenderTargetCubeBackend`: offscreen `ID3D11RenderTargetView`(s) + matching depth-stencil, `BindAsRenderTarget`/`UnbindAsRenderTarget` via `OMSetRenderTargets` | ⬜ | |
| DX-44 | `ID3D11SamplerState` creation/caching from `SamplerState` (filter/address-mode, via `DX-12-state`'s mapping table) | ⬜ | |
| DX-45 | MSAA render target support (`DXGI_SAMPLE_DESC`, resolve via `ResolveSubresource`) | ⬜ | The flip-model swap chain itself (`DX-23`) always has `SampleDesc.Count=1` — flip-model presentation does not support an MSAA back buffer directly. MSAA lives entirely in a separate offscreen `ID3D11Texture2D` render target (`DX-43`), `ResolveSubresource`'d into the non-MSAA back buffer (or an intermediate non-MSAA texture) before `Present()`. |

---

## Phase DX7 — State objects

| # | Task | Status | Notes |
|---|---|---|---|
| DX-50 | `ApplyBlendState`: `ID3D11BlendState` creation/caching from `BlendState`'s src/dst/op fields (color + alpha separately), via `DX-12-state` | ⬜ | |
| DX-51 | `ApplyDepthStencilState`: `ID3D11DepthStencilState` creation/caching, including stencil ops/masks/reference value | ⬜ | |
| DX-52 | `ApplyRasterizerState`: `ID3D11RasterizerState` creation/caching (`CullMode`/`FillMode`/depth bias/scissor-enable) | ⬜ | |
| DX-53 | Viewport/scissor rect: `RSSetViewports`/`RSSetScissorRects` | ⬜ | |

---

## Phase DX8 — Shaders and stock effects

Builds directly on `DX-13-hlsl`/`DX-15-embed` (Phase DX3) and `DX-32`'s input layout cache. Land in
the order below — cheapest/most-foundational shader variant first, same ordering discipline
`plan_software.md` used for its own rasterizer-then-shading progression.

| # | Task | Status | Notes |
|---|---|---|---|
| DX-60 | Define explicit GPU-side constant-buffer POD structs — **not** a raw `memcpy(mapped.pData, &params, sizeof(params))` of `GpuDrawParams` into one buffer — matching HLSL `cbuffer` packing rules (16-byte register boundaries; scalar/vector alignment; HLSL `bool` is 4 bytes, not C++'s 1) and this project's single, explicit matrix-layout policy (design decision 14 — pick `row_major` declared consistently everywhere, or CPU-side transpose before upload; do not decide per shader). At minimum: `struct alignas(16) D3DPerDrawConstants` (world matrix, material color, texture-enable flags, alpha-test params) and `struct alignas(16) D3DLightingConstants` (the 3-light directional/specular/ambient/emissive/fog fields) — grouped along the same lines `GpuDrawParams` already groups them conceptually — each with `static_assert(sizeof(...) % 16 == 0)` (D3D11 requires a constant buffer's `ByteWidth` to be a 16-byte multiple, design decision 14) plus field-offset checks, verified against the actual HLSL `cbuffer` declaration it's meant to match | ⬜ | This is the single most consequential design task in Phase DX8 — get this layout right once, since every effect variant below depends on it. Explicitly reject the memcpy shortcut: `GpuDrawParams` is a C++-convenience struct for the whole `IGraphicsBackend` surface, not a GPU-packing-correct layout — every other backend (Vulkan's UBO structs, WebGPU's) already went through this same explicit-GPU-struct exercise rather than reusing `GpuDrawParams`' raw layout, and D3D11 should not skip it. |
| DX-60a | `struct alignas(16) D3DBoneConstants` (the `SkinnedEffect` 72-bone array) as its **own**, separate constant buffer from `D3DPerDrawConstants`/`D3DLightingConstants` — not folded into the shared per-draw buffer; same `static_assert(sizeof(...) % 16 == 0)` and matrix-convention requirements as `DX-60` (design decision 14) | ⬜ | Matches this project's own Vulkan precedent (`skinned3d`'s dedicated `BoneBlock` UBO, distinct from the per-draw/fog UBOs — see `plan_graphics.md` Task 899's notes on `descriptorSetLayoutSkinned_`'s 3 separate bindings) and keeps the common per-draw CB's size and update frequency independent of whether a given draw uses skinning at all. |
| DX-61 | `colored3d` (stride 16, unlit vertex-color) pipeline: input layout + VS/PS + draw dispatch — first real 3D triangle, first real pixel test target | ⬜ | |
| DX-62 | `textured3d` (stride 20) + `colored_textured3d` (stride 24) | ⬜ | |
| DX-63 | `lit_textured3d` (stride 32): full per-light Blinn-Phong (`DirectionalLight0/1/2`, specular, ambient/emissive) — this is what makes D3D11 match design decision 6's "full parity, not a subset" goal | ⬜ | |
| DX-64 | `alpha_test3d`: per-pixel discard (`clip()` in HLSL) driven by `GpuDrawParams::alphaTest` | ⬜ | |
| DX-65 | `dual_texture3d`: two-sampler `DualTextureEffect` variant | ⬜ | |
| DX-66 | `env_map3d`: `TextureCube` reflection sampling, Fresnel weighting, specular tint (`DX-41` prerequisite) | ⬜ | |
| DX-67 | `skinned3d`: bind `DX-60a`'s dedicated `D3DBoneConstants` buffer (72×mat4, matching `GpuDrawParams::boneTransforms`), `weightsPerVertex`-aware blending | ⬜ | |
| DX-68 | `sprite2d` + `instanced3d` | ⬜ | `instanced3d` needs `DrawIndexedInstanced`/`DrawInstanced` and a per-instance vertex buffer slot, matching `GpuDrawParams::instanceVb`. |
| DX-69 | Fog (`GpuDrawParams::fogEnabled`/`fogColor`/`fogStart`/`fogEnd`) added to every 3D variant above, matching the Vulkan `FogParams` precedent (`plan_graphics.md` Task 899) | ⬜ | Schedule after the base (non-fog) variant of each shader lands, same order Vulkan itself used historically. |
| DX-58 | Custom `ShaderEffect` (arbitrary HLSL source, `IEffectBackend::CompileProgram`): runtime `D3DCompile()` path, separate from the offline stock-shader pipeline (design decision 5) | ⬜ | Lower priority than the stock effects above — a game using a custom `ShaderEffect` HLSL source is a narrower case than `BasicEffect`/friends working correctly first. |

---

## Phase DX9 — SpriteBatch

| # | Task | Status | Notes |
|---|---|---|---|
| DX-70 | `D3D11SpriteBatchBackend`: quad batching feeding the `sprite2d` pipeline from `DX-68`, matching `EasyGLSpriteBatchBackend`'s own destination/source-rect/origin/rotation/`SpriteEffects`-flip formula | ⬜ | |
| DX-71 | Custom `Effect` via `SpriteBatch::Begin(effect)` | ⬜ | |
| DX-72 | `TextureAddressMode::Wrap`/`Mirror` via SpriteBatch (a real gap on `SDL_Renderer`, per `docs/graphics-backend-feature-matrix.md` — D3D11 should not inherit that limitation, it has real sampler address-mode support) | ⬜ | |

---

## Phase DX10 — Tests

Per this project's convention (`CLAUDE.md`), test coverage belongs in the same task that implements
each capability — this phase names the cross-cutting suites, not "when to start testing."

| # | Task | Status | Notes |
|---|---|---|---|
| DX-80 | CTest registration for D3D11: a `cna_d3d11_test()` CMake macro, tests built for the Windows cross-target and run via `scripts/run-wine-dxvk.sh` (`DX-3`) | ✅ | **Closed 2026-07-13.** `cna_d3d11_test()` macro added (mirrors `cna_software_test()`, plus MinGW-specific link options `-static-libgcc -static-libstdc++`/`-Wl,--allow-multiple-definition` and `cna_copy_mingw_runtime`/`cna_copy_sdl_runtime` post-build DLL copies — needed since this is the first *windowed* MinGW CTest target, unlike Software/Headless). `D3D11_Smoke` registered, `COMMAND` is `scripts/run-wine-dxvk.sh $<TARGET_FILE:...>` as planned. **Verified via real `ctest` invocation, not just manual execution**: `ctest --test-dir cmake-build-d3d11 -R D3D11 --output-on-failure` → `1/1 Test #2: D3D11_Smoke ... Passed 6.17 sec`, `100% tests passed`. |
| DX-81 | Pixel tests per shader variant (`DX-61`–`DX-68`), same shape as the existing Vulkan pixel-test suite (`tests/`/`examples/vulkan_*_test.cpp`) — clear-color, flat triangle, textured quad, per-effect lighting/fog/alpha-test/dual-texture/env-map/skinned cases | ⬜ | Port the *test methodology*, not literal file copies — each Vulkan pixel test already documents its exact expected-color derivation; redo that derivation for D3D11's own coordinate/clip conventions (verify whether D3D11's clip-space Z range (0..1) needs the same "Z∈[0,1] not [-0.9,0.9]" adjustment Vulkan's own fog tests needed, per `plan_graphics.md` Task 899's notes). |
| DX-82 | State-object tests: blend/depth-stencil/rasterizer, mirroring the existing `Vulkan_BlendState_*`/`Vulkan_DepthStencilState_*` test family | ⬜ | |
| DX-83 | Resize/swap-chain tests: `BackBufferWidth`/`Height` changes, fullscreen toggle (Wine-only verification here; real fullscreen-transition behavior needs the real-Windows checklist, `DX-90`) | ⬜ | |
| DX-84 | `Discriminating power independently verified` pass for at least the first landed pixel test (`git stash`/targeted-mutation methodology, per this project's established convention) — sets the pattern for every later D3D11 test | ⬜ | |
| DX-85 | `scripts/run-wine-dxvk.sh` (or the CTest harness built on top of it) asserts DXVK was actually engaged for the run, not silently a `WineD3D` fallback — reuses `DX-4`'s own verification method (DXVK log file exists and identifies a real DXVK device/adapter line) as an automated check, not just a one-time manual spike | ⬜ | Directly closes the gap the project owner flagged: "pouhé spuštění pod Wine nestačí" (merely running under Wine isn't enough) — without this, the whole D3D11 pixel-test suite could quietly be validating `WineD3D`'s behavior instead of DXVK's (i.e., instead of a real Direct3D-semantics path), and nobody would notice from green CTest output alone. |
| DX-90 | **Real-Windows verification checklist — required for backend completion** (cannot be satisfied by Wine+DXVK alone — see "Development environment" above): a real Windows 10/11 machine; MSVC build (not just MinGW) at least compiles and passes the same test suite; real DXGI present-mode/tearing behavior; **full** device-lost/removed recovery (`DX-27` only added detection+logging — actually recreating all three lifetime groups, design decision 11, after a real device-removal event is verified here); WARP software-rasterizer fallback; D3D11 debug-layer warnings reviewed for anything Wine's DXVK path would have masked (including confirming `DX-21`'s debug-layer-missing fallback path is never silently hit on a machine that should have it); **at least one real GPU, from any single vendor** | ⬜ | Do not mark Phase DX4–DX9 "done" project-wide from Wine-only results — this checklist is the actual completion gate, matching this project's own "Wine proves the logic, not real-hardware parity" rule for `SDL_RENDERER`. GitHub-hosted Windows CI runners are useful for this row's MSVC-compile/unit-test/shader-generation-check portions, but are **not** a substitute for the real swap-chain/tearing/device-lost/driver-parity items above — those genuinely need a machine with a real display and a real GPU driver, which a typical CI runner doesn't reliably provide. |
| DX-91 | **Extended compatibility verification (not required to call the backend complete)**: repeat `DX-90`'s real-driver items (present/tearing, debug-layer warnings) on Intel, AMD, and NVIDIA hardware specifically, as each becomes available | ⬜ | Deliberately optional/best-effort, not a completion gate — requiring simultaneous physical access to three different GPU vendors before a single-developer project can call this backend "done" is an unreasonable bar, and isn't how this project's own multi-backend verification has worked historically (Vulkan/Bgfx were verified against whatever hardware was actually on hand). Pick up opportunistically as hardware becomes available; log findings per vendor rather than blocking on having all three at once. |

---

## Phase DX11 — Docs

| # | Task | Status | Notes |
|---|---|---|---|
| DX-95 | `docs/d3d11-backend.md`: what it's for, current capability boundary, the Wine+DXVK dev-loop setup (`DX-2`/`DX-3`), how to write a test, known limitations — mirrors `docs/software-backend.md`/`docs/headless-backend.md`'s own structure | ⬜ | Write incrementally as capabilities land (`plan_software.md` Phase S8's own discipline), not all at the end. |
| DX-96 | `docs/graphics-backend-feature-matrix.md`: add a `D3D11` column once its feature set is broad enough for a meaningful row-by-row comparison (mirrors how `HEADLESS`/`SOFTWARE` were each given their own explanatory note instead of a premature column) | ⬜ | |
| DX-97 | `README.md`: add D3D11 to the "Tested Compilers" table and a "Build (Windows cross-compilation — D3D11 backend)" section, mirroring the existing `SDL_RENDERER` MinGW-w64 section exactly | ⬜ | |
| DX-98 | Cross-reference this plan from `NEXT.md` once Phase DX1 actually starts (not before — this plan is not yet authorized, see the status banner) | ⬜ | |

---

## Phase DX12 — Direct3D 12 backend (deferred, none authorized yet)

Every row below is written up as a concrete, scoped task per design decision 9 — **none of these
are authorized for implementation**. Do not start this phase without an explicit project-owner
go-ahead, even after D3D11 (Phases DX1–DX11) is fully done and verified on real Windows. Coarser-
grained than the D3D11 phases above, since detailed design should wait until D3D11's own experience
(what actually worked, what Wine/DXVK-equivalent tooling exists for D3D12) can inform it.

| # | Task | Status | Notes |
|---|---|---|---|
| DX-100 | Spike: does Wine + vkd3d-proton give a usable local D3D12 dev loop on this Debian machine, equivalent to D3D11's DXVK path? | ⬜ | **Do this first, before committing to the rest of this phase's ordering** — if vkd3d-proton under Wine turns out unreliable, most of Phase DX12's own dev-and-test loop needs to shift toward Windows CI/VM much earlier than D3D11's did, and that changes how tasks below should be sequenced. |
| DX-101 | **Full `D3D12` CMake wiring, all of it deferred here rather than pre-staged in Phase DX2** (see that phase's own intro): add `"D3D12"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property and a `CNA_BACKEND_D3D12` option flag; extend `DX-11`'s `FATAL_ERROR` non-Windows guard to also cover `D3D12`; add the `cna_backend_graphics_d3d12` target (`elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D12")` block) linking `d3d12`/`dxgi` plus `D3DCommon` — same minimal-link-set discipline as `DX-12`/design decision 3, `dxguid`/`d3dcompiler` only if `DX-100`'s own spike actually found them necessary, not assumed to carry over unchanged from D3D11's own confirmed set; add the `CreateGraphicsBackend()` factory dispatch for `D3D12` | ⬜ | Until this task runs, `-DCNA_GRAPHICS_BACKEND=D3D12` is simply not a recognized value — by design, so no D3D12 scaffolding exists ahead of explicit authorization (design decision 9). |
| DX-102 | `ID3D12Device` creation, command queue (`ID3D12CommandQueue`), `IDXGISwapChain` (flip-model, `DXGI_SWAP_EFFECT_FLIP_DISCARD`) | ⬜ | |
| DX-103 | Descriptor heaps: RTV heap, DSV heap, CBV/SRV/UAV heap (shader-visible), allocation strategy | ⬜ | |
| DX-104 | Command allocators + command lists, per-frame-in-flight (matches this project's own Vulkan/Bgfx "batch a frame's draws" precedent conceptually, but D3D12 needs its own explicit allocator-reset lifecycle) | ⬜ | |
| DX-105 | Fences + frame synchronization (`ID3D12Fence`, `GetCompletedValue`/`SetEventOnCompletion`), N-frames-in-flight back-pressure | ⬜ | |
| DX-106 | Resource barriers: explicit `D3D12_RESOURCE_BARRIER` transitions for every render-target/texture state change this backend needs (present↔render-target, shader-resource↔render-target, etc.) | ⬜ | The single biggest source of "silently wrong" bugs in a first D3D12 backend, per the project owner's own research notes — needs real, deliberate state tracking per resource, not ad-hoc barrier calls. |
| DX-107 | Pipeline state objects (PSOs): one per (shader variant, input layout, blend/depth/rasterizer state combination) — reuse `D3DCommon`'s `DX-12-state` mapping tables and, as a bootstrap, `DX-13-hlsl`/`hlsl_shaders.hpp`'s DXBC bytecode (same source as D3D11, design decision 5) — PSOs accept DXBC directly, no DXIL requirement to get a first D3D12 draw working | ⬜ | PSO explosion (every state combination needs its own object) is a real design question — decide a caching/hashing strategy before implementing the first few, not after. If a later D3D12-specific need (e.g. a modern root-signature-driven binding model, or a shader feature DXBC/SM5 can't express) forces a move to DXIL/`dxc`, that's an expected, legitimate evolution of this task, not a sign the DXBC bootstrap was a mistake. |
| DX-108 | Root signatures: constant-buffer/SRV/sampler binding layout, one per shader-variant family (reuses D3D11's own `D3DPerDrawConstants`/`D3DLightingConstants`/`D3DBoneConstants` struct layouts from `DX-60`/`DX-60a`, not reinvented) | ⬜ | |
| DX-109 | Vertex/index buffers, textures, render targets — same resource *content* as D3D11's `DX-30`–`DX-45`, but through `ID3D12Resource`/`CreateCommittedResource` + explicit upload-heap staging instead of D3D11's implicit driver-managed uploads | ⬜ | |
| DX-110 | Device-removed recovery: `ID3D12Device::GetDeviceRemovedReason`, a real recreate-everything path (this is a case D3D11 backends often skip; D3D12 documentation treats it as expected to handle) | ⬜ | |
| DX-111 | Port the same shader/effect variant set D3D11 lands in Phase DX8, reusing `D3DCommon`'s HLSL sources and DXBC bytecode as the starting point (design decision 5) — treat this as the compatible bootstrap it is, not a claim that D3D11's exact shader binaries are D3D12's permanent, final shader system | ⬜ | Should be substantially cheaper than D3D11's own Phase DX8, since the actual shading math and CBuffer layout were already solved there — this phase is about the D3D12 command/resource plumbing around them, not new shader math. If D3D12-specific work later (root signatures, DXIL, ray tracing) needs a genuinely different shader representation, that's a separate, explicitly scoped follow-up task, not scope creep into this one. |
| DX-112 | SpriteBatch, matching D3D11's `DX-70`–`DX-72` | ⬜ | |
| DX-113 | Tests: same shape as `DX-80`–`DX-84`, plus D3D12-specific cases (barrier-transition correctness, fence/frame-in-flight back-pressure, device-removed recovery) | ⬜ | |
| DX-114 | Real-Windows verification checklist, same shape as `DX-90` plus DXR/ray-tracing feature-level detection if that ever becomes a project goal (explicitly out of scope for v1 — see "Why these backends" above) | ⬜ | |
| DX-115 | `docs/d3d12-backend.md` + feature-matrix column + README updates, mirroring `DX-95`–`DX-97` | ⬜ | |

---

## Boundaries (stop and ask, don't improvise)

- **Do not start any task in this plan without the project owner's explicit go-ahead** — the whole
  plan is currently unauthorized (status banner). This is stronger than the usual per-phase caution
  other plans use, since not even Phase DX1 has been approved yet.
- **Do not start Phase DX12 (D3D12) even after D3D11 is done**, without a separate, later go-ahead
  (design decision 9) — D3D11 finishing does not implicitly authorize D3D12.
- **Do not claim real-Windows parity from Wine+DXVK/vkd3d-proton results alone** — `DX-90`/`DX-114`
  are real completion gates, not optional polish, per the "Development environment" section above.
- **Do not let `D3D11`/`D3D12`-specific code leak into the shared `IGraphicsBackend`/`GpuDrawParams`
  interface layer** beyond what a genuine common-interface need justifies — same backend-locality
  rule every other backend plan already follows (`CLAUDE.md`, `plan_webgpu.md`/`plan_headless.md`/
  `plan_software.md`'s own boundaries sections).
- **Do not merge D3D11 and D3D12 into one shared device/backend class** "for less duplication" —
  design decision 4 already scoped what's genuinely shared (`D3DCommon`); forcing the actual
  device/command/resource logic to share code across two structurally different APIs is exactly the
  kind of premature abstraction `CLAUDE.md` warns against.
- If `DX-1`/`DX-3` (MinGW-w64 header completeness, or the Wine+DXVK loop itself) turn out not to
  work on this machine, **stop and report the specific gap** rather than silently downgrading scope
  (e.g. quietly deciding to only ever build on real Windows) — that would invalidate this plan's
  core "develop on Debian" premise and needs a project-owner decision about how to proceed.
- If `DX-12-state`'s "D3D11 and D3D12 enum values are numerically identical" assumption turns out
  false for some enum, that's a legitimate, expected finding to record — not a blocker, just don't
  let it silently produce a wrong mapping for the divergent case.
- **Do not skip `DX-21`'s debug-layer fallback or `DX-20`'s feature-level fallback array "since it
  works under Wine anyway"** (design decision 12) — Wine+DXVK is not evidence that a hardcoded
  `D3D11_CREATE_DEVICE_DEBUG`/`D3D_FEATURE_LEVEL_11_0`-only path is safe on real Windows; this is
  exactly the kind of gap that only shows up on `DX-90`'s real-Windows pass, expensively, if skipped
  here.
- **Do not leave `DX-6`'s COM-pointer-convention decision unresolved past Phase DX2** — every task
  from Phase DX4 onward creates COM objects; retrofitting a `ComPtr<T>` convention after several
  phases already have bare `Release()` call sites is a much larger cleanup than deciding once,
  early (design decision 10).
