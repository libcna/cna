# Platform-specific input notes

> **Related input docs (INP-0003):** [plan](../plan_input.md) · [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual results](input-manual-verification-results.md) · [demo checklist](demo-input-checklist.md)

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

## macOS

- **Mouse warp & global position work** through the SDL3 Cocoa backend, like X11 — `Mouse::SetPosition`
  (`SDL_WarpMouseInWindow`) and `SDL_GetGlobalMouseState` return real values, so warp-landing readback is
  feasible (unlike Wayland). Relative mouse mode uses Cocoa's associated-cursor / warp suppression.
- **System cursors** map to the OS theme glyphs; exact pixels are chosen by macOS (as on every platform).
- Not headless-verifiable in CI here (the CI matrix is Linux); treat macOS specifics as manual-gated.

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

## Browser / Emscripten (WebAssembly)

CNA builds for Emscripten (EasyGL backend = WebGL 2 / OpenGL ES 3.0). Input flows through SDL3's
Emscripten backend, which maps browser DOM events into the same `PlatformInputBridge::ProcessEvent`
path used everywhere else, so no browser-specific input code exists in CNA. Browser-specific behavior:

- **Exceptions are enabled.** Several input paths throw (`TouchCollection`/`TouchPanel::SetFinger` →
  `std::out_of_range`, `TouchPanel::ReadGesture` → `System::InvalidOperationException`). Emscripten disables
  C++ exception catching by default (an uncaught throw aborts the whole runtime), so CNA compiles/links with
  `-fexceptions -sNO_DISABLE_EXCEPTION_CATCHING=1` (`CMakeLists.txt`) — these input exceptions unwind
  normally instead of aborting the page.
- **Keyboard:** the browser reserves some key combos (Ctrl+W/T/N, some F-keys) that never reach the app;
  keycode vs scancode behaves as elsewhere, but the physical layout depends on the browser/OS.
- **Mouse:** relative mouse mode maps to the Pointer Lock API, which **requires a user gesture** to engage
  (a click) and can be exited by the browser (Esc); `Mouse::SetPosition`/warp is limited by pointer-lock
  rules. Wheel deltas still normalize to the XNA 120-unit notch.
- **Touch:** browser touch events map to `FINGER_DOWN/MOTION/UP`; multi-touch works on touch-capable
  devices. High-DPI touch scaling depends on the canvas CSS size vs backing store (device pixel ratio).
- **GamePad:** via the browser Gamepad API — controllers are **not visible until the user presses a button**
  on them (a browser privacy gate), so `GAMEPAD_ADDED` may arrive late; rumble/LED/sensor support depends on
  the browser and is typically absent.
- **Text/IME:** SDL routes composition through a hidden DOM input; `StartTextInput`/`StopTextInput` toggle it.

---

## Cross-cutting

- **`SetPosition` on a scaled window** (all platforms): `Mouse::SetPosition` converts the caller's
  logical coordinates to window space before `SDL_WarpMouseInWindow` (INPUT-MOUSE-002 (decision a-0001)).
  Two paths, both correct for their scaling model:
  - **SDL_Renderer** — `SDL_RenderCoordinatesToWindow`, which is **offset-aware**, so a true
    letterbox (centering bars) maps correctly, not just scaled (verified with a non-square 200×100
    window in task 858).
  - **EasyGL** — `IGraphicsRenderer::TransformLogicalToWindow`, a uniform height-scale with **no
    offset**. That is exact for EasyGL's default `FixedHeightDynamicWidth` presentation, which fixes
    the logical height and derives the logical *width* from the window aspect — so the viewport
    fills the window and there are no bars to offset. EasyGL does **not** implement true
    letterbox-with-bars for input; it doesn't need to for this model.
  - **Vulkan / bgfx** — pass-through (no logical-presentation scaling).

  The conversion is unit-tested; the OS-cursor *landing* pixel is verifiable only where global-mouse
  readback works (X11, not Wayland — see the Wayland section).
- **Keyboard layouts** (all platforms): `Keyboard::GetKeyFromScancodeEXT` translates a physical key
  position to the character the *current* layout produces. Set `FNA_KEYBOARD_USE_SCANCODES=1`
  (read once at startup) for layout-independent physical-position key bindings. 40 XNA `Keys`
  (IME/browser/media/ChatPad/OEM) have no SDL scancode and cannot map — see the list in
  `SdlInputBridge.cpp`'s `try_convert_keys_to_sdl_scancode` (task 819).

### Non-US keyboard layouts (INPUT-KBD-014)

CNA's key mapping mirrors FNA exactly (verified byte-for-byte: INPUT-KBD-009 keycodes, INPUT-KBD-010
scancodes). How a non-US layout behaves depends on the mode:

- **Keycode mode (default).** SDL delivers the keycode = the symbol the key produces on the *active*
  layout, so the mapping is inherently layout-dependent. A key that produces an ASCII letter/digit/OEM
  symbol maps to the matching XNA `Keys`. On a German QWERTZ board the physical `Z` position produces
  `y`, so it reports `Keys::Y` (you get the letter on the keycap, not the US position).
- **Scancode mode (`FNA_KEYBOARD_USE_SCANCODES=1`).** The physical position maps to the US-equivalent
  XNA `Keys` regardless of layout, so bindings stay put across layouts.

**Mapping gap — accented / non-ASCII keys have no XNA `Keys`.** XNA's `Keys` enum is US-centric, so any
key whose keycode is a non-ASCII Unicode codepoint has no XNA value. In keycode mode CNA **drops** such a
key (it never enters the pressed set — the DEC-16 policy; FNA maps it to `Keys.None` and adds it, which
CNA deliberately avoids). Confirmed dropped: German `ä ö ü ß`, French `é è à ç`, Czech `ě š č`. This is
a fundamental XNA limitation, not a CNA bug — games needing these must read `TextInputEXT` (which delivers
the composed Unicode text), not `Keyboard`. Exceptions: a few Nordic keys whose *physical position* is a
US OEM key still map to that OEM key (e.g. the codepoints for `æ`/`ø` resolve to `Keys::OemQuotes` /
`Keys::OemSemicolon`), matching FNA. Tested by `NonUsLayoutAccentedKeysAreUnmappedInKeycodeMode`.
- **Horizontal scroll wheel is a CNAEXT/EXT extension** (all platforms): XNA 4.0 `MouseState` exposes
  only the vertical `ScrollWheelValue`, so `event.wheel.x` has no strict-XNA property to route to.
  N-005 (2026-07-17/P1-018) added `MouseState::getHorizontalScrollWheelValueEXTProperty()`: `wheel.x`
  is scaled to the same 120-unit notch and surfaced there, deliberately excluded from
  `Equals`/`GetHashCode`/`ToString`/`==`/`!=` so those stay byte-identical to FNA. See DEC-18 in
  `docs/input-fna-fidelity.md`'s Mouse section.
- **Input is main-thread only** (all platforms): SDL requires event pumping on the video/window
  thread; see [`docs/input-backend.md`](input-backend.md) §6.
