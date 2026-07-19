# Audit: include/Microsoft/Xna/Framework/Audio/NoMicrophoneConnectedException.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Audio/NoMicrophoneConnectedException.hpp`
- Audit status: AUDITED (full read, 35 lines, header-only, no `.cpp`)
- Subsystem: `xna-audio` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Audio/NoMicrophoneConnectedException.cs`
  (base class plain `Exception`, NOT `ExternalException` -- confirmed via grep, and correctly
  differs from the other two audio exceptions in this shard for exactly this reason)
- Main related tests: not independently located in this pass

## Purpose
Exception thrown when a requested microphone device is not connected.

## Executive Verdict
Correct. All three constructors present; correctly uses `System::Exception` (not
`ExternalException`) as its base, matching FNA's own base-class choice for this specific exception
precisely -- FNA's `NoAudioHardwareException`/`InstancePlayLimitException` derive from
`ExternalException` while `NoMicrophoneConnectedException` derives from plain `Exception`, and this
port preserves that distinction correctly rather than applying a uniform base class across all
three.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
The base-class distinction from its two siblings in this shard (`NoAudioHardwareException`,
`InstancePlayLimitException`) is correctly preserved -- a good sign of careful, per-type FNA
verification rather than a blanket base-class choice applied to the whole file group.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct, verified base-class choice distinguishing it from its siblings.

## Final Assessment
No findings.
