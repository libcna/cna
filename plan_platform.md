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
| Distinct `SDL_*` identifiers referenced anywhere under `modules/` | **1100** |
| Files referencing SDL (all) | **578** |
| Production files (`src/` + `include/`) referencing SDL | **262** |
| …of which are renderer production files | **116** |
| Test/example files referencing SDL | **316** |
| Distinct `SDL_PROP_WINDOW_*` native-handle properties read | **7** |
| Renderer families reaching for `SDL_GL_*` directly | **11** |

Production SDL surface per module (`src/` + `include/` only):

| Module | Files | Dominant concern |
|---|---:|---|
| `modules/input` | 48 | keyboard, mouse, gamepad, joystick, haptic, sensor, touch, text input |
| `modules/devices-ext` | 34 | clipboard, message box, file dialog, tray, camera, locale, power, display, URL |
| `modules/devices` | 17 | `Microsoft::Devices` sensors + vibrate, SDL subsystem refcounting |
| `modules/audio` | 11 | audio device/stream, mixer, microphone |
| `modules/graphics` | 11 | `GraphicsDevice`, `GraphicsAdapter`, `Texture2D`, `ImageLoader` |
| `modules/media` | 7 | `MediaPlayer`, `VideoPlayer`, library paths |
| `modules/runtime` | 7 | `Game` loop, `GameWindow`, `GraphicsDeviceManager` |
| `modules/content` | 3 | `SDL_IOStream`-based readers, glTF import |
| `modules/core` | 3 | `Logger`, `Entrypoint` (`SDL_main`), `GraphicsRendererType` |
| `modules/gamer-services` | 3 | `Guide` overlay, local store |
| `modules/graphics-ext` | 1 | ASCII post-process effect |
| `modules/storage` | 1 | `SDL_GetPrefPath` |
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
| — | §12 | Possible future implementations (**not in scope**) | 0 |

