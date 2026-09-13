# CNA native X11 platform (`CNA_PLATFORM=X11`) — implementation plan and ledger

> **Purpose.** Prove that **CNA does not depend existentially on SDL3** by giving CNA a genuine,
> first-class, native X11 platform backend that uses Xlib and the X extensions directly, and by
> making a CNA configuration exist in which SDL is neither configured, built nor linked.
>
> SDL3 stays CNA's default and an excellent optional backend. Nothing here removes, deprecates or
> degrades it. What changes is that SDL stops being the only way CNA can reach a desktop.
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code exists but has not met those criteria; ⬜ not implemented;
> ⛔ understood but blocked, with the blocker named in the row;
> ❌ cut, with the evidence that killed it recorded in the row.

---

## 0. Baseline

| Fact | Value |
|---|---|
| Repository | `libcna/cna` |
| Branch | `claude/x11-native-platform-vie452` (from `next`) |
| Baseline commit | `e05b3d0f026e0926741f89459daf02579240399d` — `merge(RLGL): integrate rlgl renderer into next` |
| Working tree at start | clean (`git status --short` empty); no pre-existing user changes to preserve |
| Sibling `sharp-runtime` | cloned at `/home/user/sharp-runtime`, branch `next`, `0c82d9b8` |
| Host | Ubuntu 24.04, GCC 13.3.0, 4 cores, 15 GiB RAM, no GPU, no X session |
| X11 development libraries present | `x11 1.8.7`, `xext 1.3.4`, `xi 1.8.1`, `xrandr 1.5.2`, `xcursor 1.2.1`, `xfixes 6.0.0`, GLX via `libglvnd-dev 1.7.0`, `vulkan 1.3.275` |
| Test X servers available | `Xvfb`; window manager `openbox`; `xdotool`, `xprop` for injection/inspection |

### 0.1 Source audit — what already existed before this plan

Everything below was read out of the tree at the baseline commit, not assumed.

| Finding | Evidence |
|---|---|
| The native-handle contract **already models X11 correctly**: `NativeWindowSystem::X11`, and `NativeWindowHandle` deliberately carries the XID in a separate `std::uint64_t windowId` field rather than in `void* window`, with the header explaining why a pointer would be wrong. | `modules/platform/include/CNA/Platform/NativeWindowHandle.hpp`, PLAT-13 |
| `TryGetX11()` already exists and already validates *both* `display != nullptr` and `window != 0`. | `modules/platform/src/NativeWindowHandle.cpp:17` |
| **Six renderer families already consume X11 handles** through that accessor, so an X11 platform has real consumers on day one: `wicked`, `llgl`, `diligent`, `webgpu`, `bgfx`, `igl`. | `grep -rn TryGetX11 modules/renderers/` |
| `IPlatformGlContext`, `IPlatformVulkanSurface` and `IPlatformSurfacePresenter` are complete, native-API-free service contracts. GL context creation is keyed by `WindowId`, **not** by `IPlatformWindow`, so a native implementation must maintain its own id→window map. | `modules/platform/include/CNA/Platform/IPlatform*.hpp` |
| `WindowDescription::renderIntent` + `openGlFramebuffer` already exist precisely because *a window intended for OpenGL must be created with the right attributes already set*. For X11 this is the GLX `FBConfig`/`Visual` problem, and the contract already has the field needed to solve it. **No generic change was required.** | `modules/platform/include/CNA/Platform/WindowDescription.hpp`, PLAT-15 |
| `Scancode` values are **USB HID keyboard usage IDs**, and `KeyCode` values are **Windows virtual-key codes**. Neither is an X11 keysym or an X keycode, so both need a real translation. | `Input/Scancode.hpp`, `Input/KeyCode.hpp` |
| The conformance suite is parameterised over `PlatformFactory::GetAvailable()`, so a correctly registered backend joins it automatically. | `PlatformConformanceTests.cpp:452` |
| CNA **already installs a non-fatal Xlib error handler** in the SDL3 backend, resolved through `dlsym`, because Xlib's default handler calls `exit()` and deadlocked the test binary. The precedent and its measured stack trace are recorded in `Sdl3XErrorHandlerTests.cpp`. | `modules/platform/src/Sdl3/Sdl3Platform.cpp:69` |
| **`cna_configure_vendored_sdl()` is called unconditionally from the root `CMakeLists.txt:153`**, before platform, audio and renderer selection. So `-DCNA_PLATFORM=HEADLESS -DCNA_AUDIO_PLATFORM=NULL -DCNA_GRAPHICS_RENDERER=HEADLESS` still fails to configure without the SDL submodules. This is the single build path that makes SDL existential, and it is what X11-0090 addresses. | reproduced: configure error `Missing vendored 'SDL' in .../third_party` |

### 0.2 Baseline test results

Recorded in §14 once measured, before any X11 source exists.

---

