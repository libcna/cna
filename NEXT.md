# NEXT.md — CNA Project Handoff (feature/input branch)

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on **SDL3** with a pluggable 3D graphics backend layer
(EasyGL/OpenGL ES, Vulkan, Bgfx, SDL_Renderer). It is a framework/runtime — not a game — so
XNA/FNA game code can be ported to C++ with minimal API-surface changes.

- **Main goal (this branch):** port `Microsoft::Xna::Framework::Input` and `…::Input::Touch`
  from the FNA reference to CNA — not just API surface, but FNA-faithful runtime behavior wired
  to SDL3, CHECKLIST-compliant, and covered by tests. The original plan was `plan_input.md`
  (Phases I1–I7, tasks 700–783); all of it is now complete.
- **Current development phase: `feature/input` is FINALIZED — READY TO MERGE.** Phases I1–I10
  (tasks 700–855) complete, pre-merge-audited (Phase I11, 856–864), plus a final touch-fidelity +
  doc hardening pass (Phase I12, 865–878).
  **Final merge verification (2026-07-04):** `git submodule update --init --recursive` (completed
  clean, incl. all nested SDL_image/SDL_mixer codec submodules), then `rm -rf` + configure + build +
  run `CnaTests` on each backend — counts reproduced exactly from the Phase I12 run (task 876):
  - **EasyGL: 1968/1968** full · **221/221** input-filter
  - **Vulkan: 1968/1968** full · **221/221** input-filter
  - **bgfx: 1972/1972** full (+4 bgfx-specific, input-unrelated) · **221/221** input-filter
  - Input filter: `--gtest_filter='*Keyboard*:*Mouse*:*GamePad*:*Touch*:*Gesture*:*TextInput*:*SdlInputBridge*'`

  All green, no test failures on any backend. `feature/input` is **ready for merge / PR review**.
  There is no next numbered task — see Section 8.
  (Hardware/human-gated checks — real gamepad/IME/mobile — stay documented-as-unverified in
  `docs/input-manual-verification-results.md`, not faked.)
- **Phase I12 fixed one real fidelity gap:** the event-driven touch path (`InputManager`, which is
  what real SDL input feeds) dropped `TouchLocation::TryGetPreviousLocation()` — it now preserves
  the previous state/position for Moved/Released touches (tasks 868–872), matching FNA. The rest of
  Phase I12 was doc reconciliation (stale `TextInput`-is-`char`-based claims → `charcs`/UTF-16;
  test-count reconciliation; "complete keycode map" → "FNA-mappable subset") + test hardening
  (`SdlInputBridge::ResetForTests`, Ctrl+V-suppression regression test). No new features.
- **Two external-review passes drove the last two phases.** The first (2026-07-04) became **Phase
  I9 (792–840)**; the second became **Phase I10 (841–855)**, a stabilization pass. The one
  previously-deferred item — `Mouse::SetPosition`'s scaled/letterboxed logical→window transform
  (`plan.md` a-0001) — is now **implemented** (Phase I10 task 846; the graphics-layer change was
  authorized in Phase I10). Task **811 stays partial** (bridge SDL gamepad slot-assignment edge
  cases are hardware-gated). Real fixes across I9–I10: `GetGUIDEXT` format (816), `TextInput`→
  `char16_t` (806), touch display-size guard (828), `MouseCursor` singleton disposal (834),
  `SetPosition` logical→window transform (846).
- **Honestly unverified (hardware/human-gated, not missing code):** live gamepad rumble/sensors/
  light bar/hotplug (no controller), real IME composition + physical Czech typing (no IME/human),
  Wayland OS-cursor-landing readback (compositor policy). All recorded in
  `docs/input-manual-verification-results.md` and Section 5.
- **Key architectural decisions:**
  - The authoritative behavioral reference is the FNA source tree at
    `/rv/data/library/github.com/FNA-XNA/FNA/src`.
  - Non-XNA members inside the `Microsoft::Xna` namespace are tagged with the `NOXNA` marker
    macro. FNA's `EXT`-suffixed additions are non-XNA; a wholly-non-XNA class is tagged
    `NOXNA class`.
  - Real input behavior flows through an internal bridge: SDL3 events →
    `CNA::Internal::Input::SdlInputBridge` → `InputManager` → XNA state objects. Touch
    additionally flows `SdlInputBridge` → `TouchPanel::INTERNAL_onTouchEvent` → `GestureDetector`.
  - CNA is event-driven, not poll-driven like FNA: `InputManager` accumulates raw per-device
    state as SDL events arrive; `Game::Tick()` pumps SDL events exactly once per frame
    (`PollEvents()`), which is what keeps that accumulated state current.
  - Backend selection is compile-time via `CNA_GRAPHICS_BACKEND`. EasyGL is the primary/tested
    backend.

---

## 2. Current status

### Build
- EasyGL build (`cmake-build-debug`): clean, as of the last build in this session, including
  `cna_demo_input` (task 781), `CnaTests` (tasks 782, 790). Task 783 was a manual/integration
  check with no source change — no rebuild needed for it.
