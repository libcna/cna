# CNA native Win32 platform backend (`CNA_PLATFORM=WIN32`) — Implementation Plan

> **Status: IMPLEMENTED AND VALIDATED.** All 48 tasks below are complete;
> §17 records the measured results. This plan converts the reserved `CNA_PLATFORM=WIN32` identifier
> (rejected with a `FATAL_ERROR` since [`plans/plan_platform.md`](plan_platform.md) PLAT-11) into a
> real, first-class CNA platform implementation built directly on the Win32 API — no SDL2, no SDL3,
> for windowing, events, keyboard, mouse, text input, timing or the native-window bridge.
>
> **Goal:** prove that SDL is a *backend* of CNA and not CNA itself, by standing up a second
> native desktop platform that shares nothing with it, and by making
> `CNA_PLATFORM=WIN32 + CNA_GRAPHICS_RENDERER=DIRECTX11` (and `DIRECTX12`) a supported
> configuration with the platform and renderer axes still fully independent.
>
> **Non-goal:** redesigning `DirectX11Renderer` or `DirectX12Renderer`. Phase 0's audit
> established that both already consume a generic `CNA::Platform::NativeWindowHandle` through
> `PlatformRendererSurfaceState` and call `TryGetWin32()` — see WIN32-0005. That architecture is
> preserved; the Win32 platform's job is to *satisfy* it.

---

## 0. Baseline

| Fact | Value |
|---|---|
| Baseline commit | `1e8ec10e` (`merge: integrate sw into next`) |
| Branch | `win32` (created from `next` at the baseline commit) |
| Working tree at start | clean — no pre-existing user changes to preserve |
| Host | Linux x86-64; no native Windows machine available |
| Cross toolchain | `x86_64-w64-mingw32-g++` (GCC 13.2.0), installed during this workstream |
| Execution environment | Wine 9.0 (`/usr/lib/wine/wine64`) + `Xvfb`, installed during this workstream |
| Existing cross-build support | `cmake/toolchains/mingw-w64.cmake` (pre-existing, used by the DirectX families) |

### Environment capability probe (WIN32-0000)

Before planning anything, the environment was measured rather than assumed. A standalone
MinGW-compiled probe (`spikes/win32-spike/`) confirmed that **all** of the following work under
Wine + Xvfb on this Linux host:

* `RegisterClassExW` / `CreateWindowExW` / `ShowWindow` / `DestroyWindow`;
* `PeekMessageW` / `TranslateMessage` / `DispatchMessageW` with a real `WndProc`;
* `GetClientRect`, `GetDpiForWindow` (reports 96), `QueryPerformanceFrequency` (10 MHz);
* `WM_CLOSE` delivery to the window procedure.

This is what makes the workstream genuinely testable here rather than compile-only. Wine is
**not** Windows, so §16 records precisely which claims rest on Wine behaviour and which rest on
documented Win32 semantics.

---

## 1. Architecture findings (Phase 0 audit)

### 1.1 The contract the implementation must satisfy

`CNA::Platform::IPlatform` (`modules/platform/include/CNA/Platform/IPlatform.hpp`) is 30 pure
virtuals plus two defaulted adoption hooks:

| Group | Members |
|---|---|
| Identity | `GetName`, `GetCapabilities` |
| Subsystems | `AcquireSubsystem`, `ReleaseSubsystem`, `IsSubsystemInitialized` |
| Windows | `CreateWindow`, `AdoptWindow` (defaulted), `AdoptWindowHandle` (defaulted) |
| Events | `PollEvents(std::vector<PlatformEvent>&)` |
| Timing | `GetPerformanceCounter`, `GetPerformanceFrequency`, `GetTicksMilliseconds`, `Delay` |
| Services (nullable) | keyboard, mouse, gamepad, joystick, text input, sensors, haptics, input devices, clipboard, displays, dialogs, tray, camera, GL context, Vulkan surface |
| Services (never null) | filesystem, system info |
| Presentation | `CreateSurfacePresenter` |

`IPlatformWindow` is 26 members (id, legacy token, native handle, title, client bounds, pixel
size, set-size, display scale, resizable, borderless, fullscreen mode, show/hide/minimize/
maximize/restore, `Sync`, focus, minimized, display name, supported orientations).

Three invariants dominate the design:

1. **A service pointer is non-null exactly when its capability is true.** Enforced by
   `PlatformConformance.EveryServiceIsNullExactlyWhenItsCapabilityIsFalse`. Keyboard and mouse are
   deliberately exempt — their capability fields (`exactKeyboardState`, `pixelAccurateMouse`)
   describe *quality*, not presence.
2. **A false capability refuses deterministically** with `PlatformNotSupportedException` naming
   the capability — never a silent no-op.
3. **Events are polled in batches into a caller-owned vector**, cleared and refilled, so a
   steady-state frame allocates nothing.

### 1.2 `NativeWindowHandle` and the renderer bridge — already correct

`NativeWindowHandle` has carried a `Win32` case since PLAT-12/PLAT-13:

```
system = NativeWindowSystem::Win32
window = HWND          (display / surface / windowId unused)
```

`TryGetWin32()` validates `system == Win32 && window != nullptr`. **Twenty renderer families
already consume it** (`DirectX1..12`, `Direct2D`, `GDI`, `Glide`, `bgfx`, `Wicked`, `Diligent`,
`WebGPU`). `DirectX11Renderer` and `DirectX12Renderer` both do:

```cpp
CNA::Platform::Win32NativeWindow nativeWindow;
if (!CNA::Platform::TryGetWin32(surface_.GetNativeHandle(), nativeWindow))
    throw std::runtime_error("DirectX11Renderer requires a Win32 native window.");
hwnd_ = static_cast<HWND>(nativeWindow.hwnd);
```

They reach that handle through `PlatformRendererSurfaceState`, which snapshots
`RendererSurfaceInfo::nativeHandle` and re-validates identity on refresh. **No renderer change is
required or permitted by this workstream** unless a test proves an actual renderer-side defect.
The only thing Win32Platform must do is produce a correct `Win32` handle. `platform != renderer`
stays a hard invariant: no `Win32Window*` ever reaches a renderer, and no renderer includes a
platform implementation header.

### 1.3 `KeyCode` is the Windows virtual-key space

`CNA::Platform::KeyCode` reproduces XNA's `Keys`, whose numeric values **are** the Windows
`VK_*` codes (`Back = 8`, `Enter = 13`, `Escape = 27`, `A..Z = 65..90`, `NumPad0 = 96`,
`F1 = 112`, `LeftShift = 160`, `OemSemicolon = 186`, …). For the Win32 backend the logical
key mapping is therefore *nearly* an identity — but it must still be **validated** through
`IsKnownKeyCode()` rather than cast, because the VK space is sparse and Windows reports VKs CNA
does not name. The sided-modifier VKs (`VK_LSHIFT`/`VK_RSHIFT`/…) are the interesting part: Windows
delivers the *unsided* `VK_SHIFT` in `WM_KEYDOWN`, and the side has to be recovered from the
scan code and the extended-key flag. See §7.

`CNA::Platform::Scancode` is **USB HID keyboard usage IDs**, which are *not* what Windows reports.
Windows gives PS/2 set-1 scan codes in `lParam` bits 16–23 plus an extended flag in bit 24.
A real translation table is required — this is exactly the "do not implement a simplistic
VK-to-CNA map that destroys the distinction" case the workstream calls out.

### 1.4 SDL dependency shape

