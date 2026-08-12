# CNA Platform Abstraction (`IPlatform`) — Implementation Plan

> **Status: PLANNED — no task in this document is implemented yet.** Every row below is ⬜.
> This plan turns [`cnaplatform.md`](cnaplatform.md) into an executable task list.
>
> **Goal:** separate CNA from SDL3. Today SDL3 is called directly from the runtime, the input
> stack, audio, media, devices, storage, content and 40-odd renderers. After this plan, all of
> that goes through a CNA-owned platform contract (`CNA::Platform::IPlatform` and friends), and
> **SDL3 becomes the first implementation of that contract, not the substrate CNA is written
> against.**
>
> **Explicit scope boundary:** this plan implements **exactly one** platform implementation —
> `Sdl3Platform`. **SDL2, SDL 1.2, a native Win32 platform and any other implementation are NOT
> implemented here.** They appear only in [§12 Possible future implementations](#12-possible-future-implementations-not-in-scope)
> as the reason the seam is shaped the way it is. No task in Phases 0–11 may be closed by adding
> a second SDL implementation.
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented;
> ⛔ not implementable yet, with the blocking task and the reason named in the row — distinct
> from ⬜ because the work is understood and waiting, not merely undone;
> ❌ cut, with the evidence that killed it recorded in the row.

---

## 1. Why this plan exists

CNA's public API is XNA 4.0. XNA 4.0 says nothing about SDL. SDL3 is an implementation detail
that has leaked into essentially every subsystem, which costs the project three things:

1. **Portability.** Any target where SDL3 is impractical (older Windows, older Linux
   distributions, embedded or historical systems) is unreachable, no matter how portable the
   renderer for that target is. `DIRECTX1`…`DIRECTX8`, `GDI` and `GLIDE` exist precisely to reach
   old systems, and every one of them currently obtains its `HWND` from SDL3.
2. **Testability.** Subsystems cannot be exercised without SDL3 being initialized, which is why
   several test suites already carry hand-rolled `FakeSdl*Backend` doubles
   (`modules/input/tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp` and siblings). Those fakes
   are evidence that the seam is needed and is being invented ad hoc, per subsystem.
3. **Contract clarity.** `IGraphicsRenderer.hpp` already carries three `// TODO: SDL dependency
   should be abstracted later` markers on its *public internal contract* — `GetNativeTexture()`,
   `GetWindowInternal()`, `GetRendererInternal()` and the `SDL_Window* window` field of the
   creation parameters. This plan is the discharge of those TODOs.

The design rule from `cnaplatform.md` is binding on every task here:

> Do not start from an abstraction based on what SDL1, SDL2 and SDL3 have in common. Start from
> the contract CNA needs, and let each SDL implement that contract to the best of its ability.

---

## 2. Measured baseline (inventory)

This section is **generated** — regenerate with `python3 tools/platform/sdl_inventory.py --update`
and verify with `--check` (PLAT-1). It is the starting point every migration task drives down and
PLAT-120/PLAT-121 drive to the allowlist floor. Do not hand-edit the block below.

Counting rule: a textual occurrence of an SDL identifier or an `SDL3/`-rooted include path,
including forward declarations (`struct SDL_Window;`) and doc comments phrased in SDL's
vocabulary — both are coupling this plan removes. Two CNA-owned spellings that only look like
SDL are excluded, because they name CNA build options and renderer identities and survive the
plan by design: the `CNA_RENDERER_SDL_RENDERER` / `CNA_RENDERER_SDL_GPU` build macros, and the
bare tokens `SDL_GPU` / `SDL_RENDERER` (CNA's `CNA_GRAPHICS_RENDERER` identity names — SDL's real
API spells them `SDL_GPUDevice`, `SDL_CreateRenderer` and so on, which do count). Together the two
exclusions are worth 78 files that a naive `grep SDL_` misreports as coupling.

<!-- BEGIN GENERATED: tools/platform/sdl_inventory.py -->

| Metric | Value |
|---|---|
| Distinct `SDL_*` identifiers referenced anywhere under `modules/` | **1121** |
| Files referencing SDL (all) | **591** |
| Production files (`src/` + `include/`) referencing SDL | **268** |
| …of which are renderer production files | **116** |
| Test/example files referencing SDL | **323** |
| Distinct `SDL_PROP_WINDOW_*` native-handle properties read | **7** |
| Renderer families reaching for `SDL_GL_*` directly | **11** |

Production SDL surface per module (`src/` + `include/` only):

| Module | Files | Dominant concern |
|---|---:|---|
| `modules/input` | 48 | keyboard, mouse, gamepad, joystick, haptic, sensor, touch, text input |
| `modules/devices-ext` | 30 | clipboard, message box, file dialog, tray, camera, locale, power, display, URL |
| `modules/devices` | 17 | `Microsoft::Devices` sensors + vibrate, SDL subsystem refcounting |
| `modules/platform` | 17 | - |
| `modules/audio` | 11 | audio device/stream, mixer, microphone |
| `modules/graphics` | 11 | `GraphicsDevice`, `GraphicsAdapter`, `Texture2D`, `ImageLoader` |
| `modules/media` | 7 | `MediaPlayer`, `VideoPlayer`, library paths |
| `modules/content` | 3 | `SDL_IOStream`-based readers, glTF import |
| `modules/gamer-services` | 3 | `Guide` overlay, local store |
| `modules/runtime` | 3 | `Game` loop, `GameWindow`, `GraphicsDeviceManager` |
| `modules/core` | 1 | `Logger`, `Entrypoint` (`SDL_main`), `GraphicsRendererType` |
| `modules/graphics-ext` | 1 | ASCII post-process effect |
| `modules/renderers/*` | 116 | native window handle, GL context, Vulkan surface, SDL renderer/GPU (42 families) |

The native-window properties actually consumed today — these define the minimum
`NativeWindowHandle` surface, so the struct is derived from measured need, not guessed:

```text
SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER
SDL_PROP_WINDOW_COCOA_WINDOW_POINTER
SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER
SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER
SDL_PROP_WINDOW_WIN32_HWND_POINTER
SDL_PROP_WINDOW_X11_DISPLAY_POINTER
SDL_PROP_WINDOW_X11_WINDOW_NUMBER
```

Renderer families calling `SDL_GL_*` directly, freed as a group by one `IPlatformGlContext`:

```text
diligent easygl magnum metal opengl1 opengl2 opengl4 opengles1 openvg skia sokol
```

<!-- END GENERATED: tools/platform/sdl_inventory.py -->

---

## 3. Design decisions (recorded before implementation, not left implicit)

1. **New module `modules/platform` (`cna_platform`).** The contract and the SDL3 implementation
   are one physical module, following `docs/physical-modules.md`: contract headers under
   `modules/platform/include/CNA/Platform/`, the SDL3 implementation under
   `modules/platform/src/Sdl3/`. A separate module per implementation is deferred until a second
   implementation actually exists — inventing `modules/platform-sdl3` now would be a physical
   split with exactly one occupant.

2. **`CNA::Platform`, not `Microsoft::Xna::*`.** None of this is XNA 4.0 API. It never appears in
   a `Microsoft::Xna::Framework` header, and it therefore never needs `CNAEXT` — `CNAEXT` marks
   non-XNA surface *inside* the XNA namespace. Where a platform type must be reachable from an
   XNA type (e.g. `GameWindow`), the XNA-side accessor is `CNAEXT`-tagged as usual.

3. **Compile-time selection, runtime polymorphism.** `CNA_PLATFORM` is a CMake cache variable
   with values `SDL3` (default) and `HEADLESS`; `SDL2` / `SDL12` / `WIN32` are *reserved
   identifiers only* and configuring with them is a hard CMake error naming this plan. Only the
   selected implementation is compiled. `IPlatform` stays virtual anyway, because `HEADLESS` and
   the conformance suite need two implementations in one binary; the `using ActivePlatform = …`
   devirtualization sketched in `cnaplatform.md` is explicitly **not** adopted — it buys a
   sub-0.3 % win the performance contract already says is irrelevant, at the cost of making the
   conformance suite impossible.

4. **Coarse-grained by construction.** The performance contract from `cnaplatform.md` §"Recommended
   performance contract" is a *review rule*, enforced by PLAT-9's lint: no platform call may
   appear inside a per-pixel, per-vertex, per-fragment, per-sample or per-event loop. Events are
   polled as a batch (`PollEvents(std::vector<PlatformEvent>&)`), audio is filled a buffer at a
   time, input is a per-frame snapshot, capabilities are read once and cached.

5. **The graphics layer never calls through the platform to draw.** Renderers receive a
   `NativeWindowHandle` (plus size/DPI/lifecycle notifications) at initialization and then talk to
   their own API directly. `Game → GraphicsDevice → Platform → SDL → Renderer` is forbidden; the
   chain stays `Game → GraphicsDevice → Renderer`.

6. **Four renderers are permitted to keep a hard SDL3 dependency**, in two distinct kinds
   (established by PLAT-3's audit, which corrected this decision's original count of three):
   - *SDL by identity* — `SDL_RENDERER` and `SDL_GPU`. Their rendering path **is** an SDL3 API.
   - *SDL by upstream dependency* — `FNA3D` and `FREEDIRECT`. CNA's own sources for these are
     effectively SDL-free already; the third-party library each wraps links SDL3 itself, and
     free-direct creates and owns an internal `SDL_Renderer` that CNA never sees. No amount of
     migrating CNA code removes this, which is why it is a separate kind of exception rather
     than the same one.

   All four are declared `REQUIRES_PLATFORM SDL3` in CMake and excluded from the
   no-SDL-outside-platform gate by an explicit allowlist, not by accident. Every other renderer
   must end this plan SDL-free.

7. **Audio is a separate contract from platform.** `CNA::Audio::Platform::IAudioDevice` lives in
   `modules/audio`, is selected by its own `CNA_AUDIO_PLATFORM` variable, and does not inherit
   from `IPlatform`. This is what allows a future OpenAL/WASAPI/ALSA backend to exist without a
   matching window/event implementation, exactly as `cnaplatform.md` §"Separate audio out on its
   own" argues.

8. **Gamepad is part of the platform input contract, not its own module.** `cnaplatform.md`
   floats a separate gamepad module because SDL1 joystick, SDL2 GameController and SDL3 Gamepad
   differ so much. That difference is real, but it is a *capability* difference, and the
   capability model already expresses it. A separate module would be justified only once a
   second implementation demonstrates the seam is wrong — noted as a future revisit in §12.

9. **Migration is strangler-style, never big-bang.** Each subsystem moves behind the contract in
   its own task with its own build+test verification. `Sdl3Platform` is introduced *first* and
   proven behaviorally equivalent (Phase 3's golden tests) *before* any subsystem is rewritten to
   depend on it. At no point in this plan is the repository left unbuildable between tasks.

10. **The gate ratchets, it does not flip.** PLAT-8 lands a counter of SDL references outside the
    allowlisted locations, wired into the build as a *warning with a recorded maximum*. Each
    migration task lowers the recorded maximum. PLAT-121 flips it to a hard error once the count
    reaches the allowlist floor. This makes regressions visible from day one instead of at the end.

---

## 4. Target architecture

```text
modules/platform/
├── include/CNA/Platform/
│   ├── IPlatform.hpp               contract root: lifecycle, windows, events, timing, caps
│   ├── IPlatformWindow.hpp         one window
│   ├── PlatformCapabilities.hpp    what this implementation can actually do
│   ├── PlatformEvent.hpp           CNA event type, no SDL types
│   ├── NativeWindowHandle.hpp      handle handed to renderers
│   ├── WindowDescription.hpp       creation parameters
│   ├── PlatformFactory.hpp         one selection point, once at startup
│   ├── PlatformException.hpp       error contract
│   ├── IPlatformClipboard.hpp      ┐
│   ├── IPlatformDisplays.hpp       │ service interfaces, obtained from IPlatform,
│   ├── IPlatformDialogs.hpp        │ each independently capability-gated
│   ├── IPlatformFileSystem.hpp     │
│   ├── IPlatformPower.hpp          │
│   ├── IPlatformTray.hpp           │
│   ├── IPlatformCamera.hpp         ┘
│   ├── IPlatformSurfacePresenter.hpp  present a CPU pixel buffer to a window
│   ├── IPlatformGlContext.hpp      GL context service (11 renderer families need it)
│   ├── IPlatformVulkanSurface.hpp  Vulkan surface/extension service
│   └── Input/                      keyboard, mouse, gamepad, joystick, haptic, sensor, touch
├── src/Sdl3/                       the ONLY real implementation in this plan
├── src/Headless/                   no-op implementation; second impl for the conformance suite
└── tests/                          contract tests + conformance suite
```

Dependency direction after the plan (arrows = "may include"):

```text
runtime ─┐
graphics ─┤
input ────┼──▶ platform ──▶ SDL3   (SDL3 reachable from here and nowhere else)
devices ──┤
media ────┘

audio ────▶ audio-platform ──▶ SDL3 audio
renderers ▶ NativeWindowHandle (a struct, no platform call at draw time)
renderers/{sdl-renderer,sdl-gpu,fna3d} ──▶ SDL3   (allowlisted, by design)
```

---

## 5. Non-negotiable rules

1. **No SDL type in any contract header.** `NativeWindowHandle` carries `void*`, never
   `SDL_Window*`. A forward declaration `struct SDL_Window;` in a CNA header is exactly the
   pattern being removed — `GameWindow.hpp`, `GraphicsDeviceManager.hpp` and
   `IGraphicsRenderer.hpp` all currently do this.
2. **No behavior change is a goal of this plan.** Every migration task's acceptance criterion is
   *identical observable behavior*, proven by the existing test suite plus the golden tests from
   Phase 3. A behavioral fix discovered on the way gets its own commit and its own note.
3. **Unsupported means refused, not silently ignored.** A capability that an implementation lacks
   throws or returns a documented failure. Silent no-ops are forbidden — the same rule the SKIA
   and HEADLESS renderer plans already enforce.
4. **Capabilities are read once.** `GetCapabilities()` returns by value and callers cache it in
   the owning subsystem (`GraphicsDeviceManager`, `GameWindow`, `InputManager`). Calling it
   per-frame is a review rejection.
5. **The 46 renderer identities keep working throughout.** No task may reduce renderer coverage.
   Every renderer migration task names the exact renderer(s) it touches and rebuilds them.
6. **One task = one commit**, per `CLAUDE.md`. Task IDs (`PLAT-n`) go in commit messages.

---

## 6. Phases at a glance

| Phase | Range | Theme | Tasks |
|---|---|---|---:|
| 0 | PLAT-1 … PLAT-9 | Inventory, gates, baseline | 9 |
| 1 | PLAT-10 … PLAT-27, PLAT-127 | Platform contract (headers, no implementation) | 19 |
| 2 | PLAT-28 … PLAT-45, PLAT-128 | `Sdl3Platform` implementation | 18 (1 cut) + 1 |
| 3 | PLAT-46 … PLAT-56 | Runtime migration (`Game`, `GameWindow`, `GDM`) | 11 |
| 4 | PLAT-57 … PLAT-76 | Graphics + renderer decoupling | 20 |
| 5 | PLAT-77 … PLAT-90 | Input migration | 14 |
| 6 | PLAT-91 … PLAT-99 | Audio platform contract | 9 |
| 7 | PLAT-100 … PLAT-112 | System services (devices, storage, content, media) | 13 |
| 8 | PLAT-113 … PLAT-119 | `HeadlessPlatform` + conformance suite | 7 |
| 9 | PLAT-120 … PLAT-126 | Gates, performance, documentation, CI | 7 |
| 10 | PLAT-129 … PLAT-141 | `TerminalPlatform` (optional, after Phase 8) | 13 |
| — | §12 | Possible future implementations (**not in scope**) | 0 |

**Total: 141 task IDs — 140 active, 1 cut (PLAT-45).** IDs are never renumbered and are never
reused: a cut task keeps its ID and records why, and a task added after the plan was written gets
the next free ID and sits in its logical phase (PLAT-127/PLAT-128 from PLAT-3's audit;
PLAT-129–141 from the terminal-platform analysis).

---

## Phase 0 — Inventory, gates and baseline

Nothing is abstracted here. This phase makes the problem measurable so that every later task can
prove it made progress.

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-1 | Machine-readable SDL inventory tool | ✅ | `tools/platform/sdl_inventory.py`. Emits per-module/per-file/per-symbol counts as markdown, CSV or JSON; `--update` regenerates §2 in place, `--check` fails when it is stale. §2 is now generated. Two corrections to the hand-counted baseline it replaced: `.mm` sources were missed (`MetalRenderer.mm`), and CNA's own `CNA_RENDERER_SDL_*` build macros were being counted as SDL usage in four test files. |
| PLAT-2 | Classify all 1100 SDL identifiers into contract areas | ✅ | `tools/platform/sdl_classify.py` → `docs/platform-sdl-classification.csv`. Ordered rule table, first match wins; the script **exits non-zero if any identifier matches no rule**, so completeness is mechanical rather than claimed. 1100 identifiers over 16 non-empty areas, each naming its owning interface and migrating task. Areas split beyond this row's original wording: `lifecycle` (PLAT-29), `error` (PLAT-21), `logging` (PLAT-53) and `pixel-format` (PLAT-64/65) each have a distinct owner, and folding them into a neighbour would have hidden that. Two findings acted on: `SDL_GetWindowProperties` is native-handle extraction, not window management; and `dynamic-library` came out **empty**, which cut PLAT-45. |
| PLAT-3 | Identify the SDL-specific renderer allowlist | ✅ | `tools/platform/renderer_sdl_audit.py` → `docs/platform-renderer-sdl-audit.md`. All 46 identities over 42 module families, each given one verdict from measured usage. Verdicts read **code only** (comments and string literals stripped) — the inventory counts prose deliberately, but a verdict decided by prose would be wrong, and two families were initially misjudged on exactly that. Three findings, all acted on: the allowlist is **four**, not three (`FREEDIRECT` joins `FNA3D` as an upstream-dependency case, see design decision 6); **`SKIA` and `BLEND2D` present CPU-rasterised pixels through `SDL_Renderer`**, a capability the contract had nowhere to put — hence the new PLAT-127/PLAT-128; and **4 renderers (`STUB`, `HEADLESS`, `SOFTWARE`, `PORTABLEGL`) are coupled only by `IGraphicsRenderer`'s own SDL-typed methods**, so PLAT-59/PLAT-60 free them with no per-renderer work. |
| PLAT-4 | Audit the `SDL_INIT_*` subsystem lifecycle | ✅ | [`docs/platform-sdl-lifecycle-audit.md`](docs/platform-sdl-lifecycle-audit.md). Five subsystems, all lazily acquired; per-subsystem ownership, the process-wide `SdlSubsystemMutex`, and the `DevicesShutdownCoordinator` static-teardown hazard are all documented, with the four regression tests that pin them. **Headline finding: CNA production code never calls `SDL_Init()` or `SDL_Quit()`** — all 30 files that do are tests/examples acting as the application. So `IPlatform::Initialize()` must not become `SDL_Init()`; global SDL lifetime belongs to the host application, and the contract needs acquire/release-with-refcount plus an `SDL_WasInit`-equivalent query instead. Seven concrete requirements are recorded for PLAT-19/PLAT-29. |
| PLAT-5 | Audit `SDL_main` / entrypoint handling | ✅ | [`docs/platform-entrypoint-audit.md`](docs/platform-entrypoint-audit.md). **Conclusion: entrypoint cannot be an `IPlatform` method** — the Android `main`→`SDL_main` rename happens at preprocessing time in the TU defining `main()`, before any platform object exists. It stays a header + build-system concern, re-keyed from SDL/renderer macros onto `CNA_PLATFORM`. Three defects found in the existing seam: the non-Android branch is **dead code** (`CNA_RENDERER_SDL` is defined nowhere in the repo — only in that `#elif`), the header has **zero consumers**, and the two Android demos **bypass it** by including `<SDL3/SDL_main.h>` directly, which is precisely what its doc comment forbids. `SDL3::SDL3main` is linked by exactly one non-production target. |
| PLAT-6 | Baseline behavioral capture: event semantics | ✅ | `GameEventSemanticsGoldenTests.cpp` + checked-in transcript `modules/runtime/tests/golden/platform-event-semantics.txt`, 26 scenarios driven through the real `Game::RunOneFrame()`. What is recorded is **observable state** (`IsActive`, `RunApplication`, `ClientBounds`, cumulative wheel value), not which SDL constants appear in a switch — an assertion about `SDL_EVENT_WINDOW_FOCUS_LOST` does not survive the rewrite it exists to check. Four findings a plausible Phase 3 rewrite would get wrong: **(1)** the resize handler **ignores the event payload** and re-queries the window (a resize carrying 12345×6789 leaves `ClientBounds` alone) — reading `PlatformEvent`'s width/height would look more natural and would change behaviour; **(2)** the loop **does not filter by window id**, so `[focus-lost]` and `[focus-lost-from-a-different-window-id]` must stay identical; **(3)** minimize, restore, maximize, display-change and display-scale (DPI) change produce **no reaction at all**; **(4)** `Exit()` **does not stop the frame's event draining** — `[quit-then-wheel-in-one-frame]` still records a wheel delta, so a rewrite that breaks out of the loop on quit would drop input that currently lands. Renderer-independent by construction (synthetic window ids, no native window assumed). Verified against a deliberately-introduced regression: making `FOCUS_LOST` a no-op fails exactly the three affected lines. Regenerating is deliberate (`CNA_UPDATE_EVENT_GOLDEN=1`). |
| PLAT-7 | Baseline performance measurement | ⬜ | Frame-time distribution for a fixed scene on at least two renderers (one GPU, one CPU-raster), recorded with enough samples to give a meaningful confidence interval. PLAT-120 re-runs it. The target is "no measurable regression", so the baseline must include its own noise floor. |
| PLAT-8 | SDL-reference ratchet gate (warning mode) | ✅ | `tools/platform/sdl_ratchet.py` + `tools/platform/sdl_budget.json` + `cmake/PlatformRatchet.cmake`, included from the root `CMakeLists.txt`. **Budget: 253 files / 3641 references** outside the PLAT-3 allowlist. Ratchets **down only** — `--update` refuses to raise it without an explicit `--allow-increase`, so any increase lands in review as a deliberate diff. Verified end-to-end against a synthetic regression: warning mode warns and still configures, `-DCNA_PLATFORM_RATCHET_STRICT=ON` fails the configure (PLAT-121's flip), and a machine with no Python 3 skips the check cleanly rather than failing. `cna_platform_ratchet` target prints the per-module breakdown on demand. |
| PLAT-9 | Hot-path lint for platform calls | ⬜ | A check that rejects platform-interface calls syntactically inside per-pixel/per-vertex/per-sample/per-event loops, encoding design decision 4. Heuristic is acceptable; false positives suppressible with a documented annotation. |

---

## Phase 1 — The platform contract

Headers and types only. No SDL3 code, no migrations. Every header in this phase must compile
against a translation unit that has never seen an SDL header — that is the phase's exit test.

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-10 | Create `modules/platform` module | ✅ | `modules/platform/{CMakeLists.txt,include/CNA/Platform/,src/,tests/}`, added to `_cna_framework_modules` and composed after `math` so read order matches dependency direction. Links `cna_core_headers` (CNAEXT marker) + sharp-runtime `Core.Base`, and **no SDL**. Tests are picked up automatically by `cmake/UnitTests.cmake`'s `modules/*/tests/*.cpp` glob. |
| PLAT-11 | `CNA_PLATFORM` CMake cache variable | ✅ | `cmake/PlatformSelection.cmake`, included from the root `CMakeLists.txt`. `SDL3` (default) and `HEADLESS` available; `SDL2`/`SDL12`/`WIN32`/`EMSCRIPTEN`/`TERMINAL` reserved and rejected with a `FATAL_ERROR` naming this plan and saying why the hard failure is deliberate — falling back to SDL3 would build something other than what was asked for. Unknown values fail too, listing both sets. Defines `CNA_PLATFORM_<NAME>`, matching the `CNA_RENDERER_<NAME>` convention. All four paths verified against a harness project: default, `HEADLESS`, a reserved value, and a nonsense value. |
| PLAT-12 | `NativeWindowSystem` enum | ✅ | `Unknown, Win32, X11, Wayland, Cocoa, Android, Web, Headless` + `ToString()` for handle-mismatch diagnostics. `ToString()` uses an exhaustive `switch` with **no `default` arm**, so adding a system without naming it is a compiler diagnostic rather than a silent `"Unknown"`. 6 tests, all passing: per-value names, distinctness (two systems sharing a name would blur exactly the diagnostic that matters most), non-emptiness, reference stability across calls, member count, and `Unknown == 0` so a zero-initialised handle never reads as a real Win32 one. |
| PLAT-13 | `NativeWindowHandle` struct | ✅ | `{ system, display, window, surface, windowId }` with a per-system field table in the header. **Deviates from `cnaplatform.md`'s four-field sketch by adding `windowId`**: an X11 `Window` is a 32-bit XID, not an address, so carrying it in the `void* window` field is the classic interop bug — it happens to work on LP64 and truncates or traps elsewhere, and it makes null-checking meaningless because XID `0` (`None`) looks exactly like a null pointer. X11 therefore leaves `window` null and uses `windowId`. Trivially copyable (asserted in test), owns nothing. Plus `HasNativeWindow()` and a `Describe()` that deliberately never prints pointer values. |
| PLAT-14 | Typed native-handle accessors | ✅ | `TryGetWin32`/`TryGetX11`/`TryGetWayland`/`TryGetCocoa`/`TryGetAndroid`, each returning a validated per-system struct and each validating `system` **and** the fields that system requires. Output parameter is left untouched on failure, so a caller that ignores the return value cannot find a half-populated struct that looks usable. 18 tests, all passing, including an exhaustive cross-product asserting every accessor rejects every other system, and that an X11 handle with XID `0` is rejected even when its display is set — the case a pointer-style null check would have missed. |
| PLAT-15 | `WindowDescription` struct | ✅ | Title, client size, min/max size, position/centering, resizable, borderless, visible, `WindowFullscreenMode` (Windowed / **BorderlessFullscreen** / ExclusiveFullscreen kept distinct — collapsing them silently changes display-mode behaviour), high-DPI opt-in, and `WindowRenderIntent` (None/OpenGl/Vulkan/Metal) which must be right at creation because `SDL_WINDOW_OPENGL` cannot be applied afterwards. Defaults produce a visible, resizable, centred window so the common case sets only a title and size; size constraints default to 0 meaning *unconstrained*, not zero-sized. 6 tests. |
| PLAT-16 | `PlatformCapabilities` struct | ✅ | **26 capabilities**, grown from `cnaplatform.md`'s 10 by PLAT-2's classification, PLAT-3's audit (`surfacePresentation`, `nativeWindowHandle`) and the terminal analysis (`exactKeyboardState`, `pixelAccurateMouse`). Shipped as a **pair**: a trivially-copyable aggregate for callers to cache (a capability query must never be a per-frame virtual call), plus a `PlatformCapability` enum + `AllCapabilities()` making the set enumerable so a conformance test cannot hardcode a stale list. `Supports()` and `ToString()` both use exhaustive `switch`es with no `default` arm — **verified by experiment**, not asserted: adding an enum value without a struct field fails to compile in both. 8 tests, including a brute-force check that every capability maps to exactly one distinct struct field, which is where a copy-paste slip across 26 cases would otherwise hide. |
| PLAT-17 | `PlatformEvent` type | ✅ | `std::variant` of **13 alternatives** covering the 32 `SDL_EVENT_*` values `Game.cpp`/`SdlInputBridge.cpp`/`GameWindow.cpp` actually consume. A variant rather than a tagged struct because the payloads genuinely differ (two own a `std::string`) and `std::visit` gives exhaustive handling a type-tag `switch` does not — `GetEventTypeName()` visits every alternative, so adding one without naming it fails to compile. `WindowEventKind` keeps `Resized` and `PixelSizeChanged` **separate**: under high DPI they are different events, and conflating them is how a renderer draws at the wrong scale. `KeyEvent` carries `repeat` distinctly, because a platform with no real key-release must synthesise releases from repeat timing. `GetEventWindow()` returns 0 for process-scoped events rather than inventing a window. 11 tests. |
| PLAT-18 | `IPlatformWindow` interface | ✅ | Title, client bounds, **pixel size as a separate query** (they differ under high DPI, and a renderer sizes its swapchain from the pixel size — conflating them is how it draws at the wrong scale), display scale, show/hide, minimize/maximize/restore, resizable, borderless, fullscreen mode, `Sync()`, focus/minimized queries, display name (backs `GameWindow::ScreenDeviceName`), `GetNativeHandle()` and a stable `WindowId`. Every member mirrors something `GameWindow.cpp`/`GraphicsDeviceManager.cpp` does today. 7 tests against an in-memory fake — which is itself the point: the interface is **implementable with no windowing system at all**, the property a contract accidentally shaped around SDL would lack. The fake models `Sync()`'s real asynchrony, so the test proves the documented reason the method exists. |
| PLAT-19 | `IPlatform` root interface | ✅ | `AcquireSubsystem`/`ReleaseSubsystem`/`IsSubsystemInitialized` — **no `Initialize`/`Shutdown` pair exists**, per PLAT-4, and a test documents that absence as intentional rather than an oversight. Plus `CreateWindow`, batched `PollEvents`, timing, `GetCapabilities`, and accessors for all 12 services. Unsupported services return **null rather than a silent stub**: a stub that accepted calls and did nothing would let a caller believe it had working clipboard support. 14 tests driven by a **complete fake `IPlatform` implemented against nothing** — the one thing the per-interface tests cannot show is whether the interfaces *compose* into something only SDL could satisfy, and implementing the whole root against no windowing system is what answers that. Covers refcounted subsystems, the deliberately tolerated unpaired release, and buffer-capacity reuse across frames. |
| PLAT-20 | `PlatformFactory` | ✅ | `Create()` (build-time selection), `Create(name)` for the conformance suite running the same tests against every compiled implementation, `GetAvailable()` and `GetDefaultName()`. Keyed off `CNA_PLATFORM_<NAME>` from PLAT-11, so exactly one file knows which implementations exist. Until Phase 2 lands it **refuses by name** rather than returning null or a do-nothing stub — a stub would let a caller believe it had a working platform. 2 tests. |
| PLAT-21 | `PlatformException` and the error contract | ✅ | `PlatformException : std::runtime_error` (so callers that only know the standard type still catch it) + `PlatformNotSupportedException`, which names the missing `PlatformCapability` **machine-readably** via `GetCapability()` — a test asserting a capability-gated call refuses can check *which* capability was absent, not merely that something threw. Three-way outcome contract documented in the header: throw when an expected-to-succeed operation fails; return a status for ordinary "not present" answers a caller branches on; refuse via `NotSupported` when the implementation genuinely cannot. Underlying error text (SDL's `SDL_GetError()`, `errno`, …) is carried as a `std::string` `detail`, keeping diagnostic value without putting SDL types in the header. 7 tests. |
| PLAT-22 | `IPlatformGlContext` service | ✅ | Create/destroy/make-current/swap/get-proc-address/set-swap-interval, plus `GetContextAttributes()` for what the driver **actually granted** — a renderer that assumes it got exactly what it requested breaks on an unfamiliar driver. `GlContextDescription` fields were taken from the GL attributes the renderers measurably request (`CONTEXT_MAJOR/MINOR_VERSION`, `PROFILE_MASK`, `DEPTH_SIZE`, `STENCIL_SIZE`, `MULTISAMPLEBUFFERS/SAMPLES`, `DOUBLEBUFFER`), not from the full attribute list — a dropped attribute here is a silent rendering difference, not a compile error. MSAA off by default. 4 tests. |
| PLAT-23 | `IPlatformVulkanSurface` service | ✅ | `GetInstanceExtensions()` (takes no instance — it is called *before* the instance exists), `CreateSurface`, `DestroySurface`. Handles are deliberately **asymmetric**: `VkInstance` is dispatchable so it maps to `void*`, but `VkSurfaceKHR` is a non-dispatchable 64-bit handle that is *not* pointer-sized on 32-bit targets, so it maps to `std::uint64_t`. Declaring it `void*` would truncate it there — the same class of bug as carrying an X11 XID in a pointer. Both widths asserted in tests. No Vulkan header included. 3 tests. |
| PLAT-24 | Input service interfaces | ✅ | `IPlatformKeyboard`, `IPlatformMouse`, `IPlatformGamepad`, `IPlatformTextInput`, `IPlatformSensors` under `CNA/Platform/Input/`. **Snapshot-shaped by construction**: there is deliberately no `IsKeyDown(key)` on the interface, only `Update()` + `GetSnapshot()`, so `cnaplatform.md`'s input-snapshot rule is enforced by the contract's shape rather than by review. Touch/joystick/haptic ride on `PlatformEvent` + the gamepad and sensor services rather than getting separate interfaces with no distinct callers; PLAT-77's mapping pass revisits this against the existing `CNA/Internal/Input/` seam before Phase 5 migrates anything. Disconnected gamepad slots report `connected = false` rather than throwing — polling an empty slot is ordinary control flow. |
| PLAT-25 | System service interfaces | ✅ | `IPlatformClipboard`, `IPlatformDisplays`, `IPlatformDialogs`, `IPlatformFileSystem`, `IPlatformSystemInfo` in `IPlatformSystemServices.hpp`. Power, locale and URL-launching folded into `IPlatformSystemInfo` rather than becoming three interfaces with one method each; tray and camera are deferred to their PLAT-105/106 migrations, which is when a real caller shapes them. The **throw-vs-status split from PLAT-21 is applied consistently**: an empty clipboard, a cancelled dialog and a missing file all return a status because they are ordinary outcomes, while a capability the platform lacks refuses. `PowerInfo` defaults to `Unknown`/`-1`, not `0` — a zero percentage would make a battery-less machine look like one about to die. `IPlatformDynamicLibrary` absent per PLAT-45. 7 tests. |
| PLAT-26 | Doxygen pass over the whole contract | ✅ | **344 public declarations across 19 headers, all documented**, verified by `tools/platform/check_contract.py --doxygen` rather than by review. Forward declarations are exempt (documented where the type is defined, not once per header that mentions it). Verified by regression: adding an undocumented function makes the check fail, naming file and line. |
| PLAT-127 | `IPlatformSurfacePresenter` service | ✅ | `SurfaceFrame` (RGBA8 + size + optional stride, so a rasteriser can present a sub-rectangle of a larger buffer without copying), `PresentScaleMode` (Stretch/Letterbox/None), `PresentFilter`, `SetVSync()` returning whether it was honoured rather than throwing, and `GetTargetSize()` read per frame because it tracks the window. Header states plainly that this is **not a drawing API**: one finished frame per present, so the dispatch is once-per-frame by construction and cannot violate the performance contract. Capability-gated on `SurfacePresentation`. 2 tests, including that a default-constructed `SurfaceFrame` is *invalid* rather than empty, so validation rejects it instead of reading from `nullptr`. |
| PLAT-27 | SDL-free compilation test | ✅ | `ContractIsSdlFreeTests.cpp` includes all 19 contract headers and `#error`s on SDL sentinels — **and on Vulkan and GL sentinels too**, since `IPlatformVulkanSurface`/`IPlatformGlContext` describe their handles opaquely and must not drag a real API header into every consumer. The guarantee is compile-time, which is the right time: the coupling this plan removes is a compile-time one. `check_contract.py --headers` keeps the probe's include list complete, so a new header cannot quietly escape the check by not being listed — the way this kind of probe usually rots. Both verified by regression: an unlisted header fails the check, and a `#define SDL_h_` in a contract header fails the compile. |

---

## Phase 2 — `Sdl3Platform`

The first and, within this plan, only real implementation. It reproduces today's behavior exactly
— it is not an opportunity to redesign semantics.

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-28 | `Sdl3Platform` skeleton + SDL linkage | ✅ | `modules/platform/src/Sdl3/`, compiled only when `CNA_PLATFORM=SDL3`, with `SDL3::SDL3` linked **PRIVATE** so it never reaches the public include interface — proven by PLAT-27's probe still passing. Registered in `PlatformFactory`, which now advertises `SDL3` in `GetAvailable()` and names it when refusing an unknown platform. |
| PLAT-29 | Subsystem lifecycle and refcounting | 🟨 | Core implemented and verified against **real SDL3**: lazy acquisition, `SDL_WasInit`-backed queries, unpaired release as a no-op, and a **per-instance** count so a platform's destructor releases exactly what it acquired — never the host application's own hold, and never `SDL_Quit()`. 16 tests pass. Still open before this closes: the process-wide mutex, the pre-`SDL_Quit()` shutdown hook, and re-verification against `DevicesShutdownOrderingTests`/`SensorSubsystemOwnershipTests` once Phase 7 re-points those subsystems. |
| PLAT-30 | `Sdl3Window` creation and destruction | ✅ | `WindowDescription` → `SDL_CreateWindow` flags. Creation-time-only properties (render intent → `SDL_WINDOW_OPENGL`/`VULKAN`/`METAL`, high-DPI, borderless, hidden, fullscreen) are set as flags; the genuinely post-creation ones (explicit position, min/max size, borderless-fullscreen mode) are applied after. `Sdl3Window` owns the `SDL_Window` and destroys it — the only place in the module that stores one. |
| PLAT-31 | `Sdl3Window` geometry and state | ✅ | Title, client bounds, **`SDL_GetWindowSizeInPixels` for pixel size** (not the logical size — a renderer sizes its swapchain from this), display scale, resizable, bordered, fullscreen mode, show/hide/minimize/maximize/restore, `Sync`, focus and minimized queries, display name. `GetDisplayScale()` substitutes 1:1 when SDL returns `0.0f`, since a zero scale divides to infinity in any layout computation. |
| PLAT-32 | Native handle extraction for all 7 properties | ✅ | All 7 `SDL_PROP_WINDOW_*` properties → `NativeWindowHandle`, keyed off `SDL_GetCurrentVideoDriver()` rather than probing which properties answer — probing would pick the first responder, and XWayland answers both X11 and Wayland queries. **X11's XID is read with `SDL_GetNumberProperty` into `windowId`**, never the pointer field. `dummy`/`offscreen` report `Headless`, which is what makes a GPU renderer refuse deterministically. Tests assert *self-consistency* for whatever system the host actually reports — so the same test is meaningful on X11, Wayland, Win32 or none — plus agreement between `HasNativeWindow()` and the typed accessors, handle stability across calls, and that `Describe()` prints no addresses. Systems this host cannot reach are exercised by their branch of that switch, not claimed as verified. |
| PLAT-33 | Event pump: window events | ✅ | All 11 `WindowEventKind` values mapped, with `windowID`/`data1`/`data2` carried through. `Resized` and `PixelSizeChanged` verified **distinct** — conflating them is how a renderer draws at the wrong scale under high DPI. Cross-check against PLAT-6's golden capture stays open until PLAT-6 lands. Original scope: | Resize, pixel-size change, focus gained/lost, close, minimize/restore/maximize, move, display change, DPI change → `PlatformEvent`. Must match PLAT-6's golden capture. |
| PLAT-34 | Event pump: keyboard and text input | ✅ | Key down/up carrying scancode, keycode, modifiers, press state and **`repeat`** (losing that flag would silently break the synthetic key-up path a terminal needs); text input and IME composition with cursor/selection. A **null** `SDL_EVENT_TEXT_INPUT` text pointer is handled — dereferencing it would crash inside the event pump, the worst place for it. Original scope: | Key down/up with the existing keycode/scancode mapping preserved, plus text input and IME events. |
| PLAT-35 | Event pump: mouse and touch | ✅ | Motion with position and delta, buttons with click count, wheel, and all four touch kinds with normalised coordinates passed through unscaled. **`SDL_MOUSEWHEEL_FLIPPED` is normalised at the mapping**, so every consumer sees one convention instead of each rediscovering the flag. Original scope: | Motion, buttons, wheel, relative mode, touch and gesture events. |
| PLAT-36 | Event pump: gamepad, joystick, sensor | 🟨 | Gamepad/joystick/keyboard/mouse add+remove, gamepad axis and button mapped. Axis normalisation scales each half of SDL's asymmetric `[-32768, 32767]` range by its own magnitude — dividing the whole range by 32767 makes the extreme negative read `-1.00003`, which a clamp-free consumer carries into physics. **Sensor events not yet mapped** (they arrive with PLAT-85's sensor service); `InputDevicesHotplugTests` cross-check waits for Phase 5. Original scope: | Add/remove/axis/button/sensor events, including the hotplug paths already covered by `InputDevicesHotplugTests`. |
| PLAT-37 | Event pump: application lifecycle | ✅ | `WillEnterBackground`, `DidEnterForeground`, `LowMemory` and quit — the transitions `Game.cpp` currently handles inline. Original scope: | `SDL_EVENT_WILL_ENTER_BACKGROUND` / `SDL_EVENT_DID_ENTER_FOREGROUND` and quit — currently handled inline in `Game.cpp`. |
| PLAT-38 | Batched `PollEvents` with buffer reuse | ✅ | Fills the caller's vector via `clear()` (which keeps capacity, so a reused batch stops allocating after the first few frames) and one `MapSdlEvent` call per event — no per-event virtual dispatch. An unconsumed SDL event returns **false and leaves the destination untouched** rather than appending a default-constructed event; the variant's first alternative is `QuitEvent`, so the sloppy version would look like a real quit. Original scope: | Fills a caller-owned `std::vector`, reusing capacity across frames; no per-event allocation, no per-event virtual call. Design decision 4's concrete discharge. |
| PLAT-39 | Timing implementation | ✅ | `SDL_GetPerformanceCounter`/`Frequency`, `SDL_GetTicks`, `SDL_Delay`. 4 tests against real SDL3: non-zero frequency (the game loop divides by it), counter advance, monotonic ticks, and that `Delay` actually waits — with a deliberately generous bound, since the assertion under test is *that* it waited, not that a scheduler is precise. |
| PLAT-40 | `Sdl3PlatformCapabilities` | ✅ | All 26 capabilities answered explicitly; SDL3 supports every currently-defined one, asserted by enumerating `AllCapabilities()` rather than by spot checks, so a capability added later without being answered here shows up as a count mismatch. `exactKeyboardState` and `pixelAccurateMouse` are `true` — the two that exist precisely because a terminal cannot provide them. |
| PLAT-41 | GL context service (SDL3) | ✅ | Create/destroy/make-current/swap/proc-address/swap-interval over `SDL_GL_*`, plus `GetContextAttributes()` reading back what the driver **granted**. Attributes are set before creation, because setting them afterwards is silently ignored — the way a renderer ends up with a context it did not request. A window from another platform produces a named refusal rather than an unchecked cast. Context creation itself is untestable here (a dummy video driver has no GL), so the test asserts the failure is a clean `PlatformException`, not a crash or a silently-null context. Original scope: | Implements PLAT-22 over `SDL_GL_*`. Attribute setting must be verified against what the 11 GL renderer families currently request — a dropped attribute here is a silent rendering difference. |
| PLAT-42 | Vulkan surface service (SDL3) | ✅ | `GetInstanceExtensions`/`CreateSurface`/`DestroySurface`. Two real defects found by running it: `SDL_Vulkan_GetInstanceExtensions` **crashes** if called before `SDL_Vulkan_LoadLibrary`, now guarded and probed once; and `VkSurfaceKHR`'s C type is a **pointer on 64-bit but `uint64_t` on 32-bit**, so the conversion is width-correct via `if constexpr` rather than a single cast — vindicating the `std::uint64_t` contract type, since `void*` would truncate on 32-bit. `vulkanSurface` is now reported from a real loader probe, and `GetVulkanSurface()` returns null to match. Original scope: | Implements PLAT-23 over `SDL_Vulkan_GetInstanceExtensions`/`CreateSurface`/`DestroySurface`. |
| PLAT-43 | Displays service (SDL3) | ✅ | `SDL_GetDisplays`, bounds, desktop mode, content scale, per-window display lookup and fullscreen mode enumeration. Content scale substitutes 1:1 when SDL reports `0.0f` — a zero scale divides to infinity in any layout computation. Sibling services reach the `SDL_Window` through an implementation-internal `Sdl3Window::GetSdlWindow()`, which stays inside `CNA::Platform::Sdl3` so the public contract keeps no SDL type. An unknown display id yields no modes rather than throwing. Original scope: | `SDL_GetDisplays`, bounds, desktop/current display mode, content scale, window display scale. |
| PLAT-44 | Filesystem service (SDL3) | 🟨 | `SDL_GetBasePath`, `SDL_GetPrefPath` (verified to actually create a writable directory, since it backs `StorageDevice`), `SDL_LoadFile` as a **byte** loader with embedded NULs preserved, and idempotent recursive `CreateDirectory`. A missing file returns false with the output untouched, per PLAT-21's throw-vs-status split. Directory enumeration and `SDL_IOStream` wrapping remain, for the content/media readers in Phase 7. Original scope: | `SDL_GetBasePath`, `SDL_GetPrefPath`, `SDL_GetUserFolder`, directory enumeration/creation, `SDL_LoadFile`, `SDL_IOStream` wrapping. Consumers: `storage`, `content`, `media`, `TitleContainer`. |
| PLAT-128 | Surface presenter (SDL3) | ✅ | `SDL_CreateRenderer` + streaming `SDL_Texture` + `SDL_UpdateTexture`/`SDL_RenderTexture`/`SDL_RenderPresent`, with `SDL_SetRenderLogicalPresentation` for letterbox/stretch and `SDL_SetRenderVSync`. The texture is recreated only on a size change, so presenting is not a per-frame GPU allocation. **Genuinely exercised**, not merely compiled: SDL's software renderer works on a dummy-driver window, so the interface PLAT-3 found missing is tested end to end. Malformed frames (null pixels, non-positive extent) are rejected rather than read. Original scope: | Implements PLAT-127 over `SDL_CreateRenderer`/`SDL_CreateTexture`/`SDL_UpdateTexture`/`SDL_RenderTexture`/`SDL_RenderPresent` plus `SDL_SetRenderLogicalPresentation` and `SDL_SetRenderVSync` — the exact call set PLAT-3 measured in `SKIA` and `BLEND2D`. Note this makes `modules/platform` itself an `SDL_Renderer` user; that is correct, and is why the allowlist is about *renderers*, not about the symbol. |
| PLAT-45 | ~~Dynamic library service (SDL3)~~ | ❌ | **Cut on PLAT-2's evidence.** CNA never calls `SDL_LoadObject`/`LoadFunction`/`UnloadObject` — the only match in the entire tree is one `SDL_FunctionPointer` in a doc comment quoting `SDL_GL_GetProcAddress`'s signature (`GL4Loader.hpp`), and the two renderers that *do* load libraries at run time (`GDI`, `GLIDE`) call `dlopen`/`LoadLibrary` directly. `IPlatformDynamicLibrary` came from `cnaplatform.md`'s sketched class list, not from measured need; building it would have violated design decision "start from the contract CNA needs". The classifier keeps its rules so a future call site is classified rather than falling through. |

---

## Phase 3 — Runtime migration

The first real cut-over. Small blast radius, highest behavioral risk, so it is done early and
guarded by PLAT-6's golden capture.

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-46 | Own the platform instance in `Game` | ✅ | `Game` holds `std::unique_ptr<IPlatform> platform_` **declared before every other member**, so it is constructed first and destroyed last — the graphics device, window and content manager may all reach the platform during their own construction or teardown, and member order is the only thing that makes the containment real. Created *and installed* from a single member initialiser (`CreateAndInstallPlatform()`), not from the constructor body, so a member constructed later finds the game's instance rather than lazily creating a second. Exposed as `CNAEXT GetPlatformEXT()`; `IPlatform` is **forward-declared** in `Game.hpp` rather than included, keeping the platform contract out of every game's include graph. **The obvious implementation is wrong and the tests say why**: a per-game "the platform I displaced" pointer dangles when games are destroyed out of construction order, so install order is kept in one place and a game removes *itself* from the middle. `OutOfOrderDestructionLeavesNoDanglingInstallation` is the regression that found it. 10 tests. |
| PLAT-47 | Migrate `Game`'s event loop | ⛔ **blocked on PLAT-78** | `Game::PollEvents` hands **every** raw `SDL_Event` to `SdlInputBridge::ProcessEvent` before its own switch sees it, so the loop cannot stop polling SDL until the bridge consumes `PlatformEvent`. Polling both would drain the queue twice and lose half the events to whichever loop ran first — a data-loss bug, not a stylistic one. The phase ordering in this plan puts PLAT-78 in Phase 5, which is simply wrong for this dependency; PLAT-78 is pulled forward. PLAT-6's transcript is the acceptance oracle when it lands. Original scope: | `SDL_PollEvent` → `platform->PollEvents(batch)`; the `SDL_EVENT_*` switch becomes a `PlatformEvent` switch. Behavior must match PLAT-6 exactly, including the currently-inline quit, focus, resize and gamepad-added handling. |
| PLAT-48 | Migrate `Game`'s timing | ✅ | Every clock read in the loop goes through the platform: `GetPerformanceCounter()` seeds and advances `AdvanceElapsedTime()`, `GetPerformanceFrequency()` is its divisor, `Delay()` replaces the sleep-to-target spin, and the Emscripten main-loop callback drives its accumulator from `GetTicksMilliseconds()`. The platform's tick epoch is its own creation rather than library init, which changes nothing: only deltas are used and the first callback seeds its own baseline either way. `GameTests` unchanged. Nine new tests assert the properties the loop silently depends on and that no compile error would catch — a monotonic counter (an unsigned difference turns any step backwards into a frame that appears to have taken years, then gets hidden by the `MaxElapsedTime` clamp), a non-zero **and stable** frequency, and a counter that actually advances rather than merely never decreasing. |
| PLAT-49 | Migrate `Game`'s cursor handling | ✅ | `SDL_ShowCursor`/`SDL_HideCursor` → `IPlatformMouse::SetCursorVisible`. Now **null-guarded**, which the SDL calls had no way to be: the mouse service is null exactly when the platform reports no pointer, so `IsMouseVisible` becomes a determinate no-op under HEADLESS (and later TERMINAL) instead of calling into a windowing system that is not there. The existing window guard is kept — cursor visibility is meaningless without a window. |
| PLAT-50 | Migrate `GameWindow` to `IPlatformWindow` | ⛔ **blocked on Phase 4** | `GameWindow` does not create its window; it wraps the `SDL_Window*` the **renderer** hands it (`Game`'s constructor: `Window_.setWindowInternal(GraphicsDevice_.GetRenderer().GetWindowInternal())`). Until `IGraphicsRenderer` produces an `IPlatformWindow*` — PLAT-57's renderer-facing surface and the family migrations that follow — there is nothing for `GameWindow` to hold, and a conversion layer here would be scaffolding built to be deleted. Original scope: | Remove `struct SDL_Window;` from `GameWindow.hpp`; `window_`, `updateFromSDL()`, `refreshCachedSDLState()`, `queryClientBoundsFromSDL()`, `queryScreenDeviceNameFromSDL()` all re-point at the platform window. |
| PLAT-51 | Replace `GameWindow::GetNativeSdlWindowEXT()` | ⛔ **blocked on PLAT-50** | The public `CNAEXT` accessor returning `SDL_Window*` is replaced by one returning `NativeWindowHandle`. This is a deliberate breaking change to a CNAEXT extension; per `CLAUDE.md`'s "no backward compatibility hacks", no alias is kept. Every in-repo caller is updated in the same task and the change is called out in the commit body. |
| PLAT-52 | Migrate `GraphicsDeviceManager` | ✅ | **SDL-free, header and implementation.** `SDL_GetPlatform()` — the orientation decision, a call with nothing to do with graphics that nonetheless dragged the SDL header into a header every game includes — becomes `IPlatformSystemInfo::GetPlatformName()`, read from the *ambient* platform because the default constructor has no game to ask and this is a property of the process rather than of a game. `tryGetSDLWindow()` was **deleted rather than migrated**: it had zero callers in the entire repository, and porting dead code would have carried `struct SDL_Window;` forward for nothing. Five tests, deliberately written **without** the SDL video probe the rest of `GraphicsDeviceManagerTests.cpp` guards itself with — everything they check is decided before any window exists, which is the whole point. |
| PLAT-53 | Migrate `Logger` | ✅ | `Logger` owns its own sink; `SetSink()`/`ResetSink()` replace `SDL_LogMessage`/`SDL_SetLogPriorities`. The default writes to **stderr, never stdout** — a terminal-hosted game draws its frame on stdout, so the destination is a correctness matter, not a preference. Removing SDL also removed a **hidden second gate**: SDL defaulted non-application categories like `RENDER` to a stricter threshold, so a `Warn` CNA's own `IsEnabled()` had allowed could still be discarded. There is now one gate, pinned by a test. The old tests asserted SDL's *process-wide priority state* — testing SDL, not CNA — and were rewritten against observable behaviour: 10 tests covering per-level delivery (SDL collapsed DEBUG/TRACE/EXPERIMENT onto one priority and could not tell them apart), category pass-through, conditional overloads and format. Original scope: | `modules/core/src/Logger.cpp`'s `SDL_Log` calls (4 there, 56 repo-wide) → CNA's own sink, or a platform log service. Prefer the former: logging does not need to be a platform capability. |
| PLAT-54 | Resolve the entrypoint question | ✅ | `CNA/Entrypoint.hpp` → `modules/platform/include/CNA/Platform/Entrypoint.hpp`, re-keyed from renderer macros onto `CNA_PLATFORM_*`, and the dead `CNA_RENDERER_SDL` branch deleted rather than repaired. **Both `demo_devices` entry points now use it**, removing their direct `<SDL3/SDL_main.h>` includes and duplicated rationale — that was the row's acceptance criterion, since it turns a zero-consumer abstraction into a tested path. Exempted from PLAT-27's SDL-free probe with a documented reason (conditionally including `SDL_main.h` is its entire job) via a deliberately tiny `SDL_FREE_EXEMPT` list. Windows/`WinMain` remains unverified here — no Windows build available; recorded rather than assumed. Original scope: | PLAT-5 already decided it; this is the migration. Delete the dead `CNA_RENDERER_SDL` condition (never repaired — the renderer is the wrong axis now that renderer and platform are separate choices); re-key `Entrypoint.hpp` on `CNA_PLATFORM` and move it into `modules/platform` so `modules/core` stops including SDL headers; **convert both `demo_devices` entry points to actually use it**, which is what turns a zero-consumer abstraction into a tested path and is this row's acceptance criterion; scope the `SDL3::SDL3main` link to the SDL3 platform. Verify the Windows/`WinMain` path on a real Windows build rather than inferring it from Linux — the dead branch means no target has ever exercised it. |
| PLAT-55 | Runtime golden-behavior verification | ⬜ | PLAT-6's captured event semantics reproduced by the migrated runtime, as an automated test rather than a manual check. This is the "prove behavioral equivalence without adding SDL2" step from `cnaplatform.md`'s implementation order. |
| PLAT-56 | Runtime test-suite migration | ⬜ | `GameTests`, `GameWindowTests`, `GraphicsDeviceManagerTests` no longer reference SDL directly; they exercise the platform seam instead, which is also what makes them runnable under `HEADLESS` in Phase 8. |

---

## Phase 4 — Graphics and renderer decoupling

The largest phase: 116 production renderer files and the three `// TODO: SDL dependency should be
abstracted later` markers in `IGraphicsRenderer.hpp`. Renderers are migrated in families so each
task builds and verifies a coherent group.

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-57 | Design the renderer-facing platform surface | ⬜ | Written decision, before code: renderers get `NativeWindowHandle`, drawable size, DPI and lifecycle notifications — nothing else. Encodes design decision 5 and `cnaplatform.md`'s "graphics backends must not automatically depend on SDL3". |
| PLAT-58 | Replace `SDL_Window*` in renderer creation params | ⬜ | The `SDL_Window* window` field of `IGraphicsRenderer`'s creation parameters becomes `NativeWindowHandle` + size/DPI. Touches every renderer's `Initialize`, so it lands as a mechanical change with no behavior change. |
| PLAT-59 | Remove `GetWindowInternal()`/`GetRendererInternal()` | ⬜ | Discharges two of the three TODOs. `GetRendererInternal()` (an `SDL_Renderer*`) survives only behind the SDL-specific renderer allowlist, not on the common interface. |
| PLAT-60 | Remove `SDL_Texture*` from `GetNativeTexture()` | ⬜ | The third TODO. Either an opaque handle on the common interface, or the method moves to the SDL-specific renderer interface. Decide based on PLAT-2's classification of who actually calls it. |
| PLAT-61 | Re-key the window registry | ⬜ | `IGraphicsRenderer`'s `std::unordered_map<SDL_Window*, IGraphicsRenderer*>` becomes keyed by `WindowId`. Static-registry lifetime semantics preserved exactly. |
| PLAT-62 | Migrate `GraphicsDevice` | ⬜ | `modules/graphics/src/Xna/GraphicsDevice.cpp`. |
| PLAT-63 | Migrate `GraphicsAdapter` | ⬜ | Display enumeration and modes via the displays service. |
| PLAT-64 | Migrate `Texture2D` / `TextureCube` | ⬜ | Their SDL surface/pixel-format usage. |
| PLAT-65 | Migrate `ImageLoader` | ⬜ | Uses SDL3_image. Decide: keep SDL3_image behind an image-decode service, or vendor a decoder. Whichever is chosen, `modules/graphics` stops linking SDL directly. |
| PLAT-66 | Migrate the coordinate-conversion contract | ⬜ | `IGraphicsRenderer`'s window↔logical coordinate conversion is documented in terms of `SDL_GetWindowSize()`; restate and implement it in platform terms. |
| PLAT-67 | Migrate the EasyGL family | ⬜ | `easygl` + the five GL profiles it serves, via `IPlatformGlContext`. The single highest-leverage renderer task — EasyGL is shared by `OPENGLES2`/`OPENGLES3`/`OPENGL33`/`WEBGL1`/`WEBGL2`. |
| PLAT-68 | Migrate `OPENGL1` / `OPENGL2` / `OPENGL4` / `OPENGLES1` | ⬜ | The standalone GL profiles. |
| PLAT-69 | Migrate `MAGNUM` | ⬜ | Desktop GL via Magnum; see `docs/magnum-renderer.md` for its boundary. |
| PLAT-70 | Migrate `SKIA` / `OPENVG` / `SOKOL` | ⬜ | Remaining `SDL_GL_*` consumers. `SKIA` is the larger of the two `cpu-presentation` families PLAT-3 found (83 SDL references in code): its presentation path moves to `IPlatformSurfacePresenter` (PLAT-127), not to a GL context. |
| PLAT-71 | Migrate `VULKAN` | ⬜ | Via `IPlatformVulkanSurface`. |
| PLAT-72 | Migrate `DILIGENT` / `LLGL` / `WICKED` | ⬜ | Runtime-selected or multi-API renderers; `LlglSdlSurface.cpp` is replaced by a native-handle surface. |
| PLAT-73 | Migrate the DirectX family (`DIRECTX1`…`DIRECTX12`, `DIRECT2D`, `FREEDIRECT`) | ⬜ | All obtain `HWND` from SDL today. Split across several commits if the diff warrants it — this row names the family, not necessarily one commit. Highest-value group: these are exactly the renderers whose targets a future non-SDL3 platform would serve. |
| PLAT-74 | Migrate `GDI` / `GLIDE` / `METAL` | ⬜ | Native-handle consumers on Win32 and Cocoa. |
| PLAT-75 | Migrate `BGFX` / `WEBGPU` / `CANVAS` / `HTML_DOM` / `SVG_DOM` / `BLEND2D` / `SOFTWARE` / `PORTABLEGL` / `STUB` / `HEADLESS` | ⬜ | Remaining renderers. PLAT-3 sized this row: `BLEND2D` is a `cpu-presentation` family and moves to `IPlatformSurfacePresenter` (PLAT-127); `SOFTWARE`, `PORTABLEGL`, `STUB` and `HEADLESS` are pure `interface-leak` and need **no work here at all** beyond PLAT-59/PLAT-60; the rest are light native-handle users. |
| PLAT-76 | Confirm the SDL-specific allowlist is exactly four | ⬜ | `python3 tools/platform/renderer_sdl_audit.py --check` exits 0: `SDL_RENDERER`, `SDL_GPU`, `FNA3D` and `FREEDIRECT` are the only renderers still referencing SDL3, and each declares `REQUIRES_PLATFORM SDL3` in CMake. The gate already exists and already fails correctly (38 families still coupled today), so this row is verified by running it, not by re-reading the tree. Any other survivor is a Phase 4 defect, not an accepted exception. |

---

## Phase 5 — Input migration

`modules/input` is the largest single non-renderer consumer (48 production files) but also the
best prepared: it already has an internal backend seam and hand-written fakes. This phase
re-points that seam at the platform contract rather than inventing a new one.

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-77 | Map the existing input backend seam onto PLAT-24 | ⬜ | Written mapping from `SdlGamepadBackend`/`SdlJoystickBackend`/`SdlHapticBackend`/`System*Backend` to the platform input interfaces, including which of them disappear entirely. Prevents two parallel abstractions coexisting. |
| PLAT-78 | Migrate `SdlInputBridge` to `PlatformEvent` | ⬜ | The bridge consumes `PlatformEvent` instead of `SDL_Event` and is renamed accordingly. Its golden tests (`SdlInputBridgeGoldenTests`) are the equivalence oracle and must pass unchanged in substance. |
| PLAT-79 | Migrate `Keyboard` | 🟨 | **`Sdl3Keyboard` implemented**: one pass over SDL's key array per `Update()`, producing a compact held-key list — the snapshot shape is what enforces `cnaplatform.md`'s rule that thousands of `IsKeyDown` calls read a local structure rather than becoming thousands of platform calls. `exactKeyboardState` now honestly true. Amber until `modules/input`'s `Keyboard` is re-pointed at it in Phase 5. Original scope: | Snapshot-per-frame model per `cnaplatform.md`'s "input snapshots"; `KeyboardInputTests` pass. |
| PLAT-80 | Migrate `Mouse` | 🟨 | **`Sdl3Mouse` implemented**. The button mask is **repacked into CNA's own bit order** rather than passed through: SDL's is 1-based-button-indexed, and leaking that would make every consumer depend on an SDL detail. Relative mode **refuses when no window is focused** — SDL3 scopes it to a window, so there is genuinely nothing to capture, and silently reporting success would be worse. Amber until `modules/input`'s `Mouse` is re-pointed in Phase 5. Original scope: | Position, buttons, wheel, relative mode, window association. |
| PLAT-81 | Migrate `MouseCursor` | 🟨 | System cursor shapes implemented on `Sdl3Mouse`. The previous cursor is destroyed **only after** the new one is made current — freeing a cursor that is still set is a use-after-free inside SDL. Custom (image) cursors and the `modules/input` re-point remain. Original scope: | System and custom cursors, capability-gated. |
| PLAT-82 | Migrate `GamePad` | 🟨 | **`Sdl3Gamepad` implemented**: enumeration, buttons, axes, triggers and rumble. Axis normalisation scales each half of SDL's asymmetric `[-32768, 32767]` range by its own magnitude, matching the event mapper. Rumble strength is **clamped** before the 16-bit conversion — an out-of-range value would wrap and turn "maximum" into "almost nothing". An empty or out-of-range slot reports not-connected rather than throwing, because XNA games poll all four player indices unconditionally. Handles are closed on every rescan; verified leak-free under ASan. The mapping database and `GamePadMappingTests` re-point remain. Original scope: | Including the mapping database and `GamePadMappingTests`. The SDL3-vs-SDL2-vs-SDL1 capability gap noted in `cnaplatform.md` is expressed through capabilities here — but no second implementation is written. |
| PLAT-83 | Migrate joystick support | ⬜ | Legacy joystick path, distinct from gamepad. |
| PLAT-84 | Migrate haptics | 🟨 | **`IPlatformHaptics` added and `Sdl3Haptics` implemented.** Kept **separate from `IPlatformGamepad::SetRumble`** on evidence, not taste: `Microsoft::Devices::VibrateController` drives the device's *own* haptics with no gamepad involved, so folding the two would force a phone-only caller through a controller index. PLAT-24 had deferred the interface until a second distinct caller appeared; `modules/devices` and `modules/input` are two. Devices are opened **lazily and cached** (`SDL_OpenHaptic` is not cheap and `VibrateController` calls start/stop repeatedly on one device), and an index that vanished on hotplug **returns false rather than throwing** — losing a controller mid-effect is ordinary, not exceptional. Strength is clamped, matching gamepad rumble. Amber until `Haptics.cpp`/`HapticDevice.cpp`/`SdlHapticBackend` are re-pointed at the service in Phase 5. Original scope: | `Haptics.cpp`, `HapticDevice.cpp`, `SdlHapticBackend`; capability-gated (`supportsGamepadRumble`). |
| PLAT-85 | Migrate sensors | 🟨 | **`Sdl3Sensors` implemented** on the PLAT-24 `IPlatformSensors` contract (enumerate, open, read, close). `Start()` on a sensor the device does not have throws `PlatformNotSupportedException(Sensors)` — an absent accelerometer is a capability answer, so it names the capability instead of surfacing an SDL error string; `Stop()` on something never started is a no-op, symmetric with `ReleaseSubsystem`. `TryGetReading` leaves its out parameter **untouched** on false, so a caller that ignores the return value cannot act on stale values. `capabilities.sensors`/`.haptics` are now honestly true and the conformance suite asserts the service/capability pairing for both. Amber until `SystemSensorBackend` is re-pointed in Phase 5 and PLAT-36 maps sensor events. Original scope: | `SystemSensorBackend`; shares subsystem ownership with `modules/devices` — PLAT-29's refcounting is the contract here. |
| PLAT-86 | Migrate `TouchPanel` and gestures | ⬜ | `TouchPanel.cpp` + `GestureDetector`; `SdlInputBridgeTouchGestureTests` pass. |
| PLAT-87 | Migrate `TextInputEXT` | 🟨 | **`Sdl3TextInput` implemented**: start/stop, active state and IME input area. `textInput`/`ime` now honestly true. Amber until `modules/input`'s `TextInputEXT` is re-pointed in Phase 5. Original scope: | Text input start/stop, input area, IME; capability-gated (`supportsTextInput`, `supportsIme`). |
| PLAT-88 | Migrate input-side `Clipboard` | ⬜ | `modules/input/src/CnaExt/Clipboard.cpp` → clipboard service. Coordinate with PLAT-100 (`devices-ext` has a second clipboard surface) so one service backs both. |
| PLAT-89 | Migrate `Power` | ⬜ | `modules/input/src/CnaExt/Power.cpp` + `SystemPowerBackend` → power service. Same dedup consideration as PLAT-88 with `devices-ext`'s `PowerInfo`. |
| PLAT-90 | Retire the `FakeSdl*Backend` doubles | ⬜ | Replaced by fakes implementing the platform input interfaces. Fewer, sharper doubles is the measurable outcome; the tests they serve keep their coverage. |

---

## Phase 6 — Audio platform contract

Separate contract, separate selection variable, per design decision 7.

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-91 | `CNA::Audio::Platform::IAudioDevice` contract | ⬜ | Device open/close, format negotiation, and a whole-buffer `FillBuffer(output, sampleCount)` callback contract. Per-sample dispatch is explicitly forbidden — `cnaplatform.md` calls this out as the one place where the abstraction genuinely could cost performance. |
| PLAT-92 | `IAudioRecordingDevice` contract | ⬜ | Microphone/recording, capability-gated. |
| PLAT-93 | `CNA_AUDIO_PLATFORM` CMake variable | ⬜ | Values `SDL3` (default) and `NULL`. `OPENAL`/`WASAPI`/`ALSA` reserved and rejected loudly, same rule as PLAT-11. |
| PLAT-94 | `Sdl3AudioDevice` | ⬜ | `SDL_OpenAudioDeviceStream`, `SDL_CreateAudioStream`, `SDL_PutAudioStreamData`. |
| PLAT-95 | Migrate `AudioMixer` | ⬜ | The mixer keeps its own inner loops; only device acquisition and buffer submission cross the contract. The hot-path lint (PLAT-9) covers this file specifically. |
| PLAT-96 | Migrate `SoundEffect` / `SoundEffectInstance` / `DynamicSoundEffectInstance` | ⬜ | Including `SDL_IOFromConstMem`/`SDL_IOFromDynamicMem` decode paths (via PLAT-44's filesystem/IO service or a vendored decoder). |
| PLAT-97 | Migrate `Microphone` | ⬜ | Via PLAT-92; `SDL_GetAudioRecordingDevices`. |
| PLAT-98 | Migrate `WaveBank` / `XactParser` | ⬜ | SDL IO usage only. |
| PLAT-99 | `NullAudioDevice` | ⬜ | A second audio implementation, which is what makes the audio contract testable in CI and proves the seam is not SDL-shaped. Cheap, and it pays for itself immediately in test runtime. |

---

## Phase 7 — System services

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-100 | Migrate `devices-ext` `Clipboard` | ✅ | On `IPlatformClipboard`. A null service (no clipboard capability) returns `false`/empty rather than throwing — this API answers with a `bool`, the same shape it had when `SDL_SetClipboardText` failed. Original scope: | Shares the PLAT-88 service; the duplicate implementation is removed, not paralleled. |
| PLAT-101 | Migrate `devices-ext` `PowerInfo` | ✅ | On `IPlatformSystemInfo::GetPowerInfo()`. Required adding **`PowerState::Error`** to the contract: `Devices::PowerState` distinguishes it from `Unknown`, and collapsing the two would have lost the difference between "this device has no answer" and "the query failed" — one is diagnosable, the other is not. Original scope: | `SDL_GetPowerInfo` (8 references); shares PLAT-89's service. |
| PLAT-102 | Migrate `DisplayInfo` | ⬜ | Via the displays service. |
| PLAT-103 | Migrate `SdlMessageBoxBackend` | ⬜ | `SDL_ShowMessageBox`/`ShowSimpleMessageBox` → dialogs service; capability-gated. |
| PLAT-104 | Migrate `SdlFileDialogBackend` | ⬜ | Open/save/folder dialogs; capability-gated (`supportsNativeFileDialog`). |
| PLAT-105 | Migrate `SdlTrayBackend` | ⬜ | Tray + menus; capability-gated. |
| PLAT-106 | Migrate `SdlCameraBackend` | ⬜ | `SDL_GetCameras`/`SDL_OpenCamera`; capability-gated. |
| PLAT-107 | Migrate `Locale` / `SystemInfo` / `UrlLauncher` | ✅ | All three on `IPlatformSystemInfo`. Required changing `GetPreferredLocales()` to return a structured **`PlatformLocale{language, country}`** instead of a BCP 47 tag: `Devices::LocaleInfo` needs the parts separately, and re-splitting a formatted tag would be a lossy round-trip through a string for no benefit. Changed now, with one consumer, rather than later with several. Original scope: | `SDL_GetPreferredLocales`, `SDL_GetSystemRAM`, `SDL_GetNumLogicalCPUCores`, `SDL_GetPlatform`, `SDL_OpenURL`. |
| PLAT-108 | Migrate `Microsoft::Devices` sensors and vibrate | ⬜ | `Accelerometer`, `Gyroscope`, `SdlSensorSubsystem`, `SdlHapticVibrateBackend`, `SdlSubsystemMutex`. Must preserve the audited shutdown ordering (PLAT-4) — this subsystem has dedicated ordering tests for a reason. |
| PLAT-109 | Migrate `StorageDevice` | ✅ | `SDL_GetPrefPath` → `IPlatformFileSystem::GetPreferencesPath()`, and `SDL_getenv` → `std::getenv` for the XDG/HOME fallback — **reading an environment variable never needed a platform abstraction**, and routing it through one would have been abstraction for its own sake. Fallback ordering and trailing-separator stripping preserved exactly. `modules/storage` no longer links SDL. Required a design addition recorded here: `CNA/Platform/CurrentPlatform.hpp`, an ambient accessor for the **static** XNA API surface (`StorageDevice`, `TitleContainer`, `Keyboard::GetState`) which takes no context argument and cannot be given one without changing the API CNA exists to reproduce. `Game` still owns and passes its instance; this is only for what genuinely cannot. Lazily creates the build-time default so bare XNA calls work with no ceremony, and `SetCurrentPlatform()` lets a test point the static surface at `HeadlessPlatform`. 5 tests. Original scope: | `SDL_GetPrefPath` → filesystem service. |
| PLAT-110 | Migrate `TitleContainer` / `TitleLocation` | ✅ | `SDL_GetBasePath` → `IPlatformFileSystem::GetBasePath()`, and the Android asset path's `SDL_LoadFile`/`SDL_free` pairs → `TryLoadFile()`. The malloc-and-copy in `ReadAllBytes` is preserved deliberately: the caller owns the returned block and frees it with `std::free`, so the platform's vector cannot be released into it. Original scope: | Base path and file loading. |
| PLAT-111 | Migrate content readers and glTF import | ⬜ | `SoundEffectContentTypeReader`, `GltfImportCore` — `SDL_IOStream` usage only. |
| PLAT-112 | Migrate `media` | ⬜ | `MediaPlayer`, `VideoPlayer`, `MediaLibraryPaths`, `ThumbnailGenerator`. FFmpeg stays as-is; only the SDL touchpoints move. |

---

## Phase 8 — `HeadlessPlatform` and the conformance suite

This phase is what makes the abstraction *real* rather than nominal: a contract with one
implementation is just indirection. `HeadlessPlatform` is deliberately chosen as the second
implementation instead of SDL2 — it is genuinely useful now (CI without a display server, the
same argument `plan_headless.md` already made for the renderer), it is cheap, and it exercises
every capability-refusal path that a limited platform would.

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-113 | `HeadlessPlatform` implementation | ✅ | Full `IPlatform` with no windowing system, no SDL and no third-party library. In-memory windows that can be resized, retitled and queried — and that **model `Sync()`'s asynchrony deliberately**, so a size request lands only on sync exactly as it does on a real windowing system; applying it immediately would let game code depend on behaviour SDL does not provide. Real filesystem and host info on the standard library, since a headless platform still has both and save/load logic is worth running in CI. **Always compiled regardless of `CNA_PLATFORM`**, because the conformance suite needs two implementations live in one process and this one costs nothing to carry. Original scope: | Full `IPlatform` surface with no video subsystem, no window, no SDL. Pairs naturally with the `HEADLESS` renderer. |
| PLAT-114 | `HeadlessPlatform` capabilities and refusals | ✅ | Every capability `false` — which is the point: it is what exercises every refusal path in the contract. `CreateSurfacePresenter` throws `PlatformNotSupportedException` naming `SurfacePresentation`, never a stub that accepts frames and drops them. `GetPowerInfo()` reports **Unknown**, not "no battery": this platform cannot tell, and a fabricated answer is one a caller would display. Original scope: | Every unsupported capability reports `false` and its calls refuse deterministically — no silent no-ops (rule 3). |
| PLAT-115 | Synthetic event injection for `HeadlessPlatform` | ✅ | `InjectEvent()` queues a `PlatformEvent` for the next `PollEvents`. This is what makes input, runtime and lifecycle behaviour testable with no display server: a test drives the exact sequence it wants instead of hoping the environment produces one. Original scope: | Tests can push `PlatformEvent`s into the queue, which is what lets input, runtime and lifecycle behavior be tested with no display server at all. |
| PLAT-116 | Platform conformance suite | ✅ | `PlatformConformanceTests.cpp` — every test written against `IPlatform` and run against **every implementation compiled in**, via `INSTANTIATE_TEST_SUITE_P` over `PlatformFactory::GetAvailable()`, so a future implementation joins automatically. 29 cases × 2 implementations. Covers identity, capability/service agreement, refusal behaviour, capability stability, subsystem refcounting, unpaired release, event batching and capacity reuse, timing monotonicity, window state and `Sync()` semantics, and the throw-vs-status split. **It earned itself on first run**: it caught `Sdl3Platform` advertising `gamepad`, `textInput`, `sensors` and more while their accessors returned null. Original scope: | A test suite written *against `IPlatform`*, run against every compiled implementation. Covers lifecycle, window state transitions, event ordering/coalescing, timing monotonicity, capability self-consistency, and error/refusal behavior. This is `cnaplatform.md`'s step 6, pulled earlier because it is what protects the contract from drifting SDL3-shaped. |
| PLAT-117 | Capability self-consistency test | ✅ | Part of the conformance suite: for every service, non-null **exactly when** its capability is true. Advertising a capability whose accessor returns null sends a caller into a null dereference; returning a service while reporting false makes the capability set useless for deciding whether to ask. `Sdl3Platform`'s capability list is now conservative rather than aspirational — each flag flips true in the Phase 5/7 task that wires its accessor, listed inline in the source. Original scope: | For every capability: if reported `true`, the corresponding call must succeed; if `false`, it must refuse. Mechanically enumerated so a newly added capability cannot be forgotten. |
| PLAT-118 | Event-semantics golden suite | ⬜ | PLAT-6's capture promoted into a cross-implementation golden test. Named risk from `cnaplatform.md`: implementations differing in event semantics, DPI, fullscreen, gamepad or timing behavior. |
| PLAT-119 | Run the CNA test suite under `CNA_PLATFORM=HEADLESS` | 🟨 | **The build configures, compiles and runs with `CNA_PLATFORM=HEADLESS`**: 5532 passed, 89 skipped, only the pre-existing unrelated MRT failure. The conformance suite runs against `Headless` alone there and passes 33/33. Found a real build-configuration defect that SDL3-only testing had masked: the platform module's `Sdl3*` test TUs were compiled unconditionally while the SDL3 implementation is not, so the link failed on undefined `MapSdlEvent`. `cmake/UnitTests.cmake` now excludes them when `CNA_PLATFORM != SDL3`; the implementation-neutral tests and the parameterised conformance suite always build. Stays amber because this does not yet mean what the row will finally mean: CNA's other modules still call SDL3 directly, so the *library* keeps linking SDL regardless of the platform selection. The row closes once Phases 3–7 migrate them. Original scope: | Every test that does not genuinely need a display passes with no `DISPLAY`/`WAYLAND_DISPLAY`. Concrete, checkable proof the separation works. |

---

## Phase 9 — Gates, performance, documentation

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-120 | Performance verification against PLAT-7 | ⬜ | Re-run the baseline. Target: no regression outside the measured noise floor, consistent with `cnaplatform.md`'s "typically under 0.5 %". A regression above the floor is investigated and fixed, not accepted with a shrug — and if the abstraction turns out to *help* (centralized events, input snapshots), that is reported too. |
| PLAT-121 | Flip the SDL ratchet to hard-error | ⬜ | Default `CNA_PLATFORM_RATCHET_STRICT` to `ON` — the mechanism already exists and is already tested (PLAT-8), so this row is a one-line default change plus a final `--update` driving the budget to the allowlist floor. Allowlist: `modules/platform/src/Sdl3/`, `modules/audio`'s SDL3 audio implementation, and the four renderers from PLAT-76. Nothing else may include an SDL header. |
| PLAT-122 | Remove SDL from module link lines | ⬜ | `SDL3::SDL3` disappears from `modules/{core,input,runtime,graphics,devices,devices-ext,storage,content,media,gamer-services}/CMakeLists.txt`. Mechanical, but it is the check that PLAT-121's gate is not passing on a technicality. |
| PLAT-123 | Migrate remaining test/example SDL usage | ⬜ | 316 test/example files reference SDL. Most are incidental; each is either migrated or justified. Tests that legitimately test the SDL3 implementation stay, in `modules/platform/tests/`. |
| PLAT-124 | `docs/platform-abstraction.md` | ⬜ | The capability boundary, the native-handle contract per window system, the performance contract, and how to add an implementation. This document, not this plan, is what a future implementer reads first. |
| PLAT-125 | Update `CLAUDE.md` and `NEXT.md` | ⬜ | Record `CNA_PLATFORM` alongside `CNA_GRAPHICS_RENDERER`, and the rule that new code must not include SDL headers outside the allowlist. Without this the next contributor re-introduces the coupling in good faith. |
| PLAT-126 | CI matrix for `CNA_PLATFORM` | ⬜ | At minimum `SDL3`×(one GPU renderer, one CPU renderer) and `HEADLESS`×`HEADLESS`. Keeps both implementations honest; `cnaplatform.md` names CI-combination growth as one of the real costs, so the matrix is chosen deliberately rather than exhaustively. |

---

## Phase 10 — `TerminalPlatform` (optional, gated on Phase 8)

Feasibility established in [`docs/platform-terminal-analysis.md`](docs/platform-terminal-analysis.md).
**Verdict: yes for output and mouse, qualified for keyboard, no for gamepad** — and no new renderer
is needed, because a terminal is another consumer of `IPlatformSurfacePresenter` (PLAT-127).

Why it belongs in this plan rather than §12: it is the strongest available proof that the contract
is not SDL-shaped. `HeadlessPlatform` implements every method by doing nothing, so a contract built
entirely around SDL3's assumptions would still pass it. A terminal has a real output device with a
different model (cells, not pixels), real input with a different model (byte streams, not key
state), and a capability profile that is mostly `false`. If the contract survives it, it is real.

**Sequencing is deliberate: this phase runs *after* Phase 8, never before.** Written before the
contract has been proven against `Sdl3Platform` and the conformance suite, a terminal
implementation would shape the contract around terminal quirks — the mirror image of the
SDL3-shaped mistake this whole plan exists to avoid.

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-129 | Terminal capability spike | ⬜ | Before any implementation: detect at runtime what the host terminal actually supports — truecolor vs 256 vs 16, SGR mouse (`?1006`), the Kitty keyboard protocol's report-event-types flag, OSC 52 clipboard. **Assume nothing**; emulator support is real but partial and evolving. Output is a detection routine plus a recorded matrix for the terminals available here. |
| PLAT-130 | `TerminalPlatform` skeleton + `CNA_PLATFORM=TERMINAL` | ⬜ | Module layout under `modules/platform/src/Terminal/`, selectable like `SDL3`/`HEADLESS`. Links no SDL. |
| PLAT-131 | Terminal lifecycle and state restoration | ⬜ | `tcsetattr` raw mode, alternate screen buffer, hidden cursor, mouse reporting — each with guaranteed restoration on normal exit, `SIGINT`/`SIGTERM`/`SIGHUP`, unhandled exception **and** abort. A crash that leaves the user with no echo and an invisible cursor is a hostile bug, so this is its own task and its own test, not a corner of PLAT-130. |
| PLAT-132 | `TerminalSurfacePresenter` | ⬜ | Implements PLAT-127 by feeding the RGBA buffer to the existing `QuantizeFrameToGrid()` (`modules/graphics-ext/`) and emitting ANSI. The reuse is the point: the RGBA→glyph-grid transform, `AsciiCell`, `AsciiGrid` and the `" .:-=+*#%@"` ramp are already written and tested. |
| PLAT-133 | Damage tracking and run-length SGR | ⬜ | **Mandatory, not an optimisation.** A naive 120×40 truecolor frame is ~96 KB; at 60 fps that is ~5.8 MB/s, which is fine on a local pty and hopeless over SSH. Diff against the previous grid, emit only changed cells, and only emit an SGR change when the colour actually differs. |
| PLAT-134 | Colour degradation ladder | ⬜ | truecolor → 256 → 16 → monochrome, chosen by PLAT-129's detection. `AsciiQuantizeMode` already provides `Color`/`BlackWhite`. |
| PLAT-135 | Frame budget | ⬜ | Cap bytes per frame; drop refresh rate rather than blocking on a slow terminal. A frame *budget*, not a frame *rate*, is the right contract over a link of unknown speed. |
| PLAT-136 | Terminal window + `SIGWINCH` | ⬜ | The viewport is the window: client bounds = `columns × cellWidth` by `rows × cellHeight`. `SIGWINCH` maps onto the existing `PlatformEvent` window-resized path, so `GameWindow`/`GraphicsDeviceManager` need no terminal-specific code. Default cell aspect 1:2 or output is vertically squashed by half. |
| PLAT-137 | Keyboard: Kitty protocol path | ⬜ | With the report-event-types flag, press/repeat/release are distinct and `KeyboardState` is **exact**, genuinely equivalent to SDL3. Report `supportsExactKeyboardState = true`. |
| PLAT-138 | Keyboard: synthetic key-up fallback | ⬜ | Without release events, mark down on receipt and clear after a timeout past the auto-repeat interval. This is an **approximation and must be reported as one** (`supportsExactKeyboardState = false`): a key reads held for tens of ms after release, the initial repeat delay makes a held key stutter unless bridged, and the repeat rate is an unqueryable user setting. Silently papering over this would defeat the capability model. |
| PLAT-139 | Mouse via SGR 1006 | ⬜ | Press, release, motion, wheel. Coordinates arrive in **cells, not pixels**, so positions quantise to the cell size; report `supportsPixelAccurateMouse = false` rather than implying a precision that is not there. |
| PLAT-140 | Terminal capability profile + refusals | ⬜ | No gamepad/haptic/sensor channel exists through a TTY — `GamePad::GetState()` reports not-connected. No native window handle: add `NativeWindowSystem::Terminal` with null pointers so every GPU renderer refuses deterministically at selection instead of crashing on it. Only CPU renderers are selectable (`SOFTWARE`, `SKIA`, `BLEND2D`, `PORTABLEGL`, `HEADLESS`, `STUB`). Logging must never reach `stdout` — it would corrupt the frame, which makes PLAT-53's sink destination a correctness matter. Refuse cleanly when `stdout` is not a TTY. |
| PLAT-141 | Run the conformance suite against `TerminalPlatform` | ⬜ | PLAT-116's suite passes against a third implementation. This is the row that pays for the whole phase: it is what demonstrates the contract is genuinely implementation-neutral rather than SDL3 with extra steps. |

---

## 10. Risks

| Risk | Mitigation |
|---|---|
| Event-semantics drift between implementations | PLAT-6 captures the oracle *before* any change; PLAT-118 enforces it cross-implementation. |
| Native handle interop bugs (X11 `Window` is an integer, not a pointer) | PLAT-13's per-system documentation table + PLAT-14's validating typed accessors. |
| Two parallel abstractions in `modules/input` | PLAT-77 maps the existing seam onto the new one *before* any migration; PLAT-90 deletes what is superseded. |
| Duplicate clipboard/power surfaces in `input` and `devices-ext` | PLAT-88/89 and PLAT-100/101 are explicitly paired to share one service. |
| SDL subsystem init/quit ordering regressions | PLAT-4 audits it first; PLAT-29 preserves it; existing ordering tests are the gate. |
| Renderer coverage silently shrinking | Rule 5; every Phase 4 task names the renderers it touches and rebuilds them. |
| Performance regression in the fixed-timestep loop | PLAT-7/PLAT-39/PLAT-120; batch events, snapshot input, cached capabilities. |
| The plan stalls half-migrated | The PLAT-8 ratchet makes partial progress visible and prevents backsliding at any point. |

---

## 11. Definition of done

This plan is complete when **all** of the following hold:

1. No production source outside `modules/platform/src/Sdl3/`, the SDL3 audio implementation, and
   the four allowlisted renderers includes an SDL header — enforced by PLAT-121, not by review,
   with `renderer_sdl_audit.py --check` covering the renderer half.
2. No SDL type appears in any CNA header, including forward declarations.
3. All 46 renderer identities still build and pass their existing tests.
4. The full test suite passes under `CNA_PLATFORM=SDL3`.
5. The display-independent test suite passes under `CNA_PLATFORM=HEADLESS` with no display server.
6. The conformance suite passes against both implementations.
7. Performance shows no regression outside PLAT-7's measured noise floor.
8. `docs/platform-abstraction.md` documents the contract well enough that a second implementation
   could be written from it without reading `Sdl3Platform`'s source.

---

## 12. Possible future implementations (NOT in scope)

**None of the following is implemented by this plan.** They are recorded because they are the
reason the contract is shaped as it is — a contract designed for exactly one implementation would
have come out SDL3-shaped, which is the failure mode `cnaplatform.md` warns against. Each would
need its own plan file.

| Candidate | Rationale | Expected capability profile | Status |
|---|---|---|---|
| `Sdl2Platform` | Older Windows (back to XP per SDL2's docs), older Linux distributions, targets where SDL3 is impractical, and a long-term fallback path. The most realistic second implementation. | Close to full, minus the SDL3 GPU API; gamepad and HiDPI contracts differ. | Future — not started |
| `Sdl12Platform` | Genuinely historical systems. SDL 1.2 is deprecated upstream (last release 1.2.15) and would be CNA-owned indefinitely. | Limited profile: basic window, keyboard/mouse, old joystick API, timing, basic audio, GL or software surface, basic fullscreen. Clipboard, text input, gamepad, HiDPI largely unsupported — declared unsupported, never emulated with unsafe hacks. | Future — not started |
| `Win32Platform` | A native platform with no SDL at all, pairing naturally with the `GDI` and `DIRECTX*` renderers. | Windows-only; no cross-platform pretence. | Future — not started |
| `EmscriptenPlatform` | A direct browser platform for the web renderers. | Web-shaped: no multiple windows, no native dialogs. | Future — not started |
| `TerminalPlatform` | Runs a game in a TTY — over SSH, in CI, in `tmux`, on any machine with no display server. **Not listed here as future work: it was analysed and promoted to Phase 10**, because it is the strongest available proof the contract is not SDL-shaped. | Cells not pixels; mouse at cell granularity; exact keyboard only under the Kitty protocol; no gamepad. | **Analysed — Phase 10** |
| Separate gamepad module | If a second implementation shows the capability model cannot express the SDL1-joystick vs SDL2-GameController vs SDL3-Gamepad gap (design decision 8), the gamepad seam is split out then — on evidence, not in advance. | — | Future — revisit after a second implementation exists |
| Audio implementations (OpenAL, WASAPI, ALSA) | The audio contract (Phase 6) is separate precisely so these need no matching platform implementation. | — | Future — not started |
| `sdl2-compat` / `sdl12-compat` as test configurations | Useful to shake out API-generation assumptions, but they run on SDL3/SDL2 underneath and therefore reach **no** new operating system. They are a test configuration, never a substitute for a real implementation. | — | Future — not started |
| Legacy C++/toolchain profile | Separating SDL3 is necessary but **not sufficient** for genuinely old targets: an older compiler, a language profile below C++23, standard-library restrictions and possibly a C ABI between a modern core and a legacy host are all separate problems. | — | Future — out of scope, own plan required |

The ordering `cnaplatform.md` recommends, for whenever that work starts: prove SDL3 equivalence
first (Phases 0–9 here), then add `Sdl2Platform`, then extend the conformance suite, then design a
limited `Sdl12Platform`, and only last address the legacy C++/toolchain profile.
