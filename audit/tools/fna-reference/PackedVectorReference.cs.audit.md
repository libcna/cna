# Audit: tools/fna-reference/PackedVectorReference.cs

## Metadata
- Source file: `tools/fna-reference/PackedVectorReference.cs` (360 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-fna-reference` shard
- File type: C# tool (Task 473, part of the `FnaReference` console app)
- XNA/FNA relevance: generates authoritative FNA reference data (real `Microsoft.Xna.Framework.Graphics.PackedVector.*` constructors/`PackedValue`/`ToVectorN()` calls against the real compiled FNA.dll) used by this project's own audit/test methodology
- Main related tests: complements `tests/PackedVectorGolden.md` (Task 197's hand-derived Python re-implementation); consumed by `tools/cna-reference/` + `scripts/compare-fna-reference.py` (Task 479's cross-language diff)

## Purpose
Dumps reference pack/unpack values for all 17 `PackedVector` types by constructing each type
directly from the real, running FNA assembly and reading back `PackedValue`/`ToVectorN()` for a
handful of hand-picked input cases per type.

## Executive Verdict
**This file genuinely calls real FNA's own `Byte4`/`Short2`/`Short4`/etc. constructors — it does
NOT reimplement the pack/round formula independently in C#.** This confirms the reference data it
produces is authentic FNA ground truth, not a second, possibly-independently-wrong
transcription — a definitively trustworthy oracle for cross-checking CNA's own C++ port.

**However, this file contains a real, significant HIGH-severity gap that directly explains why
this project's own comparison-harness safety net (Task 479) never caught the already-confirmed
CNA defect (`Byte4.hpp`/`Short2.hpp`/`Short4.hpp` truncate instead of round in `Pack()`,
confirmed this session in the `xna-graphics` shard audit): this file's own hand-picked test inputs
for exactly `DumpByte4()`/`DumpShort2()`/`DumpShort4()` — and *only* those three, of all 17 dump
functions — use 100% integer-valued inputs.** Truncation and proper rounding are indistinguishable
for an already-integer input; the bug can only be observed with a genuinely fractional value. See
Detailed Findings.

## Checklist Results
- `DumpAlpha8()`: uses `0.25f`, `0.50f` (fractional) — would catch a rounding regression.
- `DumpBgr565()`/`DumpBgra4444()`/`DumpBgra5551()`: use `0.5f` (fractional, a genuine
  rounding-tie boundary) — would catch a rounding regression.
- `DumpHalfSingle()`/`DumpHalfVector2()`/`DumpHalfVector4()`: use `0.5f`, `0.25f`, `-1` (fractional)
  — appropriate for IEEE-754 half-float conversion, which has its own distinct rounding behavior.
- `DumpNormalizedByte2()`/`DumpNormalizedByte4()`/`DumpNormalizedShort2()`/`DumpNormalizedShort4()`:
  use `0.5f` (fractional, a genuine rounding-tie boundary for the normalized `[-1,1]`-range types).
- `DumpRg32()`/`DumpRgba1010102()`/`DumpRgba64()`: use `0.5f` (fractional).
- **`DumpByte4()`** (lines 126-143): inputs `{255,0,0,255}`, `{0,255,0,255}`, `{100,150,200,128}` —
  **every single value is a whole integer**, unlike every sibling `Dump*()` function above.
- **`DumpShort2()`** (lines 323-339): inputs `{32767,0}`, `{0,32767}`, `{-32768,-32768}`,
  `{100,200}` — **every single value is a whole integer**.
- **`DumpShort4()`** (lines 341-358): inputs `{32767,0,0,32767}`, `{0,32767,-32768,0}`,
  `{100,200,300,400}` — **every single value is a whole integer**.

## Detailed Findings

### HIGH — `DumpByte4()`/`DumpShort2()`/`DumpShort4()` use exclusively integer test inputs, making them structurally incapable of distinguishing truncation from correct rounding
`Byte4`, `Short2`, and `Short4` are the "plain integer-range, not `[0,1]`/`[-1,1]`-normalized"
sub-family of `PackedVector` types (their constructors take raw byte-range/short-range float
values directly, confirmed by the test inputs themselves — e.g. `255`, `32767`, `-32768`). For any
of these three types, `Pack(x)` reduces to (conceptually) `(int)Round(clamp(x, min, max))`. If `x`
is already an exact integer, `Round(x) == Truncate(x) == x` — a truncating implementation and a
correctly-rounding implementation produce byte-identical output. **None of the 10 input tuples
across these 3 functions contains a single fractional value** (e.g. `100.5`, `150.7`), unlike every
one of the other 14 `Dump*()` functions in this same file, all of which deliberately include at
least one fractional value (`0.25`, `0.5`) specifically because their own pack formulas are
sensitive to rounding behavior.

**This is very likely the root cause of why the already-confirmed CNA `Byte4.hpp`/`Short2.hpp`/
`Short4.hpp` truncation-not-rounding defect (this session's `xna-graphics` shard audit) went
undetected by this project's own FNA-vs-CNA comparison harness (Task 479,
`tools/cna-reference/` + `scripts/compare-fna-reference.py`)**: that harness's own README status
note (`tools/fna-reference/README.md`, Task 479 entry) describes running the comparison and
finding "exactly one genuine divergence" (`IndexElementSize`, since fixed) — with no mention of any
`PackedVector` divergence at all, despite `Byte4`/`Short2`/`Short4` genuinely differing between FNA
(rounds) and CNA (truncates) for a fractional input. If the CNA-side reference dump
(`tools/cna-reference/`, audited separately) mirrors this same set of integer-only test inputs for
these 3 types — which, given it's explicitly documented as a "CNA-side C++ mirror of Tasks
472/473/476's own categories," is highly likely — then the comparison was structurally guaranteed
to agree for these 3 types regardless of whether CNA's `Pack()` truncated or rounded, since neither
implementation choice is exercised by an integer input.

**Suggested fix** (report-only; no source changes made per this audit's scope): add at least one
genuinely fractional input case to each of `DumpByte4()`/`DumpShort2()`/`DumpShort4()` (e.g.
`{100.6f, 150.3f, 200.9f, 128.4f}` for `Byte4`), then regenerate both the FNA-side and CNA-side
reference dumps and re-run the comparison — this should now surface the already-independently-
confirmed truncation-vs-rounding divergence directly, providing a second, fully-automated
confirmation of that finding.

## Cross-File Observations
- Directly explains a plausible gap in `tools/cna-reference/` (its own audit, assigned to a
  sibling fork in this same pass) — if that file's own `Byte4`/`Short2`/`Short4` dump functions
  mirror these same integer-only inputs, the comparison-harness blind spot originates in the shared
  test-input design pattern, present on both sides of the comparison.
- Confirms, independently and with high confidence, that this session's `xna-graphics` shard's
  `Byte4.hpp`/`Short2.hpp`/`Short4.hpp` finding reflects a genuine divergence from real, running
  FNA (not a misreading of FNA's source) — this file calls the real FNA types directly.
- The README's own claim that "Every single value across all 17 types matches Task 197's golden
  table exactly" is consistent with, not contradicted by, the finding above: Task 197's own golden
  table was itself hand-derived by re-implementing FNA's formulas from source (not independently
  checking CNA), so agreement there only confirms the *FNA-side* formula understanding is correct
  — it says nothing about whether CNA's own C++ implementation matches, which is a separate
  question this file's output was apparently never actually diffed against for a
  rounding-sensitive input.

## Missing or Weak Tests
The gap described above (no fractional test input for `Byte4`/`Short2`/`Short4`) is itself the
finding — adding one is the direct, actionable fix.

## Positive Findings
Every other one of the 17 `Dump*()` functions correctly includes at least one fractional/boundary
test value appropriate to that type's own rounding sensitivity — this file's overall test-design
discipline is otherwise excellent; the gap is narrowly confined to exactly the 3 types that also
happen to be the ones later found buggy.

## Final Assessment
One HIGH finding: `DumpByte4()`/`DumpShort2()`/`DumpShort4()`'s integer-only test inputs are
structurally incapable of catching a truncation-vs-rounding defect, very likely explaining why this
project's own automated FNA-vs-CNA comparison harness never caught the independently-confirmed
CNA defect in these same three types.
