# Audit: include/CNA/Internal/Xnb/CurveContentTypeReader.hpp

## Metadata
- Source file: `include/CNA/Internal/Xnb/CurveContentTypeReader.hpp`
- Audit status: AUDITED (full read, 60 lines)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ header
- XNA/FNA relevance: matches FNA's `CurveReader` (`src/Content/ContentReaders/CurveReader.cs`)
- Main related tests: not independently located in this pass

## Purpose
Declares the `.xnb` reader for `Curve` (pre/post-loop type, then a key-count-prefixed list of
`CurveKey`s).

## Executive Verdict
Healthy -- correct FNA-faithful field order; one LOW-severity consistency observation (the key-count loop
doesn't consult `XnbReadLimits::maxCollectionElementCount`, though it's naturally bounded by stream
exhaustion the same way other unbounded-count loops in this codebase already are).

## Checklist Results

### FNA parity: field order verified correct
`PreLoop`, `PostLoop` (both `CurveLoopType`, read as `Int32`), then a key count, then per-key
`Position`/`Value`/`TangentIn`/`TangentOut`/`Continuity` -- matches FNA's real `CurveReader` field order.

### LOW: `keyCount`-driven loop doesn't consult `XnbReadLimits::maxCollectionElementCount`
`const int32_t keyCount = input.ReadInt32();` (line 42) is used directly as a loop bound with no limit
check. A negative `keyCount` is harmless (zero iterations, since the loop condition `i < keyCount` is
immediately false). A very large positive `keyCount` is not a distinct new vulnerability -- like this
project's own `NetDiscoveryProtocol::ReadProperties()` (a different shard, same reasoning already
established there), the loop is naturally bounded by the underlying `ContentReader`/`BinaryReader` throwing
once the stream is actually exhausted, so it cannot read past the real file's own bounded size. It is
inconsistent, though, with `XnbReadLimits`' own stated purpose ("largest single array/list/dictionary
element count this reader will allocate for") -- `CurveReader`'s bespoke key loop is a separate code path
from whatever generic collection reader enforces that limit (see `CollectionContentTypeReaders.hpp`'s own
report for whether *that* file's readers actually consult it), and doesn't benefit from an explicit,
clear-error-message rejection the way a `limits`-consulting call site would.

## Detailed Findings
1. **[LOW] `CurveReader::Read()`'s key-count loop doesn't consult `maxCollectionElementCount`** -- naturally
   bounded by stream exhaustion (not a memory-safety gap), but inconsistent with this subsystem's own
   declared-limits design intent. Lines 42-51.

## Cross-File Observations
Adds a third data point (alongside `maxStringBytes`/`maxObjectNestingDepth`) suggesting `XnbReadLimits`'
bounds are inconsistently threaded through the individual content-type-reader implementations, even where a
reader has its own bespoke count-driven loop outside the generic collection-reader path.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct FNA-faithful field order and correct reuse of the existing `Curve`/`CurveKey`/`CurveKeyCollection`
runtime API rather than re-deriving curve semantics in the reader itself.

## Final Assessment
One LOW-severity consistency observation: key-count loop bypasses the declared (if already partially dead)
`maxCollectionElementCount` limit, though naturally bounded by stream exhaustion.
