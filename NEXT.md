# NEXT.md — CNA Input Handoff

> Concise handoff for resuming work later (Claude Code or a human). Based only on the current repo
> state, recent commits, and this session's build/test runs. No invented features.
>
> **Active branch: `feature/input`.** The authoritative task backlog is **`plan_input.md`** — the fresh
> **XNA 4.0 Input completion plan**, organized as **Phases 0–12 (task IDs `P0-001` … `P12-005`)**. It was
> rewritten from scratch (commit `cafbbe10`) to drive a systematic repair/harden/validate pass against the
> FNA reference. This file is a short pointer into it. A separate **Graphics** track lives on `develop`
> with its own `GRAPHICS_TASKS.md` and the repo's most severe open bug (see §4) — out of scope here.
>
> **As of 2026-07-06:** Phases 0–4 complete; Phase 5 (Touch) audit-priority items done, verification
> remainder mostly covered. Branch in sync with `origin/feature/input` at `6585465f`.

## 1. Project summary

- **What:** **CNA** is a C++23 reimplementation of the **XNA 4.0** programming model
  (`Microsoft::Xna::Framework`) on **SDL3**, with a pluggable 3D backend layer (EasyGL/OpenGL ES,
  Vulkan, bgfx, SDL_Renderer). It is a framework/runtime, not a game.
- **Main goal:** full XNA 4.0 API coverage with **FNA-faithful** behavior, backed by unit tests and
  verified line-by-line against the FNA reference at `/rv/data/library/github.com/FNA-XNA/FNA/src`.
- **Current phase (this branch):** executing the **`plan_input.md` Phase 0–12 completion pass** — for each
  Input type: verify against FNA, fix real behavior bugs (with tests), close test-coverage gaps, and mark
  each task with status/files/tests/verification in the plan. Strict XNA-compat by default; non-XNA tagged
  `NOXNA`/`EXT` and documented. Public XNA API must never expose SDL/`CNA::Internal` types.
- **Key architectural decisions:**
  - FNA is the authoritative **behavioral** reference (its code, not its comments); deviations are
    documented in `docs/input-fna-fidelity.md`.
  - Input is **event-driven**: `SdlInputBridge::ProcessEvent` → `InputManager` (+ `TouchPanel`) →
    `Get*State()`. (FNA polls; CNA mutates state at SDL-event time.)
  - Non-XNA members inside `Microsoft::Xna` are tagged `NOXNA` and/or an `EXT` name suffix; `.NET` types
    live in the sibling repo `sharp-runtime`.
  - SDL gamepad calls go through an internal injectable seam (`ISdlGamepadBackend`) so runtime gamepad
    behavior is unit-testable with a fake — never exposed in the public XNA API.

## 2. Current status

- **Build:** clean. This session's edits are **test + doc only** (no `src/` behavior change), and were
  confirmed **backend-agnostic**: `ctest -L input` = 100% on all four backends (**EasyGL / Vulkan / bgfx /
  SDL_RENDERER**).
- **Tests:** the input subset is selected by **`ctest -L input`** (baked `--gtest_shuffle --gtest_repeat=5`
  order-independence gate), all green shuffled ×5. **~+50 input tests this session** across Phases 5–7
  (Touch +10, Gesture +17, Text/IME +6, plus the earlier Phase 2–5 work). The only local failures remain the
  3 `MouseCursor` tests that need real cursors under the SDL `dummy` driver (pass under Xvfb+x11 —
  environmental, not a bug).
