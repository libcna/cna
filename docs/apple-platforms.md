# Apple platforms: macOS and iOS

CNA targets two Apple platforms, and they are not equally supported. This document states what
each one is, how to build it, and — most importantly — what evidence exists behind each claim.

| Target | CMake identity | Status |
|---|---|---|
| macOS (Apple silicon and Intel) | `CMAKE_SYSTEM_NAME=Darwin` | Supported desktop target with a CI gate |
| iOS / iPadOS device | `CMAKE_SYSTEM_NAME=iOS`, `iphoneos` sysroot | Build-configuration target; no runtime evidence |
| iOS simulator | `CMAKE_SYSTEM_NAME=iOS`, `iphonesimulator` sysroot | Build-configuration target; no runtime evidence |
| tvOS / watchOS / visionOS | — | Rejected at configure time |

The corresponding tasks are `APPLE-1`…`APPLE-15` in [`plan_apple.md`](../plan_apple.md), which
also lists what is deliberately left undone.

## Evidence boundary

Read this before quoting anything below as "CNA supports iPhone".

**macOS** builds natively, runs the portable test suites, and has two GitHub Actions gates: the
Apple workflow (`.github/workflows/apple-ci.yml`, `SDL_RENDERER` plus the platform/storage
suites) and the older Metal workflow (`.github/workflows/metal-macos-ci.yml`). The Metal
renderer's own supported contract is narrower than "it builds" — see
[`docs/metal-renderer.md`](metal-renderer.md).

**iOS** support is *build configuration*. What exists is: a toolchain file, an iOS-aware vendored
SDL3 build, `.app` bundle generation with a generated `Info.plist`, the SDL `main()` rename
UIKit requires, application-lifecycle handling in the game loop, an iOS renderer allow-list, and
a CI leg that cross-compiles the library for device and simulator. What does **not** exist: any
run of a CNA application on an iPhone, an iPad, or the simulator; any pixel, input, audio,
storage or performance observation on iOS; any signed, installed build. No claim on this page
should be read as saying otherwise, and `plan_apple.md` records the same boundary per task.

The first execution of the `ios-build` CI legs is itself the compile evidence — until a run is
green, even "it cross-compiles" is a design intent rather than a fact.

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
| `CNA_MACOS_DEPLOYMENT_TARGET` | `11.0` | Seeds `CMAKE_OSX_DEPLOYMENT_TARGET`. The hard floor is 10.15, where the system libc++ gained the C++17 filesystem symbols CNA uses unconditionally; 11.0 is chosen because it is also the first release covering both Intel and Apple silicon. |
| `CNA_APPLE_BUNDLE_MACOS_EXECUTABLES` | `OFF` | When `ON`, executables become `.app` bundles. Off by default because every example, tool and ctest binary in this repository is invoked by path. |
| `CNA_APPLE_BUNDLE_IDENTIFIER_PREFIX` | `com.openeggbert.cna` | Generated `CFBundleIdentifier` is `<prefix>.<target-name-with-dashes>`. |

FFmpeg (VideoPlayer) is available on macOS through Homebrew and is detected by `pkg-config`,
exactly as on Linux.

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
| `CNA_IOS_DEPLOYMENT_TARGET` | `13.0` | Seeds `CMAKE_OSX_DEPLOYMENT_TARGET`. iOS 13 is the mobile counterpart of the macOS 10.15 libc++ boundary. |
| `CNA_APPLE_DEVELOPMENT_TEAM` | *(empty)* | Apple Developer Team ID. Empty disables code signing entirely — fine for the simulator and CI, not installable on a device. |
| `CNA_APPLE_ALLOW_UNVALIDATED_RENDERER` | `OFF` | Downgrades the iOS renderer allow-list from a hard error to a warning. |

### What the iOS build does differently

