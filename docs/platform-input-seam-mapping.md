# The input backend seam, mapped onto the platform contract

*plans/plan_platform.md Task PLAT-77. Written before migration and updated after PLAT-77c/PLAT-82;
historical snippets below show the seam that was removed.*

`modules/input` already has an abstraction layer. It is not the platform contract, it was not
built for platform selection, and it is 48 files — the largest remaining block of direct SDL
coupling in the repository. Migrating input without first deciding what happens to that existing
layer would produce two parallel seams doing the same job, which is the failure mode this
document exists to prevent.

This is a map of what is there, what each part becomes, and — for four of the eight backends —
why it stops existing entirely.

---

## 1. What the pre-migration seam actually was

Eight backends lived under `modules/input/src/Internal/`, every one of them with the same shape:

```cpp
class ISdlGamepadBackend
{
public:
    virtual ~ISdlGamepadBackend() = default;
    virtual bool IsGamepad(SDL_JoystickID instanceId) = 0;
    // …
};

ISdlGamepadBackend& sdl_gamepad_backend();
void SetSdlGamepadBackendForTests(ISdlGamepadBackend* backend);
```

with, in the `.cpp`:

```cpp
RealSdlGamepadBackend g_realBackend;
ISdlGamepadBackend*   g_currentBackend = &g_realBackend;
```

Three facts follow, and all three matter:

- **The vtable indirection already exists.** Every call site already goes through a virtual call
  to a swappable pointer. Re-pointing those pointers at platform services costs nothing at the
  call sites — the dispatch is already paid for.
- **Selection is runtime, through a mutable global, and its only current purpose is test
  injection.** There is no compile-time backend variance anywhere to preserve:
  `modules/input/CMakeLists.txt` globs `src/*.cpp` unconditionally and links `SDL3::SDL3`, and the
  only preprocessor conditionals in `src/Internal/` are `#ifdef __ANDROID__` guards around
  *logging*. Nothing has to be kept working across a configuration this migration would change.
- **Two of the most important pieces are not behind the seam at all.** `InputManager` and
  `SdlInputBridge` are concrete classes of static methods, called directly by name.

---

## 2. Per-backend disposition

| Backend | Disposition | Why |
|---|---|---|
| `SystemPowerBackend` | **Deleted** | Duplicates `IPlatformSystemInfo::GetPowerInfo()`, which `modules/devices-ext` already uses. Its whole surface is one method wrapping one SDL call. |
| `SystemSensorBackend` | **Deleted** | Duplicates `IPlatformSensors` (PLAT-85). Its SDL↔EXT enum mapping is already isolated in the `.cpp`, and `SensorKind` is the same mapping done once in the platform. |
| `SystemKeyboardBackend` | **Deleted** | One method, `GetModState()`. `KeyboardSnapshot::modifiers` already carries it, computed once per frame instead of on demand. |
| `SystemMouseBackend` | **Deleted after a contract addition** | `CaptureMouse` / `GetGlobalMouseState` / `WarpMouseGlobal` have **no counterpart** in `IPlatformMouse`, which is window-scoped throughout. See §4. |
| `SystemDeviceBackend` | **Deleted after a contract addition** | `GetMice` / `GetKeyboards` / `GetTouchDevices` have no counterpart. See §4. |
| `SdlJoystickBackend` | **Deleted; replaced by `IPlatformJoystick`** | PLAT-83 moved arbitrary raw axes/buttons/hats/balls into frame snapshots, while `Sdl3Joystick` alone owns native handles. The same physical controller can still appear independently through the mapped gamepad view. |
| `SdlGamepadBackend` | **Replaced** by `IPlatformGamepad`, extended | 33 methods against `IPlatformGamepad`'s 5. See §3 — most of the gap is real capability, not redundancy. |
| `SdlHapticBackend` | **Narrowed; rich effects survive** | PLAT-84 moved enumeration and standalone rumble to `IPlatformHaptics`; the seam retains only explicit rich-effect handles plus joystick/mouse correlation. See §5. |

Four deletions, two replacements, one extension, one survivor. The four deletions are the point:
this migration should end with *fewer* abstractions than it started with, not the same number
wearing new names.

`InputManager` remains a pure in-process compatibility store that makes **no SDL calls at all** —
the only SDL symbol it reaches at runtime is an `SDL_Log` inside an `#ifdef __ANDROID__` diagnostic,
and its header has zero SDL symbols. PLAT-82 removed mapped-gamepad state and PLAT-86 removed its
final public-device store, touch. It now retains only keyboard/mouse accumulators used by text/click
synthesis and legacy raw-adapter tests; public state belongs to platform snapshots or `TouchPanel`.

---

## 3. Where `IPlatformGamepad` is genuinely short

`SdlGamepadBackend` exposed 33 methods while the original `IPlatformGamepad` had 5. PLAT-77c carried
the real capability gap into the contract before PLAT-82 deleted that backend:

- **Identity beyond a name** — path, serial, firmware version, Steam handle, connection state,
  vendor/product IDs. `GamePadCapabilities` and `CNA::Input::Joysticks` surface these to games.
