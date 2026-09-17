# DirectX 12 parity, validation and hardening — WARP-backed

Living implementation and evidence ledger for the DirectX 12 renderer workstream. Task IDs are
durable: `DX12-0001`, `DX12-0002`, … . Every row names the evidence it rests on.

**Evidence classes — never merged.**

1. **Native Windows API/runtime validation** — real Win32, real MSVC ABI, real D3D12 runtime.
2. **DirectX 12 WARP validation** — the D3D12 runtime executing on the Microsoft Basic Render Driver
   (WARP), a software rasteriser. It exercises CNA's D3D12 command recording, barriers, descriptors,
   PSOs, shaders, uploads and readbacks against Microsoft's reference-quality implementation.
3. **Physical Windows GPU validation** — a real D3D12-capable GPU and vendor driver.

The `win10_local` VirtualBox guest proves **1 and 2**. It proves nothing about **3**. A WARP pass is
never reported as GPU validation, and the VirtualBox adapter's `DXGI_ERROR_UNSUPPORTED` is never
reported as a CNA defect.

---

## Status summary

| ID | Task | Status |
|---|---|---|
| DX12-0001 | Phase 0 baseline and environment record | ✅ |
| DX12-0002 | Phase 1/2 audit of the DX12 renderer and DX11 parity inventory | ✅ (inventory is refined as runs land) |
| DX12-0003 | Raw D3D12 WARP probe outside CNA (`spikes/d3d12-warp-spike/`) | ✅ |
| DX12-0004 | Explicit adapter/diagnostics configuration (`CNA_D3D12_ADAPTER` and friends) | in progress |

---

## DX12-0001 — Baseline

| Fact | Value |
|---|---|
| CNA baseline | `c2721f86ed72c90e78c8ff4afd51b452ac039cfe` (= local `next` = `origin/next` on 2026-09-17) |
| sharp-runtime | `de3604d9728ed297b73b08734da141de0b3100bb` (branch `next`) |
| Feature branch | `directx12-parity` |
| VM | `win10_local` (VirtualBox, 8 vCPU, 8 GB), reached with `tools/platform/windows_vm_exec.sh` |
| Windows | Windows 10 Home 22H2, build 19045.6466 |
| MSVC | cl 19.44.35229, VC tools 14.44.35207 (VS Build Tools 2022, `C:\BuildTools`) |
| Windows SDK | 10.0.26100.0 |
| D3D12 runtime | `D3D12.dll` / `D3D12Core.dll` 10.0.19041.5794 (in-box, no Agility SDK) |
| Debug layer | `d3d12SDKLayers.dll` — **not present at baseline**; installed 2026-09-17 as the `Tools.Graphics.DirectX~~~~0.0.1.0` capability (see below) |

**Installing the debug layer.** `Add-WindowsCapability` returns *Access is denied* from the SSH
session although that token is elevated (a network logon cannot use the Windows Update client). The
same command succeeds from a one-shot scheduled task running as `SYSTEM` (48 s). Recorded because the
next person to need a Feature on Demand in this guest will hit the same wall.

### DXGI adapters (measured by `warp_probe`, DX12-0003)

| # | Description | Vendor / Device | Memory | Software | D3D12 at FL 11_0 |
|---|---|---|---|---|---|
| 0 | VirtualBox Graphics Adapter (WDDM) | 0x80EE / 0xBEEF, subsys 0x040515AD | 2303 MB dedicated video, 2048 MB shared | no | **unsupported, `0x887A0004`** |
| 1 | Microsoft Basic Render Driver | 0x1414 / 0x008C | 4095 MB shared | yes | supported |
| WARP (`EnumWarpAdapter`) | Microsoft Basic Render Driver | 0x1414 / 0x008C | 4095 MB shared | yes | supported |

### WARP device facts

| Feature | Value |
|---|---|
| Max feature level | 12_1 |
| Highest shader model | 6.2 |
| Root signature | 1.1 |
| Resource binding / tiled resources / conservative raster tiers | 3 / 3 / 3 |
| OutputMergerLogicOp | yes |
| `R8G8B8A8_UNORM` MSAA sample counts | 2, 4, 8 |
| `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT` (SDK header) | 256 |
| `D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT` (SDK header) | 512 |
| `D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT` | 256 |
| Default `ID3D12InfoQueue` break-on-severity | corruption 0, error 0, warning 0 |