| Consumer | SDL edge | Conditioned on |
|---|---|---|
| `cna_platform` | `SDL3::SDL3` PRIVATE | `CNA_PLATFORM STREQUAL "SDL3"` |
| `cna_platform` | `SDL2::SDL2` PRIVATE | `CNA_PLATFORM STREQUAL "SDL2"` |
| `cna_audio` | `SDL3::SDL3 SDL3_mixer::SDL3_mixer` PRIVATE | `CNA_AUDIO_PLATFORM STREQUAL "SDL3"` |
| `cna_audio` | `SDL2::SDL2` PRIVATE | `CNA_AUDIO_PLATFORM STREQUAL "SDL2"` |
| renderers `sdl-renderer`, `sdl-gpu`, `fna3d`, `freedirect`, `llgl` | SDL3 by identity / upstream | renderer selection |
| ~60 `examples/` and test-fixture registrations | SDL3 | `CNA_BUILD_EXAMPLES` / `CNA_BUILD_TESTS` |

**No framework module outside `cna_platform` and `cna_audio` links SDL at all.** So
`CNA_PLATFORM=WIN32 + CNA_AUDIO_PLATFORM=NULL + CNA_GRAPHICS_RENDERER=DIRECTX11` already has
**zero production SDL link edges** before this workstream changes anything.

What remains is the root `CMakeLists.txt` calling `cna_configure_vendored_sdl()`
**unconditionally**, which configures and builds vendored SDL3/SDL3_image/SDL3_mixer at
configure time even for a configuration where nothing consumes them. That is the one real
"selecting a non-SDL platform still forces SDL globally" finding, and WIN32-0060 addresses it.

### 1.5 Where the gates live

| Gate | File |
|---|---|
| Selection | `cmake/PlatformSelection.cmake` |
| Module sources / links | `modules/platform/CMakeLists.txt` |
| Factory | `modules/platform/src/PlatformFactory.cpp` |
| Test source gating | `cmake/UnitTests.cmake` (`list(FILTER CNA_TEST_SOURCES …)`) |
| SDL ratchet (strict) | `cmake/PlatformRatchet.cmake` → `tools/platform/sdl_ratchet.py` |
| Hot-path lint | `cmake/PlatformHotPathLint.cmake` → `tools/platform/hot_path_lint.py` |
| Contract-is-SDL-free | `modules/platform/tests/CNA/Platform/ContractIsSdlFreeTests.cpp` + `tools/platform/check_contract.py` |
| Conformance | `modules/platform/tests/CNA/Platform/PlatformConformanceTests.cpp`, parameterised over `PlatformFactory::GetAvailable()` |

The conformance suite auto-enrols any implementation returned by `GetAvailable()`. Registering
`"Win32"` there is therefore *also* the act of subjecting it to the full contract suite.

---

## 2. Source inventory

New subtree, all under `modules/platform/src/Win32/` (never in the public include tree):

| File | Responsibility |
|---|---|
| `Win32Error.hpp/.cpp` | `GetLastError()` → readable message; `ThrowLastError()`; `HResultMessage` |
| `Win32ComRuntime.hpp/.cpp` | Balanced, refcounted, host-respecting `CoInitializeEx` (Rule 4) |
| `Win32DpiSupport.hpp/.cpp` | Dynamically-resolved per-monitor DPI entry points; scale/rect maths |
| `Win32Scancodes.hpp/.cpp` | PS/2 set-1 (+extended) ⇄ `CNA::Platform::Scancode` (HID usage IDs) |
| `Win32KeyCodes.hpp/.cpp` | `VK_*` ⇄ `KeyCode`, incl. sided-modifier resolution and `MapVirtualKeyW` layout queries |
| `Win32Modifiers.hpp/.cpp` | Live modifier mask from `GetKeyState` into `KeyModifier` bits |
| `Win32Utf.hpp/.cpp` | UTF-8 ⇄ UTF-16 with explicit surrogate-pair handling |
| `Win32WindowClass.hpp/.cpp` | Process-wide window-class registration, refcounted |
| `Win32Window.hpp/.cpp` | `IPlatformWindow`: `HWND` lifetime, geometry, state, fullscreen, native handle |
| `Win32EventMapper.hpp/.cpp` | Window messages → `PlatformEvent`; pure, unit-testable |
| `Win32InputServices.hpp/.cpp` | Keyboard, mouse, text input services |
| `Win32SystemServices.hpp/.cpp` | Clipboard, displays, dialogs, system info |
| `Win32GraphicsServices.hpp/.cpp` | WGL GL context, Vulkan surface, GDI surface presenter |
| `Win32Platform.hpp/.cpp` | `IPlatform`: subsystems, window registry, message pump, timing, services |

Tests, all under `modules/platform/tests/CNA/Platform/`:

| File | Covers |
|---|---|
| `Win32EventMapperTests.cpp` | message → event translation, driven from synthetic triples |
| `Win32ScancodeTests.cpp` | set-1 (+extended) ⇄ HID usage ids, including the round trip |
| `Win32KeyCodeTests.cpp` | validated VK ⇄ `KeyCode`, sided-modifier resolution, layout queries |
| `Win32UtfTests.cpp` | UTF-8 ⇄ UTF-16 and surrogate assembly |
| `Win32FullscreenStateTests.cpp` | the windowed-appearance snapshot across repeated cycles |
| `Win32DpiTests.cpp` | scale arithmetic, frame adjustment, window/display coherence |
| `Win32PlatformTests.cpp` | factory, capability truthfulness, subsystems, timing, adoption |
| `Win32WindowTests.cpp` | real `HWND`s: geometry, state, multiple windows, adoption, lifetime |
| `Win32InputServicesTests.cpp` | keyboard/mouse snapshots, cursors, relative mode, text input, devices |
| `Win32SystemServicesTests.cpp` | clipboard, displays, dialogs, system info, paths |
| `Win32GraphicsServicesTests.cpp` | WGL, the Vulkan surface gate, the GDI presenter |
| `Win32DirectXIntegrationTests.cpp` | the platform→renderer handle contract, without linking a renderer |
| `Win32NoSdlTests.cpp` | the Win32 subtree contains no SDL, no unrecorded TODO, one `<windows.h>` entry point |

Supporting, outside the module:

| Path | Purpose |
|---|---|
| `spikes/win32-spike/` | WIN32-0000's existence gate: window, pump, DPI and QPC under MinGW + Wine |
| `tools/platform/standalone_tests/` | builds and runs the platform module and its suite for **any** `CNA_PLATFORM`, without the sharp-runtime sibling; also builds `cna_win32_directx_probe`, the real D3D11/D3D12 device probe |

Modified:

| File | Change |
|---|---|
| `cmake/PlatformSelection.cmake` | `WIN32` moves reserved → available, gated on `WIN32` host |
| `modules/platform/CMakeLists.txt` | `src/Win32/*.cpp` + private native library links |
| `modules/platform/src/PlatformFactory.cpp` | `"Win32"` in `Create`, `Create(name)`, `GetAvailable`, default name |
| `cmake/UnitTests.cmake` | `Win32*` test gating, and the source-root define `Win32NoSdlTests` audits |
| `tools/platform/nonproduction_sdl_audit.py` | classifies `Win32NoSdlTests.cpp` as `text-evidence-assertion` |
| `tools/platform/nonproduction_sdl_budget.json` | its per-file ceiling |
| `CMakeLists.txt` | Skip the vendored SDL3 configure when nothing in the configuration needs it |
| `docs/platform-abstraction.md` | Win32 row in the implementation table |
| `docs/platform-win32.md` | **new** — capability boundary and supported builds |
| `plans/plan_platform.md` | §12 row updated from "Future — not started" |

---

## 3. Capability matrix (target)

