# Audit: include/CNA/Internal/Xnb/XnbReadLimits.hpp

## Metadata
- Source file: `include/CNA/Internal/Xnb/XnbReadLimits.hpp`
- Audit status: AUDITED (full read, 48 lines, header-only; cross-checked every field's actual consumers via
  repository-wide grep, not just this file's own declarations)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ header (header-only implementation)
- XNA/FNA relevance: N/A -- NOXNA hardening layer; FNA itself has no equivalent size-sanity-checking (it
  relies on the CLR eventually throwing `OutOfMemoryException` for a pathological allocation)
- Main related tests: not independently located in this pass

## Purpose
Centralizes sanity bounds (max file size, max decompressed size, max string/collection/type-reader/
shared-resource counts, max object nesting depth) intended to be consulted by every count-driven `.xnb`
read, specifically to fail fast with a clear error rather than attempting an adversarial-count-driven
allocation.

## Executive Verdict
Needs attention -- the *design* is exactly right, but a repository-wide check of every field's actual
consumers found that 2 of the 7 declared limits (`maxStringBytes`, `maxObjectNestingDepth`) have **zero**
consumers anywhere in the codebase: they are dead configuration, not enforced anywhere. The other 5
(`maxFileSize`, `maxDecompressedSize`, `maxTypeReaderCount`, `maxSharedResourceCount`,
`maxCollectionElementCount`) are genuinely wired into real checks in `XnbDecompression.cpp`,
`XnbTypeReaderTable.hpp`, and `Microsoft::Xna::Framework::Content::ContentReader.cpp`.

## Checklist Results

### MEDIUM: `maxStringBytes` is declared but never enforced anywhere
`grep -rn "maxStringBytes"` across `include/CNA/Internal/Xnb`, `src/CNA/Internal/Xnb`, and the
`Microsoft::Xna::Framework::Content` area returns only this field's own declaration -- no call site anywhere
reads `limits.maxStringBytes` to bound a string read. The one obvious candidate consumer,
`XnbTypeReaderTable.hpp`'s `ParseXnbTypeReaderTable()` (`entry.rawName = reader.ReadString();`), calls
`BinaryReader::ReadString()` with no length argument at all. Some protection still exists as a side effect:
`BinaryReader::ReadString()` (sharp-runtime, reference-only for this audit) clamps its allocation to the
underlying seekable stream's own remaining length before allocating, so a claimed string length can't
exceed what the *whole file* could possibly contain -- bounded in the worst case by `maxFileSize` (64MB), not
by `maxStringBytes`'s intended, much tighter 1MB. This is real, but coarser, protection that happens to
exist for an unrelated reason (`ReadBytes`/`ReadString`'s own defensive clamp against adversarial counts,
audited as a positive finding wherever `System::IO::BinaryReader` itself is reviewed) -- it is not evidence
that `maxStringBytes` is being honored as designed.

### MEDIUM: `maxObjectNestingDepth` is declared but never enforced anywhere
Same grep result: zero consumers of `maxObjectNestingDepth` anywhere in the codebase. This is the more
concerning of the two dead limits, because its stated purpose -- "deepest nested-object graph this reader
will follow before rejecting the file" -- is exactly the kind of guard needed to prevent a stack-overflow
DoS from a maliciously deep object graph in either `.xnb` object deserialization
(`Microsoft::Xna::Framework::Content::ContentReader`, Task #4 territory -- flag for confirmation when that
area is audited) or, concretely confirmed within *this* shard, `XnbTypeName.hpp`'s recursive-descent generic
type-name parser (see that file's own report): `ParseOne()` recurses once per nested generic-argument level
with no depth counter or `XnbReadLimits` consultation at all, so a crafted `.xnb` type-reader-name string
with enough nested `[[...]]` levels (roughly 3 bytes per level, so easily hundreds of thousands of levels
within even the unenforced `maxStringBytes`-sized string budget) can drive unbounded C++ call-stack
recursion -- a real stack-overflow crash, not merely a slow parse.

**Fix shape**: thread `limits.maxStringBytes` into `BinaryReader::ReadString()` call sites in this
subsystem (or add an overload accepting an explicit cap), and thread `limits.maxObjectNestingDepth` into
both `XnbTypeName.hpp`'s recursive parser (an explicit depth parameter, incremented per recursive `ParseOne`
call, rejected once it exceeds the limit) and `ContentReader`'s own object-graph deserialization recursion
(to be confirmed/flagged again when that file is audited under Task #4).

## Detailed Findings

1. **[MEDIUM] `maxStringBytes` declared but never enforced** -- see above. File: this file (declaration);
   `include/CNA/Internal/Xnb/XnbTypeReaderTable.hpp` (the clearest unenforced consumer candidate).
2. **[MEDIUM] `maxObjectNestingDepth` declared but never enforced** -- see above, and see
   `include/CNA/Internal/Xnb/XnbTypeName.hpp.audit.md` for the concrete unbounded-recursion instance this
   gap allows.

## Cross-File Observations
The other 5 fields are genuinely enforced: `maxFileSize`/`maxDecompressedSize` in `XnbDecompression.cpp`
(verified in this shard), `maxTypeReaderCount` in `XnbTypeReaderTable.hpp` (verified in this shard),
`maxSharedResourceCount`/`maxCollectionElementCount` in `Microsoft::Xna::Framework::Content::ContentReader.cpp`
(Task #4 territory -- confirm the actual check logic when that file is audited, this pass only confirmed the
symbol is referenced there via grep).

## Missing or Weak Tests
A test asserting each individual limit is actually enforced at its respective consumption site would have
caught both dead fields directly -- worth prioritizing when the `tests-*` Xnb shard is audited.

## Positive Findings
The overall design (fail-fast bounds checked ahead of allocation, generous-but-real limits) is exactly
right, and 5 of 7 fields are genuinely wired in correctly.

## Final Assessment
Two MEDIUM-severity findings: `maxStringBytes` and `maxObjectNestingDepth` are declared, documented security
controls with zero actual enforcement anywhere in the codebase.
