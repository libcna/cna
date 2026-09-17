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
| DX12-0004 | Explicit adapter/diagnostics configuration (`CNA_D3D12_ADAPTER` and friends) | ✅ `9624d31ad` |
| DX12-0005 | Direct3D parity corpus built and registered in SDL-free configurations | ✅ `d82676ffa` |
| DX12-0006 | Native-Windows runners: `-Environment`, sharded gtest | ✅ `d82676ffa` |
| DX12-0007 | Win32 present/resize/minimize/churn stress on a real CNA window | ✅ `d82676ffa` |
| DX12-0008 | Interactive CTest runner; fixture link options that fit a disk | ✅ `423898c43`, `d82676ffa` |
| DX12-0009 | Baseline A: the unmodified renderer on WARP | ✅ corpus 153/262, CnaTests 7831/8201 |
| DX12-0010 | DirectX11 behavioural contracts gated in for DirectX12 (173 gates) | ✅ `c45f8bc82` |
| DX12-0011 | DirectX11 reference on the same guest | ✅ corpus 225/264, CnaTests 8036/8205 |
| DX12-0012 | Stock effects: channel expansion, DualTexture black, instanced World, semantic remap | ✅ `cd4d4a2c5` (round C) |
| DX12-0013 | SpriteBatch: sprite3d stages, layerDepth/W, channel expansion, device rasterizer state | ✅ `da6a2b9ba` (round C) |
| DX12-0014 | RenderTarget2D/RenderTargetCube SetData; cube untracks its resources | ✅ `bb4589c8d` (round C) |
| DX12-0015 | Logical back-buffer readback (letterbox) | ✅ `0fbf64513` (round C) |
| DX12-0016 | Device-backed 2D/3D format classification, half-float filtering | ✅ `91d5814d2` (round C) |
| DX12-0017 | Device recovery use-after-free of fallback textures | ✅ `b9b08cd43` (round C) |
| DX12-0018 | Cube and volume byte-transfer hooks; DXT cube blocks read back | ✅ `37bf39899` (round D) |
| DX12-0019 | Back buffer honours `PresentationParameters.DepthStencilFormat` | ✅ `88363e92a` (round D) |
| DX12-0020 | `Present` keeps the game's viewport when it rebinds the back buffer | ✅ `0aca24c94` (round D) |
| DX12-0021 | Unbound stock-effect texture slots bind a defined texture (DX12 + DX11 AlphaTest) | ✅ `3f890b831`, `db4de0e91`, `ece3d8960` (round D) |
| DX12-0022 | Negative `MaxMipLevel` selects the last level (DX11 + DX12) | ✅ `a47afaf99`; DX11 passes, DX12 WARP limitation measured (`minlod_probe`) |
| DX12-0016b | DXT/NormalizedByte volumes refused explicitly, as DX11 | ✅ `d285bb823` |
| DX12-0023 | `GraphicsDevice` unbinds a render target destroyed while bound (generic) | ✅ `3db9d4abf` (Linux proven; Windows round E) |

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

### Implemented (DX12-0004)

As designed above. Measured on the VM (`CnaTests --gtest_filter=D3D12*`, interactive desktop,
`CNA_D3D12_ADAPTER=warp CNA_D3D12_DEBUG_LAYER=1 CNA_D3D12_DRED=1`): **9/9 pass**, debug-layer totals
0/0/0. The default-policy test, run with the variable removed, gets the renderer's refusal — the
VirtualBox adapter answers `0x887A0004` — naming the adapter and pointing at the explicit switch.

---

## DX12-0005 — The Direct3D parity corpus without SDL

`cmake/DirectXParityTests.cmake` lists **265** fixture executables shared by DirectX11 and DirectX12.
With `CNA_ENABLE_SDL=OFF` none of them was registered (WINNATIVE-0015), because
`PixelTestGame.hpp` used SDL to probe for a display. Measured over the sources: 4 call SDL at all
(`MRT` behind `CNA_RENDERER_EASYGL`, `GraphicsDeviceManager_Vsync` behind the same, `RealWindowResize`
and `ViewportResetAfterResize` drive an `SDL_Window`), 26 reached SDL only through the probe, 235 never
include it. A target built without SDL now defines `CNA_EXAMPLES_NO_SDL` and leaves the probe to the
platform's own window creation; the two `SDL_Window` fixtures carry `REQUIRES_SDL`.

