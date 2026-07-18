# NEXT.md

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on SDL3 with a pluggable graphics backend
(EasyGL/OpenGL ES, Vulkan, Bgfx, SDL_Renderer — selected via the `CNA_GRAPHICS_BACKEND` CMake
option). It is a framework/runtime, not a game — the goal is XNA 4.0 API coverage with behavior
fidelity to FNA (`/rv/data/library/github.com/FNA-XNA/FNA` for most namespaces,
`/rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/GamerServices/` for GamerServices), backed by
unit tests.

**Current phase:** `plan_net.md` ("2026-07-07 Re-Audit and Hardening") is fully checked off
(104/104 tasks, all 11 phases `[x]`, including Task 11.7's own final summary). **However, an
independent post-completion audit (2026-07-18) found that several of the plan's own "done" claims
overstated what was actually delivered** — see section 3 below for the specifics and section 6 for
the active remediation work now in progress. Treat `plan_net.md`'s checkmarks as "this task's
described work was completed," not as "the underlying feature is fully correct/polished" — several
were not. The prior first-implementation pass (132/132 tasks) is complete; its own archive file
(`plan_net_20260707.md`) no longer exists in the working tree (deleted by a later, separate,
deliberate repo-wide cleanup commit, `e86b7cba` — still fully recoverable via git history, see
Task 11.3's write-up in `plan_net.md`).

**Key architectural decisions (see `CLAUDE.md` for the full rules):**
- Strict separation: `Microsoft::Xna::Framework::*` types must match real XNA/FNA behavior exactly.
  `CNA::*` / `NOXNA`-marked / `*EXT`-suffixed members are CNA-original extensions, opt-in only,
  never required by XNA-compatible code paths.
- `sharp-runtime` (sibling repo, `../sharp-runtime/`) supplies all `System.*` types via a direct
  filesystem include path, not a git submodule. Never modify existing `sharp-runtime` files
  without asking the user first, for every commit.
- **Decision 1a**: the Xbox 360 XNA 4.0 reference behavior (not Windows' PC no-op stubs) is the
  correctness bar for Net/GamerServices/Avatar, since CNA already has its own real
  avatar/networking implementations. This is why GamerServices/Net/Avatar are real, tested
  implementations now, not the "Xbox Live exclusive, not planned" stubs older docs described.
- Real networking is ENet-backed (`CNA::Internal::Net::ENetBackend`), star topology only — clients
  connect directly to the host, never to each other.
- Avatar has two parallel surfaces: the faithful XNA `AvatarRenderer` API (intentionally a
  no-op-by-design, matching real XNA/FNA off-Xbox — see `demo_avatar_bone_state_boundary` for a
  live comparison), and a CNA-original real-rendering extension (`EnableRealRenderingEXT`/
  `DrawRealEXT`, backed by `SkinnedModelEXT`). The avatar art pipeline is Blender-script-based
  (`tools/avatar_builder/`), with body/clothing *shape* geometry now generated via the sibling
  `../mesh-craft` tool's CSG engine (Phase 7) rather than Blender's plain datablock-join — see
  `docs/avatar-real-rendering-ext.md`'s "Phase 7" section for why that mattered (it fixed the
  "monster avatar" mesh-explosion bug).

## 2. Current status

- **Build status:** full project rebuild is clean (`cmake --build cmake-build-debug -j$(nproc)`)
  except one pre-existing, unrelated, already-documented failure: `cna_demo_xact`'s Content-copy
  step (XACT audio demo, nothing to do with Net/GamerServices/Avatar — see
  `scripts/run-all-backend-smoke-tests.sh`'s own comment referencing it). `CNA_GRAPHICS_BACKEND=EASYGL`,
  `CNA_BUILD_TESTS=ON`, `CNA_BUILD_EXAMPLES=ON` are all set in `cmake-build-debug/CMakeCache.txt`.
- **Test status:** `ctest -j$(nproc)` → **4884/4935 passing (99%)**. All 51 failures individually
  investigated (not assumed pre-existing) — none traced to this plan's own work:
  - Missing `.xnb`/MonoGame test fixture files (majority) — an environment/checkout gap.
  - Mesa llvmpipe/Xvfb software-rendering limitations (`EasyGL_MRT_TwoAttachments`,
    `EasyGL_GraphicsDevice_ReferenceStencil`, `EasyGL_RealWindowResize` timeout,
    `easy-gl-resource-smoke-tests` abort).
  - Parallel-`ctest`-execution contention, confirmed benign: every one of
    `LeaderboardReaderTest`/`AudioCategoryTest`/`WaveBankTest`/`ENetBackendTest`/
    `ENetDiscoveryServiceTest`'s failing cases passes cleanly standalone or under `ctest -j1` (the
    `ENet*` cases line up with a real, already-documented design property — many parallel test
    *processes* sharing one well-known discovery port, 61190, is expected OS-arbitrary contention,
    not a bug). See `plan_net.md`'s own Task 10.7 write-up for the full per-category detail.
- **Tools/apps available:** 24+ demo executables under `cmake-build-debug/`, including 8
  avatar-related demos, all now with an in-app **F1 help overlay** (Phase 8) — press F1 in any of
  them for its exact current controls. `docs/avatar-demos.md` is the controls/troubleshooting
  reference. `tools/avatar_builder/` now builds production avatar content via
  `generate_body_meshcraft.py`/`generate_clothes_meshcraft.py` (mesh-craft CSG pipeline, Phase 7),
  not the older plain-primitive-join `generate_body.py`/`generate_clothes.py` (still
  standalone-runnable, but superseded for real content generation).
- **Real, working features added this pass:**
  - `Guide.BeginShowMessageBox` — real `SpriteBatch`-based overlay (Phase 3).
  - `Guide.BeginShowKeyboardInput` — real captured text via `TextInputEXT` (Phase 3).
  - Achievements/Leaderboards — real disk persistence via `LocalGamerServicesStore`
    (`StorageDevice::GetStorageRootEXT()`-backed), not in-memory-only (Phase 4).
  - Real host migration for `SystemLink` sessions (Phase 5).
  - Real `SimulatedLatency`/`SimulatedPacketLoss` on actual ENet traffic (Phase 6).
  - Fixed the "monster avatar" mesh-explosion bug via a mesh-craft CSG-based body/clothing
    pipeline (Phase 7) — see `docs/avatar-real-rendering-ext.md`'s "Phase 7" section.
  - F1 help overlay across all 8 avatar-related demos (Phase 8) — and along the way, found and
    fixed a real, previously-undetected rendering bug: the shared `MakeSimpleFont` helper's glyph
    `bounds` were `(0,0,1,1)`, so every character rendered as a single sub-pixel dot instead of a
    visible block (fixed to `(0,0,6,10)`/`(0,0,0,0)` for space), plus a panel-width calculation
    that assumed the wrong per-character advance. Both fixed in every demo that has the F1
    overlay. **The same broken `MakeSimpleFont` pattern still exists, unfixed, in 10 other
    pre-existing demos outside this plan's scope** — see section 5 below, it's a real follow-up
    item, not silently dropped.
  - `docs/xna-4-api-coverage.md`, `docs/avatar-real-rendering-ext.md`,
    `tools/avatar_builder/README.md`, `docs/coverage.md` all corrected to describe real
    GamerServices/Net status instead of "Guide-stub-only"/"Xbox Live exclusive, not planned"
    (Phase 9). New `docs/avatar-demos.md` (controls + troubleshooting).
- **Real, pre-existing bugs found and fixed this pass (Phase 12-14, done before Phase 2 in
  session order):** `NetworkSession::Dispose()` double-call use-after-free, async completion
  callbacks never invoked, `GamerCollectionEnumerator::MoveNext()` null-deref after `Dispose()`.

## 3. Known gaps (honest, not glossed over)

### Confirmed by an independent post-completion audit (2026-07-18) — remediation status below

- **F1 overlay text was not actually readable — ✅ FIXED (2026-07-18).** `MakeSimpleFont`'s Phase 8
  fix stopped every character from rendering as an invisible sub-pixel dot, but it still rendered
  every character as an *identical solid rectangle* — no letterform differentiation at all. Fixed
  by replacing the per-demo `MakeSimpleFont` with a new shared header,
  `examples/common/SimpleFontEXT.hpp`'s `CNAExamplesEXT::MakeSimpleFontEXT()` — a real 5x7
  dot-matrix bitmap-font glyph atlas covering printable ASCII 32-126, where each character samples
  its own distinct region of the atlas instead of a single uniform rectangle. Deliberately a
  *shared* header this time (not another per-demo copy) — the old per-demo-copy convention is
  exactly what let the original uniform-rectangle bug spread silently across all 8 demos in the
  first place. Rolled out to all 8 avatar demos, verified with fresh screenshots of every one:
  help text (and `demo_avatar_multi_attach_stress`'s own `Parts.size()` counter) is now genuinely,
  fully readable English, not just correctly-spaced blocks. Text drawn at 1.5x scale (legible,
  while still keeping the longest help line within an 800px-wide window).
- **`Guide.cpp`'s Phase 3 work is genuinely incomplete**, confirmed by direct code read:
  `BeginShowKeyboardInput`'s `title`/`description` parameters are unused (commented out in the
  signature itself); `getIsVisibleProperty()` is hardcoded `return false;` and the setter is a
  no-op; `UsePasswordMode` is stored on the action object but never read anywhere in the file (no
  masking behavior); there is no cancel path (only Enter completes the pending input). Phase 3's
  own `plan_net.md` write-up did not disclose these as remaining gaps.
- **Avatar visual quality has more real artifacts than Phase 7 disclosed.** Fresh screenshots
  (male, female, and mid-`Wave`) confirm the core proportions are genuinely fixed (no more
  stick-thin limbs / too-small head), but there are visible dark shading/seam artifacts at the
  neck, both shoulder-to-upper-arm junctions, the groin, and — worst — a large dark mass across
  the chest during `Wave`, not just the single "residual shoe-area artifact" and "chest-band
  artifact" the Phase 7 write-up described. Likely a flat-recomputed-normals-at-CSG-union-seams
  shading issue (a known, disclosed limitation of the mesh-craft merge, per
  `docs/avatar-real-rendering-ext.md`), but the *visual extent* was understated. **Needs deeper
  diagnosis before a fix can be scoped** — see section 6.
- **`NEXTnet.md`'s own example command was broken** when followed from the repo root (see the
  working-directory note in section 5) — fixed in this same pass that added this subsection.

### Pre-existing gaps, already known (lower severity, most already disclosed elsewhere)

- **`SignedInGamer::GetFriends()`** always returns an empty `FriendCollection` — no friend-list
  population source exists at all (found during Phase 11's final audit; self-documented in
  `FriendCollection.hpp`'s own comment, out of Phase 4's specific persistence scope, a real
  follow-up if friend-list functionality is ever prioritized).
- **The shared `MakeSimpleFont` glyph-bounds bug** (see section 2) is fixed everywhere Phase 8
  touched, but the *same* broken pattern (`bounds.push_back(Rectangle(0, 0, 1, 1))`) still exists,
  unfixed, in 10 other pre-existing demos entirely outside this plan: `demo_leaderboard_viewer`,
  `demo_gamerservices_signin_presence`, `demo_gamer_roster_hud` (the origin this pattern was
  copied from), `demo_net_client_server_arena`, `demo_gamerservices_dispatcher_watchdog`,
  `demo_achievement_showcase`, `demo_simulated_network_conditions`, `demo_gamer_profile_privileges`,
  `demo_session_browser`, `demo_friends_and_gamercard` (found via `grep -rl
  "bounds.push_back(Rectangle(0, 0, 1, 1))" examples/`). Every on-screen text label in those demos
  is almost certainly rendering as dots too. Deliberately left unfixed — out of this plan's own
  scope (avatar demos only) and too large a blast radius (11 files across unrelated
  plans/subsystems) to take on unprompted.
- **Avatar mesh quality**: the core "monster" complaint (disproportionate limbs, mesh explosions
  at joints) is genuinely fixed (Phase 7). Smaller, real gaps remain open: a residual shoe-area
  dark artifact, a `Wave`-pose chest-band artifact, and `validate_gltf.py` still lacking NaN/Inf/
  bone-index-bounds checks on generated content.
- **`NetworkSessionType::PlayerMatch`/`Ranked`, session invites** remain documented stubs — no
  matchmaking/invite backend exists to implement them against (not "Xbox Live exclusive," just no
  online service to connect to).
- **`Guide.Show`** remains a no-op — there is no system UI to show (consistent with FNA's own
  minimal PC `Guide`).
- **`cna_demo_xact`** fails to build (Content-copy step) — pre-existing, unrelated to this plan,
  already documented elsewhere.

## 4. Architecture notes

- **Namespace split:** `Microsoft::Xna::Framework::*` = must match real XNA/FNA behavior exactly —
  **check the real FNA source before assuming any `NotImplementedException`/stub is a bug**
  (`FNA` for most namespaces, `FNA.NetStub/src/GamerServices/` for GamerServices). Phase 11's own
  165-hit grep audit re-confirmed this discipline held throughout the whole pass — 162/165 hits
  were correctly intentional FNA-fidelity, 0 were a stale comment describing pre-fix behavior as
  current, only 1 was a genuine new follow-up item (`GetFriends()`, section 3).
- **`sharp-runtime`:** sibling repo (`../sharp-runtime/`), included via direct filesystem path
  (not a submodule) — any local change there is immediately visible to this build. Never modify
  existing `sharp-runtime` files without asking the user first, for every commit.
- **Real networking:** `CNA::Internal::Net::ENetBackend` wraps ENet. Star topology only. Discovery
  uses a well-known UDP port (61190, `SO_REUSEADDR`-shared across processes) plus broadcast +
  loopback fallback; the real game-session transport port is OS-assigned/ephemeral (`CreateHost(0,
  ...)`), so it essentially never has a fixed-port binding conflict — see `docs/avatar-demos.md`'s
  troubleshooting section for the full detail (search-window/retry timing, launch ordering, etc.).
- **Avatar pipeline today:** `tools/avatar_builder/generate_avatar.py`/`generate_wardrobe.py` now
  build via `generate_body_meshcraft.py`/`generate_clothes_meshcraft.py` (aliased in as drop-in
  replacements) → `mc3togltf` (mesh-craft's CSG exporter) → Blender import/rig/skin/animation →
  `export_gltf.py` → `.glb` → `tools/avatar_asset_pipeline/convert_avatar.py --embedded-clips` →
  `.skinnedmodel.json`/`.skeleton.bin`/`.clip.bin`, loaded at runtime via `SkinnedModelTypeReader`.
- **Demos:** 24+ executables under `examples/`, each building to a standalone binary directly under
  `cmake-build-debug/`. No shared `examples/common/` library exists for the F1-overlay
  `MakeSimpleFont`/rectangle-drawing helper — deliberately duplicated per demo, matching this
  project's established convention (see any Phase 8 commit's own comment for why).
- **Testing scope for this hardening pass:** EASYGL graphics backend, `cmake-build-debug` only.
- **Invariant to preserve:** one task = one commit; every behavior change needs a test that
  provably fails without the fix (revert-verify-restore discipline used throughout this project).

## 5. Useful commands

All commands below are run from the **repo root** except the demo executables themselves, which
**must be run with `cmake-build-debug/` as the working directory** — every demo's `ContentManager`
resolves asset paths (e.g. `Content/avatar/male/avatar`) relative to the process's current working
directory, not relative to the executable's own location. Running a demo binary directly from the
repo root (`./cmake-build-debug/cna_demo_avatar`) crashes with `Cannot open file:
Content/avatar/male/avatar` — confirmed by direct reproduction, not a hypothetical. `cd
cmake-build-debug` first, or `(cd cmake-build-debug && ./cna_demo_avatar ...)` in a subshell.

```sh
# Confirm/configure build (from repo root)
cmake -S . -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON -DCNA_BUILD_EXAMPLES=ON

# Full rebuild (from repo root; keep going past the known cna_demo_xact failure)
cmake --build cmake-build-debug -j$(nproc) -- -k 0

# Full test suite (from repo root)
ctest --test-dir cmake-build-debug -j$(nproc)

# Build (from repo root) + run an avatar demo with its F1 help overlay forced on, non-interactively
# (cd into cmake-build-debug/ first - see the working-directory note above)
cmake --build cmake-build-debug --target cna_demo_avatar -j$(nproc)
cd cmake-build-debug
SDL_AUDIODRIVER=dummy xvfb-run -a ./cna_demo_avatar --show-help --smoke 30 --screenshot /tmp/out.png
cd ..

# Two-process real Net test (host + join), from inside cmake-build-debug/
(cd cmake-build-debug && ./cna_demo_net_avatar_sync --host --smoke 150 &
 cd cmake-build-debug && ./cna_demo_net_avatar_sync --join --smoke 150 &
 wait)
```

No `.clang-format` or other lint/format config was found in the repo — none is currently enforced.

## 6. Next smallest tasks

**Active remediation (user-prioritized, 2026-07-18, all 4 items):**

1. ~~**F1 overlay real readable font**~~ — ✅ **DONE.** New shared
   `examples/common/SimpleFontEXT.hpp`, rolled out to all 8 avatar demos, verified with fresh
   screenshots of every one. See section 3's own entry for detail.
2. **Guide.cpp Phase 3 completion** — wire up `title`/`description` in the keyboard-input overlay,
   make `UsePasswordMode` actually mask displayed input, add a real cancel path, make
   `IsVisible`/its setter reflect actual overlay state.
3. **Avatar visual seam/shading artifacts** — diagnose and fix the neck/shoulder/groin/chest dark
   shading found by fresh screenshot inspection (section 3), likely a CSG-merge flat-normals issue.

**Not yet started, not this session's active scope:**

4. **Optional follow-up:** fix the same `MakeSimpleFont` glyph-bounds bug (section 3) in the 10
   other pre-existing demos it also affects (unrelated plans/subsystems, large blast radius —
   check in before starting, per section 7).
5. **Optional follow-up:** real friend-list population for `SignedInGamer::GetFriends()` (section
   3) — needs its own design decision, not just a mechanical fix.
6. **Optional follow-up:** `validate_gltf.py`'s NaN/Inf/bone-index-bounds gap (`plan_net.md` Task
   7.8/7.10).

