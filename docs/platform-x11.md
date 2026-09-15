# The native X11 platform backend (`CNA_PLATFORM=X11`)

`CNA_PLATFORM=X11` is a platform backend written directly against Xlib. It is the answer to a
question the project had not yet answered in code:

> Can CNA run on a desktop without SDL?

SDL3 remains CNA's default platform and an excellent one. Nothing here removes, deprecates or
degrades it. What changed is that SDL stopped being the *only* way CNA can reach a desktop, and
that a CNA build in which SDL is neither fetched, compiled nor linked is now a configuration that
exists and is tested.

The plan, the task ledger and the recorded evidence are [`plans/plan_x11.md`](../plans/plan_x11.md).
This document is the capability boundary and the operating manual.

---

## Selecting it

```sh
cmake -S . -B cmake-build-x11 -G Ninja \
      -DCNA_PLATFORM=X11 \
      -DCNA_GRAPHICS_RENDERER=HEADLESS
```

and the SDL-free configuration:

```sh
cmake -S . -B cmake-build-x11-nosdl -G Ninja \
      -DCNA_PLATFORM=X11 \
      -DCNA_AUDIO_PLATFORM=NULL \
      -DCNA_GRAPHICS_RENDERER=HEADLESS \
      -DCNA_ENABLE_SDL=OFF
```

`-DCNA_AUDIO_PLATFORM=ALSA` in place of `NULL` gives that configuration sound, still with no SDL
anywhere: ALSA playback and CNA's own mixer — see [`docs/audio-alsa.md`](audio-alsa.md).

`CNA_ENABLE_SDL` is `AUTO` by default, which is byte-for-byte the behaviour every existing build
had. `OFF` skips the vendored SDL sub-build entirely and refuses, at configure time, any selection
that genuinely needs SDL — naming which one. Nothing is ever substituted silently.

`X11` is offered only where the X development environment exists. Asking for it without that
environment is a hard error naming the missing piece, never a fall back to SDL3: falling back
would build something other than what you asked for.

---

## Dependencies

