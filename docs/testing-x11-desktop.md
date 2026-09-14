# Testing the X11 backend on a real desktop

The X11 backend's ctest suites (`CnaX11MappingTests`, `CnaX11IntegrationTests`,
`CnaX11WindowManagerTests`) run on a private `Xvfb` that `tools/platform/x11_test_server.sh`
starts and discards. That is what CI can do, and it is not the same as a real desktop: an Xvfb
has no GPU, no compositor, no focus-stealing prevention, no real input devices and no other
applications. Most of the defects found in `plans/plan_native_platform_validation.md` only
appeared on a real session.

`cna_x11_desktop_validation` is the tool for the real session. It is a standalone program (not a
GoogleTest suite) that drives the backend through the public platform contract and checks the
results over a second, independent X connection — the way a window manager, a pager or another
application would see them. It prints one `PASS`/`FAIL`/`SKIP`/`INFO` line per check and a
`SUMMARY` line, and exits 0 only when nothing failed.

```bash
cmake --build <x11 build dir> --target cna_x11_desktop_validation
DISPLAY=:0 <x11 build dir>/cna_x11_desktop_validation <scenario> [options]
```

It is built whenever `CNA_PLATFORM=X11` and `CNA_BUILD_TESTS=ON`. XTest (`libXtst`) and
X-Resource (`libXRes`) are used when their development files are present; Vulkan headers enable
the Vulkan scenarios (the loader itself is `dlopen`ed, as in the backend).

## Say what you ran it on

Every result must name the server it came from, because they are not interchangeable:

| Server | How to tell | What it proves |
|---|---|---|
| Native Xorg | `info` reports "not Xwayland"; an `Xorg` process owns the seat | the real X11 case |
| Xwayland (GNOME, KDE, ...) | `info` reports "Xwayland (extension XWAYLAND present)" | X11 applications on a Wayland desktop — **not** native Xorg |
| Xvfb | the display was started by `x11_test_server.sh` or by hand | protocol behaviour only; software GL; no real input |

Likewise for rendering: `glx` fails `glx.hardware-renderer` on llvmpipe unless
`--allow-software` is given, and `vulkan`/`lifetime` print the physical device and its type
(`integrated GPU`, `discrete GPU`, `CPU/software`). A software result is labelled as one.

## What the scenarios do to the desktop

Several scenarios act on the session they run on. Run them on a session you are prepared to have
used that way, save your work first, and read this table.

| Scenario | Options | What it does | Needs |
|---|---|---|---|
| `info` | | server kind, capabilities | nothing |
| `lifecycle` | `--rounds N` (3), `--rapid N` (300) | 1/2/4 windows: create, show, hide, move, resize, minimise, maximise, focus, close, destroy; then N rapid create/destroy; X-Resource leak check | focus (an unlocked session) |
| `stress` | `--ops N` (10000), `--fullscreen-every N` (250), `--burst` | N random window operations with RSS/fd/X-resource monitoring | focus |
| `wm` | `--toggles N` (100) | fullscreen round trips, maximise, minimise/restore, focus loss and return, move, resize through the running window manager | a window manager; focus |
| `displays` | | XRandR monitors vs CNA's display list, window-to-display mapping, DPI policy | nothing (multi-monitor checks skip on one monitor) |
| `glx` | `--frames N` (3000), `--allow-software` | GL 3.3 core context, shaders, rendered frames read back and verified, resize, swap interval, two windows/contexts | a GPU, or `--allow-software` |
| `vulkan` | `--frames N` (2000), `--device NAME`, `--validation` | `VK_KHR_xlib_surface` swapchain, presented frames read back, resize/recreate; validation layer errors fail | a Vulkan driver that can present to this server |
| `presenter` | `--rapid N` (60) | software presenter pixels at native size, stretched, after resize; MIT-SHM vs XPutImage; cost per frame | nothing |
| `lifetime` | `--iterations N` (300), `--device NAME`, `--validation` | N GL windows+contexts, N/3 Vulkan windows+swapchains, N presenter windows; RSS, fds and X resources per type | a GPU for GL/Vulkan |
| `clipboard` | `--no-wayland` | ASCII/Czech/emoji/600 KiB/4 MiB text both ways against `xclip`, `xsel` and (when usable) `wl-copy`/`wl-paste`; ownership loss; owner exit | **overwrites the clipboard** |
| `keyboard` | `--no-layout-switch` | a uinput virtual keyboard: keys, modifiers, auto-repeat, focus loss, and (with a second layout) Super+Space layout switching and back | **injects keys**; focus |
| `text` | | committed UTF-8 text, including Czech through the second layout | **injects keys**; focus |
| `mouse` | | a uinput virtual mouse: buttons, wheel, double click, enter/leave, capture | **moves the real cursor and clicks**; focus |
| `relative` | `--cycles N` (200) | XInput2 raw relative mode: grab, focus loss/return, fractional deltas | **moves the real cursor**; focus |
| `soak` | `--seconds N` (600) | a long hardware-rendered session with input and state changes | **injects input**; focus |
| `rawprobe` | `--xi-minor N`, `--no-grab`, `--verbose` | diagnostic: prints every `XI_RawMotion` a relative-mode client receives | pointer over the window |
| `interactive` | `--step N` | guided session for a **person** pressing real keys and moving the real mouse | a person |

