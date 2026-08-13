# Apple platforms: macOS and iOS

CNA targets two Apple platforms, and they are not equally supported. This document states what
each one is, how to build it, and — most importantly — what evidence exists behind each claim.

| Target | CMake identity | Status |
|---|---|---|
| macOS (Apple silicon; Intel configurable) | `CMAKE_SYSTEM_NAME=Darwin` | arm64 native build/test CI; x86_64 not CI-verified |
| iOS / iPadOS device | `CMAKE_SYSTEM_NAME=iOS`, `iphoneos` sysroot | Experimental; final-linked app build, no device run |
| iOS simulator | `CMAKE_SYSTEM_NAME=iOS`, `iphonesimulator` sysroot | Experimental; one-frame app smoke run |
| tvOS / watchOS / visionOS | — | Rejected at configure time |

The corresponding tasks are `APPLE-1`…`APPLE-15` in [`plan_apple.md`](../plan_apple.md), which
also lists what is deliberately left undone.

## Evidence boundary

Read this before quoting anything below as "CNA supports iPhone".

**macOS arm64** builds natively, runs the portable test suites, and has two GitHub Actions gates: the
Apple workflow (`.github/workflows/apple-ci.yml`, `SDL_RENDERER` plus the platform/storage
suites) and the older Metal workflow (`.github/workflows/metal-macos-ci.yml`). The Metal
renderer's own supported contract is narrower than "it builds" — see
[`docs/metal-renderer.md`](metal-renderer.md).
The CMake layer keys vendored dependencies by requested architecture and permits x86_64/universal
builds, but the current hosted workflow runs on Apple silicon and is not Intel evidence.