For DirectX12 the registration also carried the Wine lane's workarounds unconditionally: a forced
windowless device (`CNA_FORCE_HEADLESS_DEVICE_EXT=DIRECTX12`) and a bash Proton launcher. Both now apply
only when cross-compiling; natively each fixture runs windowed, as DirectX11's do.

Found by the first MSVC build of the corpus (none of it had been compiled by MSVC before):

| Target | Defect | Fix |
|---|---|---|
| `cna_diag_d3d12_swapchain` | includes SDL unconditionally — the one failing target of the first SDL-free DX12 build | built only where SDL exists |
| `rendertarget_first_use_test.cpp` | `constexpr` functions returning `Color`, whose constructor is not constexpr: C3615 on MSVC, silently accepted by GCC | plain `inline` |
| all 262 DX12 fixtures | RelWithDebInfo `/INCREMENTAL` + full PDB ≈ 150 MB each: **the guest's 47 GB free filled to 0.1 GB** (23.7 GB PDB, 17.8 GB ILK), `LNK1180` | `/INCREMENTAL:NO /DEBUG:FASTLINK` for the Direct3D fixture macros on MSVC (PDB ≈ 45 MB, no ILK) |

SDL-free DX12 registers **262** fixtures (265 − `Common`, `Pbr_VertexColor` DX11-only − 2 `REQUIRES_SDL`
+ `DescriptorAllocator` DX12-only … counted by CTest: 262).

## DX12-0006/0008 — Runners

* `win32_run_interactive.ps1 -Environment 'NAME=value',…` — a task on the interactive desktop runs with
  the logged-on user's environment, so without it `CNA_D3D12_ADAPTER=warp` never reached the program.
* `win32_gtest_shards.ps1` — N fresh interactive processes, totals, shards without XML reported as
  broken, debug-layer report lines collected.
* `win32_ctest_interactive.ps1` — a labelled CTest subset on the desktop, JUnit totals.

## DX12-0007 — Win32 present stress, baseline A

`cna_stress_directx12_win32_present --frames 3000`, `CNA_D3D12_ADAPTER=warp CNA_D3D12_DEBUG_LAYER=1`,
renderer at `d82676ffa` (no parity fixes):

| Measure | Result |
|---|---|
| frames / OS resizes (`SetWindowPos`) / minimize–restore cycles | 3 001 / 130 / 20 |
| swap-chain back buffer equals client size after each resize | **130/130** |
| render-target churn (create, draw, `GetData`, dispose) | **120/120** exact |
| process handles, warm → end | 289 → 283 (−6) |
| private bytes, warm → end | 25 → 29 MB (+3) |
| D3D12 debug layer | **0 corruption, 0 error, 0 warning** |
| RTV / SRV descriptors live at end | 2 / 1 (peaks 3 / 2) |
| back-buffer readback while the window is smaller than the logical back buffer | **fails: zeros** (26 checks) |
| mipmapped 19×7 `Texture2D` `SetData`→`GetData` | **fails every churn** (120) — under investigation |

A harness defect of the stress program's own, found on its first run: it used the Reach profile, which
forbids a mipmapped non-power-of-two render target. Fixed before any number above was taken.

---

## DX12-0009 — Baseline A: the renderer as found, on WARP

Renderer code at `423898c43`: nothing changed in DX12 rendering except adapter selection and the opt-in
diagnostics. `CNA_D3D12_ADAPTER=warp`, interactive desktop, windowed swap chains.

### Direct3D parity corpus

`win32_ctest_interactive.ps1 -Label DIRECTX12 -Parallel 3`: **262 fixtures — 153 passed, 109 failed,
0 skipped** (89 s). Report: `C:\cna\report\dx12\parity-a`.

The failures cluster by family rather than by feature — every BasicEffect, AlphaTestEffect,
DualTextureEffect, EnvironmentMapEffect and SkinnedEffect pixel fixture, and most SpriteBatch and
SpriteFont fixtures, read back the clear colour. The debug layer reports nothing for them. Isolated:

| Run of the same executable | `BasicEffect_VertexColorEnabled` | `SpriteBatch_Scale` |
|---|---|---|
| windowed (swap chain) | clear colour, FAIL | clear colour, FAIL |
| `CNA_FORCE_HEADLESS_DEVICE_EXT=DIRECTX12` (implicit off-screen back buffer) | **(160,40,30), PASS** | **PASS** |

