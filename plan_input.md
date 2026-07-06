# CNA Input XNA 4.0 Completion Plan

This plan replaces the old Input plan completely.

The goal is to make CNA's `Microsoft.Xna.Framework.Input` implementation faithful to XNA 4.0 where applicable, compatible with FNA behavior where CNA intentionally follows FNA, and clearly documented where CNA provides non-XNA extensions.

## Status legend

- `[ ]` Not started
- `[~]` In progress
- `[x]` Completed
- `[!]` Blocked or requires manual/hardware validation
- `[?]` Needs upstream/FNA/XNA verification

## Execution rules

1. Execute tasks strictly in order unless a task is explicitly blocked.
2. Do not skip a task silently.
3. For every completed task, record:
   - files changed,
   - tests added or updated,
   - command output summary,
   - behavior verified,
   - remaining risk.
4. Use XNA 4.0 and FNA as behavioral references.
5. Preserve C++ API idioms only where direct C# behavior cannot be represented.
6. Keep strict XNA behavior separate from `EXT` / `NOXNA` extensions.
7. All public headers must remain self-contained.
8. No public XNA-compatible header may expose SDL types.
9. New behavior must be tested with deterministic unit tests where possible.
10. Manual hardware validation must be recorded separately and must not replace automated tests.

---

# Phase 0 — Baseline and repository safety

## P0-001 — Record repository baseline
- [ ] Record current branch, commit hash, compiler, OS, and CMake version.
- [ ] Record whether SDL submodules are present.
- [ ] Record whether tests can be configured and built.
- [ ] Update this plan with the exact baseline.

## P0-002 — Confirm old plan was not used
- [ ] Confirm this file was overwritten from scratch.
- [ ] Do not copy any content from the previous `plan_input.md`.
- [ ] Record that the old plan was intentionally ignored.

## P0-003 — Create Input source inventory
- [ ] List all Input public headers.
- [ ] List all Input source files.
- [ ] List all internal Input backend files.
- [ ] List all Input tests.
- [ ] Add the inventory to this plan.

## P0-004 — Create build preflight notes
- [ ] Try to configure CNA with tests enabled.
- [ ] If CMake fails because of missing vendored SDL or submodules, record the exact error.
- [ ] Add a follow-up task to improve diagnostics.
- [ ] Do not mark the implementation as tested unless tests actually run.

## P0-005 — Define strict versus extension scope
- [ ] Classify every public Input type as one of:
  - strict XNA 4.0,
  - FNA-compatible extension,
  - CNA-specific extension,
  - internal-only.
- [ ] Record classification in this plan.
- [ ] Ensure extension names/comments are explicit.

---

# Phase 1 — Authoritative API parity

## P1-001 — Build XNA 4.0 Input type checklist
- [ ] Create a checklist of XNA 4.0 Input types:
  - `ButtonState`
  - `Buttons`
  - `GamePad`
  - `GamePadButtons`
  - `GamePadCapabilities`
  - `GamePadDPad`
  - `GamePadDeadZone`
  - `GamePadState`
  - `GamePadThumbSticks`
  - `GamePadTriggers`
  - `GamePadType`
  - `Keyboard`
  - `KeyboardState`
  - `Keys`
  - `KeyState`
  - `Mouse`
  - `MouseState`
  - `TouchCollection`
  - `TouchLocation`
  - `TouchLocationState`
  - `TouchPanel`
  - `TouchPanelCapabilities`
  - `GestureSample`
  - `GestureType`
- [ ] Mark each type present/missing.
- [ ] Mark each type implemented/tested/partially tested.

## P1-002 — Compare public constructors
- [ ] For every Input public type, compare constructors against XNA/FNA.
- [ ] Verify default constructor behavior.
- [ ] Verify parameter order and default values.
- [ ] Add compile-time tests for all public constructors.

## P1-003 — Compare public static methods
- [ ] Compare all public static methods against XNA/FNA.
- [ ] Verify overload count.
- [ ] Verify argument types.
- [ ] Verify return types.
- [ ] Add signature freeze tests.

## P1-004 — Compare public instance methods
- [ ] Compare all public instance methods against XNA/FNA.
- [ ] Verify constness does not break intended semantics.
- [ ] Verify equality and hash methods.
- [ ] Add tests for all methods that currently lack coverage.