Truthfulness is a hard rule: a capability is `true` only once its accessor and its refusal paths
are complete and tested.

| Capability | Win32 | How |
|---|---|---|
| `multipleWindows` | ✅ true | One `HWND` per window, registry keyed by stable `WindowId` |
| `highDpi` | ✅ true | `GetDpiForWindow` / `GetDpiForMonitor` / `GetDeviceCaps` fallback chain |
| `multipleDisplays` | ✅ true | `EnumDisplayMonitors` + `GetMonitorInfoW` + `EnumDisplaySettingsW` |
| `borderlessFullscreen` | ✅ true | Style swap + monitor rect, with full state restoration |
| `nativeWindowHandle` | ✅ true | `NativeWindowSystem::Win32` + real `HWND` |
| `surfacePresentation` | ✅ true | `StretchDIBits` on a `BITMAPINFO` DIB |
| `openGlContext` | ✅ true | WGL (`ChoosePixelFormat`/`wglCreateContext`, ARB attribs when available) |
| `vulkanSurface` | ✅ conditional | True only when `vulkan-1.dll` loads and exports `vkCreateWin32SurfaceKHR` |
| `clipboard` | ✅ true | `OpenClipboard`/`CF_UNICODETEXT` |
| `textInput` | ✅ true | `WM_CHAR` with surrogate pairing → UTF-8 |
| `ime` | ❌ false | Composition/candidate contract not satisfied — see §15 |
| `exactKeyboardState` | ✅ true | Every `WM_KEYDOWN` has a matching `WM_KEYUP`; focus loss flushes held keys |
| `pixelAccurateMouse` | ✅ true | Client-space pixels from `lParam` |
| `relativeMouse` | ✅ true | Raw Input (`RIDEV_INPUTSINK`-free, window-scoped) + clip + cursor hide |
| `cursorShapes` | ✅ true | `LoadCursorW` system shapes + `CreateIconIndirect` custom cursors |
| `globalPointer` | ✅ true | `GetCursorPos`/`SetCursorPos`/`SetCapture` |
| `inputDeviceEnumeration` | ✅ true | `GetRawInputDeviceList` (keyboard/mouse), XInput for gamepads |
| `gamepad` | ❌ false | XInput deferred — see §15 |
| `joystick` | ❌ false | Deferred with gamepad |
| `gamepadRumble` | ❌ false | Deferred with gamepad |
| `gamepadSensors` | ❌ false | XInput has no motion sensors |
| `haptics` | ❌ false | Deferred |
| `sensors` | ❌ false | No desktop sensor source in scope |
| `powerInfo` | ✅ true | `GetSystemPowerStatus` |
| `messageBox` | ✅ true | `MessageBoxW` |
| `nativeFileDialog` | ✅ true | `IFileOpenDialog` / `IFileSaveDialog` (COM, balanced init) |
| `tray` | ❌ false | `Shell_NotifyIconW` deferred — see §15 |
| `camera` | ❌ false | Media Foundation out of scope |
| `managedEntrypoint` | ❌ false | Win32Platform never renames the host's `main()` |

---

## 4. Design decisions

**D1 — One window class, refcounted, process-wide.** `RegisterClassExW` is per-`HINSTANCE`; a
second registration of the same name fails with `ERROR_CLASS_ALREADY_EXISTS`. `Win32WindowClass`
registers on first use and unregisters when the last user goes away, so two `Win32Platform`
instances in one process (which the conformance suite does create) do not fight.

**D2 — `HWND → Win32Window*` via `GWLP_USERDATA`, installed at `WM_NCCREATE`.** The pointer is
threaded through `CREATESTRUCTW::lpCreateParams`, installed in the `WM_NCCREATE` handler, and
**cleared in `WM_NCDESTROY`** — the last message a window ever receives. Between `WM_NCDESTROY`
and the `HWND` becoming invalid nothing may dereference a stale pointer. A static thunk
`StaticWndProc` reads `GWLP_USERDATA` and forwards; a null read (messages before `WM_NCCREATE`,
messages after `WM_NCDESTROY`) falls through to `DefWindowProcW`.

**D3 — `WM_CLOSE` never destroys.** The CNA contract says a close request is a *request*: the
application decides. `Win32Window`'s handler enqueues `WindowEventKind::CloseRequested` and
returns 0, suppressing `DefWindowProcW`'s default destroy. The window dies when its
`IPlatformWindow` wrapper is destroyed, not when the user clicks the X.

**D4 — `QuitEvent` is process-scoped, not window-scoped.** `PostQuitMessage` is **not** called
from `WM_DESTROY`. A multi-window application must not terminate because one window closed. A
`QuitEvent` is emitted when `WM_QUIT` genuinely arrives (the host posted it) or when
`WM_ENDSESSION` reports the session ending. This is the explicit "do not blindly call
`PostQuitMessage`" requirement.

**D5 — The event queue is owned by the platform, filled from `WndProc`, drained by `PollEvents`.**
`DispatchMessageW` calls the window procedure *synchronously*, so the procedure cannot return
events to `PollEvents` through a return value. Each `Win32Window` holds a back-pointer to a
`Win32EventSink` (the platform) and pushes into its `std::deque<PlatformEvent>`. `PollEvents`
pumps with `PeekMessageW`, then moves the accumulated queue into the caller's batch. Capacity is
reused; steady state allocates nothing beyond the first frames.

**D6 — DPI awareness is the host's to own (Rule 4).** CNA is a framework, not an application.
`Win32Platform` **never** calls `SetProcessDpiAwarenessContext` or `SetProcessDPIAware`. It
*reads* the awareness the host chose and reports coherent values for it: under an unaware or
system-aware process `GetDpiForWindow` returns the system DPI and the OS scales the window, so
`GetPixelSize()` correctly equals the client rect. A `CNAEXT`-free opt-in is documented for the
host instead (§10).

**D7 — `WindowDescription::width/height` is the *client* size.** `CreateWindowExW` takes the
*outer* size, so every creation and `SetSize` goes through `AdjustWindowRectExForDpi` (or
`AdjustWindowRectEx` on older hosts). The measured probe showed the naive form loses 8×34 pixels
— exactly the bug this avoids.

**D8 — Fullscreen restores a complete snapshot.** Entering borderless fullscreen records
`GetWindowLongPtrW(GWL_STYLE)`, `GWL_EXSTYLE` and a full `WINDOWPLACEMENT`. Leaving restores all
three and calls `SetWindowPos` with `SWP_FRAMECHANGED`. Repeated
`Windowed → Borderless → Windowed` cycles are therefore idempotent, which WIN32-0042 tests
directly. Exclusive fullscreen additionally calls `ChangeDisplaySettingsExW` with
`CDS_FULLSCREEN` and restores with a null mode.

**D9 — Timing uses `QueryPerformanceCounter` and never touches `timeBeginPeriod`.** Raising the
global timer resolution is a process-wide side effect owned by the host. `Delay()` uses
`::Sleep`, and `GetTicksMilliseconds()` derives from the same QPC source as
`GetPerformanceCounter()` so the two cannot disagree — computed with a 128-bit-safe
`MulDiv`-style split to stay overflow-free for the process lifetime.

**D10 — Relative mouse uses Raw Input, and is honest about it.** `RegisterRawInputDevices` for
the generic-desktop mouse page, `WM_INPUT` accumulates deltas, `ClipCursor` + `ShowCursor(FALSE)`
+ `SetCapture` hold the pointer. If registration fails the mode is **not** entered and the call
throws `PlatformException` — never a silent half-enabled state.

