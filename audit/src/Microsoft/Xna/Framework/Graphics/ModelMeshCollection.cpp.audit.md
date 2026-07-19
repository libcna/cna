# Audit: src/Microsoft/Xna/Framework/Graphics/ModelMeshCollection.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/ModelMeshCollection.cpp`
- Audit status: AUDITED (full read, 54 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/ModelMeshCollection.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements both `operator[]` overloads, `getCountProperty`, `TryGetValue`, `Contains`, and
`begin`/`end`.

## Executive Verdict
Correct in its bounds-checking logic — `operator[](int)` uses `meshes_.at(...)`, matching FNA's
real `ReadOnlyCollection<ModelMesh>`-backed indexer semantics.

## Checklist Results
- `operator[](int)` (line 8-11): bounds-checked via `.at()` — correct.
- `TryGetValue`/`Contains`: correct, null-safe linear-search implementations.

## Detailed Findings

### LOW — `operator[](const std::string&)` throws raw `std::out_of_range`
Same pattern as `ModelBoneCollection::operator[](const std::string&)` (audited in this same batch)
— FNA's real by-name indexer contract for this family throws `KeyNotFoundException`. See that
report for the full writeup; the finding is identical here.

## Cross-File Observations
See `ModelBoneCollection.cpp.audit.md` for the shared pattern this file follows.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The numeric-index bounds-check is correct and present.

## Final Assessment
One LOW finding: by-name lookup throws the wrong exception type relative to FNA's documented
contract (same as `ModelBoneCollection`).
