# The native Win32 platform backend (`CNA_PLATFORM=WIN32`)

`CNA::Platform::Win32::Win32Platform` implements the CNA platform contract directly on the Win32
API. It uses no SDL of any kind — not for windowing, events, keyboard, mouse, text input, timing,
clipboard, displays or dialogs — which is what makes it the evidence for the claim the platform
abstraction was built to support: **SDL is a backend of CNA, not the substrate CNA is written
against.**

Its task log and design record is [`plans/plan_win32.md`](../plans/plan_win32.md). The portable
rules it satisfies are in [`docs/platform-abstraction.md`](platform-abstraction.md); this document
is the capability boundary and the things a Win32 host needs to know.

---

## Selecting it

```bash
# Native Windows
cmake -S . -B build -DCNA_PLATFORM=WIN32 -DCNA_GRAPHICS_RENDERER=DIRECTX11

# From Linux, targeting Windows
cmake -S . -B build-windows -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_PLATFORM=WIN32 -DCNA_GRAPHICS_RENDERER=DIRECTX11
```

`WIN32` is available **only where the target is Windows**. Asking for it anywhere else is a
configure-time `FATAL_ERROR` naming the toolchain requirement, never a silent fall back to SDL3 —
the same rule `TERMINAL` follows in the opposite direction (POSIX-only, reserved on Windows).

The backend links only operating-system import libraries: `user32`, `gdi32`, `opengl32`, `ole32`,
`shell32`, `uuid`. All are PRIVATE, so none becomes a usage requirement of a CNA consumer, and
there is nothing to vendor or fetch. Vulkan is deliberately absent from the link line:
`vulkan-1.dll` is loaded at run time, so a machine with no Vulkan driver still runs a CNA binary
and simply reports the capability as false.

It is built with MSVC and with mingw-w64. Nothing in it is MSVC-only.

---

## Capability boundary

| Capability | Win32 | How, or what is missing |
|---|---|---|
| `multipleWindows` | ✅ | One `HWND` per window, registry keyed by a stable `WindowId` |
| `highDpi` | ✅ | `GetDpiForWindow` → `GetDpiForMonitor` → `GetDeviceCaps`, all resolved at run time |
| `multipleDisplays` | ✅ | `EnumDisplayMonitors` + `GetMonitorInfoW` + `EnumDisplaySettingsW`, primary first |
| `borderlessFullscreen` | ✅ | Style swap plus a complete windowed-state snapshot |
| `nativeWindowHandle` | ✅ | `NativeWindowSystem::Win32` carrying the real `HWND` |
| `surfacePresentation` | ✅ | `StretchDIBits` on a top-down 32-bit DIB; all five scale modes |
| `openGlContext` | ✅ | WGL, with `wglCreateContextAttribsARB` when the driver exports it |
| `vulkanSurface` | host-dependent | True only when `vulkan-1.dll` loads and exports `vkGetInstanceProcAddr` |
| `clipboard` | ✅ | `CF_UNICODETEXT`, converted at the boundary |
| `textInput` | ✅ | `WM_CHAR` with surrogate pairing → UTF-8 |
| `exactKeyboardState` | ✅ | Every press has a real release; focus loss flushes held keys |
| `pixelAccurateMouse` | ✅ | Client-space pixels straight from `lParam` |
| `relativeMouse` | ✅ | Raw Input + `ClipCursor` + `SetCapture`. Registration failing refuses the mode outright; confinement and cursor hiding are best-effort on top of it |
| `cursorShapes` | ✅ | `LoadCursorW` shapes and `CreateIconIndirect` image cursors |
| `globalPointer` | ✅ | `GetCursorPos` / `SetCursorPos` / `SetCapture` |
| `inputDeviceEnumeration` | ✅ | `GetRawInputDeviceList` for keyboards and pointers |
| `powerInfo` | ✅ | `GetSystemPowerStatus`, with 255 read as "unknown" rather than passed through |
| `messageBox` | ✅ | `MessageBoxW` |
| `nativeFileDialog` | ✅ | `IFileOpenDialog` / `IFileSaveDialog` |
| `ime` | ❌ | Composition and candidate lists are not delivered — see below |
| `gamepad`, `joystick`, `gamepadRumble` | ❌ | XInput is not wired up |
| `gamepadSensors` | ❌ | XInput exposes no motion sensors |
| `haptics` | ❌ | No standalone haptic device stack |
| `sensors` | ❌ | No desktop sensor source in scope |
| `tray` | ❌ | `Shell_NotifyIconW` is not wired up |
| `camera` | ❌ | Media Foundation capture is out of scope |
| `managedEntrypoint` | ❌ | This backend never renames the host's `main()` |
| `dragAndDrop` | ❌ | `WM_DROPFILES` / OLE drop targets are not wired up (the capability was added with X11-0154) |
| `primarySelection` | ❌ | Windows has no primary selection (the capability was added with X11-0157) |
| `clipboardData` | ❌ | registered clipboard formats (`CF_DIB`, `HTML Format`, ...) are not wired up; text only (the capability was added with X11-0158) |

