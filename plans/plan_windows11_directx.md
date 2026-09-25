# Physical Windows 11 and DirectX bring-up

Status: native MSVC and physical Intel GPU baseline established on 2026-09-25. The
Windows DirectX parity campaign is active on `win11-directx-modern`. DX11 and DX12
classic normal-validation gates are complete; DX11 modern CNAEXT evaluation is active.
The large DX12 descriptor fixture under Intel GPU-based validation remains a recorded
exception (WIN11-0025). Modern parity and application validation remain open.

## Stable source and machine baseline

| Item | Measured value |
|---|---|
| CNA source | `C:\rv\src\cna`, `win11-directx-modern` from `origin/next` at `96b5038de131be2dee1759153f8a432cfd94fc81` |
| Companion dependency | `C:\rv\src\sharp-runtime`, `next` at `41b918c97ed47288f87a0af176fe23942af24cdd`; cloned because CNA `next` requires its `Resources` component; not modified |
| Machine | HP EliteBook 840 14 inch G9 Notebook PC, Windows 11 Pro x64, build 22631 |
| CPU and RAM | Intel Core i7-1260P, 12 cores / 16 logical processors; 16,802,623,488 physical bytes |
| Disk | C: 401,594,327,040 bytes free before builds (~374 GiB) |
| Physical GPU | Intel Iris Xe, PCI `8086:46A6`, driver `32.0.101.7085`, WDDM 3.1, Direct3D DDI 12, feature levels through 12_1 |
| Software adapter | Microsoft Basic Render Driver, PCI `1414:008C`, DXGI software flag set; excluded from hardware claims |
| Toolchain | Visual Studio Community 2022 17.13.5, MSVC v143 19.43.34809, Windows SDK 10.0.22621.0, CMake 4.4.3, Visual Studio 17 2022 generator, x64 Debug |
| Optional tools | Ninja absent and not required; Python 3.14.7; MSVC AddressSanitizer compiled and ran a probe with `/fsanitize=address` |
| DirectX development | SDK D3D11, D3D12, DXGI headers and libraries present; `dxc.exe` present; D3D11 and D3D12 debug interfaces and debug device creation succeeded |

`dxdiag` is saved at `C:\rv\logs\dxdiag.txt`. An independent native DXGI/D3D probe at
`C:\rv\logs\dx_device_probe.txt` found both device LUIDs equal to the Intel adapter LUID:
D3D11 feature level 11_1 and D3D12 creation at 12_1. Integrated video memory is shared;
the WMI `AdapterRAM` value is not used as a capacity claim.

All builds use `CNA_PLATFORM=WIN32`, `CNA_AUDIO_PLATFORM=NULL`, `CNA_ENABLE_SDL=OFF`,
`CNA_ENABLE_VIDEO=OFF`, `CNA_ENABLE_FONT_PIPELINE=OFF`, `CNA_ENABLE_NET=OFF`,
`CNA_BUILD_EXAMPLES=OFF`, `CNA_BUILD_TESTS=ON`, and `CNA_USE_CCACHE=OFF`. The selected
renderer is compile-time `CNA_GRAPHICS_RENDERER=HEADLESS`, `DIRECTX11`, or `DIRECTX12`.
The core tree uses the default Draco setting; both DirectX trees set `CNA_ENABLE_DRACO=OFF`.
Build trees are `C:\rv\build\cna-win11-{msvc,dx11,dx12}-debug`. This is the supported
SDL-free native Win32 configuration; SDL3 is otherwise the repository default platform.
Compilation used at most four jobs per build initially because of the 16 GB RAM limit.
The native configure form for each tree was `cmake -S C:\rv\src\cna -B <tree>
-G "Visual Studio 17 2022" -A x64 -T v143` plus the options above; build with
`cmake --build <tree> --config Debug --parallel 4`. The full core tree and selected
DirectX smoke targets were built, rather than the entire ~265-executable parity corpus
in each DirectX tree.

All GPU and window tests were launched inside a private `WinSta0` desktop using the native
Windows launcher at `C:\rv\work\private_desktop.exe`; they were not displayed on the
owner's live desktop. No WARP fallback was permitted. The launcher is an external test
artifact and is not part of CNA.

## Completed bring-up tasks

