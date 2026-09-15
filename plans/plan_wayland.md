# CNA native Wayland platform (`CNA_PLATFORM=WAYLAND`) — implementation plan and ledger

> **Purpose.** Give CNA a genuine, first-class, native Wayland platform backend: a direct
> Wayland client that talks `wl_*`, `xdg-shell`, `xkbcommon`, EGL, `VK_KHR_wayland_surface` and
> `wl_shm` itself -- with no SDL and no X11 library anywhere in it -- standing beside Win32 and X11
> on the same `CNA::Platform` contract.
>
> SDL3 stays CNA's default platform. X11 stays exactly as it was merged into `next`; the only X11
> source this work touches is code that turned out to be generic (Linux, XKB, POSIX) and moved to a
> shared directory, with X11's own suites re-run to prove nothing changed.
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
| Branch | `wayland`, created from `next` |
| Baseline commit | `7c36fa3218d51141c269b16a757813966addfcc0` — `merge(X11): integrate the native X11 backend's Phase M and N into next` (`origin/next` at the start, fetched 2026-09-15) |
| Working tree at start | one pre-existing user change, left alone: `D tests/assets/media/video/video_xnb_object_fixture.xnb` |
| Host | ThinkPad T14, Debian 13.6 (trixie), kernel 6.12.107+deb13-amd64, 16 logical cores, 30 GiB RAM |
| Toolchain | GCC 14.2.0, Clang 19.1.7, CMake 3.31.6, Ninja 1.12.1, ccache (shared `/rv/cnaccache`) |
| Desktop | GNOME Shell 48.7 (Mutter 48.7), `XDG_SESSION_TYPE=wayland`, socket `$XDG_RUNTIME_DIR/wayland-0`, Xwayland `:0` running rootless beside it. `WAYLAND_DISPLAY` is **unset** in the agent's environment and `DISPLAY=:99` is a private Xvfb -- so every run against the real desktop names `WAYLAND_DISPLAY=wayland-0` explicitly. |
| Session state at start | `loginctl`: `Type=wayland`, `Active=yes`, `LockedHint=no` |
| Monitors | two Dell S2725DC, 2560×1440 @ 143.973 Hz, outputs `DP-2` (logical x=0) and `DP-1` (logical x=2048) -- **fractional scaling at 125 %** (`scale-monitor-framebuffer` enabled; `wl_output.scale` reports 2, the logical width 2048 is 2560/1.25) |
| Keyboard layouts | `us`, `cz+qwerty` (GNOME input sources) |
| Touch | none (`wl_seat` capabilities: pointer, keyboard) |
| GPU | AMD Radeon 780M (Phoenix1, `1002:15bf`), Mesa 25.0.7 |
| EGL on the Wayland platform | EGL 1.5 (Mesa), OpenGL 4.6 core `AMD Radeon 780M (radeonsi, phoenix, LLVM 19.1.7, DRM 3.61)`, GLSL 4.60, OpenGL ES 3.2 -- hardware. The X11 and GBM EGL platforms on this agent's `DISPLAY=:99` report llvmpipe, which is the Xvfb and not the GPU. |
| Vulkan | loader 1.4.309, `AMD Radeon 780M (RADV PHOENIX)`, driver 25.0.7, `VK_KHR_wayland_surface` rev 6, `VK_LAYER_KHRONOS_validation` 1.4.309 installed |
| Wayland development packages | `libwayland-dev 1.23.1` (client, server, cursor, egl), `wayland-protocols 1.44`, `wayland-scanner 1.23.1`, `libxkbcommon-dev 1.7.0`, `libegl-dev 1.7.0`, `libvulkan-dev 1.4.309`, `libdecor-0-dev 0.2.2` (not used) |
| Test compositors | Weston 14.0.2 and `wl-clipboard 2.2.1` **unpacked** (not installed; there is no sudo here) into `~/deps/weston` with wrappers in `~/deps/weston/bin`; `gnome-shell --headless` from the installed GNOME 48 |

### 0.1 Source audit -- what already existed before this plan

Read out of the tree at the baseline commit.

| Finding | Evidence |
|---|---|
| The native-handle contract **already models Wayland**: `NativeWindowSystem::Wayland`, `NativeWindowHandle{display = wl_display*, surface = wl_surface*}`, `WaylandNativeWindow` and `TryGetWayland()` (validates both pointers). No generic change is needed to hand a renderer a Wayland window. | `modules/platform/include/CNA/Platform/NativeWindowHandle.hpp`, `modules/platform/src/NativeWindowHandle.cpp` |
| **Two renderer families already consume Wayland handles** directly: `webgpu` (`WGPUSurfaceSourceWaylandSurface`) and `bgfx` (`NativeWindowHandleType::Wayland`). The SDL3 backend produces them when SDL's video driver is `wayland`. | `grep -rn TryGetWayland modules/renderers/` |
| Every other GPU renderer reaches a window through the generic services -- `IPlatformGlContext` (EasyGL, sokol, rlgl, nanovg, opengles1, openvg, diligent, igl), `IPlatformVulkanSurface` (Vulkan), `CreateSurfacePresenter` (SOFTWARE and the CPU rasterisers). So a Wayland platform that implements those three services is what makes them work; no renderer needs a Wayland include. | `grep -rln "GetGlContext\|GetVulkanSurface\|CreateSurfacePresenter" modules/renderers modules/graphics/src` |
| The Vulkan renderer already handles Wayland's "the application chooses the extent" rule: when `currentExtent.width == UINT32_MAX` it sizes the swapchain from the platform window's drawable size. | `modules/renderers/vulkan/src/VulkanRenderer.cpp:4500` |
| **There is no `ExclusiveFullscreen` capability**, and `GraphicsDevice`/`GameWindow` request `ExclusiveFullscreen` for every fullscreen game. The X11 backend already set the precedent for a display that cannot switch modes: fullscreen stays on the desktop's mode and the window **reports `BorderlessFullscreen`** (X11 D11). Wayland takes the same rule (D-11 below) rather than refusing every fullscreen game. | `modules/graphics/src/Xna/GraphicsDevice.cpp:4404`, `modules/runtime/src/GameWindow.cpp:205`, `X11Window.cpp:364` |
| `IPlatformWindow` has **no position setter**; only `WindowDescription::x/y/centered`. Wayland ignores those (D-10) -- nothing needs refusing. | `IPlatformWindow.hpp` |
| Generic code that lived under `src/X11/` without being X11: the XKB key-name and keysym tables (`X11Keyboard.cpp`, only keysym constants from X), the D-Bus loader (`X11DBus`, 0 Xlib references), the desktop portal (`X11DesktopPortal`, 0 Xlib references apart from an `x11:` parent-window string), the session-bus screen-saver inhibition (`X11ScreenSaver`, half D-Bus), the CPU frame fitting (`ComputePresentRect` and the RGBA→XRGB loop in `X11GraphicsServices.cpp`), the Vulkan loader lookup and the POSIX monotonic clock. Phase B moves exactly those. | `grep -c` of Xlib symbols per file, recorded in WAYLAND-0002 |
| The Linux evdev controllers, haptics and `LinuxSystemInfo` are already in `src/Linux/`, but `modules/platform/CMakeLists.txt` compiles them **only** for `CNA_PLATFORM=X11`. | `modules/platform/CMakeLists.txt:41-60` |
| The conformance suite is parameterised over `PlatformFactory::GetAvailable()`: registering `"Wayland"` enrols the backend in it automatically. `GetDisplays() != nullptr` must equal `multipleDisplays`; a hidden window's `SetSize` + `Sync` must land. | `PlatformConformanceTests.cpp` |
| SDL availability is decided per selection (`cmake/SdlAvailability.cmake`); only `SDL3`/`SDL2` platforms and audio, and four renderers, need SDL. `CNA_PLATFORM=WAYLAND` needs no change there to be SDL-free. | `cmake/SdlAvailability.cmake:61-74` |

### 0.2 Protocol inventory, measured

`spikes/wayland-spike/registry_dump.c` lists every global a compositor advertises (WAYLAND-0001).

