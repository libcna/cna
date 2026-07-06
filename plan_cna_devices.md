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

### DEVICES-CNA-000 — CMake scaffold: `CNA_DEVICES` option + directory wiring

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

---

## Phase 1 — smallest, safest, synchronous, all 5 target platforms

### DEVICES-CNA-001 — `PowerInfo` (battery/power status)

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

### DEVICES-CNA-002 — `Locale`

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

### DEVICES-CNA-003 — `Clipboard`

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

### DEVICES-CNA-004 — `UrlLauncher`

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

### DEVICES-CNA-005 — `SystemInfo`

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

### DEVICES-CNA-006 — Phase 1 verification pass

- **Priority:** High
- **Required work:** build `CnaTests` with `-DCNA_DEVICES=ON`, run the full new
  `tests/CNA/Devices/*Tests.cpp` suite together; run under `devices-asan`/`devices-ubsan`
  at minimum (per this project's established sanitizer discipline); confirm
  `CNA_DEVICES=OFF` (default) still builds with zero new warnings/symbols.
- **Acceptance criteria:** documented exact pass/fail/skip counts for both the plain and
  sanitizer runs, mirroring `plan_devices.md`'s `VERIFY-001`/`VERIFY-002` precedent.

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

- Plan created (this file), Task DEVICES-CNA-000 next.
