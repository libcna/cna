# Physical Windows 11 and DirectX bring-up

Status: first native MSVC and physical Intel GPU baseline established on 2026-09-25. This
ledger stops at build, bounded tests, Win32, and hardware smoke. Full DirectX parity,
CNAEXT parity, sharp-runtime hardening, and samples are later workstreams.

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
| DX11 classic parity | Existing `DirectX11_Smoke` C6 fails: depth-stencil object and dynamic reference are not bound on the immediate context as the test expects. Investigate during DX11 parity. |
| DX11 classic parity | After a real Win32 swap-chain resize to 497x301, the 32x32 sprite requested at (8,8) still renders but its red pixels appear at x=0..1, y=8..39 in a 320x240 readback. Pre-resize pixel at (16,16) is correct. Investigate viewport, scissor, coordinate transform, and readback behavior; do not claim post-resize pixel parity yet. |
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
