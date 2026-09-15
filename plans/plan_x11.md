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
> drag and drop, X11-0155, touch and pens, X11-0156, per-monitor content scale, X11-0157, the
> primary selection, and X11-0158, clipboard formats beyond text, are ✅** -- every Phase M row.
> **Phase N** takes on the gaps left after it: X11-0160, controller mappings, X11-0161, MP3 and FLAC
> for the SDL-free mixer, X11-0162, microphone capture through ALSA, and X11-0163, host facts and
> battery state from Linux, X11-0164, the clipboard handed to a clipboard manager at exit, and
> X11-0165, input-device enumeration, X11-0166, gamepad motion sensors, X11-0167, message boxes
> drawn with Xlib, X11-0168, force feedback through the kernel, X11-0169, file dialogs and OpenUrl
> through the desktop portal, X11-0170, the screen kept on by the desktop, X11-0171, tray icons
> through X11's own system tray protocol, X11-0172, a GL window with a depth buffer by default, and
> X11-0173, the house demo's smoke run looking at its depth buffer, are ✅ -- every Phase N row. See
> [§9 Evidence log](#9-evidence-log) for every command and its result, [§7](#7-findings-that-are-not-this-backends)
> for the eight defects found that belong to other parts of the tree, and
> [§8](#8-defects-this-work-found-in-its-own-implementation) for the eighteen this work found in
> this work's own code -- the last of them by the owner, on a real desktop.
>
> **The implementation is complete** (the owner's assessment, 2026-09-15): no row is open, and
> what is left is battle-testing on hardware and desktops this work could not reach -- listed in
> [§9, "Real-desktop validation"](#real-desktop-validation-what-ran-there-and-what-did-not) --
> rather than a missing subsystem. Further X11 work is fixing what real use finds.
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
| Branch | `claude/x11-native-platform-vie452` (from `next`) for the original rows; `x11` for Phase M and N |
| Baseline commit | `e05b3d0f026e0926741f89459daf02579240399d` — `merge(RLGL): integrate rlgl renderer into next` |
| Working tree at start | clean (`git status --short` empty); no pre-existing user changes to preserve |
| Sibling `sharp-runtime` | cloned at `/home/user/sharp-runtime`, branch `next`, `0c82d9b8` |
| Host (the original 53 rows) | a cloud machine: Ubuntu 24.04, GCC 13.3.0, 4 cores, 15 GiB RAM, no GPU, no X session |
| Host (Phase M and N, branch `x11`) | the owner's workstation: Debian 13, GCC 14, AMD Radeon 780M, GNOME 48 on Wayland -- the X11 backend reaches that desktop through Xwayland only; the tests run on private Xvfb and Xorg servers |
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

Measured before any X11 source existed; recorded in [§9, "Regression"](#regression).

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
| D10 | **A window's display scale is 1.0; the session's scale is each display's `contentScale`, 1.0 unless the session states one.** (Revised by X11-0156, 2026-09-15: until then `Xft.dpi` was reported as the window's display scale, which is D-16.) | The contract's display scale is physical pixels per logical unit, and X11 has one coordinate space: a window's pixels *are* its logical units, whatever the session prefers -- so it is 1 and `highDpi` is false. What a session states is a preference for sizing an interface, which the contract carries as the display's content scale, read in SDL3's order so the two backends agree on one desktop: `Xft.dpi` in the live `RESOURCE_MANAGER`, the XSETTINGS manager's `Gdk/WindowScalingFactor` then `Xft/DPI`, `GDK_SCALE`, else 1; per monitor where KDE's `QT_SCREEN_SCALE_FACTORS` names one. Every value is sanity-clamped to [0.5, 8.0]. XRandR physical millimetres are still not used: they are frequently fictional (EDID lies, and a 1×1 mm or 0×0 mm output is common), and an absurd DPI derived from a fake physical size is worse than no scaling. |
| D11 | **`BorderlessFullscreen` uses `_NET_WM_STATE_FULLSCREEN` and degrades gracefully; `ExclusiveFullscreen` is that plus an XRandR mode of the window's own.** (Revised by X11-0153, 2026-09-15; it was refused until then.) | Genuine exclusive mode means an XRandR mode switch, which changes the user's desktop resolution and can leave it wrong if the process dies -- which is why it was refused rather than faked as borderless until the restore could be made unconditional: in-process on every exit path, and by a forked mode guardian when the process dies however it dies (X11-0153). Where no mode fits, fullscreen stays on the desktop's mode and `GetFullscreenMode()` reports `BorderlessFullscreen`, never "exclusive". When the window manager does not advertise `_NET_WM_STATE_FULLSCREEN` in `_NET_SUPPORTED`, both kinds are refused rather than faked. |
| D12 | **The clipboard is a real selection owner with `TARGETS`, `UTF8_STRING`, `STRING`, `TEXT` and `INCR` for large transfers.** | An X11 clipboard that only works between two CNA windows is not a clipboard. Ownership, `SelectionRequest`, `SelectionNotify`, `SelectionClear` and incremental transfer are all handled, and paste from an external application is what the test asserts. |
| D13 | **A GLX window's `Visual` and `Colormap` are chosen from the `FBConfig` *before* `XCreateWindow`.** | This is the one place where window creation and GL are genuinely coupled. `WindowDescription::renderIntent == OpenGl` plus `openGlFramebuffer` already carries exactly the information needed, so the coupling is resolved at the contract level that already exists — no generic change. A window created with `renderIntent != OpenGl` refuses to host a GL context rather than failing obscurely inside GLX. |
| D14 | **Optional X extensions are optional at *build* time, individually.** | `X11` and `Xext` are mandatory. `Xi` (raw mouse), `Xrandr` (displays), `Xcursor` (shaped cursors), `Xfixes` (pointer barriers/hiding) are each detected separately, each guarded by its own `CNA_X11_HAVE_*` macro, and each turns off exactly one capability when absent. No extension is required because it is convenient. |
| D15 | **`camera` and `sensors` are `false`.** | Core X11 has no standard facility for any of them, and shelling out to `zenity`/`kdialog` is not a native backend. Truthfulness beats checkbox count; the contract's rule is that a false capability *refuses*, which is exactly what these do. (`gamepad` and `joystick` were on this list until X11-0150; see D17. `haptics` was until X11-0168: force feedback is the kernel's, as the controllers are. `nativeFileDialog` was until X11-0169: the desktop portal is a service on the session bus, asked over it, not a program started. `tray` was until X11-0171: X11 has a tray protocol of its own, the freedesktop System Tray Protocol. `messageBox` was until X11-0167: X has no dialog service, but a box is only a window, which the backend draws itself -- no program is started -- as SDL3's X11 backend draws its own once `zenity` has failed, a step CNA does not take.) |
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
| X11-0074 | DPI policy | ✅ | `Xft.dpi` / 96, clamped to [0.5, 8.0], and exactly 1.0 otherwise. XRandR physical millimetres are deliberately **not** used: a 0x0 mm or 1x1 mm output is common enough that deriving DPI from it produces absurd numbers. `highDpi` is advertised only when the session actually states a scale. *(Superseded by X11-0156: the scale moved to the display's content scale, and `highDpi` is false -- D10, D-16.)* |
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
| X11-0156 | Per-monitor DPI | ✅ | Doing it showed the scale was reported in the wrong place (D-16), so D10 was revised first: a window's display scale is 1 and `highDpi` false, because a window's pixels are its logical units; the session's scale is each display's `contentScale`. `X11ContentScale` (one per connection) reads it in SDL3's order -- `Xft.dpi` from the root window's **live** `RESOURCE_MANAGER` (not `XResourceManagerString()`, the string as it was when the connection opened), the XSETTINGS manager's `Gdk/WindowScalingFactor` then `Xft/DPI` (either byte order; a malformed property read as far as it is well-formed), `GDK_SCALE`, else 1 -- and **follows it**: a new `RESOURCE_MANAGER` (`xrdb -merge`), the manager's property, a manager leaving, a new one announcing itself with ICCCM's `MANAGER`. **Per monitor**: X11 itself has none; the one a desktop sets is KDE's `QT_SCREEN_SCALE_FACTORS` (`eDP-1=2;HDMI-1=1;`, or a positional list), and a display named there reports its own factor, the others the session's. A window whose content scale changes -- the setting changed, or it moved onto a monitor with another factor -- gets `DisplayScaleChanged`, as the SDL3 backend delivers SDL's window display-scale change, which covers the content scale too. `CNA::Devices::DisplayInfo::getContentScaleProperty` now combines the two as its documentation says it does: the larger of the window's pixel density and its display's content scale, so it answers 2 at 192 dpi on X11. Tests: `X11ContentScaleTests.cpp` -- 6 without a server (Xft.dpi, XSETTINGS in both byte orders past a string setting and truncated at every length, KDE's lists, the clamp) in `CnaX11MappingTests`; 5 on the launcher's private Xvfb, which they give a `RESOURCE_MANAGER` and an XSETTINGS manager of their own and restore (Xft.dpi 192: display scale 1, pixels = logical × scale, content scale 2; a live change announced; a manager appearing, changing and leaving; KDE's named and positional factors; `GDK_SCALE`), in `CnaX11IntegrationTests`. **Not covered:** a real GNOME/KDE/Xfce session changing its scale (Xvfb, and the settings managers played by the test), two monitors with different factors (Xvfb has one output), `DisplayInfo` itself (`CNA_DEVICES` is off in every build here: compiled with `-fsyntax-only` only). **Not implemented:** `QT_SCREEN_SCALE_FACTORS` changes after start-up (an environment variable, read once). |
| X11-0157 | `PRIMARY` selection | ✅ | The contract had no primary selection, so it gained one: the thirty-first capability, `primarySelection` (appended), and `IPlatform::GetPrimarySelection()`, the clipboard's own interface over the second selection, with a null default so no other platform changes. **X11**: `X11Clipboard` is now one selection's service -- the platform holds one for `CLIPBOARD` and one for `PRIMARY`, each with its own owner window, both offered every event (one requestor can be reading both at once) -- so `PRIMARY` has everything `CLIPBOARD` has: `TARGETS`, `UTF8_STRING`/`STRING`/`TEXT`, `INCR` both ways, Latin-1 transcoded, a stale `SelectionClear` ignored. Watching another client's window is now counted on the connection (`WatchForeignWindow`), since `XSelectInput` replaces a mask and one finished transfer used to clear what another -- or the X11-0156 settings-manager watch -- still needed. **SDL3**: `SDL_*PrimarySelectionText` behind the same interface, `primarySelection` true on Unix desktops (SDL's X11 and Wayland drivers own the real one; elsewhere SDL would keep a private copy, so the capability stays false there). **XNA side**: CNAEXT `CNA::Input::Clipboard::{Get,Set,Has}PrimarySelectionTextEXT`, empty and ignoring writes where there is none. Selecting and middle-click pasting stay the application's. Tests: `X11SelectionTests.cpp` -- 8 on the launcher's Xvfb against another X client (the binary started again as a separate process, owning or pasting a selection): pasting another client's selection and it pasting CNA's, `TARGETS`, the two selections independent both ways, losing the selection, Latin-1, a 3 MB transfer out and a 1 MB one in through `INCR`; the CNAEXT routing (2, fakes); the capability count, the inherited null, and the conformance rule for every platform. **Not covered:** a real desktop's applications (a terminal, a browser) -- the suite's peer and, on CI, `xclip` for `CLIPBOARD` are the external clients; Wayland's primary selection through SDL. |
| X11-0158 | More clipboard formats | ✅ | The contract carried text only, so `IPlatformClipboard` gained formats by MIME type -- `GetMimeTypes()`, `HasData()`, `GetData()`, `SetData()` with a `ClipboardOffer` per format -- as members with defaults (nothing to read, `SetData` refused naming the capability) so a text-only clipboard needs no change, and the thirty-second capability, `clipboardData`. **X11**: a target's name is its MIME type, so `SetData` offers any formats at once, in the application's order, each under its own atom with `INCR` both ways; a UTF-8 text offer (`text/plain;charset=utf-8`, `text/plain`, `UTF8_STRING`) is also served as `UTF8_STRING`, `TEXT`, `STRING`, `text/plain;charset=utf-8` and `text/plain`, and `SetText` is exactly such an offer. `STRING` is now Latin-1 on the way out as it was on the way in (D-17). Reading: `GetMimeTypes()` is the owner's `TARGETS` by name (one `XGetAtomNames`), without the protocol's own targets; `GetData()` the owner's bytes, `INCR` included; `GetText()` also takes text from an application that names it only by MIME type. Applies to `PRIMARY` too. **SDL3**: `SDL_SetClipboardData` with a callback serving a copy SDL frees on replacement (video and arguments checked first, since SDL takes that copy only past its own checks), `SDL_GetClipboardData`/`SDL_HasClipboardData`/`SDL_GetClipboardMimeTypes`; `clipboardData` true. **XNA side**: CNAEXT `CNA::Input::Clipboard::GetMimeTypesEXT`/`HasDataEXT`/`GetDataEXT`/`SetDataEXT` (one format, or several), `SetDataEXT` reporting whether the clipboard took it. Tests: 6 more in `X11SelectionTests.cpp` against another X client -- several formats offered at once and each read back exactly, another application's image and HTML read exactly (every byte value, NULs included), 2.5 MB out and 1.5 MB in through `INCR`, `STRING` as Latin-1, MIME-only text, an image replacing the text -- and 2 without a server (`X11TextEncoding`: Latin-1 both ways, the UTF-8 text names); SDL3's round trip; the CNAEXT routing (3); the contract's defaults. **Not implemented:** `MULTIPLE`, `SAVE_TARGETS` (handing the content to a clipboard manager at exit), any conversion between formats; SDL2 and Win32 stay text-only. **Not covered:** a real desktop's applications (an image editor, a browser).
---

### Phase N — the gaps left after the owner's list (same approval, branch `x11`)

Every Phase M row is done, and what is left of the owner's X11 list needs the owner (the desktop
validation, the `globalPointer` decision). The approval was for about 120 hours of X11 work; these
are the gaps the capability boundary still lists for an SDL-free Linux game, gamepads and sound
first, as the owner ordered the list.

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| X11-0160 | Controller mappings (the community database's format) | ✅ | `src/Linux/EvdevMapping`: the `gamecontrollerdb.txt` format read line by line -- GUID (a checksum written into it moved out), name, every element kind: buttons, whole, half (`+a2`/`-a2`) and inverted (`a1~`) axes, hat directions (`h0.4`), half-axis outputs (`-leftx:b4`), trigger ranges; unknown elements skipped; `platform:` other than Linux refused; the two label conditions turned positional as the database's readers do, any other `hint:` taking its stated default. The database's numbering of a device's inputs (joystick range and above first, then below; hats that look digital as hats, the rest as axes), and its matching (exact identity, then any version; a stated name checksum -- CRC-16/ARC -- must match). The state evaluates a mapping with the database's rules: first element whose range holds the value decides, the previous one's control is released, half axes threshold at their middle, hat changes press and release by bit. The hub reads `CNA_GAMECONTROLLERCONFIG_FILE` then `CNA_GAMECONTROLLERCONFIG` when started; a device a mapping describes is a gamepad through it (also one the gamepad API already describes -- a user's correction). CNA ships no database. Tests: `X11EvdevMappingTests.cpp` (24 without a device, and one more over a whole database file where `CNA_TEST_GAMECONTROLLERDB` names one: all 269 Linux entries of the database SDL embeds are read, 5101 elements), in `CnaX11MappingTests`; 2 more in `CnaX11EvdevTests` against a real uinput pad with no gamepad-API code, a gamepad through a mapping in the environment and only a joystick without. **Not covered:** a physical generic pad. |
| X11-0161 | MP3 and FLAC for CNA's own mixer | ✅ | X11-0151 refused them by name while SDL3_mixer plays both. Now dr_mp3 v0.7.3 and dr_flac v0.13.3 (public domain / MIT No Attribution), vendored byte-identical as `third_party/dr_libs/` from SDL_mixer's copies (so the SDL-free build needs no submodule), compiled in one translation unit with warnings off. The mixer's streaming decoder is generalised from Vorbis to an `EncodedCursor` with a Vorbis, an MP3 and a FLAC implementation, each decoding a block of floats at a time from the shared file bytes and seeking through its decoder; `IsEncoded()` replaces the Vorbis test in the mix loop. Formats are recognised by content (ID3 or an MPEG frame sync; `fLaC`; FLAC inside Ogg by its `\x7FFLAC` packet), so an MP3 named `.wav` plays; sound effects are decoded when loaded, Songs as they play. An MP3's length is its frames' (counted from the headers, which is also what gives a Song its duration); Opus, WMA and XMA stay refused, by name. Tests: `CnaMixerTests.cpp` (3 new, 1 changed: frame counts equal to ffmpeg's decode of the same seven MP3 files (8 to 48 kHz, VBR, a LAME header trimmed to the source's exact 22 050 frames) and the FLAC, the 440 Hz tone measured in each and a stereo pair's 440/660 Hz per channel, empty/garbage/truncated input, streamed against decoded to within 16-bit rounding for both formats, a loop's second pass equal to its first); `CnaMixerXnaTests.cpp` (an MP3 and a FLAC `SoundEffect` with their durations, Opus refused by name); `MediaPlayerTests.cpp` (an MP3 and a FLAC `Song` played by `MediaPlayer` to their own end on ALSA's real-time `null` device). A LAME header's encoder delay and padding are trimmed, as ffmpeg trims them. **Not covered:** a real sound card (as X11-0151). |
| X11-0162 | Microphone capture through ALSA | ✅ | X11-0151 left capture out, so `Microphone.All` was empty on an SDL-free build. `AlsaAudioRecordingDevice` and its provider implement the recording contract: a session opens its PCM non-blocking, negotiates format, rate and channels like playback, starts capture explicitly, and reads it on a thread of its own, ten milliseconds at a time, into a queue the pull API takes whole frames from -- eight seconds at most, oldest dropped -- so a game polling once a frame loses nothing to the device's half-second buffer; an overrun is recovered from and capture restarted, a vanished device reported lost once its queue is read, `null` and `file` paced to real time as playback is. The provider lists ALSA's `default` first, marked default, only when the host's configuration lists it for input, then each card's capture devices through `plughw`, named after card and device; `CNA_AUDIO_RECORDING_DEVICE` names one PCM instead. The libasound loader moved to `AlsaLibrary.cpp`, shared by playback and capture. **The test suites never record a room**: the binaries, ctest and CI set `CNA_AUDIO_RECORDING_DEVICE=null` as they set the playback device. Tests: `AlsaAudioRecordingDeviceTests.cpp` (6: the configured device as the one default entry, `null` captured at real-time pace, a `file` device reading a 440 Hz tone through a configuration of the test's own captured byte for byte, the contract's edges, an unknown device refused by name, the machine's devices ordered as the contract says -- enumeration only); `MicrophoneTests.cpp` now expects devices on ALSA, and its seven capture tests, skipped until now, run on `null`. **Not validated:** a real microphone -- deliberately: the machine's devices were only enumerated. |
| X11-0163 | Battery and power state from sysfs | ✅ | The X11 platform's system-information service was the portable one: no memory figure, no locales, no battery. On a Linux build it is now `src/Linux/LinuxSystemInfo`, needing no display: memory and processors from `sysconf`, preferred locales from `LANG` then `LANGUAGE` (the SDL3 backend's order), and battery state from `/sys/class/power_supply` by SDL3's rules -- system batteries only (`scope` `Device` is a peripheral's), the most time left or else the fullest, no battery meaning plugged in, "Not charging" counted as charged -- with charge over current as a further fallback for time left. `powerInfo` is true wherever the Linux facilities are compiled, with or without an X server; `OpenUrl` stays false (starting `xdg-open` would be shelling out, D15's rule). The service is held by pointer so `X11Platform` has one layout in every translation unit. Tests: `X11LinuxSystemInfoTests.cpp` (9: power-supply trees the tests build -- no battery, discharging with energy/power and with charge/current, every status, a peripheral's and an absent battery ignored, two batteries, no sysfs; locales; this machine's own answers), in `CnaX11MappingTests`; the X11 capability test and the no-display test updated. On this machine: 30 784 MB, 16 processors, the battery charged at 76 % (held at a threshold). |
| X11-0164 | The clipboard handed to a clipboard manager at exit | ✅ | X keeps no copy of a selection, so what a CNA game copied vanished when it quit. Now, as the platform closes its connection, `X11Clipboard::HandOverToClipboardManager()` follows the freedesktop protocol GTK and Qt follow: when CNA owns `CLIPBOARD` and a `CLIPBOARD_MANAGER` owner exists, it converts that selection to `SAVE_TARGETS`, naming its formats in the request's property, and serves the manager -- taking only this selection's requests, the `INCR` traffic they start and the manager's answer off the queue (`XCheckIfEvent`), nothing of the application's -- until the manager confirms or two seconds pass; with no manager, nothing waits. That needed `MULTIPLE`, which managers use to fetch everything at once and which was not implemented: each (target, property) pair is converted, a failed one's property set to `None` in the list written back, `MULTIPLE` listed in `TARGETS`; `INCR` transfers are now keyed by requestor *and* property, since one `MULTIPLE` can start several to one window. SDL3's X11 backend does neither. Tests: 4 more in `X11SelectionTests.cpp` with the peer as a `MULTIPLE` reader and as a clipboard manager: a `MULTIPLE` request answered pair by pair with the unknown target refused in place; the manager fetching text and a 4 KB image with one `MULTIPLE`, taking the clipboard over, and serving both to another client after CNA's platform is destroyed (closing took 0.18 s); closing with no manager at once; a manager that never answers given up on after 2 s. **Not covered:** a real clipboard manager (GPaste, Klipper, xfce4-clipman) -- the peer follows the protocol as written. |
| X11-0165 | Input-device enumeration through XInput2 | ✅ | `X11InputDevices` implements `IPlatformInputDevices` wherever the connection has XInput2: `XIQueryDevice` on every call, as the contract asks, enabled slave keyboards as keyboards, enabled slave pointers as mice and, with a 2.2 touch class, touch devices too -- which is what lets `TouchPanel.GetCapabilities()` report a touchscreen before its first touch. Masters, the server's `XTEST` devices, floating and disabled devices are left out (a deliberate difference from SDL3, which lists masters and `XTEST` too: none is a device a user attached). Gamepads and joysticks come from the evdev hub under their `DeviceEvent` ids (a gamepad listed under both classes, as its events say), the hub started as `GetGamepad()` starts it; X ids are offset by `0x10000` so the two never meet. `XI_HierarchyChanged` -- already selected for pens -- is diffed against the last enumeration and delivered as `DeviceEvent`s class by class. `inputDeviceEnumeration` is true with XInput2. Tests: `X11InputDevicesTests.cpp` -- 2 without a server (classification, the diff), 3 on the launcher's Xvfb (the service where XInput2 is, CNA's lists equal to the server's own slave list read independently with the masters and `XTEST` left out, a uinput pad listed as gamepad and joystick under one id below the X range); on the private Xorg the touchscreen is enumerated as touch and mouse and the pen as mouse before anything touches them. **Not exercised live:** a device hot-plugged into a running server (the private Xorg takes its devices from its configuration at start); the diff that reports it is tested. |
| X11-0166 | Gamepad motion sensors | ✅ | `hid-playstation` and `hid-nintendo` give a pad a second node for its motion sensors, which the hub refused as not-a-controller. Now `IsEvdevMotionSensor` recognises it (`INPUT_PROP_ACCELEROMETER` with an accelerometer's or gyroscope's three axes -- from sysfs too, so it is opened), and the hub pairs it with the pad that has the same unique id or, failing one, the same physical path (`EVIOCGPHYS`, now read from the node and sysfs), whichever appears first; the sensor is read with the pad, can be unplugged alone, and is never a controller of its own. `TryGetSensor` answers in the SDL3 platform's units -- m/s² from units per g, rad/s from units per degree per second -- with `hid-nintendo`'s axis order corrected as SDL3 corrects it; an axis group without a resolution is not reported. `GamepadCapabilities.gyroscope`/`accelerometer` are republished every update; `gamepadSensors` is true with the controllers. Tests: 4 without a device in `X11EvdevLayoutTests.cpp` (recognition, pairing by id and by path, scaling in both units and the Nintendo order, a group without resolution). The live test against real uinput nodes (a pad and its sensor node sharing a path: paired, one g and 90 °/s read back, the node unplugged alone) is **opt-in and runs only on CI's VM** (`CNA_X11_TEST_MOTION_SENSOR=1` in the workflow): on a desktop an input accelerometer is taken by iio-sensor-proxy for screen rotation, so this machine never creates one. **Not covered:** a physical DualShock/DualSense/Switch Pro. |
| X11-0167 | Message boxes drawn with Xlib | ✅ | `messageBox` was false by D15 while SDL3's X11 backend shows boxes of its own drawing. `X11Dialogs` is the dialog service wherever there is a connection: a box opens a connection of its own to the same server, used only by the calling thread, so nothing of the platform's connection is touched and the game's queue is as it was; the parent is asked for on that connection (a parent that has gone is no parent). The window is a dialog (`_NET_WM_WINDOW_TYPE_DIALOG`; `WM_TRANSIENT_FOR` and `_NET_WM_STATE_MODAL` with a parent; the input hint; fixed size; `WM_DELETE_WINDOW`; the title in `_NET_WM_NAME` and in Latin-1 `WM_NAME`), centred on its parent or a third of the way down the screen, never partly off it; a severity band down the left edge, the message wrapped at spaces (never inside a UTF-8 sequence), buttons right-aligned in the order given. Text is a Unicode core font drawn with `XDrawString16` -- the 6x13 `fixed` in its `iso10646-1` edition first -- because a fontset draws only what the process locale's charset holds -- SDL3 switches `LC_ALL` to the environment's for a box's lifetime, which D9 rules out -- while a Unicode font indexed directly needs no locale at all; a server without one gets `fixed` in Latin-1. Answered by a click (press and release on the same button), Return/keypad Enter/space on the focused button (Tab, Shift+Tab and the arrows move it), Escape or the window manager's close for -1. File dialogs refuse, naming `NativeFileDialog`; `nativeFileDialog` stays false. Tests: `X11MessageBoxTests.cpp` -- 12 without a server (the UTF-8 decoding with every malformed form and a non-BMP character, wrapping, layout, hit testing), in `CnaX11MappingTests`; 14 on the launcher's Xvfb, in `CnaX11IntegrationTests`: the dialog properties (type, transient, modal, input hint, class, fixed size) and the centring read by a second client, no parent and a parent that has gone, a parent at the screen's corner, each severity's band and the text read back with `XGetImage`, the focus ring moving on Tab, Tab/Shift+Tab/arrows/Return/space/keypad Enter/Escape/`WM_DELETE_WINDOW` sent as events (a client message that is not `WM_PROTOCOLS` ignored), a click on the third button found by its pixels, a press and release on different buttons answering nothing, the platform's queue polled while a box is up receiving none of its keys, no buttons refused, the three file dialogs refused. **Not covered:** a real window manager's placement of the box (the suite runs without one), and a screen reader -- a core-font window exposes nothing to one. |

| X11-0168 | Force feedback through the kernel | ✅ | `haptics` was false although the kernel's force-feedback interface is what SDL3's Linux haptic backend is built on. `src/Linux/EvdevHaptics` implements `IPlatformHaptics` and `IPlatformHapticDevice` over it. Every event node that can play an effect is a haptic device -- listed from sysfs without opening anything, when this user may write to it; its id (above `0x20000`) kept for as long as the kernel's input device (`inputN`) lasts. Effects are translated as SDL3's Linux backend translates them (directions, envelope, trigger button, the 32767 limits, two condition axes, the phase as a fraction of the period) with three deliberate differences: a left/right magnitude keeps the kernel's full 16-bit range (SDL3 clamps it to half and doubles it, saturating everything above half); a length of 0 becomes 1 ms instead of the kernel's "until stopped"; Custom is refused and not claimed. Simple rumble is SDL3's choice: a sine where there is one, else both motors. The kernel's `FF_GAIN`/`FF_AUTOCENTER` are gain and autocenter. The kernel does not report effects played at once or effect status, and cannot pause, so those answer -1, false and false. Effects belong to the open descriptor, and closing it -- or releasing the Haptic subsystem, which closes what the service opened -- erases them. `OpenFromJoystick` finds a controller's node through the hub. The default vibration device is the first that can rumble and is not a gamepad. Haptic devices are listed by the input-device enumeration too. `haptics` is true with the controllers. Tests: `X11EvdevHapticsTests.cpp` (11 without a device: haptic-device and feature classification against `HapticFeatureEXT`'s values, support by family and waveform, every direction type, each effect family's fields and limits, the rumble choice), in `CnaX11MappingTests`; 6 on uinput devices in `CnaX11EvdevTests`: a wheel's effects uploaded, updated, played three times, stopped, erased and erased on close, with the kernel refusing a change of family and the driver never seeing it; simple rumble as a sine on the wheel and both motors on a pad, the motors apart, both stopped, `CloseAll` erasing; a joystick's force feedback found by its controller id; the default vibration device a motor-only node and never a gamepad; an unplugged device answering false; the platform without a display erasing its rumble when the Haptic subsystem is released. The enumeration test lists its uinput pad as haptic under the service's id. **Not covered:** a physical wheel or pad -- the effects reach a uinput "driver", which records them; what a real motor does with them is the driver's. |

| X11-0169 | File dialogs and OpenUrl through the desktop portal | ✅ | `nativeFileDialog` was false and `OpenUrl` refused, while SDL3 asks xdg-desktop-portal (after which it tries `zenity`, a step CNA does not take). `X11DBus` loads libdbus at run time (headers only at build time, `CNA_X11_HAVE_DBUS`) and connects privately to the session bus: `DBUS_SESSION_BUS_ADDRESS`, else `$XDG_RUNTIME_DIR/bus`, never libdbus's `dbus-launch` fallback. `X11DesktopPortal` finds the portal when the platform is made, without starting it: the name has an owner, or the bus lists it as activatable. File dialogs are `FileChooser.OpenFile`/`SaveFile`: `handle_token`, `modal` with the parent as `x11:<xid>`, `multiple`, `directory` for folders, filters as case-insensitive globs, `current_folder`, and for a save `current_file`/`current_name` from the default location. The request's answer is heard through a match on `Request.Response` that is set up before the call; the request path is predicted from the unique name and the token, and an older portal's own path is followed from its reply. `PollEvents` pumps the bus only while a dialog is open. A callback runs from a later `PollEvents`, exactly once: with the `file:` URIs as paths, or with nothing on a cancel, an error reply, or a bus that went away. `OpenUrl` (LinuxSystemInfo, given an opener) is `OpenURI.OpenURI`, or `OpenURI.OpenFile` with a descriptor for a `file:` URL, and waits at most 5 s for acceptance. `nativeFileDialog` is true with a display and a portal. **No test reaches the desktop's session bus.** A chooser opened there would open on the desktop of whoever runs the tests, so the launcher, ctest (every discovered test of an X11 build) and the test binary as it loads all point `DBUS_SESSION_BUS_ADDRESS` at nothing, and a test asserts it. Tests: `X11DesktopPortalTests.cpp`:
- 6 in `CnaX11MappingTests`: globs, filters, `file:` URIs with every malformed form, request path and parent handle, save locations, and the binary's own missing bus.
- 9 against a portal the test plays on a private `dbus-daemon` whose configuration names no service it could start:
  - no portal, and no bus;
  - every option of an open dialog, and its answer arriving from `Pump` exactly once;
  - a cancel and an error reply, each answered empty;
  - save and folder dialogs;
  - an older portal's own path;
  - an answer long after the question;
  - a callback asking for another dialog;
  - the bus dying with a dialog open;
  - `OpenURI` with a URL, and `OpenFile` with a descriptor that points at the file.
- 2 through the X11 platform on the launcher's Xvfb: without a portal the capability is false and the refusals name it; with one, the chooser is modal to the game's window, `PollEvents` delivers the answer, and `OpenUrl` reaches the portal.

The old refusal test now asserts that there is no portal before it calls anything. **Not covered:** a real portal and its choosers (GNOME, KDE, GTK) -- deliberately never reached from a test; the protocol is followed as xdg-desktop-portal documents it. |

| X11-0170 | The screen kept on by the desktop, or by the server for this client alone | ✅ | `SetScreenSaverEnabled(false)` (XNA's `Guide.IsScreenSaverEnabled`) set the X server's saver timeout to zero. Desktops ignore that: GNOME leaves the server's saver off and blanks the screen itself. It is also server-wide state that a crashed game leaves zeroed. `X11ScreenSaverInhibitor` now works as SDL3's X11 backend does:
- **First:** `org.freedesktop.ScreenSaver.Inhibit(application, "Playing a game")`, where that service is on the session bus. It runs on a connection of its own, which the desktop forgets the moment it closes, and `UnInhibit` closes it.
- **Else:** `XScreenSaverSuspend` (MIT-SCREEN-SAVER 1.1, libXss, `CNA_X11_HAVE_XSS`), for this client only, which the server gives back when the client goes away.
- **Only without that extension:** the old timeout, restored.

`IsScreenSaverEnabled` is what the game asked for, since the desktop's own settings are not a client's to read. Before, it reported the server's timeout, so a GNOME session answered "disabled" before the game had asked for anything. The application name is `/proc/self/comm`.

Tests (`X11ScreenSaverTests.cpp`):
- 1 without a server: the name.
- 2 on the launcher's Xvfb: the server's timeout left untouched while suspended. With the saver set to start after one idle second, it:
  - stays off while suspended;
  - comes on after the lift;
  - comes on after the platform's destruction;
  - comes on after a peer process holding the suspension is killed with `SIGKILL`.
- 2 against a desktop service the test plays on a private bus: `Inhibit` with the name and the reason, once however often it is asked, then `UnInhibit` with the same cookie from the same connection. That connection's name is gone after the lift and after the platform's destruction.

**Not covered:** a real desktop's idle blanking. A killed game's desktop request is also not tested directly; it follows from the bus dropping a dead connection's name. |

| X11-0171 | Tray icons through X11's own system tray protocol | ✅ | `tray` was false because, by D15, a tray is a desktop-environment protocol. X11 has one of its own, though: the freedesktop System Tray Protocol, which Xfce, MATE, LXDE, i3bar and polybar implement, and which KDE Plasma bridges; SDL3 goes through GTK and libappindicator instead. `X11Tray` works on a connection of its own, read by `PollEvents`.
- **The icon** is a window with `_XEMBED_INFO` (version 0, mapped), docked with `SYSTEM_TRAY_REQUEST_DOCK` to the owner of `_NET_SYSTEM_TRAY_S<screen>`, and sized by the tray. The contract gives it no picture, so it is drawn as a badge with the tooltip's first letter. The tooltip is also its `_NET_WM_NAME`.
- **The menu** is an override-redirect `_NET_WM_WINDOW_TYPE_POPUP_MENU` window drawn with `X11CoreFont` (the message box's font, moved to a class of its own), opened by releasing a click on the icon, with the pointer grabbed so a click outside closes it. It shows check boxes, greyed entries that cannot be chosen, and the lit entry under the pointer. Choosing an entry needs a press and a release on it. A checkable entry is toggled first, then its callback runs from `PollEvents`, after the event loop, so a callback may destroy its own icon.
- **The tooltip** appears after 0.6 s of resting on the icon.
- **A tray that restarts** is followed through its `MANAGER` announcement: every icon docks again, and is made anew if the old tray took it along.
- **An icon that outlives the service** is detached rather than left to call into a closed connection.

`tray` is true where a tray owns the selection when the platform is made. Tests: `X11TrayTests.cpp`, 7 on the launcher's Xvfb against a tray the test plays:
- no tray, no service;
- the dock request, `_XEMBED_INFO` and the badge's pixels;
- the menu opened by a click, and a greyed entry doing nothing;
- a checkable entry toggled before its callback, run from `PollEvents`;
- an outside click closing the menu;
- a Quit whose callback destroys its own icon;
- entry changes read back, unknown indices ignored, the tooltip as the window's name;
- the tooltip appearing after a rest and going on leave;
- a restarted tray getting the icon again;
- a released icon leaving the tray.

The capability test still expects false: the bare Xvfb runs no tray. **Not covered:** a real panel, which sizes, scales and composites icons in its own way. |

| X11-0172 | A GL window gets a depth buffer and double buffering unless told otherwise | ✅ | **Found by the owner, running the house demo on this desktop:** on X11 the walls were drawn through each other, and on SDL3 they were not. The EasyGL renderers (OPENGL33 and the rest of the set) leave `WindowDescription::openGlFramebuffer` zeroed, which the contract calls "the platform default", and ask for 24/8 and double buffering only when they create the context. On X11 the window's visual, and every buffer with it, is fixed when the window is made, and X11's default was whatever `glXChooseFBConfig` listed first: the `GLX_DOUBLEBUFFER False` it wrote for an unstated `doubleBuffered`, depth 0, stencil 0. SDL3's default has double buffering and 16-bit depth, so the same renderer drew correctly there. `ChooseVisual` now fills an unstated value with what a game's back buffer needs: double-buffered, depth 24, stencil 8. Where the server has nothing better it steps down to 24/0, 16, 0 depth, and only then single buffering, keeping the depth buffer before multisampling. An explicit request stays a minimum. A context asking for more than its window's visual has is still created, since nothing can be added to the window any more, and says so on standard error. Evidence:
- `X11Live.AGlWindowWithoutAFramebufferRequestGetsWhatAGameNeeds` failed first: the granted context had depth 0, stencil 0 and was single-buffered. It passes now.
- The house demo on the launcher's Xvfb, X11 against SDL3, both with OPENGL33 on the same server at the same moment: **0 differing pixels**.
- The owner's original comparison set X11 with OPENGL33 against SDL3 with VULKAN. What still differs between those two is the renderers', and SDL3 shows it too.

**A lesson recorded:** `X11_House3D_SmokeTest` rendered frames and passed. It does not look at them. |

| X11-0173 | The house demo's smoke run looks at what it drew | ✅ | X11-0172's lesson: `X11_House3D_SmokeTest_<renderer>` rendered frames of a demo whose walls showed through each other, and passed. With `--depth-probe` the demo's last smoke frame draws two planes over the back buffer's centre, the nearer red one first, the farther green one after it, under the default depth state. It reads the centre back and fails, naming the cause, when green wins or when nothing drawn can be read. The probe switches the device to HiDef, because XNA allows `GetBackBufferData` only there; the Reach profile's refusal is XNA's and stays. The smoke run waits for its probe frame however many updates a slow frame gets: with fixed time steps, a slow software frame used to reach the exit before a draw. Registered for every renderer of an X11 build except `HEADLESS` and `STUB`, which keep no pixels to read, and for SDL3's `EasyGL_House3D_SmokeTest`. **The probe was shown to catch the bug:** with X11-0172's fix switched off for one run, OPENGL33 read green and exited 1 (and the new visual warning fired). With the fix it reads red on X11 with OPENGL33, VULKAN and SOFTWARE, and on SDL3 with OPENGL33 and VULKAN. |

---

## 5. Capability matrix (target)

| Capability | X11 | Why |
|---|---|---|
| `multipleWindows` | ✅ true | independent `XCreateWindow` per window |
| `highDpi` | false, by design | one coordinate space: a window's display scale is 1; the session's `Xft.dpi`/XSETTINGS scale, per monitor where KDE sets one, is each display's `contentScale` (D10, X11-0156) |
| `multipleDisplays` | ✅ true when XRandR is present | else the service is null and the capability false |
| `borderlessFullscreen` | ✅ true when the WM advertises it | else false; never faked. `ExclusiveFullscreen` has no flag of its own: it is available wherever borderless is, and borderless where no mode fits (X11-0153) |
| `nativeWindowHandle` | ✅ true | |
| `surfacePresentation` | ✅ true | `XPutImage` |
| `openGlContext` | ✅ true when GLX is present | |
| `vulkanSurface` | ✅ true when the Vulkan headers are present | |
| `clipboard` | ✅ true | real selection ownership, D12 |
| `primarySelection` | ✅ true | `PRIMARY`, the same ownership on the second selection (X11-0157) |
| `clipboardData` | ✅ true | any target, named by its MIME type (X11-0158) |
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
| `gamepadSensors` | ✅ true with the controllers | a pad's motion-sensor node paired with it; each pad's `GamepadCapabilities` says what it has (X11-0166) |
| `haptics` | ✅ true with the controllers | the kernel's force-feedback interface: every node that can play an effect (X11-0168) |
| `sensors` | ❌ false | not an X11 facility |
| `powerInfo` | ✅ true on Linux | the kernel's power supplies in sysfs, with or without a display (X11-0163) |
| `messageBox` | ✅ true | drawn with Xlib, a dialog window on a connection of its own (X11-0167) |
| `nativeFileDialog` | ✅ true with a display and the desktop portal on the session bus | xdg-desktop-portal's FileChooser, asked over D-Bus (X11-0169) |
| `tray` | ✅ true where a system tray runs | the freedesktop System Tray Protocol, XEmbed (X11-0171) |
| `camera` | ❌ false | D15 |
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
backend may change. Each was **reproduced without X11** before being classified that way -- except
F-6, which is recorded as read and says so, and F-7 and F-8, which involve no platform code at all.

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

### F-6 — the Win32 backend reports its monitor's DPI as the window's display scale

The same contract breach as D-16, in `modules/platform/src/Win32/`: `Win32Window::GetDisplayScale()`
returns the window's DPI over 96 and `highDpi` is true, while `GetPixelSize()` is -- correctly, as
its own comment explains -- the client rectangle, so pixels are logical units there as they are on
X11. A renderer relating the two (`PlatformGlSurfaceState::GetClientSize()` divides the drawable by
the display scale) then takes a window on a 150 % monitor for two thirds of its size. The DPI
belongs in the display's `contentScale`, which `Win32SystemServices` already fills from the same
value. **Found by reading while fixing D-16, not reproduced**: Win32 is outside Phase M and no
Windows machine was used. Left for the Win32 work.

### F-7 — a graphics test builds its vertices before `Color`'s constants exist

UndefinedBehaviorSanitizer reports, at the start of every `build-asan/CnaTests` run, a `Color` copied
from an object that is not one yet: `BufferDataBindingContractTests.cpp` (SOFTWARE-291) initialises
a namespace-scope `kVertices` array from `Color::Red`/`Green`/`Blue`, which are defined in another
translation unit, so which is initialised first is unspecified and here the colours are read as
zeros. Nothing to do with the platform axis; seen while running X11-0156's suites under the
sanitizers (with `halt_on_error=0` so they could run). Not fixed here.

### F-8 — a media-library test leaks the stream it saves a picture from

LeakSanitizer reports 8815 bytes in `build-asan/CnaMediaTests`:
`MediaLibrarySavePictureTest.SavePictureFromStreamProducesTheSameResultAsFromBuffer` makes its
`FileStream` with `new` and never deletes it (`MediaLibraryTests.cpp`). A test's leak, not the
library's, in a file this branch never touched; seen while running X11-0161's media suite under
the sanitizers. Not fixed here.

---

## 8. Defects this work found in its own implementation

Recorded because the interesting output of a test suite is the bugs it caught, and every one of
these was found by a test rather than by reading the code -- or, for D-6, D-7, D-16 and D-17, by
reading the contract or the protocol while writing the test that now holds it. None was fixed by
weakening a test.

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
| D-16 | **The session's scale was reported as the window's display scale** (X11-0074's D10, found by X11-0156). The contract's display scale is pixels per logical unit, and `GetPixelSize()` rightly equalled the client size -- so at `Xft.dpi: 192` a window claimed 2 while having 1, and a renderer relating the two (`PlatformGlSurfaceState::GetClientSize()` divides the drawable by the scale) took an 800x600 window for a 400x300 one and doubled every window-to-drawable coordinate. The window's scale is now 1, `highDpi` false, and the session's scale the display's `contentScale` (D10 revised). | reading the contract while writing `X11ContentScaleLive.AtXftDpi192TheWindowStaysOnePixelPerUnitAndTheDisplaySaysTwo`, which asserts the contract's own relation (pixels = logical × display scale) at 192 dpi -- the relation the old code broke; it was not run against the old code | Every X server the suites ran on had no `Xft.dpi` or 96, so the scale was 1 and both answers agreed; the old test checked only that the scale lay in [0.5, 8]. The SDL3 backend, which the contract was written from, answers SDL's pixel density -- 1 on X11 -- so nothing compared the two backends on a scaled session. |
| D-17 | **Text served as `STRING` was UTF-8** (X11-0080's code, found by X11-0158). The owner answered every text target with the UTF-8 bytes it held, `STRING` included, but the ICCCM defines `STRING` as Latin-1 -- the reading side already decoded it so -- so an application that asks for `STRING` and honours it showed each non-ASCII character as two wrong ones. It is now transcoded, a character Latin-1 lacks becoming `?`. | reading the ICCCM while writing `X11SelectionLive.StringIsLatinOneWhenCnaServesText`, which asks for `STRING` as a separate client; the old code sent the UTF-8 bytes under any text target, so the test's expectation could not hold for it (not run against it) | Every reader in the suites -- `xclip`, CNA itself -- asked for `UTF8_STRING` first and never saw a `STRING` reply; SDL3's X11 backend sends UTF-8 under `STRING` too (`SDL_ClipboardTextCallback` answers every text type with the same bytes), so comparing the two would not have shown it either -- this is a deliberate difference from SDL3, on the ICCCM's side. |
| D-18 | **A GL window had no depth buffer and one buffer** (X11-0100's visual choice, found by the owner, fixed by X11-0172). The renderers that state their framebuffer only when they create the context left the window's request zeroed, the contract's "platform default", and X11's default was the first `glXChooseFBConfig` config for `GLX_DOUBLEBUFFER False`, depth 0 and stencil 0. The house demo drew every wall through every other on the owner's desktop; SDL3's own default (double buffering, 16-bit depth) hid the same renderer's reliance on it. | the owner, running the house demo on the real desktop and comparing it with SDL3 | every GL test stated its framebuffer explicitly, and the house smoke test drew frames without looking at them -- which X11-0173 changed |

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
| CI, run 34967714452 | every X11 job green; on the runner (uinput loaded, Xorg and its drivers installed by the job) `CnaX11TouchscreenTests` passed in 2.4 s. The two Win32 jobs fail only on the known `Win32RendererBridge.TheHandleSurvivesAResizeUnchanged` |

### Content scale (X11-0156, 2026-09-15)

On the launcher's private Xvfb, whose `RESOURCE_MANAGER` the suite sets and restores and whose
XSETTINGS manager it plays itself (it owns `_XSETTINGS_S0`, publishes `_XSETTINGS_SETTINGS`, sends
`MANAGER`, then destroys its window). No desktop session was touched.

| Check | Result |
|---|---|
| `X11ContentScaleParsing.*` | 6 passed |
| `X11ContentScaleLive.*` and `X11Live.LogicalAndPixelSizeAgree...` | 6 passed: at `Xft.dpi: 192` the window's display scale is 1, its pixels its logical size, `highDpi` false and the display's content scale 2; 96 -> 144 announced and read as 1.5; a manager's `Gdk/WindowScalingFactor` 2 read, changed to 1 (which wins over its `Xft/DPI`), its departure falling back to `Xft.dpi: 120` = 1.25; `QT_SCREEN_SCALE_FACTORS` `screen=1.75;` and `1.5`; `GDK_SCALE=2` |
| `DisplayInfo.cpp` (`CNA_DEVICES` is off in every build directory here) | `-fsyntax-only -Wall -Wextra` with `-DCNA_DEVICES` and `cna_runtime`'s flags from `cmake-build-x11`: clean, no warning |
| `build-asan` (`address,undefined`): the two content-scale suites, `X11Live.*`, drag and drop, the clipboard and touch selection | 72 passed; no ASan or LSan report; one UBSan report, at start-up and outside the platform code (F-7) |
| every X11 and platform ctest entry | 10/10 |
| `sdl_inventory`, `sdl_classify`, `renderer_sdl_audit`, `sdl_ratchet --strict`, `hot_path_lint`, `nonproduction_sdl_audit`, `check_contract.py` | all pass; the contract's documented declarations 635 -> 644 over the phase |

### The primary selection (X11-0157, 2026-09-15)

Against another X client on the launcher's private Xvfb: the test binary started again as a
separate process with its own connection, owning `PRIMARY` or pasting it with `XConvertSelection`
and speaking `INCR` itself. `xclip` is not installed on this machine, so the `CLIPBOARD` interop
suite skips here and runs on CI.

| Check | Result |
|---|---|
| `X11SelectionLive.*` | 8 passed: both directions, `TARGETS`, the selections independent both ways, losing it, Latin-1, 3 MB out and 1 MB in through `INCR` (each peer's report confirms the transfer was incremental) |
| the CNAEXT routing, capability count, inherited null, IPlatform tests | 25 passed |
| every X11 and platform ctest entry (`cmake-build-x11`) | 10/10 |
| the SDL3 build's platform suite on a private Xvfb, `Sdl3DisplayTest.ThePrimarySelectionIsSeparateFromTheClipboard` among them | 466 passed, 2 skipped (a terminal test, and the conformance case that needs SDL3 to lack surface presentation), 0 failed |
| `build-asan` (`address,undefined`), `CnaPlatformModuleTests`: the selection, content-scale, live, drag-and-drop, clipboard, capability and conformance suites | 157 passed, no report |
| `sdl_inventory.py` | regenerated, 1050 -> 1053 identifiers (the three `SDL_*PrimarySelectionText`), all classified |
| `CnaCApiPlatformOverride.cpp`, which forwards the new accessor (the C API is built in no build directory here) | `-fsyntax-only -Wall -Wextra` with every module's include directory: clean |
| `check_contract.py`, `sdl_ratchet --strict`, `hot_path_lint`, `renderer_sdl_audit`, `nonproduction_sdl_audit` | all pass; 646 documented contract declarations |
| CI, run 34973830230 (X11-0156 and X11-0157) | every X11, SDL3, SDL2, headless and terminal job green; the two Win32 jobs fail only on the known `Win32RendererBridge.TheHandleSurvivesAResizeUnchanged` -- both build the new contract, MSVC and mingw-w64 |

### Clipboard formats (X11-0158, 2026-09-15)

The same peer, now serving each offered type its own bytes.

| Check | Result |
|---|---|
| `X11SelectionLive.*` | 14 passed -- the 8 of X11-0157 and 6 new: three formats offered at once and each read back by the other client exactly (`image/png` with every byte value, `text/html`, text under `UTF8_STRING` and `text/plain;charset=utf-8`), the application's order kept in `TARGETS`; another client's `image/png` (100 000 bytes) and `text/html` read exactly, `image/bmp` absent, no text; 2.5 MB out and 1.5 MB in through `INCR`; `STRING` answered as Latin-1 (`caf\xE9 ?`); MIME-only text; an image replacing the text, which is then refused |
| `X11TextEncoding.*`, the CNAEXT format routing, the contract's text-only defaults, the capability count | 14 passed |
| every X11 and platform ctest entry (`cmake-build-x11`) | 10/10 |
| the SDL3 build's platform suite on a private Xvfb, with `Sdl3DisplayTest.TheClipboardCarriesFormatsOtherThanText` | 475 passed, 2 skipped (as before), 0 failed |
| `build-asan` (`address,undefined`), `CnaPlatformModuleTests`: the selection, text-encoding, content-scale, live, drag-and-drop, clipboard, capability and conformance suites | 166 passed, no report |
| `sdl_inventory.py` | regenerated, 1053 -> 1058 identifiers (`SDL_SetClipboardData` and its four companions), all classified |
| `nonproduction_sdl_audit` | first failed the SDL3 configure: the new SDL3 test named an SDL function in a comment, which the audit counts as an SDL reference in a test file that had none. The comment no longer names it; the ceiling was not raised |
| `check_contract.py` and the other gates | all pass; 654 documented contract declarations |

### Controller mappings (X11-0160, 2026-09-15)

| Check | Result |
|---|---|
| `X11EvdevMapping.*` | 25 passed, the whole-database case included: `CNA_TEST_GAMECONTROLLERDB` pointed at the 269 Linux entries extracted from `third_party/SDL/src/joystick/SDL_gamepad_db.h` (a scratch file, not committed) -- 269 read, 5101 elements, none refused |
| `X11EvdevVirtualDevice.*` (real uinput devices; controller buttons and axes only, which the desktop's input stack ignores) | 13 passed, the two new: a pad with no gamepad-API code is a gamepad in slot 0 through a mapping in the environment -- its b2 is A, a1 up is the left stick up, the hat's up is the D-pad's, b6 the full left trigger, and the press is an event -- and only a joystick without one |
| `build-asan` (`address,undefined`), `CnaPlatformModuleTests`: every evdev suite, the database case included | 63 passed, no report |
| every X11 and platform ctest entry | 10/10 |
| the boundary gates | all pass; nothing in `src/Linux/` names SDL |
| CI, run 34976397502 (X11-0158) | every X11, SDL3, SDL2, headless and terminal job green -- the X11 cells with `xclip`, so the clipboard interop suite ran against the rewritten clipboard there; the two Win32 jobs fail only on the known `Win32RendererBridge.TheHandleSurvivesAResizeUnchanged` |

### MP3 and FLAC (X11-0161, 2026-09-15)

Everything on ALSA's silent `null` device, in `cmake-build-x11` (`CNA_AUDIO_PLATFORM=ALSA`).

| Check | Result |
|---|---|
| `CnaMixer.*` | 24 passed: dr_mp3's frame counts equal ffmpeg's decode of the same seven MP3 fixtures -- 24 192 for the 0.5 s tone without a LAME header, 22 050 exactly for the two with one (VBR, ID3-tagged), 5 184 / 12 672 / 25 344 / 89 856 at 8, 22.05, 48 and 44.1 kHz (2 s) -- and 44 100 for the FLAC; the tone measured at 440 Hz in each, the stereo MP3 at 440 Hz left and 660 Hz right; streamed and predecoded playback equal to 2/32768 for both formats; a loop's second pass equal to its first |
| the audio and media suites (`*Audio*:*Mixer*:*Song*:*Media*:*SoundEffect*:*Xact*:*Microphone*:*DynamicSound*`) | 586 passed, 8 skipped (the recording test outside its ctest entry, and the seven microphone tests: no capture on ALSA -- X11-0162); `MediaPlayerTest.Mp3AndFlacSongsPlayThroughCnasOwnMixerToTheirEnd` plays both songs to their own end in 3.04 s (2.04 + 1.0) |
| `CnaAudioAlsaTests`, `CnaAudioAlsaRecordingTest` | 2/2 |
| `build-asan` (`address,undefined`), `CnaAudioTests` and `CnaMediaTests` | 289 and 296 passed; one UBSan report, in the mixer test's own file helper, which the new empty-MP3 case reached (`memcpy` of an empty vector's null `data()`; fixed); one LeakSanitizer report, 8815 bytes, from a media-library test's own `FileStream` (F-8) |
| the boundary gates | all pass |

### Microphone capture (X11-0162, 2026-09-15)

No real microphone was opened: every session in the suites opens ALSA's `null` device or its
`file` device reading a tone the test wrote.

| Check | Result |
|---|---|
| `AlsaAudioRecordingDevice.*` | 6 passed: `null` captured at real-time pace (300 ms bring 0.2-0.6 s of silence, not everything at once); the `file` device's 0.5 s of 440 Hz captured byte for byte in no less than 0.4 s; the contract's edges (invalid format, double open, empty and sub-frame reads leaving the buffer untouched); an unknown PCM refused naming it; this machine's devices, enumerated only: `Default ALSA Output (currently PipeWire Media Server)` (default, first), `HD-Audio Generic, ALC257 Analog`, `acp63, device 0` |
| `Microphone*` | all passed, the seven capture tests among them -- skipped before this task, now on `null` |
| `build-asan` (`address,undefined`), `CnaAudioTests` | 302 passed, 1 skipped (the recording test outside its ctest entry), no report |
| `AlsaAudioDevice.*` after the loader moved to `AlsaLibrary.cpp` | 6 passed |
| the audio and media suites, and every `CnaAudio*` ctest entry | 599 passed, 1 skipped (as above); 11/11 |

### Host facts and battery (X11-0163, 2026-09-15)

| Check | Result |
|---|---|
| `LinuxSystemInfo.*` | 9 passed, against power-supply trees the tests build, and this machine's own answers: 30 784 MB, 16 processors, battery `Charged` at 76 % (a charge threshold; no time left while not discharging) |
| the no-display platform test and the X11 capability test | `powerInfo` true with and without an X server; memory reported without one |
| every platform, X11 and audio ctest entry | 21/21 |
| `build-asan` (`address,undefined`), `CnaPlatformModuleTests`: the system-info, evdev, live and conformance suites | 178 passed, no report |

### The clipboard at exit (X11-0164, 2026-09-15)

| Check | Result |
|---|---|
| `X11SelectionLive.*` | 18 passed, the 4 new: a `MULTIPLE` request (text, a 4 KB image, an unknown target, `TARGETS`) answered pair by pair, the unknown one `None` in the list written back and `MULTIPLE` among the targets; a clipboard manager fetching text and the image in one `MULTIPLE`, confirming, and serving both to another client after CNA's platform was destroyed -- the whole close 0.18 s; no manager: the close immediate; a silent manager: given up on after 2.07 s |
| every X11 and platform ctest entry | 10/10 |
| `build-asan` (`address,undefined`), `CnaPlatformModuleTests`: the selection, clipboard interop, drag-and-drop and live suites | 78 passed, no report |

### Input-device enumeration (X11-0165, 2026-09-15)

| Check | Result |
|---|---|
| `X11InputDeviceClassification.*` | 2 passed |
| `X11InputDevicesLive.*` on the launcher's Xvfb | 3 passed: Xvfb's own list (`xinput list`: the core pointer and keyboard, their `XTEST` slaves, "Xvfb mouse", "Xvfb keyboard") read independently, and CNA's keyboards and mice exactly its enabled non-`XTEST` slaves; a uinput pad listed as gamepad and joystick under one id |
| `X11Touchscreen.*` on the private Xorg | 9 passed, the new one: `cna-device-0` (the touchscreen, as Xorg names it after its configuration) as touch and mouse, `cna-device-1` (the pen) as mouse, no keyboard, before any touch |
| every X11 and platform ctest entry | 10/10 |
| `build-asan` (`address,undefined`), `CnaPlatformModuleTests`: enumeration, live, selection, conformance | 130 passed, no report |

### Gamepad motion sensors (X11-0166, 2026-09-15)

| Check | Result |
|---|---|
| `X11EvdevLayout.*`, `X11EvdevHub.*` | 29 passed, the 4 new motion tests among them |
| `X11EvdevVirtualDevice.*` here | 13 passed, the motion test skipped by design (no virtual accelerometer on this machine) |
| `X11EvdevVirtualDevice.AMotionSensorNodeIsPairedWithItsPad` on CI | **passed, 105 ms**, run 34990604395 (`9d3bc09b0`), in the step that re-runs the uinput suites with their output in the log and fails on a skip: all 14 evdev cases ran (35-105 ms each) and all 9 touchscreen cases. The earlier run 34986260548 showed only a ctest entry passing in 0.90 s, which could not tell a run from a skip; the per-case times now in the log show that a real run is that fast there -- the runner's udev grants a new node at once, where this desktop's takes up to 2.5 s |
| every X11 and platform ctest entry | 10/10 |
| `build-asan` (`address,undefined`), `CnaPlatformModuleTests`: every evdev suite | 66 passed, no report |

### Message boxes (X11-0167, 2026-09-15)

| Check | Result |
|---|---|
| `X11MessageBoxGeometry.*` | 12 passed |
| `X11MessageBoxLive.*` on the launcher's Xvfb | 14 passed, 3 runs of 3; a box held up and captured showed the amber band, the text and three buttons with the focus ring on the first (the font chosen: the 6x13 `fixed`, `iso10646-1`) |
| `X11Live.CapabilitiesDescribeThisServerRatherThanX11InGeneral` | `messageBox` true and the service present, `nativeFileDialog` still false |
| every X11 and platform ctest entry | 10/10 |
| `build-asan` (`address,undefined`), `CnaPlatformModuleTests`: the message-box suites, the capability test, the conformance suites | 113 passed, 1 skipped as before, no report |

### Force feedback (X11-0168, 2026-09-15)

| Check | Result |
|---|---|
| `X11EvdevHapticEffect.*` | 11 passed |
| `X11EvdevVirtualDevice.*` here | 19 passed, the 6 haptic tests among them (3 shuffled runs of the haptic tests without a failure); the motion test skipped by design |
| `X11InputDevicesLive.*` on the launcher's Xvfb | 3 passed: the uinput pad, now with `FF_RUMBLE`, listed as haptic under the service's id |
| `X11Live.CapabilitiesDescribeThisServerRatherThanX11InGeneral`, the no-display platform test | `haptics` true and the service present with the controllers |
| every X11 and platform ctest entry | 10/10 |
| `build-asan` (`address,undefined`), `CnaPlatformModuleTests`: every evdev suite, the haptic tests, enumeration, capability, conformance | 173 passed, no report |
| CI, run 34993257411 (`d0aac16c9`) | every X11 job green; in the step that re-runs the uinput suites with their output kept, all 7 haptic tests ran and passed on the runner (48-111 ms each). The two Win32 jobs fail only on the known `Win32RendererBridge.TheHandleSurvivesAResizeUnchanged` |

### File dialogs and URLs (X11-0169, 2026-09-15)

No test reached the desktop's session bus. This shell's `DBUS_SESSION_BUS_ADDRESS` is the user's
bus (`unix:path=/run/user/1000/bus`); run from it, the test binary reports its own address as
`unix:path=/nonexistent/cna-test-no-session-bus` and finds no portal
(`X11PortalRequest.NoTestOfThisBinaryReachesTheDesktopsSessionBus`).

| Check | Result |
|---|---|
| `X11PortalRequest.*` | 6 passed |
| `X11DesktopPortalBus.*`, private `dbus-daemon` and the test's own portal | 9 passed; the answer delivered only from `Pump`, exactly once |
| `X11DesktopPortalLive.*`, `X11MessageBoxLive.*`, the capability test on the launcher's Xvfb | 17 passed |
| every X11 and platform ctest entry | 10/10 |
| `build-asan` (`address,undefined`): the portal suites, message boxes, capability, conformance, `LinuxSystemInfo` | 127 passed, no report (the 17 portal tests among them) |
| `sdl_inventory`, `sdl_classify`, `renderer_sdl_audit`, `sdl_ratchet --strict`, `hot_path_lint`, `nonproduction_sdl_audit`, `check_contract` | all pass |
| CI, run 34996430174 (`ca096d1f4`) | every X11 job green, the D-Bus headers found; no per-case log of the portal suites yet |
| CI, run 34998434866 (`32fbe21a2`) | in the step that fails on a skip, the 9 private-bus tests and the 2 through the platform ran and passed on the runner, with `libdbus-1-dev` and `dbus-daemon` installed by the job |

### The screen kept on (X11-0170, 2026-09-15)

| Check | Result |
|---|---|
| `X11ScreenSaverName.*` | 1 passed |
| `X11ScreenSaverLive.*` on the launcher's Xvfb | 2 passed: the saver, set to one idle second, stayed off for 2.5 s while suspended, came on within 4 s after the lift, after the platform's destruction and after the peer's `SIGKILL` |
| `X11ScreenSaverDesktopLive.*`, private bus | 2 passed |
| every X11 and platform ctest entry | 10/10 |
| `build-asan` (`address,undefined`): the screen-saver and portal suites, conformance, capability | 109 passed, no report |
| CI, run 34998434866 (`32fbe21a2`) | all 4 ran and passed on the runner, the killed peer's included (10.1 s); every X11 job green |

### Tray icons (X11-0171, 2026-09-15)

| Check | Result |
|---|---|
| `X11TrayLive.*` on the launcher's Xvfb, against the test's own tray | 7 passed; 2 shuffled runs with the message-box suite, 21 of 21 each |
| `X11MessageBox*` after the font moved to `X11CoreFont` | 26 passed |
| `build-asan` (`address,undefined`): the tray, the message boxes, conformance, capability | 120 passed, no report |
| every X11 and platform ctest entry | 10/10 |

### A GL window with a depth buffer (X11-0172, 2026-09-15)

| Check | Result |
|---|---|
| `X11Live.AGlWindowWithoutAFramebufferRequestGetsWhatAGameNeeds` before the fix | failed: depth 0, stencil 0, single-buffered |
| the same, and the other GL tests, after | 4 passed |
| house demo, launcher's Xvfb, `cmake-build-multi` (X11, OPENGL33) against `cmake-build-vulkan` (SDL3, OPENGL33 added to it) | `compare -metric AE`: 0 pixels differ |
| the same demo on the owner's desktop (XWayland, AMD Radeon 780M, radeonsi) | no visual warning; its window captured with `xwd`: opaque walls, the door, windows and steps in front of the interior -- where the capture before the fix showed the stairs through the front wall |
| every X11 and platform ctest entry; `build-asan`: `X11Live.*` and conformance | 10/10; 138 passed, no report |

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

### Real-desktop validation: what ran there, and what did not

The original rows were built and tested on a cloud machine with no GPU and no X session. Phase M
and N were done on the owner's workstation -- Debian 13, AMD Radeon 780M, GNOME 48 on Wayland --
where no native Xorg session exists: CNA's X11 backend reached the real desktop through Xwayland,
and every test ran on private Xvfb and Xorg servers.

**Ran on the real desktop** (`plans/plan_native_platform_validation.md` has the commands and the 23
defects that run found): the integration suite, hardware GLX (radeonsi) and hardware Vulkan (RADV,
validation layer clean), the MIT-SHM presenter, 300-cycle GL/Vulkan/presenter lifetimes, the
clipboard against `xclip`/`xsel`, the SDL-free demos with every compiled renderer -- and, on
2026-09-15, the house demo in the owner's own hands, which found D-18.

**Not validated anywhere real**, recorded as such rather than assumed:

- a native Xorg session -- only Xwayland;
- the focus- and input-dependent desktop scenarios (`cna_x11_desktop_validation` lifecycle, wm,
  keyboard, text, mouse, relative, soak): the session was locked while they could have run, and
  input is never injected into it;
- physical controllers: a pad, a wheel, a force-feedback motor, a pad's motion sensors -- the kernel
  paths ran against uinput devices, the drivers' layouts were read from their sources;
- a real language input method (Pinyin, Hangul) through ibus or fcitx -- the private ibus runs its
  XKB engine;
- an XRandR mode switch on a physical monitor, several monitors and their hotplug, a desktop that
  sets and changes a scale -- exclusive fullscreen and scale ran on private Xvfb servers;
- window managers other than `openbox` and mutter's Xwayland;
- a real desktop portal's choosers, a real tray panel, a real clipboard manager, a real
  `org.freedesktop.ScreenSaver` -- each was played by the test on a private server or bus;
- a real sound card and microphone -- deliberately: the suites play to and record from ALSA's
  `null` device.
