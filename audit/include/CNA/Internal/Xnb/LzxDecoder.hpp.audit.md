# Audit: include/CNA/Internal/Xnb/LzxDecoder.hpp

## Metadata
- Source file: `include/CNA/Internal/Xnb/LzxDecoder.hpp`
- Audit status: AUDITED (full read, 107 lines)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ header
- XNA/FNA relevance: a from-scratch, line-by-line C++ port of FNA's `LzxDecoder.cs`
  (`src/Content/LzxDecoder.cs`), itself a C# port of libmspack's `lzxd.c`
- Main related tests: not independently located in this pass (referenced: an "XNB-30A fuzz test")

## Purpose
Declares the stateful LZX block decompressor used by `.xnb` payload decompression -- one instance's
internal state (sliding window, repeated-offset LRU queue, Huffman tables) persists across every block
within a single file.

## Executive Verdict
Healthy -- see the paired `.cpp` for a genuinely careful, explicitly-hardened port of a historically
bug-prone decompression algorithm (LZX/libmspack-family decoders have real prior CVEs elsewhere in
out-of-bounds writes from corrupt match offsets/Huffman tables).

## Checklist Results
The header candidly documents its own porting methodology and scope: a *line-by-line* port preserving
FNA's variable names, control flow, and error-as-return-code style specifically so it can be verified
against the original source directly, with "malformed/adversarial-input hardening beyond FNA's own bounds
checks" explicitly called out as a deliberately separate, subsequent task (plans/plan_xnb.md XNB-30) -- and,
per the `.cpp`'s own comments, that hardening pass has genuinely landed (not merely planned).

## Detailed Findings
None in this header -- see the paired `.cpp`.

## Cross-File Observations
`state_.window`/`window_size` etc. are correctly scoped to persist per-instance across `Decompress()`
calls, matching the documented "one instance per file, never shared across files" contract enforced by the
type's own ownership model in `XnbDecompression.cpp` (a fresh `LzxDecoder` is constructed per
`DecompressXnbPayload()` call).

## Missing or Weak Tests
Not independently located in this pass; the referenced "XNB-30A fuzz test" suggests fuzzing has already
been applied to this exact class of bug -- worth confirming that test still exists and runs in CI when the
`tests-*` shards are audited.

## Positive Findings
Unusually forthright about the intentional scope split between "faithful FNA port" and "adversarial-input
hardening," which is exactly the right way to structure a security-sensitive binary-format port.

## Final Assessment
No issues found.