| Library | Required? | What turns off without it |
|---|---|---|
| `libX11` | **mandatory** | the backend is not offered at all |
| `libXext` | **mandatory** | the backend is not offered at all |
| `X11/XKBlib.h` | **mandatory** | layout-independent scancodes have no substitute |
| `libXi` (XInput2) | optional | `relativeMouse` |
| `libXrandr` (≥ 1.2) | optional | `multipleDisplays`, and `GetDisplays()` returns null |
| `libXcursor` | optional | custom ARGB cursor images; the standard shapes still work |
| `libXfixes` | optional | reserved; no capability depends on it today |
| MIT-SHM (`X11/extensions/XShm.h`) | optional | shared-memory presentation; `XPutImage` still works |
| GLX (through `libglvnd` or Mesa) | optional | `openGlContext` |
| Vulkan **headers** | optional | `vulkanSurface` |
| Linux kernel headers (`linux/input.h`) | optional | `gamepad`, `joystick`, `gamepadRumble` — see [Gamepads and joysticks](#gamepads-and-joysticks) |

Vulkan is headers-only on purpose. `vkCreateXlibSurfaceKHR` is resolved through the
`vkGetInstanceProcAddr` the caller already has, so the platform links no Vulkan loader and a
machine with no Vulkan driver builds and runs everything else unaffected.

On Debian/Ubuntu:

```sh
sudo apt-get install -y libx11-dev libxext-dev libxi-dev libxrandr-dev libxcursor-dev \
                        libxfixes-dev libglx-dev libgl-dev libvulkan-dev
```

Nothing in the backend or its CMake integration hardcodes a distribution path. Discovery is
CMake's own `find_package(X11)`, `find_package(OpenGL COMPONENTS GLX)` and `find_package(Vulkan)`.

---

## Capability boundary

| Capability | X11 | Why |
|---|---|---|
| `multipleWindows` | ✅ | `XCreateWindow` has no single-window limit |
| `nativeWindowHandle` | ✅ | `Display*` + `Window` XID |
| `surfacePresentation` | ✅ | `XPutImage`, with MIT-SHM when available |
| `textInput` | ✅ | `Xutf8LookupString` through XIM, or `XLookupString` |
| `exactKeyboardState` | ✅ | real `KeyRelease` events; nothing is synthesised |
| `pixelAccurateMouse` | ✅ | X reports true pixels |
| `cursorShapes` | ✅ | the core cursor font; Xcursor for custom ARGB images |
| `globalPointer` | ✅ (see [Under Xwayland](#under-xwayland)) | `XQueryPointer` / `XWarpPointer` on the root window |
| `clipboard` | ✅ | real ICCCM selection ownership, including `INCR` |
| `highDpi` | conditional | true only when the session states an `Xft.dpi` |
| `multipleDisplays` | conditional | true when XRandR ≥ 1.2 is present |
| `borderlessFullscreen` | conditional | true when the running window manager advertises `_NET_WM_STATE_FULLSCREEN` |
| `openGlContext` | conditional | true when the server provides GLX ≥ 1.3 |
| `vulkanSurface` | conditional | true when the Vulkan headers were available at build time |
| `relativeMouse` | conditional | true when XInput2 is present at build **and** run time |
| `gamepad`, `joystick`, `gamepadRumble` | conditional | Linux only: the kernel's evdev nodes, true when the build has `linux/input.h` and the machine has `/dev/input` — **with or without a display** |
| `ime` | ❌ | XIM here delivers *committed* text; the capability promises composition and candidate events |
| `inputDeviceEnumeration` | ❌ | XI2 can answer it; not implemented |
| `gamepadSensors` | ❌ | a pad's motion sensors are a second evdev node; pairing it with its pad is not implemented |
| `haptics`, `sensors`, `powerInfo` | ❌ | not an X11 facility |
| `messageBox`, `nativeFileDialog` | ❌ | no core X11 facility; see below |
| `tray` | ❌ | a desktop-environment protocol, not an X11 one |
| `camera` | ❌ | not an X11 facility |
| `managedEntrypoint` | ❌ | an ordinary `main()` |

Nothing here is shelled out to `zenity` or `kdialog` to make a row turn green. A backend that
launched another program to show a message box would not be a native X11 backend, and CNA has no
abstraction for optional desktop-environment integration to hang that on. `false` here means the
call refuses deterministically, which is what a false capability promises.

**The capability set is fixed for the lifetime of a platform instance.** Half of these answers are
about a particular X server — does it have RandR, does the window manager advertise fullscreen,
is there GLX — so the connection is opened in the constructor and the set is computed once. A
process with no display still constructs a platform successfully (`StorageDevice` and
`TitleContainer` must keep working); every display-dependent capability then reads false and
`AcquireSubsystem(Video)` reports the original connection error.

---

## Under Xwayland

On a Wayland desktop (GNOME, KDE Plasma, ...) this backend talks to **Xwayland**, the compositor's
X server for X11 applications — not to a native Xorg. It works there, and was validated there on
GNOME 48 with real hardware (`plans/plan_native_platform_validation.md`), but Xwayland is an X
server whose screen belongs to someone else, and a few things behave differently:

- **The pointer is only fully known over X windows.** Xwayland learns the real cursor position
  only while the compositor's cursor is over one of its surfaces; elsewhere `XQueryPointer`
  answers with the last position it saw. `globalPointer` is therefore exact over the
  application's own windows and stale over native Wayland windows. Pointer input — motion,
  buttons, wheel, and the XI2 raw events relative mode reads — likewise reaches an X client only
  while the cursor is over one of its windows or locked to it. XTest moves only Xwayland's own
  sprite, which is why input tests on such a desktop use a kernel-level (uinput) device.
- **Minimised windows can stay mapped.** mutter keeps iconified windows mapped, so ICCCM's
  map-to-de-iconify never reaches it; `Restore()` also sends the EWMH activation request, which
  is what brings a window back there.
- **Focus is the window manager's decision.** A newly mapped window need not get focus
  (focus-stealing prevention), and a locked session gives focus to no application window at all.
  Relative mouse mode holds the pointer only while its window has focus, so it engages when the
  window is focused.
- **Keyboard layouts are XKB groups.** The compositor hands Xwayland one keymap with every input
  source as a group and switches layouts by locking a group; key codes follow the active group.
- **Scaling happens in the compositor.** With fractional scaling and Xwayland not in its native
  scaling mode, X clients see the logical size and are upscaled; `Xft.dpi` stays 96, so
  `highDpi` is false by this backend's documented policy.
- **Hidden windows are throttled.** Presentation to a window the compositor does not show (behind
  the lock screen, say) runs at a few frames per second with vsync on.
- **Clipboard** interoperability with X clients is complete, including `INCR`. Native Wayland
  applications are reached through the compositor's own X11 selection bridge, which the
  validation could not exercise on GNOME (wl-clipboard gets no data-control protocol there).

## Keyboard

The contract's two key identities are genuinely different things here, and each has its own source.

**Physical (`Scancode`) comes from XKB key *names*.** `AD01` is the top-row leftmost letter
position on every ruleset, under every layout, on every X server shipping `xkeyboard-config`.
The tempting alternative — `X keycode = evdev code + 8` — is true only for the `evdev` ruleset on
Linux, and evdev codes are not the USB HID usage IDs that `Scancode` actually is, so it would need
a second table anyway.

**Logical (`KeyCode`) comes from the group-0, level-0 keysym.** `KeyCode` values are Windows
virtual-key codes, which name a key's *unshifted* identity: there is a `VK_A` and no `VK_a`, and
punctuation is `VK_OEM_*` rather than a character. Reading the keysym the current modifier state
produces would report `None` for every capital letter. Group 0 makes the value track the user's
layout — an AZERTY keyboard reports `KeyCode::A` where a US one reports `Q` — while ignoring
transient modifiers.

**Auto-repeat is detected, never timed.** `XkbSetDetectableAutoRepeat` is requested first; when
the server grants it, a held key produces `KeyPress` with no intervening `KeyRelease` and
`KeyEvent::repeat` comes from the backend's own held-key set. When the server refuses, the
classic `KeyRelease`+`KeyPress` pair at the *identical* timestamp is coalesced by peeking one
event ahead. A human cannot release and re-press a key inside one server millisecond, so a real
double-tap is never swallowed. Emitting a false key-up during auto-repeat would break
`exactKeyboardState`, which this backend advertises as true.

On `FocusOut` every held key is released. Without that, the `KeyRelease` goes to whichever window
has focus now and the key stays down forever.

---

## Text input

`Xutf8LookupString` on a per-window `XIC` turns a key press into the characters it actually
produced: dead keys resolved, Compose sequences applied, an input method's selected candidate
delivered. `XBufferOverflow` regrowth is implemented, and it is not optional — the first call
commits nothing and reports the required size, so treating its return as a length is how a
phrase-committing IME produces truncated mojibake.

A display with no input-method server (a bare `Xvfb`, a minimal container, a session with no
`XMODIFIERS`) falls back to `XLookupString`, whose Latin-1 output is transcoded to UTF-8 rather
than passed through. `textInput` stays true because text input genuinely works; what is lost is
dead-key composition.

`ime` stays **false**. CNA's `Ime` capability promises `TextEditingEvent` and
`TextEditingCandidatesEvent` — the in-progress composition string and the candidate list — which
need XIM preedit callbacks this backend does not implement.

---

## Mouse

X's core protocol has no scroll axis: a wheel notch arrives as a press *and release* of button 4
(up), 5 (down), 6 (left) or 7 (right). Those four become `MouseWheelEvent` and never a
`MouseButtonEvent`; only the press half is counted, so one notch is one scroll. Buttons 8 and
above are the real extra buttons and are renumbered down to 4, 5, 6… so CNA sees no hole where
the wheel was.

Relative mode uses XInput2 `XI_RawMotion` plus a confining, cursor-hiding pointer grab. Raw events
carry the device's own deltas *before* pointer acceleration, screen clipping and any warp — which
removes both classic failure modes of the warp-based approach at once: motion lost at the screen
edge, and the recentring warp being read as real input. Without XInput2 the capability is false
and `SetRelativeMode` refuses, because a warp-based imitation would satisfy the signature and not
the contract.

---

## Displays and DPI

One X screen has not meant one monitor since XRandR 1.2. `XRRGetMonitors` (RandR 1.5) is preferred
because it is the server's own notion of a monitor and correctly merges a pair of CRTCs driving one
panel; active CRTCs (1.2) are the fallback; and only with no RandR at all is the whole screen
reported as a single display — which is then the truth.

**Display scale is `Xft.dpi / 96`, clamped to [0.5, 8.0], and exactly 1.0 otherwise.** X11 has no
authoritative scale. `Xft.dpi` is the one value an X session sets deliberately. XRandR physical
millimetres are deliberately *not* used: a 0×0 mm or 1×1 mm output is common enough that deriving
DPI from it produces absurd numbers, and an absurd scale is worse than no scaling. `highDpi` is
advertised only when the session actually states one.

X11 has one coordinate space, so a window's logical client size and its drawable pixel size are
always equal here. A resize therefore emits both `Resized` and `PixelSizeChanged`.

---

## Fullscreen

`BorderlessFullscreen` uses `_NET_WM_STATE_FULLSCREEN`, and refuses when the running window
manager does not advertise it.

`ExclusiveFullscreen` **refuses**. Genuine exclusive mode means an XRandR mode switch that changes
the user's desktop resolution, with the obligation to restore it on exit, on failure, and after an
abnormal termination. It is implementable and is not implemented, so the call throws rather than
quietly handing back borderless under an "exclusive" name — which would make `GetFullscreenMode()`
lie about what the display is doing.

---

## Clipboard

X11 has no clipboard storage. `CLIPBOARD` is an *ownership token*: the owning client keeps the
data in its own memory and serves it on request. So `SetText` is not a write — it is a promise to
answer `SelectionRequest` events for as long as this process owns the selection, which means the
event pump must be running for an external paste to succeed.

`TARGETS` (including `TARGETS` itself, which is easy to omit and makes well-behaved clients
conclude we offer nothing), `TIMESTAMP`, `UTF8_STRING`, `STRING` and `TEXT` are served, and the
`INCR` protocol is implemented in both directions for payloads past the server's maximum request
size. `STRING`'s Latin-1 is transcoded to UTF-8 on the way in.

This is verified against `xclip` — a genuinely external X client with its own connection and no
CNA code in it — including a 512 KB `INCR` transfer. A clipboard tested only between two CNA
windows would pass while proving nothing.

`PRIMARY` is interned but not implemented; middle-click paste is future work.

---

## Gamepads and joysticks

The X server has not delivered controller input to clients for decades, so controllers do not come
from X at all. On Linux they come from the kernel's evdev nodes (`/dev/input/event*`), read with
nothing but `<linux/input.h>`: no libudev, no libevdev, no SDL. The code is
`modules/platform/src/Linux/`, compiled into the X11 platform wherever that header exists, and
covered by the same SDL-containment scan and ratchet as `src/X11/`. Because none of it touches the
X connection, a process that cannot reach an X server still has its controllers.

**What counts as a controller is decided from sysfs, without opening anything.**
`/sys/class/input/eventN/device/capabilities/*` states the same bitmaps the node's ioctls would
(a test compares the two for every node on the machine it runs on). A device with the kernel's
gamepad button set is a gamepad; one with the joystick button range, or "trigger happy" buttons
together with sticks or a hat, is a joystick; everything else — keyboards, mice, touchpads, tablets,
a DualSense's separate motion-sensor node — is never opened. Asking by opening would mean holding
someone's keyboard open, and it is slow as well: closing an evdev node waits for an RCU grace
period in the kernel, measured at 23–72 ms per node on a ThinkPad, which had the first controller
query stall for 0.8 s before classification moved to sysfs. It now takes about 3 ms.

**Hot-plug is one inotify watch** on `/dev/input`: `IN_CREATE` when a node appears, `IN_ATTRIB`
when udev then grants the session access to it, `IN_DELETE` — or a read failing with `ENODEV` — on
unplug. There is no thread; everything runs inside `PollEvents` and the services' `Update` on the
caller's own thread. Where inotify is unavailable the directory is rescanned at most once a second.

**Nothing is opened until a controller is asked about.** The first `GetGamepad()` or `GetJoystick()`
acquires `PlatformSubsystem::Gamepad`, as on the SDL3 platform, so a game that never touches
`GamePad` pays nothing. Releasing the subsystem to zero closes every node (stopping any rumble);
the services and capabilities stay, reporting every slot empty.

**Mapping follows the kernel's gamepad API**, which names face buttons by *position*, exactly as
CNA's `GamepadButton` does: A is the bottom button whatever is printed on it, so a DualSense's cross
is A and its square is X. `xpad` predates that convention and reports an Xbox pad's left button as
`BTN_X` — the code the gamepad API calls `BTN_NORTH` — so for `xpad`, and for Microsoft pads through
HID, the two are swapped back. `RX`/`RY` is the right stick and `Z`/`RZ` the triggers where both
exist; without `RX`/`RY`, `Z`/`RZ` is the right stick and `BRAKE`/`GAS` the triggers; a pad whose
triggers are only buttons (`BTN_TL2`/`BTN_TR2`, a Switch Pro's ZL/ZR) drives them to 0 or 1. The hat
is the D-pad. Sticks are [-1, 1] with up positive, triggers [0, 1], with no dead zone: XNA applies
its own, and applying one here as well would apply it twice.

**Slots.** The first four gamepads, in connection order, take XNA's four `PlayerIndex` slots; a
fifth waits without one and takes the first slot that frees up. Every controller, gamepads included,
is also a raw device in the joystick service: axes in kernel code order with hats excluded, buttons
in the order controller databases number them, hats as POV positions, and a GUID in the same
bus/vendor/product/version layout SDL uses, so a mapping keyed on one names the same device.

**Events.** `PollEvents` delivers, after the frame's X events, a `DeviceEvent` for each connection
(joystick first, then gamepad, sharing one id; the reverse on disconnection) and a
`ControllerButtonEvent`/`ControllerAxisEvent` for every mapped change. The state a pad is in when it
is opened — a trigger already held — is its state, not a change, and produces no event.

**When the kernel's queue overflows** (`SYN_DROPPED`: nobody read the pad for a while), the damaged
packet is discarded and the pad's whole state is read back, so the snapshot ends on what the pad is
really doing rather than on whichever event happened to survive.

**Rumble** is `FF_RUMBLE`, one effect per pad updated in place rather than re-uploaded; a duration
of 0 means "until changed", which is what XNA's `SetVibration` means, and the kernel caps a duration
at 65 535 ms. It needs write access to the node; a pad opened read-only, or without `FF_RUMBLE`,
reports `rumble = false` in its capabilities and `SetRumble` returns false for it. Closing a pad
removes its effect, so a game that exits mid-rumble does not leave the pad buzzing.

**Not supported:** motion sensors, trigger rumble, light bars, player LEDs, touchpads and battery
state all return false or empty. And a pad that the kernel reports with only the joystick button
range — common for generic HID pads that describe themselves as joysticks — is a raw joystick, not
an XNA gamepad: nothing in the device says which of its buttons is A. SDL answers that with its
community controller database; CNA does not ship one.

**What has been tested, and what has not.** Everything above runs against devices the kernel really
creates through uinput — real evdev nodes, real hot-plug, real `SYN_DROPPED`, the real
force-feedback upload handshake (`CnaX11EvdevTests`). What uinput cannot reproduce is a particular
driver: the `xpad`, `hid-playstation` and `hid-nintendo` layouts are taken from the kernel's own
documentation and drivers and pinned in `X11EvdevLayoutTests.cpp`, but have **not** been exercised
with physical pads.

Other Unix systems running X have no `<linux/input.h>`; their X11 build reports no gamepad and no
joystick.

---

## Graphics bridges

**OpenGL uses GLX**, not EGL. The window this must attach to is an Xlib window with an Xlib
`Visual`, which is GLX's native currency; GLX is part of every X server's GL stack, so it needs no
second vendor library; and `glXCreateContextAttribsARB` gives the same version and profile control
EGL does. EGL would be the right choice for a future Wayland backend — which is exactly the sort
of platform-specific decision a per-window-system backend gets to make and a shared "Linux layer"
would have had to compromise on.

**A GL window's visual is decided before the window exists.** An X window's visual is fixed at
`XCreateWindow` and cannot be changed afterwards, so a GL-capable window must be created with a
GL-capable visual already chosen. `WindowDescription::renderIntent` and `openGlFramebuffer` carry
exactly that information, which is why no generic contract change was needed. A window created
without `WindowRenderIntent::OpenGl` refuses to host a context, with a message saying so, rather
than producing a `BadMatch` inside the driver several calls later.

**Vulkan uses `VK_KHR_xlib_surface`.** `GetInstanceExtensions()` returns
`{VK_KHR_surface, VK_KHR_xlib_surface}`, and `vkCreateXlibSurfaceKHR` is resolved through the
caller's own `vkGetInstanceProcAddr`. Renderers that already obtain an X11 surface themselves from
the generic `NativeWindowHandle` — `vulkan`, `wicked`, `llgl`, `diligent`, `webgpu`, `bgfx`,
`igl` all call `TryGetX11` — keep doing so unchanged. There is deliberately no second, competing
surface-creation path.

**CPU frames go through `XImage`/`XPutImage`.** RGBA8 is converted to the window visual's own
format by deriving each channel's shift and width from the visual's masks, so a 16-bit 5-6-5
visual and a 24-bit BGRX one are both correct with neither special-cased. MIT-SHM is used when the
extension is available *and* the attach succeeds (the error trap catches a remote display); it is
an optimisation and nothing depends on it.

---

## What the backend does *not* take from the host process

CNA is a library. It does not own process startup, and this backend is deliberately conservative
about everything that is process-global.

**`XInitThreads()` is never called.** To be correct it must run before any other Xlib call in the
process: a host that has already talked to X would get an unlocked display regardless, and a host
that has not would have had its global Xlib state reconfigured by a library it merely linked.
Instead, X access is confined to the owning platform instance, and the contract's "poll events once
per frame" rule keeps it uncontended. A host that wants full Xlib thread safety calls
`XInitThreads()` itself before constructing the platform; this backend neither requires that nor
is broken by it.

**The X error handler is saved, chained and restored.** Xlib's default handler calls `exit()` —
measured twice in this project already (`plans/plan_vulkan.md` VULKAN-154/157), where one bad
request either killed a test binary or deadlocked it inside a platform destructor `exit()` had run
under a held lock. CNA installs its own on the first connection, forwards errors for connections it
did not open to whatever handler it replaced, and puts the previous handler back when the last CNA
connection closes. `XSetIOErrorHandler` is left alone: a genuine connection loss must not return.

**The locale is barely touched.** `XOpenIM` needs a locale, and the tempting call is
`setlocale(LC_ALL, "")`. That would change the host process's number and date formatting as a side
effect of opening a window, and a program that started printing `3,14` after adding CNA would have
no way to connect the two. So: only `LC_CTYPE`, only when it is still the startup `"C"` default,
and only from the environment the user already set.

No signal handler is installed, no environment variable is set, and no other process-global state
is modified.

---

## Portability

The backend is X11, not Linux. Nothing outside `modules/platform/src/X11/` learns that the host is
X11 — which is what keeps a future `CNA_PLATFORM=WAYLAND` an independent addition rather than a
refactor. The one Linux-specific part is the controller support in `modules/platform/src/Linux/`,
which knows nothing about X and is compiled only where `<linux/input.h>` exists; a Wayland backend
would take it unchanged.

The only non-POSIX dependencies are the X client libraries themselves and `clock_gettime`/
`clock_nanosleep` for timing, both of which are POSIX. MIT-SHM uses System V shared memory, and is
optional. The backend should therefore build on FreeBSD, OpenBSD and NetBSD where the X packages
exist; that has **not** been tested, and is recorded as untested rather than claimed.

---

## Running the tests

Four ctest entries, split by what each actually needs:

```sh
ctest --test-dir cmake-build-x11 -R 'CnaX11'
```

| Test | Needs | Covers |
|---|---|---|
| `CnaX11MappingTests` | nothing | scancode and keysym tables, modifiers, wheel/button numbering, focus filtering, auto-repeat coalescing, the SDL-containment scan, controller classification and mapping from synthetic device descriptions |
| `CnaX11EvdevTests` | a writable `/dev/uinput` and readable event nodes; no display | controllers the kernel really creates: hot-plug, events, snapshots, slots, `SYN_DROPPED`, rumble, raw joysticks, the platform with no X server; each test skips where the machine grants neither |
| `CnaX11IntegrationTests` | `Xvfb` | connection, windows, geometry, events, native handles, displays, keyboard, pointer, text input, clipboard interop with `xclip`, GLX contexts, the surface presenter, the error policy |
| `CnaX11WindowManagerTests` | `Xvfb` + `openbox` | EWMH fullscreen, maximise, minimise, restore, focus, multi-window close semantics |

`tools/platform/x11_test_server.sh` starts a private `Xvfb` on a display number it *searches for*
rather than a hardcoded `:99` — a fixed number collides with a parallel ctest job and the collision
looks like flakiness — and exits 77 (ctest's skip code) where `Xvfb` or `openbox` is absent, so a
machine without them records a skip rather than a failure.

The launcher owns the *server*; the window-manager suite's own fixture owns the *window manager*,
because a test that needs one also needs to know when it became ready. Having both start `openbox`
raced: the fixture's `--replace` displaced the launcher's mid-run, and a window leaving fullscreen
stopped being noticed. `--require-window-manager` therefore only checks and skips.

The split is not tidiness. A bare `Xvfb` has no window manager, so maximise, minimise, restore,
EWMH fullscreen and focus do not happen there at all; asserting them against one would test the
environment rather than the backend.

**What Xvfb cannot cover**, and which therefore needs a real desktop: a physical GPU's GLX driver,
a real compositor's fullscreen behaviour, multi-monitor XRandR layouts and hotplug, real input
devices and a real input-method server (ibus, fcitx). `plans/plan_x11.md` records those gaps
rather than treating a green Xvfb run as equivalent.

---

## Proving SDL is gone

```sh
ldd cmake-build-x11-nosdl/CnaPlatformModuleTests | grep -i sdl   # nothing
nm -uC cmake-build-x11-nosdl/CnaPlatformModuleTests | grep -c SDL_   # 0
```

Three independent checks keep it that way rather than leaving it to a note:

1. `X11IsSdlFreeTests.cpp` `#error`s if an SDL header ever reaches a translation unit that also
   includes this backend, and scans every file under `src/X11/` and `src/Linux/` — with comments
   and string literals stripped, so the documentation may explain SDL while the code may not call it.
2. `tools/platform/sdl_ratchet.py` now **denylists** `modules/platform/src/X11/` and
   `modules/platform/src/Linux/` from the
   module-wide exemption `modules/platform/` otherwise has. The platform module is allowlisted
   because it is the one place SDL may be linked at all; the X11 backend inside it is specifically
   a place where it may not.
3. `CNA_ENABLE_SDL=OFF` makes the whole build refuse any selection that needs SDL, so an
   SDL-requiring dependency cannot creep back in behind a default.