| ID | Result |
|---|---|
| WIN11-0001 | Inventoried physical Windows, VS/MSVC, SDK, disk, memory, GPU, DirectX debug support, and ASan. |
| WIN11-0002 | Established clean CNA baseline on `win11-directx-modern`; configured repository-local Robert Vokac Git identity and Windows-safe Git settings; no merge, rebase, push, or change to `next`. |
| WIN11-0003 | Configured and built the full HEADLESS/Win32 Debug tree with MSVC x64. Fixed the headless test target's missing `CNA_EXAMPLES_NO_SDL` definition when SDL is disabled. |
| WIN11-0004 | Raised Google Test discovery timeout from five to 60 seconds for the large Windows suite; `ctest -N` now registers 8,521 core tests. |
| WIN11-0005 | Ran 39 Win32Window/Win32Platform Google Tests on the private desktop; all passed, including real HWND creation, events, resize, close, and shutdown. |
| WIN11-0006 | Built and ran DirectX 11 on physical Intel GPU with debug layer. New repeatable Win32 smoke verifies hardware adapter, swap chain, clear, sprite draw, Present loop, resize, and zero debug errors; one existing native smoke check and post-resize pixel placement remain defects below. |
| WIN11-0007 | Built and ran DirectX 12 on physical Intel GPU with debug layer and DRED. Windowless smoke passed 25/25 checks. Win32 presentation stress passed 221 frames, 10 resizes, a minimize, clear/draw/Present, and zero debug warnings or errors. GPU-based validation was disabled for this initial smoke. |
| WIN11-0008 | Recorded the bounded Windows test baseline and deferred work below; no samples or broad sharp-runtime work started. |
| WIN11-0009 | Reclassified the DX11 smoke C6 failure as a test setup defect: `GraphicsDeviceManager` defaults to `Depth24`, whose stencil is intentionally hidden by the renderer. The native binding check now requests `Depth24Stencil8`, then verifies the cached object and dynamic reference; 21/21 checks pass on Intel. |
| WIN11-0010 | Fixed DX11 swap-chain resize restoring a full physical viewport instead of the current presentation rectangle. At 497x301 physical / 320x240 logical, the viewport now restores to (48,0,401,301); a 32x32 sprite at (8,8) reads back as exactly 1024 red pixels within x=8..39, y=8..39. Private-desktop Intel run passes with zero D3D11 debug corruption/errors/warnings. |

## Initial test results

| Configuration and suite | Registered / selected | Pass | Fail | Skip | Timeout | Not built | Notes |
|---|---:|---:|---:|---:|---:|---:|---|
| HEADLESS CTest inventory | 8,521 | n/a | n/a | n/a | n/a | n/a | Inventory only; full suite deliberately not run |
| HEADLESS `ColorTest.*` | 61 selected | 61 | 0 | 0 | 0 | 0 | CPU-only CTest |
| HEADLESS `Vector2Test.*` | 79 selected | 79 | 0 | 0 | 0 | 0 | CPU-only CTest, four concurrent jobs |
| Win32Window/Win32Platform Google Tests | 39 selected | 39 | 0 | 0 | 0 | 0 | Private desktop; direct `CnaTests.exe` invocation |
| DIRECTX11 CTest inventory | 422 | n/a | n/a | n/a | n/a | n/a | Most renderer parity targets were not built in this phase |
| `DirectX11_Smoke` | 1 | 0 | 1 | 0 | 0 | 0 | 20/21 internal checks passed; C6 failed |
| `DirectX11_Win32HardwareSmoke` | 1 | 1 | 0 | 0 | 0 | 0 | Hardware Intel, feature 11_1, debug layer on |
| DIRECTX12 CTest inventory | 419 | n/a | n/a | n/a | n/a | n/a | Most renderer parity targets were not built in this phase |
| `DirectX12_Smoke` | 1 | 1 | 0 | 0 | 0 | 0 | 25/25 internal checks passed |
| DX12 Win32 presentation stress | 1 direct run | 1 | 0 | 0 | 0 | 0 | Hardware Intel, feature 12_1, debug layer and DRED on |

The full HEADLESS build succeeded in 818.8 seconds on the second complete build attempt.
DX11 and DX12 targeted smoke builds succeeded in 474.5 and 459.7 seconds respectively.
Historical test code emitted 62 MSVC C4834 ignored-`nodiscard` warnings and one C4129
invalid escape (`JsonTests.cpp:112`); CMake emitted a Draco policy warning. No warning
introduced by the bring-up changes remains.
At close, build trees occupied 15.86 GiB (core), 1.09 GiB (DX11), and 1.07 GiB
(DX12); C: had 379,364,896,768 bytes free (~353.31 GiB). Available RAM was about
7.1 GiB after builds. The disk and memory limits were not approached.

## Observed defects and next workstream handoff

| Area | Evidence / next step |
|---|---|
| DX11 classic parity | C6 is resolved as a test setup defect by WIN11-0009. The default `Depth24` intentionally masks stencil; the test now requests `Depth24Stencil8`. |
| DX11 classic parity | Post-resize sprite coordinates are resolved by WIN11-0010. The exact logical pixel rectangle now survives the 497x301 physical resize with the D3D11 debug layer clean. Full classic corpus and stress remain open. |
| DX12 classic parity | Smoke passes; most registered parity executables were not built or run. Full parity campaign remains open. |
| DX11 modern CNAEXT | Not evaluated in this bring-up. |
| DX12 modern CNAEXT | Not evaluated in this bring-up. |
| Win32 platform | Basic real HWND lifecycle passed; broad Win32 hardening remains separate. |
| Generic Windows portability | Review historical C4834 test warnings and C4129 JSON-test escape; expand bounded test coverage later. |
| sharp-runtime later | Dependency only; no Windows hardening or binding work done. |
| samples later | No sample checkout, build, execution, or comparison done. |

No Linux-only file is required for this baseline. No system-wide software installation is
required. This branch remains independent of the parallel Debian workstream; integration
from that branch must be evaluated later.

## Native DX11 classic campaign, 2026-09-25

WIN11-0009 and WIN11-0010 used the existing `C:\rv\build\cna-win11-dx11-debug`
Visual Studio x64 Debug tree. Exact focused commands, with `CNA_D3D11_DEBUG_LAYER=1`, were:

```powershell
cmake --build C:\rv\build\cna-win11-dx11-debug --config Debug --parallel 4 --target cna_test_directx11_smoke cna_test_directx11_win32_hardware_smoke
C:\rv\work\private_desktop.exe 120000 'C:\rv\build\cna-win11-dx11-debug\Debug\cna_test_directx11_smoke.exe'
C:\rv\work\private_desktop.exe 120000 'C:\rv\build\cna-win11-dx11-debug\Debug\cna_test_directx11_win32_hardware_smoke.exe'
```

The first command built both targets. `DirectX11_Smoke` passed 21/21 native checks;
the Win32 hardware smoke passed every check with adapter Intel Iris Xe `8086:46A6`,
`software=0`, feature level `0xB100` (11_1), debug layer enabled and zero recorded
corruption, errors, or warnings. The second smoke reads back the complete logical
320x240 image after the physical resize and verifies all 1024 sprite pixels, not only
that some red pixels survived. The full parity build and feature matrix are next; these
two focused passes alone are not a classic parity gate.

## DX11 physical classic corpus and failure classification, 2026-09-25

WIN11-0011: built the complete native DIRECTX11 Debug configuration with
`cmake --build C:\rv\build\cna-win11-dx11-debug --config Debug --parallel 4`.
The native `DIRECTX11` CTest label selects 264 executable cases. The first
private-desktop, Intel Iris Xe run used:

```powershell
$env:CNA_D3D11_DEBUG_LAYER='1'
C:\rv\work\private_desktop.exe 7200000 'ctest --test-dir C:\rv\build\cna-win11-dx11-debug -C Debug -L DIRECTX11 --output-on-failure --parallel 1'
```

The first run passed 252/264. Twelve cases failed or timed out. The raw log is
`C:\rv\logs\dx11-parity-private.log`; every selected executable was built, and
no WARP adapter was used. The failure classifications and focused reruns are:

| Case | Classification and resolution | Focused result |
|---|---|---|
| Resource_PresentLifecycle | Windows crash-report dialog blocked the fixture's deliberate unhandled-exception child. Suppressed the dialog only in that child. | 22/22 |
| CompressedTexture_StorageContract | Fixture requested decoded `Color` values from compressed cube/volume formats. Cube readback now compares exact BC blocks; compressed volume construction is checked against CNA's public HiDef profile restriction even though the Intel native format query advertises BC volume support. | 30/30 |
| SurfaceFormat_Throws | Fixture conflated renderer format refusal with a deferred unknown format. Verdict-specific exception assertions now match the public contract. | 30/30 |
| CubeVolume_GetDataContract | The fixture used Reach despite testing volume textures, which require HiDef. | 56/56 |
| RenderTargetCube_GetDataContract | Fixture still declared DX11 cube uploads unsupported although the implementation stores them. | 55/55 |
| GraphicsDevice_OrderedClear | Fixture used Reach while exercising HiDef-only state. | 51/51 |
| Backbuffer_PassOrder | Fixture used Reach while invoking HiDef-only backbuffer readback. | 30/30 |
| Deferred_Scissor | Fixture expected out-of-bounds scissor rectangles to clip, while the public setter rejects them before renderer dispatch. | 50/50 |
| CubeVolume_SetDataContract | Fixture used Reach for volume and incorrectly declared DX11 RenderTargetCube uploads unsupported. | 61/61 |
| DrawRangeValidation | Genuine DX11 dispatch divergence: renderer inherited managed buffer-range guards although FNA forwards offset/range hints to the native API. DX11 now opts out of those guards; its GPU buffers do not stage invalid vertex/index accesses through host memory. | 13/13 |
| PresentationModeContract | Fixture treated `ReadBackbuffer` as a physical readback even though DX11 maps that API's coordinates through the logical presentation rectangle. A native staging readback now measures physical pixels; all five modes produce the expected geometry. | 8/8 |
| RendererCapabilityTruth | Fixture used Reach but compared raw device capabilities to public profile-filtered answers; it also expected half-float filtering to be false despite the Intel device reporting it. The fixture now uses HiDef and checks the actual unsupported compute/indirect flags. | 22/22 |

WIN11-0012: D3D11 debug-layer warnings in the Backbuffer_PassOrder transition
showed a render target rebound as output while its old PS shader-resource view was
still bound. DX11 now unbinds only shader-resource views that alias the upcoming
RTV/DSV resources, across all shader stages, before single-target, cube-face, or
MRT output binding. The focused Backbuffer_PassOrder rerun passed 30/30 with no
D3D11 warnings or errors. Unrelated sampled resources remain bound.

WIN11-0013: the invalid-range DrawRangeValidation test passes 13/13. Its
deliberately invalid D3D11 draw calls produce debug IDs 335/336 (integer range
overflow) and 356/359 (vertex/index buffer too small). These are expected for
that negative API-contract probe and are separate from valid-rendering diagnostics.
The FNA `GraphicsDevice.DrawPrimitives` and `DrawIndexedPrimitives` implementations
check the positive required counts and forward offsets to FNA3D without a
managed buffer-capacity check.

