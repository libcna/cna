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
| `globalPointer` | ✅ | `XQueryPointer` / `XWarpPointer` on the root window |
| `clipboard` | ✅ | real ICCCM selection ownership, including `INCR` |
| `highDpi` | conditional | true only when the session states an `Xft.dpi` |
| `multipleDisplays` | conditional | true when XRandR ≥ 1.2 is present |
| `borderlessFullscreen` | conditional | true when the running window manager advertises `_NET_WM_STATE_FULLSCREEN` |
| `openGlContext` | conditional | true when the server provides GLX ≥ 1.3 |
| `vulkanSurface` | conditional | true when the Vulkan headers were available at build time |
| `relativeMouse` | conditional | true when XInput2 is present at build **and** run time |
| `ime` | ❌ | XIM here delivers *committed* text; the capability promises composition and candidate events |
| `inputDeviceEnumeration` | ❌ | XI2 can answer it; not implemented |
| `gamepad`, `joystick`, `gamepadRumble`, `gamepadSensors` | ❌ | not an X11 facility |
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

The backend is X11, not Linux. Nothing in it is named `Linux*`, and nothing outside
`modules/platform/src/X11/` learns that the host is X11 — which is what keeps a future
`CNA_PLATFORM=WAYLAND` an independent addition rather than a refactor.

The only non-POSIX dependencies are the X client libraries themselves and `clock_gettime`/
`clock_nanosleep` for timing, both of which are POSIX. MIT-SHM uses System V shared memory, and is
optional. The backend should therefore build on FreeBSD, OpenBSD and NetBSD where the X packages
exist; that has **not** been tested, and is recorded as untested rather than claimed.

---

## Running the tests

Three ctest entries, split by what each actually needs:

```sh
ctest --test-dir cmake-build-x11 -R 'CnaX11'
```

| Test | Needs | Covers |
|---|---|---|
| `CnaX11MappingTests` | nothing | scancode and keysym tables, modifiers, wheel/button numbering, focus filtering, auto-repeat coalescing, the SDL-containment scan |
| `CnaX11IntegrationTests` | `Xvfb` | connection, windows, geometry, events, native handles, displays, keyboard, pointer, text input, clipboard interop with `xclip`, GLX contexts, the surface presenter, the error policy |
| `CnaX11WindowManagerTests` | `Xvfb` + `openbox` | EWMH fullscreen, maximise, minimise, restore, focus, multi-window close semantics |

`tools/platform/x11_test_server.sh` starts a private `Xvfb` on a display number it *searches for*
rather than a hardcoded `:99` — a fixed number collides with a parallel ctest job and the collision
looks like flakiness — and exits 77 (ctest's skip code) where `Xvfb` or `openbox` is absent, so a
machine without them records a skip rather than a failure.

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
   includes this backend, and scans every file under `src/X11/` — with comments and string
   literals stripped, so the documentation may explain SDL while the code may not call it.
2. `tools/platform/sdl_ratchet.py` now **denylists** `modules/platform/src/X11/` from the
   module-wide exemption `modules/platform/` otherwise has. The platform module is allowlisted
   because it is the one place SDL may be linked at all; the X11 backend inside it is specifically
   a place where it may not.
3. `CNA_ENABLE_SDL=OFF` makes the whole build refuse any selection that needs SDL, so an
   SDL-requiring dependency cannot creep back in behind a default.
