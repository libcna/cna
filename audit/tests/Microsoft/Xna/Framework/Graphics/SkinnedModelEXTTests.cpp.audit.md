# Audit: tests/Microsoft/Xna/Framework/Graphics/SkinnedModelEXTTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/SkinnedModelEXTTests.cpp` (467 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `SkinnedModelEXT.hpp`/`.cpp` (NOXNA glTF-derived skeletal-animation
  extension, no FNA/XNA equivalent — real XNA has no runtime skeletal-animation-blend API)
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises `ComputeBoneTransformsEXT`'s hierarchy composition, clip-time sampling
(start/mid/end/clamp/loop), corrupt-data rejection (mismatched array sizes, forward-referenced or
self-referencing parent bones), and `AddPartEXT`/`AttachPartEXT`/`RemovePartEXT`'s GPU-resource
ownership bookkeeping (avatar wardrobe-swapping use case).

## Executive Verdict
Exceptionally rigorous, with two standout regression tests. `RotatingBoneKeepsItsOwnBindPivotFixed`
(Task 11.20b) is a genuinely sophisticated, order-discriminating test: its own comment explains that
every OTHER test in this file uses pure translations (which commute and thus cannot distinguish
multiply order), so this test deliberately combines a nontrivial bind-pose offset with a real
rotation specifically to catch the `InverseBindPoseGlobal[i] * worldTransforms[i]` product-order bug
that caused a real, previously-observed elongated/detached-forearm rendering artifact. This is the
same class of order-discriminating rigor already praised in `ModelTests.cpp`'s
multiply-order-verifying transform test. `WrapsHugePositionInBoundedTime` (Task 11.1) is a genuine
performance-regression guard, using exact integer-tick arithmetic (avoiding floating-point drift) to
construct a position ~1 billion clip-durations past the end and asserting the loop-based wraparound
completes in under 100ms — a real algorithmic-complexity bug (unbounded cost proportional to
position/Duration) caught and locked in.

## Checklist Results
- `MismatchedArraySizesThrows`/`ForwardReferencedParentThrows`/`SelfParentThrows` correctly test
  real, deliberately-added defensive validation (Tasks 11.2/11.3) against corrupt/malformed
  `.skeleton.bin` data, using `System::ArgumentException` (project convention, not a raw `std::`
  exception).
- `OutOfRangeOrNegativeBoneIndexTrackIsSkippedSafely` (Task 13.2) correctly confirms a
  defensive bounds check is actually exercised, not just present as dead code.
- `RemovePartFreesOwnedResources`/`AttachingSameNamedPartReplacesTheOldOne` are genuine regression
  tests for real, previously-found resource-leak/duplicate-part bugs (Tasks 11.4/11.5) in the avatar
  wardrobe-swap system — directly analogous in spirit to the `ModelMeshPartTests.cpp` effect
  reference-counting tests.

## Detailed Findings
None.

## Cross-File Observations
`RotatingBoneKeepsItsOwnBindPivotFixed`'s explicit "every other test here uses pure translations and
so cannot distinguish multiply order" reasoning is a valuable, recurring methodological lesson in
this shard (also seen in `ModelTests.cpp`) — worth flagging generally: any transform-composition test
using only translations cannot validate multiply order and should not be treated as sufficient
coverage for that class of bug.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Two genuinely rigorous, real-bug-catching regression tests (rotation-pivot order bug causing a
visible rendering artifact; unbounded-cost wraparound performance bug) — strong examples of
test-driven bug discovery in a NOXNA extension with no FNA reference to check against.

## Final Assessment
No findings; strong positive example of order-discriminating and performance-regression test
design.