- **Plan progress (`plan_input.md`):**
  - **Phases 0–4** — done (real bugs fixed: P2-002 keyboard hash OOB, P3-001 mouse null-window, P4-014
    vibration NaN).
  - **Phase 5** (Touch, P5-001..016) — **done**; DEC-20 (ascending touch-id order), GetCapabilities-after-
    first-touch, zero-display startup divergence documented; P5-015 high-DPI `[!]`→Phase 11.
  - **Phase 6** (Gesture, P6-001..016) — **done**; +17 tests closing negative-path / axis-rejection /
    disabled / velocity-gate / direction / reset gaps (audited vs FNA `GestureDetector.cs`); P6-016 `[!]`.
  - **Phase 7** (Text/IME, P7-001..010) — **done**; +6 tests (empty-text, subscriber order, repeated text,
    IME byte-offset, zero/neg rectangle); control chars + Ctrl+V confirmed FNA-identical; P7-010 `[!]`.
  - **Phase 8** (SDL bridge) — **in progress** (next).
  - **Phases 9–12** — not started (build-CI / docs / manual-HW / final gates).
- **Does NOT work / not headless-verifiable (by design, not code gaps):** real gamepad **actuation**
  (rumble, trigger haptics, live sensors, OS hot-plug/GUID); real **IME** composition; **Wayland**
  cursor-landing readback (X11-only); high-DPI touch pixel scaling (headless runs at 1×1). All documented
  as manual/hardware-gated.

## 3. Recent changes (most recent first — this session, all on `feature/input`)

Three genuine behavior bugs were found and fixed (each with tests + ASan verification); everything else was
verification against FNA plus closing real test-coverage gaps. No public XNA signature/enum changes.

