# Audit: tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp` (448 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `SdlInputBridge::ProcessEvent`'s text-input/IME path (backs
  `Microsoft::Xna::Framework::Input::TextInputEXT`, a NOXNA extension — real XNA 4.0 has no direct
  text-input event API), Tasks 704-707, 806-807, 852, 872-875
- Main related tests: complements `SdlInputBridgeCandidatesTests.cpp` (IME candidate lists, same
  shard) and `SdlInputBridgeKeyboardTests.cpp`'s key-repeat state half

## Purpose
Tests UTF-8→UTF-16 decoding of `SDL_EVENT_TEXT_INPUT` (including malformed-byte recovery), control-
key text synthesis (Backspace/Tab/Enter/Delete/Home/End), Ctrl+V paste-echo suppression and its
reset/cleanup behavior, and `TextEditing` (IME composition) byte-offset forwarding.

## Executive Verdict
Excellent, unusually rigorous malformed-UTF-8 handling coverage: 5 distinct malformed-input classes
(bad lead byte, truncated multi-byte sequence, bad continuation byte, overlong encoding, and a
UTF-8-encoded lone surrogate) are each tested SEPARATELY with a precise expected `U+FFFD`
replacement-and-resync result — this is genuinely thorough Unicode-decoder testing, not merely
"garbage in, no crash out."

## Checklist Results
- `TextInputEventDecodesAstralEmojiToSurrogatePair` correctly verifies CNA's documented choice
  (Task 806) to fire `TextInput` once per UTF-16 code unit (matching FNA's C# `Action<char>`
  semantics) — an astral code point correctly produces TWO calls (high+low surrogate), not one call
  with a 32-bit value.
  `TextInputEventDecodesCombiningCharactersAsSeparateCodeUnits` correctly confirms combining marks
  are NOT normalized/composed — matching a raw code-unit-stream semantic, not an NFC-normalized one.
- `OverlongEncodingBecomesReplacementChar` and `SurrogateCodePointEncodedInUtf8BecomesReplacementChar`
  both test UTF-8 security-relevant edge cases (overlong encodings and encoded surrogates are
  classic UTF-8 validation bypass vectors in other contexts) — even though the file's own comment
  correctly notes SDL itself would never emit these, the decoder is defensively tested against them
  anyway, which is the right call for a boundary-facing decoder.
- `CtrlVEmitsPasteCharAndSuppressesLiteralText` / `ResetForTestsClearsTextInputSuppressionFlag` /
  `CtrlVSuppressionDoesNotStickWhenCtrlReleasedWithoutVKeyUp` / `PlainVWithoutCtrlIsNotSuppressed`
  form a genuinely thorough state-machine test suite for the paste-echo-suppression flag: happy
  path, test-isolation reset, an out-of-order-release recovery path (Task 875, explicitly documented
  as fixing a "must not get stuck" defect class), and a negative control (no Ctrl held) — this is
  exactly the kind of test set that would have caught a suppression flag "stuck permanently on"
  regression.
- `TextEditingStartLengthAreRawByteOffsetsNotUtf16Indices` is a genuinely sharp test: it constructs
  a composition string where the UTF-8 byte offset (2) and the UTF-16 code-unit index (1) of the
  same character *differ*, and confirms CNA reports the byte offset — this is exactly the kind of
  test that catches an easy "helpfully" wrong re-indexing bug, and it's explicitly cross-referenced
  to the documented behavior in `TextInputEXT.hpp` (INPUT-TEXT-016).
- `KeyRepeatReemitsControlCharacter` correctly cross-references and scopes itself as the
  text-synthesis half of key-repeat, explicitly deferring the state-tracking half to
  `SdlInputBridgeKeyboardTests.cpp` — clean separation of concerns.
- `TextEditingEmptyCompositionForwardsZeroes` correctly tests the empty-composition edge case with
  its own documented FNA-difference note (FNA passes null; CNA forces start/length to 0) — an
  intentional, disclosed deviation rather than an accidental one.

## Detailed Findings
None.

## Cross-File Observations
The byte-offset-vs-UTF16-index distinction test here is a good complement to `JsonTests.cpp`'s own
byte-level UTF-8 verification earlier in this shard — both files show a consistent project-wide
discipline around precise Unicode/byte-offset semantics rather than "close enough" string handling.

## Missing or Weak Tests
None identified — the malformed-UTF-8 and suppression-flag state-machine coverage in particular is
unusually complete.

## Positive Findings
The five distinct malformed-UTF-8 test cases and the byte-offset-vs-UTF16-index test are among the
most precise Unicode-correctness tests found in this audit; the Ctrl+V suppression-flag test quartet
is a strong example of testing a state machine's edge cases (stuck-flag recovery, test-isolation
reset) rather than only its happy path.

## Final Assessment
No findings.
