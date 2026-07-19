# Audit: tests/CNA/Input/InputDevicesHotplugTests.cpp

## Metadata
- Source file: `tests/CNA/Input/InputDevicesHotplugTests.cpp` (87 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-input` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Input::InputDevices` hotplug events (NOXNA extension)
- Main related tests: N/A (this IS a test file)

## Purpose
Verifies `InputDevices::MouseConnectedEXT`/`MouseDisconnectedEXT`/`KeyboardConnectedEXT`/
`KeyboardDisconnectedEXT` events fire with the correct device ID when synthetic
`SDL_EVENT_MOUSE_ADDED`/`REMOVED`/`SDL_EVENT_KEYBOARD_ADDED`/`REMOVED` events are fed through
`SdlInputBridge::ProcessEvent`, and that mouse/keyboard events don't cross-fire each other's
handlers.

## Executive Verdict
Good, deterministic event-plumbing coverage using synthetic SDL events rather than requiring real
hardware — the correct approach for hotplug behavior that can't be reliably triggered in CI.
`InputDevices::ResetForTests()` in `SetUp`/`TearDown` correctly isolates test state.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
None.

## Missing or Weak Tests
Touch-device hotplug events (if `InputDevices` exposes any) are not covered here — only
mouse/keyboard. Not flagged as a defect since touch hotplug may not exist as a distinct event.

## Positive Findings
The cross-fire test (`MouseAndKeyboardEventsDoNotCrossFire`) is a valuable, easy-to-omit check
that two independently-wired event handlers don't leak into each other.

## Final Assessment
No findings.
