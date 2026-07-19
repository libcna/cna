# Audit: include/CNA/Internal/Xnb/XnbBuiltInReaders.hpp

## Metadata
- Source file: `include/CNA/Internal/Xnb/XnbBuiltInReaders.hpp`
- Audit status: AUDITED (full read, 40 lines)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ header
- XNA/FNA relevance: N/A -- NOXNA convenience aggregator (CNA has no reflection-based reader discovery,
  unlike FNA)
- Main related tests: not independently located in this pass

## Purpose
Declares the single call site (`RegisterAllBuiltInXnbReaders()`) a real game calls once at startup to
register every built-in `.xnb` `ContentTypeReader`, replacing the prior "discover and call all thirteen
piecemeal registration functions yourself" burden.

## Executive Verdict
Healthy.

## Checklist Results
Clearly documents a real, deliberate architectural constraint: this is NOT auto-invoked from
`ContentManager`'s constructor, specifically because existing tests rely on
`ContentTypeReaderManager::ClearTypeCreators()` isolation in their own `SetUp()` -- auto-registration would
silently defeat that isolation. Also clearly documents CNA's fundamental limitation versus FNA: no
reflection means generic collection readers (`ArrayReader<T>`/`ListReader<T>`/etc.) can't be registered "for
any T," only as specific closed-generic combinations a game must register itself.

## Detailed Findings
None.

## Cross-File Observations
Idempotency (`AddTypeCreator()` silently ignoring repeat registrations) is stated as a design assumption
here -- worth confirming directly against `ContentTypeReaderManager`'s own implementation when the
`Microsoft::Xna::Framework::Content` area is audited under Task #4.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Honest, clear documentation of a real architectural limitation (no reflection) rather than papering over it.

## Final Assessment
No issues found.