## 1. Architecture

```
CNA application
      |
      v
CNA platform abstraction        (CNA::Platform::IPlatform, unchanged)
      |
      v
X11Platform                     (modules/platform/src/X11/)
      |
      +--> XOpenDisplay / Display*
      +--> XCreateWindow / ::Window XID
      +--> native XPending/XNextEvent processing
      +--> XKB keyboard (physical scancode from XKB key NAMES, logical keycode from keysym)
      +--> XIM/XIC committed UTF-8 text
      +--> core pointer + XInput2 raw motion
      +--> XRandR displays, EWMH/ICCCM window management
      +--> clock_gettime(CLOCK_MONOTONIC) timing
      |
      v
NativeWindowHandle{ system = X11, display = Display*, windowId = XID }
      |
      +--> GLX  -> IPlatformGlContext  -> EasyGL / GL-family renderers
      +--> VK_KHR_xlib_surface -> IPlatformVulkanSurface -> Vulkan renderer
      +--> XImage/XPutImage -> IPlatformSurfacePresenter -> CPU rasterisers
      +--> the six renderers that already call TryGetX11()
```

**`platform != renderer` is a hard rule.** There is no `X11VulkanPlatform`, no
`X11OpenGLPlatform`, and no renderer includes an X11 header of CNA's. A renderer asks the generic
service or reads the generic native handle.

**X11 is not Linux.** Nothing in this backend is named `Linux*`, and nothing outside
`src/X11/` learns that the host is X11. `CNA_PLATFORM=WAYLAND` must be addable later without
touching a line of this backend; the only pieces deliberately shared are the genuinely portable
`src/Common/` ones (`StandardFileSystem`, `StandardSystemInfo`) that already exist and are already
shared by three backends.

---

## 2. Design decisions (recorded before implementation)

