# Audit: tools/fna-reference/JsonWriter.cs

## Metadata
- Source file: `tools/fna-reference/JsonWriter.cs` (91 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-fna-reference` shard
- File type: C# tool (Task 471, part of the `FnaReference` console app)
- XNA/FNA relevance: none directly (pure serialization helper); supports the FNA reference-value
  dump this shard's other files produce
- Main related tests: consumed by `scripts/compare-fna-reference.py` (parses this file's JSON
  output)

## Purpose
A tiny, dependency-free, hand-rolled JSON object writer (flat or one-level-nested, numbers/
strings/bools) — this sandbox has no `dotnet`/NuGet, so a real JSON library is not viable.

## Executive Verdict
Correct for its narrowly-scoped, explicitly-documented purpose. `Quote()` correctly escapes the 5
characters that are mandatory or highly likely to appear in this tool's actual string values
(`"`, `\`, newline, carriage return, tab) — sufficient given every string value produced elsewhere
in this shard is either an enum member name, a hand-written test-case label, or a `ToString()`
result of a well-behaved XNA value type, none of which would realistically contain other C0 control
characters.

## Checklist Results
- `Add(string, double)` uses `"R"` (round-trip) format with `CultureInfo.InvariantCulture` — the
  correct, standard .NET idiom for lossless double-to-string-and-back serialization, and
  culture-invariant (avoids a locale where `,` is the decimal separator producing invalid JSON).
- `Add(string, int)`/`Add(string, bool)` are similarly culture-invariant and unambiguous.
- `ToString()` (lines 76-89) correctly joins fields with commas only *between* entries (the
  `if (i > 0)` guard), avoiding a trailing-comma syntax error.

## Detailed Findings
None. (Minor, non-actionable observation: `Quote()` does not escape other C0 control characters
below `0x20` besides `\n`/`\r`/`\t`, which would technically produce invalid JSON if such a
character ever appeared in a string value — not flagged as a finding since no realistic input in
this tool's own actual usage could contain one.)

## Cross-File Observations
Worth checking against `tools/cna-reference/JsonWriter.hpp` (the CNA-side C++ counterpart, audited
separately) for output-format compatibility, since `scripts/compare-fna-reference.py`
(Task 479) parses and diffs both tools' JSON output — if the two `JsonWriter`s ever diverge in
escaping/number-formatting conventions, the comparison could produce false-positive/false-negative
diffs unrelated to any real FNA/CNA behavioral difference.

## Missing or Weak Tests
Not independently located in this pass; this file is itself infrastructure, not a test.

## Positive Findings
Precisely scoped to the actual need ("not a general-purpose JSON library — just enough for this
tool's own reference-value output," per its own doc comment) — no speculative generality added.

## Final Assessment
No findings.
