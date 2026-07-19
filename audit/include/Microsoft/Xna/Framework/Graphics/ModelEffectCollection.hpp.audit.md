# Audit: include/Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp`
- Audit status: AUDITED (full read, 67 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/ModelEffectCollection.cs`
- Main related tests: not independently located in this pass

## Purpose
Represents a collection of effects associated with a model mesh, indexable by position, with
internal `Add`/`Remove` mutators used by `ModelMeshPart::setEffectProperty`.

## Executive Verdict
The header itself is a correct, minimal declaration matching FNA's real API shape (`Add`/`Remove`
correctly marked `NOXNA`, matching FNA's real `internal` accessibility intent — FNA's own
`ModelMeshPart` needs to add/remove from its parent mesh's `Effects` collection, the exact
consumer this port's `ModelMeshPart::setEffectProperty` also is). Implementation (see the paired
`.cpp` report) has a confirmed MEDIUM finding: `operator[](int)` performs unchecked indexing.

## Checklist Results
- Doxygen coverage: complete.
- `NOXNA` tagging: correctly applied to `Add`/`Remove` and the STL-interop `begin()`/`end()`
  overloads.

## Detailed Findings
See the paired `.cpp` report for the full MEDIUM finding (unchecked `operator[](int)`).

## Cross-File Observations
FNA's real `ModelEffectCollection : ReadOnlyCollection<Effect>` (confirmed by direct source read)
inherits its indexer from `ReadOnlyCollection<T>`, always bounds-checked — this port's
`ModelEffectCollection` does not carry that same guarantee. See `ModelMeshPartCollection`'s report
for the identical shape of finding in a sibling type in this same batch.

## Missing or Weak Tests
A test asserting `operator[](int)` throws for an out-of-range index would have caught the paired
`.cpp` finding.

## Positive Findings
`Add`/`Remove` correctly match FNA's real `internal` mutator methods, used exactly the way FNA
itself uses them (from `ModelMeshPart`'s effect setter).

## Final Assessment
See the paired `.cpp` report — one MEDIUM finding against the implementation.
