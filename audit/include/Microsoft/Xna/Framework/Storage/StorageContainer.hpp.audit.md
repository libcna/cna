# Audit: include/Microsoft/Xna/Framework/Storage/StorageContainer.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Storage/StorageContainer.hpp`
- Audit status: AUDITED (full read, 199 lines)
- Subsystem: `xna-storage` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Storage/StorageContainer.cs` (421 lines,
  read in full)
- Main related tests: not independently located in this pass

## Purpose
Declares `StorageContainer`: a logical, per-title/per-player directory tree for user-data
persistence, with directory/file CRUD-style operations and `OpenFile` overloads.

## Executive Verdict
Needs attention, but for a confirmed **FNA-faithful** reason, not a CNA regression: no method in
this class validates that a caller-supplied relative `directory`/`file` argument stays within the
container's own root — but this is verified identical to FNA's real `StorageContainer.cs`, which
has exactly the same gap (`Path.Combine(storagePath, file)` has the same absolute-path-escapes and
`..`-traversal behavior as C++'s `std::filesystem::path::operator/`). Per this project's own
FNA-fidelity policy this is correct porting behavior, joining the pattern of similar confirmed
findings elsewhere this session (`TitleContainer::OpenStream()`). One real, disclosed-nowhere
intra-pair SPDX license inconsistency is also present.

## Checklist Results

### MEDIUM (confirmed FNA-faithful, undisclosed): no path-containment checking on `directory`/`file` arguments
None of `CreateDirectory`/`DirectoryExists`/`DeleteDirectory`/`CreateFile`/`FileExists`/
`DeleteFile`/`OpenFile` (lines 66-180) document or enforce that their relative-path parameter stays
within `storagePath_`. Verified against FNA's real `StorageContainer.cs` (all of `CreateDirectory`,
`CreateFile`, `DeleteDirectory`, `DeleteFile`, `DirectoryExists`, `FileExists`, `OpenFile`): every
one simply does `Path.Combine(storagePath, <arg>)` with no containment check either — a caller
passing an absolute path or a `..`-laden relative path can escape the container root in FNA too.
This is the same category of finding as `TitleContainer::OpenStream()` (confirmed FNA-faithful
earlier this session): correct per this project's FNA-fidelity policy, but undisclosed as
intentional anywhere in this header, unlike the project's own good practice elsewhere of citing the
FNA source location for a faithfully-reproduced surprising behavior.

### LOW: intra-pair SPDX license inconsistency
Line 1: `// SPDX-License-Identifier: MS-PL`; the paired `.cpp` uses `MIT` + a copyright line. See
the consolidated shard-wide note in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Detailed Findings
1. **[MEDIUM, confirmed FNA-faithful] No path-containment checking on any relative-path parameter**
   — lines 66-180; cf. FNA `StorageContainer.cs` (all directory/file methods use unchecked
   `Path.Combine`).
2. **[LOW] Intra-pair SPDX inconsistency** — line 1.

## Cross-File Observations
- `StorageDevice::DeleteContainer()` (audited separately) has the same underlying missing-check
  pattern, but there the consequence is materially worse — a real recursive delete rather than a
  file/directory create-or-open — since FNA's own `DeleteContainer` is an unimplemented stub, not a
  faithful reproduction. See `src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp.audit.md`.
- The constructor's null/empty-`displayName` guard (private, implemented in the `.cpp`) throws
  `std::invalid_argument` rather than the project's established `System::ArgumentNullException` —
  noted in the `.cpp` report as another instance of the shard-spanning exception-type pattern.

## Missing or Weak Tests
Not independently located in this pass. A test constructing a container, then calling
`OpenFile("../../../../tmp/evil", ...)` or an absolute path, and asserting whether the resulting
path stays within the container root, would make this FNA-faithful gap an explicit, intentional
contract rather than an implicit one.

## Positive Findings
The full directory/file/OpenFile API surface (11 public methods plus 3 `OpenFile` overloads)
matches FNA's own `StorageContainer` method-for-method, including the exact three-argument
`FileMode`/`FileAccess`/`FileShare` `OpenFile` overload chain.

## Final Assessment
One MEDIUM finding, confirmed FNA-faithful and therefore correct per project policy but undisclosed
as intentional, plus one LOW finding (SPDX inconsistency).
