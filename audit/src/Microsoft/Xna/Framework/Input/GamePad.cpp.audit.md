# Audit: src/Microsoft/Xna/Framework/Input/GamePad.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- Audit status: AUDITED (full read, 157 lines)
- Subsystem: `xna-input` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/GamePad.cs` — `ExcludeAxisDeadZone`
  verified line-for-line identical
- Main related tests: not independently located in this pass

## Purpose
Implements `ExcludeAxisDeadZone()` and `GetState()`'s raw-state-to-XNA-struct conversion; every EXT
method is a thin delegation to `CNA::Internal::Input::SdlInputBridge` (already covered under Task
#3's `cna-input` internal-backend shard).

## Executive Verdict
Correct, and the `GetState()` comment (lines 33-42) is a strong, honest disclosure of a real
architectural deviation from FNA: FNA is poll-driven (`SDL3_FNAPlatform.GetGamePadState()` re-queries
SDL fresh on every call), while CNA is event-driven (`SdlInputBridge::ProcessEvent` accumulates state
as SDL events arrive; `GetState()` just reads the accumulated snapshot). The comment correctly
identifies the practical consequence: this is only "current" because `Game::Tick()` polls events
exactly once per frame before `Update()`/`Draw()` run, and a caller that drives `InputManager`
directly (bypassing `Game::Tick()`, as tests do) sees only whatever was last pushed in, with no
implicit polling. This is a well-understood, disclosed trade-off, not a silent gap.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
`ExcludeAxisDeadZone()` verified line-for-line identical to FNA. The ~18 EXT methods all correctly
delegate to already-audited `SdlInputBridge` methods (Task #3's `cna-input` shard).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The poll-vs-event-driven architectural deviation is clearly disclosed with its exact practical
consequence spelled out, rather than left for a future maintainer to discover by surprise.

## Final Assessment
No findings.