| # | Decision | Rationale |
|---|---|---|
| D1 | **Xlib, not XCB, as the primary API.** | The contract already models an X11 native window as `Display*` + XID — that *is* the Xlib shape, and it is what `VK_KHR_xlib_surface`, GLX and every one of the six renderers already consuming `TryGetX11` expect. An XCB backend would have to synthesise an `xcb_connection_t` from `Display*` anyway. XCB is used nowhere; there is exactly one connection per platform instance. |
| D2 | **One `Display*` per `X11Platform` instance, opened on `AcquireSubsystem(Video)` and closed on final release.** | The conformance suite constructs two platform instances in one process. A process-global display would make them share state, and `XCloseDisplay` from one would invalidate the other's windows. Refcounted per instance, matching the contract's acquire/release rules. |
| D3 | **Physical scancodes come from XKB *key names*, not from keycode arithmetic.** | `X keycode = evdev code + 8` is true only for the `evdev` ruleset on Linux; `xfree86` and the BSDs differ, and evdev codes are not HID usage IDs anyway. XKB key names (`AD01`, `LatQ`, `ESC`, `SPCE`…) are defined by `xkeyboard-config` and are layout-independent by construction. A name→`Scancode` table is the only mapping that is stable across layouts *and* across X servers. A keycode-offset heuristic is the documented fallback when `XkbGetNames` fails. |
| D4 | **Logical keycodes come from the keysym at group 0, shift level 0**, not from the keysym the current modifier state produces. | `KeyCode` is a Windows virtual-key code; VK codes are unshifted identities (`VK_A`, `VK_OEM_1`), so reading the shifted keysym would report `KeyCode::None` for every capital letter. Group 0 makes the value track the *layout* (AZERTY reports `KeyCode::A` where a US keyboard reports `Q`) while ignoring transient modifiers, which is exactly the contract's "layout-dependent virtual key". |
| D5 | **Autorepeat is detected, never inferred from timing.** | `XkbSetDetectableAutoRepeat` is requested first; when the server grants it, a repeat arrives as `KeyPress` with no intervening `KeyRelease` and `KeyEvent::repeat` is set from the backend's own held-key set. When the server refuses, the classic `KeyRelease`+`KeyPress` pair at the identical timestamp is coalesced by peeking the queue — never by a timeout. Emitting a false physical key-up during autorepeat would break `exactKeyboardState`, which this backend advertises as true. |
| D6 | **Detectable autorepeat is requested per display, which is per platform instance, not process-global.** | `XkbSetDetectableAutoRepeat` affects only the requesting client's connection, so it does not reconfigure the host application's own X connection. |
| D7 | **`XInitThreads()` is never called.** | It must run before any other Xlib call in the process to be correct, and CNA is a library that does not own process startup. Instead every Xlib call this backend makes is serialised on the platform's own mutex and confined to the owning platform instance, and the contract's "poll once per frame" rule keeps that lock uncontended. Documented rather than silently assumed; a host that wants full Xlib thread-safety calls `XInitThreads()` itself before creating the platform, which this backend neither requires nor defeats. |
| D8 | **The X error handler is installed with the previous handler saved, chained and restored.** | Xlib's error handler is process-global and Xlib's default calls `exit()`. The backend installs a handler on first display open, forwards any error whose `display` is not one of ours to the handler it replaced, and restores the previous handler when the last X11 platform closes its display. `XSetIOErrorHandler` is deliberately left alone: a genuine connection loss must not return. |
| D9 | **Locale is set only if the host has not already set one.** | `XOpenIM` needs a locale and `XSupportsLocale()`; calling `setlocale(LC_CTYPE, "")` unconditionally would change the host process's number formatting. The backend reads the current `LC_CTYPE`; if it is still the startup default `"C"`, it sets `LC_CTYPE` **only** from the environment and records that it did. It never touches `LC_ALL`, `LC_NUMERIC` or any other category. |
| D10 | **Display scale is 1.0 unless `Xft.dpi` says otherwise.** | X11 has no authoritative scale. `Xft.dpi` in the resource database is the one value the user's session actually sets deliberately; XRandR physical millimetres are frequently fictional (EDID lies, and a 1×1 mm or 0×0 mm output is common). So: read `Xft.dpi`, sanity-clamp the resulting scale to [0.5, 8.0], and otherwise report exactly 1.0. An absurd DPI derived from a fake physical size is worse than no scaling. |
| D11 | **`BorderlessFullscreen` uses `_NET_WM_STATE_FULLSCREEN` and degrades gracefully; `ExclusiveFullscreen` is refused.** | Genuine exclusive mode means an XRandR mode switch, which changes the user's desktop resolution and can leave it wrong if the process dies. It is implementable but is not implemented here, so `SetFullscreenMode(ExclusiveFullscreen)` throws rather than silently giving the user borderless and calling it exclusive. When the window manager does not advertise `_NET_WM_STATE_FULLSCREEN` in `_NET_SUPPORTED`, the request is refused rather than faked. |
| D12 | **The clipboard is a real selection owner with `TARGETS`, `UTF8_STRING`, `STRING`, `TEXT` and `INCR` for large transfers.** | An X11 clipboard that only works between two CNA windows is not a clipboard. Ownership, `SelectionRequest`, `SelectionNotify`, `SelectionClear` and incremental transfer are all handled, and paste from an external application is what the test asserts. |
| D13 | **A GLX window's `Visual` and `Colormap` are chosen from the `FBConfig` *before* `XCreateWindow`.** | This is the one place where window creation and GL are genuinely coupled. `WindowDescription::renderIntent == OpenGl` plus `openGlFramebuffer` already carries exactly the information needed, so the coupling is resolved at the contract level that already exists — no generic change. A window created with `renderIntent != OpenGl` refuses to host a GL context rather than failing obscurely inside GLX. |
| D14 | **Optional X extensions are optional at *build* time, individually.** | `X11` and `Xext` are mandatory. `Xi` (raw mouse), `Xrandr` (displays), `Xcursor` (shaped cursors), `Xfixes` (pointer barriers/hiding) are each detected separately, each guarded by its own `CNA_X11_HAVE_*` macro, and each turns off exactly one capability when absent. No extension is required because it is convenient. |
| D15 | **`messageBox`, `nativeFileDialog`, `tray`, `camera`, `gamepad`, `joystick`, `haptics`, `sensors` are `false`.** | Core X11 has no standard facility for any of them, and shelling out to `zenity`/`kdialog` is not a native backend. Truthfulness beats checkbox count; the contract's rule is that a false capability *refuses*, which is exactly what these do. |
| D16 | **`ime` is `false` even though XIM is used.** | XIM here delivers *committed* text via `Xutf8LookupString`. CNA's `Ime` capability promises composition and candidate-list events (`TextEditingEvent`, `TextEditingCandidatesEvent`), which need XIM preedit callbacks that are not implemented. `textInput` is `true`; `ime` is `false`. |

---

## 3. SDL3 → X11 behaviour mapping

