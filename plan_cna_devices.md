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

### DEVICES-CNA-007 — `DisplayInfo`

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

---

## Phase 3 — desktop-only capabilities

### DEVICES-CNA-008 — `FileDialog`

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

### DEVICES-CNA-009 — `SystemTray`

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

---

## Phase 4 — `Camera` (its own design pass, not a quick addition)

### DEVICES-CNA-010 — `Camera` design note (no implementation yet)

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

---

## Blocked / needs the user

*(Nothing yet. Any task that turns out to need a decision `noxna_devices.md` did not
already make will be listed here with the exact question, and skipped in favor of the
next independent task, per the user's explicit instruction.)*

---

## Progress log

*(Updated after each task closes — newest first.)*

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