- **Vulkan (`cmake-build-vulkan`) and Bgfx (`cmake-build-bgfx`) now exist and build clean too**
  (task 791). Correction to earlier notes in this file: these dirs did not previously exist at
  all in this `cna_input` checkout — the "misconfigured, points at the sibling `cna` repo" build
  dirs referred to earlier actually live in the sibling repo itself, not here. Both were freshly
  configured (`-DCNA_GRAPHICS_BACKEND=VULKAN` / `=BGFX`) and confirmed to have the correct
  `CMAKE_HOME_DIRECTORY` (this checkout).

### Tests
- **All three configured backends pass**, as of task 791 (this session): EasyGL 1912/1912,
  Vulkan 1912/1912, Bgfx 1916/1916 (4 extra pre-existing Bgfx-specific tests, confirmed unrelated
  to input via a `--gtest_list_tests` diff). The same 165 Keyboard/Mouse/GamePad/Touch/Gesture/
  TextInput tests pass identically on all three. EasyGL's suite verified stable under
  `--gtest_filter='*Gesture*:*Touch*' --gtest_shuffle --gtest_repeat=10` and a full-suite
  `--gtest_shuffle` pass.

### Apps / libraries available
- `CNA` static library (XNA 4.0 API surface).
- `CnaTests` (Google Test unit suite).
- `cna_demo_input` — interactive input demo (keyboard, mouse, gamepad ×4 players with rumble,
  touch, text input panel). Builds; last known to run crash-free (confirmed via a timed run
  against the real X11 display in this environment; no screenshot tool available here to
  visually confirm layout — see Section 5).

### Recently implemented
- Phase I4 (Mouse + MouseCursor, tasks 745–755): real `SetPosition`/`IsRelativeMouseModeEXT`/
  `ClickedEXT` behavior; `MouseCursor` CHECKLIST compliance, `FromTexture2D`, `IDisposable`,
  lazy stock-cursor construction; 26 new tests in `MouseInputTests.cpp`.
- Phase I5 — now complete (Keyboard, tasks 760–768): `GetPressedKeys()` ordering fix;
  FNA-faithful `GetHashCode()`; `operator[](Keys)` indexer; completed SDL keycode→`Keys` map
  (F13–F24, Apps, Sleep, Volume keys, `KP_CLEAR`/`KP_PERIOD`, AZERTY/Norwegian/BEPO locale
  fallbacks); real `GetKeyFromScancodeEXT` implementation; scancode mode
  (`FNA_KEYBOARD_USE_SCANCODES` + `INTERNAL_scanMap`) wired into both the live key-event handler
  and `GetKeyFromScancodeEXT`; `KeyboardState::ToString()` now matches FNA's `ValueType` default
  (fully-qualified type name) instead of a CNA-invented placeholder, and its `NOXNA` tag was
  removed as a pre-existing mistake; `Keys`'s implicit `int` underlying type is now explicit
  (`enum class Keys : int`), matching FNA's declaration intent with no behavior change;
  `KeyboardInputTests.cpp` expanded from 3 to 21 tests (18 new), covering everything the phase
  touched.
