# Audit: tests/Microsoft/Xna/Framework/Content/CnjResolverOrderTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Content/CnjResolverOrderTests.cpp` (133 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-content` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `ContentManager::ResolveAssetPath`'s `.cnj`-before-native-extension
  resolution priority (NOXNA content pipeline extension)
- Main related tests: N/A (this IS a test file)

## Purpose
Proves `.cnj` takes priority over a same-named native file, using distinct pixel colors as an
observable signal for which candidate path was actually resolved and loaded.

## Executive Verdict
Correct, and notably self-aware about its own maintenance history: the top comment (lines 3-10)
explicitly documents that the original test-writing trick (raw PNG bytes written directly into a
`.cnj`-extensioned path, relying on format-sniffing) stopped working once the real `.cnj` JSON
envelope parser started actually validating `.cnj` files as JSON, and explains the fix (a real
envelope + `sourceFile` field) — a genuine example of a test suite adapting correctly as production
code evolved, not silently rotting.

## Checklist Results
`CnjTakesPriorityOverNativeFileOfSameName`'s pixel-color assertion (line 131-132) with an explicit
failure message citing "cnj.md's core rule" is a precise, well-documented assertion of the exact
behavior under test.

## Detailed Findings
None.

## Cross-File Observations
None.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The color-as-observable-signal technique is an elegant, unambiguous way to prove which of two
candidate resolution paths was actually taken, and the file's own comment honestly documents how
the test had to adapt as the reader's real behavior changed.

## Final Assessment
No findings.
