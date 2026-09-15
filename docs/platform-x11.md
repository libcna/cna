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
| `libXi` (XInput2) | optional | `relativeMouse`; touchscreen and pen contacts (the server must speak XInput 2.2 for touch) |
| `libXrandr` (≥ 1.2) | optional | `multipleDisplays`, and `GetDisplays()` returns null; exclusive fullscreen then has no mode to switch to and is borderless |
| `libXau` | optional (installed with `libX11`) | the exclusive-fullscreen mode guardian's cookie, for a server that asks for one — see [Fullscreen](#fullscreen) |
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
| `primarySelection` | ✅ | `PRIMARY`, the middle-click selection, served and read like `CLIPBOARD` — see [The primary selection](#the-primary-selection) |
| `clipboardData` | ✅ | any format by MIME type (`image/png`, `text/html`, ...), `INCR` both ways — see [Formats other than text](#formats-other-than-text) |
| `dragAndDrop` | ✅ | XDND 5, target side: files and text dropped by another client — see [Drag and drop](#drag-and-drop) |
| `highDpi` | ❌ by design | X11 has one coordinate space, so a window's display scale is 1; the session's scale is each display's `contentScale` — see [Displays and DPI](#displays-and-dpi) |
| `multipleDisplays` | conditional | true when XRandR ≥ 1.2 is present |
| `borderlessFullscreen` | conditional | true when the running window manager advertises `_NET_WM_STATE_FULLSCREEN` |
| `openGlContext` | conditional | true when the server provides GLX ≥ 1.3 |
| `vulkanSurface` | conditional | true when the Vulkan headers were available at build time |
| `relativeMouse` | conditional | true when XInput2 is present at build **and** run time |
| `gamepad`, `joystick`, `gamepadRumble` | conditional | Linux only: the kernel's evdev nodes, true when the build has `linux/input.h` and the machine has `/dev/input` — **with or without a display** |
| `ime` | conditional | true when the application asked to draw the composition (`CNA_IME_IMPLEMENTED_UI=composition`) and the input method offers on-the-spot composition; candidate lists stay with the input method — see [Input-method composition](#input-method-composition) |
| `inputDeviceEnumeration` | conditional | true when XInput2 is present: keyboards, mice and touch devices from the server, controllers from the kernel — see [Input devices](#input-devices) |
| `gamepadSensors` | conditional | Linux only, with the controllers: a pad's motion-sensor node paired with it — each pad's `GamepadCapabilities` says whether it has a gyroscope and an accelerometer |
| `powerInfo` | conditional | Linux only: the kernel's power supplies in sysfs — **with or without a display** — see [Host facts](#host-facts) |
| `haptics`, `sensors` | ❌ | not an X11 facility |
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
  scaling mode, X clients see the logical size and are upscaled; `Xft.dpi` stays 96, so the
  displays report a content scale of 1 and the compositor does all of the scaling.
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

### Input-method composition

By default the input method draws its own composition, in its own window, and only the committed
text reaches the application. That is the behaviour an XNA game needs: XNA had no IME API, so no
game draws a composition. `ime` is then **false**.

An application that does draw the composition — through CNA's `TextInputEXT` editing events —
says so with **`CNA_IME_IMPLEMENTED_UI=composition`** in its environment, read once when the
platform is created (on the SDL3 backend the same choice is SDL's own IME-UI hint). If the input
method offers the *on-the-spot* style (`XIMPreeditCallbacks`) — ibus and fcitx do — the backend
asks for it, and the input method hands its composition over through its preedit callbacks: each
change is a `TextEditingEvent` (the text, the caret, and the segment being converted as the
selection), delivered from `PollEvents` in order with the key and text events around it, and the
end of a composition is an editing event with empty text. `ime` is **true** exactly then.

The candidate list is never delivered: XIM has no protocol for handing it to the client, so the
input method draws its own candidate window at the spot `SetInputArea` gives it, as it does for
every X application. `TextEditingCandidatesEvent` does not occur on this backend.

**Outside text entry the input method gets no keys.** Key events are offered to it (`XFilterEvent`)
only while text input is started for their window, and its input context is focused only then.
Unfocusing alone would not do: Xlib forwards every key of a window with an input context to the
input-method server, and ibus processes them focused or not — measured: with a dead-key layout
the presses of a dead key and the letter after it never reached the game at all. A Hangul or
Japanese input mode left on would otherwise turn a game's WASD into a composition.

Stopping text input abandons a composition in progress (`XmbResetIC`) and reports it ended, so
nothing half-typed resurfaces when text input starts again. A commit arrives from Xlib as a press
of keycode 0; that is delivered as text and never as a key event for "no key".

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

**A window's display scale is exactly 1.0.** X11 has one coordinate space, so a window's logical
client size and its drawable pixel size are always equal, and the display scale — pixels per
logical unit — is 1 whatever the session's settings say. `highDpi` is false. A resize emits both
`Resized` and `PixelSizeChanged`. (Until `plans/plan_x11.md` X11-0156 the session's scale was
reported here instead, which is D-16 there: a renderer dividing the drawable by it took an 800×600
window at 192 dpi for a 400×300 one.)

**The session's scale is each display's `contentScale`** — a preference for sizing an interface,
read in SDL3's order so the two backends agree on one desktop:

1. `Xft.dpi` in the root window's live `RESOURCE_MANAGER` (what `xrdb` sets), over 96;
2. the XSETTINGS manager's `Gdk/WindowScalingFactor`, then its `Xft/DPI` (in 1024ths) over 96 —
   what `gsd-xsettings`, `xsettingsd` and Xfce's settings daemon publish;
3. `GDK_SCALE`;
4. 1.0.

A value outside [0.5, 8.0] is ignored as a broken setting. XRandR physical millimetres are
deliberately *not* used: a 0×0 mm or 1×1 mm output is common enough that deriving DPI from it
produces absurd numbers, and an absurd scale is worse than no scaling.

**Per monitor.** X11 itself has no per-monitor scale. The one a desktop sets is KDE's
`QT_SCREEN_SCALE_FACTORS` (`eDP-1=2;HDMI-1=1;`, or a list in screen order): a display named there
reports its own factor, the others the session's. It is an environment variable, read once when
the platform starts.

**Changes are followed.** A new `RESOURCE_MANAGER` (`xrdb -merge`), the settings manager's
property, a manager going away or a new one announcing itself are all picked up while the game
runs. A window whose content scale changes — because the setting changed, or because it moved onto
a monitor with another factor — receives `DisplayScaleChanged`, as SDL3 delivers it.

`CNA::Devices::DisplayInfo::getContentScaleProperty` combines the two: the larger of the window's
pixel density and its display's content scale. So a game asking it gets 2.0 at 192 dpi on X11,
while its back buffer stays one pixel per unit.

---

## Fullscreen

`BorderlessFullscreen` uses `_NET_WM_STATE_FULLSCREEN`, and refuses when the running window
manager does not advertise it.

`ExclusiveFullscreen` is borderless fullscreen with a **display mode of the window's own**, set
through XRandR (`plans/plan_x11.md` X11-0153). It is what an XNA game's `IsFullScreen` asks for.
Fullscreen itself is still the window manager's to perform, so exclusive refuses exactly where
borderless does.

- **Which mode.** SDL's rule, so a game gets the same mode it would under the SDL3 backend: the
  smallest mode at least as large as the window, preferring the aspect ratio closest to the
  window's; among modes of one size, the refresh rate closest to the desktop's. So XNA's default
  800x480 back buffer gets a 16:9 mode over a smaller 4:3 one. `SetSize` while exclusive asks for
  a new *mode*, not a new window size — the window manager keeps a fullscreen window the size of
  its monitor — which is how `GraphicsDevice` applies a back buffer after `IsFullScreen`.
- **When there is no mode** (the size is larger than every mode, the server has no RandR 1.2, the
  monitor is scaled or transformed) the window is fullscreen on the desktop's own mode, and
  `GetFullscreenMode()` says `BorderlessFullscreen`, because that is what the display is doing. SDL
  makes the same substitution and reports it the same way.
- **The screen** is resized around the change the way `xrandr` does it — to the bounding box of
  the lit CRTCs, grown before and shrunk after — so on one monitor the pointer cannot wander off
  the visible area, and on several the other monitors keep their place (SDL shrinks the screen to
  the mode and fails with `BadMatch` there).
- **The mode is in effect only while the window is on screen.** Hiding or minimising the window
  gives the desktop its mode back; losing focus minimises the window and gives it back, as SDL
  does, so a player who switches away from an 800x600 game does not find the desktop in 800x600.
  Showing, restoring or refocusing the window takes the mode again. A focus loss within 400 ms of
  a mode change is looked at again before it is believed: changing a monitor's mode makes some
  window managers move focus while they lay the screen out again. Under Xwayland, where a mode
  change is emulated for the requesting client alone, focus loss changes nothing.
  `GetFullscreenMode()` says `ExclusiveFullscreen` throughout, including while minimised.
- **The mode always goes back.** Leaving exclusive fullscreen, destroying the window, another
  client destroying it, another client taking it out of fullscreen (a key binding, a pager) and
  the platform closing its connection all restore the CRTC exactly. A mode *someone else* set in
  the meantime — the user changing the resolution while the game runs — is left alone.
- **Even when the process dies.** With the first mode change, the backend `fork()`s a small
  **mode guardian** that waits on a socket. However the game ends — a crash, `abort()`,
  `SIGKILL`, the OOM killer — the kernel closes the game's end, the guardian reads end-of-file,
  restores the mode over a connection of its own and exits. A normal restore disarms it first. It
  calls only async-signal-safe functions (it speaks the X protocol itself, since it is a fork of a
  threaded process and cannot call Xlib), leaves the terminal's session so a Ctrl+C that kills the
  game does not kill it too, keeps nothing of the game's open, and shows up as `cna-x11-mode` in
  `ps`. It authenticates with the game's own cookie (libXau) and reaches the same address the
  game's connection did. A game that `fork()`s a child which outlives it keeps the guardian's
  end-of-file waiting until that child exits too.
- **The display service** tells the two modes apart: `TryGetCurrentDisplayMode` reports the mode
  the monitor is in, `DisplayInfo::desktopMode` the one the desktop gets back.

---

## Clipboard

X11 has no clipboard storage. `CLIPBOARD` is an *ownership token*: the owning client keeps the
data in its own memory and serves it on request. So `SetText` is not a write — it is a promise to
answer `SelectionRequest` events for as long as this process owns the selection, which means the
event pump must be running for an external paste to succeed.

`TARGETS` (including `TARGETS` itself, which is easy to omit and makes well-behaved clients
conclude we offer nothing), `TIMESTAMP`, and text under every name X applications ask for it by —
`UTF8_STRING`, `TEXT`, `STRING`, `text/plain;charset=utf-8` and `text/plain` — are served, and the
`INCR` protocol is implemented in both directions for payloads past the server's maximum request
size. `STRING` is Latin-1, as the ICCCM defines it, both ways: transcoded to UTF-8 on the way in,
and from UTF-8 on the way out, a character Latin-1 lacks becoming `?` (until `plans/plan_x11.md`
X11-0158 the UTF-8 bytes were sent under `STRING` unchanged — D-17 there).

### Formats other than text

A selection target is an atom, and for anything but text the atom's name is the format's MIME type.
So `IPlatformClipboard::SetData()` offers any formats at once — an image, HTML beside its plain
text — each served under its MIME type, `INCR` included; a UTF-8 text format among them is also
served under the text names above. `GetMimeTypes()` lists what the owner offers, by name, without
the protocol's own targets (`TARGETS`, `TIMESTAMP`, `MULTIPLE`, ...); an older application's text
names (`UTF8_STRING`, `STRING`) appear as they are named. `GetData()` returns the owner's bytes
exactly. The capability is `clipboardData`; games reach it through the CNAEXT
`CNA::Input::Clipboard::SetDataEXT()` / `GetDataEXT()` / `GetMimeTypesEXT()`. Converting between
formats — decoding a PNG, stripping HTML — is not the platform's business and is not done.

### When the game closes

X keeps no copy of a selection: what a program copied is gone the moment it exits — unless the
desktop runs a clipboard manager and the program hands the content over. CNA does, as GTK and Qt
applications do (`plans/plan_x11.md` X11-0164): as the platform closes its connection, if it owns
`CLIPBOARD` and some client owns `CLIPBOARD_MANAGER`, it converts that selection to `SAVE_TARGETS`
naming the formats it offers, and keeps answering the manager — which usually fetches them all in
one `MULTIPLE` request — until the manager confirms, for at most two seconds. With no manager
running, nothing waits. `MULTIPLE` is answered for any requestor: each (target, property) pair
converted, `INCR` included, and a target that cannot be converted marked `None` in the list that is
written back; `TARGETS` lists it. SDL3's X11 backend does neither.

This is verified against `xclip` — a genuinely external X client with its own connection and no
CNA code in it — including a 512 KB `INCR` transfer. A clipboard tested only between two CNA
windows would pass while proving nothing.

### The primary selection

`PRIMARY` — what an X11 desktop pastes with the middle mouse button — is the same protocol on a
second selection, and is served the same way through `IPlatform::GetPrimarySelection()`
(`plans/plan_x11.md` X11-0157): the same targets, `INCR` both ways, Latin-1 transcoded. The two are
independent, as they are for every X application: selecting does not copy and copying does not
select. Each has an owner window of its own.

The backend carries the text both ways and does nothing else. Selecting text in a text field and
pasting on a middle click are the application's to do — through the CNAEXT
`CNA::Input::Clipboard::SetPrimarySelectionTextEXT()` / `GetPrimarySelectionTextEXT()` — because only
the application knows what is selected and where the pointer is.

It is verified against another X client too: the test binary started again as a separate process,
owning `PRIMARY` or pasting it, a 3 MB transfer included. It changes the server's selections, so
it runs only on the test launcher's private server.

---

## Touch and pens

Where the server speaks XInput 2.2, every window CNA creates selects touch events, and each
touchscreen contact becomes the contract's `TouchEvent` — `Down`, `Motion` with its delta, `Up` —
in the window it began in, normalised against the client size it had then. XNA's `TouchPanel`
reads them. A window destroyed mid-touch has its contacts `Cancelled`, so no finger stays down.

- **The mouse keeps working on a touchscreen.** A window that selects touch events is no longer
  sent the pointer events the server emulates from touches, so the backend drives the mouse from
  the contact the server marks as emulating the pointer: motion, a left-button press and release,
  and the mouse snapshot with them — once each, measured. SDL3 does the same by synthesising mouse
  events from touches.
- **Pens.** SDL3's rule finds them — an enabled slave pointer with an "Abs Pressure" valuator — at
  start-up and whenever the device hierarchy changes. A pen whose tip is down is a touch with the
  pressure it reports; a hovering pen is not. Its own XI2 events are selected per device, so the
  core pointer events it drives — the mouse — arrive as they always did, also measured. Pressure is
  1 for a finger, as SDL3 reports it.
- **Not delivered:** a pen's tilt, its barrel buttons and eraser as such (an eraser touching is a
  touch like the tip), touch-ownership and gesture grabs, and XInput 2.2 on a server that has only
  2.0 or 2.1 (touch is then absent; the mouse still works through the server's own emulation).

---

## Drag and drop

Every window CNA creates announces `XdndAware` (version 5), and a drag from another client — a
file manager, a browser, a text editor — arrives as the contract's `DropEvent` sequence: `Begin`
when the drag first comes over the window, `Position` as it moves, then on the drop one `File` per
file or one `Text`, and `Complete`; a drag that leaves ends with `Complete` alone. An XNA game sees
it as `GameWindow::FileDropEXT` (every file of a drop at once, MonoGame's shape) and `TextDropEXT`.

- **What is taken.** `text/uri-list` first, then UTF-8 text (`text/plain;charset=utf-8`,
  `UTF8_STRING`), then `text/plain`, `TEXT` and `STRING` (Latin-1, converted). A `file:` URI —
  `file:///`, `file://localhost/`, `file://<this host>/` or `file:/` — is percent-decoded into a
  local path; a URI that is not a local file, such as a link dragged out of a browser, arrives as
  text rather than being dropped silently (SDL3 discards it). Text arrives whole, where SDL3's X11
  backend splits it into one event per line.
- **What is refused.** A drag offering none of those types is refused in the protocol
  (`XdndStatus` "no") and produces no event at all — a game must not show a "drop here" highlight
  for something it cannot receive. Only `XdndActionCopy` is performed: a move would ask the source
  to delete its files.
- **The data** is read through the clipboard's selection reader, `INCR` included, synchronously and
  within its one-second timeouts: a source that dies mid-drop costs the event pump a bounded wait,
  and the sequence still ends with `Complete`. Every drop is answered with `XdndFinished`.
- **Not implemented:** dragging *from* a CNA window (the source side), and `XdndProxy`.

---

## Input devices

`GetInputDevices()` answers from XInput2 each time it is asked (`plans/plan_x11.md` X11-0165):
every enabled slave keyboard is a keyboard, every enabled slave pointer a mouse, and a slave pointer
with an XInput 2.2 touch class a touch device as well — so `TouchPanel.GetCapabilities()` knows a
touchscreen is there before anyone has touched it. The core pointer and keyboard every server has,
the server's own `XTEST` devices, floating and disabled devices are left out: none is a device a
user attached, and reporting them would tell a game a keyboard is attached to a machine with none.
(Xvfb's own "Xvfb keyboard" and "Xvfb mouse" are slaves, and are listed.) Gamepads and joysticks
are the Linux controllers, under the same ids as their `DeviceEvent`s; asking about them starts the
controller hub as `GetGamepad()` does. X device ids are offset (`0x10000 +` the XInput id) so the two
never meet. Plugging a device in or out changes the server's hierarchy, and each change is reported
as `DeviceEvent`s for the classes that appeared or went. X has no haptic devices or sensors.

## Host facts

`GetSystemInfo()` answers from Linux itself on a Linux build (`plans/plan_x11.md` X11-0163), with
or without an X server: memory and online processors from `sysconf`; the preferred locales from
`LANG`, then each entry of `LANGUAGE` (codesets and modifiers dropped, `C`/`POSIX` skipped), as the
SDL3 backend reads them; and battery state from `/sys/class/power_supply`. Only system batteries
count — a supply whose `scope` is `Device` is a gamepad's or a mouse's — and with several, the one
with the most time left, or else the fullest, is reported. No battery means plugged in; a battery
that is "Not charging" while plugged in (held at a charge threshold) counts as charged; time left
is `time_to_empty_now`, else energy over power, else charge over current. `OpenUrl` stays false:
opening a URL on Linux means starting another program, which this platform does not do on a game's
behalf. Elsewhere the portable answers apply (no battery information, no locales).

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

**Controller mappings.** A pad that the kernel reports with only the joystick button range —
common for generic HID pads that describe themselves as joysticks — says nothing about which of
its buttons is A, so on its own it is a raw joystick, not an XNA gamepad. A mapping in the
community controller database's format (`gamecontrollerdb.txt`, the one SDL-based games use) makes
it one (`plans/plan_x11.md` X11-0160): the file named by `CNA_GAMECONTROLLERCONFIG_FILE`, then the
entries in `CNA_GAMECONTROLLERCONFIG` (one per line), read when the controllers are first asked
about. The same file works for both, byte for byte:

```sh
CNA_GAMECONTROLLERCONFIG_FILE=~/gamecontrollerdb.txt ./my-game
```

A mapping matches the device's bus, vendor, product and version — then any version of it — and,
where the entry states one, the checksum of its name. Its button, axis and hat numbers are the
database's: the joystick range and everything above it first, then the rest; hats that look digital
(-1..1, or no fuzz, flat or resolution) as hats, the rest as axes. Every element kind is honoured —
half axes (`+a2`), inverted axes (`a1~`), buttons on axes and axes on buttons (`-leftx:b4`), hat
directions — with the database's rules: the first element whose range holds an axis's value
decides, and the control of the element that decided before is let go. A labelled Nintendo-style
entry is turned positional as other readers do; any other condition (`hint:`) takes the default the
entry states. A mapping also overrides a pad the gamepad API already describes, which is how a user
corrects one. CNA ships no database of its own: which one to use — the community file, a game's
own — is the user's or the game's choice. All 269 Linux entries of the database SDL embeds are read
(a test does, given the file).

**Motion sensors** (`plans/plan_x11.md` X11-0166). `hid-playstation` and `hid-nintendo` give a pad
a second node for its accelerometer and gyroscope (`INPUT_PROP_ACCELEROMETER`). It is opened with
the pad — never on its own as a controller — and given to the pad with the same unique id (a
Bluetooth address, a serial) or, failing one, the same physical path, whichever node appears first;
`TryGetSensor` then answers in the SDL3 platform's units: metres per second squared, radians per
second, `hid-nintendo`'s axis order turned into the gamepad convention. An axis group that states no
resolution cannot be put into physical units and is not reported. The sensor can come and go on its
own; the pad's capabilities follow.

**Not supported:** trigger rumble, light bars, player LEDs, touchpads and battery state all return
false or empty.

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

Seven ctest entries, split by what each actually needs:

```sh
ctest --test-dir cmake-build-x11 -R 'CnaX11'
```

| Test | Needs | Covers |
|---|---|---|
| `CnaX11MappingTests` | nothing | scancode and keysym tables, modifiers, wheel/button numbering, focus filtering, auto-repeat coalescing, the SDL-containment scan, controller classification and mapping from synthetic device descriptions |
| `CnaX11InputMethodTests` | `Xvfb` + ibus (with its XIM server), `xdotool`, `setxkbmap` | composition with and without the application drawing it, a commit, abandoning a composition, and no keys taken outside text entry — against a private ibus the launcher starts (`--with-ibus`) |
| `CnaX11EvdevTests` | a writable `/dev/uinput` and readable event nodes; no display | controllers the kernel really creates: hot-plug, events, snapshots, slots, `SYN_DROPPED`, rumble, raw joysticks, the platform with no X server; each test skips where the machine grants neither |
| `CnaX11IntegrationTests` | `Xvfb` | connection, windows, geometry, events, native handles, displays, keyboard, pointer, text input, clipboard interop with `xclip`, GLX contexts, the surface presenter, the error policy, and drag and drop against a real XDND source — a second process of the test binary |
| `CnaX11WindowManagerTests` | `Xvfb` + `openbox` | EWMH fullscreen, maximise, minimise, restore, focus, multi-window close semantics |
| `CnaX11TouchscreenTests` | writable `/dev/uinput`, readable event nodes, `Xorg` with the `dummy` video and `evdev` input drivers (`CNA_X11_XORG_MODULE_PATH` adds module directories) | real contacts: a uinput touchscreen and pen that a private, rootless Xorg takes **exclusively** — the suite checks that grab before every event it writes, so nothing reaches the desktop's own compositor; opt-in through the entry's `CNA_X11_TEST_TOUCHSCREEN=1` |
| `CnaX11ExclusiveFullscreenTests` | `Xvfb` + `openbox`; **the launcher's own server only** | exclusive fullscreen changes the display mode, so it runs only where `CNA_X11_PRIVATE_TEST_SERVER` is set: mode choice, the screen around the CRTC, SetSize while exclusive, hide/show, focus loss and return, every restore path, a mode someone else set, and a process killed with `SIGKILL` having its mode restored by its guardian |

Where the build has a renderer that draws into a window, `X11_House3D_ExclusiveFullscreen_<renderer>`
runs a real game with `IsFullScreen` on the launcher's server
(`tools/platform/x11_exclusive_fullscreen_game.sh`, needing `openbox` and `xrandr`): the monitor
switches and the window covers it, a normal exit restores the mode, and so does a `SIGKILL`.

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
a real compositor's fullscreen behaviour, multi-monitor XRandR layouts and hotplug, a real
monitor's mode change (Xvfb has one CRTC and switches instantly), and real input devices. A real input-method server *is* covered — the launcher runs a private ibus — but only with
its core engine; a language engine (Hangul, Pinyin, Anthy) in a user's session has not been driven. `plans/plan_x11.md` records those gaps
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
