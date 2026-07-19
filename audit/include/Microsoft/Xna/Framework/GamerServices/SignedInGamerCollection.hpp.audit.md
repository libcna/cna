# Audit: include/Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp`
- Audit status: AUDITED (full read, 43 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
A read-only collection of `SignedInGamer` objects, additionally indexable by `PlayerIndex`
(real XNA's `SignedInGamerCollection : ReadOnlyCollection<SignedInGamer>` with an added
`this[PlayerIndex]` indexer).

## Executive Verdict
Correct. The `using GamerCollection<SignedInGamer>::operator[];` declaration (lines 18-26) is the
same necessary C++ overload-hiding fix already seen elsewhere in this audit (e.g.
`PacketWriter`'s `using BinaryWriter::Write;`) — without it, declaring the `PlayerIndex` overload
here would hide the inherited `int` overload under C++'s overload-hiding-by-name rule, silently
breaking any FNA-idiomatic call like `Gamer.SignedInGamers[i]` with a plain int index.

## Checklist Results
- Doxygen coverage: complete.
- `NOXNA` usage: correctly applied to the `using` declaration and `CreateInternal`.
- Index-taking-member bounds check (checked per this shard's standing concern after the sibling
  `xna-net` shard's `NetworkSessionProperties::Insert`/`RemoveAt` MEDIUM finding): this class adds
  no `Insert`/`RemoveAt`/other mutating index method of its own — the only members are the two
  read-only `operator[]` overloads (the inherited `int` one, and this class's own `PlayerIndex`
  one). `GamerCollection<T>`'s own `operator[](int)` (confirmed via a scoped read of
  `GamerCollection.hpp`, out of this report's own file list but load-bearing here) correctly uses
  `System::ArgumentOutOfRangeException::ThrowIfNegative`/`ThrowIfGreaterThanOrEqual` rather than
  `std::vector::at()`'s differently-typed exception (Task 7.9) — so the inherited overload is
  safe.

## Detailed Findings

### LOW — `operator[](PlayerIndex)` has no lower-bound check before indexing
See the paired `.cpp` report for the concrete code and full analysis. Declared here for
completeness: the header's doc comment (lines 28-34) documents only "or `nullptr` if no gamer is
at that index" for an out-of-range index, with no mention of what a negative underlying value
would do — consistent with the implementation actually only checking the upper bound.

## Cross-File Observations
Real XNA's `SignedInGamerCollection[PlayerIndex]` genuinely returns `null` for an unoccupied slot
(a keyed lookup, not a `List<T>`-style indexer) — this class's own `nullptr`-on-miss behavior for
the upper-bound case is correct XNA semantics, not a bug; only the missing lower-bound guard is a
gap. See the `.cpp` report for the concrete reachability analysis.

## Missing or Weak Tests
Not independently located in this pass; a test constructing an out-of-range (including negative,
via an explicit `static_cast`) `PlayerIndex` and checking this operator's behavior would be a
natural regression test.

## Positive Findings
The `using`-declaration overload-hiding fix is correctly applied, matching the established pattern
elsewhere in this codebase.

## Final Assessment
One LOW finding (see paired `.cpp` report for details): `operator[](PlayerIndex)` has no
lower-bound check, unlike the inherited `int` overload's fully double-sided validation.