| Global | GNOME 48 (desktop) | headless gnome-shell 48 | Weston 14 headless | Used for |
|---|---|---|---|---|
| `wl_compositor` | v6 | v6 | v5 | surfaces, regions; v6 `preferred_buffer_scale` |
| `wl_subcompositor` | v1 | v1 | v1 | client-side decorations |
| `wl_shm` | v2 | v2 | v2 | software presenter, cursors, decorations |
| `wl_seat` | v8 | v8 (caps appear with RemoteDesktop input) | **absent** | keyboard, pointer, touch; v8 `axis_value120`… |
| `wl_output` | v4 ×2 | v4 | v4 | displays; v4 `name`/`description` |
| `xdg_wm_base` | v6 | v6 | v5 | windows; v6 `suspended` state |
| `zxdg_output_manager_v1` | v3 | v3 | v2 | logical output geometry |
| `wl_data_device_manager` | v3 | v3 | v3 | clipboard, drag and drop |
| `zwp_primary_selection_device_manager_v1` | v1 | v1 | — | primary selection |
| `wp_viewporter` | v1 | v1 | v1 | fractional scaling |
| `wp_fractional_scale_manager_v1` | v1 | v1 | — | fractional scaling |
| `zwp_relative_pointer_manager_v1` | v1 | v1 | v1 | relative mouse |
| `zwp_pointer_constraints_v1` | v1 | v1 | v1 | pointer lock |
| `wp_cursor_shape_manager_v1` | v2 | v2 | — | system cursors |
| `zwp_text_input_manager_v3` | v1 | v1 | — (v1 only) | IME |
| `xdg_activation_v1` | v1 | v1 | — | focus requests |
| `zwp_idle_inhibit_manager_v1` | v1 | v1 | — | screen saver |
| `wp_presentation` | v2 | v2 | v1 | presentation timing (optional) |
| `zxdg_exporter_v2` | v1 | v1 | — | portal parent window |
| `zwp_tablet_manager_v2` | v1 | v1 | — | pens (not implemented, WAYLAND-0059) |
| `zxdg_decoration_manager_v1` | **absent** | absent | absent | server-side decorations |
| `wl_drm`, `zwp_linux_dmabuf_v1` | yes | yes | — | used by Mesa's EGL/Vulkan WSI, not by CNA |

Consequences: GNOME draws **no** server-side decorations (D-22); Weston headless has **no seat**
(input tests need headless gnome-shell or the in-process compositor, §6.K).

---

## 1. Architecture

```
CNA application
      |
      v
CNA platform abstraction          (CNA::Platform::IPlatform, unchanged)
      |
      v
WaylandPlatform                   (modules/platform/src/Wayland/)
      |
      +--> wl_display_connect / own default queue, non-blocking prepare_read pump
      +--> wl_registry: versioned binds, global / global_remove
      +--> wl_compositor, wl_subcompositor, wl_shm
      +--> xdg_wm_base -> xdg_surface -> xdg_toplevel (configure state machine)
      +--> wl_seat(s) -> wl_keyboard (xkbcommon, compose), wl_pointer, wl_touch
      +--> relative-pointer + pointer-constraints, cursor-shape, text-input-v3
      +--> wl_output + xdg-output, fractional-scale + viewporter
      +--> wl_data_device (clipboard, DnD), primary selection
      +--> idle-inhibit, xdg-activation, xdg-decoration / built-in CSD, xdg-foreign
      |
      v
NativeWindowHandle{ system = Wayland, display = wl_display*, surface = wl_surface* }
      |
      +--> EGL (libEGL + libwayland-egl, loaded at run time) -> IPlatformGlContext -> EasyGL, GL families
      +--> VK_KHR_wayland_surface -> IPlatformVulkanSurface -> Vulkan (and Diligent/LLGL/Wicked)
      +--> wl_shm -> IPlatformSurfacePresenter -> SOFTWARE and the CPU rasterisers
      +--> webgpu, bgfx (TryGetWayland, already present)

Shared, window-system independent:
      src/Linux/   evdev controllers, haptics, system info, D-Bus, desktop portal, screen-saver bus
      src/Xkb/     XKB key-name -> Scancode, keysym -> KeyCode (X11 and Wayland)
      src/Common/  filesystem, frame fitting, monotonic clock, Vulkan loader lookup
```

**`platform != renderer`.** There is no `WaylandOpenGLPlatform` and no `WaylandVulkanPlatform`.
No renderer includes a Wayland header of CNA's; renderers read the generic native handle or ask the
generic services.

**Wayland is not X11.** `src/Wayland/` includes no X11 header and links no X library; a test scans
it (`WaylandIsSdlFree`, which also forbids X11), and `readelf -d` on an SDL-free Wayland binary is
the evidence (WAYLAND-0123).

---

## 2. Design decisions (recorded before implementation)

