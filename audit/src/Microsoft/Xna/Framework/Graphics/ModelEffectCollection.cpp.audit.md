# Audit: src/Microsoft/Xna/Framework/Graphics/ModelEffectCollection.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/ModelEffectCollection.cpp`
- Audit status: AUDITED (full read, 39 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/ModelEffectCollection.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `operator[](int)`, `getCountProperty`, `Contains`, `Add`, `Remove`, and `begin`/`end`.

## Executive Verdict
Confirmed MEDIUM finding: `operator[](int)` performs raw, unchecked `std::vector::operator[]`
indexing, diverging from FNA's real, always-bounds-checked `ReadOnlyCollection<Effect>` indexer
contract — the identical shape of bug found in the sibling `ModelMeshPartCollection` in this same
batch.

## Checklist Results
- `operator[](int)` (line 8-11): `return effects_[static_cast<std::size_t>(index)];` — no bounds
  check.
- `Contains`/`Add`/`Remove`: correct, straightforward `std::find`-based implementations, matching
  FNA's real `Items.Contains`/`Add`/`Remove` (backed by the same `ReadOnlyCollection<T>.Items`
  list FNA's own internal mutators use).

## Detailed Findings

### MEDIUM — `ModelEffectCollection::operator[](int)` has no bounds check; FNA's real equivalent
always throws for an out-of-range index
```cpp
Effect* ModelEffectCollection::operator[](int index) const
{
    return effects_[static_cast<std::size_t>(index)];
}
```
FNA's real `ModelEffectCollection` (confirmed by direct source read of
`src/Graphics/ModelEffectCollection.cs`) is `public sealed class ModelEffectCollection :
ReadOnlyCollection<Effect>` — its indexer is inherited from
`System.Collections.ObjectModel.ReadOnlyCollection<T>`, always bounds-checked. This port's
`effects_[index]` on a raw `std::vector<Effect*>` is undefined behavior for an out-of-range index
instead of a catchable exception. Identical shape and severity to the sibling
`ModelMeshPartCollection::operator[](int)` finding audited in this same batch, and to the
`xna-net` shard's confirmed `NetworkSessionProperties::Insert`/`RemoveAt` finding.

**Failure scenario**: `ModelMesh::getEffectsProperty()`/`Model::Draw()` (audited elsewhere in this
batch) iterates this collection by count via `effects[ei]` for `ei` in `[0, effectCount)` — always
in-bounds in that specific call site, so the bug is not reachable through `Model::Draw()` itself,
but any other caller indexing this public collection with a caller-supplied or miscalculated index
hits undefined behavior instead of a documented, FNA-matching exception.

**Suggested fix** (report-only, no source changes made per this audit's scope):
`return effects_.at(static_cast<std::size_t>(index));`.

## Cross-File Observations
See `ModelMeshPartCollection.cpp.audit.md` (this same batch) for the identical bug in a sibling
collection type, and the cross-cutting note this audit is accumulating: both types deriving from
FNA's real `ReadOnlyCollection<T>` lose that base class's bounds-checking guarantee in this port,
while collections backed by a hand-written `std::vector` with an explicit `.at()` call
(`ModelBoneCollection`/`ModelMeshCollection`) correctly preserve it.

## Missing or Weak Tests
A test asserting `operator[](int)` throws for an out-of-range index would have caught this.

## Positive Findings
`Add`/`Remove`/`Contains` are all correct.

## Final Assessment
One MEDIUM finding: `operator[](int)` is unchecked, unlike FNA's real always-checked
`ReadOnlyCollection<T>`-backed indexer. This is the second confirmed instance of this exact bug
shape in this batch (see `ModelMeshPartCollection`).
