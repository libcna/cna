# The SDL2 platform implementation

`CNA_PLATFORM=SDL2` selects `CNA::Platform::Sdl2::Sdl2Platform`, the first independent native-window
backend added after the original platform campaign. This document is its capability boundary and
its supported build; the contract it implements is described in
[`docs/platform-abstraction.md`](platform-abstraction.md), and the task ledger is §11 of
[`plan_platform.md`](../plan_platform.md).

**It is not `sdl2-compat`.** The backend is written against SDL 2.30's own API and links
`SDL2::SDL2` directly. A compatibility shim running on SDL3 underneath would reach no operating
system SDL3 cannot already reach, which is the whole reason this implementation exists.

---

## What it is for

SDL3 requires a platform SDL3 supports. SDL2 is still the version packaged by older
distributions and still builds for hosts SDL3 has dropped. Adding it is also the first real test
of the claim `plan_platform.md` makes about itself: that `IPlatform` is a contract CNA needed
rather than SDL3's shape with the names changed. A second SDL generation, with a different event
model and a different display API, is the cheapest way to find out — and it did find things (see
*What building it exposed*).

---

## Capability profile

`Sdl2Platform::GetCapabilities()` advertises exactly six flags. Five are *presence* capabilities,
each backed by a service the conformance suite checks is non-null; `exactKeyboardState` is a
*quality* flag on a service that exists either way.

| Capability | Value | Backed by |
|---|---|---|
| `multipleWindows` | true | `CreateWindow` has no single-window slot |
| `highDpi` | true | `Sdl2Window::GetDisplayScale` measures drawable-vs-logical size |
| `multipleDisplays` | true | `GetDisplays()` → `Sdl2Displays` |
| `borderlessFullscreen` | true | `SDL_WINDOW_FULLSCREEN_DESKTOP` |
| `openGlContext` | true | `GetGlContext()` → `Sdl2Platform::GlContext` |
| `exactKeyboardState` | true | the keyboard snapshot is SDL2's own key-state array |

Every other capability is false **and its accessor returns null**, which PLAT-117's
`EveryServiceIsNullExactlyWhenItsCapabilityIsFalse` verifies mechanically for every implementation
compiled into the binary. Nothing here is a stub that accepts work and drops it.

Absent today: mouse, gamepad, joystick, text input, sensors, haptics, input-device enumeration,
clipboard, dialogs, tray, camera, Vulkan surface, surface presentation, power info and
`managedEntrypoint`.

### Consequences a game can observe

- **No native window handle.** `Sdl2Window::GetNativeHandle()` returns
  `NativeWindowSystem::Unknown`, so `HasNativeWindow()` is false and
  `PresentationParameters::DeviceWindowHandle` is left unset. SDL2 *can* answer this through
  `SDL_SysWMinfo`, but that union is deliberately not part of the initial backend contract: every
  window system needs its own typed mapping and its own test, and shipping an untested one would
  be exactly the fabricated capability claim the contract forbids. Until it exists, renderers that
  reach a window system directly — the `DIRECTX*` family, `GDI`, `GLIDE`, `METAL` — cannot run on
  this platform. Renderers that go through `IPlatformGlContext` can, because a GL context is
  identified by `WindowId`, never by a native handle.
- **No surface presentation.** `CreateSurfacePresenter()` throws
  `PlatformNotSupportedException(SurfacePresentation)`, so the CPU-raster renderers that present
  through the platform (`SKIA`, `BLEND2D`) have no path here yet.
- **No mouse or gamepad service.** `Mouse` and `GamePad` see a null service and report their
  documented empty state. Mouse *events* still arrive — motion, buttons and wheel are translated
  into `PlatformEvent` — but there is no per-frame mouse snapshot service, and no cursor,
  warp, relative-mode or capture support.
- **No safe area.** `TryGetSafeAreaForWindow` always returns false. SDL2 has no equivalent of
  SDL3's `SDL_GetWindowSafeArea`, and returning the full client bounds would be a guess presented
  as an answer.
- **Display content scale is 1.0.** `SDL_GetDisplayDPI` reports the panel's physical dot pitch,
  not the desktop's scaling factor; deriving a content scale from it would tell a caller that an
  unscaled 4K desktop is scaled about 1.7×. The genuine high-DPI signal on SDL2 is the per-window
  drawable-to-logical ratio, which `Sdl2Window::GetDisplayScale` measures and reports.

### Display ids are offset by one

SDL2 addresses displays by a dense 0-based index, while `DisplayInfo::id` is an opaque
`std::uint32_t` that `GraphicsAdapter` reads as "no display" when it is zero. `Sdl2Displays`
therefore publishes `index + 1` and maps back on every lookup, which keeps SDL3's own 1-based
`SDL_DisplayID` convention as the contract-wide meaning of the field instead of making callers ask
which platform they are on.

---

## Events