Every ❌ is a capability reported **false** with a service accessor that returns `nullptr` and a
capability-gated call that raises `PlatformNotSupportedException` naming the capability. None is a
stub that silently succeeds. What each one still needs is recorded in
[`plans/plan_win32.md` §15](../plans/plan_win32.md).

`ime` is the one worth expanding on, because it is the closest to done and the easiest to get
wrong: `WM_IME_COMPOSITION` could be read today, but `TextEditingCandidatesEvent` needs the
candidate list, its selected index and its orientation. Delivering composition without candidates
would give a game a partially-working IME it could not detect, which is worse for the user than a
capability that honestly reads false.

---

## Things a Win32 host needs to know

### The backend does not touch process-global policy

CNA is a framework inside somebody else's process. `Win32Platform` therefore **never**:

- sets DPI awareness (`SetProcessDpiAwarenessContext`, `SetProcessDPIAware`) — declare it in your
  manifest or set it before creating the platform, and the backend reports values coherent with
  whatever you chose;
- raises the timer resolution (`timeBeginPeriod`) — `Delay()` is a plain `::Sleep`;
- changes the current directory;
- takes the thread's COM apartment. `CoInitializeEx` is called once, balanced, and treated as
  *not ours to end* when the host had already chosen an apartment (`RPC_E_CHANGED_MODE` and
  `S_FALSE` both leave the reference alone).

### Four `<windows.h>` macros collide with the CNA contract

`<windows.h>` defines a family of `#define Name NameW` aliases, and four of them collide with
identifiers the platform contract uses:

| Macro | Collides with |
|---|---|
| `CreateWindow` | `IPlatform::CreateWindow` |
| `CreateDirectory` | `IPlatformFileSystem::CreateDirectory` |
| `MessageBox` | `PlatformCapability::MessageBox` |
| `GetClassName` | `Win32WindowClass::GetClassName` |

A macro does not respect namespaces or member scope, so with them in force a declaration and its
definition are silently renamed and an `override` stops matching anything. The backend undoes them
at its single `<windows.h>` entry point (`Win32Common.hpp`, which also sets `NOMINMAX`,
`WIN32_LEAN_AND_MEAN` and `UNICODE`). **A host application that includes both `<windows.h>` and
CNA's platform headers needs the same four lines**, or must include the CNA headers first:

```cpp
#include <windows.h>
#undef CreateWindow
#undef CreateDirectory
#undef MessageBox
#undef GetClassName
#include "CNA/Platform/IPlatform.hpp"
```

### Closing a window is a request, and closing one window is not quitting

`WM_CLOSE` produces `WindowEventKind::CloseRequested` and **does not destroy the window**. The
application decides; the window dies when its `IPlatformWindow` goes out of scope. And
`PostQuitMessage` is never called from `WM_DESTROY`, so a process with several windows survives
closing one of them. A `QuitEvent` is produced only for a real `WM_QUIT` or a `WM_ENDSESSION` that
was not vetoed.

### Window sizes are client sizes

`WindowDescription::width`/`height` and `IPlatformWindow::SetSize` mean the **client** area, while
`CreateWindowExW` and `SetWindowPos` take the outer frame. Every path converts through
`AdjustWindowRectExForDpi`. (The difference is not cosmetic: a default decorated window loses 8×34
pixels without it.)

### DPI values are coherent for the awareness you chose

`GetClientBounds()` is logical units, `GetPixelSize()` is the drawable surface, and
`GetDisplayScale()` relates them. Under a per-monitor-aware process the client rect is true device
pixels; under a system-aware or unaware one Windows virtualises it, and the virtualised rectangle
*is* the surface being drawn into. Both are coherent, and in both the drawable size is read
directly rather than derived by multiplying the logical size by the scale — deriving it
double-applies the scale.

---

## Graphics

The native window handle is the entire contract between this platform and a renderer:

```
Win32Platform → IPlatformWindow::GetNativeHandle() → NativeWindowSystem::Win32 + HWND
              → TryGetWin32() → DirectX11Renderer / DirectX12Renderer / GDI / Direct2D / bgfx / …
```

No renderer receives a `Win32Window*`, no renderer includes a platform implementation header, and
there is no "Win32DirectX11" renderer. **Platform and renderer remain orthogonal axes**: this
backend works with every renderer that accepts a Win32 handle, and every renderer works with any
platform that produces one.

