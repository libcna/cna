# Audit: tools/fna-reference/README.md

## Metadata
- Source file: `tools/fna-reference/README.md` (84 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-fna-reference` shard
- File type: documentation
- XNA/FNA relevance: documents the `FnaReference` tool's purpose, prerequisites, and per-task
  status history
- Main related tests: N/A

## Purpose
Documents prerequisites (mono/xbuild, a locally-built `FNA.dll`), build/run instructions, and a
detailed, task-by-task status history (Tasks 471-480) of this reference-generation tool's
development.

## Executive Verdict
Accurate against the actual tool files audited alongside this README, with one notable point worth
flagging for a future pass: the Task 473 status note's claim that "Every single value across all
17 types matches Task 197's golden table exactly" is true as far as it goes, but — per this
session's own direct read of `PackedVectorReference.cs` — does **not** account for the fact that
the specific test inputs chosen for `Byte4`/`Short2`/`Short4` are exclusively integer-valued, so
this "exact match" claim is not actually evidence that CNA's own rounding behavior for those 3
types is correct (a separate, already-confirmed-buggy question this README doesn't address, since
Task 473 is FNA-vs-Task-197 only, not FNA-vs-CNA).

## Checklist Results
- Build/run instructions match `FnaReference.csproj`'s own actual configuration exactly (same
  `xbuild`/`mono` commands, same default output filename).
- The Task 479 status note accurately describes a real, specific, dated, fixed defect
  (`IndexElementSize`'s `SixteenBits=0`/`ThirtyTwoBits=1` vs. CNA's then-`16`/`32`, "tracked as Task
  921 and fixed 2026-07-09") — a concrete, falsifiable, well-evidenced claim rather than a vague
  status update.
- The Tasks 474/475/477/478 deferral note gives a specific, technically substantive reason (a live
  `GraphicsDevice` needs a native `FNA3D` build with its own uninitialized `MojoShader` submodule
  and unresolved SDL2/SDL3 linkage) rather than a vague "not done yet."

## Detailed Findings
None new beyond the cross-file note above (already captured as the HIGH finding in
`PackedVectorReference.cs.audit.md` — this README simply doesn't yet reflect that finding, which is
expected since this audit is what surfaced it).

## Cross-File Observations
See `PackedVectorReference.cs.audit.md` for the HIGH-severity finding this README's Task 473 status
note doesn't (and couldn't have, at authoring time) account for.

## Missing or Weak Tests
Not applicable — this is documentation.

## Positive Findings
The task-by-task status history is unusually detailed and specific (dated fixes, exact defect
descriptions, explicit reasoning for deferrals) — a strong example of documentation that would let
a future maintainer understand not just *what* was built but *why*, and *what remains uncertain*.

## Final Assessment
No findings in the README's own accuracy against the current tool files; the Task 473 status claim
is accurate on its own narrow terms (FNA-vs-Task-197) but should ideally be updated to note the
integer-only-input gap this session's audit found once that finding is acted on.
