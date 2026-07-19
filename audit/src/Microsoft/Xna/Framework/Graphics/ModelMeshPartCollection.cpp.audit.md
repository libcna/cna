# Audit: src/Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.cpp`
- Audit status: AUDITED (full read, 21 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/ModelMeshPartCollection.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `operator[](int)`, `getCountProperty`, and `begin`/`end`.

## Executive Verdict
Confirmed MEDIUM finding: `operator[](int)` performs raw, unchecked `std::vector::operator[]`
indexing, diverging from FNA's real, always-bounds-checked `ReadOnlyCollection<ModelMeshPart>`
indexer contract.

## Checklist Results
- `operator[](int)` (line 7-10): `return parts_[static_cast<std::size_t>(index)];` — no bounds
  check at all, unlike the sibling `ModelBoneCollection`/`ModelMeshCollection` in this same batch
  (both correctly use `.at()`).

## Detailed Findings

### MEDIUM — `ModelMeshPartCollection::operator[](int)` has no bounds check; FNA's real
equivalent always throws for an out-of-range index
```cpp
ModelMeshPart* ModelMeshPartCollection::operator[](int index) const
{
    return parts_[static_cast<std::size_t>(index)];
}
```
FNA's real `ModelMeshPartCollection` (confirmed by direct source read of
`src/Graphics/ModelMeshPartCollection.cs`) is `public sealed class ModelMeshPartCollection :
ReadOnlyCollection<ModelMeshPart>` — its indexer is inherited from
`System.Collections.ObjectModel.ReadOnlyCollection<T>`, which is always bounds-checked (throws
`ArgumentOutOfRangeException` for `index < 0` or `index >= Count`, backed internally by a real
`List<T>`). This port's `parts_[index]` on a raw `std::vector<ModelMeshPart*>` is undefined
behavior for an out-of-range index instead of a catchable exception — the exact same class of
regression as the `xna-net` shard's confirmed `NetworkSessionProperties::Insert`/`RemoveAt` finding,
and directly contrasted by the sibling `ModelBoneCollection`/`ModelMeshCollection` in this same
batch, both of which correctly use `.at()`.

**Failure scenario**: any caller indexing `ModelMesh::getMeshPartsProperty()` (a public accessor)
with an out-of-range index — e.g. a bug in content-loading code that miscounts parts, or any test
exercising bounds behavior — hits undefined behavior instead of a documented, FNA-matching
exception.

**Suggested fix** (report-only, no source changes made per this audit's scope):
`return parts_.at(static_cast<std::size_t>(index));`, matching the sibling collections' own
established pattern in this exact file family.

## Cross-File Observations
See `ModelEffectCollection.cpp` (audited in this same batch) for the identical bug in a sibling
collection type — both `ModelMeshPartCollection` and `ModelEffectCollection` derive from FNA's real
`ReadOnlyCollection<T>` and both lose that base class's bounds-checking guarantee in this port,
while `ModelBoneCollection`/`ModelMeshCollection` (backed by a hand-written `std::vector`, not a
`ReadOnlyCollection<T>` wrapper, but still expected to preserve the same observable contract)
correctly preserve it via `.at()`.

## Missing or Weak Tests
A test asserting `operator[](int)` throws for an out-of-range index would have caught this.

## Positive Findings
`getCountProperty()`/`begin()`/`end()` are correct.

## Final Assessment
One MEDIUM finding: `operator[](int)` is unchecked, unlike FNA's real always-checked
`ReadOnlyCollection<T>`-backed indexer.
