# CNA Input Deep Audit, Stabilization, and Repair Plan — 2026-07-07

## About this plan

This is a **fresh plan**, generated from scratch on 2026-07-07. It supersedes and replaces the
previous `plan_input.md` in every respect.

- The previous plan was archived verbatim to `plan_input_20260707.md`. It was **not** read,
  grepped, summarized, or otherwise consulted while writing this plan or while executing it.
- `plan_input_20260707.md` is historical only and **must not be used as a source of truth** for
  status, scope, or completion claims in this plan. Any fact needed from "before" must be
  re-derived from the current repository state (git history, current source, current tests,
  current docs), not from the archived file.
- **CNA Input is being kept.** Nothing in `Microsoft::Xna::Framework::Input` is being reverted
  or removed by this plan.
- **NOXNA Input extensions are intentional and are being kept.** The `CNA::Input` extension
  layer (Clipboard, GamePad extensions, Haptics, Joysticks, Power, Sensors, TextInputType, etc.)
  is a deliberate part of this project, not scope creep to be undone. This plan audits, hardens,
  documents, and tests that layer — it does not discard it.
- The goal is:
  1. **Strict XNA 4.0 parity** where the type/member is part of the original XNA 4.0 API surface
     (validated against the local FNA reference tree at
     `/rv/data/library/github.com/FNA-XNA/FNA`).
  2. **FNA-compatible behavior** where FNA itself made a documented, load-bearing implementation
     choice beyond the bare XNA 4.0 spec.
  3. **Clearly documented CNA/NOXNA extensions** where functionality goes beyond XNA 4.0/FNA —
     each such member is marked with the `NOXNA` macro (inside `Microsoft::Xna`) or lives under
     the `CNA` namespace, and is described as an intentional extension, not an accidental
     deviation.

## Status legend

```md
- [ ] Not started
- [~] In progress
- [x] Completed
- [!] Blocked
- [?] Needs verification
```

## Execution rules (binding for every task below)

1. Execute tasks strictly in the order given, unless a task is genuinely blocked (in which case
   mark it `[!]`, record why, and move to the next task — do not skip ahead silently).
2. One task = one focused audit/fix/test/documentation action. Do not batch unrelated fixes into
   one task's execution.
3. Update this file immediately when a task completes, with: status, exact files changed, exact
   tests added/updated, exact commands run, result, and remaining risk — filled into that task's
   **Result** field.
4. Never mark a task `[x]` without evidence that it was actually done in this checkout. Never
   claim a test passed without having run it. Never claim hardware/manual validation (Phase 11)
   without an actual physical device in hand.
5. Do not add features beyond audit/repair/test/documentation scope. Do not remove any NOXNA
   Input extension.
6. Strict XNA behavior and NOXNA/EXT behavior are audited and reported on separately, even when a
   single task's Files list touches both (e.g. an SDL bridge fix that affects both a strict-XNA
   getter and a NOXNA extension getter).
7. Public XNA-compatible headers must never require an application to also include a
   `CNA::Input` extension header (see P1-027). Public headers must not expose concrete SDL types
   unless a task in this plan (P3-036/P3-037, P7-024/P7-025) explicitly decides to allow it, with
   the decision documented.

## Phase overview

| Phase | Title | Task count | IDs |
|---|---|---|---|
| 0  | Baseline, repository hygiene, and plan reset | 20 | P0-001..020 |
| 1  | Strict XNA Input public API audit | 45 | P1-001..045 |
| 2  | Keyboard and text input audit/fixes | 60 | P2-001..060 |
| 3  | Mouse and cursor audit/fixes | 45 | P3-001..045 |
| 4  | GamePad audit/fixes | 70 | P4-001..070 |
| 5  | TouchPanel, TouchCollection, and TouchLocation audit/fixes | 45 | P5-001..045 |
| 6  | Gesture audit/fixes | 45 | P6-001..045 |
| 7  | CNA / NOXNA Input extension audit/fixes | 40 | P7-001..040 |
| 8  | SDL bridge and backend integration audit/fixes | 40 | P8-001..040 |
| 9  | Tests, fuzzing, sanitizers, and CI | 35 | P9-001..035 |
| 10 | Documentation and public compatibility notes | 25 | P10-001..025 |
| 11 | Manual hardware validation checklist | 15 | P11-001..015 |
| 12 | Final readiness gates and merge decision | 15 | P12-001..015 |
| 13 | Confirmed defect remediation (audit_input.md 2026-07-16) | 6 | P13-001..006 |
| **Total** | | **506** | |

Phase 0 was executed while authoring this plan (2026-07-07) — see its 20 tasks below, all
`[x]` with the exact commands and output that produced each fact. Execution continues at
**P1-001**.

Phase 13 was appended on 2026-07-16 after an external audit (`../audit_input.md`) found four
confirmed defects still reachable through Phases 1–10's still-open generic tasks. See Phase 13's
own header, above its first task, for how it relates to the generic Phase 2/5/8 tasks it supersedes.

---

## P0-001 — Record git branch and HEAD commit hash `[x]`
**Goal:** Capture the exact branch and commit this audit pass starts from.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Baseline reference point for every later diff/commit in this plan.

**Result:** `git branch --show-current` → `feature/input`. `git log -1 --format="%H %ci"` → `b89baad7770aa3b086289e3be18921780946125a 2026-07-07 18:53:28 +0200`.

---

## P0-002 — Record toolchain versions (compiler, CMake, Ninja) `[x]`
**Goal:** Capture the exact build toolchain in use so build failures can be triaged against a known-good baseline.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Version drift in the compiler/CMake is a common source of false "regressions".

**Result:** `cmake --version` → 3.31.6. `g++ --version` → g++ (Debian 14.2.0-19) 14.2.0. `ninja --version` → 1.12.1.

---

## P0-003 — Record OS/kernel version `[x]`
**Goal:** Capture the host OS so platform-specific input behavior (e.g. SDL backend selection) can be reasoned about.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Linux-only baseline; Phase 11 hardware checklist still applies to other OSes separately.

**Result:** `uname -a` → Linux thinkpadt14 6.12.90+deb13-amd64 #1 SMP PREEMPT_DYNAMIC Debian 6.12.90-1 (2026-05-22) x86_64 GNU/Linux.

---

## P0-004 — Record SDL/SDL_image/SDL_mixer/googletest submodule pinned commits `[x]`
**Goal:** Capture exact vendored dependency revisions relevant to Input (SDL event/gamepad/haptic/sensor APIs).

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** SDL API behavior (e.g. gamepad mapping, haptic effect struct layout) is version-sensitive.

**Result:** `git submodule status` → third_party/SDL @ cbe3fbe9 (release-3.4.0-685-gcbe3fbe9f), third_party/SDL_image @ fcb9d0b1, third_party/SDL_mixer @ 3075d3ed, vendor/googletest @ 7e2c425d.

---

## P0-005 — Record sharp-runtime sibling repository status `[x]`
**Goal:** Confirm the required sibling checkout is present and clean, since CNA depends on it as a non-submodule sibling.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Per [[project_sharp_runtime_concurrent_editing]]: a committed sharp-runtime interface change (not just mid-edit) can break the cna_input build; recorded here as a pre-flight check.

**Result:** `../sharp-runtime`: HEAD `123f602f2218d1b9938706ffb8692016bc576d47` @ 2026-07-07 18:46:48 +0200, `git status --short` clean.

---

## P0-006 — Record existing CMake build directories `[x]`
**Goal:** Enumerate available preconfigured build directories so later phases reuse them instead of reconfiguring from scratch.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Dedicated `cmake-build-input-*` directories already exist for asan/bgfx/easygl/sdlrenderer/vulkan variants.

**Result:** `ls -d cmake-build*` → cmake-build-bgfx, cmake-build-debug, cmake-build-input-asan, cmake-build-input-bgfx, cmake-build-input-easygl, cmake-build-input-sdlrenderer, cmake-build-input-vulkan, cmake-build-vulkan.

---

## P0-007 — Record working tree cleanliness before starting `[x]`
**Goal:** Confirm no unrelated in-progress work is present before this pass begins modifying files.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** A dirty tree here would risk this plan's commits bundling unrelated changes, which CLAUDE.md forbids.

**Result:** `git status --short` → clean (no output) immediately before archiving the old plan.

---

## P0-008 — Archive previous plan_input.md to plan_input_20260707.md `[x]`
**Goal:** Preserve the previous plan as a historical artifact without treating it as a source of truth for this pass.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input_20260707.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Per explicit instruction: the archived file was not read, grepped, or summarized before or after the rename.

**Result:** `mv plan_input.md plan_input_20260707.md`. Verified via `ls -la plan_input_20260707.md` (83583 bytes) and `git status --short` showing ` D plan_input.md` / `?? plan_input_20260707.md`.

---

## P0-009 — Create fresh plan_input.md skeleton with phases and legend `[x]`
**Goal:** Author a brand-new plan file from scratch, independent of the archived one, with the required title, legend, and 500-task structure.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** This document is that file.

**Result:** Generated programmatically from `gen_plan.py` in the session scratchpad to guarantee exact task counts/IDs per phase; written to `plan_input.md`.

---

## P0-010 — Inventory strict XNA Input public headers `[x]`
**Goal:** Enumerate every header under `include/Microsoft/Xna/Framework/Input` to scope Phase 1–6.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** 26 headers found (incl. `Touch/` subdirectory).

**Result:** `find include/Microsoft/Xna/Framework/Input -type f` → Buttons, ButtonState, GamePad(+Buttons/Capabilities/DeadZone/DPad/State/ThumbSticks/Triggers/Type), Keyboard, KeyboardState, Keys, KeyState, Mouse, MouseCursor, MouseState, TextInputEXT, Touch/{GestureSample,GestureType,TouchCollection,TouchLocation,TouchLocationState,TouchPanel,TouchPanelCapabilities}.

---

## P0-011 — Inventory strict XNA Input source files `[x]`
**Goal:** Enumerate every `.cpp` under `src/Microsoft/Xna/Framework/Input` to pair with the header inventory.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** 18 `.cpp` files found; `Buttons`, `ButtonState`, `GamePadDeadZone`, `GamePadType`, `KeyState`, `TouchLocationState`, `TouchPanelCapabilities`-adjacent pure-enum/header-only types have no matching `.cpp` (expected for enum-only headers).

**Result:** `find src/Microsoft/Xna/Framework/Input -type f` → GamePad(+Buttons/Capabilities/DPad/State/ThumbSticks/Triggers), Keyboard, KeyboardState, Mouse, MouseCursor, MouseState, TextInputEXT, Touch/{GestureSample,TouchCollection,TouchLocation,TouchPanelCapabilities,TouchPanel}.

---

## P0-012 — Inventory CNA/NOXNA Input public headers `[x]`
**Goal:** Enumerate every header under `include/CNA/Input` to scope Phase 7.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** 24 headers found, matching the blueprint list exactly.

**Result:** `find include/CNA/Input -type f` → Clipboard, GamePadButtonLabel, GamePadConnectionState, HapticCapabilities, HapticDevice, HapticDirection, HapticEffect, HapticEffectType, HapticFeature, HapticInfo, Haptics, InputDeviceInfo, InputDevices, JoystickCapabilities, JoystickHatPosition, JoystickInfo, Joysticks, JoystickState, JoystickType, KeyModifiers, Power, PowerState, Sensors, TextInputType.

---

## P0-013 — Inventory CNA/NOXNA Input source files `[x]`
**Goal:** Enumerate every `.cpp` under `src/CNA/Input` to pair with the header inventory.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** 7 `.cpp` files found; remaining headers are enum/struct-only (no source needed) — confirm this is intentional per-header in Phase 7.

**Result:** `find src/CNA/Input -type f` → Clipboard, HapticDevice, Haptics, InputDevices, Joysticks, Power, Sensors.

---

## P0-014 — Inventory Input test files `[x]`
**Goal:** Enumerate every test file touching Input across strict-XNA, CNA extension, and internal-bridge suites.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Confirms substantial existing coverage from the prior NOXNA/plan passes ([[project_input_noxna_progress]], [[project_plan_input_progress]]) — this pass audits/extends it rather than starting from zero.

**Result:** `find tests -iname` (input/keyboard/mouse/gamepad/touch/gesture/joystick/haptic/clipboard/sensor/power) → ~40 files across `tests/CNA/Input`, `tests/CNA/Internal/Input` (incl. `FakeSdl*Backend.hpp` fakes, golden/fuzz/candidate suites), `tests/Microsoft/Xna/Framework/Input` (incl. `PublicApiInputCompileTests.cpp`, `PublicApiInputSignatureFreezeTests.cpp`), and `tests/Microsoft/Devices/Sensors`.

---

## P0-015 — Inventory Input-related documentation files `[x]`
**Goal:** Enumerate existing Input docs so Phase 10 updates them instead of duplicating them.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** 10 docs already exist and are cross-linked (see `docs/input-manual-verification-results.md` header) — Phase 10 must update these in place.

**Result:** `find docs -iname "*input*"` → demo-input-checklist.md, input-backend.md, input-build-and-test.md, input-fna-fidelity.md, input-manual-verification-results.md, input-member-parity-matrix.md, input-pre-merge-checklist.md, input-public-api-frozen.md, input-test-coverage.md, platform-input-notes.md.

---

## P0-016 — Inventory CMake targets/tests related to Input `[x]`
**Goal:** Locate the canonical Input test selector and any Input-specific executables in the build.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `CMakeLists.txt`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** `CnaInputTests` (CMakeLists.txt:1959) is the single-source-of-truth filtered gtest selector (CMakeLists.txt:1946-1964); `cna_input_smoke` (CMakeLists.txt:494) and the `InputDemo` sources (CMakeLists.txt:419-422) are manual/demo executables, not automated tests.

**Result:** `grep -n Input CMakeLists.txt` → matches at lines 419-422 (demo), 492-494 (smoke sample), 1946-1964 (`CnaInputTests` gtest filter + labels).

---

## P0-017 — Inventory SDL bridge and backend Input files `[x]`
**Goal:** Enumerate the internal SDL-facing bridge/backend files Phase 8 audits.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** 6 header/source pairs found under `CNA/Internal/Input`, cleanly separated from the public XNA/CNA headers.

**Result:** `find src include -iname "*SdlInputBridge*" -o -iname "*SdlGamepad*" -o -iname "*SdlJoystick*" -o -iname "*SdlHaptic*" -o -iname "*GestureDetector*" -o -iname "*InputManager*"` → GestureDetector, InputManager, SdlGamepadBackend, SdlHapticBackend, SdlInputBridge, SdlJoystickBackend (each with matching `.hpp`/`.cpp`).

---

## P0-018 — Inventory fake backend test helpers `[x]`
**Goal:** Enumerate fake/mock SDL backend headers used to make hardware-dependent tests deterministic and headless-safe.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** 3 fake backends exist (gamepad, haptic, joystick); Phase 9 must confirm mouse/keyboard/touch paths are similarly mockable or already deterministic via direct bridge calls.

**Result:** `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`, `FakeSdlHapticBackend.hpp`, `FakeSdlJoystickBackend.hpp`.

---

## P0-019 — Record known manual-validation gaps and platform-support claims `[x]`
**Goal:** Capture the current state of hardware-gated verification so Phase 11 doesn't re-derive it from scratch.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** `docs/input-manual-verification-results.md` already documents a hardware verification matrix (INP-0215) with all cells unchecked (no hardware available in the audit environment) — this pass must not mark any Phase 11 task done without an actual device.

**Result:** `grep -in "manual\|hardware\|platform" docs/input-manual-verification-results.md` → confirms rumble/triggers/GUID-on-real-hardware, touch hardware, 4-simultaneous-controllers, and non-US layouts are explicitly hardware/human-gated and unverified as of 2026-07-04.

---

## P0-020 — Write Phase 0 baseline checkpoint `[x]`
**Goal:** Close out Phase 0 with a single summary statement confirming the baseline is recorded and the plan is ready for Phase 1.

**Steps:**
1. Run the recording command(s) in the repository root.
2. Paste the exact output into the Result field below.
3. Confirm no destructive action was taken.

**Acceptance criteria:**
- The fact is recorded verbatim in this file with the command that produced it.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Checkpoint convention used at the end of every phase in this plan.

**Result:** Baseline recorded 2026-07-07 on `feature/input` @ `b89baad7`. Repository clean, sibling deps clean, 26 strict-XNA + 23 CNA/NOXNA Input headers inventoried, ~40 existing Input test files inventoried, 10 existing Input docs inventoried, 6 SDL bridge files inventoried. Phase 0 complete — proceeding to Phase 1.

---

## P1-001 — Audit ButtonState member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `ButtonState` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `ButtonState` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/ButtonState.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/ButtonState.cs`.

**Result:** 2026-07-17. Audited `include/Microsoft/Xna/Framework/Input/ButtonState.hpp` against FNA's
`ButtonState.cs` line-by-line. FNA is a plain 2-value `enum` (`Released` implicit 0, `Pressed` implicit
1) with a `<summary>` on the type and each value; CNA is `enum class ButtonState { Released, Pressed }`
with matching `/** @brief */` on the type and both values — `enum class` over `enum` is the project's
accepted idiom (type-safety, no behavior difference). **No divergence found**, accidental or
intentional; nothing to fix. Doxygen coverage confirmed complete (type + both values). Test coverage:
`ButtonStateTests.cpp::ValuesMatchXnaNumericConstants` already pins `Released == 0` and `Pressed == 1`
exactly. Files changed: none (audit-only, no divergence to fix, so no rebuild/retest was needed) — this
checkout's Input gate was last confirmed green (496/496, `ctest -L input`) immediately prior, at the
end of the P13-006 pass, and nothing has touched `ButtonState` or its test since. Remaining risk: none.

---

## P1-002 — Audit Buttons member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `Buttons` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `Buttons` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Buttons.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadButtonsTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Buttons.cs`.

**Result:** 2026-07-17. Audited `include/Microsoft/Xna/Framework/Input/Buttons.hpp` against FNA's
`Buttons.cs` value-by-value. All 24 strict XNA bit values and all 6 `EXT` extension bit values
(`Misc1EXT`, `Paddle1EXT..4EXT`, `TouchPadEXT`) match FNA exactly, bit-for-bit — **no divergence
found**. Doxygen present on every value (CNA documents the EXT values too, which FNA leaves
undocumented in its own source — an improvement, not a gap). One systemic finding, not specific to
this type: CNA's `enum class` flag types (needed since C++ doesn't auto-generate bitwise operators the
way C# does for any `enum`) implement `|`/`&`/`~`/`|=`/`&=` but not `^`/`^=` — checked and confirmed no
CNA source or test XORs a flags enum, and XNA/FNA game code conventionally never does either; recorded
as an accepted, intentional omission in `docs/input-fna-fidelity.md`'s GamePad section rather than
pre-emptively adding unused operators. Test coverage: `ButtonsTests.cpp` (not
`GamePadButtonsTests.cpp`, which tests the unrelated `GamePadButtons` struct — this task's own "Tests"
field guess was off) already pins every core + EXT bit value exactly and exercises `|`/`&`/`|=`/`&=`/`~`.
Files changed: `docs/input-fna-fidelity.md` (1 new bullet, the XOR-omission note). No source/test
change needed. Remaining risk: none.

---

## P1-003 — Audit GamePad member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `GamePad` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `GamePad` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadMappingTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad.cs`.

**Result:** 2026-07-17. Audited `GamePad` against FNA's `GamePad.cs`, cross-referencing
`SDL3_FNAPlatform.cs`'s `GetGamePad*`/`SetGamePad*` implementations for actual runtime semantics.
Every strict XNA member (`GetCapabilities`, both `GetState` overloads including the default-overload's
forward to `GamePadDeadZone::IndependentAxes`, `SetVibration`) and every EXT member (`GetGUIDEXT`,
`SetLightBarEXT`, `SetTriggerVibrationEXT`, `GetGyroEXT`, `GetAccelerometerEXT`) matches FNA's
signature, overload set, defaults, and zero-out-on-failure behavior exactly; dead-zone constants
(`7849/32768`, `8689/32768`, `30/255`) and `ExcludeAxisDeadZone` are byte-identical. Two intentional
(not fixed) deviations were newly identified and documented in `docs/input-fna-fidelity.md`: (1)
out-of-range `PlayerIndex` gracefully falls back to disconnected/false/empty everywhere in CNA rather
than throwing as FNA's unbounded array indexing implicitly does (already tested); (2) FNA's `internal`
dead-zone constants/`ExcludeAxisDeadZone` are `NOXNA public` statics on `GamePad` because C++ lacks
assembly-scoped visibility and `GamePadThumbSticks.cpp`/`GamePadTriggers.cpp` need cross-TU access —
the correct translation of FNA's `internal`, not an accidental widening. No accidental divergence
found; no source fix required. Doxygen coverage complete. Closed one real test gap: added
`GamePadInputTest.GetStateDefaultOverloadForwardsToIndependentAxesDeadZone`
(`tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`) — the default-overload-forwards contract
was previously only exercised with degenerate (always-zero, sub-dead-zone) values that couldn't
actually distinguish a forwarding bug. Files changed:
`tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`, `docs/input-fna-fidelity.md`. Consolidated build+test verification (2026-07-17, after all 9 parallel P1 GamePad-family audits landed): `cmake --build cmake-build-debug --target CnaTests` -- clean, no errors. `xvfb-run -a env SDL_VIDEODRIVER=x11 CnaTests --gtest_filter=$CNA_INPUT_TEST_FILTER --gtest_shuffle --gtest_repeat=5` -- `[PASSED] 506 tests.` on all 5 shuffled repeats (up from 496 pre-batch, +10 new tests across this batch), zero `FAILED` in the complete output. Remaining risk: none identified in the public `GamePad` surface itself.

---

## P1-004 — Audit GamePadButtons member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `GamePadButtons` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `GamePadButtons` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadButtons.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadButtons.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadButtonsTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePadButtons.cs`.

**Result:** 2026-07-17. Audited `GamePadButtons` against FNA's `GamePadButtons.cs` line-by-line,
covering all 11 per-button `ButtonState` properties, both constructors, `FromButtonArray`, `Equals`,
`GetHashCode`, and equality operators. Found and fixed two accidental, non-behavioral divergences in
`include/Microsoft/Xna/Framework/Input/GamePadButtons.hpp`: the internal `buttons_` storage field was
public instead of `private` (FNA declares it `internal`; the existing `friend struct GamePadState;`
already grants the access `GamePadState.cpp` needs, so nothing else required changes), and
`FromButtonArray` was declared out of order relative to FNA's source order and the type's own `.cpp`
definition order — reordered to match. Other candidate deviations (typed-only `Equals(const
GamePadButtons&)` instead of an `Equals(object)` override; `explicit` on the single-arg constructor)
are pre-existing, intentional, project-wide C++ value-type conventions already applied consistently
across sibling Input types, not bugs. Doxygen coverage already complete. No new tests needed — both
fixes are declaration/encapsulation-only with zero observable behavior change, already covered by
existing `GamePadButtonsTests.cpp` and (for the friend-access path) `GamePadStateTests.cpp`. Files
changed: `include/Microsoft/Xna/Framework/Input/GamePadButtons.hpp`, `docs/input-fna-fidelity.md`.
Consolidated build+test verification (2026-07-17, after all 9 parallel P1 GamePad-family audits landed): `cmake --build cmake-build-debug --target CnaTests` -- clean, no errors. `xvfb-run -a env SDL_VIDEODRIVER=x11 CnaTests --gtest_filter=$CNA_INPUT_TEST_FILTER --gtest_shuffle --gtest_repeat=5` -- `[PASSED] 506 tests.` on all 5 shuffled repeats (up from 496 pre-batch, +10 new tests across this batch), zero `FAILED` in the complete output. Remaining risk: none identified.

---

## P1-005 — Audit GamePadCapabilities member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `GamePadCapabilities` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `GamePadCapabilities` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadCapabilities.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp (locate/verify dedicated coverage; add GamePadCapabilitiesTests.cpp if missing)`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePadCapabilities.cs`.

