# Audit: tools/cna-reference/JsonWriter.hpp

## Metadata
- Source file: `tools/cna-reference/JsonWriter.hpp` (87 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-cna-reference` shard
- File type: C++ header-only helper (minimal hand-rolled JSON writer)
- XNA/FNA relevance: none — internal dev tooling only, explicitly outside both the `Microsoft::Xna`
  and `CNA` namespaces per its own top-of-file comment
- Main related tests: N/A

## Purpose
A minimal, dependency-free JSON writer (`Add(name, value)` overloads for `double`/`int`/`bool`/
`string`/nested `JsonWriter`) used by `CnaReferenceDump.cpp` to emit reference-value JSON that
structurally mirrors `tools/fna-reference/JsonWriter.cs`'s own output shape.

## Executive Verdict
Correct, minimal, and specifically hardened against a real, previously-found precision bug:
`Add(name, double)`'s comment (lines 25-28) explains that `std::ostringstream`'s default 6-
significant-digit precision silently truncated both large packed-value integers (e.g.
`4278190335` → `"4.2782e+09"`) and sub-millimeter float differences, fixed by using
`std::numeric_limits<double>::max_digits10` — the minimum precision that round-trips any `double`
back to its exact original bit pattern.

## Checklist Results
- `Quote()` (lines 73-83) correctly escapes only `"` and `\` — sufficient for this tool's own
  controlled input domain (enum/property names, `ToString()` output, exception messages via the
  caller), though it does not escape control characters (`\n`, `\t`, etc.) the way a
  general-purpose JSON writer would. Not flagged as a defect: this header's own top comment
  explicitly disclaims general-purpose JSON-library responsibilities ("internal dev tooling only");
  a caller here would need to already be producing single-line, printable content, which matches
  every actual call site in `CnaReferenceDump.cpp`.
- `Add(name, JsonWriter)` (nested-object support, lines 53-57) correctly recurses via `ToString()`,
  producing valid nested JSON without needing a separate value-type discriminator.

## Detailed Findings
None.

## Cross-File Observations
The `max_digits10` fix is directly exercised and validated by `CnaReferenceDump.cpp`'s own large
packed-vector integer values and Viewport round-trip error calculations (audited alongside this
file) — a real, load-bearing precision requirement, not a speculative hardening.

## Missing or Weak Tests
No test was located verifying this writer's exact JSON output against a fixture — reasonable given
its narrow, single-consumer scope, but a regression in the `max_digits10` precision fix specifically
would currently only be caught by a human noticing a truncated value in a diff output, not by an
automated check.

## Positive Findings
The precision-bug comment is a clear, specific example of "this fix exists because of a real,
previously-observed failure," not a defensive guess.

## Final Assessment
No findings.