| # | Decision | Rationale |
|---|---|---|
| D-1 | **`wayland-client` and `xkbcommon` are linked; `libEGL`, `libwayland-egl`, `libwayland-cursor`, `libvulkan` and `libdbus` are loaded at run time.** Their headers are needed to build the corresponding capability, and each absent header turns off exactly that capability. | The X11 backend's rule (NPV-0121): a HEADLESS or SOFTWARE Wayland build must not need a GL implementation installed to start, and nothing above the platform module may inherit a GL library in its link closure. `wayland-client` and `xkbcommon` are the backend's reason to exist -- there is no Wayland window without the first and no keyboard without the second. |
| D-2 | **Protocol bindings are generated at build time by `wayland-scanner` from the installed `wayland-protocols` XML**, as `client-header` + `private-code` (and `server-header` for the test compositor). Each optional protocol whose XML is absent from an older `wayland-protocols` turns off its capability (`CNA_WAYLAND_HAVE_<PROTOCOL>`); `xdg-shell` is mandatory. | Hand-written protocol declarations are forbidden by the task and drift from the XML. `private-code` gives the interface symbols hidden visibility, so a host that links its own copy of `xdg_wm_base_interface` (a toolkit, libdecor) cannot collide with CNA's. Generated files live in the build tree; nothing generated is committed. |
| D-3 | **The connection is opened in the platform's constructor; a failure is recorded, not thrown**, and the capability set is computed once after a bounded startup exchange (registry + one sync for seat capabilities and output information). | X11's reasoning holds unchanged: the capability set must be stable for the instance's lifetime, and a process with no compositor must still reach `StorageDevice`. `AcquireSubsystem(Video)` reports the original connection error. |
| D-4 | **One `wl_display` per platform instance, owned by it; every Wayland call is made on the thread that pumps events.** The default queue is the platform's own because the platform created the connection. Mesa's EGL and Vulkan WSI create their own queues on it. | The conformance suite runs two platform instances in one process, and libwayland's event dispatch is per queue. A platform that shared a host's `wl_display` would be dispatching the host's events. |
| D-5 | **`PollEvents` never blocks**: `dispatch_pending`, `prepare_read` (looping `dispatch_pending` while it fails), `flush` (an `EAGAIN` just leaves data queued), `poll(fd, 0)`, then `read_events` or `cancel_read`, then `dispatch_pending`. Blocking waits exist only where the contract asks for one (`CreateWindow`, `Show`, `Sync`, clipboard reads) and are **bounded**: a `wl_display_sync` callback awaited with a deadline, never `wl_display_roundtrip()` (which waits forever for a hung compositor). | The prepare/read protocol is the only race-free way to read the socket without blocking. An unbounded roundtrip turns a stuck compositor into a stuck game. |
| D-6 | **Every proxy has one owner, and the owner destroys it before the memory its listener `data` points to is freed.** Windows are owned by the application; each registers with the platform and unregisters in its destructor, destroying children first (locked pointer, text-input focus, idle inhibitor, fractional scale, viewport, decoration, toplevel, xdg_surface, wl_surface). Events libwayland had already queued for a destroyed proxy are dropped by libwayland itself. Services that keep a window pointer keep it in an id-keyed registry and look it up per event; none holds a raw `WaylandWindow*` across a dispatch. | Wayland's asynchronous callbacks are where stale-pointer defects come from. A pointer in listener data that outlives its object is a use-after-free that ASan finds only when the compositor happens to send the event late -- so the rule is structural, not tested into existence. |
| D-7 | **A window has an explicit configure state machine**: `NoRole` → `AwaitingInitialConfigure` → `Configured`. The initial commit carries no buffer; `CreateWindow`/`Show` wait (bounded) for the first `xdg_surface.configure`; every `xdg_surface.configure` is acknowledged before the commit that applies it; `xdg_toplevel.configure` state is pending until its `xdg_surface.configure`. | The `unconfigured_buffer` and `invalid_serial` protocol errors are exactly what a renderer's first `eglSwapBuffers` would trigger if `CreateWindow` returned before the first configure. |
| D-8 | **`Hide` destroys the window's role (`xdg_toplevel` + `xdg_surface`) and commits a null buffer; `Show` recreates the role and waits for the initial configure again.** A window created with `visible = false` has no role until `Show`. The `wl_surface` -- and therefore the native handle, the `wl_egl_window` and any `VkSurfaceKHR` -- lives for the whole window. | xdg-shell has no "hide": unmapping returns the surface to the unconfigured state, and a renderer that keeps presenting (EGL and Vulkan attach buffers on their own) into an unconfigured `xdg_surface` is a fatal protocol error. A surface with no role may have buffers committed harmlessly. This is SDL3's approach, for the same reason. |
| D-9 | **`SetSize` applies at once to a floating window** (Wayland clients size themselves) and is **remembered, not applied**, while the compositor constrains the window (maximized, fullscreen, tiled); `GetClientBounds` always reports the size the window actually has. | A configure with a non-zero size in a constraining state is a size the client must honour; a floating client may pick any size. Reporting a size the compositor did not grant would be a fabricated state. |
| D-10 | **Window position is the compositor's.** `WindowDescription::x/y/centered` are ignored, no `Moved` event is ever produced, and no private positioning protocol is used. | Wayland does not let ordinary clients place top-level windows, by design. The contract has no position setter to refuse. |
| D-11 | **`BorderlessFullscreen` is `xdg_toplevel.set_fullscreen` on the window's current output; `ExclusiveFullscreen` makes the same request and the window reports `BorderlessFullscreen`**, never `ExclusiveFullscreen`. | Ordinary Wayland clients cannot take a display mode. X11's D11 already fixed CNA's rule for "exclusive requested, mode switch impossible": stay on the desktop's mode and say so. Refusing would break every fullscreen XNA game, because the framework asks for exclusive whenever `IsFullScreen` is set. |
| D-12 | **`Minimize` is `xdg_toplevel.set_minimized`; `IsMinimized` is the `suspended` state (xdg_wm_base v6), the only thing a compositor tells a client.** `Restore` leaves maximized and fullscreen; it cannot un-minimize, because xdg-shell has no such request -- the user does that through the compositor. | Wayland sends no "you are minimized" event. Reporting minimized because the application asked would be a synthesised state the compositor never confirmed. |
| D-13 | **Logical units are surface coordinates; a window that opted into `highDpi` renders at the compositor's preferred scale** -- fractional through `wp_fractional_scale_v1` + `wp_viewporter` (buffer = round(logical × scale), viewport destination = logical size), integer through `wl_surface.preferred_buffer_scale` (v6) or the largest scale of the outputs the surface is on. A window without `highDpi` renders at scale 1 and the compositor scales it. `GetDisplayScale()` is the pixel density `pixel size / logical size`, as the SDL3 backend defines it. | Using only `wl_surface.set_buffer_scale` renders a 125 % desktop at 200 % and lets the compositor downscale: blurry and wasteful. The contract's own definition makes the three getters coherent: `GetPixelSize() == round(GetClientBounds() × GetDisplayScale())`. |
| D-14 | **The keyboard is xkbcommon on the compositor's keymap.** Physical `Scancode` from the keymap's **key names** through the table X11 already uses (moved to `src/Xkb/`); logical `KeyCode` from the active layout's level-0/level-1 keysyms through X11's `BuildKeyCodeTable` rules; modifiers from `xkb_state`. Held keys are accumulated from events and seeded by `wl_keyboard.enter` (state, not presses); `leave` releases every held key without events, as X11 does on `FocusOut` (X11-0034). Key repeat is generated by CNA from `repeat_info`. | Wayland sends no repeat events and offers no key-state query, so both have to be the client's. Sharing the XKB tables means a key has the same `Scancode` and `KeyCode` under X11 and Wayland on the same machine. |
| D-15 | **Committed text is `xkb_state_key_get_utf8` through `xkb_compose` (dead keys, `Multi_key` sequences), delivered only while text input is started for the focused window**, with lone control characters dropped. | X11's rule (X11-0040), so one game behaves the same under both. |
| D-16 | **`text-input-v3` provides composition when the compositor offers it; `ime` is true only when it does** -- preedit as `TextEditingEvent`, commit as `TextInputEvent`; `delete_surrounding_text` has nothing to act on (the platform does not hold the application's text) and is ignored. Candidate lists are drawn by the input method and never delivered (as under XIM). | The contract's IME events are what the protocol carries, apart from candidates, which X11's D16 already records as undeliverable. |
| D-17 | **Scroll is `axis_value120` where the seat has it (v8), `axis_discrete` (v5–v7), else `axis` / 10 (the 10-units-per-notch convention libinput and every compositor use).** `MouseWheelEvent` carries notches (fractional for high-resolution wheels); the snapshot accumulates XNA units, 120 per notch. | X11's D-6: the snapshot was once accumulated in notches, off by 120. value120 is exactly XNA's unit. |
| D-18 | **Relative mode is `zwp_relative_pointer_v1` (unaccelerated deltas, fractions carried) plus a persistent `zwp_locked_pointer_v1`**; `relativeMouse` is true when both managers exist. The lock is destroyed before its surface, on disable, on window destruction and when the pointer goes away; the cursor is hidden while locked. | The X11 backend reports raw (unaccelerated) motion too. A persistent lock is re-activated by the compositor on refocus; the desktop can never stay trapped because the compositor owns activation and CNA owns destruction. |
| D-19 | **`globalPointer` is false.** `SetCapture`, `TryGetGlobalPosition` and `SetGlobalPosition` refuse naming `GlobalPointer`; window-scoped `SetPosition` records the position and, while the pointer is locked, sends it as `set_cursor_position_hint`. | Wayland gives ordinary clients neither global coordinates nor warping, by design. |
| D-20 | **Cursors: `wp_cursor_shape_device_v1` for system shapes; a `wl_cursor` theme (libwayland-cursor, loaded at run time) where the protocol is absent; custom image cursors on a `wl_shm` cursor surface.** | Cursor-shape lets the compositor draw its own theme at the right scale. |
| D-21 | **The clipboard is `wl_data_device`**, with transfers through pipes that never block the event thread: an outgoing `send` is written incrementally from `PollEvents`, an incoming read is a bounded wait that keeps dispatching the display. CNA's own selection is answered from memory. | A client owning the selection must answer `send` requests while it reads; blocking in `read()` on a pipe another process fills slowly would freeze the game. |
| D-22 | **Decorations: `zxdg_toplevel_decoration_v1` server-side where offered; otherwise a minimal built-in client-side frame** (a `wl_subsurface` title bar with close, maximize and minimize, drag-to-move, double-click maximize, resize borders), hidden when fullscreen or borderless. No libdecor, no toolkit. | GNOME offers no server-side decorations; a window with none cannot be moved or closed with the mouse. libdecor's GTK plugin loads GTK into a game process. |
| D-23 | **EGL on the Wayland platform (`eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR)`), one `EGLDisplay` per platform, a `wl_egl_window` per GL window resized on configure and scale changes.** Vsync is implemented by CNA: EGL's own interval is 0, and `SwapBuffers` with interval 1 waits for the previous frame's `wl_surface.frame` on a private queue with a deadline. | Mesa's EGL with interval 1 waits for a frame callback indefinitely -- a hidden or occluded window then hangs the game inside `eglSwapBuffers`. SDL3 does the same for the same reason. |
| D-24 | **Vulkan through `VK_KHR_surface` + `VK_KHR_wayland_surface`**, `vkCreateWaylandSurfaceKHR` resolved through the caller's `vkGetInstanceProcAddr` (the lookup X11 already had, moved to `src/Posix/VulkanLoader`). | No Vulkan loader in the link closure (D-1). |
| D-25 | **The software presenter is `wl_shm` XRGB8888 with a ring of up to three buffers in one `memfd` pool; a buffer is reused only after `wl_buffer.release`**, damage through `damage_buffer`, vsync through the frame callback with a deadline. | Reusing a buffer the compositor still reads is undefined behaviour the protocol forbids. |
| D-26 | **`SetScreenSaverEnabled(false)` creates a `zwp_idle_inhibitor_v1` on every window**, falling back to the session bus's `org.freedesktop.ScreenSaver` (shared with X11) where the protocol is absent. | The inhibitor is tied to a visible surface, which is exactly a game's case. |
| D-27 | **Shared Linux services** -- evdev gamepads/joysticks/haptics, power and host information, the desktop portal (file dialogs, OpenUrl, parent window via `xdg-foreign`), D-Bus -- are the X11 backend's, moved to `src/Linux/` (evdev, sysfs), `src/Freedesktop/` (D-Bus, portal, screen saver, drop parsing), `src/Xkb/` and `src/Posix/`, and compiled for both. | Controllers, power and the portal are not window-system features. |
| D-28 | **Tests never touch the user's desktop session or session bus.** Automated suites run against an in-process compositor, a private Weston, or a private headless gnome-shell on a private D-Bus bus (input through that compositor's own RemoteDesktop API -- no uinput, no input into the real desktop). The real desktop is used only by the explicitly invoked validation harness, which injects no input. | The owner's desktop is in use, and GNOME locks when idle; X11's rules (openbox-unpacked-in-deps, x11-perfection-deferred memories) carry over. |
| D-29 | **`messageBox` is false; `ShowMessageBox` refuses** (WAYLAND-0094). | Wayland has no dialog protocol, so a message box is a window the backend draws, text included. X11's own dialog draws with the X server's core fonts; a Wayland client has no server fonts and the platform module no text renderer. The two ways to get one were declined: embedding a third-party bitmap font (its licence would travel inside every CNA binary -- the project owner's call, not the backend's) and launching `zenity`/`kdialog` (then it is not this backend; X11's documentation rules it out for the same reason). File dialogs are unaffected: the portal draws them. Revisit if the project adopts a font, or with fontconfig + FreeType loaded at run time. |
| D-30 | **`presentation-time` is not used, and not bound** (WAYLAND-0084). | The contract has no presentation-feedback API -- nothing above the platform can ask when a frame reached the screen or what the refresh is -- and the pacing the presenter and EGL need is `wl_surface.frame` with a deadline, which every compositor has. Binding a global nothing uses is dead code that a reader has to explain away; the protocol's XML is no longer generated either. Revisit if the contract grows a presentation-timing service. |

---

## 3. Capability matrix (target, verified in WAYLAND-0100)

| Capability | Wayland | Why |
|---|---|---|
| multipleWindows | ✔ | any number of `xdg_toplevel`s |
| highDpi | ✔ | fractional/integer scale, D-13 |
| multipleDisplays | ✔ | `wl_output` enumeration |
| borderlessFullscreen | ✔ | `set_fullscreen`, D-11 |
| nativeWindowHandle | ✔ | `wl_display*` + `wl_surface*` |
| surfacePresentation | ✔ | `wl_shm`, D-25 |
| openGlContext | runtime | libEGL + libwayland-egl loadable and an EGL display initialises |
| vulkanSurface | build | Vulkan headers present (the loader is the caller's) |
| clipboard | runtime | `wl_data_device_manager` present |
| clipboardData | runtime | same |
| primarySelection | runtime | `zwp_primary_selection_device_manager_v1` present |
| dragAndDrop | runtime | `wl_data_device_manager` present |
| textInput | ✔ | xkbcommon committed text |
| ime | runtime | `zwp_text_input_manager_v3` present, D-16 |
| exactKeyboardState | ✔ | real key releases; leave releases held keys |
| pixelAccurateMouse | ✔ | surface coordinates are `wl_fixed_t` |
| relativeMouse | runtime | relative-pointer **and** pointer-constraints present |
| cursorShapes | runtime | cursor-shape, or a loadable cursor theme; custom images always |
| globalPointer | ✘ | not exposed to Wayland clients, D-19 |
| inputDeviceEnumeration | runtime | seats' devices and evdev controllers (WAYLAND-0060) |
| gamepad / joystick / gamepadRumble / gamepadSensors / haptics | Linux | shared evdev, D-27 |
| powerInfo | Linux | shared sysfs |
| messageBox | ✘ | D-29 (WAYLAND-0094) |
| nativeFileDialog | runtime | desktop portal on the session bus |
| tray | ✘ | no Wayland protocol; StatusNotifierItem is desktop-specific |
| camera, sensors, managedEntrypoint | ✘ | not window-system facilities |

---

## 4. Task ledger

### Phase A — baseline and audit

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| WAYLAND-0001 | Environment and protocol inventory | ✅ | §0 and §0.2. `spikes/wayland-spike/registry_dump.c`, `output_dump.c` run against the desktop, Weston headless and headless gnome-shell. |
| WAYLAND-0002 | CNA architecture audit | ✅ | §0.1. |
| WAYLAND-0003 | Test-compositor feasibility | ✅ | Weston 14 unpacked into `~/deps/weston` (`WESTON_MODULE_MAP` redirects its modules) runs headless with pixman, no seat. `gnome-shell --headless --wayland --no-x11 --virtual-monitor` runs on a private `dbus-daemon` with `GSETTINGS_BACKEND=keyfile` and a private `XDG_RUNTIME_DIR`; `spikes/wayland-spike/remote_desktop_probe.c` created and started an `org.gnome.Mutter.RemoteDesktop` session there, after which that compositor's `wl_seat` reported a pointer, and Stop removed it again. |
| WAYLAND-0004 | Baseline test results before any change | ✅ | `cmake-build-x11` (X11, HEADLESS, ALSA, `CNA_ENABLE_SDL=OFF`) at `7c36fa321`, with openbox and the Xorg dummy/evdev drivers from `~/deps`: CnaPlatformTests 370 passed (2 skipped), CnaPlatformWindowTests 31 (5 skipped), CnaX11MappingTests 181 (1 skipped), CnaX11EvdevTests 19 (1 skipped), CnaX11IntegrationTests 128 (5 skipped: `X11ClipboardInterop.*`, no `xclip` here), CnaX11WindowManagerTests 19, CnaX11InputMethodTests 4, CnaX11ExclusiveFullscreenTests 17; 0 failed.  |

### Phase B — shared, window-system independent code

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| WAYLAND-0010 | XKB mapping to `src/Xkb/` | ✅ | `src/Xkb/XkbKeyMapping.{hpp,cpp}` (`CNA::Platform::Xkb`): `ScancodeFromKeyName` (NUL-terminated, xkbcommon's form), `ScancodeFromKeyNameField` (X's four-byte field), `KeyCodeFromKeysym`, `UsLayoutKeyCode`, `BuildKeyCodeTable` over spans of any length (Wayland keycodes pass 255). No X or xkbcommon header is needed: the 90 keysym values are stated in the file and `static_assert`ed against whichever header the build has (`XKB_KEY_*` or `XK_*`). `X11Keyboard.cpp` keeps its four entry points as one-line forwards, so every X11 suite is unchanged. New `XkbKeyMappingTests.cpp`, 7 cases, in `CnaX11MappingTests` (181 → 188).  |
| WAYLAND-0011 | D-Bus and desktop portal to `src/Freedesktop/` | ✅ | Not `src/Linux/` but a new `src/Freedesktop/`: libdbus and the portal are freedesktop.org services that exist on the BSDs too, and `src/Linux/` is compiled only with `<linux/input.h>`. `X11DBus` → `Freedesktop/DBusLibrary` (`DBusLibrary`, `GetDBus()`, `DBusMessagePtr`, `OpenSessionBus`), `X11DesktopPortal` → `Freedesktop/DesktopPortal`, whose parent window is now the portal's own string; `PortalParentWindow(xid)` (`x11:<hex>`) stays in X11 beside `X11Dialogs`. `CNA_X11_HAVE_DBUS` → `CNA_PLATFORM_HAVE_DBUS`. `X11PrivateSessionBus.hpp` → `FreedesktopPrivateSessionBus.hpp`. X11 portal suites (`X11PortalRequest`, `X11DesktopPortalBus`, `X11DesktopPortalLive`) unchanged in name and result.  |
| WAYLAND-0012 | Session-bus screen-saver inhibition to `src/Freedesktop/` | ✅ | `Freedesktop/ScreenSaverBus.{hpp,cpp}`: `ScreenSaverBusInhibition` (Inhibit/Lift/IsHeld) and `ScreenSaverApplicationName()`. `X11ScreenSaverInhibitor` keeps its Xss suspension and timeout fallback and delegates the desktop request. `X11ScreenSaverLive`/`X11ScreenSaverDesktopLive` unchanged and green.  |
| WAYLAND-0013 | CPU frame fitting to `src/Common/` | ✅ | `Common/SurfaceFrameFitting.{hpp,cpp}`: `ComputePresentRect` and `ScaleSurfaceFrame<Store>` -- the nearest/2×2-box scaling loop with a per-pixel store the backend supplies, so X11 keeps its visual-mask packing and `XPutPixel` fallback and Wayland writes XRGB8888. X11 presenter suites green.  |
| WAYLAND-0014 | Monotonic clock and Vulkan loader lookup to `src/Posix/` | ✅ | `src/Posix/MonotonicClock` (`CLOCK_MONOTONIC` nanoseconds, absolute-deadline `clock_nanosleep` resumed on `EINTR`) and `src/Posix/VulkanLoader` (`ResolveVulkanProcAddr`). X11Platform's counter and `Delay` and X11VulkanSurface use them; conformance timing tests green.  |
| WAYLAND-0015 | Build `src/Linux/` for WAYLAND | 🟨 | `modules/platform/CMakeLists.txt` compiles `src/Xkb/`, `src/Freedesktop/`, `src/Posix/` and (with `<linux/input.h>`) `src/Linux/` for `X11` **or** `WAYLAND`, and links `${CMAKE_DL_LIBS}` for both. The four pure Linux test files carried an `X11` prefix only because the X11 build was the only one to compile them; renamed `LinuxEvdevHapticsTests.cpp`, `LinuxEvdevLayoutTests.cpp`, `LinuxEvdevMappingTests.cpp`, `LinuxSystemInfoTests.cpp` (suites `LinuxEvdevHapticEffect`, `LinuxEvdevLayout`, `LinuxEvdevHub`, `LinuxEvdevMapping`, `LinuxSystemInfo`) and compiled for both selections. Open: proven only in the X11 build until the Wayland build exists (WAYLAND-0020).  |
| WAYLAND-0016 | X11 regression after the extraction | ✅ | After the extraction, same tree, same command: every count identical to WAYLAND-0004 except CnaX11MappingTests 188 (the 7 new `XkbKeyMapping` cases), same skips, 0 failed. Gates: `sdl_inventory`, `sdl_classify`, `renderer_sdl_audit`, `sdl_ratchet` (denylist now also `src/Wayland/`, `src/Xkb/`, `src/Freedesktop/`, `src/Posix/`), `hot_path_lint`, `nonproduction_sdl_audit` all exit 0.  |
| WAYLAND-0017 | Drag-and-drop payload parsing to `src/Freedesktop/` | ✅ | Found while writing the Wayland data device: X11's `ChooseDropTarget`, `ParseUriList`, `FileUriToPath` and the text decoding are freedesktop.org conventions (`text/uri-list`, RFC 8089 `file:` URIs, UTF-8/Latin-1 text), not X. `Freedesktop/DropParsing.{hpp,cpp}` (`DropEncoding`, `DropTarget`, `ChooseDropTarget`, `DroppedItem`, `ParseUriList`, `FileUriToPath`, `DecodeDropText`, `LocalHostName`); `X11DragAndDrop` calls them, the XDND protocol stays in X11. The pure cases moved from `X11DragAndDropTests.cpp` to `FreedesktopDropParsingTests.cpp` (suites `FreedesktopDropTarget`, `FreedesktopUriList`, `FreedesktopDropText`), compiled for X11 and Wayland; `CnaX11MappingTests` filter renamed accordingly. X11 regression: `cmake-build-x11` rebuilt, `ctest -R "CnaX11|CnaPlatform"` 10/10 passed or skipped as before, the window-manager pair passed with `~/deps/openbox`. |

### Phase C — build system and protocol generation

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| WAYLAND-0020 | `cmake/PlatformWayland.cmake` discovery and `CNA_PLATFORM=WAYLAND` | ✅ | pkg-config `wayland-client`, `xkbcommon`, `wayland-scanner`, `wayland-protocols`; optional headers (EGL, wayland-egl, wayland-cursor, Vulkan, D-Bus). An explicit selection without the mandatory set fails configure with the reason and the packages; the default stays SDL3. Implemented: `cna_detect_wayland()` finds every piece through pkg-config (no distro path), `CNA_PLATFORM=WAYLAND` with a missing mandatory piece fails configure naming the package; the default selection is unchanged. `cmake-build-wayland` (Ninja, `-DCNA_PLATFORM=WAYLAND -DCNA_ENABLE_SDL=OFF`, renderers HEADLESS;OPENGL33;VULKAN;SOFTWARE) configures with wayland-client 1.23.1, xkbcommon 1.7.0, wayland-protocols 1.44 and every optional piece present, and builds all 1895 steps. |
| WAYLAND-0021 | Protocol code generation | ✅ | `wayland-scanner` client-header + private-code per protocol into the build tree; server-header for the test compositor; `CNA_WAYLAND_HAVE_<PROTOCOL>` per optional XML. `cna_wayland_generate_protocols()`: client header, server header and `private-code` per protocol into `<build>/generated/wayland-protocols`; tablet-v2 is generated with cursor-shape-v1, whose tables name it. Nothing generated is committed. |
| WAYLAND-0022 | Factory registration and conformance enrolment | ✅ | `"Wayland"` in `PlatformFactory`; conformance suite runs it. `"Wayland"` in `PlatformFactory` (default name when `CNA_PLATFORM=WAYLAND`); the conformance suite instantiates it and, with no compositor in the unit-test environment, skips the window cases. Live: the conformance suite runs its Wayland instances against private Weston in all three `CnaWaylandWeston*` entries (34 pass, 1 correct skip). |
| WAYLAND-0023 | Configuration tests | ✅ | ctest script: `CNA_PLATFORM=WAYLAND` with `PKG_CONFIG_LIBDIR` pointed at nothing fails with the diagnostic; a default configure does not select Wayland. `cmake/Tests/WaylandPlatformSelection.cmake`, ctest `CnaWaylandPlatformSelection` (runs in every configuration): a fixture including the real `cmake/PlatformWayland.cmake` is offered WAYLAND on this machine with the summary naming the libraries found; with `PKG_CONFIG_LIBDIR` pointed at an empty directory the selection fails loudly, names a package to install and offers no fall back; detection itself still succeeds and its reason names what is missing; and `CNA_PLATFORM`'s default is still SDL3. |

### Phase D — connection, windows, lifecycle

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| WAYLAND-0030 | Connection and registry | ✅ | Versioned binds (`min(advertised, supported)`), `global_remove` for every bound global, protocol and display errors reported with interface, object id and code. `WaylandConnection`: binds at `NegotiateVersion(advertised, handled, headers)`; `global_remove` destroys a withdrawn singleton and nulls its field; the first protocol/display error is kept with interface, object id and code. Protocol tests `ConnectsBindsAndReportsWhatTheCompositorOffers`, `AnOldCompositorIsBoundAtItsOwnVersions`, `AWithdrawnSingletonIsNoLongerUsed`, `ACompositorThatGoesAwayEndsTheApplicationOnceAndSafely`. |
| WAYLAND-0031 | Non-blocking event pump | ✅ | D-5; a test holds the compositor silent and shows `PollEvents` returns in under a millisecond. `Pump` = `DispatchFor(0)`: prepare_read / poll(0) / read_events / dispatch_pending, flush with EAGAIN kept for later. `PollingWithNothingToReadNeverWaits`: 1000 polls well under 1 ms each. |
| WAYLAND-0032 | `xdg_wm_base.ping` | ✅ | Answered from the pump; test with the in-process compositor. Answered inside the dispatch; `APingIsAnsweredFromTheOrdinaryPump`. |
| WAYLAND-0033 | Window and configure state machine | ✅ | D-7; tests: configure before buffer, several configures before a commit, 0×0 configure, destroy while a configure is pending. D-7 as designed; the in-process compositor posts `unconfigured_buffer`, `invalid_serial`, `invalid_size` and Mutter-style `invalid_surface_state` for a constrained geometry mismatch, and none is raised by any test. `AShownWindowIsConfiguredAcknowledgedAndDescribed`, `AHiddenWindowHasNoRoleUntilShown`, `ACompositorResizeReachesTheApplicationAndTheNextBuffer`, `AFloatingSizeRespectsTheApplicationsLimits`, `StateChangesAfterTheLastWindowAreHarmless`. |
| WAYLAND-0034 | Window operations | ✅ | Title, app id, `SetSize` (D-9), min/max size, resizable, `Show`/`Hide` (D-8), maximize, minimize (D-12), restore, fullscreen (D-11), close request, `Sync`, focus, `Exposed`. Maximize/restore, fullscreen (never exclusive, D-11), close, Show/Hide, focus tested (`MaximizeTakesExactly…`, `FullscreenIsBorderless…`, `ACompositorThatIgnoresStateRequests…`, `CloseAsksTheApplication…`). `SetSizeResizesAFloatingWindowAndWaitsOutAMaximizedOne`, `ANonResizableWindowPinsItsLimitsToItsSize`, `ABorderlessWindowHasNoTitleBarAndGetsOneBack`, `MinimizeAsksAndSuspendedIsWhatReportsIt`. Defect found and fixed: a non-resizable window took a floating configure's suggested size (it now keeps its size whatever a floating configure says). |
| WAYLAND-0035 | Multiple windows and lifetime | 🟨 | 1, 2, 4 windows; rapid create/destroy; closing secondary and primary; no listener outlives its object (ASan). Two windows, secondary close without quit, five create/present/destroy rounds leaving no surface, toplevel or subsurface behind (`DestroyingWindowsLeavesNothingBehind`). Open: ASan run (WAYLAND-0116). |
| WAYLAND-0036 | Native handle | ✅ | `{Wayland, wl_display*, wl_surface*}`, `TryGetWayland` succeeds. Implemented (`{Wayland, wl_display*, wl_surface*}`). `TheNativeHandleIsTheDisplayAndTheSurface`. |
| WAYLAND-0037 | `AdoptWindow` / `AdoptWindowHandle` | ✅ | Borrowed wrappers for own windows; a foreign token refused. Implemented (borrowed wrappers; a foreign token refused). `OwnWindowsCanBeAdoptedAndForeignOnesCannot`. |

### Phase E — outputs and scaling

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| WAYLAND-0040 | Outputs and `IPlatformDisplays` | ✅ | `wl_output` v4 + `zxdg_output_v1`; name, description, logical geometry, mode, transform, scale; hot add/remove. `WaylandOutput` (wl_output v4 + zxdg_output_v1, atomic on `done`), `WaylandDisplays`; `OutputsComeAndGoWhileRunning` (hot add with a 125 % logical size, hot remove while a window is on it), `AnOutputsScaleChangeIsSeenAtOnce`. |
| WAYLAND-0041 | Window ↔ output tracking | ✅ | `wl_surface.enter/leave`; `DisplayChanged`; `GetDisplayName`. `wl_surface.enter/leave`; `TheOutputsAWindowIsOnDecideItsIntegerScaleOnOldCompositors` (display name, scale per output), removal forgets the output. |
| WAYLAND-0042 | Fractional and integer scaling | 🟨 | D-13; 1.25 on the real desktop; transitions between outputs; `Resized`/`PixelSizeChanged`/`DisplayScaleChanged`. D-13: viewport for every scale where there is one, `set_buffer_scale` only without. `AFractionalScaleResizesTheBufferNotTheWindow` (1.25: 800×600 logical, 1000×750 buffer, buffer scale 1, viewport 800×600), `AnApplicationThatIsNotHighDpi…`, `WithoutAViewporterAnIntegerScaleIsTheBufferScale` (odd logical size, even buffer). Open: 1.25 on the real desktop (WAYLAND-0120). |

### Phase F — input

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| WAYLAND-0050 | Seats | ✅ | Capabilities gained and lost at run time; several seats; a seat's removal. Seats announced at startup and later; a seat without devices; `WithoutASeatThereIsNoClipboardAndNoInputDevices`. `DevicesComeAndGoWithTheSeatsCapabilities` (keyboard unplugged, touchscreen attached, and back); under GNOME the pointer appears at run time. Inconsistency found and fixed: unplugging the last keyboard from a window the compositor keeps active posted FocusLost while `HasFocus()` stayed true; focus events now follow `HasFocus()` exactly, activation included. |
| WAYLAND-0051 | Keyboard | ✅ | D-14; keymap fd mapped read-only; layouts, AltGr, Caps/Num Lock, sided modifiers; `IPlatformKeyboard` name queries. D-14. `FocusAndKeysArriveWithScancodesKeyCodesAndText`, `KeysHeldAtEnterAreStateNotEvents…`, `TheCzechLayoutTypesCzechAndKeepsGameKeysWhereTheyAre`, `ANewKeymapTakesEffectForTheNextKey`, mapping suite `WaylandModifierMapping`. Defect found and fixed here: xkbcommon before 1.8 never reports a virtual modifier active, so AltGr (`LevelThree`) never became `Mode`; Mod5 is now asked as well. |
| WAYLAND-0052 | Key repeat | ✅ | From `repeat_info`; rate 0 disables; only keys the keymap marks repeating. `AutoRepeatFollowsTheCompositorsRateAndStopsOnRelease` (rate 50, delay 100 ms), `ARepeatRateOfZeroMeansNoRepeat`. |
| WAYLAND-0053 | Committed text and compose | ✅ | D-15; dead keys, compose sequences, Czech. Committed text through xkbcommon and the compose table of the environment's locale; tested for plain and Czech text. `DeadKeysComposeThroughTheLocalesTable` (us intl: dead acute + e = é). |
| WAYLAND-0054 | IME (`text-input-v3`) | 🟨 | D-16; tested with ibus in the headless gnome-shell. text-input-v3 per seat, done applies commit then preedit, preedit cursor bytes→characters; `AnInputMethodComposesAndCommitsThroughTextInputV3` on the in-process compositor. Open: ibus under headless gnome-shell. |
| WAYLAND-0055 | Pointer | ✅ | Buttons (left, middle, right, X1, X2), motion, wheel (D-17), frames, double click. `PointerMotionButtonsAndDoubleClicks`, `WheelNotchesHighResolutionSlicesAndTouchpadScrollAllCount` (value120 halves, touchpad 5 units = half a notch), `OldSeatsSendDiscreteStepsInstead`, mapping suites `WaylandButtonMapping`, `WaylandWheelMapping`. |
| WAYLAND-0056 | Relative pointer and constraints | ✅ | D-18; lock/unlock stress, focus loss, window close. D-18: persistent lock, unaccelerated motion with fractions carried, lock destroyed before its surface. `RelativeModeLocks…`, `DestroyingALockedWindowReleasesTheLockFirst`, `SetPositionWhileLockedBecomesTheUnlockHint`, `TheGlobalPointerIsRefusedByName`. |
| WAYLAND-0057 | Cursors | ✅ | D-20. cursor-shape-v1 with the enter serial, hidden = `set_cursor(NULL)`, custom ARGB surface with hot spot (`SystemCursorsGoThroughCursorShape…`, `ACustomCursorIsASurfaceWithItsHotSpot`). Defect found and fixed: the libwayland-cursor theme's buffers were destroyed after the connection closed (crash at exit on compositors without cursor-shape); the mouse now releases them in `OnDisconnecting`. `WithoutCursorShapeTheThemeDrawsTheSystemCursor` (Adwaita through libwayland-cursor). |
| WAYLAND-0058 | Touch | ✅ | `wl_touch` down/up/motion/frame/cancel, stable ids, normalised coordinates. `TouchesAreNormalisedAndCancelled`. |
| WAYLAND-0059 | Tablets | ⬜ | Investigate `zwp_tablet_manager_v2` against the contract. |
| WAYLAND-0060 | Input-device enumeration | ⬜ | Seats' keyboards/pointers/touch plus evdev controllers. |

### Phase G — data exchange

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| WAYLAND-0070 | Clipboard | ✅ | D-21; to and from an external client; UTF-8; ≥ 512 KiB and multi-megabyte; source cancelled; offer replaced. D-21. `AnotherProgramsClipboardIsReadWhenFocused`, `ALargeClipboardArrivesWhole` (24 MiB), `OurClipboardIsServedToOtherProgramsWhileWeKeepRunning` (3 MiB served from the game loop), `APasteTargetThatGivesUpDoesNotHurtUs`, `LosingTheSelectionForgetsWhatWeOffered`. Defect found and fixed: a paste target that closed its pipe early raised SIGPIPE and would have killed the game; writes now block SIGPIPE for the call and consume it. Interop with an independent client on GNOME: `WaylandMutter.TheClipboardIsSharedWithAnIndependentWaylandClient`. |
| WAYLAND-0071 | Clipboard formats | ✅ | `GetMimeTypes`/`GetData`/`SetData`. `BinaryClipboardDataRoundTripsUnderItsOwnMimeType`; text aliases (`WaylandMimeTypes`). |
| WAYLAND-0072 | Primary selection | ✅ | Separate from the clipboard. `ThePrimarySelectionIsASeparateSelection`. |
| WAYLAND-0073 | Drag and drop | ✅ | Target side: files (`text/uri-list`) and text; enter/motion/leave/drop/finish. Copy-only target; drop read outside the listener (no re-entrant dispatch). `DroppedFilesArriveAsLocalPathsAndTheDropIsFinished`, `ADragThatLeavesDropsNothing`. |

### Phase H — graphics

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| WAYLAND-0080 | EGL/OpenGL context | 🟨 | D-23; versions, profiles, debug, swap interval, resize, destroy. EGL on the Wayland platform, loaded at run time; `cna_demo_2d` with `CNA_GRAPHICS_RENDERER=OPENGL33` runs on Weston (GL renderer) and on the real GNOME compositor: OpenGL 4.6 core, Mesa 25.0.7. Open: harness checks (versions, profiles, readback, resize, swap interval) and radeonsi confirmation (WAYLAND-0121). |
| WAYLAND-0081 | Vulkan surface | 🟨 | D-24. `VK_KHR_wayland_surface` through the caller's loader; `cna_demo_2d` with `VULKAN` runs on Weston and on GNOME (AMD Radeon 780M, RADV PHOENIX). Open: swapchain/resize checks and validation layers (WAYLAND-0122). |
| WAYLAND-0082 | `wl_shm` presenter | ✅ | D-25. XRGB8888 memfd buffers, ≤ 3 with release tracking, black bars, damage_buffer. `ThePresenterMapsTheWindowWithABufferOfItsPixelSize` (centre pixel checked), every scaling test; `cna_demo_2d` with `SOFTWARE` on Weston and GNOME. `BuffersTheCompositorHoldsAreNeverDrawnInto`: with every buffer held, a frame waits its bound and is dropped, never drawn over a buffer the compositor may be reading. |
| WAYLAND-0083 | Frame callbacks | ✅ | Pacing for the presenter and EGL vsync. Frame pacer on a private queue with a 100 ms bound, used by the presenter and EGL. `WithheldFrameCallbacksSlowThePresenterButNeverStallIt`; live pacing on Weston. Defect found and fixed: on a timed-out wait the pacer dropped its callback and asked for a new one every frame, so a game running unseen (minimized, behind the lock screen) piled one pending callback per frame into the compositor for as long as it ran; the pending callback is now kept and waited on again. |
| WAYLAND-0084 | `wp_presentation` | ✅ | Investigated and decided: D-30, not used and not bound; the presenter and EGL pace on `wl_surface.frame` with a bound. |

### Phase I — desktop integration and shared Linux services

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| WAYLAND-0090 | Decorations | 🟨 | D-22. D-22: title bar and resize border of CNA's own where there is no `zxdg_decoration_manager_v1` (GNOME); geometry includes the bar (`AShownWindowIsConfigured…`: geometry y −32, height +32; fullscreen drops it). `TheTitleBarsCloseButtonClosesAndTheGameSeesNoClick`, `DraggingTheTitleBarMovesAndDoubleClickingMaximizes` (move/menu with the serial of the press still held -- the test compositor now checks Mutter's implicit-grab rule), `TheBorderResizesFromTheEdgeUnderThePointer` (edge 4, ew-resize cursor), `WhereTheCompositorDecoratesThereIsNoTitleBarOfOurOwn`, `ACompositorThatRefusesToDecorateGetsOurTitleBar`; under GNOME `CnasOwnTitleBarClosesAndMaximizesUnderGnome`. Open: the real desktop at 125 %. |
| WAYLAND-0091 | Desktop portal | 🟨 | File dialogs and OpenUrl, parent through `xdg-foreign`. Portal file dialogs parented through `xdg-foreign` ("wayland:<handle>"); none in tests (no session bus). Open: private-bus portal test. |
| WAYLAND-0092 | Idle inhibition | ✅ | D-26. `zwp_idle_inhibitor_v1` per window while the screen saver is disabled, the session-bus inhibition without the protocol. `DisablingTheScreenSaverInhibitsIdleOnEveryWindow`. |
| WAYLAND-0093 | `xdg-activation` | ✅ | Investigate; a new window's focus request with the last input serial; the launcher's `XDG_ACTIVATION_TOKEN`. Defect found: `RequestActivation` was never called. Now `Show()` asks: the launcher's `XDG_ACTIVATION_TOKEN` once (then unset), else a token for the latest input serial. `TheLaunchersActivationTokenIsSpentOnTheFirstWindowOnly`. |
| WAYLAND-0094 | Message box | ✅ | Decided: D-29, `messageBox = false`, `ShowMessageBox`/`ShowMessageBoxWithButtons` throw `PlatformNotSupportedException(MessageBox)`; documented in `docs/platform-wayland.md` (Message boxes). |
| WAYLAND-0095 | Gamepads, joysticks, haptics, power | ⬜ | The shared Linux services, same behaviour as X11. |
| WAYLAND-0096 | Audio orthogonality | ✅ | ALSA with a Wayland window; no audio code in `src/Wayland/`. No audio identifier appears anywhere in `src/Wayland/` (grep for `snd_`, `alsa`, `SDL_`, `MIX_`). `cna_demo_sound` (ALSA platform, CNA's own mixer, the null device so nothing is audible) ran a 30-second session on private Weston with the `SOFTWARE` renderer -- window, presenter and mixer together -- with no error. |

### Phase J — capability matrix

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| WAYLAND-0100 | Truthful capability matrix | 🟨 | §3, each entry justified in `ComputeCapabilities` and tested. `ComputeCapabilities` names what backs each flag; `EachMissingOptionalProtocolTurnsOffExactlyItsCapability`. Open: §3 matrix review against the final state. |

### Phase K — tests and environments

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| WAYLAND-0110 | `CnaWaylandMappingTests` | ✅ | No compositor. `WaylandMappingTests.cpp` (version negotiation, scale decisions and rounding, toplevel states and configure sizes, output content scale, title-bar hit testing and resize edges, Linux button codes, wheel 120ths, XKB modifiers incl. AltGr on a `us,cz` keymap, committable text, text-input byte offsets, text MIME aliases, SIGPIPE-free transfer writes, sealed shm files) and `WaylandTestEnvironment.cpp` (WAYLAND_DISPLAY and the session bus pointed at nothing for every test of a Wayland build unless a private launcher names its compositor). ctest `CnaWaylandMappingTests` also runs the shared `Xkb*`, `Freedesktop*` and `Linux*` suites. |
| WAYLAND-0111 | In-process test compositor and `CnaWaylandProtocolTests` | ✅ | libwayland-server in the test binary; scripted configures, input, seat/output hot-plug, clipboard owner loss, protocol-error detection. `WaylandTestCompositor.{hpp,cpp}`: libwayland-server on its own thread in the test binary, reached through a socketpair in WAYLAND_SOCKET; every request of every interface the backend uses implemented and checked against its protocol's error rules (xdg-shell incl. Mutter's constrained-geometry rule, viewporter, fractional-scale, subcompositor, seat, pointer constraints, cursor-shape, data device, primary selection, text-input-v3, xdg-decoration, xdg-activation, idle-inhibit, xdg-foreign). Scripted configures, input, output and global hot-plug, selections and drags from "another program", disconnection. `WaylandProtocolTests.cpp`: 52 cases; every test fails on any protocol error or serial misuse in its traffic. ctest `CnaWaylandProtocolTests` 3.6 s. |
| WAYLAND-0112 | Weston launcher and `CnaWaylandWestonTests` | ✅ | `tools/platform/wayland_test_server.sh`: private runtime dir, readiness, teardown, no leftovers. `tools/platform/wayland_test_server.sh`: a private runtime directory (`mktemp`, 0700), readiness by socket, teardown of every process and the directory on exit, 77 where the compositor is missing; `WAYLAND_DISPLAY`/`CNA_WAYLAND_TEST_DISPLAY` name only the private socket, the session bus nothing. The unpacked Weston's shell client is found beside it. `WaylandLiveTests.cpp` + the conformance suite's Wayland instances, three ctest entries: `CnaWaylandWestonTests` (pixman: EGL runs on llvmpipe over wl_shm), `CnaWaylandWestonGpuTests` (`--renderer gl`: `GL_RENDERER AMD Radeon 780M (radeonsi, phoenix …)`, Vulkan surface presentable on RADV), `CnaWaylandWestonScaledTests` (output scale 2: a high-DPI window is 1600 px wide at 800 logical). 35 cases each, one conformance skip that is correct (surface presentation is supported). 30 vsync-paced presents take ~0.76 s: paced by Weston's frame callbacks, neither free-running nor timing out. |
| WAYLAND-0113 | Headless gnome-shell launcher and `CnaWaylandMutterTests` | ✅ | Private bus, RemoteDesktop input. Launcher mode `--compositor mutter`: a private `dbus-daemon` with no service directories, `gnome-shell --headless --wayland --no-x11 --sm-disable --virtual-monitor 1920x1080` with every XDG directory private and `GSETTINGS_BACKEND=keyfile` (optional `--layout`), readiness by socket then by the RemoteDesktop object; `CNA_WAYLAND_TEST_BUS` names the private bus. Proven by hand (registry of GNOME 48 listed through it). Open: the Mutter suite with RemoteDesktop input. `WaylandMutterTests.cpp` (suite `WaylandMutter`) with real input from `org.gnome.Mutter.RemoteDesktop` on the private bus, through the platform module's own dlopen'd libdbus: GNOME focuses the new window and its keys, snapshot state, a pointer that appears at run time, clicks, discrete wheel, relative mode with GNOME's lock (exactly 40,−20 read back unaccelerated), CNA's own title bar under GNOME (close button closes, the click is not the game's), fullscreen covering the monitor, the Czech layout incl. AltGr (`--layout cz`), and clipboard both ways with wl-clipboard as an independent client (Czech text in; 200 kB out, served from the game loop while wl-paste reads). ctest `CnaWaylandMutterTests` (also runs `WaylandLive.*` and the conformance instances under GNOME) and `CnaWaylandMutterCzechTests`, both green twice in a row. Four environment facts learned and handled in the launcher/fixture, none a backend defect: the shell's readiness is its "GNOME Shell started" log line, not its socket; it starts in the Activities overview (closed through `OverviewActive` on the private bus); a fresh profile gets the modal Welcome dialog (`welcome-dialog-last-shown-version`); RemoteDesktop's virtual keyboard and pointer exist only after their first event and deliver only later ones (primed before the platform starts). GNOME's focus-stealing prevention does not focus a second client window without an activation token; the fixture clicks it, as a user would. |
| WAYLAND-0114 | Real-desktop validation harness | ✅ | `cna_wayland_desktop_validation`. `tools/platform/wayland_desktop_validation/` (`cna_wayland_desktop_validation`, `cmake/Harnesses.cmake`): scenarios `info`, `lifecycle`, `scale`, `presenter`, `gl`, `vulkan` (port of the X11 harness's, `--validation`), `lifetime`, `stress`, `soak`, `clipboard` (only with `--replace-my-clipboard`) and `interactive` (a person's keys, mouse, IME and relative mode, echoed). No input is ever injected. Every scenario green on private Weston (GL renderer) and headless GNOME. The `stress` scenario found a defect on Weston: toggling the title bar of a maximized window moved its window geometry by 32 units -- `xdg_wm_base.invalid_surface_state`, the connection lost; fixed (`SetBorderless` on a constrained window keeps the configured geometry and resizes the content) with protocol case `TogglingTheTitleBarOfAMaximizedWindowKeepsItsGeometryExact`; 3000 random operations are then clean on Weston and on GNOME. |
| WAYLAND-0115 | SDL-free and X11-free source scan, ratchet | ✅ | `WaylandIsSdlFree`; `src/Wayland/` on the ratchet's denylist. `WaylandIsSdlFreeTests.cpp`: compile-time sentinels for SDL and Xlib/xcb headers, and a comment-stripped scan of `src/Wayland/` and the shared `Linux/ Xkb/ Freedesktop/ Posix/ Common/` for SDL and X11 code and includes (the one X header named in shared code, `<X11/keysym.h>` as an `#elif` fallback in XkbKeyMapping.cpp, is asserted never to be compiled in a Wayland build). `nonproduction_sdl_audit` classifies it as a containment assertion; the ratchet has denylisted `src/Wayland/` since Phase B. |
| WAYLAND-0116 | Sanitizers | ✅ | ASan + LSan (+ UBSan) over every Wayland suite. `build-asan` reconfigured to `CNA_PLATFORM=WAYLAND` (`CNA_SANITIZE=address,undefined`, `CNA_PLATFORM_CTEST_BINARY=CnaPlatformModuleTests`). Clean under ASan + UBSan + LSan: 137 compositor-free cases; the live suites on Weston (pixman and GL renderer) and headless GNOME (35, 35, 42 cases); and the harness (`lifecycle` ten times on GNOME, `scale`, `presenter`, `gl`, `vulkan --validation`, `lifetime --validation`, `stress --ops 2000`). Two defects found by the sanitizer runs and fixed: a use-after-free in the test compositor (a fractional-scale object outliving its surface during client teardown) and, from ASan's slowness on GNOME, the stale-configure race below. Vulkan under a sanitizer selects lavapipe, because the RADV ICD leaks 2 × 128 bytes in `vkCreateInstance` (the driver finding already recorded for X11); the harness skips its resident-size check in an ASan build, where the quarantine holds freed memory and LeakSanitizer is the leak check. |
| WAYLAND-0117 | Stress and soak | ✅ | ≥ 10,000 window/state operations, lock/unlock, fullscreen, resize, clipboard, GL/Vulkan/shm lifetimes. 3000 random window operations clean on Weston and on headless GNOME, and 2000 under ASan; `soak` runs a rendered session with state changes. Open: the ≥ 10,000-operation run and a long soak on the real desktop. 12,000 random window operations (create, destroy, resize, maximize, restore, fullscreen toggle, title, hide/show, borderless -- about 1,200 of each) on private Weston with its GL renderer: no protocol error, descriptors exact, RSS +8 MB. A 300-second soak at 37 fps (11,403 frames, 38 state changes): no protocol error, descriptors exact, RSS +21 MB and flat by the end. 2,000 operations also clean under ASan. |

### Phase L — validation and regression

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| WAYLAND-0120 | Real GNOME desktop validation | 🟨 | Run 2026-09-16 on the real GNOME 48 session (`WAYLAND_DISPLAY=wayland-0`, session locked -- windows only, no input): built-in eDP-1 1920×1200 at 125 % (logical 1536×960, `wl_output.scale` 2, xdg-output content scale 1.25). `info`, `lifecycle` (maximize 1536×896 under the top bar, fullscreen reported borderless at 1536×960, hide/show, borderless, 3 more windows), `scale` (high-DPI 800×600 → 1000×750 pixels at 1.25; a plain window unscaled), `presenter`, `gl`, `vulkan --validation`, `lifetime --validation`, `stress --ops 1000`: all pass, 0 protocol errors. Behind the lock screen GNOME sends no frame callbacks and no `wl_surface.enter`: vsync falls back to the 100 ms bound (~10 fps) by design, and the window's output is unknown (one SKIP). Open: an unlocked run with frame callbacks, a second monitor, the interactive scenario by a person. |
| WAYLAND-0121 | Hardware EGL/OpenGL | ✅ | radeonsi, not llvmpipe. On the real GNOME session and on Weston's GL renderer: `GL_RENDERER AMD Radeon 780M (radeonsi, phoenix, LLVM 19.1.7, DRM 3.61)`, `GL_VERSION 4.6 (Core Profile) Mesa 25.0.7`; contexts 4.6 core, 3.3 core, 2.1 compatibility, ES 3.0 and ES 2.0 granted; clear read back exactly; resize reaches the EGL surface before the next frame; a high-DPI GL window's drawable is 1000×750 at 1.25; swap interval 1 paced, 0 free-running. |
| WAYLAND-0122 | Hardware Vulkan + validation layers | ✅ | RADV, zero CNA-caused validation errors. `vulkan --validation` on the real GNOME session: `AMD Radeon 780M (RADV PHOENIX)`, driver 25.0.7, API 1.4; surface presentable, 3-image FIFO swapchain 800×600, 100 frames at 62 fps, resize to 1100×700 recreates and presents (centre read back), `VK_LAYER_KHRONOS_validation`: 0 errors, 0 warnings; `lifetime --validation` 10 surface/swapchain lifetimes clean. Same on Weston's GL renderer (300 frames). |
| WAYLAND-0123 | SDL-free proof | ✅ | `CNA_ENABLE_SDL=OFF`; `ldd`, `readelf -d`. `cmake/Tests/WaylandLinkClosure.cmake`, ctest `CnaWaylandLinkClosure`: no executable NEEDs an SDL, X11, xcb or GLX library, none NEEDs what the platform opens at run time (EGL, wayland-egl, wayland-cursor, libdbus), `libwayland-server` is in the test binary only, `libcna_platform.a` references no SDL/Xlib/xcb/GLX symbol, and the platform-only executable's whole closure is `libwayland-client`, `libxkbcommon`, `libffi` and the C/C++ runtime. Checked against an X11 binary to be sure it can fail. A full game additionally links what its engine modules use; Debian's FFmpeg brings libX11 through VA-API/VDPAU/cairo, which `-DCNA_ENABLE_VIDEO=OFF` drops -- recorded rather than hidden. |
| WAYLAND-0124 | A real CNA application | ⬜ | |
| WAYLAND-0125 | SDL3 regression | ✅ | `cmake-build-debug` (the default `CNA_PLATFORM=SDL3` configuration) rebuilt clean and its platform suites pass: `CnaPlatformTests`, `CnaPlatformWindowTests`, `CnaPlatformXErrorHandlerTests`, `CnaSdl*` -- 6/6 ctest entries, 0 failed. |
| WAYLAND-0126 | X11 regression | ✅ | `cmake-build-x11` rebuilt clean; `ctest -R "CnaX11|CnaPlatform"` 10/10 (8 passed, 2 skipped without their tools), and the window-manager pair passes with `~/deps/openbox`. Re-run after every Phase that touched shared code. |
| WAYLAND-0127 | Win32 cross-build regression | ✅ | `cmake-build-win32` (mingw-w64 cross-build, `CNA_PLATFORM=WIN32`) builds clean, and `cna_platform_tests.exe` runs under Wine: 388 passed, 1 correct skip. Defect found and fixed here: `tools/platform/standalone_tests/CMakeLists.txt` filtered `X11*.cpp` but not the Wayland suites, nor the shared `Xkb*/Freedesktop*/Posix*/Linux*` suites that Phase B renamed out of the X11 prefix -- so a WIN32 cross-build compiled tests whose headers that toolchain has nothing to find. |
| WAYLAND-0128 | Broad platform matrix and gates | 🟨 | Gates: `sdl_inventory` (the §2 table regenerated: one more test file mentions SDL, the Wayland containment assertion), `sdl_classify`, `renderer_sdl_audit`, `sdl_ratchet` (0 files, 0 references), `hot_path_lint`, `nonproduction_sdl_audit`, `check_contract` -- all exit 0. Selections built and tested: WAYLAND, X11, SDL3, WIN32 (cross + Wine). Open: SDL2, HEADLESS and TERMINAL configure/build smoke. |
| WAYLAND-0129 | Error audit | ⬜ | |
| WAYLAND-0130 | `docs/platform-wayland.md` and final audit | 🟨 | `docs/platform-wayland.md` written: selection, dependency table, capability boundary, windows and the built-in title bar, scaling, keyboard/text/mouse/touch, selections and drag and drop, the three graphics bridges, desktop integration, the message-box decision, error behaviour, how to run every suite and the harness, sanitizers, and how to prove SDL and X11 are gone. `CLAUDE.md` names `WAYLAND` in the platform axis and the boundary rules. Open: the final audit and report. |

---

## 5. Findings that are not this backend's

None yet.

## 6. Defects this work found in its own implementation

None yet.

## 7. Evidence log

### Baseline

Recorded in §0.

---

## 8. Risks and limitations

| Risk / limitation | Kind | Handling |
|---|---|---|
| No top-level positioning, no global pointer, no warping, no exclusive display mode, no un-minimize | Wayland protocol model | D-10, D-19, D-11, D-12; documented, not faked |
| GNOME offers no server-side decorations | compositor | built-in frame, D-22 |
| Weston headless has no seat | test environment | input suites run on headless gnome-shell and the in-process compositor |
| Only one physical desktop compositor (GNOME) available | environment | Weston and gnome-shell headless for the rest; KDE/sway unvalidated |
| No touchscreen, no tablet on this machine | hardware | protocol-level tests only |