## P1-005 — Compare public properties
- [ ] Compare all XNA properties to CNA property-style methods.
- [ ] Verify getter/setter availability.
- [ ] Verify read-only versus mutable behavior.
- [ ] Document C++ naming deviations.

## P1-006 — Verify enum numeric values
- [ ] Check numeric values for every `Buttons` enum value.
- [ ] Check numeric values for every `Keys` enum value.
- [ ] Check numeric values for `ButtonState`, `KeyState`, `GamePadDeadZone`, `GamePadType`, `GestureType`, and `TouchLocationState`.
- [ ] Add tests that freeze numeric values.

## P1-007 — Verify public header hygiene
- [ ] Ensure each public Input header compiles independently.
- [ ] Ensure each public Input header includes only what it needs.
- [ ] Ensure public headers do not leak SDL headers.
- [ ] Add/extend public API compile tests.

## P1-008 — Generate or update parity matrix
- [ ] Generate an Input member parity matrix.
- [ ] Mark strict XNA members.
- [ ] Mark FNA/extension members.
- [ ] Mark intentional C++ deviations.
- [ ] Commit the generated/updated matrix if the repository tracks it.

---

# Phase 2 — Keyboard correctness

## P2-001 — Audit `Keys` enum completeness
- [ ] Compare CNA `Keys` against XNA 4.0.
- [ ] Verify all common XNA key values are present.
- [ ] Record keys that cannot be mapped from SDL.
- [ ] Add comments only where useful and non-misleading.

## P2-002 — Harden invalid `Keys` handling
- [ ] Audit all code paths accepting `Keys`.
- [ ] Ensure invalid enum values cannot cause out-of-bounds access.
- [ ] Fix `KeyboardState::GetHashCode` if it indexes a fixed bit array without range checks.
- [ ] Add tests for negative, too-large, and unknown `Keys` values.

## P2-003 — Verify `KeyboardState` default behavior
- [ ] Test default state has no pressed keys.
- [ ] Test `IsKeyDown` and `IsKeyUp`.
- [ ] Test equality and inequality.
- [ ] Test hash stability.

## P2-004 — Verify `KeyboardState::GetPressedKeys`
- [ ] Verify returned keys are deterministic.
- [ ] Verify sorting order matches CNA's documented XNA-compatible policy.
- [ ] Verify duplicates are impossible.
- [ ] Add tests with multiple keys.

## P2-005 — Audit SDL keycode mapping
- [ ] Compare SDL keycode to XNA `Keys` conversion.
- [ ] Add missing mappings where SDL has reliable equivalents.
- [ ] Do not guess mappings that are platform-layout dependent.
- [ ] Document unmappable keys.

## P2-006 — Audit SDL scancode mapping
- [ ] Compare SDL scancode to XNA `Keys` conversion.
- [ ] Ensure scancode mode is deterministic for physical keyboard layout.
- [ ] Add tests for representative keys.
- [ ] Document scancode/keycode tradeoffs.

## P2-007 — Verify key repeat behavior
- [ ] Ensure key repeat does not create false press/release transitions.
- [ ] Ensure repeated keydown may still produce text input if intended.
- [ ] Add tests for repeated SDL keydown events.

## P2-008 — Verify focus loss behavior
- [ ] Determine what happens when window focus is lost.
- [ ] Ensure pressed keys are cleared or behavior is documented.
- [ ] Add SDL bridge test for focus lost if supported.
- [ ] Add manual validation task if needed.

## P2-009 — Verify modifier keys
- [ ] Test left/right Shift.
- [ ] Test left/right Control.
- [ ] Test left/right Alt.
- [ ] Test CapsLock, NumLock, and ScrollLock if supported.
- [ ] Verify no accidental merging unless XNA does so.

## P2-010 — Verify OEM keys
- [ ] Test all mapped OEM punctuation keys.
- [ ] Verify US keyboard behavior.
- [ ] Add manual validation tasks for CZ, DE, FR/AZERTY, and other layouts.
- [ ] Document layout-dependent behavior.

## P2-011 — Verify Android/browser special keys
- [ ] Verify Back/Menu behavior if CNA supports mobile/browser input.
- [ ] Ensure mappings are documented as platform-specific.
- [ ] Add tests where fake SDL events can represent these keys.

## P2-012 — Verify keyboard reset for tests/runtime
- [ ] Ensure `InputManager::ResetAllForTests` clears keyboard state.
- [ ] Ensure no stale key state leaks between tests.
- [ ] Add regression tests.