| CNA contract | SDL3 backend does | X11 backend does |
|---|---|---|
| `AcquireSubsystem(Video)` | `SDL_InitSubSystem(SDL_INIT_VIDEO)` | `XOpenDisplay(nullptr)`, refcounted |
| `CreateWindow` | `SDL_CreateWindow` + flags | `XCreateWindow` on a chosen `Visual`/`Colormap`, `XSetWMProtocols(WM_DELETE_WINDOW)`, `XSetWMNormalHints`, `_NET_WM_NAME`, `XSelectInput` |
| `WindowId` | `SDL_GetWindowID` | CNA-assigned dense counter; XID→`WindowId` map kept by the platform |
| `PollEvents` | `SDL_PollEvent` | `XPending` + `XNextEvent`, mapped by `X11EventMapper` |
| close request | `SDL_EVENT_WINDOW_CLOSE_REQUESTED` | `ClientMessage` with `WM_PROTOCOLS`/`WM_DELETE_WINDOW` → `WindowEventKind::CloseRequested` (the window is **not** destroyed) |
| resize | `SDL_EVENT_WINDOW_RESIZED` | `ConfigureNotify` with a changed size → `Resized` + `PixelSizeChanged` |
| move | `SDL_EVENT_WINDOW_MOVED` | `ConfigureNotify` with a changed position, translated to root coordinates |
| focus | `FOCUS_GAINED/LOST` | `FocusIn`/`FocusOut` with `NotifyGrab`/`NotifyUngrab` modes filtered out |
| minimise/restore | `MINIMIZED`/`RESTORED` | `WM_STATE` `PropertyNotify` (`IconicState` ↔ `NormalState`) |
| maximise | `MAXIMIZED` | `_NET_WM_STATE` `PropertyNotify` containing both `MAXIMIZED_VERT` and `_HORZ` |
| exposure | `SDL_EVENT_WINDOW_EXPOSED` | `Expose` with `count == 0` |
| key | `SDL_EVENT_KEY_DOWN/UP` | `KeyPress`/`KeyRelease` → XKB name → `Scancode`, group-0 keysym → `KeyCode` |
| text | `SDL_EVENT_TEXT_INPUT` | `Xutf8LookupString` on the focused window's XIC |
| mouse motion | `SDL_EVENT_MOUSE_MOTION` | `MotionNotify`, or XI2 `XI_RawMotion` while relative mode is on |
| mouse wheel | `SDL_EVENT_MOUSE_WHEEL` | buttons 4/5 (vertical) and 6/7 (horizontal) → `MouseWheelEvent`, **never** a `MouseButtonEvent` |
| `GetPerformanceCounter` | `SDL_GetPerformanceCounter` | `clock_gettime(CLOCK_MONOTONIC)` in nanoseconds |
| `Delay` | `SDL_Delay` | `clock_nanosleep(CLOCK_MONOTONIC, …)` with `EINTR` resumption |
| clipboard | `SDL_GetClipboardText` | ICCCM selection ownership + `INCR` |
| displays | `SDL_GetDisplays` | XRandR 1.5 monitors, else 1.2 CRTCs, else one screen |
| GL | `SDL_GL_CreateContext` | `glXCreateContextAttribsARB`, else `glXCreateNewContext` |
| Vulkan | `SDL_Vulkan_CreateSurface` | `vkCreateXlibSurfaceKHR` resolved through `vkGetInstanceProcAddr` |
| CPU present | `SDL_Renderer` streaming texture | `XImage` + `XPutImage` (`XShmPutImage` when MIT-SHM is available and local) |

---

## 4. Task ledger

