# Apple platform support: macOS and iOS

Support for Apple's two platforms, treated as one build/runtime subsystem below every renderer.
The renderer question is separate and stays in `plan_metal.md` / `docs/metal-renderer.md`.

The current contract and validation boundary are maintained in
[`docs/apple-platforms.md`](docs/apple-platforms.md).

## Design decisions

1. **macOS and iOS are one subsystem, not two.** They share a compiler, a sysroot mechanism, a
   bundle format, `SDL_GetPrefPath` semantics and a lifecycle model. Splitting them would
   duplicate every one of those and let the two drift apart. `cmake/ApplePlatform.cmake` is the
   single place that knows which Apple target a configure is for.

2. **iOS is a build-configuration target, and says so everywhere.** Nothing in this lane runs on
   a device or a simulator. Every configure of an iOS renderer prints that it has no runtime
   evidence; the CI leg is named "build only"; the documentation states it twice. The project's
   existing habit of separating "compiles" from "works" is what this follows.

3. **An allow-list, not a deny-list, for iOS renderers.** CNA has 46 renderer identities and
   almost all of them are impossible on iOS. Enumerating the impossible ones would rot the
   moment a renderer is added; enumerating what CNA actually wires up cannot. A renderer outside
   the list fails at configure time with a message naming the platform, instead of failing deep
   inside a third-party dependency that was never configured for an iOS sysroot.

4. **One bundle sweep instead of ~200 edited example registrations.** iOS products must be `.app`
   bundles with an `Info.plist`. The module-local example/tool/test registrations know nothing
   about Apple and should not have to. The top-level `CMakeLists.txt` walks the finished
   buildsystem once and hands every executable target its bundle configuration.

5. **macOS keeps plain executables.** Every example, tool and ctest binary in this repository is
   invoked by path; a `.app` layout would break all of them. Bundling on macOS is available
   behind `CNA_APPLE_BUNDLE_MACOS_EXECUTABLES=ON` for shipping a real application.

6. **The mobile lifecycle deviates from FNA deliberately.** FNA tracks only `IsActive` on the
   background/foreground events. iOS terminates an application that submits GPU work after
   entering the background, so on mobile the loop has to actually stop. The deviation is guarded
   by the compile-time `CNA::isMobilePlatform()`, so desktop behavior is byte-identical to
   before, and it is documented at the site in `Game.cpp` as the checklist requires.

7. **The platform layer is testable without a Mac.** Every line of the Apple CMake code is behind
   `if(APPLE)` and therefore unreachable to most of this project's developers and to every Linux
   CI job. `scripts/check-apple-platform-cmake.sh` parses it in cmake script mode and exercises
   the allow-list decisions, so the layer has a gate that runs on every push rather than only
   when someone happens to configure on macOS.

8. **SDL3 is linked statically on iOS.** A dylib inside an `.app` needs `Frameworks/` embedding,
   an `@rpath` install name and separate signing — none of which the plain `cmake --install` used
   by the vendored SDL build performs.

## Tasks

