# Audit: include/CNA/Internal/Xnb/XnbTypeName.hpp

## Metadata
- Source file: `include/CNA/Internal/Xnb/XnbTypeName.hpp`
- Audit status: AUDITED (full read, 164 lines, header-only)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ header (header-only implementation)
- XNA/FNA relevance: matches FNA's own `.xnb` type-reader-name canonicalization needs (assembly-qualified
  .NET type names, e.g. `ListReader\`1[[Vector3, ...]]`)
- Main related tests: not independently located in this pass

## Purpose
Parses a raw, possibly-assembly-qualified, possibly-generic `.xnb` type-reader name into a structured,
assembly-stripped `XnbTypeName` (recursively for nested generic arguments), and reconstructs the canonical
registry-key string from it.

## Executive Verdict
Needs attention -- a correct, careful recursive-descent parser (nested generics, bracket-depth tracking,
and EOF/malformed-input all correctly handled with `std::invalid_argument`, not UB) but with a genuine
MEDIUM-severity gap: no recursion-depth limit, allowing a maliciously deeply-nested generic type name to
overflow the C++ call stack.

## Checklist Results

### Correctness: nested-generic bracket-depth tracking verified correct
`Detail::ParseOne()`'s generic-argument-list handling correctly locates each argument's own matching `]` by
bracket depth (incrementing on `[`, decrementing on `]`, tracking when depth returns to 0) rather than
naively splitting on the first `,` or `]` -- verified by hand-tracing a nested example
(`DictionaryReader\`2[[String, ...],[ListReader\`1[[Int32, ...]], ...]]`) through the algorithm: the inner
`ListReader\`1[[Int32, ...]]` argument's own internal `,`/`]` characters are correctly *not* mistaken for
the outer list's separators, since the depth counter is still > 0 while scanning through them.

### Correctness: no dangling `string_view`
The recursive call passes `argText` (a `string_view` slice of the *original* backing string, not a
temporary) down to the nested `ParseOne()` call -- verified this stays valid because the top-level entry
point (`ParseXnbTypeName`) is given a `const std::string&` whose backing buffer outlives the whole
(synchronous, non-escaping) parse.

### MEDIUM: unbounded recursion depth -- stack-overflow DoS via a crafted type-reader name
`ParseOne()` recurses once per nested generic-argument level with **no depth counter and no consultation of
`XnbReadLimits::maxObjectNestingDepth`** (which is declared for exactly this purpose but, per that file's
own audit, has zero consumers anywhere in the codebase). A `.xnb` file's type-reader-table entry name (fed
here via `XnbTypeReaderTable.hpp`'s `ParseXnbTypeReaderTable()`) is attacker-shapeable content if a hostile
`.xnb` file can be substituted into a game's content directory: a name like `A[[A[[A[[A[[...` needs only
~3 bytes per nesting level, so even a modest string (well within any single-string size any reasonable
`.xnb` file would contain) can encode tens or hundreds of thousands of nesting levels -- comfortably enough
to exhaust a typical thread's C++ call stack (each `ParseOne` frame holds several locals; typical stack
sizes are a few MB) and crash the process, rather than fail cleanly with `ContentLoadException` the way
every other malformed-input case in this pipeline does.

**Fix shape**: thread an explicit depth parameter through `Detail::ParseOne()` (or a thread-local/parameter
counter), incrementing on each generic-argument-list recursion and throwing `std::invalid_argument` (already
this file's established malformed-input signal, correctly translated to `ContentLoadException` by
`XnbTypeReaderTable.hpp`'s own caller) once it exceeds a bound -- ideally `XnbReadLimits::maxObjectNestingDepth`,
finally giving that currently-dead limit a real consumer.

## Detailed Findings

1. **[MEDIUM] Unbounded recursion depth in `Detail::ParseOne()`'s generic-argument parsing** -- a crafted
   `.xnb` type-reader name with deep generic nesting can stack-overflow the process rather than failing
   cleanly. Lines 67-133.

## Cross-File Observations
This is the concrete instance backing `XnbReadLimits.hpp`'s own finding that `maxObjectNestingDepth` is
declared but has zero real consumers anywhere in the codebase.

## Missing or Weak Tests
Not independently located in this pass; a test parsing a deliberately deeply-nested (e.g. 100,000-level)
generic type name would directly demonstrate the crash this finding describes.

## Positive Findings
Careful, correct handling of nested-generic bracket matching and `string_view` lifetime -- the parsing logic
itself (absent the missing depth guard) is well-implemented and correctly throws on genuinely malformed
(unbalanced-bracket) input rather than misbehaving silently.

## Final Assessment
One MEDIUM-severity finding: unbounded recursion depth allows a stack-overflow DoS via a crafted deeply-
nested generic type-reader name.
