# NEXT.md — CNA Input Handoff

> Closing handoff, written 2026-07-18. `plans/plan_input.md`'s own 500+-task audit is **done and
> merged** — this revision replaces the 2026-07-07 "Phase 0 just finished" version, which had
> gone stale (Phases 1–13 all completed and closed on 2026-07-16/17 without this file ever being
> updated to match — the live progress notes were instead written into the repo's shared
> `NEXT.md`, which has since moved on to other tracks; see §3). Nothing in this revision was
> built or re-tested by the session that wrote it — this is a documentation-reconciliation pass
> only, based on `plans/plan_input.md`'s own recorded Results and `git log`. No invented claims.

**Branch:** `feature/input`. **As of this writing, `feature/input`, `develop`, and
`origin/develop` all point at the exact same commit** (`aaa956df`, `Merge branch 'feature/input'
into develop`) — the Input-audit work described below has already been merged. This checkout
being "on `feature/input`" no longer means "diverged from develop"; there is currently no
outstanding Input-track diff to merge.

## 0. TL;DR for a clean context

- **The Input audit (`plans/plan_input.md`, Phases 0–13, 505 tasks) is closed.** 490 done, 15 blocked
  by design (real-hardware checks, see §5), 0 left open. Closed 2026-07-17, merged to `develop`
  shortly after (exact merge-commit date not independently re-verified in this pass — the merge
  commit itself, `aaa956df`, is what's confirmed).
- **There is currently no active Input work item.** Unless the user gives a fresh, explicit
  request (new NOXNA feature, a newly-found bug, the Phase-11 hardware pass), there is nothing
  queued in `plans/plan_input.md` to pick up next — see §8.
- **The only two genuinely open Input-adjacent items** are (a) the 15 Phase-11 manual-hardware
  checks (need real keyboards/mice/gamepads/touchscreen/IME/high-DPI hardware — inherently
  un-automatable, tracked `[!]` in `plans/plan_input.md`), and (b) a flagged-but-out-of-scope
  intermittent crash (`P9-031`/`P12-013` item 2) in the *full, non-Input-filtered* test suite —
  confirmed unrelated to Input, not fixed here, needs a separate cross-subsystem owner.
- **This repo is a shared mainline across many concurrent tracks** (Input, Devices, Net, Media,
  Audio, Graphics, WebGPU, Avatar, …), each with its own `plan_<track>.md` and, while active, the
  *same* `NEXT.md` file (renamed to `NEXT<track>.md` once that track's work is done and merged —
  see the rename history via `git log --follow -- NEXTinput.md`). **The shared `NEXT.md` file in
  this checkout is currently about `feature/graphics`, not Input** — do not read it for Input
  status; it is unrelated, later history that happens to share this file's git ancestry.

## 1. Project summary

- **What:** CNA is a C++23 reimplementation of the **XNA 4.0** programming model
  (`Microsoft::Xna::Framework`) on **SDL3**, with a pluggable 3D graphics-backend layer
  (EasyGL/OpenGL ES, Vulkan, bgfx, SDL_Renderer, WebGPU). It is a framework/runtime, not a game.
- **What `plans/plan_input.md` covered:** a from-scratch, 13-phase deep audit/hardening/documentation
  pass over the entire **Input** subsystem — strict XNA-4.0-compatible types under
  `Microsoft::Xna::Framework::Input` (26 headers) plus the NOXNA `CNA::Input` extension layer
  (24 headers: clipboard, haptics, joysticks, power, sensors, extra gamepad/mouse/keyboard/touch
  members) built on top of it.
- **Architectural decisions that still matter for any future Input work:**
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
  - Public XNA member **names/signatures are frozen** — pinned in
    `PublicApiInputSignatureFreezeTests.cpp` + `docs/input-public-api-frozen.md`.

## 2. Final status (from `plans/plan_input.md`'s own closing tasks)

- **Task tally (`P12-015`, 2026-07-17):** 505 real tasks (see `plans/plan_input.md`'s top-of-file
  status banner for the 506-vs-505 arithmetic note — a harmless, documented pre-existing table
  artifact). **490 `[x]` complete, 15 `[!]` blocked (all Phase 11, by design), 0 `[ ]`/`[~]`/`[?]`
  remaining.**
- **Every phase 0–13 is fully closed except Phase 11**, which is blocked exactly as designed
  pending real hardware (not a code gap).
- **Merge/no-merge recommendation (`P12-014`, 2026-07-17):** MERGE the audit work; do not yet
  claim the project's own "Input stable" release-gate label (`docs/input-pre-merge-checklist.md`,
  `INP-0199`) until the Phase-11 hardware pass actually happens. This has since been merged
  (`aaa956df`) — the "do not merge yet" caveat was about the *label*, not the branch, and the
  recommendation was to merge regardless.