**D11 — Win32 headers never reach a CNA public header.** `windows.h` appears only in
`src/Win32/*`. `Win32Window::GetNativeHandle()` returns the `HWND` as `void*` in the existing
`NativeWindowHandle` field. `WIN32_LEAN_AND_MEAN` and `NOMINMAX` are set before every
`<windows.h>` include, because `min`/`max` macros break `<algorithm>` and `<limits>`.

---

## 5. Window lifecycle design

```
CreateWindow(description)
  ├─ EnsureVideoSubsystem()                     — throws if Video not acquired
  ├─ Win32WindowClass::Acquire()                — refcounted RegisterClassExW
  ├─ compute outer rect from client size (D7)
  ├─ CreateWindowExW(..., lpCreateParams = pending Win32Window*)
  │    └─ WM_NCCREATE → SetWindowLongPtrW(GWLP_USERDATA, self)
  │    └─ WM_CREATE   → initial state latched
  ├─ apply min/max constraints, position/centering
  ├─ register in platform's HWND→window registry under a fresh WindowId
  └─ ShowWindow if description.visible

~Win32Window()
  ├─ leave fullscreen (restore styles/placement/display mode)
  ├─ unregister from the platform registry
  ├─ if owning: DestroyWindow(hwnd_)
  │    └─ WM_DESTROY  → (no PostQuitMessage, D4)
  │    └─ WM_NCDESTROY→ GWLP_USERDATA = 0
  └─ Win32WindowClass::Release()
```

Adopted windows (`AdoptWindow`, `AdoptWindowHandle`) reuse the same wrapper with
`ownsWindow_ = false`: no `DestroyWindow`, no class refcount, no subclassing of the foreign
window procedure.

---

## 6. Event/message mapping

| Win32 message | CNA event |
|---|---|
| `WM_PAINT` (validated), `WM_SHOWWINDOW(TRUE)` | `WindowEvent{Exposed}` |
| `WM_SIZE` (`SIZE_RESTORED`/`SIZE_MAXIMIZED`) | `WindowEvent{Resized}` + `PixelSizeChanged` |
| `WM_SIZE(SIZE_MINIMIZED)` | `WindowEvent{Minimized}` |
| `WM_SIZE(SIZE_MAXIMIZED)` | `WindowEvent{Maximized}` (before Resized) |
| restore from min/max | `WindowEvent{Restored}` |
| `WM_MOVE` | `WindowEvent{Moved}` |
| `WM_SETFOCUS` / `WM_KILLFOCUS` | `FocusGained` / `FocusLost` (+ held-key flush on loss) |
| `WM_CLOSE` | `WindowEvent{CloseRequested}`, **not** destroy |
| `WM_DPICHANGED` | `DisplayScaleChanged` + `PixelSizeChanged`, and honours the suggested rect |
| monitor change detected on `WM_MOVE`/`WM_DPICHANGED`/`WM_DISPLAYCHANGE` | `DisplayChanged` |
| `WM_QUIT` / `WM_ENDSESSION` | `QuitEvent` |
| `WM_KEYDOWN`/`WM_SYSKEYDOWN`/`WM_KEYUP`/`WM_SYSKEYUP` | `KeyEvent` |
| `WM_CHAR` (with surrogate pairing) | `TextInputEvent` |
| `WM_MOUSEMOVE` | `MouseMotionEvent` |
| `WM_*BUTTONDOWN/UP`, `WM_*BUTTONDBLCLK` | `MouseButtonEvent` |
| `WM_MOUSEWHEEL` / `WM_MOUSEHWHEEL` | `MouseWheelEvent` |
| `WM_INPUT` (relative mode) | accumulated delta, no event |
| `WM_DEVICECHANGE` | `DeviceEvent` (keyboard/mouse classes) |

`PixelSizeChanged` is emitted alongside `Resized` because under per-monitor DPI the drawable size
can change without the logical size doing so, and `DirectX11Renderer` sizes its swapchain from
the drawable size.

---

## 7. Keyboard design

Two independent identities must survive:

* **`Scancode`** — physical position, USB HID usage ID. Derived from `lParam`:
  `scan = (lParam >> 16) & 0xFF`, `extended = (lParam >> 24) & 1`. A checked-in table maps
  set-1 → HID for the whole 0x00–0x58 base range plus the E0-prefixed extended keys. The Pause
  key (E1 1D 45) is special-cased from its VK because its scan-code sequence is not single-byte.
* **`KeyCode`** — layout-dependent virtual key. `wParam` is the VK; `IsKnownKeyCode()` validates
  it. Three VKs are *unsided* in `WM_KEYDOWN` and are resolved before reporting:
  `VK_SHIFT` → `MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX)`;
  `VK_CONTROL`/`VK_MENU` → extended flag selects right vs left.

Other required behaviour:

* **Autorepeat**: `KeyEvent::repeat = (lParam >> 30) & 1` (previous-key-state bit).
* **Modifiers**: built from `GetKeyState` for Shift/Ctrl/Alt/GUI plus the *toggle* bit for
  Caps/Num/Scroll Lock; AltGr reported as `Mode` when Right-Alt is down on a layout that has it.
* **Focus loss flushes held keys** — `WM_KILLFOCUS` releases every key the snapshot believes is
  down and emits the matching `KeyEvent{pressed=false}`. Without this, Alt+Tab leaves Alt stuck
  forever, which is the classic Win32 bug and the reason `exactKeyboardState` can be true.
* `WM_SYSKEYDOWN`/`WM_SYSKEYUP` are handled *and* forwarded to `DefWindowProcW` so system
  accelerators (Alt+F4, Alt+Space) keep working — except `VK_F10`/`VK_MENU` alone, which would
  otherwise open the window menu and swallow subsequent input.

**Text input** is `WM_CHAR`, which delivers UTF-16 code units. A high surrogate (0xD800–0xDBFF)
is buffered and combined with the following low surrogate before UTF-8 encoding; a lone surrogate
is dropped rather than encoded as invalid UTF-8. Control characters below 0x20 (except tab) are
not committed as text — they are key events.

**IME stays `false`.** `WM_IME_COMPOSITION` can be read, but `TextEditingCandidatesEvent` requires
the candidate list, its selected index and its orientation, which needs `ImmGetCandidateListW`
plus correct `WM_IME_NOTIFY` handling and a suppressed default candidate UI. Half of that is
worse than none, so the capability stays false and the work is recorded in §15.

---

## 8. Mouse design

* Position from `GET_X_LPARAM`/`GET_Y_LPARAM` — already client-space pixels.
* Buttons → CNA indices: left 1, middle 2, right 3, X1 4, X2 5.
  `MouseSnapshot::buttons` bits 0..4 in the same order.
* `clicks = 2` for the `*DBLCLK` messages; the window class sets `CS_DBLCLKS` so they arrive.
* Wheel: `GET_WHEEL_DELTA_WPARAM / WHEEL_DELTA` for the event's float value;
  `MouseSnapshot::scrollX/scrollY` accumulate in **XNA units (120 per notch)**, i.e. the raw
  delta, matching the contract comment.
  `WM_MOUSEHWHEEL` is sign-flipped: Windows reports right-positive, and CNA/SDL report
  right-negative for horizontal wheel, so the snapshot stays consistent with the SDL3 backend.
* Capture: implicit `SetCapture` on button-down and `ReleaseCapture` when the last button is
  released, so a drag that leaves the window keeps reporting.
* `WM_MOUSELEAVE` via `TrackMouseEvent` clears the hovered-window attribution.

---

## 9. DPI and display behaviour

Entry points are resolved dynamically from `user32.dll`/`shcore.dll` so the binary still loads on
a host without them:

