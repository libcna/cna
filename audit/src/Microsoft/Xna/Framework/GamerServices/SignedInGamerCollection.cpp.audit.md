# Audit: src/Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.cpp`
- Audit status: AUDITED (full read, 23 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
Implements the private constructor, `CreateInternal`, and `operator[](PlayerIndex)`.

## Executive Verdict
Mostly correct, but `operator[](PlayerIndex)` has a real, if narrow-reachability, LOW-severity gap:
no lower-bound check on the converted index before array access.

## Checklist Results
- Constructor/`CreateInternal`: trivial, correct forwarding to the base class.
- `operator[](PlayerIndex)` (lines 16-22): checks only the upper bound.

## Detailed Findings

### LOW — `operator[](PlayerIndex)`: no lower-bound check before `collection_[...]`
```cpp
SignedInGamer* SignedInGamerCollection::operator[](Microsoft::Xna::Framework::PlayerIndex index) const
{
    int id = static_cast<int>(index);
    if (id >= static_cast<int>(collection_.size()))
        return nullptr;
    return collection_[static_cast<std::size_t>(id)];
}
```
For every value in the actual `PlayerIndex` enum (`One=0`..`Four=3`), `id` is always non-negative,
so this is safe in ordinary use. However, `PlayerIndex` is a C++ `enum class`, which — unlike a
genuinely closed/validated type — permits `static_cast<PlayerIndex>(-1)` (or any other
out-of-declared-range integer) from caller code with no compiler diagnostic. If a negative `id`
ever reaches this method, `id >= collection_.size()` (comparing a negative `int` against a
size-derived `int`) evaluates false, so the early return is skipped, and
`static_cast<std::size_t>(id)` converts the negative value into an enormous unsigned index —
`collection_[...]`'s `std::vector::operator[]` then performs an out-of-bounds read (undefined
behavior, not a caught error). Contrast with the base class `GamerCollection<T>::operator[](int)`
(used by this same class via the `using` declaration), which correctly double-sided-validates via
`ArgumentOutOfRangeException::ThrowIfNegative`/`ThrowIfGreaterThanOrEqual`.

**Reachability**: low. `PlayerIndex` only has four named, non-negative values in ordinary code —
reaching this bug requires a caller to deliberately construct an invalid enum value via an
explicit `static_cast`, not something that happens from ordinary XNA-idiomatic usage
(`Gamer.SignedInGamers[PlayerIndex.One]`). Rated LOW rather than MEDIUM (unlike the sibling
`xna-net` shard's `NetworkSessionProperties::Insert`/`RemoveAt`, whose `int index` parameter has no
type-level restriction at all and is far more plausibly reachable with an ordinary out-of-range
caller value).

**Suggested fix** (report-only; no source changes made per this audit's scope): add a
`if (id < 0 || id >= static_cast<int>(collection_.size())) return nullptr;` check, matching real
XNA's actual keyed-lookup semantics (returns `null` for any unoccupied/invalid slot, never
throws) — unlike `NetworkSessionProperties::Insert`/`RemoveAt`, the right fix here is a null
return, not an exception, since this method's own contract is already "miss returns null."

## Cross-File Observations
See the paired `.hpp` report. Also note: `GamerCollection<T>` itself (the base class, out of this
report's own assigned file list) declares no `Insert`/`RemoveAt`/other mutating index method, so
this is the only index-related gap found anywhere in this collection family.

## Missing or Weak Tests
A test constructing `static_cast<PlayerIndex>(-1)` and indexing a non-empty
`SignedInGamerCollection` with it would reproduce this; not independently located in this pass.

## Positive Findings
The constructor/`CreateInternal` pair is correct and trivial.

## Final Assessment
One LOW finding: `operator[](PlayerIndex)` lacks a lower-bound check, creating a narrow (requires a
deliberately-constructed invalid enum value) undefined-behavior path.
