# remediation/ — CNA Post-Audit Remediation Plan

**Status: CLOSED HISTORICAL ARCHIVE.** The remediation campaign has exited; see
`REMEDIATION_EXIT.md`. New implementation work is tracked in the relevant active file under
`plans/`, not by editing this directory.

This directory turns the completed, frozen repository-wide audit (`audit/`, 2297 per-file reports plus
6 synthesis documents) into a single, deduplicated, implementation-ready remediation plan.

## What this is

The audit found the same underlying defect many times over. A single mirrored fog formula appears in
4 backend groups, ~20 shader files, and dozens of per-file audit reports. A single missing world-space
normal transform appears in every backend that implements `SkinnedEffect`. A single `fs::path`
concatenation pitfall appears in three unrelated subsystems.

This plan collapses those into **root-cause remediation tasks**, not per-symptom tickets. One root
cause gets exactly one task, one owner, and one coordinated fix — even when it spans seven backends.

## Files

| File | Purpose |
|---|---|
| `MASTER_REMEDIATION_PLAN.md` | The authoritative task list. Every task, fully specified. |
| `REMEDIATION_INDEX.md` | Fast lookup: by ID, severity, priority, owner, backend, audit source. |
| `REMEDIATION_DEPENDENCIES.md` | Dependency graph, critical path, parallelization lanes, merge order. |
| `REMEDIATION_PROGRESS.md` | Frozen implementation log. It is not generated and must not receive new progress entries. |
| `REMEDIATION_TRACEABILITY.md` | Audit-finding → task mapping. Proves nothing was dropped. |

## Reading order

1. **`REMEDIATION_DEPENDENCIES.md` § Wave 1** — what to do first, and why order matters more than severity here.
2. **`MASTER_REMEDIATION_PLAN.md`** — the task you have been assigned, in full.
3. **`REMEDIATION_PROGRESS.md`** — read the historical execution record; do not update it.

## Ground rules for implementers

1. **The audit and remediation archive are frozen.** Do not edit `audit/**` or
   `REMEDIATION_PROGRESS.md` to reflect new fixes; record them in their active `plans/plan_*.md`.
2. **One root cause, one owner, one branch.** If your task says `PARALLEL_SAFE: NO`, it shares files
   with another task — coordinate before starting. Never fix the same root cause in two branches.
3. **Some tests assert the bug.** Three test files bake confirmed defects in as expected behavior
   (see `REMED-TEST-001`). Fixing the production code *requires* updating those tests in the same
   commit, or CI will go red for the right reason and be reverted for the wrong one.
4. **Verify before you fix.** Tasks marked `Verification required: YES` rest on static analysis that
   was never executed. Reproduce first; if the finding does not reproduce, record that outcome — a
   disproved finding is a real result, not a failed task.
5. **Test infrastructure comes first.** Until `REMED-BUILD-001` lands, `ctest` results for ~220 tests
   are meaningless. Do not use CTest pass/fail as evidence for anything before that.

## Scope boundary

This plan covers findings in the `cnaaudit` repository only. Sibling repositories (`easy-gl`,
`free-direct`, `sharp-runtime`, `bgfx`, `wgpu-native`) were reference-only in the audit per decisions
D-P2/D-6, and any defect bottoming out inside them is recorded as a scope boundary on the relevant
task, not silently absorbed.
