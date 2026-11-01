# Platform-specific input behavior

This page describes current CNA paths and their evidence limits, reconciled on 2026-11-01.
SDL3 is the default graphical provider; HEADLESS and TERMINAL implement different capability
subsets. A source branch or canned test does not qualify every OS, compositor or physical device.

See [input architecture](input-backend.md), [FNA decisions](input-fna-fidelity.md),
[member parity](input-member-parity-matrix.md), [build/test](input-build-and-test.md) and
[manual results](input-manual-verification-results.md).

## Desktop pointer behavior

Sdl3Mouse deliberately distinguishes native video drivers:

| Driver/target | Current CNA behavior and evidence boundary |
|---|---|
| x11, windows, cocoa | Snapshot position/buttons use global desktop state converted to window coordinates. CNA suppresses SDL warp-to-relative emulation unless the host already set that hint. Historical X11/XWayland landing checks do not qualify native Windows/macOS behavior. |
| wayland | Uses window-relative input and a virtual warp anchor. SetPosition can produce the requested XNA coordinate without moving the visible OS cursor. Historical Task 783 observed global readback as zero on its compositor; this is not a universal numeric result for all future drivers. |
| Emscripten | Also enables the virtual warp anchor; browser pointer-lock and canvas policy still control native capture/motion. |
| Other drivers | Use the service's ordinary window-relative path; no blanket global-pointer or warp guarantee follows. |

SetPosition maps renderer logical game coordinates through the registered window transform,
including scale and letterbox offsets, into platform logical client units. It is a no-op in
relative mode. Entering relative mode clears the virtual anchor and old relative displacement.
Mouse.GetState in relative mode consumes accumulated displacement; absolute state reads do not.

The SDL3 global-position wrapper currently returns success whenever video is initialized; it
does not detect whether the compositor supplied a useful global position. Do not interpret a
successful TryGetGlobalPosition as proof of desktop-coordinate availability on Wayland.
Capture/global-warp status also depends on the native operation.

MouseCursor stock objects are lazy platform-neutral descriptions, not persistent SDL handles.
The selected IPlatformMouse creates and retains the active native cursor when applied. Creating a description
before video startup is valid; applying it still depends on service/video/cursor support.
There is no owner-independent native cursor to carry across a platform switch.

## Keyboard layouts and text

FNA_KEYBOARD_USE_SCANCODES=1 selects physical-position bindings; the environment choice is cached,
with explicit internal test overrides. Default keycode mode follows layout-produced symbols.
Native SDL values are translated to CNA KeyCode/Scancode at the edge, then to XNA Keys.

XNA Keys cannot represent arbitrary composed Unicode. CNA drops unmapped keycodes rather than
adding Keys::None (DEC-16); selected Nordic/OEM fallbacks remain in the mapping table.
GetKeyFromScancodeEXT and key/scancode name helpers use the selected keyboard service. There
are XNA values with no physical scancode; do not assume this conversion is bijective.

Use TextInputEXT for committed Unicode/composition rather than reconstructing text from
Keyboard.GetState. Software keyboard availability, visibility, IME presentation and physical
keyboard attachment are provider/OS questions. Android/iOS can have attached keyboards/mice;
neither platform should be described as inherently touch-only.

## Touch and mobile

TouchPanel.GetCapabilities queries IPlatformInputDevices.HasDevice(Touch) on each call, then uses
the sticky TouchDeviceExists flag and live event touches as fallbacks. It may report connected
before first interaction if enumeration already knows the device; first touch is not a universal
Android prerequisite. The compatibility maximum is four when connected, independent of the
larger internal tracked-touch capacity.

Touch events carry their client size for normalized-to-window conversion before the renderer
transform. High-density/native canvas dimensions must not be confused with logical client units.
Mobile focus/background reaction belongs to Game's lifecycle policy; it is not a second input
event queue. See [renderer/event boundary](platform-renderer-boundary.md).

## Browser

Input uses SDL3's Emscripten driver and the same PlatformEvent visitor, regardless of whether
graphics selects WEBGL2, WEBGPU or a CPU path. CNA also has deliberate Emscripten branches such
as virtual mouse warps; the old claim of no browser-specific input behavior was too broad.

- Browsers can reserve keyboard shortcuts; a missing DOM key event cannot be reconstructed by CNA.
- Pointer lock has user-engagement and browser escape rules. Relative mode requests can be
  refused or revoked; see [MDN Pointer Lock](https://developer.mozilla.org/en-US/docs/Web/API/Pointer_Lock_API).
- An already-connected gamepad can become visible only after button/axis interaction, depending
  on browser policy/focus. This is not a universal requirement to press a button on every
  connection; see [MDN Gamepad API](https://developer.mozilla.org/en-US/docs/Web/API/Gamepad_API/Using_the_Gamepad_API).
- Controller rumble, LEDs, sensors, touchpads, IME and multitouch require their actual browser/
  device capability. Contract presence and native desktop tests do not qualify them.
- CNA's exception ABI/link policy is shared with the browser build; see
  [browser platform limits](web-emscripten-graphics-limitations.md). Do not add conflicting local exception flags.

## Gamepads, joysticks, haptics and sensors

SDL3 supplies native controller mappings; CNA does not maintain a duplicate device database.
Use SDL3's SDL_AddGamepadMappingsFromFile or SDL_GAMECONTROLLERCONFIG at the SDL integration
edge when needed, not the removed SDL2 GameController spelling. Sdl3GamepadControls translates
the resulting standard controls; unmapped hardware may still be usable through raw joysticks.

GetGUIDEXT emits xinput when vendor and product are both zero, otherwise a little-endian
vendor/product hex string, with Valve model overrides for Xbox/PS4/PS5. This formatting rule is
not restricted to Windows and does not prove that zero IDs uniquely identify an XInput device.
Canned GUID tests qualify formatting, not Steam Input routing or native actuation.

Mapped gamepads and raw joysticks publish independent snapshots. Startup is lazy on first
controller query; subsequent Game pumps update both once the shared subsystem is initialized.
Repeated raw reads preserve that publication's trackball deltas, unlike consuming relative mouse
reads. DeviceId, not enumeration position, is the haptic/joystick correlation identity.

Both simple and rich haptics are platform-owned; a HapticDevice retains its client subsystem
reference until disposal. SDL3's retained discovery reference keeps native IDs alive but does
not retain every client effect. Host sensor polling opens/reads/closes per call, whereas
platform SensorEvent streams and Microsoft::Devices sessions have separate contracts.
See [seam mapping](platform-input-seam-mapping.md) and [SDL lifecycle](platform-sdl-lifecycle-audit.md).

## Qualification boundary

Native event pumping and mutable input/session operations follow the selected platform's thread
rules; public snapshot APIs do not grant blanket cross-thread safety. Synthetic input tests,
private X11/Wayland runs and manual physical-device runs are different evidence classes.
Apple CI now exists, but its build/bundle smoke is not a complete macOS/iOS input qualification.
Windows/macOS landing, physical relative capture, mobile keyboards and multi-vendor controller/
sensor/haptic behavior require the dated manual evidence recorded by the input/device checklists.