`Sdl2Platform::PollEvents` translates quit, window, keyboard, text-input and mouse events into
`PlatformEvent`. SDL2 delivers all window state changes as one `SDL_WINDOWEVENT` carrying a
sub-code, where SDL3 promoted each sub-code to its own event type; ten of SDL2's sub-codes map onto
`WindowEventKind`, and anything else is dropped rather than guessed at.

Keyboard translation converts SDL2 keycodes (Unicode-based) to CNA's Windows virtual-key values and
SDL2 scancodes to CNA `Scancode`, so an SDL2 build and an SDL3 build deliver the same
platform-neutral values for the same physical key. `cna_platform_sdl2_tests` proves that by
pushing real events onto SDL2's own queue with `SDL_PushEvent` and asserting on what comes out of
`PollEvents`.

---

## Audio

`CNA_AUDIO_PLATFORM=SDL2` is a separate axis and selects `Sdl2AudioDevice`, a private callback
playback device. SDL2 has no SDL3-style audio stream object, but its callback is an exact fit for
`IAudioDevice`: SDL owns the buffer and CNA fills it in place, and `Stop()` takes SDL's device lock
after pausing to give the contract's callback-barrier guarantee.

Two honest limits:

- **Capture is unsupported.** There is no SDL2 recording device; `Microphone` has no backend here.
- **High-level XNA sound is disabled.** `SOUND_ENABLED` is defined only for
  `CNA_AUDIO_PLATFORM=SDL3`, because CNA's decoder/mixer engine is an SDL3_mixer implementation.
  Turning it on for SDL2 would compile the XNA facade against an engine that cannot exist in the
  resulting binary. `SoundEffect`, `SoundEffectInstance`, `MediaPlayer` and the XACT surface
  therefore compile in this profile but do not play; the transport layer (`IAudioDevice`) is real
  and tested. An SDL2_mixer or CNA-native mixer is the open work — see PLAT-SDL2-8.

---

## Renderer compatibility

| Renderer | On SDL2 |
|---|---|
| `OPENGLES2`, `OPENGLES3`, `OPENGL33`, `OPENGL1`, `OPENGL2`, `OPENGL4`, `OPENGLES1` | supported — `IPlatformGlContext` |
| `HEADLESS`, `STUB`, `SOFTWARE`, `PORTABLEGL` | supported — need no window |
| `SDL_RENDERER`, `SDL_GPU`, `FNA3D`, `FREEDIRECT` | **rejected at configure time** when audio is also SDL2 |
| `SKIA`, `BLEND2D` | not yet — need `IPlatformSurfacePresenter` |
| `DIRECTX*`, `GDI`, `GLIDE`, `METAL` | not yet — need a native window handle |
| `VULKAN` | not yet — needs `IPlatformVulkanSurface` |

`cmake/Sdl2OnlyConfiguration.cmake` rejects the four allowlisted SDL3 renderer families when both
`CNA_PLATFORM` and `CNA_AUDIO_PLATFORM` are SDL2, because those families link SDL3 themselves and
would defeat an otherwise SDL2-only binary. That same condition publishes
`CNA_SDL2_ONLY_CONFIGURATION`, which keeps SDL3 off the test and harness link lines too: SDL2 and
SDL3 export identically named entry points (`SDL_Init`, `SDL_GetError`, `SDL_PollEvent` and many
more), so a binary linking both would leave the SDL2 backend's calls bound to whichever library the
loader reached first — and a conformance run over such a binary tests neither implementation while
reporting success.

The "not yet" rows are **not** rejected at configure time, and deliberately so: they already refuse
correctly at run time, naming the capability they need. `VULKAN` reaches
`RequirePlatformVulkanSurface`, which throws `PlatformNotSupportedException(VulkanSurface)` when the
service is null; `SKIA` and `BLEND2D` reach `CreateSurfacePresenter`, which throws
`PlatformNotSupportedException(SurfacePresentation)`. That is the contract's own answer — a false
capability refuses, it does not no-op — so a second, earlier gate would restate it rather than add
anything. `TERMINAL` has a configure-time renderer gate (PLAT-140) for a different reason: several
of the renderers it excludes would otherwise demand an SDK or a driver at *configure* time, long
before any refusal could be reached.

---

## Supported build

SDL2 is fetched only when it is selected, so a default build never downloads a second toolkit.
Point `CNA_SDL2_ROOT` at a local SDL 2.30.x checkout, or leave it empty to use the pinned
`FetchContent` revision in `cmake/ThirdPartySDL2.cmake`.

```sh
cmake -S . -B cmake-build-sdl2 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCNA_PLATFORM=SDL2 \
  -DCNA_AUDIO_PLATFORM=SDL2 \
  -DCNA_GRAPHICS_RENDERER=OPENGLES3 \
  -DCNA_SDL2_ROOT=$HOME/deps/sdl2

cmake --build cmake-build-sdl2 --target CnaTests cna_platform_sdl2_tests cna_audio_sdl2_tests -j3
```

