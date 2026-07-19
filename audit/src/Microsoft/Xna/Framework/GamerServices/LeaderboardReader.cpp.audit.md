# Audit: src/Microsoft/Xna/Framework/GamerServices/LeaderboardReader.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/GamerServices/LeaderboardReader.cpp`
- Audit status: AUDITED (full read, 403 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace (confirmed:
  FNA's real `LeaderboardReader` is entirely `NotSupportedException`-stubbed, so every behavior
  below is a CNA-original, honestly-disclosed default, not an FNA-fidelity claim)
- Main related tests: not independently located in this pass

## Purpose
Implements the constructor, `ResliceEntriesEXT`, every property, `Dispose`, the full
`PageUp`/`PageDown`/`Read` (+ Begin/End) factory/paging surface, and the local
`LoadFullLocalLeaderboardEXT`/`FindGamerIndex`/`CenterPageOnPivot`/`LeaderboardAction` helpers.

## Executive Verdict
Correct, and confirms the header's documented quirk-preservation split is genuinely implemented
as described.
- Constructor (lines 148-174): slices `entries_` from `entryCache_` using `i < pageSize_` (not
  `i < pageStart_ + pageSize_`) — confirmed matching the header's claimed FNA quirk, with an inline
  comment reiterating "not a typo introduced here, this is the reference behavior."
- `ResliceEntriesEXT()` (lines 186-193): uses the actually-correct `i < pageStart_ + pageSize_`
  window, confirmed used by every real production paging path (`EndPageDown`, `EndPageUp`, and
  every `BeginRead` overload, each calling it immediately after `CreateInternal` to correct the
  constructor's page-0-only slice).
- `getCanPageDownProperty()` (lines 197-207): its own comment documents and explains a real,
  previously-fixed bug (Task 4.4) — the old non-friend-board branch used
  `pageStart_ < entryCache_.size()` (true for almost the entire board), letting `PageDown()` walk
  one page past the real end; the current single bounded-array check
  `(pageStart_ + pageSize_) < entryCache_.size()` applies uniformly to both board kinds and is
  confirmed correct.
- `LoadFullLocalLeaderboardEXT` (lines 30-67): sorts locally-persisted entries rating-descending,
  assigns 1-based `RankingEXT` reflecting that full sorted order, and — a documented, real
  limitation, not a silent gap — skips any persisted gamertag with no currently-signed-in `Gamer*`
  match, since `LeaderboardEntry::getGamerProperty()` needs a real, live, non-owning pointer with
  no fabricated stand-in.
- `LeaderboardAction` (lines 100-124): a real, working `IAsyncResult`, confirmed to actually invoke
  its stored callback (both `CompleteReadEXT`/`CompletePageEXT`), citing this project's own prior
  "callback stored but never invoked" High finding (`audit_net.md`) as the precedent this
  deliberately avoids repeating.

## Checklist Results
No issues found.

## Detailed Findings
None. One minor, non-defect inefficiency: every `BeginRead` overload constructs its
`LeaderboardReader` via `CreateInternal` (which performs the page-0-only slice into `entries_`)
and then immediately calls `reader.ResliceEntriesEXT()` to redo that same work correctly — the
constructor's first slice is entirely wasted for any `pageStart != 0` call. Functionally harmless
(the final `entries_` value is always correct) and not flagged as a finding given the file's own
honest documentation of why the constructor can't simply be changed (it must preserve Task 10.6's
original quirk-validated behavior for direct `CreateInternal` callers).

## Cross-File Observations
`FindGamerIndex`/`CenterPageOnPivot` are pure, well-isolated local helpers with no observable side
effects — `CenterPageOnPivot`'s `pivotIndex < 0` fallback-to-top-of-page behavior is explicitly
disclosed as "a conservative default (no FNA reference exists to contradict it)," consistent with
the header's own framing.

## Missing or Weak Tests
Not independently located in this pass; the `getCanPageDownProperty()` off-by-one Task 4.4 fix and
the constructor-vs-`ResliceEntriesEXT()` slicing-window distinction would both be natural
regression-test candidates.

## Positive Findings
The three-overload `Read`/`BeginRead`/`EndRead` family and `PageUp`/`PageDown` both correctly
implement this project's "fake async, real callback invocation" convention, and the previously
fixed `getCanPageDownProperty()` off-by-one bug is confirmed genuinely resolved with a clear
account of what the old, broken logic looked like.

## Final Assessment
No findings.
