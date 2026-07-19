# Audit: include/Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp`
- Audit status: AUDITED (full read, 304 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
A string-keyed, variant-valued (`std::any`) dictionary used for leaderboard/session property
values (`LeaderboardWriter`/`NetworkSessionProperties`-adjacent usage) — the C++ port of real XNA's
`PropertyDictionary : IDictionary<string, object>`.

## Executive Verdict
Mostly correct and well-documented (three explicit-interface-implementation mapping deviations —
`Add`/`Remove`/`Clear` becoming ordinary public methods, `Keys()`/`Values()` becoming snapshot
vectors instead of live views — are all honestly disclosed with a specific task citation, Task
8.1). However, every read accessor (`operator[]` both overloads, all eight `GetValueXxx` methods)
throws a raw `std::` exception (`std::out_of_range` via `.at()`; `std::bad_any_cast` via
`std::any_cast`) instead of this project's own provided `System::Collections::Generic::
KeyNotFoundException`/`System::InvalidCastException` types — see Detailed Findings.

## Checklist Results
- Doxygen coverage: complete.
- `NOXNA` usage: correctly applied to `begin()`/`end()` (STL/range-for interop) and
  `CreateInternal`.
- `getIsReadOnlyProperty()`'s doc comment (lines 260-270) correctly and honestly documents the same
  real upstream inconsistency already seen for `NetworkSessionProperties` in the sibling `xna-net`
  shard: `IsReadOnly` hardcoded `true` while `Add`/`Remove`/`Clear` remain fully functional
  mutators — explicitly framed as "a real upstream inconsistency... preserved faithfully rather
  than corrected, per this project's behavior-fidelity rule."
- `CopyTo`'s doc comment (lines 272-280) correctly documents it "always throws, matching FNA's own
  unimplemented... stub" — though note FNA has no reference material for this type at all (see
  shard-wide note); this specific claim is unverifiable but plausible given the surrounding
  disclosed-stub pattern already confirmed for `NetworkSessionProperties`/other GamerServices
  types.

## Detailed Findings

### MEDIUM — every read accessor throws a raw `std::` exception instead of this project's own
provided XNA-equivalent exception type
`operator[]` (both const and non-const overloads, via `dictionary_.at(key)`) and all eight
`GetValueXxx(key)` methods (via `dictionary_.at(key)` plus `std::any_cast<T>`) throw
`std::out_of_range` for a missing key and `std::bad_any_cast` for a type mismatch. This project's
own `sharp-runtime` provides both `System::Collections::Generic::KeyNotFoundException`
(`sharp-runtime/include/System/Collections/Generic/KeyNotFoundException.hpp`) and
`System::InvalidCastException` (`sharp-runtime/include/System/InvalidCastException.hpp`) — the
correct, XNA-faithful types real .NET's own `Dictionary<TKey,TValue>` indexer/typed-cast-adjacent
code would actually throw for these exact two failure modes (`KeyNotFoundException` for a missing
key, `InvalidCastException` for a value stored as the wrong type). This is the same recurring
"project-provided exception type available and used elsewhere, but a raw `std::` exception thrown
at this specific call site" pattern already flagged repeatedly across other shards in this audit
(see `AUDIT_CROSS_CUTTING_FINDINGS.md`). Practical effect: real-XNA-compatible game code written to
catch `KeyNotFoundException`/`InvalidCastException` specifically around a `PropertyDictionary`
access would not catch these `std::` exceptions in this port. This spans nine methods in a single
file (both `operator[]` overloads plus all eight `GetValueXxx`), making it a systematic gap in this
file rather than an isolated oversight.

Contrast: `PropertyDictionary::operator[](key)`'s non-const overload doc comment/`.cpp` comment
(lines 26-33 of the `.cpp`) explicitly and correctly documents *switching* from
`dictionary_[key]`(silent-insert) to `dictionary_.at(key)` as a real, deliberate Task 7.4 fix for a
genuine Count-inflation-on-read bug — but that fix stopped at "use `.at()`" rather than going the
full distance to the project's own XNA-faithful exception type.

## Cross-File Observations
`LeaderboardOutcome` (audited separately) is the one non-primitive type this dictionary explicitly
special-cases (`GetValueOutcome`/`SetValue(..., LeaderboardOutcome)`) alongside the primitive/
`DateTime`/`TimeSpan`/`Stream*`/`string` set — consistent with `LeaderboardWriter`'s real XNA API
surface, which stores leaderboard column values through exactly this dictionary type.

## Missing or Weak Tests
A test asserting `GetValueInt32`/`operator[]` etc. throw the correct, XNA-faithful exception type
for a missing key or wrong-typed value would have caught the finding above; not independently
located in this pass.

## Positive Findings
The three explicit-interface-implementation-to-ordinary-method mapping deviations (`Add`/`Remove`/
`Clear`, `Keys()`/`Values()` snapshot semantics) are all honestly and specifically disclosed with a
task citation (Task 8.1), consistent with this project's established documentation discipline for
genuine, unavoidable C#-to-C++ structural gaps.

## Final Assessment
One MEDIUM finding: every read accessor in this file throws a raw `std::` exception type instead of
the project's own provided `KeyNotFoundException`/`InvalidCastException` equivalents, spanning nine
methods.