- **Touchpads** — count, finger count, per-finger state. DualShock/DualSense controllers.
- **Motion sensors on the pad itself** — enable/disable plus data, which is *not*
  `IPlatformSensors`: those are the device's own sensors, these are a specific controller's.
- **Outputs beyond rumble** — trigger rumble and the LED light bar.
- **Player index** — get and set, which is how slot assignment survives a hotplug.

Those additions are now explicit CNA-owned enums/records and refusable methods on
`IPlatformGamepad`; no SDL type crosses the contract.

---

## 4. Two capabilities the contract has nowhere to put

Both are small, both are load-bearing, and both were missed when the contract was drafted because
they are not window-scoped:

**Global-space pointer control.** `Mouse::SetCaptureEXT`, `Mouse::GetGlobalPositionEXT` and
`Mouse::WarpGlobalEXT` work in desktop coordinates, across windows, and are what makes a drag
survive the pointer leaving the window. `IPlatformMouse` is window-scoped throughout —
`SetPosition` takes an `IPlatformWindow&` — and cannot express any of the three. It needs a
capture call, a global-space warp and a global position read, or an explicit decision that CNA
drops the capability. It should not make that decision silently.

**Input device enumeration.** `CNA::Input::InputDevicesEXT` lists connected mice, keyboards and
touch devices by id and name. `TouchPanel::GetCapabilities` queries the touch-device list on
*every call*, matching FNA — with a sticky flag and a panel-owned live-state peek as fallbacks for
platforms that only enumerate a touchscreen after the first interaction. Nothing in `IPlatform`
enumerates input devices: `DeviceEvent` reports *changes*, but there is no way to ask for the
current set. A platform that starts with a touchscreen already attached and never fires an add
event therefore cannot answer "is there a touchscreen?" at all — only the two fallbacks would,
and they were written as fallbacks, not as the answer.

---

## 5. Haptics: one platform contract, one device

`IPlatformHaptics` (PLAT-84) enumerates stable `DeviceId`s and owns both simple rumble and rich
force feedback. An opened `IPlatformHapticDevice` is an independently owned session whose effect
descriptor uses CNA enums, fixed-width magnitudes, millisecond durations and owned custom samples.
That preserves create/update/run/stop/destroy, effect status, gain, autocenter, pause/resume and
feature/capacity queries without putting an SDL union in the contract. `Sdl3Haptics` performs the
only native conversion and owns handle teardown; the former `SdlHapticBackend` is deleted.

---

## 6. The two things that must be done together

**`SdlInputBridge` was the whole event-migration job.** It was the only consumer that both fanned
out to three seams
(58 gamepad calls, 13 joystick, 1 keyboard) *and* bypasses them with direct SDL calls of its own:
subsystem init/quit, window and coordinate mapping, gamepad capability properties, keyboard
name/scancode translation. It also owns every mapping table — `SDL_SCANCODE_*` ↔ XNA `Keys`,
`SDL_GAMEPAD_BUTTON_*`, `SDL_GAMEPAD_AXIS_*`, `SDL_JOYSTICK_TYPE_*`, `SDL_POWERSTATE_*`,
`SDL_KMOD_*`, `SDL_HAT_*`. PLAT-78 moved the event-state behavior to
`PlatformInputBridge::ProcessEvent(const PlatformEvent&)`; PLAT-47 then moved the single production
caller in `Game.cpp` to `IPlatform::PollEvents(batch)`. The raw SDL overload now exists only as a
compatibility adapter for legacy tests.

That signature was why **PLAT-47 depended on PLAT-78**: running raw and platform loops together
would have drained one native queue twice. The completed path maps once, drains once, and feeds
every event to the input bridge before the runtime handles quit/focus/resize/lifecycle behavior.

**The raw SDL handle handoff was removed with PLAT-83.** `Haptics::OpenFromJoystickEXT` now checks
the platform-neutral `IPlatformJoystick` using its `DeviceId`, then gives only that id to the
surviving full-effect haptic seam. The real SDL haptic backend opens its own joystick reference and
keeps it associated with the returned haptic handle. `CloseHaptic` closes the haptic first and that
joystick reference second, exactly as SDL requires; neither `SdlInputBridge` nor a public/platform
header exposes `SDL_Joystick*`. This had to land in the joystick migration because deleting the old
handle store first would otherwise have broken `OpenFromJoystickEXT` between commits.

---

## 7. Order of work

1. Contract additions first (§3, §4) — each as its own task, each refusable by a second
   implementation.
2. `SdlInputBridge` → `PlatformEvent` (**PLAT-78**, complete), followed by `Game`'s platform-batch
   event loop (**PLAT-47**, complete).
3. The four deletions (§2), each re-pointing its XNA-side caller at the platform service.
4. `SdlGamepadBackend` replacement (**complete, PLAT-82**), followed by the distinct
   `SdlJoystickBackend` replacement (**complete, PLAT-83**).
5. Standalone haptics → `IPlatformHaptics` (**complete, PLAT-84**); the remaining rich effect model
   stays behind its narrowed seam pending its own design decision.

The two renderer examples that call `InputManager::SetKeyState` directly
(`modules/renderers/easygl/examples/`, `modules/renderers/sdl-renderer/examples/`) use it as a
synthetic-input injection seam and are unaffected: PLAT-86 removed only the independent touch store.