- **`6585465f` — Phase 5 (Touch) audit-priority items (P5-001/002/006/007):** no behavior change warranted.
  - **P5-001** (external-audit #1) `TouchCollection` read-only: FNA's `IsReadOnly` is hard-coded `true` but
    **advisory** — `Add`/`Clear`/`Insert`/`Remove`/`RemoveAt` + settable indexer still mutate the non-null
    backing list. CNA is faithful; making mutation throw would *diverge* from FNA. Fixed the misleading
    `getIsReadOnlyProperty` doc (`TouchCollection.hpp`) + added `IsReadOnlyIsAdvisoryAndMutationStillSucceedsLikeFna`.
  - **P5-002** `CopyTo`: added `CopyToFromEmptyCollectionIsANoOp`.
  - **P5-006** (audit #5) `GetState` dual path: slot vs InputManager fallback — cannot diverge in production
    (`SetFinger` never runs on the real event path); both paths already tested; authoritative path documented.
  - **P5-007** (audit #4) `SetFinger` "duplicated condition": verified line-by-line vs FNA — **no duplicate
    exists** in current code. Added the two missing NO_FINGER release-branch tests.
- **`b612b413` — Phase 4 (GamePad) (P4-001..020):** source verified FNA-faithful (Buttons/GamePadState/DPad/
  ThumbSticks/Triggers, dead-zone, button/axis/type mapping, GUID).
  - **Bug fixed:** `SetVibration`/`SetTriggerVibration` cast a possibly-NaN float to `Uint16` (`std::clamp`
    propagates NaN; NaN→int is UB in C++). Added `motor_level()` → NaN→0 (matching C# `(ushort)NaN`),
    +Inf→full, −Inf→0. ASan-clean.
  - **+15 tests** (gamepad 96→111): vibration clamp/NaN, trigger vibration, light bar, sensor lazy-enable-once,
    slot reuse, stale-state clear on disconnect, packet-number stability, extended GamePadType map, gamepad reset.
    Fake backend gained `lastTriggerLow/High` + `setSensorEnabledCalls` introspection.
- **`aeaa7c23` — Phase 3 (Mouse) (P3-001..012):** **bug fixed** — `Mouse::SetPosition` passed a null window to
  `SDL_WarpMouseInWindow` (UB); added a null-window guard mirroring the relative-mode methods. +tests
  (unknown-button ignore P3-004, mouse reset P3-012, no-window SetPosition).
- **`219ce410` + `d9c3de06` — Phase 2 (Keyboard) (P2-001..012):** **bug fixed** — `KeyboardState::GetHashCode`
  wrote out of bounds (`words[value>>5]` on an 8-word array) for an invalid `Keys` ≥256 / negative; added a
  bounds guard per FNA's `InternalSetKey`. ASan-clean. +tests (OOB/negative/boundary hash, right-modifier/lock
  no-merge). `Keys` confirmed byte-identical to FNA (160 members).
- **`8323bffd` / `2a201814` / `cafbbe10` — Phases 0–1 + plan reset:** replaced `plan_input.md` with the fresh
  XNA 4.0 completion plan; recorded baseline/inventory/scope; verified authoritative API parity.

> Earlier on this same branch (before the plan reset): an **INPUT-*** hardening pass (`f0a185ca` `ctest -L
> input` label, `d2adefd8` EXT/NOXNA tagging, `2ec61806` signature freeze, `ca9bd074` Keys/enum ABI parity,
> `bcf1b92b` bridge fuzz). Those guardrails are still in force (see §6).

## 4. Current blocker / main problem

- **Input track: no hard functional blocker.** It builds on all 4 backends and the input suite passes
  (headless: the 3 `MouseCursor` tests require Xvfb, not a bug). Work is proceeding phase-by-phase through
  `plan_input.md`; the next unit of work is simply "finish Phase 5's small remaining gaps, then Phase 6".
- **No failing command to reproduce.** `ctest -L input` is green (shuffle ×5); the ASan build is exit 0.
- **Most severe open problem in the repo is on the Graphics track (`develop`), NOT input** — recorded so it
  is not forgotten, but **do not fix here**: `confirmed bug` **Task 870** (Vulkan `DepthStencilState`
  largely non-functional — stencil bypassed, `DepthBufferFunction` ignored), **Task 871** (`Clear` ignores
  `ClearOptions::Stencil`), **Task 872** (`ReferenceStencil` override unwired on all backends). See
  `GRAPHICS_TASKS.md` (868–872) and `docs/depthstencilstate-support.md`.

## 5. Known bugs and limitations (input track)

- `needs verification / by-design` — real-hardware gamepad **actuation** (rumble, trigger haptics, live
  sensors, OS hot-plug/GUID) is not headless-verifiable; the fake backend proves translation/bookkeeping
  only. Real controllers untested. (Phase 11 = manual hardware; expected to be marked `[!]` blocked.)
- `incomplete` — **Phase 5 remaining gaps** (small): no test for `TouchCollection::begin()/end()` iteration
  order (P5-003); no test asserting reset empties the gesture queue / clears `previousTouches_` (P5-016);
  no explicit high-DPI touch pixel-scaling test (P5-015 — hard headless, likely `[!]`).
- `incomplete` — **Phases 6–12 not started** (Gesture verification, Text/IME incl. real IME, SDL-bridge
  audit, build/CI closeout, docs, manual hardware, final gates).
- Documented **intentional FNA deviations** (not bugs — `docs/input-fna-fidelity.md`):
  `FNA_GAMEPAD_NUM_GAMEPADS` clamped to 4; event-driven `PacketNumber` (within-dead-zone wobble can bump
  it — `not asserted`); partial-field `GamePadState::GetHashCode`; `MaximumTouchCount` reports 4 but the
  event path caps at 8; GUID/caps live vs FNA cached-at-connect; `TouchCollection` mutable+advisory
  `IsReadOnly` and default collection empty-not-null (P5-001).
- `suspected` (cannot occur via CNA's own API) — Mouse relative-mode cache could desync if SDL relative
  mode is toggled outside `Mouse::setIsRelativeMouseModeEXTProperty` (DEC-14 accepted this).

## 6. Architecture notes

- **Main modules:**
  - `src/CNA/Internal/Input/SdlInputBridge.cpp` — the single funnel: SDL events → `InputManager` /
    `TouchPanel` / `Mouse` / `TextInputEXT`. Owns gamepad slot maps, finger-id map, UTF-8 decode, env
    parsing, gamepad-subsystem init, and the vibration/LED/sensor calls (through `ISdlGamepadBackend`).
  - `src/CNA/Internal/Input/InputManager.cpp` — accumulated input-state singleton (keyboard / mouse /
    4 gamepads / touch) + `Get*State()` snapshots + `ResetForTests()` / `ResetAllForTests()`.
  - `src/CNA/Internal/Input/GestureDetector.cpp` — gesture state machine (has a test clock).
  - `include|src/CNA/Internal/Input/SdlGamepadBackend.*` — `ISdlGamepadBackend` seam + real impl; the fake
    is `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp` (introspection: open/close/rumble/led/sensor
    counters, `lastRumbleLow/High`, `lastTriggerLow/High`, `lastLedR/G/B`, `setSensorEnabledCalls`).
  - `include/Microsoft/Xna/Framework/Input/**` — the XNA-facing public API (26 headers).
- **Data flow:** host loop → `Game::PollEvents` → `SdlInputBridge::ProcessEvent(event)` → mutate
  `InputManager` / `TouchPanel` → game reads `GamePad::GetState` / `Keyboard::GetState` / `Mouse::GetState`
  / `TouchPanel::GetState`.
- **Invariants (keep true):** state updates at event-processing time; gamepad subsystem initialized once at
  startup + lazily in `ProcessEvent` (both idempotent); packet number bumps only on a real state change;
  `ResetAllForTests()` restores a deterministic baseline **and** re-installs the real SDL gamepad backend
  (fake is test-only); tests must stay order-independent under shuffle ×5.
- **Boundaries / API rules that must stay stable (enforced by guards):**
  - Public XNA names/signatures are **frozen** — `PublicApiInputSignatureFreezeTests.cpp` + golden
    `docs/input-public-api-frozen.md`. A change must update BOTH in the same commit.
  - Enum **values** are frozen — exhaustive enum-value suites; renumbering fails a test.
  - No public header may leak SDL — `PublicApiInputCompileTests.cpp` `#error` guard.
  - Do **not** expose `ISdlGamepadBackend` or any `CNA::Internal` type in the XNA layer.
  - Every non-XNA member carries `NOXNA` and/or an `EXT` suffix.

## 7. Useful commands

```bash
# Configure + build tests (EasyGL; swap backend for VULKAN / BGFX / SDL_RENDERER):
cmake -S . -B cmake-build-input-easygl -G Ninja -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-input-easygl --target CnaTests -j"$(nproc)"

# Run the input subset — THE canonical gate (baked shuffle x5, order-independence):
ctest --test-dir cmake-build-input-easygl -L input --output-on-failure
# List what that selects (must be exactly 1 entry: CnaInputTests):
ctest --test-dir cmake-build-input-easygl -N -L input

# Headless box: MouseCursor tests need real cursors — run under a virtual X server:
xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure

# Run a focused subset directly (e.g. gamepad or touch):
xvfb-run -a env SDL_VIDEODRIVER=x11 ./cmake-build-input-easygl/CnaTests --gtest_filter='*GamePad*:*Gamepad*'
xvfb-run -a env SDL_VIDEODRIVER=x11 ./cmake-build-input-easygl/CnaTests --gtest_filter='*Touch*'

# Sanitizer build (ASan+UBSan) + run (Mesa libGLX leaks at exit are third-party):
cmake --build cmake-build-input-asan --target CnaTests -j"$(nproc)"
xvfb-run -a env SDL_VIDEODRIVER=x11 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  ./cmake-build-input-asan/CnaTests --gtest_filter='*FakeGamepad*:*SetVibration*'

# Watch CI (no gh token assumed — unauthenticated API):
curl -s "https://api.github.com/repos/openeggbert/cna/actions/runs?per_page=1" | grep -E '"head_sha"|"status"|"conclusion"'
```
No lint/format config for the input track. There is **no failing input command to reproduce**.

## 8. Next smallest tasks (input track, ordered)

1. **P5-003 — `TouchCollection` iteration-order test.**
   - Goal: assert `for (const auto& t : collection)` (begin/end) yields elements in insertion order.
   - Files: `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp` (test only).
   - Verify: `xvfb-run -a env SDL_VIDEODRIVER=x11 ./cmake-build-input-easygl/CnaTests --gtest_filter='*TouchCollection*'`.
2. **P5-016 — touch-reset completeness test.**
   - Goal: assert `TouchPanel::ResetForTests()` empties the gesture queue (`getIsGestureAvailableProperty()==false`)
     and clears `previousTouches_` slot continuity (a moved finger after reset reads Pressed, not Moved).
   - Files: `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp` (test only).
   - Verify: `... --gtest_filter='*TouchEdgeCase*:*InputResetAllForTests*'`.
3. **P5-015 / P5-004 / P5-005 / P5-008..014 — record verified-covered in `plan_input.md`.**
   - Goal: mark the already-covered Phase 5 verification tasks `[x]` with their existing test names; mark the
     high-DPI pixel-scaling sub-item `[!]` blocked (headless runs at 1×1) with a Phase-11 follow-up.
   - Files: `plan_input.md` (doc only). Verify: none (doc); then run `ctest -L input` to confirm still green.
4. **Phase 6 — Gesture correctness** (`plan_input.md` P6-*). Start by mapping `GestureDetectorTest` +
   `SdlInputBridgeTouchGestureTests.cpp` coverage to the P6 task list, then fix/close real gaps.
   - Files: `src/CNA/Internal/Input/GestureDetector.cpp`, `tests/.../GestureDetectorTest*`. Verify: `ctest -L input`.

After Phase 5 is fully recorded, continue phase-by-phase: 6 (Gesture) → 7 (Text/IME) → 8 (SDL bridge) →
9 (build/CI) → 10 (docs) → 11 (manual HW, expected `[!]`) → 12 (final gates). One task = one commit;
update the matching `plan_input.md` task block (status/files/tests/verification) in the same commit.

## 9. Do not do yet

- **No public XNA API changes** (names / signatures / enum values are frozen and guarded); if unavoidable,
  update the signature-freeze test + `docs/input-public-api-frozen.md` in the **same** commit.
- **No broad refactor** of `SdlInputBridge` / `InputManager` — keep the single-funnel event design.
- **No behavior change that diverges from FNA** without checking the FNA *source* (not its comments) and
  recording it in `docs/input-fna-fidelity.md`. (Reminder from P5-001: FNA's own comments can be misleading.)
- **No exposing** `ISdlGamepadBackend` or any `CNA::Internal` type in the XNA layer.
- **No Graphics work on this branch** — Task 870/871/872 belong to the Graphics track (`GRAPHICS_TASKS.md`);
  don't fix them here or edit that file without deliberately switching context.
- **No merge** of `feature/input` to `develop`/`master` without explicit confirmation.
- **No `git add -A`/`git add .`** — stage files by explicit name (this repo carries unrelated local changes).
- **No new large abstractions** for the hardware-gated items — they are manual/hardware, not missing code.
- **No skipping/batching** unrelated `plan_input.md` tasks — execute in order, one task = one commit, and
  never mark a task done only because it compiles (it must be verified against FNA + tested).

## 10. Resume prompt

```
Read NEXT.md first. Then, working on branch feature/input against the plan_input.md Phase 0–12 backlog:
- Inspect ONLY the files needed for the first task in NEXT.md §8 (do not read the whole tree, and do not
  re-read the obsolete pre-reset plan history).
- Do NOT refactor unrelated code, do NOT change the public XNA API (frozen + guarded), and do NOT touch the
  Graphics track (GRAPHICS_TASKS.md / Task 870-872) unless explicitly asked.
- Make ONE small, verified improvement (start with §8 task 1). Verify each behavior fix against the FNA
  source at /rv/data/library/github.com/FNA-XNA/FNA/src — FNA's code is authoritative, its comments are not.
- Run the relevant command from NEXT.md §7; the input subset must stay green via `ctest -L input`
  (shuffle x5 baked in). If a fix touches source, also confirm the ASan build is clean.
- Update the matching plan_input.md task block (status/files/tests/verification) and NEXT.md (§2, §3, §8) in
  the same commit. One task = one commit; stage files by explicit name. Do not push/merge without asking.
```
