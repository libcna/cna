# Audit: examples/demo_gamer_roster_hud/src/RosterGame.cpp

## Metadata
- Source file: `examples/demo_gamer_roster_hud/src/RosterGame.cpp` (261 lines)
- Audit status: AUDITED
- Subsystem: `examples-demo_gamer_roster_hud` shard
- File type: standalone `Game`-subclass demo implementation (Task 15.6)
- XNA/FNA relevance: exercises `NetworkSession`'s roster event surface, real two-process ENet
  networking, host migration (`setAllowHostMigrationProperty`), and `SpriteBatch::DrawString`
- Related production code: `NetworkSession.hpp`/`.cpp`, `SpriteFont.hpp`/`.cpp` (already audited
  this session)

## Purpose
Implements the roster HUD: session creation/join, all four roster event handlers, a synthetic
1x1-glyph `SpriteFont` builder, and per-frame roster rendering via `SpriteBatch::DrawString`.

## Executive Verdict
Correct overall, with one LOW finding: the destructor `Dispose()`s the owned `NetworkSession*` but
never `delete`s it, diverging from `NetworkSession`'s own documented ownership contract (caller
must `delete` the pointer separately once done with it, since `Dispose()` deliberately does not
`delete this`). Practically harmless here (the process exits immediately afterward, so the OS
reclaims the leaked allocation), but worth flagging as a pattern shared with two other already-
audited demos this session.

## Checklist Results
- `MakeSimpleFont()` (lines 28-47) builds a `SpriteFont` with `defaultCharacter = ' '` (space,
  ASCII 32), which IS included in the constructed `characters` range (32-126) — this demo does
  **not** reproduce the HIGH-severity `SpriteFont`/`SpriteBatch::DrawString` default-character
  fallback defect documented in this session's `xna-graphics` shard audit, since its default
  character is always resolvable.
- `DrawString` is only ever called with the 4-argument overload (position + color, no `effects`
  parameter) — this demo does **not** exercise the other HIGH finding from that same audit (the
  `SpriteEffects` combined-flags array-bounds issue), since `effects` always defaults to `None`
  through that call path.
- `Update()`'s smoke-test auto-toggle guard (`smokeFramesLeft_ > 0`, not `>= 0`) correctly avoids
  re-triggering every frame after reaching zero, with an inline comment explicitly citing "Task
  15.14's own discovery of this exact bug class" (`Exit()` not halting `Update()` immediately) —
  a real, previously-learned lesson correctly applied here.
- `~RosterGame()` (lines 61-68): calls `session_->Dispose()` but never `delete session_` — see
  Detailed Findings.

## Detailed Findings

### LOW — `NetworkSession*` is `Dispose()`d but never `delete`d, diverging from its own documented ownership contract
```cpp
RosterGame::~RosterGame()
{
    if (session_ != nullptr)
    {
        session_->Dispose();
        session_ = nullptr;
    }
}
```
`NetworkSession.hpp`'s own class-level doc comment (audited this session,
`include/Microsoft/Xna/Framework/Net/NetworkSession.hpp.audit.md`) explicitly documents: "The
caller must `delete` the pointer separately once truly done with it, the same way any other
`new`-returned, non-reference-counted C++ object would be freed." This destructor calls `Dispose()`
(correctly releasing the session's ENet transport and owned gamers) but never calls
`delete session_` — a real, confirmed violation of that documented contract, leaking the
`NetworkSession` heap allocation itself (though not its owned resources, which `Dispose()` does
release).

**Practical impact**: negligible in this specific demo — `main()` (`Main.cpp`) exits the process
immediately after `delete game;` runs (which triggers this destructor), so the OS reclaims the
leaked allocation at process exit regardless. This would become a real, accumulating leak only if
`RosterGame` were ever instantiated/destroyed repeatedly within a single longer-running process.

## Cross-File Observations
The identical "Dispose() called, `delete` never called" pattern is present in two other demos
already audited this session: `examples/demo_qos_probe/src/Main.cpp` and
`examples/demo_session_lifecycle_events/src/Main.cpp` (both call `session->Dispose(); return 0;`
with no corresponding `delete session;`). This is a consistent, repeated pattern across every Net
demo audited so far in this session, not an isolated oversight in this one file — worth a
dedicated cross-cutting note (added to `AUDIT_CROSS_CUTTING_FINDINGS.md`) since a reader following
any of these demos as an example of "correct `NetworkSession` usage" would learn an incomplete
lifecycle pattern.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The `IsReady`-not-synced limitation is called out again, precisely, at the exact `Update()` call
site that toggles it (line 186-189) — consistent, repeated honesty about this real scope boundary
rather than a single buried disclosure.

## Final Assessment
One LOW finding: `NetworkSession*` leaked (not `delete`d after `Dispose()`), part of a
consistently-repeated pattern across multiple demos in this session — negligible practical impact
for a short-lived demo process, but worth a shared cross-cutting note.
