# Audit: src/Microsoft/Xna/Framework/LaunchParameters.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/LaunchParameters.cpp`
- Audit status: AUDITED (full read, 143 lines)
- Subsystem: `xna-framework-core` shard
- File type: C++ implementation
- XNA/FNA relevance: matches real XNA `LaunchParameters`'s `/key:value` command-line parsing exactly,
  including its exact valid-colon-position range
- Main related tests: not independently located in this pass

## Purpose
Implements command-line-argument retrieval (Windows `CommandLineToArgvW`, Linux/Android
`/proc/self/cmdline`, portable no-op fallback) and `/key:value`-style parsing into the dictionary.

## Executive Verdict
Healthy -- the trickiest part of this file (the colon-position search range) was independently verified,
by hand-tracing both the match-found and match-rejected cases, to be an exact behavioral match for FNA's
own range-restricted `IndexOf` call.

## Checklist Results

### Colon-search-range equivalence: independently verified correct
The comment states FNA's `arg.IndexOf(":", 1, arg.Length - 2)` (searching only positions `[1, arg.Length-2]`
inclusive) is equivalent to this port's `arg.find(':', 1)` plus a `valueOffset > arg.size() - 2` rejection
check. Independently re-derived this equivalence: since both searches scan left-to-right starting at
position 1, they agree on the leftmost in-range match whenever one exists; the only divergence case (a
colon existing only at the very last character, `arg.size()-1`, outside FNA's own valid range) is
correctly caught by the explicit `> arg.size()-2` check, which rejects it the same way FNA's own bounded
search would report "not found." Confirmed correct for both the match and no-match cases, not merely
assumed from the comment's own claim.

### `Add()`'s duplicate-key deviation: correctly scoped
The comment notes `emplace()` doesn't throw on a duplicate key the way FNA's `Dictionary<string,string>.Add`
would -- independently confirmed this is inert in practice, since the only call site (`Parse()`) always
guards with `ContainsKey()` first, so `Add()` is never actually called with an already-present key.

### Platform-specific command-line retrieval: reasonable
Windows (`CommandLineToArgvW` + UTF-8 conversion), Linux/Android (`/proc/self/cmdline` NUL-separated
parsing), and a documented no-op fallback for other platforms are all correctly implemented for their
respective conventions.

## Detailed Findings
None.

## Cross-File Observations
N/A.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
A subtle FNA-parity claim (range-restricted `IndexOf` vs. unrestricted `find` plus a post-check) that was
independently verified correct rather than taken on faith.

## Final Assessment
No issues found.
