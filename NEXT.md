# NEXT.md — CNA Input Handoff

> Concise handoff for resuming work later (Claude Code or a human). Based only on the current repo
> state, recent commits, and this session's build/test runs. No invented features.
>
> **Active branch: `feature/input`.** The authoritative, exhaustive task backlog is **`plan_input.md`**
> (265 tasks; 51 DONE / 214 TODO as of 2026-07-05). This file is a short pointer into it, not a
> replacement. A separate **Graphics** track lives on `develop` with its own `GRAPHICS_TASKS.md` and the
> repo's most severe open bug (see §4) — out of scope for this branch.

## 1. Project summary

- **What:** **CNA** is a C++23 reimplementation of the **XNA 4.0** programming model
  (`Microsoft::Xna::Framework`) on **SDL3**, with a pluggable 3D backend layer (EasyGL/OpenGL ES,
  Vulkan, bgfx, SDL_Renderer). It is a framework/runtime, not a game.
- **Main goal:** full XNA 4.0 API coverage with **FNA-faithful** behavior, backed by unit tests and
  verified line-by-line against the FNA reference at `/rv/data/library/github.com/FNA-XNA/FNA/src`.
- **Current phase (this branch):** Input **stabilization + hardening**. Behavior fixes and edge cases
  are largely closed; the recent focus is **guardrails** (API/ABI drift locks) and **test/CI
  infrastructure**. The public Input API is now frozen on hygiene, signatures, enum values, and tagging.
- **Key architectural decisions:**
  - FNA is the authoritative behavioral reference; deviations are documented in `docs/input-fna-fidelity.md`.
  - Input is **event-driven**: `SdlInputBridge::ProcessEvent` → `InputManager` (+ `TouchPanel`) →
    `Get*State()`. (FNA polls; CNA mutates state at SDL-event time.)
  - Non-XNA members inside `Microsoft::Xna` are tagged `NOXNA` and/or an `EXT` name suffix; `.NET` types
    live in the sibling repo `sharp-runtime`.
  - SDL gamepad calls go through an internal injectable seam (`ISdlGamepadBackend`) so runtime gamepad
    behavior is unit-testable with a fake — never exposed in the public XNA API.

## 2. Current status

- **Build:** clean. Verified backend-agnostic across **EasyGL / Vulkan / bgfx / SDL_RENDERER**
  (INPUT-BUILD-002). This session built `CnaTests` clean on EasyGL and under an ASan+UBSan config.
