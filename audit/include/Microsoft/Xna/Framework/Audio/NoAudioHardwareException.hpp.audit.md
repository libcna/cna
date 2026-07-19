# Audit: include/Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp`
- Audit status: AUDITED (full read, 36 lines, header-only, no `.cpp`)
- Subsystem: `xna-audio` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Audio/NoAudioHardwareException.cs`
  (base class `ExternalException`, confirmed via grep)
- Main related tests: not independently located in this pass

## Purpose
Exception thrown when no audio hardware is available.

## Executive Verdict
Correct. All three constructors present, matching FNA's shape and base class exactly.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Same correct pattern as `InstancePlayLimitException` (audited alongside this file).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Complete, correct three-constructor exception shape.

## Final Assessment
No findings.
