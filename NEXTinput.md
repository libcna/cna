# NEXT.md — CNA Input Handoff

> Concise handoff for resuming work in a fresh context (Claude Code or a human). Based only on the
> current repo state, git history, and prior sessions' recorded build/test runs — nothing here was
> built or tested in the session that wrote this revision (2026-07-07, plan-reset session). No
> invented features, no unverified claims of success.

**Active branch:** `feature/input`, 1 commit ahead of `origin/feature/input` (needs a push).

**Three backlogs have run on this branch, in order:**
1. **`plan_input.md` (original XNA 4.0 Input completion/hardening pass, Phases 0–12).** COMPLETE
   as of 2026-07-06 (automated scope; only its Phase 11 manual-hardware remained `[!]`). **This
   plan file has since been archived** — see below.
2. **`input_noxna.md` / `input_noxna_progress.md` (NOXNA/SDL3 extension pass).** COMPLETE as of
   2026-07-07. 21 tasks done (one commit each, all pushed as of `74c323a9`), N-004 skipped
   (engineering conflict), N-012 Pen cancelled (explicit owner scope cut). Nothing queued.
3. **`plan_input.md` v3 — fresh 500-task deep audit (2026-07-07, THIS IS THE ACTIVE BACKLOG).**
   The user ordered the plan reset from scratch: the old `plan_input.md` (backlog #1's file) was
   archived verbatim to **`plan_input_20260707.md`** (historical only — do not read it as a
   source of truth) and a brand-new `plan_input.md` was authored with **500 sequential tasks**
   across 13 phases (`P0-001`…`P12-015`), re-auditing/hardening/documenting both the strict XNA
   4.0 `Microsoft::Xna::Framework::Input` API and the `CNA::Input` NOXNA extension layer that
   backlog #2 built. **Only Phase 0 (`P0-001`..`P0-020`, baseline/inventory) is done so far.**
   Execution continues at **`P1-001`**.

---

## 1. Project summary

- **What:** CNA is a C++23 reimplementation of the **XNA 4.0** programming model
  (`Microsoft::Xna::Framework`) on **SDL3**, with a pluggable 3D graphics-backend layer
  (EasyGL/OpenGL ES, Vulkan, bgfx, SDL_Renderer). It is a framework/runtime, not a game.
- **Goal (current phase):** this branch (`feature/input`) is a deep, ongoing audit/hardening pass
  over the **Input** subsystem specifically — strict XNA-4.0-compatible types under
  `Microsoft::Xna::Framework::Input` plus the NOXNA `CNA::Input` extension layer built on top of
  it (clipboard, haptics, joysticks, power, sensors, extra gamepad/mouse/keyboard/touch members).
- **Architectural decisions that matter for any Input work:**
  - **FNA is the authoritative behavioral reference** (its *code*, not its comments):
    `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`. NOXNA extensions have no FNA counterpart;
    for those, SDL3 itself is the reference.
  - **Input is event-driven:** `SdlInputBridge::ProcessEvent` → `InputManager` (+ `TouchPanel`
    for touch/gestures) → the public `Get*State()` getters. Public XNA API must never expose SDL
    or `CNA::Internal` types (enforced by `PublicApiInputCompileTests.cpp`).
  - **NOXNA rule:** anything inside `Microsoft::Xna` that is not XNA 4.0 carries the `NOXNA`
    macro (from `CNA/CNAHelper.hpp`) and usually an `EXT` name suffix. Brand-new extension types
    live in the public `CNA::Input` namespace (`include|src/CNA/Input/`), whole class `NOXNA`.
  - `.NET`-style helpers (`System::EventHandler<T>`, type aliases like `bytecs`/`String`, etc.)
    live in the sibling repo **`sharp-runtime`** (`../sharp-runtime`), not inline in CNA.

## 2. Current status

- **Build/test status: not re-verified in this session** (explicitly instructed not to build).
  Last known-good state, from the `input_noxna.md` pass (2026-07-07, commit `74c323a9`): EasyGL
  build (`cmake-build-input-easygl`) and ASan build (`cmake-build-input-asan`) both clean;
  `ctest -L input` 100% green (shuffled ×5). That state has not changed since — no `src/`,
  `include/`, or `tests/` files were touched in the plan-reset session, only `plan_input.md` /
  `plan_input_20260707.md` (both docs, no code).
- **What works (per the last verified pass, not re-checked here):**
  - All 26 strict-XNA Input types (`Buttons` … `TouchPanelCapabilities`) and all 24 `CNA::Input`
    extension headers build and pass their existing test suites across all 4 graphics backends.
  - `cna_input_smoke` combined demo and `InputDemo` (under `examples/`) build.
  - Fake backends (`FakeSdlGamepadBackend`, `FakeSdlHapticBackend`, `FakeSdlJoystickBackend`) make
    gamepad/haptic/joystick behavior testable headlessly.
- **What does not work / is not verified:**
  - **Zero of the 15 Phase-11 manual-hardware checks have been physically performed** (real
    keyboards/mice/gamepads/touchscreen/IME/high-DPI) — by design, tracked as `[!]` Blocked in
    the new `plan_input.md`, not a code gap.
  - The new 500-task audit (Phases 1–12) has **not started executing** yet — Phase 0 was pure
    inventory/recording (no source touched), so no new bugs have been found or fixed by it yet.
- **Recently implemented features:** see `input_noxna_progress.md` for the full list (21 NOXNA
  extension tasks — Clipboard, Power, Sensors, InputDevices, Joysticks, Haptics, GamePad/Mouse/
  Keyboard/Touch EXT members). Nothing new was implemented in the plan-reset session itself.

## 3. Recent changes (this session, 2026-07-07 — plan reset only, no code)

- **Archived:** `plan_input.md` (backlog #1, 83,583 bytes) → `plan_input_20260707.md` (git `A`,
  historical only, not read/used as a source of truth after the rename).
- **Rewrote:** `plan_input.md` — new 500-task plan, 13 phases, generated programmatically to
  guarantee exact per-phase task counts and sequential IDs (`P0-001`..`P12-015`).
- **Executed:** Phase 0 (`P0-001`..`P0-020`) of the new plan — baseline recording and repository
  inventory only (git/toolchain/submodule state, header/source/test/doc counts). No `src/`,
  `include/`, or `tests/` file was modified.
- **Commit:** `46bb525c docs(input): reset plan_input.md as 500-task deep audit plan (P0-001..020 done)`
  (1 commit, not yet pushed as of this NEXT.md revision).
- No tests were added/changed, no behavior changed, no bugs fixed in this session.

## 4. Current blocker / main problem

**There is no known failing build or test right now.** The last verified state (`input_noxna.md`
pass, commit `74c323a9`) was fully green, and nothing since then has touched code. The actual
"blocker" is procedural, not a bug:

- **Symptom:** none observed — no failing command, no failing test known at this time.
- **What's blocking forward progress:** the new `plan_input.md` (500 tasks) has only had its
  Phase 0 (bookkeeping) executed. The real audit work — starting at **`P1-001`**
  (`Audit ButtonState member parity vs FNA`) — has not begun, so no fresh findings exist yet.
- **Suspected risk to watch for once Phase 1 starts:** none identified yet; Phase 1 is exactly
  the mechanism designed to surface any such issue (member-by-member diff against
  `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`).
- **Already tried:** N/A — no debugging has occurred; this is a fresh-start handoff point.

## 5. Known bugs and limitations

- **Confirmed, historical (already fixed):** an event-driven `TouchLocation` previous-location
  bug (I12, see git history around `868-872` in the old audit) was found and fixed in an earlier
  pass — not currently open, but worth re-checking under the new Phase 5 touch audit
  (`P5-023` in `plan_input.md`) since it's a historically fragile area.
- **Incomplete by design (not bugs):** the 15 Phase-11 manual-hardware checks in `plan_input.md`
  — real device validation is inherently un-automatable; tracked `[!]` Blocked, not missing code.
- **Engineering decision, not a bug:** **N-004** (Mouse cursor-visibility EXT) was skipped —
  would conflict with the existing `Game::IsMouseVisible` path (already calls `SDL_ShowCursor`).
- **Explicitly out of scope, not a bug:** **N-012 `CNA::Input::Pen`** (stylus) was cancelled by
  the owner (2026-07-07); `input_noxna.md` still documents its analysis for reference only — do
  not implement it without a fresh, explicit owner request.
- **Needs verification (unknown until Phase 1–9 of the new plan actually runs):** whether every
  public Input header is fully self-contained / SDL-leak-free (`plan_input.md` `P1-027`/`P1-028`),
  whether the `HapticDevice` SDL-pointer exposure is the right long-term design (`P7-025`),
  whether all 4 graphics-backend builds currently pass Input tests identically (`P8-035`..`038`).
- **Unrelated, out-of-scope known bug (different branch, do not fix here):** Graphics-track
  Vulkan `DepthStencilState` issues (Tasks 870/871/872 in `GRAPHICS_TASKS.md`, on `develop`) — not
  an Input issue, not touched by this branch.

## 6. Architecture notes

- **Data flow:** SDL3 event → `SdlInputBridge::ProcessEvent` (`src/CNA/Internal/Input/`) →
  `InputManager` (keyboard/mouse/gamepad state) or `TouchPanel` (touch + `GestureDetector`) →
  public `Keyboard::GetState()` / `Mouse::GetState()` / `GamePad::GetState()` /
  `TouchPanel::GetState()` snapshot getters. Device-scoped extensions (`Joysticks`, `Haptics`)
  have their **own** seams (`ISdlJoystickBackend`, `ISdlHapticBackend`), separate from the
  gamepad seam (`ISdlGamepadBackend`), because a gamepad is a *mapped view* of the same physical
  device a raw joystick handle also opens independently.
- **Main modules:** `include|src/Microsoft/Xna/Framework/Input/**` (strict XNA, 26 headers/18
  sources) · `include|src/CNA/Input/**` (24 NOXNA extension headers/7 sources) ·
  `include|src/CNA/Internal/Input/**` (SDL bridge + backends, internal-only) ·
  `tests/{Microsoft/Xna/Framework/Input, CNA/Input, CNA/Internal/Input}` (~40 test files) ·
  `docs/input-*.md` + `platform-input-notes.md` (10 cross-linked docs).
- **Invariants / boundaries that must not be broken:**
  - Public XNA-compatible headers must **never** require including a `CNA::Input` extension
    header, and must **never** leak a concrete SDL type (`PublicApiInputCompileTests.cpp` guards
    this; `MouseCursor.hpp`'s opaque `struct SDL_Cursor;` forward-decl is the reference pattern).
  - Public XNA member **names/signatures are frozen** — pinned in
    `PublicApiInputSignatureFreezeTests.cpp` + `docs/input-public-api-frozen.md`; any EXT-on-XNA
    member change must update both in the same commit. Whole-class NOXNA `CNA::Input` types are
    additive and are **not** pinned there (e.g. `Clipboard`, `Power`).
  - Enum numeric values are frozen (exhaustive enum-value test suites) — never renumber.
  - Process-wide static input state (event bridge, seams) needs a reset hook
    (`ResetForTests` / `SetSystem*BackendForTests(nullptr)`) exercised in test fixtures, since the
    `-L input` gate runs shuffled ×5 and tests must be order-independent.
  - Don't broaden the single-funnel event design (`SdlInputBridge::ProcessEvent`) into multiple
    dispatch paths.
- **Reusable extension patterns** (for any new NOXNA work, if ever requested): full detail with
  examples lives in `input_noxna_progress.md` §4-equivalent content preserved from the prior
  `NEXT.md` revision — poll-based gamepad-seam extension, NOXNA member added to a frozen XNA
  type, standalone `CNA::Input` type + injectable system seam, standalone type + its own
  device-scoped seam with independent hot-plug. Don't reinvent these; mirror the closest fit.

## 7. Useful commands

*(Carried forward from the prior verified pass — not re-run in this session; confirm before
relying on them if significant time has passed.)*

```bash
# Configure (first time only, if a build dir is missing — swap EASYGL for VULKAN/BGFX/SDL_RENDERER):
cmake -S . -B cmake-build-input-easygl -G Ninja -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON

# Build the Input-relevant test binary:
ninja -C cmake-build-input-easygl CnaTests

# THE gate — canonical Input test subset (baked --gtest_shuffle --gtest_repeat=5), must be 100%:
xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure
# Equivalent named test (see CMakeLists.txt ~1946-1964, CNA_INPUT_TEST_FILTER / CnaInputTests):
xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -R CnaInputTests --output-on-failure

# Run one suite directly by filter, e.g. while working a single plan_input.md task:
xvfb-run -a env SDL_VIDEODRIVER=x11 ./cmake-build-input-easygl/CnaTests --gtest_filter='KeyboardInputTest.*'

# ASan build + rerun after any src/ change:
ninja -C cmake-build-input-asan CnaTests
xvfb-run -a env SDL_VIDEODRIVER=x11 ./cmake-build-input-asan/CnaTests --gtest_filter='<your suite>*'

# Full CNA test suite (confirm no non-Input regression):
xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl --output-on-failure
```

No known failing command exists right now to "reproduce a bug" (§4) — there is no active bug.

## 8. Next smallest tasks

These are simply the next items in `plan_input.md`, in order — do not skip ahead:

1. **`P1-001` — Audit `ButtonState` member parity vs FNA.**
   Goal: member-by-member audit of `ButtonState` (ctors, operators, enum values, defaults,
   equality, hash) against `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/ButtonState.cs`.
   Files: `include/Microsoft/Xna/Framework/Input/ButtonState.hpp`.
   Verify: `xvfb-run -a env SDL_VIDEODRIVER=x11 ./cmake-build-input-easygl/CnaTests --gtest_filter='GamePadInputTest.*'`
2. **`P1-002` — Audit `Buttons` member parity vs FNA.**
   Goal: same treatment for the `Buttons` flag enum, including bit-value parity.
   Files: `include/Microsoft/Xna/Framework/Input/Buttons.hpp`.
   Verify: `--gtest_filter='GamePadButtonsTest.*'` (same runner as above).
3. **`P1-003` — Audit `GamePad` member parity vs FNA.**
   Goal: static entry points (`GetState`, `GetCapabilities`, `SetVibration`) vs FNA overload set.
   Files: `include/Microsoft/Xna/Framework/Input/GamePad.hpp`, matching `.cpp`.
   Verify: `--gtest_filter='GamePadTest.*:GamePadInputTest.*:GamePadMappingTest.*'`
4. **`P1-004`..`P1-026` — continue the same per-type audit** for the remaining 23 strict-XNA
   Input types listed in `plan_input.md`'s Phase 1 section, one task = one commit, updating each
   task's **Result** field with real findings before moving to the next.
5. **After P1-026: `P1-027`** — header self-containment audit (no strict-XNA header may include
   a `CNA/Input/*` header) using `PublicApiInputCompileTests.cpp` as the check.

Each task in `plan_input.md` already has its own Goal/Steps/Acceptance/Files/Tests/Notes/Result
block — use that as the authoritative detail, this section is just a pointer to "start here."

## 9. Do not do yet

- **No broad refactor** of the SDL bridge or `InputManager` — Phase 8 of `plan_input.md` audits
  it task-by-task; don't pre-empt with a sweeping rewrite.
- **No unrelated cleanup** outside the task currently being executed in `plan_input.md`.
- **No new NOXNA extension features** — the `input_noxna.md` backlog is complete and closed;
  don't reopen N-012 Pen or invent new N-xxx tasks without an explicit fresh owner request.
- **No public XNA API signature/behavior change** without updating
  `PublicApiInputSignatureFreezeTests.cpp` + `docs/input-public-api-frozen.md` in the same commit.
- **No merge** of `feature/input` to `develop`/`master` without explicit confirmation.
- **No `git add -A` / `git add .`** — stage by explicit file name (repo carries unrelated local
  changes, e.g. vendored dirs).
- **No batching** — one `plan_input.md` task = one commit; never bundle unrelated task IDs.
- **No marking a task `[x]` without evidence actually gathered in this checkout**, and no
  claiming a test passed without having run it (see `plan_input.md`'s own execution rules).
- **No Graphics-track work** — Tasks 870/871/872 (Vulkan `DepthStencilState`) live on `develop`,
  not here.
- **No touching `plan_input_20260707.md`** as a source of truth — it is archived history only.

## 10. Resume prompt

```
Read NEXT.md first, then open plan_input.md and find the first task still marked `[ ]` (currently
P1-001). Inspect only the files that task's own "Files likely touched" list names — do not
refactor unrelated code. Do the audit/fix it describes, add or update exactly the test(s) it
names if a fix is needed, and run the verification command from NEXT.md §7 (or the task's own
Tests section) to confirm. Make one small, verified improvement per task — do not batch multiple
plan_input.md task IDs into one commit. Update that task's Result field in plan_input.md with the
exact files changed, exact tests run, exact command output, and remaining risk, then commit by
explicit file name (never `git add -A`) referencing the task ID. After finishing, update NEXT.md's
"Recent changes" and "Next smallest tasks" sections to reflect the new state before ending the
session.
```