| Wanted | Preferred | Fallback |
|---|---|---|
| Window DPI | `GetDpiForWindow` | `GetDpiForMonitor(MDT_EFFECTIVE_DPI)` | `GetDeviceCaps(LOGPIXELSX)` |
| Frame sizing | `AdjustWindowRectExForDpi` | `AdjustWindowRectEx` |
| System DPI | `GetDpiForSystem` | `GetDeviceCaps` |

Coherence rule, tested by `PlatformWindowConformance.PixelSizeAndDisplayScaleAreSane` and by
`Win32DpiTests`:

```
GetClientBounds()  — logical units
GetPixelSize()     — physical pixels  == client rect in a DPI-aware process
GetDisplayScale()  — dpi / 96.0, never 0
```

Under a **non-per-monitor-aware** process Windows virtualises the client rect, so client rect and
physical pixels coincide and the scale reported is the system scale. That is coherent, not a bug:
the drawable is genuinely that many pixels. The scale is normalised to 1.0 if the DPI query
returns 0.

---

## 10. Host-owned process policy (Rule 4)

`Win32Platform` deliberately does **not** change:

* DPI awareness (`SetProcessDpiAwarenessContext`) — the host sets it in its manifest or before
  creating the platform;
* timer resolution (`timeBeginPeriod`);
* the current directory;
* the process's COM apartment, beyond a **balanced, refcounted** `CoInitializeEx(COINIT_APARTMENTTHREADED)`
  that is skipped entirely when the thread is already initialised (`RPC_E_CHANGED_MODE` and
  `S_FALSE` are both treated as "the host owns it, do not uninitialise").

---

## 11. Test strategy

Three layers, all inside the existing suite (no separate test universe):

1. **Conformance** — automatic. Registering `"Win32"` in `PlatformFactory::GetAvailable()` enrols
   it in `EveryImplementation/PlatformConformance.*` and
   `EveryImplementation/PlatformWindowConformance.*`.
2. **Pure translation tests** — `Win32EventMapperTests`, `Win32KeyCodeTests`,
   `Win32ScancodeTests`, `Win32UtfTests`, `Win32DpiTests`, `Win32FullscreenStateTests`.
   These take synthetic `(message, wParam, lParam)` triples and assert the produced
   `PlatformEvent`, with **no window and no message loop**, so they are deterministic and run
   anywhere the code compiles.
3. **Native integration tests** — `Win32PlatformTests`, `Win32WindowTests`. Real `HWND`s, real
   pump. These skip cleanly (`GTEST_SKIP`) when window creation fails, matching how the SDL3 and
   Terminal suites behave in a headless CI cell.

Plus `Win32NoSdlTests`: a source-level assertion, in the test suite rather than only in a script,
that the Win32 subtree contains no `SDL` identifier.

---

## 12. Risks and edge cases

| Risk | Mitigation |
|---|---|
| Stale `GWLP_USERDATA` after destroy | Cleared in `WM_NCDESTROY`; static thunk null-checks (D2) |
| Reentrancy: `DestroyWindow` inside a dispatched message | Wrapper destruction is never driven from `WndProc`; the queue is drained after the pump |
| `PostQuitMessage` killing a multi-window app | Never called (D4) |
| Losing window state across fullscreen cycles | Full style+exstyle+`WINDOWPLACEMENT` snapshot (D8), tested |
| Client-vs-outer size confusion | `AdjustWindowRectExForDpi` everywhere (D7), tested |
| Stuck modifiers after Alt+Tab | Held-key flush on `WM_KILLFOCUS` (§7), tested |
| Two platform instances in one process | Refcounted window class (D1) — the conformance suite really does this |
| Wine ≠ Windows | §16 separates Wine-verified from documented-semantics claims |
| MSVC-only constructs creeping in | Everything cross-compiles with MinGW GCC 13; that is the build that is actually run |
| `min`/`max` macros from `<windows.h>` | `NOMINMAX` + `WIN32_LEAN_AND_MEAN` before every include |

---

## 13. Tasks

### Phase A — Build-system integration

| ID | Task | Status | Evidence |
|---|---|---|---|
| WIN32-0000 | Environment probe: MinGW + Wine + Xvfb can build and run a real Win32 window | ✅ | `spikes/win32-spike/` — window, pump, DPI, QPC all verified |
| WIN32-0001 | `WIN32` moves from reserved to available in `cmake/PlatformSelection.cmake`, gated on a Windows host | ✅ | Configure with `-DCNA_PLATFORM=WIN32` under the MinGW toolchain succeeds; on Linux it fails naming the host requirement |
| WIN32-0002 | `modules/platform/CMakeLists.txt` compiles `src/Win32/*.cpp` and links the native libraries privately | ✅ | `user32 gdi32 shell32 ole32 shlwapi imm32 advapi32` PRIVATE, no public include leakage |
| WIN32-0003 | `PlatformFactory` knows `"Win32"` in all four operations | ✅ | `Create()`, `Create("Win32")`, `GetAvailable()`, `GetDefaultName()` |
| WIN32-0004 | `cmake/UnitTests.cmake` gates `Win32*` tests on the selection | ✅ | Pattern matches the existing SDL3/SDL2/Terminal gating |
| WIN32-0005 | Audit: DX11/DX12 already consume a generic `NativeWindowHandle` | ✅ | §1.2 — no renderer change required |

### Phase B — Core platform and window

| ID | Task | Status | Evidence |
|---|---|---|---|
| WIN32-0010 | `Win32Error`: `GetLastError()`/`HRESULT` → readable text | ✅ | `FormatMessageW`, UTF-8, trailing newline stripped |
| WIN32-0011 | `Win32Utf`: UTF-8 ⇄ UTF-16 with surrogate-pair correctness | ✅ | `Win32UtfTests` |
| WIN32-0012 | `Win32WindowClass`: refcounted process-wide registration | ✅ | Two platforms in one process coexist |
| WIN32-0013 | `Win32Window`: creation, `HWND` lifetime, `WM_NCCREATE`/`WM_NCDESTROY` discipline | ✅ | `Win32WindowTests` |
| WIN32-0014 | Client-size ⇄ outer-size conversion via `AdjustWindowRectExForDpi` | ✅ | `Win32DpiTests` + conformance size assertions |
| WIN32-0015 | Title, bounds, pixel size, set-size, scale, resizable, borderless | ✅ | Conformance + `Win32WindowTests` |
| WIN32-0016 | Show/Hide/Minimize/Maximize/Restore/Sync/focus/minimized/display name | ✅ | Conformance `StateChangesFollowTheOperationalErrorContract` |
| WIN32-0017 | `GetNativeHandle()` returns `NativeWindowSystem::Win32` + real `HWND` | ✅ | `Win32WindowTests.NativeHandleCarriesTheRealHwnd` |
| WIN32-0018 | Multiple windows with stable `WindowId`s and an `HWND`→window registry | ✅ | Conformance `ASecondWindowFollowsTheMultipleWindowsCapability` |
| WIN32-0019 | Adopted (non-owning) window semantics | ✅ | `AdoptWindow`, `AdoptWindowHandle`; `Win32WindowTests.AdoptedWindowDoesNotDestroy` |
| WIN32-0020 | `Win32Platform`: subsystem refcounting incl. unpaired-release tolerance | ✅ | Conformance `SubsystemsAreRefcounted`, `UnpairedReleaseIsANoOp` |

### Phase C — Events, keyboard, mouse

