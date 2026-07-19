# Audit: tests/Microsoft/Xna/Framework/Graphics/GraphicsDeviceDefaultStateTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/GraphicsDeviceDefaultStateTests.cpp` (168 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `GraphicsDevice.hpp`/`.cpp` default `BlendState`/`DepthStencilState`/
  `RasterizerState`, and `ReferenceStencil` propagation
- Main related tests: N/A (this IS a test file)

## Purpose
Verifies `GraphicsDevice`'s default-constructed state properties actually match FNA's documented
defaults (`BlendState.Opaque`, `DepthStencilState.Default`, `RasterizerState.CullCounterClockwise`)
by `Name` (not just coincidentally-matching values), plus `ReferenceStencil` propagation when a
`DepthStencilState` is assigned wholesale (Task 319).

## Executive Verdict
Excellent, historically-grounded test file: several tests and their comments explicitly document a
recurring bug shape found and fixed multiple times in this codebase — a state property's default
constructor happened to numerically coincide with the FNA-documented preset's values, silently
masking the fact that the *actual* preset object was never being used, until each preset gained a
distinguishing `Name` field to catch the divergence (Tasks 302, 312, 321). Not directly one of the
10 assigned cross-check items, but a valuable methodological pattern: **matching values alone is
insufficient to prove a default equals the "real" documented preset instance** — this is exactly
the kind of coincidental-pass risk this fork's own investigation (of round-trip-only Matrix tests)
also flagged for `EffectParameterTests.cpp`.

## Checklist Results
- `MutatingBlendStateAfterAssignmentDoesNotAffectDevice`/
  `MutatingRasterizerStateAfterAssignmentDoesNotAffectDevice` correctly document and test a genuine,
  disclosed architectural deviation from FNA: CNA stores state objects by value (copy-on-assign)
  where FNA stores them by reference (mutating the original object afterward is visible in FNA, not
  in CNA) — confirmed via direct FNA source read, and explicitly justified as project-wide,
  intentional, and not observed to break any game code.
- `AssigningDepthStencilStatePropagatesReferenceStencil` is a real regression test for a genuine
  bug (Task 319): assigning a whole `DepthStencilState` didn't propagate its own
  `ReferenceStencil` into the device's separate `referenceStencil_` field.

## Detailed Findings
None.

## Cross-File Observations
The "values coincidentally match, only Name distinguishes them" pattern documented here (Tasks 302,
312, 321) is a useful general lesson: any test elsewhere in this codebase that verifies a default
value purely by comparing individual fields (not full-object identity or a distinguishing `Name`/id)
risks the same class of false-positive as these tests originally exhibited before being fixed.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The comments' historical narration of "values coincided until X existed to distinguish them" is
some of the most methodologically valuable test documentation encountered in this audit — it
teaches the general principle (test full identity, not just numeric coincidence) rather than just
fixing the one instance.

## Final Assessment
No findings; strong methodological positive example, cross-referenced above.
