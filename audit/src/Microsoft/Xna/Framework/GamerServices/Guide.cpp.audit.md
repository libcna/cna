# Audit: src/Microsoft/Xna/Framework/GamerServices/Guide.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/GamerServices/Guide.cpp`
- Audit status: AUDITED (full read, 864 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
Implements `Guide`'s real message-box and keyboard-input overlays (state machine, rendering,
real mouse/keyboard interaction, UTF-8↔UTF-16 conversion for text capture) plus every documented
no-op `Show*` entry point.

## Executive Verdict
Correct, and confirms the header's documented behaviors are genuinely implemented as described.
Both async families (`BeginShowMessageBox`/`EndShowMessageBox` and
`BeginShowKeyboardInput`/`EndShowKeyboardInput`) correctly invoke their stored `AsyncCallback`
exactly once, at the real completion point (a button click / Enter or cancel), not merely storing
it — confirming this shard's own Begin*/End* implementation does **not** share the
callback-never-invoked bug (Task 12) just found and fixed for `NetworkSession` in the sibling
`xna-net` shard. The `BeginShowKeyboardInput`/`BeginShowMessageBox` "already pending" checks both
throw `System::InvalidOperationException` — confirming the header's paired MEDIUM finding
(`GuideAlreadyVisibleException` is declared and tested but never actually thrown here).

## Checklist Results
- `CompletePendingMessageBox`/`CompletePendingKeyboardInput` (lines 117-127, 312-328): both capture
  the pending action pointer and clear the corresponding `pending*_` global **before** invoking the
  callback — correctly reentrancy-safe, explicitly citing the same fix already applied to
  `NetworkSession`'s Begin*/End* family (`audit_net.md` High finding) as the precedent.
- `DecodeUtf8ToUtf16`/`EncodeUtf16ToUtf8` (lines 135-233): manual UTF-8↔UTF-16 codec with malformed
  input substituting U+FFFD (matching `Encoding.UTF8`'s own real behavior) and correct
  surrogate-pair handling (`RemoveLastCodeUnit` correctly removes a preceding high surrogate when
  backspacing over a low surrogate, so one Backspace deletes a whole non-BMP code point, not half
  of one).
- `SimulateMessageBoxClickEXT` (lines 773-784): correctly validates `buttonIndex` via
  `System::ArgumentOutOfRangeException::ThrowIfNegative`/`ThrowIfGreaterThanOrEqual` before
  completing — a real project-provided exception type used correctly here (contrast with the
  `PropertyDictionary` finding in this same shard, which does not use the equivalent
  project-provided type for its own out-of-range/missing-key cases).

## Detailed Findings
None beyond the paired `.hpp` report's `GuideAlreadyVisibleException` finding — this file is the
concrete evidence for that finding (both "already pending" throw sites use
`InvalidOperationException`, confirmed at lines 392 and 623).

## Cross-File Observations
- `RenderPendingMessageBoxEXT`/`RenderPendingKeyboardInputEXT` both implement real edge-triggered
  input detection (`WasLeftMouseDown`/`WasEscapeDown` persisted on the pending action, comparing
  against the current frame's state) rather than re-triggering every frame the button/key is held —
  correct, standard UI-button semantics.
- `BeginShowMessageBox`'s empty-`buttons`-list check (`ArgumentException`, lines 617-620) is
  explicitly disclosed as a CNA-original validation decision, not an FNA behavior to match: "No FNA
  reference behavior exists for this validation (FNA's own BeginShowMessageBox is a permanent
  NotSupportedException stub...); this is a CNA-original, conservative default for a real
  implementation."

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The reentrancy-safe completion pattern (capture-and-clear-before-invoking-callback) is applied
consistently to both async families in this file, correctly citing the precedent this exact
project already established (and needed to retrofit) for `NetworkSession` — evidence the lesson
from that earlier fix was carried forward into new code, not just applied once and forgotten.

## Final Assessment
No new findings beyond the paired `.hpp` report's `GuideAlreadyVisibleException` MEDIUM finding,
which this file is the direct evidence for.