---

# Phase 3 — Mouse correctness

## P3-001 — Harden `Mouse::SetPosition`
- [ ] Audit `Mouse::SetPosition`.
- [ ] Ensure it does not call SDL with a null window.
- [ ] Ensure it updates CNA internal state consistently.
- [ ] Add tests for no-window behavior.

## P3-002 — Verify default `MouseState`
- [ ] Test default X/Y/scroll values.
- [ ] Test default buttons are released.
- [ ] Test equality and hash behavior.

## P3-003 — Verify `MouseState` hash behavior
- [ ] Compare hash behavior against FNA where practical.
- [ ] Decide whether button states should affect hash.
- [ ] Fix or document current behavior.
- [ ] Add regression tests.

## P3-004 — Verify mouse button mapping
- [ ] Test left, middle, right, XButton1, and XButton2.
- [ ] Verify unknown SDL buttons are ignored safely.
- [ ] Add SDL bridge tests.

## P3-005 — Verify mouse position updates
- [ ] Test SDL mouse motion updates X/Y.
- [ ] Test button events carrying position update X/Y.
- [ ] Test no negative coordinate crashes.
- [ ] Test large coordinate values.

## P3-006 — Verify scroll wheel behavior
- [ ] Test vertical wheel increments.
- [ ] Verify XNA-compatible 120-unit behavior.
- [ ] Decide/document behavior for horizontal wheel.
- [ ] Add tests for fractional SDL wheel values if possible.

## P3-007 — Verify relative mouse mode
- [ ] Test enabling relative mode.
- [ ] Test disabling relative mode.
- [ ] Test delta accumulation.
- [ ] Test delta drain-on-read behavior.
- [ ] Ensure behavior is documented as extension if not XNA.

## P3-008 — Verify relative mode with no window
- [ ] Ensure getter returns safe value.
- [ ] Ensure setter does not crash.
- [ ] Decide whether internal desired mode should be remembered.
- [ ] Add regression tests.

## P3-009 — Verify `Mouse::WindowHandle`
- [ ] Test setting and getting the mouse window handle.
- [ ] Ensure invalid/null handles are safe.
- [ ] Ensure public API does not expose SDL types beyond opaque pointer policy.
- [ ] Document behavior.

## P3-010 — Verify `MouseCursor`
- [ ] Test system cursor creation.
- [ ] Test null cursor handling.
- [ ] Test disposed cursor behavior.
- [ ] Test repeated `Dispose`.
- [ ] Test setting cursor before a window exists.

## P3-011 — Verify custom cursor behavior
- [ ] Audit custom cursor image format expectations.
- [ ] Validate hotspot/origin behavior.
- [ ] Add tests for invalid dimensions if possible.
- [ ] Document platform limitations.

## P3-012 — Verify mouse reset
- [ ] Ensure reset clears buttons, wheel, position, clicked extension state, and relative delta.
- [ ] Add regression tests.

---

# Phase 4 — GamePad correctness

## P4-001 — Audit `Buttons` enum
- [ ] Verify all XNA button bit values.
- [ ] Verify extension button bit values do not collide with XNA bits.
- [ ] Add numeric value tests.
- [ ] Document extension buttons.

## P4-002 — Verify `GamePadState` constructors
- [ ] Test default disconnected state.
- [ ] Test constructor with thumbsticks/triggers/buttons/dpad.
- [ ] Test constructor with packet number if present.
- [ ] Test equality and hash behavior.

## P4-003 — Verify `GamePadButtons`
- [ ] Test every XNA button property.
- [ ] Test pressed/released conversion.
- [ ] Test multi-button combinations.
- [ ] Test extension buttons separately.

## P4-004 — Verify `GamePadDPad`
- [ ] Test all DPad directions.
- [ ] Test default released state.
- [ ] Test equality/hash.
- [ ] Test interaction with `Buttons` flags.

## P4-005 — Verify `GamePadThumbSticks`
- [ ] Test default values.
- [ ] Test clamping.
- [ ] Test independent axes dead zone.
- [ ] Test circular dead zone.
- [ ] Test no dead zone.
- [ ] Test left and right stick independently.

## P4-006 — Verify `GamePadTriggers`
- [ ] Test default values.
- [ ] Test clamping below 0 and above 1.
- [ ] Test dead zone threshold behavior.
- [ ] Test equality/hash.

