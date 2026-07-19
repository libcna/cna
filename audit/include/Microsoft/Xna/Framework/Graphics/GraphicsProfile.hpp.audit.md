# Audit: include/Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp` (16 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/GraphicsProfile.cs`
- Main related tests: not independently located in this pass

## Purpose
Defines the `Reach`/`HiDef` graphics capability profiles.

## Executive Verdict
Correct, trivial. Values and order match FNA exactly.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Consumed by `GraphicsAdapter::IsProfileSupported()`/`QueryRenderTargetFormat()`/
`QueryBackBufferFormat()` (audited separately) — real, hardware-backed checks on D3D9, honest
`true`/`Color`-fallback on the other nine backends.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Minimal, correct.

## Final Assessment
No findings.