### Safety rules the harness enforces

- **uinput reaches the real seat, whatever `DISPLAY` says.** On an Xvfb the virtual keyboard
  would type into whatever the user has focused on the real desktop. Input scenarios therefore
  use uinput only when the target server is Xwayland, or when `--native-x-input` states that the
  seat belongs to this native Xorg server; otherwise they skip.
- **Every keyboard-driven scenario first proves its keys arrive in its own window** by tapping
  Right Ctrl (which does nothing on its own almost anywhere) and requiring the CNA window to
  report it. If that fails — another window has focus, the session is locked — nothing else is
  typed.
- **Never run the input scenarios on a locked session.** GNOME's lock screen takes focus and
  keyboard input; while it is up no application window can gain focus (so every focus-dependent
  check fails, which is the lock, not CNA), and keys would go to the password field. `info` does
  not detect a lock; check first, for example
  `gdbus call --session --dest org.gnome.ScreenSaver --object-path /org/gnome/ScreenSaver --method org.gnome.ScreenSaver.GetActive`.
  Rendering, presenter, lifetime and clipboard scenarios are safe on a locked session, but
  presentation to a hidden window is throttled by the compositor (a few frames per second on
  GNOME), so their timings mean nothing then.
- **Layouts are switched with the desktop's own shortcut (Super+Space) and switched back**;
  `keyboard.layout-restored` fails if the group at the end differs from the one at the start. The
  keymap itself is never changed on a desktop. (The ctest `KeyCodesFollowALayoutSwitch` does
  change the keymap, and therefore runs only on the private server `x11_test_server.sh` marks
  with `CNA_X11_PRIVATE_TEST_SERVER`.)
- **Every external clipboard tool runs under a deadline**, and `wl-copy` is probed first:
  wl-clipboard needs a data-control protocol the compositor may not offer (GNOME's mutter does
  not), and without one it waits forever. Then the Wayland peer is reported as unavailable.
- The window-manager requests the harness sends (`_NET_ACTIVE_WINDOW`, `_NET_CLOSE_WINDOW`) carry
  a real server timestamp; mutter refuses `CurrentTime` when it pings the window.

## Evidence the harness gives that the ctests cannot

- **X-Resource counts per resource type** for CNA's own client (`lifecycle`, `stress`,
  `presenter`, `lifetime`), so a leak shows up as the type that keeps growing — and a driver's
  leak can be told from CNA's. Mesa's X11 Vulkan WSI (seen with 25.0.7) leaves a GC per
  swapchain on the connection it is given, with RADV and with lavapipe, and lavapipe on a server
  with MIT-SHM (Xwayland) also leaves the three `ShmSeg` attachments of its images. A program with
  no CNA in it leaks exactly the same, so `lifetime` reports those two types for its Vulkan loop
  and does not count them against CNA.
- **Pixels read back from the X server** (`XGetImage`) for GLX, Vulkan and the presenter — what
  the server holds, not what the application believes it drew.
- **Cost per frame** for the presenter: the number that NPV-0116 was found by.

## Known environment effects (not CNA defects)

| Effect | Where | Consequence |
|---|---|---|
| Xwayland delivers pointer input only while the compositor's cursor is over an X surface | GNOME/KDE | XTest moves only Xwayland's own sprite; the harness steers the real cursor with a uinput mouse |
| mutter keeps minimised windows mapped | GNOME | de-iconify needs `_NET_ACTIVE_WINDOW` (NPV-0113) |
| focus-stealing prevention | mutter | a newly mapped window need not get focus; tests ask for activation as a pager does |
| lock screen | GNOME | no application window can gain focus; see the safety rules |
| hidden-window throttling | mutter/Xwayland | presentation to a covered window runs at a few frames per second |
| no DRI3 on Xvfb | Xvfb | RADV cannot present there; use `--device llvmpipe` |
| wl-clipboard cannot reach mutter's clipboard | GNOME | the native-Wayland clipboard peer is unavailable |

## A full run

On an unlocked session, with work saved:

```bash
V=<x11 build dir>/cna_x11_desktop_validation
for s in info lifecycle displays "wm --toggles 100" "glx --frames 3000" \
         "vulkan --frames 2000 --validation" presenter "lifetime --iterations 300" \
         "stress --ops 10000" clipboard keyboard text mouse relative "soak --seconds 600"; do
  $V $s 2>&1 | tee "x11-desktop-${s%% *}.log"
done
```

and on a private Xvfb (software, labelled):

```bash
sh tools/platform/x11_test_server.sh $V vulkan --device llvmpipe --validation
```