**Total: 128 task IDs — 127 active, 1 cut (PLAT-45).** IDs are never renumbered and are never
reused: a cut task keeps its ID and records why, and a task added after the plan was written gets
the next free ID and sits in its logical phase (PLAT-127/PLAT-128, added by PLAT-3's audit).

---

## Phase 0 — Inventory, gates and baseline

Nothing is abstracted here. This phase makes the problem measurable so that every later task can
prove it made progress.

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-1 | Machine-readable SDL inventory tool | ✅ | `tools/platform/sdl_inventory.py`. Emits per-module/per-file/per-symbol counts as markdown, CSV or JSON; `--update` regenerates §2 in place, `--check` fails when it is stale. §2 is now generated. Two corrections to the hand-counted baseline it replaced: `.mm` sources were missed (`MetalRenderer.mm`), and CNA's own `CNA_RENDERER_SDL_*` build macros were being counted as SDL usage in four test files. |
| PLAT-2 | Classify all 1100 SDL identifiers into contract areas | ✅ | `tools/platform/sdl_classify.py` → `docs/platform-sdl-classification.csv`. Ordered rule table, first match wins; the script **exits non-zero if any identifier matches no rule**, so completeness is mechanical rather than claimed. 1100 identifiers over 16 non-empty areas, each naming its owning interface and migrating task. Areas split beyond this row's original wording: `lifecycle` (PLAT-29), `error` (PLAT-21), `logging` (PLAT-53) and `pixel-format` (PLAT-64/65) each have a distinct owner, and folding them into a neighbour would have hidden that. Two findings acted on: `SDL_GetWindowProperties` is native-handle extraction, not window management; and `dynamic-library` came out **empty**, which cut PLAT-45. |
| PLAT-3 | Identify the SDL-specific renderer allowlist | ✅ | `tools/platform/renderer_sdl_audit.py` → `docs/platform-renderer-sdl-audit.md`. All 46 identities over 42 module families, each given one verdict from measured usage. Verdicts read **code only** (comments and string literals stripped) — the inventory counts prose deliberately, but a verdict decided by prose would be wrong, and two families were initially misjudged on exactly that. Three findings, all acted on: the allowlist is **four**, not three (`FREEDIRECT` joins `FNA3D` as an upstream-dependency case, see design decision 6); **`SKIA` and `BLEND2D` present CPU-rasterised pixels through `SDL_Renderer`**, a capability the contract had nowhere to put — hence the new PLAT-127/PLAT-128; and **4 renderers (`STUB`, `HEADLESS`, `SOFTWARE`, `PORTABLEGL`) are coupled only by `IGraphicsRenderer`'s own SDL-typed methods**, so PLAT-59/PLAT-60 free them with no per-renderer work. |
| PLAT-4 | Audit the `SDL_INIT_*` subsystem lifecycle | ⬜ | Document every `SDL_Init`/`SDL_InitSubSystem`/`SDL_QuitSubSystem` call site (126 + 123 references) and the current refcounting (`SdlSubsystemMutex.hpp`, `SdlSensorSubsystem.hpp`, `DevicesShutdownCoordinator.hpp`). This ordering is subtle and already has dedicated tests — the platform lifecycle must preserve it exactly. |
| PLAT-5 | Audit `SDL_main` / entrypoint handling | ⬜ | `modules/core/include/CNA/Entrypoint.hpp` and `SDL3::SDL3main` linkage in `cmake/Harnesses.cmake`. Determine what the platform contract must own vs. what stays a build-system concern (Windows/Android entrypoints in particular). |
| PLAT-6 | Baseline behavioral capture: event semantics | ⬜ | Record current observable event ordering/coalescing for resize, focus, quit, minimize/restore and DPI change, as a checked-in golden file. This is the oracle Phase 3 must match; capturing it *before* any change is the point. |
| PLAT-7 | Baseline performance measurement | ⬜ | Frame-time distribution for a fixed scene on at least two renderers (one GPU, one CPU-raster), recorded with enough samples to give a meaningful confidence interval. PLAT-120 re-runs it. The target is "no measurable regression", so the baseline must include its own noise floor. |
| PLAT-8 | SDL-reference ratchet gate (warning mode) | ⬜ | Build-time check: count SDL references in production sources outside `modules/platform` and the PLAT-3 allowlist; compare against a recorded maximum in a checked-in file; warn (not fail) if exceeded. Initial maximum = 262 minus allowlisted files. |
| PLAT-9 | Hot-path lint for platform calls | ⬜ | A check that rejects platform-interface calls syntactically inside per-pixel/per-vertex/per-sample/per-event loops, encoding design decision 4. Heuristic is acceptable; false positives suppressible with a documented annotation. |

---

## Phase 1 — The platform contract

Headers and types only. No SDL3 code, no migrations. Every header in this phase must compile
against a translation unit that has never seen an SDL header — that is the phase's exit test.

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-10 | Create `modules/platform` module | ⬜ | `CMakeLists.txt`, `include/`, `src/`, `tests/`; registered in `modules/CMakeLists.txt`; passes the physical source-partition validator. Links no SDL yet. |
| PLAT-11 | `CNA_PLATFORM` CMake cache variable | ⬜ | Values `SDL3` (default) and `HEADLESS`. `SDL2`/`SDL12`/`WIN32` are recognized and rejected with a message pointing at §12 of this plan — a reserved-but-unimplemented identifier must fail loudly, never fall back to SDL3 silently. |
| PLAT-12 | `NativeWindowSystem` enum | ⬜ | `Unknown, Win32, X11, Wayland, Cocoa, Android, Web, Headless`. Derived from the 7 measured `SDL_PROP_WINDOW_*` properties plus the Emscripten and headless cases the renderer set already needs. |
| PLAT-13 | `NativeWindowHandle` struct | ⬜ | `{ NativeWindowSystem system; void* display; void* window; void* surface; }` per `cnaplatform.md`, plus a documented per-system table of what each `void*` means (X11 `Window` is an integer ID, not a pointer — that must be spelled out, it is the classic interop bug here). Trivially copyable; no ownership. |
| PLAT-14 | Typed native-handle accessors | ⬜ | Small `TryGetWin32Hwnd()`/`TryGetX11()`/`TryGetWayland()`/`TryGetCocoa()`/`TryGetAndroid()` helpers that validate `system` before reinterpreting. Renderers use these rather than casting `void*` themselves, which is what makes a mismatch a caught error instead of a crash. |
| PLAT-15 | `WindowDescription` struct | ⬜ | Title, client size, position/centering, resizable, borderless, fullscreen mode, high-DPI request, visibility, minimum/maximum size, and a renderer-intent flag (`RequiresOpenGl`, `RequiresVulkan`, `None`) — SDL3 needs that intent at *creation* time (`SDL_WINDOW_OPENGL`), so it cannot be a post-creation setter. |
| PLAT-16 | `PlatformCapabilities` struct | ⬜ | Starts from `cnaplatform.md`'s 10 fields, extended from PLAT-2's classification with at minimum: relative mouse, cursor shapes, haptics, sensors, tray, camera, native message box, `SDL_main`-style managed entrypoint, multiple displays, borderless fullscreen. Aggregate, trivially copyable, read once. |
| PLAT-17 | `PlatformEvent` type | ⬜ | A discriminated union (`std::variant` or tagged struct) covering quit, window (resize/pixel-size/focus/minimize/restore/close/DPI/move/display-change), key, text input, mouse (motion/button/wheel), gamepad/joystick add/remove/input, touch, sensor, and app lifecycle (background/foreground). No SDL enum values leak; mapping tables live in the implementation. |
| PLAT-18 | `IPlatformWindow` interface | ⬜ | Title, client bounds, pixel size, display scale, position, show/hide, minimize/restore/maximize, borderless, resizable, fullscreen, sync, `GetNativeHandle()`, and a stable `WindowId`. Mirrors what `GameWindow.cpp` and the renderers actually use today — nothing speculative. |
| PLAT-19 | `IPlatform` root interface | ⬜ | Lifecycle (`Initialize`/`Shutdown`), `CreateWindow`, `PollEvents(std::vector<PlatformEvent>&)`, timing (`GetPerformanceCounter`/`GetPerformanceFrequency`/`GetTicksNs`/`Delay`), `GetCapabilities`, and accessors for each service interface below. Batch event API per design decision 4. |
| PLAT-20 | `PlatformFactory` | ⬜ | One creation point, called once at startup. Returns `std::unique_ptr<IPlatform>`. Compile-time gated on `CNA_PLATFORM`; the factory is where a future second implementation plugs in without touching a single caller. |
| PLAT-21 | `PlatformException` and the error contract | ⬜ | Which failures throw, which return a status, and how the underlying implementation's error text (today: 323 `SDL_GetError()` references) is surfaced without exposing SDL. Follows `CLAUDE.md`'s `std::runtime_error` convention. |
| PLAT-22 | `IPlatformGlContext` service | ⬜ | Create/destroy/make-current/swap/get-proc-address/set-swap-interval/attribute configuration. **11 renderer families** currently call `SDL_GL_*` directly (`easygl`, `opengl1`, `opengl2`, `opengl4`, `opengles1`, `openvg`, `skia`, `sokol`, `magnum`, `metal`, `diligent`) — this interface is what frees all of them at once. |
| PLAT-23 | `IPlatformVulkanSurface` service | ⬜ | `GetInstanceExtensions` / `CreateSurface` / `DestroySurface`, taking and returning opaque handles so the header does not include Vulkan either. Consumers: `VULKAN`, and `DILIGENT`/`LLGL`/`WICKED` where they select a Vulkan device. |
| PLAT-24 | Input service interfaces | ⬜ | `IPlatformKeyboard`, `IPlatformMouse`, `IPlatformGamepad`, `IPlatformJoystick`, `IPlatformHaptic`, `IPlatformSensor`, `IPlatformTouch`, `IPlatformTextInput` under `CNA/Platform/Input/`. Deliberately mirrors the existing `modules/input/include/CNA/Internal/Input/` seam so Phase 5 is a re-pointing, not a redesign. |
| PLAT-25 | System service interfaces | ⬜ | `IPlatformClipboard`, `IPlatformDisplays`, `IPlatformDialogs` (message box + file/folder dialogs), `IPlatformFileSystem` (base/pref/user paths, directory ops, `LoadFile`), `IPlatformPower`, `IPlatformTray`, `IPlatformCamera`, `IPlatformLocale`, `IPlatformSystemInfo`, `IPlatformUrlLauncher`. Each independently capability-gated. `IPlatformDynamicLibrary` is **not** in this list — PLAT-45 cut it for want of a single caller. |
| PLAT-26 | Doxygen pass over the whole contract | ⬜ | `CLAUDE.md` requires a full Doxygen block on every public method, constant and operator in every `.hpp`. This is a contract of ~15 headers; the documentation is the specification a second implementation is written against, so it is a task, not a formality. |
| PLAT-127 | `IPlatformSurfacePresenter` service | ⬜ | **Added by PLAT-3's audit**, which found `SKIA` and `BLEND2D` rasterise on the CPU and use `SDL_Renderer` purely to get finished pixels onto the window. Present an RGBA buffer with a target rectangle, letterbox/logical-presentation scaling and a vsync setting; capability-gated. Without it those two renderers would have stayed permanently allowlisted for want of an interface, not for any real SDL dependence. Genuinely a platform capability: SDL2 has it, SDL 1.2 has it as a software surface, Win32 has `StretchDIBits`. |
| PLAT-27 | SDL-free compilation test | ⬜ | A test TU that includes every `CNA/Platform/*.hpp` and fails to compile if any SDL header is transitively pulled in. Phase 1's exit criterion; guards the whole contract for the rest of the plan. |

---

## Phase 2 — `Sdl3Platform`

The first and, within this plan, only real implementation. It reproduces today's behavior exactly
— it is not an opportunity to redesign semantics.

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-28 | `Sdl3Platform` skeleton + SDL linkage | ⬜ | `src/Sdl3/`; `SDL3::SDL3` linked PRIVATE by `cna_platform` only. |
| PLAT-29 | Subsystem lifecycle and refcounting | ⬜ | Implements PLAT-4's audited semantics: init on demand, quit in the audited order, refcounted across subsystems. The existing `DevicesShutdownOrderingTests` and `SensorSubsystemOwnershipTests` must pass unchanged against it. |
| PLAT-30 | `Sdl3Window` creation and destruction | ⬜ | Maps `WindowDescription` → `SDL_CreateWindow` flags, including the `RequiresOpenGl`/`RequiresVulkan` intent from PLAT-15. |
| PLAT-31 | `Sdl3Window` geometry and state | ⬜ | Size, pixel size, position, display scale, title, bordered, resizable, fullscreen, minimize/restore, sync. Direct port of `GameWindow.cpp`'s current SDL calls. |
| PLAT-32 | Native handle extraction for all 7 properties | ⬜ | Win32 / X11 (display + window number) / Wayland (display + surface) / Cocoa / Android → `NativeWindowHandle`. Includes the Emscripten and headless cases. Unit-tested per platform where CI can reach it; unreachable systems documented as untested rather than claimed. |
| PLAT-33 | Event pump: window events | ⬜ | Resize, pixel-size change, focus gained/lost, close, minimize/restore/maximize, move, display change, DPI change → `PlatformEvent`. Must match PLAT-6's golden capture. |
| PLAT-34 | Event pump: keyboard and text input | ⬜ | Key down/up with the existing keycode/scancode mapping preserved, plus text input and IME events. |
| PLAT-35 | Event pump: mouse and touch | ⬜ | Motion, buttons, wheel, relative mode, touch and gesture events. |
| PLAT-36 | Event pump: gamepad, joystick, sensor | ⬜ | Add/remove/axis/button/sensor events, including the hotplug paths already covered by `InputDevicesHotplugTests`. |
| PLAT-37 | Event pump: application lifecycle | ⬜ | `SDL_EVENT_WILL_ENTER_BACKGROUND` / `SDL_EVENT_DID_ENTER_FOREGROUND` and quit — currently handled inline in `Game.cpp`. |
| PLAT-38 | Batched `PollEvents` with buffer reuse | ⬜ | Fills a caller-owned `std::vector`, reusing capacity across frames; no per-event allocation, no per-event virtual call. Design decision 4's concrete discharge. |
| PLAT-39 | Timing implementation | ⬜ | `SDL_GetPerformanceCounter`/`Frequency`, `SDL_GetTicks`/`GetTicksNS`, `SDL_Delay`. Consumers today: `Game.cpp`'s fixed-timestep loop above all — this is the one path where even small overhead would be visible, so it is measured in PLAT-120. |
| PLAT-40 | `Sdl3PlatformCapabilities` | ⬜ | Every field of PLAT-16 answered honestly for SDL3, with an exhaustive `switch`/aggregate initialization so a newly added capability is a compile error rather than a silently-inherited `false`. |
| PLAT-41 | GL context service (SDL3) | ⬜ | Implements PLAT-22 over `SDL_GL_*`. Attribute setting must be verified against what the 11 GL renderer families currently request — a dropped attribute here is a silent rendering difference. |
| PLAT-42 | Vulkan surface service (SDL3) | ⬜ | Implements PLAT-23 over `SDL_Vulkan_GetInstanceExtensions`/`CreateSurface`/`DestroySurface`. |
| PLAT-43 | Displays service (SDL3) | ⬜ | `SDL_GetDisplays`, bounds, desktop/current display mode, content scale, window display scale. |
| PLAT-44 | Filesystem service (SDL3) | ⬜ | `SDL_GetBasePath`, `SDL_GetPrefPath`, `SDL_GetUserFolder`, directory enumeration/creation, `SDL_LoadFile`, `SDL_IOStream` wrapping. Consumers: `storage`, `content`, `media`, `TitleContainer`. |
| PLAT-128 | Surface presenter (SDL3) | ⬜ | Implements PLAT-127 over `SDL_CreateRenderer`/`SDL_CreateTexture`/`SDL_UpdateTexture`/`SDL_RenderTexture`/`SDL_RenderPresent` plus `SDL_SetRenderLogicalPresentation` and `SDL_SetRenderVSync` — the exact call set PLAT-3 measured in `SKIA` and `BLEND2D`. Note this makes `modules/platform` itself an `SDL_Renderer` user; that is correct, and is why the allowlist is about *renderers*, not about the symbol. |
| PLAT-45 | ~~Dynamic library service (SDL3)~~ | ❌ | **Cut on PLAT-2's evidence.** CNA never calls `SDL_LoadObject`/`LoadFunction`/`UnloadObject` — the only match in the entire tree is one `SDL_FunctionPointer` in a doc comment quoting `SDL_GL_GetProcAddress`'s signature (`GL4Loader.hpp`), and the two renderers that *do* load libraries at run time (`GDI`, `GLIDE`) call `dlopen`/`LoadLibrary` directly. `IPlatformDynamicLibrary` came from `cnaplatform.md`'s sketched class list, not from measured need; building it would have violated design decision "start from the contract CNA needs". The classifier keeps its rules so a future call site is classified rather than falling through. |

---

## Phase 3 — Runtime migration

The first real cut-over. Small blast radius, highest behavioral risk, so it is done early and
guarded by PLAT-6's golden capture.

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-46 | Own the platform instance in `Game` | ⬜ | `PlatformFactory::Create()` once during `Game` construction; lifetime clearly outlives windows, renderer and input. Ownership and teardown order documented. |
| PLAT-47 | Migrate `Game`'s event loop | ⬜ | `SDL_PollEvent` → `platform->PollEvents(batch)`; the `SDL_EVENT_*` switch becomes a `PlatformEvent` switch. Behavior must match PLAT-6 exactly, including the currently-inline quit, focus, resize and gamepad-added handling. |
| PLAT-48 | Migrate `Game`'s timing | ⬜ | Fixed-timestep loop uses the platform timer. `GameTests` pass unchanged. |
| PLAT-49 | Migrate `Game`'s cursor handling | ⬜ | `SDL_ShowCursor`/`SDL_HideCursor` → mouse service. |
| PLAT-50 | Migrate `GameWindow` to `IPlatformWindow` | ⬜ | Remove `struct SDL_Window;` from `GameWindow.hpp`; `window_`, `updateFromSDL()`, `refreshCachedSDLState()`, `queryClientBoundsFromSDL()`, `queryScreenDeviceNameFromSDL()` all re-point at the platform window. |
| PLAT-51 | Replace `GameWindow::GetNativeSdlWindowEXT()` | ⬜ | The public `CNAEXT` accessor returning `SDL_Window*` is replaced by one returning `NativeWindowHandle`. This is a deliberate breaking change to a CNAEXT extension; per `CLAUDE.md`'s "no backward compatibility hacks", no alias is kept. Every in-repo caller is updated in the same task and the change is called out in the commit body. |
| PLAT-52 | Migrate `GraphicsDeviceManager` | ⬜ | Remove `struct SDL_Window;` and `tryGetSDLWindow()` from the header; `SDL_GetPlatform`/`SDL_GetWindowSize` go through the platform. |
| PLAT-53 | Migrate `Logger` | ⬜ | `modules/core/src/Logger.cpp`'s `SDL_Log` calls (4 there, 56 repo-wide) → CNA's own sink, or a platform log service. Prefer the former: logging does not need to be a platform capability. |
| PLAT-54 | Resolve the entrypoint question | ⬜ | Act on PLAT-5: decide and implement whether `Entrypoint.hpp`/`SDL3::SDL3main` becomes a platform concern or stays build-system-only. Android and Windows are the deciding cases. |
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
| PLAT-79 | Migrate `Keyboard` | ⬜ | Snapshot-per-frame model per `cnaplatform.md`'s "input snapshots"; `KeyboardInputTests` pass. |
| PLAT-80 | Migrate `Mouse` | ⬜ | Position, buttons, wheel, relative mode, window association. |
| PLAT-81 | Migrate `MouseCursor` | ⬜ | System and custom cursors, capability-gated. |
| PLAT-82 | Migrate `GamePad` | ⬜ | Including the mapping database and `GamePadMappingTests`. The SDL3-vs-SDL2-vs-SDL1 capability gap noted in `cnaplatform.md` is expressed through capabilities here — but no second implementation is written. |
| PLAT-83 | Migrate joystick support | ⬜ | Legacy joystick path, distinct from gamepad. |
| PLAT-84 | Migrate haptics | ⬜ | `Haptics.cpp`, `HapticDevice.cpp`, `SdlHapticBackend`; capability-gated (`supportsGamepadRumble`). |
| PLAT-85 | Migrate sensors | ⬜ | `SystemSensorBackend`; shares subsystem ownership with `modules/devices` — PLAT-29's refcounting is the contract here. |
| PLAT-86 | Migrate `TouchPanel` and gestures | ⬜ | `TouchPanel.cpp` + `GestureDetector`; `SdlInputBridgeTouchGestureTests` pass. |
| PLAT-87 | Migrate `TextInputEXT` | ⬜ | Text input start/stop, input area, IME; capability-gated (`supportsTextInput`, `supportsIme`). |
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
| PLAT-100 | Migrate `devices-ext` `Clipboard` | ⬜ | Shares the PLAT-88 service; the duplicate implementation is removed, not paralleled. |
| PLAT-101 | Migrate `devices-ext` `PowerInfo` | ⬜ | `SDL_GetPowerInfo` (8 references); shares PLAT-89's service. |
| PLAT-102 | Migrate `DisplayInfo` | ⬜ | Via the displays service. |
| PLAT-103 | Migrate `SdlMessageBoxBackend` | ⬜ | `SDL_ShowMessageBox`/`ShowSimpleMessageBox` → dialogs service; capability-gated. |
| PLAT-104 | Migrate `SdlFileDialogBackend` | ⬜ | Open/save/folder dialogs; capability-gated (`supportsNativeFileDialog`). |
| PLAT-105 | Migrate `SdlTrayBackend` | ⬜ | Tray + menus; capability-gated. |
| PLAT-106 | Migrate `SdlCameraBackend` | ⬜ | `SDL_GetCameras`/`SDL_OpenCamera`; capability-gated. |
| PLAT-107 | Migrate `Locale` / `SystemInfo` / `UrlLauncher` | ⬜ | `SDL_GetPreferredLocales`, `SDL_GetSystemRAM`, `SDL_GetNumLogicalCPUCores`, `SDL_GetPlatform`, `SDL_OpenURL`. |
| PLAT-108 | Migrate `Microsoft::Devices` sensors and vibrate | ⬜ | `Accelerometer`, `Gyroscope`, `SdlSensorSubsystem`, `SdlHapticVibrateBackend`, `SdlSubsystemMutex`. Must preserve the audited shutdown ordering (PLAT-4) — this subsystem has dedicated ordering tests for a reason. |
| PLAT-109 | Migrate `StorageDevice` | ⬜ | `SDL_GetPrefPath` → filesystem service. |
| PLAT-110 | Migrate `TitleContainer` / `TitleLocation` | ⬜ | Base path and file loading. |
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
| PLAT-113 | `HeadlessPlatform` implementation | ⬜ | Full `IPlatform` surface with no video subsystem, no window, no SDL. Pairs naturally with the `HEADLESS` renderer. |
| PLAT-114 | `HeadlessPlatform` capabilities and refusals | ⬜ | Every unsupported capability reports `false` and its calls refuse deterministically — no silent no-ops (rule 3). |
| PLAT-115 | Synthetic event injection for `HeadlessPlatform` | ⬜ | Tests can push `PlatformEvent`s into the queue, which is what lets input, runtime and lifecycle behavior be tested with no display server at all. |
| PLAT-116 | Platform conformance suite | ⬜ | A test suite written *against `IPlatform`*, run against every compiled implementation. Covers lifecycle, window state transitions, event ordering/coalescing, timing monotonicity, capability self-consistency, and error/refusal behavior. This is `cnaplatform.md`'s step 6, pulled earlier because it is what protects the contract from drifting SDL3-shaped. |
| PLAT-117 | Capability self-consistency test | ⬜ | For every capability: if reported `true`, the corresponding call must succeed; if `false`, it must refuse. Mechanically enumerated so a newly added capability cannot be forgotten. |
| PLAT-118 | Event-semantics golden suite | ⬜ | PLAT-6's capture promoted into a cross-implementation golden test. Named risk from `cnaplatform.md`: implementations differing in event semantics, DPI, fullscreen, gamepad or timing behavior. |
| PLAT-119 | Run the CNA test suite under `CNA_PLATFORM=HEADLESS` | ⬜ | Every test that does not genuinely need a display passes with no `DISPLAY`/`WAYLAND_DISPLAY`. Concrete, checkable proof the separation works. |

---

## Phase 9 — Gates, performance, documentation

| ID | Task | Status | Acceptance criteria / Notes |
|---|---|---|---|
| PLAT-120 | Performance verification against PLAT-7 | ⬜ | Re-run the baseline. Target: no regression outside the measured noise floor, consistent with `cnaplatform.md`'s "typically under 0.5 %". A regression above the floor is investigated and fixed, not accepted with a shrug — and if the abstraction turns out to *help* (centralized events, input snapshots), that is reported too. |
| PLAT-121 | Flip the SDL ratchet to hard-error | ⬜ | PLAT-8's warning becomes a configure/build failure. Allowlist: `modules/platform/src/Sdl3/`, `modules/audio`'s SDL3 audio implementation, and the four renderers from PLAT-76. Nothing else may include an SDL header. |
| PLAT-122 | Remove SDL from module link lines | ⬜ | `SDL3::SDL3` disappears from `modules/{core,input,runtime,graphics,devices,devices-ext,storage,content,media,gamer-services}/CMakeLists.txt`. Mechanical, but it is the check that PLAT-121's gate is not passing on a technicality. |
| PLAT-123 | Migrate remaining test/example SDL usage | ⬜ | 316 test/example files reference SDL. Most are incidental; each is either migrated or justified. Tests that legitimately test the SDL3 implementation stay, in `modules/platform/tests/`. |
| PLAT-124 | `docs/platform-abstraction.md` | ⬜ | The capability boundary, the native-handle contract per window system, the performance contract, and how to add an implementation. This document, not this plan, is what a future implementer reads first. |
| PLAT-125 | Update `CLAUDE.md` and `NEXT.md` | ⬜ | Record `CNA_PLATFORM` alongside `CNA_GRAPHICS_RENDERER`, and the rule that new code must not include SDL headers outside the allowlist. Without this the next contributor re-introduces the coupling in good faith. |
| PLAT-126 | CI matrix for `CNA_PLATFORM` | ⬜ | At minimum `SDL3`×(one GPU renderer, one CPU renderer) and `HEADLESS`×`HEADLESS`. Keeps both implementations honest; `cnaplatform.md` names CI-combination growth as one of the real costs, so the matrix is chosen deliberately rather than exhaustively. |

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
| Separate gamepad module | If a second implementation shows the capability model cannot express the SDL1-joystick vs SDL2-GameController vs SDL3-Gamepad gap (design decision 8), the gamepad seam is split out then — on evidence, not in advance. | — | Future — revisit after a second implementation exists |
| Audio implementations (OpenAL, WASAPI, ALSA) | The audio contract (Phase 6) is separate precisely so these need no matching platform implementation. | — | Future — not started |
| `sdl2-compat` / `sdl12-compat` as test configurations | Useful to shake out API-generation assumptions, but they run on SDL3/SDL2 underneath and therefore reach **no** new operating system. They are a test configuration, never a substitute for a real implementation. | — | Future — not started |
| Legacy C++/toolchain profile | Separating SDL3 is necessary but **not sufficient** for genuinely old targets: an older compiler, a language profile below C++23, standard-library restrictions and possibly a C ABI between a modern core and a legacy host are all separate problems. | — | Future — out of scope, own plan required |

The ordering `cnaplatform.md` recommends, for whenever that work starts: prove SDL3 equivalence
first (Phases 0–9 here), then add `Sdl2Platform`, then extend the conformance suite, then design a
limited `Sdl12Platform`, and only last address the legacy C++/toolchain profile.
