# Viewport / DisplayMode / GraphicsAdapter Support Matrix

Phase 40 (`plans/plan_graphics.md` Tasks 341–350) audited `Viewport`, `DisplayMode`,
`DisplayModeCollection`, and `GraphicsAdapter` against FNA, verified the real window-resize and
render-target-switch chains end-to-end, and fixed several real bugs along the way. This document
summarizes those findings and closes the phase with a look at non-desktop (Android/Web) platform
limitations.

---

## 1. Viewport property-convention fix and math audit (Tasks 341–344)

`Project`/`Unproject` math, `AspectRatio`, `Bounds`, and `TitleSafeArea` already matched FNA
exactly — no math bug found. One real, fixed `CLAUDE.md` violation: `X`, `Y`, `MinDepth`,
`MaxDepth` were public raw fields while `Height`/`Width` in the same class already used the
project's `getXProperty()`/`setXProperty()` convention — converted all four to match. Added
non-identity-matrix `Project`/`Unproject` tests (the only prior tests used identity matrices,
which never exercise the perspective-divide branch) and confirmed `MinDepth`/`MaxDepth` have zero
validation/clamping in both FNA and CNA, by design (matches FNA's own unguarded arithmetic).

## 2. `DisplayModeCollection` `SurfaceFormat` indexer (Tasks 345/347)

Task 345's `GraphicsAdapter` audit found FNA has a real `this[SurfaceFormat format]` indexer on
`DisplayModeCollection` that CNA was missing entirely. Task 347 added it as a second
`operator[](SurfaceFormat)` overload (coexisting with the pre-existing `operator[](int)`), and
retroactively wrapped CNA's own `getCountProperty()`/integer indexer in `CNAEXT` (neither exists in
FNA's real API).

## 3. `GraphicsAdapter` audit (Tasks 345–346) — real bugs fixed

Four real, fixed findings from a line-by-line audit against FNA's `GraphicsAdapter.cs` and
`SDL3_FNAPlatform.cs`:

- `AdaptersChanged()` used the same string for both `DeviceName` and `Description` — FNA gives
  them different values (`DeviceName` = a synthetic Windows-style path, `Description` = the real
  display name). Fixed to match.
- Display-mode enumeration never deduplicated same-resolution/different-refresh-rate entries and
  iterated forward; FNA iterates in **reverse** and dedupes by `(width,height)`. Fixed.
- **The most severe finding**: `static GraphicsAdapter& DefaultAdapter` was a raw C++ reference
  bound once at static-init time. Since `AdaptersChanged()` clears and repopulates `adapters_` on
  every call (an intended, FNA-documented usage for display reconfiguration), any later call left
  `DefaultAdapter` — and its 2 production call sites — dangling. Fixed by removing the field
  entirely in favor of the already-correct `getDefaultAdapterProperty()`.
- Stale doc comments claimed `DeviceId`/`VendorId` were "not implemented" when they're actually
  backed by a real Linux `/sys/class/drm` PCI query. Corrected.

Task 346 verified the existing headless-CI fallback chain (`AdaptersChanged()` degrading to a
single synthetic 800×480 "Default Display" adapter when `SDL_GetDisplays()` fails) is structurally
complete, fixed one small `SDL_free` leak on that path, and added a genuine (non-mocked)
regression test for the "SDL video subsystem not initialized" case — see §5 below, this fallback
is directly relevant to non-desktop platforms.

## 4. Real window-resize and backbuffer-resize verification (Tasks 348–349) — one real bug fixed

Task 348 traced the actual OS-level resize chain (`SDL_EVENT_WINDOW_RESIZED` → `GameWindow` →
`GraphicsDeviceManager` → `GraphicsDevice::UpdateViewportFromWindow()`) end-to-end with a real
`SDL_SetWindowSize()` test, and confirmed a deliberate, documented CNA divergence from FNA:
`PresentationParameters.BackBufferWidth`/`Height` do **not** follow a real window resize (only
`Viewport` does, via `FixedHeightDynamicWidth` scaling) — this is intentional, not a bug.

Task 349 found and fixed a real bug while checking whether a `GraphicsDeviceManager::ApplyChanges()`
-driven backbuffer resize correctly resets a previously-set **custom sub-region** `Viewport` (e.g.
split-screen): `UpdateViewportFromWindow()` decided "did the size change?" by comparing against
`Viewport`'s own current width/height — so a custom `Viewport` was silently stomped back to
full-window size on the very next frame (`Present()` calls this every frame), even with no actual
resize. Fixed by tracking the last-known renderer-derived size in dedicated fields instead of
diffing against `Viewport` itself.

---

## 5. Non-desktop platform (Android/Web) limitations — anticipated, not verified

**Current reality: CNA has never actually run its display/adapter code on a non-desktop
platform.** This section is scoped strictly to what has and hasn't actually been checked, per this
project's verify-over-assume standard everywhere else in `plans/plan_graphics.md`.

### What actually exists today

- **Android**: the `CNA` static library **cross-compiles** for `arm64-v8a` via the Android NDK
  (confirmed compile-only, re-verified across multiple Devices-phase tasks — see
  `docs/devices-build.md` §4). No `GraphicsAdapter`/`Viewport`/`DisplayMode` code has ever
  **executed** on Android: no APK packaging integration exists in this project's build system, and
  the one attempted emulator run failed with a hard KVM-acceleration error before any CNA code
  could run (`docs/devices-build.md` §4.1, Task P9-4). The Android-specific `#ifdef __ANDROID__`
  branches that do exist and are confirmed compiled-in are all in `Microsoft::Devices::Sensors`
  (accelerometer/gyroscope) — nothing in `GraphicsAdapter.cpp`/`DisplayMode.cpp`/
  `DisplayModeCollection.cpp`/`Viewport.cpp` has any Android-specific code path at all; they call
  plain cross-platform SDL3 display APIs.
- **Web (Emscripten/WASM)**: `CMakeLists.txt` has real, working Emscripten-aware build logic
  (EasyGL/WebGL 2 is the default renderer on Emscripten, a prebuilt-SDL3 hint path exists,
  exception-handling flags are set), but this is all forward-looking scaffolding — no actual
  `emcc` build of `CNA` has ever been performed in this project's history (no
  `.sdl-prebuilt-emscripten` directory exists, and a full Emscripten/WebGPU target is tracked as
  wholly unstarted future work in Phase 69, Tasks 10155–10161, all `⬜`). Nothing has been verified
  running in a browser.

