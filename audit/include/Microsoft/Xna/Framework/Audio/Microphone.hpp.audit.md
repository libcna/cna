# Audit: include/Microsoft/Xna/Framework/Audio/Microphone.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Audio/Microphone.hpp`
- Audit status: AUDITED (full read, 166 lines)
- Subsystem: `xna-audio` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Audio/Microphone.cs`
- Main related tests: not independently located in this pass

## Purpose
Represents a microphone capture device: enumeration (`All`/`Default`), capture control
(`Start`/`Stop`), buffered-read (`GetData`), and the `BufferReady` polling event.

## Executive Verdict
Correct, and a genuinely real (not stub) implementation backed by SDL3 audio-capture APIs. The
visibility mapping for FNA's `internal void CheckBuffer()` is explicitly and correctly disclosed
(kept private, with the public `CheckAllBuffers()` static as the sanctioned bridge for
`FrameworkDispatcher`) -- a direct, well-reasoned application of CLAUDE.md's own Visibility Mapping
rule, citing the specific task ID (MC-6) that established it.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
`GetSampleDuration()`/`GetSampleSizeInBytes()` correctly delegate to `SoundEffect`'s static
equivalents (audited separately, confirmed correct).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Real SDL3-capture-backed implementation (not a stub); correct, explicitly-justified visibility
mapping for the FNA-internal `CheckBuffer()` method.

## Final Assessment
No findings.