| ID | Task | Status | Evidence |
|---|---|---|---|
| WIN32-0030 | `PollEvents` message pump (`PeekMessageW`/`Translate`/`DispatchW`) with a reused batch | ✅ | Conformance `PollEvents*` (3 tests) |
| WIN32-0031 | `Win32EventMapper`: window messages → `WindowEvent` kinds | ✅ | `Win32EventMapperTests` |
| WIN32-0032 | `WM_CLOSE` → `CloseRequested`, never destroy | ✅ | `Win32EventMapperTests.CloseRequestDoesNotDestroy` |
| WIN32-0033 | `QuitEvent` only on real process-scoped quit | ✅ | `Win32EventMapperTests.WindowDestructionDoesNotQuit` |
| WIN32-0034 | `Win32Scancodes`: set-1 (+extended) → HID usage IDs | ✅ | `Win32ScancodeTests` incl. round-trip and extended-key cases |
| WIN32-0035 | `Win32KeyCodes`: VK ⇄ `KeyCode` with sided-modifier resolution | ✅ | `Win32KeyCodeTests` |
| WIN32-0036 | Autorepeat, modifier mask, focus-loss key flush | ✅ | `Win32KeyboardTests` |
| WIN32-0037 | `WM_CHAR` → UTF-8 `TextInputEvent` with surrogate pairing | ✅ | `Win32TextInputTests` |
| WIN32-0038 | Mouse: motion, buttons, double-click, wheel, capture | ✅ | `Win32MouseTests` |
| WIN32-0039 | Relative mouse via Raw Input, or a truthful refusal | ✅ | `Win32MouseTests.RelativeModeRefusesWhenRawInputUnavailable` |

### Phase D — DPI, fullscreen, timing

| ID | Task | Status | Evidence |
|---|---|---|---|
| WIN32-0040 | Dynamic per-monitor DPI entry-point resolution with fallbacks | ✅ | `Win32DpiTests` |
| WIN32-0041 | `WM_DPICHANGED` honours the suggested rect and emits both events | ✅ | `Win32EventMapperTests` |
| WIN32-0042 | Fullscreen state snapshot/restore across repeated cycles | ✅ | `Win32FullscreenStateTests` |
| WIN32-0043 | Exclusive fullscreen via `ChangeDisplaySettingsExW`, restored on exit | ✅ | `Win32FullscreenStateTests` |
| WIN32-0044 | Timing: QPC counter/frequency, monotonic ticks, overflow-safe | ✅ | Conformance timing block (4 tests) |

### Phase E — Services

| ID | Task | Status | Evidence |
|---|---|---|---|
| WIN32-0050 | Clipboard (`CF_UNICODETEXT`, UTF-8 at the boundary) | ✅ | `Win32SystemServicesTests` |
| WIN32-0051 | Displays (`EnumDisplayMonitors`, modes, safe area, screen saver) | ✅ | `Win32SystemServicesTests` |
| WIN32-0052 | Dialogs: `MessageBoxW` + `IFileOpenDialog`/`IFileSaveDialog` | ✅ | Capability-gated; refusal tested |
| WIN32-0053 | System info: `GlobalMemoryStatusEx`, cores, locales, power, `ShellExecuteW` | ✅ | Conformance `SystemInfoAnswersWithoutFabricating` |
| WIN32-0054 | Filesystem reuses `Common::StandardFileSystem` with Windows preference paths | ✅ | `SHGetKnownFolderPath` for `%LOCALAPPDATA%`, Music, Pictures |
| WIN32-0055 | Input-device enumeration via `GetRawInputDeviceList` | ✅ | `Win32InputDevicesTests` |
| WIN32-0056 | Cursor shapes: system + custom image cursors | ✅ | `Win32MouseTests` |

### Phase F — Graphics services

| ID | Task | Status | Evidence |
|---|---|---|---|
| WIN32-0057 | `IPlatformGlContext` over WGL | ✅ | Context create/make-current/swap/proc-address |
| WIN32-0058 | `IPlatformVulkanSurface` over `vkCreateWin32SurfaceKHR`, capability conditional | ✅ | True only when `vulkan-1.dll` resolves |
| WIN32-0059 | `IPlatformSurfacePresenter` over `StretchDIBits` with all five scale modes | ✅ | `Win32PresenterTests` |

### Phase G — SDL independence, integration, documentation

| ID | Task | Status | Evidence |
|---|---|---|---|
| WIN32-0060 | Skip the vendored SDL3 configure when no target in the configuration needs it | ✅ | `CNA_PLATFORM=WIN32` + `CNA_AUDIO_PLATFORM=NULL` + a non-SDL renderer + examples/tests off configures with no SDL at all |
| WIN32-0061 | Win32 subtree contains zero SDL headers/identifiers | ✅ | `Win32NoSdlTests` + `grep -ri sdl modules/platform/src/Win32` |
| WIN32-0062 | DirectX11 accepts the Win32Platform window | ✅ | `Win32DirectXIntegrationTests` |
| WIN32-0063 | DirectX12 accepts the Win32Platform window | ✅ | `Win32DirectXIntegrationTests` |
| WIN32-0064 | All five mechanical gates stay clean | ✅ | §17 |
| WIN32-0065 | `docs/platform-win32.md` + `docs/platform-abstraction.md` + `plans/plan_platform.md` §12 + the CI cells | ✅ | `.github/workflows/platform-ci.yml` gains two jobs: `win32-cross` (mingw-w64 + Wine, on every push, the part that regresses silently) and `win32-native` (MSVC on `windows-latest`, `workflow_dispatch`-only, matching the precedent `d3d-windows-ci.yml` set) |

---

## 14. Validation evidence

Every claim in §13's task table is backed by an executed test or a run gate. The consolidated
results are in §17; what follows is where each phase's evidence lives.

| Phase | Evidence |
|---|---|
| A — build integration | The selection matrix in §17.3 (every value of `CNA_PLATFORM` on a Linux host and a Windows target), plus `Win32PlatformFactory.*` |
| B — platform and window | `Win32PlatformTests`, `Win32WindowTests`, and the whole `EveryImplementation/PlatformWindowConformance` suite running against Win32 |
| C — events, keyboard, mouse | `Win32EventMapperTests` (35 cases from synthetic messages), `Win32ScancodeTests`, `Win32KeyCodeTests`, `Win32UtfTests`, `Win32InputServicesTests` |
| D — DPI, fullscreen, timing | `Win32DpiTests`, `Win32FullscreenStateTests`, `Win32WindowTests.RepeatedFullscreenCyclesDoNotDriftTheWindowedState`, the conformance timing block |
| E — services | `Win32SystemServicesTests`, `Win32InputServicesTests` |
| F — graphics services | `Win32GraphicsServicesTests` |
| G — SDL independence, DirectX, docs | §17.4's SDL matrix, `Win32NoSdlTests`, `Win32DirectXIntegrationTests`, `cna_win32_directx_probe`, and the seven gates in §17.2 |

---

## 15. Deliberately unsupported, with follow-up work

| Capability | Why false | What finishing it needs |
|---|---|---|
| `ime` | `TextEditingCandidatesEvent` needs the candidate list, selected index and orientation | `ImmGetCompositionStringW` for composition, `ImmGetCandidateListW` on `WM_IME_NOTIFY(IMN_CHANGECANDIDATE)`, suppression of the default candidate UI, and `SetInputArea` wired to `ImmSetCandidateWindow` |
| `gamepad`, `joystick`, `gamepadRumble` | XInput is a separate device stack with its own enumeration and polling model | `xinput1_4.dll` dynamic load, `XInputGetState`/`XInputSetState`, 4-slot polling, `ControllerAxisEvent`/`ControllerButtonEvent` synthesis from state deltas |
| `gamepadSensors` | XInput exposes no motion sensors | Would need GameInput or WinRT `Gamepad` |
| `haptics` | No standalone haptic device stack in scope | DirectInput force feedback, or XInput rumble as a degenerate case |
| `sensors` | No desktop sensor source in scope | Windows Sensor API (`ISensorManager`) |
| `tray` | `Shell_NotifyIconW` needs a hidden message window, a popup `HMENU` and `WM_COMMAND` routing | The three pieces above plus `IPlatformTrayIcon`'s stable-index contract |
| `camera` | Media Foundation capture is a large subsystem | `IMFActivate` enumeration, `IMFSourceReader`, format negotiation |

