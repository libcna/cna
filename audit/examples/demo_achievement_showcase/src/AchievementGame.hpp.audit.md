# Audit: examples/demo_achievement_showcase/src/AchievementGame.hpp

## Metadata
- Source file: `examples/demo_achievement_showcase/src/AchievementGame.hpp` (64 lines)
- Audit status: AUDITED
- Subsystem: `examples-demo_achievement_showcase` shard
- File type: standalone `Game`-subclass demo header (Task 15.9)
- XNA/FNA relevance: exercises `Achievement`, `AchievementCollection`, `GamerServicesComponent`,
  `SignedInGamer::AwardAchievement`/`GetAchievements`
- Related production code: `Achievement.hpp`/`.cpp`, `AchievementCollection.hpp`/`.cpp` (already
  audited this session as part of the `xna-gamerservices` shard), `SignedInGamer.hpp`/`.cpp`

## Purpose
Declares a single-process demo showing an achievement tile grid; number keys 1-6 award tiles.

## Executive Verdict
**MEDIUM finding: this header's own "honest scope note" is stale and now factually incorrect,
contradicted by its own paired `.cpp` file's runtime behavior and comments.** The note states
"`SignedInGamer::AwardAchievement()` is a real, confirmed no-op on this platform... and
`GetAchievements()` always returns an empty `AchievementCollection`... (matching FNA's own stub
too)." This was evidently accurate when Task 15.9 originally authored this demo, but
`AchievementGame.cpp`'s own comments explicitly state both methods were later upgraded to real,
disk-backed implementations ("Task 4.5 (plans/plan_net.md Phase 4): real API call - persists to the
local GamerServices store"; "now real disk-backed persistence - survives across process runs"). The
header was never updated to match, so a reader consulting only this header would be told the
opposite of the demo's actual, current, self-demonstrated behavior.

## Checklist Results
See Detailed Findings.

## Detailed Findings

### MEDIUM — Stale "honest scope note" contradicts the paired `.cpp`'s own later-added comments and printed runtime output
The header's doc comment (lines 24-32) asserts `AwardAchievement()` is a no-op and
`GetAchievements()` always returns empty, "matching FNA's own stub." `AchievementGame.cpp`'s
`AwardTile()` (lines 103-108, 118-123) and `Update()`'s smoke-test summary (lines 171-175)
both explicitly describe and print evidence of `GetAchievements()` returning a **non-empty,
real, disk-persisted** result after `AwardAchievement()` is called — directly contradicting the
header's claim. This is the same class of documentation-staleness this project's own audit
history has already identified elsewhere (see `AUDIT_CROSS_CUTTING_FINDINGS.md`'s note on
`bgfx_alphatest_fog_test.cpp`'s header comment going stale after an unrelated later fix landed) —
not a fabricated claim at authoring time, but an unnoticed side effect of a later, real API
upgrade (Task 4.5) not being reflected back into this file's own top-of-file scope note.

**Suggested fix** (report-only; no source changes made per this audit's scope): update the header's
scope note to match the `.cpp`'s own accurate, current description: both methods are real,
disk-backed implementations as of Task 4.5, with the caveat (still accurate) that
`AwardAchievement`'s real API surface never carries Name/Description/GamerScore, so
`GetAchievements()`'s results only ever reflect key+earned+earnedDateTime — this demo's own
`tiles_` grid remains the actual source of truth for display content, not `GetAchievements()`.

## Cross-File Observations
See `AchievementGame.cpp.audit.md` for the corroborating evidence (comments + printed runtime
output) that directly contradicts this header's stale claim.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The underlying design decision this header otherwise correctly documents — `tiles_` as the demo's
own display source of truth, since `AwardAchievement`'s real API never carries Name/Description/
GamerScore — remains sound and accurately explained.

## Final Assessment
One MEDIUM finding: a stale scope-note claim in this header, contradicted by the paired `.cpp`'s
own later-added, accurate comments and printed output.