- **What the audit actually found and fixed** (see `plans/plan_input.md`'s per-phase Result fields for
  exact detail): 3 behavioral bugs in Phase 1's strict-XNA parity sweep, 2 real gaps in Phase 3's
  Mouse motion-event/DPI-transform audit, a documented (not fixed — judged correct as-is)
  gesture-timestamp unit-mismatch finding in Phase 6 (`P6-012`), a gamepad-subsystem
  shutdown-symmetry fix in Phase 8 (`P8-002`, the only production-code change from the generic
  Phase 1–12 sweep), plus **Phase 13's 4 externally-audited (`audit_input.md`, 2026-07-16)
  confirmed defects**: `TouchPanel` frame-accuracy (`P13-002`), desktop focus-lost/gained
  `Game::IsActive` wiring (`P13-003`), a consistent focus-loss state-retention policy
  (`P13-004`), and `TouchPanel::GetCapabilities()` real SDL touch-device enumeration (`P13-005`).
  **Zero `Microsoft::Xna::Framework::Input` public member was removed; zero `CNA::Input` NOXNA
  extension was removed** — confirmed explicitly in `P12-015`'s closing statement.
  - Note for memory-carryover accuracy: an earlier session's memory record described `P13-006`
    (stale-documentation reconciliation) as "still open" — that was true mid-Phase-13 but is now
    stale itself; `P13-006`'s own Result (2026-07-16) shows it closed, having regenerated
    `docs/input-test-coverage.md` and `docs/input-member-parity-matrix.md` and corrected a stale
    numbering disclaimer in `docs/input-backend.md`.
- **Build/test state at closure (`P12-001`..`009`, `P13-001`, all re-verified same pass):** all 4
  graphics backends green; 2 sanitizer configs (ASan, UBSan) clean under CI's exact flags; the
  canonical Input-filtered CTest gate (`-L input`, `--gtest_shuffle --gtest_repeat=5`) passed
  repeatedly with 0 failures at **524 tests** (grew from 314 at the 2026-07-07 baseline through
  Phases 1–13's new coverage). None of this has been *re-verified* by the session writing this
  particular revision — it is what `plans/plan_input.md` itself recorded as of 2026-07-17.

## 3. Where the "recent work" record actually lives

The 2026-07-07 revision of this file predicted execution would continue task-by-task with this
file (`NEXTinput.md`) kept current. **That is not what happened.** Whoever executed Phases 1–13
did so against the repo's single shared, currently-active `NEXT.md` (per this repo's convention
of one live `NEXT.md` at a time, renamed to `NEXT<track>.md` once a track closes — see
`git log --follow -- NEXTinput.md` for the rename chain: `NEXTaudio.md` → ... → `NEXT.md` →
`NEXTdevices.md` → ... → the 2026-07-07 input reset → never renamed again). By the time the Input
audit actually closed (2026-07-17), the shared `NEXT.md` had moved on to interleaved progress
notes for several *other* concurrent tracks (Devices, Net, Media, Audio, Avatar, Graphics,
WebGPU) — nobody wrote a dedicated Input-closing entry there, and nobody renamed it back to
`NEXTinput.md` at that point. **Do not go looking in the current `NEXT.md` for Input detail** —
as of this writing it is headed "`feature/graphics` session handoff" and is unrelated. The
authoritative record of what happened, phase by phase, is `plans/plan_input.md`'s own per-task Result
fields (grep `git log --oneline -- plans/plan_input.md` for the phase-closing commits, e.g.
`b2a26493`..`1746df1e` for Phases 1–12, `d4b2e662`/`ed0a4c6a`/`4639b060`/`8561a1e9` for Phase 13).

This revision's own "recent changes": only this file and `plans/plan_input.md`'s top-of-file status
banner were touched (2026-07-18), to reconcile them with reality. No source, test, or other doc
file was changed in this pass.

## 4. Current blocker / main problem

**None.** There is no known failing build, no failing test, and no open task in `plans/plan_input.md`.
The two items worth tracking are not blockers on any next action, just standing, documented facts:

- **15 Phase-11 hardware-validation checks remain `[!]` Blocked** — by design; they require
  physical keyboards/mice/gamepads/a touchscreen/an IME/a high-DPI display, none of which are
  available to an automated/headless session. Not a code defect.
