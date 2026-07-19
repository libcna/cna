# Audit: include/Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp`
- Audit status: AUDITED (full read, 192 lines)
- Subsystem: `xna-net` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace, but the
  file's own "TODO: Expand list to index size?" comment quotes a specific claimed FNA source
  comment — unverifiable against the local FNA tree (see shard-wide cross-cutting note)
- Main related tests: not independently located in this pass

## Purpose
A growable list of `std::optional<int>` session properties, implementing
`System::Collections::Generic::IList<std::optional<int>>` — the C++ port of real XNA's
`NetworkSessionProperties : IList<int?>, ICollection<int?>, IEnumerable<int?>`.

## Executive Verdict
Mostly correct and unusually well-documented, but the implementation (see the paired `.cpp`
report) has a real bounds-checking gap in `Insert`/`RemoveAt` not disclosed anywhere in this
header. See Detailed Findings.

The non-const `operator[]`'s doc comment (lines 42-66) is an exemplary disclosure of a genuine,
unavoidable C#-to-C++ structural divergence: real XNA's `int?[index]` indexer has *separate*
`get`/`set` accessors where only `set` auto-appends past the end (`get` throws via `List<T>`'s own
indexer); `IList<T>::operator[]` in this codebase has a single non-const signature returning `T&`
for both reading and writing, so C++ cannot distinguish a read-only access from a write the way
C#'s separate accessors can. This is explicitly accepted as a known, structural, unfixable-without-
a-wider-ripple-effect divergence rather than silently present.

## Checklist Results
- Doxygen coverage: complete, every public member documented.
- `NOXNA` usage: none in this header (all members are direct `IList<int?>` XNA API surface, plus
  STL-interop `begin()`/`end()` helpers appropriately left undocumented as NOXNA... actually they
  ARE tagged NOXNA at lines 156-162; correctly marked).

## Detailed Findings

### MEDIUM — `Insert(int, ...)`/`RemoveAt(int)` declared with no bounds-check contract, and the
implementation (see `.cpp`) has none
The header documents `Insert`/`RemoveAt` (lines 76-89) with no `@throws` clause and no mention of
what happens for an out-of-range `index`, unlike the const `operator[]` just above (line 37:
`@throws std::out_of_range if index is out of range`) and `CopyTo` further down (lines 143-144:
`@throws System::ArgumentOutOfRangeException if index is negative` / `@throws
System::ArgumentException if destination is too small`). Real .NET's `List<T>.Insert`/`RemoveAt`
both throw `ArgumentOutOfRangeException` for an out-of-range index — this type's own `IList<T>`
contract should match. See the `.cpp` report for the concrete undefined-behavior consequence.

## Cross-File Observations
See the `.cpp` report for the full analysis of the missing bounds check's reachable consequences.

## Missing or Weak Tests
Not independently located in this pass; given the finding above, a test for `Insert`/`RemoveAt`
with a negative or past-the-end index would be a natural regression test once fixed.

## Positive Findings
- The non-const `operator[]`'s doc comment is one of the best examples in this entire audit of
  disclosing a genuine, load-bearing C#-to-C++ structural divergence rather than hiding it.
- `getIsReadOnlyProperty()` correctly documents the real XNA oddity that `IsReadOnly` is `true`
  while `Add`/`Remove`/`Clear` remain fully functional (matching FNA's own explicit interface
  implementation quirk, independently well-known as a real XNA behavior).
- `CopyTo`'s exception contract is fully and correctly specified.

## Final Assessment
One MEDIUM finding: `Insert`/`RemoveAt` lack the bounds-check contract present on sibling members
of the same class.
