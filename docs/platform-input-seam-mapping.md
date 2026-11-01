# Input services after the platform migration

PLAT-77 mapped the former native input adapters onto CNA::Platform. The migration is complete;
the earlier proposal, raw SDL snippets and ordering rationale remain in Git history. This page
records the implemented seams, checked against source on 2026-11-01.

## Current ownership

modules/input links cna_platform rather than SDL3 directly. Its XNA/CNA wrappers consume
platform-owned enums, records, services and snapshots. Native SDL translation and handles live
under modules/platform/src/Sdl3. HEADLESS/TERMINAL can therefore supply their own supported
subset without a parallel native adapter layer inside input.

| Former input adapter | Current contract |
|---|---|
| SystemPowerBackend | IPlatformSystemInfo.GetPowerInfo |
| SystemSensorBackend | IPlatformSensors, with scoped subsystem ownership |
| SystemKeyboardBackend | IPlatformKeyboard snapshots, including modifiers |
| SystemMouseBackend | IPlatformMouse, including capture/global read/global warp |
| SystemDeviceBackend | IPlatformInputDevices enumeration/HasDevice |
| SdlJoystickBackend | IPlatformJoystick raw device snapshots |
| SdlGamepadBackend | IPlatformGamepad mapped slots, metadata and controls |
| SdlHapticBackend | IPlatformHaptics plus owned IPlatformHapticDevice sessions, including rich effects |

All eight duplicate adapters are gone. The former proposed global-pointer/device-enumeration
contract gaps are implemented, and rich haptics no longer waits behind a surviving internal
backend. Capability/status/refusal contracts still apply; a method's existence does not promise
that every platform supplies it.

## Events and public state

Game drains IPlatform.PollEvents once, sends each owning PlatformEvent to
PlatformInputBridge.ProcessEvent, then applies its runtime reaction. Adding a second raw SDL drain
would consume the same native queue twice. The old raw SDL event adapter is no longer an input
module API or the way current input tests drive the bridge.

The file is still named src/Internal/SdlInputBridge.cpp, and SdlInputBridge remains a legacy
key-name/test-control type. Both are platform-neutral: keyboard name/layout queries delegate to
IPlatformKeyboard, and there are no direct SDL calls, including Android logging, in InputManager.
Native mapping tables belong to Sdl3KeyCodes/Scancodes/Modifiers/GamepadControls/JoystickControls
and related edge helpers, not this compatibility name.

InputManager retains keyboard/mouse accumulators for bridge synthesis and internal test paths.
It is not the authoritative public device store. Public Keyboard/Mouse/GamePad/Joysticks read
platform snapshots; TouchPanel owns event-derived touch/gesture state and queries current input
device enumeration for capabilities. Synthetic InputManager writes do not guarantee that public
Keyboard.GetState will change; tests needing public state install canned platform services.

After event processing, Game advances keyboard and mouse services and its keyboard-based
extensions. Gamepad/joystick refresh is gated on the subsystem already being initialized.
The first genuine controller service query lazily attempts initialization and publishes an initial
snapshot; Game does not unconditionally pay controller startup before frame one.

## Mapped pads, raw joysticks and haptics

IPlatformGamepad carries mapped four-slot state plus identity, connection/power/category,
player assignment, touchpads, controller sensors and output controls. Controller motion sensors
are distinct from host IPlatformSensors. Native-to-contract and contract-to-XNA mappings are
tested separately; a wrapper snapshot test alone does not prove a native device mapping.

IPlatformJoystick independently publishes device-ordered axes, buttons, hats and trackballs.
The same physical controller may appear in both views. Stored trackball deltas remain available
through repeated reads of a publication; they are not drained like relative mouse displacement.

Haptic enumeration/correlation uses DeviceId. Opening from a joystick passes that identity,
not an SDL_Joystick pointer. SDL3 owns any extra native joystick reference and closes haptic
resources before that reference. An IPlatformHapticDevice session owns rich-effect state:
create/update/run/stop/destroy, status, capacity, gain, autocenter and pause/resume remain available
through CNA descriptors, including owned custom samples.

Sensor helpers open/read/close through IPlatformSensors and balance their subsystem reference;
they do not cache an ambient service across calls. The longer-lived Microsoft::Devices sensor
sessions use the same platform boundary with their own callback/session lifetime.

## Tests and related contracts

Platform edge tests cover vocabulary translation and native services; input tests use
PlatformEvent and canned snapshots. Relevant families include PlatformInputBridge*, GamePadMappingTest,
GamePadPlatformTest, Sdl3EventMapperTests and Sdl3InputTest. Missing hardware and skipped
configuration guards remain qualification limits, not completed physical input tests.

See [input architecture](input-backend.md), [platform-specific behavior](platform-input-notes.md),
[SDL lifetime](platform-sdl-lifecycle-audit.md) and [input test coverage](input-test-coverage.md).
Performance comparisons need measurement; deleting a duplicate adapter does not by itself
establish a zero-cost change.
