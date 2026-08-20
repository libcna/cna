# Audit: docs/dualtextureeffect-support.md

## Metadata
- Source file: `docs/dualtextureeffect-support.md` (139 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (phase-closure support matrix)
- XNA/FNA relevance: documents `DualTextureEffect` conformance across EasyGL/Vulkan/Bgfx
- Related audit: `xna-graphics` shard (this session, 191 files, 6 HIGH findings — none in this
  specific effect class)

## Purpose
Closes out Phase 44 (`plans/plan_graphics.md` Tasks 381-390): documents `DualTextureEffect`'s
property/default audit, blend-formula bug (`color.rgb *= 2` doubling factor), alpha
premultiplication, texture null-fallback, fog, and cross-backend consistency work.

## Executive Verdict
Internally consistent and consistent with sibling docs audited in this same batch. Its own "Task
868 is now fixed (2026-07-09)" cross-reference to the Vulkan BlendState fix matches
`docs/graphics-backend-feature-matrix.md`'s account exactly (same task number, same fix date).
Its "Task 888 ... Since fixed by Task 899" note is likewise internally consistent with
`docs/environmentmapeffect-support.md`'s identical Task 899 citation (both audited in this batch).

## Checklist Results
- Task 383's "doubling factor" bug write-up correctly explains why it was invisible to every prior
  test (Tasks 133/135/191/293/294/296/297 all used pure 0/1-saturated texture values, where a
  missing `×2` clamps back to the same saturated result) — a specific, falsifiable, and
  plausible root-cause account, not a vague "found a bug."
  the same 0/1-saturation blind spot pattern independently identified in
  `docs/environmentmapeffect-support.md`'s Task 394 (white-cubemap sub-case) — cross-referenced
  correctly there ("the same 0/1-saturation blind spot Task 383 already taught this project to
  watch for"), confirming the two documents' cross-citations are mutually consistent, not just
  independently plausible.
- The Task 385 note about Vulkan's `BlendState` being "known-fake" at the time (Task 868) and the
  test being narrowed to alpha-channel-only as a workaround, "not yet revisited to add the fuller
  RGB check now that the workaround's reason no longer applies" — is an honestly-disclosed,
  still-open test-coverage gap (not a behavior bug), correctly distinguished from a production
  defect.
- The "Open, tracked follow-up work" section's Task 889 (`VertexColorEnabled` total no-op on all 3
  backends) is presented as a genuinely new multi-shader-file feature gap, not silently deferred —
  consistent with this project's general practice of flagging incomplete features explicitly
  rather than omitting them from a support matrix.

## Detailed Findings
None. No stale or contradicted claims found against this session's other audited material.

## Cross-File Observations
- Mutually consistent with `docs/environmentmapeffect-support.md` (audited alongside this file) on
  the Task 868/888/899 fix timeline and the "0/1-saturation blind spot" methodology lesson.
- `docs/graphics-backend-feature-matrix.md`'s own note ("several per-effect `docs/*-support.md`
  files... predate Tasks 885-900's fog/lighting/specular fixes on Vulkan/Bgfx and still show some
  of these rows as gaps... due for a refresh") does **not** apply to this specific document's fog
  row — this doc's own Task 388/899 fog account is already up to date (explicitly notes the Task
  899 fix), unlike whatever older per-effect docs that feature-matrix note is warning about. Worth
  noting this document is NOT one of the stale ones that warning refers to.

## Missing or Weak Tests
Task 385's own account already flags the real, still-open test-coverage gap itself (Vulkan's
premultiplication test remains alpha-channel-only, narrower than EasyGL/Bgfx's full RGB check) —
correctly self-identified, not found independently by this audit pass.

## Positive Findings
This document's own historical bug write-ups consistently explain *why* a bug was invisible to
prior tests (the 0/1-saturation blind spot, recurring across Tasks 383/394) — genuinely useful
methodological documentation that helped later phases (`EnvironmentMapEffect`) anticipate and avoid
the same blind spot proactively, and the cross-document citations between the two are accurate.

## Final Assessment
No findings. An accurate, well-cross-referenced phase-closure document.