## P4-007 — Verify SDL axis normalization
- [ ] Test min, max, zero, and near-zero SDL axis values.
- [ ] Verify Y-axis inversion matches XNA expectations.
- [ ] Verify trigger normalization matches XNA/FNA policy.
- [ ] Add fake backend tests.

## P4-008 — Verify SDL button mapping
- [ ] Test A/B/X/Y.
- [ ] Test shoulders.
- [ ] Test Back/Start.
- [ ] Test sticks.
- [ ] Test Guide if supported.
- [ ] Test DPad.
- [ ] Test extension buttons if present.

## P4-009 — Verify slot lifecycle
- [ ] Test controller connect.
- [ ] Test controller disconnect.
- [ ] Test reconnect.
- [ ] Test slot reuse.
- [ ] Test maximum player count.
- [ ] Ensure stale state is cleared on disconnect.

## P4-010 — Verify `PlayerIndex` bounds
- [ ] Test all valid player indices.
- [ ] Test invalid enum values if possible.
- [ ] Ensure no out-of-bounds access.
- [ ] Add regression tests.

## P4-011 — Verify `GamePad::GetState`
- [ ] Test disconnected state.
- [ ] Test connected state.
- [ ] Test dead zone overload.
- [ ] Test packet number behavior.
- [ ] Test deterministic state snapshots.

## P4-012 — Verify `GamePad::GetCapabilities`
- [ ] Test disconnected capabilities.
- [ ] Test connected capabilities from fake backend.
- [ ] Verify each capability flag.
- [ ] Document unsupported capabilities such as voice if always false.

## P4-013 — Verify packet number behavior
- [ ] Packet number should change on meaningful input changes.
- [ ] Packet number should change on connect/disconnect.
- [ ] Packet number should not change unnecessarily on repeated identical events.
- [ ] Add tests for button and axis jitter.

## P4-014 — Verify vibration
- [ ] Test `SetVibration` clamps values.
- [ ] Test disconnected controller behavior.
- [ ] Test no haptic support behavior.
- [ ] Test NaN/Inf handling if applicable.
- [ ] Document platform limitations.

## P4-015 — Verify extension rumble APIs
- [ ] Test trigger vibration extension.
- [ ] Test duration-based rumble if present.
- [ ] Ensure extension APIs are clearly marked.
- [ ] Add fake backend tests.

## P4-016 — Verify light bar extension
- [ ] Test light bar success path.
- [ ] Test no support path.
- [ ] Test invalid color values if applicable.
- [ ] Document supported devices.

## P4-017 — Verify sensor extensions
- [ ] Test accelerometer availability.
- [ ] Test gyroscope availability.
- [ ] Test sensor enable/disable.
- [ ] Test no support path.
- [ ] Document device/platform limitations.

## P4-018 — Verify GUID extension
- [ ] Test GUID for connected fake controller.
- [ ] Test GUID for disconnected controller.
- [ ] Document SDL/FNA compatibility expectations.

## P4-019 — Verify `GamePadType`
- [ ] Audit SDL joystick type to XNA `GamePadType` mapping.
- [ ] Fix suspicious mappings.
- [ ] Document unknown/unmappable types.
- [ ] Add fake backend tests.

## P4-020 — Verify gamepad reset
- [ ] Ensure reset clears all slots.
- [ ] Ensure reset clears backend state for tests.
- [ ] Ensure no packet/state leak across tests.
- [ ] Add regression tests.

---

# Phase 5 — Touch state correctness

## P5-001 — Audit `TouchCollection` read-only behavior
- [ ] `TouchCollection::IsReadOnly` currently claims read-only behavior.
- [ ] Verify XNA/FNA behavior for mutation methods.
- [ ] Make mutation methods throw/not supported if strict XNA requires it.
- [ ] Or clearly mark mutable behavior as C++ deviation.
- [ ] Add tests for `Add`, `Clear`, `Insert`, `Remove`, `RemoveAt`, and non-const indexing.

## P5-002 — Verify `TouchCollection::CopyTo`
- [ ] Compare CNA behavior against XNA/FNA.
- [ ] Decide how to represent C# array overwrite semantics in C++.
- [ ] Add tests for offset, insufficient capacity, empty collection, and invalid offset.
- [ ] Document any unavoidable C++ deviation.

## P5-003 — Verify `TouchCollection` enumeration
- [ ] Test begin/end iteration.
- [ ] Test count.
- [ ] Test contains.
- [ ] Test index lookup.
- [ ] Test deterministic order.