WIN11-0014: bounded stress overrides were added to the existing Win32 hardware
smoke (`CNA_DX11_STRESS_FRAMES`) and dynamic buffer fixture
(`CNA_STRESS_FRAMES`). Their normal CTest frame counts remain unchanged; extended
physical runs and memory/handle measurements remain to be recorded.

WIN11-0015: the second whole-label run passed 263/264. Its sole timeout was
`SkinnedEffect_Vector4BoneIndices`, after the fixture printed 6/6 passed checks;
CTest recorded 1336.03 seconds even though its generated test property is
`TIMEOUT 60`. The original executable then passed ten direct private-desktop
runs and 20 CTest repeats without modification. Windows System log records
repeated Microsoft-Windows-Kernel-Power 506/507 Modern Standby transitions
during the long run, including idle-timeout entries. This is strong evidence
that the abnormal wall-clock timeout arose from system sleep, not a reproducible
renderer failure. `C:\rv\work\private_desktop_awake.exe` is an external launcher
variant that holds `ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED`
only while the private-desktop child is active, then clears the request. It
passed the physical Intel Win32 smoke with zero debug messages. A complete
awake-runner corpus is in progress to confirm the classification. The launcher
does not modify CNA or the machine's persistent power policy.

WIN11-0016: the complete `DIRECTX11` label passed **264/264** on the physical
Intel Iris Xe with the D3D11 debug layer enabled when launched through the
temporary private-desktop awake runner. Exact command:

```powershell
$env:CNA_D3D11_DEBUG_LAYER='1'
C:\rv\work\private_desktop_awake.exe 7200000 'ctest --test-dir C:\rv\build\cna-win11-dx11-debug -C Debug -L DIRECTX11 --output-on-failure --parallel 1'
```

The run completed in 218.45 seconds; no 506/507 standby event occurred during
it. The launcher calls `SetThreadExecutionState` only for the child lifetime.
It is outside CNA and outside the live desktop. Internal-output review found
skips in three fixtures despite their CTest passes. The FrontFaceWinding
fixture now unbinds its VB/IB after a draw before attempting `SetData`; the
RenderTarget_PassBoundary fixture requests HiDef for its MRT/readback cases;
and SpriteBatch_3DOrder resolves its render target before `GetData`. Focused
native Intel reruns passed all three cases with **256 internal passes, zero
internal skips**, and no D3D11 debug output.

WIN11-0017: registered all 32 renderer-neutral classic parity fixtures for
native D3D11, plus InstancedTexturedDraw and DrawLineTopology. The first
34-case physical Intel run passed 31 and exposed three defects: missing
`TEXCOORD1` DualTextureEffect fallback sampled `TEXCOORD0`; the blend cache
sent vector-only factors to D3D11's scalar alpha equation; and stock instanced
draws ignored the active effect's texturing, alpha test, lighting, dual texture,
skinning and PBR state. The fixed DualTexture fixture passes 5/5 with its
absent `TEXCOORD1` sampling (0,0), and the blend fixture passes its exhaustive
factor/function/write-mask sweep. D3D11 rejects `SourceAlphaSaturation` in a
destination slot before native state creation, with explicit assertions in
the shared fixture; its API has no fixed-function equivalent for that factor.
The rich stock-effect instanced path now reads four matrix columns from the
existing upload shadow and issues ordered GPU draws through the ordinary
effect-aware path. The simple colored instancing path retains a single native
`DrawIndexedInstanced`. The focused rich instancing fixture passed **37/37**
on Intel with no D3D11 debug output. This is semantic parity but the rich path
scales draw calls with instance count; stress/performance evidence is still
needed before classic closeout. The fixture now catches an escaped exception
in `main`, so a future failure exits as a test failure rather than waiting for
Windows Error Reporting and a CTest timeout.

WIN11-0018: the next full native Debug build succeeded, then the physical
Intel Iris Xe private-desktop CTest label passed **298/298** with zero external
failures, timeouts, or skips in 293.11 seconds. Exact command (with
`CNA_D3D11_DEBUG_LAYER=1`):

```powershell
C:\rv\work\private_desktop_awake.exe 7200000 'ctest --test-dir C:\rv\build\cna-win11-dx11-debug -C Debug -L DIRECTX11 --output-on-failure --parallel 1'
```

`LastTest.log` contains 298 sections and no `[FAIL]` or `[SKIP]` line. Four
fixtures emit D3D11 debug messages. `DrawRangeValidation` deliberately sends
invalid offsets and counts (IDs 335/336/356/359); `Resource_PresentLifecycle`
deliberately draws past a tiny buffer (ID 356). `ShaderEffect_ReflectionContract`
tries two declarations lacking required shader inputs and receives IDs 163
for `NORMAL0` and `TEXCOORD1`. The MRT fixture uses a four-output pixel shader
while binding fewer than four targets for some target-binding cases; D3D11
reports writes to unbound slots discarded (ID 3146081). These are classified
test probes, not warnings from the valid rendering cases. No other debug-layer
messages occurred in the full label. All important tests selected the physical
Intel adapter; no WARP fallback was enabled.

WIN11-0019: physical Intel stress on the private desktop, both with
`CNA_D3D11_DEBUG_LAYER=1`:

```powershell
$env:CNA_DX11_STRESS_FRAMES='3000'
C:\rv\work\private_desktop_awake.exe 1800000 'C:\rv\build\cna-win11-dx11-debug\Debug\cna_test_directx11_win32_hardware_smoke.exe'
$env:CNA_STRESS_FRAMES='3000'
C:\rv\work\private_desktop_awake.exe 1800000 'C:\rv\build\cna-win11-dx11-debug\Debug\cna_test_directx11_dynamic_buffer_stress.exe'
```

The Win32 run passed 3,000 frames, 20 resizes and 31 presentation/pixel
readbacks, with zero D3D11 debug corruption/errors/warnings. The dynamic
buffer run passed **9007/9007 checks over 3002 frames**, cycling None,
Discard and NoOverwrite uploads and readbacks, with zero debug messages.
One 100-sample, 100 ms-interval process observation saw working set
**62.92–64.42 MiB**, handles fixed at **456**, and threads fixed at **16**.
Later frame-1711 and frame-2604 observations were **64.82/64.96 MiB**,
**449 handles**, and **14 threads**; there was no progressive growth.

### DX11 classic capability matrix (physical Intel, Debug)

The 298-case label and the extended probes above establish the following
current CNA/XNA classic surface. Every listed passing row ran on the hardware
adapter with `software=0`; no WARP result is used as the verdict.

| Capability | Evidence / boundary |
|---|---|
| GraphicsDevice, adapter and capability reporting | `DeviceValidation`, `GraphicsAdapterQueryContract`, `RendererCapabilityTruth` pass. |
| Presentation, Present, resize, shutdown | `Win32HardwareSmoke`, `PresentationModeContract`, `Resource_PresentLifecycle`, `DeviceResetEvents` pass; 3,000 frames and 20 resizes pass. |
| Backbuffer and readback | `Backbuffer_PassOrder`, `BackbufferReadbackDimension`, `RenderTarget_BackbufferConsumer` pass. |
| Viewport and scissor | `Deferred_Viewport`, `Deferred_Scissor`, `RenderTarget_ViewportScissorReset`, `SpriteBatch_CustomViewport` pass. |
| Rasterizer, culling and wireframe | `RasterizerState_CullMode*`, `FrontFaceWinding`, `Parity_fill_mode_wireframe` pass. |
| Blend equations, factors, write masks | `BlendState_*`, `ColorWriteChannels*`, `Parity_blend_states` pass; D3D11 destination `SourceAlphaSaturation` is explicitly rejected and tested. |
| Depth, stencil, two-sided stencil | `DepthStencilState_*`, `GraphicsDevice_ReferenceStencil`, `Parity_stencil_*` pass. |
| Vertex declarations and multi-stream binding | `Parity_vertex_semantics`, `Parity_multi_stream_split`, `DrawUserPrimitives_CustomVD` pass. |
| Vertex and index buffers, uploads/readbacks | `Buffer_*`, `DrawRangeValidation`, `DrawUserIndexedPrimitives_*` pass; 3,002-frame dynamic upload/readback run passes. |
| Instancing | `Parity_instanced_draw` and `InstancedTexturedDraw` (37/37) pass, including effects and per-instance World. Rich stock effects currently use ordered per-instance GPU draws. |
| Primitive topologies | `DrawLineTopology`, triangle and strip winding, user/buffered indexed and non-indexed draws pass. |
| Texture2D, cube and volume transfers | `Texture2D_*`, `TextureCube_*`, `Texture3D_*`, `CubeVolume_*DataContract` pass. |
| Formats and compressed textures | `SurfaceFormat_*`, `CompressedTexture_StorageContract`, `Parity_compressed_cube` pass. Public HiDef rejects compressed `Texture3D` construction. |
| Mips and samplers | Texture/cube/volume mip round trips, `Sampler*`, `TextureFilter*`, address modes, anisotropy and `Parity_sampler_*` pass. |
| RenderTarget2D/Cube, MRT, MSAA | `RenderTarget*`, `MRT`, `Parity_render_target_mip`, `Parity_hdr_render_target`, `Parity_backbuffer_msaa` pass. |
| SpriteBatch and SpriteFont | `SpriteBatch_*`, `SpriteFont_*`, `Parity_sprite_*` pass, including resize placement and sort/state ordering. |
| BasicEffect, AlphaTestEffect, DualTextureEffect | Named effect suites plus light/alpha/UV parity fixtures pass. |
| EnvironmentMapEffect and SkinnedEffect | Named effect suites and `Parity_env_map_terms`/`Parity_skinned_terms` pass. |
| Custom ShaderEffect | `ShaderEffect_ReflectionContract` passes; its two missing-input probes cause the classified native ID 163 messages. |
| Occlusion queries | `OcclusionQuery_Cycle`, `_VisibleQuad`, `_OccludedQuad` pass. |
| Resource lifetime and device disposal | `Resource_*`, `Buffer_Disposed`, `DeviceResetEvents` pass, including deferred sources and bound-target lifetime. |

## DX12 classic hardware campaign

WIN11-0020: full native Debug build passed in `C:\rv\build\cna-win11-dx12-debug`
(262 parity executables). The first full private-desktop hardware run selected
Intel Iris Xe `8086:46A6`, feature level `0xC100` (12_1), with
`CNA_D3D12_ADAPTER=hardware`, debug layer on, DRED on and GPU validation off:

```powershell
$env:CNA_D3D12_ADAPTER='hardware'
$env:CNA_D3D12_DEBUG_LAYER='1'
$env:CNA_D3D12_DRED='1'
$env:CNA_D3D12_GPU_VALIDATION='0'
C:\rv\work\private_desktop_awake.exe 7200000 'ctest --test-dir C:\rv\build\cna-win11-dx12-debug -C Debug -L DIRECTX12 --output-on-failure --parallel 1'
```

Result: **258/262 passed**, 4 failed, 575.56 seconds. Baseline CTest output is
`C:\rv\logs\dx12-hardware-classic-baseline.log`, and its complete
`LastTest.log` was preserved as
`C:\rv\logs\dx12-hardware-baseline-lasttest.log`. Failures:

| Fixture | Classification | Evidence |
|---|---|---|
| `CubeVolume_SetDataContract` | Test contract stale | All six RenderTargetCube faces accept and return exact uploads; fixture still required refusal. |
| `CompressedTexture_StorageContract` | DX12 renderer | `Texture2D::GetData` refuses native BC block readback after a valid upload. |
| `DrawRangeValidation` | Generic validation gate for DX12 | Six buffered range values are rejected by managed guards although measured XNA forwards them to native D3D. |
| `PresentationModeContract` | Test observation error | Fixture asks DX12's logical-coordinate `ReadBackbuffer` for physical pixels; its geometry and coordinate transforms pass. |

The full log has only classified D3D12 debug warnings: `Deferred_Scissor`
intentionally applies a zero-size scissor (ID 695, three messages), and `MRT`
binds fewer outputs than its four-output pixel shader declares (ID 679, ten
messages). The controlled device-removal/recreation fixtures run with DRED;
there is no unplanned device removal or DRED fault in the baseline. The
historically WARP-sensitive `TextureFilterMipContract` and
`DescriptorCapacityContract` both pass on the physical Intel adapter; WARP was
not selected for this result. The four failures remain open pending focused
hardware reruns and the shared parity-fixture expansion.

WIN11-0021: focused physical Intel rerun passed the four baseline failures:
`CubeVolume_SetDataContract` 61/61, compressed DXT storage 30/30,
`DrawRangeValidation` 13/13, and `PresentationModeContract` 8/8, with no
unexpected debug messages. The compressed 2D readback now copies the complete
BC subresource through the driver's D3D12 copyable footprint and extracts the
requested block rows; this verifies real GPU bytes, including sub-4x4 mip tails.
The render-target cube fixture now requires exact uploads on all six faces.
DX12 forwards buffered ranges to native D3D12 as measured XNA does. The
presentation fixture temporarily uses identity presentation geometry while
observing physical pixels, preserving the renderer's logical readback contract.

WIN11-0022: registered 32 renderer-neutral classic parity fixtures plus
`InstancedTexturedDraw` and `DrawLineTopology`. Initial window-attached run
passed 11/34. Most failures were fixture geometry: Win32's actual 800x480
client area exceeded the fixed 128-256 pixel test canvas under
`NativeBackBuffer`, so the fixtures sampled the wrong physical regions. Five
representative failures passed unchanged with
`CNA_FORCE_HEADLESS_DEVICE_EXT=DIRECTX12`; the complete exact-sized offscreen
run then passed **33/34** on the same Intel hardware. The one genuine DX12 gap
was rich stock-effect instancing, which rejected an implicit four-column
64-byte matrix stream. DX12 now reads those columns from its existing upload
shadow and issues effect-aware GPU draws per instance; the plain colored path
retains native `DrawIndexedInstanced`. The focused rich fixture passed **37/37**
with no D3D12 debug messages. The 34 parity CTest registrations now request
the exact-sized offscreen device. Swap-chain presentation is still covered by
the separate window-attached Win32 tests. Full rebuilt-suite and stress
results are pending.

WIN11-0023: complete rebuilt native Debug label passed **296/296** in
349.00 seconds on Intel `8086:46A6` with hardware selection, D3D12 debug
layer and DRED on, GPU validation off. The complete CTest log contains 296
sections, zero internal `[FAIL]` or `[SKIP]`, no timeout and no unplanned
device removal. Its 17 D3D12 debug warnings are all deliberate fixture probes:
empty scissor ID 695 (3), buffer overrun probes IDs 210/213 (3/1), and a
four-output shader bound with fewer MRT slots ID 679 (10). There were no
other debug-layer warnings or errors. Exact command is WIN11-0020's command,
now against the 296-case tree; log:
`C:\rv\logs\dx12-classic-full-hardware.log`.

The private-desktop window-attached `cna_stress_directx12_win32_present.exe
--frames 3000` passed with 3,001 observed frames, 130 resizes, 20 minimizes,
120 resource-churn rounds, 281 backbuffer reads and 120 target reads. Warm to
end process handles 478 to 476, private bytes 69 to 71 MiB. D3D12 debug
corruption/error/warning totals all zero. RTV descriptors live 2, peak 3;
SRV descriptors live 1, peak 2. Debug layer, DRED and explicit hardware
adapter were enabled; log `C:\rv\logs\dx12-win32-stress-3000.log`.