So the draws are right and the **windowed** path is not. Every DX12 run before this workstream was
windowless (the Wine lane forced it), which is why nothing had ever measured it. The fixture asks for a
64×64 back buffer; the swap chain is the window's physical size and the 64×64 logical buffer is presented
letterboxed inside it, exactly as `GetDefaultViewportRect()` places every draw. `ReadBackbuffer` reads
**physical** `(x, y)` — the letterbox bar. This is DirectX11's WINCLOSE-0012 defect, which DX11 fixed by
sampling the presentation geometry at logical pixel centres; DX12 never received it. The Win32 present
stress measured the same defect independently (zeros whenever the window is smaller than the logical
buffer). Fix: DX12-0015.

Failures not explained by that (to be re-measured after DX12-0015, against the DX11 reference):
`ContextRecovery_Model` (**SegFault**), `SkinnedEffect_WorldNormal` (non-uniform bone scale, 90 vs 212),
`TextureFilterMipContract` (negative `MaxMipLevel`), `DescriptorCapacityContract` (sampler states),
`Deferred_Scissor` E2/E3 (a scissor outside the target raises), `RasterizerState_CullModeCamera`
(scenario setup), `AlphaTestEffect_NullTexture`, `DrawRangeValidation`, `SpriteFont_Properties`,
`Resource_PresentLifecycle` (expects MinGW's exit code 3; MSVC's abort is `0xC0000409`),
`RenderTarget_SurfaceFormat`, `SurfaceFormat_Throws`, `Backbuffer_PassOrder` (Reach profile),
`RenderTarget2D_MipComplete`, `MRT`, `RenderTargetCube_SampleAfterUnbind`, `Resource_BoundTargetLifetime`.

### CnaTests

`win32_gtest_shards.ps1 -Shards 27 -Parallel 3`, `CNA_D3D12_ADAPTER=warp`: **8 201 tests — 7 831 passed,
32 failed, 338 skipped, 0 disabled, 0 broken shards** (177 s). Report: `C:\cna\report\dx12\cnatests-a`.

At this commit 173 gates still excluded DirectX12, so most pixel contracts reported SKIPPED rather than
running; DX12-0010 is what exposes them (baseline B). The 32:

| Class | Tests |
|---|---|
| WINNATIVE-F26, deliberately red on Windows | `CaseInsensitivePathTest` ×2 |
| cube transfers (float, normalized, DXT, generic windows) | `TextureCubeTest` ×7, `ClassicTextureFormat` cube ×4, `Texture3DTextureCubeContentTypeReaderTest` ×2 |
| volume transfers | `Texture3DTest` ×2 |
| format refusals DirectX12 does not make | `UnsupportedFormatConstructionTest` ×4 |
| half-float linear filtering capability | `ClassicTextureFormat.HalfVector4PhysicalLinearCapabilityDoesNotBypassXnaRestriction` |
| channel expansion (point sampling) | `ClassicTextureFormat.PointSamplingExpandsChannelsAndPreservesDeclaredRanges` |
| refused declarations still rasterize | `DeclarationGuardTest` ×4 |
| instanced World, duplicate semantics, multi-stream offsets | `InstancedVertexColorTest`, `InstancedDrawMultiStreamTest`, `OrdinaryDrawBindingOffsetTest` |
| HDR target partial/mip transfers | `HdrRenderTargetRoundTripTest.FloatTargetPartialAndMipTransfersKeepExactTypedValues` |

### Tooling installed for diagnosis

cdb (Debugging Tools for Windows, `winsdksetup.exe` 10.1.26100 `/features OptionId.WindowsDesktopDebuggers`,
through a SYSTEM task) — needed to locate the DX12-0017 crash, which printed nothing: `cdb -g -G -lines -y
<build> -c ".ecxr;kn 30;q" <exe>` against the FASTLINK PDBs gives a full source-line stack.

---

## DX12-0011 — DirectX11 reference, same guest, same harness

DX11 tree `full-win32-d3d11-nosdl` at `423898c43` (same harness as DX12 baseline A), guest power-cycled
first. The VBoxSVGA adapter, so this is **virtual-GPU** evidence, not WARP.

