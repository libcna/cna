# Audit: tools/input_parity/gen_input_parity_matrix.py

## Metadata
- Source file: `tools/input_parity/gen_input_parity_matrix.py` (538 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-input-parity` shard
- File type: Python script (developer inspection tool)
- XNA/FNA relevance: developer tooling, not XNA API surface — cross-checks CNA's C++ Input headers'
  member-level surface against real FNA `.cs` reference source
- Main related tests: N/A (this tool generates a review document; it is not itself a test)

## Purpose
Mechanically extracts every public member (methods, operators, fields, enum values) from CNA's
public `Microsoft::Xna::Framework::Input` headers via a regex-based C++ parser, extracts the same
from real FNA `.cs` reference source via a regex-based C# parser, and cross-checks them, flagging
STRICT/EXT members with no FNA counterpart and FNA public members with no CNA counterpart.

## Executive Verdict
A genuinely sophisticated piece of tooling for what it is — a hand-rolled, brace-depth-tracking C++
declaration parser (not just line-based regex) that correctly distinguishes access-specifier
sections, handles inline method bodies (flushing the accumulated declaration text at the closing
`}` the same way a `;` would, with an explicit comment explaining why this is necessary to avoid
gluing onto the next declaration), and defends against enum-forward-declaration misparsing via a
documented `[^\{;]+` exclusion. Correctly and repeatedly self-identified as "a review aid, not an
authority."

## Checklist Results
- `parse_class()`'s statement-walking loop (lines 105-153) correctly tracks brace depth and access
  labels character-by-character — a real, non-trivial mini-parser, not a naive regex over the whole
  file. The inline-method-body-flush special case (lines 114-129) is specifically called out in its
  own comment as fixing a real garbled-output bug ("otherwise silently glues onto the *next*
  declaration's text"), suggesting this was found and fixed through actual use, not designed in
  from a spec.
- `ENUM_CLASS_RE`'s `[^\{;]+` exclusion (documented at length, lines 189-196) correctly prevents a
  bodyless forward-declared `enum class FooEXT : std::uint32_t;` from being misparsed as starting
  an enum body that swallows an unrelated later brace — a genuine, subtle regex-correctness fix
  with a clear root-cause explanation.
- `CS_INTERFACE_PLUMBING` (lines 327-330) correctly documents an intentional, disclosed deviation
  (CNA's value-type collections don't mirror C#'s `IList<T>`/`IEnumerator`/`IDisposable` plumbing)
  rather than silently treating those as parity gaps.
- The cross-check's own "Review summary" output (lines 442-461) explicitly warns that flagged rows
  "may be false positives... Review each against the .cs before acting" — consistent, honest framing
  throughout the tool, not just in the top-of-file docstring.

## Detailed Findings
None. This is heuristic tooling that correctly and repeatedly discloses its own heuristic nature;
no correctness defect was found in the parsing/cross-check logic itself during a full read.

## Cross-File Observations
Complements `tools/input_parity/check_input_test_coverage.py` (audited alongside this file) — see
that report's Cross-File Observations for how the two tools cover distinct axes of Input-subsystem
health.

## Missing or Weak Tests
No test was located verifying this tool's own parser against a synthetic C++/C# fixture pair with
a known-correct expected parity matrix — a regression in the brace-depth-tracking logic (e.g. the
inline-body-flush fix) would currently only be caught by a human reviewing the generated matrix's
output, not by an automated check on the tool itself.

## Positive Findings
The documented history of two specific parser bugs found and fixed (the inline-method-body-flush
issue, the bodyless-enum-forward-declaration misparse) both come with precise root-cause
explanations rather than just a fix with no context — genuinely useful for anyone maintaining this
tool later.

## Final Assessment
No findings.