- **Tests:** the input subset is selected by **`ctest -L input`** (INPUT-BUILD-003) — **301** tests,
  order-independent under the baked-in `--gtest_shuffle --gtest_repeat=3`. The **only** local failures
  are 3 `MouseCursor` tests that need real cursors (the SDL `dummy` video driver can't create them); they
  pass under CI's Xvfb+x11. Full `CnaTests` suite is **3290 passed / 2 skipped**. Authoritative recorded
  counts live in `docs/input-build-and-test.md` (§Test counts), refreshed 2026-07-06 to these numbers
  (fuzz + signature-freeze + enum-parity + INPUT-TEST-008 golden suites now included).
- **CI:** GitHub Actions `.github/workflows/input-ci.yml`, a 5-job matrix
  (EasyGL / SDL_RENDERER / Vulkan / bgfx / ASan+UBSan) on ubuntu-24.04, headless via `xvfb-run`, cloning
  the public SDL submodules + `sharp-runtime`/`easy-gl` siblings. **Green** through `d2adefd8`. The latest
  commit `f0a185ca` **changed the CI run step itself** (now `ctest -L input`) — its run is the current
  watch item (§4).
- **Recently locked (this session):** exhaustive `Keys` value parity + enum-ABI guard; full public-API
  **signature freeze**; EXT/NOXNA **tagging audit**; deterministic **fuzz** of the bridge; the `ctest -L
  input` label. See §3.
- **Does NOT work / not headless-verifiable (by design, not code gaps):** real gamepad **actuation**
  (rumble, trigger haptics, live sensors, OS hot-plug/GUID); real **IME** composition; **Wayland**
  cursor-landing readback (X11-only). All documented as manual/hardware-gated.

## 3. Recent changes (most recent first — this session, all on `feature/input`)

- **INPUT-API-006 / -023 — value-type coverage verified + extended:** confirmed `GamePadButtons` needs no
  `ToString` (FNA `ValueType` default; already fully covered) and closed the two `TouchLocation` gaps —
  added `EqualityDistinguishesPreviousStateAndPosition` and `ToStringMatchesFnaFormatExactly` to
  `TouchLocationTest` (both `TryGetPreviousLocation` paths were already tested). `TouchLocationTest` now 9.
- **INPUT-API-028 — namespace + include-path cross-check + guard:** audited all 26 public headers — every
  top-level type is in `…::Input`, every Touch type in `…::Input::Touch`, path mirrors namespace, matching
  FNA. Added a fully-qualified namespace-placement guard to `PublicApiInputCompileTests.cpp` (negative-
  verified: a Touch type referenced via the Input alias fails to compile).
- **INPUT-API-029 — `GetTypeName()` policy confirmed + guarded:** audited all 26 public headers — no Input
  type inherits `System::Object` (the sole base relationship `MouseCursor : System::IDisposable` is not an
  `Object` subclass), so `GetTypeName()` applies to none; all exempt. Pinned by a
  `static_assert(!std::is_base_of_v<System::Object, T>)` block over the 18 class/struct types in
  `PublicApiInputCompileTests.cpp` (negative-verified). Recorded in `docs/input-public-api-frozen.md`.
- **INPUT-API-027 finding fixed — `KeyboardState::ToString()` retagged `NOXNA`:** FNA `KeyboardState.cs`
  has no `ToString` (unlike `MouseState`/`GamePadState`/`TouchLocation`), so per CLAUDE.md the CNA
  convenience is now `NOXNA` in `KeyboardState.hpp` + `docs/input-public-api-frozen.md`. Signature-invariant
  (freeze test INPUT-API-031 still green); the regenerated matrix now reports **0 STRICT/EXT gaps**.
- **INPUT-API-027 — mechanical member-parity matrix:** `tools/input_parity/gen_input_parity_matrix.py`
  parses the 26 public Input headers (member + STRICT/EXT/NOXNA tag) and the FNA `.cs`, emitting
  `docs/input-member-parity-matrix.md` — one table per type with an FNA cross-check column. It reproduces
  the hand-audited `docs/input-public-api-frozen.md` classification mechanically (drift catcher) and
  surfaced exactly one finding: `KeyboardState::ToString()` is STRICT but FNA has no such member → should
  be `NOXNA` (queued as §8 task 2). Script lives outside `src/`; no build/test impact.
- **INPUT-TEST-008 — golden behavior tests:** `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp` —
  4 fixed recorded event scripts driven through `SdlInputBridge::ProcessEvent` with the COMPLETE resulting
  snapshot asserted (keyboard pressed-set, mouse pos/buttons/wheel, two-finger touch snapshots, and one
  interleaved cross-subsystem checkpoint). The correctness anchor to the fuzz test (fuzz = no crash,
  golden = correct output). Deterministic headless; suite name matches the `*SdlInputBridge*` filter token.
- **INPUT-BUILD-003 follow-up — count refresh:** re-ran counts after the fuzz/signature-freeze/enum-parity
  suites; `docs/input-build-and-test.md` (§Test counts) now records 301 input / 3290 full (2026-07-06).
- **`f0a185ca` INPUT-BUILD-003:** single-source-of-truth input filter (`CNA_INPUT_TEST_FILTER` in
  `CMakeLists.txt`) + `add_test(CnaInputTests ... LABELS input)`; CI switched to `ctest -L input`; purged
  the duplicated/drifted filter strings from `docs/input-build-and-test.md`, `docs/input-backend.md` (had
  the stale 7-token filter), and this NEXT.md.
- **`d2adefd8` INPUT-API-032:** EXT/NOXNA tagging audit vs FNA. Found + fixed 2 defects — `GamePadState()`
  and `GestureSample()` default ctors were untagged while their 9 value-struct siblings are `NOXNA`; both
  now `NOXNA`. Convention + audit table documented in `docs/input-public-api-frozen.md`. Files:
  `GamePadState.hpp`, `Touch/GestureSample.hpp`, doc.
- **`2ec61806` INPUT-API-031:** compile-time **signature freeze** of ~250 public members across all 26
  public Input types (`tests/Microsoft/Xna/Framework/Input/PublicApiInputSignatureFreezeTests.cpp`) +
  human-readable golden snapshot `docs/input-public-api-frozen.md`. Removing/renaming/re-signaturing a
  public member now fails to compile (negative-verified).
- **`ca9bd074` INPUT-KBD-001 / INPUT-API-034:** exhaustive 160-entry `Keys` value table
  (`KeyboardInputTests.cpp`), byte-identical to FNA `Keys.cs`; all 8 public Input enums now exhaustively
  value-pinned → renumbering any enum value fails a test.
- **`bcf1b92b` INPUT-TEST-009:** deterministic seeded fuzz of `SdlInputBridge::ProcessEvent`
  (`tests/CNA/Internal/Input/SdlInputBridgeFuzzTests.cpp`) — 5000 edge-case events; clean under ASan+UBSan.
- **`c948f6f1`..`d44435d2` (DEC-06/08/09/10/12/13/14/15/16..21):** FNA-verified behavior decisions
  (multicast EXT callbacks; U+FFFD for malformed UTF-8; touch max-count 4/cap-8; `TryGetPreviousLocation`
  always writes out-param; focus-loss matches FNA; etc.).

## 4. Current blocker / main problem

- **Input track: no hard functional blocker.** It builds on all 4 backends and every input test passes
  (headless: the 3 `MouseCursor` tests require Xvfb, not a bug).
- **Immediate watch item (not a blocker):** commit **`f0a185ca` changed the CI workflow** to
  `ctest -L input`. Its Actions run is the validation of that change.
  - Symptom to watch for: the "Run input tests" step erroring on `ctest --test-dir build -L input`
    (e.g. label not applied, or `gtest_discover_tests` PRE_TEST discovery failing under the sanitizer job).
  - Verify: `ctest --test-dir cmake-build-input-easygl -N -L input` must list exactly **1** test
    (`CnaInputTests`); locally it runs the full input set (only MouseCursor fails under `dummy`).
  - Already checked locally: label selects 1 entry and runs the input subset; YAML validated.
  - If it fails, likely fix is in `.github/workflows/input-ci.yml` (the run step) or the `add_test` in
    `CMakeLists.txt`.
- **Most severe open problem in the repo is on the Graphics track (`develop`), NOT input** — recorded so
  it is not forgotten, but **do not fix here**: `confirmed bug` **Task 870** (Vulkan `DepthStencilState`
  largely non-functional — stencil bypassed, `DepthBufferFunction` ignored), **Task 871** (`Clear` ignores
  `ClearOptions::Stencil`), **Task 872** (`ReferenceStencil` override unwired on all backends). See
  `GRAPHICS_TASKS.md` (868–872) and `docs/depthstencilstate-support.md`.

## 5. Known bugs and limitations (input track)

- `by-design / needs verification` — real-hardware gamepad **actuation** is not headless-verifiable; the
  fake backend proves translation/bookkeeping only. Real controllers untested this session.
- `incomplete` — exhaustive **gesture matrix** (tap/double-tap/hold/drag/flick/pinch × interruption) only
  partially covered (`plan_input.md` gesture tasks).
- `incomplete` — the **member-level behavioral parity matrix** (§5 of `plan_input.md`, INPUT-API-027) is
  still hand-maintained, not mechanically generated; §2.3 confidence there is "Medium". (Enum-value and
  signature parity are now "High" — locked by INPUT-API-034 / INPUT-API-031.)
- Documented **intentional FNA deviations** (not bugs — `docs/input-fna-fidelity.md`):
  `FNA_GAMEPAD_NUM_GAMEPADS` clamped to 4; event-driven `PacketNumber` (within-dead-zone wobble can bump
  it — `not asserted`); partial-field `GamePadState::GetHashCode`; `MaximumTouchCount` reports 4 but the
  event path caps at 8; GUID/caps live vs FNA cached-at-connect.
- `suspected` (cannot occur via CNA's own API) — Mouse relative-mode cache could desync if SDL relative
  mode is toggled outside `Mouse::setIsRelativeMouseModeEXTProperty` (DEC-14 accepted this).
- `resolved 2026-07-06` — the recorded test counts in `docs/input-build-and-test.md` (§Test counts) were
  refreshed to include the fuzz + signature-freeze + enum-parity + golden suites (301 input / 3290 full passed).

## 6. Architecture notes

- **Main modules:**
  - `src/CNA/Internal/Input/SdlInputBridge.cpp` — the single funnel: SDL events → `InputManager` /
    `TouchPanel` / `Mouse` / `TextInputEXT`. Owns gamepad slot maps, finger-id map, UTF-8 decode, env
    parsing, and gamepad-subsystem init.
  - `src/CNA/Internal/Input/InputManager.cpp` — accumulated input-state singleton (keyboard / mouse /
    4 gamepads / touch) + `Get*State()` snapshots + `ResetAllForTests()`.
  - `src/CNA/Internal/Input/GestureDetector.cpp` — gesture state machine (has a test clock).
  - `include|src/CNA/Internal/Input/SdlGamepadBackend.*` — `ISdlGamepadBackend` seam + real impl + fake.
  - `include/Microsoft/Xna/Framework/Input/**` — the XNA-facing public API (26 headers).
- **Data flow:** host loop → `Game::PollEvents` → `SdlInputBridge::ProcessEvent(event)` → mutate
  `InputManager` / `TouchPanel` → game reads `GamePad::GetState` / `Keyboard::GetState` / `Mouse::GetState`
  / `TouchPanel::GetState`.
- **Invariants (keep true):** state updates at event-processing time; gamepad subsystem initialized once at
  startup + lazily in `ProcessEvent` (both idempotent); `ResetAllForTests()` restores a deterministic
  baseline (tests must stay order-independent); the production gamepad path uses the **real** SDL backend
  (fake is test-only, restored by the central reset).
- **Boundaries / API rules that must stay stable (now enforced by guards):**
  - Public XNA names/signatures are **frozen** — `PublicApiInputSignatureFreezeTests.cpp` (INPUT-API-031)
    + golden `docs/input-public-api-frozen.md`. A change must update BOTH in lockstep.
  - Enum **values** are frozen — the exhaustive enum-value suites (INPUT-API-034); renumbering fails a test.
  - No public header may leak SDL — `PublicApiInputCompileTests.cpp` (INPUT-API-030) `#error` guard.
  - Do **not** expose `ISdlGamepadBackend` or any `CNA::Internal` type in the XNA layer.
  - Every non-XNA member carries `NOXNA` and/or an `EXT` suffix (convention in the golden doc, INPUT-API-032).

## 7. Useful commands

```bash
# Configure + build tests (EasyGL; swap backend for VULKAN / BGFX / SDL_RENDERER):
cmake -S . -B cmake-build-input-easygl -G Ninja -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-input-easygl --target CnaTests

# Run the input subset — THE canonical way (INPUT-BUILD-003): runs CNA_INPUT_TEST_FILTER shuffled x3.
ctest --test-dir cmake-build-input-easygl -L input --output-on-failure
# List what that selects (must be exactly 1 entry: CnaInputTests):
ctest --test-dir cmake-build-input-easygl -N -L input

# Headless box: the MouseCursor tests need real cursors — run under a virtual X server:
xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure

# Run the whole suite directly:
./cmake-build-input-easygl/CnaTests

# Sanitizer build (ASan+UBSan), matching the CI job:
cmake -S . -B cmake-build-input-asan -G Ninja -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON -DCNA_SANITIZE=ON
cmake --build cmake-build-input-asan --target CnaTests

# Watch CI (no gh CLI/token in this env — unauthenticated API):
curl -s "https://api.github.com/repos/openeggbert/cna/actions/runs?per_page=1" | grep -E '"head_sha"|"status"|"conclusion"'
```
No lint/format config for the input track. There is **no failing input command to reproduce** — §4's
input item is a CI-change to confirm, not a failure. Graphics bugs (§4) reproduce via the pixel tests in
`GRAPHICS_TASKS.md` / `docs/depthstencilstate-support.md` (out of scope here).

## 8. Next smallest tasks (input track, ordered)

1. **Confirm `f0a185ca` CI is green** (the `ctest -L input` workflow change) — *locally validated
   2026-07-06; GitHub Actions run still to be eyeballed.*
   - Goal: validate the CI-step change actually runs the input subset on all 5 matrix jobs.
   - Files: `.github/workflows/input-ci.yml`, `CMakeLists.txt` (`CnaInputTests` add_test).
   - Done locally: `ctest -N -L input` selects exactly 1 entry (`CnaInputTests`); it runs **100% green**
     under `xvfb-run` on EasyGL (incl. MouseCursor). Remaining: confirm the Actions run for `f0a185ca`
     is `success`; if not, read the failing job's "Run input tests" step and fix the run cmd / `add_test`.
2. **Broader input backlog** — the immediate small tasks are cleared; remaining input work is the
   exhaustive list in `plan_input.md` (e.g. the gesture-matrix coverage gaps §5, INPUT-API-028/029
   namespace + `GetTypeName` audits). Pick from there, or hold for the merge-vs-switch-area decision (§0/§4).

## 9. Do not do yet

- **No public XNA API changes** (names / signatures / enum values are frozen and guarded); if you must,
  update the signature-freeze test + `docs/input-public-api-frozen.md` in the **same** commit.
- **No broad refactor** of `SdlInputBridge` / `InputManager` — keep the single-funnel event design.
- **No behavior change that diverges from FNA** without checking the FNA source and recording it in
  `docs/input-fna-fidelity.md`.
- **No exposing** `ISdlGamepadBackend` or any `CNA::Internal` type in the XNA layer.
- **No Graphics work on this branch** — Task 870/871/872 belong to the Graphics track (`GRAPHICS_TASKS.md`);
  don't fix them here or edit that file without deliberately switching context.
- **No merge** of `feature/input` to `develop`/`master` without explicit confirmation.
- **No committing** `.claude/settings.json`, `cmake-build-*` dirs, or unrelated vendor directories
  (stage files by explicit name; never `git add -A`).
- **No new large abstractions** for the hardware-gated items — they are manual/hardware, not missing code.

## 10. Resume prompt

```
Read NEXT.md first. Then, working on branch feature/input:
- Inspect ONLY the files needed for the first task in NEXT.md §8 (do not read the whole tree).
- Do NOT refactor unrelated code, do NOT change the public XNA API (it is frozen and guarded), and do
  NOT touch the Graphics track (GRAPHICS_TASKS.md / Task 870-872) unless explicitly asked.
- Make ONE small, verified improvement (start with §8 task 1).
- Run the relevant command from NEXT.md §7 and confirm it passes — the input subset must stay green via
  `ctest -L input` and order-independent (shuffle x3 is baked in). If you changed a public member, update
  the signature-freeze test + docs/input-public-api-frozen.md in the same commit.
- Update NEXT.md (§2 status, §3 recent changes, §8 tasks) after finishing, then stop and report.
Do not merge anything without explicit confirmation. Commit files by explicit name only.
```