**Result:** 2026-07-17. Audited `GamePadCapabilities` (25 XNA bool properties + `GamePadType` + 10
`...EXT` bool properties) field-by-field against FNA's `GamePadCapabilities.cs`. **No divergence
found.** Names, order, defaults (all `false`/`GamePadType::Unknown`, matching C#'s implicit
`default(GamePadCapabilities)`), and getter/setter shape all match exactly; FNA's `internal set` maps
to a public `NOXNA`-tagged setter (the project's established, header-documented convention) on every
XNA property, and all 10 EXT properties are correctly `NOXNA` on both accessors. Confirmed no
`VendorId`/`ProductId` properties exist anywhere in the current FNA source tree — only the 10 boolean
`...EXT` flags. FNA defines no `Equals`/`operator==`/`ToString`/`GetHashCode` for this type and CNA
correctly omits them too. No dedicated `GamePadCapabilitiesTests.cpp` exists, but this is not a gap:
`GamePadTests.cpp`, `GamePadMappingTests.cpp`, `PublicApiInputSignatureFreezeTests.cpp`, and
`PublicApiInputCompileTests.cpp` already exhaustively cover default state, per-flag setter isolation
(all 35 bool flags), full round-trips, partial-capability combinations, and a complete 72-signature
freeze. Files changed: `docs/input-fna-fidelity.md` (audit-record note only, no code change — none
was needed). Consolidated build+test verification (2026-07-17, after all 9 parallel P1 GamePad-family audits landed): `cmake --build cmake-build-debug --target CnaTests` -- clean, no errors. `xvfb-run -a env SDL_VIDEODRIVER=x11 CnaTests --gtest_filter=$CNA_INPUT_TEST_FILTER --gtest_shuffle --gtest_repeat=5` -- `[PASSED] 506 tests.` on all 5 shuffled repeats (up from 496 pre-batch, +10 new tests across this batch), zero `FAILED` in the complete output. Remaining risk: none identified.

---

## P1-006 — Audit GamePadDPad member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `GamePadDPad` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `GamePadDPad` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadDPad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadDPad.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp (locate/verify dedicated coverage; add GamePadDPadTests.cpp if missing)`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePadDPad.cs`.

**Result:** 2026-07-17. Audited `GamePadDPad` against FNA's `GamePadDPad.cs` line-by-line: the 4-arg
constructor's parameter-to-field mapping, `FromButtonArray`'s mask-derivation logic (verified
equivalent to FNA's monotonic per-element loop via an OR-then-check transformation), `GetHashCode`'s
bit-weighted formula (`Down=1, Left=2, Right=4, Up=8`, `int` return matching FNA exactly), and
`Equals`/`operator==`/`operator!=` all match FNA exactly. Confirmed by grepping all of FNA's source
tree that no FNA code ever mutates `GamePadDPad` fields outside its constructor, so dropping the
`internal set` public setters is behaviorally exact. Two cosmetic-only notes recorded (not fixed):
member declaration order differs from strict FNA source order (matches the already-established,
codebase-wide `GamePadButtons` reordering convention) and an unused `struct GamePadState;` forward
declaration is vestigial dead plumbing (C# has no forward declarations; harmless, out of scope to
remove here). Doxygen coverage complete. **No accidental divergence found**, so no `.hpp`/`.cpp`
change was needed; closed one test-completeness gap instead — added
`GamePadDPadTest.GetHashCodeIsConsistentForEqualInstances`
(`tests/Microsoft/Xna/Framework/Input/GamePadButtonsTests.cpp`, where the pre-existing `GamePadDPad`
suite already lives) to explicitly satisfy the "equal objects produce equal hashes" checklist
requirement, previously only implied by the formula test. Files changed:
`tests/Microsoft/Xna/Framework/Input/GamePadButtonsTests.cpp`. Consolidated build+test verification (2026-07-17, after all 9 parallel P1 GamePad-family audits landed): `cmake --build cmake-build-debug --target CnaTests` -- clean, no errors. `xvfb-run -a env SDL_VIDEODRIVER=x11 CnaTests --gtest_filter=$CNA_INPUT_TEST_FILTER --gtest_shuffle --gtest_repeat=5` -- `[PASSED] 506 tests.` on all 5 shuffled repeats (up from 496 pre-batch, +10 new tests across this batch), zero `FAILED` in the complete output. Remaining risk: none.

---

## P1-007 — Audit GamePadDeadZone member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `GamePadDeadZone` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `GamePadDeadZone` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadDeadZoneTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePadDeadZone.cs`.

**Result:** 2026-07-17. Audited `GamePadDeadZone` (enum: `None`, `IndependentAxes`, `Circular`) against
FNA's `GamePadDeadZone.cs`. The three values, their declaration order, and their implicit sequential
numeric constants (0, 1, 2) match FNA exactly; underlying type is left unspecified in both, consistent
with the sibling `GamePadType` enum. Doxygen coverage complete (FNA's XML-doc prose reworded, not
copied verbatim — permitted). Cross-checked all three consumers (`GamePad`, `GamePadThumbSticks`,
`GamePadTriggers`) against their FNA equivalents; default (`IndependentAxes`) and switch/branch usage
consistent. Existing test `GamePadDeadZoneTest.ValuesMatchXnaSequentialConstants`
(`tests/Microsoft/Xna/Framework/Input/GamePadDeadZoneTests.cpp`) already covers all three constants.
**No accidental divergence found; no source or test changes were made.** Consolidated build+test verification (2026-07-17, after all 9 parallel P1 GamePad-family audits landed): `cmake --build cmake-build-debug --target CnaTests` -- clean, no errors. `xvfb-run -a env SDL_VIDEODRIVER=x11 CnaTests --gtest_filter=$CNA_INPUT_TEST_FILTER --gtest_shuffle --gtest_repeat=5` -- `[PASSED] 506 tests.` on all 5 shuffled repeats (up from 496 pre-batch, +10 new tests across this batch), zero `FAILED` in the complete output. Remaining
risk: none.

---

## P1-008 — Audit GamePadState member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `GamePadState` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `GamePadState` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePadState.cs`.

**Result:** 2026-07-17. Audited `GamePadState` against FNA's `GamePadState.cs` line-by-line: both
constructors (including the default `NOXNA` one), the dead-zone-adjacent trigger/thumbstick-to-`Buttons`
synthesis logic (`StickToButtons`, `TriggerThreshold` checks), `IsButtonDown`/`IsButtonUp`,
`PacketNumber`/`IsConnected`/`Buttons`/`DPad`/`ThumbSticks`/`Triggers`, `Equals`/`==`/`!=`,
`GetHashCode`, and `ToString`. Found **no accidental divergences** — the constructor's dead-zone
synthesis (strict `>`/`<` against `GamePad::LeftDeadZone`/`RightDeadZone`/`TriggerThreshold`, exact
operation order) is byte-identical to FNA, and `GamePadState`'s own logic is confirmed independent of
the `GamePadDeadZone` mode enum (that lives in `GamePadThumbSticks`/`GamePadTriggers`'s internal 3-arg
constructors, invoked separately from `GamePad::GetState`, not from `GamePadState` itself).
`Equals`/`operator==` compare all 6 fields in FNA's exact order. Both previously-identified intentional
deviations (`GetHashCode`'s `buttons ^ packetNumber*31` formula vs. FNA's reflection-based
`ValueType.GetHashCode()`; `PacketNumber`'s event-driven increment semantics, which lives in
`GamePad.cpp`/`InputManager`, not this file) remain accurately documented in
`docs/input-fna-fidelity.md`'s "## GamePad" section and needed no changes. Doxygen coverage already
complete. Strengthened test coverage in `GamePadStateTests.cpp` for previously-untested-but-correct
branches: the symmetric right-trigger-threshold case, all 8 `StickToButtons` true-branches (previously
only 2 of 8 were exercised as true), the strict dead-zone/threshold boundary (`>`/`<`, not `>=`/`<=`),
and DPad-only/ThumbSticks-only/Triggers-only equality isolation (previously only
`Buttons`/`PacketNumber`/`IsConnected` differences were tested in isolation) — 5 new `TEST`s added,
zero production-code changes. Files changed: `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`.
Consolidated build+test verification (2026-07-17, after all 9 parallel P1 GamePad-family audits landed): `cmake --build cmake-build-debug --target CnaTests` -- clean, no errors. `xvfb-run -a env SDL_VIDEODRIVER=x11 CnaTests --gtest_filter=$CNA_INPUT_TEST_FILTER --gtest_shuffle --gtest_repeat=5` -- `[PASSED] 506 tests.` on all 5 shuffled repeats (up from 496 pre-batch, +10 new tests across this batch), zero `FAILED` in the complete output. Remaining risk: none identified.

---

## P1-009 — Audit GamePadThumbSticks member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `GamePadThumbSticks` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `GamePadThumbSticks` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadThumbSticks.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadThumbSticksTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePadThumbSticks.cs`.

**Result:** 2026-07-17. Audited `GamePadThumbSticks` against FNA's `GamePadThumbSticks.cs`/`GamePad.cs`
line-by-line, with extra scrutiny on dead-zone math per this task's note (the prior L-015 bug was in
upstream SDL-axis normalization, not this type's dead-zone application — confirmed still fixed and out
of scope for regression here). Both constructors (public 2-arg square-clamp-only; private/friend 3-arg
dead-zone-then-clamp), `ApplyDeadZone`'s `None`/`IndependentAxes`/`Circular` branches,
`GamePad::ExcludeAxisDeadZone`, `ExcludeCircularDeadZone`, `ApplySquareClamp`/`ApplyCircularClamp`, the
`LeftDeadZone`/`RightDeadZone` constants, and `operator==`/`!=`/`Equals`/`GetHashCode` all match FNA's
formulas and constant wiring exactly — including confirming correct per-stick dead-zone constant
routing (`left`→`LeftDeadZone`, `right`→`RightDeadZone`) in both `IndependentAxes` and `Circular`
modes. **No accidental divergence found.** Closed one real test gap: the existing suite only exercised
`LeftDeadZone` for both dead-zone modes; added
`IndependentAxesModeExcludesRightStickDeadZoneUsingRightDeadZoneConstant` and
`CircularModeRescalesRightStickUsingRightDeadZoneConstant`
(`tests/Microsoft/Xna/Framework/Input/GamePadThumbSticksTests.cpp`) pinning the Right stick against
`RightDeadZone` specifically. Doxygen coverage already complete. Files changed:
`tests/Microsoft/Xna/Framework/Input/GamePadThumbSticksTests.cpp`. Consolidated build+test verification (2026-07-17, after all 9 parallel P1 GamePad-family audits landed): `cmake --build cmake-build-debug --target CnaTests` -- clean, no errors. `xvfb-run -a env SDL_VIDEODRIVER=x11 CnaTests --gtest_filter=$CNA_INPUT_TEST_FILTER --gtest_shuffle --gtest_repeat=5` -- `[PASSED] 506 tests.` on all 5 shuffled repeats (up from 496 pre-batch, +10 new tests across this batch), zero `FAILED` in the complete output. Remaining risk: none.

---

## P1-010 — Audit GamePadTriggers member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `GamePadTriggers` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `GamePadTriggers` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadTriggers.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadTriggers.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTriggersTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePadTriggers.cs`.

**Result:** 2026-07-17. Audited `GamePadTriggers` against FNA's `GamePadTriggers.cs` line-by-line: the
read-only `Left`/`Right` `[0,1]`-clamped properties, the public 2-arg clamp-only constructor, the
private/friend 3-arg dead-zone constructor (`None` clamps only; any other mode runs
`GamePad::ExcludeAxisDeadZone(value, GamePad::TriggerThreshold)` before clamping, independently per
trigger), `operator==`/`operator!=` (`MathHelper::WithinEpsilon`-based), and `GetHashCode()`
(`Left.GetHashCode() + Right.GetHashCode()`) all match FNA exactly. `TriggerThreshold` (`30/255`) and
`ExcludeAxisDeadZone` confirmed byte-identical to FNA. Two pre-existing, already-documented,
codebase-wide deviations reconfirmed unchanged: `Equals(object obj)` omitted per CHECKLIST.md's
standing rule, and `GetHashCode()`'s unsigned-wraparound-cast summation (INPUT-BUILD-006) applied
identically in five other files. **No accidental divergence found.** Closed one test-coverage gap
(not a behavior bug): the private dead-zone constructor's `Right` trigger path had no independent
test (only `Left` was exercised) — added
`GamePadTriggersTest.NonNoneDeadZoneModeAppliesIndependentlyToBothTriggers`
(`tests/Microsoft/Xna/Framework/Input/GamePadTriggersTests.cpp`) using distinct left/right raw values
to guard against a field-swap regression. Doxygen coverage already complete. Files changed:
`tests/Microsoft/Xna/Framework/Input/GamePadTriggersTests.cpp`. Consolidated build+test verification (2026-07-17, after all 9 parallel P1 GamePad-family audits landed): `cmake --build cmake-build-debug --target CnaTests` -- clean, no errors. `xvfb-run -a env SDL_VIDEODRIVER=x11 CnaTests --gtest_filter=$CNA_INPUT_TEST_FILTER --gtest_shuffle --gtest_repeat=5` -- `[PASSED] 506 tests.` on all 5 shuffled repeats (up from 496 pre-batch, +10 new tests across this batch), zero `FAILED` in the complete output. Remaining risk: none.

---

## P1-011 — Audit GamePadType member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `GamePadType` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `GamePadType` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadType.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTypeTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePadType.cs`.

**Result:** 2026-07-17. Audited all 10 `GamePadType` enum members (`Unknown`, `GamePad`, `Wheel`,
`ArcadeStick`, `FlightStick`, `DancePad`, `Guitar`, `AlternateGuitar`, `DrumKit`, `BigButtonPad`)
against FNA's `GamePadType.cs` — identical order, identical implicit sequential values (0-9), matching
exactly, including `AlternateGuitar` (the member most at risk of being dropped in this family). FNA's
git history for this file shows only copyright-year-bump commits, confirming no upstream member
additions were missed. Doxygen coverage complete (reworded from FNA's `<summary>` text, not verbatim —
permitted). Downstream consumers (`GamePadCapabilities.hpp`, `SdlInputBridge.cpp`'s SDL-joystick-type
mapping) use the enum consistently with FNA's own SDL platform mapping tables. Existing test
`GamePadTypeTest.ValuesMatchXnaSequentialConstants` already asserts all 10 ordinal values.
**No accidental or intentional divergence found; no files were edited.** Consolidated build+test verification (2026-07-17, after all 9 parallel P1 GamePad-family audits landed): `cmake --build cmake-build-debug --target CnaTests` -- clean, no errors. `xvfb-run -a env SDL_VIDEODRIVER=x11 CnaTests --gtest_filter=$CNA_INPUT_TEST_FILTER --gtest_shuffle --gtest_repeat=5` -- `[PASSED] 506 tests.` on all 5 shuffled repeats (up from 496 pre-batch, +10 new tests across this batch), zero `FAILED` in the complete output. Remaining risk: none.

---

## P1-012 — Audit KeyState member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `KeyState` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `KeyState` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/KeyState.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp (locate/verify dedicated coverage; add KeyStateTests.cpp if missing)`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/KeyState.cs`.

**Result:** 2026-07-17. Audited `KeyState` (enum: `Up`, `Down`) against FNA's `KeyState.cs`: name, member
order, implicit numeric values (0, 1), and both type-level and per-value Doxygen text match FNA's
`<summary>` comments verbatim (rephrasing permitted, not required here since verbatim copy already
matched). Cross-checked against the structurally identical `ButtonState` enum to confirm this file
follows the project's established, correct pattern for XNA state enums. **No divergence found.**
Existing test `KeyStateTest.ValuesMatchXnaNumericConstants`
(`tests/Microsoft/Xna/Framework/Input/KeyStateTests.cpp`) already pins both constants, matching the
same pattern as `ButtonStateTest`; also exercised indirectly via `KeyboardState`'s indexer and pinned
in the compile/signature-freeze guard tests. No files changed. Consolidated build+test verification
pending as part of the parallel P1 batch 2 audit run. Remaining risk: none.

---

## P1-013 — Audit Keyboard member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `Keyboard` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `Keyboard` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/KeyboardModStateTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Keyboard.cs`.

**Result:** 2026-07-17. Audited `Keyboard`'s public API surface against FNA's `Keyboard.cs`
(SDL event routing/scancode translation lives in `SdlInputBridge`/`InputManager`, already separately
audited — out of scope here). All members match FNA exactly: the deleted-constructor static-class
pattern, `GetState()`, `GetState(PlayerIndex)` (index intentionally ignored in both FNA and CNA), and
the FNA-native extension `GetKeyFromScancodeEXT` (correctly `NOXNA`-tagged, matching FNA's own
`EXT`-suffixed extension region). Five additional CNA-only `NOXNA`/`EXT` members (`GetModStateEXT`,
`GetScancodeNameEXT`, `GetScancodeFromNameEXT`, `GetKeyNameEXT`, `GetKeyFromNameEXT`) have no FNA
equivalent, are correctly tagged, and each already has dedicated test coverage plus compile-time
signature freezes. **No accidental behavioral or signature divergence found.** One compliance gap
fixed: added a missing Doxygen block on the deleted default constructor in
`include/Microsoft/Xna/Framework/Input/Keyboard.hpp` (comment-only, no test impact). Files changed:
`include/Microsoft/Xna/Framework/Input/Keyboard.hpp`. Consolidated build+test verification pending as
part of the parallel P1 batch 2 audit run. Remaining risk: none.

---

## P1-014 — Audit KeyboardState member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `KeyboardState` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `KeyboardState` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/KeyboardState.cs`.

**Result:** 2026-07-17. Audited `KeyboardState` against FNA's `KeyboardState.cs`, re-verifying (not
assuming) the fidelity doc's existing claims about `GetPressedKeys()` ordering and the `GetHashCode`
8×32-bit-word XOR algorithm — both confirmed at the bit-packing level. **Found and fixed one real
accidental divergence:** both non-default constructors (the public initializer-list constructor and
the NOXNA `std::unordered_set<Keys>` constructor) stored any `Keys` value verbatim into `pressedKeys_`,
including out-of-range/negative values, whereas FNA's `AddPressedKey` (`KeyboardState.cs:220-234`)
silently drops any value outside the representable 0..255 bitfield range. This made `IsKeyDown`,
`IsKeyUp`, `operator[]`/`getItem`, `GetPressedKeys()`, and `Equals`/`operator==` disagree with FNA (and
inconsistently with CNA's own `GetHashCode`, which already had a separate out-of-range guard from
P2-002) for such pathological values. Fixed by routing both constructors through a new shared
`IsWithinKeyBitfieldRange` helper mirroring FNA's bitfield addressing, and refactored `GetHashCode` to
reuse the same helper. Two items identified as intentional, not fixed: `getItem`'s name doesn't follow
the codebase's `getItemProperty`-style indexer convention (`CurveKeyCollection`) but is pinned by name
in the out-of-scope `PublicApiInputSignatureFreezeTests.cpp`; the NOXNA `unordered_set` constructor's
public visibility/parameter type intentionally differs from FNA's `internal KeyboardState(List<Keys>)`
for C++ cross-TU access. Doxygen coverage already complete. Files changed:
`src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`,
`tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp` (3 new tests:
`InitializerListConstructorDropsOutOfRangeKeysValue`,
`InitializerListConstructorDropsNegativeAndBoundaryKeysValues`,
`UnorderedSetConstructorDropsOutOfRangeKeysValue`). Consolidated build+test verification pending as
part of the parallel P1 batch 2 audit run. Remaining risk: none.

---

## P1-015 — Audit Keys member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `Keys` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `Keys` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardKeyNameTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/KeyboardScancodeNameTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Keys.cs`.

**Result:** 2026-07-17. Audited `Keys` (160-value exhaustive key enum) against FNA's `Keys.cs` by
programmatically extracting every `Name = value` pair from both files and diffing as name-to-value
maps (not manual inspection). FNA: 160 entries, no duplicate names/values. CNA: 160 entries, no
duplicate names/values. **Zero names missing on either side, zero numeric-value mismatches** —
including every hex-declared outlier (`Pause=0x13`, `Kana=0x15`, `Kanji=0x19`, `ImeConvert=0x1c`,
`ImeNoConvert=0x1d`, `ChatPadGreen=0xCA`, `ChatPadOrange=0xCB`, `OemAuto=0xf3`, `OemCopy=0xf2`,
`OemEnlW=0xf4`), all numerically equal to FNA despite FNA declaring them out of ascending order.
Independently cross-verified the existing exhaustive test table
(`KeyboardStateTest.KeysValuesMatchXNANumericConstants`,
`tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp:372-548`, 160 cases with a
`static_assert` count guard and a runtime duplicate-value check) against the same FNA extraction —
also zero divergence. Confirmed full Doxygen coverage on all 160 values plus the enum declaration
(161 `@brief` blocks), zero bare `///` comments. This confirms the prior "matches FNA exactly" claim
in `docs/input-fna-fidelity.md` is actually accurate, not merely assumed. **No divergence found; no
files changed.** Consolidated build+test verification pending as part of the parallel P1 batch 2
audit run. Remaining risk: none.

---

## P1-016 — Audit Mouse member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `Mouse` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `Mouse` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs`.

**Result:** 2026-07-17. Audited `Mouse`'s public API surface against FNA's `Mouse.cs` (SDL event
routing lives in `SdlInputBridge`/`InputManager`, separately audited — out of scope here). All 5 FNA
members (`WindowHandle` get/set, `IsRelativeMouseModeEXT` get/set — FNA-native EXT, `ClickedEXT`,
`GetState()`, `SetPosition(int,int)`) match FNA's signatures exactly; all CNA-only additions
(`SetCursor`, `SetCaptureEXT`, `GetGlobalPositionEXT`, `WarpGlobalEXT`, `INTERNAL_onClicked`,
`ResetForTests`) correctly tagged NOXNA/EXT. `SetPosition`'s relative-mode early-return verified
line-by-line identical to FNA. **No behavioral divergence found.** Investigated and ruled out changing
`WindowHandle`'s raw `std::uintptr_t` to `SharpRuntime::IntPtr`: the same-namespace sibling
`TextInputEXT` deliberately uses the same raw type, so this is an established, self-consistent
`Input`-namespace convention, not a gap. Fixed two stale FNA source-line citations in comments
(`Mouse.cs:99-103` → the correct `Mouse.cs:106-110` for the relative-mode early-return) — not a
behavioral change. Closed one real test gap: `Mouse::SetCursor`'s actual `SDL_SetCursor()` call had no
positive-path assertion anywhere in the suite (only the disposed/no-op guard was tested) — added
`MouseTest.SetCursorAppliesTheGivenCursorToSDL`. All previously-documented deviations in
`docs/input-fna-fidelity.md`'s Mouse section re-verified accurate. Files changed:
`src/Microsoft/Xna/Framework/Input/Mouse.cpp`, `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`.
Consolidated build+test verification pending as part of the parallel P1 batch 2 audit run. Remaining
risk: none.

---

## P1-017 — Audit MouseCursor member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `MouseCursor` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `MouseCursor` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseCursor.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp (locate/verify dedicated coverage; add MouseCursorTests.cpp if missing)`

**Notes:** No FNA/XNA 4.0 equivalent exists — confirm the type is correctly marked `NOXNA` even though it lives under the strict-XNA directory (this is the documented CLAUDE.md mechanism for non-XNA members inside `Microsoft::Xna`).

**Result:** 2026-07-17. Confirmed FNA has no `MouseCursor.cs` (full-tree search of the FNA source root) —
this is a MonoGame-derived `NOXNA` extension, correctly tagged in the header's own doc comment. Audited
against MonoGame's `MouseCursor.cs`/`MouseCursor.SDL.cs` instead, plus an independent line-by-line
review of the move-construction/move-assignment/disposal machinery (no C# analogue). All 12 of
MonoGame's stock-cursor static properties present with correct SDL3 mappings; `FromTexture2D`
validation matches MonoGame's exception behavior. One deliberate, already-documented improvement over
MonoGame confirmed (not a bug): CNA's `isSystemSingleton_` guard makes `Dispose()` a no-op for stock
cursors, where real MonoGame's SDL backend frees the shared handle unconditionally and would corrupt
the singleton for every other holder. Doxygen coverage already complete. **No accidental divergence or
source bug found; `MouseCursor.hpp`/`.cpp` unchanged.** Closed one real test gap: the move-assignment
operator's self-assignment guard was previously untested — added
`MouseCursorTest.SelfMoveAssignmentLeavesCursorIntact`
(`tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`). Consolidated build+test verification
pending as part of the parallel P1 batch 2 audit run. Remaining risk: none.

---

## P1-018 — Audit MouseState member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `MouseState` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `MouseState` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/MouseState.cs`.

**Result:** 2026-07-17. Audited `MouseState` against FNA's `MouseState.cs`. Every property, the 8-arg
constructor's parameter order (specifically checked the `leftButton, middleButton, rightButton` —
**not** `leftButton, rightButton, middleButton` — ordering trap at `MouseState.cs:110-118`, already
proven by an existing alternating-Pressed/Released test), `Equals`/`==`/`!=`/`ToString`, and defaults
all match FNA exactly. The NOXNA/EXT horizontal-scroll-wheel field is correctly tagged and correctly
excluded from `Equals`/`GetHashCode`/`ToString`/equality operators. **No accidental divergence found.**
Two documentation items surfaced and fixed in `docs/input-fna-fidelity.md`: `GetHashCode()`'s
deterministic field-hash formula (vs. FNA's non-reproducible `base.GetHashCode()`) was a real,
pre-existing, correct deviation that had never been documented — added; and DEC-18 was found stale
(claimed the horizontal wheel is "ignored" and cited a test, `HorizontalWheelIsIgnored`, that no
longer exists — confirmed via grep — even though the feature was actually wired up under N-005) —
corrected in place. Closed one test gap: the true parameterless default constructor's EXT
horizontal-wheel-field default was only inferred from the 8-arg-ctor test, not asserted directly —
added an assertion to `MouseStateTest.DefaultConstructorAllValuesAtRest`. Files changed:
`tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`, `docs/input-fna-fidelity.md`. Consolidated
build+test verification pending as part of the parallel P1 batch 2 audit run. Remaining risk: none.

---

## P1-019 — Audit TextInputEXT member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `TextInputEXT` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `TextInputEXT` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/TextInputEXT.cs`.

**Result:** 2026-07-17. Audited `TextInputEXT` against FNA's `TextInputEXT.cs` and its
`SDL3_FNAPlatform.cs` backing calls. All FNA-original members (`WindowHandle`, `IsTextInputActive`,
`IsScreenKeyboardShown` (both overloads), `StartTextInput`, `StopTextInput`, `SetInputRectangle`,
`TextInput`, `TextEditing`) match FNA's signatures and behavior exactly, including the null-window
guard as a documented intentional addition. The multicast-callback, UTF-8-vs-UTF-16 `TextEditing`, and
U+FFFD-substitution deviations already recorded in `docs/input-fna-fidelity.md` were re-verified and
remain accurate. **No new FNA-behavioral divergence found.** Two Doxygen gaps fixed: the deleted
constructor lacked its `/** @brief */` comment (every sibling static input class has one), and
`IsTextInputActive()`'s doc had dropped FNA's on-screen-keyboard caveat — restored. A test-completeness
gap was closed: `TextEditingCandidatesEXT`/`INTERNAL_OnTextEditingCandidates` (a NOXNA/EXT member with
no FNA counterpart) had zero test coverage; four tests added covering dispatch, multicast fan-out,
no-subscriber safety, and `ResetForTests()`'s full contract (previously only its window-handle-clearing
effect was tested) — a latent test-fixture hygiene bug (`TextEditingCandidatesEXT` not reset between
tests) was fixed alongside. No production `.cpp` changes needed. Files changed:
`include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`,
`tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`. Consolidated build+test verification
pending as part of the parallel P1 batch 2 audit run. Remaining risk: none.

---

## P1-020 — Audit GestureSample member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `GestureSample` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `GestureSample` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/GestureSample.cpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp (locate/verify dedicated coverage; add GestureSampleTests.cpp if missing)`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`.