### FNA's own precedent: zero platform branching in this exact area

Auditing FNA's `GraphicsAdapter.cs`/`DisplayMode.cs`/`DisplayModeCollection.cs`/`Viewport.cs` and
`FNAPlatform/SDL3_FNAPlatform.cs`'s `GetGraphicsAdapters()`/`GetCurrentDisplayMode()`/
`GetMonitorHandle()` found **zero `#if`/platform-conditional code** anywhere in this area — FNA
relies entirely on plain `SDL_GetDisplays`/`SDL_GetFullscreenDisplayModes`/
`SDL_GetCurrentDisplayMode` calls with no Android/mobile-specific branching in the C# layer at all
(the handful of Android mentions found elsewhere in `SDL3_FNAPlatform.cs` are about window
creation quirks and save-path resolution, unrelated to adapter/display enumeration). CNA's
`GraphicsAdapter.cpp` mirrors this: it is a thin, unconditional wrapper over the same SDL3 calls,
with no platform guards of its own.

### Anticipated SDL3-level behavior differences (per SDL3's own public documentation, not locally verified)

These are documented characteristics of SDL3 itself on these platforms, not CNA-specific findings
— included here because they would directly shape what a game sees through `GraphicsAdapter`/
`DisplayModeCollection` if CNA were actually run on these platforms:

- **Android**: SDL3 typically reports exactly **one** display, with a display-mode list tied to
  the physical screen's native resolution and refresh rate — there is no real multi-monitor
  concept and no OS-level fullscreen mode-switching (Android apps run in a fixed, compositor-owned
  surface). `SDL_GetFullscreenDisplayModes()` would likely return only the single native mode (or a
  very short list), not the rich desktop-style mode list CNA's tests currently exercise on Linux.
- **Web/Emscripten**: the browser sandbox constrains SDL3's display APIs severely — there is no
  real display-mode enumeration or switching at all; "the display" is effectively the `<canvas>`
  element, sized by CSS/JS, not a hardware display mode list.

### What in CNA's existing logic would (or wouldn't) need to change

- `GraphicsAdapter::AdaptersChanged()`'s existing headless-CI fallback (Task 346: degrade to one
  synthetic 800×480 "Default Display" adapter when `SDL_GetDisplays()` returns nothing) is a
  reasonable **degradation** path for a total-failure case, but it is **not** the path Android
  would actually take — Android is expected to report a real, valid single display through
  `SDL_GetDisplays()`, so the normal per-display loop (not the empty-fallback branch) would run
  unmodified. No code change is anticipated to be *necessary* here, but this has never been
  confirmed against a real device.
  `queryDisplayModes()`'s reverse-iteration + `(width,height)` dedup (Task 345) would likely be a
  no-op on Android's short single-resolution mode list, but is harmless either way.
- No code in `DisplayMode`/`DisplayModeCollection` makes any assumption about *how many* modes or
  displays exist — both are already plain, size-agnostic containers, so a single-display,
  single-mode Android result would work through the existing API without modification.
- Nothing in this project has attempted to reason about Web/Emscripten's canvas-as-display model
  against `GraphicsAdapter`'s API surface at all; this is unstarted, tracked-elsewhere work (Phase
  69).

**This section documents anticipated behavior only. No non-desktop `GraphicsAdapter`/
`DisplayMode`/`Viewport` execution has been verified in this project, and no code changes are made
by this task** — a real Android device/emulator run (blocked today by the KVM failure documented
in `docs/devices-build.md` §4.1) or a real Emscripten build (unstarted, Phase 69) would be required
before any of the above could move from "anticipated" to "confirmed."

---

This closes Phase 40 (`plans/plan_graphics.md` Tasks 341–350) in full.
