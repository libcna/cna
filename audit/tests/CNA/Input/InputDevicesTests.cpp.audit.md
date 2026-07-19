# Audit: tests/CNA/Input/InputDevicesTests.cpp

## Metadata
- Source file: `tests/CNA/Input/InputDevicesTests.cpp` (74 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-input` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Input::InputDevices` enumeration (NOXNA extension)
- Main related tests: N/A (this IS a test file)

## Purpose
Verifies `InputDevices::GetMiceEXT`/`GetKeyboardsEXT`/`GetTouchDevicesEXT` correctly forward a
fake `ISystemDeviceBackend`'s enumeration (including the empty case), plus `InputDeviceInfoEXT`'s
equality operator (id and name both compared).

## Executive Verdict
Clean, deterministic coverage via dependency injection (`SetSystemDeviceBackendForTests`),
avoiding reliance on CI having a predictable set of physical devices — the correct testing strategy
for this kind of OS-enumeration wrapper.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Same fake-backend dependency-injection pattern as `PowerTests.cpp` and `SensorsTests.cpp` in this
same shard — a consistent, positive testing convention for OS-facing NOXNA singletons.

## Missing or Weak Tests
Not identified for this wrapper's scope.

## Positive Findings
Dependency-injected fake backend cleanly decouples the test from real hardware/OS state.

## Final Assessment
No findings.
