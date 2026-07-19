# Audit: tests/Microsoft/Xna/Framework/Input/KeyboardKeyNameTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Input/KeyboardKeyNameTests.cpp` (69 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-input` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Input::Keyboard::GetKeyNameEXT`/
  `GetKeyFromNameEXT` (NOXNA)
- Main related tests: N/A (this IS a test file)

## Purpose
Covers the NOXNA `GetKeyNameEXT`/`GetKeyFromNameEXT` layout-dependent key-name round-trip API:
known-key names on a US layout, an unmapped key (`Keys::None`) producing an empty name, name-to-key
round-tripping for a representative key set, and unrecognized-name handling.

## Executive Verdict
No findings. Correctly gated behind a real SDL video subsystem (layout resolution needs an active
keymap) with a clean `GTEST_SKIP()` for headless/no-display environments, consistent with this
project's established pattern for display-dependent tests.

## Checklist Results
- `NameToKeyReversesKeyToName` verifies genuine round-trip correctness (name -> key -> same key)
  across a representative key sample (letters, `Space`, `Enter`, arrows, `Escape`), not just a
  single key.
- `UnmappedKeyHasEmptyName`/`UnrecognizedNameYieldsNone` correctly test both directions of the
  "no mapping" edge case.

## Detailed Findings
None.

## Cross-File Observations
Directly parallels `KeyboardScancodeNameTests.cpp`'s structure and coverage, but for the
layout-dependent (keycode) name API rather than the layout-independent (scancode) one — the file's
own comment explains why this one needs the video subsystem and the scancode-name test doesn't.

## Missing or Weak Tests
None identified for this NOXNA API's surface.

## Positive Findings
Correct test-environment gating (SDL video subsystem) with a clean skip path.

## Final Assessment
No findings.
