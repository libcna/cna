# Audit: src/Microsoft/Xna/Framework/GameComponentCollection.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/GameComponentCollection.cpp`
- Audit status: AUDITED (full read, 168 lines)
- Subsystem: `xna-framework-core` shard
- File type: C++ implementation
- XNA/FNA relevance: matches real XNA `GameComponentCollection`'s exact add/remove/duplicate-rejection
  behavior, including the literal FNA error message text
- Main related tests: not independently located in this pass

## Purpose
Implements `InsertItem`/`RemoveItem`/`ClearItems`/`SetItem` and the public `Add`/`Insert`/`Remove`/
`RemoveAt`/`Clear`/`IndexOf`/`Contains`/indexer methods that delegate to them.

## Executive Verdict
Needs attention -- one LOW-severity, easily-fixable finding: `SetItem()` throws `std::logic_error` with a
comment claiming `System::NotSupportedException` isn't available yet, but that type already exists in
sharp-runtime and is used by 16 other CNA files -- a stale comment and an avoidable exception-type-fidelity
gap. Every other behavior (duplicate-component rejection with the literal FNA error message,
`ClearItems()`'s removed-event-per-component-then-clear ordering) is correct.

## Checklist Results

### LOW: `SetItem()` uses `std::logic_error` instead of the already-available `System::NotSupportedException`
```cpp
void GameComponentCollection::SetItem(size_type index, IGameComponent* item)
{
    ...
    // FNA throws NotSupportedException; C++ uses std::logic_error (no System::NotSupportedException yet).
    throw std::logic_error("SetItem is not supported.");
}
```
The comment's premise is factually stale: `System::NotSupportedException` already exists in sharp-runtime
(`sharp-runtime/include/System/NotSupportedException.hpp`, deriving from `SystemException`, with the same
string-message constructor shape used throughout this codebase) and is already used by 16 other files in
this repository. This project's own `CLAUDE.md` conventions require adding a missing `.NET`-runtime type to
sharp-runtime rather than substituting a raw C++ standard-library exception -- that requirement is already
satisfied (the type exists), it just wasn't used here, likely because this file was written before
`NotSupportedException` was added to sharp-runtime and never revisited. Low severity because `SetItem()` is
a private override hook with no public "set component at index" entry point exposed by this class (the
public `operator[]` is read-only), so this is very unlikely to ever actually throw in practice -- but it's
a simple, concrete fix: replace `throw std::logic_error(...)` with `throw System::NotSupportedException(...)`
for full exception-type fidelity with real XNA's `Collection<T>.SetItem` override.

### `InsertItem()`: correctly matches FNA's literal error message and duplicate-rejection behavior
Rejects re-adding an already-present component with the exact message
`"Cannot Add Same Component Multiple Times"` -- matches FNA's own real `GameComponentCollection.InsertItem`
error text verbatim, confirming a faithful port (not just a similar-sounding message).

### `ClearItems()`: correct removal-event ordering
Raises `ComponentRemoved` for every component (skipping null entries) before clearing the underlying
vector -- matches the expected "notify then remove" semantics.

## Detailed Findings

1. **[LOW] `SetItem()` uses `std::logic_error` instead of the already-available
   `System::NotSupportedException`, based on a now-stale comment.** Line 150-156.

## Cross-File Observations
Worth a quick repository-wide check (when convenient, e.g. during Pass 3's FNA/XNA parity sweep) for other
files with similar "no System::X yet" comments that may have become stale as sharp-runtime grew.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct, literal-error-message-verified faithful port of `InsertItem`'s duplicate-rejection and
`ClearItems`'s removal-event ordering.

## Final Assessment
One LOW-severity finding: `SetItem()`'s exception type is stale relative to sharp-runtime's current
`System::NotSupportedException` availability -- a simple, low-risk fix.
