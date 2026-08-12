# `demo_input` — Manual Input Verification Checklist

> **Related input docs (INP-0003):** [plan](../plan_input.md) · [renderer](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual results](input-manual-verification-results.md) · [demo checklist](demo-input-checklist.md)

`modules/input/examples/demo_input` (`cna_demo_input`) is the interactive input demo. The unit suite
(`CnaTests`) exercises the input pipeline headlessly, but some behavior can only be confirmed with
real hardware and a real window — that's what this checklist is for (INPUT-TEST-018 / the manual-verification tasks in plan_input.md). Work through it on
each platform you care about; pair it with the platform-specific caveats in
[`docs/platform-input-notes.md`](platform-input-notes.md).

## Build & run

```bash
git submodule update --init --recursive           # first time only
cmake -S . -B cmake-build-debug -G Ninja -DCNA_GRAPHICS_RENDERER=OPENGLES3
cmake --build cmake-build-debug --target cna_demo_input -j"$(nproc)"
./cmake-build-debug/cna_demo_input                 # needs a display
```

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
(INP-0220), and gamepad light-bar cycling + gyro/accelerometer readout (INP-0221). The demo builds and
smoke-launches crash-free (2026-07-06, EasyGL/Xvfb). **Their visual/behavioral correctness on real
hardware is still human/hardware-gated** — see `input-manual-verification-results.md`.

## Still requires separate verification

- **Real hardware actuation** — a real controller's light bar physically changing color, live gyro/accel
  values responding to motion, real rumble/trigger-haptics, and a real touchscreen's gestures. The demo
  now surfaces them, but confirming the *physical* result needs the hardware (INP-0231..0242).
- **Gestures beyond Tap/FreeDrag/Flick** — the demo enables that subset; DoubleTap/Hold/H-V drag/Pinch are
  covered by `GestureDetectorTests` (deterministic) and `SdlInputBridgeTouchGestureTests`, not the demo UI.
- **Cursor-warp landing pixel on native Wayland** — X11/XWayland only (compositor policy); see platform notes.
- **`CNA::Input::Joysticks`/`Sensors`/`Power` (P7-039, found 2026-07-17)** — `cna_demo_input` does not
  currently surface the standalone raw-joystick API (distinct from `GamePad`'s mapped view of the same
  hardware), device-level `Sensors` (distinct from gamepad-attached gyro/accel, already in the checklist
  above), or `Power` (battery query for any input device). There is therefore no manual-checklist item to
  physically exercise for these three, and no Phase 11 task names them (`plan_input.md`'s P11-001..015
  cover Keyboard/Mouse/GamePad/Touch/high-DPI only). Their current verification tier is unit tests against
  canned platform services (`JoystickTests.cpp`, `SensorsTests.cpp`, `PowerTests.cpp` — real,
  substantial coverage, just not hardware-gated). Extending the demo UI to surface them is a legitimate
  follow-up but is out of this audit/documentation plan's scope (CLAUDE.md: no new features beyond audit/
  repair/test/doc); recorded here so the gap is explicit rather than silently unverifiable.