| Suite | Result |
|---|---|
| Direct3D parity corpus (`-L DIRECTX11`, its first SDL-free native run too) | **264 — 225 passed, 39 failed, 0 skipped** (75 s) |
| CnaTests, 27 shards | **8 205 — 8 036 passed, 5 failed, 164 skipped, 0 broken** (248 s) |

The 5 CnaTests failures: `CaseInsensitivePathTest` ×2 (WINNATIVE-F26), the two VBoxSVGA cube defects
(`RenderTargetSemantics.EachRenderTargetCubeFaceKeepsItsOwnContent`,
`NormalizedRenderTargetRoundTrip.RenderedCubeMipsResolveAndSampleWithoutRgba8Substitution`, both reproduced
with raw D3D11 in WINCLOSE rounds), and `MediaLibrarySavePictureTest.SavePictureFromBufferCreatesARealReadablePicture`
(not in round 7; not graphics; recorded, not investigated here).

### Corpus failures compared, DX12 baseline A against DX11

| Set | Count | Fixtures |
|---|---|---|
| fail on both D3D renderers | 26 | AlphaTestEffect_NullTexture, Backbuffer_PassOrder, CompressedTexture_StorageContract, CubeVolume_GetDataContract, CubeVolume_SetDataContract, Deferred_Scissor, DepthStencilState_StencilTwoSided, DrawRangeValidation, Dxt1_FromStream, GraphicsAdapterQueryContract, GraphicsDevice_OrderedClear, MsaaChange, RenderTargetCube_DepthFormat, RenderTarget_ActiveMsaaReadback, RenderTarget_SurfaceFormat, RendererCapabilityTruth, Resource_BoundTargetLifetime, Resource_PresentLifecycle, SamplerLodAddressWContract, ShaderEffect_ReflectionContract, SkinnedEffect_BoneDeformation, SkinnedEffect_WorldNormal, SpriteFont_Properties, SurfaceFormat_Throws, TextureFilterMipContract, ViewSpaceFog |
| DX12 only | 83 | the letterbox class (all effect families, SpriteBatch/SpriteFont), plus ContextRecovery_Model (DX12-0017), DepthBias, DescriptorCapacityContract, RasterizerState_CullModeCamera/IndexedBasicEffect, RenderTarget_MsaaDepthContract, SkinnedEffect_Vector4BoneIndices, SourceRectangleOrientation, MRT, RenderTargetCube_SampleAfterUnbind, RenderTarget2D_MipComplete, GraphicsDevice_DepthContract, SpriteBatch_RenderTargetSize, … |
| DX11 only | 13 | ColorSpace_MidTone, EnvMapCubeSamplerContract, PresentationFormatContract, PresentationModeContract, RenderTargetCube_GetDataContract, RenderTargetCube_MsaaFace, RenderTargetCube_PluralBinding, RenderTargetCube_Usage, RenderTarget_DepthStencilUsage, RenderTarget_EffectSource, RenderTarget_PassBoundary, Smoke, SurfaceFormat_StorageContract |

A fixture failing on both D3D renderers is not, by that fact alone, a DX12 parity gap: it is the shared
layer, the harness, or a contract both D3D renderers miss. Each is triaged on its own before anything
changes.

---

## DX12-0010 — Baseline B: DirectX11's contracts run on DirectX12

Renderer unchanged from baseline A; `c45f8bc82` only removes the 173 test gates that excluded DirectX12.
CnaTests, 27 shards, `CNA_D3D12_ADAPTER=warp`: **8 201 — 7 969 passed, 64 failed, 168 skipped, 0 broken**
(186 s). Report: `C:\cna\report\dx12\cnatests-b`. The 170 fewer skips are the contracts now running;
32 of them fail. By family: SpriteBatch depth/W/layerDepth (8, DX12-0013), back-buffer depth format (6,
DX12-0019), DualTexture null sampler (4) and instancing/semantic remap (3) (DX12-0012), `StateEnumFallback`/
`StateNumericFallback` (7, device rasterizer and sampler state reaching SpriteBatch, DX12-0013),
RenderTarget2D/Cube SetData and typed transfers (6, DX12-0014), cube/volume transfers (baseline A's,
DX12-0018), `PresentationRectangleTest.ALetterboxedDefaultViewportIsNotACustomSubViewport` (DX12-0015),
`NonBlendableTargetsRejectBlendAndAllColorMasks` (open).