- **SDL3 is linked statically.** A dylib inside an `.app` needs to be embedded in `Frameworks/`,
  given an `@rpath` install name, and signed separately. Static linking keeps the product a
  single Mach-O executable. The persistent SDL install root is keyed by sysroot
  (`.sdl-prebuilt-iOS-arm64` vs `.sdl-prebuilt-iOS-arm64-simulator`), because device and
  simulator binaries are not interchangeable even at the same architecture.
- **Every executable becomes an `.app` bundle** with a generated `Info.plist`
  (`cmake/AppleInfo.iOS.plist.in`). The plist declares an empty `UILaunchScreen`; without a
  launch screen declaration UIKit refuses to give the app the full screen and hands it the
  device's compatibility resolution, silently changing every backbuffer size.
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
| `HEADLESS`, `SOFTWARE`, `STUB` | Allowed. No GPU or window API involved. |
| `SDL_RENDERER` | Allowed. SDL3's own 2D renderer, Metal-backed on iOS. The default choice. |
| `SDL_GPU` | Allowed. SDL3's GPU API, Metal-backed on iOS. |
| `OPENGLES2`, `OPENGLES3` | Allowed. EasyGL over an SDL GL ES context. OpenGL ES is deprecated by Apple but present. |
| `METAL` | Refused by default. It is the natural iOS renderer, but its supported contract covers macOS only; `-DCNA_APPLE_ALLOW_UNVALIDATED_RENDERER=ON` lets you configure it anyway, unsupported. |
| Everything else | Refused. Desktop OpenGL, Direct3D, GDI, Glide and the browser DOM renderers cannot exist on iOS, and the third-party-backed ones (Skia, Wicked, Diligent, bgfx, MoltenVK/Vulkan, wgpu-native, LLGL, sokol, Magnum, FNA3D) have never been configured for an iOS sysroot. |

"Allowed" means the configure accepts it and CI compiles it. It does not mean pixels have been
observed. `CNA_APPLE_ALLOW_UNVALIDATED_RENDERER=ON` downgrades the refusal to a warning for
experimentation; expect build failures, and nothing about such a configuration is supported.

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
unchanged.

### Touch input

Touch already reaches the XNA API on any platform SDL reports fingers on: `SdlInputBridge`
translates `SDL_EVENT_FINGER_DOWN`/`_MOTION`/`_UP`/`_CANCELED` into `TouchPanel` locations and
feeds `GestureDetector`, and `GraphicsDevice` keeps `TouchPanel`'s display metrics in sync with
the backbuffer. None of that is iOS-specific and none of it needed changing — but equally, none
of it has ever been exercised against a real iOS touch screen.

### Display orientation

`GraphicsDeviceManager.SupportedOrientations` reaches the operating system on mobile:
`GameWindow::SetSupportedOrientations` publishes the requested set through
`SDL_HINT_ORIENTATIONS`, which SDL's UIKit view controller consults whenever UIKit asks which
orientations the application accepts. iOS intersects that with the
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
  on a non-Apple host. Run it locally on any platform — it is the only part of the Apple layer a
  Linux or Windows developer can exercise at all.
- **`macos-build`** — macOS 14 runner, `SDL_RENDERER`, builds `CnaTests` and runs the platform,
  window, storage and desktop-OS suites (dummy audio driver, real video driver). A real gate.
- **`ios-build` (device, simulator)** — cross-compiles the module archives for both sysroots and
  reads the Mach-O build-version load command back off `libcna_core.a` to prove the artifact
  really targets iOS rather than the host. Build-only.

`.github/workflows/metal-macos-ci.yml` keeps its own separate Metal renderer gate.

## Known gaps

These are gaps, not bugs — nothing below has been attempted and failed:

- No iOS runtime, pixel, input, audio or storage evidence of any kind. Touch and orientation are
  wired end to end, but "wired" is not "observed working on a device".
- No safe-area handling: the notch/home-indicator insets are not exposed to the game, so
  full-screen UI can sit under them.
- No app-icon or asset-catalog generation, no `.ipa` packaging, no App Store metadata.
- `METAL` is refused on iOS by default even though it is the platform's native API.
- No Mac Catalyst, tvOS, watchOS or visionOS target.