## P5-004 — Verify `TouchLocation`
- [ ] Test constructors.
- [ ] Test id, state, position, pressure.
- [ ] Test previous location behavior.
- [ ] Test equality/hash if implemented.

## P5-005 — Verify `TouchLocationState`
- [ ] Freeze numeric values.
- [ ] Test state transitions:
  - Invalid
  - Released
  - Pressed
  - Moved
- [ ] Add regression tests.

## P5-006 — Audit `TouchPanel::GetState`
- [ ] Inspect both internal touch slot path and `InputManager` fallback path.
- [ ] Ensure the two paths cannot diverge in production.
- [ ] Add tests for each path.
- [ ] Document which path is authoritative.

## P5-007 — Fix duplicated condition in `TouchPanel::SetFinger`
- [ ] Remove duplicate nested condition.
- [ ] Add regression tests around first touch, moved touch, and released touch.
- [ ] Ensure no behavior accidentally changes unless intended.

## P5-008 — Verify touch previous-location tracking
- [ ] Test Pressed has invalid previous location.
- [ ] Test Moved has previous Pressed/Moved location.
- [ ] Test Released has previous location.
- [ ] Test unknown Released does not create bogus state.

## P5-009 — Verify repeated touch down
- [ ] Test repeated Pressed with same id.
- [ ] Decide whether it should replace, ignore, or transition.
- [ ] Match FNA/XNA where possible.
- [ ] Add regression tests.

## P5-010 — Verify touch cancel behavior
- [ ] Test SDL canceled finger event.
- [ ] Ensure it becomes Released or documented cancellation behavior.
- [ ] Ensure cleanup happens exactly once.
- [ ] Add tests.

## P5-011 — Verify max touch count
- [ ] Test more touches than maximum supported by current storage.
- [ ] Ensure deterministic truncation or rejection.
- [ ] Ensure no out-of-bounds writes.
- [ ] Add tests.

## P5-012 — Verify touch id ordering
- [ ] Test multiple touch ids.
- [ ] Verify order is deterministic.
- [ ] Verify order matches documented behavior.
- [ ] Add tests.

## P5-013 — Verify `TouchPanel::GetCapabilities`
- [ ] Determine how touch capability is detected.
- [ ] Avoid capability becoming true only after first touch unless intentionally documented.
- [ ] Add fake backend/device query if needed.
- [ ] Add tests.

## P5-014 — Verify display size dependency
- [ ] Audit behavior when display width/height is zero.
- [ ] Ensure touch state and gesture state do not diverge unexpectedly.
- [ ] Add tests for touch before display size is known.
- [ ] Document startup behavior.

## P5-015 — Verify touch coordinate scaling
- [ ] Test normalized SDL touch coordinates to pixel coordinates.
- [ ] Test logical/display size changes.
- [ ] Test high-DPI behavior.
- [ ] Add manual validation task for real devices.

## P5-016 — Verify touch reset
- [ ] Ensure reset clears active touches.
- [ ] Ensure reset clears previous touches.
- [ ] Ensure reset clears gesture queue.
- [ ] Add regression tests.

---

# Phase 6 — Gesture correctness

## P6-001 — Audit `GestureType` enum
- [ ] Verify XNA numeric flags.
- [ ] Test bitwise combinations.
- [ ] Test extension values if any.
- [ ] Add numeric freeze tests.

## P6-002 — Verify enabled gestures behavior
- [ ] Test default enabled gestures.
- [ ] Test enabling one gesture.
- [ ] Test enabling multiple gestures.
- [ ] Test disabling gestures clears or preserves queue according to XNA/FNA behavior.
- [ ] Add regression tests.

## P6-003 — Verify `IsGestureAvailable`
- [ ] Test false when queue is empty.
- [ ] Test true when queue has entries.
- [ ] Test behavior after `ReadGesture`.
- [ ] Add tests.

## P6-004 — Verify `ReadGesture`
- [ ] Test FIFO order.
- [ ] Test exception when no gesture is available.
- [ ] Test gesture data fields.
- [ ] Add tests.

## P6-005 — Verify tap detection
- [ ] Test simple tap.
- [ ] Test movement threshold.
- [ ] Test duration threshold.
- [ ] Test disabled tap gesture.
- [ ] Add deterministic clock tests.

