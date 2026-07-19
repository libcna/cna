# Audit: tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp` (537 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests the SDL→XNA keyboard conversion inside `SdlInputBridge::ProcessEvent`
  (backs `Microsoft::Xna::Framework::Input::Keyboard`), Tasks 820-821
- Main related tests: cross-references `SdlInputBridgeTextInputTests.cpp` (repeat-key text
  synthesis) and `MouseInputTests.cpp` (not in this shard)

## Purpose
Table-driven verification of both keyboard-conversion modes (keycode map, the default; scancode map,
env-var-gated) via synthetic `SDL_EVENT_KEY_DOWN`/`UP` events through the real bridge, plus window-
lifecycle no-op behavior, locale/non-US-layout key handling, and `GetKeyFromScancodeEXT`.

## Executive Verdict
An exceptionally well-documented file — nearly every test carries a task-ID comment
(DEC-15/16/17, P2-xxx, P3-xxx, P8-xxx, INPUT-KBD-xxx) explaining the specific FNA-fidelity question
or intentional deviation being pinned, several with direct citations to FNA's own source line
numbers (`SDL3_FNAPlatform.cs:1026-1035`, `:905-908`, `:2768-2773`). This is the same
citation-quality standard already established by `XactParserTests.cpp` in this shard, applied here
to the input-bridge domain.

## Checklist Results
- `WindowFocusLostDoesNotClearHeldKeysMatchingFna` (DEC-15) explicitly documents and tests an
  intentional, deliberated design decision (a beyond-FNA transient-clear-on-focus-loss was
  considered and REJECTED in favor of exact FNA parity) — this is a genuinely valuable test because
  it pins a decision against future well-intentioned "fix," not just a mechanical behavior.
- `UnmappedKeycodeIsDroppedNotMarkedNone` / `LocaleUnmappedKeycodeIsDroppedNotMarkedNone` /
  `IsoLayoutExtraScancodesAreDroppedNotMarkedNone` (DEC-16) each document and test a genuine,
  deliberate IMPROVEMENT over FNA: FNA's real `SDL3_FNAPlatform.cs:905-908` actually adds
  `Keys.None` to its pressed-keys list for unmapped keycodes (an FNA bug/quirk), while CNA
  intentionally drops instead — correctly framed as an intentional deviation, not an unverified
  claim, and tested from three different unmapped-key angles (raw unknown keycode, non-US locale
  codepoints, ISO-layout-extra scancodes).
- `NordicOemKeysMapToTheirOemKeyMatchingFna` correctly tests the *opposite* case — some non-ASCII
  keys (æ, ø) DO map to an OEM key (matching FNA) rather than being dropped — showing the DEC-16
  drop policy is applied selectively and correctly, not blanket-applied to every non-ASCII input.
  This kind of paired (drop-most, keep-some) test meaningfully raises confidence over one direction
  alone.
- `ImeAndChatPadKeysExistAndAreConsoleOrImeOnly` uses `static_assert` to pin the exact numeric
  `Keys` enum values for IME/ChatPad keys — a correct choice for confirming presence/ordinal
  stability that requires no runtime SDL event at all (these keys have no SDL source).
- `AndroidBackButtonMapsToEscape` (DEC-17) correctly documents this as a CNA-only convenience
  beyond FNA, not a hidden, unexplained deviation.
- `SimultaneousModifierAndLockKeyCombinationTracksAllIndependently` (P2-056) is a genuinely thorough
  test: 9 modifier/lock keys held simultaneously, verified all independently tracked via
  `std::unordered_set<Keys>`, then one released and the other 8 confirmed untouched — a real,
  non-trivial concurrency-of-state check.
- `KeyRepeatKeepsKeyDownWithoutSpuriousTransitions` (INPUT-KBD-019) correctly separates the
  state-tracking half of key-repeat handling from the text-synthesis half (explicitly cross-
  referenced to `SdlInputBridgeTextInputTests.cpp`), avoiding an all-in-one test that would
  conflate two genuinely distinct code paths.
- `INPUT-KBD-009`'s own comment claims "a full line-by-line diff of the keycode map vs FNA's
  INTERNAL_keyMap is byte-identical on all 122 shared keycodes" — a strong, falsifiable claim; the
  representative-case tests here are consistent with that claim but this audit did not independently
  re-run that full diff (out of scope for a per-file test audit; flagged for awareness only, not as
  a finding, since the claim is plausible given the file's overall rigor and is not contradicted by
  anything observed).

## Detailed Findings
None.

## Cross-File Observations
This file's DEC-15/16/17 pattern (explicitly test-pinning a deliberated, documented FNA-fidelity
decision rather than just "what the code currently does") is a strong practice this audit has not
seen as consistently in other shards; it directly forecloses the risk of a future "fix" silently
reintroducing an FNA-matching quirk (Keys::None pollution) that was deliberately rejected.

## Missing or Weak Tests
None identified. The one caveat above (the "byte-identical on all 122 shared keycodes" claim is
plausible but not independently re-verified by this audit pass) is noted for completeness, not as a
defect.

## Positive Findings
The consistent task-ID-cited, FNA-source-line-cited documentation throughout this file sets a high
bar for traceability; the paired drop/keep Nordic-key test and the DEC-15/16/17 decision-pinning
tests are particularly strong examples of tests that protect a considered design decision, not just
current behavior.

## Final Assessment
No findings.
