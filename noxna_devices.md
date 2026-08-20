# NOXNA Devices — Analysis: Extending `Microsoft::Devices`/`Sensors` with a New `CNA::Devices` Layer

**Status:** Analysis only. No implementation in this document or in this pass — this is a
proposal to be reviewed before any code is written. Every class/method name below is a
suggestion, not a commitment.

**Scope of the question this document answers:** CNA currently implements
`Microsoft::Devices`/`Microsoft::Devices::Sensors` strictly as the real XNA 4.0/Windows
Phone 7 API surface (see `plans/plan_devices.md`, now closed). That surface is fixed by
definition — it cannot grow beyond what WP7 actually shipped. This document asks: what
*additional*, CNA-specific (non-XNA) device/sensor capabilities could reasonably be
added on top, using SDL3 as the only new dependency, so the result stays buildable on
every platform CNA already targets (Windows, Linux, macOS, Android, Web/Emscripten; iOS
has no toolchain in this environment but is architecturally in scope)?

---

## 1. Executive summary

CNA already has an established, working pattern for exactly this kind of extension:
`CNA::Graphics` (`include/CNA/Graphics/`, e.g. `PbrMaterial`, `RenderPipelineSettings`),
gated behind a `CNA_NOXNA` CMake option that becomes a `CNA_NOXNA` compile definition.
This document proposes a sibling namespace, **`CNA::Devices`**, following the same
architectural conventions, to house new device/sensor capabilities SDL3 exposes that
have no XNA/WP7 equivalent at all — battery status, clipboard, camera, locale, URL
launching, native file dialogs, system tray, display/orientation info, and basic
system/CPU info.

Every capability surveyed below is backed by a real, already-vendored SDL3 header
(`third_party/SDL/include/SDL3/`) with real per-platform backend source
(`third_party/SDL/src/<subsystem>/<platform>/`) already present in this repository — so
this is not speculative; the underlying SDL3 support already exists and already builds
as part of this project's normal SDL3 vendoring. What's missing is the CNA-facing C++
wrapper layer, its tests, and its documentation.

**Recommended phase 1 (smallest, safest, most broadly useful):** `PowerInfo` (battery),
`Locale`, `Clipboard`, `UrlLauncher`, `SystemInfo` (CPU/RAM). All five are stateless or
near-stateless, have simple SDL3 APIs (single function calls, no async callback
plumbing), and have real backend support on all five target platforms.

**Recommended later phases:** `DisplayInfo` (orientation/safe-area/content-scale —
needs careful scoping against the existing `Microsoft::Xna::Framework::GraphicsDevice`/
`GameWindow` surface to avoid duplication); `FileDialog` (real backends on Desktop
**and Android** — corrected during implementation, see Section 4.7 below; not
desktop-only) and `SystemTray` (genuinely desktop-only — no Android/iOS/Web tray
concept exists in SDL3), both needing an honest `IsSupported`-style story from day one,
mirroring `Microsoft::Devices::Sensors`' `SensorState`; and finally `Camera` (the most
complex — asynchronous frame delivery, per-platform permission prompts, and a
texture-upload path into
`Microsoft::Xna::Framework::Graphics::Texture2D`).

**Round 2 (Section 8, added 2026-07-07, after Phases 1-4 landed):** a second SDL3
capability sweep found `MessageBox` — the single best cross-platform coverage of
anything surveyed in either round — as the standout new candidate; it is now approved
and being implemented as `DEVICES-CNA-011` (`plans/plan_cna_devices.md`). A handful of other
candidates surfaced in that same sweep (`Monitors`/multi-display enumeration, `Process`,
pen-device queries) were considered and rejected by the user as not a fit for this
project; see Section 8 for the surviving analysis and `plans/plan_cna_devices.md`'s progress
log for the rejection record.

---

## 2. What already exists in CNA (grounding this proposal in real precedent)

### 2.1 The `CNA::Graphics` precedent — the template to copy

`include/CNA/Graphics/{PbrMaterial,RenderPipelineSettings,RenderQuality,ShadowQuality,TonemappingMode}.hpp`
is the only existing example of a public, non-XNA, CNA-namespace extension living
alongside the XNA-faithful `Microsoft::Xna::Framework` tree. Its shape:

- Public headers under `include/CNA/Graphics/`, implementation under `src/CNA/Graphics/`
  — same `include`/`src` split as everything else in this project.
- Every header is wrapped in `#ifdef CNA_NOXNA ... #endif` (see
  `include/CNA/Graphics/PbrMaterial.hpp:4` and `:123`). When `CNA_NOXNA` is not defined,
  the header contributes nothing at all — not even a forward declaration.