Then run, from the repository root (the fixtures are resolved relative to the working directory):

```sh
# The implementation-neutral contract, now including SDL2 as a parameter.
./cmake-build-sdl2/CnaTests \
  --gtest_filter='EveryImplementation/PlatformConformance.*:EveryImplementation/PlatformWindowConformance.*'

# SDL2's own native-queue translation, and its audio device lifecycle.
SDL_VIDEODRIVER=dummy ./cmake-build-sdl2/cna_platform_sdl2_tests
SDL_AUDIODRIVER=dummy ./cmake-build-sdl2/cna_audio_sdl2_tests
```

`cna_platform_sdl2_tests` and `cna_audio_sdl2_tests` are separate executables on purpose. They are
the two suites that include SDL2's own headers, and SDL2 and SDL3 publish mutually exclusive
interface requirements, so they cannot share a binary with the SDL3-native fixtures that
`CnaTests` still carries in non-SDL2 selections. Both are registered with CTest as
`CnaSdl2PlatformTests` and `CnaSdl2AudioDeviceTests`.

For a windowed run — the GL context path in particular — use a real or virtual display:

```sh
Xvfb :99 -screen 0 1280x720x24 &
DISPLAY=:99 ./cmake-build-sdl2/cna_demo_2d --smoke 6
```

The `SDL2 + OpenGLES3` cell of `.github/workflows/platform-ci.yml` runs exactly this shape.

---

## Verified state

Measured 2026-08-17 in `cmake-build-sdl2` (`CNA_PLATFORM=SDL2`, `CNA_AUDIO_PLATFORM=SDL2`,
`CNA_GRAPHICS_RENDERER=OPENGLES3`), on an Xvfb display rather than the dummy video driver:

| Check | Result |
|---|---|
| Full `CnaTests`, five sandbox-blocked ENet/UDP suites excluded | **6,680 passed / 70 skipped / 0 failed** (6,750 run) |
| Registered platform CTest entries | **4/4** — `CnaPlatformTests`, `CnaPlatformWindowTests`, `CnaSdl2PlatformTests`, `CnaSdl2AudioDeviceTests` |
| Implementation-neutral conformance, SDL2 as a parameter | **87/87** over SDL2, Headless and Terminal |
| GL context end to end | `cna_demo_2d --smoke 6` exits 0 on a real **OpenGL ES 3.2** context (Mesa 25.0.7) |
| Link graph | `ldd` shows `libSDL2-2.0d.so.0` and no SDL3 |

## What building it exposed

A second implementation is worth having partly for what it finds. Four things, all fixed:

1. **`SoundEffectInstance.cpp` did not compile for any non-SDL3 audio selection.** Three pure-XNA
   math helpers — `2^pitch`, the FAudio pan crossfeed matrix and the F3DAudio Doppler ratio — sat
   inside the `SOUND_ENABLED` block, while the public `INTERNAL_calculate*` shims that forward to
   them are compiled unconditionally. `CNA_AUDIO_PLATFORM=SDL2` and `=NULL` alike therefore failed
   to build `cna_audio`, and no gate noticed because no build tree used either value.
2. **`multipleDisplays` was advertised with no display service.** The capability was true while
   `GetDisplays()` returned null — the exact inconsistency PLAT-117 exists to catch, invisible only
   because SDL2 was never a parameter of the conformance suite. `Sdl2Displays` now implements the
   service for real.
3. **The test binary linked both SDL generations.** `CnaTests` linked `SDL3::SDL3`
   unconditionally, so an SDL2-selected build produced a process containing two libraries exporting
   the same symbols. See `CNA_SDL2_ONLY_CONFIGURATION` above.
4. **Every navigation, editing and keypad key mistranslated.** The keycode mapping matched the
   function keys with a single `SDLK_F1 .. SDLK_F24` range, but SDL2 leaves a gap between F12
   (scancode 69) and F13 (scancode 104) and fills it with exactly those keys. `SDLK_LEFT` (scancode
   80) never reached the switch that handles it and came out as `112 + (80 - 58) = 134` — F23. F13
   itself was wrong too, because Windows virtual-key codes have their own gap in the same place.
   Now two ranges, with regression cases driving thirteen keys from inside the old bad range.

---

## Not implemented, and why it is recorded rather than hidden

The absent services above are absent because no task has implemented them, not because SDL2 cannot
do them. SDL2 has a clipboard, a message box, game controllers, haptics and `SDL_SysWMinfo`. Each
one is a separate piece of work with its own translation and its own tests, and the capability
model exists precisely so the backend can be useful before all of them land. The rule this
implementation follows is the contract's own: a capability is false until its service is real, and
an unsupported call refuses rather than silently doing nothing.
