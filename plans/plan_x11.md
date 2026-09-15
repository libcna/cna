# CNA native X11 platform (`CNA_PLATFORM=X11`) — implementation plan and ledger

> **Purpose.** Prove that **CNA does not depend existentially on SDL3** by giving CNA a genuine,
> first-class, native X11 platform backend that uses Xlib and the X extensions directly, and by
> making a CNA configuration exist in which SDL is neither configured, built nor linked.
>
> SDL3 stays CNA's default and an excellent optional backend. Nothing here removes, deprecates or
> degrades it. What changes is that SDL stops being the only way CNA can reach a desktop.
>
> **Status: the original ledger is complete** -- its 53 rows are ✅. The SDL-free configuration is
> measured, not argued: `ldd` shows no `libSDL` and `nm` shows no undefined `SDL_` symbol in a
> working CNA test binary, and its 427 tests pass. **Phase M** (the owner approved it on
> 2026-09-15, worked on branch `x11`) goes past that first delivery: **X11-0150, gamepads and
> joysticks through Linux evdev, X11-0151, sound for SDL-free builds (ALSA and CNA's own mixer),
> X11-0152, input-method composition, X11-0153, exclusive fullscreen through XRandR, X11-0154,
> drag and drop, and X11-0155, touch and pens, are ✅**; X11-0156..X11-0158 are ⬜. See
> [§9 Evidence log](#9-evidence-log) for every command and its result, [§7](#7-findings-that-are-not-this-backends)
> for the five defects found that belong to other parts of the tree, and
> [§8](#8-defects-this-work-found-in-its-own-implementation) for the fifteen this work's own tests
> caught in this work's own code.
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
| D11 | **`BorderlessFullscreen` uses `_NET_WM_STATE_FULLSCREEN` and degrades gracefully; `ExclusiveFullscreen` is that plus an XRandR mode of the window's own.** (Revised by X11-0153, 2026-09-15; it was refused until then.) | Genuine exclusive mode means an XRandR mode switch, which changes the user's desktop resolution and can leave it wrong if the process dies -- which is why it was refused rather than faked as borderless until the restore could be made unconditional: in-process on every exit path, and by a forked mode guardian when the process dies however it dies (X11-0153). Where no mode fits, fullscreen stays on the desktop's mode and `GetFullscreenMode()` reports `BorderlessFullscreen`, never "exclusive". When the window manager does not advertise `_NET_WM_STATE_FULLSCREEN` in `_NET_SUPPORTED`, both kinds are refused rather than faked. |
| D12 | **The clipboard is a real selection owner with `TARGETS`, `UTF8_STRING`, `STRING`, `TEXT` and `INCR` for large transfers.** | An X11 clipboard that only works between two CNA windows is not a clipboard. Ownership, `SelectionRequest`, `SelectionNotify`, `SelectionClear` and incremental transfer are all handled, and paste from an external application is what the test asserts. |
| D13 | **A GLX window's `Visual` and `Colormap` are chosen from the `FBConfig` *before* `XCreateWindow`.** | This is the one place where window creation and GL are genuinely coupled. `WindowDescription::renderIntent == OpenGl` plus `openGlFramebuffer` already carries exactly the information needed, so the coupling is resolved at the contract level that already exists — no generic change. A window created with `renderIntent != OpenGl` refuses to host a GL context rather than failing obscurely inside GLX. |
| D14 | **Optional X extensions are optional at *build* time, individually.** | `X11` and `Xext` are mandatory. `Xi` (raw mouse), `Xrandr` (displays), `Xcursor` (shaped cursors), `Xfixes` (pointer barriers/hiding) are each detected separately, each guarded by its own `CNA_X11_HAVE_*` macro, and each turns off exactly one capability when absent. No extension is required because it is convenient. |
| D15 | **`messageBox`, `nativeFileDialog`, `tray`, `camera`, `haptics`, `sensors` are `false`.** | Core X11 has no standard facility for any of them, and shelling out to `zenity`/`kdialog` is not a native backend. Truthfulness beats checkbox count; the contract's rule is that a false capability *refuses*, which is exactly what these do. (`gamepad` and `joystick` were on this list until X11-0150; see D17.) |
| D16 | **`ime` is `false` unless the application draws the composition.** (Revised by X11-0152, 2026-09-15.) | XIM delivers committed text via `Xutf8LookupString`, which is `textInput`. Composition (`TextEditingEvent`) needs the on-the-spot style, in which the input method stops drawing its own composition -- so it is requested only when the application says it draws one (`CNA_IME_IMPLEMENTED_UI=composition`, as SDL's hint does), and `ime` is true exactly then. Candidate lists (`TextEditingCandidatesEvent`) have no XIM protocol and are never delivered. |
| D17 | **Controllers come from the Linux kernel's evdev nodes directly -- no libudev, no libevdev, no SDL -- in `src/Linux/`, not `src/X11/`.** (X11-0150, 2026-09-15) | X delivers no controller input, so this was never going to be an X feature; it is a Linux one, and keeping it out of `src/X11/` is what lets a future Wayland backend take it unchanged. The kernel's own interfaces -- `<linux/input.h>`, sysfs, inotify -- are all it needs; libudev would add a link dependency for a hot-plug signal inotify already gives, and libevdev a dependency for ioctls that are a page of code. Classification reads sysfs rather than opening nodes (D-12). On a Unix without `<linux/input.h>` the capabilities are simply false. |

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
| X11-0062 | Fullscreen | ✅ | `_NET_WM_STATE_FULLSCREEN` for `BorderlessFullscreen`, refused explicitly when the window manager does not advertise it. `ExclusiveFullscreen` refused here, and is implemented by X11-0153 -- see design decision 11. |
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
| X11-0091 | Zero SDL in the backend | ✅ | `X11IsSdlFreeTests.cpp` -- a compile-time `#error` on every SDL header sentinel, plus a scan of every file under `src/X11/` with comments and string literals stripped (so the documentation may explain SDL while the code may not call it; the stripper is itself unit-tested so the scan cannot rot into a no-op). **And** `tools/platform/sdl_ratchet.py` now denylists `modules/platform/src/X11/` from the module-wide exemption `modules/platform/` otherwise has -- verified by experiment: a synthetic `SDL_Init` in that directory takes the ratchet from 0 to 1 file / 2 references and fails the strict check. |
| X11-0092 | SDL-free configuration proof | ✅ | Measured. `-DCNA_PLATFORM=X11 -DCNA_AUDIO_PLATFORM=NULL -DCNA_GRAPHICS_RENDERER=HEADLESS -DCNA_ENABLE_SDL=OFF` configures without touching the SDL submodules, builds, and its test binary shows **no `libSDL` in `ldd`** and **0 undefined `SDL_` symbols in `nm -uC`**; 360 tests pass. Five test/harness targets needed their SDL link edge made conditional (`TARGET SDL3::SDL3`) rather than removed -- the two compiled-effect tools, the devices shutdown-ordering harness, the headless renderer examples and the portable Metal suite; all keep exactly their previous link edge wherever SDL is configured. |
### Phase L — tests, regression, documentation (M12–M14)

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| X11-0100 | Server-free unit tests | ✅ | `modules/platform/tests/CNA/Platform/X11KeyboardMappingTests.cpp` -- 24 cases over the scancode table, the keysym table, the modifier mask, wheel/button numbering, focus filtering and auto-repeat coalescing. All run with no `DISPLAY`. |
| X11-0101 | Xvfb integration tests | ✅ | `X11PlatformIntegrationTests.cpp` (34 cases) and `X11ClipboardInteropTests.cpp` (5 cases), driven by `tools/platform/x11_test_server.sh`, which **searches for a free display number** rather than assuming `:99` -- a fixed number collides with a parallel ctest job and the collision reads as flakiness -- and exits 77 (ctest's skip code) when `Xvfb` is absent. Registered as `CnaX11IntegrationTests`. The clipboard half exchanges selections with `xclip`, a genuinely external X client, including a 512 KB `INCR` transfer in both directions. |
| X11-0102 | Window-manager integration | ✅ | `X11WindowManagerTests.cpp` (10 cases), registered as `CnaX11WindowManagerTests`, starting `openbox` under the same launcher. Covers EWMH fullscreen enter/leave and three repeated round trips, minimise/restore with their events, maximise/restore by observed size, focus on map, focus transfer between two windows, closing a secondary window not ending the application, and the title read back through `xdotool` rather than through CNA. Skips with the reason recorded when `openbox` is absent. |
| X11-0103 | Conformance | ✅ | `EveryImplementation/PlatformConformance.*` and `PlatformWindowConformance.*` green for `X11`. **Two real defects found and fixed, neither by weakening a test:** the capability set was being recomputed per call and reported `textInput`/`clipboard` true while their accessors were still null before `Video`; and `AcquireSubsystem` refused the subsystems X11 has no facility for, where the cross-implementation rule is that acquisition is bookkeeping and absence is reported through a null service. See the evidence log. |
| X11-0104 | Regression matrix | ✅ | See §9. Every mechanical gate passes; the SDL3-platform baseline is unchanged; the X11 selection runs the whole `CnaTests` suite; and the platform suite additionally runs clean under AddressSanitizer + LeakSanitizer in three separate display environments (window manager, bare Xvfb, no `DISPLAY` at all). |
| X11-0105 | `docs/platform-x11.md` | ✅ | `docs/platform-x11.md`: dependency table with what each optional library gates, the full capability boundary with a reason per row, the keyboard/text/mouse/display/fullscreen/clipboard/graphics designs, the DPI policy, the threading/locale/error-handler ownership rules, how to run the three suites, what Xvfb cannot cover, and the three independent mechanisms that keep SDL out. |

### Phase M — past the first delivery (approved 2026-09-15, branch `x11`)

The owner's list from `plans/plan_native_platform_validation.md` "Next steps", in the owner's order.
Win32 and Wayland are out of scope for this phase.

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| X11-0150 | Gamepads and joysticks through Linux evdev | ✅ | `modules/platform/src/Linux/` (D17): `EvdevLayout` (classification, the gamepad mapping, normalisation -- pure), `EvdevDevice` (one node: ioctls, sysfs description, `SYN_DROPPED` resync, `FF_RUMBLE`), `EvdevControllers` (the hub over `/dev/input` with inotify hot-plug, and `IPlatformGamepad`/`IPlatformJoystick` over it). `X11Platform` reports `gamepad`/`joystick`/`gamepadRumble` wherever the build has `<linux/input.h>` and the machine `/dev/input` -- **independently of the X connection** -- starts the hub lazily on the first `GetGamepad()`/`GetJoystick()` like the SDL3 platform, closes every node when `PlatformSubsystem::Gamepad` is released, and delivers `DeviceEvent`/`ControllerButtonEvent`/`ControllerAxisEvent` from `PollEvents`. Tests: `X11EvdevLayoutTests.cpp` (25 cases, no device: classification of pads/keyboards/mice/touchpads/tablets/sensor nodes, xpad vs gamepad-API face buttons, sticks/triggers/hats, scaling, sysfs parsing, which nodes the hub opens) in `CnaX11MappingTests`; `X11EvdevVirtualDeviceTests.cpp` (11 cases against real kernel devices through uinput: hot-plug both ways, events and snapshots, held-at-open state, `SYN_DROPPED`, xpad identity, the whole force-feedback upload/play/stop/erase handshake, a flight stick as a raw joystick, the platform with no `DISPLAY`, sysfs vs ioctl agreement for every node on the machine) as `CnaX11EvdevTests`. Two defects caught on the way, D-11 and D-12. **Not covered:** physical pads -- the xpad/hid-playstation/hid-nintendo layouts follow the kernel's documentation and drivers but no physical controller was available. Not implemented: motion sensors, trigger rumble, light bar, player LEDs, touchpad, battery, a controller mapping database for pads outside the gamepad API. |
| X11-0151 | Native audio for SDL-free builds | ✅ | `CNA_AUDIO_PLATFORM=ALSA` (`docs/audio-alsa.md`): **ALSA playback** (`modules/audio/src/Platform/Alsa/`, libasound loaded at run time -- ALSA's `default` is PipeWire or PulseAudio on a desktop, so one backend reaches all three) and **CNA's own mixer** (`modules/audio/src/Backend/CnaMixer/`) implementing `MixerEngine.hpp`, the facade the XNA classes already used over SDL3_mixer, with SDL3_mixer's semantics read from its source (gain before the mix callback, master gain after, loops as extra passes ending at the max frame, streams that wait when starved, deferred destruction from a stopped callback) and FAudio's linear resampling. Decoding: CNA's own WAV decoder, and Ogg Vorbis through `third_party/stb/stb_vorbis.c` v1.22 (songs stream-decoded); MP3/FLAC/Opus/WMA/XMA are refused by name. `SOUND_ENABLED` now means "a mixer exists" (SDL3 or ALSA). Tests, all on ALSA's silent devices (`CNA_AUDIO_DEVICE`; the test binaries default to `null` themselves): `CnaMixerTests.cpp` (20, sample-exact), `AlsaAudioDeviceTests.cpp` (6, real libasound `null`/`file`, byte-exact recording) plus the ALSA case of the device conformance suite, `CnaMixerXnaTests.cpp` (6 through the XNA API) and an end-to-end recording -- a SoundEffect played through the public API measured off ALSA's `file` device at 440 Hz, its level and its duration -- and `AudioCategoryTests`, previously SDL3-only, now passes (24) on this mixer; the media suite passes whole (295). **Not validated:** playback through PipeWire/PulseAudio/a sound card itself (nobody at the machine; opening the real device even silently can pop the codec), latency under load, surround devices. **Not implemented:** capture (Microphone), MP3/FLAC/Opus. |
| X11-0152 | IME through XIM preedit callbacks | ✅ | `X11TextInput` negotiates the input method's style: by default the IM draws its own composition (what an XNA game needs -- XNA had no IME API) and `ime` stays false; with `CNA_IME_IMPLEMENTED_UI=composition` (read at construction; SDL's IME-UI hint is the SDL3 equivalent) and an IM offering on-the-spot, the four preedit callbacks become `TextEditingEvent`s (text, caret, the converting segment as the selection, an empty event at the end) drained in order from `PollEvents`, and `ime` is true. Per-window composition state lives with the window's XIC and is released after `XDestroyIC`. Key events are offered to the IM only while text input is started for their window -- unfocusing the IC is not enough, Xlib forwards every key of a window with an IC and ibus processes them focused or not (measured: a dead key and the letter after it never reached the game) -- and the IC is focused only then; stopping text input abandons a composition (`XmbResetIC`); an IM commit's keycode-0 press is text only. Candidate lists stay with the IM: XIM has no protocol for them, so `TextEditingCandidatesEvent` never occurs. Tests: `X11InputMethodTests.cpp` (4) against a private ibus that `x11_test_server.sh --with-ibus` starts on the private server (own socket, config and cache; a US-International keymap for a real dead key), as `CnaX11InputMethodTests`, stable over repeated runs; the X11 live suite also passes with that ibus present. **Not covered:** a language engine (Hangul, Pinyin, Anthy) and fcitx. |
| X11-0153 | Exclusive fullscreen through XRandR | ✅ | `X11ModeSwitch` (one per connection, owned by `X11Connection`, restored before `XCloseDisplay`): SDL's mode rule (smallest mode holding the size, aspect ratio first, then the desktop's refresh rate); the screen grown before and shrunk after the CRTC change to the lit CRTCs' bounding box, so no step is an invalid configuration and other monitors keep their place; a server grab around each read-modify-write; a scaled/transformed CRTC, no RandR, or no mode large enough gives borderless, reported as such. `X11Window`: `SetSize` while exclusive picks a new mode (what `GraphicsDevice` does after `IsFullScreen`); the mode is held only while the window is shown and not minimised, and a focus loss minimises the window and gives the mode back, as SDL does (held 400 ms after a mode change, skipped under Xwayland); every restore path -- leaving, `Hide`, destroy, destroyed by another client, taken out of fullscreen by another client, connection closed -- and a mode someone else set in the meantime left alone; the display service reports the current and the desktop mode apart. **`X11ModeGuardian`**: forked with the first switch, it restores the mode when the process dies however it dies, speaking the X protocol itself (async-signal-safe after a fork of a threaded process), authenticating with the game's own cookie (libXau, new optional dependency), out of the terminal's session, disarmed by a normal restore. Tests: `X11ExclusiveModeTests.cpp` (21, no server: mode choice, screen plans, the wire format) in `CnaX11MappingTests`; `X11ExclusiveFullscreenTests.cpp` (17, on the launcher's private Xvfb with modes added through RandR and `openbox`, never on a desktop) as `CnaX11ExclusiveFullscreenTests`, including a helper process killed with `SIGKILL` whose mode its guardian restores and the guardian's own restore driven in-process; `X11_House3D_ExclusiveFullscreen_<renderer>` runs a real XNA game with `IsFullScreen` (`tools/platform/x11_exclusive_fullscreen_game.sh`). One defect found in the backend's older code on the way, D-14. **Not covered:** a real monitor's mode change (Xvfb switches its one CRTC instantly), multi-monitor layouts (the screen plan is unit-tested, not run), GNOME/KDE's compositors. |
| X11-0154 | Drag and drop (XDND) | ✅ | The contract had no drop at all, so it gained one: `DropEvent` (`Begin`, `Position`, `File`, `Text`, `Complete` -- SDL3's sequence) and the thirtieth capability, `dragAndDrop`, appended so no recorded enum value moves. **X11**: `X11DragAndDrop`, XDND 5 target side -- `XdndAware` on every window CNA creates; `XdndEnter` (three types inline or `XdndTypeList`), `XdndPosition` translated to window coordinates and answered with `XdndStatus` (copy, positions on every move), `XdndLeave`, `XdndDrop` read through the clipboard's selection reader generalised to any selection and timestamp (`INCR` included, one-second bounds), `XdndFinished` always. `text/uri-list` > UTF-8 text > `text/plain`/`TEXT` > `STRING` (Latin-1); `file:` URIs percent-decoded to local paths, any other URI delivered as text (SDL3 discards it), text whole (SDL3 splits it by line); a drag of nothing takeable refused and silent. **SDL3**: `SDL_EVENT_DROP_*` mapped one to one, `dragAndDrop` true. **XNA side**: CNAEXT `GameWindow::FileDropEXT` (MonoGame's `FileDrop` shape, once per drop) and `TextDropEXT`, raised from `Game`'s pump. SDL2 and Win32 report `false`. Tests: `X11DragAndDropTests.cpp` -- 10 without a server (type choice, URI spellings, escapes, lists) in `CnaX11MappingTests`, 9 against a real XDND source (a second process of the test binary speaking the protocol from outside) in `CnaX11IntegrationTests`: files, whole text, a link, Latin-1, a type list, a leave, a refusal, a source that never delivers; `Sdl3EventMapperTests` (3 more); `GameWindowDropTests.cpp` (6, through a real `Game`); the contract's own event and capability tests. **Not implemented:** the source side (dragging out of a CNA window), `XdndProxy`, SDL2's and Win32's drop events. |
| X11-0155 | Touch and pen through XInput2 | ✅ | `X11Touch`: the connection now negotiates XInput 2.2; every window CNA creates selects `XI_TouchBegin/Update/End` from the master devices, and each contact becomes `TouchEvent` (`Down`, `Motion` with its delta, `Up`) in the window it began in, normalised against the client size it had then -- exactly the size the input bridge multiplies back by; a window destroyed mid-touch has its contacts `Cancelled` on the next pump. A window that selects touch is sent no pointer events emulated from touches (the server's listener walk stops at the first selection it finds), so the contact flagged `XITouchEmulatingPointer` drives the mouse -- events and snapshot, once each, as SDL3's touch-to-mouse synthesis does. **Pens**: SDL3's rule (an enabled slave pointer with an "Abs Pressure" valuator), found at start-up and on `XI_HierarchyChanged`; a pen with its tip down is a touch with its normalised pressure, a hovering pen is not; its XI2 button/motion events are selected per pen device so the master's core pointer events -- the mouse -- are untouched. Tests: `X11TouchTests.cpp` -- 4 without a server, the selection on the launcher's Xvfb (asked over the platform's own connection: XI2 selections are per client), and **8 against real devices**: a uinput touchscreen and pen read by a private rootless Xorg (dummy video, evdev input with `GrabDevice`) that the suite verifies holds each device exclusively before it writes a single event, and re-verifies before every one -- the desktop's compositor, which sees the devices appear, receives none of it. As `CnaX11TouchscreenTests` (opt-in by the entry's environment). **Not delivered:** tilt, barrel buttons, the eraser as distinct from the tip, touch ownership and gesture grabs. |
| X11-0156 | Per-monitor DPI | ⬜ | Beyond the session-wide `Xft.dpi` of D10. |
| X11-0157 | `PRIMARY` selection | ⬜ | Middle-click paste, both directions. |
| X11-0158 | More clipboard formats | ⬜ | Beyond text. |
---

## 5. Capability matrix (target)

| Capability | X11 | Why |
|---|---|---|
| `multipleWindows` | ✅ true | independent `XCreateWindow` per window |
| `highDpi` | ✅ true | `Xft.dpi`-derived scale, D10 |
| `multipleDisplays` | ✅ true when XRandR is present | else the service is null and the capability false |
| `borderlessFullscreen` | ✅ true when the WM advertises it | else false; never faked. `ExclusiveFullscreen` has no flag of its own: it is available wherever borderless is, and borderless where no mode fits (X11-0153) |
| `nativeWindowHandle` | ✅ true | |
| `surfacePresentation` | ✅ true | `XPutImage` |
| `openGlContext` | ✅ true when GLX is present | |
| `vulkanSurface` | ✅ true when the Vulkan headers are present | |
| `clipboard` | ✅ true | real selection ownership, D12 |
| `dragAndDrop` | ✅ true | XDND 5, target side (X11-0154) |
| touch (`TouchEvent`) | ✅ where the server speaks XInput 2.2 | no capability flag in the contract; `TouchPanel` learns of a touch device from the first contact (X11-0155) |
| `textInput` | ✅ true | committed UTF-8 |
| `ime` | ✅ true when the application draws the composition and the IM offers on-the-spot | D16, X11-0152; no candidate lists |
| `exactKeyboardState` | ✅ true | real releases, D5 |
| `pixelAccurateMouse` | ✅ true | |
| `relativeMouse` | ✅ true when XI2 is present | |
| `cursorShapes` | ✅ true | core cursor font; Xcursor for custom images |
| `globalPointer` | ✅ true | |
| `inputDeviceEnumeration` | ✅ true when XI2 is present | keyboards, mice, touch from the XI2 device list |
| `gamepad`, `joystick`, `gamepadRumble` | ✅ true on Linux with `/dev/input` | the kernel's evdev nodes, with or without a display (D17, X11-0150) |
| `gamepadSensors`, `haptics`, `sensors` | ❌ false | not an X11 facility; a pad's sensor node is not paired with its pad |
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

## 7. Findings that are not this backend's

Recorded rather than fixed, because each is pre-existing, generic, and outside what a platform
backend may change. Each was **reproduced without X11** before being classified that way.

### F-1 — `plans/plan_platform.md` §2 is stale at the baseline commit

`python3 tools/platform/sdl_inventory.py --check` already failed at `e05b3d0f`, before a line of
this work existed (verified by stashing the whole change and re-running). Regenerating it would
put an unrelated diff in this branch's commits, so it is left as it was found.

### F-2 — `GraphicsDevicePlatformWindowTests` could not compile for any non-SDL3 platform

**Fixed**, because it blocked the regression matrix and the fix is three lines.
`AViewportRefreshSurvivesAWindowThatRefusesItsDrawableSize` calls the private
`GraphicsDevice::UpdateViewportFromWindow()`. Its own `#if` skips the body under an SDL3
selection, so the error was invisible there — and a hard compile error under every other one.
Reproduced identically with `-DCNA_PLATFORM=HEADLESS -DCNA_GRAPHICS_RENDERER=HEADLESS`, which is
what establishes it as generic rather than X11's.

Fixed the way this codebase already handles the identical case three times over
(`Texture2DArrayGraphicsDeviceTestPeer`, `StorageTexture2DGraphicsDeviceTestPeer`,
`StorageBufferGraphicsDeviceTestPeer`): a named `CNA::Internal::GraphicsDevicePlatformWindowTestPeer`
friend, rather than widening the XNA-visible API. The test itself is the regression test.

### F-3 — `CNA_AUDIO_PLATFORM=NULL` cannot link anything containing `cna_content`

`modules/content/src/Xnb/XnbCanonicalData.cpp` calls `CNA::Internal::Audio::DecodeWavToPcm16`
unconditionally, and that function lives in `modules/audio/src/Backend/Sdl3Mixer/WavDecoder.cpp`,
which `modules/audio/CMakeLists.txt` compiles **only** for `CNA_AUDIO_PLATFORM=SDL3`. So any
executable linking the content module under NULL or SDL2 audio fails with an undefined symbol.

Reproduced with no X11 involved at all: a three-line probe compiled against
`modules/audio/include` and linked against the `libcna_audio.a` from a
`-DCNA_PLATFORM=HEADLESS -DCNA_AUDIO_PLATFORM=NULL` build produces exactly the same undefined
reference.

**Fixed by `plans/plan_native_platforms_integration.md` NPI-0007..0011**, when this backend was
integrated alongside Win32 and hit the same finding via `CNA_PLATFORM=X11 CNA_ENABLE_SDL=OFF
CNA_AUDIO_PLATFORM=NULL`. `DecodeWavToPcm16` is now implemented natively in
`modules/audio/src/Internal/WavDecoder.cpp` (PCM 8/16/24/32-bit, IEEE float 32/64-bit,
`WAVE_FORMAT_EXTENSIBLE`, MS-ADPCM and IMA-ADPCM), built on the pre-existing SDL-free
`WavFormatReader.cpp` chunk reader and compiled unconditionally regardless of
`CNA_AUDIO_PLATFORM`; the old SDL3Mixer implementation was removed. `cna_content`,
`CnaContentTests` and `CnaContentPipelineTests` now build and pass under `CNA_PLATFORM=X11
CNA_ENABLE_SDL=OFF CNA_AUDIO_PLATFORM=NULL`, confirmed with `ldd`/`readelf` showing no SDL
reference in either binary.

### F-4 — one renderer is missing from the glTF index-width disposition table

`GltfRendererIndexWidthPolicy.InventoryClassifiesEveryRenderer` scans `modules/renderers/` for
families that mention `CreateIndexBuffer16` and compares the result against a hardcoded set of 45.
The tree has **46**: `rlgl` is present and unclassified.

Introduced by this branch's own baseline commit — `e05b3d0f merge(RLGL): integrate rlgl renderer
into next` — and nothing to do with the platform axis. Not fixed here because the fix is a policy
decision about whether `rlgl` refuses 32-bit indices explicitly or inherits the throwing default,
and guessing it into one of the two fixed-size arrays would be worse than leaving it flagged.

### F-5 — the content-pipeline suite needs fixtures and FFmpeg this configuration does not have

The remaining failures in a raw `CnaTests` run are all content-pipeline and XNB tests
(`XnaSourceToOutput`, `XnaBuildDeterminism`, `Cnj*`, `Cnb*`, `ContentManagerSkinnedModel*`). They
depend on assets that ctest builds through fixture dependencies, and on an FFmpeg-backed video
processor that `-DCNA_ENABLE_VIDEO=AUTO` disables on a machine without the libraries. Reproduced
identically on the SDL3 baseline build at the same commit; see §10.

---

## 8. Defects this work found in its own implementation

Recorded because the interesting output of a test suite is the bugs it caught, and every one of
these was found by a test rather than by reading the code. None was fixed by weakening a test.

| # | Defect | Found by | Why it was invisible |
|---|---|---|---|
| D-1 | Capabilities were recomputed per call, and advertised `textInput`/`clipboard` true while their accessors were still null before `Video` was acquired. | `PlatformConformance.EveryServiceIsNullExactlyWhenItsCapabilityIsFalse` | The rule is a cross-implementation one; nothing in the X11 code alone states it. |
| D-2 | `AcquireSubsystem` refused the subsystems X11 has no facility for, where the cross-implementation rule is that acquisition is bookkeeping and absence is reported through a null service. | `PlatformConformance.SubsystemsAreRefcounted` | Reads as correct in isolation -- refusing what you cannot do is usually right. |
| D-3 | **`GLXFBConfig` is itself an opaque pointer**, and the window stored the address of the array slot rather than the config. | `X11Live.AGlWindowGetsARealContextThatCanBeMadeCurrentAndSwapped` | Both are pointers, so it compiled; it produced `GLXBadFBConfig` from a perfectly valid config. |
| D-4 | `X11Connection` unregistered from the error policy **before** `XCloseDisplay`. Closing flushes and can deliver a protocol error, so Xlib's `exit()`-calling default handler was back in place for it. | `X11WithWindowManager` teardown killed the test binary | Only reachable when another client destroys a window during teardown. |
| D-5 | A window destroyed by the window manager was destroyed again by its wrapper. | the same run | `DestroyNotify` removed the window from the registry but never told the window. |
| D-6 | `MouseSnapshot::scrollX/Y` were accumulated **in notches, not in XNA units** -- off by a factor of 120 from every other backend and from what `Mouse::GetState().ScrollWheelValue` means. | reading the contract while writing `X11Live.WheelNotchesAccumulateInXnaUnits...` | Both are plain `int`s; no compiler and no single-backend test can see it. |
| D-7 | X's `Button4Mask`/`Button5Mask` are the **wheel**, and were copied into the snapshot's X1/X2 bits -- reporting a side button held for the duration of every scroll notch. | the same | The one-to-one translation is the obvious one and is wrong. |
| D-8 | `WindowEventKind::Maximized` was computed and then never emitted, so a game reacting to maximise never heard. | `X11WithWindowManager.MaximizingReportsAMaximizedEvent...` | The size-based test passed throughout. |
| D-9 | `_NET_WM_STATE` was written **directly** on an iconified window. An iconified window is unmapped but still *managed*, so this raced the window manager's own update and `Restore()` intermittently left the window iconic -- about one run in three inside the full suite, every time green in isolation. | `X11WithWindowManager.MinimizeAndRestoreRoundTrip` | The guard said "not mapped right now" where EWMH means "never mapped". Looks like flakiness. |
| D-10 | `Sync()` round-tripped the X server but not the **window manager**. `XResizeWindow` on a managed window is redirected: the server forwards a `ConfigureRequest` and the window manager decides. So `SetSize(); Sync(); GetClientBounds()` read the old size whenever a window manager was running. | `PlatformWindowConformance.SizeChangeLandsAfterSync/X11` under `openbox` | It passed under bare Xvfb, where there is no window manager to wait for. |

| D-11 | **After a kernel queue overflow a button could stay stuck in a stale state** (X11-0150). The resync read the pad's key state and then went on applying the rest of the same `read()` -- events *older* than that state. Reading the key state makes the kernel drop the key events still queued (`EVIOCGKEY` flushes them), so nothing newer followed to correct the stale ones: 27 of 48 flood lengths ended with the wrong buttons held. | noticed while writing `X11EvdevVirtualDevice.AnOverflowedQueueEndsOnTheDevicesRealState`, whose first form passed with the bug in place; rewritten to vary the flood across a whole ring buffer, it fails 27/48 rounds without the fix and passes with it | Only when more than one read's worth of events is left after the `SYN_DROPPED`, which depends on where the kernel's ring last wrapped. |
| D-12 | **The first controller query stalled for 0.8 s** (X11-0150): the hub opened every `/dev/input` node to classify it, and closing an evdev node waits for an RCU grace period (23-72 ms per node, measured). Classification now reads sysfs and never opens a keyboard or mouse: 3 ms. | every uinput test taking about a second; timing each node's open, ioctl and close | Correct output, just slow -- and it only shows on a machine with many input nodes. `X11EvdevHub.SysfsDecidesWhatIsOpenedAndAKeyboardNeverIs` watches the opens through inotify and fails without the fix. |

| D-13 | **A zero-length sound handed a null pointer to `memcpy`** in CNA's mixer (X11-0151): wrapping an empty SoundEffect's PCM copied zero bytes from an empty vector's `data()`, which may be null -- undefined behaviour even for zero bytes, since glibc declares both arguments nonnull. The same class as NPV-0103. | UndefinedBehaviorSanitizer, on the audio suites in `build-asan` switched to ALSA | Harmless on every compiler in practice, which is exactly why only a sanitizer sees it. `CnaMixer.AnEmptySoundPlaysAndEndsAtOnce` now plays an empty sound on purpose, forever-looped as well. |
| D-14 | **Fullscreen asked for before a game's first event pump was never applied** (found by X11-0153, in X11-0062's code). `SetNetWmState` wrote `_NET_WM_STATE` directly onto a window until it had *seen* its `MapNotify`, but a game shows its window and applies `IsFullScreen` while it is still setting up, before pumping anything: the window manager had managed the window by then and ignores a property written onto a managed window. The window stayed windowed -- and under exclusive fullscreen, a decorated 800x600 window on an 800x600 monitor. Once `Show()` has sent the map request, the client message is the right one (the MapRequest reaches the window manager first). | `X11_House3D_ExclusiveFullscreen_OPENGL33`, checking the game window's geometry; reduced to `X11WithWindowManager.FullscreenAskedForBeforeTheFirstEventPumpIsApplied`, which fails (320x240, windowed) without the fix | `GetFullscreenMode()` reads `_NET_WM_STATE` back -- the very property the backend had written -- so every assertion on it passed; only the geometry shows it. Every suite waited for the window to be managed before asking. |
| D-15 | **A composition was not ended by its commit** (X11-0152). The backend ended it only when the input method drew its preedit empty or finished it after committing; the ibus 1.5.32 it was written against does, the CI runner's ibus 1.5.29 did not within three seconds of the commit, so the application kept showing the composition beside the text that replaced it. The commit now ends it, ahead of the text, and an empty composition is delivered once however many the input method sends -- as SDL3's X11 backend does. | `X11InputMethod.AnApplicationThatDrawsTheCompositionReceivesIt` on CI (run 34955898208), not locally | Locally ibus cleared the preedit itself, so the path the backend was missing never ran. The test now also checks the composition ended exactly once and before the text, which exercises the de-duplication against the local ibus. |

D-10 is the one worth the extra sentence: it is exactly the failure the plan predicted when it
split the Xvfb and window-manager suites, and it would have shipped green had the suite only ever
run against a bare virtual server.

---

## 9. Evidence log

Every line below is a command that was run and its measured result, not a claim about what should
happen.

### Commits

| Commit | What |
|---|---|
| `b2c78033` | the backend, the build integration and the SDL-availability gate |
| `8086fa82` | the three test suites, the SDL-containment gate and `docs/platform-x11.md` |
| `60d58a44` | real Vulkan surfaces, XNA key names, the window-manager fixes and the honest guards |

Baseline: `e05b3d0f026e0926741f89459daf02579240399d`.

### The SDL-free proof

```
cmake -S . -B cmake-build-x11-nosdl -G Ninja \
      -DCNA_PLATFORM=X11 -DCNA_AUDIO_PLATFORM=NULL \
      -DCNA_GRAPHICS_RENDERER=HEADLESS -DCNA_ENABLE_SDL=OFF
```

```
-- CNA: SDL is NOT configured (CNA_ENABLE_SDL=OFF). No SDL source is fetched,
   built, found or linked by this configuration.

$ ldd cmake-build-x11-nosdl/CnaPlatformModuleTests | grep -ci sdl
0
$ nm -uC cmake-build-x11-nosdl/CnaPlatformModuleTests | grep -c SDL_
0
$ ./tools/platform/x11_test_server.sh --require-window-manager \
      ./cmake-build-x11-nosdl/CnaPlatformModuleTests
[  PASSED  ] 427 tests.
```

The shared objects that binary does load: `libX11`, `libXext`, `libXi`, `libXrandr`, `libXcursor`,
`libXfixes`, `libGLX`, `libGLdispatch`, `libxcb`, and libc/libstdc++. Nothing else.

`CNA_ENABLE_SDL=OFF` was also verified to refuse rather than substitute: adding
`-DCNA_AUDIO_PLATFORM=SDL3` to the above fails the configure naming
`CNA_AUDIO_PLATFORM=SDL3` as the reason.

### The X11 test suites

| Suite | Environment | Result |
|---|---|---|
| `CnaX11MappingTests` | no `DISPLAY` | passed, 0.08 s |
| `CnaX11IntegrationTests` | private `Xvfb` | passed, 8.55 s |
| `CnaX11WindowManagerTests` | `Xvfb` + `openbox` | passed, 2.81 s |

```
$ ctest -R CnaX11 --output-on-failure
100% tests passed, 0 tests failed out of 3
```

Platform-module suite, run directly in three separate display environments:

| Environment | Result |
|---|---|
| `Xvfb` + `openbox` | 427 passed, 2 skipped, 0 failed |
| bare `Xvfb` | 427 passed |
| no `DISPLAY` at all | 354 passed, the rest skipping with a recorded reason |

### Under AddressSanitizer and LeakSanitizer

`-DCNA_SANITIZE=address`, same three environments. **0 leaks, 0 sanitizer errors.** LeakSanitizer
was confirmed active in this environment first, with a deliberately leaking probe — a clean run
from a sanitizer that is not switched on proves nothing.

### What ran against a real X server rather than a fake

- a real GLX context: `glXCreateContextAttribsARB` at 3.3 core on Xvfb's software GLX, made
  current, swapped, its granted attributes read back from `glXGetFBConfigAttrib`;
- a real `VK_KHR_xlib_surface`: a real `VkInstance` built with the two extensions the platform
  names, a surface on a real window, and `vkGetPhysicalDeviceSurfaceSupportKHR` confirming a
  physical device (llvmpipe) can present to it — creation succeeding proves only the call was
  well-formed, that query proves the surface is the window's;
- real pixels: a frame presented through `XPutImage` and read back off the window with
  `XGetImage`, unpacked through the visual's own masks and compared per channel;
- real input: `xdotool` keystrokes into a focused window arriving with both `Scancode::A` and
  `KeyCode::A` and a matching release, committed text appearing only while text input is started,
  and a click arriving as CNA button 1 with client coordinates;
- a real external clipboard peer: `xclip` pasting what CNA copied and CNA pasting what `xclip`
  copied, including a 512 KB `INCR` transfer in both directions and the `TARGETS` list.

### Controllers through evdev (X11-0150, 2026-09-15)

Debian 13, kernel 6.12, a ThinkPad with sixteen input nodes and no physical controller; the user in
the `input` group and `/dev/uinput` granted by ACL. `cmake-build-x11` (X11, SDL OFF, HEADLESS).

```
$ ctest -R 'CnaX11(Mapping|Evdev|Integration|WindowManager)Tests'
CnaX11MappingTests ....... Passed      (includes 25 X11EvdevLayout/X11EvdevHub cases)
CnaX11EvdevTests ......... Passed  16.7 s   (11 cases, real kernel devices through uinput)
CnaX11IntegrationTests ... Passed
CnaX11WindowManagerTests . Skipped (no openbox on this machine)
```

- **sysfs agrees with the kernel's ioctls for every node on the machine** -- keyboard, TrackPoint,
  touchpad, ACPI buttons, audio jacks, and the test's own virtual pad
  (`SysfsAndTheNodeItselfDescribeEveryDeviceAlike`), classification included.
- **Hub start:** 830 ms before D-12's fix, 3 ms after (the same test, measured around `Start()`).
- **D-11:** `AnOverflowedQueueEndsOnTheDevicesRealState` fails 27 of 48 rounds with the resync
  change reverted, passes 5 of 5 runs with it.
- **Under ASan + UBSan + LSan** (`build-asan`, `CnaPlatformModuleTests`): the evdev suites, the
  SDL-containment scan and the conformance suite -- 96 passed, 1 skipped (X11 supports surface
  presentation), no sanitizer report.
- **The ratchet covers `src/Linux/`:** a planted `SDL_Init` there takes
  `sdl_ratchet.py --check --strict` from 0 to 1 reference and fails it; removed, it is at budget again.
- `sdl_inventory`, `sdl_classify`, `renderer_sdl_audit`, `sdl_ratchet`, `hot_path_lint`: all pass.

### Sound for SDL-free builds (X11-0151, 2026-09-15)

`cmake-build-x11` switched to `-DCNA_AUDIO_PLATFORM=ALSA` (X11, SDL OFF, HEADLESS). Nothing ever played
to a real device: the test binaries default to ALSA's `null` device themselves, ctest sets it, and
the one end-to-end test records through ALSA's `file` device.

| Check | Result |
|---|---|
| `CnaAudioTests`, whole | 286 passed, 7 skipped (Microphone capture: ALSA has no capture yet) |
| the same under ASan + UBSan + LSan (`build-asan` switched to ALSA) | 286 passed, no sanitizer report after D-13's fix |
| `CnaMixer.*` / `AlsaAudioDevice.*` / `CnaMixerXna.*` | 20 / 6 / 6 passed; the ALSA case of the device conformance suite passes beside NULL |
| end to end (`CnaAudioAlsaRecordingTest`) | a SoundEffect played through the public API, measured off ALSA's `file` device: 440 Hz ± 10, peak 8000/32768 ± 0.01, 0.3 s ± 0.02, identical in both channels |
| `AudioCategoryTests` (previously SDL3-only) | 24 passed, and 6 repetitions of the suite at `ctest -j16` after two fixture races it exposed were fixed: fixture files written in place while other test processes read them ("Engine initialization failed!"), and a 2 ms wave that finished before `IsPlaying` was asserted -- both test-side, both would equally hit SDL3 under load |
| `CnaMediaTests`, whole | 295 passed (MediaPlayer and VideoPlayer now take their SOUND_ENABLED paths) |
| `CnaContentTests`, whole | 1795 passed; the 13 failures are the pre-existing content/renderer set recorded in `plans/plan_native_platform_validation.md` "Regression runs" |
| full `ctest -j12` | 9255 of 9285; every other failure is that recorded pre-existing set (content/renderer, `SupportsMultipleRenderTargets`, `CApi*`, `CNAEXT_*`, `CnaGltfConformanceL0`, `CnaXnbModelCorpusSweep`, parallel ENet), plus the AudioCategory races above and `XnaDifferentialBuildTest` timing out while the ASan build ran beside it -- alone it passes in 530 s of its 600 s budget. `CNAEXT_NoPosixSetenv` also flagged this work's own tests (POSIX `setenv` in the ALSA and evdev tests); they use `System::Environment` now, leaving only the RLGL examples it already listed |
| the SDL-free game, `cna_demo_2d --smoke 6` | runs with ALSA audio on the `null` device; no binary has libasound (or SDL) in `NEEDED` |

### Input-method composition (X11-0152, 2026-09-15)

`CnaX11InputMethodTests` under `x11_test_server.sh --with-ibus` (a private Xvfb, a private ibus
1.5 with `ibus-x11`, `xkb:us::eng`, the keymap switched to `us(intl)`):

| Case | Result |
|---|---|
| composition drawn by the application (`CNA_IME_IMPLEMENTED_UI=composition`) | `ime` true; a dead key produces a non-empty `TextEditingEvent`, the next key commits `é` and an empty editing event ends the composition; no key event for keycode 0 |
| default | `ime` false; `é` committed, no editing event at all |
| text input stopped | the dead key and the letter arrive as key events. **Fails before the change that stops offering key events to the input method outside text entry**: only the two releases arrived, both presses swallowed by ibus -- unfocusing the input context alone did not prevent it |
| stopping text input mid-composition | an empty editing event; an `e` typed after restarting is `e`, not `é` |

Four passes of the suite in a row; `X11Live.*` (51) passes with the private ibus present as well
as without one. On CI's Ubuntu 24.04 (ibus 1.5.29) the first case failed -- the commit arrived and
the composition was never ended -- which is D-15; after its fix, four more local passes with the
composition checked to end once and before the committed text, and `X11Live.*`, the clipboard suite and both conformance suites pass (137, 6
skipped as before). The developer's own ibus daemon ran throughout, untouched.

### Exclusive fullscreen (X11-0153, 2026-09-15)

On the launcher's private Xvfb (one output, one CRTC, its 1280x1024 mode plus 1024x768, 800x600
and 640x480 added through RandR) with `openbox` -- unpacked from its Debian packages into
`~/deps/openbox`, not installed, since this machine has none. No display of the owner's was
touched.

| Suite | Result |
|---|---|
| `X11ExclusiveModeChoice.*`, `X11ScreenPlan.*`, `X11ModeGuardianWire.*` | 21 passed |
| `X11ExclusiveFullscreen.*` | 17 passed; 8 shuffled repetitions of the first 16 and 3 of all 17 without a failure. Includes the SIGKILLed helper: mode back within the 5 s budget, a guardian present exactly while a mode is changed and gone after, and a mode the "user" set left alone by the guardian and by a normal leave |
| the same, `build-asan` (`address,undefined`) | 21 and 16x2 passed, 0 reports |
| `X11WithWindowManager.*` | 19 passed twice -- the first local run of this suite, with the unpacked `openbox` -- including D-14's regression test, which fails without the fix |
| `X11Live.*`, clipboard, Vulkan surface | 55 passed; under ASan 51 passed with the Mesa swrast GLX leak already on record (NPV-0004) the only report, suppressed by `tools/platform/lsan_x11_mesa.supp`, and none without the three GLX tests |
| `X11_House3D_ExclusiveFullscreen_OPENGL33`, `_VULKAN` (`cmake-build-multi`, llvmpipe/lavapipe) | passed: the monitor at 800x600 and the game's window at 800x600+0+0 while it runs, 1280x1024 back after a normal exit, and after `kill -9`. SOFTWARE and HEADLESS draw off-screen (`needsWindow = false`) and have no window to make fullscreen, so the test is not registered for them |

### Drag and drop (X11-0154, 2026-09-15)

| Suite | Result |
|---|---|
| `X11DropTarget.*`, `X11UriList.*`, `PlatformEventTests.*`, `PlatformCapabilities*` | 33 passed |
| `X11DragAndDropLive.*` on the launcher's Xvfb, against the helper source process | 9 passed; 5 shuffled repetitions without a failure; the never-delivering source costs 1.2 s, the selection reader's bound |
| the same plus `X11Live.*` and the clipboard, `build-asan` (`address,undefined`, the Mesa suppression of NPV-0004) | 93 passed, 0 reports |
| `GameWindowDropTest.*`, `FileDropEventArgsEXTTests.*` and the runtime's window/event suites | 50 passed, 1 skipped as before |
| `cmake-build-debug` (SDL3): `Sdl3EventMapperTests.*Drop*` | 3 passed; the SDL3 build's `CnaPlatformTests` filter: 379 passed |
| every X11 and platform ctest entry | 9/9 |
| `sdl_inventory.py` | regenerated: 1045 -> 1050 distinct `SDL_*` identifiers, the five `SDL_EVENT_DROP_*`, all classified; `nonproduction_sdl_audit.py`'s ceiling for `Sdl3EventMapperTests.cpp` raised 195 -> 208 for the three new mapper tests |

### Touch and pens (X11-0155, 2026-09-15)

Real devices, not Xvfb: a uinput touchscreen (multitouch protocol B, `INPUT_PROP_DIRECT`) and a
uinput display-tablet pen, read by a private Xorg 21.1.16 started by the suite as this user --
the `dummy` video driver and the `evdev` input driver unpacked into `~/deps/xorg-dummy` (not
installed; `CNA_X11_XORG_MODULE_PATH`), a private config directory, GLX and DRI left out so it opens
no GPU, `GrabDevice` on each device. Every injected frame first checks that another reader holds
the device exclusively (the suite's own `EVIOCGRAB` must fail with `EBUSY`). The first probe, before
that config was right, found the grab not held -- and injected nothing, as designed.

| Suite | Result |
|---|---|
| `X11TouchMath.*` | 4 passed |
| `X11TouchSelection.*` on the launcher's Xvfb | 1 passed -- after the test was corrected to ask over the platform's own connection |
| `X11Touchscreen.*` | 8 passed; 12 shuffled repetitions without a failure after two harness defects were found and fixed: the scripted pen came into range with a position evdev does not apply until the next in-range move, and with an axis the kernel suppressed as unchanged since an earlier test -- so it now enters beside its point and moves onto it. No server, device or directory left behind; a crashed run's server now dies with it (`PR_SET_PDEATHSIG`) |
| `build-asan` (`address,undefined`): `X11Touchscreen.*` and `X11TouchMath.*` twice; `X11Live.*`, the selection and drag and drop | 12 x 2 and 61 passed, 0 reports |
| every X11 and platform ctest entry, the touchscreen one included | 10/10 |

### Regression

| Check | Result |
|---|---|
| SDL3 platform suite (`cmake-build-baseline`) | 506 tests, 453 passed, 52 skipped, 1 failed — **bit-identical to the baseline commit**, and the one failure is `AcquireSubsystem(Audio)` on a machine with no audio device |
| `sdl_inventory.py --check` | already stale at `e05b3d0f`; see F-1 |
| `sdl_classify.py --check` | all 1047 identifiers classified |
| `renderer_sdl_audit.py --check` | allowlist unchanged at the four families |
| `sdl_ratchet.py --check --strict` | at budget, 0 files / 0 references — **and now covers `src/X11/`**, verified by planting a synthetic `SDL_Init` there and watching it fail |
| `hot_path_lint.py` | 0 violations across 2052 production sources |
| `nonproduction_sdl_audit.py --check` | at per-file ceilings |
| `check_contract.py` | 28 headers covered, 635 declarations documented |
| full `CnaTests` under `CNA_PLATFORM=X11` | 9476 tests; every remaining failure is content-pipeline/XNB and **fails identically on the SDL3 baseline build at the same commit** (measured, §7 F-4 and F-5) |

### Real-desktop validation: what is still missing

This machine has no X session, no GPU and no input-method server. Xvfb plus `openbox` plus
llvmpipe covers a great deal and is not the same thing. Not validated here, and recorded as such
rather than assumed:

- a hardware GLX driver, and a compositor's own fullscreen and vsync behaviour;
- a multi-monitor XRandR layout, and monitor hotplug;
- an `Xft.dpi` other than unset, so the high-DPI branch of the scale policy;
- a real input method (ibus, fcitx) driving `Xutf8LookupString` through a composition;
- physical input devices, so XInput2 raw motion from a real mouse;
- a window manager other than `openbox`.