Beyond the handle the backend also provides the three generic graphics services:

| Service | Implementation |
|---|---|
| `IPlatformGlContext` | WGL, including the throwaway-context bootstrap needed to reach `wglCreateContextAttribsARB` |
| `IPlatformVulkanSurface` | `vkCreateWin32SurfaceKHR`, resolved from a run-time-loaded `vulkan-1.dll` |
| `IPlatformSurfacePresenter` | `StretchDIBits` from a top-down 32-bit DIB, with all five scale modes |

The window class carries `CS_OWNDC`, which is what lets a WGL pixel format survive on the window's
device context; a pixel format can be set on a DC exactly once, which is also why
`WindowRenderIntent` has to be right at creation time rather than being a setter.

---

## SDL independence

With `CNA_PLATFORM=WIN32` and `CNA_AUDIO_PLATFORM=NULL` and a renderer that does not itself use
SDL, **no production target in a CNA build links SDL at all**. The root configure additionally
skips building vendored SDL3 for such a configuration, so it is not merely unlinked but never
compiled:

```bash
cmake -S . -B build-windows -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_PLATFORM=WIN32 -DCNA_AUDIO_PLATFORM=NULL \
      -DCNA_GRAPHICS_RENDERER=DIRECTX11 \
      -DCNA_BUILD_TESTS=OFF -DCNA_BUILD_EXAMPLES=OFF
# -- CNA: SDL3 not configured -- no target in this configuration uses it
```

The gate is deliberately conservative: it runs before the selection files declare their cache
defaults, so an axis that is unset reads as "not chosen explicitly" and keeps SDL3. Only a
configuration that has said no to every axis skips it. Tests and examples keep SDL3 because a
large number of their fixtures genuinely use it; that is a property of the fixtures, not of the
framework.

Five renderer families still require SDL3 by identity or by upstream dependency — `SDL_RENDERER`,
`SDL_GPU`, `FNA3D`, `FREEDIRECT`, `LLGL` — and selecting one of them keeps it, correctly.

---

## Testing

The implementation-neutral suites cover this backend automatically: registering `"Win32"` in
`PlatformFactory::GetAvailable()` is what enrols it in `PlatformConformance` and
`PlatformWindowConformance`.

```sh
CnaTests --gtest_filter='EveryImplementation/Platform*Conformance.*:Win32*'
```

Its own suites are `Win32EventMapperTests`, `Win32KeyCodeTests`, `Win32ScancodeTests`,
`Win32UtfTests`, `Win32DpiTests`, `Win32FullscreenStateTests`, `Win32PlatformTests`,
`Win32WindowTests`, `Win32InputServicesTests`, `Win32SystemServicesTests`,
`Win32GraphicsServicesTests`, `Win32DirectXIntegrationTests` and `Win32NoSdlTests`. The message
translation, key tables, UTF conversion and fullscreen state are all pure and are driven from
synthetic inputs with no window and no message loop; the rest create real windows and skip cleanly
where a host has no window manager.

### Building and running without a full CNA configure

`tools/platform/standalone_tests/` builds the platform module and its whole test suite on their
own, for any `CNA_PLATFORM`. The platform module depends on nothing but the C++ standard library —
exactly two of its fifty-odd test suites reach for SharpRuntime — so this works without the
sharp-runtime sibling checkout, which is what makes the Win32 backend testable from a Linux host
through mingw-w64 and Wine:

```sh
cmake -S tools/platform/standalone_tests -B cmake-build-win32 -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_PLATFORM=WIN32 -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-win32 --parallel
Xvfb :97 -screen 0 1280x1024x24 &
DISPLAY=:97 WINEDEBUG=-all wine64 cmake-build-win32/cna_platform_tests.exe
```

The same harness runs the suite for a host-native selection, which is how the other backends are
checked for regressions without a full configure:

```sh
cmake -S tools/platform/standalone_tests -B cmake-build-platform-headless -G Ninja \
      -DCNA_PLATFORM=HEADLESS -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-platform-headless --parallel
./cmake-build-platform-headless/cna_platform_tests
```

The same directory builds `cna_win32_directx_probe`, which creates a real Direct3D 11 and
Direct3D 12 device and swap chain on a Win32Platform window and exercises resize, present and the
close-request path. It reports "no Direct3D on this machine" as a distinct exit status (2), because
that is an environment limitation rather than a defect.

Wine is not Windows. [`plans/plan_win32.md` §16](../plans/plan_win32.md) records exactly which
claims rest on measured Wine behaviour and which rest on documented Win32 semantics.