WIN11-0024: targeted GPU-based validation on Intel exposed a failure in the
large `DescriptorCapacityContract` fixture. Legs A-C passed, including 256
simultaneously live textured/sampled resources. Leg D's repeated draw/read
loop then encountered a GPU hang and device removal, and later legs failed
because the device was removed. DRED reported `DXGI_ERROR_DEVICE_HUNG`,
breadcrumb node 16 first incomplete operation 6 (`DISPATCH`, likely GPU
validation instrumentation because this classic fixture issues no compute),
node 24 at 34/37 operations with first incomplete operation 15, and a page
fault at `0x0000B802062F0000` involving two recently freed D3D12 resources
(allocation type 34). A process dump was preserved at
`C:\rv\artifacts\dx12-gbv-descriptor-capacity.dmp`; full log:
`C:\rv\logs\dx12-classic-gpu-validation.log`. `InstancedTexturedDraw` also
exceeded its 180-second CTest limit under GPU validation after passing the
normal Debug run 37/37. Seven of the nine targeted cases passed. The cause
of the descriptor test's GPU-validation removal remains under investigation;
DX12 classic is **not yet closed**. DRED logging now includes command-list,
queue and allocation names where the runtime supplies them, and CNA names
its main D3D12 lists and texture/buffer/target resources for a focused rerun.

WIN11-0025: the same physical Intel device passed four smaller GPU-validation
cases (`DescriptorAllocator`, `Resource_BoundDispose`,
`Resource_DeferredSourceLifetime`, `Parity_instanced_draw`) with debug layer and
DRED enabled and no removal; log `C:\rv\logs\dx12-gbv-small-lifetime.log`.
The descriptor-capacity fixture gained an optional `--legs` diagnostic selector;
ordinary CTest still runs every leg. Under Intel GPU validation, D alone,
C+D, and A+D passed; A+B+C+D reproduced the hang at D. The named DRED rerun
identified two recently freed `CNA Texture2D resource` objects at the page
fault VA. Its first incomplete `DISPATCH` belonged to the debug layer's own
GBV queue, and an application frame list's first incomplete operation was a
resource barrier; log `C:\rv\logs\dx12-gbv-descriptor-legs-abcd-names.log`.
Explicit WARP, used solely as a differential, completed D after A+B+C without
a device removal, but failed B/C sampler expectations, so it is not used as a
parity verdict; log `C:\rv\logs\dx12-gbv-descriptor-legs-abcd-warp-differential.log`.
Clearing reclaimed SRV descriptors after their fence did not alter the Intel
failure and was reverted. The repeat was stopped at 90 seconds after DRED
capture; no more high-cardinality GBV reruns are planned on this physical GPU.
Microsoft's GBV guidance recommends smaller resource sets because GBV can slow
execution substantially, and states that it injects Dispatch calls and may
use an asynchronous validation queue:
https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-d3d12-debug-layer-gpu-based-validation .
The precise Intel GBV page-fault cause is **not proven**; preserve this as an
environment/driver investigation item, not a renderer pass. Routine Debug
hardware parity and the 3,000-frame stress result remain clean. Smaller
targeted GBV cases are the practical validation profile for this machine.

### DX12 classic capability matrix (physical Intel, Debug)

The 296-case label has zero external/internal failures or skips, with GPU
validation off and the D3D12 debug layer and DRED on. All rows use the
hardware adapter `8086:46A6`, except the expressly labeled WARP differential
above. The large-GBV issue in WIN11-0025 remains separate from ordinary
classic conformance.

