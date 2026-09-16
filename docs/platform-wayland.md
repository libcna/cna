# The native Wayland platform backend (`CNA_PLATFORM=WAYLAND`)

`CNA_PLATFORM=WAYLAND` is a platform backend written directly against the Wayland protocols: a
genuine Wayland client that speaks to the compositor itself through `libwayland-client`,
`xdg-shell`, `xkbcommon`, EGL, `VK_KHR_wayland_surface` and `wl_shm`. It uses no SDL and no X11
library for anything -- not SDL's Wayland driver, not Xlib, not Xwayland.

SDL3 remains CNA's default platform, and X11 remains a native backend of its own
([`docs/platform-x11.md`](platform-x11.md)). Nothing here changes either. The Wayland backend is
a third way to reach a Linux desktop, and the one that runs on a Wayland session natively.

The plan, the design decisions (D-1 … D-28) and the task ledger with its recorded evidence are
[`plans/plan_wayland.md`](../plans/plan_wayland.md). This document is the capability boundary and
the operating manual.

---

## Selecting it

```sh
cmake -S . -B cmake-build-wayland -G Ninja \
      -DCNA_PLATFORM=WAYLAND \
      -DCNA_GRAPHICS_RENDERER=HEADLESS
```

and the SDL-free configuration, with sound and three real renderers selectable at run time:

```sh
cmake -S . -B cmake-build-wayland -G Ninja \
      -DCNA_PLATFORM=WAYLAND \
      -DCNA_ENABLE_SDL=OFF \
      -DCNA_AUDIO_PLATFORM=ALSA \
      -DCNA_GRAPHICS_RENDERER=HEADLESS \
      "-DCNA_GRAPHICS_RENDERERS=HEADLESS;OPENGL33;VULKAN;SOFTWARE"
```

`CNA_GRAPHICS_RENDERER=SOFTWARE` presents through the backend's `wl_shm` presenter, `OPENGL33`
through its EGL contexts, `VULKAN` through its `VkSurfaceKHR`. With the multi-renderer build the
choice is made at run time (`CNA_GRAPHICS_RENDERER=OPENGL33 ./game`), see
[`docs/runtime-renderer-selection.md`](runtime-renderer-selection.md).

`WAYLAND` is offered only where the Wayland development environment exists. Asking for it without
that environment is a configure error naming the package to install -- never a fall back to SDL3
or to X11 through Xwayland, because either would build something other than what was asked for.
The default platform is unchanged.

---

## Dependencies

Found through `pkg-config`; no distribution path is written anywhere
([`cmake/PlatformWayland.cmake`](../cmake/PlatformWayland.cmake)).

| Piece | Required? | What turns off without it |
|---|---|---|
| `wayland-client` ≥ 1.18 | **mandatory**, linked | the backend is not offered |
| `xkbcommon` ≥ 0.5 | **mandatory**, linked | the backend is not offered |
| `wayland-scanner` | **mandatory**, build time | the backend is not offered |
| `wayland-protocols` with `stable/xdg-shell` | **mandatory**, build time | the backend is not offered |
| each optional protocol XML (`xdg-output`, `viewporter`, `fractional-scale-v1`, `relative-pointer`, `pointer-constraints`, `text-input-v3`, `primary-selection`, `xdg-decoration`, `xdg-activation`, `idle-inhibit`, `xdg-foreign-v2`, `tablet-v2`, `cursor-shape-v1`) | optional, build time | exactly that protocol's capability (`CNA_WAYLAND_HAVE_<PROTOCOL>`) |
| EGL + `wayland-egl` **headers** | optional; `libEGL.so.1` and `libwayland-egl.so.1` are loaded at run time | `openGlContext` |
| `wayland-cursor` **headers** | optional; `libwayland-cursor.so.0` loaded at run time | the cursor-theme fallback (cursor-shape still works) |
| Vulkan **headers** | optional; the loader is the caller's | `vulkanSurface` |
| D-Bus **headers** | optional; `libdbus-1.so.3` loaded at run time | the desktop portal (file dialogs, `OpenUrl`) and the session-bus screen-saver fallback |
| `linux/input.h` | optional | `gamepad`, `joystick`, `gamepadRumble`, `gamepadSensors`, `haptics` (the evdev services the X11 backend has, shared) |
| `wayland-server` | test builds only | the in-process test compositor; never linked into `cna_platform` |

