# Audit: src/CNA/Internal/Xnb/LzxDecoder.cpp

## Metadata
- Source file: `src/CNA/Internal/Xnb/LzxDecoder.cpp`
- Audit status: AUDITED (full read, 679 lines)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ implementation
- XNA/FNA relevance: line-by-line port of FNA's `LzxDecoder.cs`/libmspack's `lzxd.c`
- Main related tests: not independently located in this pass

## Purpose
Implements the LZX bitstream reader (`BitBuffer`), Huffman decode-table construction
(`MakeDecodeTable`/`ReadLengths`/`ReadHuffSym`), and the main block-type dispatch loop
(Verbatim/Aligned/Uncompressed) with sliding-window match-copy.

## Executive Verdict
Healthy -- a genuinely well-hardened port of a historically bug-prone algorithm family. Independently
re-derived and confirmed correct for the two classic LZX/libmspack vulnerability classes (Huffman
decode-table overflow, out-of-window match-offset), both of which are explicitly, correctly guarded.

## Checklist Results

### Huffman decode-table overflow: correctly guarded (lines 190-211)
`MakeDecodeTable()`'s secondary (16-bit) table-fill path is the exact site of a well-known LZX/libmspack
vulnerability class (a corrupt/adversarial code-length table can drive `leaf`/`next_symbol` past the
table's allocated size). The comment correctly identifies *why* this needs an explicit guard in C++ that
FNA's own C# port doesn't (the CLR's automatic bounds-checked array access turns the same corrupt input
into a catchable `IndexOutOfRangeException`, whereas `std::vector::operator[]` would be undefined
behavior) -- and the guard itself (`if (leaf >= table.size() || (next_symbol << 1) + 1 >= table.size())
return 1;`, checked before every write in the inner fill loop, plus a second check before the final
`table[leaf] = sym` assignment) is independently verified correct: every write path into `table` is
preceded by a bounds check against the vector's real size.

### Out-of-window match-offset: correctly guarded (lines 480, 596)
The second classic vulnerability site: a corrupt/adversarial `match_offset` used to compute `runsrc` for a
sliding-window match copy, without validation, is a textbook out-of-bounds-read (and, via the subsequent
`window[rundest++] = window[runsrc++]` copy loop, potentially an out-of-bounds *write* if `rundest` were
also unbounded -- though `rundest`/`window_posn` are separately bounded by the run-can't-straddle-wraparound
check at line 411). Both the Verbatim and Aligned block-type paths correctly reject `match_offset <= 0 ||
match_offset > window_size` before computing `runsrc`, for the identical CLR-safety-net-vs-C++-UB reason
documented in the comment.

### Window run-length bound: correctly guarded (line 411)
`if ((window_posn + this_run) > window_size) return -1;` (checked once per `this_run` chunk, before any
block-type-specific processing) correctly prevents a run from straddling the window's wraparound boundary
regardless of block type -- verified this check's placement covers all three block-type branches
(Verbatim/Aligned/Uncompressed) since it precedes the `switch` that dispatches to them.

### `ExtraBits()`/`PositionBase()` indexing: verified in-bounds by construction
`match_offset` (post `main_element -= NUM_CHARS`, before the R0/R1/R2/repeated-offset resolution) is
derived from a Huffman symbol bounded by `MAINTREE_MAXSYMBOLS` (656), yielding a slot value in `[0, 49]`
after the `>>3` shift -- safely within both lookup tables' sizes (52 and 51 elements respectively).
Independently computed this bound by hand rather than assuming the table sizes were simply "big enough."

### EOF handling: deliberately unhardened, faithfully matching FNA, and confirmed harmless
`BitBuffer::EnsureBits()` reads past a truncated stream exactly as FNA does (`ReadByte()`'s `-1` wrapped to
`0xFF` via the `(byte)` cast, not treated as a special case) -- the comment is explicit this is deferred to
the (already-applied, per the findings above) XNB-30 hardening pass rather than an oversight. Confirmed
harmless: the outer decode loop is bounded by `togo` (derived from the caller-supplied, `XnbReadLimits`-
bounded `outLen`), so even worst-case "phantom `0xFF` padding past EOF" can only ever produce bounded
(possibly garbage, but never unbounded or memory-unsafe) output, or trip one of the explicit bounds checks
above and return early with an error code.

### Incomplete "Intel E8" decoding: faithfully reproduces FNA's own unfinished stub, confirmed safe
Lines 654-677 correctly and explicitly document that FNA's own port never finished this feature (always
returns `-1` regardless of what the preceding loop computes) -- reproduced verbatim rather than "fixed."
Independently confirmed this can be reached by an adversarial `.xnb` file (the `intel` header bit is read
directly from the attacker-controlled bitstream), but the only observable effect of reaching it is a clean
decompression failure (`return -1`), not memory corruption -- a safe, if functionally incomplete, path.

## Detailed Findings
None.

## Cross-File Observations
This is the most rigorously-hardened decompression code found anywhere in this audit so far -- the explicit
comments identifying *why* a C++ port needs guards a C# port doesn't (rather than just adding bounds checks
silently) make this file unusually easy to verify independently, which this pass did for both major
vulnerability classes.

## Missing or Weak Tests
Not independently located in this pass; the header's own reference to an "XNB-30A fuzz test" (presumably
covering the Huffman-table-overflow fix) should be confirmed to still exist when `tests-*` shards are
audited, along with a dedicated test for the out-of-window match-offset guard.

## Positive Findings
A textbook-quality hardening job against a historically real vulnerability class (LZX/libmspack-family
decompressors have genuine prior CVEs for exactly these two bug shapes) -- both guards independently
re-derived and confirmed correct in this pass, not merely trusted because a comment claims they're there.

## Final Assessment
No issues found.
