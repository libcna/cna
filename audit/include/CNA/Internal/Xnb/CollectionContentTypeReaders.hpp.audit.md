# Audit: include/CNA/Internal/Xnb/CollectionContentTypeReaders.hpp

## Metadata
- Source file: `include/CNA/Internal/Xnb/CollectionContentTypeReaders.hpp`
- Audit status: AUDITED (full read, 263 lines, header-only, no paired `.cpp` -- template classes)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ header (header-only implementation)
- XNA/FNA relevance: matches FNA's `ArrayReader<T>`/`ListReader<T>`/`DictionaryReader<TKey,TValue>`/
  `NullableReader<T>`
- Main related tests: not independently located in this pass

## Purpose
Declares the generic collection `.xnb` readers, each templated on element type(s) and constructed with the
element reader's hardcoded canonical name (since CNA has no reflection to resolve `typeof(T)` the way FNA's
own `Initialize(ContentTypeReaderManager)` does).

## Executive Verdict
Healthy -- a well-designed adaptation of FNA's reflection-based generic-reader resolution to a
reflection-free C++ registry, with correctly-enforced collection-size limits (a positive contrast to
`CurveContentTypeReader.hpp`'s bespoke, unguarded key-count loop, same shard).

## Checklist Results

### Collection-size limit: correctly enforced (positive contrast within this same shard)
`ArrayReader`/`ListReader`/`DictionaryReader::Read()` all call `input.CheckCollectionElementCount(count,
...)` immediately after reading the count and before any allocation/loop -- this is the actual, working
consumer of `XnbReadLimits::maxCollectionElementCount` the earlier grep found registered in
`ContentReader.cpp`. Confirms the limit genuinely is wired in for the *generic* collection-reading path,
even though `CurveContentTypeReader.hpp`'s own bespoke key-count loop (a separate code path for the same
general "count-prefixed list" shape) does not benefit from it.

### `ArrayReader`'s existing-instance-resize deviation: a safe, well-reasoned improvement over FNA
FNA's own `ArrayReader<T>` assumes an `existingInstance` array is already correctly sized and writes into it
by raw index (undefined behavior if the caller's array is too small); this port instead resizes
`existingInstance` to match the serialized count if it doesn't already -- a deliberate, safe deviation
(documented as such) that trades one possible caller-error mode (a wrongly-sized existing array, which FNA's
C# would silently overrun or throw `IndexOutOfRangeException` for) for defined resizing behavior.

### `ListReader`/`DictionaryReader`'s asymmetric existing-instance handling: correctly attributed to FNA
`ListReader` appends without clearing (matching FNA's own `list.Add()`-without-`.Clear()` loop);
`DictionaryReader` clears first (matching FNA). Both are documented as intentionally faithful to FNA's own
(admittedly asymmetric) real behavior -- not independently re-verified against the FNA reference tree in
this pass, but consistent with this file's overall care in citing FNA source files by name elsewhere.

### Reflection-free generic-reader resolution: a sound architectural adaptation
Each closed-generic reader hardcodes its own element-reader canonical name string (passed at construction)
rather than attempting runtime type resolution -- correctly reasoned as valid specifically because every
CNA reader so far is stateless, with an explicit note to revisit if that ever changes. `IsSharedPtr<T>`
correctly distinguishes reference-type-like (polymorphic, 1-based-dispatch) elements from value-type
elements needing an explicit fixed reader.

## Detailed Findings
None.

## Cross-File Observations
Provides the actual working enforcement of `XnbReadLimits::maxCollectionElementCount` that
`CurveContentTypeReader.hpp`'s own bespoke loop lacks (see that file's LOW-severity finding) -- confirms the
limit is not entirely dead, just inconsistently applied across bespoke vs. generic count-prefixed reads.

## Missing or Weak Tests
Not independently located in this pass; given the reflection-free design's reliance on hardcoded canonical
name strings at each call site, a test asserting a typo'd/unregistered element-reader name fails cleanly
via `Detail::RequireReader`'s `ContentLoadException` (rather than silently) would be valuable.

## Positive Findings
A genuinely well-designed reflection-free adaptation of FNA's generic collection readers, with real,
working collection-size-limit enforcement -- the strongest example in this shard of `XnbReadLimits`
actually being consulted as designed.

## Final Assessment
No issues found.