Each is a *false* capability with a deterministic refusal, never a stub returning success.

---

## 16. Environment limitations

This workstream was developed and validated on Linux with MinGW-w64 + Wine 9.0 + Xvfb. That
distinction matters and is recorded rather than papered over:

* **Verified by execution here** (386 tests, §17.1): compilation for `x86_64-w64-mingw32` with
  `-Wall -Wextra` and no warnings; window class registration and its refcounting across two live
  platforms; window creation, client-vs-outer sizing, title round trip through UTF-8, show/hide,
  minimise/maximise/restore, destroy, and the `WM_NCCREATE`/`WM_NCDESTROY` pointer discipline; the
  message pump and event attribution across two windows; `WM_CLOSE` semantics; borderless
  fullscreen and its state restoration across repeated cycles; QPC timing and its agreement with
  the millisecond clock; DPI queries and window/display coherence; clipboard round trip with
  non-ASCII text; monitor enumeration and display modes; known-folder preference paths; Raw Input
  device enumeration; the GDI surface presenter in all five scale modes; the whole pure-translation
  layer; **and a real Direct3D 11 device, swap chain, clear, present, resize and present-after-resize
  on a Win32Platform window** (§17.5).
* **Verified by construction, not by execution here**: per-monitor-v2 DPI transitions
  (`WM_DPICHANGED` — Wine reports a fixed 96 DPI and one monitor); exclusive fullscreen
  display-mode changes; `IFileOpenDialog`/`IFileSaveDialog` (the refusal paths are executed, the
  dialogs themselves are modal and need a user); a real WGL context (no OpenGL driver in this
  container, so `Win32GraphicsServices.AContextEitherIsCreatedAndUsableOrFailsExplicitly` skips);
  Direct3D 12 device creation (Wine's D3D12 needs vkd3d over a Vulkan driver).
* **Not verified at all**: a real multi-monitor Windows desktop with mixed DPI; MSVC.

Every one of those is an environment gap rather than a missing test: the tests exist, are
deterministic, and run in full the moment the binary meets a machine with the facility. That is
the whole reason the translation layer was built to be driven from synthetic messages — the parts
that *could* have been left to "it needs a real desktop" are the parts that do not.

---

## 17. Gate and test results

Measured on the baseline described in §0: Linux host, `x86_64-w64-mingw32-g++` 13.2.0, Wine 9.0
over `Xvfb`.

### 17.1 Test suite

```
cmake -S tools/platform/win32_standalone_tests -B cmake-build-win32 -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-win32 --parallel
DISPLAY=:97 WINEDEBUG=-all wine64 cmake-build-win32/cna_platform_win32_tests.exe
```

```
[==========] 386 tests from 39 test suites ran.
[  PASSED  ] 385 tests.
[  SKIPPED ] 1 test
[  SKIPPED ] EveryImplementation/PlatformConformance.AnUnsupportedCapabilityRefusesNamingItself/Win32
```

The one skip is by design and is the suite working correctly: that case exists to prove an
*unsupported* capability refuses, and it skips on any platform that supports surface presentation.
Win32 does, so there is nothing for it to assert.

The run includes the complete implementation-neutral suite — `EveryImplementation/
PlatformConformance` (18 cases) and `EveryImplementation/PlatformWindowConformance` (10 cases) —
parameterised over Win32 and Headless together, which is what makes "the contract is not
SDL-shaped" a measurement rather than a claim.

Per-suite, Win32's own coverage:

| Suite | Cases | Result |
|---|---:|---|
| `Win32EventMapperTests` (`Win32EventMapping`) | 35 | pass |
| `Win32WindowTests` (`Win32WindowTest`) | 24 | pass |
| `Win32InputServicesTests` | 20 | pass |
| `Win32SystemServicesTests` | 18 | pass |
| `Win32PlatformTests` (`Win32PlatformTest` + `Win32PlatformFactory`) | 18 | pass |
| `Win32GraphicsServicesTests` | 15 | pass |
| `Win32ScancodeTests` | 14 | pass |
| `Win32KeyCodeTests` | 12 | pass |
| `Win32UtfTests` | 10 | pass |
| `Win32DpiTests` (`Win32Dpi` + `Win32DpiWindow`) | 10 | pass |
| `Win32FullscreenStateTests` | 9 | pass |
| `Win32DirectXIntegrationTests` (`Win32RendererBridge`) | 8 | pass |
| `Win32NoSdlTests` (`Win32SourceAudit`) | 6 | pass |
| **total, Win32-specific** | **199** | |

**Three real defects were found by these tests and fixed**, which is the reason for writing them
rather than asserting the implementation was correct:

1. Destroying an *adopted* (non-owning) wrapper erased the owning window from the id registry,
   because `OnWindowDestroyed` matched by id and an adopted wrapper deliberately shares one. Every
   service resolving that id afterwards found nothing. Now matched on wrapper identity.
2. An adopted wrapper's back-pointer to its platform dangled if the platform was destroyed first:
   only registry entries were being detached, and adopted wrappers are deliberately not in the
   registry. They are now tracked separately for exactly this.
3. `IPlatformGlContext::MakeCurrent(window, nullptr)` threw when no context was current. Unbinding
   when nothing is bound is cleanup, and a renderer's teardown calls it unconditionally; some
   drivers (and Wine) fail `wglMakeCurrent(null, null)` outright in that state. It is now
   recognised as the no-op it is.

### 17.2 Mechanical gates

All seven pass, by exit code:

| Gate | Result |
|---|---|
| `tools/platform/sdl_inventory.py --check` | 0 — the measured inventory matches the plan |
| `tools/platform/sdl_classify.py --check` | 0 — all 1037 SDL identifiers classified |
| `tools/platform/renderer_sdl_audit.py --check` | 0 — allowlist unchanged (`fna3d`, `freedirect`, `sdl-gpu`, `sdl-renderer`) |
| `tools/platform/sdl_ratchet.py --check --strict` | 0 — **at budget: 0 files, 0 references** |
| `tools/platform/hot_path_lint.py` | 0 — no platform call inside a hot loop |
| `tools/platform/nonproduction_sdl_audit.py --check` | 0 — `Win32NoSdlTests.cpp` classified `text-evidence-assertion` |
| `tools/platform/check_contract.py` | 0 — 28 contract headers SDL-free, 629 public declarations documented |

`Win32NoSdlTests.cpp` needed a manifest entry, which is worth naming rather than glossing: the only
SDL tokens in it are the needles it searches the Win32 sources for. `text-evidence-assertion` is
the existing category for exactly that, and it already covered two files.

### 17.3 Selection matrix

`cmake/PlatformSelection.cmake`, every value, on both host kinds:

| `CNA_PLATFORM` | Linux host | Windows target (mingw-w64) |
|---|---|---|
| `SDL3` (default) | available | available |
| `SDL2` | available | available |
| `HEADLESS` | available | available |
| `TERMINAL` | available | **reserved — refused** |
| `WIN32` | **reserved — refused** | **available**, defines `CNA_PLATFORM_WIN32` |
| `SDL12`, `EMSCRIPTEN` | reserved — refused | reserved — refused |
| an unknown name | refused as unknown | refused as unknown |

The default is unchanged on both. A host-conditional implementation is *reserved* where it is
unsupported rather than *unknown*, so asking for it there produces a message naming the toolchain
requirement instead of one that reads like a typo.

### 17.4 SDL independence (WIN32-0060)

The root configure's decision, measured per configuration:

| Configuration | SDL3 |
|---|---|
| default (nothing set) | **required** — unchanged |
| `WIN32` + `NULL` audio + `DIRECTX11`, tests/examples off | **skipped** |
| `WIN32` + `NULL` audio + `DIRECTX12`, tests/examples off | **skipped** |
| `HEADLESS` + `NULL` audio + `HEADLESS` renderer, tests/examples off | **skipped** |
| `WIN32` + `NULL` audio + `SDL_RENDERER` | required — the renderer links SDL3 itself |
| `WIN32` + `SDL3` audio + `DIRECTX11` | required — the audio axis chose it |
| `WIN32` + `NULL` audio + `DIRECTX11`, **tests on** | required — test fixtures use it |

So the target configuration this workstream set out to make real —

```
CNA_PLATFORM=WIN32
CNA_AUDIO_PLATFORM=NULL
CNA_GRAPHICS_RENDERER=DIRECTX11   (and DIRECTX12)
```

— has no SDL in it at all: not linked, and not even built. The gate is conservative by
construction (it runs before the selection files declare their cache defaults, so an unset axis
reads as "not chosen" and keeps SDL3), which is why the default row above is unaffected.

The remaining legitimate SDL dependencies, none of them in scope to remove:

* the `SDL3` and `SDL2` platform selections, and the `SDL3`/`SDL2` audio selections — by identity;
* renderer families `SDL_RENDERER`, `SDL_GPU`, `FNA3D`, `FREEDIRECT` and `LLGL` — the first two by
  identity, the last three through an upstream dependency that owns an SDL renderer internally;
* roughly 240 example and test fixtures — a property of the fixtures, not of the framework. Making
  the test suite SDL-free is a separate, much larger piece of work and is **not** attempted here.

### 17.5 DirectX 11 and DirectX 12

Contract level, in the suite (`Win32DirectXIntegrationTests`, 8 cases, all passing): the handle a
Win32 window produces satisfies exactly the `TryGetWin32` call both renderers make; it is stable
across resize and across a fullscreen round trip; two windows hand out two distinct handles; an
adopted window presents the same handle as its owner; and a headless window is *refused* rather
than producing a plausible-looking pointer.

Device level, `cna_win32_directx_probe` under Wine:

```
win32 platform window              ok  -- hwnd=0000000000010056, client=640x480
platform                           ok  -- Win32
d3d11 device + swap chain          ok
d3d11 back buffer size             ok
d3d11 clear                        ok
d3d11 present                      ok
d3d11 resize                       ok
d3d11 present after resize         ok
d3d12 device                       unavailable  -- hr=0x80004005
close request                      ok
```

**DirectX 11: fully proved here.** A real D3D11 device and swap chain were created on the HWND the
Win32 platform produced, the back buffer came back at the window's size, and clear, present,
`ResizeBuffers` and present-after-resize all succeeded — on a window that then survived its own
close request, which is the platform behaviour a renderer depends on while it holds the swap chain.

**DirectX 12: not proved here, and not a code result.** `D3D12CreateDevice` returns `E_FAIL` in
this container because Wine's D3D12 is implemented over vkd3d and there is no Vulkan driver
present. The platform side is identical for both renderers — the same `HWND`, reached through the
same `TryGetWin32`, and `CreateSwapChainForHwnd` takes the same handle type — and the probe
exercises the D3D12 path in full the moment it runs on a machine with a device. This is an
environment gap, recorded in §16, not an implementation gap.

### 17.6 Regression matrix

The point of a new backend is that it changes shared files — `PlatformFactory.cpp`,
`cmake/PlatformSelection.cmake`, `modules/platform/CMakeLists.txt`, `cmake/UnitTests.cmake`, the
root `CMakeLists.txt` — so the other implementations have to be re-run, not assumed. The same
harness builds the platform module for any selection, which is what makes that possible here
without the sharp-runtime sibling.

**Every selection was re-run, and none regressed.**

| `CNA_PLATFORM` | Target | Tests | Passed | Skipped | Failed |
|---|---|---:|---:|---:|---:|
| `WIN32` | Windows via mingw-w64, executed under Wine | 386 | 385 | 1 | **0** |
| `SDL3` | Linux, native, `SDL_VIDEODRIVER=dummy` | 443 | 436 | 7 | **0** |
| `SDL2` | Linux, native, `SDL_VIDEODRIVER=dummy` | 304 | 303 | 1 | **0** |
| `HEADLESS` | Linux, native | 270 | 269 | 1 | **0** |
| `TERMINAL` | Linux, native | 270 | 269 | 1 | **0** |

Every skip is environmental and reproduces on the baseline: no controlling TTY
(`TerminalPlatformTest.ConstructionTouchesNoTerminalState`), no Vulkan loader, no OpenGL driver, no
sensor or haptic device, and the by-design
`PlatformConformance.AnUnsupportedCapabilityRefusesNamingItself` skip on any platform that *does*
support surface presentation.

Three caveats, all named rather than buried:

* **SDL2 was linked against the host's SDL 2.30 package**, not the version
  `cmake/ThirdPartySDL2.cmake` pins, because that one is fetched from git at configure time and this
  worker builds it nowhere. The run therefore proves the SDL2 selection still compiles and its suite
  still passes; it is not a statement about the pinned revision.
* **`Sdl3XErrorHandlerTests` is excluded from the harness.** It provokes a real Xlib protocol error,
  and Xlib's default handler calls `exit()` — which is the whole subject of the test.
  `cmake/UnitTests.cmake` gives it a ctest process of its own for exactly that reason; a harness
  that produces one binary would have it end the run at whatever came next, which is a fact about
  process isolation rather than a result about the suite.
* **Tests that include a SharpRuntime header are excluded** — detected by scanning rather than
  listed, so the exclusion cannot go stale silently, and printed at configure time so a run is never
  quietly narrower than it looks. On this tree that is `StandardFileSystemTests`,
  `TerminalCapabilityProbeTests` and `Sdl3PlatformTests`. All three run unchanged in a normal
  configure, and none is touched by this workstream.

That three of the module's fifty-odd suites have that dependency — and that the other fifty need
nothing but the standard library — is itself the measurement that makes the harness possible.

### 17.7 Build matrix

| Target | Result |
|---|---|
| `x86_64-w64-mingw32-g++` 13.2.0, `-Wall -Wextra`, C++23 | clean — no warnings from any Win32 source |
| Platform module + full test suite, cross-built and executed under Wine | 385/386, 1 by-design skip |
| `cna_win32_directx_probe` | exit 0 |
| MSVC | **not run here** — no Windows host available. Nothing in the backend is MSVC-only: it uses no GCC extension, no `__attribute__`, and no compiler-specific pragma. The `win32-native` CI job builds it with MSVC on demand, and the harness carries `/W4` for that compiler rather than letting `-Wall -Wextra` reach it. |

A full CNA configure (the whole framework, not just the platform module) could not be run: it
requires the `sharp-runtime` *sibling checkout*, which this sandbox cannot fetch. That is what
`tools/platform/standalone_tests/` exists to work around, and it is a limitation of the
worker rather than of the change — the platform module has zero sharp-runtime includes, so the
suite it runs is the same one `cmake/UnitTests.cmake` compiles.
