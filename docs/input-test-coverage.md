# Input source → test coverage (INPUT-AUDIT-002)

> **Generated** by `tools/input_parity/check_input_test_coverage.py`. Maps each Input
> type to whether it has a dedicated `TEST(<Type>Test, …)` suite and how many test files
> reference it. Inspection aid — a value type covered inside a sibling suite is not a real
> gap (see the `note` column). Do not hand-edit; re-run the script.
## Gaps (candidate INPUT-TEST-* tasks)

None — every Input type has a dedicated suite or a documented sibling-suite cover.



## Public XNA Input types

| Type | Header | Dedicated suite | Test files | Note |
|------|--------|-----------------|-----------|------|
| `ButtonState` | `include/Microsoft/Xna/Framework/Input/ButtonState.hpp` | yes | 11 |  |
| `Buttons` | `include/Microsoft/Xna/Framework/Input/Buttons.hpp` | yes | 8 |  |
| `GamePad` | `include/Microsoft/Xna/Framework/Input/GamePad.hpp` | yes | 12 |  |
| `GamePadButtons` | `include/Microsoft/Xna/Framework/Input/GamePadButtons.hpp` | yes | 4 |  |
| `GamePadCapabilities` | `include/Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp` | yes | 4 |  |
| `GamePadDPad` | `include/Microsoft/Xna/Framework/Input/GamePadDPad.hpp` | yes | 4 |  |
| `GamePadDeadZone` | `include/Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp` | yes | 7 |  |
| `GamePadState` | `include/Microsoft/Xna/Framework/Input/GamePadButtons.hpp` | yes | 5 |  |
| `GamePadThumbSticks` | `include/Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp` | yes | 4 |  |
| `GamePadTriggers` | `include/Microsoft/Xna/Framework/Input/GamePadTriggers.hpp` | yes | 4 |  |
| `GamePadType` | `include/Microsoft/Xna/Framework/Input/GamePadType.hpp` | yes | 6 |  |
| `GestureSample` | `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp` | yes | 6 |  |
| `GestureType` | `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp` | yes | 9 |  |
| `KeyState` | `include/Microsoft/Xna/Framework/Input/KeyState.hpp` | yes | 4 |  |
| `Keyboard` | `include/Microsoft/Xna/Framework/Input/Keyboard.hpp` | yes | 7 |  |
| `KeyboardState` | `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp` | yes | 3 |  |
| `Keys` | `include/Microsoft/Xna/Framework/Input/Keys.hpp` | no | 8 | covered via KeyboardInputTests.cpp (exhaustive Keys value table, INPUT-KBD-001) |
| `Mouse` | `include/Microsoft/Xna/Framework/Input/Mouse.hpp` | yes | 7 |  |
| `MouseCursor` | `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp` | yes | 3 |  |
| `MouseState` | `include/Microsoft/Xna/Framework/Input/MouseState.hpp` | yes | 6 |  |
| `TextInputEXT` | `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp` | yes | 7 |  |
| `TouchCollection` | `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp` | yes | 7 |  |
| `TouchLocation` | `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp` | yes | 5 |  |
| `TouchLocationState` | `include/Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp` | yes | 9 |  |
| `TouchPanel` | `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp` | yes | 9 |  |
| `TouchPanelCapabilities` | `include/Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.hpp` | yes | 4 |  |

## Internal (`CNA::Internal::Input`) types

| Type | Header | Dedicated suite | Test files | Note |
|------|--------|-----------------|-----------|------|
| `GamePadAxis` | `include/CNA/Internal/Input/InputManager.hpp` | no | 4 | covered via SdlInputBridge* / InputManager gamepad tests (internal enum) |
| `GamePadButton` | `include/CNA/Internal/Input/InputManager.hpp` | yes | 2 |  |
| `GestureDetector` | `include/CNA/Internal/Input/GestureDetector.hpp` | yes | 4 |  |
| `ISdlGamepadBackend` | `include/CNA/Internal/Input/SdlGamepadBackend.hpp` | no | 1 | covered via SdlGamepadBackendTests.cpp via the FakeSdlGamepadBackend seam |
| `InputManager` | `include/CNA/Internal/Input/InputManager.hpp` | no | 16 | covered via InputResetTests / SdlInputBridge* / SdlGamepadBackendTests (no same-named suite by design) |
| `MouseButton` | `include/CNA/Internal/Input/InputManager.hpp` | no | 2 | covered via SdlInputBridgeMouseTests / InputManager (internal enum) |
| `RawGamePadState` | `include/CNA/Internal/Input/InputManager.hpp` | no | 0 | covered via SdlGamepadBackendTests.cpp via InputManager::GetRawGamePadState (bound as auto) |
| `SdlInputBridge` | `include/CNA/Internal/Input/SdlInputBridge.hpp` | yes | 12 |  |

