# Audit: src/CNA/Internal/Xnb/ModelContentTypeReaders.cpp

## Metadata
- Source file: `src/CNA/Internal/Xnb/ModelContentTypeReaders.cpp`
- Audit status: AUDITED (full read, 345 lines) -- final file of the `cna-internal-core` shard (113/113)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ implementation
- XNA/FNA relevance: matches FNA's `ModelReader`/`VertexBufferReader`/`IndexBufferReader`/
  `VertexDeclarationReader`
- Main related tests: not independently located in this pass

## Purpose
Implements the Model-graph readers, including bone-reference variable-width encoding, shared-resource
dedup via `ReadSharedResource<T>()`, and `ModelReaderOwnedResources` (the single ownership sink kept alive
via `Model::setOwnedResources()`).

## Executive Verdict
Healthy -- the most complex reader in this shard, and correspondingly the most carefully hardened: explicit
`int64_t`-widened overflow protection for `vertexCount * stride`, consistent bone-index bounds validation
via a single `RequireBone()` helper, and generous-but-real sanity caps on bone/mesh/mesh-part counts. One
LOW-priority edge case flagged for Task #4 cross-checking (a zero-bone model's root-bone index), not
confirmed as an actual bug in this pass.

## Checklist Results

### `VertexBufferReader`: correct, explicitly-reasoned integer-overflow hardening
`vertexCount * stride` is computed in `int64_t` specifically because `vertexCount` can be
attacker-controlled up to `INT32_MAX`, and the comment correctly identifies the exact failure mode a
32-bit-only product would enable (silent wraparound to a small/negative byte count that would defeat
`ReadBytesExactOrThrow()`'s own mismatch check and let `SetDataRaw()` read past the actual, much shorter,
buffer) -- independently re-derived and confirmed this reasoning is sound, not merely trusted from the
comment.

### `RequireBone()`: a single, consistently-applied bounds-check chokepoint
Every bone-index dereference in `ModelReader::Read()` (child-bone links, mesh-parent-bone lookups, the
root-bone lookup) routes through this one helper, which explicitly checks both `index < 0` and
`index >= bones.size()` before returning the pointer -- verified this catches every code path that could
otherwise dereference an out-of-range bone index, including the case where a malformed file's `boneId`
field encodes a value wildly inconsistent with the model's own declared `boneCount` (traced through
`ReadBoneReference()`'s `boneId - 1` computation for both an implausibly-large and a moderately-out-of-range
`boneId`, confirming both are caught by `RequireBone`'s range check, not merely the large one).

### Count sanity caps: present, though not routed through `XnbReadLimits`
`boneCount > 100000`, `meshCount < 0 || > 100000`, `partCount < 0 || > 100000` are all explicitly checked
before any per-item allocation loop -- correct in effect, though these are hardcoded literals rather than
consulting `XnbReadLimits::maxCollectionElementCount` (a further data point, alongside
`CurveContentTypeReader.hpp`'s own finding, that this subsystem's declared shared limits aren't uniformly
threaded through every bespoke count-driven reader; not scored as an additional actionable finding here
since a real, if ad-hoc, bound is genuinely present).

### LOW, not confirmed: zero-bone model's root-bone index
If a `.xnb` Model file declares `boneCount == 0` and its root-bone-reference field encodes "no bone" (which
`ReadBoneReference()` returns as `-1`), `ModelReader::Read()`'s fallback (`rootIndex = rootBoneIndex != -1 ?
... : 0`) passes `rootIndex = 0` to `Model`'s constructor together with an *empty* `boneRawPtrs` vector --
`RequireBone()` is only called when `rootBoneIndex != -1`, so this specific fallback path is never
range-checked against the (empty) bones vector at all. Whether this is actually reachable/harmful depends
entirely on `Model`'s own constructor (Task #4, `Microsoft::Xna::Framework::Graphics::Model`, not yet
audited) -- if it indexes `bones[rootIndex]` without its own bounds check, a zero-bone `.xnb` Model would
trigger an out-of-bounds access on an empty vector. Flagging as a cross-file dependency to confirm when
`Model.cpp` is audited under Task #4, not asserting a confirmed bug here (a model with literally zero bones
is an unusual, likely-never-produced-by-real-tooling case, but a `.xnb` file's own field values are always
adversary-shapeable in principle).

### Shared-resource dedup and ownership: correct
`ReadSharedResource<T>()`'s callback-based resolution correctly transfers ownership into
`ModelReaderOwnedResources` (mirroring the existing `.model.json` `ModelTypeReader::ModelResources`
precedent, per the comment) while handing out only a raw observer pointer to the `ModelMeshPart` -- a
consistent, correctly-reasoned ownership model matching this codebase's established GPU-resource
conventions.

### `Tag` field handling: correct, honest about scope
`RejectNonNullTag()` reads (for stream-position correctness) but rejects any actually-non-null `Tag` with a
clear exception rather than silently dropping data a caller might expect -- correctly scoped as "not
expected to be reached in practice" rather than "impossible," and consistently applied at all three levels
(model/mesh/mesh-part) FNA's own format allows a `Tag` at.

## Detailed Findings

1. **[LOW, not confirmed -- cross-file dependency]** A zero-bone `.xnb` Model file's root-bone-index
   fallback (`rootIndex = 0`) is never checked against the (in that case, empty) bones vector before being
   passed to `Model`'s constructor. Whether this is exploitable depends on `Model`'s own constructor
   validation, to be confirmed when Task #4 reaches that file. Lines 315-322.

## Cross-File Observations
This file, `Texture2D/3D/CubeContentTypeReader.cpp`, and `VertexBufferReader::Read()`'s own `SetDataRaw()`/
`SetData()` calls all share the same class of cross-file dependency: trusting a downstream
`Microsoft::Xna::Framework::Graphics` setter/constructor to itself validate bounds the reader has already
partially (but not fully, in every case) checked. Worth a consolidated pass when Task #4 reaches the
`Graphics` buffer/texture/model classes.

## Missing or Weak Tests
Not independently located in this pass; a zero-bone Model fixture would directly test the edge case flagged
above.

## Positive Findings
The most rigorously hardened reader in this shard against integer overflow and out-of-range indices --
`RequireBone()`'s single-chokepoint design is a genuinely good pattern for consistently enforcing an
invariant across many call sites rather than repeating (and risking inconsistently repeating) the same
check.

## Final Assessment
No confirmed defects; one LOW-priority cross-file dependency flagged for confirmation under Task #4
(`Model`'s own constructor validation of a zero-bone root-bone index).

---

**This is the final file of the `cna-internal-core` shard (113/113 complete).**