The concrete evidence for the claims on this page is the green Apple workflow
[run 31736845749](https://github.com/openeggbert/cna/actions/runs/31736845749) for commit
`be4ea08bc`: macOS 14 arm64, an arm64 iOS device build, and an arm64 iOS Simulator build/run.

**iOS** support is experimental. What exists is: a toolchain file; an iOS-aware, static vendored
SDL3/SDL3_image/SDL3_mixer build; `.app` bundle generation with a generated `Info.plist`; the SDL
`main()` bridge UIKit requires; application lifecycle and orientation handling; a conservative
renderer allow-list; and `cna_ios_smoke`, a real application that constructs `Game` and runs one
frame. CI final-links that app for device and simulator, checks its Mach-O platform, entry-point
symbols and dynamic dependencies, then installs and launches the simulator build.

What does **not** exist is a run on a physical iPhone/iPad, pixel correctness, real touch,
audio, storage or performance evidence, an App Store package, or production signing. A green
one-frame simulator smoke test proves initialization/event/update/draw/present returns without
an exception; it does not prove those unobserved features.

## Building for macOS

Nothing special is required beyond the normal prerequisites (`sharp-runtime` cloned as a sibling
checkout, submodules initialized):

```bash
brew install ccache ffmpeg
cmake -S . -B cmake-build-macos -DCNA_GRAPHICS_RENDERER=SDL_RENDERER
cmake --build cmake-build-macos --parallel 4
```

macOS-specific defaults, all overridable:

| Option | Default | Meaning |
|---|---|---|
| `CNA_MACOS_DEPLOYMENT_TARGET` | `13.3` | Seeds `CMAKE_OSX_DEPLOYMENT_TARGET` and is the supported floor. CNA/sharp-runtime use floating-point `std::to_chars`, which Apple libc++ marks unavailable before macOS 13.3. |
| `CNA_APPLE_BUNDLE_MACOS_EXECUTABLES` | `OFF` | When `ON`, repository executables become `.app` bundles and non-system dylibs are copied into `Contents/Frameworks` with fixed install names. Off by default because examples/tools/tests are normally invoked by path. |
| `CNA_APPLE_BUNDLE_IDENTIFIER_PREFIX` | `com.openeggbert.cna` | Generated `CFBundleIdentifier` is `<prefix>.<target-name-with-dashes>`. |

FFmpeg (VideoPlayer) is available on macOS through Homebrew and is detected by `pkg-config`,
exactly as on Linux.

An application consuming CNA with `add_subdirectory()` is not part of CNA's repository-owned
bundle sweep. Configure its executable explicitly:

```cmake
add_subdirectory(path/to/cna)
add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE CNA SHARP_RUNTIME)
cna_apple_configure_bundle(my_game)
```

The helper uses paths relative to CNA itself, not the outer project's `CMAKE_SOURCE_DIR`. On
macOS it takes effect only with `CNA_APPLE_BUNDLE_MACOS_EXECUTABLES=ON`; sign a distributable
bundle after the post-build dependency fixup. On iOS the call is mandatory, and the translation
unit containing `main()` must include `CNA/Entrypoint.hpp` before SDL headers so SDL can hand
process startup to UIKit.

## Building for iOS

Requires a macOS host with Xcode installed.

```bash
# Device (arm64)
cmake -S . -B cmake-build-ios \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ios.cmake \
      -DCNA_GRAPHICS_RENDERER=SDL_RENDERER \
      -DCNA_BUILD_TESTS=OFF -DCNA_BUILD_EXAMPLES=OFF
cmake --build cmake-build-ios --parallel 4

# Simulator (arm64 on Apple silicon, x86_64 on Intel)
cmake -S . -B cmake-build-ios-sim \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ios.cmake \
      -DCNA_IOS_SIMULATOR=ON \
      -DCNA_GRAPHICS_RENDERER=SDL_RENDERER \
      -DCNA_BUILD_TESTS=OFF -DCNA_BUILD_EXAMPLES=OFF
```

Use the Xcode generator (`-G Xcode`) when you need to sign and deploy to a physical device:
signing is expressed through `XCODE_ATTRIBUTE_*` target properties, which only Xcode consumes.
Set `-DCNA_APPLE_DEVELOPMENT_TEAM=<TEAMID>`; without it, code signing is disabled and the
product builds but cannot be installed on a device.

iOS-specific defaults:

| Option | Default | Meaning |
|---|---|---|
| `CNA_IOS_SIMULATOR` | `OFF` | Selects the `iphonesimulator` sysroot and the host architecture instead of `iphoneos`/arm64. |
| `CNA_IOS_DEPLOYMENT_TARGET` | `16.3` | Seeds `CMAKE_OSX_DEPLOYMENT_TARGET` and is the supported floor. Floating-point `std::to_chars` is unavailable in Apple libc++ before iOS 16.3. |
| `CNA_APPLE_DEVELOPMENT_TEAM` | *(empty)* | Apple Developer Team ID. Empty disables code signing entirely — fine for the simulator and CI, not installable on a device. |
| `CNA_APPLE_ALLOW_UNVALIDATED_RENDERER` | `OFF` | Downgrades the iOS renderer allow-list from a hard error to a warning. |
| `CNA_BUILD_APPLE_SMOKE_APP` | `ON` | Builds the final-linked one-frame `cna_ios_smoke.app`; keep it enabled when validating an iOS toolchain. On macOS it defaults off and creates `cna_macos_smoke.app` when enabled. |

### What the iOS build does differently

- **SDL3, SDL3_image and SDL3_mixer are linked statically.** A dylib inside an `.app` needs to be embedded in `Frameworks/`,
  given an `@rpath` install name, and signed separately. Static linking keeps the product a
  single Mach-O executable. The persistent SDL install root is keyed by sysroot
  (`.sdl-prebuilt-iOS-arm64` vs `.sdl-prebuilt-iOS-arm64-simulator`), because device and
  simulator binaries are not interchangeable even at the same architecture.
- **Every executable becomes an `.app` bundle** with a generated `Info.plist`
  (`cmake/AppleInfo.iOS.plist.in`). The plist declares an empty `UILaunchScreen`; without a
  launch screen declaration UIKit refuses to give the app the full screen and hands it the
  device's compatibility resolution, silently changing every backbuffer size.
- **A real smoke application is built by default.** `cna_ios_smoke` links the complete selected
  renderer, includes `CNA/Entrypoint.hpp`, selects a landscape orientation and runs one `Game`
  frame. Disable it only with `-DCNA_BUILD_APPLE_SMOKE_APP=OFF`.
- **`main()` is renamed** by `CNA/Entrypoint.hpp`, which pulls in `<SDL3/SDL_main.h>` on iOS for
  the same reason it already did on Android: UIKit owns the process, and SDL's own `main()` has
  to run `UIApplicationMain` before the game's `main()` is called. A game that does not include
  `CNA/Entrypoint.hpp` never gets a `UIApplication`, and therefore no window and no events.
- **FFmpeg is disabled** (`CNA_FFMPEG_AVAILABLE=OFF`): `pkg-config` on a macOS host resolves to
  Homebrew's *macOS* FFmpeg, which cannot be linked into an iOS binary. `VideoPlayer` and the
  rest of the video surface are therefore absent from an iOS build, exactly as on Android,
  Emscripten and Windows.
- **Multi-process tests are excluded** from `CnaTests`. An app-sandboxed process may not spawn
  another executable, and the harness paths those tests bake in are build-machine absolute paths
  that do not exist inside the `.app`.

### Renderers on iOS

Only renderers CNA actually wires up for iOS may be selected; anything else fails at configure
time with a readable message instead of somewhere deep inside a dependency build.

| Renderer | On iOS |
|---|---|
| `SDL_RENDERER` | Allowed. SDL3's own 2D renderer, Metal-backed on iOS. It is the only renderer built and final-linked by Apple CI. |
| `SDL_GPU` | Refused by default. Its build currently requires a target-compatible shaderc dependency that the iOS workflow does not provide. |
| `OPENGLES2`, `OPENGLES3` | Refused by default. They require the sibling `easy-gl` and `meta-gl` repositories, which the iOS workflow does not provide or validate. |
| `HEADLESS`, `SOFTWARE`, `STUB` | Refused by default. They may be useful for experiments, but they are not final-linked by Apple CI and therefore are not advertised as supported iOS configurations. |
| `METAL` | Refused by default. The renderer's supported contract covers macOS only. |
| Everything else | Refused. Desktop APIs cannot exist on iOS; other third-party-backed renderers have not been configured for an iOS sysroot. |

"Allowed" means configure accepts it, CI final-links it for both iOS sysroots, and the simulator
smoke app launches one frame. It does not mean correct pixels have been observed.
`CNA_APPLE_ALLOW_UNVALIDATED_RENDERER=ON` downgrades any refusal to a warning for experimentation;
expect build or runtime failures, and nothing about such a configuration is supported.

## Runtime behavior on Apple platforms

### Platform identification

`CNA/Platform.hpp` answers the compile-time questions:

```cpp
CNA::getCurrentPlatform();      // Platform::Desktop on macOS, Platform::iOS on iOS
CNA::isApplePlatform();         // true on both
CNA::isMobilePlatform();        // true on iOS and Android
CNA::getCurrentPlatformName();  // "macOS", "iOS", "Linux", "Windows", "Android", "Web"
CNA::getCurrentDesktopOS();     // DesktopOS::MacOSX on macOS; throws on iOS
```

The `CNA_PLATFORM_APPLE`, `CNA_PLATFORM_MACOS` and `CNA_PLATFORM_IOS` macros are defined by the
same header for preprocessor conditions. macOS deliberately reports `Platform::Desktop` — it is
a desktop — so `isApplePlatform()` is the query that spans both Apple targets.

### Application lifecycle

iOS terminates an application that submits GPU work after entering the background, and Android
destroys the rendering surface at the same moment. `Game`'s loop therefore stops between the
operating system's "did enter background" and "will enter foreground" notifications: it blocks
on the SDL event queue instead of ticking, and neither updates nor draws. This is a deliberate
deviation from FNA, which tracks only `IsActive` on those events — it is documented in
`Game.cpp` at the site and covered by `plan_apple.md` APPLE-7.

On resume the performance counter is restarted, so the first frame after a resume measures only
itself rather than the whole background period. `SDL_EVENT_TERMINATING` ends the loop so
`Exiting` still runs; `SDL_EVENT_LOW_MEMORY` is logged as a warning.

The compile-time `isMobilePlatform()` guard means desktop builds keep the plain `Tick()` loop
unchanged. The event-state transitions are also covered by a focused `GameTest` in the macOS CI
suite; the simulator smoke covers the normal one-frame path, not a real OS background/resume.

### Touch input

Touch already reaches the XNA API on any platform SDL reports fingers on: `SdlInputBridge`
translates `SDL_EVENT_FINGER_DOWN`/`_MOTION`/`_UP`/`_CANCELED` into `TouchPanel` locations and
feeds `GestureDetector`, and `GraphicsDevice` keeps `TouchPanel`'s display metrics in sync with
the backbuffer. None of that is iOS-specific and none of it needed changing — but equally, none
of it has ever been exercised against a real iOS touch screen.

### Display orientation

`GraphicsDeviceManager.SupportedOrientations` reaches the operating system on mobile:
before `SDL_INIT_VIDEO`, CNA seeds SDL's complete XNA-default orientation set. Later,
`GameWindow::SetSupportedOrientations` publishes the requested narrower set through
`SDL_HINT_ORIENTATIONS`; an Objective-C++ adapter then asks the existing UIKit view controller to
re-evaluate supported orientations. iOS intersects that result with the
`UISupportedInterfaceOrientations` array in the bundle's `Info.plist`, so the plist is the outer
bound and the hint can only narrow it. On desktop the hint is not set at all and the property
stays CNA-internal bookkeeping, exactly as before.

The *current* orientation continues to be derived from the window bounds on resize, which is what
a rotation produces, so `OrientationChanged` fires without any iOS-specific code.

### Storage locations

`StorageDevice` resolves its root through `SDL_GetPrefPath`, which already returns
`~/Library/Application Support/<app>` on macOS and the app container's equivalent on iOS. Its
fallback path — used only if `SDL_GetPrefPath` fails — now follows the same Apple convention
instead of the Linux XDG layout, which exists on neither Apple platform, and which on iOS would
place saves outside the app container where they would not survive an app update.

Content is loaded relative to `SDL_GetBasePath()`, which is the `.app` bundle's resource
directory on both Apple platforms, so bundled content works without changes.

## Continuous integration

`.github/workflows/apple-ci.yml`:

- **`apple-cmake-check`** — Linux runner, seconds, no Apple hardware. Runs
  `scripts/check-apple-platform-cmake.sh`, which parses `cmake/ApplePlatform.cmake` in cmake
  script mode and asserts that the iOS allow-list accepts an allow-listed renderer, refuses one
  outside it with a message naming the override, honours the override, and stays completely inert
  on a non-Apple host. It also checks all three static SDL switches, deployment floors, the macOS
  dependency fixup and both halves of the orientation bridge. Run it locally on any platform.
- **`macos-build`** — macOS 14 runner, `SDL_RENDERER`, builds `CnaTests` and runs the platform,
  window, lifecycle, storage and desktop-OS suites (dummy audio driver, real video driver). A
  separate build tree enables `.app` bundling, final-links and launches `cna_macos_smoke.app`,
  and rejects dependencies that still point to the SDL build cache or a Homebrew prefix.
- **`ios-build` (device, simulator)** — builds and final-links `cna_ios_smoke.app` for both
  sysroots; validates `Info.plist`, Mach-O platform, `_main`/`_SDL_main`, and the absence of
  dynamic SDL/build-machine dependencies. The simulator leg then boots an available iPhone,
  ad-hoc signs and installs the app, launches it, and requires `CNA_APPLE_SMOKE_OK`.

All four jobs in this workflow are green in
[run 31736845749](https://github.com/openeggbert/cna/actions/runs/31736845749). That run is compile,
bundle and bounded smoke evidence only; it does not expand the feature boundary stated above.

`.github/workflows/metal-macos-ci.yml` keeps its own separate Metal renderer gate.

## Known gaps

These remain outside the verified support boundary:

- No physical iPhone/iPad run, and no pixel, real-touch, audio, storage, background/resume or
  performance observation. The simulator smoke proves only that one framework frame returns.
- No current Intel-macOS CI run; x86_64 and universal builds are configured but unverified.
- No safe-area handling: the notch/home-indicator insets are not exposed to the game, so
  full-screen UI can sit under them.
- No app-icon or asset-catalog generation, no `.ipa` packaging, no App Store metadata.
- `METAL` is refused on iOS by default even though it is the platform's native API.
- No Mac Catalyst, tvOS, watchOS or visionOS target.