## P6-006 — Verify double-tap detection
- [ ] Test two taps within threshold.
- [ ] Test two taps outside threshold.
- [ ] Test moved second tap.
- [ ] Test disabled double-tap.
- [ ] Add deterministic tests.

## P6-007 — Verify hold detection
- [ ] Test hold threshold.
- [ ] Test movement cancels hold.
- [ ] Test release before threshold.
- [ ] Test disabled hold.
- [ ] Add deterministic tests.

## P6-008 — Verify horizontal drag
- [ ] Test drag start.
- [ ] Test drag delta.
- [ ] Test drag complete.
- [ ] Test vertical movement rejection if required.
- [ ] Add tests.

## P6-009 — Verify vertical drag
- [ ] Test drag start.
- [ ] Test drag delta.
- [ ] Test drag complete.
- [ ] Test horizontal movement rejection if required.
- [ ] Add tests.

## P6-010 — Verify free drag
- [ ] Test drag start.
- [ ] Test drag delta.
- [ ] Test drag complete.
- [ ] Test disabled free drag.
- [ ] Add tests.

## P6-011 — Verify flick detection
- [ ] Test flick velocity calculation.
- [ ] Test too-slow movement.
- [ ] Test direction data.
- [ ] Test disabled flick.
- [ ] Add tests.

## P6-012 — Verify pinch detection
- [ ] Test two-finger pinch start.
- [ ] Test pinch delta.
- [ ] Test pinch complete.
- [ ] Test one finger released.
- [ ] Add tests.

## P6-013 — Verify pinch-complete gesture
- [ ] Test pinch-complete queue entry.
- [ ] Verify position and delta fields.
- [ ] Verify ordering relative to final pinch event.
- [ ] Add tests.

## P6-014 — Verify multi-touch interactions
- [ ] Test tap while second finger appears.
- [ ] Test drag interrupted by second finger.
- [ ] Test pinch after one-finger drag.
- [ ] Document expected policy.

## P6-015 — Verify gesture queue reset
- [ ] Ensure reset clears queued gestures.
- [ ] Ensure reset clears detector state.
- [ ] Add regression tests.

## P6-016 — Manual gesture validation
- [ ] Create manual test checklist for real touchscreen.
- [ ] Validate tap, double-tap, hold, drag, flick, and pinch on at least one real device.
- [ ] Record device, OS, display scale, and result.

---

# Phase 7 — Text input and IME correctness

## P7-001 — Audit `TextInputEXT` scope
- [ ] Confirm it is an extension, not strict XNA 4.0.
- [ ] Ensure naming and docs make this clear.
- [ ] Verify public API does not claim strict XNA support.

## P7-002 — Verify UTF-8 to UTF-16 decoding
- [ ] Test ASCII.
- [ ] Test multi-byte BMP characters.
- [ ] Test astral characters requiring surrogate pairs.
- [ ] Test invalid UTF-8.
- [ ] Test truncated UTF-8.
- [ ] Test overlong sequences if decoder handles them.
- [ ] Add regression tests.

## P7-003 — Verify text input events
- [ ] Test SDL text input event conversion.
- [ ] Test empty text.
- [ ] Test multiple code units.
- [ ] Test event callback ordering.
- [ ] Add tests.

## P7-004 — Verify synthesized control characters
- [ ] Test Backspace.
- [ ] Test Tab.
- [ ] Test Enter.
- [ ] Test Delete.
- [ ] Test Home/End if currently synthesized.
- [ ] Verify behavior against FNA/MonoGame policy.
- [ ] Add tests.

## P7-005 — Verify clipboard paste behavior
- [ ] Audit Ctrl+V handling.
- [ ] Ensure text is not double-inserted if SDL also emits text input.
- [ ] Add tests for Ctrl+V keydown and text input sequence.
- [ ] Document platform behavior.

## P7-006 — Verify key repeat and text repeat
- [ ] Ensure repeated keydown does not corrupt keyboard state.
- [ ] Ensure repeated text input is delivered where intended.
- [ ] Add tests.

## P7-007 — Verify text editing / IME composition
- [ ] Test text editing callback.
- [ ] Test empty composition.
- [ ] Test start/length values.
- [ ] Determine whether SDL byte offsets or UTF-16 offsets are exposed.
- [ ] Document behavior.
- [ ] Add tests where possible.

## P7-008 — Verify start/stop text input
- [ ] Test start with valid window.
- [ ] Test stop with valid window.
- [ ] Test no-window behavior.
- [ ] Ensure no crash.
- [ ] Document no-op behavior if that is intended.

