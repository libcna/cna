# Audit: src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- Audit status: AUDITED (full read, 147 lines)
- Subsystem: `xna-input` shard
- File type: C++ implementation
- XNA/FNA relevance: N/A — `NOXNA`/FNA extension; `SetInputRectangle()`'s cursor-offset-0 choice
  verified against FNA `SDL3_FNAPlatform.cs` line 779
- Main related tests: not independently located in this pass

## Purpose
Implements SDL3 text-input/on-screen-keyboard control, the `TextInputTypeEXT`-to-SDL3 enum mapping,
and dispatch of the three text-input events.

## Executive Verdict
Correct. `SetInputRectangle()`'s cursor-offset-0 argument to `SDL_SetTextInputArea()` is verified to
match FNA's own choice exactly, including citing FNA's own unresolved `// FIXME SDL3: Do we need a
cursor here?` comment — CNA correctly follows FNA's current (admittedly-uncertain) behavior rather
than inventing a different offset FNA itself doesn't have. Every SDL call is correctly guarded
against a null/unset window handle.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
None beyond the paired `.hpp` report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correctly follows FNA's own acknowledged-uncertain behavior rather than silently diverging from it.

## Final Assessment
No findings.