## 7. Do not do yet

- Do not modify any existing `sharp-runtime` file without asking the user first — for every
  single commit, no exceptions.
- Do not "fix" any `NotImplementedException`/`NotSupportedException`/stub-looking code without
  first checking the real FNA source (`FNA`/`FNA.NetStub`) first.
- Do not build or test against any backend other than EASYGL, or any build directory other than
  `cmake-build-debug`, for this plan's own scope (explicit user decision).
- Do not fix the 10-other-demos `MakeSimpleFont` bug (section 6, item 4) without checking in first
  — it's real and worth doing, but it's outside `plan_net.md`'s own stated scope (avatar demos
  only) and touches files across multiple unrelated plans.
- No mass rewrites, no speculative architecture changes, no unrelated cleanup.
- Before claiming any of section 6's active remediation items (1-3) "done," re-verify with fresh
  eyes (a fresh screenshot, a fresh read of the actual code) — this exact document previously
  described the F1 overlay as "legible" when it genuinely wasn't; don't repeat that mistake on the
  next round of fixes either.

## 8. Resume prompt

```text
Read NEXT.md first, section 3's "Confirmed by an independent post-completion audit" subsection and
section 6's active remediation list. Continue whichever of the 3 active items (F1 font, Guide.cpp,
avatar seams) is least complete. Before marking any of them done, verify with fresh
screenshots/code reads, not just re-reading your own prior claims about them.
```
