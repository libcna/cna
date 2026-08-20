# Audit: include/CNA/Internal/GamerServices/LocalGamerServicesStore.hpp

## Metadata
- Source file: `include/CNA/Internal/GamerServices/LocalGamerServicesStore.hpp`
- Audit status: AUDITED
- Subsystem: `cna-internal-core` shard
- File type: C++ header
- XNA/FNA relevance: local persistence backing `Microsoft::Xna::Framework::GamerServices`
  (Achievement/LeaderboardWriter/LeaderboardReader) — no catalog, only the fact-of-earning/rating is real
  local data
- Main related tests: presumably under `tests/CNA/Internal/GamerServices/` or `tests/Microsoft/Xna/Framework/GamerServices/`

## Purpose
Declares the local JSON-file-backed persistence API for achievements (one file per gamertag) and leaderboards
(one file per leaderboard-key+game-mode), plus the file-name-sanitization helpers both use.

## Executive Verdict
Needs attention — 1 minor, latent (not actively exploitable given current call sites) finding in the
sanitizer's own contract; otherwise clean, well-documented API design.

## Checklist Results
### API design
Clearly documents the "never throws for missing/corrupt file, starts empty instead" contract (matching a
named task requirement, `plans/plan_net.md` Task 4.7) consistently across every loader. Explains its reuse of the
existing `StorageDevice::GetStorageRootEXT()` convention rather than inventing a new storage location.

### Security — sanitizer contract
See `LocalGamerServicesStore.cpp`'s own report for the confirmed finding: `SanitizeStoreFileNameComponent()`'s
own doc comment claims it makes a string "safe to use as a single path component," but the implementation
allows a bare `"."`/`".."` to pass through completely unchanged (both characters are in its whitelist) — not
actively exploitable at either of this codebase's 2 real call sites (both always append a non-empty suffix
after sanitizing), but a latent gap relative to the function's own stated contract should a future caller use
it without a subsequent suffix.

## Detailed Findings
See `.cpp` report for the concrete confirmation.

## Cross-File Observations
`MakeLeaderboardFileKeyEXT()`'s own `_<gameMode>` suffix is what actually prevents a bare `".."` from a
leaderboard key; the raw sanitizer function itself does not.

## Missing or Weak Tests
No dedicated test found in this pass exercising `SanitizeStoreFileNameComponent("..")`'s own return value in
isolation (only indirectly, via the 2 real call sites' own suffix-protected usage).

## Positive Findings
Consistent, clearly-documented "never throws on corrupt/missing data" contract across the whole API surface.

## Final Assessment
1 minor, non-actively-exploitable finding (sanitizer allows bare "."/".." through) — flagged for awareness,
not a confirmed vulnerability given current usage.