---

## DX12-0002 — Audit of the existing renderer

Read in full before any change: `DirectX12Renderer.{hpp,cpp}` (5 167 lines), `D3D12Textures`,
`D3D12ResourceStateTracker`, the render-target, cube, buffer, sprite, descriptor and PSO sources, and
the DirectX11 counterparts plus the WINCLOSE-0013…0036 diffs that changed DX11 after the Windows
closeout.

### Architecture as found

* **Adapter selection** — `EnumAdapters1` walk, skip `DXGI_ADAPTER_FLAG_SOFTWARE`, take the *first*
  remaining adapter, then `D3D12CreateDevice` at 12_1 → 11_0. Two defects in policy, not only in
  reach:
  1. if no hardware adapter exists the chosen pointer stays null, and `D3D12CreateDevice(nullptr, …)`
     means *the default adapter* — which on a machine whose only adapter is the Microsoft Basic
     Render Driver is software. The production path could therefore silently land on a software
     rasteriser, which is exactly what this workstream must never allow;
  2. only the first hardware adapter is tried — a machine whose adapter 0 lacks D3D12 but whose
     adapter 1 has it gets no device.
* **Debug layer** — `D3D12GetDebugInterface` + `EnableDebugLayer` + `DXGI_CREATE_FACTORY_DEBUG`
  **unconditionally, in every build type**, whenever the SDK layers are installed. On any developer
  machine with Graphics Tools this puts every Release game on the validation layer. It is absent on
  the VM only because the layer was not installed.
* **No WARP path**, no DRED, no info-queue capture, no live-object report.
* **Frames** — two frame slots (`kFramesInFlight = 2`), one allocator + list + constant arena +
  persistently mapped upload ring + retained-object set per slot; a single shared fence; an immediate
  list for synchronous readbacks (`BeginImmediateCommandsEXT` submits the open frame first).
* **Barriers** — one `D3D12ResourceStateTracker` (whole-resource state per `ID3D12Resource*`);
  resources register at creation and transition through it.
* **Descriptors** — REMED-GFX-177 allocators with fence-stamped deferred reuse and growth.
* **Swap chain** — `FLIP_DISCARD`, 2 buffers, `R8G8B8A8_UNORM`, tearing when supported; resize drains
  (`WaitForGpuIdle`), releases the window-size group and calls `ResizeBuffers`. Without a swap chain
  (windowless or failed) an implicit off-screen back buffer is used.

### DX11 fixes after the Windows closeout, audited against DX12

| DX11 fix | Required behaviour | DX12 as found |
|---|---|---|
| WINCLOSE-0014 SpriteBatch depth/W/near clip, float UVs, 16-bit wrap | FNA `MatrixTransform`, `layerDepth` reaches depth | `Sprite2d` variant; `layerDepth` parameter ignored — **audit on WARP** |
| WINCLOSE-0015 Position-only BasicEffect | draws in DiffuseColor | ✅ shared commit |
| WINCLOSE-0016 packed format device verification | refuse what the device will not store | device-independent classification — **audit on WARP** |
| WINCLOSE-0017 `RenderTargetCube.SetData` | stores the face | **missing** (no `SetData`/byte hooks on the cube RT) |
| WINCLOSE-0018 DualTexture unbound slot | opaque black | **white fallback** |
| WINCLOSE-0019/0026 channel expansion | (R,1,1,1) / (R,G,1,1) | shaders shared, constants left at identity — **missing** |
| WINCLOSE-0022 COLOR0 saturation | saturate unlit COLOR0 | ✅ shared shaders |
| WINCLOSE-0023 half-float linear filtering | ask the device | to verify |
| WINCLOSE-0025 `RenderTarget2D.SetData` | stores the data | **missing** (`UpdatePixels`/`UpdatePixelsLevel` not overridden) |
| WINCLOSE-0030 HLSL dialect | declared | ✅ |
| WINCLOSE-0033 duplicate semantics across streams | effective usage index | **missing** (declared index used) |
| WINCLOSE-0034 instanced `Effect.World` | instance · World · View · Projection | **missing** (View · Projection) |

---

