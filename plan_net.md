# Plan: CNA XNA 4.0 Net / GamerServices / Avatar — 2026-07-07 Re-Audit and Hardening

This is a fresh, second-pass hardening plan. The prior plan (`plan_net_20260707.md`, formerly
`plan_net.md`) closed out 132/132 tasks and is archived, not deleted — some findings below
reference it directly where this pass revisits or extends a decision made there.

This pass is **not** a first-implementation plan. `Net`, `GamerServices`, and the Avatar
real-rendering extension already have large, working implementations, extensive tests, and 24
demo applications. This plan is about hardening what exists, closing genuinely-open gaps, and
implementing a handful of features the user explicitly decided to build out further (real host
migration, real simulated network conditions, a real Guide message-box overlay, real keyboard
capture, disk-persisted achievements/leaderboards, and an avatar art-quality pass).

## Rules

- Work one task at a time.
- Keep XNA-compatible API behavior (namespace `Microsoft::Xna`) separate from CNA/NOXNA
  extensions (`NOXNA` macro / `*EXT` suffix), per this repo's own `CLAUDE.md`.
- Add or extend a test for every behavior change — a test that would fail without the fix.
- One task = one commit (user-confirmed).
- Do not use proprietary Xbox Avatar assets, not even as private reference/measurement input
  (user-confirmed, no exceptions).
- Document assumptions when a decision is needed and the user is unavailable; use the safest
  conservative default and keep moving.
- Testing scope for this pass: **EASYGL graphics backend, `cmake-build-debug` only**
  (user-confirmed) — other backends/build dirs are out of scope unless something here turns out
  to be backend-specific.

## User Decisions

Full questionnaire was asked interactively, one question at a time, in the conversation that
produced this plan. Recorded answers:

| # | Topic | Decision |
|---|-------|----------|
| 1a | XNA reference platform | **Xbox 360** — real XNA multiplayer/avatar behavior (not Windows' no-op stubs) is the reference for what "correct" means, since CNA already has its own real avatar/networking implementations. |
| 1b | Strict XNA/NOXNA convention | Keep as-is: `Microsoft::Xna` stays behaviorally faithful to the (now Xbox-360-reference) XNA API; CNA-only improvements stay behind `NOXNA`/`*EXT`. |
| 2 (tool) | Networking scope beyond SystemLink | **SystemLink-focused only.** PlayerMatch/Ranked/Invite stay documented, tested-as-unsupported stubs — no synthetic offline approximation work this pass. |
| 2a | SimulatedLatency/SimulatedPacketLoss | **Implement a real effect** on real ENet traffic (delay queue / probabilistic drop), deterministic under test. |
| 2b | Transport | ENet remains the real transport; no change. |
| 3 (tool) | Guide.BeginShowMessageBox | **Build a real CNA overlay implementation** (SpriteBatch-based), not just a no-op/NotSupportedException stub. |
| 3a | Guide.BeginShowKeyboardInput | **Real captured text** through CNA's existing `TextInputEXT` input layer, not a stub value. |
| 3b | Achievements/leaderboards | **Persist to disk** (not in-memory-only fake). |
| 4 (tool) | Avatar visual target | **Toy-like Xbox-Avatar-inspired** stylization, fully original CNA geometry/textures. |
| 4a | Proprietary Xbox Avatar assets | **Never use them, not even for private reference/measurement.** |
| 4b | Body types/hair/clothes/colors scope | Improve what `demo_avatar`/the asset pipeline already generates so it looks intentional, not "monsters." Body/head (and other) geometry should be generated as glTF via the sibling `../mesh-craft` tool going forward. |
| 4c | Phase 7 acceptance criteria | Accepted as originally stated: front/side/back screenshots, male + female, animation gallery, no mesh explosions, no distorted limbs. |
| 5a | F1 help text | Use the exact default text block (below), verbatim. |
| 5b | F1 toggle behavior | Press toggles show/hide. |
| 5c | F1 overlay scope | **All avatar-related demos**, not just `demo_avatar` — needs a small shared demo-only helper. |
| 5d | F1 overlay styling | Translucent white rectangle, black text, as originally described. |
| 6a | Plan rename | Yes — done (`plan_net.md` → `plan_net_20260707.md`, no prior file existed at that name). |
| 6b | If the target name already existed | N/A this time (it didn't) — if it ever does in a future pass, stop and ask rather than auto-backup. |
| 6c | Commits | One commit per finished task. |
| 6d | Build/test scope | EASYGL backend, `cmake-build-debug` only. |
| host migration (tool) | AllowHostMigration | **Implement simple real host migration**, superseding the prior pass's "stored but unsupported" conclusion (`plan_net_20260707.md` Task 2.6). |

### Still-open micro-decisions (no explicit user answer; conservative default applied)

- **`Achievement::GetPicture()`** currently always throws `System::NotImplementedException`
  (`src/Microsoft/Xna/Framework/GamerServices/Achievement.cpp:51`). Real Xbox 360 achievement
  artwork was streamed from Xbox LIVE and is permanently unavailable, same reasoning as the
  Avatar real-rendering doc's own justification for why faithful XNA Avatar rendering is a no-op.
  **Default: keep throwing, matching genuine platform unavailability — not a bug.** Documented
  explicitly in Phase 4 rather than silently left alone.
- **Achievement/leaderboard disk persistence format/location**: no format was specified.
  **Default: a small local JSON file under the same user-data-directory convention this repo
  already uses elsewhere** (see Phase 4 for the concrete path once located).
- **Host migration's new-host selection rule**: no rule was specified. **Default: deterministic
  lowest-wire-id remaining gamer becomes the new host** — simplest deterministic rule, matches
  the "simple" qualifier in the user's decision.

## F1 Help Overlay — Default Text (verbatim, per decision 5a)

```text
CNA Avatar Demo Help

F1: Show/hide this help
Esc: Quit
Space: Next animation
Left/Right: Rotate camera

Command line:
--gender male|female
--wardrobe-hair Cap|Ponytail

This demo uses CNA real avatar rendering extensions.
XNA-compatible AvatarRenderer.Draw remains a no-op on Windows-like platforms.
```

Per-demo controls differ (not every demo has `--gender`/`--wardrobe-hair` or the same camera
scheme) — Phase 2 covers adapting this text per-demo where the literal command-line/control
lines don't apply, while keeping the header/F1/Esc lines consistent across all of them.

---

## Phase 0 — Safety, archive, inventory (this phase)

- [x] `git status --short` at start: clean tree, branch `feature/net`.
- [x] Renamed `plan_net.md` → `plan_net_20260707.md` (target name did not already exist, no
  backup needed).
- [x] Created this fresh `plan_net.md`.
- [x] Inventoried all public headers/src/tests in `Net`, `GamerServices`, `CNA::Internal::Net`
  (full file lists gathered; omitted here as pure listings — see `git ls-files` for the
  authoritative live list, this plan cites specific files/lines per task instead).
- [x] Grepped `TODO|FIXME|NotImplementedException|NotSupportedException|no-op|placeholder|stub`
  across all of the above — results folded into the phase tasks below with file:line citations.
- [x] Investigated the sibling `mesh-craft` repo (`/rv/data/development/github.com/openeggbert/mesh-craft`):
  a C++23 constructive-scene editor (`.mc3.xml` format — primitives, CSG via Manifold, groups,
  materials, animation *channels* but **no skeletal/bone-weight concept**) with a CLI exporter
  `mc3togltf` (`.mc3.xml` → `.gltf`/`.glb`) and a scene-generation system prompt (`gen.md`)
  suited to scripted/procedural authoring. It has no rigging concept, so it can only replace the
  **shape** stage of avatar asset generation (bodies, heads, and per user's answer potentially
  other props), not skeleton binding/skinning/animation — those stay in the existing
  `tools/avatar_builder/` Blender pipeline (`generate_skeleton.py`, `generate_morphs.py`,
  `generate_animations.py`). See Phase 7 for the concrete integration plan.

---

## Phase 1 — API surface parity audit (light-touch this pass)

The prior pass (`plan_net_20260707.md`) already did a full line-by-line FNA-ABI audit across all
132 tasks. This phase is a targeted re-check of anything the Xbox-360-reference decision (1a)
might change, plus the two concrete gaps found during Phase 0's grep sweep that are genuinely
new observations, not already-covered ground.

- [ ] **Task 1.1** — Re-read `docs/xna-4-api-coverage.md` end to end and fix the now-stale
  sections found during Phase 0's grep (exact lines):
  - `docs/xna-4-api-coverage.md:43` — `GamerServices` row says "❌ (not in FNA) ⚠️ Stub – Guide
    only" — false; GamerServices now has Achievements, Avatar (full real-rendering extension),
    Friends, Presence, Leaderboards, Privileges, Profile, SignedInGamer, etc.
  - `docs/xna-4-api-coverage.md:70` — "FNA itself does not implement GamerServices. CNA has only
    `Guide` (stub)." — same staleness.
  - `docs/xna-4-api-coverage.md:267` — "All classes listed in §3 under GamerServices fall here.
    FNA has no implementation for any of them." — false, most are implemented.
  - `docs/xna-4-api-coverage.md:271` — describes Avatar rendering as "not part of PC XNA 4.0 and
    never implemented by FNA" with no mention of CNA's own real-rendering extension
    (`docs/avatar-real-rendering-ext.md`) — needs a cross-reference added.
  - `docs/xna-4-api-coverage.md:285` — "`Microsoft.Xna.Framework.Net` ... **Intentionally
    excluded from CNA.**" — false; Net is extensively implemented (24 headers, ENet-backed real
    transport, 132 tasks of hardening in the prior pass).
  - `docs/xna-4-api-coverage.md:411` — coverage table row `Framework.Net | 0% | ... intentionally
    excluded` — needs a real coverage percentage and a "see Net/GamerServices/Avatar sections"
    pointer instead.
  - `docs/xna-4-api-coverage.md:451-453` — `GamerServicesComponent`/`GamerServicesNotAvailableException`
    still marked "stub" — verify current state and correct if outdated.
  - `docs/xna-4-api-coverage.md:473` — "Xbox Live Networking ... — Xbox Live exclusive" under
    what reads as a won't-implement list — needs correcting or removing.
  This task folds into Phase 8 (docs cleanup) execution-wise but is scoped here first since it's
  pure investigation, not a code change.

- [ ] **Task 1.2** — `NetworkMachine::RemoveFromSession` (`src/Microsoft/Xna/Framework/Net/NetworkMachine.cpp:25`)
  always throws `System::NotImplementedException`, matching FNA's own stub
  (`NetworkMachine.hpp:26`). Confirm this is still correct under the Xbox-360-reference decision
  (i.e. real Xbox 360 XNA's `NetworkMachine.RemoveFromSession` was *also* never implemented, not
  just FNA's port of it — check FNA's own source comments/XNA docs for evidence one way or the
  other). If confirmed genuinely unimplemented on real Xbox 360 too, leave as-is but add an
  explicit test locking in the throw (grep found no dedicated test for this method — verify and
  add one if missing). If evidence suggests real Xbox 360 behavior differs, flag for a follow-up
  task rather than guessing an implementation.

- [ ] **Task 1.3** — `NetworkSession::BeginCreate(NetworkSessionType, int maxLocalGamers, int
  maxGamers, AsyncCallback, object)` (the simplest/original 3-arg-plus-callback overload,
  `src/Microsoft/Xna/Framework/Net/NetworkSession.cpp:601-624`) silently ignores its own
  `maxGamers` parameter and the actually-used private constructor hardcodes `69` instead
  (`NetworkSession.cpp:697-698`, both comments explicitly say this preserves an FNA
  upstream quirk/FIXME). Re-verify against real XNA 4.0 documentation (not just FNA's comment)
  whether real XNA also silently drops this parameter on this exact overload. If yes: leave as
  faithful, add a regression test locking in the `69`-gamer cap so a future edit can't
  "accidentally fix" it without noticing. If real XNA actually honors the parameter and this is
  purely an FNA-introduced bug: this becomes a real fix — forward `maxGamers` instead of `69`,
  with a test proving the caller's value is honored, and document the deviation from FNA.

---

## Phase 2 — NetworkSession lifecycle safety (confirmed real bug)

- [ ] **Task 2.1** — `NetworkSession::~NetworkSession()` (`src/Microsoft/Xna/Framework/Net/NetworkSession.cpp:200-203`)
  only decrements `instanceCount_`. Unlike `Dispose()` (`NetworkSession.cpp:266-282`), the
  destructor does **not**:
  - tear down ENet transport state (`CNA::Internal::Net::ENetBackend::TeardownSession`),
  - clear `ownedGamers_`,
  - reset the `activeSession_` singleton pointer (`NetworkSession.hpp:883`) to `nullptr`.

  **Confirmed real bug, not theoretical:** if a caller `delete`s a `NetworkSession*` directly
  (skipping `Dispose()`) — a real risk since `Create`/`Find`/`Join` all return a caller-owned raw
  pointer per this class's own doc comment (`NetworkSession.hpp:49`) — `activeSession_` is left
  dangling, pointing at now-freed memory. Every subsequent `BeginCreate`/`BeginFind`/`BeginJoin`
  call checks `activeAction_ != nullptr || activeSession_ != nullptr` and throws
  (`NetworkSession.cpp:613,643,669,782,806,868,969,988`) — meaning **no new session can ever be
  created again for the rest of the process's life** after one mismanaged delete, and any
  transport resources (ENet host socket, discovery advertisement) leak permanently.

  **Fix:** make the destructor call `Dispose()` if not already disposed (standard C++
  IDisposable-pattern safety net — `if (!isDisposed_) Dispose();`), matching how other
  `System::IDisposable` types in this codebase are expected to behave per `CLAUDE.md`'s
  IDisposable section.

  **Add tests**: (a) delete a session without calling `Dispose()` first, then confirm a *new*
  session can still be created afterward (`activeSession_` correctly reset); (b) confirm
  transport teardown actually happened (e.g. a discovery advertisement is no longer found by
  `FindSessions` after the delete); (c) repeated `Dispose()` calls stay safe (idempotent, already
  guarded by `isDisposed_` — verify this still holds once the destructor also calls it); (d)
  creating a new session immediately after a properly-`Dispose()`d one still works (regression
  guard for the existing, already-correct `Dispose()`-then-recreate path).

  **Verify the bug is real**: revert-verify via the standard `git stash` cycle — write the new
  "delete without Dispose, then create again" test first, confirm it fails (either hangs/throws
  because `activeSession_` is stale, or a sanitizer catches the dangling-pointer read) against
  the current destructor, then apply the fix and confirm it passes.

- [ ] **Task 2.2** — While fixing Task 2.1, audit whether `ENetBackend::TeardownSession` and
  `ownedGamers_.clear()` are safe to call twice (once from a hypothetical explicit `Dispose()`
  call, once from the destructor's new safety-net call) — confirm idempotency or guard
  appropriately. Add a test for "explicit `Dispose()` then the object goes out of scope /
  destructs" to lock in no double-teardown crash.

---

## Phase 3 — Guide: real message-box overlay + real keyboard capture

### Task 3.1 — Guide.BeginShowMessageBox: real CNA overlay

Current state: both overloads (`src/Microsoft/Xna/Framework/GamerServices/Guide.cpp:116-140`)
unconditionally `throw System::NotSupportedException()`. `EndShowMessageBox` (`Guide.cpp:142`)
also always throws. No existing SpriteBatch/SpriteFont-based UI helper was found anywhere in the
codebase to reuse — this needs a small new one.

- [ ] Design a minimal `NOXNA` message-box overlay: a static-ish rendering + input helper that
  the game's own draw loop calls (matching how the F1 help overlays in Phase 2's plan area work —
  no automatic hook into `Game.Draw()`, since `Guide` has no access to the game's own
  `SpriteBatch`/`GraphicsDevice` today). Reuse the exact same visual language as the F1 overlay
  (translucent white rectangle, black text) for consistency, per decision 5d's spirit.
- [ ] `BeginShowMessageBox` stores the requested title/text/buttons/icon and returns a real
  `IAsyncResult` (the existing `GuideAction`-style pattern already used by
  `BeginShowKeyboardInput`, `Guide.cpp:14-42`), but — unlike today's other Guide `Begin*`
  fake-syncs — must **not** synchronously complete, since a message box needs a real user click.
  Completes when the game explicitly renders the overlay and the user selects a button (needs a
  small public "pump/render" entry point on the overlay helper that a game's `Draw()` calls).
- [ ] `EndShowMessageBox` returns the selected button index (`std::optional<int>`, matching the
  existing signature) instead of always throwing; throws `System::InvalidOperationException` (not
  `NotSupportedException`) only if called before the async op completes, matching real
  Begin/End-pair semantics used elsewhere in this codebase.
- [ ] Add tests: both `BeginShowMessageBox` overloads no longer throw and return a valid
  `IAsyncResult`; `EndShowMessageBox` throws if called too early; a full synthetic
  "render frame → simulate click → End" cycle returns the right button index; `focusButton`
  parameter is honored as the initial default selection; empty `buttons` vector is rejected
  (matches FNA's own validation if FNA validates this — check first).
- [ ] Wire the new overlay into at least `examples/demo_guide_overlay_console` (the demo whose
  name literally suggests this is its purpose — check its current content first, since it may
  already assume the old no-op/NotSupportedException behavior and need updating, not just
  extending).

### Task 3.2 — Guide.BeginShowKeyboardInput: real captured text

Current state (`Guide.cpp:81-113`): both overloads call `TextInputEXT::StartTextInput()`, then
**immediately** set `isCompleted_ = true` on the returned `GuideAction` — a fully synchronous
fake-async completion, matching `AvatarDescription::BeginGetFromGamer`'s documented pattern
(`AvatarDescription.hpp:97`). `EndShowKeyboardInput` (`Guide.cpp:107-110`) calls
`TextInputEXT::StopTextInput()` and unconditionally returns `""` — the callback is invoked
correctly (per `GuideAction`'s normal machinery) but the captured text itself is always empty,
regardless of what the user actually types.

`TextInputEXT` (`include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`) already has everything
needed: `TextInput` is a `std::function<void(charcs)>` fired per UTF-16 code unit as the user
types (already SDL-backed, already used elsewhere).

**Architecture problem to solve**: real typed text can't be known at `Begin*` call time — it
accumulates over an unknown number of frames until the user finishes typing. The current
"complete synchronously at Begin" model is fundamentally incompatible with real capture. This
needs a real state machine:

- [ ] Add an internal (non-`Begin*`-signature-visible) accumulation buffer that subscribes to
  `TextInputEXT::TextInput` for the duration between `BeginShowKeyboardInput` and completion,
  appending each code unit (handling the documented UTF-16 surrogate-pair case, i.e. don't split
  surrogate pairs when converting to the returned `std::string`).
- [ ] Define a completion trigger — real XNA/Xbox 360 keyboard input completes when the user
  presses Enter/confirms on the virtual keyboard. Given this is a native SDL text-input session,
  the most faithful analog is completing on an Enter keypress. Needs a hook into CNA's existing
  keyboard-state or SDL event pump — investigate whether `Keyboard`/`Keys::Enter` polling from
  inside `TextInputEXT` machinery (or a new small internal poll called from `Game.Update`/`Draw`)
  is the right layer; do not invent a second, parallel input system.
  `defaultText` (already a parameter) pre-seeds the buffer so a user who presses Enter immediately
  gets the default, matching real XNA semantics.
- [ ] `isCompleted_` only flips to `true` once Enter is detected (or, for the `usePasswordMode`
  overload, same trigger — password mode only affects on-screen masking, not completion timing,
  matching real XNA).
- [ ] `EndShowKeyboardInput` returns the actual accumulated string instead of `""`; still calls
  `StopTextInput()`.
- [ ] Add tests: simulate `TextInputEXT::TextInput` firing several code units then an Enter
  signal, confirm `EndShowKeyboardInput` returns exactly what was typed; confirm `defaultText`
  pre-seeds correctly; confirm a surrogate pair (an emoji) round-trips correctly, matching the
  existing UTF-16 documentation on `TextInputEXT`; confirm `IsCompleted` stays false until Enter;
  confirm calling `End*` before completion throws (matching the Begin/End-pair convention used
  elsewhere — check what the existing pattern throws, e.g. `InvalidOperationException`).
- [ ] Update any demo/doc that assumed the old always-empty-string behavior.

---

## Phase 4 — Achievements/leaderboards: disk persistence

Current state: `AchievementCollection`/`Achievement` (`include/Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp`,
`Achievement.hpp`) — confirm exactly how achievement state is populated today (constructor-only,
no evident load/save path was found in the Phase 0 grep). `LeaderboardReader`/`LeaderboardWriter`
(`src/Microsoft/Xna/Framework/GamerServices/LeaderboardReader.cpp`,
`LeaderboardWriter.cpp`) currently throw `System::NotSupportedException` on every real read/write
path (`LeaderboardReader.cpp:86,91,106,111,155,165,176,181`; `LeaderboardWriter.cpp:9`).

- [ ] **Task 4.1** — Re-confirm current achievement population/storage mechanism with a targeted
  read of `AchievementCollection`'s constructor(s) and any demo that populates one (start with
  `examples/demo_achievement_showcase`). Determine whether "earned" state is currently
  per-process/in-memory-only (expected) or already touches disk anywhere (grep in Phase 0 found
  none, but confirm before designing the fix).
- [ ] **Task 4.2** — Design a small local persistence format/location for earned-achievement
  state and leaderboard entries. Default (no user spec given): plain local JSON file(s), one per
  gamertag, under whatever local-user-data-directory convention this codebase already uses
  elsewhere (search for an existing "user data dir"/"save game" path helper before inventing a
  new one — reuse it if found). Document the chosen path/format explicitly in this plan once
  decided, since no existing convention was confirmed during Phase 0.
- [ ] **Task 4.3** — Implement real (not `NotSupportedException`) `LeaderboardWriter::Write`-path
  behavior: persist a written leaderboard entry to the local store from Task 4.2.
- [ ] **Task 4.4** — Implement real `LeaderboardReader` read paths (the `BeginRead`/`EndRead`
  pair and whatever synchronous accessors exist) sourcing from the same local store, instead of
  every path throwing `NotSupportedException`. Preserve `NotSupportedException` only for
  operations that are genuinely online-only in real XNA (e.g. friend-leaderboard filtering with no
  local friends data) — do not blanket-implement everything if some paths have no honest local
  answer; document any path that stays intentionally unsupported with a one-line reason.
  Investigate `include/Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp:239-241`'s own
  doc comment (references `BeginRead`/`EndRead` being an intentional stub today) before deciding
  scope.
- [ ] **Task 4.5** — Wire achievement earning to persist to the same local store, and load
  previously-earned state back on `SignedInGamer`/`AchievementCollection` construction for a
  returning gamertag.
- [ ] **Task 4.6** — `Achievement::GetPicture()` (`Achievement.cpp:51`): confirmed still throwing
  `NotImplementedException` in Phase 0. Per the "still-open micro-decisions" note above, default
  to **leaving this throwing** (genuine platform unavailability, not a local-persistence
  question) — add a test locking in the throw plus a doc-comment explanation if one isn't already
  present, rather than building a placeholder-texture system nobody asked for.
- [ ] **Task 4.7** — Add tests: write an entry, destroy the in-process objects, re-construct, read
  it back — proves real disk persistence, not just in-memory state that happens to survive within
  one test. Add tests for corrupt/missing store file handling (should not crash — start empty).
- [ ] **Task 4.8** — Update `examples/demo_leaderboard_viewer` and `demo_achievement_showcase` if
  their current behavior assumed the old unsupported/in-memory-only state.

---

## Phase 5 — Real host migration

Supersedes `plan_net_20260707.md` Task 2.6's "stored but unsupported" conclusion — the user has
now decided to implement this for real.

Current state confirmed in Phase 0:
- `AllowHostMigration` is a plain stored bool (`NetworkSession.hpp:158-170`, `.cpp:219-220`),
  read nowhere else.
- Client-side host disconnect handling (`CNA::Internal::Net::HandleDisconnect`,
  `src/CNA/Internal/Net/ENetBackend.cpp:359-374`) unconditionally ends the session
  (`session->RemoveGamer(locals[0], NetworkSessionEndReason::HostEndedSession)`) the instant the
  host peer disconnects — there is currently no branch point where migration could occur instead.
- `NetPacketCodec.hpp:31` already reserves wire opcode `0x05` for a future
  `HostChangeBroadcast`, explicitly noting "not implemented" — this is the natural wire message to
  build out.
- Host-side peer bookkeeping (`SessionState::WireIdToGamer`, `WireIdToPeer`, `PeerWireIds`,
  `HostPeer` — `ENetBackend.cpp:41-58`) exists per-session but is host-centric; a promoted client
  has none of this and would need to build it from scratch (it only knows the wire ids/gamers
  that were broadcast to it).

- [ ] **Task 5.1** — Design the migration protocol (write this up in this plan before coding):
  on host disconnect, if `AllowHostMigration` is true and at least one other gamer remains, the
  surviving gamers deterministically agree on a new host **without any additional round-trip**
  (default rule, per the "still-open micro-decisions" note: lowest remaining wire id becomes
  host — every peer already has the same roster via existing `ServerWelcomeBroadcast`/
  `GamerJoinBroadcast` messages, so this needs no negotiation). The new host must:
  - start listening as a real ENet host (reuse `ENetBackend`'s existing host-start machinery),
  - every other surviving peer must learn the new host's address/port and reconnect,
  - the `0x05 HostChangeBroadcast` message (already reserved) carries the new host's identity/
    endpoint to whichever peers can be reached before the old host's connections all drop.
  Flag the concrete open engineering question this raises: in a star topology, non-host peers
  currently only have a connection *to the host*, not to each other — a promoted host has no
  pre-existing connections to migrate, so surviving clients need a genuine reconnect (a `Connect`
  call, same code path `JoinInvited`/regular join already uses), not a live migration of existing
  sockets. Confirm this is acceptable "simple" scope (matches the user's own "simple" qualifier)
  before building anything fancier.
- [ ] **Task 5.2** — Implement new-host promotion: the promoted client calls the same
  `ENetBackend` host-start path a normal `Create` would use, transitions its local gamer's
  `IsHost` via the existing `SetIsHost` (`NetworkGamer.hpp:97`), and re-registers session
  discovery advertisement if applicable (SystemLink discovery — check `ENetDiscoveryService`'s
  `RegisterHost`/`UnregisterHost` for how a mid-session host-address change should be surfaced).
- [ ] **Task 5.3** — Implement surviving-peer reconnect-to-new-host path, raising
  `HostChangedEventArgs` (already exists — `include/Microsoft/Xna/Framework/Net/HostChangedEventArgs.hpp`,
  confirm it's currently unused/dead and wire it up here) instead of ending the session, when
  `AllowHostMigration` is true.
- [ ] **Task 5.4** — Preserve the existing behavior exactly when `AllowHostMigration` is false
  (the current immediate-session-end path) — this must stay a pure branch, not a behavior change
  for the default-off case. Add a test locking in the unchanged false-case behavior alongside the
  new true-case tests.
- [ ] **Task 5.5** — Add real two/three-peer integration tests (matching this codebase's existing
  style of real ENet host+client(s) over loopback, not mocks): host disconnects mid-session with
  `AllowHostMigration=true` and 2 remaining clients, confirm a real new host emerges, the
  remaining client reconnects, and both can still exchange `AppData`. Add a 3-peer variant to
  exercise the deterministic-lowest-wire-id tie-break rule concretely (not just 1-remaining-peer,
  where there's no real choice to verify).
- [ ] **Task 5.6** — Verify the bug/gap is real and the fix works via the standard revert-verify
  cycle. Update `NetworkSession.hpp:158-170`'s doc comment (currently says migration is "not
  implemented") to describe the real behavior.
- [ ] **Task 5.7** — Update `examples/demo_session_lifecycle_events` and any other demo whose
  behavior or comments assumed no migration.

---

## Phase 6 — Real SimulatedLatency / SimulatedPacketLoss

Supersedes `plan_net_20260707.md` Task 4.3's "document as non-functional placeholder, matching
FNA" conclusion for *this specific pair of properties* — the user decided to implement a real
effect this pass. (`SimulatedLatency`/`SimulatedPacketLoss` getters/setters already exist,
`NetworkSession.hpp`/`.cpp:258-262`, currently read nowhere else — confirmed in Phase 0.)

- [ ] **Task 6.1** — Identify the exact hook points: outbound send (`ENetBackend`'s `SendTo`,
  `ENetBackend.cpp:170`) and/or inbound receive processing (`PumpSession`'s
  `ENET_EVENT_TYPE_RECEIVE` branch, `ENetBackend.cpp:579`). Decide whether simulated latency/loss
  applies per-send (delay when a packet is handed to ENet) or as a receive-side hold-and-release
  queue (delay when a packet would otherwise be delivered to game code) — the latter is closer to
  real network jitter and doesn't require faking ENet's own reliability/ack timing, so prefer it
  unless investigation shows a strong reason not to.
- [ ] **Task 6.2** — Implement a per-session delayed-delivery queue keyed off `simulatedLatency_`:
  received packets are timestamped and only handed to game code once
  `now >= receiveTime + simulatedLatency_`. Implement probabilistic drop keyed off
  `simulatedPacketLoss_` (a `[0,1]` drop probability per packet, matching the property's
  documented range — confirm exact documented range in `NetworkSession.hpp` before implementing).
- [ ] **Task 6.3** — **Determinism for tests is a hard requirement** (explicit user instruction:
  "implementovat reálný efekt" with no flakiness). Use an injectable/seedable RNG for the drop
  decision (do not call an unseeded global RNG) and a controllable time source (reuse whatever
  time abstraction `GameTime`/existing tests already use — do not add `std::chrono::steady_clock`
  calls directly into test-exercised code paths if an injectable clock exists; if none exists,
  add a minimal one scoped to this feature only, per `CLAUDE.md`'s minimal-stub rule).
- [ ] **Task 6.4** — Add tests: zero latency/loss behaves exactly as before (regression guard);
  a fixed non-zero latency measurably delays delivery by at least that amount, deterministically
  (using the injectable clock, not a real sleep + flaky timing assertion); a drop probability of
  1.0 deterministically drops every packet (seeded RNG); a drop probability of 0.0 drops none.
- [ ] **Task 6.5** — Verify real effect end-to-end over a real loopback ENet connection (not just
  unit-level queue logic) — send N packets with simulated loss=1.0, confirm zero arrive; send with
  a fixed latency, confirm none arrive before the expected delay.
- [ ] **Task 6.6** — Update `examples/demo_simulated_network_conditions` to actually demonstrate
  the now-real effect (check its current content first — it may currently just set the properties
  and claim they work, or may already honestly disclaim they're placeholders; either way it needs
  to now show real, observable behavior).
- [ ] **Task 6.7** — Revert-verify the fix, run the full suite, update this plan with results.

---

## Phase 7 — Avatar asset quality: stop the "monster" avatars

Goal (per decisions 4/4a/4b/4c): toy-like Xbox-Avatar-inspired look, fully original CNA assets,
generated via `../mesh-craft` for body/head (and other feasible) geometry, feeding into the
existing `tools/avatar_builder/` Blender pipeline for skeleton/skinning/animation (mesh-craft has
no rigging concept — confirmed in Phase 0).

- [ ] **Task 7.1** — Capture baseline screenshots: current `demo_avatar` male and female avatars,
  front/side/back, plus a few animation-gallery poses from `demo_avatar_animation_gallery` that
  best reveal today's deformation problems. Store under a scratch/docs location (not committed
  binary bloat unless the repo already commits demo screenshots elsewhere — check convention
  first).
- [ ] **Task 7.2** — Write `docs/avatar-art-direction.md`: restate the user-approved acceptance
  criteria from decision 4c verbatim, plus concrete proportion targets (head/neck/shoulder/torso/
  limb/hip/foot ratios) derived from *original* reference thinking, not any proprietary asset.
- [ ] **Task 7.3** — Audit `tools/avatar_builder/generate_body.py`/`generate_skeleton.py`'s
  current proportion logic against the new art-direction doc; identify concretely where today's
  generator produces the "monster" look (disproportionate head/limbs, bad joint placement, etc.)
  — read the actual generator code and baseline screenshots together rather than guessing.
- [ ] **Task 7.4** — Prototype body/head geometry authored via `mesh-craft`'s `.mc3.xml` format
  (primitives + CSG, per `gen.md`'s documented capabilities) as a replacement input to
  `generate_body.py`'s current procedural-Blender body construction. Export via `mc3togltf` to
  `.glb`, then feed into the existing skeleton/skinning stages. This is a real pipeline
  integration change — document the new data flow (mesh-craft `.mc3.xml` → `mc3togltf` → `.glb`
  base mesh → Blender import → `generate_skeleton.py`/rigging/`generate_morphs.py`/
  `generate_animations.py` → final avatar `.glb`) explicitly once working, since it changes
  `tools/avatar_builder/README.md`'s documented pipeline.
- [ ] **Task 7.5** — Iterate on body/head proportions and topology in mesh-craft until baseline
  deformation problems from Task 7.3 are resolved (shoulders/elbows/knees/wrists/neck/spine no
  longer distort badly under the existing animation clips — reuse the existing animation set,
  don't regenerate animations as part of this task unless a specific clip requires new bone
  layout).
- [ ] **Task 7.6** — Audit and fix hair mesh intersection with face/neck; audit and fix clothing
  mesh clipping/explosion under animation, using the same iterate-in-mesh-craft approach where
  hair/clothes are also good candidates for mesh-craft authoring (per decision 4b's "těla hlavy
  apod" — bodies/heads/etc. — the "etc." covers this).
  Investigate `generate_hair.py`/`generate_clothes.py`/`generate_wardrobe.py` current logic first.
- [ ] **Task 7.7** — Audit normals/tangents/winding/UVs/material assignment on the new
  mesh-craft-sourced geometry (mesh-craft's own `mc3togltf` exporter should produce correct glTF
  PBR material data per its own format spec — verify, don't assume).
- [ ] **Task 7.8** — Audit vertex weights end to end (normalize, cap influence count, reject
  NaN/Inf, validate bone indices) — this stays in the Blender/`generate_skeleton.py` stage since
  mesh-craft has no skinning concept; confirm `validate_gltf.py` (already exists,
  `tools/avatar_builder/validate_gltf.py`) already checks these, extend it if it doesn't.
- [ ] **Task 7.9** — Make the new pipeline deterministic and documented (same requirement the old
  pipeline already had per its own `README.md`) — running it twice with the same inputs produces
  byte-identical (or at least semantically-identical) output.
- [ ] **Task 7.10** — Extend `validate_gltf.py` (or confirm it already covers) failing loudly on
  missing skeletons, invalid weights, invalid bounds, or broken references for the new
  mesh-craft-sourced assets specifically.
- [ ] **Task 7.11** — Capture after screenshots (same views as Task 7.1) and confirm against the
  decision-4c acceptance criteria: front/side/back for both genders, animation gallery, no mesh
  explosions, no distorted limbs.
- [ ] **Task 7.12** — Confirm no proprietary Xbox Avatar asset was referenced anywhere in this
  process (decision 4a — zero exceptions); note this explicitly in the task write-up when this
  phase closes out.
- [ ] **Task 7.13** — Rebuild and rerun the avatar demos affected (`demo_avatar`,
  `demo_avatar_animation_gallery`, `demo_avatar_wardrobe_hotswap`, `demo_avatar_dual_compare`,
  `demo_avatar_appearance_tint_studio`, `demo_avatar_bone_state_boundary`,
  `demo_avatar_multi_attach_stress`, `demo_net_avatar_sync`) to confirm nothing regressed
  visually or functionally with the new asset pipeline.

This phase is the largest and most open-ended in this plan — expect to split it into several
commits (one per concrete sub-task group, not one giant commit), consistent with the
one-task-one-commit rule.

---

## Phase 8 — F1 help overlay for all avatar demos

Per decision 5c, rolled out to **all** avatar-related demos (Phase 0 found 8:
`demo_avatar`, `demo_avatar_animation_gallery`, `demo_avatar_appearance_tint_studio`,
`demo_avatar_bone_state_boundary`, `demo_avatar_dual_compare`, `demo_avatar_multi_attach_stress`,
`demo_avatar_wardrobe_hotswap`, `demo_net_avatar_sync` — re-confirm this list at execution time in
case more exist).

- [ ] **Task 8.1** — Design a small **demo-only** shared helper (per `CLAUDE.md`/meta-prompt
  guidance: "keep demo-only helpers in examples, not public API" — do not add this to CNA's
  public API surface). Likely home: a shared header/source under `examples/common/` or similar if
  that convention already exists (check first), otherwise a new small shared location scoped to
  avatar demos only.
- [ ] **Task 8.2** — Implement: F1 (`Keys::F1`, confirm it exists in the existing `Keys` enum)
  toggles overlay visibility; draws the 3D scene first, then the 2D overlay on top via
  `SpriteBatch`; translucent white rectangle behind black text (decision 5d); uses the default
  text block from this plan's header verbatim (decision 5a) for `demo_avatar`, adapted per-demo
  for the others where command-line args/controls differ (keep the "F1: Show/hide this help" /
  "Esc: Quit" lines identical across all of them for consistency; customize the rest).
- [ ] **Task 8.3** — Needs a `SpriteBatch`, a `SpriteFont`, and a 1x1 white texture in each demo
  that doesn't already have them — reuse if present, add minimally if not.
  Overlay must not crash if font/texture assets fail to load; show inline fallback text if
  practical.
- [ ] **Task 8.4** — Update each demo's window title or intro text to mention F1 help.
- [ ] **Task 8.5** — Verify: build and run each of the 8 demos, confirm F1 toggles correctly, Esc
  still quits, overlay renders on top of the 3D content, and male/female avatar selection (where
  applicable per-demo) doesn't break the overlay.
- [ ] **Task 8.6** — One commit per demo is likely excessive for 8 near-identical additions built
  on the same Task 8.1 helper — since the user's "one task = one commit" rule maps to *this
  plan's tasks*, treat Task 8.1+8.2+8.3 (the shared helper + its first real usage in
  `demo_avatar`) as one task/commit, then each subsequent demo's rollout as its own small
  task/commit referencing this same Phase.

---

## Phase 9 — Docs and demo cleanup

- [ ] **Task 9.1** — Execute Task 1.1's `docs/xna-4-api-coverage.md` fixes (staged here since
  Phase 1 only investigated).
- [ ] **Task 9.2** — Update `docs/avatar-real-rendering-ext.md` with the new mesh-craft-based
  asset pipeline description (Phase 7) and current real-rendering status.
- [ ] **Task 9.3** — Update `tools/avatar_builder/README.md` to describe the new mesh-craft →
  Blender pipeline stages (Phase 7).
  Add a support-matrix table to GamerServices docs: implemented / local-fake-persisted (Phase 4) /
  CNA extension / no-op / genuinely unsupported — replacing whatever currently-stale
  characterization exists.
- [ ] **Task 9.4** — Update Net docs similarly: SystemLink-real, PlayerMatch/Ranked/Invite-stub,
  host-migration-real (Phase 5), simulated-conditions-real (Phase 6).
- [ ] **Task 9.5** — Confirm no doc claims Xbox Live compatibility (only SystemLink-style local
  play is real; PlayerMatch/Ranked/Invite remain documented stubs per the networking-scope
  decision).
- [ ] **Task 9.6** — Add a troubleshooting section for avatar asset generation (mesh-craft +
  Blender pipeline) and network demo startup (ENet port binding, discovery).
- [ ] **Task 9.7** — Add a short README for avatar demo controls/command-line args if one doesn't
  already exist, referencing the new F1 overlays from Phase 8 as the authoritative in-app control
  reference (avoid duplicating control lists that can drift out of sync).

---

## Phase 10 — Tests and validation

- [ ] **Task 10.1** — Run the full existing Net test suite; confirm baseline pass count before
  any Phase 1-9 change (record the exact number here once run).
- [ ] **Task 10.2** — Run the full existing GamerServices test suite; record baseline.
- [ ] **Task 10.3** — After each phase's changes, rerun the full suite (not just the new/changed
  tests) — this is the same revert-verify-restore discipline the prior pass used throughout;
  continue it here.
- [ ] **Task 10.4** — Build/test scope for this entire pass: `cmake-build-debug` with the
  **EASYGL** backend only (decision 6d) — confirm the build is actually configured for EASYGL
  before running (`cmake -S . -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=EASYGL` if not already).
  Do not spend time on Vulkan/SDL_RENDERER/BGFX backends this pass unless a failure turns out to
  be backend-specific and genuinely blocks EASYGL too.
- [ ] **Task 10.5** — Add demo smoke-build targets for the affected avatar and network demos if
  the build system already supports this pattern (check for precedent before inventing one).
- [ ] **Task 10.6** — Record exact commands and pass/fail counts in this plan as each phase
  closes out (matching the prior pass's own documentation style — see
  `plan_net_20260707.md` for the level of detail expected per task write-up).
- [ ] **Task 10.7** — Fix all failures caused by this pass's own changes before considering any
  task done.

---

## Phase 11 — Final audit

- [ ] **Task 11.1** — Re-run `rg -i "TODO|FIXME|NotImplemented|stub|no-op|placeholder|fake"`
  across the same file set as Phase 0 and classify every remaining item as: intentional
  FNA/XNA-fidelity (leave as-is, documented), genuinely fixed by this pass, or a new follow-up
  item for a future pass (list explicitly, do not silently drop).
- [ ] **Task 11.2** — Re-verify `docs/xna-4-api-coverage.md` matches actual code state after all
  phases (not just after Phase 1/9's initial pass — things may have changed further by then).
- [ ] **Task 11.3** — Confirm `plan_net_20260707.md` stays archived (not deleted) and this plan
  (`plan_net.md`) shows every task's final `[x]`/write-up state.
- [ ] **Task 11.4** — Confirm no doc claims Xbox Live compatibility (re-check after Phase 9,
  since new docs/sections were added that could reintroduce a stale/misleading claim).
- [ ] **Task 11.5** — Confirm no proprietary Xbox Avatar asset was introduced anywhere across the
  whole pass (decision 4a) — one final explicit grep/review, not just Phase 7's own internal
  check.
- [ ] **Task 11.6** — Confirm commits are clean and logically separated (one task = one commit,
  per decision 6c) — spot-check `git log` against this plan's task list.
- [ ] **Task 11.7** — Final summary to the user: what changed, tests run and results, remaining
  gaps (including any still-open micro-decisions from this plan's header that never got a
  concrete user answer), and recommended next steps.

---

## Implementation quality rules (carried over, still binding)

- Keep changes minimal but complete — no half-finished implementations.
- Prefer correctness and tests over visual gimmicks.
- Do not hide broken behavior behind demos — if a demo can't honestly show a feature working,
  say so in its own help text rather than faking it.
- Intentionally-unsupported features stay documented and tested as failing clearly (e.g.
  PlayerMatch/Ranked/Invite, `Achievement::GetPicture`, `NetworkMachine::RemoveFromSession` if
  Task 1.2 confirms it should stay that way).
- Local-fake/persisted behavior (Phase 4) is documented as such, not silently presented as a real
  online service.
- XNA-compatible methods (`Microsoft::Xna` namespace) never unexpectedly depend on CNA-only
  rendering/networking services — CNA extensions stay opt-in (`EnableRealRenderingEXT`,
  `*EXT` methods, etc.).
- Avoid global mutable state where possible; where XNA compatibility requires static state
  (e.g. `activeSession_`, `activeAction_`), test reset/dispose behavior explicitly (Phase 2).
- Keep demo-only helpers (Phase 8's F1 overlay helper) in `examples/`, not CNA's public API.
- Make avatar asset generation (Phase 7) deterministic and reviewable.
- Clear English in docs, comments, plan files, and demo help text.
