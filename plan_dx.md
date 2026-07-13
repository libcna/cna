# Direct3D 11 / Direct3D 12 Graphics Backends — Implementation Plan

> **Status: not started, 2026-07-13.** This plan is a proposal, written up for review before any
> code lands — no `D3D11`/`D3D12` backend directory exists yet, `CNA_GRAPHICS_BACKEND` does not yet
> accept either value, and nothing below is authorized for implementation until the project owner
> gives an explicit go-ahead (same convention as `plan_software.md`'s Phase S9 and the old
> WebGPU-forbidden period before it was lifted — see `NEXT.md`). **Direct3D 11 is the actual near-
> term target; Direct3D 12 is written up in full but deliberately deferred** (Phase DX12) — see
> "Why D3D11 first, D3D12 later" below for the reasoning, which came from the project owner directly.
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
    └── D3D11 / D3D12
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
9. **D3D12 tasks in this plan (Phase DX12) are written in full but explicitly NOT authorized.**
   Same convention as `plan_software.md`'s Phase S9: a concrete, scoped-out task list the project
   owner can approve later, one task or one batch at a time — not a vague "someday" note. Do not
   start Phase DX12 without an explicit go-ahead, even after Phase DX1–DX11 (D3D11) is fully done.
10. **COM object lifetime: one decided convention before Phase DX2 lands, not per-file
    improvisation.** `Microsoft::WRL::ComPtr<T>` is the obvious default on real Windows/MSVC, but
    its availability and ergonomics under MinGW-w64 — this project's actual *primary* Debian
    dev-loop compiler (Phase DX1) — are not something to assume without checking (`DX-6`). Options:
    `Microsoft::WRL::ComPtr` (Windows-only, MinGW support varies by version/headers actually
    present); `wil::com_ptr` (an extra vendored dependency); a small project-local `CNA::ComPtr<T>`
    (mirrors this project's own "add a minimal stub rather than a new dependency" philosophy, see
    `CLAUDE.md`'s SharpRuntime-extension rule); or raw `IUnknown::Release()` wrapped in whatever
    RAII pattern this codebase already uses elsewhere. **Leaning recommendation, not yet decided**:
    a small project-local `ComPtr<T>` unless `DX-6` finds `Microsoft::WRL::ComPtr` genuinely clean
    under the already-standardized MinGW-w64 toolchain — this avoids tying the backend to a
    Windows-only header/dependency question when the primary dev loop is Linux-hosted cross-
    compilation. Whichever is chosen, **every** `ID3D11*`/`IDXGI*`/`ID3D12*` interface pointer
    anywhere in `D3D11GraphicsBackend`/`D3D12GraphicsBackend`/`D3DCommon` uses it — no bare
    `Release()` call sites. Without this, leaks on resize, double-releases, and forgotten releases
    on a partially-failed initialization are the expected failure mode for a hand-rolled D3D
    backend, not a hypothetical risk. See `DX-6`'s own acceptance checklist for the exact method
    surface required if the project-local option is chosen (move semantics, `GetAddressOf()` vs.
    `ReleaseAndGetAddressOf()`, `Attach`/`Detach`, a `QueryInterface` helper) — each maps to a
    specific, real bug class in hand-rolled COM wrappers, not an optional nice-to-have.
11. **Two independent resource-lifetime groups: `CreateDeviceResources()` and
    `CreateWindowSizeDependentResources()`.** Device-level resources (device, context, immutable
    state-object caches, compiled shaders) are created once and only torn down on device-removed
    recovery (`DX-27`). Window-size-dependent resources (swap chain, back-buffer RTV, default DSV,
    viewport) are recreated on resize (`DX-29`) *and* on device-removed recovery — but a plain
    resize must never touch the first group. This directly shapes how Phase DX4's tasks are split
    (`DX-20`/`DX-21` = device group; `DX-22`–`DX-24` = window-size group) — a flat, undivided
    "create everything" function is exactly what makes resize and device-loss recovery fragile in a
    hand-rolled D3D backend.
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

---

## Active execution order — do this one phase at a time

1. **Phase DX1** (Windows cross-build dev loop) unblocks everything else — nothing in this plan can
   be built, let alone tested, without a working MinGW-w64 + Wine + DXVK loop on this Debian
   machine. `cmake/toolchains/mingw-w64.cmake` already exists and is already proven for
   `SDL_RENDERER`; this phase is mostly about confirming it also covers D3D11's extra headers/libs
   and standing up the Wine/DXVK side, which is new.
2. **Phase DX2** (CMake integration + skeleton) depends only on Phase DX1's toolchain being
   confirmed working — it wires `D3D11` (and the `D3D12` placeholder) into `CNA_GRAPHICS_BACKEND`
   exactly like every other backend, with a real (if minimal) `D3D11GraphicsBackend` that at least
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
| DX-1 | Confirm `cmake/toolchains/mingw-w64.cmake` (already used for `SDL_RENDERER`, see `README.md`) resolves `<d3d11.h>`/`<dxgi.h>` (and `<d3dcompiler.h>` separately, for the shader-tooling target only — design decision 3) from the `mingw-w64` apt package on this machine, and **empirically determine the actual minimum link-library set** — do not assume `dxguid` is needed just because it's available | ⬜ | Spike task: a throwaway `.cpp` that includes the headers and links only `libd3d11.a`/`libdxgi.a` first; add `libdxguid.a` only if the linker actually demands a GUID symbol it provides. Keep `libd3dcompiler.a` out of this spike entirely — it belongs on the separate shader-tooling link line (`DX-14-compile`/`DX-58`), not the main backend. Record the confirmed minimum set in `docs/d3d11-backend.md` (`DX-95`) so `DX-12`'s CMake wiring encodes a verified answer, not a guess. If anything is missing/thin beyond that, record exactly what and consider whether Windows SDK headers need to be vendored instead. |
| DX-2 | Install and configure a dedicated Wine prefix + DXVK for D3D11 testing (`WINEPREFIX=~/.wine-cna-d3d11`, DXVK's `d3d11.dll`/`dxgi.dll` installed into it), following the project owner's own researched steps | ⬜ | Document exact install commands in `docs/d3d11-backend.md` (Phase DX11) once confirmed working, not just in this plan. |
| DX-3 | `scripts/run-wine-dxvk.sh` wrapper (mirrors the existing `scripts/run-all-backend-smoke-tests.sh` convention): sets `WINEPREFIX`, optional `DXVK_HUD`/`DXVK_LOG_LEVEL`, execs `wine64 "$1"` | ⬜ | Needed so CTest registration (Phase DX10) can invoke Windows `.exe` test binaries the same way a human would from the CLI. |
| DX-4 | Prove the loop end-to-end with a minimal non-CNA smoke program: create a bare `ID3D11Device`+swap chain, clear to a known color, run under `wine64` with DXVK installed, confirm no crash/error in `DXVK_LOG_LEVEL=info` output **and confirm DXVK is actually the thing that ran** (see Notes) | ⬜ | Merely "ran under Wine without crashing" is not sufficient — Wine can silently fall back to its own `WineD3D` implementation instead of DXVK if the DXVK DLL override isn't actually in effect, which would make every later pixel test's "passed under Wine" claim meaningless. Confirm: (1) a DXVK log file was actually created in the configured `DXVK_LOG_PATH`; (2) its contents identify a real DXVK device/adapter line (DXVK logs its own version and the selected Vulkan device on startup); (3) optionally, run once with `DXVK_HUD=devinfo,version` and visually confirm the HUD shows a DXVK version string, not nothing/WineD3D. Record the exact verification method used in `docs/d3d11-backend.md` (`DX-95`) so every later test run can be spot-checked the same way. |
| DX-5 | Document the CLion CMake-profile setup the project owner described (separate `Windows-D3D11-MinGW` profile, custom "run" step invoking `scripts/run-wine-dxvk.sh`) | ⬜ | Documentation-only task; no new CMake logic beyond what Phase DX2 adds. |
| DX-6 | Decide the COM object lifetime convention (design decision 10) before any Phase DX2 code lands: spike whether `Microsoft::WRL::ComPtr<T>` builds cleanly against this project's actual MinGW-w64 toolchain (`cmake/toolchains/mingw-w64.cmake`); if not (or if it's awkward), implement a small project-local `CNA::ComPtr<T>` in `D3DCommon` instead. **If building the local type, it must implement** (not a smaller subset): move constructor/move assignment; copy either disallowed or done via a correct `AddRef()`; `Reset()`; `Get()`; `GetAddressOf()`; `ReleaseAndGetAddressOf()` (distinct from `GetAddressOf()` — see Notes); `Attach()`/`Detach()`; a safe `operator->()`; and a `QueryInterface`-style helper (e.g. `As<U>()`) | ⬜ | This decision gates every later task that touches a `ID3D11*`/`IDXGI*` pointer — resolve it once, here, not piecemeal as each later task happens to need a COM pointer. **The `GetAddressOf()` vs. `ReleaseAndGetAddressOf()` distinction is the actual reason this needs a real acceptance checklist, not just "write a ComPtr"**: passing a `ComPtr` that already holds a live object into an out-parameter via plain `GetAddressOf()` silently overwrites the stored raw pointer with whatever the callee writes, without releasing the old object first — a real, easy-to-introduce leak in exactly the kind of resize/recreate code this backend has a lot of (`DX-29`, `DX-27`). Every call site that re-populates an already-possibly-live `ComPtr<T>` (as opposed to first-time construction into an empty one) must use `ReleaseAndGetAddressOf()`, not `GetAddressOf()` — a documented, checked convention, not left to each call site's author to remember. |

---

## Phase DX2 — CMake integration and skeleton

| # | Task | Status | Notes |
|---|---|---|---|
| DX-10 | Add `"D3D11"` and `"D3D12"` to `CNA_GRAPHICS_BACKEND`'s CMake `STRINGS` property and matching `CNA_BACKEND_D3D11`/`CNA_BACKEND_D3D12` option flags, following the exact existing pattern (`CMakeLists.txt` lines ~94–139) | ⬜ | `D3D12`'s option flag can exist from the start (cheap), even though Phase DX12 itself is not authorized — matches how every other backend option is declared together. |
| DX-11 | `FATAL_ERROR` guard: reject `CNA_GRAPHICS_BACKEND` = `D3D11`/`D3D12` when `CMAKE_SYSTEM_NAME` is not `Windows`, with a message pointing at `cmake/toolchains/mingw-w64.cmake` (design decision 2) | ⬜ | Place near the existing `BGFX`-platform-`WARNING` check (`CMakeLists.txt` line ~161) for discoverability, but as `FATAL_ERROR` not `WARNING` — these truly cannot build elsewhere. |
| DX-12 | `cna_backend_graphics_d3dcommon` static library target + `cna_backend_graphics_d3d11`/`cna_backend_graphics_d3d12` targets (`elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D11")` / `"D3D12"` blocks, mirroring every existing backend's own block) | ⬜ | Link only `DX-1`'s confirmed minimum set (design decision 3) — expected `target_link_libraries(cna_backend_graphics_d3d11 PUBLIC cna_backend_graphics_d3dcommon d3d11 dxgi)` (D3D12 analogously `d3d12 dxgi`), adding `dxguid` on either only if `DX-1`/`DX-100` actually found it necessary. `d3dcompiler` is deliberately **not** on this list — it's linked only where `D3DCompile()` is actually called (the shader-compile tool from `DX-14-compile`, and/or `DX-58`'s runtime custom-effect path), so a plain `cna_backend_graphics_d3d11` build carries no `d3dcompiler_47.dll` runtime dependency. No `find_package`/`FetchContent` needed either way. |
| DX-13 | `include/CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.hpp` + `src/CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.cpp`: a class implementing every `IGraphicsBackend` pure virtual — real where Phase DX2 can make it real (construction/teardown), honest stubs elsewhere until later phases replace them (mirrors `HEADLESS-3`/`SOFTWARE-3`'s own bar: `CnaTests` must link cleanly against it even before most methods are real) | ⬜ | Every COM interface member on this class uses whatever pointer convention `DX-6` decided (design decision 10) from the very first commit — do not start with bare pointers "for now" and convert later. |
| DX-14 | `CreateGraphicsBackend()` factory dispatch for `D3D11` (and a placeholder, throwing-if-selected dispatch for `D3D12` until Phase DX12 starts) | ⬜ | |
| DX-15 | First real build: Windows cross-build via `cmake/toolchains/mingw-w64.cmake -DCNA_GRAPHICS_BACKEND=D3D11`, confirm `CNA` and `CnaTests` targets both link | ⬜ | This is the actual "does the skeleton compile" gate for the whole plan. |

---

## Phase DX3 — `D3DCommon` shared core

Everything in this phase lives under `include/CNA/Internal/Backends/D3DCommon/` +
`src/CNA/Internal/Backends/D3DCommon/`, linked by both D3D11 and D3D12 (design decision 4). Nothing
here depends on which of the two consumes it first — build each piece just-in-time for whichever
Phase DX4–DX9 task actually needs it, per the "Active execution order" note above.

| # | Task | Status | Notes |
|---|---|---|---|
| DX-11-fmt | `D3DFormatMapping.hpp`/`.cpp`: XNA `SurfaceFormat`/`DepthFormat` → `DXGI_FORMAT` (e.g. `Color`→`DXGI_FORMAT_R8G8B8A8_UNORM`, `Depth24Stencil8`→`DXGI_FORMAT_D24_UNORM_S8_UINT`) | ⬜ | Cross-check against Vulkan's own `VkFormat` mapping table (`VulkanGraphicsBackend.cpp`) for the same source enum — same source-of-truth enum, just a different target format table. |
| DX-12-state | `D3DStateMapping.hpp`/`.cpp`: XNA `Blend`/`BlendFunction`/`CompareFunction`/`CullMode`/`FillMode`/`TextureAddressMode`/`TextureFilter` → `D3D11_*`/`D3D12_*` equivalents | ⬜ | **Verify, do not assume, that the D3D11 and D3D12 enum values are numerically identical** for each of these before writing one shared table used by both — check both SDK headers directly (design decision 4). If any diverge, split just that one enum's table, keep the rest shared. |
| DX-13-hlsl | Port the 10 existing Vulkan GLSL shader pairs to HLSL source (`colored3d`, `textured3d`, `colored_textured3d`, `lit_textured3d`, `alpha_test3d`, `dual_texture3d`, `env_map3d`, `skinned3d`, `sprite2d`, `instanced3d`) into `src/CNA/Internal/Backends/D3DCommon/shaders/*.hlsl`, one file per stage per variant | ⬜ | Direct semantic port (constant buffers instead of UBOs/push constants, `SV_Position`/`TEXCOORD`/`COLOR` semantics instead of GLSL `layout(location=N)`), not a redesign — cross-check math against the GLSL source line-by-line, same discipline `CLAUDE.md` requires for XNA API ports. |
| DX-14-compile | `compile_shaders_hlsl.py` (mirrors `compile_shaders.py`'s role exactly): offline HLSL→DXBC compile step (design decision 5), generating `hlsl_shaders.hpp` with byte arrays, checked in like `spirv_shaders.hpp` | ⬜ | Decide and record where this actually runs (a real Windows machine, Windows CI, or `fxc`/`d3dcompiler_47.dll` under Wine if that proves reliable enough — spike it, don't assume) before treating the generated header as routinely regeneratable in this Debian dev loop. |
| DX-15-embed | Wire `hlsl_shaders.hpp`'s byte arrays into D3D11's `CreateVertexShader`/`CreatePixelShader` calls (Phase DX8 consumes this directly) | ⬜ | |
| DX-16-vtx | `D3DVertexFormatHelper.hpp`: stride-keyed vertex layout inference (16/20/24/32/52-byte strides), mirroring `VulkanVertexFormatHelper.hpp`'s own convention, emitting a `D3D11_INPUT_ELEMENT_DESC[]`/`D3D12_INPUT_ELEMENT_DESC[]` array per stride | ⬜ | Reuse the exact same stride→layout table this project already established for WebGPU/Software — do not invent new stride semantics. |

---

## Phase DX4 — D3D11 device, swap chain, back buffer

Split into two independent resource-lifetime groups per design decision 11 —
`CreateDeviceResources()` (`DX-20`/`DX-21`) and `CreateWindowSizeDependentResources()`
(`DX-22`–`DX-24`) — specifically so resize (`DX-29`) and device-removed recovery (`DX-27`) each
touch only the group they actually need to.

| # | Task | Status | Notes |
|---|---|---|---|
| DX-20 | `CreateDeviceResources()`: `ID3D11Device`/`ID3D11DeviceContext` creation via `D3D11CreateDevice` (**not** the combined `...AndSwapChain` entry point — device creation is deliberately separated from swap-chain creation, design decision 11), requesting feature levels as a fallback array `{11_1, 11_0, 10_1, 10_0}`; if the call returns `E_INVALIDARG` retry once with `11_1` dropped from the array (some drivers reject an explicit 11.1 request outright), `D3D11_SDK_VERSION` | ⬜ | Record the actual obtained `D3D_FEATURE_LEVEL` (`GetFeatureLevel()`) and log it (design decision 12). CNA may still end up rejecting anything below 11.0 once negotiation completes — that's a separate policy decision from making the negotiation itself broad. |
| DX-21 | Debug layer as best-effort: attempt the `DX-20` call with `flags \| D3D11_CREATE_DEVICE_DEBUG` first; if it returns `DXGI_ERROR_SDK_COMPONENT_MISSING`, retry the *exact same* call with that flag cleared and log `"D3D11 debug layer unavailable; retrying without it."` (design decision 12) — the debug layer must never be a hard requirement for the backend to construct | ⬜ | This is the concrete reason `DX-20` is split out of `D3D11CreateDeviceAndSwapChain`: the retry-without-debug-layer logic needs to wrap the device-creation call specifically. A debug build that only ever ran under Wine (where DXVK doesn't require the real D3D11 SDK debug layer to be installed) must not silently assume every real Windows machine has it — this exact gap is why `DX-90`'s real-Windows checklist exists. |
| DX-22 | Obtain the modern DXGI factory chain and query tearing support **before** the swap chain is created: `IDXGIDevice` (`QueryInterface` off the `ID3D11Device`) → `IDXGIAdapter` (`GetParent`) → `IDXGIFactory2` (`GetParent`) → `QueryInterface<IDXGIFactory5>` → `CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, ...)`; store the result (`allowTearingSupported_`) | ⬜ | Order matters here (see `DX-23`'s Notes) — this must complete before `DXGI_SWAP_CHAIN_DESC1::Flags` is built, not be queried after the swap chain already exists. Not universally available — query, don't assume; confirm during `DX-4`/here what DXVK itself reports for this under Wine, since that's this project's primary dev-loop environment. |
| DX-23 | `CreateWindowSizeDependentResources()` — modern swap-chain creation: build a `DXGI_SWAP_CHAIN_DESC1` (`BufferCount=2`, `SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD`, `SampleDesc.Count=1`, `Flags = allowTearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0`) sized from `PresentationParameters`, then `IDXGIFactory2::CreateSwapChainForHwnd` targeting the real `HWND` from `SDL_PROP_WINDOW_WIN32_HWND_POINTER` (design decision 7) | ⬜ | **`DXGI_PRESENT_ALLOW_TEARING` at `Present()`-time (`DX-26`) is not sufficient on its own** — the swap chain itself must be created with `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` in this descriptor, or every later tearing-flagged `Present()` call is invalid. This is exactly why `DX-22`'s tearing query must run before this task's `CreateSwapChainForHwnd` call, not after. Deliberately not the legacy `D3D11CreateDeviceAndSwapChain` combined path — the modern DXGI factory chain is what makes flip-model presentation and tearing possible at all. A throwaway `D3D11CreateDeviceAndSwapChain`-based spike is fine for `DX-4`'s own non-CNA smoke test, but the real backend uses this path from the start, not as a later "modernization" pass. |
| DX-24 | `CreateWindowSizeDependentResources()`'s remaining piece — back-buffer `ID3D11RenderTargetView` + default depth-stencil `ID3D11Texture2D`/`ID3D11DepthStencilView`, sized to match the swap chain | ⬜ | |
| DX-25 | Real `Clear(r,g,b,a)` (`ClearRenderTargetView`) and depth/stencil-inclusive `Clear*` variants (`ClearDepthStencilView`) | ⬜ | |
| DX-26 | Real `Present()`: sync interval and tearing are **backend state**, not a direct D3D11 "set swap interval" API — there is no such entry point. `SetSwapInterval`/`SetPresentationMode` just update stored fields (`vsyncEnabled_`; `exclusiveFullscreen_` from whatever presentation-mode plumbing already tracks fullscreen state); `Present()` itself computes `const bool mayTear = allowTearingSupported_ && !vsyncEnabled_ && !exclusiveFullscreen_;` (using `DX-22`'s stored capability flag), `UINT syncInterval = vsyncEnabled_ ? 1 : 0`, `UINT flags = mayTear ? DXGI_PRESENT_ALLOW_TEARING : 0`, then calls `swapChain->Present(syncInterval, flags)` | ⬜ | `DXGI_PRESENT_ALLOW_TEARING` is only valid for windowed/borderless presentation, never classic exclusive fullscreen — `mayTear` must exclude that case explicitly, not just check the tearing-support capability bit, or `Present()` can return an error/undefined result while in exclusive fullscreen. Corrects this plan's own earlier, inaccurate framing of `SetSwapInterval` as something mapped directly onto a present-time call — it's a state write now, applied every `Present()`. |
| DX-27 | Device-lost/removed **detection**, starting here in Phase DX4 — not deferred to `DX-90`: check the `HRESULT` returned by `Present()` (and any other call that can surface it) for `DXGI_ERROR_DEVICE_REMOVED`/`DXGI_ERROR_DEVICE_RESET`; on either, call `device->GetDeviceRemovedReason()` and log/report the reason | ⬜ | Detection/diagnostics only at this stage — full automatic recreate-everything recovery is real, additional work, legitimately deferred, but every later phase's draw/present call sites should already be able to surface "the device is gone" instead of silently misbehaving or crashing. `DX-90` verifies this against a *real* device-removal scenario on real hardware/driver, which Wine+DXVK cannot manufacture convincingly. |
| DX-28 | `ReadBackbuffer()`/`GetBackBufferData()`: real GPU→CPU readback via a staging `ID3D11Texture2D` (`D3D11_USAGE_STAGING` + `CopyResource` + `Map`) — this backend's first genuine pixel-correctness proof, same bar `SOFTWARE-13` set | ⬜ | First real pixel test candidate (Phase DX10): clear to a known color, read back, assert exact match. |
| DX-29 | Window resize handling: tear down and recreate **only** `CreateWindowSizeDependentResources()`'s outputs (`DX-22`–`DX-24`: swap chain `ResizeBuffers`, RTV/DSV recreation, viewport update) — `CreateDeviceResources()`'s device/context/shader/state caches (`DX-20`/`DX-21`) are untouched (design decision 11) | ⬜ | |

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
| DX-45 | MSAA render target support (`DXGI_SAMPLE_DESC`, resolve via `ResolveSubresource`) | ⬜ | |

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
| DX-60 | Define explicit GPU-side constant-buffer POD structs — **not** a raw `memcpy(mapped.pData, &params, sizeof(params))` of `GpuDrawParams` into one buffer — matching HLSL `cbuffer` packing rules (16-byte register boundaries; scalar/vector alignment; explicit, matched `row_major`/`column_major` matrix layout on both the C++ struct and the HLSL declaration; HLSL `bool` is 4 bytes, not C++'s 1). At minimum: `struct alignas(16) D3DPerDrawConstants` (world matrix, material color, texture-enable flags, alpha-test params) and `struct alignas(16) D3DLightingConstants` (the 3-light directional/specular/ambient/emissive/fog fields) — grouped along the same lines `GpuDrawParams` already groups them conceptually — each with `static_assert` checks on size and field offsets, verified against the actual HLSL `cbuffer` declaration it's meant to match | ⬜ | This is the single most consequential design task in Phase DX8 — get this layout right once, since every effect variant below depends on it. Explicitly reject the memcpy shortcut: `GpuDrawParams` is a C++-convenience struct for the whole `IGraphicsBackend` surface, not a GPU-packing-correct layout — every other backend (Vulkan's UBO structs, WebGPU's) already went through this same explicit-GPU-struct exercise rather than reusing `GpuDrawParams`' raw layout, and D3D11 should not skip it. |
| DX-60a | `struct alignas(16) D3DBoneConstants` (the `SkinnedEffect` 72-bone array) as its **own**, separate constant buffer from `D3DPerDrawConstants`/`D3DLightingConstants` — not folded into the shared per-draw buffer | ⬜ | Matches this project's own Vulkan precedent (`skinned3d`'s dedicated `BoneBlock` UBO, distinct from the per-draw/fog UBOs — see `plan_graphics.md` Task 899's notes on `descriptorSetLayoutSkinned_`'s 3 separate bindings) and keeps the common per-draw CB's size and update frequency independent of whether a given draw uses skinning at all. |
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
| DX-80 | CTest registration for D3D11: a `cna_d3d11_test()` CMake macro, tests built for the Windows cross-target and run via `scripts/run-wine-dxvk.sh` (`DX-3`) | ⬜ | Mirrors `cna_software_test()`/`cna_headless_test()`'s own macro pattern. |
| DX-81 | Pixel tests per shader variant (`DX-61`–`DX-68`), same shape as the existing Vulkan pixel-test suite (`tests/`/`examples/vulkan_*_test.cpp`) — clear-color, flat triangle, textured quad, per-effect lighting/fog/alpha-test/dual-texture/env-map/skinned cases | ⬜ | Port the *test methodology*, not literal file copies — each Vulkan pixel test already documents its exact expected-color derivation; redo that derivation for D3D11's own coordinate/clip conventions (verify whether D3D11's clip-space Z range (0..1) needs the same "Z∈[0,1] not [-0.9,0.9]" adjustment Vulkan's own fog tests needed, per `plan_graphics.md` Task 899's notes). |
| DX-82 | State-object tests: blend/depth-stencil/rasterizer, mirroring the existing `Vulkan_BlendState_*`/`Vulkan_DepthStencilState_*` test family | ⬜ | |
| DX-83 | Resize/swap-chain tests: `BackBufferWidth`/`Height` changes, fullscreen toggle (Wine-only verification here; real fullscreen-transition behavior needs the real-Windows checklist, `DX-90`) | ⬜ | |
| DX-84 | `Discriminating power independently verified` pass for at least the first landed pixel test (`git stash`/targeted-mutation methodology, per this project's established convention) — sets the pattern for every later D3D11 test | ⬜ | |
| DX-85 | `scripts/run-wine-dxvk.sh` (or the CTest harness built on top of it) asserts DXVK was actually engaged for the run, not silently a `WineD3D` fallback — reuses `DX-4`'s own verification method (DXVK log file exists and identifies a real DXVK device/adapter line) as an automated check, not just a one-time manual spike | ⬜ | Directly closes the gap the project owner flagged: "pouhé spuštění pod Wine nestačí" (merely running under Wine isn't enough) — without this, the whole D3D11 pixel-test suite could quietly be validating `WineD3D`'s behavior instead of DXVK's (i.e., instead of a real Direct3D-semantics path), and nobody would notice from green CTest output alone. |
| DX-90 | **Real-Windows verification checklist** (cannot be satisfied by Wine+DXVK alone — see "Development environment" above): real DXGI present-mode/tearing behavior; **full** device-lost/removed recovery (`DX-27` only added detection+logging; actually recreating all resources after a real device-removal event on real hardware is verified here); WARP fallback; at least one real driver each from Intel/AMD/NVIDIA if available; D3D11 debug-layer warnings reviewed for anything Wine's DXVK path would have masked (including confirming `DX-21`'s debug-layer-missing fallback path is never silently hit on a machine that should have it); MSVC build (not just MinGW) at least compiles and passes the same test suite | ⬜ | Do not mark Phase DX4–DX9 "done" project-wide from Wine-only results — this checklist is the actual completion gate, matching this project's own "Wine proves the logic, not real-hardware parity" rule for `SDL_RENDERER`. |

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
| DX-101 | `CNA_GRAPHICS_BACKEND=D3D12` CMake wiring (`cna_backend_graphics_d3d12`, links `d3d12`/`dxgi` in addition to `D3DCommon`) — same minimal-link-set discipline as `DX-12`/design decision 3 (`dxguid`/`d3dcompiler` only if actually needed, confirmed via `DX-100`, not assumed to carry over unchanged from D3D11's own confirmed set) — the option flag itself can already exist from `DX-10` | ⬜ | |
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
