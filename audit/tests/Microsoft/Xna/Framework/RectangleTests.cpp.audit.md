# Audit: tests/Microsoft/Xna/Framework/RectangleTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/RectangleTests.cpp` (337 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-framework-core` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Rectangle`
- Main related tests: N/A (this IS a test file)

## Purpose
Comprehensive tests for `Rectangle`: construction, edge/center properties (including odd-sized
truncation), `Contains`/`Intersects` (point, rectangle, and `Point`-overload forms, value and
out-ref), static `Intersect`/`Union` (value and out-ref), equality, `Offset`/`Inflate`,
`Location` getter/setter, the static `Empty` property, `GetHashCode`, and the exact `ToString`
format.

## Executive Verdict
Excellent, complete coverage, correctly verifying XNA's real exclusive-right/bottom-edge `Contains`
semantics (`ContainsPointOnEdgeReturnsFalse`) — a real, easy-to-get-backwards XNA behavioral detail
correctly tested rather than assumed.

## Checklist Results
`CenterOfOddSizedRectangleTruncates` correctly verifies integer-division truncation behavior for an
odd width/height, rather than only testing the even (exact) case.

## Detailed Findings
None.

## Cross-File Observations
None.

## Missing or Weak Tests
Not identified — coverage is comprehensive.

## Positive Findings
The exclusive-edge `Contains` semantics and the odd-size truncation case are both real, non-obvious
XNA behavioral details correctly captured.

## Final Assessment
No findings.