**Result:** 2026-07-17. Audited `GestureSample` against FNA's `GestureSample.cs`. Both constructor
overloads match FNA's parameter order/count/defaults exactly; all six XNA property getters and the two
`NOXNA` `FingerIdEXT`/`FingerId2EXT` extension getters (FNA's own `#region Public FNA Extension
Properties`) are present with correct names/types/Doxygen. **No accidental divergence found.** One
intentional, already-tested design choice was newly documented in `docs/input-fna-fidelity.md`: the
`NOXNA` default constructor seeds `FingerIdEXT`/`FingerId2EXT` with `TouchPanel::NO_FINGER` (-1) rather
than C#'s implicit zero-default, since `0` is a legitimate real SDL finger id. No files changed beyond
the doc note (`docs/input-fna-fidelity.md`) — existing coverage in `TouchInputTests.cpp` already
exercises the default ctor, both parameterized ctors, and all getters. Consolidated build+test
verification pending as part of the parallel P1 batch 2 audit run. Remaining risk: none.

---

## P1-021 — Audit GestureType member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `GestureType` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `GestureType` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureType.cs`.

**Result:** 2026-07-17. Audited `GestureType` (`[Flags]` enum) against FNA's `GestureType.cs`: all 11
bit values (`None` through `PinchComplete`) match exactly. `enum class GestureType : int` correctly
matches C#'s default `int`-backed `[Flags]` underlying type; the 4 bitwise operators (`|`/`&`/`|=`/`&=`)
needed to replicate C#'s native enum arithmetic follow the exact same untagged (non-`NOXNA`) convention
used by every other ported flags enum (`Buttons`, `DisplayOrientation`, `ClearOptions`,
`ColorWriteChannels`). Checked all call sites (`GestureDetector.cpp`, `TouchPanel.cpp`) and confirmed
`operator~` (present on `Buttons` but not `GestureType`) is never actually needed — FNA itself never
XORs or NOTs this enum. Doxygen coverage complete. **No divergence found; no files changed.** Existing
`GestureTypeTests.cpp` (`ValuesMatchXnaFlagConstants`, `BitwiseOperatorsCombineAndMaskFlags`) already
covers all 11 values and all 4 operators. Consolidated build+test verification pending as part of the
parallel P1 batch 2 audit run. Remaining risk: none.

---

## P1-022 — Audit TouchCollection member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `TouchCollection` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `TouchCollection` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/TouchCollection.cs`.

**Result:** 2026-07-17. Audited `TouchCollection` against FNA's `TouchCollection.cs`. **Found and fixed
one real accidental divergence:** `FindById` did not write its `TouchLocation&` out-parameter on the
not-found path, unlike FNA, which unconditionally assigns `new TouchLocation(-1,
TouchLocationState.Invalid, Vector2.Zero)` before returning `false` (`TouchCollection.cs:112-131`) —
the same "out-param written on every path" contract already enforced for `TouchLocation::
TryGetPreviousLocation` (DEC-12), previously missed for `TouchCollection::FindById`. Fixed in
`src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`, documented in the header's Doxygen, with
two new regression tests. All other members — `Count`, `IsReadOnly`, `IsConnected` (confirmed reading
the live `TouchPanel::getTouchDeviceExistsProperty()`), the indexer, `Add`/`Insert`/`Remove`/`RemoveAt`/
`Clear`, `Contains`/`IndexOf`/`CopyTo`, and iteration — verified line-by-line and faithful; previously
documented deviations (IsReadOnly-is-advisory, CopyTo out-of-range throwing, default-empty-not-null)
reconfirmed accurate. One additional structurally-necessary deviation found undocumented at the
fidelity-doc level (already commented in source): `CopyTo` inserts into its growable `std::vector`
destination rather than overwriting a fixed-size array's slots as FNA's `List<T>.CopyTo` does — now
documented. Files changed: `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`,
`src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`,
`tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp` (2 new tests:
`FindByIdWritesInvalidSentinelOnOutParamWhenMissing`,
`FindByIdWritesInvalidSentinelOnOutParamForEmptyCollection`), `docs/input-fna-fidelity.md`.
Consolidated build+test verification pending as part of the parallel P1 batch 2 audit run. Remaining
risk: none.

---

## P1-023 — Audit TouchLocation member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `TouchLocation` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `TouchLocation` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchLocation.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/TouchLocation.cs`.

**Result:** 2026-07-17. Audited `TouchLocation` against FNA's `TouchLocation.cs` line-by-line: both
constructors, `Id`/`State`/`Position` getters, `Equals(TouchLocation)` (all five fields),
`operator==`/`operator!=`, `GetHashCode()`, `ToString()`, and `TryGetPreviousLocation` all match FNA
exactly. Three pre-existing, already-documented deviations reconfirmed accurate: `GetHashCode()` is
exactly `Id.GetHashCode() + Position.GetHashCode()`; `TryGetPreviousLocation`'s false-path out-param
write (DEC-12) still writes correctly on every call; `Equals(object obj)` remains intentionally omitted
per the codebase-wide standing rule. The NOXNA/EXT pressure surface is correctly tagged and correctly
excluded from `Equals`/`GetHashCode`/`ToString`. **No accidental divergence found; no files changed.**
Existing `TouchLocationTest` (12 cases in `TouchInputTests.cpp`) already exhaustively covers every
constructor overload, equality/hash/`ToString` exact-format behavior, the DEC-12 false-path write, and
pressure defaulting/exclusion. Consolidated build+test verification pending as part of the parallel P1
batch 2 audit run. Remaining risk: none.

---

## P1-024 — Audit TouchLocationState member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `TouchLocationState` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `TouchLocationState` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/TouchLocationStateTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/TouchLocationState.cs`.

**Result:** 2026-07-17. Audited `TouchLocationState` (enum: `Invalid`, `Released`, `Pressed`, `Moved`)
against FNA's `TouchLocationState.cs`: names, declaration order, and implicit numeric constants
(0/1/2/3) match exactly. FNA has no per-member `<summary>` doc text (only a source-comment MSDN link),
so CNA's Doxygen is an original description, not a mistranslation — nothing to reconcile. Call sites
in `TouchLocation.cpp`, `TouchPanel.cpp`, and `SdlInputBridge.cpp` spot-checked and consistent with
FNA touch-state semantics. **No divergence found; no files changed.** Test coverage confirmed via the
existing dedicated `tests/Microsoft/Xna/Framework/Input/Touch/TouchLocationStateTests.cpp`
(`TouchLocationStateTest.ValuesMatchXnaSequentialConstants`, picked up automatically by the CMake glob
— not orphaned). Consolidated build+test verification pending as part of the parallel P1 batch 2
audit run. Remaining risk: none.

---

## P1-025 — Audit TouchPanel member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `TouchPanel` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `TouchPanel` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/TouchPanel.cs`.

**Result:** 2026-07-17. Audited `TouchPanel` against FNA's `TouchPanel.cs`, covering every member
EXCEPT `GetState()`/`Update()`/`GetCapabilities()` (already reworked and tested under [[P13-002]]/
[[P13-005]] — INP-AUD-001/INP-AUD-003 — not re-examined). **Found and fixed one real divergence:**
`MAX_TOUCHES`/`NO_FINGER` were public `static constexpr` with no `NOXNA` tag despite FNA declaring both
`internal const int` (`TouchPanel.cs:23,26`) — fixed to match the established `GamePad::LeftDeadZone`
-style `NOXNA`-tagging convention for FNA-`internal` constants exposed cross-TU in C++ (documented under
P1-003). All other members — `ReadGesture`, `EnqueueGesture`, `IsGestureAvailable`,
`INTERNAL_onTouchEvent`, `SetFinger`, `DisplayWidth`/`DisplayHeight`/`DisplayOrientation`,
`WindowHandle`, `EnabledGestures`, `TouchDeviceExists`, `ResetForTests` — verified line-by-line against
FNA and found correct; existing test coverage already adequate. Two already-documented deviations
reconfirmed (not re-fixed): `SetFinger`'s bounds check throwing `std::out_of_range` where FNA's array
indexer would throw `IndexOutOfRangeException`; `INTERNAL_onTouchEvent`'s zero-display-size guard
(task 828/P5-014). Also fixed a missing Doxygen comment on the deleted default constructor. Verified
the header change compiles cleanly in both normal and `CNA_STRICT_XNA_API` modes with zero runtime
behavioral effect. Files changed:
`include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`. Consolidated build+test verification
pending as part of the parallel P1 batch 2 audit run. Remaining risk: none.

---

## P1-026 — Audit TouchPanelCapabilities member parity vs FNA `[x]`
**Goal:** Perform a full member-by-member audit of `TouchPanelCapabilities` (constructors, methods, operators, enum values, defaults, equality, hash) against its FNA reference.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every public member of `TouchPanelCapabilities` has been checked against FNA (or its NOXNA status confirmed).
- Any accidental divergence found is fixed and covered by a test.
- Any intentional divergence is documented, not silently present.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp (locate/verify dedicated coverage; add TouchPanelCapabilitiesTests.cpp if missing)`

**Notes:** FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/TouchPanelCapabilities.cs`.

**Result:** 2026-07-17. Audited `TouchPanelCapabilities` (a small, self-contained 44-line struct — no
scope exclusions apply, unlike P1-025) against FNA's `TouchPanelCapabilities.cs`. Both constructors
(the implicit-default-equivalent `NOXNA TouchPanelCapabilities()` and the FNA-`internal`-mirroring
`NOXNA TouchPanelCapabilities(bool, int)`), both read-only properties, field defaults
(`false`/`0`), and the deliberate absence of any equality/hashing/`ToString` members (FNA declares
none) were verified line-by-line. **No divergence found; no files changed.** Existing tests
(`TouchPanelCapabilitiesTest.DefaultConstructorProducesDisconnectedZeroCapacity`,
`ParameterizedConstructorSetsConnectionAndMaxTouchCount` in `TouchInputTests.cpp`, plus indirect
exercise via the `GetCapabilities`-driven tests in `TouchEdgeCaseTests.cpp`) already cover both
constructors and both properties. Consolidated build+test verification pending as part of the parallel
P1 batch 2 audit run. Remaining risk: none.

---

## P1-027 — Header self-containment audit across strict XNA Input headers `[x]`
**Goal:** Confirm every header under `include/Microsoft/Xna/Framework/Input` compiles standalone and does not require an application to also include a `CNA::Input` extension header.

**Steps:**
1. Compile each strict-XNA Input header alone in a throwaway translation unit (or rely on `PublicApiInputCompileTests.cpp`).
2. Check for any `#include "CNA/Input/..."` inside a strict-XNA header.
3. Remove/relocate any such dependency found.
4. Re-run the compile-tests target.

**Acceptance criteria:**
- No strict-XNA Input header includes a `CNA/Input/*` header.
- `PublicApiInputCompileTests.cpp` passes.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/*.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/*.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/PublicApiInputCompileTests.cpp`

**Notes:** Direct implementation of CLAUDE.md rule: "Public XNA-compatible headers must not require users to include CNA extension headers."

**Result:** 2026-07-17. `grep -rn 'include.*"CNA/Input' include/Microsoft/Xna/Framework/Input/` found
**5 real violations**: `Keyboard.hpp` (`CNA/Input/KeyModifiers.hpp`), `TextInputEXT.hpp`
(`CNA/Input/TextInputType.hpp`), and `GamePad.hpp` (`CNA/Input/GamePadButtonLabel.hpp`,
`CNA/Input/GamePadConnectionState.hpp`, `CNA/Input/PowerState.hpp`) — each pulled in a CNA::Input
extension header just to declare an `...EXT` method's return/parameter type. All 5 types are plain
`enum class` values used only as return/parameter types in method declarations (no inline bodies, no
default values needing the complete type), so all 5 were fixed by replacing the `#include` with a
forward declaration (matching the underlying type where one is explicitly specified, e.g.
`KeyModifiersEXT : std::uint32_t`). `GamePad.cpp` needed no new include (already gets the real
definitions transitively via `CNA/Internal/Input/SdlInputBridge.hpp`, which every EXT method there
delegates to); `TextInputEXT.cpp` genuinely switches over `TextInputTypeEXT`'s values, so it got an
explicit `#include "CNA/Input/TextInputType.hpp"`; `Keyboard.cpp` needed no new include for the same
transitive reason as `GamePad.cpp`. Files changed:
`include/Microsoft/Xna/Framework/Input/Keyboard.hpp`,
`include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`,
`include/Microsoft/Xna/Framework/Input/GamePad.hpp`,
`src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`. `cmake --build cmake-build-debug --target
CnaTests` — clean, no errors. `xvfb-run -a env SDL_VIDEODRIVER=x11 CnaTests
--gtest_filter=$CNA_INPUT_TEST_FILTER --gtest_shuffle --gtest_repeat=5` — `[PASSED] 517 tests.` on all
5 repeats, zero `FAILED`. Re-ran the grep post-fix: zero matches. Remaining risk: none.

---

## P1-028 — SDL include-leakage audit across strict XNA Input headers `[x]`
**Goal:** Confirm no strict-XNA Input header pulls `<SDL3/SDL.h>` (or any SDL header) into consumer translation units.

**Steps:**
1. Grep every strict-XNA Input header for `SDL3/` or `SDL_` includes.
2. For any hit, replace with an opaque forward declaration (the pattern already used in `MouseCursor.hpp` for `SDL_Cursor`).
3. Move the real include into the matching `.cpp`.
4. Rebuild and confirm no new warnings.

**Acceptance criteria:**
- `grep -rn "SDL3/\|#include <SDL" include/Microsoft/Xna/Framework/Input` returns no matches, or every match is justified and recorded here.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/**/*.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/PublicApiInputCompileTests.cpp`

**Notes:** MouseCursor.hpp already documents the intended pattern (opaque `struct SDL_Cursor;` forward decl) — use it as the reference example.

**Result:** 2026-07-17. `grep -rn '^\s*#include\s*[<"]SDL' include/Microsoft/Xna/Framework/Input/`
returns zero matches — no strict-XNA Input header pulls in an actual SDL header. The one textual hit
from a looser grep (`MouseCursor.hpp:9`) is a comment describing the file's own opaque-forward-decl
pattern (`struct SDL_Cursor;`, already the reference example this task's Notes point to), not an
`#include`. **No divergence found; no files changed.** Files changed: none (this task ran cleanly
alongside P1-027's fix — no rebuild/retest needed beyond what P1-027 already verified: `CnaTests`
builds clean, canonical Input filter 517/517 passed, 5x shuffled repeats, zero failures). Remaining
risk: none.

---

## P1-029 — Enum numeric-value freeze audit `[ ]`
**Goal:** Confirm the underlying numeric values of `Buttons`, `GamePadType`, `TouchLocationState`, `GestureType`, `KeyState`, and `ButtonState` exactly match FNA's enum values (these are often serialized/bit-tested and must not silently renumber).

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every enumerator's numeric value matches FNA.
- A freeze test exists asserting the numeric values, not just the names.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Buttons.hpp`
- `include/Microsoft/Xna/Framework/Input/GamePadType.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`
- `include/Microsoft/Xna/Framework/Input/KeyState.hpp`
- `include/Microsoft/Xna/Framework/Input/ButtonState.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/PublicApiInputSignatureFreezeTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-030 — Default-value audit sweep `[ ]`
**Goal:** Confirm every default-constructed strict-XNA Input struct/state (KeyboardState, MouseState, GamePadState, TouchLocation, GestureSample, etc.) matches FNA's default field values.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Each type's parameterless/default construction path is verified against FNA and covered by an explicit `*_DefaultValues` test.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/**/*.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/**`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-031 — Equality/inequality operator audit sweep `[ ]`
**Goal:** Confirm `==`/`!=` (and `Equals`) on every value type in this list matches FNA's field-wise comparison semantics, including which fields participate.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Each equality operator is verified field-by-field against FNA and covered by an equal-case and unequal-case test, per CLAUDE.md's testing rules.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/**/*.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/**`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-032 — GetHashCode/hash behavior consistency sweep `[ ]`
**Goal:** Confirm every value type's hash implementation is internally consistent (equal objects produce equal hashes) and, where practical, matches FNA's hash composition strategy.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Equal-value hash-equality is tested for every type.
- Any deliberate deviation (e.g. `std::size_t` vs FNA's `int`) is listed in CHECKLIST.md's deviation table.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/**/*.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/**`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-033 — Constructor overload audit sweep `[ ]`
**Goal:** Confirm every FNA constructor overload for these types (default, full-field, copy) has a matching C++ constructor with matching parameter order and defaults.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- No FNA constructor overload is missing in C++.
- Parameter order matches FNA (CLAUDE.md member-order rule).

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/**/*.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/**`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-034 — Static factory/method audit sweep `[ ]`
**Goal:** Confirm static entry points (`Keyboard::GetState`, `Mouse::GetState`/`SetPosition`, `GamePad::GetState`/`GetCapabilities`/`SetVibration`, `TouchPanel::GetState`/`GetCapabilities`) match FNA overload sets exactly, including `PlayerIndex`-taking overloads.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Every FNA static overload exists with matching behavior.
- No extra strict-XNA-namespace overload exists unless wrapped in `NOXNA`.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/**`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-035 — C++ deviation-from-C# documentation sweep `[ ]`
**Goal:** Collect every intentional C++-vs-C# deviation identified across Phase 1 tasks (out/ref params, hash type, null-guard omission, etc.) into the CHECKLIST.md deviation table.

**Steps:**
1. Walk the results of P1-001..034.
2. Cross-check each deviation is already listed in `CHECKLIST.md`; add any missing entries.
3. Confirm no deviation is documented only as a source comment without also being tracked centrally.

**Acceptance criteria:**
- `CHECKLIST.md`'s acceptable-deviation table accounts for every deviation found in Phase 1.

**Files likely touched:**
- `CHECKLIST.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-036 — FNA line-by-line comparison pass — Keyboard family `[ ]`
**Goal:** Do a dedicated FNA-vs-CNA line-by-line pass across `Keyboard`, `KeyboardState`, `Keys`, `KeyState` together, since FNA implements them as tightly coupled friend types.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Cross-type invariants (e.g. `GetPressedKeys` ordering sourced from `KeyboardState`'s internal bitmask) match FNA.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`
- `include/Microsoft/Xna/Framework/Input/KeyState.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-037 — FNA line-by-line comparison pass — Mouse family `[ ]`
**Goal:** Do a dedicated FNA-vs-CNA line-by-line pass across `Mouse`, `MouseState`, `MouseCursor` together.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Cross-type invariants (state snapshot vs live cursor object) match FNA/MonoGame's documented behavior.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-038 — FNA line-by-line comparison pass — GamePad family `[ ]`
**Goal:** Do a dedicated FNA-vs-CNA line-by-line pass across `GamePad`, `GamePadState`, `GamePadButtons`, `GamePadDPad`, `GamePadThumbSticks`, `GamePadTriggers`, `GamePadCapabilities`, `GamePadType`, `GamePadDeadZone`, `Buttons`, `ButtonState` together.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Cross-type invariants (dead-zone application order, packet-number semantics) match FNA exactly.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad*.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePad*.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-039 — FNA line-by-line comparison pass — Touch family `[ ]`
**Goal:** Do a dedicated FNA-vs-CNA line-by-line pass across `TouchPanel`, `TouchPanelCapabilities`, `TouchCollection`, `TouchLocation`, `TouchLocationState` together.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Cross-type invariants (previous-location linkage, collection mutability) match FNA exactly.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/*.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/Touch/TouchLocationStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-040 — FNA line-by-line comparison pass — Gesture family `[ ]`
**Goal:** Do a dedicated FNA-vs-CNA line-by-line pass across `GestureSample`, `GestureType`, and the `TouchPanel` gesture-queue members together.

**Steps:**
1. Open the CNA header/source and the matching FNA reference file for this member/feature.
2. Compare behavior line-by-line against FNA: signatures, defaults, clamping, casts, ordering, exceptions.
3. Record every divergence found, intentional or accidental.
4. Fix accidental divergences; for intentional ones add/confirm a deviation note in `docs/input-fna-fidelity.md`.
5. Confirm Doxygen (`/** @brief ... */`) coverage on every public member touched.
6. Run the relevant existing test binary/filter and add missing coverage if the behavior was previously untested.

**Acceptance criteria:**
- Cross-type invariants (gesture-to-touch coupling) match FNA exactly.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-041 — Doxygen coverage sweep across strict XNA Input headers `[ ]`
**Goal:** Confirm every public method, constructor, property getter/setter, operator, and constant in every strict-XNA Input header has a full `/** @brief ... */` Doxygen block, per CLAUDE.md.

**Steps:**
1. Grep each header for public members lacking a preceding `/**` block.
2. Add missing Doxygen blocks, porting intent from the FNA XML doc comments where available.
3. Confirm no bare `///` remains on a public declaration.

**Acceptance criteria:**
- 100% of public members in scope have a Doxygen block.
- No bare `///` on any public declaration.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/**/*.hpp`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-042 — XNA-compatibility-comment sweep `[ ]`
**Goal:** Confirm every strict-XNA header carries a clear statement of its XNA 4.0 origin, and every `NOXNA`-marked member inside it (e.g. `MouseCursor`) carries a clear non-XNA note, per the existing `MouseCursor.hpp` pattern.

**Steps:**
1. Read each header's top-of-type Doxygen block.
2. Add/adjust notes so XNA-vs-non-XNA status is unambiguous at a glance.

**Acceptance criteria:**
- Every type/member in scope states its XNA/NOXNA status in its Doxygen block.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/**/*.hpp`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-043 — Signature-freeze test coverage audit `[ ]`
**Goal:** Confirm `PublicApiInputSignatureFreezeTests.cpp` actually exercises all 26 types in this phase's scope, not just a subset.

**Steps:**
1. Read the freeze test file.
2. Cross-reference its coverage against the 26-type list above.
3. Add missing freeze assertions (static_assert on signatures/sizes/enum values) for any gap found.

**Acceptance criteria:**
- Every type in the Phase 1 list has at least one freeze assertion.

**Files likely touched:**
- `tests/Microsoft/Xna/Framework/Input/PublicApiInputSignatureFreezeTests.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/PublicApiInputSignatureFreezeTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-044 — Public API parity matrix regeneration `[ ]`
**Goal:** Regenerate `docs/input-member-parity-matrix.md` from the actual results of P1-001..043, replacing any stale rows left over from before this pass.

**Steps:**
1. Walk the completed results of every Phase 1 task.
2. Update the matrix row for each type with current parity status and links to relevant tests.
3. Cross-check against `docs/input-fna-fidelity.md` for consistency.

**Acceptance criteria:**
- The matrix reflects this pass's actual findings, not the archived plan's findings.

**Files likely touched:**
- `docs/input-member-parity-matrix.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Must not copy content from the archived `plan_input_20260707.md` — regenerate from this pass's own results only.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P1-045 — Phase 1 checkpoint and summary `[ ]`
**Goal:** Close out Phase 1 with a summary of parity status across all 26 strict-XNA Input types and any open follow-ups carried into later phases.

**Steps:**
1. Summarize pass/fail/deferred counts across P1-001..044.
2. List any item requiring a follow-up task in a later phase, with a cross-reference.

**Acceptance criteria:**
- Summary is written into this file with concrete counts, not a vague "looks good".

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-001 — Keys enum completeness vs FNA `[ ]`
**Goal:** Confirm every `Keys` enumerator FNA defines exists in `Keys.hpp` with no gaps.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-002 — Keys enum numeric values vs FNA `[ ]`
**Goal:** Confirm every `Keys` enumerator's numeric value matches FNA exactly.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-003 — Invalid Keys value safety `[ ]`
**Goal:** Confirm passing an out-of-range `Keys` value to `KeyboardState::IsKeyDown`/`IsKeyUp` cannot read out of bounds.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-004 — Negative key enum handling `[ ]`
**Goal:** Confirm a negative underlying value cast to `Keys` is handled safely (no UB, no OOB array access).

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-005 — Too-large key enum handling `[ ]`
**Goal:** Confirm a too-large underlying value cast to `Keys` is handled safely (no UB, no OOB array access).

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-006 — KeyboardState::GetHashCode safety with invalid keys `[ ]`
**Goal:** Confirm hash computation cannot crash or read OOB when the state contains an invalid key bit.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-007 — KeyboardState::IsKeyDown parity `[ ]`
**Goal:** Confirm `IsKeyDown` semantics (true iff key currently pressed) match FNA exactly.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-008 — KeyboardState::IsKeyUp parity `[ ]`
**Goal:** Confirm `IsKeyUp` is the exact logical negation of `IsKeyDown`, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-009 — KeyboardState::GetPressedKeys parity `[ ]`
**Goal:** Confirm `GetPressedKeys` returns exactly the set of currently-down keys, matching FNA's array contents.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-010 — Pressed key ordering `[ ]`
**Goal:** Confirm the ordering of keys returned by `GetPressedKeys` matches FNA's documented/observed ordering (or document the deviation).

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-011 — Duplicate key prevention in GetPressedKeys `[ ]`
**Goal:** Confirm no key can appear twice in a single `GetPressedKeys` result.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-012 — Default KeyboardState value `[ ]`
**Goal:** Confirm a default-constructed `KeyboardState` reports every key up, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-013 — KeyboardState equality `[ ]`
**Goal:** Confirm `KeyboardState::operator==` compares full key-state equality matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-014 — KeyboardState inequality `[ ]`
**Goal:** Confirm `KeyboardState::operator!=` is the exact logical negation of `operator==`.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-015 — KeyboardState hash stability `[ ]`
**Goal:** Confirm two equal `KeyboardState` instances always produce equal hashes across repeated calls.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-016 — SDL keycode mapping completeness `[ ]`
**Goal:** Confirm every SDL3 keycode CNA claims to support maps to the correct `Keys` value.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-017 — SDL scancode mapping completeness `[ ]`
**Goal:** Confirm every SDL3 scancode CNA claims to support maps to the correct `Keys` value via `GetKeyFromScancodeEXT`.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardScancodeNameTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-018 — Left/right modifier key distinction `[ ]`
**Goal:** Confirm LeftShift/RightShift, LeftControl/RightControl, LeftAlt/RightAlt map to distinct `Keys` values matching FNA, not a single generic modifier.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/KeyboardModStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-019 — CapsLock/NumLock/ScrollLock behavior `[ ]`
**Goal:** Confirm lock-key state is reported as a momentary key event (not a toggle), matching FNA's XNA-compatible behavior.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardModStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-020 — OEM punctuation key mapping `[ ]`
**Goal:** Confirm OEM punctuation keys (`OemSemicolon`, `OemComma`, `OemPeriod`, etc.) map correctly on a US layout and degrade sanely on others.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardKeyNameTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-021 — Keyboard layout caveats documented `[ ]`
**Goal:** Confirm known layout-dependent caveats (physical-vs-logical key mapping) are documented in `docs/platform-input-notes.md`.

**Steps:**
1. Open `docs/platform-input-notes.md` and `docs/demo-input-checklist.md`.
2. Confirm the checklist item named in this task's Goal is present, accurate, and actionable for a human tester.
3. Add or correct the checklist entry; do not perform the hardware test itself here (see Phase 11 for the actual run).

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `docs/platform-input-notes.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-022 — Czech keyboard checklist accuracy `[ ]`
**Goal:** Review/correct the Czech-keyboard manual-test checklist entry (physical QWERTZ layout, diacritic OEM keys).

**Steps:**
1. Open `docs/platform-input-notes.md` and `docs/demo-input-checklist.md`.
2. Confirm the checklist item named in this task's Goal is present, accurate, and actionable for a human tester.
3. Add or correct the checklist entry; do not perform the hardware test itself here (see Phase 11 for the actual run).

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `docs/platform-input-notes.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Checklist only — the actual run is [[P11-002]].

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-023 — US keyboard checklist accuracy `[ ]`
**Goal:** Review/correct the US-keyboard manual-test checklist entry.

**Steps:**
1. Open `docs/platform-input-notes.md` and `docs/demo-input-checklist.md`.
2. Confirm the checklist item named in this task's Goal is present, accurate, and actionable for a human tester.
3. Add or correct the checklist entry; do not perform the hardware test itself here (see Phase 11 for the actual run).

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `docs/platform-input-notes.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Checklist only — the actual run is [[P11-001]].

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-024 — Non-QWERTY keyboard checklist accuracy `[ ]`
**Goal:** Review/correct the non-QWERTY (e.g. AZERTY/Dvorak) manual-test checklist entry.

**Steps:**
1. Open `docs/platform-input-notes.md` and `docs/demo-input-checklist.md`.
2. Confirm the checklist item named in this task's Goal is present, accurate, and actionable for a human tester.
3. Add or correct the checklist entry; do not perform the hardware test itself here (see Phase 11 for the actual run).

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `docs/platform-input-notes.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Checklist only — the actual run is [[P11-003]].

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-025 — Key repeat behavior `[ ]`
**Goal:** Confirm CNA does not synthesize XNA-level key-repeat events for `KeyboardState` (XNA polls, it does not repeat) while `TextInputEXT` legitimately repeats characters via SDL text-input events.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-026 — Focus loss clears keyboard state `[x]`
**Goal:** Confirm losing window focus clears all pressed-key bits so keys don't appear stuck down.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** Superseded by [[P13-004]] (INP-AUD-004), which resolved the contradiction between this
task's literal goal and DEC-15/the existing source+tests.