## P7-009 — Verify input rectangle
- [ ] Test setting input rectangle.
- [ ] Test no-window behavior.
- [ ] Test negative and zero rectangle values if possible.
- [ ] Document platform limitations.

## P7-010 — Manual IME validation
- [ ] Validate with at least one IME on desktop.
- [ ] Validate with mobile soft keyboard if supported.
- [ ] Record OS, keyboard/IME, and result.

---

# Phase 8 — SDL bridge and backend integration

## P8-001 — Audit SDL bridge event coverage
- [ ] List every SDL event consumed by `SdlInputBridge`.
- [ ] List every relevant SDL input event not consumed.
- [ ] Decide whether missing events are intentional.
- [ ] Document gaps.

## P8-002 — Verify event ordering
- [ ] Test keyboard event ordering.
- [ ] Test text input ordering.
- [ ] Test mouse motion/button ordering.
- [ ] Test touch/gesture ordering.
- [ ] Test gamepad connect/input ordering.

## P8-003 — Verify SDL initialization ownership
- [ ] Ensure input code initializes only the SDL subsystems it owns.
- [ ] Ensure repeated init/shutdown is safe.
- [ ] Ensure tests do not depend on global hidden state.
- [ ] Add tests.

## P8-004 — Verify fake backend coverage
- [ ] Ensure fake keyboard/mouse/touch/gamepad paths exist or are testable.
- [ ] Add fake backend helpers where useful.
- [ ] Keep fake backend internal to tests.

## P8-005 — Verify window handle resolution
- [ ] Audit all places resolving SDL window handle.
- [ ] Ensure null window is safe.
- [ ] Ensure stale window handle is safe where possible.
- [ ] Add tests.

## P8-006 — Verify high-DPI / logical coordinate handling
- [ ] Test mouse logical coordinates.
- [ ] Test touch logical coordinates.
- [ ] Test display resize.
- [ ] Add manual validation task.

## P8-007 — Verify focus, minimize, and window close behavior
- [ ] Decide how input state is cleared on focus loss.
- [ ] Decide how input behaves when minimized.
- [ ] Add SDL bridge tests if fake events can represent this.
- [ ] Document runtime behavior.

## P8-008 — Verify backend reset
- [ ] Ensure all internal input state can be reset for tests.
- [ ] Ensure reset does not require SDL window.
- [ ] Ensure reset leaves system in deterministic state.
- [ ] Add tests.

---

# Phase 9 — Build, tests, and CI

## P9-001 — Improve missing submodule diagnostics
- [ ] If vendored SDL is required, make CMake error explicit and actionable.
- [ ] Print exact command to initialize submodules.
- [ ] Do not fail later with obscure include/link errors.
- [ ] Add documentation.

## P9-002 — Add optional system SDL mode if desired
- [ ] Determine whether CNA should support system SDL for local testing.
- [ ] If yes, add a CMake option.
- [ ] If no, document why vendored SDL is required.
- [ ] Keep behavior deterministic.

## P9-003 — Create focused Input test target
- [ ] Ensure there is a simple command to run only Input tests.
- [ ] Include Keyboard, Mouse, GamePad, Touch, Gesture, TextInput, and SDL bridge tests.
- [ ] Document command in this plan.

## P9-004 — Run Input tests repeatedly
- [ ] Run focused Input tests once.
- [ ] Run focused Input tests with shuffle.
- [ ] Run focused Input tests with repeat count.
- [ ] Fix any order-dependent failures.

## P9-005 — Run sanitizer builds
- [ ] Run AddressSanitizer if supported.
- [ ] Run UndefinedBehaviorSanitizer if supported.
- [ ] Fix sanitizer findings.
- [ ] Record unsupported sanitizer/platform cases.

## P9-006 — Add fuzz-style SDL bridge tests
- [ ] Feed randomized but valid SDL-like events.
- [ ] Ensure no crashes.
- [ ] Ensure state remains internally consistent.
- [ ] Keep fuzz tests deterministic with recorded seeds.

## P9-007 — Add golden event sequence tests
- [ ] Create golden sequences for keyboard.
- [ ] Create golden sequences for mouse.
- [ ] Create golden sequences for touch.
- [ ] Create golden sequences for gamepad.
- [ ] Assert final state and packet/queue behavior.

