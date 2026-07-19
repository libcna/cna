# Audit: src/Microsoft/Xna/Framework/Audio/AudioEmitter.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Audio/AudioEmitter.cpp`
- Audit status: AUDITED (full read, 71 lines)
- Subsystem: `xna-audio` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Audio/AudioEmitter.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `AudioEmitter`'s defaults (`DopplerScale=1.0f`, `Forward`, `Position=Zero`, `Up`,
`Velocity=Zero`) and property accessors.

## Executive Verdict
Correct. Matches FNA's defaults exactly, and `setDopplerScaleProperty()`'s negative-value guard
correctly uses `System::ArgumentOutOfRangeException` -- the established convention, applied
correctly here (a positive contrast to several exception-type inconsistencies found elsewhere this
session).

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
None beyond the paired `.hpp` report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct default values and correct use of the project's established `System::ArgumentOutOfRangeException`
convention.

## Final Assessment
No findings.
