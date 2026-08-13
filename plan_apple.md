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

2. **iOS support is experimental and every claim names its evidence.** The workflow final-links a
   real application for device and simulator and runs one framework frame in the simulator. That
   is initialization/runtime smoke evidence, not physical-device, pixel, input, audio, storage or
   performance evidence. The project's existing habit of separating "compiles" from "works" is
   what this follows.

3. **An allow-list, not a deny-list, for iOS renderers.** CNA has 46 renderer identities and
   almost all of them are impossible on iOS. Enumerating the impossible ones would rot the
   moment a renderer is added; enumerating what CNA actually wires up cannot. A renderer outside
   the list fails at configure time with a message naming the platform, instead of failing deep
   inside a third-party dependency that was never configured for an iOS sysroot.

4. **One repository-owned bundle sweep instead of ~200 edited example registrations.** iOS products must be `.app`
   bundles with an `Info.plist`. The module-local example/tool/test registrations know nothing
   about Apple and should not have to. The top-level `CMakeLists.txt` walks the finished
   CNA buildsystem once and hands every CNA-owned executable its bundle configuration. A
   downstream target created outside CNA's source tree calls `cna_apple_configure_bundle()`
   explicitly; the helper resolves plist paths relative to CNA, not the outer source tree.

5. **macOS keeps plain executables.** Every example, tool and ctest binary in this repository is
   invoked by path; a `.app` layout would break all of them. Bundling on macOS is available
   behind `CNA_APPLE_BUNDLE_MACOS_EXECUTABLES=ON` for shipping a real application. The opt-in
   bundle copies non-system dylibs into `Contents/Frameworks` and fixes their install names.

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

8. **All SDL components are linked statically on iOS.** SDL3, SDL3_image and SDL3_mixer each have
   independent shared/static switches. A dylib inside an `.app` needs `Frameworks/` embedding, an
   `@rpath` install name and separate signing, so all three are forced static and CI rejects a
   final app with a dynamic SDL dependency.

9. **Deployment floors follow the libraries actually used.** Floating-point `std::to_chars` in
   CNA/sharp-runtime is unavailable in Apple libc++ before macOS 13.3 and iOS 16.3. Those versions
   are hard floors; suppressing availability diagnostics would create older binaries with missing
   runtime symbols.

## Tasks

| ID | Task | Status |
|---|---|---|
| `APPLE-1` | `cmake/ApplePlatform.cmake`: Apple target detection (macOS / iOS device / iOS simulator), deployment-target defaults, bundle identity options, rejection of tvOS/watchOS/visionOS | Done |
| `APPLE-2` | `cmake/toolchains/ios.cmake`: device and simulator toolchain, architecture selection, re-rooted find rules | Done |
| `APPLE-3` | `.app` bundle generation: downstream-safe helper, CNA-owned target sweep, plist templates, macOS dylib embedding/fixup plus bundled-app launch/dependency CI | Done |
| `APPLE-4` | Conservative iOS renderer allow-list (`SDL_RENDERER` only) + `CNA_APPLE_ALLOW_UNVALIDATED_RENDERER` escape hatch | Done |
| `APPLE-5` | Platform-conditional build surface: deployment/architecture/sysroot-keyed SDL cache, static SDL3/image/mixer, Apple toolchain propagation, FFmpeg off on iOS, multi-process tests excluded | Done |
| `APPLE-6` | `CNA/Platform.hpp`: `CNA_PLATFORM_APPLE`/`_MACOS`/`_IOS` macros, `isApplePlatform()`, `isMobilePlatform()`, `getCurrentPlatformName()` | Done |
| `APPLE-7` | Mobile application lifecycle in `Game`: suspend/resume timing, termination/low-memory handling, focused event-state test | Done |
| `APPLE-8` | `CNA/Entrypoint.hpp`: pull in `<SDL3/SDL_main.h>` on iOS so UIKit owns the process | Done |
| `APPLE-9` | `.github/workflows/apple-ci.yml`: host-portable checks, macOS build/tests, final-linked iOS device/simulator app validation, simulator install/launch | Done — [run 31736845749](https://github.com/openeggbert/cna/actions/runs/31736845749) |
| `APPLE-9a` | `scripts/check-apple-platform-cmake.sh`: exercises the Apple CMake layer from any host, since every line of it is behind `if(APPLE)` and unreachable otherwise | Done — passes on Linux |
| `APPLE-10` | `modules/core/tests/CNA/PlatformTests.cpp`: platform-helper coverage, including the macOS/iOS-specific expectations | Done |
| `APPLE-11` | `METAL` on iOS: refused by default, configurable through `CNA_APPLE_ALLOW_UNVALIDATED_RENDERER` | Done (gate only) |
| `APPLE-12` | Storage-root fallback follows the Apple convention instead of the Linux XDG layout | Done |
| `APPLE-13` | Minimal `CNA/Entrypoint` + `Game::RunOneFrame()` app is final-linked for both Apple platforms and launched from macOS bundle/simulator CI | Done — [run 31736845749](https://github.com/openeggbert/cna/actions/runs/31736845749) |
| `APPLE-15` | Initial orientation hint is set before SDL video init; later `SupportedOrientations` changes invalidate UIKit's cached answer | Done |
| `APPLE-20` | Enforce macOS 13.3 / iOS 16.3 libc++ availability floors and request only CNA's sharp-runtime component closure | Done |

## Verification status

| Claim | Evidence |
|---|---|
| The Apple CMake layer parses and its policies stay connected | Linux smoke check: renderer gate/override, non-Apple no-ops, deployment floors, all static SDL switches, bundle fixup and orientation bridge |
| Non-Apple configures are unaffected | Linux `HEADLESS` configure completes, the whole `CnaTests` corpus builds, and `PlatformTest.*` / `GameWindowTest.*` / `Storage*` / `*DesktopOS*` pass (24 cases; the one unrelated pre-existing failure, `GameWindowTest.MinimizeAndRestoreEXT_UsingSdlWindow`, only appears under `SDL_VIDEODRIVER=dummy`, which rejects minimize/restore, and skips otherwise) |
| macOS builds and its platform contracts hold | Green `macos-build` in [run 31736845749](https://github.com/openeggbert/cna/actions/runs/31736845749): CnaTests, focused suites, self-contained bundle verification and launch |
| CNA final-links an iOS device application | Green `ios-build/device` in the same run: Mach-O platform, plist, entry-point symbols and dependency-closure checks |
| A minimal CNA application runs in the iOS simulator | Green `ios-build/simulator` in the same run: install, launch and `CNA_APPLE_SMOKE_OK` after one `Game` frame |
| A CNA application runs on physical iPhone/iPad hardware | **None. Not attempted.** |

## Open work

These are unstarted, not attempted-and-failed:

- Run a representative content-bearing CNA game/example for multiple frames in the iOS simulator;
  the current smoke app deliberately exits after one empty framework frame.
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
