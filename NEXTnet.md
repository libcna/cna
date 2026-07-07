# NEXT.md

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on SDL3 with a pluggable graphics backend
(EasyGL/OpenGL ES, Vulkan, Bgfx, SDL_Renderer — selected via the `CNA_GRAPHICS_BACKEND` CMake
option). It is a framework/runtime, not a game — the goal is XNA 4.0 API coverage with behavior
fidelity to FNA (`/rv/data/library/github.com/FNA-XNA/FNA` for most namespaces,
`/rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/GamerServices/` for GamerServices), backed by
unit tests.

**Current phase:** a fresh second-pass hardening plan, `plan_net.md` ("2026-07-07 Re-Audit and
Hardening"), covering `Net`, `GamerServices`, and Avatar. The prior first-implementation pass
(132/132 tasks) is complete and archived at `plan_net_20260707.md`. The new plan has 11 phases;
**Phase 1 is in progress** (2 of 6 tasks remain), Phase 2 is done, Phases 3-11 have not started.

**Key architectural decisions (see `CLAUDE.md` for the full rules):**
- Strict separation: `Microsoft::Xna::Framework::*` types must match real XNA/FNA behavior exactly.
  `CNA::*` / `NOXNA`-marked / `*EXT`-suffixed members are CNA-original extensions, opt-in only,
  never required by XNA-compatible code paths.
- `sharp-runtime` (sibling repo, `../sharp-runtime/`) supplies all `System.*` types via a direct
  filesystem include path, not a git submodule. Never modify existing `sharp-runtime` files
  without asking the user first, for every commit.
- Real networking is ENet-backed (`CNA::Internal::Net::ENetBackend`), star topology only — clients
  connect directly to the host, never to each other. This matters for the planned host-migration
  work (Phase 5).
- Avatar has two parallel surfaces: the faithful XNA `AvatarRenderer` API (intentionally a
  no-op-by-design, matching real XNA/FNA), and a CNA-original real-rendering extension
  (`EnableRealRenderingEXT`/`DrawRealEXT`, backed by `SkinnedModelEXT`). The current avatar art
  pipeline is Blender-script-based (`tools/avatar_builder/`); this pass is planning (not yet
  started) a shift to author body/head *shape* geometry in the sibling `../mesh-craft` tool
  (a constructive `.mc3.xml` scene editor, `mc3togltf` exports to glTF) feeding into the existing
  Blender rig/skin/animation stages — mesh-craft itself has no skeletal-skinning concept.

## 2. Current status

- **Build status:** last known-good build is at commit `77beeeed` (2026-07-07 19:07), configured
  with `-DCNA_GRAPHICS_BACKEND=EASYGL` in `cmake-build-debug/` (confirmed via `CMakeCache.txt`).
  The one commit since (`f6b74020`) is docs-only — no source changed, so the build should still be
  current, but it has not been re-verified.
- **Test status:** full `CnaTests` suite was **3405/3405 passing** (2 expected skips —
  `AccelerometerTests`/`GyroscopeTests`, hardware-dependent) as of `77beeeed`. Not rerun since.
- **Tools/apps available:** 24+ demo executables build directly under `cmake-build-debug/` (e.g.
  `cna_demo_avatar`, `cna_demo_avatar_animation_gallery`, `cna_demo_net_avatar_sync`).
  `tools/avatar_builder/` (Blender/`bpy` procedural avatar generator, offline, produces
  body+skeleton+animations). `tools/avatar_asset_pipeline/convert_avatar.py` (MakeHuman body +
  Mixamo animation clips → CNA's own `.skinnedmodel.json`/`.skeleton.bin`/`.clip.bin`).
- **Recently implemented:** `NetworkSession`'s destructor now falls back to `Dispose()` if not
  already disposed (fixes a real bug — see section 3).
- **Confirmed-correct, left unchanged:** `NetworkMachine::RemoveFromSession`,
  `NetworkSession::BeginCreate`'s hardcoded-69 `maxGamers` quirk, and
  `PropertyDictionary::CopyTo`'s always-throw are all genuine, source-verified FNA fidelity, not
  bugs.
- **Known working demo:** `cna_demo_avatar --gender male|female [--wardrobe-hair Cap|Ponytail]`
  renders a real, procedurally-generated, animated avatar through the real engine (confirmed
  working as of the prior 132-task pass; not re-verified this session).
- **Does NOT work yet:** `Guide.BeginShowMessageBox` (always throws `NotSupportedException`),
  `Guide.BeginShowKeyboardInput` (always returns an empty string), achievement/leaderboard
  persistence (in-memory only, not confirmed to survive process exit), `AllowHostMigration`
  (stored, no effect — host disconnect always ends the session immediately), `SimulatedLatency`/
  `SimulatedPacketLoss` (stored, no effect on real traffic), and avatar mesh quality (a confirmed
  elbow/sleeve tear and vertex-weight gaps — see section 5 — are the actual root cause of the
  "avatars look like monsters" complaint that triggered this whole re-audit pass, not yet fixed).

## 3. Recent changes

Since the prior 132-task pass closed out (`a3f1c618`), in order:

- `eefaeea3` — archived `plan_net.md` → `plan_net_20260707.md` (132/132 done); wrote a fresh
  `plan_net.md` for this second-pass hardening plan.
- `f7daecea` — Phase 1 investigation only: confirmed `NetworkMachine::RemoveFromSession` and
  `BeginCreate`'s hardcoded `maxGamers=69` are genuine FNA fidelity. No code change.
- `77beeeed` — **fix**: `NetworkSession::~NetworkSession()` now calls `Dispose()` if not already
  disposed. Previously, deleting a session without an explicit `Dispose()` call left the
  `activeSession_` process-wide singleton dangling and skipped ENet transport teardown —
  permanently blocking any new session for the rest of the process. 2 new tests added
  (`NetworkSessionTests.cpp`); verified via revert-verify-restore (tests fail without the fix).
  Full suite 3405/3405.
- `f6b74020` — docs only: confirmed `PropertyDictionary::CopyTo`'s always-throw is genuine,
  source-verified FNA fidelity (checked FNA's real `PropertyDictionary.cs` directly), not a bug —
  a "fix" was implemented, then fully reverted after verification.

Two Phase 1 tasks are written into the plan but **not yet implemented**: `AvatarRenderer::Draw`
and `AvatarRenderer::EnableRealRenderingEXT` both have missing null-argument checks (found by a
separate read-only inventory pass, confirmed by direct code reading, not yet fixed).

## 4. Current blocker / main problem

**There is no failing build or failing test right now.** The nearest thing to a blocker is scope,
not a crash:

- Phase 1 has 2 small unfinished tasks before it can close out (see section 8, tasks 1-2).
- The larger, structural open problem is **Phase 7 (avatar asset quality)** — the actual reason
  this re-audit was requested. It requires prototyping a new pipeline stage (author body/head
  shape in the sibling `../mesh-craft` tool, export via `mc3togltf`, feed into the existing
  Blender rig/skin stage) that has not been prototyped at all yet, only planned on paper
  (`plan_net.md` Phase 7, Task 7.4).
- **Phase 5 (real host migration)** has an explicitly-unresolved design question written into the
  plan itself (Task 5.1): in this star-topology transport, a promoted host has no pre-existing
  connections to the surviving peers, so migration needs a genuine reconnect, not a live socket
  handoff. This needs to be confirmed as acceptable "simple" scope before implementing anything.
- **Process note:** earlier in this pass, a background research agent was unintentionally given
  too much autonomy (it inherited broader "work autonomously" instructions from its parent
  context) and pushed 3 commits — archiving the plan, writing the new plan, and the
  `NetworkSession` destructor fix — without the review checkpoint that was intended. Those commits
  were reviewed afterward and are correct/kept, but it's why Phase 0-2 finished before Phase 1's
  remaining 2 small tasks did.

## 5. Known bugs and limitations

- **Confirmed bug, not fixed:** `AvatarRenderer::Draw(IAvatarAnimation* animation)`
  (`src/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.cpp:101-105`) has no null check —
  passing `nullptr` is undefined behavior (crash), not a catchable exception.
- **Confirmed bug, not fixed:** `AvatarRenderer::EnableRealRenderingEXT(GraphicsDevice&,
  shared_ptr<SkinnedModelEXT>)` (`AvatarRenderer.cpp:121-134`) does not validate its `model`
  argument is non-null — a null model crashes later, inside `DrawRealEXT`, not at the call site.
- **Confirmed bug, fixed:** `NetworkSession` destructor not calling `Dispose()` — see section 3.
- **Confirmed bug, not fixed** (pre-existing, from the prior Avatar-generation pass, not touched
  this session): the procedural avatar body/clothes rig has a real elbow/sleeve tear under bending
  (visible at the `Wave` animation's peak fold) — the forearm/hand visibly separates from the
  shirt sleeve. Root cause: automatic Blender weight-painting, never hand-corrected.
- **Confirmed limitation, not fixed** (same root cause): 32 of 1086 body-mesh vertices have zero
  total bone weight; 24 shirt vertices exceed glTF's 4-joint-influence limit. Both are silently
  handled (a synthetic `neutral_bone` joint; glTF's own trim/renormalize) rather than crashing or
  failing export, but are real visual-quality gaps.
- **Confirmed, intentional — verified against real FNA source, not bugs:**
  `PropertyDictionary::CopyTo` always throws `NotImplementedException`;
  `NetworkMachine::RemoveFromSession` always throws `NotImplementedException`;
  `NetworkSession::BeginCreate`'s simplest overload hardcodes `maxGamers=69` regardless of the
  caller's argument.
- **Incomplete (planned, Phase 3):** `Guide.BeginShowMessageBox`/`BeginShowKeyboardInput` have no
  real overlay/text-capture implementation.
- **Incomplete (planned, Phase 4):** achievement/leaderboard state has no disk persistence.
  Exactly how "earned" state is populated today needs re-confirming (`plan_net.md` Task 4.1) —
  not fully nailed down yet.
- **Incomplete (planned, Phase 5):** `AllowHostMigration` is stored but inert.
- **Incomplete (planned, Phase 6):** `SimulatedLatency`/`SimulatedPacketLoss` are stored but have
  no effect on real traffic.
- **Incomplete (planned, Phase 7):** avatar mesh-craft integration not started.
- **Stale docs, not fixed (planned, Task 9.1):** `docs/xna-4-api-coverage.md` has multiple
  sections still claiming Net/GamerServices/Avatar are unimplemented or intentionally excluded —
  false, per Task 1.1's own line-by-line citations in `plan_net.md`.
- **Needs verification:** whether all 24 demos still build cleanly after the `NetworkSession`
  destructor fix (not re-tested since; the fix is Net-side and unlikely to affect unrelated
  demos, but unconfirmed).
- **Risky assumption, flagged in the plan itself (Task 5.1):** the "simple" host-migration design
  assumes surviving peers can just reconnect fresh to a promoted host. Not yet confirmed as
  acceptable scope.

## 6. Architecture notes

- **Namespace split:** `Microsoft::Xna::Framework::*` = must match real XNA/FNA behavior exactly —
  **check the real FNA source before assuming any `NotImplementedException`/stub is a bug**
  (`FNA` for most namespaces, `FNA.NetStub/src/GamerServices/` for GamerServices). A near-miss on
  `PropertyDictionary::CopyTo` (section 3) is the concrete lesson here. `CNA::*`/`NOXNA`/`*EXT` =
  CNA-original, opt-in extensions.
- **`sharp-runtime`:** sibling repo (`../sharp-runtime/`), included via direct filesystem path
  (not a submodule) — any local change there is immediately visible to this build. Never modify
  existing `sharp-runtime` files without asking the user first, for every commit.
- **Real networking:** `CNA::Internal::Net::ENetBackend` wraps ENet. Star topology only. Key
  state: `SessionState::WireIdToGamer`/`WireIdToPeer`/`PeerWireIds`/`HostPeer`. Wire protocol:
  `NetPacketCodec` — opcode `0x05` (`HostChangeBroadcast`) is reserved but unimplemented, the
  natural hook for Phase 5.
- **`NetworkSession`** is a process-wide singleton (`activeSession_`) — only one active session
  per game, enforced by `BeginCreate`/`BeginFind`/`BeginJoin` throwing if already set.
  `Create`/`Find`/`Join` hand back a caller-owned raw pointer (documented ownership contract); the
  destructor is now a `Dispose()` safety net, but callers should still prefer explicit `Dispose()`.
- **Avatar pipeline today:** `tools/avatar_builder/` (Blender/`bpy` script, offline) →
  `tools/avatar_asset_pipeline/convert_avatar.py` (MakeHuman+Mixamo → CNA's own
  `.skinnedmodel.json`+`.skeleton.bin`+`.clip.bin`) → loaded at runtime via
  `SkinnedModelTypeReader`. Planned change (Phase 7, not started): author body/head *shape*
  geometry in `../mesh-craft` (`.mc3.xml` → `mc3togltf` → `.glb`), which has no skinning concept,
  so rig/skin/animation stays in the existing Blender stage.
- **Demos:** 24 executables under `examples/`, each building to a standalone binary directly under
  `cmake-build-debug/` (e.g. `cmake-build-debug/cna_demo_avatar`). No shared `examples/common/`
  library exists — a `MakeSimpleFont`/rectangle-drawing SpriteBatch helper is duplicated verbatim
  across 11 demos already (established convention: copy-paste per demo).
- **Testing scope for this hardening pass (explicit user decision, do not deviate without
  asking):** EASYGL graphics backend, `cmake-build-debug` only.
- **Invariant to preserve:** one task = one commit; every behavior change needs a test that
  provably fails without the fix (revert-verify-restore discipline used throughout this project).

## 7. Useful commands

```sh
# Confirm/configure build (already configured for EASYGL as of the last build)
cmake -S . -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=EASYGL

# Build the main test target
cmake --build cmake-build-debug --target CnaTests -j$(nproc)

# Run the full test suite
./cmake-build-debug/CnaTests

# Run a filtered subset, e.g. just NetworkSession tests
./cmake-build-debug/CnaTests --gtest_filter="NetworkSessionTest.*"

# Run just the Avatar tests relevant to Phase 1's remaining tasks (1.5, 1.6)
./cmake-build-debug/CnaTests --gtest_filter="AvatarRendererTest.*"

# Build and run the primary avatar demo
cmake --build cmake-build-debug --target cna_demo_avatar -j$(nproc)
./cmake-build-debug/cna_demo_avatar --gender male
./cmake-build-debug/cna_demo_avatar --gender female --wardrobe-hair Ponytail

# Graphics smoke/pixel-readback tests (separate from CnaTests, via ctest)
ctest --test-dir cmake-build-debug
```

No `.clang-format` or other lint/format config was found in the repo — none is currently enforced.

## 8. Next smallest tasks

1. **Add a null check to `AvatarRenderer::Draw(IAvatarAnimation* animation)`.**
   Goal: throw `System::ArgumentNullException("animation")` instead of crashing on `nullptr`.
   Files: `include/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp`,
   `src/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.cpp`,
   `tests/Microsoft/Xna/Framework/GamerServices/AvatarRendererTests.cpp`.
   Verify: `./cmake-build-debug/CnaTests --gtest_filter="AvatarRendererTest.*"`, then full suite.

2. **Add a null check to `AvatarRenderer::EnableRealRenderingEXT(GraphicsDevice&,
   shared_ptr<SkinnedModelEXT>)`.**
   Goal: throw `System::ArgumentNullException("model")` at the call site instead of deferring the
   crash to a later `DrawRealEXT` call.
   Files: same as task 1.
   Verify: same as task 1.

3. **Close out Phase 1 in `plan_net.md`** once tasks 1-2 land (mark `[x]`, write up, commit,
   push) — this finishes Phase 1 (6/6 tasks).
   Files: `plan_net.md`.
   Verify: `git log --oneline -5` shows the closing commit; full suite still passing.

4. **Phase 3, Task 3.1 — investigate before coding.** Read
   `src/Microsoft/Xna/Framework/GamerServices/Guide.cpp:116-142` to confirm the exact current
   `BeginShowMessageBox`/`EndShowMessageBox` signatures and behavior before designing the overlay.
   No code change in this step — pure investigation, matching the plan's own Task 3.1 checklist.
   Files: `src/Microsoft/Xna/Framework/GamerServices/Guide.cpp`.
   Verify: n/a (investigation only).

5. **Phase 4, Task 4.1 — re-confirm achievement state population.** Read
   `AchievementCollection`'s constructor(s) and `examples/demo_achievement_showcase` to determine
   whether "earned" state is currently in-memory-only or already touches disk anywhere.
   Files: `include/Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp`,
   `examples/demo_achievement_showcase/`.
   Verify: n/a (investigation only).

## 9. Do not do yet

- Do not modify any existing `sharp-runtime` file without asking the user first — for every
  single commit, no exceptions.
- Do not start Phase 7 (mesh-craft avatar pipeline integration) or Phase 5 (host migration) before
  Phases 1-4 are done — both are large and design-heavy, and Phase 5 has an explicitly-unconfirmed
  scope question (Task 5.1) that needs a decision first.
- Do not refactor the 11-demo `MakeSimpleFont` copy-paste pattern into a shared
  `examples/common/` header as a side effect of Phase 8 — the plan explicitly defers that as a
  possible future cleanup, out of scope for this pass.
- Do not "fix" any `NotImplementedException`/`NotSupportedException`/stub-looking code without
  first checking the real FNA source (`FNA`/`FNA.NetStub`) — Task 1.4 already cost real time on
  exactly this mistake once.
- Do not build or test against any backend other than EASYGL, or any build directory other than
  `cmake-build-debug`, for this pass (explicit user decision).
- Do not delete or rewrite `plan_net_20260707.md` — it's archived, not deleted, and referenced by
  file/line throughout the current `plan_net.md`.
- No mass rewrites, no speculative architecture changes, no unrelated cleanup.

## 10. Resume prompt

```text
Read NEXT.md first. Inspect only the files needed for the first unfinished task in section 8
(currently: adding a null check to AvatarRenderer::Draw). Do not refactor unrelated code. Make one
small, verified improvement: implement the fix, add a test that fails without it (revert-verify-
restore), then restore the fix. Run:
  cmake --build cmake-build-debug --target CnaTests -j$(nproc)
  ./cmake-build-debug/CnaTests --gtest_filter="AvatarRendererTest.*"
followed by the full suite (./cmake-build-debug/CnaTests) to confirm no regressions. Commit with a
message referencing the plan_net.md task ID, then update NEXT.md's sections 2, 3, 4, and 8 to
reflect the new state before finishing.
```