| ID | Task | Status |
|---|---|---|
| `APPLE-1` | `cmake/ApplePlatform.cmake`: Apple target detection (macOS / iOS device / iOS simulator), deployment-target defaults, bundle identity options, rejection of tvOS/watchOS/visionOS | Done |
| `APPLE-2` | `cmake/toolchains/ios.cmake`: device and simulator toolchain, architecture selection, re-rooted find rules | Done |
| `APPLE-3` | `.app` bundle generation: `cna_apple_configure_bundle()`, the buildsystem-wide sweep, `Info.plist` templates for both platforms | Done |
| `APPLE-4` | iOS renderer allow-list + `CNA_APPLE_ALLOW_UNVALIDATED_RENDERER` escape hatch, called from `cmake/RendererSelection.cmake` | Done |
| `APPLE-5` | Platform-conditional build surface: iOS-keyed SDL prebuilt root, static SDL on iOS, Apple sysroot/architecture propagation into the vendored SDL sub-builds, FFmpeg off on iOS, multi-process tests excluded on iOS | Done |
| `APPLE-6` | `CNA/Platform.hpp`: `CNA_PLATFORM_APPLE`/`_MACOS`/`_IOS` macros, `isApplePlatform()`, `isMobilePlatform()`, `getCurrentPlatformName()` | Done |
| `APPLE-7` | Mobile application lifecycle in `Game`: suspend the loop between background/foreground, restart timing on resume, handle `SDL_EVENT_TERMINATING` and `SDL_EVENT_LOW_MEMORY` | Done |
| `APPLE-8` | `CNA/Entrypoint.hpp`: pull in `<SDL3/SDL_main.h>` on iOS so UIKit owns the process | Done |
| `APPLE-9` | `.github/workflows/apple-ci.yml`: host-portable CMake-layer check, macOS build + portable suites, iOS device/simulator cross-compile with a Mach-O platform check | Done — only the CMake-layer leg has run |
| `APPLE-9a` | `scripts/check-apple-platform-cmake.sh`: exercises the Apple CMake layer from any host, since every line of it is behind `if(APPLE)` and unreachable otherwise | Done — passes on Linux |
| `APPLE-10` | `modules/core/tests/CNA/PlatformTests.cpp`: platform-helper coverage, including the macOS/iOS-specific expectations | Done |
| `APPLE-11` | `METAL` on iOS: refused by default, configurable through `CNA_APPLE_ALLOW_UNVALIDATED_RENDERER` | Done (gate only) |
| `APPLE-12` | Storage-root fallback follows the Apple convention instead of the Linux XDG layout | Done |
| `APPLE-15` | `GraphicsDeviceManager.SupportedOrientations` reaches the OS on mobile through `SDL_HINT_ORIENTATIONS`, bounded by the bundle's `UISupportedInterfaceOrientations` | Done |

## Verification status

| Claim | Evidence |
|---|---|
| The Apple CMake layer parses and its allow-list behaves | `cmake -P` smoke run of `cmake/ApplePlatform.cmake` on Linux: allow-listed renderer, refused renderer under the escape hatch, and both non-Apple no-op paths |
| Non-Apple configures are unaffected | Linux `HEADLESS` configure completes, the whole `CnaTests` corpus builds, and `PlatformTest.*` / `GameWindowTest.*` / `Storage*` / `*DesktopOS*` pass (24 cases; the one unrelated pre-existing failure, `GameWindowTest.MinimizeAndRestoreEXT_UsingSdlWindow`, only appears under `SDL_VIDEODRIVER=dummy`, which rejects minimize/restore, and skips otherwise) |
| macOS builds and its platform contracts hold | `apple-ci.yml` `macos-build` — **not yet executed** |
| CNA cross-compiles for iOS device and simulator | `apple-ci.yml` `ios-build` — **not yet executed** |
| A CNA application runs on iOS | **None. Not attempted.** |

The orientation-hint mapping (`APPLE-15`) was written against SDL's documented
`SDL_HINT_ORIENTATIONS` token set. The FNA reference tree was not present in the environment this
work was done in, so the mapping has not been diffed against FNA's own equivalent; if FNA orders
or names the tokens differently, that is the place to reconcile.

## Open work

These are unstarted, not attempted-and-failed:

- `APPLE-13` — run a CNA example on the iOS simulator and record what actually happens. This is
  the task that would turn iOS from a build target into a supported one; everything below depends
  on it.
- `APPLE-14` — exercise touch on iOS. The path already exists and is platform-neutral
  (`SdlInputBridge` translates SDL finger events into `TouchPanel`/`GestureDetector`, and
  `GraphicsDevice` keeps the display metrics in sync), so this is verification work, not
  implementation work — but until it runs, iOS input is unobserved and iOS has no
  keyboard/mouse fallback to hide behind.
- `APPLE-16` — app icons / asset catalog, `.ipa` packaging, signing documentation.
- `APPLE-17` — decide whether `METAL` becomes the supported iOS renderer, which requires the
  Metal renderer's own macOS contract gaps (`plan_metal.md`) to be closed first.
- `APPLE-18` — Mac Catalyst / tvOS, if ever wanted. Currently rejected at configure time on
  purpose, so that nothing pretends to target them.
- `APPLE-19` — safe-area insets. The notch and the home indicator are not exposed to the game, so
  full-screen UI can sit underneath them. `SDL_GetWindowSafeArea` is the obvious source.
