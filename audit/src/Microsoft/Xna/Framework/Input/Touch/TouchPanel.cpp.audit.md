# Audit: src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- Audit status: AUDITED (full read, 332 lines) — last file of the `xna-input` shard (44/44 complete)
- Subsystem: `xna-input` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/Touch/TouchPanel.cs`;
  `GetCapabilities()`'s "completely bogus" `MaximumTouchCount = 4` citation verified against FNA's
  own comment in both `SDL2_FNAPlatform.cs` and `SDL3_FNAPlatform.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements display-metric/gesture-enable property accessors, `GetCapabilities()`,
`GetState()`'s `SetFinger`-array-or-`InputManager`-fallback logic, `ReadGesture()`/`EnqueueGesture()`,
`INTERNAL_onTouchEvent()`'s coordinate scaling and dispatch to `GestureDetector`, `SetFinger()`'s
per-slot state-transition logic, and `Update()`.

## Executive Verdict
Correct, and every non-obvious deviation is disclosed with a specific citation. `GetState()`'s
fallback-to-`InputManager` path (lines 132-140) is a clearly-explained, genuine architectural
deviation from FNA's poll-driven model (the same pattern already seen in `Mouse`/`GamePad`): FNA
feeds `touches_` via a per-frame platform poll; CNA's event-driven `SdlInputBridge` doesn't drive
`SetFinger`/`touches_` in production, so `GetState()` falls back to `InputManager`'s event-driven
snapshot, correctly capped at `MAX_TOUCHES` to match FNA's fixed-array behavior (citing `DEC-10`).
`INTERNAL_onTouchEvent()`'s display-size guard (citing task 828) correctly drops touch events before
the display size is published, preventing bogus corner-position gestures from a zero-size scaling
divide. `GetCapabilities()`'s fixed `MaximumTouchCount = 4` and "completely bogus" characterization
were independently verified against FNA's actual source comment, not merely asserted.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
`INTERNAL_onTouchEvent()` dispatches to `CNA::Internal::Input::GestureDetector::OnPressed/OnMoved/OnReleased`
— the real gesture-recognition algorithm, out of scope for this shard (see the paired `.hpp`
report). `ResetForTests()` correctly also resets `displayWidth_`/`displayHeight_`/
`displayOrientation_`/`windowHandle_`, explicitly citing why: a leaked display size from a prior
test would otherwise silently corrupt another test's touch/gesture coordinates via the same
zero-size guard mentioned above — this replaced a previous save/restore workaround in the touch
tests themselves, a genuine test-infrastructure improvement.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Every architectural deviation from FNA (poll-vs-event-driven touch state, the `MAX_TOUCHES` cap,
the display-size guard) is clearly disclosed with its exact rationale and, where applicable, a
specific verified task/decision citation.

## Final Assessment
No findings. This is the last file of the `xna-input` shard (44/44 complete) — like `xna-audio`,
this shard showed an exceptionally strong, consistent track record: every file checked matched FNA
exactly (including several automated/spot-checked exhaustive value comparisons — `Buttons`, `Keys`,
`GamePadDPad`/`GamePadThumbSticks`/`GamePadTriggers`'s dead-zone formulas), with only two very
minor, functionally-inconsequential documentation-framing notes (`GamePadState`/`MouseState`'s
`GetHashCode()` comments).
