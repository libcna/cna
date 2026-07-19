# Audit: examples/demo_friends_and_gamercard/src/FriendsGame.cpp

## Metadata
- Source file: `examples/demo_friends_and_gamercard/src/FriendsGame.cpp` (204 lines)
- Audit status: AUDITED
- Subsystem: `examples-demo_friends_and_gamercard` shard
- File type: standalone `Game`-subclass demo implementation (Task 15.14)
- XNA/FNA relevance: exercises `FriendGamer::CreateInternal`, `FriendCollection::CreateInternal`,
  and all four `Guide` friend-related static methods
- Related production code: `FriendCollection.hpp`/`.cpp`, `FriendGamer.hpp`/`.cpp`, `Guide.hpp`/
  `.cpp` (all already audited this session)

## Purpose
Builds five synthetic `FriendGamer` entries, wraps them in a `FriendCollection` via
`CreateInternal`, and lets the user cycle through them (Up/Down) triggering the four `Guide`
friend-related calls (G/R/F/C keys), logging each call to an on-screen scrolling panel.

## Executive Verdict
Correct. `MakeSimpleFont()`'s `defaultCharacter = ' '` is always present in its own
32-126 character range, so this demo does **not** reproduce the HIGH-severity `SpriteFont`
default-character-fallback defect found in this session's `xna-graphics` shard audit; `DrawString`
is only ever called with the 4-argument (no-`effects`) overload, so it also does not reproduce the
`SpriteEffects` combined-flags array-bounds finding from that same audit.

## Checklist Results
- `Initialize()`'s comment (lines 83-86) correctly documents `FriendCollection` as this demo's own
  non-owning registry pattern (`friendStorage_` owns, `friends_`/the `FriendCollection` merely
  view) — consistent with `FriendCollection`'s own already-audited class contract.
- `selectedIndex_` wrap-around (`Update()`, lines 132-139) uses `% friends_.size()` — always safe
  here since `friends_` is built from the fixed 5-element `kFriendDefs` array and never resized
  afterward, so no divide-by-zero risk.
- Smoke-test auto-trigger guard (`smokeFramesLeft_ > 0`, not `>= 0`) correctly avoids
  re-triggering after reaching zero, consistent with the same previously-learned lesson ("Task
  15.14's own discovery") cited in the sibling `demo_gamer_roster_hud` demo audited earlier this
  session.
- No `NetworkSession` is used in this demo, so the `Dispose()`-without-`delete` pattern flagged in
  other Net demos this session does not apply here.

## Detailed Findings
None.

## Cross-File Observations
`TriggerAction()`'s log messages ("no-op, no real OS UI") directly corroborate the parallel
`xna-gamerservices` shard fork's own finding that `Guide::ShowGamerCard`/`ShowFriendRequest`/
`ShowFriends`/`ShowComposeMessage` are confirmed no-ops on this platform, matching FNA's own
reference — independent runtime confirmation of a static-analysis conclusion.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Every `Guide` no-op call is logged with an explicit, accurate "no-op, no real OS UI" annotation
rather than silently doing nothing with no user-visible acknowledgment.

## Final Assessment
No findings.