- `CNA_NOXNA` is a CMake option (`CMakeLists.txt:20`, `option(CNA_NOXNA "Enable CNA
  NOXNA extended graphics layer (beyond XNA 4.0)" OFF)`), OFF by default, propagated as
  a `PUBLIC` compile definition on the `CNA` target
  (`$<$<BOOL:${CNA_NOXNA}>:CNA_NOXNA>`, `CMakeLists.txt:239`) — so any consumer of the
  `CNA` library sees the same macro state the library itself was built with.
  Source `.cpp` files are **not** conditionally compiled at the CMake level (they're
  still picked up by the library's normal glob) — only their *content* is conditional,
  via the same `#ifdef` in the `.cpp`.
- A single demo/smoke-test executable, `cna_example_noxna_settings`
  (`examples/noxna_settings_example.cpp`), gated behind
  `if(CNA_BUILD_EXAMPLES AND CNA_NOXNA)` (`CMakeLists.txt:548`), with a matching
  `add_test(NAME NOXNA_Settings_Compile_Run ...)`. This is a *compile-and-run* smoke
  test, not a full Google Test suite — `CNA::Graphics` currently has no `tests/CNA/`
  directory at all.
- **Gap in the current precedent, worth fixing in this new effort, not copying:**
  `PbrMaterial`/`RenderPipelineSettings` are settings-bag classes with no actual runtime
  behavior yet (per `PbrMaterial.hpp`'s own doc comment, "Actual PBR rendering requires
  a matching `PbrEffect` ... Task N52" — not yet built) and no proper Google Test
  coverage, only the one smoke-test executable. `CNA::Devices` should do better: real
  Google Test suites under `tests/CNA/Devices/`, matching the rigor
  `Microsoft::Devices::Sensors` already has (every public member tested, `SetXForTesting()`-style
  fake-injection seams where real hardware can't be exercised in CI).

### 2.2 The `Microsoft::Devices::Sensors` precedent — the template for hardware-backed classes

The just-completed `plans/plan_devices.md` work established a second, equally relevant
precedent for anything that talks to real hardware through SDL3:

- **`Detail::I<X>Backend` interface + `SetBackendForTesting()`** (see
  `Detail::IVibrateBackend`/`Detail::ICompassBackend`/`Detail::IMotionBackend`,
  `Detail::SdlHapticVibrateBackend`, `VibrateController::SetBackendForTesting()`,
  `Compass::SetBackendForTesting()`). This is how the existing code makes SDL3-backed,
  hardware-dependent classes unit-testable in a headless CI container with no real
  sensor/haptic/camera present: production code talks to a small interface, tests
  inject a fake implementation. **Any new `CNA::Devices` class whose real backend
  cannot be exercised in this container (`Camera` above all) should follow this pattern
  from day one**, not bolt it on later.
- **`SensorState`-style honesty about support** (`getIsSupportedProperty()`,
  `getStateProperty()`): every existing sensor class can be asked, cheaply and without
  side effects, whether it's actually usable on the current platform/device, and every
  consumer is expected to check before assuming data will arrive. `CNA::Devices` classes
  that are meaningfully unavailable on some platforms (see Section 4's per-capability
  matrices) must expose the same kind of honest, queryable "not supported here" signal
  — never a silent, unexplained no-op with no way to detect it in advance.
- **`CNA::Platform`/`CNA::getCurrentPlatform()`** (`include/CNA/Platform.hpp`) already
  gives every class a compile-time `Platform::{Desktop,Android,iOS,Web}` value — useful
  for `CNA::Devices` classes that need materially different behavior per platform
  (e.g. `SystemTray` simply doesn't exist as a concept on `Android`/`iOS`/`Web`).
- **`NOXNA` macro is now compile-time enforced** (`include/CNA/CNAHelper.hpp`, closed by
  `plans/plan_devices.md` Task `VERIFY-003`/`DEV-API-002` in this same session): when
  `CNA_STRICT_XNA_API` is defined, `NOXNA` expands to `[[deprecated]]`, and a dedicated
  `cna_strict_xna_api_check` CMake target fails to build if it references any
  `NOXNA`-tagged member. **Open design question, addressed in Section 3.1:** should
  `CNA::Devices` classes use the `NOXNA` marker macro at all, given they live in a
  wholly separate namespace (`CNA::Devices`, not `Microsoft::Devices`) and are gated by
  a *different* mechanism (`#ifdef CNA_NOXNA`/`CNA_DEVICES`, a namespace-level opt-in)
  rather than a *per-member* tag inside an otherwise-XNA-faithful class? See Section 3.1
  for the recommendation.

### 2.3 SDL3 is already fully vendored and already used for hardware access

`third_party/SDL` is a full, buildable SDL3 checkout, already used for
`Microsoft::Devices::Sensors`' `Accelerometer`/`Gyroscope` (`SDL_sensor.h`) and
`VibrateController` (`SDL_haptic.h`). Every capability surveyed in Section 4 below is
backed by a header **already present** in `third_party/SDL/include/SDL3/`, with real,
already-vendored per-platform backend `.c`/`.m` source under
`third_party/SDL/src/<subsystem>/<platform>/` — this is not a new dependency, just
unused-so-far surface area of a dependency already fully present.

---

## 3. Proposed design for `CNA::Devices`

### 3.1 Namespace, gating, and file layout

- **Namespace:** `CNA::Devices` — mirrors `CNA::Graphics` exactly. Public headers under
  `include/CNA/Devices/`, implementation under `src/CNA/Devices/`, tests under
  `tests/CNA/Devices/`. Internal-only backend interfaces (the `Detail::I<X>Backend`
  pattern from Section 2.2) go under `include/CNA/Devices/Detail/`, never included from
  a public header, matching `include/Microsoft/Devices/Sensors/Detail/`'s existing rule.
- **Do not put any of this under `Microsoft::Devices` or `Microsoft::Devices::Sensors`.**
  Those namespaces are reserved for the real, frozen XNA 4.0/WP7 API surface — mixing in
  CNA-only classes there, even `NOXNA`-tagged, would blur exactly the line
  `plans/plan_devices.md`'s entire effort (and the brand-new `VERIFY-003` strict-mode check)
  exists to keep sharp. A fresh `CNA::Devices` namespace makes the "this is not XNA"
  fact structural, not just a per-member comment.
- **Gating mechanism — open question, two real options:**
  1. **Reuse `CNA_NOXNA`.** Its CMake description ("Enable CNA NOXNA extended graphics
     layer") is graphics-specific in wording, but the macro name and mechanism
     (`CNA_NOXNA` compile definition, `#ifdef CNA_NOXNA` in headers) are already
     generic. Reusing it means one flag turns on *all* CNA-only extensions at once —
     simplest for consumers, but conflates two independent feature areas (a build that
     wants `CNA::Devices` but not the PBR/render-pipeline settings can't opt out of one
     without the other), and its CMake help text would need updating to stop being
     graphics-specific.
  2. **New, sibling option, e.g. `CNA_DEVICES` or `CNA_DEVICES_EXT`.** Keeps the two
     extension areas independently toggleable, at the cost of a second flag a consumer
     has to know about. Given this project's existing convention of one option per
     backend/feature area (`CNA_BACKEND_SDL_RENDERER`, `CNA_BACKEND_EASY_GL`,
     `CNA_ENABLE_NET`, `CNA_NOXNA` itself), **this document recommends option 2** — add
     `CNA_DEVICES` as its own option, independent of `CNA_NOXNA`, documented explicitly
     as "Enable CNA-specific device/sensor extensions beyond XNA 4.0 (battery, camera,
     clipboard, ...)". This keeps `CNA_NOXNA`'s existing meaning intact (no behavior
     change for existing consumers) and lets a project use one extension area without
     the other.
- **Should `CNA::Devices` members still carry the `NOXNA` marker macro?** Recommendation:
  **no, or at least not as the primary signal.** The `NOXNA` macro's entire purpose (per
  `CNAHelper.hpp` and the project-wide convention in `CLAUDE.md`) is to flag a
  non-XNA *member sitting inside an otherwise-XNA-faithful class in the
  `Microsoft::Xna`/`Microsoft::Devices` namespace* — i.e. it marks a local exception to
  an otherwise-strict rule. A `CNA::Devices` class is not in that namespace at all and
  is not pretending to be XNA-faithful anywhere in its surface — the namespace itself
  already communicates "this is a CNA extension," making a per-member `NOXNA` tag
  redundant noise on every single declaration. The `#ifdef CNA_DEVICES` file-level gate
  is the real enforcement mechanism here, exactly as it already is for `CNA::Graphics`
  (which also does not use `NOXNA` on its own members). Doxygen comments should instead
  open with a one-line note like "CNA extension — no XNA/WP7 equivalent." if useful
  context, but this is a documentation nicety, not a compile-time-enforced tag.
- **`SharpRuntime` types still apply.** Every rule in `CLAUDE.md` about
  `bytecs`/`String`/`Single`/`System::TimeSpan`/`System::EventHandler<T>`/
  `System::IDisposable` etc. applies unchanged — `CNA::Devices` is still C++ code
  representing device state to a game, and should still feel like the rest of CNA, not
  like raw SDL3 C leaking through. The only thing that changes is *which* API contract
  it's being faithful to (none — it's free-form CNA design) versus
  `Microsoft::Devices::Sensors` (WP7's frozen contract).
- **Doxygen is still mandatory** on every public member, per `CLAUDE.md`'s project-wide
  rule — this does not relax just because the namespace is CNA's own.

### 3.2 Testability

Every class below should follow the `SetBackendForTesting()` pattern from Section 2.2
wherever its real backend cannot be meaningfully exercised in this headless container
(no battery, no camera, no clipboard-owning desktop session in CI, etc.) — i.e. nearly
all of them. Concretely: a small `Detail::I<X>Backend` interface per capability, a real
`Detail::Sdl<X>Backend` implementation calling straight into the SDL3 functions surveyed
below, and a fake implementation living only in the test file (mirroring
`FakeCompassBackend`/`FakeVibrateBackend` already in this codebase), injected via a
`SetBackendForTesting()` method. This lets `tests/CNA/Devices/*Tests.cpp` assert real
behavior (state transitions, error propagation, event firing) without needing real
hardware, exactly as `AccelerometerTests.cpp`/`CompassTests.cpp` do today.

### 3.3 Support/availability signaling

Every class must expose a cheap, side-effect-free way to ask "is this actually usable
right now, on this platform/device" — following `getIsSupportedProperty()`'s existing
naming convention. For capabilities that are *architecturally* unavailable on a whole
platform (e.g. `SystemTray` on Android/iOS/Web; `FileDialog` on Web without a
same-origin/user-gesture caveat), this should be knowable at compile time too (e.g. via
`CNA::getCurrentPlatform()`), not just discovered by a runtime probe that always
returns false there — a consumer should be able to `#ifdef`/branch around a
known-unsupported capability without even linking against it, the same way
`Microsoft::Devices::Sensors::Compass`'s real backend only exists
`#if defined(__ANDROID__)`.

### 3.4 Threading and lifetime

SDL3's own threading contracts vary per subsystem (some functions are main-thread-only,
some are documented thread-safe, some are silent on the question — exactly the kind of
gap `Detail::GetGlobalSdlSensorMutex()`'s own doc comment in
`include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp` had to work around for
`SDL_INIT_SENSOR`). Each capability's design pass must re-read its own SDL3 header's
`\threadsafety` annotations (or lack thereof) before writing any code, not assume
uniformity across subsystems. `Camera` in particular will need its own async
frame-delivery story (see Section 4.3) — SDL3 cameras deliver frames on a background
thread via `SDL_AcquireCameraFrame()` polling, not a push callback, which is a
different shape from the existing `Accelerometer`/`Compass` event-callback model and
needs its own design, not a copy-paste of `SensorBase<T>`.

---

## 4. Candidate capability areas

Each subsection: purpose, the real SDL3 API backing it (already vendored, already has
per-platform source in this repo), a rough platform-support matrix, a proposed
(illustrative, not final) class sketch, and a complexity/priority estimate.

### 4.1 `PowerInfo` — battery/power status

- **SDL3 API:** `SDL_power.h` — one function, `SDL_GetPowerInfo(int* seconds, int*
  percent)` returning an `SDL_PowerState` (`ERROR`/`UNKNOWN`/`ON_BATTERY`/`NO_BATTERY`/
  `CHARGING`/`CHARGED`). Real per-platform backends already vendored:
  `third_party/SDL/src/power/{windows,linux,android,emscripten,macos,uikit,haiku}/SDL_syspower.c(.m)`.
- **Platform support:** Windows, Linux, macOS, Android, iOS, **and Web/Emscripten** all
  have real backends already in this vendored tree — genuinely the best cross-platform
  story of any capability surveyed here. Desktops with no battery (most towers) report
  `SDL_POWERSTATE_NO_BATTERY`, which is itself useful, correct information, not a
  failure.
- **Proposed sketch:**
  ```cpp
  namespace CNA::Devices {
      enum class PowerState { Error, Unknown, OnBattery, NoBattery, Charging, Charged };
      class PowerInfo {
      public:
          static PowerState getStateProperty();
          static int getBatteryPercentProperty();   // -1 if unknown
          static int getSecondsRemainingProperty();  // -1 if unknown
      };
  }
  ```
  Likely fully static (mirrors SDL3's own single free-function shape) — no instance
  state, no lifecycle, no backend interface needed beyond a thin test seam if desired.
- **Complexity/priority: Low complexity, high value. Recommended Phase 1.**

### 4.2 `Locale` — user's preferred language/region

- **SDL3 API:** `SDL_locale.h` — `SDL_GetPreferredLocales(int* count)` returns an array
  of `SDL_Locale { const char* language; const char* country; }`, ordered by user
  preference (can return more than one).
- **Platform support:** generic implementation in SDL3 core reads OS-level locale
  settings; documented to work everywhere SDL3 runs, including Web (browser's own
  `navigator.languages`).
- **Proposed sketch:**
  ```cpp
  namespace CNA::Devices {
      struct LocaleInfo { System::String Language; System::String Country; }; // "en"/"US"
      class Locale {
      public:
          static std::vector<LocaleInfo> getPreferredLocalesProperty();
      };
  }
  ```
- **Complexity/priority: Low complexity, moderate value (useful for
  localization-aware games). Recommended Phase 1.**

### 4.3 `Clipboard` — text clipboard read/write

- **SDL3 API:** `SDL_clipboard.h` — `SDL_SetClipboardText`/`SDL_GetClipboardText`/
  `SDL_HasClipboardText`, plus a primary-selection variant (X11-specific concept, safe
  no-op elsewhere) and a newer MIME-type-based data API
  (`SDL_SetClipboardData`/`SDL_GetClipboardData`) for non-text payloads.
- **Platform support:** implemented generically as part of SDL3's video subsystem, works
  on every windowed platform. **Caveat for Web:** browser clipboard access is
  permission-gated and often requires a user-gesture context (a click), and may be
  read-only/write-only depending on browser security policy — this needs to be an
  explicit, tested, documented limitation (returning `false`/empty rather than silently
  hanging), not an assumed non-issue.
- **Proposed sketch:**
  ```cpp
  namespace CNA::Devices {
      class Clipboard {
      public:
          static bool setTextProperty(const System::String& text);
          static System::String getTextProperty();
          static bool getHasTextProperty();
      };
  }
  ```
- **Complexity/priority: Low complexity, moderate value. Recommended Phase 1**, with the
  Web permission caveat explicitly tested/documented before calling it done.

### 4.4 `UrlLauncher` — open a URL in the system browser

- **SDL3 API:** `SDL_misc.h` — a single function, `SDL_OpenURL(const char* url)`.
- **Platform support:** real implementations for every desktop OS, Android (via an
  `Intent`), iOS, and Web (window.open-equivalent) — SDL3's own doc comment for this
  function explicitly discusses per-platform behavior differences (e.g. some platforms
  may background the app).
- **Proposed sketch:**
  ```cpp
  namespace CNA::Devices {
      class UrlLauncher {
      public:
          static bool Open(const System::String& url);
      };
  }
  ```
- **Complexity/priority: Very low complexity, niche but real value (privacy policy
  links, "rate this game" store links, Discord/community links). Recommended Phase 1.**

### 4.5 `SystemInfo` — CPU/RAM/SIMD capability queries

- **SDL3 API:** `SDL_cpuinfo.h` — `SDL_GetNumLogicalCPUCores()`, `SDL_GetSystemRAM()`
  (MB), `SDL_GetCPUCacheLineSize()`, plus a family of `SDL_Has{MMX,SSE*,AVX*,NEON,...}()`
  boolean capability queries.
- **Platform support:** implemented generically in SDL3 core for every platform,
  including Web (reasonable approximations via Emscripten/browser APIs where a true
  answer isn't available — e.g. logical core count reflects
  `navigator.hardwareConcurrency`).
- **Proposed sketch:**
  ```cpp
  namespace CNA::Devices {
      class SystemInfo {
      public:
          static int getLogicalCpuCoreCountProperty();
          static int getSystemRamMegabytesProperty();
          // Deliberately NOT wrapping every SIMD flag individually — expose only what
          // a game would plausibly branch on (thread pool sizing, quality presets),
          // not a full CPUID dump.
      };
  }
  ```
- **Complexity/priority: Low complexity, moderate value (thread-pool sizing, "low-end
  device" quality presets). Recommended Phase 1**, deliberately minimal scope (do not
  wrap all ~15 SIMD-flag functions unless a real consumer need appears).

### 4.6 `DisplayInfo` — orientation, safe area, content scale

- **SDL3 API:** `SDL_video.h` — `SDL_GetCurrentDisplayOrientation`/
  `SDL_GetNaturalDisplayOrientation` (`SDL_DisplayOrientation` enum: landscape/portrait,
  each with a flipped variant), `SDL_GetDisplayContentScale` (DPI scale factor),
  `SDL_GetWindowSafeArea` (insets for notches/rounded corners/system bars on mobile).
- **Platform support:** orientation and content-scale are supported on every platform
  SDL3 targets, including Android (where orientation actually changes at runtime) and
  Web (via CSS/viewport signals). Safe-area insets matter most on Android/iOS
  (notches, gesture-navigation bars) and are a no-op-but-harmless concept elsewhere.
- **Scoping risk — must be resolved before implementation, not after:** CNA already has
  a real `Microsoft::Xna::Framework::GraphicsDevice`/`GameWindow` surface that owns the
  actual `SDL_Window`. A `CNA::Devices::DisplayInfo` class must be a thin, read-only
  query layer over the *same* window CNA already created — **not** a second window/
  display abstraction. This likely means `DisplayInfo`'s methods need to reach the
  existing window handle (however `GraphicsDevice`/`GameWindow` currently expose or hide
  it internally) rather than calling `SDL_GetDisplays()` independently and guessing
  which display/window the game means. This needs its own short design pass to find the
  right internal hook before any code is written — flagged explicitly as **not yet
  scoped enough to start**, unlike Section 4.1–4.5.
- **Complexity/priority: Medium complexity (mostly due to the window-ownership
  question above, not the SDL3 calls themselves). Recommended Phase 2, after the
  window-ownership question is answered.**

### 4.7 `FileDialog` — native open/save/folder-picker dialogs

- **SDL3 API:** `SDL_dialog.h` — `SDL_ShowOpenFileDialog`/`SDL_ShowSaveFileDialog`/
  `SDL_ShowOpenFolderDialog`, all **asynchronous**: they take a callback invoked later
  (possibly on a different thread, possibly after pumping the event loop, per SDL3's own
  docs) rather than blocking and returning a result directly.
- **Platform support — corrected during implementation (Task `DEVICES-CNA-008`,
  2026-07-07): NOT desktop-only.** This section's original claim was wrong, caught by
  reading `third_party/SDL/src/dialog/` and `third_party/SDL/CMakeLists.txt` directly
  rather than assuming. Real native backends exist for Windows, Linux (via XDG
  portal/zenity), macOS, **and Android** (`third_party/SDL/src/dialog/android/SDL_androiddialog.c`
  is a real, complete backend — `CMakeLists.txt`'s own per-platform dialog-source
  selection confirms it's compiled in for `ANDROID`). Only iOS and Web/Emscripten
  genuinely have no backend (confirmed by their absence from both the source tree and
  the CMake selection logic) — `getIsSupportedProperty()` should report `false` only
  for those two, not for Android too.
- **Proposed sketch (async, so shaped differently from 4.1–4.5):**
  ```cpp
  namespace CNA::Devices {
      class FileDialog {
      public:
          static bool getIsSupportedProperty(); // false on iOS/Web only, per the correction above
          static void ShowOpenFile(std::function<void(const std::vector<System::String>&)> onResult,
                                    /* filters, default location, allow-multiple */);
          static void ShowSaveFile(std::function<void(const System::String&)> onResult, /* ... */);
          static void ShowOpenFolder(std::function<void(const System::String&)> onResult, /* ... */);
      };
  }
  ```
- **Complexity/priority: Medium complexity (async callback plumbing; testability
  required a swappable backend, since the real one launches a genuine interactive OS
  dialog no automated test can safely trigger). Recommended Phase 3.**

### 4.8 `SystemTray` — desktop tray icon + menu

- **SDL3 API:** `SDL_tray.h` — a real, fairly complete API: `SDL_CreateTray`,
  `SDL_CreateTrayMenu`/`SDL_InsertTrayEntryAt` (with checkbox/submenu support),
  `SDL_SetTrayEntryCallback`, `SDL_DestroyTray`.
- **Platform support: desktop-only**, same story as `FileDialog` — no tray concept
  exists on Android/iOS/Web. Must be designed the same way: an honest, permanent,
  documented `getIsSupportedProperty() == false` on those three platforms.
- **Proposed sketch:** a stateful, owned object (unlike the mostly-static classes
  above) — `class SystemTray { public: SystemTray(/* icon, tooltip */); TrayMenu&
  getMenuProperty(); ~SystemTray(); }` with a nested menu/entry API mirroring SDL3's own
  shape. This is the closest of the "simple" candidates to needing genuine object
  lifetime management (create/destroy pairing, callback ownership) — closer in spirit to
  `VibrateController`'s singleton/lifetime discipline than to the static utility classes
  in 4.1–4.5.
- **Complexity/priority: Medium-high complexity (real object lifetime + menu tree +
  callback ownership), niche value (most games don't need a tray icon; useful for
  companion/launcher-style desktop apps). Recommended Phase 3, lower priority than
  `FileDialog`.**

### 4.9 `Camera` — camera device enumeration and frame capture

**A dedicated design note now exists: `docs/cna-devices-camera-design.md` (Task
`DEVICES-CNA-010`, 2026-07-07).** It found the texture-upload bridge this section
originally flagged as the hardest part is actually already solved by existing
infrastructure (`ITextureBackend::UpdatePixels()`/`Texture2D::SetDataRGBA()`), refines
the permission/state-machine design, and proposes a poll-based (not callback-based)
public API shape matching `SDL_AcquireCameraFrame()`'s own poll-based contract rather
than this codebase's push-callback sensor model. Read that document before starting
any `Camera` implementation work — the summary below is retained for context but the
design note supersedes it on every point where they'd otherwise duplicate.

- **SDL3 API:** `SDL_camera.h` — `SDL_GetCameras` (enumerate), `SDL_GetCameraSupportedFormats`,
  `SDL_OpenCamera`, `SDL_GetCameraPermissionState` (camera access is permission-gated on
  every platform that has a permission model), `SDL_AcquireCameraFrame`/
  `SDL_ReleaseCameraFrame` (poll-based frame delivery, returns an `SDL_Surface*`),
  `SDL_CloseCamera`.
- **Platform support:** real backends already vendored for `v4l2` (Linux),
  `mediafoundation` (Windows), `coremedia` (macOS/iOS), `android`, and **`emscripten`**
  (browser `getUserMedia`) — genuinely the full target-platform set, better coverage
  than initially expected for something this complex. `pipewire` (Linux, newer
  desktops) and a `dummy` backend also exist.
- **Why this is the hardest one, and should be last:**
  1. **Asynchronous by nature** — permission grants and frame delivery both happen
     later, not at the call site (`SDL_GetCameraPermissionState` can return "pending";
     frames arrive via polling `SDL_AcquireCameraFrame`, not a push callback), which is
     a genuinely different shape from every other class surveyed here and from
     `Microsoft::Devices::Sensors`' existing event-driven model.
  2. **Needs a texture-upload bridge** — a captured frame is an `SDL_Surface`
     (CPU-side pixel buffer); a game almost certainly wants it as a
     `Microsoft::Xna::Framework::Graphics::Texture2D` to actually draw it, which means
     this capability cannot be designed in isolation from the graphics backend
     (`EASYGL`/`VULKAN`/`BGFX`/`SDL_RENDERER`) the same way `Accelerometer`'s plain
     `Vector3` output could be.
  3. **Permission UX varies enormously per platform** (a browser tab prompt vs. an
     Android runtime permission dialog vs. a macOS system privacy prompt), and a
     `CNA_STRICT_XNA_API`-style "just call it and get a value back" shape does not fit
     — this needs its own state machine (`Requesting`/`Granted`/`Denied`/`NotSupported`),
     closer in spirit to `Compass`'s `Calibrate` event than to `PowerInfo`'s one-shot
     query.
  4. **`Emscripten`/Web camera access additionally requires a secure context (HTTPS)
     and an explicit user gesture** in every modern browser — this is a hard browser
     platform constraint, not an SDL3 limitation, and must be documented as such rather
     than treated as a bug if a Web build's camera silently fails outside those
     conditions.
- **Complexity/priority: High complexity, high value if a project actually needs it
  (AR-style effects, video chat, QR/barcode scanning), but should not block or delay
  Phase 1. Recommended Phase 4 — its own dedicated design pass, not a quick addition.**

### 4.10 Explicitly considered and recommended against (for now)

- **Geolocation/GPS.** `plans/plan_devices.md`'s own `docs/location-future-plans/plan.md` already
  gives a considered "not in `Microsoft::Devices::Sensors`, ever" answer for the
  strict-XNA layer, specifically because SDL3 itself has **no geolocation API at all**
  — this would require an entirely separate, per-platform native integration (Android
  `FusedLocationProviderClient`, Core Location on iOS/macOS, W3C Geolocation on Web),
  contradicting this document's own "SDL3-only" premise. **Recommendation: keep this
  out of `CNA::Devices` too**, for the same reason plus the added one that it's a
  materially different (and much larger) engineering effort than everything else here.
  If a real project need arises, it deserves its own standalone analysis, not a bullet
  in this one.
- **Storage/filesystem paths** (`SDL_filesystem.h`'s `SDL_GetBasePath`/`SDL_GetPrefPath`/
  `SDL_GetUserFolder`). **Recommendation: skip** — `Microsoft::Xna::Framework::Storage`
  (`StorageContainer`/`StorageDevice`) and `Microsoft::Xna::Framework::TitleContainer`
  already exist and already cover this need inside the real XNA surface; duplicating it
  under `CNA::Devices` would create two competing ways to ask "where do I save game
  data," which is worse than not having the CNA-extension version at all.
- **Microphone / audio recording** (`SDL_audio.h` recording devices).
  **Recommendation: skip** — `Microsoft::Xna::Framework::Audio::Microphone`/
  `MicrophoneState` already exist as real XNA API covering this exact need; same
  duplication concern as storage above.
- **Bluetooth, NFC, biometric (fingerprint/face) sensors, phone/SMS, contacts,
  push notifications.** None of these have any SDL3 API at all — every one would
  require a fully separate, per-platform native integration (JNI on Android, platform
  frameworks on iOS, no meaningful equivalent on Web), directly contradicting the
  "SDL3-only" premise this document was asked to explore. **Recommendation: out of
  scope for this proposal entirely** — a different (and likely much larger, per-feature)
  analysis would be needed if any of these become a real requirement.

---

## 5. Proposed phased roadmap (for future planning, not started)

| Phase | Capabilities | Rationale |
|---|---|---|
| **1** | `PowerInfo`, `Locale`, `Clipboard`, `UrlLauncher`, `SystemInfo` | Smallest SDL3 surface each (1-6 functions), synchronous, real backends on all 5 target platforms, no cross-cutting design questions to resolve first. |
| **2** | `DisplayInfo` | Needs one design decision first (how it reaches the existing window/`GraphicsDevice` rather than owning a second one) — otherwise simple. |
| **3** | `FileDialog`, `SystemTray` | `FileDialog`: real backends on Desktop and Android (corrected during implementation — not desktop-only). `SystemTray`: genuinely desktop-only. Both needed the async-callback shape and an honest `IsSupported` story designed once, then reused for both. `FileDialog` first (more broadly useful, and its swappable-backend testability pattern is reusable for `SystemTray` too). |
| **4** | `Camera` | Materially harder than everything else combined — async permission + frame delivery + graphics-backend texture upload. Deserves its own dedicated design document when it's actually prioritized, not a rushed addition to this one. |
| **Not recommended** | Geolocation, Storage/filesystem paths, Microphone, Bluetooth/NFC/biometric/etc. | See Section 4.10 for why each is out of scope. |

If/when this roadmap is approved and implementation begins, each capability should be
tracked the same way `plans/plan_devices.md` tracked `Microsoft::Devices::Sensors` work: one
task per capability (or per logical sub-piece for `Camera`), its own build+test
verification, its own commit — not one large, unreviewable patch.

---

## 6. Open questions requiring a decision before implementation starts

1. **`CNA_NOXNA` vs. a new `CNA_DEVICES` CMake option** (Section 3.1) — this document
   recommends a new, independent option; needs sign-off before any `CMakeLists.txt`
   changes.
2. **Whether `CNA::Devices` members should carry the `NOXNA` marker macro at all**
   (Section 3.1) — this document recommends no (namespace already signals it), but this
   is a stylistic project convention decision, not a technical one, and should be
   confirmed explicitly rather than assumed.
3. **`DisplayInfo`'s relationship to the existing `GraphicsDevice`/`GameWindow`
   window-ownership** (Section 4.6) — needs its own short design pass; not yet safe to
   start.
4. **Whether any of these should be exposed through the existing
   `CNA::Internal::Backends::*` graphics-backend-selection pattern** (i.e. a pluggable
   `IDevicesBackend` per platform, mirroring `IGraphicsBackend`) **or stay directly
   SDL3-based with only the narrower `Detail::I<X>Backend`-per-capability testing seam**
   from Section 3.2. This document recommends the latter (SDL3 already *is* the
   cross-platform abstraction for every capability here; a second backend-selection
   layer on top would be solving a problem that doesn't exist for this specific set of
   features) — but this should be confirmed, since it's a real architectural fork.
5. **Whether `CNA::Devices` needs its own `docs/` file** (e.g.
   `docs/cna-devices-design.md`) once implementation begins, mirroring
   `docs/devices-native-backend-design.md`'s role for the XNA-side `Compass`/`Motion`
   work — recommended yes, but not written yet since no implementation exists to
   document.

---

## 7. Summary of concrete evidence used in this analysis

Every platform-support claim above is backed by real files already in this repository,
not assumption:

- `third_party/SDL/include/SDL3/{SDL_power,SDL_locale,SDL_clipboard,SDL_misc,SDL_cpuinfo,SDL_video,SDL_dialog,SDL_tray,SDL_camera}.h`
  — the public API surface for each capability.
- `third_party/SDL/src/power/{windows,linux,android,emscripten,macos,uikit,haiku}/`,
  `third_party/SDL/src/camera/{v4l2,mediafoundation,coremedia,android,emscripten,pipewire,dummy}/`
  — real, already-vendored per-platform backend implementations proving the
  cross-platform claims in Section 4, not just header-level API existence.
- `third_party/SDL/CMakeLists.txt` — confirms `SDL_CAMERA`/`SDL_POWER`/`SDL_DIALOG`/
  `SDL_TRAY` default ON for every platform this project targets (the only `OFF`
  overrides found are for `DOS`/`NGAGE`, retro platforms entirely outside this
  project's scope).
- `include/CNA/Graphics/*.hpp`, `CMakeLists.txt` (`CNA_NOXNA` option and its propagation)
  — the existing precedent this proposal's design section (3.1) is modeled on.
- `include/Microsoft/Devices/{VibrateController.hpp,Sensors/*}`,
  `include/Microsoft/Devices/Sensors/Detail/*` — the existing precedent for
  backend-interface testability (3.2) and support-signaling (3.3).
- `include/CNA/Platform.hpp` — the existing compile-time platform-detection utility this
  proposal's classes should use for their own platform-support signaling.

---

## 8. Round 2 (2026-07-07): further candidates, surveyed after Phases 1-4 landed

Sections 1-7 above are the original analysis, written before any `CNA::Devices`
implementation existed. All nine of that analysis's Phase 1-3 capabilities are now
implemented (`plans/plan_cna_devices.md`, tasks `DEVICES-CNA-000` through `-009`, all
CLOSED), and `Camera` (Phase 4) has its own dedicated design note
(`docs/cna-devices-camera-design.md`). This section is a **second, independent sweep**
of `third_party/SDL/include/SDL3/` looking specifically for capabilities the first
pass did not already cover or explicitly reject — same evidence discipline as
Section 7 (every claim below is checked against the vendored SDL3 source and
`third_party/SDL/CMakeLists.txt` directly, not assumed from a header alone). **This is
analysis only — no code, headers, CMake changes, or tests were written for this
section.**

### 8.1 `MessageBox` — native OS message/alert dialogs

- **SDL3 API:** `SDL_messagebox.h` — `SDL_ShowMessageBox(const SDL_MessageBoxData*,
  int* buttonid)` (full control: custom buttons, icon flags, optional color scheme) and
  `SDL_ShowSimpleMessageBox(flags, title, message, window)` (one-line convenience
  wrapper for the common case).
- **Platform support:** confirmed by grepping every `video/<platform>/` backend
  directory for a messagebox entry point — real implementations exist for
  `android`, `cocoa` (macOS), `haiku`, `riscos`, `uikit` (iOS), `vita`, `wayland`,
  `windows`, `x11`, **and `emscripten`** (`Emscripten_ShowMessagebox()` in
  `src/video/emscripten/SDL_emscriptenvideo.c`, confirmed present by direct grep).
  **This is the single best cross-platform coverage of any capability surveyed across
  both rounds of this analysis** — strictly wider than `PowerInfo` (Section 4.1, no
  Haiku/RISC OS/Vita/Wayland-specific mention needed since it rides the general video
  backend) and on par with `Camera`'s already-noted breadth.
- **Key difference from `FileDialog` (Section 4.7): this call is synchronous, not
  callback-based.** SDL3's own doc comment on `SDL_ShowMessageBox` is explicit: "This
  function should be called on the thread that created the parent window... It will
  block execution of that thread until the user clicks a button or closes the
  messagebox." This removes the entire class of lifetime bug `FileDialog` actually hit
  in `DEVICES-CNA-008` (the heap-allocated `DialogContext`, the async trampoline) — a
  message box wrapper is a strict subset of that complexity, closer to a single
  request/response call.
- **Testability still needs the same lesson applied from day one, though**: a real
  `SDL_ShowMessageBox()` call pops a real, blocking, modal OS dialog — running it
  unmocked in an automated test suite would hang exactly like `FileDialog`'s original
  `zenity` incident (Section 4.7 / `DEVICES-CNA-008`'s resolution note), just via a
  different SDL subsystem. A `Detail::IMessageBoxBackend` (`Show`/`ShowSimple`, fake
  returns a pre-set button id) is required from the first line of implementation, not
  retrofitted.
- **Proposed sketch:**
  ```cpp
  namespace CNA::Devices {
      enum class MessageBoxType { Error, Warning, Information };
      class MessageBox {
      public:
          static bool getIsSupportedProperty();
          static void ShowSimple(MessageBoxType type, const std::string& title, const std::string& message);
          static int Show(MessageBoxType type, const std::string& title,
                           const std::string& message, const std::vector<std::string>& buttonLabels); // returns clicked index
          static void SetBackendForTesting(std::unique_ptr<Detail::IMessageBoxBackend>);
      };
  }
  ```
  Mirrors `FileDialog`'s process-wide swappable-backend shape (Section 4.7) rather than
  `SystemTray`'s constructor-injection shape (Section 4.8), since — like `FileDialog` —
  there is no persistent instance/lifecycle here, just one-shot calls.
- **Complexity/priority: Low complexity (simpler than `FileDialog` — no async
  callback lifetime to manage), high value (broadest platform reach of anything
  surveyed). Strong candidate for the next phase if implementation resumes.**

### 8.2 Explicitly considered and rejected (round 2) — same rationale style as Section 4.10

- **HID raw device access (`SDL_hidapi.h`)** — `SDL_hid_enumerate`/`_open`/`_read`/
  `_write`/`_get_feature_report` expose arbitrary USB/Bluetooth HID devices by
  vendor/product ID. **Recommendation: out of scope.** This isn't one capability to
  wrap, it's a whole low-level subsystem (raw report I/O against arbitrary,
  unspecified hardware) with real security sensitivity (an app reading/writing
  arbitrary HID reports) and no concrete consumer need identified — same "no clear
  use case yet, revisit if requested" rationale Section 4.10 already applied to
  Bluetooth/NFC.
- **OpenXR / VR-headset support (`SDL_openxr.h`, `src/gpu/xr/SDL_gpu_openxr.c`)** —
  real and present in the vendored tree, and CNA's own `SdlGraphicsBackend.cpp`
  already touches SDL's GPU API in one place (`SDL_GetGPURendererDevice`), so this
  isn't wholly disconnected from the codebase. **Recommendation: out of scope for
  `CNA::Devices` regardless.** OpenXR is an entire VR-runtime integration (external
  XR runtime/loader, headset hardware, its own render-loop/frame-timing model) that
  is a graphics-backend-level concern, not a thin device wrapper in the shape of
  anything else in this document — would need its own dedicated design effort from
  scratch if ever pursued, not a subsection here.
- **Dynamic library loading (`SDL_loadso.h`: `SDL_LoadObject`/`SDL_LoadFunction`)** —
  technically compiles broadly (`dlopen` backend covers `UNIX OR APPLE` per
  `third_party/SDL/CMakeLists.txt`, i.e. Linux/macOS/iOS/Android, plus a dedicated
  `windows` backend). **Recommendation: out of scope, narrow niche if ever needed.**
  Two real-world caveats undercut the broad compile-time coverage: loading and
  executing arbitrary native code at runtime is exactly what iOS App Store review
  prohibits in practice regardless of `dlopen` compiling there, and a typical
  Emscripten build of this project is fully static — runtime dynamic loading on Web
  needs `-sMAIN_MODULE`/`-sSIDE_MODULE` linker flags this project does not currently
  use. Legitimate use would be desktop-only (e.g. a game's own optional native plugin)
  — small enough value to not warrant a class unless a concrete need appears.
- **Async file I/O (`SDL_asyncio.h`)** — genuinely well-covered
  (`third_party/SDL/src/io/io_uring/` for Linux, a generic thread-pool fallback
  (`src/io/generic/`) covering every other platform including Web/mobile, a
  dedicated Windows `ioring` backend) and a real gap versus today's fully synchronous
  `TitleContainer`/`StorageDevice`. **Recommendation: out of scope for
  `CNA::Devices` specifically, not because it lacks merit** — it's a filesystem/
  threading utility, not a device/hardware capability, so it doesn't semantically
  belong in this namespace. If pursued, it belongs in a hypothetical separate
  `CNA::IO` namespace, which is a different proposal than this document.
- **`SDL_storage.h`'s abstract Title/User/File storage API** — investigated whether
  this reveals a gap in the existing XNA `Storage`/`TitleContainer` implementation.
  **Conclusion: no gap found, existing decision confirmed correct.**
  `src/Microsoft/Xna/Framework/TitleContainer.cpp` already uses `SDL_LoadFile()` with
  explicit Android-asset-path handling (a dedicated code path plus an `std::ifstream`
  fallback for the general case) — the practical cross-platform title-asset-loading
  problem `SDL_Storage`'s newer abstraction targets is already solved here by simpler
  means. This reconfirms Section 4.10's original "storage/filesystem paths: skip,
  `TitleContainer`/`StorageDevice` already exist as real XNA API" call, now with the
  Android-asset angle specifically double-checked rather than assumed.
- **Touch device enumeration (`SDL_GetTouchDevices`/`_GetTouchDeviceName`/
  `_GetTouchDeviceType`)** — **recommendation: skip, duplicate.** Real XNA
  `Microsoft::Xna::Framework::Input::Touch::TouchPanel`/`TouchCollection` already
  exist and cover touch input in depth; a `CNA::Devices` touch-device-enumeration
  class would duplicate that surface for no clear added value, the same "already
  covered by real XNA API" reasoning already applied to storage and microphone in
  Section 4.10.

### 8.3 Round 2 summary table

| Candidate | Platform reach | Complexity | Recommendation |
|---|---|---|---|
| `MessageBox` | Best of any capability surveyed (incl. Web) | Low (simpler than `FileDialog`, synchronous) | **Implemented (`DEVICES-CNA-011`, CLOSED 2026-07-07)** |
| HID raw device access | N/A | N/A | Out of scope — no concrete need, security-sensitive |
| OpenXR/VR | N/A | N/A | Out of scope — graphics-backend-level concern, not a device wrapper |
| Dynamic library loading | Desktop-only in practice | N/A | Out of scope — narrow niche, App Store/Emscripten caveats |
| Async file I/O | Universal | N/A | Out of scope for *this* namespace — belongs in a hypothetical `CNA::IO`, not `CNA::Devices` |
| `SDL_Storage` abstraction | — | — | No gap found — existing `TitleContainer`/`StorageDevice` already solve this |
| Touch device enumeration | — | — | Out of scope — duplicates real XNA `TouchPanel` |

---

## 9. Current status (2026-07-07)

Snapshot of where `CNA::Devices` stands right now, for anyone picking this document
up cold — see `plans/plan_cna_devices.md` for full task-level detail on every item below.

**Implemented and closed:** `PowerInfo`, `Locale`, `Clipboard`, `UrlLauncher`,
`SystemInfo` (Phase 1); `DisplayInfo` (Phase 2); `FileDialog`, `SystemTray` (Phase 3);
`MessageBox` (Phase 5, Section 8.1 above). All build under `CNA_DEVICES=ON`, have full
test coverage, and are clean under `devices-asan`/`devices-ubsan`.

**Designed but not implemented:** `Camera` (Phase 4) — `docs/cna-devices-camera-design.md`
has the full design note; implementing it is tracked as `DEVICES-CNA-012` in
`plans/plan_cna_devices.md`, not yet started.

**Not part of `CNA::Devices` at all, but worth knowing about if returning to this
area:** this same session also closed three related items in the separate
`Microsoft::Devices::Sensors` work (`plans/plan_devices.md`, real XNA API, not a
`CNA::Devices` extension) — `ACCEL-008` (Android landscape-remap decision, kept +
documented as `NOXNA` + opt-out), `COMPASS-009` (Android Compass tilt-mode axis
switch), and a cross-repo `sharp-runtime` data race (`SDL-SENSOR-004`). One follow-up
from that work remains open: `MOTION-012` (apply the same landscape remap to
`Motion`'s `Gravity`/`DeviceAcceleration`/`DeviceRotationRate`, or explicitly decide
not to) — tracked in `plans/plan_devices.md`, not here, since it's real XNA API surface,
not a `CNA::Devices` capability.
