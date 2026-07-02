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
- **Current development phase:** **Phases I1–I4 are all complete** (TextInputEXT; Touch & gesture
  pipeline; GamePad behavior/FNA fidelity; Mouse behavior and MouseCursor). **Phase I5 (Keyboard
  fidelity and SDL key mapping) is next**, starting at task 760.
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
- **1892 / 1892 unit tests pass** (EasyGL build, `cmake-build-debug/CnaTests`), verified stable under
  `--gtest_shuffle --gtest_repeat=10`. Phase I4 added 26 tests in the new
  `MouseInputTests.cpp` (`MouseStateTest` 10, `MouseTest` 9, `MouseCursorTest` 7) — see
  `plan_input.md` task 755 for full per-suite detail.

### Apps / libraries available
- `CNA` static library (XNA 4.0 API surface).
- `CnaTests` (Google Test unit suite).
- `cna_demo_input` — interactive input demo (keyboard, mouse, gamepad, touch, text input panel).
  Builds; runs crash-free.

### Recently implemented (Phase I4 — tasks 745–755, now complete)
- `Mouse::SetPosition` really warps the OS cursor via `SDL_WarpMouseInWindow`, with FNA's
  relative-mode early-return guard (task 745).
- `Mouse::IsRelativeMouseModeEXT` is a real `getIsRelativeMouseModeEXTProperty()`/
  `NOXNA setIsRelativeMouseModeEXTProperty(bool)` pair backed by `SDL_Get/SetWindowRelativeMouseMode`;
  relative motion deltas from `SDL_EVENT_MOUSE_MOTION`'s `xrel`/`yrel` feed `InputManager`, which
  reports them as `GetMouseState()`'s `X`/`Y` (drained on read) while relative mode is active
  (task 746).
- FNA's dead `INTERNAL_WindowWidth/Height`/`INTERNAL_BackBufferWidth/Height`/`INTERNAL_MouseWheel`
  fields were removed rather than wired up — CNA already solves window↔logical coordinates more
  generally via `SdlInputBridge::to_logical_position`/`IGraphicsBackend::TransformWindowToLogical`.
  **Known limitation:** `SetPosition` has no inverse (logical→window) transform, so its warp target
  is off by the scale factor on a letterboxed/scaled window; documented in-source (`Mouse.cpp`),
  fixing it for real is a graphics-layer change (task 747).
- `ClickedEXT` now fires: `SdlInputBridge` calls `Mouse::INTERNAL_onClicked(event.button.button - 1)`
  on `SDL_EVENT_MOUSE_BUTTON_DOWN` (task 748).
