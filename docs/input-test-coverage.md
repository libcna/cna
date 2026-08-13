# Input source → test coverage (INPUT-AUDIT-002)

> **Related input docs (INP-0003):** [plan](../plan_input.md) · [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual results](input-manual-verification-results.md) · [demo checklist](demo-input-checklist.md)

> **Generated** by `tools/input_parity/check_input_test_coverage.py`. Maps each Input
> type to whether it has a dedicated `TEST(<Type>Test, …)` suite and how many test files
## Gaps (candidate INPUT-TEST-* tasks)

None — every Input type has a dedicated suite or a documented sibling-suite cover.


> reference it. Inspection aid — a value type covered inside a sibling suite is not a real
> gap (see the `note` column). Do not hand-edit; re-run the script.

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
| `KeyModifiersEXT` | `include/Microsoft/Xna/Framework/Input/Keyboard.hpp` | no | 2 | covered via KeyboardModStateTests.cpp (KeyboardModStateEXTTest) — every flag tested individually + combined |
| `KeyState` | `include/Microsoft/Xna/Framework/Input/KeyState.hpp` | yes | 4 |  |
| `Keyboard` | `include/Microsoft/Xna/Framework/Input/Keyboard.hpp` | yes | 11 |  |
| `KeyboardState` | `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp` | yes | 3 |  |
| `Keys` | `include/Microsoft/Xna/Framework/Input/Keys.hpp` | no | 12 | covered via KeyboardInputTests.cpp (exhaustive Keys value table, INPUT-KBD-001) |
| `Mouse` | `include/Microsoft/Xna/Framework/Input/Mouse.hpp` | yes | 9 |  |
| `MouseCursor` | `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp` | yes | 5 |  |
| `MouseState` | `include/Microsoft/Xna/Framework/Input/MouseState.hpp` | yes | 6 |  |
| `TextInputEXT` | `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp` | yes | 9 |  |
| `TextInputTypeEXT` | `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp` | no | 2 | covered via TextInputEXTTests.cpp (TextInputEXTTest) — all 9 values are null-window safe and `EveryExtensionTypeMapsToThePortableContract` pins their contract mapping; `Sdl3TextInputTypesTest` pins the native edge separately |
| `TouchCollection` | `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp` | yes | 7 |  |
| `TouchLocation` | `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp` | yes | 5 |  |
| `TouchLocationState` | `include/Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp` | yes | 9 |  |
| `TouchPanel` | `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp` | yes | 9 |  |
| `TouchPanelCapabilities` | `include/Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.hpp` | yes | 4 |  |

## Internal (`CNA::Internal::Input`) types

| Type | Header | Dedicated suite | Test files | Note |
|------|--------|-----------------|-----------|------|
| `GestureDetector` | `include/CNA/Internal/Input/GestureDetector.hpp` | yes | 4 |  |
| `ISdlHapticBackend` | `include/CNA/Internal/Input/SdlHapticBackend.hpp` | no | 0 | covered via SdlHapticBackendTests.cpp (FakeHapticTest) via the FakeSdlHapticBackend seam |
| `ISystemDeviceBackend` | `include/CNA/Internal/Input/SystemDeviceBackend.hpp` | no | 2 | covered via InputDevicesTests.cpp (CnaInputDevicesTest) / TouchEdgeCaseTests.cpp (TouchCapabilitiesEnumerationTest) via FakeSystemDeviceBackend |
| `ISystemMouseBackend` | `include/CNA/Internal/Input/SystemMouseBackend.hpp` | no | 1 | covered via MouseGlobalTests.cpp (MouseGlobalEXTTest) via a fake system-mouse-backend seam |
| `ISystemPowerBackend` | `include/CNA/Internal/Input/SystemPowerBackend.hpp` | no | 1 | covered via PowerTests.cpp (CnaInputPowerTest) via a fake system-power-backend seam |
| `ISystemSensorBackend` | `include/CNA/Internal/Input/SystemSensorBackend.hpp` | no | 1 | covered via SensorsTests.cpp (CnaInputSensorsTest) via a fake system-sensor-backend seam |
| `InputManager` | `include/CNA/Internal/Input/InputManager.hpp` | no | 18 | covered via InputResetTests / SdlInputBridge* (no same-named suite by design) |
| `MouseButton` | `include/CNA/Internal/Input/InputManager.hpp` | no | 2 | covered via SdlInputBridgeMouseTests / InputManager (internal enum) |
| `SdlInputBridge` | `include/CNA/Internal/Input/SdlInputBridge.hpp` | yes | 16 |  |