On Debian/Ubuntu:

```sh
sudo apt-get install -y libwayland-dev wayland-protocols libxkbcommon-dev \
                        libegl-dev libvulkan-dev libdbus-1-dev
```

The protocol bindings are generated into the build tree by `wayland-scanner` (client header,
server header, `private-code`); nothing generated is committed, and the interface tables have
hidden visibility so a host that links its own copy (a toolkit, libdecor) cannot collide with
CNA's (D-2).

---

## Capability boundary

The set is computed once, after a bounded startup exchange with the compositor, and is fixed for
the platform instance's lifetime. A process with no compositor still constructs a platform (its
file system and host services work); every display capability is then false and
`AcquireSubsystem(Video)` reports the connection error (D-3). Every service is non-null exactly
when its capability is true.

| Capability | Wayland | Backed by |
|---|---|---|
| `multipleWindows` | ✅ | any number of `xdg_toplevel`s |
| `nativeWindowHandle` | ✅ | `NativeWindowSystem::Wayland`, `wl_display*` + `wl_surface*` |
| `highDpi` | ✅ | fractional scale through `wp_fractional_scale_v1` + `wp_viewporter`, integer scale otherwise (see [Scaling](#scaling)) |
| `multipleDisplays` | ✅ | `wl_output` v4 + `zxdg_output_v1`, hot-plugged |
| `borderlessFullscreen` | ✅ | `xdg_toplevel.set_fullscreen` on the window's output |
| `surfacePresentation` | ✅ | `wl_shm` XRGB8888 (the compositor must offer it) |
| `textInput` | ✅ | committed text through xkbcommon and the locale's compose table |
| `exactKeyboardState` | ✅ | real releases; focus loss releases held keys |
| `pixelAccurateMouse` | ✅ | surface coordinates in `wl_fixed_t` |
| `cursorShapes` | conditional | `wp_cursor_shape_manager_v1`, else a loadable cursor theme; custom images always |
| `relativeMouse` | conditional | `zwp_relative_pointer_manager_v1` **and** `zwp_pointer_constraints_v1` |
| `ime` | conditional | `zwp_text_input_manager_v3` (GNOME and KDE offer it) |
| `clipboard`, `clipboardData`, `dragAndDrop` | conditional | a seat and `wl_data_device_manager` |
| `primarySelection` | conditional | `zwp_primary_selection_device_manager_v1` |
| `openGlContext` | conditional | libEGL and libwayland-egl load and an EGL display for the connection initialises |
| `vulkanSurface` | conditional | the build had the Vulkan headers |
| `nativeFileDialog` | conditional | the session bus has the desktop portal |
| `inputDeviceEnumeration` | ✅ | the seats' keyboards, pointers, touchscreens and tablet tools, and the evdev controllers |
| `gamepad`, `joystick`, `gamepadRumble`, `gamepadSensors`, `haptics`, `powerInfo` | Linux | the shared evdev and sysfs services, exactly as under X11 |
| `globalPointer` | ❌ by design | Wayland gives an ordinary client neither the pointer's position outside its surfaces nor the power to move it (D-19): `SetCapture`, `TryGetGlobalPosition` and `SetGlobalPosition` throw `PlatformNotSupportedException(GlobalPointer)` |
| `messageBox` | ❌ | see [Message boxes](#message-boxes) |
| `tray` | ❌ | no Wayland protocol; StatusNotifierItem is a desktop's own |
| `camera`, `sensors`, `managedEntrypoint` | ❌ | not window-system facilities |

No privileged protocol is used: nothing from `wlr-*`, no layer shell, no virtual keyboard or
pointer, no screen capture. A Wayland client that needed one of those would be a desktop
component, not a game.

---

## Windows

A window has an explicit configure state machine: no role → awaiting the first configure →
configured (D-7). `CreateWindow` with `visible = true` returns only after the compositor has sent
the first configure and it was acknowledged, so a renderer's first frame can never be a buffer
committed too early (`xdg_surface.unconfigured_buffer`). The wait is bounded (5 s); a compositor
that does not answer makes `CreateWindow` throw with the reason.

| Operation | What happens |
|---|---|
| `Show` / `Hide` | `Hide` destroys the role (`xdg_toplevel` + `xdg_surface`) and commits a null buffer; `Show` creates it again. The `wl_surface` -- the native handle, the EGL window, a `VkSurfaceKHR` -- lives as long as the window (D-8) |
| `SetSize` | applies at once to a floating window (Wayland clients size themselves); remembered while the compositor constrains the window and applied when it floats again (D-9). A configure the compositor sent before it saw the resize is recognised and does not undo it |
| `SetResizable(false)` | the compositor is told min = max = the size; a floating configure that suggests another is ignored |
| `SetBorderless` | removes CNA's title bar (or asks the compositor for no decoration); on a maximized or fullscreen window the geometry stays exactly what the compositor configured and the content takes the difference |
| `Maximize` / `Restore` | `set_maximized` / `unset_maximized`; `Maximized` and `Restored` are posted when the compositor confirms, never when asked |
| `Minimize` | `set_minimized`. `IsMinimized()` is the `suspended` state (xdg_wm_base v6) -- the only thing a compositor says (D-12). A minimized window cannot un-minimize itself; the user does that |
| `SetFullscreenMode` | `set_fullscreen` on the window's current output; exclusive is asked for the same way and reported as `BorderlessFullscreen` -- an ordinary Wayland client cannot own a display mode (D-11) |
| position | the compositor's. `WindowDescription::x/y/centered` are ignored and no `Moved` event exists (D-10) |
| close | `xdg_toplevel.close` posts `CloseRequested`; the last window's close also posts `QuitEvent`. Nothing is destroyed until the application destroys it |
| `Sync` | a bounded roundtrip, plus a short bounded wait for the configure a state request is answered with |
| focus | keyboard focus (`wl_keyboard.enter`), or, on a seat with no keyboard, the compositor's `activated` state. `FocusGained`/`FocusLost` follow `HasFocus()` exactly |
| activation | a window being shown asks for focus through `xdg-activation`: with the token the launcher left in `XDG_ACTIVATION_TOKEN` (spent once, then removed from the environment), else with the serial of the latest input the client received. The compositor decides; GNOME does not focus a window from a client the user has not interacted with |

### The title bar CNA draws

GNOME's compositor offers no `zxdg_decoration_manager_v1`: a window it shows has no title bar and
cannot be moved or closed with the mouse unless the client draws one. Where the compositor
decorates (KDE, most wlroots compositors), CNA asks it to. Where it does not, the backend draws a
minimal frame of its own (D-22): a 32-unit bar above the content with close, maximize and minimize
buttons (each only when the compositor's `wm_capabilities` offer the action), drag to move,
double-click to maximize, right-click for the compositor's window menu, and an invisible 8-unit
border that resizes, with the matching cursors. It is built from `wl_subsurface`s on `wl_shm`,
inside the window geometry, hidden while fullscreen or borderless. No libdecor, no toolkit. The
bar draws no title text -- the platform module has no text renderer; the compositor shows the
title in its overview and task switcher. A click on the frame is never delivered to the game.

---

## Scaling

Logical units are surface coordinates. A window that opted into `highDpi` renders at the
compositor's preferred scale (D-13):

* a fractional scale (`wp_fractional_scale_v1`, e.g. 1.25 on a 125 % desktop) renders a buffer of
  `round(logical × scale)` pixels shown at the logical size through `wp_viewporter`;
* an integer scale uses the viewport too wherever there is one -- `set_buffer_scale(n)` makes an odd
  buffer size a protocol error, and an EGL buffer one frame behind a scale change is exactly that;
  `set_buffer_scale` is the fallback for a compositor without a viewporter;
* without `highDpi` the window renders at scale 1 and the compositor scales it.

`GetDisplayScale() == pixel size / logical size`, as the SDL3 backend defines it, so
`GetPixelSize() == round(GetClientBounds() × GetDisplayScale())` always. A scale change posts
`DisplayScaleChanged` and `PixelSizeChanged` (never `Resized`); moving to another output posts
`DisplayChanged`. `IPlatformDisplays` reports each output's logical geometry (from xdg-output) and
its content scale -- 1.25 on a 125 % output, where `wl_output.scale` says 2.

---

## Keyboard

The compositor sends a keymap; the backend runs xkbcommon on it (D-14):

* `Scancode` is the physical key, from the keymap's key names -- the same table the X11 backend
  uses (`src/Xkb/`), so a key has the same scancode under both;
* `KeyCode` is the active layout's key, with the X11 backend's rules (a number row that types
  `ě š č` on a Czech keyboard still reports `D2`…`D0` for games; `Y`/`Z` follow the QWERTZ labels);
* modifiers from the xkb state; AltGr is `Mode` (xkbcommon before 1.8 never reports a virtual
  modifier active, so the real Mod5 that `LevelThree` maps to is read);
* key repeat is generated by the backend from `repeat_info` (Wayland sends no repeats); a rate
  of 0 disables it;
* keys already held when focus arrives are state, not presses; focus loss releases every held
  key without events (as X11 does on `FocusOut`);
* a new keymap (a layout switched in the desktop's settings) takes effect for the next key.

## Text input

Committed text is `xkb_state_key_get_utf8` through `xkb_compose` with the environment's locale
(dead keys, `Multi_key` sequences), posted as `TextInputEvent` only while text input is started
for the focused window (D-15). Where the compositor offers `text-input-v3` (`ime` true), an input
method composes: preedit as `TextEditingEvent` (cursor in characters), commit as
`TextInputEvent`, the content type from `TextInputType`, and the caret rectangle from
`SetInputArea` so the candidate window appears beside the text. Candidate lists are the input
method's own window and are not delivered (as under XIM); `delete_surrounding_text` has nothing
to act on, since the platform does not hold the application's text.

## Mouse

Buttons 1–5 (left, middle, right, X1, X2), motion in fractional surface coordinates, double
clicks (500 ms, 4 units), enter/leave per window. The wheel is counted in 120ths of a notch
(D-17): `axis_value120` where the seat has it (high-resolution wheels give fractions),
`axis_discrete` on older seats, else the continuous value at libinput's 10 units per notch -- so a
touchpad's smooth scroll adds up instead of vanishing. `MouseWheelEvent` carries notches; the
snapshot accumulates XNA units, 120 per notch.

**Relative mode** (D-18) locks the pointer with a persistent `zwp_locked_pointer_v1` on the window
and reads `zwp_relative_pointer_v1`'s unaccelerated motion, fractions carried between reads. The
lock is the compositor's to activate and the backend's to destroy: on disable, on the window's
destruction and when the pointer goes away, always before its surface, so the desktop can never be
left trapped. While locked the cursor is hidden and `SetPosition` becomes the position the
compositor leaves the pointer at on unlock.

**Cursors** (D-20): `wp_cursor_shape_device_v1` for the system shapes, so the compositor draws its
own theme at the right scale; the `wl_cursor` theme (`XCURSOR_THEME`, `XCURSOR_SIZE`) where the
protocol is absent; custom images on a `wl_shm` cursor surface.

## Touch

`wl_touch` down, motion, up and cancel, one finger id per seat and contact, coordinates normalised
to the window.

## Graphics tablets

`zwp_tablet_manager_v2` (WAYLAND-0059), and the rule is the X11 backend's: **a pen whose tip is
down is a touch with the pressure it reports, and a hovering pen is nothing.** A game that reads
`TouchPanel` sees a pen under both backends and cannot tell which device drew the stroke.

- One contact per tool, with the seat's global name in its id, so two seats' tablets never collide
  with each other or with a touchscreen's fingers. An eraser touching is a touch like the tip.
- Frames are respected: position, pressure and tip state are applied together when
  `zwp_tablet_tool_v2.frame` arrives, never one event at a time. A frame that only changes the
  pressure is a `Motion` with a zero delta, because a drawing program reads the pressure of a
  stroke from the contact it already has.
- A tool that leaves proximity, a tool that is unplugged, a tablet that is unplugged and a window
  that is destroyed each end a stroke in progress as `Cancelled` -- no contact is ever left down.
- A tool that reports no pressure capability draws at pressure 1, as a finger does.
- Each tool is listed by `GetDevices(InputDeviceKind::Touch)` under its protocol type name (`pen`,
  `eraser`, `airbrush`, ...), because a touch is what its strokes arrive as.

**Not delivered, deliberately:** tilt, rotation, the slider, the wheel, the barrel buttons and the
pad with its rings and strips. The contract carries none of them, and a private event for them
would be an API no other backend has; the pad object is destroyed as soon as it is announced so the
compositor stops sending its events. A tablet's own cursor (`zwp_tablet_tool_v2.set_cursor`) is
left to the compositor.

---

## Clipboard, primary selection, drag and drop

The clipboard is `wl_data_device` (D-21). Transfers go through pipes and never block the game
loop: a read is a bounded wait that keeps dispatching the connection (2 s without progress, 256 MiB
at most); what another program pastes from CNA is written incrementally from `PollEvents`. CNA's
own selection is answered from memory without a round trip through the compositor. UTF-8 text is
offered under `text/plain;charset=utf-8`, `text/plain`, `UTF8_STRING`, `TEXT` and `STRING`, so
Xwayland programs paste it too; other formats go by MIME type (`GetData`/`SetData`). A paste target
that hangs up mid-transfer ends that transfer with `EPIPE`, never the game with `SIGPIPE`.

A Wayland compositor gives the selection only to the client with keyboard focus: `SetText` takes
effect when a window of the game is focused, and `GetText` reads what the focused client can see.

The primary selection (middle-click paste) is `zwp_primary_selection_v1`, separate from the
clipboard. Drag and drop is the target side: files (`text/uri-list`, turned into local paths) and
text, copy only -- a game never takes a dragged file away from its owner. The payload is read after
the drop, outside the protocol listener.

---

## Graphics bridges

**OpenGL** (D-23): EGL on the Wayland platform (`eglGetPlatformDisplay`), one `EGLDisplay` per
platform, a `wl_egl_window` per GL window resized before the next frame is drawn. Core, compatibility
and ES contexts (4.6 core, 3.3 core, 2.1 compatibility, ES 3.0 and ES 2.0 on radeonsi). Vsync is
the backend's: EGL's own interval is 0 and `SwapBuffers` at interval 1 waits for the previous
frame's callback on a private queue, at most 100 ms -- a hidden, minimized or locked-away window
keeps its game loop running at ~10 fps instead of hanging inside `eglSwapBuffers`. The pending
callback is kept across timeouts, so the compositor holds one, not one per frame.

**Vulkan** (D-24): `VK_KHR_surface` + `VK_KHR_wayland_surface`, `vkCreateWaylandSurfaceKHR`
resolved through the caller's `vkGetInstanceProcAddr`; the platform links no loader. The
swapchain's extent is the window's pixel size (`currentExtent` is `0xFFFFFFFF` on Wayland).

**Software** (D-25): the `wl_shm` presenter keeps up to three XRGB8888 buffers in sealed `memfd`s,
reuses a buffer only after `wl_buffer.release`, fills letterbox bars with black, damages with
`damage_buffer`, and paces vsync by frame callbacks with the same 100 ms bound.

---

## Desktop integration

* **Screen saver**: `SetScreenSaverEnabled(false)` puts a `zwp_idle_inhibitor_v1` on every window,
  including windows made later; without the protocol, the session bus's `org.freedesktop.ScreenSaver`
  (D-26, shared with X11).
* **File dialogs and URLs**: the desktop portal (`org.freedesktop.portal.FileChooser`, `OpenURI`)
  over the session bus, parented to the game's window through `xdg-foreign` (`wayland:<handle>`),
  shared with X11 (`src/Freedesktop/`).
* **Gamepads, joysticks, haptics, power, host facts**: the Linux services the X11 backend has,
  compiled for both (D-27) -- see [`docs/platform-x11.md`](platform-x11.md#gamepads-and-joysticks).
* **Audio** is not the platform's: `CNA_AUDIO_PLATFORM=ALSA` works the same under Wayland and X11.

## Message boxes

`messageBox` is **false**, and `ShowMessageBox` refuses (WAYLAND-0094). Wayland has no dialog
protocol; a message box would have to be a window the backend draws, text included. The X11
backend can draw its own because the X server supplies fonts; a Wayland client has none, and the
platform module carries no text renderer. The two ways to get one -- embedding a third-party font,
whose licence would then travel inside every CNA binary, or launching `zenity`/`kdialog`, which
would not be this backend at all -- were both declined. A game that needs a dialog can draw it with
its own `SpriteFont`; file dialogs are unaffected (the portal draws them).

---

## Errors and what the application sees

A protocol error, or a compositor that goes away, ends the connection for good -- libwayland
refuses every later request. The backend records the first error with the interface, object and
code the compositor named, prints it once to stderr (`[CNA][Wayland] …`), and posts one
`QuitEvent`. Every later call is safe: rendering does nothing, `CreateWindow` throws with the
recorded error, destroying windows and the platform is clean.

`PollEvents` never blocks (D-5): it dispatches what is queued, reads the socket only if it is
readable, and flushes what the socket takes, leaving the rest for later. Blocking exists only where
the contract asks for it (`CreateWindow`, `Show`, `Sync`, clipboard reads), and every such wait is
bounded; `wl_display_roundtrip()`, which waits forever for a hung compositor, is never called.

---

## Running the tests

| ctest | Needs | What it covers |
|---|---|---|
| `CnaWaylandMappingTests` | nothing | pure decisions (versions, scaling, configure sizes, title-bar hit testing, buttons, wheel, XKB modifiers, text-input offsets, MIME aliases, SIGPIPE-free writes, sealed shm), the shared Xkb/Freedesktop/Linux suites, the SDL/X11 source scan |
| `CnaWaylandProtocolTests` | nothing | 70+ cases against an in-process libwayland-server compositor that checks every protocol rule (see below) |
| `CnaWaylandWestonTests`, `…GpuTests`, `…ScaledTests` | Weston | the live suite and the conformance suite on headless Weston: software, GL renderer (EGL and Vulkan on the real GPU), output scale 2 |
| `CnaWaylandMutterTests`, `…CzechTests` | gnome-shell | GNOME's own compositor, headless on a private bus, with real input from its RemoteDesktop API: focus, keys, Czech layout with AltGr, pointer, relative mode, CNA's title bar, fullscreen, the clipboard with wl-clipboard as an independent client |
| `CnaWaylandLinkClosure` | readelf | no executable NEEDs SDL, X11, xcb or GLX; the platform-only binary reaches none even transitively |

Every live suite runs through `tools/platform/wayland_test_server.sh`, which starts a compositor in
a private runtime directory, runs the command against it and takes everything it started down again
(a missing compositor exits 77, ctest's skip). **No test ever reaches the desktop it runs on**:
`WaylandTestEnvironment.cpp` points `WAYLAND_DISPLAY` and the session bus at nothing before any
test runs, and only a launcher's private compositor is ever named.

The in-process compositor (`WaylandTestCompositor`) runs on its own thread inside the test binary,
reached through a socketpair in `WAYLAND_SOCKET`. It implements every request of every interface the
backend uses and posts the protocol error a strict compositor would -- `unconfigured_buffer`,
`invalid_serial`, `invalid_size`, Mutter's constrained-geometry `invalid_surface_state`, viewport and
constraint errors, implicit-grab serials for move/resize/menu -- and every protocol test fails on any
error or serial misuse in its traffic.

### The validation harness

`cna_wayland_desktop_validation <scenario>` drives the backend against whatever `WAYLAND_DISPLAY`
names -- normally the real desktop -- and prints one `PASS`/`FAIL`/`SKIP` line per check. It never
injects input.

| Scenario | What it checks |
|---|---|
| `info` | globals and versions, outputs (mode, scale, logical geometry, content scale), seats, capabilities and their services |
| `lifecycle [--windows N]` | configure, resize, maximize/restore, fullscreen (reported borderless), borderless, hide/show, minimize, several windows |
| `scale` | a high-DPI window's pixel size at the output's (fractional) scale; a plain window unscaled |
| `presenter [--frames N]` | vsync pacing, unpaced throughput, scale modes, resize |
| `gl [--frames N] [--allow-software]` | hardware context, every version/profile, readback, swap intervals, resize, a high-DPI drawable |
| `vulkan [--frames N] [--validation] [--device NAME]` | swapchain, present, resize and recreate, read-back; with `--validation`, `VK_LAYER_KHRONOS_validation` must report nothing |
| `lifetime [--iterations N]`, `stress [--ops N]`, `soak [--seconds N]` | repeated shm/GL/Vulkan lifetimes, thousands of random window operations, a long rendered session -- with open descriptors and resident memory sampled |
| `clipboard --replace-my-clipboard` | round trips through the user's real clipboard (asked for explicitly) |
| `interactive [--seconds N]` | for a person: type, click, scroll, lock the pointer; the harness reports what arrived |

### Sanitizers

The Wayland suites and the harness run under AddressSanitizer, UndefinedBehaviorSanitizer and
LeakSanitizer (`CNA_SANITIZE=address,undefined`, `CNA_PLATFORM_CTEST_BINARY=CnaPlatformModuleTests`).
Vulkan tests under a sanitizer select lavapipe (`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`):
the RADV ICD leaks 2 × 128 bytes from a `pthread_once` initialiser in `vkCreateInstance`, a driver
finding already recorded for X11 (`tools/platform/lsan_x11_mesa.supp`), reproducible without CNA.

---

## Proving SDL and X11 are gone

```sh
readelf -d cmake-build-wayland/cna_wayland_desktop_validation | grep NEEDED
ldd cmake-build-wayland/cna_wayland_desktop_validation
```

The platform-only executable's entire closure is `libwayland-client`, `libxkbcommon`, `libffi` and
the C/C++ runtime. A full game additionally NEEDs what its engine modules use (FFmpeg for video,
`libvulkan` for the Vulkan renderer); Debian's FFmpeg itself links libX11 (through VA-API, VDPAU
and cairo), which a build with `-DCNA_ENABLE_VIDEO=OFF` does not load. `CnaWaylandLinkClosure`
checks all of this on every run, and `WaylandIsSdlFreeTests` checks the sources.
