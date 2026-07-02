# NEXT.md — CNA Project Handoff (feature/input branch)

> This handoff covers the **Input subsystem porting** work that is the active focus of the
> `feature/input` branch. The broader Graphics work is tracked separately in `GRAPHICS_TASKS.md`
> (and was the subject of an earlier handoff); it is **not** the current focus here.

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`),
built on **SDL3** with a pluggable 3D graphics backend layer (EasyGL/OpenGL ES, Vulkan, Bgfx,
SDL_Renderer). It is a framework/runtime — not a game — so XNA/FNA game code can be ported to C++
with minimal API-surface changes.

- **Main goal (this branch):** Port `Microsoft::Xna::Framework::Input` and `…::Input::Touch` from
  the FNA reference to CNA — not just API surface, but **FNA-faithful runtime behavior** wired to
  SDL3, CHECKLIST-compliant, and covered by tests. The plan is `plan_input.md` (Phases I1–I7,
  tasks 700–783).
- **Current development phase:** **Phase I1 (TextInputEXT), Phase I2 (Touch & gesture pipeline),
  and Phase I3 (GamePad behavior/FNA fidelity) are all complete. Phase I4 (Mouse behavior and
  MouseCursor) is implementation-complete (tasks 745–754)** — only the batched test task (755)
  remains before Phase I5 (Keyboard) starts.
- **Key architectural decisions:**
  - The authoritative behavioral reference is the FNA source tree at
    `/rv/data/library/github.com/FNA-XNA/FNA/src`.
  - Non-XNA members inside the `Microsoft::Xna` namespace are tagged with the `NOXNA` marker macro.
    FNA's `EXT`-suffixed additions are non-XNA; a wholly-non-XNA class is tagged `NOXNA class`.
  - Real input behavior flows through an internal bridge: SDL3 events →
    `CNA::Internal::Input::SdlInputBridge` → `InputManager` → XNA state objects. Touch additionally
    flows `SdlInputBridge` → `TouchPanel::INTERNAL_onTouchEvent` → `GestureDetector`.
  - CNA is **event-driven**, not poll-driven like FNA: `InputManager` accumulates raw per-device
    state as SDL events arrive; `Game::Tick()` pumps SDL events exactly once per frame
    (`PollEvents()`), which is what keeps that accumulated state current. See `GamePad.cpp`'s
    `GetState()` and `InputManager`'s class doc comment for the fuller writeup (task 735).
  - Backend selection is compile-time via `CNA_GRAPHICS_BACKEND`. EasyGL is the primary/tested backend.

---

## 2. Current status

### Build
- **EasyGL build (`cmake-build-debug`):** clean.
- **Vulkan (`cmake-build-vulkan`) and Bgfx (`cmake-build-bgfx`) build dirs are still wired to the
  sibling `…/openeggbert/cna` repo** — see Section 4. They were not used or fixed for input work.

### Tests
- **1866 / 1866 unit tests pass** (EasyGL build, `cmake-build-debug/CnaTests`). GamePad-related
  additions this phase (58 tests total across 5 new files, plus 1 added to the existing
  `GamePadInputTests.cpp` for `PacketNumber`):
  - `GamePadButtonsTests.cpp` — `GamePadButtonsTest` (7) + `GamePadDPadTest` (7): constructors, all
    named getters, `FromButtonArray` (multi-flag/cross-element/empty), equality, `GetHashCode`.
  - `GamePadStateTests.cpp` — 10 tests: both constructors, trigger/stick→button packing,
    `IsButtonDown`/`IsButtonUp`, equality (incl. `PacketNumber`), `GetHashCode`, `ToString`.
  - `GamePadThumbSticksTests.cpp` — 9 tests: construction/equality/hash, plus `IndependentAxes`/
    `Circular` dead-zone math (incl. the private `ExcludeCircularDeadZone`) driven indirectly
    through `InputManager` + `GamePad::GetState`.
  - `GamePadTriggersTests.cpp` — 7 tests: construction, `WithinEpsilon`-based equality (with a
    worked-out ULP argument for why the test values are chosen), `GetHashCode`, dead-zone ctor.
  - `GamePadTests.cpp` — 13 tests: `ExcludeAxisDeadZone` math, `GamePad`'s no-hardware fallback
    paths (`GetCapabilities`/`SetVibration`/EXT methods), and a full `GamePadCapabilities`
    getter/setter round-trip (36 properties).

### Apps / libraries available
- `CNA` static library (XNA 4.0 API surface).
- `CnaTests` (Google Test unit suite).
- `cna_demo_input` — interactive input demo (keyboard, mouse, gamepad, touch, text input panel).
  Builds; runs crash-free.

### Recently implemented (Phase I3 — tasks 725–740)
- `GamePad::SetLightBarEXT`/`GetGyroEXT`/`GetAccelerometerEXT` wired to real SDL3 calls
  (`SDL_SetGamepadLED`, `SDL_Get/SetGamepadSensorEnabled` + `SDL_GetGamepadSensorData`).
- EXT buttons (Misc1/Paddle1–4/TouchPad) now reach `GamePadState.Buttons` via new
  `InputManager::GamePadButton` entries and matching SDL button-conversion cases.
- `GamePadState.PacketNumber` now increments on real state changes. **Architectural deviation from
  FNA:** tracked at the raw `InputManager` layer (bumped when a connection/button/axis value
  actually changes) rather than by comparing freshly-built `GamePadState`s like FNA's poll loop —
  comparing *built* states would falsely bump the counter depending on which `GamePadDeadZone` mode
  a caller uses, since dead-zone mode changes the built button flags for the same raw state.
- `GamePadCapabilities` reworked: 34 raw public `bool` fields (+ `GamePadType_`) → 36 private fields
  with `getXProperty()`/`NOXNA setXProperty()` pairs; the 10 EXT properties are `NOXNA` on both
  getter and setter. Implementation moved out of the header into a new `GamePadCapabilities.cpp`.
- `GamePadButtons::FromButtons`/`GamePadDPad::FromButtons` renamed to `FromButtonArray` (matching
  FNA's internal names); `GamePadDPad::FromButtonArray` now takes `std::initializer_list<Buttons>`
  instead of a single combined value, matching FNA's `params Buttons[]` signature exactly —
  `GamePadState`'s 5-arg public constructor now passes the same list to both `FromButtonArray` calls
  directly, removing a previous CNA-only round-trip workaround.
- FNA-fidelity fixes: `GamePadThumbSticks`/`GamePadTriggers.GetHashCode` (were truncated `*1000`
  formulas, now match FNA's `Vector2`/float-hash-based formulas exactly); `GamePadState.ToString()`
  now returns the fixed fully-qualified type name, matching FNA's `base.ToString()` (FNA never
  overrides `ToString` on this struct).
- Optional `FNA_GAMEPAD_NUM_GAMEPADS` env override added, clamped to 4 (CNA's `PlayerIndex` is
  frozen XNA API with exactly 4 values, so an override above 4 has no addressable slot — the
  practically useful direction, reducing/disabling gamepad tracking, works).

### What does NOT work yet
- **Mouse:** Phase I4 behavior (745-748) and every `MouseCursor` CHECKLIST task (750-754) are now
  done — see `plan_input.md`'s Phase I4 table for full per-task detail. `MouseCursor`'s overall status
  decision is recorded in `AUDIT.md`: **kept** as a MonoGame-derived `NOXNA` extension. Only remaining
  known limitation (task 747's resolution): `SetPosition` has no inverse (logical→window) coordinate
  transform, so on a letterboxed/scaled window (render resolution ≠ window size) the OS cursor warp
  target will be off by the scale factor — documented in-source (`Mouse.cpp`); fixing it for real
  requires a graphics-layer change (`IGraphicsBackend` inverse transform), out of scope here. Next up
  is the batched `MouseInputTests.cpp` test task (755), the last task in Phase I4.
- **Keyboard:** `GetPressedKeys()` order is non-deterministic; SDL keycode→Keys map is incomplete;
  `GetKeyFromScancodeEXT` is an identity stub (Phase I5).
- **demo_input text panel is not visually verified** (see Section 5; carried over from Phase I1,
  unrelated to gamepad work).
- **Minor, out of scope for Phase I3:** `TouchPanel::GetCapabilities()` still passes `MAX_TOUCHES`
  unconditionally in both branches (noted in the Phase I2 handoff, not yet fixed — see Section 5).

---

## 3. Recent changes

All on `feature/input` (most recent first). Phase I4 (this session) grouped commits by related
tasks rather than one-commit-per-task like Phase I3 did; each `feat(Tasks N-M)` commit has a paired
`docs: mark Tasks N-M complete` commit immediately after it in `git log`.

| Commit | Change |
|--------|--------|
| `0936afa` (docs) | mark Task 752 complete in `plan_input.md`; NEXT.md handoff update |
| `70b3955` | feat(Task 752): `MouseCursor::Dispose()` / `System::IDisposable`, + latent move-ctor double-free fix |
| `8ab03d9` (docs) | mark Tasks 745-751 complete in `plan_input.md`; NEXT.md handoff update |
| `e8bac2e` | feat(Tasks 750-751): `MouseCursor` CHECKLIST fixes (SPDX/NOXNA) and `FromTexture2D` |
| `e295db9` | feat(Tasks 745-748): real Mouse behavior — cursor warp, relative mode, `ClickedEXT` |
| `088f4a4` | fix: implement `IAsyncResult.AsyncState/AsyncWaitHandle` in `StorageDevice`'s result types (unrelated sharp-runtime-triggered build blocker) |
| `af2174a` | docs: add per-task verification commands to NEXT.md's Phase I4 task list (previous session) |
| `3c6be52` | docs: rewrite NEXT.md as Phase I3 complete handoff (previous session) |

Task 753 (stock-cursor lazy-init + `WaitCursor`→`WaitArrow` rename) is done in the working tree as of
this handoff but not yet committed — commit/push it (or fold it into the next task's commit) before
moving on if it's still uncommitted when you resume.

- **Files added (Phase I4 so far):** none — all changes are to existing Mouse/MouseCursor/StorageDevice files.
- **Files modified:** `Mouse.{hpp,cpp}`, `MouseCursor.{hpp,cpp}`, `InputManager.{hpp,cpp}`,
  `SdlInputBridge.cpp`, `StorageDevice.cpp`, `plan_input.md`, `NEXT.md`.
- **Behavior changed:** `Mouse::SetPosition` now really warps the OS cursor; `IsRelativeMouseModeEXT`
  is real and feeds relative deltas into `GetMouseState()`; `ClickedEXT` fires; `MouseCursor` supports
  custom cursors (`FromTexture2D`), proper disposal, and lazy stock-cursor construction.

---

## 4. Current blocker / main problem

**There is no hard blocker on the EasyGL track** — it builds clean and all 1866 unit tests pass.
One practical issue carries over, and Phase I4 is the next real gap:

1. **Vulkan/Bgfx build dirs are mis-wired (practical gotcha, unchanged from last handoff).**
   - **Symptom:** `cmake-build-vulkan/CMakeCache.txt` and `cmake-build-bgfx/CMakeCache.txt` have
     `CMAKE_HOME_DIRECTORY=/rv/data/development/github.com/openeggbert/cna` (the **sibling** repo),
     not this `cna_input` checkout. Building there compiles the wrong source tree and will not
     include input changes.
   - **Affected:** any Vulkan/Bgfx verification of input work.
   - **Fix pattern (already applied to `cmake-build-debug`):**
     `rm -rf cmake-build-<x> && cmake -S … -B … -G Ninja -DCNA_GRAPHICS_BACKEND=<X> …`.
     The Vulkan/Bgfx dirs have **not** been reconfigured.

2. **Next functional focus: Phase I4's last task — `MouseInputTests.cpp` (task 755).** Every other
   Phase I4 task (745–754: Mouse behavior + full `MouseCursor` CHECKLIST compliance) is done. Only
   test coverage remains before Phase I4 closes out. See `plan_input.md` Phase I4.
   Affected: new `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`.

---

## 5. Known bugs and limitations

| Status | Item |
|--------|------|
| **Confirmed** | `cmake-build-vulkan` / `cmake-build-bgfx` caches point at the sibling `…/cna` repo (Section 4). |
| **Incomplete** | Mouse/MouseCursor: all Phase I4 implementation tasks (745–754) are done. Only the batched `Mouse`/`MouseCursor` test task (755) remains before Phase I4 closes. |
| **Incomplete** | Keyboard: `GetPressedKeys()` unordered; SDL keycode→Keys map missing F13–F24/Apps/Volume/locale fallbacks; `GetKeyFromScancodeEXT` is identity stub; no scancode mode (Phase I5). |
| **Needs verification** | `demo_input` text panel not visually confirmed: it builds and runs crash-free ~4s under the native backend, but no Wayland screenshot tool is available here and forcing the X11 driver makes SDL exit. A human at a display should run it and type. |
| **Intentional deviation** | `GamePadState.PacketNumber` is tracked at the raw `InputManager` layer (bumped on real connection/button/axis changes) rather than by comparing freshly-built `GamePadState`s like FNA's poll loop; documented in-source (`InputManager.cpp`). |
| **Intentional deviation** | `TouchPanel::GetState()` falls back to `InputManager::GetTouchState()` because CNA's `SdlInputBridge` is event-driven, not poll-driven like FNA's `UpdateTouchPanelState()`/`SetFinger` path; documented in-source (`TouchPanel.cpp`). |
| **Intentional deviation** | `TouchCollection`'s `IEnumerable<TouchLocation>::GetEnumerator()` is replaced by NOXNA-tagged `begin()`/`end()`, matching the precedent used by `GameComponentCollection`, `EffectAnnotationCollection`, etc. — no bespoke `Enumerator` struct exists anywhere in CNA. |
| **Intentional deviation** | `FNA_GAMEPAD_NUM_GAMEPADS` env override is clamped to 4 (CNA's `PlayerIndex` enum is frozen XNA API with exactly 4 values); FNA nominally allows values above 4 but its own comment says doing so also requires adding `PlayerIndex` names, which CLAUDE.md forbids. |
| **Minor, not yet fixed** | `TouchPanel::GetCapabilities()` passes `MAX_TOUCHES` unconditionally in both branches (connected and fallback); FNA returns `0` for `MaximumTouchCount` when disconnected. Noted during Phase I2, not yet scheduled as its own task. |
| **Intentional deviation** | `TextInputEXT::TextInput` is `char`-based, so `SDL_EVENT_TEXT_INPUT` is forwarded **per UTF-8 byte** (appending rebuilds the UTF-8 string). FNA decodes to UTF-16 because C# strings are UTF-16. |
| **Intentional deviation** | `Mouse::SetPosition`/`GetState` do not replicate FNA's `INTERNAL_WindowWidth/Height ÷ INTERNAL_BackBufferWidth/Height` faux-backbuffer scale (`Mouse.cs:107-116`) — those fields were removed (task 747). CNA already solves window↔logical coordinates more generally via `SdlInputBridge::to_logical_position`/`IGraphicsBackend::TransformWindowToLogical`, which `GetState()` benefits from for free; but there is no inverse (logical→window) transform yet, so `SetPosition`'s `SDL_WarpMouseInWindow` target will be off by the scale factor on a letterboxed/scaled window. Documented in-source (`Mouse.cpp`). Fixing it needs a graphics-layer `IGraphicsBackend` addition, out of scope for this branch. |
| **Intentional deviation** | `InputManager::GetMouseState()` reports relative-mode `X`/`Y` from a float delta accumulator fed by every `SDL_EVENT_MOUSE_MOTION`'s `xrel`/`yrel` (drained to `0` on each read), rather than FNA's `SDL_GetRelativeMouseState` poll — the equivalent for CNA's event-driven model (see the `InputManager` class doc, task 735). Documented in-source (`InputManager.cpp`). |
| **Pre-existing (not from input work)** | Working tree shows `D .claude/settings.json` (deleted) and untracked `vendor/wgpu-native/`. These predate this branch's input work and were **not** committed. |
| **Fixed** | `MouseCursor`'s move constructor/assignment were `= default`, which bitwise-copied the raw `sdlCursor_` pointer without nulling the moved-from source — a moved-from cursor and its target both believed they owned the same `SDL_Cursor*`, so both destructors would eventually double-`SDL_DestroyCursor` it. Not reachable via any code path in this repo (every call site returns a prvalue, elided under C++17 rather than actually moved), but fixed while implementing task 752's `Dispose()` since the same lines were already being touched. |
| **Fixed (unrelated to Mouse work, but blocked all builds)** | The sibling `sharp-runtime` checkout committed (`d61ffb8`) a `System::IAsyncResult` interface addition (`AsyncState`/`AsyncWaitHandle`, full 4-member .NET interface) mid-session, which broke this repo's `StorageDevice.cpp` (`ContainerResult`/`SelectorResult` didn't implement the two new pure-virtual overrides — `CNA` target wouldn't link). Fixed alongside task 750: `void* asyncState` → `std::any asyncState` (same values, no `Begin*` signature change) and a `mutable System::Threading::EventWaitHandle waitHandle{true, ManualReset}` (both classes always report `getIsCompletedProperty() == true`), matching the pattern `sharp-runtime`'s own commit used in its test implementers. If a future `sharp-runtime` interface change breaks this repo's build again the same way, this is the pattern to follow: implement the missing overrides in the CNA-side consumer, don't touch `sharp-runtime`. |

---

## 6. Architecture notes

### Main modules (input)
| Layer | Location | Notes |
|-------|----------|-------|
| XNA public API | `include/Microsoft/Xna/Framework/Input/**` | Must match XNA 4.0 / FNA; FNA `EXT` additions tagged `NOXNA`. |
| Internal bridge | `src/CNA/Internal/Input/SdlInputBridge.cpp` | Single entry point `ProcessEvent(const SDL_Event&)`; knows SDL types. |
| Internal state | `src/CNA/Internal/Input/InputManager.cpp` | Accumulates per-device state; exposes `GetKeyboardState/GetMouseState/GetRawGamePadState/GetTouchState`. Event-driven, not poll-driven — see class doc comment. |
| Gesture engine | `src/CNA/Internal/Input/GestureDetector.cpp` | Full ported state machine — live, driven by `TouchPanel::INTERNAL_onTouchEvent`/`Update`. |
| FNA reference | `/rv/data/library/github.com/FNA-XNA/FNA/src/Input` | Authoritative behavior. |

### Data flow (input)
```
SDL_PollEvent  (Game::PollEvents, called once per frame from Game::Tick())
  → SdlInputBridge::ProcessEvent(event)        // switch on event.type
      → InputManager::Set*State(...)           // keyboard / mouse / gamepad / touch snapshot
      → TextInputEXT::INTERNAL_OnText*(...)     // text input / editing (Phase I1)
      → TouchPanel::INTERNAL_onTouchEvent(...)  // touch → GestureDetector (Phase I2)
      → TouchPanel::TouchDeviceExists = true    // on FINGER_DOWN, opens the Update() gate
  → Keyboard/Mouse/GamePad/TouchPanel::GetState()  read by game code each frame
      → GamePad::GetState builds a GamePadThumbSticks/Triggers/Buttons/DPad from the raw
        InputManager snapshot, applying dead-zone processing per the requested GamePadDeadZone
  → FrameworkDispatcher::Update() → TouchPanel::Update() → GestureDetector::OnUpdate()
```

### Invariants / boundaries that must stay stable
- `NOXNA` marks every non-XNA member in the `Microsoft::Xna` namespace; include `CNA/CNAHelper.hpp`
  where it is used. A wholly-non-XNA class uses `NOXNA class` (precedent: `ShaderEffect`).
- `// SPDX-License-Identifier: MS-PL` at the top of every `.hpp` and `.cpp`.
- C# properties → `getXProperty()` / `setXProperty()` (no public fields for XNA API). FNA's
  `{ get; internal set; }` pattern maps to a `NOXNA`-tagged public setter (precedent:
  `TextInputEXT::setWindowHandleProperty`, `TouchPanel::setTouchDeviceExistsProperty`,
  `GamePadState::setPacketNumberProperty`, all of `GamePadCapabilities`'s 36 setters) — **not** a
  `friend`-based private setter; there is no precedent anywhere in CNA for the friend approach.
- XNA/FNA API names are frozen — no renames, and no adding enum values to frozen types (e.g.
  `PlayerIndex` stays at exactly 4 values even where FNA's own `GAMEPAD_COUNT` env var nominally
  allows going higher).
- FNA's public constructor signatures are frozen too — don't add convenience parameters to them
  even for otherwise-internal-set fields (e.g. `GamePadState`'s two public constructors take no
  `packetNumber` argument, matching FNA exactly; `PacketNumber` is set via the property setter
  after construction instead).
- The window handle is published by `GraphicsDevice` at create/destroy; `TextInputEXT` reads it via
  `getWindowHandleProperty()`. (Note: `Mouse.WindowHandle` and `TouchPanel.WindowHandle` are **not**
  yet published — a follow-up, since FNA sets all three together in `DisposeWindow`.)
- `SdlInputBridge::ProcessEvent` is the single SDL-event funnel; do not add a second event path.
- `GestureDetector`'s state machine lives in file-static variables with **no reset hook** — tests
  that drive it must fully release every finger they press and drain the gesture queue between
  cases (see `GestureDetectorTests.cpp`'s fixture for the established pattern).
- Private dead-zone-mode constructors (`GamePadThumbSticks`/`GamePadTriggers`'s 3-arg ctors,
  friended to `GamePad` only) are only reachable in tests via `InputManager::SetGamePadAxisValue` +
  `GamePad::GetState(playerIndex, deadZoneMode)` — see `GamePadThumbSticksTests.cpp`/
  `GamePadTriggersTests.cpp` for the pattern.

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

# Run the GamePad test suites specifically
./cmake-build-debug/CnaTests --gtest_filter='*GamePad*'

# Run the input demo (needs a display; F1 toggles text input, Esc exits)
./cmake-build-debug/cna_demo_input

# Verify a build dir's source wiring (should print …/cna_input)
grep CMAKE_HOME_DIRECTORY cmake-build-debug/CMakeCache.txt
```

> No project-wide lint/format target was observed. Doxygen config (`Doxyfile`) exists but is not a
> build/verify step.
>
> Note: builds in this environment occasionally trigger a large rebuild pass (`-- GLOB mismatch! /
> Re-running CMake...`) unrelated to the change being made — this has coincided with a sibling
> `sharp-runtime` checkout being edited concurrently in another session on this machine. If a build
> fails on an unrelated `sharp-runtime` file (e.g. `Decimal.cpp`, `DateTime.cpp`), it has always
> resolved itself on retry once the other session's edit settles; it is not this branch's problem.

---

## 8. Next smallest tasks

Tasks 745–754 (all of Phase I4's Mouse behavior + `MouseCursor` CHECKLIST work) are **done**. Full
per-task detail is in `plan_input.md`'s Phase I4 table — read that instead of a duplicated summary
here. Highlights worth knowing before touching this code:
- `Mouse::SetPosition`'s OS-cursor warp has a known letterbox-scaling limitation, documented in-source
  in `Mouse.cpp` (task 747).
- `MouseCursor`'s move ctor/assignment had a latent double-`SDL_DestroyCursor` bug (fixed in task 752;
  see Section 5's "Fixed" row).
- `MouseCursor`'s 11 stock cursors are lazy `getXProperty()` singletons now, not static fields (753).
- `MouseCursor`'s overall status decision (kept, as a MonoGame `NOXNA` extension) is recorded in
  `AUDIT.md`'s `Microsoft::Xna::Framework::Input` table (754).
- An unrelated sharp-runtime `IAsyncResult` build blocker was hit and fixed along the way — see
  Section 5's "Fixed (unrelated...)" row if a future sharp-runtime change breaks the build similarly.

**Next task to pick up — Task 755 (closes out Phase I4):** new
`tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp` (auto-discovered via the `tests/*.cpp`
glob) covering:
- `MouseState`: 8 getters, both constructors, `==`/`!=`, `Equals` (equal + unequal), `GetHashCode`
  consistency, `ToString` format (including multi-button and `None`).
- `Mouse`: `GetState`, `SetPosition` (task 745), `getIsRelativeMouseModeEXTProperty`/
  `setIsRelativeMouseModeEXTProperty` + relative-delta accumulation (task 746), `INTERNAL_onClicked`→
  `ClickedEXT` (task 748).
- `MouseCursor`: stock cursors non-null (task 753's lazy singletons), `FromTexture2D` (task 751,
  including the `std::invalid_argument` format-validation path), `Dispose()` idempotency (task 752).
- Files: new `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`.
- Verify: `cmake --build cmake-build-debug --target CnaTests -j"$(nproc)" && ./cmake-build-debug/CnaTests --gtest_filter='*Mouse*'`

---

## 9. Do not do yet

- **Do not reconfigure or commit build directories.** `cmake-build-*` are gitignored; if Vulkan/Bgfx
  builds are needed, reconfigure them locally for `cna_input`, but do not commit the dirs.
- **Do not commit the pre-existing working-tree changes** (`D .claude/settings.json`, untracked
  `vendor/wgpu-native/`) — they are unrelated to the input work.
- **Do not start Keyboard phase (I5) before Mouse (I4)** — Mouse is the agreed next focus per
  `plan_input.md`'s phase ordering.
- **No API renames or namespace moves** — XNA/FNA names are frozen. This includes not adding values
  to frozen enums like `PlayerIndex`, and not adding parameters to FNA's public constructors even
  when convenient (see the `GamePadState`/`PacketNumber` precedent in Section 6).
- **No broad refactor of `SdlInputBridge` or `InputManager`** — keep the single-funnel design;
  add cases, do not restructure.
- **No graphics changes** on this branch — graphics is tracked in `GRAPHICS_TASKS.md` on its own track.
- **Do not change the `TextInputEXT` char-based callback signature** to widen it (UTF-16/char32) —
  that is a deliberate, documented deviation and would ripple through the public EXT API.
- **Do not silently "fix" the `GetCapabilities()` `MAX_TOUCHES` deviation** noted in Section 5 as part
  of unrelated work — it needs its own small task/PR since it changes public-facing behavior.

---

## 10. Resume prompt

```
Read NEXT.md first. Open only the files needed for the first task.
Do not refactor unrelated code. Do not expand scope beyond the task.

Context: on branch feature/input. Phase I1 (TextInputEXT), Phase I2 (Touch & gesture pipeline), and
Phase I3 (GamePad behavior/FNA fidelity, tasks 725-740) are all complete. Phase I4 (Mouse) is
implementation-complete too: every task 745-754 (Mouse behavior: SetPosition warp,
IsRelativeMouseModeEXT, back-buffer scale field removal, ClickedEXT; MouseCursor CHECKLIST
compliance: SPDX/NOXNA, FromTexture2D, Dispose/IDisposable, lazy stock-cursor init +
WaitCursor->WaitArrow rename, Handle/AUDIT.md status decision) is done, committed, and pushed. Full
per-task detail is in plan_input.md's Phase I4 table and NEXT.md Section 8 — read those instead of
relying on a summary here. 1866/1866 unit tests pass on the EasyGL build (cmake-build-debug, correctly
wired to cna_input). One unrelated build blocker was hit and fixed this session: a sharp-runtime
System::IAsyncResult interface change broke this repo's StorageDevice.cpp — see Section 5's "Fixed"
row if a future sharp-runtime change breaks the build the same way again.

Next: Task 755 — the last task in Phase I4. New tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp
(auto-discovered via the tests/*.cpp glob) covering MouseState (getters, both constructors,
==/!=/Equals/GetHashCode/ToString), Mouse (GetState, SetPosition, IsRelativeMouseModeEXT property +
relative-delta accumulation, INTERNAL_onClicked->ClickedEXT), and MouseCursor (stock cursors non-null,
FromTexture2D including its std::invalid_argument validation path, Dispose idempotency) — every task
from 745 onward is still untested; dedicated coverage was deliberately deferred to this one task,
matching the task-740 precedent from Phase I3. Completing this closes out Phase I4; Phase I5
(Keyboard) is next after that.

Build/test: cmake --build cmake-build-debug --target CnaTests -j$(nproc)
            ./cmake-build-debug/CnaTests --gtest_filter='*Mouse*'
Make one small verified change, then update NEXT.md (and plan_input.md task status) before finishing.
```
