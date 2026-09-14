# Testing the Win32 backend on native Windows

Everything CNA knows about `CNA_PLATFORM=WIN32` so far was measured with MinGW cross-builds run
under **Wine on Linux** (`plans/plan_native_platform_validation.md`, "Win32: NOT NATIVE WINDOWS
VALIDATION"). Wine is a reimplementation of the Windows API, not Windows: it is excellent at
showing that the code paths run, and it cannot show how Windows itself behaves — its window
manager, DPI virtualisation, input stack, IMEs, clipboard, DXGI flip model, driver quirks. This
page is what to do on a real Windows 10 or 11 installation, physical or in a virtual machine.

## What Wine did and did not establish

| Established under Wine (Debian 13, wine-10.0) | Only native Windows can establish |
|---|---|
| The platform test suite (`cna_platform_tests.exe`): 385 passed, 1 skipped, Debug and Release | Those tests against the real `user32`/`gdi32`/`imm32`/`ole32` |
| A platform HWND accepted by D3D11 and D3D12: device, swap chain, clear, present, resize | DXGI flip-model behaviour, tearing, fullscreen transitions, device removal (TDR) on a real driver |
| `cna_demo_2d.exe` running with D3D11 (WineD3D → OpenGL and DXVK → Vulkan), D3D12 (Wine's vkd3d → Vulkan) and SOFTWARE on a real AMD GPU | The same through Microsoft's own D3D runtime and a Windows GPU driver |
| No SDL library imported or loaded (PE import tables; nothing named SDL in the binaries) | The runtime module list of the process on Windows (the script records it) |
| | Per-monitor DPI v2, `WM_DPICHANGED`, moving between monitors of different scale |
| | Keyboard layouts and dead keys through the real keyboard stack; IME composition and candidate windows (Microsoft Pinyin, Japanese IME) |
| | Clipboard interoperability with real applications (Notepad, Word, a browser), including large and non-ASCII text |
| | Raw input (`WM_INPUT`) relative mouse on real hardware, high-resolution wheels, touchpads |
| | Focus, activation and foreground-lock rules; Alt+Tab; Win+L and resume; Aero Snap |
| | XInput controllers |

## Build the artifacts

Cross-built on Linux with the repository's MinGW toolchain (`cmake/toolchains/mingw-w64.cmake`),
or natively on Windows with the same CMake options.

```bash
# The platform module's own suite and the Direct3D integration probe (small, platform-only).
cmake -S tools/platform/standalone_tests -B cmake-build-win32 -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchains/mingw-w64.cmake \
      -DCNA_PLATFORM=WIN32 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
cmake --build cmake-build-win32

# A real application: SDL-free, Direct3D 11/12 + software + headless, chosen at run time.
cmake -S . -B cmake-build-d3d11 -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchains/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release \
      -DCNA_PLATFORM=WIN32 -DCNA_ENABLE_SDL=OFF -DCNA_AUDIO_PLATFORM=NULL \
      -DCNA_GRAPHICS_RENDERER=DIRECTX11 -DCNA_GRAPHICS_RENDERERS="DIRECTX11;DIRECTX12;SOFTWARE;HEADLESS" \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
cmake --build cmake-build-d3d11 --target cna_demo_2d cna_demo_renderer_selection
```

Copy into one folder on the Windows machine (called `BinDir` below):

- `cmake-build-win32/cna_platform_tests.exe` and `cmake-build-win32/cna_win32_directx_probe.exe`
  — these two link the MinGW C++ runtime dynamically, so also copy `libstdc++-6.dll` and
  `libgcc_s_seh-1.dll` from `$(x86_64-w64-mingw32-g++ -print-file-name=libstdc++-6.dll)`'s
  directory (and `libwinpthread-1.dll` if a posix-thread toolchain was used);
- `cmake-build-d3d11/cna_demo_2d.exe`, `cmake-build-d3d11/cna_demo_renderer_selection.exe`
  and the `cmake-build-d3d11/Content` folder. These import only Windows system DLLs
  (`d3d11`, `d3d12`, `dxgi`, `D3DCOMPILER_47`, `user32`, `gdi32`, ...);
- `tools/platform/validate_win32_native.ps1`.

## Run the automated part

From a normal (not administrator) PowerShell prompt:

```powershell
powershell -ExecutionPolicy Bypass -File validate_win32_native.ps1 -BinDir C:\cna\bin -OutDir C:\cna\report
```

It records the machine (Windows build, physical or virtual, GPU and driver, monitors, DPI, a
`dxdiag` report), runs the platform suite with an XML report, runs the Direct3D probe, runs
`cna_demo_2d.exe --smoke 600` with each renderer, and lists the DLLs each demo process actually
loaded — failing if any of them is an SDL library. `report.md` and `summary.json` land in
`OutDir`; the exit code is 1 when any check failed. Outcomes are `PASS`, `FAIL`, `NOT-RUN` (an
artifact was missing — never counted as a pass) and `ENVIRONMENT` (the machine cannot exercise it,
for example no Direct3D 12 at all).

**The script was written on Linux and has not yet been executed**; its first run on Windows is
also its own test. Fix it in place if Windows PowerShell disagrees with it.

## Interactive checks (a person at the keyboard)

Record each as pass/fail with a note in `report.md`. Run `cna_demo_2d.exe` (no arguments) unless a
different program is named.

1. **Window states.** Minimise from the title bar and restore from the taskbar; maximise and
   restore; Aero Snap (Win+Left/Right, Win+Up/Down); drag across monitors. The picture must be
   correct after each, with no stretched or black frame.
2. **Focus.** Alt+Tab away and back; press Win and return; lock with Win+L and unlock. Held keys
   must not stay "down" after focus returns.
3. **Keyboard.** US layout, then Czech (QWERTZ) switched with Win+Space: the Y/Z keys swap, the
   number row still reports the digit keys (`Keys.D1`...), AltGr combinations and dead keys
   (´ then e) produce text in a text field, and switching back restores everything.
4. **Text input and IME.** In a program with a text field, commit Czech text (ěščřžýáíé) and, with
   Microsoft Pinyin or the Japanese IME, a composed string; the candidate window must appear near
   the caret, not in a screen corner.
5. **Clipboard.** Copy from Notepad into CNA and from CNA into Notepad: ASCII, Czech, emoji, and a
   text of several megabytes.
6. **Mouse.** Buttons 1-5, wheel (and a high-resolution or touchpad scroll), double click, relative
   mode (the cursor must stay hidden and captured while the window is focused, and be released on
   Alt+Tab), capture while dragging outside the window.
7. **DPI.** Settings > Display > Scale at 100 %, 125 % and 150 %, changed while the program runs;
   then with two monitors at different scales, move the window between them. The back buffer and
   the reported sizes must follow, without blurring.
8. **Fullscreen.** Borderless fullscreen on and off repeatedly, and Alt+Tab out of fullscreen.
   Exclusive fullscreen is implemented on Win32 as a display-mode change
   (`ChangeDisplaySettingsExW(..., CDS_FULLSCREEN)`): enter and leave it, Alt+Tab out of it, and
   end the process from Task Manager while it is active — the desktop's own mode must come back
   every time.
9. **Displays.** Unplug or disable a monitor while the program runs (physical machines).
10. **Controllers.** An XInput controller, if one is available.

## Virtual machines

A VM runs real Windows, so everything above is native-Windows evidence — with the GPU caveat: the
Direct3D results describe the VM's virtual adapter, not a real GPU driver. VirtualBox guests with
3D acceleration expose Direct3D 11 through the VirtualBox graphics driver; Direct3D 12 is usually
not available there, and the probe then reports `ENVIRONMENT` for it rather than a failure.
Record which it was. Relative mouse and raw input in a VM depend on the VM's mouse integration
(disable it for relative-mode checks), and multi-monitor/DPI checks need the VM's virtual monitors
configured accordingly.
