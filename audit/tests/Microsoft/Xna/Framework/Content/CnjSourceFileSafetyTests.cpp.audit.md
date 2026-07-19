# Audit: tests/Microsoft/Xna/Framework/Content/CnjSourceFileSafetyTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Content/CnjSourceFileSafetyTests.cpp` (174 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-content` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `.cnj` `"sourceFile"` safe/non-recursive resolution (NOXNA content
  pipeline extension)
- Main related tests: N/A (this IS a test file)

## Purpose
Proves a `.cnj`'s `"sourceFile"` field can never escape the content root (via `..` or an absolute
path), chain into another `.cnj`, or cycle back into itself.

## Executive Verdict
**Significant positive finding, and highly relevant cross-reference for a HIGH finding elsewhere
in this shard**: `AbsolutePathIsRejected` (lines 130-145) proves the `.cnj` `"sourceFile"` resolver
correctly rejects an absolute-path reference — exactly the class of containment gap this session's
production-code audit found in the *different*, older `ContentReader::ReadExternalReference<T>()`
path (`ResolveRelativeAssetPath()` only rejects `..`-escapes, not absolute paths — see
`audit/include/Microsoft/Xna/Framework/Content/ContentReader.hpp.audit.md`, and
`ContentReaderExternalReferenceTests.cpp.audit.md`, audited alongside this file, for the
corresponding missing-test finding). This test file proves the codebase already has a working,
tested containment pattern for exactly this bug class in a sibling resolver — the
`ReadExternalReference<T>()` fix could very plausibly reuse whatever safe-path-resolution logic
this `.cnj` `sourceFile` path already implements correctly.

## Checklist Results
- `DotDotEscapeIsRejected`/`AbsolutePathIsRejected`/`CnjChainIsRejected`/
  `SelfCycleViaExtensionlessNameIsRejected`: four distinct escape/cycle vectors, each with its own
  dedicated test and a real, concrete attack construction (a genuine file written outside the
  content root, a genuine absolute path, a genuine `.cnj`-to-`.cnj` chain, a genuine
  extensionless self-reference) — not a single generic "rejects bad input" test.
- `InRootSiblingResolvesCorrectly`/`InRootNestedPayloadResolvesCorrectly`: correctly verify the
  positive case (legitimate same-directory and nested-subdirectory references) still work,
  confirming the safety checks aren't simply over-broad rejections.

## Detailed Findings
None — this file's own subject matter is correctly implemented and correctly tested.

## Cross-File Observations
See Executive Verdict — this is the key positive counter-example to the confirmed HIGH
absolute-path-escape finding in `ContentReader::ReadExternalReference<T>()`. Two structurally
similar asset-reference-resolution features in the same codebase (`.cnj` `sourceFile` vs. legacy
`ReadExternalReference<T>()`) currently have different containment robustness — this file is the
evidence that the safer pattern already exists and works.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
A genuinely comprehensive security-hardening test suite: covers escape-via-`..`, escape-via-
absolute-path, chain-into-another-document, and self-cycle, each with its own real attack
construction and the corresponding legitimate-use-still-works counterpart.

## Final Assessment
No findings in this file. Its existence and passing tests are the key evidence needed to resolve
the "no fix pattern available" concern for the sibling HIGH finding in `ReadExternalReference<T>()`.
