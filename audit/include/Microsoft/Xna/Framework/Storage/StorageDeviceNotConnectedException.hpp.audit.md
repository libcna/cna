# Audit: include/Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.hpp`
- Audit status: AUDITED (full read, 38 lines)
- Subsystem: `xna-storage` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference:
  `src/Storage/StorageDeviceNotConnectedException.cs`
- Main related tests: not independently located in this pass

## Purpose
Declares the exception thrown when an operation is attempted on a `StorageDevice`/
`StorageContainer` whose backing storage is no longer accessible.

## Executive Verdict
Minor issues only. The three-constructor shape (default, message, message+inner) matches FNA's
`ExternalException`-derived class exactly. Two small findings: the default constructor's message
differs from FNA's (see `.cpp` report), and this file's SPDX header (`MS-PL`) is inconsistent with
its paired `.cpp`'s (`MIT` + explicit copyright line) — see Cross-File Observations.

## Checklist Results

### LOW: intra-pair SPDX license inconsistency
Line 1: `// SPDX-License-Identifier: MS-PL`. The paired `.cpp` uses `// SPDX-License-Identifier:
MIT` plus `// Copyright (c) Robert Vokac and contributors`. This is the same three-variant
licensing inconsistency already noted in `AUDIT_CROSS_CUTTING_FINDINGS.md` for the `CNA::Internal::Net`
subsystem, but here it is an **intra-pair** mismatch (the header and its own implementation file
disagree), which is a stronger inconsistency than the previously-noted cross-file case. All three
file pairs in this shard show the identical header-says-MS-PL / cpp-says-MIT pattern — see the
consolidated cross-cutting note.

## Detailed Findings
1. **[LOW] Default-constructor message diverges from FNA's generic base-class default** — see
   `src/Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.cpp.audit.md` for the
   implementation-side detail.
2. **[LOW] SPDX header (`MS-PL`) disagrees with the paired `.cpp`'s (`MIT` + copyright line)** —
   line 1; consolidated across the whole shard in the cross-cutting findings doc.

## Cross-File Observations
This shard's three `.hpp`/`.cpp` pairs (`StorageContainer`, `StorageDevice`,
`StorageDeviceNotConnectedException`) all show the identical intra-pair SPDX mismatch (header:
`MS-PL`; implementation: `MIT` + copyright line) — worth a single consolidated
`AUDIT_CROSS_CUTTING_FINDINGS.md` entry rather than three separate notes.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The three-constructor shape and base-class relationship (`ExternalException`) match FNA precisely.

## Final Assessment
Two LOW findings, both minor (a message-text deviation and a licensing-header inconsistency).
