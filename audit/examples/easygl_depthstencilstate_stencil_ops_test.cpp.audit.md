# Audit: examples/easygl_depthstencilstate_stencil_ops_test.cpp

## Metadata

- Source file: `examples/easygl_depthstencilstate_stencil_ops_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard), Task 317
- File type: standalone `Game` subclass / pixel-readback integration test
- Related production code: `DepthStencilState` (`StencilFail`/`StencilDepthBufferFail`/`StencilPass` properties),
  `EasyGLGraphicsBackend::ApplyDepthStencilState` (`EasyGLGraphicsBackend.cpp:1924-1974`)
- FNA reference: `Graphics/States/DepthStencilState.cs` (`StencilFail`/`StencilDepthBufferFail`/`StencilPass`,
  `StencilOperation` enum semantics)
- Build registration: `cmake/Tests/EasyGLTests.cmake:1404-1409` (`EasyGL_DepthStencilState_StencilOps`) and
  `cmake/Tests/VulkanTests.cmake:184-190` (`Vulkan_DepthStencilState_StencilOps`) — shared source, two backend
  targets.

## Purpose

Verifies that the three per-face `StencilOperation` slots (`StencilFail`, `StencilDepthBufferFail`, `StencilPass`)
each fire independently and only under their own triggering condition, using 4 columns: one per operation slot plus
a 4th "contrast/control" column whose entire purpose is to prove the stencil test isn't bypassed wholesale.

## Executive Verdict

**Healthy.** This is the strongest-designed file in this batch: its own header comment (lines 37-56) explicitly
documents a real methodological bug the *authors themselves* found and fixed in an earlier draft (a version without
column 3 that "coincidentally passed all its checks on Vulkan too… giving the test zero power to detect the bug").
That kind of documented self-correction is a strong positive signal for the file's genuine validity, and the current
4-column design closes the gap.

## Checklist Results

### Purpose
PASS — correctly scoped to Task 317, single file, single responsibility.

### API / XNA / FNA parity
PASS — `setStencilFailProperty`/`setStencilDepthBufferFailProperty`/`setStencilPassProperty` correspond exactly to
FNA's `StencilFail`/`StencilDepthBufferFail`/`StencilPass` (`DepthStencilState.cs:116-186`). `StencilOperation`
values used (`Keep`, `Replace`, `Increment`, `Decrement`) are standard FNA `StencilOperation` enum members.

### Behavioral correctness
PASS, independently traced:
- `MakeStampState()` (lines 103-114) sets `DepthBufferFunction=Always`/writes depth, `StencilFunction=Always`/
  `StencilPass=Replace`/`ReferenceStencil=0x05` → establishes depth=0.5 and stencil=0x05 baseline. Correct — using
  `Always` for both compares during the stamp means winding/depth of the stamp draw itself can't accidentally fail.
- `MakeOpState(stencilPasses, depthFunc, onFail, onDepthFail, onPass)` (lines 119-134) sets the *other two* slots to
  `Decrement` as a deliberate trap — if the wrong slot fires, the buffer diverges from the expected `0x06` and the
  read-back check (which specifically requires `Equal, ReferenceStencil=0x06`) fails. This "trap the other slots"
  technique is a real test-design strength: a naive "does column N show green" test without decrement traps on the
  *other* slots could not distinguish "correct slot fired" from "some slot fired."
  - Column 0 (`StencilFail`): stencil ref forced to `0x99` (mismatch) so the compare fails deterministically,
    `depthFunc=Always` so depth trivially passes — isolates `StencilFail` as the only slot that can fire. Verified:
    only path to `Increment` is via the fail branch.
  - Column 1 (`StencilDepthBufferFail`): stencil passes (`0x05==0x05`), but drawn at `z=0.8` against `Less` compare
    vs. the `0.5` baseline — `0.8 < 0.5` is false, so depth fails while stencil passes → isolates
    `StencilDepthBufferFail`. Correct.
  - Column 2 (`StencilPass`): drawn at `z=0.2`, `0.2 < 0.5` true, both tests pass → isolates `StencilPass`.
  - Column 3 (contrast): identical draw to column 2 (buffer really does become `0x06`), but the read-back
    deliberately queries `ReferenceStencil=0x99` instead of `0x06` — a working implementation must reject this
    (expects `!IsGreen`, line 265 `expectGreen[3]=false`). This is the one column of the four that requires the
    stencil test to actively *reject* a fragment, closing the "bypassed stencil test" blind spot the file's own
    comment (lines 46-56) describes finding in an earlier draft.

### Logic
PASS — the read-back state (`MakeReadBackState`, lines 136-146) always disables depth testing and sets both
`StencilPass`/`StencilFail` to `Keep`, so the read-back draw itself is guaranteed not to further mutate the buffer
it's inspecting — a correctness precondition for the whole test that's easy to get wrong and is handled correctly
here.

### C++ correctness
PASS — no issues; parameter passing by value for small enums/bools is appropriate, no dangling references.

### Architecture
PASS — depth writes are correctly turned off during the operation draws (`setDepthBufferWriteEnableProperty(false)`,
line 125) so the *stamp*'s depth value (0.5, used as the comparison baseline for column 1/2) survives the operation
draw itself, which matters because the read-back draw (disabled depth) doesn't re-establish it.

### Maintainability
PASS — comments are dense but directly map to the code (each column's setup is preceded by a comment restating the
exact expected outcome), which is exactly the kind of "why" documentation this project's `CLAUDE.md` values.

### Cross-file consistency
**Finding F1 (see below)** — a real inconsistency between this file's own predicted Vulkan outcome and the
predicted outcome recorded in `cmake/Tests/VulkanTests.cmake`.

## Detailed Findings

### F1 — In-file Vulkan-behavior prediction and its CMake registration comment disagree

- Severity: LOW
- Confidence: MEDIUM (a genuine textual discrepancy was found; which one is actually correct can only be settled by
  running the Vulkan build, which is outside this shard's scope — flagged for the Vulkan shard's audit to close)
- Category: cross-file consistency / documentation accuracy
- Location: `examples/easygl_depthstencilstate_stencil_ops_test.cpp:46-56` vs.
  `cmake/Tests/VulkanTests.cmake:184-190`
- Evidence: this file's own header comment predicts, for the Vulkan backend, "expect columns 0-2 to pass on Vulkan
  purely by coincidence and column 3 to fail" (i.e. 3 pass / 1 fail). The CMake registration comment for the exact
  same source compiled as a Vulkan test says "expected to FAIL all 3 checks per Task 870 - stencil testing never
  gates on Vulkan, so no operation ever fires" — which reads as 3 (or more) columns failing, the opposite
  cardinality of what the source file itself predicts.
- Why it matters: these are two independent, checked-in claims about the same test's expected behavior on a
  specific backend; at most one can be correct. Low severity because it has zero bearing on EasyGL correctness (the
  subject of this shard) and doesn't indicate a code defect — only a doc-drift risk that could mislead whoever next
  investigates a Vulkan CI failure on this test.
- FNA/XNA comparison: N/A (Vulkan-backend-specific claim, not an XNA API question).
- Related files: `cmake/Tests/VulkanTests.cmake` (owned by the `backend-vulkan`/`ci-build` shard, not this one).
- Suggested action (not implemented by this audit): flag to the Vulkan shard/CMake shard audit to reconcile which
  prediction is actually correct by running the test, then fix whichever comment is stale.

No MEDIUM/HIGH/CRITICAL findings for the file's actual EasyGL-facing logic — it's correct and well cross-checked
against the real backend implementation.

## Missing or Weak Tests

No coverage of `StencilFail`/`StencilDepthBufferFail`/`StencilPass` interacting with `TwoSidedStencilMode` in the
same test (that combination is deferred to the sibling `easygl_depthstencilstate_stencil_twosided_test.cpp`, which
is appropriate — no duplication needed).

## Positive Findings

- The file's own comment documents a real prior test-design bug it found and fixed (missing differential power
  without column 3) — a genuinely rare and valuable piece of self-documented test evolution.
- The "trap the other two slots with Decrement" technique is a robust, well-reasoned way to isolate exactly one
  operation slot per column without needing three separate frames/devices.

## Final Assessment

A rigorously-designed differential test, verified correct against the real EasyGL `ApplyDepthStencilState`
implementation and FNA's `DepthStencilState` property semantics. Only issue found is a low-severity, out-of-scope
documentation inconsistency about Vulkan-specific expected behavior (F1), which does not affect this file's EasyGL
correctness.