- `MouseCursor` is now fully CHECKLIST-compliant: correct SPDX header, full `NOXNA` tagging (750);
  `FromTexture2D(const Texture2D&, int, int)` via `SDL_CreateColorCursor`, ported from MonoGame's SDL
  backend since FNA has no `MouseCursor` (751); implements `System::IDisposable` with an idempotent
  `Dispose()` (752) — this also surfaced and fixed a latent double-`SDL_DestroyCursor` bug in the
  old defaulted move ctor/assignment; the 11 stock cursors are now lazily-constructed
  `getXProperty()` singletons (Meyer's singleton) instead of eagerly-initialized static fields, with
  `WaitCursor` renamed to `WaitArrow` to match MonoGame (753); the `Handle`/status decision is
  recorded in `AUDIT.md` — **kept**, `GetSDLCursor()` already serves as the `Handle` equivalent (754).
- New `MouseInputTests.cpp` (task 755, 26 tests) — see Section 2's Tests note above.
- **Unrelated build blocker hit and fixed along the way:** the sibling `sharp-runtime` checkout
  committed a `System::IAsyncResult` interface addition that broke this repo's `StorageDevice.cpp`
  — see Section 5's "Fixed" row for the pattern if this happens again.

### What does NOT work yet
- **Keyboard (Phase I5, next focus):** `GetPressedKeys()` order is non-deterministic; SDL
  keycode→Keys map is incomplete (missing F13–F24, Apps, Volume keys, locale fallbacks);
  `GetKeyFromScancodeEXT` is an identity stub; no scancode mode; `GetHashCode()` is not FNA-faithful;
  no `operator[]` indexer. See Section 8.
- **Mouse:** one known limitation carried over from Phase I4 — `SetPosition` has no inverse
  (logical→window) coordinate transform (see above); everything else is done.
- **demo_input text panel is not visually verified** (see Section 5; carried over from Phase I1).
- **Minor, out of scope for Phase I3:** `TouchPanel::GetCapabilities()` still passes `MAX_TOUCHES`
  unconditionally in both branches (noted in the Phase I2 handoff, not yet fixed — see Section 5).

---

## 3. Recent changes

All on `feature/input` (most recent first). Phase I4 grouped commits by related tasks rather than
one-commit-per-task like Phase I3 did; each `feat(Tasks N-M)` commit has a paired
`docs: mark Tasks N-M complete` commit immediately after it in `git log`.

| Commit | Change |
|--------|--------|
| `92911c9` (docs) | Task 754: record `MouseCursor` status decision in `AUDIT.md`; condense NEXT.md |
| `cf5db14` (docs) | mark Task 753 complete in `plan_input.md`; condense NEXT.md handoff |
| `4d833be` | feat(Task 753): lazy stock-cursor construction; rename `WaitCursor`→`WaitArrow` |
| `0936afa` (docs) | mark Task 752 complete in `plan_input.md`; NEXT.md handoff update |
| `70b3955` | feat(Task 752): `MouseCursor::Dispose()` / `System::IDisposable`, + latent move-ctor double-free fix |
| `8ab03d9` (docs) | mark Tasks 745-751 complete in `plan_input.md`; NEXT.md handoff update |
| `e8bac2e` | feat(Tasks 750-751): `MouseCursor` CHECKLIST fixes (SPDX/NOXNA) and `FromTexture2D` |
| `e295db9` | feat(Tasks 745-748): real Mouse behavior — cursor warp, relative mode, `ClickedEXT` |
| `088f4a4` | fix: implement `IAsyncResult.AsyncState/AsyncWaitHandle` in `StorageDevice`'s result types (unrelated sharp-runtime-triggered build blocker) |

Task 755 (`MouseInputTests.cpp`, 26 tests, closes out Phase I4) is done in the working tree as of
this handoff but **not yet committed** — commit/push it before starting Phase I5 if it's still
uncommitted when you resume.

- **Files added:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`.
- **Files modified:** `Mouse.{hpp,cpp}`, `MouseCursor.{hpp,cpp}`, `InputManager.{hpp,cpp}`,
  `SdlInputBridge.cpp`, `StorageDevice.cpp`, `AUDIT.md`, `plan_input.md`, `NEXT.md`.
- **Behavior changed:** `Mouse::SetPosition` now really warps the OS cursor; `IsRelativeMouseModeEXT`
  is real and feeds relative deltas into `GetMouseState()`; `ClickedEXT` fires; `MouseCursor` supports
  custom cursors (`FromTexture2D`), proper disposal, and lazy stock-cursor construction.

---

## 4. Current blocker / main problem

**There is no hard blocker on the EasyGL track** — it builds clean and all 1892 unit tests pass.
One practical issue carries over, and Phase I5 is the next real gap:

1. **Vulkan/Bgfx build dirs are mis-wired (practical gotcha, unchanged from last handoff).**
   - **Symptom:** `cmake-build-vulkan/CMakeCache.txt` and `cmake-build-bgfx/CMakeCache.txt` have
     `CMAKE_HOME_DIRECTORY=/rv/data/development/github.com/openeggbert/cna` (the **sibling** repo),
     not this `cna_input` checkout. Building there compiles the wrong source tree and will not
     include input changes.
   - **Affected:** any Vulkan/Bgfx verification of input work.
   - **Fix pattern (already applied to `cmake-build-debug`):**
     `rm -rf cmake-build-<x> && cmake -S … -B … -G Ninja -DCNA_GRAPHICS_BACKEND=<X> …`.
     The Vulkan/Bgfx dirs have **not** been reconfigured.

2. **Next functional focus: Phase I5 (Keyboard fidelity and SDL key mapping).** `KeyboardState` is
   populated from SDL3 key events and `Keys`/`KeyState` values match FNA exactly, but:
   `GetPressedKeys()` iterates an `unordered_set` (nondeterministic order — XNA/FNA contract requires
   ascending numeric order); the SDL keycode→`Keys` map is missing F13–F24, `Apps`, volume keys, and
   locale fallbacks; `GetKeyFromScancodeEXT` is an identity no-op stub; there's no scancode mode; and
   `GetHashCode()`/`ToString()`/the `operator[]` indexer all need attention. See `plan_input.md`
   Phase I5 (tasks 760–768). Affected: `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`,
   `src/Microsoft/Xna/Framework/Input/{KeyboardState,Keyboard}.cpp`,
   `src/CNA/Internal/Input/SdlInputBridge.cpp`.

---

## 5. Known bugs and limitations

| Status | Item |
|--------|------|
| **Confirmed** | `cmake-build-vulkan` / `cmake-build-bgfx` caches point at the sibling `…/cna` repo (Section 4). |
| **Incomplete** | Keyboard: `GetPressedKeys()` unordered; SDL keycode→Keys map missing F13–F24/Apps/Volume/locale fallbacks; `GetKeyFromScancodeEXT` is identity stub; no scancode mode (Phase I5, next focus). |
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
| **Fixed** | `MouseCursor`'s move constructor/assignment were `= default`, which bitwise-copied the raw `sdlCursor_` pointer without nulling the moved-from source — a moved-from cursor and its target both believed they owned the same `SDL_Cursor*`, so both destructors would eventually double-`SDL_DestroyCursor` it. Not reachable via any code path in this repo (every call site returns a prvalue, elided under C++17 rather than actually moved), but fixed while implementing task 752's `Dispose()` since the same lines were already being touched. Regression-tested in `MouseInputTests.cpp`. |
| **Fixed (unrelated to Mouse work, but blocked all builds)** | The sibling `sharp-runtime` checkout committed (`d61ffb8`) a `System::IAsyncResult` interface addition (`AsyncState`/`AsyncWaitHandle`, full 4-member .NET interface) mid-session, which broke this repo's `StorageDevice.cpp` (`ContainerResult`/`SelectorResult` didn't implement the two new pure-virtual overrides — `CNA` target wouldn't link). Fixed: `void* asyncState` → `std::any asyncState` (same values, no `Begin*` signature change) and a `mutable System::Threading::EventWaitHandle waitHandle{true, ManualReset}` (both classes always report `getIsCompletedProperty() == true`), matching the pattern `sharp-runtime`'s own commit used in its test implementers. **If a future `sharp-runtime` interface change breaks this repo's build again the same way:** check `cd ../sharp-runtime && git status --short && git log -1` before assuming it's transient — if the relevant file is committed (not mid-edit), it's a real, permanent break needing the same kind of fix here, not a wait-and-retry. |

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
  where it is used. A wholly-non-XNA class uses `NOXNA class` (precedent: `ShaderEffect`, `MouseCursor`).
- `// SPDX-License-Identifier: MS-PL` at the top of every `.hpp` and `.cpp`.
- C# properties → `getXProperty()` / `setXProperty()` (no public fields for XNA API). FNA's
  `{ get; internal set; }` pattern maps to a `NOXNA`-tagged public setter (precedent:
  `TextInputEXT::setWindowHandleProperty`, `TouchPanel::setTouchDeviceExistsProperty`,
  `GamePadState::setPacketNumberProperty`, all of `GamePadCapabilities`'s 36 setters,
  `Mouse::setIsRelativeMouseModeEXTProperty`) — **not** a `friend`-based private setter; there is no
  precedent anywhere in CNA for the friend approach.
- A read-only C# auto-property with lazy static-constructor semantics (e.g. MonoGame's
  `MouseCursor.Arrow { get; private set; }`) maps to a `getXProperty()` backed by a function-local
  `static` variable (Meyer's singleton) — not a plain static field — since a plain C++ static data
  member can't be lazily initialized (see `MouseCursor`'s 11 stock-cursor getters, task 753).
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
  cases (see `GestureDetectorTests.cpp`'s fixture for the established pattern). `MouseCursor`'s
  stock-cursor singletons have the same "no reset" property — tests that need a real SDL window
  (e.g. relative-mouse-mode round-trips) must init/quit `SDL_INIT_VIDEO` symmetrically and restore
  any static fields they touched (`Mouse::WindowHandle`, `Mouse::ClickedEXT`) — see
  `MouseInputTests.cpp`'s `ResetMouseState()` helper and the `GameWindowTests.cpp` precedent for the
  `SDL_InitSubSystem(...)`-fails-so-`GTEST_SKIP()` pattern.
- Private dead-zone-mode constructors (`GamePadThumbSticks`/`GamePadTriggers`'s 3-arg ctors,
  friended to `GamePad` only) are only reachable in tests via `InputManager::SetGamePadAxisValue` +
  `GamePad::GetState(playerIndex, deadZoneMode)` — see `GamePadThumbSticksTests.cpp`/
  `GamePadTriggersTests.cpp` for the pattern.
- Classes that require a `GraphicsDevice` to construct (which creates a real SDL window + graphics
  backend) are **not** unit-tested in `CnaTests` — see `OcclusionQueryDynamicBufferTests.cpp`'s and
  `MouseInputTests.cpp`'s header comments for the precedent. `MouseCursor::FromTexture2D` is
  deliberately untested for this reason.

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
```

> No project-wide lint/format target was observed. Doxygen config (`Doxyfile`) exists but is not a
> build/verify step.
>
> Note: builds in this environment occasionally trigger a large rebuild pass (`-- GLOB mismatch! /
> Re-running CMake...`) unrelated to the change being made — this has coincided with a sibling
> `sharp-runtime` checkout being edited concurrently in another session on this machine. Most of the
> time this resolves itself on retry once the other session's edit settles — but not always: if the
> failure is in a `sharp-runtime`-consumed interface (not just an isolated file), check whether the
> other session already **committed** the change (`cd ../sharp-runtime && git log -1`); if so, it's a
> permanent break needing a real fix on the `cna_input` side (see Section 5's "Fixed (unrelated...)"
> row for the pattern), not a wait-and-retry.

---

## 8. Next smallest tasks

Phase I4 (tasks 745–755) is **fully complete**. Phase I5 (Keyboard fidelity and SDL key mapping) is
next — full task list in `plan_input.md` (tasks 760–768). In priority order, each is one focused
session:

1. **Task 760 — Fix `KeyboardState::GetPressedKeys()` ordering.**
   - Goal: must return keys sorted ascending by numeric value (XNA/FNA contract; FNA iterates bits
     0→255). Currently backed by an `unordered_set`, so iteration order is nondeterministic
     (`KeyboardState.cpp:32–35`).
   - Files: `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`.
   - Verify: `cmake --build cmake-build-debug --target CnaTests -j"$(nproc)" && ./cmake-build-debug/CnaTests --gtest_filter='*Keyboard*'`

2. **Task 761 — `KeyboardState::GetHashCode()` FNA fidelity.**
   - Goal: FNA's formula is `keys0 ^ keys1 ^ … ^ keys7` over an 8×32-bit field covering all 256 key
     values. CNA currently uses `XOR(key*31)`. Either port FNA's formula exactly, or document the
     deviation in-source and in `plan_input.md` if an exact port isn't practical.
   - Files: `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`,
     `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`.

3. **Task 762 — Add `KeyState operator[](Keys) const`.**
   - Goal: mirror FNA's `this[Keys]` indexer; keep/alias the existing `getItem` method rather than
     removing it (check for existing callers first).
   - Files: `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`,
     `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`.

4. **Task 763 — Complete the SDL keycode→`Keys` map.**
   - Goal: `try_convert_sdl_key` (`SdlInputBridge.cpp:258–369`) is missing `F13–F24`,
     `APPLICATION`/`MENU`→`Apps`, `SLEEP`, `VOLUMEUP`/`VOLUMEDOWN`, `KP_CLEAR`→`OemClear`,
     `KP_PERIOD`→`OemPeriod`, and AZERTY/Norwegian/BEPO locale fallbacks. Port from FNA's `keyMap`
     (`SDL3_FNAPlatform.cs:2360–2489`).
   - Files: `src/CNA/Internal/Input/SdlInputBridge.cpp`.

Full Phase I5 task list (760–768, including `GetKeyFromScancodeEXT` (764), scancode mode (765),
`ToString` (766), and the expanded `KeyboardInputTests.cpp` (768)) is in `plan_input.md`.

---

## 9. Do not do yet

- **Do not reconfigure or commit build directories.** `cmake-build-*` are gitignored; if Vulkan/Bgfx
  builds are needed, reconfigure them locally for `cna_input`, but do not commit the dirs.
- **Do not commit the pre-existing working-tree changes** (`D .claude/settings.json`, untracked
  `vendor/wgpu-native/`) — they are unrelated to the input work.
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
- **Do not attempt to unit-test `GraphicsDevice`-dependent construction** (e.g. `Texture2D` with real
  pixel data) in `CnaTests` — it requires a real SDL window + graphics backend; use a separate
  integration test executable instead, matching `OcclusionQueryDynamicBufferTests.cpp`'s precedent.

---

## 10. Resume prompt

```
Read NEXT.md first. Open only the files needed for the first task.
Do not refactor unrelated code. Do not expand scope beyond the task.

Context: on branch feature/input. Phases I1-I4 are all complete (TextInputEXT; Touch & gesture
pipeline; GamePad behavior/FNA fidelity, tasks 725-740; Mouse behavior and MouseCursor, tasks
745-755 — MouseInputTests.cpp added 26 tests, all committed/pushed except possibly task 755 itself,
check git status). 1892/1892 unit tests pass on the EasyGL build (cmake-build-debug, correctly wired
to cna_input), verified stable under --gtest_shuffle --gtest_repeat=10. Full per-task detail for
Phase I4 is in plan_input.md — read that instead of relying on a summary here. One unrelated build
blocker was hit and fixed during Phase I4: a sharp-runtime System::IAsyncResult interface change
broke this repo's StorageDevice.cpp — see NEXT.md Section 5's "Fixed" row and Section 7's note if a
future sharp-runtime change breaks the build the same way again (check git log in ../sharp-runtime
before assuming it's transient).

Next: Phase I5 (Keyboard fidelity and SDL key mapping), starting at Task 760 — fix
KeyboardState::GetPressedKeys() ordering (must be ascending by numeric key value, matching FNA's bit
0->255 iteration; currently backed by an unordered_set, so order is nondeterministic,
KeyboardState.cpp:32-35). Then Task 761 (GetHashCode FNA fidelity), Task 762 (operator[] indexer),
and Task 763 (complete the SDL keycode->Keys map: F13-F24, Apps, volume keys, locale fallbacks) follow
the same pattern used throughout Phases I1-I4. Full Phase I5 task list (760-768) is in plan_input.md.

Build/test: cmake --build cmake-build-debug --target CnaTests -j$(nproc)
            ./cmake-build-debug/CnaTests --gtest_filter='*Keyboard*'
Make one small verified change, then update NEXT.md (and plan_input.md task status) before finishing.
```
