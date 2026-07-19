# Audit: src/Microsoft/Xna/Framework/GamerServices/PropertyDictionary.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/GamerServices/PropertyDictionary.cpp`
- Audit status: AUDITED (full read, 192 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
Implements every `PropertyDictionary` member: constructor, `CreateInternal`, both `operator[]`
overloads, `ContainsKey`, `TryGetValue`, all eight `GetValueXxx`/`SetValue` overload pairs, `Add`,
`Remove`, `Clear`, `Keys`, `Values`, `IsReadOnly`, `CopyTo`.

## Executive Verdict
Mostly correct, but is the concrete source of the paired `.hpp` report's MEDIUM finding: every read
path (`operator[]` via `.at()`, every `GetValueXxx` via `.at()` + `std::any_cast`) throws a raw
`std::` exception instead of this project's own `System::Collections::Generic::
KeyNotFoundException`/`System::InvalidCastException`. `Add()` is the one accessor in this file that
*does* correctly use a project-provided exception type (`System::ArgumentException`, for the
duplicate-key case) — confirming the project's exception-mapping convention is known and
selectively applied elsewhere in this same file, making its absence from every read path more
likely an inconsistency than a deliberate choice.

## Checklist Results
- `operator[]` non-const (lines 24-33): confirmed switched from `dictionary_[key]` to
  `dictionary_.at(key)` per Task 7.4's own inline comment, fixing a real Count-inflation-on-read
  bug (`std::map::operator[]` silently default-constructs and inserts a missing key) — a genuine,
  correct, well-explained fix, though it stops short of using the project's own
  `KeyNotFoundException` (see the paired `.hpp` finding).
- `Add()` (lines 139-149): correctly throws `System::ArgumentException` with real .NET's own exact
  message format ("An item with the same key has already been added. Key: <key>") for a duplicate
  key — confirmed matches `Dictionary<TKey,TValue>.Add`'s real behavior.
- `CopyTo()` (lines 188-191): unconditionally throws `System::NotImplementedException` — matches
  its header's documented "always throws" contract.

## Detailed Findings
See the paired `.hpp` report for the full MEDIUM finding (raw `std::` exceptions instead of
project-provided types across nine read-path methods in this file).

## Cross-File Observations
None beyond the paired `.hpp` report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`Add()`'s exact real-.NET-message-format fidelity and the Task 7.4 `.at()` fix are both genuine,
well-reasoned improvements.

## Final Assessment
No new findings beyond the paired `.hpp` report's MEDIUM finding, for which this file is the direct
evidence.
