# Audit: include/Microsoft/Xna/Framework/GamerServices/Gamer.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/GamerServices/Gamer.hpp`
- Audit status: AUDITED (full read, 275 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
Abstract base class for all gamer types in the XNA GamerServices API (`NetworkGamer`,
`SignedInGamer` both derive from it); holds display name/gamertag/tag/leaderboard-writer state and
the static process-wide signed-in-gamers list.

## Executive Verdict
Correct. Constructor is `protected` (base-class-only-via-subclass), consistent with
`NetworkGamer`'s own already-audited constructor (`xna-net` shard). Every "not supported on this
platform" member (`GetFromGamertag`, `BeginGetFromGamertag`, `EndGetFromGamertag`,
`GetPartnerToken`, `BeginGetPartnerToken`, `EndGetPartnerToken`) throws
`System::NotSupportedException` — the correct sharp-runtime exception type, not a raw `std::`
exception, matching this project's own established convention (see Positive Findings).

## Checklist Results
- Doxygen coverage: complete.
- `NOXNA` usage: none needed — every member here maps to a real XNA `Gamer` member; the one
  genuinely non-XNA note (`setSignedInGamersProperty`'s doc comment, lines 84-93) is explained
  in-place rather than tagged, since the member itself (a plain public setter substituting for C#'s
  `internal set`) is real XNA API surface, just with C++-widened visibility.
- Visibility: constructor `protected`, correct.

## Detailed Findings
None.

## Cross-File Observations
- `leaderboardWriter_`'s doc comment (lines 260-268) discloses a real, load-bearing address-
  stability constraint: `LeaderboardWriter` captures `this` (the owning `Gamer`) at construction
  time, and neither `Gamer` nor `LeaderboardWriter` declares a custom copy/move constructor, so
  that captured pointer is copied verbatim (not re-pointed) by any copy/move of a constructed
  `Gamer`/`SignedInGamer` — including an ordinary `std::vector<SignedInGamer>::push_back(prvalue)`.
  This is a genuine, sharp, correctly-documented gotcha (Task 4.3) rather than a hidden trap; the
  comment names a concrete safe pattern (`cna_demo_leaderboard_viewer`'s heap-allocated
  `syntheticGamers_`) to follow instead.
- `getSignedInGamersProperty()`/`setSignedInGamersProperty()`'s lazy-init-singleton-with-
  self-managed-lifetime pattern is confirmed safe in the paired `.cpp` (checks `signedInGamers_ !=
  value` before deleting the old instance, avoiding a double-free if the same pointer is set twice).
- `NetworkSession`'s constructor (audited in the `xna-net` shard) calls
  `Gamer::getSignedInGamersProperty()` directly — confirmed this static accessor is a real,
  reachable production dependency, not dead API surface.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Every "not supported" method correctly throws `System::NotSupportedException` (the sharp-runtime
type), not a raw `std::` exception — a clean example of the exception-type convention this audit
has repeatedly found violated elsewhere in the codebase (recorded as a recurring cross-cutting
pattern). `GamerAction`'s constructor comment explicitly cites the already-fixed `audit_net.md`
"High finding" (callback stored but never invoked) as the reason `BeginGetProfile` invokes the
callback immediately after construction — confirmed consistent with the fix already applied to the
sibling `NetworkSession::NetworkSessionAction`/`InvokeActiveActionCallback` pattern in the `xna-net`
shard.

## Final Assessment
No findings.