## Round C — after DX12-0012…0017

Tree at `b9b08cd43`, `CNA_D3D12_ADAPTER=warp`.

| Suite | Result | Report |
|---|---|---|
| Direct3D parity corpus | **262 — 232 passed, 30 failed** (72 s; baseline A 153) | `C:\cna\report\dx12\parity-c` |
| CnaTests, 27 shards | **8 201 — 8 003 passed, 31 failed, 167 skipped, 0 broken** (184 s; baseline B 7 969) | `C:\cna\report\dx12\cnatests-c` |

Corpus: 28 of the 30 also fail on the DirectX11 reference (DX12-0011). The two DirectX12-only ones:
`AdditiveBlendContract` (a viewport reset after `Present`, found by this round because the letterbox fix
let frame 3 be read at all → DX12-0020) and `DescriptorCapacityContract` (point-sampled 2×2→2×2 boundary
texels; open, to be checked against raw D3D12 on WARP before CNA is touched). `RenderTargetCube_GetDataContract`
and `PresentationModeContract` moved from DX11-only to shared: the first because its reviewed DirectX12
row predates DX12-0014 (test row updated, `d5672efb5`), the second because DirectX12 now reads the logical
back buffer as DirectX11 does and the fixture expects the physical one — a contract question for both
renderers, not a DX12 gap. Among the shared ones two were CNA defects in both D3D renderers, measured
against the XNA-derived fixtures: `AlphaTestEffect_NullTexture` (DX12-0021) and `TextureFilterMipContract`
L3/L9 (DX12-0022).

CnaTests: the 31 are cube and volume transfers — `TextureCubeTest` ×8, `Texture3DTest` ×2, the content
readers ×6 and the float/normalized cube and volume `ClassicTextureFormat` rows ×8 (24, DX12-0018) —
back-buffer depth format (4, DX12-0019), `CaseInsensitivePathTest` ×2 (WINNATIVE-F26) and
`NonBlendableTargetsRejectBlendAndAllColorMasks` (open).

## Round D — after DX12-0018…0022

Tree at `d5672efb5`, guest up since before round C (not power-cycled).

| Suite | Result | Report |
|---|---|---|
| DX12 corpus, WARP | **262 — 233 passed, 29 failed** (63 s; round C 232) | `C:\cna\report\dx12\parity-d` |
| DX12 CnaTests, WARP, 27 shards | **8 201 — 8 028 passed, 6 failed, 167 skipped, 0 broken** (155 s; round C 8 003) | `C:\cna\report\dx12\cnatests-d` |
| DX11 corpus (regression) | **264 — 225 passed, 39 failed** (150 s; reference 225) | `C:\cna\report\dx11\parity-d` |

The first CnaTests attempt of this round is void: piping the runner into `Select-Object -First 8` stops the
PowerShell pipeline, which killed the shard jobs after eight lines. The guest-side wrappers now write to a
log and print the summary afterwards.

Corpus, DirectX12: fixed since round C — `AdditiveBlendContract` (DX12-0020), `AlphaTestEffect_NullTexture`
(DX12-0021), `RenderTargetCube_GetDataContract` (test row, `d5672efb5`). Newly failing, each traced:

- `AlphaTest_Fog` — the fixture set no texture and expected the material colour, i.e. the retired
  white-null convention. Microsoft XNA samples a null AlphaTestEffect texture as opaque black
  (SOFTWARE-303, measured; `AlphaTestEffect.cpp` has requested it since 2026-09-10). DirectX12 read exactly
  XNA's values: (0,0,0) at z=0 and half the fog colour, (13,76,115), at z=0.45. Fixture corrected to bind a
  1×1 white texture (`ece3d8960`) — its subject is fog. DirectX11 failed it identically in round D.
- `PresentationFormatContract` — asks for Depth16 and demands the old fixed D24S8 back buffer (DX-213).
  DirectX11 honours the request since WINCLOSE-0012, as the gated `BackBufferDepthStencilContractTest`
  requires, and failed this fixture in the reference run already; DirectX12 now does the same (DX12-0019).
  **Shared contract conflict, recorded, not resolved here.** Its store-only leg also shows a real shared
  inconsistency: `SetPresentationParameters(None)` reports None while the Depth16 surface stays bound,
  because `GetAppliedDepthStencilFormatEXT` echoes the request (it must, since `Reset` normalizes before it
  applies). Both D3D renderers behave identically.