## DX12-0003 — Raw D3D12 WARP probe (no CNA)

`spikes/d3d12-warp-spike/warp_probe.cpp`. Enumerates adapters, creates the WARP device by
`EnumWarpAdapter` (never by name), creates every object the renderer creates at construction, clears
and reads back through `GetCopyableFootprints`, and optionally runs a flip-model HWND swap chain with a
`ResizeBuffers` every 50 frames under the debug layer, DXGI debug factory, DRED and GPU-based
validation, ending with `IDXGIDebug::ReportLiveObjects` captured through `IDXGIInfoQueue`.

| Run (interactive desktop, session 1) | Result |
|---|---|
| device + objects + 5×3 clear/readback | PASS — `64,128,191,255` in every pixel, row pitch 256 |
| `--hardware` control | `D3D12CreateDevice` → `0x887A0004`: environment |
| session 0 (SSH) `--swapchain` | `CreateSwapChainForHwnd` → `0x887A0022` (`NOT_CURRENTLY_AVAILABLE`): no desktop, expected |
| `--swapchain 300`, no layers | PASS — 300 frames, 6 resizes, ~5 000 fps |
| `--debug --dxgi-debug --dred --swapchain 3000` | **PASS — 3 000 frames, 60 resizes, 0 debug messages, 0 live objects** |
| `--gbv --dxgi-debug --swapchain 300` | PASS — 0 messages besides the GBV startup notice |

**A harness defect the debug layer found in the probe itself.** The first swap-chain version released
its back buffers for `ResizeBuffers` straight after `Present`. The fence it had waited on covered the
command list, not the `Present` queued after it, so the buffers were still referenced by in-flight
queue work. The debug layer reported **ID 921 `OBJECT_DELETED_WHILE_STILL_IN_USE`** and terminated the
process with exception `0x87D` (exit code 2173, no output) — regardless of the break-on-severity
settings, which are all off by default. It was isolated by a vectored exception handler that drains the
info queue at the moment of the raise. Two lessons carried into CNA:

* the invariant CNA's resize must hold is *signal a fresh fence after Present and wait*, not *wait for
  the last command list*;
* under the debug layer a use-after-final-release does not produce a test failure, it kills the
  process silently; a validation harness has to capture the message at the raise.

Also recorded: `ClearRenderTargetView` against a resource created without an optimized clear value
emits warning ID 820 (`CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE`), 821 for depth. Swap-chain buffers
have no clear value and XNA clears to arbitrary colours, so these are classified **performance hints,
not defects**.

---

## DX12-0004 — Explicit adapter and diagnostics selection (design)

**Mechanism.** An internal `D3D12Configuration`, parsed once per renderer from the process
environment by a pure, unit-testable parser — the same shape as `GdiConfiguration`
(`modules/renderers/gdi`). No XNA-visible API and no public CNA API is added.

| Variable | Values | Default | Effect |
|---|---|---|---|
| `CNA_D3D12_ADAPTER` | `hardware`, `warp` | `hardware` | `hardware`: every non-software DXGI adapter, high-performance order, first that creates a D3D12 device. `warp`: `IDXGIFactory4::EnumWarpAdapter`. |
| `CNA_D3D12_DEBUG_LAYER` | `0`, `1` | `0` | `ID3D12Debug::EnableDebugLayer`, DXGI debug factory, info-queue capture |
| `CNA_D3D12_GPU_VALIDATION` | `0`, `1` | `0` | GPU-based validation (implies the debug layer) |
| `CNA_D3D12_DRED` | `0`, `1` | `0` | DRED auto-breadcrumbs and page-fault reporting before device creation |

**Invariants.**

* Hardware stays the default. **WARP is never a fallback**: when no hardware adapter creates a device
  the renderer throws, naming each adapter and its HRESULT, and saying that `CNA_D3D12_ADAPTER=warp`
  exists for validation. `D3D12CreateDevice` is never called with a null adapter.
* An invalid value keeps the safe default for that setting and is reported once, like GDI's.
* The chosen adapter (description, vendor/device/subsystem/revision, memory, software flag, how it was
  selected, feature level) is logged at device creation and exposed to tests.
* The debug layer is opt-in in every build type — the unconditional enable found by the audit is
  removed.
