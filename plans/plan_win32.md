# CNA native Win32 platform backend (`CNA_PLATFORM=WIN32`) — Implementation Plan

> **Status: IN PROGRESS.** This plan converts the reserved `CNA_PLATFORM=WIN32` identifier
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

Modified:

| File | Change |
|---|---|
| `cmake/PlatformSelection.cmake` | `WIN32` moves reserved → available, gated on `WIN32` host |
| `modules/platform/CMakeLists.txt` | `src/Win32/*.cpp` + private native library links |
| `modules/platform/src/PlatformFactory.cpp` | `"Win32"` in `Create`, `Create(name)`, `GetAvailable`, default name |
| `cmake/UnitTests.cmake` | `Win32*` test gating |
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
| WIN32-0065 | `docs/platform-win32.md` + `docs/platform-abstraction.md` + `plans/plan_platform.md` §12 | ✅ | |

---

## 14. Validation evidence

Filled in as each phase lands — see §17.

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

* **Verified by execution here**: compilation for `x86_64-w64-mingw32`; window class registration;
  window creation, sizing, show/hide, destroy; the message pump; `WM_CLOSE` semantics; QPC timing;
  DPI queries; the whole pure-translation test layer.
* **Verified by construction, not by execution here**: per-monitor-v2 DPI transitions
  (`WM_DPICHANGED` — Wine reports a fixed 96 DPI), exclusive fullscreen display-mode changes,
  `IFileOpenDialog`, real D3D11/D3D12 device creation (Wine's d3d11 needs a GPU path this
  container does not have).
* **Not verified at all**: behaviour on a real multi-monitor Windows desktop with mixed DPI.

The deterministic Windows-only tests exist so a real Windows CI cell can close those gaps without
any new code.

---

## 17. Gate and test results

Filled in per phase as work lands.