- **`P9-031`/`P12-013` item 2: a real, reproducible, non-deterministic `double free or
  corruption` crash exists in the *full* (non-Input-filtered) test suite.** Confirmed by the
  audit to be unrelated to Input, but it remains unresolved and needs dedicated cross-subsystem
  bisection by whoever owns that area — it is explicitly out of `plans/plan_input.md`'s own scope.

## 5. Known bugs and limitations

- **Incomplete by design (not a bug):** the 15 Phase-11 manual-hardware checks — see §4.
- **Documented, judged-acceptable deviation from FNA:** `P6-012` — CNA's gesture-timestamp
  formula deliberately does not replicate FNA's own tick/millisecond unit-mismatch quirk. Revisit
  only if bit-for-bit FNA replication of that specific value is ever required.
- **Low-priority, real, tracked gap:** `P7-039` — `Joysticks`/`Sensors`/`Power` NOXNA extensions
  have no demo-UI surface, so no Phase-11 manual-checklist item exists for them either; their
  fake-backend unit coverage is substantial, only physical-hardware confirmation is missing.
- **Engineering decisions from the earlier `input_noxna.md` pass (still valid, unchanged):**
  **N-004** (Mouse cursor-visibility EXT) was skipped — would conflict with the existing
  `Game::IsMouseVisible` path. **N-012 `CNA::Input::Pen`** (stylus) was cancelled by the owner —
  do not implement without a fresh, explicit request.
- **Out-of-scope, not an Input bug:** the `P9-031` full-suite crash (§4); do not attempt to fix
  it under an Input-scoped task — it needs its own cross-subsystem investigation.
- **Historical, already fixed, worth re-checking only if touch code changes again:** an
  event-driven `TouchLocation` previous-location bug (old audit item "I12") — fixed in an earlier
  pass, re-confirmed clean by Phase 5/`P13-002`'s frame-accuracy work.

## 6. Architecture notes

- **Data flow:** SDL3 event → `SdlInputBridge::ProcessEvent` (`src/CNA/Internal/Input/`) →
  `InputManager` (keyboard/mouse/gamepad state) or `TouchPanel` (touch + `GestureDetector`) →
  public `Keyboard::GetState()` / `Mouse::GetState()` / `GamePad::GetState()` /
  `TouchPanel::GetState()` snapshot getters. Device-scoped extensions (`Joysticks`, `Haptics`)
  have their **own** seams (`ISdlJoystickBackend`, `ISdlHapticBackend`), separate from the
  gamepad seam (`ISdlGamepadBackend`), because a gamepad is a *mapped view* of the same physical
  device a raw joystick handle also opens independently.
