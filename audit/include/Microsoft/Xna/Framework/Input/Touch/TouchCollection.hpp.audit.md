# Audit: include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`
- Audit status: AUDITED (full read, 171 lines)
- Subsystem: `xna-input` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/Touch/TouchCollection.cs`
- Main related tests: not independently located in this pass

## Purpose
Immutable-in-name-only snapshot of current touch locations, with full `IList<TouchLocation>`-style
mutation methods (matching FNA's own real, if surprising, contract).

## Executive Verdict
Correct, and notably honest about a real, easy-to-miss XNA/FNA quirk. `getIsReadOnlyProperty()`'s
doc comment (lines 30-43) explicitly documents that FNA's `IsReadOnly` getter is hard-coded `true`,
yet its `Add`/`Clear`/`Insert`/`Remove`/`RemoveAt` methods still genuinely mutate the backing list
whenever it is non-null — an advisory-only flag, not an enforced one — and this port is faithful to
that exact (if unusual) contract. The one disclosed, unavoidable C++ deviation (a default-constructed
collection is empty-and-mutable here, vs. FNA's null-backed-and-throws) is clearly explained as a
consequence of C++ value semantics having no null-struct equivalent.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
`FindById()`'s doc comment correctly specifies the output parameter is written unconditionally on
every path (including the not-found case, to an `Invalid` sentinel location) — verified in the
paired `.cpp`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The `IsReadOnly`-is-advisory-only quirk disclosure is a strong example of faithfully preserving a
real, surprising API contract rather than "fixing" it into something more intuitive but
non-FNA-matching.

## Final Assessment
No findings.
