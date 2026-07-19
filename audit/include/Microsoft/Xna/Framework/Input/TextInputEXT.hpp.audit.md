# Audit: include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- Audit status: AUDITED (full read, 176 lines)
- Subsystem: `xna-input` shard
- File type: C++ header
- XNA/FNA relevance: N/A — entirely `NOXNA`/FNA extension (XNA 4.0 had no portable text-input event)
- Main related tests: not independently located in this pass

## Purpose
Text input events (`TextInput`/`TextEditing`/`TextEditingCandidatesEXT`) and on-screen keyboard
control.

## Executive Verdict
Correct, and the threading/encoding note (lines 33-37, citing `INPUT-TEXT-016`) is a genuinely
useful piece of API documentation: it correctly warns that `TextEditing`'s composition string is
UTF-8 with byte (not code-point) offsets, and that events are dispatched on the game-loop thread —
exactly the kind of detail a consumer needs and could easily get wrong otherwise. `TextInput`'s
UTF-16-code-unit/surrogate-pair semantics are correctly documented to match FNA's own
`Encoding.UTF8.GetChars` decode behavior for a code point above U+FFFF.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Same "strict-XNA header" forward-declaration policy as `GamePad.hpp`/`Keyboard.hpp` (audited
separately).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The threading/UTF-8-byte-offset warning is genuinely valuable, non-obvious API documentation.

## Final Assessment
No findings.