| Capability | Evidence / boundary |
|---|---|
| GraphicsDevice, adapter and capability reporting | `DeviceValidation`, `GraphicsAdapterQueryContract`, `RendererCapabilityTruth` pass; explicit hardware selection reports feature level 12_1. |
| Presentation, Present, resize, shutdown | `Win32HardwareSmoke`, `PresentationModeContract`, `Resource_PresentLifecycle`, `DeviceResetEvents` pass; 3,001-frame Win32 stress includes 130 resizes and 20 minimizes. |
| Backbuffer and readback | `Backbuffer_PassOrder`, `BackbufferReadbackDimension`, `RenderTarget_BackbufferConsumer` pass; stress reads 281 backbuffers. |
| Viewport and scissor | `Deferred_Viewport`, `Deferred_Scissor`, `RenderTarget_ViewportScissorReset`, `SpriteBatch_CustomViewport` pass. |
| Rasterizer, culling and wireframe | `RasterizerState_CullMode*`, `FrontFaceWinding`, `Parity_fill_mode_wireframe` pass. |
| Blend equations, factors, write masks | `BlendState_*`, `ColorWriteChannels*`, `Parity_blend_states` pass with the scalar alpha-factor fix; invalid destination `SourceAlphaSaturation` is rejected. |
| Depth, stencil, two-sided stencil | `DepthStencilState_*`, `GraphicsDevice_ReferenceStencil`, `Parity_stencil_*` pass. |
| Vertex declarations and multi-stream binding | `Parity_vertex_semantics`, `Parity_multi_stream_split`, `DrawUserPrimitives_CustomVD` pass. |
| Vertex/index buffers and range validation | `Buffer_*`, `DrawRangeValidation`, `DrawUserIndexedPrimitives_*`, dynamic buffer stress pass. |
| Instancing and primitive topologies | `Parity_instanced_draw`, `InstancedTexturedDraw` 37/37 and `DrawLineTopology` pass. Rich stock-effect instancing uses ordered per-instance GPU draws; simple color instancing remains native. |
| Texture2D, TextureCube, Texture3D | `Texture2D_*`, `TextureCube_*`, `Texture3D_*`, `CubeVolume_SetDataContract` pass, including all six cube faces. |
| Formats, compression, mips | `SurfaceFormat_*`, `CompressedTexture_StorageContract` 30/30, `Parity_compressed_cube`, texture/cube/volume mip fixtures pass. Compressed 2D readback uses the native copyable footprint. |
| Samplers, filtering, address modes | `Sampler*`, `TextureFilter*`, anisotropy, `Parity_sampler_*` and descriptor capacity pass on Intel without GBV. |
| RenderTarget2D/Cube, MRT, MSAA | `RenderTarget*`, `MRT`, `Parity_render_target_mip`, `Parity_hdr_render_target`, `Parity_backbuffer_msaa` pass; stress checks 120 targets. |
| SpriteBatch and SpriteFont | `SpriteBatch_*`, `SpriteFont_*`, `Parity_sprite_*` pass. |
| Basic/AlphaTest/DualTexture effects | Named effect suites and light/alpha/UV parity fixtures pass. |
| EnvironmentMap/Skinned effects | Named effect suites and `Parity_env_map_terms`/`Parity_skinned_terms` pass. |
| Custom ShaderEffect | `ShaderEffect_ReflectionContract` passes, including invalid-declaration diagnostics. |
| Occlusion queries | `OcclusionQuery_Cycle`, `_VisibleQuad`, `_OccludedQuad` pass. |
| Resource lifetime and device disposal | `Resource_*`, `Buffer_Disposed`, `DeviceResetEvents`, descriptor allocator tests, 3,001-frame churn pass. The large Intel GBV descriptor-capacity exception is WIN11-0025. |

WIN11-0026: after restoring the original descriptor allocator and adding DRED
wide-name decoding, the complete native Debug build passed. The final normal
hardware label passed **296/296** in 286.70 seconds with debug layer and DRED
on, GBV off; `LastTest.log` has 296 sections, zero internal `[FAIL]`/`[SKIP]`,
zero unplanned removals, and only the 17 classified deliberate warnings
(IDs 210 x3, 213 x1, 679 x10, 695 x3). Exact test command:

```powershell
$env:CNA_D3D12_ADAPTER='hardware'
$env:CNA_D3D12_DEBUG_LAYER='1'
$env:CNA_D3D12_DRED='1'
$env:CNA_D3D12_GPU_VALIDATION='0'
C:\rv\work\private_desktop_awake.exe 7200000 'ctest --test-dir C:\rv\build\cna-win11-dx12-debug -C Debug -L DIRECTX12 --output-on-failure --parallel 1'
```

Log: `C:\rv\logs\dx12-classic-final-hardware.log`. With GBV on, a bounded
ten-case physical Intel subset passed **10/10**, zero internal fails/skips,
zero removals, and only MRT's ten deliberate ID 679 warnings. It covers
descriptor allocation, bound/disposed and deferred source lifetime, 2D and
compressed texture readback, render-target roundtrip/mips, MRT, custom effect
reflection, and instancing. Log:
`C:\rv\logs\dx12-classic-gbv-bounded-final.log`. The command used the same
environment with `CNA_D3D12_GPU_VALIDATION='1'` and an exact-name CTest `-R`
selection of those ten cases. The normal classic capability gate is met on
physical Intel. The high-cardinality GBV-only TDR remains a separately
recorded Intel validation limitation under WIN11-0025; no claim is made that
that fixture passes GBV, and the DRED evidence must travel with integration.

## Native DX11 modern CNAEXT campaign, 2026-09-25

WIN11-0027: configured `C:\rv\build\cna-win11-dx11-modern` with the native
Visual Studio 17 2022 x64/v143 generator, `CNA_PLATFORM=WIN32`,
`CNA_GRAPHICS_RENDERER=DIRECTX11`, `CNA_CNAEXT=ON`, `CNA_BUILD_TESTS=ON`,
`CNA_BUILD_EXAMPLES=ON`, `CNA_ENABLE_SDL=OFF`, NULL audio, video/font/Draco
off, and `CNA_SHARP_RUNTIME_ROOT=C:/rv/src/sharp-runtime`. The first
`cmake --build C:\rv\build\cna-win11-dx11-modern --config Debug --parallel 4
--target CnaGraphicsExtTests` exposed MSVC C2026: three generated shader
source literals in each of the clustered-forward and post-process package
headers exceeded MSVC 19.43's per-literal limit. The generator now splits
UTF-8 payloads on complete lines below 12,000 bytes into adjacent raw
literals. Rebuilt generated headers preserve their exact concatenated source
bytes. The same build exposed one test translation unit's missing `<algorithm>`
for `std::clamp`. After those fixes the modern test executable built cleanly;
its original inventory is **969 cases in 124 suites**. Build logs:
`C:\rv\logs\dx11-modern-gtest-build-3.log`; inventory:
`C:\rv\logs\dx11-modern-gtest-list.txt`. No system tool or dependency was
installed, and no GPU result is inferred from this compilation fix.
