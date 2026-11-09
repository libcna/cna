# `demo_input` — Manual Input Verification Checklist

> **Related input docs (INP-0003):** [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual hardware checks](demo-input-checklist.md#hardware-verification-record) · [demo checklist](demo-input-checklist.md)

`modules/input/examples/demo_input` (`cna_demo_input`) is the interactive input demo. The unit suite
(`CnaTests`) exercises the input pipeline headlessly, but some behavior can only be confirmed with
real hardware and a real window — that's what this checklist is for (INPUT-TEST-018 and the P11-001..015 manual-verification checks; results go in [Hardware verification record](#hardware-verification-record)). Work through it on
each platform you care about; pair it with the platform-specific caveats in
[`docs/platform-input-notes.md`](platform-input-notes.md).

## Build & run

```bash
git submodule update --init                       # first time only
cmake -S . -B cmake-build-debug -G Ninja -DCNA_GRAPHICS_RENDERER=OPENGLES3
cmake --build cmake-build-debug --target cna_demo_input --parallel 8

# Bounded startup smoke on a private display; timeout normally exits with status 124.
tools/platform/run_gpu_tests_private.sh --exec \
  timeout 5s ./cmake-build-debug/cna_demo_input
```

Automated runs must use the private runner, never the owner's live desktop. The bounded command
checks startup only; it does not perform the interactive checklist or establish hardware parity.
For the checks below, a human tester needs an isolated interactive test session or a dedicated
device with the relevant peripherals. Record that environment and the observed results; the
private headless smoke does not supply physical input or visual confirmation.

Global controls: **F1** toggles text input on/off; **F2** toggles relative mouse mode; **F3** warps
the cursor to the window centre; **Esc** exits.

## What the demo shows

| Panel | Location | Shows |
|-------|----------|-------|
| Keyboard | top-left | On-screen keyboard; pressed keys highlight |
| Mouse | left, below keyboard | Cursor position, L/M/R + X buttons, scroll-wheel indicator |
| GamePad — Player One | center | Full pad: connection, DPad, ABXY, shoulders, trigger bars, rumble bar, sticks |
| GamePad — Players Two/Three/Four | right column | Compact pads (same fields, condensed) |
| Touch points | overlay | A marker at each active touch position |
| Text panel | bottom | Committed text, IME composition draft, last code unit as 8 bit-LEDs, blinking caret |

## Checklist

### Keyboard
- [ ] Pressing a key highlights the matching on-screen key; releasing clears it.
- [ ] Multiple keys held at once all highlight (no ghosting beyond the hardware's own limits).
- [ ] Modifier keys (Shift/Ctrl/Alt, left and right) highlight independently.
- [ ] Function keys, arrows, numpad, and OEM keys (`,` `.` `;` etc.) all register.

### Text input & IME
- [ ] With text input **on** (F1), typing appends characters to the text buffer.
- [ ] Accented/non-Latin characters (é, ñ, €, CJK) appear correctly (multi-byte UTF-8 → UTF-16).
- [ ] An emoji (astral code point) appears/handles without corrupting the buffer.
- [ ] Backspace deletes the last character; Enter clears the line.
- [ ] With an IME active (e.g. Japanese/Chinese/Korean), the **composition draft** shows in the
      IME line before commit, and commits into the buffer.
- [ ] The 8 bit-LEDs reflect the low byte of the most recent code unit.
- [ ] Pressing F1 to turn text input **off** stops character accumulation.

### Mouse
- [ ] Moving the mouse updates the reported position.
- [ ] Left/Middle/Right and X1/X2 buttons light up when pressed.
- [ ] Scrolling the wheel moves the scroll indicator (cumulative value).
- [ ] On a letterboxed/scaled window, the reported position matches the logical render position.
- [ ] **F2** toggles relative mouse mode — the mouse-panel relative indicator turns green; motion is
      captured (pointer-lock), and `SetPosition` becomes a no-op while active (INP-0219).
- [ ] **F3** warps the cursor toward the window centre — the warp indicator flashes yellow (INP-0219).

### Touch (touch-capable display)
- [ ] Each finger down shows a marker at the correct position.
- [ ] Multiple simultaneous touches each show a marker.
- [ ] Markers track finger movement and disappear on release.
- [ ] A **Tap / FreeDrag / Flick** lights the gesture-readout cells in the mouse panel (via
      `ReadGesture`) — one cell per set `GestureType` bit, flashing when recognized (INP-0220).

### GamePad (up to 4 controllers)
- [ ] Connecting a controller flips its panel to "connected"; disconnecting flips it back.
- [ ] A/B/X/Y, both shoulders, DPad, Start/Back, and stick-click all light up.
- [ ] Left/right triggers move their bars proportionally (analog).
- [ ] Thumbsticks move their visualizers; direction and magnitude look correct.
- [ ] **Rumble:** pulling a trigger vibrates that controller (the demo calls `SetVibration` every
      frame from the trigger values); the magenta rumble bar mirrors the trigger.
- [ ] Plugging in a **second/third/fourth** controller populates the compact panels in order.
- [ ] **Light bar** (PS4/PS5): the Player One light bar cycles color each frame (`SetLightBarEXT`); the
      on-screen swatch mirrors the color being sent (INP-0221).
- [ ] **Motion sensors** (gyro/accelerometer): the two sensor bars in the Player One panel move with the
      controller's motion (`GetGyroEXT`/`GetAccelerometerEXT`); grey when the pad lacks sensors (INP-0221).

## Now exercised by the demo (INP-0219/0220/0221)

The demo was extended to **exercise** these EXT paths end-to-end (they are in the checklist above):
relative mouse mode + cursor warp (F2/F3, INP-0219), recognized-gesture readout via `ReadGesture`
(INP-0220), and gamepad light-bar cycling + gyro/accelerometer readout (INP-0221). **Their
visual/behavioral correctness on real hardware is human/hardware-gated** — see
[Hardware verification record](#hardware-verification-record).

## Still requires separate verification

- **Real hardware actuation** — a real controller's light bar physically changing color, live gyro/accel
  values responding to motion, real rumble/trigger-haptics, and a real touchscreen's gestures. The demo
  now surfaces them, but confirming the *physical* result needs the hardware (INP-0231..0242).
- **Gestures beyond Tap/FreeDrag/Flick** — the demo enables that subset; DoubleTap/Hold/H-V drag/Pinch are
  covered by `GestureDetectorTests` and `PlatformInputBridgeTouchGestureTest`, not the demo UI.
- **Cursor-warp landing pixel on native Wayland** — X11/XWayland only (compositor policy); see platform notes.
- **`CNA::Input::Joysticks`/`Sensors`/`Power` (P7-039)** — `cna_demo_input` does not surface the
  standalone raw-joystick API (distinct from `GamePad`'s mapped view of the same hardware),
  device-level `Sensors` (distinct from gamepad-attached gyro/accel, already in the checklist above),
  or `Power` (battery query for any input device), and P11-001..015 cover only
  Keyboard/Mouse/GamePad/Touch/high-DPI. Their verification tier is unit tests against canned
  platform services (`JoystickTests.cpp`, `SensorsTests.cpp`, `PowerTests.cpp`), not hardware.
  Surfacing them in the demo UI would be a new feature; the gap is recorded here so it is explicit.
- **`CNA::Input::Haptics`/`HapticDevice`** — the same holds for standalone force feedback. No example
  opens a haptic device, so the effect families and gain/autocenter/pause/resume are verified only
  against a canned platform service (`SdlHapticBackendTests.cpp`); actuation on a real force-feedback
  wheel or joystick has not been checked.

## Hardware verification record

The headless suites prove the input pipeline's translation and bookkeeping; they cannot prove
that a physical device acts. These checks are implemented in code but **not verified on real
hardware** until a dated run below says so: gamepad rumble, trigger haptics, light bar, gyro and
accelerometer, hotplug, analog input and button mapping on real controllers, `GetGUIDEXT` on a
real controller, real IME composition, touch hardware, four simultaneous controllers, and the
OS-cursor landing point of `Mouse::SetPosition` on native Wayland (`SDL_GetGlobalMouseState`
returns (0,0) there; the conversion is unit-tested and the warp is pixel-exact under X11).

### Hardware verification matrix

Status: ✅ verified · ⬜ not yet verified · n/a. Fill a cell only from a real run on that family, and
add a dated entry under *Recorded runs* below. All cells are ⬜: no run on real hardware has been recorded.

| Controller family | Buttons/DPad | Analog sticks/triggers | Rumble | Trigger haptics | Light bar | Gyro/Accel | GUID | Hotplug |
|-------------------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Xbox (XInput) | ⬜ | ⬜ | ⬜ | ⬜ | n/a | n/a | ⬜ (`xinput`) | ⬜ |
| PlayStation (DS4/DualSense) | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ (hex) | ⬜ |
| Nintendo Switch Pro | ⬜ | ⬜ | ⬜ | n/a | n/a | ⬜ | ⬜ | ⬜ |
| Generic / DirectInput | ⬜ | ⬜ | ⬜ | n/a | n/a | n/a | ⬜ | ⬜ |
| Bluetooth (any) | ⬜ | ⬜ | ⬜ | — | — | — | ⬜ | ⬜ (pair) |

| Other hardware | Multi-touch | Gestures | IME composition | Non-US layout |
|----------------|:---:|:---:|:---:|:---:|
| Touchscreen | ⬜ | ⬜ | — | — |
| IME (JP/CN/KR) | — | — | ⬜ | — |
| Czech keyboard | — | — | — | ⬜ |

| Other check (P11) | Status | What a run must show |
|-----------------------------------------|:---:|----------------------|
| Non-QWERTY layout, e.g. AZERTY or Dvorak (P11-003) | ⬜ | Keyboard input behaves correctly on that physical layout |
| Mouse with side buttons (P11-005) | ⬜ | `XButton1`/`XButton2` report presses from the real buttons |
| High-DPI display (P11-015) | ⬜ | Mouse and touch coordinates land correctly on a real high-DPI display |

### Recording a run

Add a new `#### Entry — YYYY-MM-DD` block under *Recorded runs* and fill it. Required fields per entry:
**Date · OS (kernel/distro) · Display server · Graphics renderer · Toolchain + SDL rev · Hardware present.**
For each hardware check, record **Check · Method · Result (Pass/Fail + notes)**, and update the matrix
cell(s). Never mark a cell ✅ without a dated row backing it.

### Supported-controllers checklist

- [ ] Xbox One / Series (XInput) — verify all 15 buttons, DPad, 2 sticks, 2 triggers, rumble, `GetGUIDEXT`=`xinput`, hotplug.
- [ ] PlayStation DualShock 4 — + light bar (`SetLightBarEXT`), trigger haptics off, gyro/accel.
- [ ] PlayStation DualSense — + adaptive-trigger haptics (`SetTriggerVibrationEXT`), light bar, sensors.
- [ ] Nintendo Switch Pro — button remap (SDL mapping), gyro.
- [ ] Generic / DirectInput pad — mapping via SDL `gamecontrollerdb`; unmapped devices not reported.
- [ ] Bluetooth controller — pairing + hotplug add/remove.

### Supported-OS checklist

- [ ] Linux / X11 (or Xvfb) — cursor warp landing, relative capture, global mouse position. *(automated subset green here)*
- [ ] Linux / Wayland — virtual XNA recentering and relative mode (pointer lock);
  `SDL_GetGlobalMouseState` returns (0,0), and the visible OS cursor remains compositor-controlled.
- [ ] Windows — XInput `GetGUIDEXT`=`xinput`, IME composition window, rumble/trigger-rumble/light-bar.
- [ ] macOS — Cocoa warp + global position; relative mode.
- [ ] Android — touch primary (device seen after first touch); on-screen keyboard; attached-HW gamepad.
- [ ] iOS — touch-only, no cursor; on-screen keyboard; MFi/BT gamepad.

### Recorded runs

None yet.
