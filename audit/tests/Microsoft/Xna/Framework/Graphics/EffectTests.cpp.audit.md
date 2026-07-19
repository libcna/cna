# Audit: tests/Microsoft/Xna/Framework/Graphics/EffectTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/EffectTests.cpp` (460 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Effect.hpp`/`.cpp` (base class), `EffectPass.hpp`/`.cpp`,
  `EffectTechnique.hpp`/`.cpp`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises the `Effect` base class's construction contract (single default "Default" technique),
the bytecode-constructor safety-net throw (Task 353), `CurrentTechnique`'s no-validation setter,
`Apply()`/`OnApply()` dispatch, `EffectPass::Apply()`'s "not in current technique" guard (Task
355), disposal (idempotency, apply-after-dispose), and `Clone()`'s base-class contract.

## Executive Verdict
Exceptionally thorough and well-reasoned, with several tests explicitly documenting and justifying
deliberate CNA-specific safety improvements over FNA's own real (and less safe) behavior — e.g.
`ApplyAfterDisposeThrowsObjectDisposedException`'s comment explicitly notes "FNA's own
`EffectPass.Apply()`/`Effect` has NO `IsDisposed` check at all... calling `Apply()` on a disposed
FNA effect hands a zeroed-out native handle straight to `FNA3D_ApplyEffect`, undefined behavior at
the native layer. CNA's `Effect::Apply()` throwing `ObjectDisposedException` is a deliberate,
confirmed, beneficial CNA-specific safety improvement over FNA's silent UB here, not a bug to
match."

## Checklist Results
- `BytecodeConstructorThrowsNotImplementedException`/`...MessageMentionsPhase74Roadmap` correctly
  test a deliberate "interim safety net" (Task 353): the bytecode constructor exists to match FNA's
  public API surface but throws a clear, informative exception rather than silently building a
  broken `Effect`, until the real MojoShader-equivalent pipeline (Phase 74) lands.
- `ApplyOnPassNotInCurrentTechniqueThrowsInvalidOperationException`/
  `ApplyWithNullCurrentTechniqueThrowsInvalidOperationException` correctly test Task 355's guard,
  with the null-technique case explicitly documented as a deliberate divergence from FNA's own
  undefined-behavior null-dereference crash — mapped to a defined, catchable exception instead.
- `ApplyIsConsistentAcrossInterleavedTechniqueSwitches` is a genuinely rigorous bidirectional test:
  switches `CurrentTechnique` back and forth and re-verifies the guard's correctness in both
  directions, not just a single one-way check.
- `CloneAfterDisposeDoesNotThrow`'s comment is a careful, explicit reasoning about *why* this
  specific behavior (unlike `Apply()`) doesn't need a disposed-guard, with an explicit caveat that
  this should be revisited if a future `Clone()` implementation starts touching backend state —
  a mature acknowledgment of the test's own current scope limits.

## Detailed Findings
None.

## Cross-File Observations
This file's base-class-level `Clone()` tests (`CloneThroughBasePointerDispatchesToDerivedOverride`,
`CloneGetsIndependentTechniqueNotAliasedToOriginal`) complement, rather than duplicate, each stock
effect's own field-copy-specific `Clone()` tests audited elsewhere in this batch.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The explicit, well-reasoned documentation of deliberate CNA-specific safety improvements over FNA's
own undefined-behavior/crash-prone real implementation (disposed-effect `Apply()`, null-technique
`Apply()`) is some of the clearest "intentional divergence, not a bug" documentation encountered in
this audit's test-file sweep.

## Final Assessment
No findings.
