# Audit: examples/demo_input/src/InputDemo.cpp

## Metadata
- Source file: `examples/demo_input/src/InputDemo.cpp` (636 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_input` shard
- File type: standalone `Game`-subclass demo implementation
- XNA/FNA relevance: exercises `Keyboard::GetState`/`Mouse::GetState`/`Mouse::SetPosition`/
  `GamePad::GetState`/`GamePad::SetVibration`, `TouchPanel::Update`/`GetState`/`ReadGesture`, plus
  NOXNA extensions `TextInputEXT`, `Mouse::setIsRelativeMouseModeEXTProperty`,
  `GamePad::SetLightBarEXT`/`GetGyroEXT`/`GetAccelerometerEXT`
- Related production code: `xna-input`/`tests-xna-input`/`cna-input` shards (already fully audited,
  no HIGH findings)

## Purpose
Drives every input peripheral surface CNA exposes on a single screen, and doubles as a manual UTF-8
codec for `TextInputEXT`'s UTF-16 code-unit stream.

## Executive Verdict
Correct throughout, including a non-trivial piece of logic verified independently: `AppendTextCodeUnit`'s
UTF-16-surrogate-pair-to-UTF-8 conversion (lines 117-164). Manually re-derived and confirmed
bit-exact against the standard algorithm: high surrogate range check (`0xD800`-`0xDBFF`), low
surrogate range check (`0xDC00`-`0xDFFF`), combination formula
`0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00)` (matches the standard UTF-16 decode formula,
since `<< 10` is equivalent to the canonical `× 0x400`), and correct 1/2/3/4-byte UTF-8 encoding
branches with correct continuation-byte masks (`0x80 | (cp & 0x3F)`, etc.) at every code-point-size
boundary (`0x80`, `0x800`, `0x10000`).

## Checklist Results
- Destructor (lines 60-68) detaches `TextInputEXT::TextInput`/`TextEditing` and calls
  `StopTextInput()` **before** `delete spriteBatch_` — correct ordering, since these are
  process-global callback slots that could otherwise be invoked with a dangling `this` capture after
  partial teardown.
- An unpaired low surrogate (no pending high surrogate) is defensively dropped (line 128:
  `if (pendingHighSurrogate_ == 0) return;`) rather than mis-decoded — correct defensive handling of
  malformed input.
- A stale, never-completed high surrogate is silently discarded the moment any subsequent
  non-surrogate code unit arrives (the `else` branch at line 136 unconditionally clears
  `pendingHighSurrogate_`) — reasonable behavior for a demo (a genuinely malformed/truncated
  surrogate stream), not flagged as a defect.
- Every EXT-suffixed capability call (`SetLightBarEXT`, `GetGyroEXT`, `GetAccelerometerEXT`,
  `SetVibration`) is correctly treated as capability-gated / a harmless no-op on unsupported or
  disconnected controllers, per this file's own comments (lines 218-219, 227-228) — consistent with
  the `xna-input` shard's own confirmed design for these APIs.
- `GamePad::SetVibration` is driven from trigger pressure for all 4 player slots every frame
  (lines 220-225), correctly using `PlayerIndex::One`..`Four` rather than hardcoding a single slot.

## Detailed Findings
None.

## Cross-File Observations
Directly and correctly exercises `TouchPanel::getIsGestureAvailableProperty`/`ReadGesture` in a
drain loop (lines 210-215) and `GestureType`'s `[Flags]` composition (`Tap | FreeDrag | Flick`,
line 57) — consistent with the `xna-input` shard's own confirmed `GestureType` flag-composability
findings elsewhere in this audit (cf. the `SpriteEffects` missing-`operator|` finding in
`xna-graphics`, which `GestureType` does not share).

## Missing or Weak Tests
Not applicable — manual/visual demo.

## Positive Findings
The UTF-16-to-UTF-8 conversion is genuinely correct, non-trivial logic for a demo file to get right,
and it was verified here independently rather than assumed correct because "it's just a demo."

## Final Assessment
No findings.
