# Audit: docs/input-backend.md

## Metadata
- Source file: `docs/input-backend.md` (324 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (architecture reference)
- XNA/FNA relevance: describes how CNA wires SDL3 input events to
  `Microsoft::Xna::Framework::Input`/`Input::Touch`
- Related audit: `xna-input`/`tests-xna-input`/`cna-input` shards (this session, fully audited,
  exhaustive Buttons(31)/Keys(160) parity confirmed against FNA, no HIGH findings)

## Purpose
Architecture reference for the SDL3-event → XNA-state pipeline: `SdlInputBridge`/`InputManager`/
`GestureDetector`, the complete SDL-event-to-XNA-effect mapping table, the event-driven-vs-FNA's-
poll-driven architectural deviation, per-device fidelity notes, how to run input tests, and the
single-threaded concurrency contract.

## Executive Verdict
Accurate, precise, and consistent with this session's own `xna-input`/`tests-xna-input`/`cna-input`
shard audits (all fully audited, no HIGH findings, exhaustive `Buttons`/`Keys` FNA-parity confirmed).
This is one of a well-maintained 10-document Input cluster sharing an identical cross-reference
banner across every file — a genuinely good practice for keeping a large, multi-file documentation
set navigable and avoiding the "which doc is authoritative" ambiguity problem this audit found
between `docs/graphics-resource-lifetime.md` and `docs/graphicsresource-fna-audit.md`.

## Checklist Results
- The event-driven-vs-poll-driven architectural deviation (§3) is explained with genuine specificity
  about *where* the difference is actually observable (multiple `Get*State()` calls per frame;
  `TouchPanel::GetState()`'s `SetFinger` dead code path; `GamePadState.PacketNumber` bump timing;
  mouse relative-mode delta draining) rather than a vague "it's different but equivalent" hand-wave.
- The SDL-event-to-XNA-effect mapping table (§2) is exhaustive and includes explicitly-documented
  "intentionally unhandled" event types (`SDL_EVENT_GAMEPAD_REMAPPED`, `SDL_EVENT_SENSOR_UPDATE`)
  with a specific FNA cross-reference confirming FNA itself has no handler either — a genuinely
  useful negative-space disclosure (what's deliberately NOT handled, and why that's correct) that
  most architecture docs skip.
- §6 ("Thread safety")'s claim that no `mutex`/`atomic`/`thread` exists anywhere under
  `src/CNA/Internal/Input/`/`src/Microsoft/Xna/Framework/Input/` is a falsifiable, specific claim —
  consistent with the general single-threaded design this project's Input subsystem is known (via
  this session's own `cna-input` shard audit) to follow.
- The touch-coordinate dual-scaling-path explanation (two independent normalized→pixel conversions
  feeding two different consumers: `to_touch_pixel_position` for `InputManager`, a separate scale by
  `TouchPanel::DisplayWidth/Height` for `GestureDetector`) is a genuinely subtle architectural detail,
  explained precisely enough to be independently checkable against source.

## Detailed Findings
None.

## Cross-File Observations
Consistent with `docs/input-fna-fidelity.md` and `docs/input-manual-verification-results.md`
(both read in this batch) on the event-driven-vs-poll-driven framing and the specific per-device
fidelity claims (dead-zone constants, `PacketNumber` semantics, touch previous-location tracking).
No contradiction found across the 10-document Input cluster read in this batch.

## Missing or Weak Tests
N/A for this document itself — it describes, rather than needs, its own tests; `docs/input-test-coverage.md`
(read in this batch, already reports 0 orphans) is the relevant coverage claim.

## Positive Findings
The "task-number scheme" disambiguation note at the top (three generations of task numbering
across `plans/plan_input.md`'s revisions, explicitly warning readers not to infer current-plan status
from a legacy citation) is an unusually disciplined piece of documentation hygiene — proactively
heading off a real class of confusion (stale task-ID citations misread as current status) that this
audit found actually happening in several *other* documents this session (e.g. task numbers cited
without a "still open"/"since fixed" qualifier).

## Final Assessment
No findings. An accurate, precise architecture document, consistent with this session's own
independently-audited Input source.
