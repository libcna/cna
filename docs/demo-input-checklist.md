# `demo_input` — Manual Input Verification Checklist

> **Related input docs (INP-0003):** [plan](../plan_input.md) · [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual results](input-manual-verification-results.md) · [demo checklist](demo-input-checklist.md)

`examples/demo_input` (`cna_demo_input`) is the interactive input demo. The unit suite
(`CnaTests`) exercises the input pipeline headlessly, but some behavior can only be confirmed with
real hardware and a real window — that's what this checklist is for (INPUT-TEST-018 / the manual-verification tasks in plan_input.md). Work through it on
each platform you care about; pair it with the platform-specific caveats in
[`docs/platform-input-notes.md`](platform-input-notes.md).

## Build & run

```bash
git submodule update --init --recursive           # first time only
cmake -S . -B cmake-build-debug -G Ninja -DCNA_GRAPHICS_BACKEND=EASYGL
cmake --build cmake-build-debug --target cna_demo_input -j"$(nproc)"
./cmake-build-debug/cna_demo_input                 # needs a display
```

Global controls: **F1** toggles text input on/off; **Esc** exits.

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

### Touch (touch-capable display)
- [ ] Each finger down shows a marker at the correct position.
- [ ] Multiple simultaneous touches each show a marker.
- [ ] Markers track finger movement and disappear on release.

### GamePad (up to 4 controllers)
- [ ] Connecting a controller flips its panel to "connected"; disconnecting flips it back.
- [ ] A/B/X/Y, both shoulders, DPad, Start/Back, and stick-click all light up.
- [ ] Left/right triggers move their bars proportionally (analog).
- [ ] Thumbsticks move their visualizers; direction and magnitude look correct.
- [ ] **Rumble:** pulling a trigger vibrates that controller (the demo calls `SetVibration` every
      frame from the trigger values); the magenta rumble bar mirrors the trigger.
- [ ] Plugging in a **second/third/fourth** controller populates the compact panels in order.

## Not exercised by the current demo (verify separately)

These input features are implemented and unit-tested where possible, but the demo does not
surface them. Verifying them needs a small demo enhancement or a separate harness:

- **Relative mouse mode** (`Mouse::IsRelativeMouseModeEXT`) — not toggled by the demo. Covered by
  `MouseInputTests` (real-window round-trip) and the manual check INPUT-MOUSE-023 in `plan_input.md`.
- **Mouse cursor warp** (`Mouse::SetPosition`) — not called by the demo. Manually verified in
  INPUT-MOUSE-023 (pixel-exact under X11); the scaled/letterboxed logical→window conversion is now
  implemented (INPUT-MOUSE-002 (decision a-0001)) and unit-tested (`SetPositionConvertsLogicalToWindowFor
  LetterboxedRenderer`).
- **Gamepad sensors** (`GetGyroEXT` / `GetAccelerometerEXT`) — not read by the demo. Only the
  disconnected/zeroed fallback is unit-tested (INPUT-GAMEPAD-017/018); live values need real sensor hardware.
- **Gamepad light bar** (`SetLightBarEXT`) — not driven by the demo.
- **Touch *gestures*** (Tap/DoubleTap/Hold/Flick/Drag/Pinch via `TouchPanel::ReadGesture`) — the demo
  **enables** `Tap | FreeDrag | Flick` (`setEnabledGesturesProperty`) and pumps `TouchPanel::Update()`
  each frame, but shows raw touch points only and never calls `ReadGesture`, so recognized gestures are
  not surfaced. A future enhancement only needs to drain + display the gesture queue. Gesture recognition
  itself is covered by `GestureDetectorTests` (deterministic) and `SdlInputBridgeTouchGestureTests`.
