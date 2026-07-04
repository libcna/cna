# Platform-specific input notes

CNA's input runs on SDL3, so most platform differences are SDL's, surfaced through the XNA-style
API. This page collects the ones that affect observable input behavior. Items marked **verified**
were confirmed directly during the input work (see the cited task); the rest are documented SDL3 /
OS platform behavior that CNA inherits.

See also [`docs/input-backend.md`](input-backend.md) (architecture) and
[`docs/demo-input-checklist.md`](demo-input-checklist.md) (manual verification).

---

## Linux / X11

- **Mouse warp & global position work.** `Mouse::SetPosition` (`SDL_WarpMouseInWindow`) is
  pixel-exact, and `SDL_GetGlobalMouseState` returns real coordinates. **Verified** in task 783
  (under `SDL_VIDEODRIVER=x11`, incl. XWayland: `SetPosition(x,y)` warps to exactly
  `windowPos + (x,y)`, and relative mode genuinely no-ops the warp).
- Keyboard, text input/IME, gamepad hotplug/rumble, and touch (on touch-capable X inputs) all
  behave as the SDL3 backend provides.

## Wayland

- **Global cursor position is restricted.** `SDL_GetGlobalMouseState` silently returns `(0, 0)`
  regardless of real cursor movement — Wayland's compositor security model forbids querying the
  global pointer position outside your own surface. **Verified** in task 783 (this environment's
  ambient session; forcing `SDL_VIDEODRIVER=x11`/XWayland restores real values). This is a Wayland
  platform restriction, **not** a CNA bug. CNA's `Mouse::GetState()` reports *window-local* logical
  coordinates from motion events, which are unaffected.
- **Absolute cursor warp is constrained.** Wayland only lets an application move the pointer under
  specific conditions (e.g. pointer lock / relative mode, or a focused surface). `SetPosition` may
  therefore be a no-op or clamped when the surface isn't focused. **Relative mouse mode**
  (`IsRelativeMouseModeEXT`, backed by `SDL_SetWindowRelativeMouseMode` → Wayland pointer lock)
  works and is the correct approach for FPS-style mouse-look on Wayland.

## Windows

- **XInput controllers report no USB vendor/product**, so `GamePad::GetGUIDEXT` returns the literal
  `"xinput"` (vs. an 8-hex `vendor+product` string on Linux, where the real IDs are exposed).
  **Verified** by the GUID-format fix/tests in task 816, which mirror FNA's own `xinput`/hex/Valve
  logic.
- Mouse warp, global position, scroll wheel, text input/IME (including the Windows IME composition
  window), and gamepad rumble/trigger-rumble/light-bar are all provided by the SDL3 Windows backend.

## Android

- **Touch is the primary input.** The touch device is only reported as connected *after the first
  touch* — CNA sets `TouchPanel::TouchDeviceExists` on the first `SDL_EVENT_FINGER_DOWN`, matching
  FNA's "Windows/Android only notices a touch screen once it's touched" comment (task 712). So
  `TouchPanel::GetCapabilities().IsConnected` may be false until the user first touches the screen.
- **Keyboard is the on-screen (software) keyboard.** `TextInputEXT::StartTextInput` /
  `StopTextInput` show/hide it; `IsScreenKeyboardShown` reports its state. There is generally no
  physical keyboard, so raw `Keyboard::GetState()` key coverage is limited to what the OS delivers.
- A diagnostic keyboard-event log path exists behind `#ifdef __ANDROID__` in `SdlInputBridge`
  (audited in task 822: it only logs via `SDL_Log`, and is compiled out entirely off-Android — no
  behavior change on other platforms).
- Mouse and gamepad support depend on attached hardware and the SDL3 Android backend.

## iOS

- **Touch-only, no system cursor.** Like Android, touch is primary and there is no mouse cursor, so
  `Mouse::SetPosition` / warp and global cursor position are not meaningful.
- **On-screen keyboard** via `StartTextInput`/`StopTextInput`; `IsScreenKeyboardShown` reports it.
- Gamepad support (MFi / Bluetooth controllers) depends on the SDL3 iOS backend.

---

## Cross-cutting

- **`SetPosition` on a scaled/letterboxed window** (all platforms): `Mouse::SetPosition` converts
  the caller's logical coordinates to window space (via `SDL_RenderCoordinatesToWindow` for the
  SDL_Renderer backend, or `IGraphicsBackend::TransformLogicalToWindow` for EasyGL; pass-through for
  Vulkan/bgfx, which don't do logical-presentation scaling) before `SDL_WarpMouseInWindow`, so the
  OS cursor lands at the correct physical pixel (implemented in `plan.md` a-0001 / task 846). The
  conversion is unit-tested; the OS-cursor *landing* pixel is verifiable only where global-mouse
  readback works (X11, not Wayland — see the Wayland section).
- **Keyboard layouts** (all platforms): `Keyboard::GetKeyFromScancodeEXT` translates a physical key
  position to the character the *current* layout produces. Set `FNA_KEYBOARD_USE_SCANCODES=1`
  (read once at startup) for layout-independent physical-position key bindings. 40 XNA `Keys`
  (IME/browser/media/ChatPad/OEM) have no SDL scancode and cannot map — see the list in
  `SdlInputBridge.cpp`'s `try_convert_keys_to_sdl_scancode` (task 819).
- **No horizontal scroll wheel** (all platforms): XNA 4.0 / this FNA `MouseState` expose only the
  vertical `ScrollWheelValue`; `event.wheel.x` is intentionally dropped (task 805).
- **Input is main-thread only** (all platforms): SDL requires event pumping on the video/window
  thread; see [`docs/input-backend.md`](input-backend.md) §6.