- **Cursor creation needs `SDL_INIT_VIDEO`** (INPUT-MOUSE-020, all platforms): stock cursors are lazy
  function-local statics (Meyer's singleton) built on first access — deliberately *not* static-init time,
  so `SDL_CreateSystemCursor` runs after `SDL_Init(SDL_INIT_VIDEO)`. Without a video subsystem SDL returns
  a null cursor handle; CNA wraps it gracefully (the handle is null, `Mouse::SetCursor` becomes a no-op,
  no crash), and the headless cursor tests `GTEST_SKIP` (INPUT-BUILD-008) rather than fail. Under the SDL
  `dummy` video driver real cursors cannot be created either — CI runs them under Xvfb+x11.

### Cursor & warp caveats — platform matrix (INPUT-MOUSE-022)

| Platform | `SetPosition` warp | Global pos (`SDL_GetGlobalMouseState`) | Warp-landing readback | Relative mode |
|----------|--------------------|----------------------------------------|-----------------------|---------------|
| Linux / X11 (incl. XWayland) | ✅ works | ✅ real values | ✅ testable | ✅ warp no-op |
| Linux / Wayland | ⚠️ focus-gated / clamped | ❌ returns `(0,0)` | ❌ not readable | ✅ pointer-lock |
| Windows | ✅ works | ✅ real values | ✅ (manual) | ✅ |
| macOS | ✅ works | ✅ real values | ✅ (manual) | ✅ |
| Android / iOS | n/a (touch, no cursor) | n/a | n/a | n/a |

Legend: ✅ works / ❌ unavailable / ⚠️ constrained. The X11 row is the only one machine-verified in CI
(Xvfb+x11); Windows/macOS warp-landing and all real-hardware relative-capture are **manual-only**
(INPUT-MOUSE-023 tracks the dated manual run). See the per-platform sections above for the details behind
each cell.

### Gamepad backend & mapping (INPUT-GAMEPAD-031/033/036/037)

- **SDL is the mapping authority (INPUT-GAMEPAD-033).** CNA consumes SDL3's `SDL_Gamepad` abstraction, so
  the physical device → standard-layout (A/B/X/Y, DPad, two sticks, two triggers, shoulders, start/back,
  guide) normalization is done by SDL's bundled **`gamecontrollerdb`** mapping table plus SDL's built-in
  entries. CNA does **not** ship or parse its own mapping DB; a device SDL cannot map is simply not
  reported as a gamepad. `SDL_GameControllerAddMappingsFromFile`/env (`SDL_GAMECONTROLLERCONFIG`) can add
  mappings at the SDL layer without any CNA change. `Sdl3GamepadControls` is the single CNA-owned
  SDL-layout translation point. Its native axis/button mapping is pinned by
  `Sdl3EventMapperTests.GamepadAxesMapToCnaVocabularyAndBridgeNumerics` and
  `EverySupportedGamepadButtonMapsToCnaVocabulary`; `GamePadMappingTest.EveryButtonMapsToItsXnaFlag`
  then pins the platform-to-XNA boundary through whole canned snapshots.
- **Joystick type → `GamePadType` (INPUT-GAMEPAD-031).** When a device first occupies a stable player
  slot, `Sdl3Gamepad` caches its `SDL_JoystickType` as `GamepadKind`; public `GamePad` maps that CNA-owned
  enum to XNA's `GamePadType`. `GamepadDeviceMetadataUsesOnlyContractEnums` pins the SDL edge and
  `GamePadPlatformTest.CapabilitiesMapEveryCategoryWithoutNativeQueries` pins the public edge.
- **Steam Input / virtual controllers (INPUT-GAMEPAD-036).** Valve controllers report the Steam vendor id
  `0x28de`; matching FNA, CNA remaps the re-exposed controller to a fixed GUID (`xinput` for Xbox-emulated,
  `4c05c405`/`4c05e60c` for PS4/PS5 — the public `GamePad` GUID path, mirroring
  `SDL3_FNAPlatform.cs:2193-2210`).
  Steam Input commonly presents a **virtual Xbox 360 controller**; the real device is then hidden behind
  it, so CNA sees the virtual pad (this is expected, not a bug). Verified without native hardware by
  `GamePadPlatformTest.GuidFormattingCoversXinputHexPaddingAndValveOverrides`.
- **Per-platform specifics (INPUT-GAMEPAD-037)** are in the platform sections above: Linux/X11 hotplug+rumble,
  Windows XInput `"xinput"` GUID + rumble/trigger-rumble/light-bar, Android/iOS depend on attached hardware /
  the SDL backend. Real-hardware actuation across vendors (Xbox/PS/Switch/generic/BT) is manual-gated
  (INPUT-GAMEPAD-035).

### Raw joystick snapshots (PLAT-83)

- **Raw and mapped views remain distinct.** A wheel/HOTAS can expose arbitrary device-ordered axes,
  buttons, POV hats and trackballs through `IPlatformJoystick` even when SDL also maps the same
  hardware into a four-slot `IPlatformGamepad`. Neither view derives its state from the other.
- **One publish per frame.** `Game` acquires the shared gamepad/joystick subsystem before frame one,
  drains native events, then calls `IPlatformJoystick::Update()` once. Public `Joysticks::GetStateEXT`
  therefore cannot consume a trackball delta twice or observe axes from a different native instant
  than buttons. Trackball deltas are zero when SDL reports no motion during that publication.
- **Hotplug identity is platform-owned.** `JoystickInfo::id`, `DeviceEvent::device` and the haptic
  joystick correlation use the same `DeviceId`. SDL3 maps its instance id at the native edge; no
  public or platform header exposes `SDL_JoystickID` or `SDL_Joystick*`.

### Standalone haptics (PLAT-84)

- **Stable ids, not enumeration positions.** `IPlatformHaptics::GetHaptics()` returns ascending
  `DeviceId` descriptors. Cached rumble handles are keyed by that id and retired when enumeration
  no longer contains it, so removing device A cannot make A's cached handle masquerade as the new
  device at index zero.
- **The simple subset is platform-owned.** Standalone enumeration, rumble capability,
  initialization, play and stop cross `IPlatformHaptics`. `HapticDevice` keeps the selected
  platform's haptic-subsystem reference until `Dispose`, and the SDL3 platform closes its cached
  handles before releasing the final subsystem reference.
- **The rich effect model is preserved deliberately.** SDL3 can still create/update/run arbitrary
  constant, periodic, condition, ramp, left-right and custom effects through the internal
  `SdlHapticBackend`. That seam no longer enumerates devices or handles standalone rumble, and the
  public `HapticDevice` header no longer declares `SDL_Haptic`.

### Host motion sensors (PLAT-85)

- **Static reads keep the old lifetime shape.** `CNA::Input::Sensors` acquires
  `PlatformSubsystem::Sensor`, opens the selected `IPlatformSensors` kind, reads, closes it and
  releases the subsystem on every call. It does not cache a service pointer or started flag across
  calls; either would outlive the ambient platform that supplied it.
- **Teardown order is explicit.** A started sensor closes before its subsystem reference is
  released. SDL3 also deactivates any cached service handles before the platform's last sensor
  reference reaches zero, so destruction cannot close a stale native pointer.
- **Polling and events are separate contracts.** Public accelerometer/gyroscope helpers stay
  on-demand. `SensorEvent` independently preserves an event stream's stable id, six values and
  sensor-clock timestamp for general platform consumers; the input bridge intentionally ignores it.
