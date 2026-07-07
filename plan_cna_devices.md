# plan_cna_devices.md — Implementation Plan for `CNA::Devices` (NOXNA)

**Source analysis:** `noxna_devices.md` (this repo, root). This plan turns that
analysis's recommendations into concrete, one-at-a-time implementation tasks.

**Working mode:** autonomous, one task at a time — no batching. Each task: read
relevant code first, implement, build, test, update this file's task status/resolution
note, commit, push, then move to the next task. If a task genuinely requires the user's
decision (not already answered by `noxna_devices.md`'s own recommendations), it is
marked **BLOCKED** here with the exact question, and work continues with the next
independent task instead of guessing.

**Decisions already made (from `noxna_devices.md` Section 6), not re-asked:**
1. New, independent CMake option `CNA_DEVICES` (not reusing `CNA_NOXNA`) — Section 3.1.
2. `CNA::Devices` members do **not** carry the `NOXNA` marker macro — the namespace
   itself signals "not XNA"; adding `NOXNA` on every member would be redundant noise.
3. No pluggable `IDevicesBackend`-per-platform layer — SDL3 itself is already the
   cross-platform abstraction for every capability in this plan; only a narrow
   per-capability `Detail::I<X>Backend` test seam is used, mirroring
   `Microsoft::Devices::Sensors`' existing `Detail::IVibrateBackend`/`ICompassBackend`
   pattern.
4. File layout: `include/CNA/Devices/`, `src/CNA/Devices/`, `tests/CNA/Devices/`,
   internal-only interfaces under `include/CNA/Devices/Detail/`.

---

## 0. Setup

### DEVICES-CNA-000 — CMake scaffold: `CNA_DEVICES` option + directory wiring — CLOSED (2026-07-07)

- **Priority:** Critical (blocks every other task)
- **Problem:** No `CNA::Devices` namespace, directory, or build option exists yet.
- **Required work:**
  - Add `option(CNA_DEVICES "Enable CNA-specific device/sensor extensions beyond XNA
    4.0 (battery, camera, clipboard, ...)" OFF)` to `CMakeLists.txt`, next to
    `CNA_NOXNA`.
  - Propagate as a `PUBLIC` compile definition on the `CNA` target, mirroring
    `$<$<BOOL:${CNA_NOXNA}>:CNA_NOXNA>` exactly.
  - Create `include/CNA/Devices/.gitkeep`-equivalent (first real header lands with
    Task 001, so this may just be folded into that task instead of a separate empty
    commit — decide at execution time based on whether an empty directory commit is
    useful).
  - Confirm `src/CNA_SOURCES` glob (`src/*.cpp`, `CMakeLists.txt:206`) picks up
    `src/CNA/Devices/*.cpp` automatically — it already does (recursive, no exclusion
    regex would match), so no glob change needed, just confirm once a real file exists.
- **Acceptance criteria:** `cmake -DCNA_DEVICES=ON` configures cleanly; `CNA_DEVICES` is
  visible as a compile definition to any `.cpp` under `src/CNA/Devices/`; building with
  `CNA_DEVICES=OFF` (default) is completely unaffected (no new symbols, no new
  warnings).
- **Suggested files:** `CMakeLists.txt`.
- **Resolution:** Added `option(CNA_DEVICES ...)` right after `CNA_NOXNA`
  (`CMakeLists.txt:21`) and its `PUBLIC` compile-definition propagation on the `CNA`
  target (`$<$<BOOL:${CNA_DEVICES}>:CNA_DEVICES>`, right after `CNA_NOXNA`'s own line)
  — byte-for-byte the same pattern, deliberately no divergence. Verified
  `-DCNA_DEVICES` reaches `CMakeFiles/CNA.dir/flags.make` when ON and is absent when
  OFF (checked both directions via `grep`, not assumed from the CMake generator
  expression alone). Rebuilt the full `CNA` target with `CNA_DEVICES=ON`: clean, no new
  warnings (no new source files exist yet, so this is expected, but confirms the flag
  itself introduces no side effects). Did not create an empty
  `include/CNA/Devices/`/`src/CNA/Devices/` directory in this task — git does not track
  empty directories, and the first real header lands in the very next task
  (`DEVICES-CNA-001`), so a separate empty-directory commit would add no value. Left
  the local `cmake-build-debug` cache configured with `CNA_DEVICES=ON` for the
  remainder of this plan's implementation work (default for new/consumer builds stays
  `OFF`, unchanged).

---

## Phase 1 — smallest, safest, synchronous, all 5 target platforms

### DEVICES-CNA-001 — `PowerInfo` (battery/power status) — CLOSED (2026-07-07)

- **Priority:** High
- **SDL3 API:** `SDL_power.h`, `SDL_GetPowerInfo(int* seconds, int* percent)`.
- **Required work:**
  - `include/CNA/Devices/PowerState.hpp` — enum class mirroring `SDL_PowerState`
    (`Error`, `Unknown`, `OnBattery`, `NoBattery`, `Charging`, `Charged`).
  - `include/CNA/Devices/PowerInfo.hpp` / `src/CNA/Devices/PowerInfo.cpp` — static class,
    `getStateProperty()`, `getBatteryPercentProperty()`, `getSecondsRemainingProperty()`.
  - Full Doxygen on every public member (project-wide rule, `CLAUDE.md`).
  - `tests/CNA/Devices/PowerInfoTests.cpp` — at minimum: calling every method does not
    throw/crash on this (battery-less, containerized) host; percent/seconds are either
    `-1` or within documented valid ranges; `getStateProperty()` returns a valid enum
    value.
- **Acceptance criteria:** builds under `-DCNA_DEVICES=ON`; new test suite passes;
  building with `CNA_DEVICES=OFF` unaffected.
- **Suggested files:** new files only, per above.
- **Resolution:** Created `PowerState.hpp` (enum), `PowerInfo.hpp`/`.cpp` (static class,
  three properties), all `#ifdef CNA_DEVICES`-gated per the established `CNA::Graphics`
  convention. `getStateProperty()` converts `SDL_PowerState` via a small anonymous-
  namespace switch (default case folds to `PowerState::Error`, matching SDL3's own
  "error determining status" semantics — not a silent fallback to `Unknown`).
  `getBatteryPercentProperty()`/`getSecondsRemainingProperty()` each call
  `SDL_GetPowerInfo()` with the other out-param as `nullptr` (SDL3's own documented
  contract: either pointer may be `NULL` to ignore that value) rather than one shared
  call plus caching, since this class holds no state at all. Added
  `tests/CNA/Devices/PowerInfoTests.cpp` (4 tests) asserting the *contract* (valid enum,
  documented sentinel/range) rather than a specific value, since this headless
  container's actual battery status is unknown/unverifiable ahead of time — matches the
  `docs/devices-hardware-checklist.md` precedent of never asserting a specific
  hardware-dependent value no test can control. Build: `cmake --build cmake-build-debug
  --target CNA` (clean, no warnings) and `--target CnaTests` (clean); ran
  `PowerInfoTests.*` (4/4 pass) and the full existing Devices/Sensors filter plus
  `PowerInfoTests.*` together — 347 tests, 345 passed, 2 pre-existing expected
  hardware-gated skips, zero regressions.

### DEVICES-CNA-002 — `Locale` — CLOSED (2026-07-07)

- **Priority:** Medium
- **SDL3 API:** `SDL_locale.h`, `SDL_GetPreferredLocales(int* count)`.
- **Required work:**
  - `include/CNA/Devices/LocaleInfo.hpp` — simple struct (`Language`/`Country` as
    `System::String`, per `SharpRuntime` convention).
  - `include/CNA/Devices/Locale.hpp` / `.cpp` — static `getPreferredLocalesProperty()`
    returning `std::vector<LocaleInfo>`, correctly freeing SDL's returned array
    (`SDL_free`) after copying into CNA-owned storage (never leak or return
    SDL-owned pointers past this function).
  - `tests/CNA/Devices/LocaleTests.cpp` — returns a non-empty list on a normal Linux
    container (or documents/handles the empty case), no leaks (consider running under
    `devices-asan` explicitly for this one, since it's a manual malloc/free boundary).
- **Acceptance criteria:** builds/tests pass; ASan clean.
- **Suggested files:** new files only.
- **Resolution:** Created `LocaleInfo.hpp` (struct) and `Locale.hpp`/`.cpp` (static
  class, one property). **Deviation from this task's own text, noted deliberately:**
  used `std::string` for `Language`/`Country` rather than `System::String`/
  `SharpRuntime::String` — checked `VibrateController.hpp`'s existing
  `getDeviceNameProperty()` (already-established `Microsoft::Devices` precedent for a
  string-returning property) and confirmed it already returns plain `std::string`
  directly, not the `SharpRuntime` alias, despite `SharpRuntime::String` being merely
  `using String = std::string;` — a type alias, not a distinct type. Followed the
  existing local precedent rather than the plan text's own (slightly generic)
  suggestion, for consistency with code already in this repository. `Country` is
  documented and handled as possibly-empty (SDL3's own `SDL_Locale::country` doc
  comment: "Can be NULL") — mapped to `""`, never a null-pointer dereference.
  `getPreferredLocalesProperty()` copies every `language`/`country` C-string into
  CNA-owned `std::string`s before calling `SDL_free()` on SDL's one single allocation
  (per `SDL_GetPreferredLocales()`'s own documented ownership contract), so no
  SDL-owned pointer is ever returned past this function. Added
  `tests/CNA/Devices/LocaleTests.cpp` (3 tests). Build: `cmake --build cmake-build-debug
  --target CNA`/`--target CnaTests` (both clean). Ran `LocaleTests.*` (3/3 pass) and,
  per this task's own acceptance criterion, explicitly re-ran under `devices-asan`
  (`cmake -DCNA_DEVICES=ON` reconfigure + rebuild) with `ASAN_OPTIONS=detect_leaks=1`:
  `LocaleTests.*:PowerInfoTests.*` — 7/7 pass, **zero ASan reports** (confirms the
  manual `SDL_free()` boundary is leak-free). Full existing Devices/Sensors filter plus
  both new `CNA::Devices` suites together: 350 tests, 348 passed, 2 pre-existing
  expected skips, zero regressions.

### DEVICES-CNA-003 — `Clipboard` — CLOSED (2026-07-07)

- **Priority:** Medium
- **SDL3 API:** `SDL_clipboard.h` — `SDL_SetClipboardText`/`SDL_GetClipboardText`/
  `SDL_HasClipboardText`.
- **Required work:**
  - `include/CNA/Devices/Clipboard.hpp` / `.cpp` — static `setTextProperty(const
    System::String&)` returning `bool`, `getTextProperty()` returning `System::String`,
    `getHasTextProperty()` returning `bool`.
  - Explicitly documented behavior when no windowing/video subsystem is initialized
    (this container has no real desktop session) — must not crash, should return a
    clear false/empty result.
  - `tests/CNA/Devices/ClipboardTests.cpp` — round-trip set/get where a real clipboard
    is available; graceful no-crash behavior verified where it is not (this headless
    container is exactly that case, so this test doubles as the "no video subsystem"
    verification).
- **Acceptance criteria:** builds/tests pass in this exact headless container (proving
  the no-crash path for real, not just in theory).
- **Suggested files:** new files only.
- **Resolution:** Created `Clipboard.hpp`/`.cpp` (static class, three properties). Used
  plain `std::string` (same rationale/precedent as `DEVICES-CNA-002`'s `Locale`
  deviation note — matches `VibrateController.hpp`'s existing style). `getTextProperty()`
  frees SDL's returned buffer via `SDL_free()` after copying into a CNA-owned
  `std::string`, mirroring `Locale`'s ownership pattern. Documented in the class's own
  Doxygen that every member is main-thread-only per SDL3's own doc comments, and that
  Web/Emscripten additionally gates clipboard access behind a user-gesture/permission
  context. Added `tests/CNA/Devices/ClipboardTests.cpp` (5 tests) — since this test
  binary (`CnaTests`) never initializes SDL's video subsystem (no window is ever
  created), running these tests here **is** the "no video subsystem available"
  verification this task's acceptance criteria specifically asked for, not just a
  theoretical claim: `SetThenGetIsConsistentWhetherOrNotAClipboardIsAvailable` branches
  on `setTextProperty()`'s actual return value and asserts the correct behavior for
  either outcome, rather than assuming one. Build: `cmake --build cmake-build-debug
  --target CNA`/`--target CnaTests` (both clean). Ran `ClipboardTests.*` (5/5 pass) and
  the full existing filter plus all three new `CNA::Devices` suites together: 355
  tests, 353 passed, 2 pre-existing expected skips, zero regressions.

### DEVICES-CNA-004 — `UrlLauncher` — CLOSED (2026-07-07)

- **Priority:** Low
- **SDL3 API:** `SDL_misc.h`, `SDL_OpenURL(const char*)`.
- **Required work:**
  - `include/CNA/Devices/UrlLauncher.hpp` / `.cpp` — static `Open(const System::String&
    url)` returning `bool`.
  - `tests/CNA/Devices/UrlLauncherTests.cpp` — cannot verify a browser actually opens in
    CI; test what's testable (return value on a malformed/empty URL, no crash on a
    well-formed one even with no display available — document honestly if this
    container's `SDL_OpenURL` call has any observable side effect at all here).
- **Acceptance criteria:** builds/tests pass; test file honestly documents what it could
  and could not verify in this container (mirroring the `docs/devices-hardware-checklist.md`
  precedent of never overclaiming coverage).
- **Suggested files:** new files only.
- **Resolution:** Created `UrlLauncher.hpp`/`.cpp` — a single static `Open()` method,
  the simplest class in this plan. Used `std::string` (same precedent as
  `DEVICES-CNA-002`/`003`). Added `tests/CNA/Devices/UrlLauncherTests.cpp` (3 tests),
  each explicitly documented as verifying only "does not crash/hang," never asserting a
  specific return value for the well-formed-URL case — this container has no browser
  or `xdg-open`-equivalent to observe, so any assumption about success/failure would be
  environment-guessing, not a real check. Ran with an explicit `timeout 15` wrapper as
  a safety net (a `SDL_OpenURL()` call shells out to an external process on desktop
  platforms, and a hang there would otherwise stall the whole run) — completed in 10ms
  total for all 3 tests, confirming SDL3's own call returns promptly even with no
  handler available, not just in theory. Build: `cmake --build cmake-build-debug
  --target CNA`/`--target CnaTests` (both clean). Full existing filter plus all four
  new `CNA::Devices` suites together: 358 tests, 356 passed, 2 pre-existing expected
  skips, zero regressions.

### DEVICES-CNA-005 — `SystemInfo` — CLOSED (2026-07-07)

- **Priority:** Medium
- **SDL3 API:** `SDL_cpuinfo.h` — `SDL_GetNumLogicalCPUCores()`, `SDL_GetSystemRAM()`.
  Deliberately minimal scope per `noxna_devices.md` Section 4.5 — do not wrap every
  SIMD-flag function.
- **Required work:**
  - `include/CNA/Devices/SystemInfo.hpp` / `.cpp` — static
    `getLogicalCpuCoreCountProperty()`, `getSystemRamMegabytesProperty()`.
  - `tests/CNA/Devices/SystemInfoTests.cpp` — both values are `> 0` on any real/CI
    host (a container always has at least 1 logical core and some RAM reported).
- **Acceptance criteria:** builds/tests pass.
- **Suggested files:** new files only.
- **Resolution:** Created `SystemInfo.hpp`/`.cpp` (static class, two properties),
  deliberately scoped to exactly the two functions this task named — no SIMD-flag
  wrapping added. Added `tests/CNA/Devices/SystemInfoTests.cpp` (3 tests): both values
  positive on this container, plus a determinism check (repeated calls return the same
  value within one process, since neither CPU core count nor RAM changes at runtime).
  Build: `cmake --build cmake-build-debug --target CNA`/`--target CnaTests` (both
  clean). **This closes every individual Phase 1 capability task** — full existing
  filter plus all five new `CNA::Devices` suites together: 361 tests, 359 passed, 2
  pre-existing expected skips, zero regressions. `DEVICES-CNA-006` (the dedicated
  Phase 1 sanitizer verification pass) is next.

### DEVICES-CNA-006 — Phase 1 verification pass — CLOSED (2026-07-07)

- **Priority:** High
- **Required work:** build `CnaTests` with `-DCNA_DEVICES=ON`, run the full new
  `tests/CNA/Devices/*Tests.cpp` suite together; run under `devices-asan`/`devices-ubsan`
  at minimum (per this project's established sanitizer discipline); confirm
  `CNA_DEVICES=OFF` (default) still builds with zero new warnings/symbols.
- **Acceptance criteria:** documented exact pass/fail/skip counts for both the plain and
  sanitizer runs, mirroring `plan_devices.md`'s `VERIFY-001`/`VERIFY-002` precedent.
- **Resolution:** Ran all 18 tests across the 5 Phase 1 `CNA::Devices` suites
  (`PowerInfoTests` 4, `LocaleTests` 3, `ClipboardTests` 5, `UrlLauncherTests` 3,
  `SystemInfoTests` 3) together under both sanitizer presets, reconfigured with
  `-DCNA_DEVICES=ON`:
  - **`devices-asan`** (`ASAN_OPTIONS=detect_leaks=1`): **18/18 passed, zero ASan
    reports.** Confirms `Locale`/`Clipboard`'s manual `SDL_free()` boundaries are
    leak-free under the full combined suite, not just their own individual test files
    in isolation.
  - **`devices-ubsan`**: **18/18 passed, zero UBSan reports** (`grep -c "runtime
    error"` on the full output — `0`, verified directly rather than eyeballed).
  - **`CNA_DEVICES=OFF` (default) unaffected:** configured a fresh, separate build
    directory with no `CNA_DEVICES` flag at all (true default), confirmed via `grep`
    that `-DCNA_DEVICES` is absent from `CNA.dir/flags.make`, then built the `CNA`
    target from scratch — clean, and confirmed via `build.make` that all 5 new
    `src/CNA/Devices/*.cpp` files are still compiled (as empty, `#ifdef`-gated
    translation units, exactly like `CNA::Graphics`'s existing `CNA_NOXNA` behavior) —
    zero new warnings, zero new symbols, no observable difference from before this
    plan's work began.
  - Plain `cmake-build-debug` build (no sanitizer) was already exercised continuously
    across `DEVICES-CNA-001` through `-005`, each time confirming the full existing
    Devices/Sensors filter plus every `CNA::Devices` suite added so far — last full
    count (after `DEVICES-CNA-005`): 361 tests, 359 passed, 2 pre-existing expected
    hardware-gated skips.
  - **Phase 1 is now fully verified and complete.** `DEVICES-CNA-007` (`DisplayInfo`,
    Phase 2) is next.

---

## Phase 2 — needs one design decision first

### DEVICES-CNA-007 — `DisplayInfo` — CLOSED (2026-07-07)

- **Priority:** Medium
- **Problem:** per `noxna_devices.md` Section 4.6, this needs to reach the *same*
  `SDL_Window`/display the existing `Microsoft::Xna::Framework::GraphicsDevice`/
  `GameWindow` already owns — not create a second, independent window/display
  abstraction. **This requires reading `GraphicsDevice.hpp`/`GameWindow.hpp`/the active
  backend's window-creation code first**, to find the right internal hook, before
  writing any `CNA::Devices` code.
- **Required work (once the hook is found):** `include/CNA/Devices/DisplayInfo.hpp`/`.cpp`
  wrapping `SDL_GetCurrentDisplayOrientation`/`SDL_GetNaturalDisplayOrientation`/
  `SDL_GetDisplayContentScale`/`SDL_GetWindowSafeArea` against that existing window.
- **Acceptance criteria:** does not construct or own any `SDL_Window`/`SDL_DisplayID` of
  its own; reads from the engine's existing one; builds/tests pass.
- **Suggested files:** read-only research first —
  `include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp`,
  `include/Microsoft/Xna/Framework/GameWindow.hpp`,
  `include/CNA/Internal/Backends/*/*.hpp` (whichever backend is active) — then new
  `CNA::Devices` files.
- **Resolution:** Read `GameWindow.hpp` first, as required. Found: `GameWindow`
  already privately owns an `SDL_Window* window_` (constructor `GameWindow(SDL_Window*
  window)`, `friend class Game; friend class GraphicsDeviceManager;`, no existing
  public accessor to reach it). **Bigger finding that reshaped this task's scope:**
  `GameWindow` already exposes `getCurrentOrientationProperty()` (real XNA
  `DisplayOrientation`, no `NOXNA` tag) and `getClientBoundsProperty()` (real XNA
  `Rectangle`) — i.e. orientation and window bounds, two of the four SDL3 functions
  `noxna_devices.md` Section 4.6 originally proposed wrapping, are **already covered by
  real XNA API** and must not be duplicated (same "don't create two competing ways to
  ask the same question" principle `noxna_devices.md` Section 4.10 already applied to
  storage/microphone). **Narrowed `DisplayInfo`'s actual scope to only the two
  genuinely new capabilities:** window content scale and window safe-area insets —
  neither has any XNA/`GameWindow` equivalent today.
  - **Window-ownership hook:** added exactly one new method to the existing
    `GameWindow` class — `NOXNA [[nodiscard]] SDL_Window* GetNativeSdlWindowEXT()
    const;` (`GameWindow.hpp`/`.cpp`), returning the private `window_` directly. Tagged
    both `NOXNA` (the project-wide, compile-time-enforced convention) and with the
    `EXT` name suffix, matching this same class's existing sibling convention
    (`getIsBorderlessEXTProperty()`) for a CNA-added member on an otherwise-real-XNA
    class. This is the "new members on an existing file" pattern the user's own
    original request for `noxna_devices.md` explicitly anticipated as one of two valid
    approaches — not an unplanned scope expansion.
  - **Used `SDL_GetWindowDisplayScale(window)` instead of `SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window))`**
    (the function `noxna_devices.md` originally named): re-reading `SDL_video.h`'s own
    doc comment for `SDL_GetDisplayContentScale()` found it explicitly recommends the
    per-window function instead, since "the per-window content scale factor may differ
    from the base value of the display it is on, particularly on high-DPI and/or
    multi-monitor desktop configurations" — a more correct choice found only by reading
    the source directly rather than trusting the analysis document's own (slightly
    imprecise) function name.
  - Did **not** implement `SDL_GetCurrentDisplayOrientation`/
    `SDL_GetNaturalDisplayOrientation` wrapping at all, per the duplication finding
    above.
  - `getSafeAreaProperty()` returns a `Microsoft::Xna::Framework::Rectangle` (SDL3's
    own `SDL_GetWindowSafeArea()` returns a rect, not directional inset amounts) —
    reused the existing `Rectangle` type rather than inventing a new insets struct.
    Both methods return SDL3's own documented failure sentinels (`0.0f` content scale,
    `Rectangle::Empty` safe area) for a `GameWindow` with no attached SDL window,
    rather than an invented default that could mask a real failure (matches
    `PowerInfo`'s `-1` sentinel precedent).
  - Added `tests/CNA/Devices/DisplayInfoTests.cpp` (4 tests): 3 against the "no SDL
    window" path (this test binary's normal, always-available state), plus one
    following `GameWindowTest.SetAndGetTitle_UsingSdlWindow`'s existing precedent
    (`tests/Microsoft/Xna/Framework/GameWindowTests.cpp`) that creates a real, hidden
    `SDL_Window` and gracefully `GTEST_SKIP()`s if this container's video subsystem is
    unusable. **It was not skipped** — this container's video subsystem is usable, so
    this test genuinely exercised both real SDL3 calls end-to-end and confirmed
    positive, non-degenerate values, not just a "didn't crash" check.
  - Build: `cmake --build cmake-build-debug --target CNA`/`--target CnaTests` (both
    clean, zero new warnings from the `GameWindow.hpp`/`.cpp` edit). Confirmed the
    existing `GameWindowTest`/`GameTest` suites (12 tests) still pass unchanged — the
    new method introduced zero regression to the class it was added to. Ran
    `DisplayInfoTests.*` (4/4 pass, including the real-window test) and, given this
    task touches real SDL window creation/destruction, explicitly re-ran
    `DisplayInfoTests.*:GameWindowTest.*:GameTest.*` under `devices-asan` with
    `ASAN_OPTIONS=detect_leaks=1`: 16/16 pass, zero reports. Full existing filter plus
    every `CNA::Devices` suite through this task: 377 tests, 375 passed, 2 pre-existing
    expected skips, zero regressions.

---

## Phase 3 — desktop-only capabilities

### DEVICES-CNA-008 — `FileDialog` — CLOSED (2026-07-07)

- **Priority:** Medium
- **SDL3 API:** `SDL_dialog.h` — async callback-based
  `SDL_ShowOpenFileDialog`/`SDL_ShowSaveFileDialog`/`SDL_ShowOpenFolderDialog`.
- **Required work:**
  - `include/CNA/Devices/FileDialog.hpp`/`.cpp` — static `getIsSupportedProperty()`
    (compile-time `false` on Android/iOS/Web via `CNA::getCurrentPlatform()`, real probe
    on Desktop), `ShowOpenFile`/`ShowSaveFile`/`ShowOpenFolder` each taking a
    `std::function` callback per `noxna_devices.md`'s sketch.
  - Document the desktop-only limitation as permanent, not a gap (mirrors
    `Compass`/`Motion`'s "real on Android only" precedent).
  - `tests/CNA/Devices/FileDialogTests.cpp` — `getIsSupportedProperty()` behavior per
    platform; cannot test actual dialog interaction headlessly — document that
    limitation explicitly rather than skipping silently.
- **Acceptance criteria:** builds/tests pass; `getIsSupportedProperty()` is correct on
  this Linux container (desktop → real SDL3 probe, not a hardcoded true).
- **Suggested files:** new files only.
- **Resolution:** **Correction found before writing any implementation:** reading
  `third_party/SDL/src/dialog/` directly (per this project's own established "verify
  against source, don't trust the analysis document" discipline) found a real
  `third_party/SDL/src/dialog/android/SDL_androiddialog.c` backend, and
  `third_party/SDL/CMakeLists.txt`'s own per-platform dialog-source selection compiles
  it in for `ANDROID` — this task's (and `noxna_devices.md`'s) original "desktop-only"
  claim was wrong. Only iOS and Web/Emscripten genuinely lack a backend. Corrected
  `getIsSupportedProperty()`'s design (`false` only for `iOS`/`Web` via
  `CNA::getCurrentPlatform()`) and updated `noxna_devices.md` Sections 1/4.7/5 to match,
  before implementing anything against the wrong assumption.
  - **A real incident during this task's own test-writing, caught and fixed
    immediately:** an early draft of `FileDialogTests.cpp` called the real
    `Show*Dialog()` functions directly with normal parameters. Running it on this
    development machine (which, unlike assumed, has a real, working desktop session
    with `zenity`/XDG portals available) launched four real, interactive `zenity`
    windows that hung indefinitely waiting for a human — confirmed via `ps aux`, then
    killed manually. This was an active, unwanted side effect on the real machine, not
    a hypothetical risk. **Fixed by giving `FileDialog` a swappable backend**
    (`Detail::IFileDialogBackend`, default `Detail::SdlFileDialogBackend`,
    `FileDialog::SetBackendForTesting()`) — the exact `Detail::I<X>Backend` pattern
    `plan_cna_devices.md`'s own Section 3.2 already called for "wherever its real
    backend cannot be meaningfully exercised" — this class specifically needed it,
    unlike the simpler static Phase 1 classes, because its real backend has a
    genuinely uncontainable side effect, not merely an untestable one.
  - Also found and fixed a real memory-safety bug in the same draft, before it was
    ever run: SDL3's own doc comment for `SDL_Show*Dialog()`'s `filters` parameter
    states it "must remain valid at least until the callback is invoked" — since these
    calls are asynchronous, a naive implementation building `SDL_DialogFileFilter`
    pointers into a function-local vector (destroyed when the synchronous call
    returns) would leave SDL reading freed memory once the async callback actually
    fires. Fixed by heap-allocating a `DialogContext` (owning the callback,
    `defaultLocation`, and every filter name/pattern string, with `SdlFilters` built
    only after `FilterNames`/`FilterPatterns` are reserved to their final size so no
    reallocation can invalidate the `c_str()` pointers taken into it) and freeing it
    exactly once, inside the trampoline, matching SDL3's own "callback fires exactly
    once" contract.
  - Final architecture: `include/CNA/Devices/Detail/IFileDialogBackend.hpp` (interface
    + `FileDialogResultCallback` type, defined outside `FileDialog` specifically so the
    interface doesn't depend on it), `Detail/SdlFileDialogBackend.hpp`/`.cpp` (real
    backend, the `DialogContext`/trampoline logic above), `FileDialog.hpp`/`.cpp`
    (thin static dispatcher holding a process-wide swappable backend pointer behind a
    mutex, mirroring `VibrateController`'s own backend-storage discipline).
  - Added `tests/CNA/Devices/FileDialogTests.cpp` (6 tests) using a
    `FakeFileDialogBackend`/`ScopedFakeFileDialogBackend` pair — the exact
    `FakeVibrateBackend`/`ScopedFakeVibrateBackend` pattern from
    `VibrateControllerTests.cpp`, restoring the real backend in the destructor so a
    fake never leaks into an unrelated test run later in the same process. **The real
    backend is never invoked by any test in this file** — confirmed directly by
    re-running under a `timeout` wrapper (0ms total for all 6, versus the ~130ms and
    four real `zenity` spawns the first, incorrect draft produced) and by `ps aux`
    showing zero `zenity`/`kdialog` processes after the run.
  - Build: `cmake --build cmake-build-debug --target CNA`/`--target CnaTests` (both
    clean). Ran `FileDialogTests.*` (6/6 pass, 0ms, no stray processes) and the full
    existing filter plus every `CNA::Devices` suite through this task: 383 tests, 381
    passed, 2 pre-existing expected skips, zero regressions. Also explicitly re-ran
    under `devices-asan` with `ASAN_OPTIONS=detect_leaks=1`: 6/6 pass, exit code `0`,
    zero reports — confirms the `DialogContext`/fake-backend heap allocations are
    leak-free.

### DEVICES-CNA-009 — `SystemTray` — CLOSED (2026-07-07)

- **Priority:** Low
- **SDL3 API:** `SDL_tray.h` — `SDL_CreateTray`/`SDL_CreateTrayMenu`/
  `SDL_InsertTrayEntryAt`/`SDL_SetTrayEntryCallback`/`SDL_DestroyTray`.
- **Required work:**
  - `include/CNA/Devices/SystemTray.hpp`/`.cpp` — owned-object class (constructor/
    destructor pairing, not static), nested menu/entry API per `noxna_devices.md`'s
    sketch. `getIsSupportedProperty()` same desktop-only story as `FileDialog`.
  - `tests/CNA/Devices/SystemTrayTests.cpp` — construction/destruction safety,
    `getIsSupportedProperty()` correctness; real tray-icon visual verification is
    inherently manual/human — document what's out of reach here explicitly (mirrors
    `docs/devices-hardware-checklist.md`'s honesty precedent for anything a headless
    container cannot observe).
- **Acceptance criteria:** builds/tests pass; no leak of the underlying `SDL_Tray*`
  (verify under `devices-asan`).
- **Suggested files:** new files only.
- **Resolution:** Confirmed genuinely desktop-only by reading
  `third_party/SDL/src/tray/` (`cocoa`/`unix`/`windows` only, no `android` directory,
  unlike `FileDialog`'s corrected finding) and `third_party/SDL/CMakeLists.txt`'s own
  per-platform tray-source selection — this claim, unlike `FileDialog`'s original one,
  checked out as accurate. **Designed with a swappable backend from the very start
  this time**, applying the lesson from `DEVICES-CNA-008`'s real `zenity` incident
  before writing any test: `Detail::ITrayBackend` + `Detail::SdlTrayBackend`, deliberately
  scoped to a flat, single-level menu (no submenus — `SDL_CreateTraySubmenu()` not
  wrapped, matching this task's own "Low priority, niche value" framing). **Backend
  injection had to be a constructor parameter, not a post-construction
  `SetBackendForTesting()` call** (unlike `VibrateController`/`Compass`/`FileDialog`):
  the real backend's `Create()` — which is what actually makes an OS-visible tray icon
  appear — runs as soon as `SystemTray`'s constructor asks for it, so a fake must
  already be in place *before* construction, not swapped in afterward. Added a second,
  explicitly test-only constructor overload taking the backend directly.
  - **A second real bug found by this task, this time via `devices-asan` rather than
    by direct observation like the `zenity` incident:** an early draft of
    `DestructorCallsDestroyOnTheInjectedFakeBackend` read a raw `FakeTrayBackend*`
    pointer's `DestroyCalled` field *after* the owning `SystemTray` (and therefore the
    backend it owned) had already gone out of scope and been destroyed — a genuine
    heap-use-after-free, confirmed by ASan's full report (`SystemTrayTests.cpp:137`,
    reading memory freed by `SystemTray::~SystemTray()` at `SystemTrayTests.cpp:135`).
    **This did not crash or fail under the plain (non-sanitizer) `cmake-build-debug`
    build at all** — a concrete, first-hand demonstration of exactly why this
    project's sanitizer discipline exists, not a hypothetical risk. Fixed by adding a
    `std::shared_ptr<bool> DestroyedFlag` to the fake backend, set from `Destroy()`,
    checked via the test's own independently-owned `shared_ptr` copy (which safely
    outlives the fake) rather than via the fake's own (about-to-be-freed) member.
  - Added `tests/CNA/Devices/SystemTrayTests.cpp` (8 tests): construction/destruction
    forwarding to the fake, tooltip forwarding, entry-add parameter forwarding and
    index assignment, entry click-callback firing, and checked/enabled round-trips —
    all exclusively via the fake backend; the real backend's actual OS-level tray
    visibility is, as this task's own acceptance criteria anticipated, inherently
    manual/human-observable and out of reach for an automated test — stated honestly,
    not silently skipped.
  - Build: `cmake --build cmake-build-debug --target CNA`/`--target CnaTests` (both
    clean). Ran `SystemTrayTests.*` (8/8 pass, 0ms, confirmed via `ps aux` that no
    real tray-related process/state was created). Re-ran under `devices-asan` with
    `ASAN_OPTIONS=detect_leaks=1`: caught the real use-after-free above on the first
    run (non-zero exit, full report captured); after the fix, 8/8 pass, exit code `0`,
    zero reports. Full existing filter plus every `CNA::Devices` suite through this
    task: 391 tests, 389 passed, 2 pre-existing expected skips, zero regressions.
    **All of Phases 1-3 are now complete.**

---

## Phase 4 — `Camera` (its own design pass, not a quick addition)

### DEVICES-CNA-010 — `Camera` design note (no implementation yet) — CLOSED (2026-07-07)

- **Priority:** Low (relative to Phases 1-3 — high complexity, narrower audience)
- **Problem:** per `noxna_devices.md` Section 4.9, this is materially harder than every
  other capability in this plan: async permission state machine, poll-based frame
  delivery (not the existing push-callback sensor model), and a required
  texture-upload bridge into whichever `Microsoft::Xna::Framework::Graphics`
  backend (`EASYGL`/`VULKAN`/`BGFX`/`SDL_RENDERER`) is active.
- **Required work (this task specifically):** write a short, dedicated design note
  (either its own section appended here, or a new `docs/cna-devices-camera-design.md`)
  covering: the permission/state-machine shape, how `SDL_AcquireCameraFrame`'s
  `SDL_Surface*` gets turned into a `Texture2D` for each graphics backend, and what a
  `Detail::ICameraBackend` test seam would look like — **before** writing any
  `Camera.hpp`. Do not implement `Camera` itself in this task.
- **Acceptance criteria:** a reviewable design note exists; no `Camera` class exists
  yet.
- **Resolution:** Wrote `docs/cna-devices-camera-design.md`. Key findings from actually
  reading this codebase's own graphics-texture pipeline before writing the note
  (rather than speculating): the texture-upload bridge this task's own problem
  statement flagged as needing per-graphics-backend work turns out to already be
  solved, generically, by existing infrastructure —
  `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`'s `ITextureBackend::UpdatePixels(const
  uint8_t* rgba, int stride)` plus `Texture2D::SetDataRGBA()` (`NOXNA`, already
  public) together mean no new backend-specific code is needed for the upload step
  itself — only correct RGBA format negotiation when opening the camera (flagged as an
  open question needing empirical, per-platform verification, not resolved here).
  Refined the permission/state design into a `CameraState` enum (`NotSupported`/
  `Closed`/`Opening`/`Denied`/`Ready`/`Lost`) mirroring `SensorState`'s own honesty
  principle. Proposed a **poll-based** public API (`TryAcquireFrame()`, no result
  callback) specifically because `SDL_AcquireCameraFrame()` itself is poll-based, not
  push-based — matching the underlying SDL3 shape rather than forcing every
  `CNA::Devices` class into the same callback pattern regardless of fit. Documented
  four open questions for the implementation task (RGBA negotiation reliability,
  event-queue/permission-UX ownership, multi-camera support, device-loss recovery) and
  a recommended narrow first-implementation scope. Cross-linked from `noxna_devices.md`
  Section 4.9. **No `Camera` class, header, or test file was created** — confirmed
  by design, matching this task's own acceptance criteria exactly.

### DEVICES-CNA-012 — Implement `Camera` per the design note's recommended narrow scope — CLOSED (2026-07-07)

- **Priority:** Medium
- **Problem:** `docs/cna-devices-camera-design.md` (Task `DEVICES-CNA-010`) designed
  `Camera` but explicitly did not implement it. This task implements exactly the
  design note's own "recommended scope for the first implementation task" (Section 7
  there): single camera device (first one `SDL_GetCameras()` reports, no device
  selection), synchronous permission polling (no SDL event-queue integration),
  RGBA-only format request.
- **Required work:**
  - `include/CNA/Devices/CameraState.hpp`, `CameraPosition.hpp`, `CameraDeviceInfo.hpp`
    — small value types, mirroring the design note's proposed `CameraState` enum
    exactly (`NotSupported`/`Closed`/`Opening`/`Denied`/`Ready`/`Lost`).
  - `include/CNA/Devices/Detail/ICameraBackend.hpp` — `CameraFrame` (tightly-packed
    RGBA8 bytes) plus the backend interface (`GetState`/`GetFrameWidth`/
    `GetFrameHeight`/`TryAcquireFrame`).
  - `include/CNA/Devices/Detail/SdlCameraBackend.hpp`/`src/.../SdlCameraBackend.cpp`
    — real backend. Constructor-injection testability seam (mirroring `SystemTray`'s
    pattern, not `FileDialog`'s post-construction swap), designed in from the start
    per the design note's own Section 5 — never retrofitted after an incident.
  - `include/CNA/Devices/Camera.hpp`/`src/CNA/Devices/Camera.cpp` — poll-based public
    API (`TryAcquireFrame(Texture2D&)`, no result callback, matching
    `SDL_AcquireCameraFrame()`'s own poll-based shape exactly, per the design note's
    Section 4).
  - `tests/CNA/Devices/CameraTests.cpp` — fake backend injected via the constructor
    from the first test written; never touches the real backend.
- **Design decisions made during implementation, beyond the design note's own scope:**
  - **`SDL_GetCameraSupportedFormats()`'s documented empty-list case (Emscripten
    specifically) handled explicitly:** SDL3's own doc comment states this call
    legally returns an empty list on Emscripten (which won't describe a camera's
    capabilities until opened) — this does **not** mean "unsupported"; `SdlCameraBackend`
    falls back to building its own RGBA32-format request spec directly rather than
    incorrectly treating an empty supported-formats list as "no camera available".
  - **RGBA-only enforced by verifying the actual negotiated format after opening,**
    not just trusting the request: `SDL_GetCameraFormat()` is checked after
    `SDL_OpenCamera()` succeeds; if the actual format isn't `SDL_PIXELFORMAT_RGBA32`
    (would contradict SDL's own documented "seamlessly converts" contract for
    `SDL_OpenCamera()`), the device is closed and reported `NotSupported` rather than
    attempting any pixel-format conversion — matching the design note's own scope
    ("fail/report NotSupported rather than converting non-RGBA formats").
  - **`Camera::getFrameWidthProperty()`/`getFrameHeightProperty()` added** (not in the
    design note's illustrative sketch) — needed because `Texture2D` has no resize API
    after construction (`Texture2D(GraphicsDevice&, width, height)` is fixed at
    construction time), so the caller must construct their `Texture2D` with the
    camera's actual negotiated frame size in advance; `TryAcquireFrame()` returns
    `false` without modifying the texture if the caller's dimensions don't match,
    rather than silently uploading with a wrong stride.
  - **Non-tightly-packed `SDL_Surface` rows handled explicitly:**
    `Texture2D::SetDataRGBA()` assumes `width * 4` stride with no row padding;
    `SdlCameraBackend::TryAcquireFrame()` compacts rows into a tightly-packed buffer
    whenever the acquired surface's `pitch` differs from `width * 4`, rather than
    assuming camera frames are always tightly packed.
- **Acceptance criteria:** builds under `CNA_DEVICES=ON`; tests pass using the fake
  backend exclusively (no real camera ever opened in an automated run); full existing
  suite has zero regressions; clean under `devices-ubsan`.
- **Suggested files:** new files only.
- **Build/test:** `CNA` and `CnaTests` both build cleanly. 8 new tests in
  `CameraTests.cpp`, all passing (some exercise a real `GraphicsDevice`/`Texture2D` —
  the first `CNA::Devices` test file to do so — confirmed via existing test-log output
  that this correctly constructs the real EasyGL/OpenGL backend, matching established
  precedent in `DrawUserPrimitivesArgumentGuardTest`/etc., not a new risk). Full suite:
  3388/3388 (3386 pass + 2 expected skips, up from 3380 — the 8 new tests), **zero
  regressions**. Clean under `devices-ubsan` (exit 0) and under `devices-asan` with
  leak detection explicitly disabled (`ASAN_OPTIONS=detect_leaks=0`, exit 0, no
  memory-safety errors).
  - **A pre-existing, unrelated ASan leak report found and correctly attributed, not
    misdiagnosed as a `Camera` bug:** running `CameraTests.*` under
    `devices-asan` with `ASAN_OPTIONS=detect_leaks=1` reports ~12KB leaked across 16
    allocations, all inside the EasyGL/OpenGL/Mesa graphics-driver stack (stack frames
    inside `libdrm.so.2`/unknown modules, none inside any `CNA::Devices`/`Camera` code
    path). **Verified this is pre-existing and unrelated to `Camera`**, not a new bug:
    ran the same `ASAN_OPTIONS=detect_leaks=1` against a completely unrelated,
    already-existing test that also constructs a real `GraphicsDevice`
    (`DrawUserPrimitivesArgumentGuardTest`, no `Camera`/`CNA::Devices` involvement at
    all) and got the identical leak pattern (~30KB across 40 allocations, same
    stack-trace shape). This is a real, pre-existing gap in this project's own
    sanitizer coverage — no `CNA::Devices` test had ever constructed a real
    `GraphicsDevice` under `devices-asan` before `CameraTests.cpp`, so this is the
    first time it surfaced, not something `Camera`'s own code introduced. Out of
    scope to fix here (a graphics-backend/driver-level investigation, unrelated to
    this task's own code); noted for whoever next touches `devices-asan` +
    `GraphicsDevice`-constructing tests together.
  **This closes Phase 4 and every currently-assigned task in `plan_cna_devices.md`.**

---

## Phase 5 — Round 2 additions (`noxna_devices.md` Section 8)

### DEVICES-CNA-011 — `MessageBox` — CLOSED (2026-07-07)

- **Priority:** Medium (approved by the user 2026-07-07, after `noxna_devices.md`
  Section 8's Round 2 survey; `Monitors`, `Process`, and the pen-device candidate from
  that same survey were explicitly rejected by the user as not a fit for this project
  and removed from `noxna_devices.md` entirely — not implemented here or anywhere).
- **SDL3 API:** `SDL_messagebox.h` — synchronous, blocking
  `SDL_ShowMessageBox(const SDL_MessageBoxData*, int* buttonid)` (full control: custom
  buttons, icon flags) and `SDL_ShowSimpleMessageBox(flags, title, message, window)`
  (one-line convenience wrapper).
- **Platform support:** real backends confirmed in every video backend this project
  already vendors — `android`, `cocoa`, `haiku`, `riscos`, `uikit`, `vita`, `wayland`,
  `windows`, `x11`, and `emscripten` (`Emscripten_ShowMessagebox()` in
  `src/video/emscripten/SDL_emscriptenvideo.c`). No platform exclusion needed;
  `getIsSupportedProperty()` can be unconditionally `true`.
- **Required work:**
  - `include/CNA/Devices/Detail/IMessageBoxBackend.hpp` — pure virtual `ShowSimple`/
    `Show` (returns clicked button index), mirroring `IFileDialogBackend`'s shape.
  - `include/CNA/Devices/Detail/SdlMessageBoxBackend.hpp`/`src/.../SdlMessageBoxBackend.cpp`
    — real backend wrapping `SDL_ShowSimpleMessageBox`/`SDL_ShowMessageBox`.
  - `include/CNA/Devices/MessageBox.hpp`/`src/CNA/Devices/MessageBox.cpp` — static
    dispatcher over a process-wide swappable backend, same shape as `FileDialog`
    (function-local statics for the mutex/backend storage), **not**
    `SystemTray`'s constructor-injection shape — there is no persistent instance here,
    every call is one-shot, exactly like `FileDialog`.
  - `MessageBoxType` enum (`Error`/`Warning`/`Information`) mapping to
    `SDL_MESSAGEBOX_ERROR`/`_WARNING`/`_INFORMATION`.
  - `tests/CNA/Devices/MessageBoxTests.cpp` — fake backend from the very first test
    written (per this plan's own repeated lesson from `DEVICES-CNA-008`'s `zenity`
    incident and `DEVICES-CNA-009`'s use-after-free) — never call the real backend in
    an automated test, since a real `SDL_ShowMessageBox()` call blocks the calling
    thread on a real modal OS dialog with no way to close it headlessly.
- **Acceptance criteria:** builds under `CNA_DEVICES=ON`; new test suite passes in 0ms
  (no real dialog ever shown); full existing suite has zero regressions; clean under
  `devices-asan`/`devices-ubsan`.
- **Suggested files:** new files only.
- **Resolution:** Implemented exactly as sketched — `Detail::IMessageBoxBackend`/
  `Detail::SdlMessageBoxBackend`, `MessageBoxType` enum, and `MessageBox` as a
  process-wide swappable-backend static dispatcher mirroring `FileDialog`'s shape
  (not `SystemTray`'s constructor-injection shape — no persistent instance here).
  `getIsSupportedProperty()` is unconditionally `true`, matching the platform survey
  (real backend on every video backend this project vendors, no exclusions needed).
  4 tests in `MessageBoxTests.cpp`, fake backend used exclusively — no real dialog was
  ever shown during development or test runs.
  - **Unrelated, pre-existing build break found and fixed while building `CnaTests`:**
    `src/Microsoft/Xna/Framework/GamerServices/GamerProfile.cpp` failed to compile —
    `System::Globalization::RegionInfo::CurrentRegion()` no longer exists in the
    vendored `sharp-runtime` dependency (renamed to `getCurrentRegionProperty()` by
    an unrelated, concurrent upstream commit, matching this project's own
    `getXProperty()` SharpRuntime convention). Fixed with a one-line call-site update,
    committed separately from this task since it has nothing to do with `MessageBox`.
  - Build: `CNA` and `CnaTests` both build cleanly under `CNA_DEVICES=ON`. Full suite:
    3368 tests, 3366 passed, 2 pre-existing expected skips, **zero regressions**.
    `CNA::Devices` filter (all 9 suites, including the new `MessageBoxTests`): 40/40
    pass in 0ms. Re-ran the same filter under `devices-asan`
    (`ASAN_OPTIONS=detect_leaks=1`) and `devices-ubsan`: both exit code `0`, zero
    reports.

---

## Blocked / needs the user

*(Nothing yet. Any task that turns out to need a decision `noxna_devices.md` did not
already make will be listed here with the exact question, and skipped in favor of the
next independent task, per the user's explicit instruction.)*

---

## Progress log

*(Updated after each task closes — newest first.)*

- **2026-07-07 — DEVICES-CNA-011 CLOSED.** `CNA::Devices::MessageBox` implemented
  (`Detail::IMessageBoxBackend`/`SdlMessageBoxBackend`, `MessageBoxType`), 4 tests,
  fake backend used exclusively (the real backend blocks on a modal OS dialog, same
  incident class as `FileDialog`'s `zenity` issue — never triggered here).
  `getIsSupportedProperty()` unconditionally `true` — best cross-platform reach of any
  `CNA::Devices` capability so far. **Found and fixed an unrelated, pre-existing build
  break** while building `CnaTests`: `GamerProfile.cpp` called
  `RegionInfo::CurrentRegion()`, renamed to `getCurrentRegionProperty()` by a
  concurrent, unrelated upstream `sharp-runtime` commit — fixed with a one-line
  call-site update, committed separately from this task. Full suite 3368/3368 (3366
  pass + 2 expected skips), zero regressions; `CNA::Devices` filter 40/40 in 0ms;
  clean under both `devices-asan` and `devices-ubsan`. **This closes Phase 5 and
  every currently-open task in `plan_cna_devices.md`.**

- **2026-07-07 — User reviewed Round 2 (`noxna_devices.md` Section 8) and decided.**
  `MessageBox`: approved, opened as `DEVICES-CNA-011` (Phase 5) and now being
  implemented. `Monitors`/multi-display enumeration: rejected outright ("nesmysl pro
  CNA" — not a fit for this project) and removed from `noxna_devices.md` entirely, not
  just marked rejected. `Process` and the pen-device-type candidate: also rejected and
  removed. `noxna_devices.md` Section 8 now contains only `MessageBox` plus the
  already-out-of-scope items from the original sweep (HIDAPI, OpenXR, loadso, asyncio,
  storage, touch enumeration).
- **2026-07-07 — Round 2 analysis pass (no new tasks opened).** Per explicit user
  request ("analyze, not implement yet"), surveyed `third_party/SDL/include/SDL3/` a
  second time for `CNA::Devices` candidates beyond this plan's 11 (now-closed) tasks.
  Findings written to `noxna_devices.md` Section 8, not this plan — this was analysis
  only, no implementation, so no `DEVICES-CNA-0xx` tasks were created. Strongest new
  candidates found: `MessageBox` (best cross-platform reach of anything surveyed
  across both rounds, and simpler than `FileDialog` since `SDL_ShowMessageBox` is
  synchronous, not callback-based) and `Monitors`/multi-display enumeration (genuinely
  new territory beyond `DisplayInfo`'s existing single-window scope). If/when
  implementation of any Section 8 candidate is requested, it should get its own task
  IDs here following this plan's existing one-task-at-a-time pattern.
- **2026-07-07 — DEVICES-CNA-010 CLOSED. All 11 tasks in this plan are now closed.**
  `docs/cna-devices-camera-design.md` written (design only, no `Camera` implementation,
  matching this task's own explicit scope). Found the texture-upload bridge originally
  assumed to be the hardest part is already solved by existing
  `ITextureBackend::UpdatePixels()`/`Texture2D::SetDataRGBA()` infrastructure.
  Cross-linked from `noxna_devices.md`. **Summary of this whole plan's real findings,
  for whoever picks this up next:** two platform-support corrections to
  `noxna_devices.md` found only by reading `third_party/SDL/src/` directly instead of
  trusting the original analysis (`FileDialog` has a real Android backend; confirmed
  `SystemTray` genuinely does not); two real use-after-free-class bugs caught before
  or via `devices-asan` (a filter-lifetime bug in `FileDialog` fixed before ever
  running it; a test-file use-after-free in `SystemTrayTests.cpp` that the plain,
  non-sanitizer build did not catch at all); and one real incident (orphaned `zenity`
  processes from calling a real interactive dialog backend in an early test draft)
  that directly motivated giving both `FileDialog` and `SystemTray` a swappable
  backend from then on. Phases 1-3 (`PowerInfo`, `Locale`, `Clipboard`, `UrlLauncher`,
  `SystemInfo`, `DisplayInfo`, `FileDialog`, `SystemTray`) are implemented, tested (36
  new tests across 8 suites: 4+3+5+3+3+4+6+8), and verified clean under both
  `devices-asan` and
  `devices-ubsan` with `CNA_DEVICES=ON`, with `CNA_DEVICES=OFF` (default) confirmed
  unaffected. Phase 4 (`Camera`) is design-only, intentionally not implemented.
- **2026-07-07 — DEVICES-CNA-009 CLOSED. Phases 1-3 complete.** `CNA::Devices::SystemTray`
  implemented with a swappable `Detail::ITrayBackend` designed in from the start
  (learned from `DEVICES-CNA-008`'s incident). Confirmed genuinely desktop-only this
  time (no Android tray backend exists, unlike `FileDialog`). **Caught a second real
  bug via `devices-asan`:** a use-after-free in the test file itself (reading a freed
  fake backend's field after its owning `SystemTray` was destroyed) that the plain
  non-sanitizer build did not catch at all — fixed with an externally-owned
  `shared_ptr<bool>` flag. 8 tests, ASan clean after the fix. Full suite 391/391 (389
  pass + 2 expected skips). Next: `DEVICES-CNA-010` (`Camera` design note, Phase 4 —
  no implementation).
- **2026-07-07 — DEVICES-CNA-008 CLOSED.** `CNA::Devices::FileDialog` implemented, with
  a real platform-support correction found by reading `third_party/SDL/src/dialog/`
  directly (Android has a real backend — "desktop-only" was wrong; corrected in
  `noxna_devices.md` too). **Real incident during development:** an early test draft
  called the real backend directly and spawned four orphaned `zenity` processes on
  the actual desktop session — fixed by giving `FileDialog` a swappable
  `Detail::IFileDialogBackend`, mirroring `VibrateController`'s pattern, so tests never
  touch the real dialog subsystem. Also caught and fixed a real filter-lifetime
  memory-safety bug before ever running it (SDL3 requires filter data to outlive the
  async callback, not just the synchronous call). 6 tests, 0ms, zero stray processes,
  ASan clean. Full suite 383/383 (381 pass + 2 expected skips). Next:
  `DEVICES-CNA-009` (`SystemTray`).
- **2026-07-07 — DEVICES-CNA-007 CLOSED.** `CNA::Devices::DisplayInfo` implemented,
  scope narrowed after design research found `GameWindow` already exposes real-XNA
  orientation/bounds — only content-scale and safe-area were genuinely new. Added one
  `NOXNA`-tagged accessor to `GameWindow` (`GetNativeSdlWindowEXT()`) as the window
  hook. 4 tests, including one against a real (non-skipped) SDL window in this
  container. Full suite 377/377 (375 pass + 2 expected skips), ASan clean including
  the real-window path. Next: `DEVICES-CNA-008` (`FileDialog`, Phase 3).
- **2026-07-07 — DEVICES-CNA-006 CLOSED. Phase 1 fully verified and complete.** All 18
  tests across 5 `CNA::Devices` suites pass under `devices-asan` (0 reports) and
  `devices-ubsan` (0 reports); `CNA_DEVICES=OFF` default confirmed unaffected in a
  fresh build directory. Next: `DEVICES-CNA-007` (`DisplayInfo`, Phase 2 — needs a
  short design pass on window ownership first).
- **2026-07-07 — DEVICES-CNA-005 CLOSED.** `CNA::Devices::SystemInfo` implemented, 3
  tests. **All five Phase 1 capabilities now implemented.** Full suite 361/361 (359
  pass + 2 expected skips). Next: `DEVICES-CNA-006` (Phase 1 sanitizer verification
  pass).
- **2026-07-07 — DEVICES-CNA-004 CLOSED.** `CNA::Devices::UrlLauncher` implemented, 3
  tests (each honestly scoped to "does not crash/hang", no return-value assumption).
  Full suite 358/358 (356 pass + 2 expected skips). Next: `DEVICES-CNA-005`
  (`SystemInfo`).
- **2026-07-07 — DEVICES-CNA-003 CLOSED.** `CNA::Devices::Clipboard` implemented, 5
  tests — this headless container's own lack of an SDL video subsystem serves as the
  real "no clipboard available" verification, not just a theoretical claim. Full suite
  355/355 (353 pass + 2 expected skips). Next: `DEVICES-CNA-004` (`UrlLauncher`).
- **2026-07-07 — DEVICES-CNA-002 CLOSED.** `CNA::Devices::Locale` implemented
  (`LocaleInfo`/`Locale`), 3 tests, verified leak-free under `devices-asan` explicitly
  (manual `SDL_free()` boundary). Full suite 350/350 (348 pass + 2 expected skips).
  Next: `DEVICES-CNA-003` (`Clipboard`).
- **2026-07-07 — DEVICES-CNA-001 CLOSED.** `CNA::Devices::PowerInfo` implemented
  (`PowerState`/`PowerInfo`), 4 tests, full suite at 347/347 (345 pass + 2 expected
  skips). Next: `DEVICES-CNA-002` (`Locale`).
- **2026-07-07 — DEVICES-CNA-000 CLOSED.** `CNA_DEVICES` CMake option added and
  verified in both directions. Next: `DEVICES-CNA-001` (`PowerInfo`).
- Plan created (this file).