- `TextureFilterMipContract` L3/L9 were not fixed on WARP by DX12-0022 although DirectX11 passes them with
  the identical mapping. `spikes/d3d12-warp-spike/minlod_probe.cpp` (raw Direct3D 11, no CNA): the
  VirtualBox driver selects the last level for MinLOD 14…FLT_MAX; WARP for 14…1e6 but **level 0 for 1e30
  and FLT_MAX**. A WARP rasterizer limitation; CNA is not adapted to it. Physical-GPU checklist item.

CnaTests, DirectX12, the six: `CaseInsensitivePathTest` ×2 (WINNATIVE-F26);
`ClassicTextureFormat.HiDefVolumeFormatsHaveAnExplicitCompleteRendererContract` (DX12-0016 had not copied
DirectX11's explicit refusals → `d285bb823`); `GltfRendererPbrFallbackPolicy.EveryPbrMapReachesTheShaderBindingIntendedByItsRenderer`
(a source-text audit of the PBR slot line DX12-0021 had rewritten → restored, `db4de0e91`);
`NonBlendableTargetsRejectBlendAndAllColorMasks` (→ DX12-0023); `AudioEngineTest.DisposeRaisesDisposingEvent`
("Could not find file …\Temp\cna_audio_engine_test\fixture.xgs": two shards share one fixed %TEMP%
directory; a test-isolation race of the parallel harness, not graphics).

Corpus, DirectX11 regression: `AlphaTestEffect_NullTexture` and `TextureFilterMipContract` now pass
(DX12-0021, DX12-0022 are shared fixes); `AlphaTest_Fog` fails as above; `RenderTarget_MsaaMipReadback` B4
failed three times in a row on the long-running guest and passed twice straight after a power cycle, with
no DirectX11 code on this branch reaching it — VirtualBox driver state (the F24/F29 class), recorded.

### DX12-0023 — a render target destroyed while bound

`NonBlendableTargetsRejectBlendAndAllColorMasks` passed for its first format and threw "no off-screen
color target bound" for the other five; `Resource_BoundTargetLifetime` M1 stopped at round 2 with the same
message. Each loop iteration's `RenderTarget2D` is destroyed while bound and the next one is built at the
same address. `RenderTarget2D::Dispose` refuses a bound target but its destructor cannot, and
`GraphicsDevice` never dropped the binding: `GetRenderTargets()` returned a dangling pointer and, since
SOFTWARE-222 (2026-09-09) made `SetRenderTargets` return early for an address-identical binding set,
binding the replacement was a no-op. DirectX12's backend detach (DX-233) leaves no target, so the next draw
threw; DirectX11, EasyGL and Software silently drew into the back buffer. DX-233 had measured M1 green on
2026-09-07, two days before SOFTWARE-222.

Why it was missed: every renderer-level test of DX-233 destroyed the target and then checked the renderer,
and M1 checks no pixels of its rounds. The regression test reuses one `std::optional`'s storage so the
address reuse is certain; it fails without the fix on OpenGL33 (EasyGL: binding set not emptied, viewport
16 instead of 800, replacement never cleared) and Software, and passes with it on OpenGL33, Software and
Headless (`cmake-build-multi`, Xvfb `:99`; 470/470 of the render-target/device/texture groups on OpenGL33).

### Stock-effect null textures: a shared question left open

SOFTWARE-303 (2026-09-10) measured Microsoft XNA sampling a null classic stock-effect texture as opaque
black and changed EasyGL/Software to that for every classic effect. DirectX11 still binds opaque white for
SkinnedEffect and BasicEffect (GLTF-386, pinned by
`GltfRendererPbrFallbackPolicy.DirectX11SkinnedEffectUsesOpaqueWhiteForMissingTexture`, for untextured glTF
skins). DirectX12 follows DirectX11 there (parity), and XNA's black for AlphaTestEffect, DualTextureEffect
and EnvironmentMapEffect where no such pin exists. Resolving SkinnedEffect/BasicEffect needs the glTF
importer to bind its own white texture first — outside this workstream, recorded for the owner.