### Phase A — build integration (M1)

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| X11-0001 | `CNA_PLATFORM=X11` selection | ✅ | `cmake/PlatformSelection.cmake` offers `X11` when `cna_detect_x11()` succeeds. An explicit `-DCNA_PLATFORM=X11` on a machine without the headers fails with the reason and the package name, never a fall back. Verified: configure succeeded here with `X11 + Xext; optional present: Xi, Xrandr, Xcursor, Xfixes, XShm, GLX, Vulkan headers`. |
| X11-0002 | Individual X dependency discovery | ✅ | `cmake/PlatformX11.cmake`. `find_package(X11)` plus `find_package(OpenGL COMPONENTS GLX)` and `find_package(Vulkan)`; no hardcoded path anywhere. Mandatory: `X11`, `Xext`, and `X11/XKBlib.h` (without it there is no layout-independent scancode at all). Optional, each publishing its own `CNA_X11_HAVE_<EXT>` and each gating exactly one capability: `Xi`, `Xrandr`, `Xcursor`, `Xfixes`, `XShm`, `GLX`, Vulkan headers. Vulkan is **headers only** -- the surface service resolves `vkCreateXlibSurfaceKHR` through the caller's own loader, so no Vulkan runtime dependency is added. |
| X11-0003 | Module sources and private linkage | ✅ | `modules/platform/CMakeLists.txt` compiles `src/X11/*.cpp` only for the X11 selection and links every X library, include root and definition **PRIVATE**. `ContractIsSdlFreeTests` and `check_contract.py --headers` still pass, so no X header reaches a consumer. |
| X11-0004 | `PlatformFactory` registration | ✅ | `"X11"` in `Create()`, `Create(name)`, `GetAvailable()` and the `kDefaultName` chain. The conformance suite picked it up automatically, which is how the two contract defects in X11-0103 were found. |
| X11-0005 | Test-source gating | ✅ | `cmake/UnitTests.cmake` filters `modules/platform/tests/.../X11*.cpp` out of every non-X11 selection, and gives the X11 selection the X include roots, the `CNA_X11_HAVE_*` definitions and the X libraries through `cna_test_build_config`. |
### Phase B — display, window, lifecycle (M1)

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| X11-0010 | `X11Display` connection lifetime | ✅ | `X11Connection` (`X11Display.hpp/.cpp`). `XOpenDisplay` in the platform constructor, `XCloseDisplay` in its destructor; 25 atoms interned in **one** batched `XInternAtoms` round trip; XKB, XI2 and RandR negotiated once; `_NET_SUPPORTED` and `_NET_SUPPORTING_WM_CHECK` read once and re-read when the root window's properties change. |
| X11-0011 | `X11Error` — scoped error trap + chained global handler | ✅ | `X11ErrorPolicy` + `X11ErrorTrap`. The previous process handler is saved on first registration, consulted for errors on connections CNA did not open, and restored when the last CNA connection closes. `XSetIOErrorHandler` is untouched. `X11ErrorTrap` brackets one request and turns its asynchronous error into a synchronous answer; traps nest and unwind through their destructors, so an exception cannot leave one installed. |
| X11-0012 | `X11Window` creation/destruction | ✅ | `X11Window`. Visual decided by render intent **before** `XCreateWindow`; `WM_PROTOCOLS`/`WM_DELETE_WINDOW`, `WM_CLASS`, `_NET_WM_PID`, `WM_CLIENT_MACHINE`, `_NET_WM_NAME` **and** `WM_NAME`, `WM_NORMAL_HINTS`. Destructor order is XIC, then window, then colormap -- an XIC outliving its window leaves the input method a dangling client window. |
| X11-0013 | Multiple windows + `WindowId` mapping | ✅ | XID->`WindowId` registry in the platform; `WindowId` is a dense CNA counter, never the XID (an XID is a sparse 29-bit server id). `AdoptWindow` returns a **non-owning** `BorrowedWindow` that forwards to the live object rather than constructing a second wrapper over the same XID. Closing a secondary window produces `CloseRequested` only; `QuitEvent` follows only when the last window is asked to close. |
| X11-0014 | Geometry, title, visibility, state | ✅ | Title through `_NET_WM_NAME` + `WM_NAME`; client bounds translated to root coordinates with `XTranslateCoordinates`, because a reparenting window manager makes `attributes.x/y` relative to the decoration frame; `Show`/`Hide`/`Minimize`/`Maximize`/`Restore`; `Sync()` is `XSync`. |
| X11-0015 | Resizable / borderless / size constraints | ✅ | `WM_NORMAL_HINTS` min/max, with an unconstrained axis pinned to the protocol's 32767 rather than to zero; borderless through `_MOTIF_WM_HINTS` (declared locally -- the property is five `long`s frozen since 1989, and Motif is not a dependency worth adding for it). |
| X11-0016 | Native handle | ✅ | `{X11, display, windowId = XID}` with `window` left null, per the contract's own explanation of why an XID is not an address. `TryGetX11` succeeds on it; the conformance suite's `NativeHandleAgreesWithTheCapability` passes. |
### Phase C — event loop (M2)

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| X11-0020 | `X11EventMapper` | ✅ | `X11Platform::PollEvents` + `X11EventMapper`. `XPending` then `XNextEvent`, which is non-blocking by construction. `XFilterEvent` gets first refusal so an IME's composition keystrokes are not also delivered as key events. No per-event allocation beyond the `std::string` the contract's `TextInputEvent` already owns. |
| X11-0021 | Window-state derivation | ✅ | `ConfigureNotify` (size vs. position, and only a **synthetic** one carries root coordinates), `Expose` with `count == 0`, `MapNotify`/`UnmapNotify` distinguished from iconification by `WM_STATE`, `FocusIn`/`FocusOut` with grab-induced changes filtered, `PropertyNotify` on `_NET_WM_STATE`, `DestroyNotify`. Nothing is synthesised that X11 does not justify. |
| X11-0022 | Close request semantics | ✅ | `WM_DELETE_WINDOW` -> `CloseRequested`, and the window is **not** destroyed -- the application answers. A `QuitEvent` is appended only when this is the last window. |
### Phase D — keyboard (M3)

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| X11-0030 | XKB key-name → `Scancode` table | ✅ | `ScancodeFromXkbKeyName` + `XkbGetNames(XkbKeyNamesMask)`. 100+ names mapped; the four-byte NUL-padded field is compared length-bounded **and** padding-checked, so `KP1` cannot match `KP10`. 9 unit tests, no X server needed. |
| X11-0031 | Keysym → `KeyCode` (group 0) | ✅ | `KeyCodeFromKeysym`, fed from `XkbKeycodeToKeysym(group 0, level 0)`. Both letter-case ranges fold onto the upper-case virtual key; the F12/F13 virtual-key gap is handled as two ranges; keypad digits and their Num-Lock-off navigation aliases agree; AltGr reports `RightAlt`. 6 unit tests. |
| X11-0032 | Modifier state | ✅ | `ModifiersFromXState`. The AltGr bit is **looked up** from `XGetModifierMapping` rather than hardcoded to Mod5, because which modifier carries `Mode_switch` is the layout's choice. 3 unit tests. |
| X11-0033 | Autorepeat | ✅ | `XkbSetDetectableAutoRepeat` requested per connection (which is per client, so the host process's own X connection is untouched). When the server refuses, the release/press pair at the **identical timestamp** is coalesced by peeking one event ahead -- never by a timeout. 3 unit tests including the deliberate-double-tap case. |
| X11-0034 | Focus-loss key release | ✅ | `FocusOut` calls `X11Keyboard::ReleaseAllKeys()`. Without it the `KeyRelease` goes to whichever window has focus now and the key sticks down forever. |
| X11-0035 | `IPlatformKeyboard` snapshot | ✅ | `Update()` reads `XQueryKeymap` -- the server's own vector -- rather than accumulating events, so the snapshot is correct after a missed event. Modifiers from `XkbGetState` (effective + locked), falling back to `XQueryPointer`'s mask. |
### Phase E — text input (M4)

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| X11-0040 | XIM/XIC lifecycle | ✅ | `XOpenIM` once per connection, one `XIC` per window with `XIMPreeditNothing|XIMStatusNothing`, `XSetICFocus`/`XUnsetICFocus` on focus change, contexts destroyed by their windows before the XIM closes. A display with no input-method server degrades to `XLookupString` and `textInput` stays true. |
| X11-0041 | UTF-8 committed text | ✅ | `Xutf8LookupString` with the mandatory `XBufferOverflow` regrowth (the first call commits **nothing** and reports the size, which is how a phrase-committing IME produces truncated mojibake when it is treated as a length). `XLookupString`'s Latin-1 fallback is transcoded to UTF-8 rather than passed through. Control characters are suppressed: they are key events, not text. |
| X11-0042 | Locale policy | ✅ | Only `LC_CTYPE`, only when it is still the startup `"C"`/`"POSIX"` default, and only from the environment. `LC_ALL` and `LC_NUMERIC` are never touched, so adding CNA cannot make a host process start printing `3,14`. |
### Phase F — mouse (M5)

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| X11-0050 | Buttons, motion, enter/leave | ✅ | Buttons 1/2/3 pass through; buttons >= 8 are renumbered down to 4,5,6 so CNA sees no hole where the wheel was. Double click detected from timestamp **and** position (500 ms, 4 px) -- X11 has no double-click concept at all. |
| X11-0051 | Wheel | ✅ | Buttons 4/5/6/7 become `MouseWheelEvent` only, and only on the press half; the release half is dropped so one notch is not counted twice. `MapButtonNumber` returns 0 for them, so no `MouseButtonEvent` can ever carry a wheel. 3 unit tests. |
| X11-0052 | `IPlatformMouse` snapshot + cursor | ✅ | `XQueryPointer`-backed snapshot through the window the snapshot already names, `XWarpPointer` for `SetPosition`, a 1x1 transparent pixmap cursor for hiding (XFixes' hide is screen-wide, which is not what the contract promises), `XGrabPointer` with `confine_to = None` for capture -- confining is relative mode's job and would break dragging past the window edge. |
| X11-0053 | XInput2 raw relative motion | ✅ | `XIQueryVersion(2,0)` + `XISelectEvents(XI_RawMotion)` on the root, plus a confining grab and a hidden cursor. Raw deltas bypass pointer acceleration, screen clipping and warps entirely, which removes both classic failure modes at once. The valuator mask is **walked**, not indexed -- `raw_values` is packed, so indexing it reads another axis whenever the pointer moved in one direction only. `relativeMouse` is advertised only when XI2 is present at build **and** run time. |
| X11-0054 | Global pointer | ✅ | `XQueryPointer` on the root for reading; `XWarpPointer(None, root, ...)` for writing. A pointer on another screen of the same display returns false rather than a fabricated position. |
| X11-0055 | Cursor shapes | ✅ | `XCreateFontCursor` for all twelve shapes, cached per shape. The four with no cursor-font equivalent fall back to the nearest glyph that genuinely exists, each named in the row. A custom ARGB image uses Xcursor with the required 0xAABBGGRR -> premultiplied 0xAARRGGBB conversion; without libXcursor it **refuses** rather than rendering the image down to 1-bit. |
### Phase G — window manager integration (M6)

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| X11-0060 | EWMH probe | ✅ | `_NET_SUPPORTED` read once per connection and re-read on a root `PropertyNotify`, so a window manager started after the application is noticed. Every EWMH request asks first. |
| X11-0061 | Maximise/minimise/restore | ✅ | `_NET_WM_STATE` client message for maximise (and a direct property write while the window is still unmapped, because a window manager only starts managing at MapRequest); `XIconifyWindow` for minimise, which is ICCCM and works without EWMH. |
| X11-0062 | Fullscreen | ✅ | `_NET_WM_STATE_FULLSCREEN` for `BorderlessFullscreen`, refused explicitly when the window manager does not advertise it. `ExclusiveFullscreen` **refuses** -- see design decision 11. |
| X11-0063 | Focus | ✅ | `_NET_ACTIVE_WINDOW` with EWMH source indication 1 when supported. |
### Phase H — displays and DPI (M7)

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| X11-0070 | XRandR enumeration | ✅ | `XRRGetMonitors` (RandR 1.5) preferred, active CRTCs (1.2) as fallback, whole screen when RandR is absent. The primary monitor is moved to index 0 because `GraphicsAdapter` treats the first display as the default adapter. Ids start at 1: id 0 means "no display" to `GraphicsAdapter`. |
| X11-0071 | Modes and refresh | ✅ | Mode list per output; refresh computed as `dotClock / (hTotal * vTotal)` with the doublescan and interlace adjustments -- without them an interlaced 1080i reports 30 Hz instead of 60. |
| X11-0072 | Window→display association | ✅ | Largest-overlap rectangle test against root-space bounds, not a corner test: a window straddling two monitors belongs to the one showing most of it. |
| X11-0073 | Hotplug | ✅ | `XRRSelectInput(RRScreenChangeNotifyMask|RRCrtcChangeNotifyMask|RROutputChangeNotifyMask)`; the handler calls `XRRUpdateConfiguration` (without it every later query returns the pre-hotplug geometry) and invalidates the cache. |
| X11-0074 | DPI policy | ✅ | `Xft.dpi` / 96, clamped to [0.5, 8.0], and exactly 1.0 otherwise. XRandR physical millimetres are deliberately **not** used: a 0x0 mm or 1x1 mm output is common enough that deriving DPI from it produces absurd numbers. `highDpi` is advertised only when the session actually states a scale. |
### Phase I — clipboard (M8)

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| X11-0080 | Selection ownership | ✅ | `X11Clipboard` owns `CLIPBOARD` through an unmapped 1x1 `InputOnly` window of its own -- not an application window, so closing the window the user copied from does not empty the clipboard. `TARGETS` (including `TARGETS` itself), `TIMESTAMP`, `UTF8_STRING`, `STRING`, `TEXT`; `SelectionRequest`, `SelectionClear` and the INCR `PropertyNotify` handled inside the normal pump. |
| X11-0081 | Paste with `INCR` | ✅ | `XConvertSelection` with a 1 s bound and `XCheckTypedWindowEvent` (never `XNextEvent`, which would eat the application's own events). `INCR` reassembled in both directions. `STRING`'s Latin-1 is transcoded to UTF-8. |
### Phase J — graphics bridges (M9, M10)

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| X11-0085 | GLX `IPlatformGlContext` | ✅ | `X11GlContext`. FBConfig chosen at **window creation** and carried on the window, because an X window's visual is fixed for its lifetime; a window created without `WindowRenderIntent::OpenGl` refuses to host a context with a message saying exactly that rather than producing a BadMatch inside the driver. `glXCreateContextAttribsARB` with version and profile, falling back to `glXCreateNewContext`; `GetContextAttributes` reports what `glXGetFBConfigAttrib` actually granted. Swap interval tries EXT, MESA then SGI, with whole-token extension matching (`GLX_EXT_swap_control` is a prefix of `GLX_EXT_swap_control_tear`). |
| X11-0086 | Vulkan `VK_KHR_xlib_surface` | ✅ | `X11VulkanSurface`. `{VK_KHR_surface, VK_KHR_xlib_surface}`; `vkCreateXlibSurfaceKHR` resolved through the caller's `vkGetInstanceProcAddr`, so the platform links no Vulkan loader and a machine with no driver still builds everything else. The `VkXlibSurfaceCreateInfoKHR` layout is restated locally rather than making the Vulkan headers a hard dependency of every X11 build. |
| X11-0087 | `XImage` surface presenter | ✅ | `X11SurfacePresenter`. RGBA8 converted to the window visual's own format by deriving each channel's shift and width from the visual's mask -- so a 16-bit 5-6-5 visual and a 24-bit BGRX one are both correct with neither special-cased. All five scale modes; a 2x2 box filter on downscale only. MIT-SHM used when available **and** attachable (the error trap catches a remote display), plain `XPutImage` otherwise. |
### Phase K — SDL independence (M11)

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| X11-0090 | `CNA_ENABLE_SDL` build gate | ✅ | `cmake/SdlAvailability.cmake` + `cmake/RendererIdentityDefault.cmake`. `CNA_ENABLE_SDL` is `AUTO` by default and byte-for-byte today's behaviour. `OFF` skips the SDL sub-build entirely and refuses any selection that needs it, naming which one. The renderer default rule is extracted into a shared include rather than restated, so an SDL-free configuration cannot silently stop being SDL-free when the per-host default moves. |
| X11-0091 | Zero SDL in the backend | ⬜ | `grep -rn 'SDL' modules/platform/src/X11/` is empty; asserted by a test, not only by a grep in this document. |
| X11-0092 | SDL-free configuration proof | ⬜ | `CNA_PLATFORM=X11 CNA_AUDIO_PLATFORM=NULL CNA_GRAPHICS_RENDERER=<native>` configures, builds and runs with no SDL target in the link graph; `ldd` of the test binary shows no `libSDL`. |

### Phase L — tests, regression, documentation (M12–M14)

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| X11-0100 | Server-free unit tests | ✅ | `modules/platform/tests/CNA/Platform/X11KeyboardMappingTests.cpp` -- 24 cases over the scancode table, the keysym table, the modifier mask, wheel/button numbering, focus filtering and auto-repeat coalescing. All run with no `DISPLAY`. |
| X11-0101 | Xvfb integration tests | ⬜ | an isolated server on a free display number (never a hardcoded `:99`), torn down with the fixture. |
| X11-0102 | Window-manager integration | ⬜ | `openbox` under Xvfb for maximise/minimise/restore/fullscreen/focus; skipped with a recorded reason when no WM is installed. |
| X11-0103 | Conformance | ✅ | `EveryImplementation/PlatformConformance.*` and `PlatformWindowConformance.*` green for `X11`. **Two real defects found and fixed, neither by weakening a test:** the capability set was being recomputed per call and reported `textInput`/`clipboard` true while their accessors were still null before `Video`; and `AcquireSubsystem` refused the subsystems X11 has no facility for, where the cross-implementation rule is that acquisition is bookkeeping and absence is reported through a null service. See the evidence log. |
| X11-0104 | Regression matrix | ⬜ | SDL3, SDL2, Headless, Terminal suites, ratchet, hot-path lint, contract check, CMake selection tests. |
| X11-0105 | `docs/platform-x11.md` | ⬜ | capability boundary, dependency table, DPI policy, threading/locale/error-handler policy, how to run the tests. |

---

## 5. Capability matrix (target)

| Capability | X11 | Why |
|---|---|---|
| `multipleWindows` | ✅ true | independent `XCreateWindow` per window |
| `highDpi` | ✅ true | `Xft.dpi`-derived scale, D10 |
| `multipleDisplays` | ✅ true when XRandR is present | else the service is null and the capability false |
| `borderlessFullscreen` | ✅ true when the WM advertises it | else false; never faked |
| `nativeWindowHandle` | ✅ true | |
| `surfacePresentation` | ✅ true | `XPutImage` |
| `openGlContext` | ✅ true when GLX is present | |
| `vulkanSurface` | ✅ true when the Vulkan headers are present | |
| `clipboard` | ✅ true | real selection ownership, D12 |
| `textInput` | ✅ true | committed UTF-8 |
| `ime` | ❌ false | D16 |
| `exactKeyboardState` | ✅ true | real releases, D5 |
| `pixelAccurateMouse` | ✅ true | |
| `relativeMouse` | ✅ true when XI2 is present | |
| `cursorShapes` | ✅ true | core cursor font; Xcursor for custom images |
| `globalPointer` | ✅ true | |
| `inputDeviceEnumeration` | ✅ true when XI2 is present | keyboards, mice, touch from the XI2 device list |
| `gamepad`, `joystick`, `gamepadRumble`, `gamepadSensors`, `haptics`, `sensors` | ❌ false | not an X11 facility |
| `powerInfo` | ❌ false | not an X11 facility |
| `messageBox`, `nativeFileDialog`, `tray`, `camera` | ❌ false | D15 |
| `managedEntrypoint` | ❌ false | ordinary `main()` |

---

## 6. Risks

| Risk | Mitigation |
|---|---|
| Xlib's default error handler calls `exit()` | D8; the SDL3 backend already proved this is not theoretical |
| Asynchronous X errors attributed to the wrong request | scoped trap brackets one request with `XSync` |
| A window manager that does not implement an EWMH hint | `_NET_SUPPORTED` probed once; every request degrades |
| Xvfb has no window manager, so state transitions are untestable there | separate `openbox` fixture, X11-0102; missing-WM coverage recorded honestly |
| Xvfb's GLX is software or absent | GL tests skip on a recorded reason, never on a silent pass |
| XIM unavailable in a bare test server | `XLookupString` fallback keeps `textInput` true |
| Making SDL optional breaks an existing configuration | `CNA_ENABLE_SDL=AUTO` default is bit-identical to today; the gate only ever *adds* a configuration |

---

## 7. Evidence log

Filled in as tasks complete. Each entry names the exact command and its result.
