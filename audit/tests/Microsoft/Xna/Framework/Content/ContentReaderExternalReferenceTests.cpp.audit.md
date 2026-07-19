# Audit: tests/Microsoft/Xna/Framework/Content/ContentReaderExternalReferenceTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Content/ContentReaderExternalReferenceTests.cpp` (141 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-content` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Content::ContentReader::ReadExternalReference<T>()`
- Main related tests: N/A (this IS a test file)

## Purpose
Tests `ReadExternalReference<T>()`'s relative-path resolution against a real `ContentManager`/`.xnb`
round trip, the empty-reference `nullopt` case, and the content-root-escape hardening this task
(XNB-35) adds.

## Executive Verdict
**MEDIUM finding: this file does NOT catch the already-confirmed HIGH production defect in
`ContentReader::ReadExternalReference<T>()`.** The production audit
(`audit/include/Microsoft/Xna/Framework/Content/ContentReader.hpp.audit.md`, committed earlier
this session) found that `ResolveRelativeAssetPath()` only rejects `..`-relative escapes, not
**absolute-path** escapes — a caller-controlled reference string that is itself an absolute path
(e.g. `/etc/passwd` or an absolute path outside the content root) is not rejected at all.
`PathEscapingContentRootThrowsContentLoadException` (lines 122-132) only tests a `..`-relative
escape (`"../../outside"`) — a case the production code correctly handles — and never constructs
an absolute-path reference string. This test suite's coverage gap is precisely why the absolute-
path vulnerability went unnoticed until this audit's static code reading found it.

## Checklist Results
- `ResolvesSiblingPathAndLoadsThroughContentManager` (lines 84-107): a genuine, real end-to-end
  test — copies a real `.xnb` fixture, resolves a sibling-relative reference, and asserts on the
  actual loaded `Texture2D`'s dimensions. Not a tautological assertion.
- `EmptyReferenceReturnsNullopt` (lines 109-120): correctly tests the documented empty-reference
  contract, with an explicit comment confirming `cm` is never dereferenced on this path (verifying
  the short-circuit is real, not accidentally safe).
- `NoOwningContentManagerThrowsContentLoadException` (lines 134-141): a real, meaningful negative
  test for the null-`ContentManager` case.

## Detailed Findings

### MEDIUM — No test constructs an absolute-path external-reference string, missing the exact scenario the confirmed HIGH production vulnerability requires
See Executive Verdict. A test resembling
```cpp
TEST_F(ContentReaderExternalReferenceTest, AbsolutePathReferenceThrowsContentLoadException)
{
    ContentManager cm;
    std::string storage;
    auto stream = MakeReferenceStream("/etc/passwd", storage); // or an absolute Windows path
    ContentReader reader(&cm, stream.get(), "textures/foo", 5, 'w');
    EXPECT_THROW(reader.ReadExternalReference<Texture2D>(), ContentLoadException);
}
```
would fail against the current implementation (confirmed via the production audit's own finding
that `std::filesystem::path::operator/` silently discards the content-root base path when the RHS
`is_absolute()`), and should be added once the production fix lands.

## Cross-File Observations
Confirms the production `ContentReader.hpp`/`.cpp` audit's finding is real and untested — this is
exactly the kind of gap this audit's "systematic FNA parity sweep" (Pass 3, pending) is meant to
surface across the whole codebase.

**Important, higher-value cross-file finding**: `CnjSourceFileSafetyTests.cpp` (same shard,
audited alongside this file) proves that a *sibling* asset-reference-resolution code path — the
newer `.cnj` JSON `"sourceFile"` field's own resolver — already correctly rejects absolute paths
(`AbsolutePathIsRejected`), `..`-escapes, `.cnj`-chaining, and self-cycles, all with dedicated,
passing regression tests. This means the codebase already has a working, tested containment
pattern for exactly the class of bug `ReadExternalReference<T>()`'s `ResolveRelativeAssetPath()`
is missing — the fix for the confirmed HIGH finding in this file's own subject could very likely
reuse or mirror whatever containment logic the `.cnj` `sourceFile` resolver already implements
correctly, rather than needing to be designed from scratch.

## Missing or Weak Tests
The absolute-path-escape test described above is the concrete, missing regression test for the
already-confirmed HIGH finding.

## Positive Findings
The `..`-relative-escape test that does exist is well-constructed and appropriately targeted at the
one containment check the production code does implement correctly.

## Final Assessment
One MEDIUM finding: this suite does not test the exact scenario (absolute-path reference string)
needed to catch the already-confirmed HIGH production vulnerability in `ReadExternalReference<T>()`.