## P9-008 — Freeze public API signatures
- [ ] Update signature freeze tests after intentional corrections.
- [ ] Ensure strict XNA API changes are intentional.
- [ ] Ensure extension API changes are documented.

## P9-009 — Update test coverage document
- [ ] List every Input type.
- [ ] List corresponding tests.
- [ ] Mark remaining gaps.
- [ ] Do not claim 100% behavior coverage unless true.

---

# Phase 10 — Documentation

## P10-001 — Document strict XNA compatibility
- [ ] Document which Input APIs are intended to match XNA 4.0 exactly.
- [ ] Document known deviations.
- [ ] Document C++-specific representation differences.

## P10-002 — Document FNA compatibility
- [ ] Document where CNA follows FNA behavior.
- [ ] Document any FNA extensions supported by CNA.
- [ ] Document any known FNA behavior not yet implemented.

## P10-003 — Document `NOXNA` / extension APIs
- [ ] Document `TextInputEXT`.
- [ ] Document relative mouse mode.
- [ ] Document gamepad GUID, sensors, trigger vibration, light bar, and extra buttons.
- [ ] Ensure extension docs do not imply XNA 4.0 compatibility.

## P10-004 — Document platform notes
- [ ] Windows notes.
- [ ] Linux notes.
- [ ] macOS notes.
- [ ] Android notes if supported.
- [ ] Browser/Emscripten notes if supported.
- [ ] Gamepad device notes.

## P10-005 — Document manual validation checklist
- [ ] Keyboard layouts.
- [ ] Mouse and relative mode.
- [ ] Gamepads.
- [ ] Touchscreen.
- [ ] IME/text input.
- [ ] High-DPI display.

---

# Phase 11 — Manual hardware validation

## P11-001 — Keyboard hardware validation
- [ ] Validate US layout.
- [ ] Validate CZ layout.
- [ ] Validate at least one non-QWERTY layout if available.
- [ ] Validate modifiers and OEM keys.
- [ ] Record results.

## P11-002 — Mouse hardware validation
- [ ] Validate normal mouse motion.
- [ ] Validate wheel.
- [ ] Validate extra buttons.
- [ ] Validate relative mode.
- [ ] Validate high-DPI behavior.
- [ ] Record results.

## P11-003 — Xbox-compatible gamepad validation
- [ ] Validate connect/disconnect.
- [ ] Validate buttons.
- [ ] Validate sticks.
- [ ] Validate triggers.
- [ ] Validate rumble.
- [ ] Record results.

## P11-004 — PlayStation-compatible gamepad validation
- [ ] Validate mapping.
- [ ] Validate GUID.
- [ ] Validate sensors if supported.
- [ ] Validate light bar if supported.
- [ ] Record results.

## P11-005 — Generic SDL gamepad validation
- [ ] Validate a generic mapped controller.
- [ ] Validate unknown controller fallback.
- [ ] Record mapping issues.

## P11-006 — Touchscreen validation
- [ ] Validate single touch.
- [ ] Validate multi-touch.
- [ ] Validate gestures.
- [ ] Validate display scaling.
- [ ] Record results.

## P11-007 — IME and soft keyboard validation
- [ ] Validate desktop IME.
- [ ] Validate mobile soft keyboard if supported.
- [ ] Validate composition events.
- [ ] Record results.

---

# Phase 12 — Final quality gates

## P12-001 — Run full Input test suite
- [ ] Run all Input tests.
- [ ] Run repeated/shuffled Input tests.
- [ ] Record command and result.
- [ ] Fix failures.

## P12-002 — Run full CNA test suite
- [ ] Run all available CNA tests.
- [ ] Ensure Input changes did not break other modules.
- [ ] Record command and result.

## P12-003 — Re-run public API parity
- [ ] Regenerate parity matrix.
- [ ] Confirm no strict XNA members are missing.
- [ ] Confirm all extensions are documented.
- [ ] Record result.

## P12-004 — Review all compatibility deviations
- [ ] List every remaining strict-XNA deviation.
- [ ] Decide whether to fix or document.
- [ ] Do not leave accidental deviations undocumented.

## P12-005 — Final Input readiness statement
- [ ] Write a final status section:
  - API completeness estimate,
  - implementation correctness estimate,
  - automated test coverage,
  - manual validation status,
  - known remaining risks.
- [ ] Do not overstate readiness.
- [ ] Mark this plan complete only when all non-blocked tasks are done.