- **Main modules:** `include|src/Microsoft/Xna/Framework/Input/**` (strict XNA, 26 headers) ·
  `include|src/CNA/Input/**` (24 NOXNA extension headers) ·
  `include|src/CNA/Internal/Input/**` (SDL bridge + backends, internal-only) ·
  `tests/{Microsoft/Xna/Framework/Input, CNA/Input, CNA/Internal/Input}` (grew to 524
  Input-filtered tests by closure) · `docs/input-*.md` + `platform-input-notes.md`
  (cross-linked docs, all refreshed/regenerated as of Phase 13's `P13-006`).
- **Invariants / boundaries that must not be broken** (unchanged from the 2026-07-07 revision,
  now backed by frozen compile-time tests rather than just convention):
  - Public XNA-compatible headers must **never** require including a `CNA::Input` extension
    header, and must **never** leak a concrete SDL type (`PublicApiInputCompileTests.cpp` guards
    this; confirmed still passing at closure).
  - Public XNA member names/signatures are frozen — pinned in
    `PublicApiInputSignatureFreezeTests.cpp` + `docs/input-public-api-frozen.md`; any EXT-on-XNA
    member change must update both in the same commit.
  - Enum numeric values are frozen (exhaustive enum-value test suites) — never renumber.
  - Process-wide static input state (event bridge, seams) needs a reset hook
    (`ResetForTests` / `SetSystem*BackendForTests(nullptr)`), exercised because the `-L input`
    gate runs shuffled ×5 and tests must be order-independent.
  - Don't broaden the single-funnel event design (`SdlInputBridge::ProcessEvent`) into multiple
    dispatch paths.

## 7. Useful commands

*(Unchanged from the last verified pass — not re-run by the session that wrote this revision;
confirm before relying on them if significant time has passed or if a build dir is missing.)*

```bash
# Configure (first time only, if a build dir is missing — swap EASYGL for VULKAN/BGFX/SDL_RENDERER):
cmake -S . -B cmake-build-input-easygl -G Ninja -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON

# Build the Input-relevant test binary:
ninja -C cmake-build-input-easygl CnaTests

# THE gate — canonical Input test subset (baked --gtest_shuffle --gtest_repeat=5), was 100% (524 tests) at closure:
xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure

# Run one suite directly by filter, e.g. while working a single plans/plan_input.md task:
xvfb-run -a env SDL_VIDEODRIVER=x11 ./cmake-build-input-easygl/CnaTests --gtest_filter='KeyboardInputTest.*'

# ASan/UBSan builds + rerun after any src/ change (both were clean at closure):
ninja -C cmake-build-input-asan CnaTests
xvfb-run -a env SDL_VIDEODRIVER=x11 ./cmake-build-input-asan/CnaTests --gtest_filter='<your suite>*'

# Full CNA test suite (confirm no non-Input regression) — WARNING: this is the suite where the
# unrelated P9-031 intermittent crash lives; a failure here may not be an Input regression:
xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl --output-on-failure

# Regenerate the two audit-generated docs if source/tests change again (Phase 13's P13-006 tools):
python3 tools/input_parity/check_input_test_coverage.py
python3 tools/input_parity/gen_input_parity_matrix.py
```

## 8. Next smallest tasks

**There is no queued task.** `plans/plan_input.md` has no open (`[ ]`/`[~]`/`[?]`) items — the next
task depends entirely on what the user actually wants next. Reasonable options, in likely order
of usefulness, if/when this track is picked back up:

1. **Do nothing until asked.** This is the most likely correct answer — the audit's own
   recommendation was to merge (done) and move on; there is no standing backlog.
2. **If the user wants to close the "Input stable" release-gate label for real:** perform the 15
   Phase-11 manual-hardware checks on real hardware (a keyboard, a mouse, at least one real
   gamepad, a touchscreen, an IME-capable input method, a high-DPI display) and record results in
   `plans/plan_input.md`'s Phase 11 tasks. This is the only thing standing between "code-complete,
   headless-verified" (current state) and the project's own defined "Input stable" bar.
3. **If a new Input bug is reported or a new NOXNA feature is explicitly requested:** treat it as
   a fresh, small, separately-scoped task — append it to `plans/plan_input.md` (don't reopen closed
   phases) or start a new plan file, per this repo's usual convention for post-closure work.
4. **If someone wants the cross-subsystem `P9-031` crash fixed:** that is not an Input task; it
   needs its own investigation, likely coordinated with whichever track currently owns the full
   test suite's stability (check the current live `NEXT.md`/active `plan_*.md` files for who that
   is now).
5. **Housekeeping, only if the user asks:** `feature/input` now points at the same commit as
   `develop`/`origin/develop` — there is no outstanding diff to merge. Whether to delete the
   now-redundant local/remote `feature/input` ref is a decision for the user, not something to do
   unprompted.

## 9. Do not do yet

- **No new NOXNA extension features** without an explicit fresh owner request — don't reopen
  N-012 Pen or invent new N-xxx tasks.
- **No public XNA API signature/behavior change** without updating
  `PublicApiInputSignatureFreezeTests.cpp` + `docs/input-public-api-frozen.md` in the same commit.
- **No claiming "Input stable"** (the project's own release-gate label) until the Phase-11
  hardware pass actually happens on real devices — "code-complete + headless-verified" is the
  accurate characterization today, per the audit's own P12-014 recommendation.
- **No `git add -A` / `git add .`** — stage by explicit file name (repo carries unrelated local
  changes, e.g. vendored dirs).
- **No fixing `P9-031`** under an Input-scoped task/commit — it's confirmed unrelated to Input;
  fold it into whichever track legitimately owns full-suite stability.
- **No treating the current shared `NEXT.md` as Input-relevant** — it is currently about
  `feature/graphics`; Input's own record lives in `plans/plan_input.md` and this file.

## 10. Resume prompt

```
Read NEXTinput.md first (this file) — it is the closing handoff for plans/plan_input.md, which is
fully closed (505 tasks: 490 done, 15 blocked on real hardware, 0 open) and already merged into
develop. There is no queued Input task. Before doing anything, confirm with the user what they
actually want: (a) nothing — just confirming status, (b) the Phase-11 manual-hardware validation
pass (needs real devices), (c) a brand-new bug/feature request, to be scoped as its own small task
appended to plans/plan_input.md, or (d) something unrelated to Input entirely (check which track's
plan/NEXT file is currently active in the shared NEXT.md before assuming Input is what's meant).
Do not restart or reopen any of Phases 0-13 speculatively.
```
