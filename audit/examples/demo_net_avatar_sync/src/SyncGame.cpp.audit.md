# Audit: examples/demo_net_avatar_sync/src/SyncGame.cpp

## Metadata
- Source file: `examples/demo_net_avatar_sync/src/SyncGame.cpp` (382 lines)
- Audit status: AUDITED
- Subsystem: `examples-demo_net_avatar_sync` shard
- File type: standalone `Game`-subclass demo implementation (Task 15.21)
- XNA/FNA relevance: exercises `NetworkSession::Create`/`Find`/`Join`, `LocalNetworkGamer::SendData`/
  `ReceiveData`, `AvatarRenderer::EnableRealRenderingEXT`/`SetAppearanceEXT`/`DrawRealEXT`,
  `SpriteBatch::DrawString` (with an explicit `SpriteEffects` argument)
- Related production code: `NetworkSession.hpp`/`.cpp`, `LocalNetworkGamer.hpp`/`.cpp`,
  `AvatarRenderer.hpp`/`.cpp` (all already audited this session)

## Purpose
Implements the two-avatar, two-process sync: loads local/remote `AvatarView`s (each a
`SkinnedModelEXT` + `AvatarRenderer` + clip-name list), broadcasts position/yaw/clip-index every
frame, and renders both avatars in one 3D scene plus an optional F1 help overlay.

## Executive Verdict
Correct, with one LOW finding shared across seven other demos audited this session (leaked, un-
`delete`d `NetworkSession*`).

## Checklist Results
- The `Vector2`/`float`/`int32_t` position/yaw/clip-index broadcast (`PacketWriter::Write(Vector2)`/
  `Write(float)` via the `int32_t` overload/`PacketReader::ReadVector2()`/`ReadSingle()`/
  `ReadInt32()`) are all correctly symmetric pairs — no encounter with the deliberately-asymmetric
  `Color` read/write quirk documented elsewhere in the `xna-net` shard audit.
- The one `DrawString` call with an explicit `SpriteEffects` argument (line 368-369) passes
  `SpriteEffects::None` explicitly — does **not** reproduce the HIGH-severity combined-flags
  array-bounds finding from this session's `xna-graphics` shard audit (that requires a *combined*
  flag value, which nothing in this demo constructs).
- Uses the shared `CNAExamplesEXT::MakeSimpleFontEXT()` helper (from `examples/common/`) rather
  than a local per-demo font builder — this file's own comment explains this replaced an older,
  confirmed-unreadable per-demo "block font" after an independent audit found every character
  rendered identically. That shared helper is out of this file's own scope (belongs to the
  `examples-common` shard) but is worth flagging for that shard's own audit to specifically verify
  its `defaultCharacter` choice doesn't reproduce the `SpriteFont`/`SpriteBatch` default-character
  fallback UB finding, since it's now depended on by potentially many demos.
- `remoteClipIndex_ = newClipIndex % remoteView_.clipNames.size();` (line 283) correctly guards
  against an out-of-range clip index arriving over the wire (a real, if benign, defensive check
  given the value crosses a network boundary).

## Detailed Findings

### LOW — `NetworkSession*` is `Dispose()`d but never `delete`d in `~SyncGame()`
```cpp
SyncGame::~SyncGame()
{
    if (session_ != nullptr)
    {
        session_->Dispose();
        session_ = nullptr;
    }
}
```
Same shape as the finding already documented in seven other Net demos audited this session
(`demo_qos_probe`, `demo_session_lifecycle_events`, `demo_gamer_roster_hud`,
`demo_session_browser`, `demo_simulated_network_conditions`, `demo_net_client_server_arena`,
`demo_gamerservices_dispatcher_watchdog`) — a real, confirmed violation of `NetworkSession`'s
documented "caller must `delete` separately" ownership contract, negligible in practice since the
process exits immediately afterward. This is now the **eighth** instance of this exact pattern
found this session.

## Cross-File Observations
Adds an eighth confirmed instance to the cross-cutting note tracking this `Dispose()`-without-
`delete` pattern across every Net-adjacent demo audited this session. Also flags the shared
`examples/common/SimpleFontEXT.hpp` helper (not yet audited) as worth checking specifically for the
`SpriteFont` default-character-fallback finding, given its now-broad reuse.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The remote clip-index modulo guard against an out-of-range network-supplied value, and the correct
symmetric packet field pairing throughout, both reflect careful, defensive networking code.

## Final Assessment
One LOW finding: `NetworkSession*` leaked (not `delete`d after `Dispose()`) — the eighth instance
of this exact pattern found across Net-adjacent demos this session.