- Phase I6, now complete (tasks 775–778): added the missing
  `// SPDX-License-Identifier: MS-PL` header to `SdlInputBridge.{hpp,cpp}` and
  `InputManager.{hpp,cpp}`, the only four files under `CNA/Internal/Input/` that lacked it;
  swept all Input headers and tagged 7 more non-XNA members with `NOXNA`
  (`TouchLocation()`/`TouchCollection()` default ctors, `TouchPanelCapabilities`'s two ctors, and
  `TouchPanel`'s `EnqueueGesture`/`INTERNAL_onTouchEvent`/`SetFinger`/`Update`) — all confirmed
  against FNA source; also confirmed no other `ToString`/`Equals`/`GetHashCode`/equality-operator
  override is incorrectly `NOXNA`-tagged (the mistake task 766 found and fixed on
  `KeyboardState`); updated `AUDIT.md`'s `Input`/`Input::Touch` tables with a `Runtime` column
  distinguishing API-surface completeness from actual FNA-faithful behavior; updated the
  Input/Touch portions of `docs/xna-4-api-coverage.md` (Touch was still described as an unwired
  0%-functional stub there — corrected to reflect Phase I2's real gesture pipeline); added new
  `docs/input-backend.md` (architecture overview, complete SDL3-event→XNA-state mapping table,
  event-driven-vs-FNA-polling deviation, per-device fidelity notes).
- Phase I7, now complete (tasks 780–783): `demo_input`'s `Update()` now calls
  `GamePad::SetVibration` every frame for all 4 `PlayerIndex` slots, driven by each slot's own
  trigger values; `Draw()` renders `PlayerIndex::Two/Three/Four` via a new compact
  `DrawGamePadMini` panel (connection/DPad/ABXY/shoulders/triggers/rumble-bar/sticks) alongside
  the existing full-detail Player One panel (task 781). New `SdlInputBridgeTouchGestureTests.cpp`
  (2 tests) drives synthetic `SDL_EVENT_FINGER_DOWN/MOTION/UP` through
  `SdlInputBridge::ProcessEvent` and asserts a Tap and a Flick `GestureSample` come out of
  `TouchPanel::ReadGesture()` — an end-to-end proof of the Phase I2 wiring from the real SDL
  entry point (task 782). Task 783 (manual/integration check, no source change): confirmed with
  a real SDL window under `SDL_VIDEODRIVER=x11` that `Mouse::SetPosition`'s OS-level cursor warp
  and relative-mouse-mode's no-op guard are pixel-exact and round-trip cleanly with no stuck
  state — see Section 5 for the one environment-specific finding (Wayland's global-mouse-query
  restriction) this check turned up.
- Phase I8, now complete (tasks 790–791), added to `plan_input.md` after the user chose "define
  new follow-up tasks" over merging or stopping. Task 790: `TouchPanel::GetCapabilities()` returns
  `MaximumTouchCount = 0` when disconnected via the `InputManager`-fallback branch, matching FNA's
  `touchDeviceExists ? 4 : 0` formula — only that one branch needed the fix (the
  `touchDeviceExists_` branch was already always connected there). Extended the existing
  `GetCapabilitiesFallsBackToInputManagerTouchStateWhenFlagIsUnset` test rather than adding a new
  one. Task 791: freshly configured `cmake-build-vulkan`/`cmake-build-bgfx` for this checkout
  (they hadn't existed here before) and confirmed all input work passes on both backends — see
  Section 2.
- One unrelated build blocker was hit and fixed: the sibling `sharp-runtime` checkout committed a
  `System::IAsyncResult` interface change that broke this repo's `StorageDevice.cpp`.

### What does NOT work yet
Everything planned (Phases I1–I12, tasks 700–878) is done. The one previously-deferred item —
`Mouse::SetPosition`'s scaled/letterboxed logical→window transform — was **implemented in Phase I10
(a-0001 / task 846)**. Remaining unverifiable items are platform/hardware-gated only (real gamepad
hardware, real IME, Wayland global-mouse), documented honestly in Section 5 and
`docs/input-manual-verification-results.md`, not missing implementation.
- `demo_input`'s layout (text panel, multi-pad section) has not been visually verified in this
  environment — it runs crash-free, but no screenshot tool works here (see Section 5).

---

## 3. Recent changes

All on `feature/input`, most recent first (`git log`):

| Commit | Change |
|--------|--------|
| `79a9064` | docs(Task 791): verify input work on Vulkan and Bgfx backends; closes Phase I8 |
| `a7f4b9e` | fix(Task 790): `TouchPanel::GetCapabilities()` reports 0 `MaximumTouchCount` when disconnected |
| `1732de1` (docs) | update `NEXT.md` for newly-added Phase I8 (tasks 790–791) |
| `8f4a97e` | docs: add Phase I8 follow-up tasks (790–791) to `plan_input.md` |
| `33e2ba7` (docs) | rewrite `NEXT.md` to reflect `plan_input.md` is fully complete (700–783) |
| `fc56845` | docs(Task 783): verify relative mouse mode + `SetPosition` warp integration; closes `plan_input.md` |
| `1cc6a8b` | test(Task 782): integration test for the touch/gesture pipeline via `ProcessEvent` |
| `f78e155` (docs) | mark Task 781 complete in `plan_input.md`; update `NEXT.md` for Task 782 |
| `91a4b09` | feat(Task 781): add rumble feedback and multi-pad rendering to `demo_input` |
| `77ea4c2` (docs) | mark Task 778 complete in `plan_input.md`; `NEXT.md` now points at Phase I7 |
| `121773b` | docs(Task 778): add `docs/input-backend.md`; closes Phase I6 |
| `9e85dcf` (docs) | mark Task 777 complete in `plan_input.md`; update `NEXT.md` for Task 778 |
| `20e0d6e` | docs(Task 777): update AUDIT.md and xna-4-api-coverage.md for Input/Touch |
| `edefc1a` (docs) | mark Task 776 complete in `plan_input.md`; update `NEXT.md` for Task 777 |
| `9c0deb3` | docs(Task 776): tag remaining non-XNA Touch members with NOXNA |
| `d4afc62` (docs) | mark Task 775 complete in `plan_input.md`; update `NEXT.md` for Task 776 |
| `09ff0ba` | docs(Task 775): add SPDX-License-Identifier to internal input backend files |
| `56b32ee` (docs) | mark Task 768 complete in `plan_input.md`; `NEXT.md` now points at Phase I6 |
| `3045ef7` | test(Task 768): expand `KeyboardInputTests.cpp` coverage; closes Phase I5 |
| `9b2e464` (docs) | mark Task 767 complete in `plan_input.md`; update `NEXT.md` |
| `1600bd6` | fix(Task 767): make `Keys` enum's `int` underlying type explicit |
| `37b3bd6` (docs) | mark Task 766 complete in `plan_input.md`; update `NEXT.md` |
| `d7156a3` | fix(Task 766): `KeyboardState::ToString()` matches FNA's `ValueType` default |
| `112edd4` (docs) | mark Task 765 complete in `plan_input.md`; update `NEXT.md` |
| `7dd4bf3` | feat(Task 765): add keyboard scancode mode (`INTERNAL_scanMap` + `FNA_KEYBOARD_USE_SCANCODES`) |
| `028ba32` (docs) | rewrite `NEXT.md` as a concise handoff document |
| `b63c56a` (docs) | mark Task 764 complete in `plan_input.md`; update `NEXT.md` |
| `697ae83` | feat(Task 764): implement `Keyboard::GetKeyFromScancodeEXT` |
| `a312df4` (docs) | mark Task 763 complete |
| `15f7667` | feat(Task 763): complete the SDL keycode→`Keys` map |
| `5bbadf0` (docs) | mark Task 762 complete |
| `b2b8939` | feat(Task 762): add `KeyState operator[](Keys) const` |
| `bfa4a0b` (docs) | mark Task 761 complete |
| `5c9532b` | fix(Task 761): `KeyboardState::GetHashCode()` FNA fidelity |
| `52f7bc3` (docs) | mark Task 760 complete |
| `345b30e` | fix(Task 760): `KeyboardState::GetPressedKeys()` ascending order |
| `92911c9`…`088f4a4` | Phase I4 (Mouse/MouseCursor, tasks 745–755) + the `sharp-runtime`
  `StorageDevice.cpp` build-blocker fix — see `plan_input.md` for full detail. |

- **Files added:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`,
  `docs/input-backend.md`, `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`.
- **Files modified this session:** `Keyboard.{hpp,cpp}`, `KeyboardState.{hpp,cpp}`, `Keys.hpp`,
  `SdlInputBridge.{hpp,cpp}`, `Mouse.{hpp,cpp}`, `MouseCursor.{hpp,cpp}`, `InputManager.{hpp,cpp}`,
  `StorageDevice.cpp`, `KeyboardInputTests.cpp`, `AUDIT.md`, `plan_input.md`, `NEXT.md`.
  (`SdlInputBridge.{hpp,cpp}`/`InputManager.{hpp,cpp}` this round only gained an SPDX header
  line — no logic changed.) Also `TouchLocation.hpp`, `TouchCollection.hpp`,
  `TouchPanelCapabilities.hpp`, `TouchPanel.hpp` (task 776 — Doxygen/`NOXNA` markers only, no
  logic changed), `AUDIT.md`, `docs/xna-4-api-coverage.md` (task 777 — docs only),
  `examples/demo_input/src/InputDemo.{hpp,cpp}` (task 781),
  `src/.../Input/Touch/TouchPanel.cpp` + `tests/.../Input/TouchInputTests.cpp` (task 790).
  Task 791 touched no tracked source files — it created `cmake-build-vulkan`/`cmake-build-bgfx`
  (both gitignored, confirmed via `git status`) and updated `plan_input.md`/`NEXT.md`.
- **Behavior changed:** `KeyboardState::GetPressedKeys()`/`GetHashCode()` now FNA-faithful;
  `Keyboard::GetKeyFromScancodeEXT` is a real layout-aware translation instead of an identity
  stub, and now respects scancode mode; SDL keycode coverage is materially more complete;
  `FNA_KEYBOARD_USE_SCANCODES=1` now switches the live key-event handler to physical-key-position
  (`INTERNAL_scanMap`-equivalent) lookups instead of layout-dependent keycodes;
  `KeyboardState::ToString()` now returns `"Microsoft.Xna.Framework.Input.KeyboardState"`
  (FNA's `ValueType` default) instead of the old `"[KeyboardState]"` placeholder, and is no
  longer `NOXNA`-tagged; `Keys`'s underlying type is now explicitly `int` (no observable change);
  Mouse/MouseCursor behavior described above under "Recently implemented"; `demo_input` now
  drives real rumble on all 4 gamepad slots and renders all 4 players (task 781);
  `TouchPanel::GetCapabilities()` now reports `MaximumTouchCount = 0` when disconnected via the
  `InputManager`-fallback branch, matching FNA (task 790).
- **Tests added:** 26 (`MouseInputTests.cpp`, Phase I4) + 18 (`KeyboardInputTests.cpp`, Phase I5,
  task 768 — expanded from 3 to 21) + 2 (`SdlInputBridgeTouchGestureTests.cpp`, task 782).
  Phase I5's earlier tasks (760–767) were verified during development with ad-hoc standalone
  harnesses/manual checks that were **not committed**; task 768 is where that coverage became
  real, committed tests, matching the batching pattern used for tasks 740 and 755. Phase I6
  (tasks 775–778) added no tests — 775/776 are comment/marker-only fixes with no behavior change
  (1910/1910 still passing confirms this), and 777/778 are pure documentation updates with no
  source change at all. Task 781 (demo-only, `examples/`) added no `CnaTests` coverage either —
  confirmed 1910/1910 still passing (this task can't touch library test count since it only
  changes example code), and confirmed the demo itself builds and runs crash-free for 4s against
  a real display. Task 783 added no tests either (its own ad-hoc verification harness was
  explicitly not committed, matching its "integration/manual check" framing) and no source
  change — 1912/1912 unaffected. Task 790 added no new test case — extended the existing
  `GetCapabilitiesFallsBackToInputManagerTouchStateWhenFlagIsUnset` test instead, since it
  already exercised both branches; 1912/1912 still passing. Task 791 added no new tests either —
  it's build-directory/backend verification; the same 1912 EasyGL/Vulkan tests and 1916 Bgfx
  tests (4 pre-existing, input-unrelated) all pass.

---

## 4. Current blocker / main problem

**There is no blocker, and there is no next numbered task.** The last known state: all three
configured backends (EasyGL, Vulkan, Bgfx) build clean; 1912/1912 (EasyGL, Vulkan) and 1916/1916
(Bgfx) tests pass. Every task in `plan_input.md` (700–791, Phases I1–I8) is done. See Section 8
for what to actually do next — same three-way decision as before Phase I8 was added.

The Vulkan/Bgfx build-dir issue previously tracked here (task 791) is resolved: both build dirs
now exist in this checkout with the correct `CMAKE_HOME_DIRECTORY`, and both pass all tests.

---

## 5. Known bugs and limitations

| Status | Item |
|--------|------|
| Needs verification | `demo_input`'s layout (text panel, and the task-781 multi-pad section) is not visually confirmed in this environment. It does run crash-free against this environment's real X11 display (`DISPLAY=:0`, confirmed this session via a timed run with no error/crash trace, contradicting an earlier note that forcing X11 made SDL exit — re-verified false this session, see the task-783 row below); the remaining gap is purely that no screenshot tool works here (`import -window root` fails silently). |
| Environment finding, not a CNA bug | `SDL_GetGlobalMouseState` silently returns `(0, 0)` under this environment's ambient Wayland session (`XDG_SESSION_TYPE=wayland`) regardless of real cursor movement — Wayland's compositor security model restricts querying global cursor position outside your own window. Forcing `SDL_VIDEODRIVER=x11` (XWayland) makes it work correctly. Same root cause as the screenshot-tool gap above. Discovered during task 783; not a defect in CNA's `Mouse` implementation. |
| Resolved (Phase I10, a-0001 / task 846) | `Mouse::SetPosition` now converts logical→window (inverse `IGraphicsBackend::TransformLogicalToWindow` + `SDL_RenderCoordinatesToWindow`) before `SDL_WarpMouseInWindow`, so the OS cursor lands correctly on a scaled/letterboxed window. The graphics-layer change was authorized in Phase I10. |
| Intentional deviation | `InputManager::GetMouseState()` reports relative-mode `X`/`Y` from a float delta accumulator fed by `SDL_EVENT_MOUSE_MOTION`'s `xrel`/`yrel` (drained to `0` on each read), rather than FNA's `SDL_GetRelativeMouseState` poll — the event-driven equivalent. Documented in-source in `InputManager.cpp`. |
| Intentional deviation | `GamePadState.PacketNumber` is tracked at the raw `InputManager` layer (bumped on real connection/button/axis changes) rather than by comparing freshly-built `GamePadState`s like FNA's poll loop. Documented in-source in `InputManager.cpp`. |
| Intentional deviation | `TouchPanel::GetState()` falls back to `InputManager::GetTouchState()` because CNA's bridge is event-driven, not poll-driven like FNA. Documented in-source in `TouchPanel.cpp`. |
| FNA-faithful (was a deviation, fixed by task 806) | `TextInputEXT::TextInput` is `charcs`/`char16_t` — one **UTF-16 code unit** per call, astral code points as surrogate pairs, matching FNA's `Action<char>`. (The old per-UTF-8-byte `char` behaviour was the deviation; corrected in Phase I9 task 806.) `TextEditing` remains a UTF-8 `std::string` — a separate, still-intact deviation. |
| Pre-existing, unrelated to input work | Working tree shows `D .claude/settings.json` (deleted) and untracked `vendor/wgpu-native/`. Do not commit either. |
| Fixed | `MouseCursor`'s move constructor/assignment used to bitwise-copy the raw `SDL_Cursor*` without nulling the moved-from source (latent double-free risk, not reachable by any current call site). Fixed in task 752; regression-tested in `MouseInputTests.cpp`. |
| Fixed | Keyboard scancode mode (`FNA_KEYBOARD_USE_SCANCODES`/`INTERNAL_scanMap`) was entirely absent — the live key-event handler always used layout-dependent keycodes, and `GetKeyFromScancodeEXT` never took the scancode-mode passthrough branch. Fixed in task 765. |
| Fixed | `KeyboardState::ToString()` returned a CNA-invented `"[KeyboardState]"` placeholder, incorrectly tagged `NOXNA`, instead of matching FNA's `ValueType` default (fully-qualified type name) like `GamePadState::ToString()` does. Fixed in task 766. |
| Fixed (cosmetic, no behavior change) | `Keys`'s `int` underlying type was implicit; task 767 made it explicit (`enum class Keys : int`) to match FNA's declaration intent. |
| Fixed | `KeyboardInputTests.cpp` had only 3 tests, none covering tasks 761–767's changes. Task 768 expanded it to 21 (18 new), closing out Phase I5's test-coverage debt. |
| Fixed | `SdlInputBridge.{hpp,cpp}`/`InputManager.{hpp,cpp}` were missing the `// SPDX-License-Identifier: MS-PL` header used everywhere else in the codebase. Fixed in task 775. |
| Fixed | 7 non-XNA members across `TouchLocation`/`TouchCollection`/`TouchPanelCapabilities`/`TouchPanel` were missing the `NOXNA` marker (default ctors with no FNA counterpart, and `TouchPanel` methods that are `internal` in FNA but public in CNA). Fixed in task 776. |
| Fixed (docs) | `AUDIT.md`'s Input/Touch tables didn't distinguish API-surface completeness from runtime behavior; `docs/xna-4-api-coverage.md` still described `Input::Touch` as an unwired, 0%-functional stub even though Phase I2 made it real. Fixed in task 777. |
| Fixed (docs) | No architecture doc existed for the input backend (`SdlInputBridge`/`InputManager`/`GestureDetector`, the SDL3-event mapping, or the event-driven-vs-polling deviation). Added `docs/input-backend.md` in task 778. |
| Fixed | `demo_input` only rendered `PlayerIndex::One` and never called `GamePad::SetVibration`. Task 781 added rumble (driven by each connected pad's own trigger values) and a compact multi-pad panel for `PlayerIndex::Two/Three/Four`. |
| Fixed | The Phase I2 gesture pipeline had no test proving it end-to-end from the real SDL entry point (`GestureDetectorTests.cpp` only drives `GestureDetector`'s direct API). Task 782 added `SdlInputBridgeTouchGestureTests.cpp`. |
| Verified, no bug found | Task 783: with a real SDL window under `SDL_VIDEODRIVER=x11`, `Mouse::SetPosition`'s OS-level warp is pixel-exact, relative mode genuinely no-ops it at the OS level, and disabling relative mode restores warping immediately with no stuck state. Also confirmed via source reading that `InputManager::GetMouseState()` doesn't go stale across the round-trip. No code change was needed. |
| Fixed | `TouchPanel::GetCapabilities()` passed `MAX_TOUCHES` unconditionally in both branches; FNA returns `0` when disconnected. Fixed in task 790 (only the `InputManager`-fallback branch needed it). |
| Fixed (was actually just missing, not misconfigured) | `cmake-build-vulkan`/`cmake-build-bgfx` didn't exist in this `cna_input` checkout at all — the "points at the sibling `cna` repo" build dirs earlier notes referred to actually live in that sibling repo, not here. Task 791 configured both fresh; both build clean and pass all tests (Vulkan 1912/1912, Bgfx 1916/1916 — 4 pre-existing Bgfx-specific tests unrelated to input). |
| Fixed (unrelated to input work, but blocked all builds) | The sibling `sharp-runtime` checkout committed a `System::IAsyncResult` interface addition that broke this repo's `StorageDevice.cpp`. Fixed by implementing the two missing overrides in `ContainerResult`/`SelectorResult`. If a future `sharp-runtime` change breaks the build the same way, check `cd ../sharp-runtime && git status --short && git log -1` before assuming it's a transient concurrent-edit race — if the change is already committed, it needs the same kind of fix here, not a wait-and-retry. |

---

## 6. Architecture notes

**Full architecture writeup: `docs/input-backend.md`** (added task 778) — data-flow diagram,
complete SDL3-event→XNA-state mapping table, event-driven-vs-FNA-polling deviation, and
per-device fidelity notes. The summary below is a quick-reference subset of that doc.

### Main modules (input)
| Layer | Location | Notes |
|-------|----------|-------|
| XNA public API | `include/Microsoft/Xna/Framework/Input/**` | Must match XNA 4.0 / FNA; FNA `EXT` additions tagged `NOXNA`. |
| Internal bridge | `src/CNA/Internal/Input/SdlInputBridge.cpp` | Single event entry point `ProcessEvent(const SDL_Event&)`, plus public static query methods (`GetKeyFromScancode`, `GetCapabilities`, `GetGUID`, …) that XNA-layer classes call into directly. |
| Internal state | `src/CNA/Internal/Input/InputManager.cpp` | Accumulates per-device state; exposes `GetKeyboardState/GetMouseState/GetRawGamePadState/GetTouchState`. Event-driven, not poll-driven. |
| Gesture engine | `src/CNA/Internal/Input/GestureDetector.cpp` | Ported state machine, driven by `TouchPanel::INTERNAL_onTouchEvent`/`Update`. |
| FNA reference | `/rv/data/library/github.com/FNA-XNA/FNA/src/Input` | Authoritative behavior. |

### Data flow (input)
```
SDL_PollEvent  (Game::PollEvents, called once per frame from Game::Tick())
  → SdlInputBridge::ProcessEvent(event)        // switch on event.type
      → InputManager::Set*State(...)           // keyboard / mouse / gamepad / touch snapshot
      → TextInputEXT::INTERNAL_OnText*(...)     // text input / editing
      → TouchPanel::INTERNAL_onTouchEvent(...)  // touch → GestureDetector
  → Keyboard/Mouse/GamePad/TouchPanel::GetState()  read by game code each frame
  → FrameworkDispatcher::Update() → TouchPanel::Update() → GestureDetector::OnUpdate()
```

### Invariants / boundaries that must stay stable
- `NOXNA` marks every non-XNA member in the `Microsoft::Xna` namespace; a wholly-non-XNA class
  uses `NOXNA class` (precedent: `ShaderEffect`, `MouseCursor`).
- `// SPDX-License-Identifier: MS-PL` at the top of every `.hpp` and `.cpp`.
- C# properties → `getXProperty()` / `setXProperty()`, not public fields. FNA's
  `{ get; internal set; }` maps to a `NOXNA`-tagged public setter, not a `friend`-based private
  one — there is no `friend` precedent anywhere in CNA.
- A read-only C# auto-property with lazy static-constructor semantics maps to a `getXProperty()`
  backed by a function-local `static` variable (Meyer's singleton), since a plain C++ static data
  member can't be lazily initialized (precedent: `MouseCursor`'s 11 stock-cursor getters).
- XNA/FNA API names and public constructor signatures are frozen — no renames, no new enum
  values on frozen types (e.g. `PlayerIndex` stays at 4 values), no convenience constructor
  parameters even when they'd be internal-set.
- `SdlInputBridge::ProcessEvent` is the single SDL-event funnel; do not add a second event path.
  Query-only helper methods on `SdlInputBridge` (not event handling) are fine and already
  established practice.
- `GestureDetector` and `MouseCursor`'s stock-cursor singletons are process-lifetime file-static
  state with no reset hook; tests that touch them must clean up after themselves (see
  `GestureDetectorTests.cpp` and `MouseInputTests.cpp`'s `ResetMouseState()` for the pattern).
- Classes that require a `GraphicsDevice` to construct (creates a real SDL window + graphics
  backend) are not unit-tested in `CnaTests` — see `OcclusionQueryDynamicBufferTests.cpp` and
  `MouseInputTests.cpp` for the precedent and rationale.

---

## 7. Useful commands

```bash
# Configure EasyGL build for THIS repo (already done; redo only if the cache is wrong)
cmake -S /rv/data/development/github.com/openeggbert/cna_input \
      -B /rv/data/development/github.com/openeggbert/cna_input/cmake-build-debug \
      -G Ninja -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON

# Build library / tests / demo
cmake --build cmake-build-debug --target CNA -j"$(nproc)"
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"
cmake --build cmake-build-debug --target cna_demo_input -j"$(nproc)"

# Run all unit tests
./cmake-build-debug/CnaTests

# Run the Keyboard test suite specifically
./cmake-build-debug/CnaTests --gtest_filter='*Keyboard*'

# Run the input demo (needs a display; F1 toggles text input, Esc exits)
./cmake-build-debug/cna_demo_input

# Verify a build dir's source wiring (should print …/cna_input)
grep CMAKE_HOME_DIRECTORY cmake-build-debug/CMakeCache.txt

# Vulkan and Bgfx builds (configured task 791; already exist, but shown for reconfiguring)
cmake -S . -B cmake-build-vulkan -G Ninja -DCNA_GRAPHICS_BACKEND=VULKAN -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-vulkan --target CnaTests -j"$(nproc)" && ./cmake-build-vulkan/CnaTests

cmake -S . -B cmake-build-bgfx -G Ninja -DCNA_GRAPHICS_BACKEND=BGFX -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-bgfx --target CnaTests -j"$(nproc)" && ./cmake-build-bgfx/CnaTests
# Bgfx's configure step fetches bgfx.cmake via git (FetchContent) — took ~6.5 min in this
# environment on first configure; needs network access to github.com.
```

There is no known bug to reproduce right now (Section 4). No project-wide lint/format target was
observed; the `Doxyfile` exists but is not a build/verify step.

If a build fails on a `sharp-runtime` file unrelated to the change being made, it may be a
concurrent edit in the sibling `sharp-runtime` checkout settling — retry once. If it persists,
run `cd ../sharp-runtime && git status --short && git log -1` to check whether the change is
already committed (permanent) rather than assuming it will resolve itself.

---

## 8. Final handoff (tasks 855 + 864)

**`feature/input` is finalized, pre-merge-audited, and touch-hardened — merge-ready.** Phases
I1–I12 (tasks 700–878) are all done and clean-built + tested on all three backends (task 876:
EasyGL/Vulkan **1968**, bgfx **1972**; **221** input tests each). Both external-review passes (→
Phase I9, I10), the pre-merge audit (I11), and the final touch-fidelity/doc pass (I12) are
complete. Bugs found & fixed: I11 confirmed `SetPosition` handles letterbox *offset* (858); **I12
fixed the event-driven touch path dropping `TryGetPreviousLocation` (868–872)**. The
previously-deferred `plan.md` a-0001 is **implemented** (task 846). There is **no next numbered
task**.

**What is done (on this branch):** the full XNA 4.0 `Input` + `Input::Touch` surface, FNA-faithful
runtime behavior on SDL3, FNA `*EXT` extensions, the MonoGame-style `MouseCursor`, `TextInputEXT`
(UTF-16), gesture recognition, `Mouse::SetPosition` logical→window scaling, and event-driven touch
previous-location. 221 input tests + a
combined `cna_input_smoke` sample. Coverage is documented **by category** (never blended) in
`plan_input.md`'s "final split" and `docs/xna-4-api-coverage.md`.

**What remains — outside this branch (do NOT do here):**
- **Hardware/human verification** (not code): live gamepad rumble/sensors/light bar/hotplug, real
  IME composition + Czech typing, Wayland OS-cursor-landing. Run `docs/input-manual-verification-
  results.md`'s checklist on a machine with a controller + IME + X11 session. Task **811** stays
  partial for the same reason (bridge SDL slot-assignment needs a real device).
- **Anything graphics/audio/content.** `GRAPHICS_TASKS.md` is the graphics track; `plan.md` holds
  cross-cutting `a-NNNN` items (a-0001 now done).

**The user's call now (a repo-affecting decision, confirm first):**
1. **Merge/PR `feature/input` → `master`** — scope complete, tested, documented.
2. Report the branch complete and **switch to the next branch** (graphics work on
   `GRAPHICS_TASKS.md`, or whatever the user prioritizes) — nothing input-side is pending.
3. Define new follow-up tasks only if a real gap is discovered (verify first — do not invent).

---

## 9. Do not do yet

- No broad refactor of `SdlInputBridge` or `InputManager` — keep the single-funnel event design;
  add cases/query methods, do not restructure.
- No API renames or namespace moves — XNA/FNA names and constructor signatures are frozen (see
  Section 6).
- No graphics changes on this branch — graphics is tracked in `GRAPHICS_TASKS.md` on its own
  track.
- Do not re-open `Mouse::SetPosition` / `a-0001` unless a regression is found — it was resolved in
  Phase I10 task 846 (logical→window transform via `IGraphicsBackend::TransformLogicalToWindow` +
  `SDL_RenderCoordinatesToWindow`; the graphics-layer change was authorized then). Don't edit
  `GRAPHICS_TASKS.md` unless specifically asked — it's a separate, large, actively-maintained plan
  not owned by this branch's work.
- Do not invent new tasks unprompted now that Phases I1–I8 (700–791) are all done — ask the user
  which of Section 8's options applies, same as when Phase I8 itself was created.
- Do not merge or push `feature/input` to `master` without confirming with the user first — the
  branch being "done" per `plan_input.md` is not the same as authorization to merge.
- Do not commit the pre-existing working-tree changes (`D .claude/settings.json`, untracked
  `vendor/wgpu-native/`) — unrelated to input work.
- Do not commit `cmake-build-*` directories (including the new `cmake-build-vulkan`/
  `cmake-build-bgfx`) — they are gitignored. Reconfiguring/rebuilding them is fine when asked.
- Do not attempt to unit-test `GraphicsDevice`-dependent construction in `CnaTests` (Section 6) —
  use a separate integration test executable instead.
- ~~Do not change the `TextInputEXT` char-based callback signature — a deliberate, documented
  deviation.~~ **Superseded (Phase I9 task 806, user decision 2026-07-04):** the callback is now
  `std::function<void(charcs)>` (`char16_t`), one UTF-16 code unit per call, matching FNA's
  `Action<char>` and CLAUDE.md's `char → charcs` type mapping. The old per-UTF-8-byte `char`
  signature was the deviation; it was corrected, not frozen. (`TextEditing` stays a UTF-8
  `std::string` — a separate, still-intact deviation.)

---

## 10. Resume prompt

```
Read NEXT.md first. feature/input is FINALIZED, pre-merge-audited, and touch-hardened: Phases
I1-I12 (700-878) are all complete and clean-built + tested on EasyGL/Vulkan/bgfx (task 876:
1968/1968/1972, 221 input tests each). There is no next numbered input task to pick up.

Two external-review passes (I9, I10), a pre-merge audit (I11), and a final touch-fidelity/doc pass
(I12) are done. Real bugs found & fixed: 858 (SetPosition letterbox offset) and 868-872 (the
event-driven touch path now preserves TryGetPreviousLocation for Moved/Released). plan.md a-0001
(Mouse::SetPosition logical->window transform) is IMPLEMENTED (task 846). Task 811 stays partial
and the manual/hardware checks in docs/input-manual-verification-results.md stay open ONLY because
they need real hardware/IME/X11 - not missing code. Do NOT re-open these as input tasks.

The decision now (Section 8) is the user's: merge/PR feature/input into master (confirm before
pushing - do NOT merge/push to master without explicit confirmation), or report complete and
move to the next branch (graphics = GRAPHICS_TASKS.md). Do not invent new input tasks; the input
layer is done. If the user reports a real, reproduced input bug, treat it as a normal small fix
(inspect only the files it needs, build+test via cmake-build-input-easygl, update the plans).
```
