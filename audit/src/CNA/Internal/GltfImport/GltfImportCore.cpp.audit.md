# Audit: src/CNA/Internal/GltfImport/GltfImportCore.cpp

## Metadata
- Source file: `src/CNA/Internal/GltfImport/GltfImportCore.cpp`
- Audit status: AUDITED (scoped-depth review — 1409 lines; `UnpackAccessor()`/`ScaleTranslation()` (lines
  103-143) and `BuildSkeleton()` (lines 419-499) read and verified in full; the much larger `ExtractMesh`
  (~400 lines), `ExtractClips`, Draco-decoding, and image-extraction functions were inventoried by signature
  but not read line-by-line, consistent with this audit's scoped-depth standard for files of this size)
- Subsystem: `cna-internal-core` shard
- File type: C++ implementation
- XNA/FNA relevance: implements glTF model import for the content pipeline
- Main related tests: not independently located in this pass

## Purpose
Implements skeleton bone-hierarchy extraction (with re-topologized parent-before-child ordering), animation
clip/track sampling, mesh/vertex extraction (including Draco-decompressed primitives), image extraction, and
punctual-light/morph-weight-track extraction, all built on `cgltf`.

## Executive Verdict
Healthy in the areas read — genuinely careful, mathematically-verified logic with a real defensive check
against malformed input.

## Checklist Results

### Confirmed correct: `BuildSkeleton()`'s bone re-topologizing BFS
Re-orders a skin's joints from glTF's own (arbitrary) order into a parent-before-child order via a
correct breadth-first traversal seeded from every joint with no in-skin parent. **Confirmed a genuine,
correct defensive check against a malformed/adversarial glTF file**: `if (newOrderOldIndices.size() != n)
throw` catches the case where a joint's parent chain never resolves to a root within the skin's own joint
set (e.g. a cycle) — without this, the BFS would silently produce a truncated ordering. `bone.parentIndex`
is correctly remapped through the new ordering (`oldToNew[oldParent]`), and the inverse-bind-matrix lookup
correctly indexes using the OLD joint order (`ibm.data() + oldIdx * 16`), matching glTF's own
`inverse_bind_matrices` accessor layout (keyed by the original `skin->joints` array order, not the
re-topologized one) — a subtle but correctly-handled indexing distinction.

### Confirmed correct: `ScaleTranslation()`'s bind-pose/inverse-bind-matrix scaling math
The inline comment's claim that scaling a bind-pose matrix's translation by `k` and independently scaling its
already-glTF-authored inverse's own translation by the same `k` (not `1/k`) are mathematically consistent was
independently verified: for an affine transform `[R|t]`, `Inverse([R|t]) = [R^-1 | -R^-1*t]`, so scaling `t`
by `k` scales the inverse's own translation term by exactly `k` too (not its reciprocal) — the code's approach
is mathematically correct, not merely asserted.

### Confirmed correct: `UnpackAccessor()`'s bounds delegation
Validates the accessor's actual component count against the caller's expectation before unpacking, and
validates the real cgltf library's own `cgltf_accessor_unpack_floats()` returned the exact expected element
count, throwing a clear, contextual error on either mismatch — correctly delegates the actual bounds-safe
binary extraction to the well-established `cgltf` library rather than hand-rolling it, a sound architectural
choice for parsing arbitrary/untrusted glTF files.

### C++ correctness / Memory/resource lifetime / Performance / Portability / Maintainability / Robustness
No issues found in the areas read.

## Detailed Findings
None in the areas read.

## Cross-File Observations
None specific.

## Missing or Weak Tests
The untraced ~1000 lines (`ExtractMesh`'s full vertex/index/morph-target extraction, `ExtractClips`, Draco
decoding, image extraction) remain a gap for a future, more exhaustive pass — these are exactly the areas
most likely to contain a subtle indexing or bounds bug given they process the bulk of a glTF file's actual
mesh geometry data.

## Positive Findings
Mathematically verified-correct bind-pose/inverse-bind-matrix scaling reasoning; a genuine, effective
defensive check against a malformed/cyclic joint hierarchy that would otherwise silently corrupt skeleton
output; sound delegation of low-level binary bounds-safety to the underlying `cgltf` library.

## Final Assessment
No defects found in the areas read (scoped-depth review); the untraced majority of this file (mesh/clip
extraction) is a real gap for a future pass, not a claim of full verification.