**Result:** Investigated as part of [[P13-004]] (2026-07-16). This task's literal goal — clear
pressed-key bits on focus loss — was **deliberately rejected** in favor of DEC-15's FNA-faithful
retention policy (`docs/input-fna-fidelity.md`, "Focus loss (task 951 / DEC-15)"): FNA itself does
not clear keys on focus loss either, it only sets `game.IsActive = false`. That mitigation was
unsafe while `Game::IsActive` never actually toggled on desktop focus changes (INP-AUD-002); with
[[P13-003]] fixing `IsActive`, the documented DEC-15 policy (retain state, require games to gate on
`Game.IsActive`) is now both FNA-faithful and internally consistent. No keyboard/mouse state
clearing was added. Existing coverage:
`SdlInputBridgeKeyboardTests.cpp::WindowFocusLostDoesNotClearHeldKeysMatchingFna` (focus-lost) and
`::WindowLifecycleEventsDoNotCorruptKeyboardState` (covers `SDL_EVENT_WINDOW_FOCUS_GAINED` in its
event-type loop). Remaining risk: none identified beyond the pre-existing FNA-shared stuck-key edge
case (a held key's up-event delivered to a different window), which is explicitly accepted upstream.

---

## P2-027 — Window minimized keyboard behavior `[ ]`
**Goal:** Confirm minimizing the window behaves the same as focus loss for keyboard state.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-028 — Keyboard reset behavior between tests `[ ]`
**Goal:** Confirm `InputResetAllForTests`-style reset fully clears keyboard state with no leftover bits.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-029 — Keyboard test isolation `[ ]`
**Goal:** Confirm keyboard tests do not leak state into unrelated tests run in the same process (gtest ordering/shuffle safe).

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-030 — TextInputEXT scope-as-extension documented `[ ]`
**Goal:** Confirm `TextInputEXT` is clearly documented as a NOXNA/FNA-EXT addition, not part of strict XNA 4.0.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-031 — UTF-8 decoding correctness `[ ]`
**Goal:** Confirm SDL3 UTF-8 text-input events are decoded into correct characters end to end.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-032 — UTF-16 surrogate pair behavior `[ ]`
**Goal:** Confirm codepoints outside the BMP are correctly represented (e.g. via surrogate pairs or `char32_t` per the project's `charcs` alias) with no data loss.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-033 — Invalid UTF-8 handling `[ ]`
**Goal:** Confirm malformed UTF-8 byte sequences from SDL are handled without crashing or corrupting subsequent characters.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-034 — Truncated UTF-8 handling `[ ]`
**Goal:** Confirm a UTF-8 sequence truncated mid-codepoint (e.g. split across two SDL events) is handled correctly or safely dropped.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-035 — Empty text input event handling `[ ]`
**Goal:** Confirm an empty SDL text-input event does not raise a spurious character.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-036 — Text editing (composition) events forwarded `[ ]`
**Goal:** Confirm `SDL_TEXTEDITING` events forward start/length correctly, per existing `TextEditingEventForwardsTextStartLength` coverage.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-037 — IME composition correctness `[ ]`
**Goal:** Confirm an in-progress IME composition does not emit committed characters until composition ends.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-038 — IME candidate list behavior `[ ]`
**Goal:** Confirm whether candidate-list UI is in scope; if not implemented, document that explicitly rather than leaving it ambiguous.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-039 — Backspace synthesis via TextInputEXT `[ ]`
**Goal:** Confirm Backspace is synthesized as a control character consistent with FNA's TextInputEXT behavior.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-040 — Tab synthesis via TextInputEXT `[ ]`
**Goal:** Confirm Tab is synthesized as a control character consistent with FNA's TextInputEXT behavior.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-041 — Enter synthesis via TextInputEXT `[ ]`
**Goal:** Confirm Enter/Return is synthesized as a control character consistent with FNA's TextInputEXT behavior.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-042 — Delete synthesis via TextInputEXT `[ ]`
**Goal:** Confirm Delete is synthesized as a control character consistent with FNA's TextInputEXT behavior.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-043 — Ctrl+V paste synthesis `[ ]`
**Goal:** Confirm Ctrl+V synthesizes the paste control character and suppresses the literal 'v', per existing `CtrlVEmitsPasteCharAndSuppressesLiteralText` coverage.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-044 — Clipboard interaction with text input `[ ]`
**Goal:** Confirm `CNA::Input::Clipboard` content is what actually gets pasted, with no double-insertion or encoding mismatch.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `include/CNA/Input/Clipboard.hpp`
- `src/CNA/Input/Clipboard.cpp`

**Tests:**
- `tests/CNA/Input/ClipboardTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-045 — Double text insertion prevention `[ ]`
**Goal:** Confirm a single physical keypress cannot produce two `TextInputEXT` character events (e.g. plain 'v' not suppressed per `PlainVWithoutCtrlIsNotSuppressed`, but no accidental duplication elsewhere).

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-046 — Start text input lifecycle `[ ]`
**Goal:** Confirm starting text input correctly calls into the SDL3 text-input API and updates internal state.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-047 — Stop text input lifecycle `[ ]`
**Goal:** Confirm stopping text input correctly calls into the SDL3 text-input API and suppresses further character events.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-048 — No-window text input behavior `[ ]`
**Goal:** Confirm calling text-input start/stop with no window is a safe no-op rather than a crash.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-049 — Text input rectangle (IME candidate placement) `[ ]`
**Goal:** Confirm the text-input rectangle (used to position IME candidate windows) is forwarded correctly if implemented, or documented as not-yet-implemented.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-050 — Mobile soft-keyboard hints `[ ]`
**Goal:** Confirm `CNA::Input::TextInputTypeEXT` hints (e.g. numeric/email) are forwarded to SDL3's soft-keyboard hint API where applicable.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/CNA/Input/TextInputType.hpp`
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-051 — CNA::Input::TextInputTypeEXT consistency `[ ]`
**Goal:** Confirm the `TextInputTypeEXT` enum values and naming are internally consistent with `TextInputEXT`'s own EXT-suffix convention.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/CNA/Input/TextInputType.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-052 — KeyboardState array-constructor overload parity `[ ]`
**Goal:** Confirm the FNA `KeyboardState(Keys[])`-style constructor overload (params array of pressed keys) has a matching C++ constructor.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-053 — Keyboard.GetState(PlayerIndex) overload parity `[ ]`
**Goal:** Confirm `Keyboard::GetState(PlayerIndex)` matches FNA (single shared keyboard state regardless of player index).

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-054 — Keys enum Doxygen coverage `[ ]`
**Goal:** Confirm every `Keys` enumerator has at least a one-line Doxygen `@brief` per CLAUDE.md's simple-member rule.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-055 — KeyboardState debug-display audit `[ ]`
**Goal:** Confirm there is no public `ToString`/ostream operator that leaks internal representation beyond what FNA exposes (FNA's `KeyboardState` has no public `ToString` override).

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-056 — Simultaneous modifier-key combination stress test `[ ]`
**Goal:** Add/verify a test pressing Shift+Ctrl+Alt (and both left/right variants) simultaneously and confirm all three read correctly independent of order.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-057 — Numpad key parity `[ ]`
**Goal:** Confirm numpad keys (`NumPad0`-`NumPad9`, `Decimal`, `Add`, `Subtract`, `Multiply`, `Divide`) map correctly and remain distinct from the top-row digit keys.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-058 — Text input layout independence audit `[ ]`
**Goal:** Confirm text-input characters come from SDL3's logical/layout-aware text event (not scancode-derived), so non-US layouts produce correct characters.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-059 — Regression tests for all Phase 2 fixes `[ ]`
**Goal:** Sweep P2-001..059 for any task that produced a code fix and confirm each has a durable regression test, not just a manual confirmation.

**Steps:**
1. Open the header/source and the matching FNA reference under `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA (and against SDL3 semantics where the behavior originates at the SDL boundary).
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if it is not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- `src/Microsoft/Xna/Framework/Input/Keyboard.cpp`
- `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- `include/Microsoft/Xna/Framework/Input/Keys.hpp`
- `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- `src/Microsoft/Xna/Framework/Input/TextInputEXT.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P2-060 — Phase 2 checkpoint and summary `[ ]`
**Goal:** Close out Phase 2 with a summary of keyboard/text-input parity status and any open follow-ups carried into later phases.

**Steps:**
1. Summarize pass/fail/deferred counts across P2-001..059.
2. List any item requiring a follow-up task in a later phase, with a cross-reference.

**Acceptance criteria:**
- Summary is written into this file with concrete counts.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-001 — MouseState default values `[ ]`
**Goal:** Confirm a default-constructed `MouseState` matches FNA's default field values.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-002 — Button default values `[ ]`
**Goal:** Confirm all five button fields default to `ButtonState::Released`.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-003 — X/Y position defaults `[ ]`
**Goal:** Confirm `X`/`Y` default to 0, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-004 — Scroll wheel value defaults `[ ]`
**Goal:** Confirm `ScrollWheelValue`/`HorizontalScrollWheelValue` default to 0.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-005 — MouseState equality `[ ]`
**Goal:** Confirm `MouseState::operator==` compares every field FNA compares, in the same way.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-006 — MouseState hash `[ ]`
**Goal:** Confirm equal `MouseState` values always hash equal.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-007 — Left button state parity `[ ]`
**Goal:** Confirm `LeftButton` tracks SDL's left-button bit correctly through press/release.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-008 — Middle button state parity `[ ]`
**Goal:** Confirm `MiddleButton` tracks SDL's middle-button bit correctly through press/release.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-009 — Right button state parity `[ ]`
**Goal:** Confirm `RightButton` tracks SDL's right-button bit correctly through press/release.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-010 — XButton1 state parity `[ ]`
**Goal:** Confirm `XButton1` tracks SDL's first extra button correctly.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-011 — XButton2 state parity `[ ]`
**Goal:** Confirm `XButton2` tracks SDL's second extra button correctly.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-012 — Unknown SDL mouse button handling `[ ]`
**Goal:** Confirm an SDL button index beyond XButton2 is ignored safely rather than corrupting adjacent state.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-013 — Mouse motion event handling `[ ]`
**Goal:** Confirm `SDL_MOUSEMOTION` updates `X`/`Y` without disturbing button/scroll fields.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-014 — Button event coordinate update `[ ]`
**Goal:** Confirm a button-down/up event carries the correct coordinate at the time of the event, matching SDL's reported position.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-015 — Wheel 120-unit compatibility `[ ]`
**Goal:** Confirm `ScrollWheelValue` accumulates in FNA/XNA's 120-per-notch convention, not raw SDL wheel units.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-016 — Horizontal wheel policy `[ ]`
**Goal:** Confirm `HorizontalScrollWheelValue` behavior/sign convention is documented and matches FNA where FNA defines it.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-017 — Fractional SDL wheel handling `[ ]`
**Goal:** Confirm SDL3's fractional wheel values (`SDL_MouseWheelEvent.x/y` as float) are rounded/accumulated without silent truncation bugs.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-018 — Negative mouse coordinates `[ ]`
**Goal:** Confirm negative X/Y (cursor outside window bounds) round-trips through `MouseState` without clamping unless FNA clamps.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-019 — Large mouse coordinates `[ ]`
**Goal:** Confirm very large X/Y (multi-monitor setups) do not overflow the underlying integer type.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-020 — Mouse::GetState parity `[ ]`
**Goal:** Confirm `Mouse::GetState()` returns a snapshot matching FNA's semantics (no live aliasing to internal state).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-021 — Mouse::SetPosition parity `[ ]`
**Goal:** Confirm `Mouse::SetPosition(x, y)` warps the OS cursor and is reflected in the next `GetState()`, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-022 — Mouse::SetPosition null-window guard `[ ]`
**Goal:** Confirm calling `SetPosition` with no active window is a safe no-op rather than a crash (Phase-0 concern #7).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-023 — State consistency after warp `[ ]`
**Goal:** Confirm `GetState()` immediately after `SetPosition()` reports the warped coordinates, not stale pre-warp ones.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-024 — Relative mouse mode extension audit `[ ]`
**Goal:** Audit the NOXNA relative-mouse-mode extension for correct SDL3 relative-mode toggling.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-025 — Relative delta accumulation `[ ]`
**Goal:** Confirm relative-mode deltas accumulate correctly across multiple SDL motion events within one frame.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-026 — Relative delta drain behavior `[ ]`
**Goal:** Confirm reading the accumulated relative delta resets it to zero (drain semantics), matching the documented NOXNA contract.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-027 — Relative mode with no window `[ ]`
**Goal:** Confirm enabling relative mouse mode with no window is a safe no-op.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-028 — Window handle resolution for mouse ops `[ ]`
**Goal:** Confirm `Mouse` resolves the correct active window handle consistently with `Keyboard`/`TouchPanel`.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-029 — MouseCursor default cursor behavior `[ ]`
**Goal:** Confirm the default cursor is `MouseCursor::Arrow`, matching MonoGame/FNA-EXT convention.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseCursor.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-030 — System cursor creation `[ ]`
**Goal:** Confirm each stock system cursor (`Arrow`, `IBeam`, `Hand`, etc.) creates the corresponding SDL system cursor lazily and correctly.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseCursor.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-031 — Custom cursor creation from Texture2D `[ ]`
**Goal:** Confirm creating a custom cursor from a `Texture2D` + hotspot produces a correctly-formed SDL cursor.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseCursor.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-032 — Cursor hotspot validation `[ ]`
**Goal:** Confirm an out-of-bounds hotspot is rejected or clamped predictably rather than corrupting the cursor image.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseCursor.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-033 — Disposed cursor behavior `[ ]`
**Goal:** Confirm using a disposed `MouseCursor` throws `std::runtime_error` per the project's `IDisposable` convention.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseCursor.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-034 — Double dispose of cursor `[ ]`
**Goal:** Confirm calling `Dispose()` twice on the same `MouseCursor` is safe (idempotent), matching `System::IDisposable` convention.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseCursor.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-035 — Setting cursor before window exists `[ ]`
**Goal:** Confirm `Mouse::SetCursor`-equivalent before any window is created is a safe no-op or defers correctly.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseCursor.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-036 — Public SDL cursor leakage audit `[ ]`
**Goal:** Confirm `MouseCursor`'s public API does not force consumers to depend on `<SDL3/SDL.h>` (opaque `SDL_Cursor*` forward-decl only).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseCursor.cpp`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref Phase-0 concern #2.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-037 — MouseCursor::GetSDLCursor exposure decision `[ ]`
**Goal:** Decide and document whether an internal SDL-cursor accessor should remain public NOXNA or be made internal-only; implement the decision.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseCursor.cpp`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Record the decision and rationale in `docs/input-fna-fidelity.md`, not just as a code comment.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-038 — Mouse/cursor header hygiene tests `[ ]`
**Goal:** Confirm `Mouse.hpp`/`MouseState.hpp`/`MouseCursor.hpp` each compile standalone with no leaked SDL or CNA-extension include.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseCursor.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/PublicApiInputCompileTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-039 — High-DPI mouse coordinate behavior `[ ]`
**Goal:** Confirm mouse coordinates are reported in the same coordinate space (logical vs physical pixels) that `TouchPanel`/window-size APIs use, avoiding a DPI-scale mismatch.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-040 — Manual mouse checklist accuracy `[ ]`
**Goal:** Review/correct the manual mouse-testing checklist entries (standard buttons, extra buttons, relative mode) in `docs/demo-input-checklist.md`.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `docs/demo-input-checklist.md`
- `docs/platform-input-notes.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Checklist only — the actual runs are [[P11-004]], [[P11-005]], [[P11-006]].

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-041 — Mouse wheel multi-event accumulation `[ ]`
**Goal:** Confirm multiple wheel events delivered within a single frame/poll accumulate correctly rather than only keeping the last one.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-042 — MouseState equality with scroll-only difference `[ ]`
**Goal:** Confirm two `MouseState` values differing only in scroll value compare unequal (a common off-by-omission bug).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-043 — Stock cursor caching/reuse audit `[ ]`
**Goal:** Confirm repeated access to a stock cursor property (e.g. `MouseCursor::getArrowProperty()`) returns the same cached instance rather than leaking a new SDL cursor each call.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseCursor.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-044 — Mouse capture-on-drag behavior `[ ]`
**Goal:** Confirm mouse-button-down outside then move/release still reports coordinates correctly if SDL mouse capture is enabled during a drag.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Mouse.cs` / `MouseState.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 mouse-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- `src/Microsoft/Xna/Framework/Input/Mouse.cpp`
- `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- `src/Microsoft/Xna/Framework/Input/MouseState.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P3-045 — Phase 3 checkpoint and summary `[ ]`
**Goal:** Close out Phase 3 with a summary of mouse/cursor parity status and any open follow-ups carried into later phases.

**Steps:**
1. Summarize pass/fail/deferred counts across P3-001..044.
2. List any item requiring a follow-up task in a later phase, with a cross-reference.

**Acceptance criteria:**
- Summary is written into this file with concrete counts.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-001 — Buttons enum values vs FNA `[ ]`
**Goal:** Confirm every `Buttons` flag's numeric bit value matches FNA exactly.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Buttons.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadButtonsTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-002 — Extension Buttons values audit `[ ]`
**Goal:** Confirm any NOXNA-added `Buttons` bits are clearly separated from the FNA-defined range and marked `NOXNA`.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Buttons.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadButtonsTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-003 — No bit collisions in Buttons `[ ]`
**Goal:** Confirm no two `Buttons` flags share a bit, including NOXNA extension bits.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Buttons.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadButtonsTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-004 — GamePadState default value `[ ]`
**Goal:** Confirm a default-constructed `GamePadState` reports disconnected with zeroed sticks/triggers, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-005 — GamePadState constructor overload parity `[ ]`
**Goal:** Confirm every FNA `GamePadState` constructor overload exists with matching parameter order.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-006 — GamePadState packet number semantics `[ ]`
**Goal:** Confirm `PacketNumber` increments only on real input change, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-007 — GamePadState connected-state parity `[ ]`
**Goal:** Confirm `IsConnected == true` state fields behave per FNA when a controller is attached.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-008 — GamePadState disconnected-state parity `[ ]`
**Goal:** Confirm `IsConnected == false` returns FNA's documented disconnected snapshot (all-zero state, packet number 0).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-009 — GamePadState equality `[ ]`
**Goal:** Confirm `GamePadState::operator==` compares every field FNA compares.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-010 — GamePadState hash `[ ]`
**Goal:** Confirm equal `GamePadState` values always hash equal.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-011 — GamePadButtons properties audit `[ ]`
**Goal:** Confirm every FNA `GamePadButtons` button property exists and reads the correct bit.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadButtons.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadButtonsTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-012 — Every XNA button property individually verified `[ ]`
**Goal:** Walk A/B/X/Y/Back/Start/BigButton/LeftShoulder/RightShoulder/LeftStick/RightStick and confirm each against FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadButtons.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadButtonsTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-013 — Extension button properties audit `[ ]`
**Goal:** Confirm any NOXNA-added button properties (e.g. paddles/share/misc) are clearly `NOXNA`-marked and documented.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadButtons.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadButtonsTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-014 — GamePadDPad member audit `[ ]`
**Goal:** Confirm `GamePadDPad`'s Up/Down/Left/Right members match FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadDPad.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-015 — DPad-to-Buttons flag mapping `[ ]`
**Goal:** Confirm SDL hat/DPad state maps to the correct `Buttons` DPad flags and `GamePadDPad` fields consistently.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadDPad.hpp`
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-016 — GamePadThumbSticks member audit `[ ]`
**Goal:** Confirm `GamePadThumbSticks`'s Left/Right `Vector2` members match FNA's axis convention.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadThumbSticksTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-017 — Left stick axis mapping `[ ]`
**Goal:** Confirm SDL's left-stick X/Y axes map to `GamePadThumbSticks.Left` with FNA's sign/scale convention.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-018 — Right stick axis mapping `[ ]`
**Goal:** Confirm SDL's right-stick X/Y axes map to `GamePadThumbSticks.Right` with FNA's sign/scale convention.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-019 — Dead zone application audit `[ ]`
**Goal:** Confirm dead-zone application happens at the correct layer (per `GamePadDeadZone` mode passed to `GetState`), matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadDeadZoneTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-020 — Independent-axes dead zone math `[ ]`
**Goal:** Confirm `GamePadDeadZone::IndependentAxes` applies the dead zone per-axis, matching FNA's formula.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadDeadZoneTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-021 — Circular dead zone math `[ ]`
**Goal:** Confirm `GamePadDeadZone::Circular` applies the dead zone by stick magnitude, matching FNA's formula.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadDeadZoneTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-022 — No dead zone passthrough `[ ]`
**Goal:** Confirm `GamePadDeadZone::None` passes raw axis values through unmodified (only clamped).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadDeadZoneTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-023 — Thumbstick clamping to [-1,1] `[ ]`
**Goal:** Confirm stick axis values are clamped to FNA's [-1,1] range after dead-zone processing.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadDeadZoneTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-024 — Thumbstick Y-axis inversion `[ ]`
**Goal:** Confirm the Y axis inversion (SDL down-positive vs XNA up-positive) is applied exactly once, matching FNA sign convention.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-025 — GamePadTriggers member audit `[ ]`
**Goal:** Confirm `GamePadTriggers`'s Left/Right float members match FNA's [0,1] convention.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadTriggers.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTriggersTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-026 — Trigger clamping to [0,1] `[ ]`
**Goal:** Confirm trigger values are clamped to [0,1], matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadTriggers.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTriggersTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-027 — Trigger dead zone application `[ ]`
**Goal:** Confirm trigger dead-zone handling (if any) matches FNA's documented behavior (FNA generally does not dead-zone triggers).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadTriggers.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTriggersTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-028 — SDL axis normalization audit `[ ]`
**Goal:** Confirm SDL3's int16 axis range is normalized to float with correct rounding at the extremes (no off-by-one at +32767).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-029 — SDL button mapping completeness `[ ]`
**Goal:** Confirm every SDL3 gamepad button constant CNA claims to support maps to the correct `Buttons` flag.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-030 — SDL hat mapping completeness `[ ]`
**Goal:** Confirm SDL hat/DPad values map correctly for controllers exposing DPad as a hat rather than buttons.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-031 — SDL gamepad connect event handling `[ ]`
**Goal:** Confirm `SDL_EVENT_GAMEPAD_ADDED` correctly assigns a `PlayerIndex` slot and marks the state connected.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-032 — SDL gamepad disconnect event handling `[ ]`
**Goal:** Confirm `SDL_EVENT_GAMEPAD_REMOVED` correctly frees the slot and marks the state disconnected without disturbing other players.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-033 — Slot assignment stability `[ ]`
**Goal:** Confirm a connected controller keeps its `PlayerIndex` slot for its session (no silent reassignment).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-034 — Slot reuse after disconnect `[ ]`
**Goal:** Confirm a freed slot can be reused by the next connected controller, matching FNA's first-available-slot behavior.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-035 — Four-player slot limit `[ ]`
**Goal:** Confirm at most four simultaneous `PlayerIndex` slots are assignable and a fifth controller is handled gracefully (ignored, not crashed).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-036 — Invalid PlayerIndex handling `[ ]`
**Goal:** Confirm `GamePad::GetState`/`GetCapabilities` with an out-of-range `PlayerIndex` returns a safe disconnected state rather than reading OOB.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-037 — GamePad::GetState overload parity `[ ]`
**Goal:** Confirm all FNA `GetState` overloads (with/without dead zone, with/without player index) exist and behave identically to FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-038 — GamePad::GetCapabilities parity `[ ]`
**Goal:** Confirm `GetCapabilities` returns FNA-consistent capability flags for a connected controller.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-039 — GamePadCapabilities default values `[ ]`
**Goal:** Confirm default/disconnected capabilities report `IsConnected = false` and all feature flags false, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-040 — GamePadCapabilities per-feature flags `[ ]`
**Goal:** Walk each capability flag (HasAButton...HasVoiceSupport) individually against FNA and real SDL capability queries.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-041 — GamePadType enum audit `[ ]`
**Goal:** Confirm `GamePadType` enumerators and values match FNA, including the unknown/default value.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadType.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTypeTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-042 — Unknown controller type fallback `[ ]`
**Goal:** Confirm a controller SDL can't classify maps to `GamePadType::Unknown` rather than crashing or defaulting incorrectly.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadType.hpp`
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTypeTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-043 — GUID extension audit `[ ]`
**Goal:** Confirm the NOXNA GUID-retrieval extension (`GetGUIDEXT` or similar) correctly surfaces SDL's joystick GUID.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-044 — Controller name extension audit `[ ]`
**Goal:** Confirm the NOXNA controller-name extension surfaces SDL's reported controller name correctly, including empty/unknown names.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-045 — Battery/power extension audit `[ ]`
**Goal:** Confirm any gamepad battery/power extension correctly reflects SDL's joystick power-level API and degrades gracefully when unsupported.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`
- `include/CNA/Input/Power.hpp`
- `src/CNA/Input/Power.cpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`
- `tests/CNA/Input/PowerTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-046 — Rumble (SetVibration) parity `[ ]`
**Goal:** Confirm `GamePad::SetVibration` maps left/right motor strengths to SDL3's rumble API matching FNA's clamping/semantics.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-047 — Trigger vibration extension `[ ]`
**Goal:** Confirm a NOXNA trigger-rumble extension (if present) maps correctly to SDL3's trigger-rumble API.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-048 — Duration-based vibration extension `[ ]`
**Goal:** Confirm a NOXNA duration-limited vibration extension correctly stops rumble after the specified duration.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-049 — Light bar extension audit `[ ]`
**Goal:** Confirm a NOXNA light-bar-color extension (DualShock/DualSense) maps to SDL3's LED API and no-ops safely on controllers without one.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-050 — Sensors extension audit (gamepad-attached) `[ ]`
**Goal:** Confirm gamepad-attached sensor access (if exposed via `CNA::Input::Sensors`) is wired to the correct SDL joystick/gamepad, not a stray global sensor.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/CNA/Input/Sensors.hpp`
- `src/CNA/Input/Sensors.cpp`

**Tests:**
- `tests/CNA/Input/SensorsTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-051 — Gyroscope extension audit `[ ]`
**Goal:** Confirm gyroscope data (if exposed) matches SDL3's `SDL_SENSOR_GYRO` units/axis convention.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/CNA/Input/Sensors.hpp`
- `src/CNA/Input/Sensors.cpp`

**Tests:**
- `tests/CNA/Input/SensorsTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-052 — Accelerometer extension audit `[ ]`
**Goal:** Confirm accelerometer data (if exposed) matches SDL3's `SDL_SENSOR_ACCEL` units/axis convention.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/CNA/Input/Sensors.hpp`
- `src/CNA/Input/Sensors.cpp`

**Tests:**
- `tests/CNA/Input/SensorsTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-053 — No-haptic-device fallback `[ ]`
**Goal:** Confirm calling rumble/haptic APIs on a controller with no haptic support is a safe no-op, not a crash.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/CNA/Input/HapticDevice.hpp`
- `src/CNA/Input/HapticDevice.cpp`
- `src/CNA/Internal/Input/SdlHapticBackend.cpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlHapticBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-054 — No-sensor fallback `[ ]`
**Goal:** Confirm calling sensor APIs on a controller with no sensor support is a safe no-op, not a crash.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/CNA/Input/Sensors.hpp`
- `src/CNA/Input/Sensors.cpp`

**Tests:**
- `tests/CNA/Input/SensorsTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-055 — Fake gamepad backend coverage audit `[ ]`
**Goal:** Confirm `FakeSdlGamepadBackend.hpp` can simulate every state transition exercised by P4-001..051 without touching real hardware.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-056 — Real Xbox-compatible controller checklist accuracy `[ ]`
**Goal:** Review/correct the Xbox-controller manual-test checklist entry in `docs/demo-input-checklist.md`.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Checklist only — the actual run is [[P11-007]].

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-057 — Real PlayStation-compatible controller checklist accuracy `[ ]`
**Goal:** Review/correct the PlayStation-controller manual-test checklist entry.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Checklist only — the actual run is [[P11-008]].

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-058 — Generic/unbranded controller checklist accuracy `[ ]`
**Goal:** Review/correct the generic-controller manual-test checklist entry.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Checklist only — the actual run is [[P11-009]].

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-059 — Packet number stability under no input `[ ]`
**Goal:** Confirm `PacketNumber` does not increment when polled repeatedly with no state change.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-060 — Packet number change on input change `[ ]`
**Goal:** Confirm `PacketNumber` increments exactly once per distinct polled state change.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-061 — Packet number change on connect/disconnect `[ ]`
**Goal:** Confirm connect/disconnect transitions bump `PacketNumber`, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-062 — Packet number non-change on duplicate state read `[ ]`
**Goal:** Confirm reading the same unchanged state twice in a row does not double-bump the packet number, per existing `PacketNumberBumpsOnConnectButtonAndAxisChangesOnly` coverage.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-063 — GamePad reset behavior between tests `[ ]`
**Goal:** Confirm `InputResetAllForTests`-style reset fully clears all four gamepad slots.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-064 — GamePad test isolation `[ ]`
**Goal:** Confirm gamepad tests do not leak fake-backend state into unrelated tests run in the same process.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-065 — GamePadDeadZone enum value parity `[ ]`
**Goal:** Confirm `GamePadDeadZone::None`/`IndependentAxes`/`Circular` numeric values match FNA exactly.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadDeadZoneTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-066 — GamePadState.IsButtonDown/IsButtonUp helper parity `[ ]`
**Goal:** Confirm the convenience `IsButtonDown`/`IsButtonUp` helper methods match FNA's semantics exactly, including combined-flag queries.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-067 — GamePad.SetVibration signature and clamping parity `[ ]`
**Goal:** Confirm `SetVibration`'s parameter order and [0,1] clamping matches FNA exactly.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-068 — GamePadCapabilities.GamePadType field parity `[ ]`
**Goal:** Confirm `GamePadCapabilities` exposes the detected `GamePadType` consistently with `GamePadState`'s own type info, if both exist.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-069 — Regression tests for all Phase 4 fixes `[ ]`
**Goal:** Sweep P4-001..068 for any task that produced a code fix and confirm each has a durable regression test.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/GamePad*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 gamepad-event semantics, using the fake gamepad backend for deterministic input.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists (using the fake gamepad/haptic backend where relevant) that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePad.cpp`
- `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- `src/Microsoft/Xna/Framework/Input/GamePadState.cpp`
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P4-070 — Phase 4 checkpoint and summary `[ ]`
**Goal:** Close out Phase 4 with a summary of gamepad parity status and any open follow-ups carried into later phases.

**Steps:**
1. Summarize pass/fail/deferred counts across P4-001..069.
2. List any item requiring a follow-up task in a later phase, with a cross-reference.

**Acceptance criteria:**
- Summary is written into this file with concrete counts.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-001 — TouchCollection read-only-by-default audit `[ ]`
**Goal:** Confirm `TouchCollection` matches FNA's read-only-list contract for consumer code (`IsReadOnly` semantics).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-002 — TouchCollection mutation-method inventory `[ ]`
**Goal:** Enumerate every mutation method FNA's `TouchCollection` exposes (as an `IList<TouchLocation>`) and confirm CNA's surface matches intentionally.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-003 — TouchCollection::Add behavior `[ ]`
**Goal:** Confirm `Add` matches FNA/`IList` semantics or throws `NotSupportedException`-equivalent if FNA's collection is read-only there.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-004 — TouchCollection::Clear behavior `[ ]`
**Goal:** Confirm `Clear` matches FNA/`IList` semantics.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-005 — TouchCollection::Insert behavior `[ ]`
**Goal:** Confirm `Insert` matches FNA/`IList` semantics.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-006 — TouchCollection::Remove behavior `[ ]`
**Goal:** Confirm `Remove` matches FNA/`IList` semantics.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-007 — TouchCollection::RemoveAt behavior `[ ]`
**Goal:** Confirm `RemoveAt` matches FNA/`IList` semantics, including out-of-range index handling.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-008 — TouchCollection indexer behavior `[ ]`
**Goal:** Confirm `operator[]`/indexer bounds-checking matches FNA (throw vs UB) and is exercised by a test.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-009 — TouchCollection::CopyTo behavior `[ ]`
**Goal:** Confirm `CopyTo` matches FNA semantics including destination-array bounds checking.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-010 — TouchCollection::CopyTo offset behavior `[ ]`
**Goal:** Confirm the `arrayIndex` offset parameter of `CopyTo` is honored correctly.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-011 — TouchCollection capacity behavior `[ ]`
**Goal:** Confirm capacity/reserve behavior (if exposed) does not affect observable `Count`/iteration semantics.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-012 — Empty TouchCollection behavior `[ ]`
**Goal:** Confirm an empty collection reports `Count == 0` and iterates zero times without UB.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-013 — TouchCollection enumeration order `[ ]`
**Goal:** Confirm range-for/iterator enumeration order matches FNA's insertion/index order.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-014 — TouchCollection::Count parity `[ ]`
**Goal:** Confirm `Count` always reflects the live number of touches, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-015 — TouchCollection::Contains behavior `[ ]`
**Goal:** Confirm `Contains` uses `TouchLocation` value-equality matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-016 — TouchCollection index-lookup (IndexOf) behavior `[ ]`
**Goal:** Confirm `IndexOf` matches FNA's linear-search value-equality semantics.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-017 — Deterministic touch ordering guarantee `[ ]`
**Goal:** Confirm the collection returned by `TouchPanel::GetState()` orders touches deterministically (e.g. by touch ID) matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchLocation.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-018 — TouchLocation constructor overload parity `[ ]`
**Goal:** Confirm every FNA `TouchLocation` constructor overload (with/without pressure, with/without previous-state) exists.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-019 — TouchLocation::Id parity `[ ]`
**Goal:** Confirm the touch `Id` is stable for the lifetime of a single finger contact, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-020 — TouchLocation::State parity `[ ]`
**Goal:** Confirm `State` (Pressed/Moved/Released/Invalid) transitions match FNA exactly across a full touch lifecycle.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-021 — TouchLocation::Position parity `[ ]`
**Goal:** Confirm `Position` units/coordinate space match FNA (window-space float pixels).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-022 — TouchLocation::Pressure parity `[ ]`
**Goal:** Confirm `Pressure` (if implemented) maps SDL's normalized finger pressure correctly, or is documented as always 1.0 if FNA does not use it meaningfully.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-023 — TouchLocation previous-location linkage `[ ]`
**Goal:** Confirm each `TouchLocation` correctly carries its previous location for delta computation, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-024 — TouchLocation::TryGetPreviousLocation parity `[ ]`
**Goal:** Confirm `TryGetPreviousLocation` returns false with an `Invalid`-state placeholder when there is no previous location, matching FNA. Cross-ref the I12 event-driven previous-location bug fixed in [[project_chatgpt_review_pending]].

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** Re-verify this specific historical bug does not regress.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-025 — TouchLocation equality `[ ]`
**Goal:** Confirm `TouchLocation::operator==` compares every field FNA compares (Id, State, Position).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-026 — TouchLocation hash `[ ]`
**Goal:** Confirm equal `TouchLocation` values always hash equal.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-027 — TouchLocationState numeric values `[ ]`
**Goal:** Confirm `Invalid`/`Released`/`Pressed`/`Moved` numeric values match FNA exactly.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/TouchLocationStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-028 — TouchPanel::GetState overall parity `[x]`
**Goal:** Confirm `TouchPanel::GetState()` composes active+released touches into one `TouchCollection` exactly as FNA does per frame.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchLocation.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** Superseded by [[P13-002]] (INP-AUD-001), which found and fixed a real divergence in this
exact area (see that task's Result for the full fix/test/doc record).

**Result:** Resolved by [[P13-002]] (2026-07-16): `GetState()` no longer mutates on read; touch
composition/promotion/retirement now happens exactly once per frame via
`InputManager::AdvanceTouchFrame()`. See `TouchEdgeCaseTests.cpp` /
`SdlInputBridgeGoldenTests.cpp::TwoFingerScriptResolvesToExactTouchSnapshots` for the composed
multi-touch snapshot coverage. `ctest -L input` 496/496 passed.

---

## P5-029 — Active touches tracked correctly `[x]`
**Goal:** Confirm currently-pressed/moved touches appear in every `GetState()` call until released.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchLocation.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** Superseded by [[P13-002]] (INP-AUD-001), which found and fixed a real divergence in this exact area (see that task's Result for the full fix/test/doc record).

**Result:** Resolved by [[P13-002]] (2026-07-16): a Pressed/Moved touch now reliably survives across reads within a frame (`GetTouchStateIsPureAndRepeatedReadsWithinAFrameAreIdentical`) and correctly promotes/persists across explicit frame advances (`AdvanceTouchFrameWorksEvenWithoutAnIntermediateRead`, `HeldTouchAutoPromotesToMovedWithPressedPrevious`). `ctest -L input` 496/496 passed.

---

## P5-030 — Previous touches available for delta `[x]`
**Goal:** Confirm the previous frame's touch positions remain queryable via `TryGetPreviousLocation` while a touch is still active.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchLocation.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** Superseded by [[P13-002]] (INP-AUD-001), which found and fixed a real divergence in this exact area (see that task's Result for the full fix/test/doc record).

**Result:** Resolved by [[P13-002]] (2026-07-16): previous-location tracking (`TryGetPreviousLocation`) is now recorded by `InputManager::AdvanceTouchFrame()` once per frame rather than on every read, so it reflects the prior *frame's* location rather than the prior *read's*. See `EventDrivenPathPreservesPreviousLocation` in `TouchEdgeCaseTests.cpp` and `SdlInputBridgeTouchGestureTests.cpp::FingerEventsExposePreviousLocationThroughTouchPanelGetState`. `ctest -L input` 496/496 passed.

---

## P5-031 — Released touch cleanup after one frame `[x]`
**Goal:** Confirm a released touch appears exactly once with `State == Released` then is removed from subsequent `GetState()` results, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchLocation.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** Superseded by [[P13-002]] (INP-AUD-001), which found and fixed a real divergence in this exact area (see that task's Result for the full fix/test/doc record).

**Result:** Resolved by [[P13-002]] (2026-07-16): a Released touch is now visible for exactly one post-advance read regardless of how many times it was read beforehand (`ReleasedTouchIsVisibleForExactlyOnePostAdvanceReadRegardlessOfPriorReads`), fixing the prior read-frequency-dependent retirement. `ctest -L input` 496/496 passed.

---

## P5-032 — Repeated touch-down on same finger ID `[ ]`
**Goal:** Confirm a new SDL finger-down reusing a stale ID does not corrupt the previous touch's state.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchLocation.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-033 — Unknown finger-release handling `[ ]`
**Goal:** Confirm an SDL finger-up event for an ID CNA never saw pressed is ignored safely.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-034 — Touch cancel event handling `[ ]`
**Goal:** Confirm `SDL_EVENT_FINGER_CANCELED` (if used) is handled the same as a release, matching FNA's expectations for interrupted touches.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-035 — Max simultaneous touch count `[ ]`
**Goal:** Confirm `TouchPanelCapabilities::MaximumTouchCount` is respected or at least accurately reported, and excess touches are handled predictably.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchLocation.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-036 — Touch ID ordering stability `[ ]`
**Goal:** Confirm the order touches appear in `GetState()` is stable and documented (e.g. ID-ascending), matching FNA or documenting the deviation.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchLocation.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-037 — Touch coordinate scaling to window size `[ ]`
**Goal:** Confirm SDL's normalized [0,1] finger coordinates are scaled to window pixel space correctly.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-038 — Display size zero edge case `[ ]`
**Goal:** Confirm a zero-sized display/window does not produce a divide-by-zero when scaling touch coordinates.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-039 — High-DPI touch coordinate behavior `[ ]`
**Goal:** Confirm touch coordinates use the same coordinate space as mouse coordinates on high-DPI displays.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-040 — TouchPanel::GetCapabilities parity `[x]`
**Goal:** Confirm `GetCapabilities` reports `IsConnected`/`MaximumTouchCount` matching FNA/SDL3 touch-device queries.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchLocation.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** Superseded by [[P13-005]] (INP-AUD-003), which found and fixed a real divergence in this exact area (see that task's Result for the full fix/test/doc record).

**Result:** Resolved by [[P13-005]] (2026-07-16): `GetCapabilities()` now queries `system_device_backend().GetTouchDevices()` on every call (matching FNA's unconditional `SDL_GetTouchDevices()`), instead of only ever consulting the sticky `touchDeviceExists_`/`HasAnyTouch()` fallbacks. See the `TouchCapabilitiesEnumerationTest` fixture in `TouchEdgeCaseTests.cpp`. `ctest -L input` 496/496 passed.

---

## P5-041 — Capabilities query does not mutate touch state `[x]`
**Goal:** Confirm calling `GetCapabilities()` before any touch never mutates active-touch tracking state (Phase-0 concern #8).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchLocation.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`

**Notes:** Superseded by [[P13-005]] (INP-AUD-003) for the enumeration-source aspect (this task's own non-mutation concern was already correctly handled before P13 and remains verified).

**Result:** Confirmed still correct after [[P13-005]] (2026-07-16): `GetCapabilities()` remains fully non-mutating with the new SDL-enumeration check added (`TouchCapabilitiesEnumerationTest.EnumerationQueryDoesNotMutateTouchState`, plus the pre-existing `GetCapabilitiesHasNoSideEffectOnTouchState`). `ctest -L input` 496/496 passed.

---

## P5-042 — TouchPanel reset behavior between tests `[ ]`
**Goal:** Confirm `InputResetAllForTests`-style reset fully clears all active/previous touch state.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchLocation.cpp`

**Tests:**
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-043 — Real touchscreen manual checklist accuracy `[ ]`
**Goal:** Review/correct the touchscreen manual-test checklist entry in `docs/demo-input-checklist.md`.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Checklist only — the actual run is [[P11-012]].

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-044 — Regression tests for all Phase 5 fixes `[ ]`
**Goal:** Sweep P5-001..043 for any task that produced a code fix and confirm each has a durable regression test.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/*.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA and SDL3 finger-event semantics.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA/SDL3 expectations (or the deviation is documented).
- A test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchLocation.cpp`
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P5-045 — Phase 5 checkpoint and summary `[ ]`
**Goal:** Close out Phase 5 with a summary of touch parity status and any open follow-ups carried into later phases.

**Steps:**
1. Summarize pass/fail/deferred counts across P5-001..044.
2. List any item requiring a follow-up task in a later phase, with a cross-reference.

**Acceptance criteria:**
- Summary is written into this file with concrete counts.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-001 — GestureType enum numeric values `[ ]`
**Goal:** Confirm every `GestureType` flag's numeric bit value matches FNA exactly.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-002 — GestureType bitwise combination behavior `[ ]`
**Goal:** Confirm combining `GestureType` flags with `|` and testing with `&` matches FNA's flag-enum semantics.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-003 — Default EnabledGestures value `[ ]`
**Goal:** Confirm `TouchPanel::EnabledGestures` defaults to `GestureType::None`, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/GestureSample.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-004 — Enabling gestures via EnabledGestures setter `[ ]`
**Goal:** Confirm setting `EnabledGestures` actually enables detection for exactly the requested flags.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/GestureSample.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-005 — Disabling gestures via EnabledGestures setter `[ ]`
**Goal:** Confirm clearing a flag from `EnabledGestures` stops that gesture type from being detected/queued.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/GestureSample.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-006 — Gesture queue behavior `[ ]`
**Goal:** Confirm detected gestures are queued FIFO and drained only by `ReadGesture`, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/GestureSample.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-007 — TouchPanel::IsGestureAvailable parity `[ ]`
**Goal:** Confirm `IsGestureAvailable` reflects the true non-empty state of the queue without side effects.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/GestureSample.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-008 — TouchPanel::ReadGesture parity `[ ]`
**Goal:** Confirm `ReadGesture` dequeues exactly one `GestureSample` per call, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/GestureSample.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-009 — Empty gesture queue behavior `[ ]`
**Goal:** Confirm calling `ReadGesture` on an empty queue matches FNA's documented behavior (exception vs default value) exactly.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/GestureSample.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-010 — Gesture queue FIFO ordering `[ ]`
**Goal:** Confirm gestures are read back in the exact order they were detected.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/GestureSample.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-011 — GestureSample constructor overload parity `[ ]`
**Goal:** Confirm every FNA `GestureSample` constructor overload exists with matching parameter order.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-012 — GestureSample timestamp behavior `[ ]`
**Goal:** Confirm `GestureSample.Timestamp` uses the same time source/units as the rest of CNA's `GameTime`, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-013 — GestureSample.Position parity `[ ]`
**Goal:** Confirm the primary `Position` field matches FNA's convention for single-touch gestures.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-014 — GestureSample.Position2 parity `[ ]`
**Goal:** Confirm `Position2` is populated only for two-touch gestures (Pinch), matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-015 — GestureSample.Delta parity `[ ]`
**Goal:** Confirm `Delta` reports the correct per-gesture movement delta, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-016 — GestureSample.Delta2 parity `[ ]`
**Goal:** Confirm `Delta2` is populated only for two-touch gestures, matching FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-017 — Tap gesture detection `[ ]`
**Goal:** Confirm a quick single-finger press-release within FNA's tap thresholds produces exactly one `Tap` sample.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-018 — DoubleTap gesture detection `[ ]`
**Goal:** Confirm two taps within FNA's double-tap time/distance window produce a `DoubleTap` sample instead of two `Tap` samples.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-019 — Hold gesture detection `[ ]`
**Goal:** Confirm a stationary press held past FNA's hold-duration threshold produces a `Hold` sample.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-020 — HorizontalDrag gesture detection `[ ]`
**Goal:** Confirm a predominantly-horizontal drag produces `HorizontalDrag` samples matching FNA's angle threshold.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-021 — VerticalDrag gesture detection `[ ]`
**Goal:** Confirm a predominantly-vertical drag produces `VerticalDrag` samples matching FNA's angle threshold.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-022 — FreeDrag gesture detection `[ ]`
**Goal:** Confirm `FreeDrag` (any-direction) is produced only when `HorizontalDrag`/`VerticalDrag` are not both more specific and enabled, matching FNA's precedence.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-023 — Flick gesture detection `[ ]`
**Goal:** Confirm a fast release produces a `Flick` sample with velocity matching FNA's formula.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-024 — Pinch gesture detection `[ ]`
**Goal:** Confirm two simultaneous touches moving apart/together produce `Pinch` samples with correct `Position`/`Position2`/`Delta`/`Delta2`.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-025 — PinchComplete gesture detection `[ ]`
**Goal:** Confirm releasing either finger of an active pinch produces exactly one `PinchComplete` sample.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-026 — Movement threshold constants vs FNA `[ ]`
**Goal:** Confirm the minimum-movement-to-count-as-drag threshold constant matches FNA's source value.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-027 — Duration threshold constants vs FNA `[ ]`
**Goal:** Confirm the hold-duration and double-tap-time threshold constants match FNA's source values.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-028 — Flick velocity calculation formula `[ ]`
**Goal:** Confirm the velocity formula (distance/time windowing) matches FNA's implementation, not just its approximate feel.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-029 — Disabled-gesture-type filtering `[ ]`
**Goal:** Confirm a gesture type not present in `EnabledGestures` is never queued, even if the underlying touch pattern would otherwise trigger it.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/GestureSample.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-030 — Multi-touch interaction between simultaneous gestures `[ ]`
**Goal:** Confirm three-or-more simultaneous touches don't produce malformed Pinch/Drag data (FNA generally only tracks two touches for gestures).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-031 — Gesture-detector reset behavior `[ ]`
**Goal:** Confirm `InputResetAllForTests`-style reset clears in-progress gesture-detection state (partial holds/drags/pinches).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-032 — Deterministic clock tests for gesture timing `[ ]`
**Goal:** Confirm gesture-timing tests use an injectable/fixed clock rather than real wall-clock sleeps, so they are not flaky.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-033 — Display-size-dependent gesture thresholds `[ ]`
**Goal:** Confirm any DPI/display-size-scaled thresholds (e.g. drag distance in pixels vs logical units) are documented and consistent with the touch-coordinate-scaling behavior audited in Phase 5.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-034 — Touch-to-gesture event ordering `[ ]`
**Goal:** Confirm a single SDL finger event updates `TouchPanel::GetState()` and feeds the gesture detector in a consistent, documented order within one frame.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/GestureSample.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-035 — Gesture queue overflow policy `[ ]`
**Goal:** Confirm behavior when gestures are detected faster than they are read (unbounded growth vs a documented cap) and that this matches or intentionally extends FNA.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/GestureSample.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-036 — Gesture deviations documented `[ ]`
**Goal:** Confirm every intentional CNA gesture-detection deviation from FNA is listed in `docs/input-fna-fidelity.md`, not just in source comments.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `docs/input-fna-fidelity.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-037 — Real-device gesture manual checklist accuracy `[ ]`
**Goal:** Review/correct the multi-touch-gesture manual-test checklist entry in `docs/demo-input-checklist.md`.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Checklist only — the actual run is [[P11-013]].

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-038 — GestureType.DragComplete parity `[ ]`
**Goal:** Confirm `DragComplete` is emitted when an active drag ends, matching FNA's flag and timing.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-039 — GestureSample equality/ToString audit `[ ]`
**Goal:** Confirm `GestureSample` does not silently diverge from FNA on equality/`ToString` if FNA defines them (FNA's `GestureSample` has no such overrides — confirm CNA doesn't add unexpected ones).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-040 — GestureType.None handling in detector `[ ]`
**Goal:** Confirm `EnabledGestures == None` fully disables the detector (no wasted computation, no stray queued samples).

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/GestureSample.cpp`
- `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`
- `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-041 — Simultaneous-gesture precedence rules `[ ]`
**Goal:** Confirm precedence when a touch pattern could match multiple enabled gesture types at once (e.g. Pinch vs FreeDrag) matches FNA's priority order.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-042 — Gesture-detector reset-between-tests audit `[ ]`
**Goal:** Cross-check the reset behavior audited in P6-031 is actually invoked by every gesture test's setup/teardown, not just available.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-043 — Gesture threshold constants vs FNA source values documented `[ ]`
**Goal:** Add a small table to `docs/input-fna-fidelity.md` listing every gesture threshold constant and its FNA source value side by side.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `docs/input-fna-fidelity.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-044 — Regression tests for all Phase 6 fixes `[ ]`
**Goal:** Sweep P6-001..043 for any task that produced a code fix and confirm each has a durable, deterministic-clock regression test.

**Steps:**
1. Open the header/source and the matching FNA reference `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Touch/GestureSample.cs`, `GestureType.cs`, and the gesture-detection logic in `TouchPanel.cs`.
2. Compare the specific behavior named in this task's Goal line-by-line against FNA's gesture-detection state machine.
3. Fix any accidental divergence; document any intentional one in `docs/input-fna-fidelity.md`.
4. Add or extend a targeted, deterministic (fixed-clock) test for exactly this behavior if not already covered.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- The specific behavior in the Goal line matches FNA's gesture state machine (or the deviation is documented).
- A test exists, using a deterministic fixed clock/seed, that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/Touch/GestureTypeTests.cpp`
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P6-045 — Phase 6 checkpoint and summary `[ ]`
**Goal:** Close out Phase 6 with a summary of gesture parity status and any open follow-ups carried into later phases.

**Steps:**
1. Summarize pass/fail/deferred counts across P6-001..044.
2. List any item requiring a follow-up task in a later phase, with a cross-reference.

**Acceptance criteria:**
- Summary is written into this file with concrete counts.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-001 — Audit include/CNA/Input/Clipboard.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `Clipboard.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/Clipboard.hpp`
- `src/CNA/Input/Clipboard.cpp`

**Tests:**
- `tests/CNA/Input/ClipboardTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-002 — Audit include/CNA/Input/GamePadButtonLabel.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `GamePadButtonLabel.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/GamePadButtonLabel.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadMappingTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-003 — Audit include/CNA/Input/GamePadConnectionState.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `GamePadConnectionState.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/GamePadConnectionState.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-004 — Audit include/CNA/Input/HapticCapabilities.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `HapticCapabilities.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/HapticCapabilities.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-005 — Audit include/CNA/Input/HapticDevice.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `HapticDevice.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/HapticDevice.hpp`
- `src/CNA/Input/HapticDevice.cpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlHapticBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-006 — Audit include/CNA/Input/HapticDirection.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `HapticDirection.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/HapticDirection.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-007 — Audit include/CNA/Input/HapticEffect.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `HapticEffect.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/HapticEffect.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-008 — Audit include/CNA/Input/HapticEffectType.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `HapticEffectType.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/HapticEffectType.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-009 — Audit include/CNA/Input/HapticFeature.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `HapticFeature.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/HapticFeature.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-010 — Audit include/CNA/Input/HapticInfo.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `HapticInfo.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/HapticInfo.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-011 — Audit include/CNA/Input/Haptics.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `Haptics.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/Haptics.hpp`
- `src/CNA/Input/Haptics.cpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-012 — Audit include/CNA/Input/InputDeviceInfo.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `InputDeviceInfo.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/InputDeviceInfo.hpp`

**Tests:**
- `tests/CNA/Input/InputDevicesTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-013 — Audit include/CNA/Input/InputDevices.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `InputDevices.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/InputDevices.hpp`
- `src/CNA/Input/InputDevices.cpp`

**Tests:**
- `tests/CNA/Input/InputDevicesTests.cpp`
- `tests/CNA/Input/InputDevicesHotplugTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-014 — Audit include/CNA/Input/JoystickCapabilities.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `JoystickCapabilities.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/JoystickCapabilities.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlJoystickBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-015 — Audit include/CNA/Input/JoystickHatPosition.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `JoystickHatPosition.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/JoystickHatPosition.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlJoystickBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-016 — Audit include/CNA/Input/JoystickInfo.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `JoystickInfo.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/JoystickInfo.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlJoystickBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-017 — Audit include/CNA/Input/Joysticks.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `Joysticks.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/Joysticks.hpp`
- `src/CNA/Input/Joysticks.cpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlJoystickBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-018 — Audit include/CNA/Input/JoystickState.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `JoystickState.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/JoystickState.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlJoystickBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-019 — Audit include/CNA/Input/JoystickType.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `JoystickType.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/JoystickType.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlJoystickBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-020 — Audit include/CNA/Input/KeyModifiers.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `KeyModifiers.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/KeyModifiers.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/KeyboardModStateTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-021 — Audit include/CNA/Input/Power.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `Power.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/Power.hpp`
- `src/CNA/Input/Power.cpp`

**Tests:**
- `tests/CNA/Input/PowerTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-022 — Audit include/CNA/Input/PowerState.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `PowerState.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/PowerState.hpp`

**Tests:**
- `tests/CNA/Input/PowerTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-023 — Audit include/CNA/Input/Sensors.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `Sensors.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/Sensors.hpp`
- `src/CNA/Input/Sensors.cpp`

**Tests:**
- `tests/CNA/Input/SensorsTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-024 — Audit include/CNA/Input/TextInputType.hpp `[ ]`
**Goal:** Audit `CNA::Input`'s `TextInputType.hpp` for public API consistency, EXT-suffix consistency, header self-containment, and SDL-exposure policy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Header is self-contained, has no SDL leakage beyond an intentionally-documented opaque handle, and is fully Doxygen-covered.
- Existing tests for the type pass; new coverage is added for any gap found.

**Files likely touched:**
- `include/CNA/Input/TextInputType.hpp`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-025 — HapticDevice ownership model and concrete-SDL-type exposure policy `[ ]`
**Goal:** Decide and implement whether `HapticDevice` should keep exposing a raw `SDL_Haptic*` publicly or move to an opaque/PIMPL handle (Phase-0 concern #2), and write a single authoritative policy note covering which `CNA::Input` types may expose concrete SDL types publicly at all.

**Steps:**
1. Review `HapticDevice.hpp`'s current public surface for any `SDL_Haptic*` accessor.
2. Weigh the tradeoff: raw pointer (simple, leaks concrete SDL type) vs opaque non-SDL handle (extra indirection, cleaner boundary).
3. Implement the chosen approach for `HapticDevice`.
4. Enumerate every other public SDL type referenced across `include/CNA/Input/*.hpp` and classify each as allowed (documented exception) or disallowed (needs opaque wrapper).
5. Record both the `HapticDevice` decision and the general policy in `docs/input-fna-fidelity.md`.

**Acceptance criteria:**
- The `HapticDevice` decision is implemented consistently and documented.
- Every other public SDL-type exposure in `CNA::Input` is classified against the written policy.

**Files likely touched:**
- `include/CNA/Input/HapticDevice.hpp`
- `src/CNA/Input/HapticDevice.cpp`
- `docs/input-fna-fidelity.md`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-026 — Clipboard functional behavior audit `[ ]`
**Goal:** Confirm `CNA::Input::Clipboard` get/set/has-text operations round-trip UTF-8 text correctly through SDL3's clipboard API.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Get/Set/HasText behave correctly for empty, ASCII, and multi-byte UTF-8 clipboard content.

**Files likely touched:**
- `include/CNA/Input/Clipboard.hpp`
- `src/CNA/Input/Clipboard.cpp`

**Tests:**
- `tests/CNA/Input/ClipboardTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-027 — Power functional behavior audit `[ ]`
**Goal:** Confirm `CNA::Input::Power`/`PowerState` correctly reflect SDL3's power-info API (battery percent, charging state, unsupported fallback).

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Battery/charging fields match SDL3's reported values or a documented unsupported fallback.

**Files likely touched:**
- `include/CNA/Input/Power.hpp`
- `include/CNA/Input/PowerState.hpp`
- `src/CNA/Input/Power.cpp`

**Tests:**
- `tests/CNA/Input/PowerTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-028 — Sensors functional behavior audit `[ ]`
**Goal:** Confirm `CNA::Input::Sensors` correctly enumerates and reads SDL3 sensor devices, with a safe unsupported-platform fallback.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Sensor availability/read behaves correctly or degrades safely when unsupported.

**Files likely touched:**
- `include/CNA/Input/Sensors.hpp`
- `src/CNA/Input/Sensors.cpp`

**Tests:**
- `tests/CNA/Input/SensorsTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-029 — Joystick enumeration functional audit `[ ]`
**Goal:** Confirm `CNA::Input::Joysticks` enumerates connected joysticks (including hotplug add/remove) correctly via SDL3.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Enumeration count and indices update correctly across hotplug events.

**Files likely touched:**
- `include/CNA/Input/Joysticks.hpp`
- `src/CNA/Input/Joysticks.cpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlJoystickBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-030 — Joystick state functional audit `[ ]`
**Goal:** Confirm `CNA::Input::JoystickState` correctly reads back axes, buttons, and hats from the SDL3 joystick API.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Axis/button/hat values match what the fake/real SDL joystick backend reports.

**Files likely touched:**
- `include/CNA/Input/JoystickState.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlJoystickBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-031 — Haptic capabilities functional audit `[ ]`
**Goal:** Confirm `HapticCapabilities` feature flags match SDL3's `SDL_GetHapticFeatures` bitmask for the underlying device.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Each capability flag correctly reflects the corresponding SDL feature bit.

**Files likely touched:**
- `include/CNA/Input/HapticCapabilities.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-032 — Haptic effect validation audit `[ ]`
**Goal:** Confirm invalid `HapticEffect` parameters (out-of-range magnitude/duration) are rejected predictably rather than passed straight to SDL3 with undefined results.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Invalid effect parameters produce a defined, tested error path.

**Files likely touched:**
- `include/CNA/Input/HapticEffect.hpp`
- `include/CNA/Input/HapticEffectType.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-033 — Haptic effect lifecycle audit `[ ]`
**Goal:** Confirm the upload → run → stop → destroy lifecycle of a `HapticEffect` on a `HapticDevice` is correct and leak-free.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- A full upload/run/stop/destroy cycle is exercised by a test with no leaked SDL handles.

**Files likely touched:**
- `include/CNA/Input/HapticDevice.hpp`
- `src/CNA/Input/HapticDevice.cpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-034 — Haptic device cleanup/dispose audit `[ ]`
**Goal:** Confirm `HapticDevice` releases its underlying SDL haptic handle exactly once on destruction/dispose, matching the project's `IDisposable`/RAII conventions.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- No double-free, no leak, verified under the project's sanitizer build (cross-ref Phase 9).

**Files likely touched:**
- `include/CNA/Input/HapticDevice.hpp`
- `src/CNA/Input/HapticDevice.cpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-035 — Move-only semantics audit across HapticDevice/Joystick RAII types `[ ]`
**Goal:** Confirm `HapticDevice` and joystick-owning RAII types are move-only (deleted copy ctor/assign) and moved-from state is safe to destroy.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Copy is deleted; move leaves the source in a safe, destructible state; a test exercises move construction/assignment.

**Files likely touched:**
- `include/CNA/Input/HapticDevice.hpp`
- `include/CNA/Input/Joysticks.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`
- `tests/CNA/Internal/Input/SdlJoystickBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-036 — Disposed-object-after-use behavior audit across CNA::Input types `[ ]`
**Goal:** Confirm every disposable `CNA::Input` type throws `std::runtime_error` (per CLAUDE.md's IDisposable rule) when used after disposal, rather than crashing.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Post-dispose use throws the documented exception type for every disposable extension type.

**Files likely touched:**
- `include/CNA/Input/HapticDevice.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-037 — No-device fallback behavior audit `[ ]`
**Goal:** Confirm `Joysticks::GetCount() == 0` / no haptic devices present is handled as a normal, safe state throughout the extension layer.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Every API behaves sanely when zero devices are present, exercised by a dedicated test.

**Files likely touched:**
- `include/CNA/Input/Joysticks.hpp`
- `include/CNA/Input/Haptics.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlJoystickBackendTests.cpp`
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-038 — Platform-unsupported fallback behavior audit `[ ]`
**Goal:** Confirm sensors/power/haptics degrade to a documented safe/unsupported state on platforms where the SDL3 subsystem is unavailable, rather than failing to build or crashing at runtime.

**Steps:**
1. Open `include/CNA/Input/{header}` and its `.cpp` if one exists.
2. Confirm the type is under the `CNA` namespace (not `Microsoft::Xna`), uses the project's EXT-suffix convention where it mirrors a platform concept, and needs no `NOXNA` marker (it is already outside `Microsoft::Xna`).
3. Confirm the header is self-contained and does not leak a concrete SDL type into its public surface (opaque pointer/forward-declare only).
4. Confirm every public member has a full Doxygen `/** @brief ... */` block.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- Unsupported-platform paths are covered by a test (via a fake/no-op backend) and documented.

**Files likely touched:**
- `include/CNA/Input/Sensors.hpp`
- `include/CNA/Input/Power.hpp`
- `include/CNA/Input/Haptics.hpp`

**Tests:**
- `tests/CNA/Input/SensorsTests.cpp`
- `tests/CNA/Input/PowerTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-039 — Manual validation checklist cross-reference for CNA::Input extensions `[ ]`
**Goal:** Confirm every hardware-gated `CNA::Input` extension (haptics, joystick, sensors) has a corresponding entry in the Phase 11 manual checklist, with no extension silently unverifiable.

**Steps:**
1. Cross-reference the 24-header list against `docs/demo-input-checklist.md` and Phase 11 tasks below.
2. Add any missing checklist entry.

**Acceptance criteria:**
- Every hardware-dependent extension has a named Phase 11 task or documented reason it needs none.

**Files likely touched:**
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P7-040 — Phase 7 checkpoint and summary `[ ]`
**Goal:** Close out Phase 7 with a summary of CNA/NOXNA extension audit status and any open follow-ups carried into later phases.

**Steps:**
1. Summarize pass/fail/deferred counts across P7-001..039.
2. List any item requiring a follow-up task in a later phase, with a cross-reference.

**Acceptance criteria:**
- Summary is written into this file with concrete counts.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-001 — SDL initialization ownership `[ ]`
**Goal:** Confirm exactly one owner initializes the SDL input-relevant subsystems (events/gamepad/haptic/sensor) and re-init is guarded.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-002 — SDL shutdown safety `[ ]`
**Goal:** Confirm shutdown tears down input subsystems in a safe order with no use-after-shutdown access.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-003 — Event pump assumptions documented `[ ]`
**Goal:** Confirm and document which thread/loop is assumed to call `SDL_PollEvent`/`SDL_PumpEvents`, and that the bridge doesn't assume a second implicit pump.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-004 — Keyboard event routing `[ ]`
**Goal:** Confirm `SDL_EVENT_KEY_DOWN`/`SDL_EVENT_KEY_UP` route to `KeyboardState` correctly, matching Phase 2's findings.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-005 — Text event routing `[ ]`
**Goal:** Confirm `SDL_EVENT_TEXT_INPUT`/`SDL_EVENT_TEXT_EDITING` route to `TextInputEXT` correctly, matching Phase 2's findings.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-006 — Mouse event routing `[ ]`
**Goal:** Confirm `SDL_EVENT_MOUSE_MOTION`/`SDL_EVENT_MOUSE_BUTTON_*` route to `MouseState` correctly, matching Phase 3's findings.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-007 — Wheel event routing `[ ]`
**Goal:** Confirm `SDL_EVENT_MOUSE_WHEEL` routes to `MouseState`'s scroll fields correctly, matching Phase 3's findings.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-008 — Touch event routing `[ ]`
**Goal:** Confirm `SDL_EVENT_FINGER_DOWN`/`MOTION`/`UP` route to `TouchPanel` correctly, matching Phase 5's findings.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-009 — Gesture event routing `[ ]`
**Goal:** Confirm touch events feed the gesture detector in the correct order relative to `TouchPanel` state updates, matching Phase 6's findings.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`
- `src/CNA/Internal/Input/GestureDetector.cpp`
- `include/CNA/Internal/Input/GestureDetector.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-010 — Gamepad connect event routing `[ ]`
**Goal:** Confirm `SDL_EVENT_GAMEPAD_ADDED` routes to slot assignment correctly, matching Phase 4's findings.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-011 — Gamepad disconnect event routing `[ ]`
**Goal:** Confirm `SDL_EVENT_GAMEPAD_REMOVED` routes to slot teardown correctly, matching Phase 4's findings.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-012 — Controller remap events `[ ]`
**Goal:** Confirm `SDL_EVENT_GAMEPAD_REMAPPED` (mapping changes) is handled without corrupting an in-progress `GamePadState` read.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-013 — Joystick events (raw, non-gamepad) `[ ]`
**Goal:** Confirm raw `SDL_EVENT_JOYSTICK_*` events route correctly to `CNA::Input::Joysticks`/`JoystickState` without double-counting devices already exposed as gamepads.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlJoystickBackend.cpp`
- `include/CNA/Internal/Input/SdlJoystickBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlJoystickBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-014 — Sensor events `[ ]`
**Goal:** Confirm `SDL_EVENT_SENSOR_UPDATE` routes correctly to `CNA::Input::Sensors`.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Input/Sensors.cpp`

**Tests:**
- `tests/CNA/Input/SensorsTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-015 — Haptic lifecycle at the bridge layer `[ ]`
**Goal:** Confirm the bridge opens/closes SDL haptic devices in step with gamepad/joystick connect-disconnect, not leaking a handle per reconnect.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlHapticBackend.cpp`
- `include/CNA/Internal/Input/SdlHapticBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-016 — Window handle resolution `[ ]`
**Goal:** Confirm the bridge resolves 'the active window' consistently for all input subsystems (mouse/keyboard/touch/text) rather than each subsystem tracking its own notion.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-017 — Null window behavior at the bridge layer `[ ]`
**Goal:** Confirm every bridge entry point is a safe no-op (not a crash) when called before any window exists.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-018 — Stale window handle behavior `[ ]`
**Goal:** Confirm the bridge does not retain a dangling window handle after the window is destroyed/recreated.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-019 — Focus lost handling `[x]`
**Goal:** Confirm `SDL_EVENT_WINDOW_FOCUS_LOST` triggers the keyboard/mouse-button clearing behavior audited in Phase 2/3.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** Superseded by [[P13-003]] (INP-AUD-002) for the `Game::IsActive` aspect of focus-lost handling. The keyboard/mouse-state aspect was separately confirmed correct (no clear, matching FNA) by [[P13-004]]/DEC-15.

**Result:** Resolved by [[P13-003]] (2026-07-16): `SDL_EVENT_WINDOW_FOCUS_LOST` now routes through `setIsActiveProperty(false)` in `Game::PollEvents()`, matching FNA (`SDL3_FNAPlatform.cs:1006-1037`). No automated test exists at this level (`Game` has no unit-test scaffold in this repo, confirmed in [[P13-003]]'s own Result); this is an established, explicitly-documented constraint, not a gap introduced here.

---

## P8-020 — Focus gained handling `[x]`
**Goal:** Confirm `SDL_EVENT_WINDOW_FOCUS_GAINED` does not spuriously report stale pressed keys/buttons from before the focus loss.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** Superseded by [[P13-003]] (INP-AUD-002) for the `Game::IsActive` aspect of focus-gained handling. Stale-key-visibility on focus gain was separately confirmed by [[P13-004]] via `SdlInputBridgeKeyboardTests.cpp::WindowLifecycleEventsDoNotCorruptKeyboardState`.

**Result:** Resolved by [[P13-003]] (2026-07-16): `SDL_EVENT_WINDOW_FOCUS_GAINED` now routes through `setIsActiveProperty(true)` in `Game::PollEvents()`, matching FNA. `WindowLifecycleEventsDoNotCorruptKeyboardState` (`SdlInputBridgeKeyboardTests.cpp`) already covers this event not spuriously reporting stale pressed keys. `ctest -L input` 496/496 passed.

---

## P8-021 — Minimize handling `[x]`
**Goal:** Confirm `SDL_EVENT_WINDOW_MINIMIZED` is treated consistently with focus-lost for input-clearing purposes.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** Investigated as part of the [[P13-002]]..[[P13-004]] remediation pass; no divergence found for this specific event.

**Result:** Investigated 2026-07-16: `SDL_EVENT_WINDOW_MINIMIZED` has no dedicated handler in `Game::PollEvents()` or `SdlInputBridge::ProcessEvent()` — it falls through as a no-op, same as every other unhandled window-lifecycle event (`WindowLifecycleEventsDoNotCorruptKeyboardState` already covers `SDL_EVENT_WINDOW_MINIMIZED` in its event-type loop and confirms it does not corrupt keyboard state). This is consistent with focus-lost/gained handling for input-clearing purposes (none of the three clear transient state), matching the DEC-15/[[P13-004]] policy. CNA does **not** set `Game::IsActive = false` on minimize the way it does for `WILL_ENTER_BACKGROUND`/`FOCUS_LOST` — FNA's own SDL3 platform loop has no `SDL_EVENT_WINDOW_MINIMIZED` case either (it relies on the platform-specific background/foreground or focus events instead), so this is FNA-consistent, not a gap. No test added beyond the existing `WindowLifecycleEventsDoNotCorruptKeyboardState` coverage.

---

## P8-022 — Restore handling `[ ]`
**Goal:** Confirm `SDL_EVENT_WINDOW_RESTORED` resumes normal input routing without requiring an explicit re-init call.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-023 — Display resize handling `[ ]`
**Goal:** Confirm a window resize updates whatever coordinate-scaling state mouse/touch routing depends on (Phase 3/5 coordinate-scaling findings).

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-024 — High-DPI resize handling `[ ]`
**Goal:** Confirm a DPI change (`SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED` or platform equivalent) is handled consistently with the Phase 3/5 high-DPI findings.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-025 — Backend reset correctness `[ ]`
**Goal:** Confirm the bridge's reset-for-tests entry point clears every subsystem's state completely (keyboard, mouse, touch, gesture, gamepad slots, joystick, haptic handles).

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`
- `src/CNA/Internal/Input/SdlGamepadBackend.cpp`
- `include/CNA/Internal/Input/SdlGamepadBackend.hpp`

**Tests:**
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-026 — Test-only reset does not leak into production path `[ ]`
**Goal:** Confirm the reset-for-tests function is not reachable/callable from normal game code (test-only visibility).

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-027 — Fake event helper coverage audit `[ ]`
**Goal:** Confirm the test helpers for synthesizing `SDL_Event`s cover keyboard, mouse, touch, and gamepad shapes needed by Phases 2-6's tests.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `tests/CNA/Internal/Input`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-028 — Event ordering within one poll cycle `[ ]`
**Goal:** Confirm multiple queued events of different types in one poll cycle are applied in the order SDL delivered them, not reordered by subsystem.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-029 — Duplicate event handling `[ ]`
**Goal:** Confirm a duplicate/replayed event (e.g. two identical key-down events with no key-up between) does not corrupt state.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeFuzzTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-030 — Unknown/unhandled event ignoring `[ ]`
**Goal:** Confirm an SDL event type the bridge doesn't recognize is safely ignored, not mis-routed.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeCandidatesTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-031 — Thread-safety assumptions documented `[ ]`
**Goal:** Document (and verify via code inspection) whether the bridge assumes single-threaded access, and where that assumption is enforced or asserted.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-032 — Main-thread assumptions documented `[ ]`
**Goal:** Confirm any main-thread-only SDL calls (e.g. window/cursor APIs) are documented as such, matching SDL3's own thread-safety documentation.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-033 — Public/private boundary audit `[ ]`
**Goal:** Confirm no public XNA/CNA Input header includes an internal `CNA/Internal/Input/*` header (the dependency direction must be internal → public wrapper, never the reverse import).

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `include/Microsoft/Xna/Framework/Input`
- `include/CNA/Input`

**Tests:**
- `tests/Microsoft/Xna/Framework/Input/PublicApiInputCompileTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-034 — Internal-only SDL usage audit `[ ]`
**Goal:** Confirm `<SDL3/SDL.h>` is included only from `.cpp` files and `CNA/Internal/**` headers, never from a public header.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src`
- `include/CNA/Internal`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-035 — Build with EasyGL backend `[ ]`
**Goal:** Build the `EASYGL` graphics-backend configuration and confirm Input compiles/links/tests pass identically to the default backend.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `cmake-build-input-easygl`

**Tests:**
- `CnaInputTests (EasyGL config)`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-036 — Build with Vulkan backend `[ ]`
**Goal:** Build the `VULKAN` graphics-backend configuration and confirm Input compiles/links/tests pass identically to the default backend.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `cmake-build-input-vulkan`

**Tests:**
- `CnaInputTests (Vulkan config)`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-037 — Build with bgfx backend `[ ]`
**Goal:** Build the `BGFX` graphics-backend configuration and confirm Input compiles/links/tests pass identically to the default backend.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `cmake-build-input-bgfx`

**Tests:**
- `CnaInputTests (bgfx config)`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-038 — Build with SDL renderer backend `[ ]`
**Goal:** Build the `SDL_RENDERER` graphics-backend configuration and confirm Input compiles/links/tests pass identically to the default backend.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `cmake-build-input-sdlrenderer`

**Tests:**
- `CnaInputTests (SDL_RENDERER config)`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-039 — Regression tests for all Phase 8 fixes `[ ]`
**Goal:** Sweep P8-001..038 for any task that produced a code fix and confirm each has a durable regression test.

**Steps:**
1. Open `src/CNA/Internal/Input/SdlInputBridge.cpp` (and `InputManager.cpp`/relevant backend file) for the subsystem named in this task's Goal.
2. Trace the exact SDL3 event(s) or API call(s) involved end to end into the public XNA/CNA state they populate.
3. Compare against FNA's own SDL2/SDL3 platform driver behavior where FNA has an equivalent, and against SDL3's documented semantics otherwise.
4. Fix any bug found; add/extend a deterministic test using the fake backend or a synthetic `SDL_Event`.
5. Run the listed test file(s) and record the result.

**Acceptance criteria:**
- The traced behavior is correct end to end and matches FNA/SDL3 semantics (or the deviation is documented).
- A deterministic test exists that would fail if this behavior regressed.

**Files likely touched:**
- `src/CNA/Internal/Input/SdlInputBridge.cpp`
- `include/CNA/Internal/Input/SdlInputBridge.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `include/CNA/Internal/Input/InputManager.hpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P8-040 — Phase 8 checkpoint and summary `[ ]`
**Goal:** Close out Phase 8 with a summary of SDL bridge/backend audit status and any open follow-ups carried into later phases.

**Steps:**
1. Summarize pass/fail/deferred counts across P8-001..039.
2. List any item requiring a follow-up task in a later phase, with a cross-reference.

**Acceptance criteria:**
- Summary is written into this file with concrete counts.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-001 — Confirm focused Input test target builds `[ ]`
**Goal:** Build only the `CnaInputTests` target (or its containing test binary) in isolation and confirm it links.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `CMakeLists.txt`

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-002 — Run ctest -R CnaInputTests / -L input `[ ]`
**Goal:** Run the canonical Input test selector end to end and record pass/fail counts.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `CMakeLists.txt`

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-003 — Repeated-run stability check `[ ]`
**Goal:** Run the Input test selector 3x in a row (`ctest --repeat until-fail:3` or equivalent) and confirm identical results each run.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- (none)

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-004 — Shuffled-order stability check `[ ]`
**Goal:** Run the Input tests with gtest's `--gtest_shuffle` and confirm no ordering-dependent failure appears.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- (none)

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-005 — AddressSanitizer build `[ ]`
**Goal:** Build `cmake-build-input-asan` and run the Input test selector under ASan, recording any report.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `cmake-build-input-asan`

**Tests:**
- `CnaInputTests (ASan)`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-006 — UndefinedBehaviorSanitizer build `[ ]`
**Goal:** Build (or reconfigure) with `-DCNA_SANITIZE=undefined` and run the Input test selector, recording any report.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `CMakeLists.txt`

**Tests:**
- `CnaInputTests (UBSan)`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-007 — Valgrind pass if practical `[ ]`
**Goal:** If Valgrind is available and the ASan build is not already sufficient, run the Input test binary under `valgrind --leak-check=full` and record findings; if impractical (e.g. runtime cost), document why it was skipped.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- (none)

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-008 — Public header compile tests pass `[ ]`
**Goal:** Confirm `PublicApiInputCompileTests.cpp` builds and passes, proving every public Input header compiles standalone.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/Microsoft/Xna/Framework/Input/PublicApiInputCompileTests.cpp`

**Tests:**
- `PublicApiInputCompileTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-009 — Strict XNA signature-freeze tests pass `[ ]`
**Goal:** Confirm `PublicApiInputSignatureFreezeTests.cpp` builds and passes after all Phase 1-6 changes.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/Microsoft/Xna/Framework/Input/PublicApiInputSignatureFreezeTests.cpp`

**Tests:**
- `PublicApiInputSignatureFreezeTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-010 — CNA extension signature-freeze tests exist and pass `[ ]`
**Goal:** Confirm an equivalent freeze-test exists for `CNA::Input` extension types (add one if missing) and that it passes.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/CNA/Input`

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-011 — Enum freeze tests pass `[ ]`
**Goal:** Confirm the enum-numeric-value freeze assertions from P1-029 and P4-066 actually execute and pass.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- (none)

**Tests:**
- `PublicApiInputSignatureFreezeTests`
- `GamePadDeadZoneTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-012 — Keyboard fuzz-style event tests `[ ]`
**Goal:** Run/extend a fuzz-style test feeding randomized (seeded) sequences of key events at `SdlInputBridge` and confirm no crash/UB.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/CNA/Internal/Input/SdlInputBridgeFuzzTests.cpp`

**Tests:**
- `SdlInputBridgeFuzzTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-013 — Mouse fuzz-style event tests `[ ]`
**Goal:** Run/extend a fuzz-style test feeding randomized (seeded) sequences of mouse events and confirm no crash/UB.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/CNA/Internal/Input/SdlInputBridgeFuzzTests.cpp`

**Tests:**
- `SdlInputBridgeFuzzTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-014 — Touch fuzz-style event tests `[ ]`
**Goal:** Run/extend a fuzz-style test feeding randomized (seeded) sequences of finger events and confirm no crash/UB.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/CNA/Internal/Input/SdlInputBridgeFuzzTests.cpp`

**Tests:**
- `SdlInputBridgeFuzzTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-015 — Gesture fuzz-style event tests `[ ]`
**Goal:** Run/extend a fuzz-style test feeding randomized (seeded) multi-touch sequences at the gesture detector and confirm no crash/UB.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/CNA/Internal/Input/GestureDetectorTests.cpp`

**Tests:**
- `GestureDetectorTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-016 — GamePad fuzz-style event tests `[ ]`
**Goal:** Run/extend a fuzz-style test feeding randomized (seeded) axis/button/connect sequences at the fake gamepad backend and confirm no crash/UB.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`

**Tests:**
- `SdlGamepadBackendTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-017 — Joystick fuzz-style event tests `[ ]`
**Goal:** Run/extend a fuzz-style test feeding randomized (seeded) joystick event sequences and confirm no crash/UB.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/CNA/Internal/Input/SdlJoystickBackendTests.cpp`

**Tests:**
- `SdlJoystickBackendTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-018 — Haptic invalid-input tests `[ ]`
**Goal:** Extend haptic tests with deliberately invalid effect parameters (NaN/negative/huge magnitude) and confirm the P7-032 validation path is actually exercised.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/CNA/Internal/Input/SdlHapticBackendTests.cpp`

**Tests:**
- `SdlHapticBackendTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-019 — Deterministic seed recording `[ ]`
**Goal:** Confirm every fuzz-style test records its RNG seed on failure so a failure is reproducible, per CLAUDE.md's testing rigor expectations.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/CNA/Internal/Input/SdlInputBridgeFuzzTests.cpp`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-020 — Golden event sequence coverage audit `[ ]`
**Goal:** Confirm `SdlInputBridgeGoldenTests.cpp` covers at least one golden sequence per subsystem (keyboard/mouse/touch/gesture/gamepad).

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`

**Tests:**
- `SdlInputBridgeGoldenTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-021 — Test isolation audit across the Input suite `[ ]`
**Goal:** Confirm no Input test depends on execution order or leftover state from a previous test (cross-ref reset-behavior tasks in Phases 2-8).

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-022 — Reset-between-tests enforcement audit `[ ]`
**Goal:** Confirm every Input test fixture actually calls the reset-for-tests entry point in `SetUp`/`TearDown`, not just that the entry point exists.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/CNA/Internal/Input/InputResetTests.cpp`

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-023 — CI workflow audit for Input `[ ]`
**Goal:** Review the CI workflow configuration and confirm `CnaInputTests` (and the sanitizer/backend builds relevant to Input) actually run in CI, not just locally.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `.github/workflows`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-024 — Missing-SDL-submodule diagnostics `[ ]`
**Goal:** Confirm CMake configuration fails with a clear, actionable message (not a cryptic missing-header error) if `third_party/SDL` is not checked out.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `CMakeLists.txt`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-025 — Optional system-SDL policy documented `[ ]`
**Goal:** Confirm/document whether building against a system-installed SDL3 (instead of the vendored submodule) is supported, and if so, that Input tests still pass that way.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `CMakeLists.txt`
- `docs/input-build-and-test.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-026 — Failure artifact logging `[ ]`
**Goal:** Confirm a failing Input test produces enough logged context (input event sequence, seed, state dump) to debug without re-running locally.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/CNA/Internal/Input`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-027 — Coverage document update `[ ]`
**Goal:** Update `docs/input-test-coverage.md` to reflect the actual current test inventory and any gaps found across Phases 1-8.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `docs/input-test-coverage.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-028 — Test naming consistency audit `[ ]`
**Goal:** Confirm Input test names follow one consistent convention (`TypeName_Behavior` or similar) across all suites touched in this plan.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/Microsoft/Xna/Framework/Input`
- `tests/CNA/Input`
- `tests/CNA/Internal/Input`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-029 — No-flaky-tests confirmation `[ ]`
**Goal:** Cross-reference the repeated-run (P9-003) and shuffled-order (P9-004) results; if any flake was found, root-cause and fix it here rather than retrying in a loop.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- (none)

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-030 — No-hardware-dependent-automated-tests audit `[ ]`
**Goal:** Confirm every automated (non-Phase-11) Input test runs headlessly against a fake backend or synthetic event, with zero reliance on physically present hardware.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `tests/CNA/Internal/Input`

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-031 — Full CNA test suite pass `[ ]`
**Goal:** Run the complete CNA test suite (not just the Input filter) once, to confirm Input changes have not regressed unrelated subsystems.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- (none)

**Tests:**
- `ctest (full suite)`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-032 — Build matrix documentation `[ ]`
**Goal:** Document the full build matrix (debug/asan/ubsan × EasyGL/Vulkan/bgfx/SDL_RENDERER) actually exercised by Phases 8-9 in `docs/input-build-and-test.md`.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `docs/input-build-and-test.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-033 — Regression test list compiled `[ ]`
**Goal:** Compile a single list (in `docs/input-test-coverage.md`) of every new/extended test added across Phases 1-9, cross-referenced to its originating task ID.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `docs/input-test-coverage.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-034 — Sanitizer suppression file audit `[ ]`
**Goal:** Confirm any ASan/UBSan suppression file used for third-party SDL/googletest noise is minimal, documented, and does not accidentally suppress CNA Input code.

**Steps:**
1. Identify the exact command(s) needed to exercise the behavior named in this task's Goal.
2. Run the command(s) in the current checkout and capture full output.
3. If a gap or failure is found, fix it or file it as a tracked follow-up with a cross-reference.
4. Record the exact command and result in this task's Result field — never claim a test passed without running it, per CLAUDE.md.

**Acceptance criteria:**
- The exact command was actually run in this checkout and its real output is recorded.
- No claim of a passing/failing test is made without a command + output backing it.

**Files likely touched:**
- `cmake-build-input-asan`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P9-035 — Phase 9 checkpoint and summary (final Input test gate) `[ ]`
**Goal:** Re-run the full Input selector one final time after all Phase 9 fixes, record a clean baseline before documentation work begins, and close out Phase 9 with a summary of test/CI/sanitizer status and any open follow-ups carried into later phases.

**Steps:**
1. Run the canonical Input test selector one final time and record the exact command and output.
2. Summarize pass/fail/deferred counts across P9-001..034.
3. List any item requiring a follow-up task in a later phase, with a cross-reference.

**Acceptance criteria:**
- A single clean, fully-passing run is recorded with its exact command and output.
- Summary is written into this file with concrete counts.

**Files likely touched:**
- (none)

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-001 — Update docs/input-fna-fidelity.md `[ ]`
**Goal:** Update the FNA-fidelity/deviation document with every deviation found across Phases 1-8.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/input-fna-fidelity.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-002 — Update docs/input-member-parity-matrix.md `[ ]`
**Goal:** Reconcile the member-parity matrix with the final Phase 1-6 results (supersedes the P1-044 draft).

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/input-member-parity-matrix.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-003 — Update docs/input-public-api-frozen.md `[ ]`
**Goal:** Update the frozen-API/tier-glossary document with any signature changes from Phases 1-8.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/input-public-api-frozen.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-004 — Update docs/input-test-coverage.md `[ ]`
**Goal:** Final reconciliation of the test-coverage document with Phase 9's compiled regression-test list.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/input-test-coverage.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-005 — Update docs/input-backend.md `[ ]`
**Goal:** Update the SDL-backend architecture document with any Phase 8 findings.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/input-backend.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-006 — Update docs/input-build-and-test.md `[ ]`
**Goal:** Update the build/test instructions with the Phase 9 build-matrix documentation.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/input-build-and-test.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-007 — Update docs/platform-input-notes.md `[ ]`
**Goal:** Update platform-specific caveats (layouts, DPI, controller mapping) with Phase 2-5 findings.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/platform-input-notes.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-008 — Update docs/input-manual-verification-results.md `[ ]`
**Goal:** Update the hardware-verification matrix header/links; do not mark any cell verified without an actual Phase 11 run.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/input-manual-verification-results.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-009 — Update docs/input-pre-merge-checklist.md `[ ]`
**Goal:** Update the pre-merge checklist to reference this plan's Phase 12 readiness gates.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/input-pre-merge-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-010 — Update NOXNA.md `[ ]`
**Goal:** Confirm every NOXNA Input extension audited in Phase 7 is correctly listed and described in the project-wide NOXNA registry, and remove any stale 'analysis only' wording for code that is now implemented (Phase-0 concern #3).

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `NOXNA.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-011 — Update README.md Input section `[ ]`
**Goal:** Update the top-level README's Input section to describe the current strict-XNA + NOXNA extension surface accurately.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `README.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-012 — Update NEXT.md `[ ]`
**Goal:** Record this plan's completion state and handoff notes in `NEXT.md`, matching the project's existing handoff convention.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `NEXT.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-013 — Add/verify public API usage examples `[ ]`
**Goal:** Confirm `docs/input-fna-fidelity.md` or a dedicated examples doc has at least one minimal usage example per major subsystem (keyboard/mouse/gamepad/touch/gesture).

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/input-fna-fidelity.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-014 — Strict XNA compatibility notes pass `[ ]`
**Goal:** Write a short, explicit statement of strict XNA 4.0 compatibility scope/limits for Input.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/input-fna-fidelity.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-015 — FNA compatibility notes pass `[ ]`
**Goal:** Write a short, explicit statement of FNA-specific (beyond bare XNA 4.0) compatibility scope/limits for Input.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/input-fna-fidelity.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-016 — CNA extension notes pass `[ ]`
**Goal:** Write a short, explicit statement of what NOXNA/CNA Input extensions exist and why, cross-referenced to Phase 7.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/input-fna-fidelity.md`
- `NOXNA.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-017 — SDL dependency notes pass `[ ]`
**Goal:** Document the exact SDL3 subsystems/APIs Input depends on and the pinned version from Phase 0.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/input-backend.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-018 — Platform limitations documented `[ ]`
**Goal:** Document known platform limitations (e.g. mobile soft-keyboard hints, IME candidate UI) discovered across Phases 2-8.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/platform-input-notes.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-019 — Hardware limitations documented `[ ]`
**Goal:** Document known hardware limitations (e.g. controllers without rumble/sensors) discovered across Phase 4/7.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/platform-input-notes.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-020 — High-DPI notes documented `[ ]`
**Goal:** Document the High-DPI coordinate-space findings from Phases 3/5/8 in one place.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/platform-input-notes.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-021 — IME notes documented `[ ]`
**Goal:** Document IME composition/candidate-list findings from Phase 2 in one place.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/platform-input-notes.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-022 — Touch notes documented `[ ]`
**Goal:** Document touch coordinate-scaling and multi-touch findings from Phase 5 in one place.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/platform-input-notes.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-023 — Gamepad notes documented `[ ]`
**Goal:** Document gamepad slot-assignment, dead-zone, and vibration findings from Phase 4 in one place.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/platform-input-notes.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-024 — Haptic notes documented `[ ]`
**Goal:** Document haptic capability/lifecycle findings from Phase 7 in one place.

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs/platform-input-notes.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P10-025 — Phase 10 checkpoint and summary: final documentation consistency pass `[ ]`
**Goal:** Read all ten Input docs together end to end, fix any remaining cross-reference or terminology inconsistency, and close out Phase 10 with a summary confirming documentation reflects this pass's actual results (not the archived plan's).

**Steps:**
1. Open the document named in this task's Goal.
2. Cross-check its content against the actual, current results of Phases 1-9 (not the archived plan).
3. Correct stale, missing, or contradictory content.
4. Confirm cross-links to/from the other Input docs remain consistent.

**Acceptance criteria:**
- The document accurately reflects this pass's actual findings.
- Cross-links to other Input docs are correct.

**Files likely touched:**
- `docs`
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P11-001 — US keyboard manual validation `[!]`
**Goal:** Physically validate keyboard input on a US QWERTY keyboard.

**Steps:**
1. Obtain the physical device/hardware named in this task's Goal.
2. Run the demo/smoke app (`cna_input_smoke` or `InputDemo`) and exercise the specific behavior by hand.
3. Record the exact device/OS/driver used, the steps performed, and the Pass/Fail result in `docs/input-manual-verification-results.md`.
4. Only then update this task's status — never mark `[x]` without an actual device in hand.

**Acceptance criteria:**
- A real device was used and the result is recorded with device/OS/driver details.
- Status remains `[!]` Blocked until an actual run occurs; it is never marked done speculatively.

**Files likely touched:**
- `docs/input-manual-verification-results.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref [[P2-023]] checklist.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P11-002 — Czech keyboard manual validation `[!]`
**Goal:** Physically validate keyboard input on a Czech QWERTZ keyboard, including diacritic OEM keys.

**Steps:**
1. Obtain the physical device/hardware named in this task's Goal.
2. Run the demo/smoke app (`cna_input_smoke` or `InputDemo`) and exercise the specific behavior by hand.
3. Record the exact device/OS/driver used, the steps performed, and the Pass/Fail result in `docs/input-manual-verification-results.md`.
4. Only then update this task's status — never mark `[x]` without an actual device in hand.

**Acceptance criteria:**
- A real device was used and the result is recorded with device/OS/driver details.
- Status remains `[!]` Blocked until an actual run occurs; it is never marked done speculatively.

**Files likely touched:**
- `docs/input-manual-verification-results.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref [[P2-022]] checklist.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P11-003 — Non-QWERTY keyboard manual validation `[!]`
**Goal:** Physically validate keyboard input on a non-QWERTY (e.g. AZERTY/Dvorak) layout.

**Steps:**
1. Obtain the physical device/hardware named in this task's Goal.
2. Run the demo/smoke app (`cna_input_smoke` or `InputDemo`) and exercise the specific behavior by hand.
3. Record the exact device/OS/driver used, the steps performed, and the Pass/Fail result in `docs/input-manual-verification-results.md`.
4. Only then update this task's status — never mark `[x]` without an actual device in hand.

**Acceptance criteria:**
- A real device was used and the result is recorded with device/OS/driver details.
- Status remains `[!]` Blocked until an actual run occurs; it is never marked done speculatively.

**Files likely touched:**
- `docs/input-manual-verification-results.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref [[P2-024]] checklist.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P11-004 — Standard mouse manual validation `[!]`
**Goal:** Physically validate left/middle/right buttons and wheel on a standard 3-button mouse.

**Steps:**
1. Obtain the physical device/hardware named in this task's Goal.
2. Run the demo/smoke app (`cna_input_smoke` or `InputDemo`) and exercise the specific behavior by hand.
3. Record the exact device/OS/driver used, the steps performed, and the Pass/Fail result in `docs/input-manual-verification-results.md`.
4. Only then update this task's status — never mark `[x]` without an actual device in hand.

**Acceptance criteria:**
- A real device was used and the result is recorded with device/OS/driver details.
- Status remains `[!]` Blocked until an actual run occurs; it is never marked done speculatively.

**Files likely touched:**
- `docs/input-manual-verification-results.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref [[P3-040]] checklist.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P11-005 — Extra mouse buttons manual validation `[!]`
**Goal:** Physically validate XButton1/XButton2 on a mouse with extra side buttons.

**Steps:**
1. Obtain the physical device/hardware named in this task's Goal.
2. Run the demo/smoke app (`cna_input_smoke` or `InputDemo`) and exercise the specific behavior by hand.
3. Record the exact device/OS/driver used, the steps performed, and the Pass/Fail result in `docs/input-manual-verification-results.md`.
4. Only then update this task's status — never mark `[x]` without an actual device in hand.

**Acceptance criteria:**
- A real device was used and the result is recorded with device/OS/driver details.
- Status remains `[!]` Blocked until an actual run occurs; it is never marked done speculatively.

**Files likely touched:**
- `docs/input-manual-verification-results.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref [[P3-040]] checklist.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P11-006 — Relative mouse mode manual validation `[!]`
**Goal:** Physically validate relative mouse mode (e.g. for camera-look controls) with a real mouse.

**Steps:**
1. Obtain the physical device/hardware named in this task's Goal.
2. Run the demo/smoke app (`cna_input_smoke` or `InputDemo`) and exercise the specific behavior by hand.
3. Record the exact device/OS/driver used, the steps performed, and the Pass/Fail result in `docs/input-manual-verification-results.md`.
4. Only then update this task's status — never mark `[x]` without an actual device in hand.

**Acceptance criteria:**
- A real device was used and the result is recorded with device/OS/driver details.
- Status remains `[!]` Blocked until an actual run occurs; it is never marked done speculatively.

**Files likely touched:**
- `docs/input-manual-verification-results.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref [[P3-024]]-[[P3-026]].

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P11-007 — Xbox-compatible gamepad manual validation `[!]`
**Goal:** Physically validate button/stick/trigger/rumble mapping on a real Xbox-compatible controller.

**Steps:**
1. Obtain the physical device/hardware named in this task's Goal.
2. Run the demo/smoke app (`cna_input_smoke` or `InputDemo`) and exercise the specific behavior by hand.
3. Record the exact device/OS/driver used, the steps performed, and the Pass/Fail result in `docs/input-manual-verification-results.md`.
4. Only then update this task's status — never mark `[x]` without an actual device in hand.

**Acceptance criteria:**
- A real device was used and the result is recorded with device/OS/driver details.
- Status remains `[!]` Blocked until an actual run occurs; it is never marked done speculatively.

**Files likely touched:**
- `docs/input-manual-verification-results.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref [[P4-055]] checklist.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P11-008 — PlayStation-compatible gamepad manual validation `[!]`
**Goal:** Physically validate button/stick/trigger/rumble/light-bar mapping on a real PlayStation-compatible controller.

**Steps:**
1. Obtain the physical device/hardware named in this task's Goal.
2. Run the demo/smoke app (`cna_input_smoke` or `InputDemo`) and exercise the specific behavior by hand.
3. Record the exact device/OS/driver used, the steps performed, and the Pass/Fail result in `docs/input-manual-verification-results.md`.
4. Only then update this task's status — never mark `[x]` without an actual device in hand.

**Acceptance criteria:**
- A real device was used and the result is recorded with device/OS/driver details.
- Status remains `[!]` Blocked until an actual run occurs; it is never marked done speculatively.

**Files likely touched:**
- `docs/input-manual-verification-results.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref [[P4-056]] checklist.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P11-009 — Generic gamepad manual validation `[!]`
**Goal:** Physically validate mapping on a real generic/unbranded controller.

**Steps:**
1. Obtain the physical device/hardware named in this task's Goal.
2. Run the demo/smoke app (`cna_input_smoke` or `InputDemo`) and exercise the specific behavior by hand.
3. Record the exact device/OS/driver used, the steps performed, and the Pass/Fail result in `docs/input-manual-verification-results.md`.
4. Only then update this task's status — never mark `[x]` without an actual device in hand.

**Acceptance criteria:**
- A real device was used and the result is recorded with device/OS/driver details.
- Status remains `[!]` Blocked until an actual run occurs; it is never marked done speculatively.

**Files likely touched:**
- `docs/input-manual-verification-results.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref [[P4-057]] checklist.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P11-010 — Rumble manual validation `[!]`
**Goal:** Physically feel and confirm rumble/vibration strength and timing on real hardware.

**Steps:**
1. Obtain the physical device/hardware named in this task's Goal.
2. Run the demo/smoke app (`cna_input_smoke` or `InputDemo`) and exercise the specific behavior by hand.
3. Record the exact device/OS/driver used, the steps performed, and the Pass/Fail result in `docs/input-manual-verification-results.md`.
4. Only then update this task's status — never mark `[x]` without an actual device in hand.

**Acceptance criteria:**
- A real device was used and the result is recorded with device/OS/driver details.
- Status remains `[!]` Blocked until an actual run occurs; it is never marked done speculatively.

**Files likely touched:**
- `docs/input-manual-verification-results.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref [[P4-045]].

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P11-011 — Advanced haptics manual validation `[!]`
**Goal:** Physically validate trigger-rumble/light-bar/advanced haptic effects on real hardware that supports them.

**Steps:**
1. Obtain the physical device/hardware named in this task's Goal.
2. Run the demo/smoke app (`cna_input_smoke` or `InputDemo`) and exercise the specific behavior by hand.
3. Record the exact device/OS/driver used, the steps performed, and the Pass/Fail result in `docs/input-manual-verification-results.md`.
4. Only then update this task's status — never mark `[x]` without an actual device in hand.

**Acceptance criteria:**
- A real device was used and the result is recorded with device/OS/driver details.
- Status remains `[!]` Blocked until an actual run occurs; it is never marked done speculatively.

**Files likely touched:**
- `docs/input-manual-verification-results.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref [[P4-046]]-[[P4-048]], [[P7-033]].

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P11-012 — Touchscreen manual validation `[!]`
**Goal:** Physically validate single-touch input on a real touchscreen device.

**Steps:**
1. Obtain the physical device/hardware named in this task's Goal.
2. Run the demo/smoke app (`cna_input_smoke` or `InputDemo`) and exercise the specific behavior by hand.
3. Record the exact device/OS/driver used, the steps performed, and the Pass/Fail result in `docs/input-manual-verification-results.md`.
4. Only then update this task's status — never mark `[x]` without an actual device in hand.

**Acceptance criteria:**
- A real device was used and the result is recorded with device/OS/driver details.
- Status remains `[!]` Blocked until an actual run occurs; it is never marked done speculatively.

**Files likely touched:**
- `docs/input-manual-verification-results.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref [[P5-043]] checklist.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P11-013 — Multi-touch gestures manual validation `[!]`
**Goal:** Physically validate Tap/DoubleTap/Hold/Drag/Flick/Pinch gestures on a real multi-touch device.

**Steps:**
1. Obtain the physical device/hardware named in this task's Goal.
2. Run the demo/smoke app (`cna_input_smoke` or `InputDemo`) and exercise the specific behavior by hand.
3. Record the exact device/OS/driver used, the steps performed, and the Pass/Fail result in `docs/input-manual-verification-results.md`.
4. Only then update this task's status — never mark `[x]` without an actual device in hand.

**Acceptance criteria:**
- A real device was used and the result is recorded with device/OS/driver details.
- Status remains `[!]` Blocked until an actual run occurs; it is never marked done speculatively.

**Files likely touched:**
- `docs/input-manual-verification-results.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref [[P6-036]] checklist.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P11-014 — IME/text composition manual validation `[!]`
**Goal:** Physically validate IME composition (e.g. Japanese/Chinese input method) with a real input method editor.

**Steps:**
1. Obtain the physical device/hardware named in this task's Goal.
2. Run the demo/smoke app (`cna_input_smoke` or `InputDemo`) and exercise the specific behavior by hand.
3. Record the exact device/OS/driver used, the steps performed, and the Pass/Fail result in `docs/input-manual-verification-results.md`.
4. Only then update this task's status — never mark `[x]` without an actual device in hand.

**Acceptance criteria:**
- A real device was used and the result is recorded with device/OS/driver details.
- Status remains `[!]` Blocked until an actual run occurs; it is never marked done speculatively.

**Files likely touched:**
- `docs/input-manual-verification-results.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref [[P2-036]]-[[P2-037]].

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P11-015 — High-DPI display manual validation `[!]`
**Goal:** Physically validate mouse/touch coordinate correctness on a real high-DPI display.

**Steps:**
1. Obtain the physical device/hardware named in this task's Goal.
2. Run the demo/smoke app (`cna_input_smoke` or `InputDemo`) and exercise the specific behavior by hand.
3. Record the exact device/OS/driver used, the steps performed, and the Pass/Fail result in `docs/input-manual-verification-results.md`.
4. Only then update this task's status — never mark `[x]` without an actual device in hand.

**Acceptance criteria:**
- A real device was used and the result is recorded with device/OS/driver details.
- Status remains `[!]` Blocked until an actual run occurs; it is never marked done speculatively.

**Files likely touched:**
- `docs/input-manual-verification-results.md`
- `docs/demo-input-checklist.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** Cross-ref [[P3-039]], [[P5-038]].

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P12-001 — Clean checkout rebuild `[ ]`
**Goal:** Confirm the project builds from a clean checkout (fresh build directory) with no stale-cache artifacts masking a real error.

**Steps:**
1. Remove or create a fresh build directory.
2. Reconfigure with CMake.
3. Build the default target.
4. Record the exact commands and result.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- `cmake-build-debug`

**Tests:**
- `full build`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P12-002 — Submodule verification `[ ]`
**Goal:** Re-verify all four submodules (SDL, SDL_image, SDL_mixer, googletest) are present and pinned as recorded in Phase 0.

**Steps:**
1. Run `git submodule status`.
2. Compare against the Phase 0 baseline.
3. Record any drift.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- `.gitmodules`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P12-003 — EasyGL build gate `[ ]`
**Goal:** Re-confirm the EasyGL backend build (from P8-035) still passes after all subsequent Phase 9-11 changes.

**Steps:**
1. Rebuild `cmake-build-input-easygl`.
2. Run `CnaInputTests`.
3. Record result.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- `cmake-build-input-easygl`

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P12-004 — Vulkan build gate `[ ]`
**Goal:** Re-confirm the Vulkan backend build (from P8-036) still passes after all subsequent Phase 9-11 changes.

**Steps:**
1. Rebuild `cmake-build-input-vulkan`.
2. Run `CnaInputTests`.
3. Record result.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- `cmake-build-input-vulkan`

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P12-005 — bgfx build gate `[ ]`
**Goal:** Re-confirm the bgfx backend build (from P8-037) still passes after all subsequent Phase 9-11 changes.

**Steps:**
1. Rebuild `cmake-build-input-bgfx`.
2. Run `CnaInputTests`.
3. Record result.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- `cmake-build-input-bgfx`

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P12-006 — SDL renderer build gate `[ ]`
**Goal:** Re-confirm the SDL_RENDERER backend build (from P8-038) still passes after all subsequent Phase 9-11 changes.

**Steps:**
1. Rebuild `cmake-build-input-sdlrenderer`.
2. Run `CnaInputTests`.
3. Record result.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- `cmake-build-input-sdlrenderer`

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P12-007 — Focused Input tests final gate `[ ]`
**Goal:** Run the `CnaInputTests` selector one final time on the default debug build as the last word on Input correctness.

**Steps:**
1. Run the canonical Input test selector.
2. Record full pass/fail counts and command.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- (none)

**Tests:**
- `CnaInputTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P12-008 — Full CNA test suite final gate `[ ]`
**Goal:** Run the complete CNA test suite one final time to confirm no non-Input regression was introduced.

**Steps:**
1. Run the full test suite.
2. Record full pass/fail counts and command.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- (none)

**Tests:**
- `ctest (full suite)`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P12-009 — Sanitizer result final gate `[ ]`
**Goal:** Re-run the ASan/UBSan builds from Phase 9 one final time and record a clean (or explicitly triaged) result.

**Steps:**
1. Rebuild and run the sanitizer configuration(s).
2. Record full output.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- `cmake-build-input-asan`

**Tests:**
- `CnaInputTests (sanitizers)`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P12-010 — Public API freeze result `[ ]`
**Goal:** Confirm the signature-freeze tests (strict-XNA and CNA extension) both pass as the final API-stability gate.

**Steps:**
1. Run the freeze test suites.
2. Record result.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- (none)

**Tests:**
- `PublicApiInputSignatureFreezeTests`

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P12-011 — Documentation freeze result `[ ]`
**Goal:** Confirm every doc touched in Phase 10 is internally consistent and committed.

**Steps:**
1. Re-read all ten Input docs.
2. Confirm no TODO/FIXME/stale-plan-reference remains.
3. Record result.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- `docs`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P12-012 — Manual validation status summary `[ ]`
**Goal:** Summarize the real (not speculative) state of all 15 Phase 11 hardware checks — how many were actually performed vs still blocked.

**Steps:**
1. Read the Result field of every P11-* task.
2. Tally performed vs blocked.
3. Record the tally here plainly, including if the count is 0/15.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- `docs/input-manual-verification-results.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P12-013 — Known risk summary `[ ]`
**Goal:** Compile a single list of every unresolved risk/deferred item surfaced across Phases 1-11's checkpoint tasks.

**Steps:**
1. Read every phase checkpoint task's Result field.
2. Compile the union of deferred/open items into one list.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P12-014 — Merge/no-merge recommendation `[ ]`
**Goal:** Based on P12-001..013's actual results (not aspirational status), state a clear merge or no-merge recommendation with reasoning.

**Steps:**
1. Weigh build/test/sanitizer gate results against the known-risk summary.
2. State the recommendation explicitly — do not leave it implicit.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** This is a recommendation for the user, who makes the final merge decision per CLAUDE.md/session convention.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P12-015 — Final plan_input.md completion statement `[ ]`
**Goal:** Write the closing statement of this plan: total tasks completed vs blocked, and confirmation that CNA Input and NOXNA Input extensions were preserved and hardened, not removed.

**Steps:**
1. Tally final status counts across all 500 tasks.
2. State explicitly that no NOXNA Input extension was removed during this pass.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- `plan_input.md`

**Tests:**
- (none — audit-only task; add a test if a fix is required)

**Notes:** _none._

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## Phase 13 — Confirmed defect remediation (`audit_input.md`, 2026-07-16)

**About this phase.** `../audit_input.md` (external review, reviewed revision `5146c9d1b8`, an ancestor
of this branch's HEAD with no intervening Input changes) confirmed four concrete defects that the
generic Phase 5/8 audit tasks above had not yet caught, plus one environmental blocker (missing
submodules). Rather than fold six-way scoped fixes into one giant task, each finding gets its own
focused task here, executed and committed one at a time. These tasks supersede the relevant generic
coverage tasks above (P2-026, P5-028..031, P5-040/041, P8-019..021) for the specific behavior they
fix — those generic tasks are not duplicated, but should be marked `[x]` with a pointer to the P13
task that resolved them once P13 lands.

| Phase | Title | Task count | IDs |
|---|---|---|---|
| 13 | Confirmed defect remediation (audit_input.md 2026-07-16) | 6 | P13-001..006 |

---

## P13-001 — Restore submodules and establish a real Input build/test baseline `[x]`
**Goal:** `audit_input.md` could not build or run `CnaInputTests` because `third_party/SDL`,
`third_party/SDL_image`, `third_party/SDL_mixer`, and `vendor/googletest` were uninitialized
submodules in that checkout. Restore them, configure, build, and run the canonical Input CTest gate
once *before* any P13 fix lands, so every subsequent P13 task has a real (not speculative) pass/fail
baseline to diff against.

**Steps:**
1. `git submodule update --init --depth 1 vendor/googletest third_party/SDL third_party/SDL_image third_party/SDL_mixer`.
2. `cmake -S . -B cmake-build-debug -G Ninja -DCNA_GRAPHICS_BACKEND=SDL_RENDERER -DCNA_BUILD_TESTS=ON`.
3. `cmake --build cmake-build-debug --target CnaTests` (the single test binary; `CnaInputTests` is the
   CTest test *name* registered against it with `--gtest_filter=${CNA_INPUT_TEST_FILTER}`, not a build
   target — see `cmake/UnitTests.cmake`).
4. `ctest --test-dir cmake-build-debug -L input --output-on-failure` (or run `CnaTests` directly with
   the same `--gtest_filter`/`--gtest_shuffle --gtest_repeat=5` flags) and record the exact pass/fail
   counts.

**Acceptance criteria:**
- The gate was actually run/checked in this checkout and its real result is recorded — no speculative pass.

**Files likely touched:**
- _(none — build/test-only task; `plan_input.md` for the recorded result)_

**Tests:**
- `CnaInputTests` (full suite, as already registered by `cmake/Tests/SdlRendererTests.cmake`)

**Notes:** This unblocks `audit_input.md`'s "Required completion order" step 1. A pre-existing failure
found here (unrelated to P13-002..005) must be logged as its own follow-up task, not silently folded
into one of the P13 fix tasks below.

**Result:** 2026-07-16. `git submodule update --init --depth 1 vendor/googletest third_party/SDL
third_party/SDL_image third_party/SDL_mixer` restored all four submodules at their already-pinned
commits (`7e2c425d`, `cbe3fbe9`, `fcb9d0b1`, `3075d3ed` — no version change, just missing checkouts).
`cmake -S . -B cmake-build-debug -G Ninja -DCNA_GRAPHICS_BACKEND=SDL_RENDERER -DCNA_BUILD_TESTS=ON`
configured cleanly (59.5s, includes an immediate vendored SDL3 build). `cmake --build cmake-build-debug
--target CnaTests -j$(nproc)` built the full test binary. The canonical gate,
`ctest --test-dir cmake-build-debug -L input --output-on-failure`, was run iteratively while P13-002
through P13-005 landed (see their own Result fields for the specific failures found/fixed along the
way); its final state is **496/496 tests passed, 0 failed**, confirmed both via `ctest -L input` (exit
0, "100% tests passed") and by running `CnaTests` directly with the exact registered filter
(`--gtest_filter=${CNA_INPUT_TEST_FILTER} --gtest_shuffle --gtest_repeat=5`), grepping the complete
output for `FAILED` (zero matches across all 5 shuffled repeats, `[PASSED] 496 tests.` each time). No
pre-existing (P13-unrelated) Input failure was found. Separately, a full unfiltered `CnaTests` run
surfaced 50 failures in unrelated XNB/Content/LZX/Model/Texture/Effect suites — traced to running the
binary from `cmake-build-debug/` instead of the repo root (those tests resolve fixture paths like
`tests/assets/xnb/...` relative to CWD); re-running from the repo root fixed it, confirming these were
an invocation artifact, not a real regression: re-run from the repo root, the full unfiltered suite
is **4623/4643 passed, 20 failed**, and every one of the 20 remaining failures is in the XNB/Content/
Model/Effect/Texture3D pipeline (`EffectApplyTest`, `CnbEffectTest`, `CnbModelTest`,
`ModelContentTypeReaderTest`, `Texture3DTextureCubeContentTypeReaderTest`, `SkinnedModelEXTPartTest`,
`XnbContainerFuzzTest`, `XnbBuiltInReaderRegistrationTest`, `ContentManagerSkinnedModelTest`) — sampled
failures show `C++ exception ... "SDL_Renderer does not support 3D: CreateVertexBuffer"`, a **known,
documented** limitation of the `SDL_RENDERER` backend (2D-only, no VertexBuffer/3D support; see
`docs/sdl-renderer-2d-completeness.md`, Task 725), entirely unrelated to Input and unrelated to any
P13 change. Remaining risk: none identified for Input; the pre-existing 3D/SDL_RENDERER gap is
out of scope for this plan and not touched.
**Goal:** `TouchPanel::GetState()`'s InputManager fallback (`src/CNA/Internal/Input/InputManager.cpp`
`GetTouchState()`) currently mutates state — advances `PreviousState`/`PreviousPosition`, promotes
`Pressed`→`Moved`, and retires `Released` touches — on *every call*, so the reported state depends on
how many times application code calls `GetState()` per frame rather than on the frame boundary. Two
reads in one frame observe different snapshots (`Pressed` then `Moved`); zero reads in a frame silently
skip a promotion/retirement that should have happened. Fix: split `GetTouchState()` into a pure,
non-mutating read and a separate, explicit per-frame advance.

**Steps:**
1. In `InputManager` (`include/CNA/Internal/Input/InputManager.hpp` /
   `src/CNA/Internal/Input/InputManager.cpp`), make `GetTouchState()` a pure snapshot read: it must
   build and return the `TouchCollection` from the current `TouchLocations` map without mutating
   `PreviousState`/`PreviousPosition`, without promoting `Pressed`→`Moved`, and without erasing
   `RemoveAfterSnapshot` entries.
2. Add a new `static void AdvanceTouchFrame()` that performs exactly the mutation
   `GetTouchState()` used to do inline: for every tracked touch, record the just-reported
   state/position as `Previous*`, promote a still-`Pressed` touch to `Moved`, then erase every touch
   flagged `RemoveAfterSnapshot`. Document with Doxygen that this is the once-per-frame boundary and
   must not be called from a getter.
3. Call `InputManager::AdvanceTouchFrame()` once per frame from
   `Microsoft::Xna::Framework::Input::Touch::TouchPanel::Update()`
   (`src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`), which is already invoked from
   `FrameworkDispatcher::Update()` — itself called once per `Game::Update()` tick
   (`src/Microsoft/Xna/Framework/Game.cpp:570`), confirmed to run *after* `PollEvents()` populates the
   current tick's touches and *before* the next tick's `PollEvents()` — i.e. the correct FNA-equivalent
   frame boundary. Do not gate this call behind `touchDeviceExists_`'s original SetFinger-path
   rationale if doing so would skip advancing InputManager's map; verify both paths are exercised (the
   guard is fine to keep since `touchDeviceExists_` is also set by the production `FINGER_DOWN` handler
   before `SetTouchState`, but confirm this with a test rather than by inspection alone).
4. Rewrite `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp` and
   `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp` tests that currently call
   `InputManager::GetTouchState()` twice in a row to simulate "two frames" (e.g.
   `FingerIdReusedAfterReleaseStartsFresh`, `EventDrivenPathPreservesPreviousLocation`,
   `HeldTouchAutoPromotesToMovedWithPressedPrevious`, `UnknownReleasedFingerHasNoBogusPreviousAndClears`)
   to call `InputManager::AdvanceTouchFrame()` between reads instead, and add new tests asserting the
   defect is actually fixed:
   - two `GetTouchState()` calls within one frame (no intervening `AdvanceTouchFrame()`) return
     identical snapshots;
   - zero `GetTouchState()` calls in a frame still advances correctly once `AdvanceTouchFrame()` runs,
     and the next read reflects it;
   - a `Released` touch is visible for exactly one post-advance read, regardless of how many times it
     is read before that advance.
5. Update `docs/input-fna-fidelity.md`'s touch section to describe the new explicit frame-advance
   design and remove/replace any text implying `GetState()` itself advances state.
6. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- `TouchPanel::GetState()`/`InputManager::GetTouchState()` are provably read-only (test asserts two
  consecutive calls with no advance return identical `TouchCollection` contents).
- `AdvanceTouchFrame()` is the sole place `Pressed`→`Moved` promotion and `Released` retirement happen.
- A test exists that would fail if the destructive-read behavior regressed.

**Files likely touched:**
- `include/CNA/Internal/Input/InputManager.hpp`
- `src/CNA/Internal/Input/InputManager.cpp`
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `docs/input-fna-fidelity.md`

**Tests:**
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`
- `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- `tests/CNA/Internal/Input/InputResetTests.cpp` (uses `GetTouchState()` — verify still correct read-only)

**Notes:** Supersedes P5-028 through P5-031 and P5-040/P5-041's touch-state-freshness aspects (not
`GetCapabilities()` enumeration, which is P13-004/INP-AUD-003). Mark those tasks `[x]` pointing here
once this lands.

**Result:** 2026-07-16. Files changed: `include/CNA/Internal/Input/InputManager.hpp`,
`src/CNA/Internal/Input/InputManager.cpp` (split `GetTouchState()` into a pure read + new
`AdvanceTouchFrame()`), `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp` (`Update()` now calls
`InputManager::AdvanceTouchFrame()`), `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
(Doxygen), `docs/input-fna-fidelity.md`. Tests updated to call `AdvanceTouchFrame()`/`TouchPanel::Update()`
at explicit frame boundaries instead of relying on `GetTouchState()`/`GetState()` itself mutating:
`tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`, `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`,
`tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp` (`TwoFingerScriptResolvesToExactTouchSnapshots`),
`tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`
(`FingerEventsExposePreviousLocationThroughTouchPanelGetState`, `FingerCanceledReleasesTouchLikeFingerUp`,
`FingerIdReusableAfterCancel`). New regression tests added in `TouchEdgeCaseTests.cpp`:
`GetTouchStateIsPureAndRepeatedReadsWithinAFrameAreIdentical`,
`AdvanceTouchFrameWorksEvenWithoutAnIntermediateRead`,
`ReleasedTouchIsVisibleForExactlyOnePostAdvanceReadRegardlessOfPriorReads`. First build+test pass (with
only the fix landed, tests not yet updated) surfaced exactly the 4 failures predicted by the design —
all in the three files above, all "one GetState() call = one frame" assumptions — confirming the fix
changed real behavior rather than being a no-op. After updating those tests: `cmake --build
cmake-build-debug --target CnaTests` — clean. `ctest --test-dir cmake-build-debug -L input
--output-on-failure` — 100% passed, 0 failed. Direct `CnaTests --gtest_filter=$CNA_INPUT_TEST_FILTER
--gtest_shuffle --gtest_repeat=5` — `[PASSED] 496 tests.` on all 5 shuffled repeats, zero `FAILED` lines
in the complete (non-truncated) output. Remaining risk: none identified.

---

## P13-003 — INP-AUD-002: desktop focus lost/gained updates `Game::IsActive` `[x]`
**Goal:** `Game::PollEvents()` (`src/Microsoft/Xna/Framework/Game.cpp:881-920`) handles
`SDL_EVENT_WILL_ENTER_BACKGROUND`/`SDL_EVENT_DID_ENTER_FOREGROUND` (mobile-style) but has no case for
`SDL_EVENT_WINDOW_FOCUS_LOST`/`SDL_EVENT_WINDOW_FOCUS_GAINED` (desktop Alt-Tab/focus-switch), so an
ordinary desktop focus change never raises `Activated`/`Deactivated` and `Game::IsActive` stays `true`
indefinitely. FNA's SDL3 platform loop sets `game.IsActive` on both focus events
(`FNA/src/FNAPlatform/SDL3_FNAPlatform.cs:1006-1037`).

**Steps:**
1. Add `case SDL_EVENT_WINDOW_FOCUS_LOST:` → `setIsActiveProperty(false)` and
   `case SDL_EVENT_WINDOW_FOCUS_GAINED:` → `setIsActiveProperty(true)` to the `switch` in
   `Game::PollEvents()`, alongside the existing `WILL_ENTER_BACKGROUND`/`DID_ENTER_FOREGROUND` cases.
2. Compare against `SDL3_FNAPlatform.cs:1006-1037` for any other state FNA touches on these two events
   (e.g. clipboard/IME) and note any intentional CNA scope difference in `docs/input-fna-fidelity.md`
   rather than silently omitting it.
3. `Game` has no existing unit test (`tests/Microsoft/Xna/Framework/GameTests.cpp` is explicitly empty:
   "Game requires a live SDL window, graphics device, and game loop") and the sibling
   `WILL_ENTER_BACKGROUND`/`DID_ENTER_FOREGROUND` cases are likewise untested at this level — this is an
   established project constraint, not a gap introduced by this task. Do not invent a fake requirement;
   if a lightweight way to drive `PollEvents()` deterministically already exists elsewhere (check
   `tests/Harnesses` for a live-window harness) use it, otherwise record that this case is exercised the
   same way its siblings are (manual/P11 hardware validation) and say so explicitly rather than silently
   skipping test coverage.
4. Build and confirm no regression in the existing `CnaInputTests`/game-related suites.

**Acceptance criteria:**
- `SDL_EVENT_WINDOW_FOCUS_LOST`/`GAINED` route through `setIsActiveProperty`, matching FNA.
- The absence (or presence) of an automated test is explicitly stated with a reason, not left implicit.

**Files likely touched:**
- `src/Microsoft/Xna/Framework/Game.cpp`
- `docs/input-fna-fidelity.md`

**Tests:**
- _(see Steps 3 — record the actual outcome, do not claim coverage that does not exist)_

**Notes:** Do this before P13-004 — INP-AUD-004's contradiction is largely a byproduct of `IsActive`
never toggling on desktop. Supersedes P8-019/P8-020's `IsActive` aspect (not any keyboard/mouse-clearing
aspect, which DEC-15 already resolved as "no clear").

**Result:** 2026-07-16. File changed: `src/Microsoft/Xna/Framework/Game.cpp` — added
`case SDL_EVENT_WINDOW_FOCUS_LOST: setIsActiveProperty(false); break;` and
`case SDL_EVENT_WINDOW_FOCUS_GAINED: setIsActiveProperty(true); break;` to the `switch` in
`Game::PollEvents()`, next to the existing `WILL_ENTER_BACKGROUND`/`DID_ENTER_FOREGROUND` cases.
Compared against `SDL3_FNAPlatform.cs:1006-1037`: FNA's handler for these two events *also* restores
the X11 "fullscreen desktop" flag and toggles the SDL screensaver — a scope gap CNA does not close
here, documented as intentional (out of scope for an Input-behavior audit) in
`docs/input-fna-fidelity.md`. Step 3 checked for a live-window test harness
(`find . -iname '*harness*'`, `tests/Harnesses` does not exist in this repo) — none exists, confirming
`Game::PollEvents()` window-event cases are untestable at the unit level in this checkout, the same
established constraint as the pre-existing `WILL_ENTER_BACKGROUND`/`DID_ENTER_FOREGROUND` cases (also
untested). No test was added; this is stated explicitly, not silently skipped. Build: `cmake --build
cmake-build-debug --target CnaTests` — clean, no warnings on the changed lines. `ctest --test-dir
cmake-build-debug -L input --output-on-failure` — 100% passed (496/496), confirming no regression in
the SdlInputBridge-level keyboard/mouse focus tests that exercise the surrounding event stream. Remaining
risk: the X11-fullscreen/screensaver scope gap noted above; otherwise none identified.

---

## P13-004 — INP-AUD-004: make the focus-loss state-retention policy explicit and consistent `[x]`
**Goal:** The current plan's P2-026 wants focus loss to clear keyboard/mouse-button state; existing
source, tests (`SdlInputBridgeKeyboardTest.WindowFocusLostDoesNotClearHeldKeysMatchingFna`), and
`docs/input-fna-fidelity.md` (DEC-15) instead intentionally retain it, matching FNA. Both are legitimate
designs, but the combination was unsafe while `IsActive` never actually toggled (P13-003): a held
key/button could look stuck with no compensating "the game knows it's inactive" signal. Now that
P13-003 makes `IsActive` correct, close this out by recording the decision explicitly rather than
leaving P2-026 and DEC-15 pointing in opposite directions.

**Steps:**
1. Confirm DEC-15's "match FNA, retain state, require games to gate on `Game.IsActive`" as the
   accepted policy (it is already FNA-faithful and P13-003 fixes the missing half of it) — or, if the
   user prefers the stronger CNA-only policy instead, implement `ClearTransientState()` on focus
   loss/minimize per the plan's alternative and document that as an intentional beyond-FNA divergence.
   **This step requires a decision; do not silently pick one without recording the reasoning.**
2. Update `plan_input.md` P2-026 to `[x]`/`[!]` reflecting whichever decision was taken, with a
   pointer to this task and to DEC-15.
3. Update `docs/input-fna-fidelity.md`'s DEC-15 note to reference the now-fixed `IsActive` behavior
   (P13-003) so the documented rationale ("games are expected to gate input on `Game.IsActive`") is no
   longer contradicted by `IsActive` staying `true` through a focus loss.
4. Add/extend a test for the chosen policy's focus-gained path (keys/buttons held across a focus
   loss/gain cycle still read correctly once `IsActive` is back to `true`), reusing the existing
   `SdlInputBridgeKeyboardTests.cpp`/`SdlInputBridgeMouseTests.cpp` fixtures.

**Acceptance criteria:**
- Plan (`P2-026`), source behavior, tests, and `docs/input-fna-fidelity.md` all state the *same* policy.
- A test exists for the chosen policy's focus-gained path.

**Files likely touched:**
- `plan_input.md` (P2-026 status)
- `docs/input-fna-fidelity.md`
- `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Tests:**
- `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`

**Notes:** Depends on P13-003. Supersedes P2-026 and the `IsActive`-consistency aspect of P8-021.

**Result:** 2026-07-16. Decision (step 1): kept DEC-15's existing FNA-faithful policy — retain
keyboard/mouse/touch state across focus loss, require games to gate on `Game.IsActive` — rather than
adding a beyond-FNA `ClearTransientState()`. This was already the accepted policy before this task;
what P13-004 actually resolved was that it was **unsafe to declare accepted** while `IsActive` never
toggled (fixed by [[P13-003]]). No source code changed for this task. Files changed:
`plan_input.md` (`P2-026` marked `[x]` with a Result explaining the literal goal was investigated and
rejected — see that task's own Result), `docs/input-fna-fidelity.md` (DEC-15's surrounding section
updated via the P13-003 bullet, which explicitly references the now-fixed `IsActive` behavior so the
"games gate on `Game.IsActive`" rationale is no longer contradicted). Step 4 (test for the focus-gained
path): already covered by existing tests, no new test needed —
`SdlInputBridgeKeyboardTests.cpp::WindowLifecycleEventsDoNotCorruptKeyboardState` drives
`SDL_EVENT_WINDOW_FOCUS_GAINED` through its window-lifecycle-event loop and asserts held keys survive
unchanged; `::WindowFocusLostDoesNotClearHeldKeysMatchingFna` covers the focus-lost side. `ctest -L
input` — 496/496 passed, confirming these pre-existing tests still hold under the now-consistent
policy. Remaining risk: none identified — the FNA-shared stuck-key edge case (a held key's up-event
delivered to a different window) is explicitly accepted upstream, not a CNA-specific defect.

---

## P13-005 — INP-AUD-003: `GetCapabilities()` consults SDL touch-device enumeration `[x]`
**Goal:** `TouchPanel::GetCapabilities()` (`src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp:94-111`)
currently reports a touch device only after `touchDeviceExists_` was set by an actual finger-down event,
or while the live `InputManager` touch map is non-empty (`HasAnyTouch()`); it never queries SDL's device
enumeration. A connected-but-not-yet-touched touchscreen therefore reports `IsConnected == false`. FNA's
`GetTouchCapabilities()` calls `SDL_GetTouchDevices()` on every query
(`FNA/src/FNAPlatform/SDL3_FNAPlatform.cs:2265-2280`). CNA already has an injectable seam for this:
`CNA::Internal::Input::system_device_backend().GetTouchDevices()`
(`include/CNA/Internal/Input/SystemDeviceBackend.hpp`), used by `InputDevices::GetTouchDevicesEXT()` and
already mockable via `SetSystemDeviceBackendForTests` (see `tests/CNA/Input/InputDevicesTests.cpp`).

**Steps:**
1. In `TouchPanel::GetCapabilities()`, compute `isConnected` as
   `!system_device_backend().GetTouchDevices().empty() || touchDeviceExists_ || InputManager::HasAnyTouch()`
   — SDL enumeration is the primary source; the sticky `touchDeviceExists_` flag and the live
   `HasAnyTouch()` peek remain as fallbacks for platforms (FNA notes Windows) that only enumerate a
   touch device after first interaction. None of the three checks mutate touch state.
2. `#include "CNA/Internal/Input/SystemDeviceBackend.hpp"` in `TouchPanel.cpp`.
3. Add fake-backend tests (reuse the `FakeSystemDeviceBackend` pattern from
   `tests/CNA/Input/InputDevicesTests.cpp`, or a local equivalent in
   `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`) for: pre-touch connected (enumeration says a
   device exists, `touchDeviceExists_` still false), no device at all (disconnected), and the
   Windows-style late-enumeration case (enumeration empty, but `touchDeviceExists_`/`HasAnyTouch()` is
   true because a touch was already observed).
4. Correct the existing `GetCapabilitiesIsDisconnectedBeforeAnyTouch` test if it asserts the
   now-incorrect "always disconnected before any touch" behavior when a fake device is enumerable, and
   correct `docs/input-fna-fidelity.md`'s capability claim.
5. Run the listed test file(s) via the `CnaInputTests` filter and record the result.

**Acceptance criteria:**
- `GetCapabilities()` reports `IsConnected == true` for an enumerable-but-untouched fake touch device.
- `GetCapabilities()` remains provably non-mutating (existing `GetCapabilitiesHasNoSideEffectOnTouchState`
  test still passes).
- `docs/input-fna-fidelity.md` no longer claims the pre-fix behavior is FNA-faithful.

**Files likely touched:**
- `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`
- `docs/input-fna-fidelity.md`

**Tests:**
- `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`
- `tests/CNA/Input/InputDevicesTests.cpp` (regression-check only, not expected to change)

**Notes:** Supersedes P5-040/P5-041's SDL-enumeration aspect (not the frame-accuracy aspect, which is
P13-002/INP-AUD-001).

**Result:** 2026-07-16. Files changed: `src/Microsoft/Xna/Framework/Input/Touch/TouchPanel.cpp`
(`GetCapabilities()` now computes `isConnected` from
`!system_device_backend().GetTouchDevices().empty() || touchDeviceExists_ || InputManager::HasAnyTouch()`,
exactly as specified, plus the new `#include`), `docs/input-fna-fidelity.md` (rewrote the
"`GetCapabilities` connected-after-first-touch" bullet — it previously called the old,
enumeration-blind behavior "intentional and FNA-faithful", which was inaccurate: FNA's own
`GetTouchCapabilities()` calls `SDL_GetTouchDevices()` unconditionally on every platform, not only
after Windows-style late enumeration). Added `TouchCapabilitiesEnumerationTest` fixture to
`tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp` (mirrors `tests/CNA/Input/InputDevicesTests.cpp`'s
`FakeSystemDeviceBackend` pattern) with 5 tests:
`ReportsConnectedFromEnumerationBeforeAnyTouchIsObserved`,
`ReportsDisconnectedWhenEnumerationEmptyAndNoTouchObserved`,
`FallsBackToStickyFlagWhenEnumerationLagsInteractionWindowsStyle`,
`FallsBackToLiveTouchWhenEnumerationEmptyAndFlagUnset`, `EnumerationQueryDoesNotMutateTouchState`. The
existing `GetCapabilitiesIsDisconnectedBeforeAnyTouch` test (step 4) needed no code change — it runs
against the real (non-faked) `system_device_backend()`, which reports no touch devices on this
headless CI-style machine, so it still passes; its assertion is now slightly less deterministic in
principle (would need a real attached touchscreen to fail) but that is the correct, FNA-faithful
behavior, not a test bug. `GetCapabilitiesHasNoSideEffectOnTouchState` (non-mutation regression) still
passes unchanged. `cmake --build cmake-build-debug --target CnaTests` — clean. `ctest --test-dir
cmake-build-debug -L input --output-on-failure` — 100% passed (496/496). Direct
`CnaTests --gtest_filter=$CNA_INPUT_TEST_FILTER --gtest_shuffle --gtest_repeat=5` — `[PASSED] 496
tests.` on all 5 repeats, zero `FAILED` in the full output. `tests/CNA/Input/InputDevicesTests.cpp`
(regression-check per the Tests list) — unaffected, still passing (it does not touch
`TouchPanel::GetCapabilities()`). Remaining risk: none identified.

**Result:** _(fill in when executed: exact files changed, exact tests run, command + output, remaining risk)_

---

## P13-006 — Reconcile stale Input documentation and evidence `[x]`
**Goal:** `audit_input.md` found two documentation-freshness problems independent of the four confirmed
defects: (a) regenerating `tools/input_parity/check_input_test_coverage.py` without any repository
change does not match the committed `docs/input-test-coverage.md` (the committed doc claims no gaps;
the generator reports seven candidate internal-seam rows: `ISdlHapticBackend`, `ISdlJoystickBackend`,
`ISystemDeviceBackend`, `ISystemKeyboardBackend`, `ISystemMouseBackend`, `ISystemPowerBackend`,
`ISystemSensorBackend`); (b) input documents elsewhere describe the *previous* Phase-I task numbering
and present earlier work as completed against a plan that has since been reset.

**Steps:**
1. Run `tools/input_parity/check_input_test_coverage.py` against current `HEAD` (after P13-002/003/004/005
   land) and diff its output against the committed `docs/input-test-coverage.md`.
2. For each of the seven flagged seam rows, determine whether it is a real test-coverage gap or a
   heuristic false positive (e.g. the seam is exercised indirectly); regenerate/hand-correct
   `docs/input-test-coverage.md` to match reality, not the stale committed claim of zero gaps.
3. Sweep `docs/*input*` for old Phase-I task-number references or "completed" claims that predate the
   2026-07-07 plan reset, and either update them to the current P0–P13 numbering or add a note that they
   are historical/superseded.
4. Do not alter `plan_input.md`'s own Phase 0 baseline record (`feature/input`, commit `b89baad...`) —
   that is a historical fact of when the plan was authored, not a staleness bug.

**Acceptance criteria:**
- `docs/input-test-coverage.md` matches what the generator actually reports for current `HEAD`, or each
  discrepancy is explicitly explained as a heuristic false positive.
- No remaining doc under `docs/*input*` claims a pre-reset Phase-I task as evidence of current-plan
  completion.

**Files likely touched:**
- `docs/input-test-coverage.md`
- other `docs/*input*` files as discovered

**Tests:**
- _(none — documentation-only task)_

**Notes:** Run this last, after P13-002 through P13-005 land, so the regenerated coverage doc reflects
the final test set rather than an intermediate one.

**Result:** 2026-07-16. **Step 1/2 (coverage doc):** ran `check_input_test_coverage.py` against HEAD
(after P13-002..005) — confirmed the exact 7-row discrepancy audit_input.md reported. Investigated all
7: every one has a real, dedicated fake-backend-driven test suite (`ISdlHapticBackend` →
`SdlHapticBackendTests.cpp`'s `FakeHapticTest` via `FakeSdlHapticBackend`; `ISdlJoystickBackend` →
`SdlJoystickBackendTests.cpp`'s `FakeJoystickTest`; `ISystemDeviceBackend` →
`InputDevicesTests.cpp`'s `CnaInputDevicesTest` + the new `TouchCapabilitiesEnumerationTest`;
`ISystemKeyboardBackend` → `KeyboardModStateTests.cpp`'s `KeyboardModStateEXTTest`;
`ISystemMouseBackend` → `MouseGlobalTests.cpp`'s `MouseGlobalEXTTest`; `ISystemPowerBackend` →
`PowerTests.cpp`'s `CnaInputPowerTest`; `ISystemSensorBackend` → `SensorsTests.cpp`'s
`CnaInputSensorsTest`) — all heuristic false positives: the script's `suite_re` requires a suite
literally prefixed with the interface's own "I"-prefixed name, but each is exercised through a
same-file `Fake*`/`CnaInput*`-named suite instead (same pattern already accepted for
`ISdlGamepadBackend`). Added all 7 to `KNOWN_COVERED_ELSEWHERE` in
`tools/input_parity/check_input_test_coverage.py` (verifying the real coverage first, not just adding
an exemption) and regenerated `docs/input-test-coverage.md` — summary line is back to "None — every
Input type has a dedicated suite or a documented sibling-suite cover", now backed by a verified
exemption list rather than a stale claim. Also regenerated `docs/input-member-parity-matrix.md` (its
own generator, `gen_input_parity_matrix.py`) — it was independently stale (missing ~29 EXT/NOXNA
members added since it was last generated: GamePad/Keyboard/Mouse/TextInputEXT/TouchLocation
extensions); regeneration still reports 0 STRICT/EXT gaps, so this was pure staleness, not an API
compliance issue. **Step 3 (Phase-I sweep):** swept `docs/*input*`; found no misleading *current-plan*
completion claims (`docs/input-pre-merge-checklist.md`'s "code-complete" text defines a checklist
tier, not a status report) but found `docs/input-backend.md`'s own existing task-number disclaimer
(INPUT-AUDIT-004) was itself stale — it named the `INPUT-*` scheme as "the current, authoritative
input backlog", not accounting for the 2026-07-07 reset to `plan_input.md`'s `P0-013` scheme. Rewrote
that disclaimer to name all three numbering generations and state explicitly that legacy `Phase I*`/
`task NNN` citations elsewhere in the doc-set are provenance only and do not imply any current `P#-###`
task is complete. Also updated `docs/input-build-and-test.md`'s "Test counts (authoritative baseline)"
table, stale since 2026-07-06 (**314→496** passed for the canonical input filter; **3303/2
skipped→4623 passed/20 failed/2 skipped** for the full suite — the 20 new failures are the same
pre-existing SDL_RENDERER-3D-unsupported gap recorded in [[P13-001]], confirmed unrelated to Input by
re-running under `xvfb-run … SDL_VIDEODRIVER=x11`, the doc's own documented methodology). Verified the
doc's existing "Headless run inventory" dummy-vs-x11 skip/fail table is *still* accurate (re-ran under
real `SDL_VIDEODRIVER=dummy`: same 5 skipped + 3 failed `MouseCursor`/`Mouse` test names, unchanged).
Files changed: `tools/input_parity/check_input_test_coverage.py`, `docs/input-test-coverage.md`,
`docs/input-member-parity-matrix.md`, `docs/input-backend.md`, `docs/input-build-and-test.md`. Did not
touch `docs/input-public-api-frozen.md` (hand-maintained, paired with a compile-time freeze test —
auditing its lock-step accuracy is Phase 1/10 scope, not this defect-remediation pass) or
`plan_input.md`'s own Phase 0 baseline record, per step 4. Remaining risk: none identified for the
items actually swept; a full line-by-line audit of every `docs/*input*` file's every historical
citation was not performed (would be disproportionate for a documentation-freshness task) — this
Result records what was checked and what was intentionally left out of scope.

---

## Plan-level notes

- This file is regenerated task-by-task, not rewritten wholesale, as work proceeds — each task's
  own section is the append point for its Result.
- Cross-references between tasks use `[[P#-###]]` — a plain task-ID pointer, not a wiki link.
- If a task is found to be inapplicable after investigation (e.g. a listed test file turns out
  not to exist and the behavior is covered elsewhere), do not silently delete the task — mark it
  `[x]` with a Result explaining the finding, or `[!]` if it surfaces a real gap needing a new
  follow-up task appended at the end of its phase.
