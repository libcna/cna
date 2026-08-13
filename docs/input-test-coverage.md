# Input source → test coverage (INPUT-AUDIT-002)

> **Related input docs (INP-0003):** [plan](../plan_input.md) · [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual results](input-manual-verification-results.md) · [demo checklist](demo-input-checklist.md)

> **Generated** by `tools/input_parity/check_input_test_coverage.py`. Maps each Input
> type to whether it has a dedicated `TEST(<Type>Test, …)` suite and how many test files
> reference it. Inspection aid — a value type covered inside a sibling suite is not a real
> gap (see the `note` column). Do not hand-edit; re-run the script.

## Gaps (candidate INPUT-TEST-* tasks)

None — every Input type has a dedicated suite or a documented sibling-suite cover.

## Public XNA Input types

| Type | Header | Dedicated suite | Test files | Note |
|------|--------|-----------------|-----------|------|
| `ButtonState` | `modules/input/include/Microsoft/Xna/Framework/Input/ButtonState.hpp` | yes | 11 |  |
| `Buttons` | `modules/input/include/Microsoft/Xna/Framework/Input/Buttons.hpp` | yes | 9 |  |
| `GamePad` | `modules/input/include/Microsoft/Xna/Framework/Input/GamePad.hpp` | yes | 10 |  |
| `GamePadButtons` | `modules/input/include/Microsoft/Xna/Framework/Input/GamePadButtons.hpp` | yes | 4 |  |
| `GamePadCapabilities` | `modules/input/include/Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp` | yes | 4 |  |
| `GamePadDPad` | `modules/input/include/Microsoft/Xna/Framework/Input/GamePadDPad.hpp` | yes | 4 |  |
| `GamePadDeadZone` | `modules/input/include/Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp` | yes | 8 |  |
| `GamePadState` | `modules/input/include/Microsoft/Xna/Framework/Input/GamePadButtons.hpp` | yes | 6 |  |
| `GamePadThumbSticks` | `modules/input/include/Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp` | yes | 4 |  |
| `GamePadTriggers` | `modules/input/include/Microsoft/Xna/Framework/Input/GamePadTriggers.hpp` | yes | 4 |  |
| `GamePadType` | `modules/input/include/Microsoft/Xna/Framework/Input/GamePadType.hpp` | yes | 5 |  |
| `GestureSample` | `modules/input/include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp` | yes | 6 |  |
| `GestureType` | `modules/input/include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp` | yes | 9 |  |
| `KeyModifiersEXT` | `modules/input/include/Microsoft/Xna/Framework/Input/Keyboard.hpp` | no | 2 | covered via KeyboardModStateTests.cpp (KeyboardModStateEXTTest) — every flag tested individually + combined |
| `KeyState` | `modules/input/include/Microsoft/Xna/Framework/Input/KeyState.hpp` | yes | 4 |  |
| `Keyboard` | `modules/input/include/Microsoft/Xna/Framework/Input/Keyboard.hpp` | yes | 12 |  |
| `KeyboardState` | `modules/input/include/Microsoft/Xna/Framework/Input/KeyboardState.hpp` | yes | 3 |  |
| `Keys` | `modules/input/include/Microsoft/Xna/Framework/Input/Keys.hpp` | no | 12 | covered via KeyboardInputTests.cpp (exhaustive Keys value table, INPUT-KBD-001) |
| `Mouse` | `modules/input/include/Microsoft/Xna/Framework/Input/Mouse.hpp` | yes | 9 |  |
| `MouseCursor` | `modules/input/include/Microsoft/Xna/Framework/Input/MouseCursor.hpp` | yes | 5 |  |
| `MouseState` | `modules/input/include/Microsoft/Xna/Framework/Input/MouseState.hpp` | yes | 6 |  |
| `TextInputEXT` | `modules/input/include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp` | yes | 8 |  |
| `TextInputTypeEXT` | `modules/input/include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp` | no | 2 | covered via TextInputEXTTests.cpp (TextInputEXTTest) — all 9 values iterated in StartTextInputWithTypeWithoutWindowIsSafeNoOpForEveryType and StartTextInputWithTypeRoundTripsThroughRealWindowForEveryType |
| `TouchCollection` | `modules/input/include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp` | yes | 7 |  |
| `TouchLocation` | `modules/input/include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp` | yes | 5 |  |
| `TouchLocationState` | `modules/input/include/Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp` | yes | 9 |  |
| `TouchPanel` | `modules/input/include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp` | yes | 9 |  |
| `TouchPanelCapabilities` | `modules/input/include/Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.hpp` | yes | 4 |  |

## Internal (`CNA::Internal::Input`) types

| Type | Header | Dedicated suite | Test files | Note |
|------|--------|-----------------|-----------|------|
| `GestureDetector` | `modules/input/include/CNA/Internal/Input/GestureDetector.hpp` | yes | 4 |  |
| `InputManager` | `modules/input/include/CNA/Internal/Input/InputManager.hpp` | no | 10 | covered via InputResetTests / PlatformInputBridge* (no same-named suite by design) |
| `MouseButton` | `modules/input/include/CNA/Internal/Input/InputManager.hpp` | no | 2 | covered via PlatformInputBridgeMouse* / InputManager (internal enum) |
| `PlatformInputBridge` | `modules/input/include/CNA/Internal/Input/PlatformInputBridge.hpp` | yes | 10 |  |
| `SdlInputBridge` | `modules/input/include/CNA/Internal/Input/SdlInputBridge.hpp` | yes | 7 |  |
