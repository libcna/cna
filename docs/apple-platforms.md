# Apple platforms: macOS and iOS

This page separates build configuration, recorded Apple measurements and current qualification
limits. `CMAKE_SYSTEM_NAME=Darwin` selects macOS; `iOS` with an `iphoneos` or
`iphonesimulator` sysroot selects an iPhone/iPad device or simulator.

| Target | Current boundary |
|---|---|
| macOS arm64 | Native build/test evidence and configured CI gates; renderer-specific limitations remain. |
| macOS x86_64 / universal | Configurable; no current Intel/universal runtime qualification recorded here. |
| iOS device | Experimental application-bundle build support; no physical-device run. |
| iOS Simulator | Experimental SOFTWARE smoke evidence; retired METAL pixel measurements are historical. |
| tvOS / watchOS / visionOS | Rejected by the Apple platform CMake layer. |

`SDLGPU` is the default on macOS and iOS. SDL supplies its Metal backend; CNA's former
`METAL` and `SDL_RENDERER` identities are retired and refused. See
[removed-renderers.md](removed-renderers.md). Default selection and iOS allow-list membership
are configuration facts, not a claim of successful iOS SDLGPU rendering.

## Recorded evidence

The Apple M4 campaign ran native macOS suites and external samples with several renderer families
before the 2026-10-13 retirement. Its Phase 4 full-tree measurements were recorded at
`fdd33d2c6`; later tasks checked C API packaging and mobile behavior. The complete AM4 table,
counts, machine/toolchain details and retired-renderer results remain in this page's Git history.
They must not be read as tests of the current head or selectors that no longer exist.

The earlier platform campaign also recorded device/simulator application builds at `be4ea08bc`
and a final device bundle at `49bdf63b8`. The recorded iOS multi-frame pixel probe
(AM4-120) used the retired METAL renderer. It does not qualify SOFTWARE or SDLGPU pixels today.
The repository currently records SOFTWARE simulator smoke evidence, while full surviving-renderer
Apple requalification and SDLGPU on iOS remain open in
[KNOWN_ISSUES.md](../KNOWN_ISSUES.md#limitations).

The workflow definitions below describe what CI attempts and whether a failure gates the workflow.
They are not a live report of the latest remote workflow result.

## macOS build

Use a macOS host, a compatible modular Sharp Runtime sibling, initialized submodules and dependencies
for the selected configuration. A normal SDLGPU configuration is:

```sh
brew install ccache ffmpeg nlohmann-json
cmake -S . -B cmake-build-macos -DCNA_GRAPHICS_RENDERER=SDLGPU
cmake --build cmake-build-macos --parallel 4
```

FFmpeg is optional through `CNA_ENABLE_VIDEO=AUTO/ON/OFF`. AUTO enables the decoder only when
all target dependencies are found; OFF retains the API without FFmpeg decoding. Installed Homebrew
libraries can impose a newer OS floor than CNA's own minimum. A binary intended for the minimum
OS must use dependencies built for that floor or omit the optional dependency.

| Option | Default / contract |
|---|---|
| `CNA_MACOS_DEPLOYMENT_TARGET` | 13.3; seeds the deployment target. The CMake layer refuses a lower effective macOS target. |
| `CNA_APPLE_BUNDLE_MACOS_EXECUTABLES` | OFF; ON makes CNA-owned executables application bundles and fixes up their non-system dylibs. |
| `CNA_APPLE_BUNDLE_IDENTIFIER_PREFIX` | `com.openeggbert.cna`; used with the target name for bundle identity. |
| `CNA_APPLE_BUNDLE_VERSION` | Defaults to numeric product version; pre-release suffix is omitted for bundle metadata. |

The 13.3 macOS / 16.3 iOS floors are enforced for CNA/Sharp Runtime floating-point
`std::to_chars` availability. They are not evidence that every external SDK or library runs on
those minimum releases.

## Application bundles and entry point

A source-tree consumer configures its own executable explicitly; the CNA-owned target sweep does
not own a parent project's later targets:

```cmake
add_subdirectory(path/to/cna)
add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE CNA SHARP_RUNTIME)
cna_apple_configure_bundle(my_game)
```

On macOS the helper acts when bundling is enabled; it copies/fixes up the non-system dylib closure
under `Contents/Frameworks`. Sign a distributable application after those post-build changes.
The helper resolves templates relative to CNA's own source root.

On iOS the bundle setup is required. The translation unit defining `main()` must include
`CNA/Platform/Entrypoint.hpp` before SDL headers so SDL can provide UIKit startup.
The generated plist includes `UILaunchScreen` and supported orientations. These source-build
helpers do not establish an installed Apple SDK packaging/signing workflow; see
[installed-cpp-sdk.md](installed-cpp-sdk.md) for that separate qualification boundary.

Windowed Cocoa/UIKit games must use the process main thread. Offscreen-capable configurations have
their own platform behavior; do not generalize a headless run into windowed main-thread evidence.

## iOS build

Use a macOS host with Xcode. SOFTWARE is the recorded platform-smoke path, so use it for the
baseline; SDLGPU is allowed but remains separately unqualified:

```sh
# Device.
cmake -S . -B cmake-build-ios \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ios.cmake \
  -DCNA_GRAPHICS_RENDERER=SOFTWARE \
  -DCNA_ENABLE_NET=OFF \
  -DCNA_BUILD_TESTS=OFF -DCNA_BUILD_EXAMPLES=OFF
cmake --build cmake-build-ios --target cna_ios_smoke --parallel 4

# Simulator.
cmake -S . -B cmake-build-ios-sim \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ios.cmake \
  -DCNA_IOS_SIMULATOR=ON \
  -DCNA_GRAPHICS_RENDERER=SOFTWARE \
  -DCNA_ENABLE_NET=OFF \
  -DCNA_BUILD_TESTS=OFF -DCNA_BUILD_EXAMPLES=OFF
cmake --build cmake-build-ios-sim --target cna_ios_smoke --parallel 4
```

The toolchain defaults devices to arm64 and the simulator to the host architecture, using separate
sysroots and SDL cache identities. Explicit `CMAKE_OSX_SYSROOT` / `CMAKE_OSX_ARCHITECTURES`
override defaults; the observed simulator identity follows the effective sysroot.

The baseline disables networking because iOS does not supply the required target libcurl and this
repository has no qualified iOS service transport integration. Host Homebrew curl is not a target
dependency. A separately supplied target library would need its own qualification; the OFF recipe
is not an intrinsic prohibition on all future networking.

iOS builds use static SDL3/SDL3_mixer and final-link an application bundle. FFmpeg AUTO remains
unavailable on iOS and ON refuses this unsupported target integration. The public video surface
still links and reports unavailable decoding through its normal API contract. Multi-process native
test cases are excluded from iOS test builds.

| Option | Default / contract |
|---|---|
| `CNA_IOS_SIMULATOR` | OFF; ON selects simulator defaults. |
| `CNA_IOS_DEPLOYMENT_TARGET` | 16.3; lower effective deployment targets are refused. |
| `CNA_BUILD_APPLE_SMOKE_APP` | ON on iOS, OFF on macOS; builds the corresponding one-frame application target. |
| `CNA_APPLE_DEVELOPMENT_TEAM` | Empty disables signing. Set a valid team and use an Xcode generator for the documented signing target properties. |
| `CNA_APPLE_ALLOW_UNVALIDATED_RENDERER` | OFF; ON downgrades the iOS platform allow-list refusal for experiments. |

Unsigned device bundles are build artifacts, not installable physical-device qualification. There
is no `.ipa`/App Store/signing-product pipeline in this support claim.

### iOS renderer selection

The normal allow-list contains SOFTWARE and SDLGPU. SOFTWARE backs the gating platform-smoke
legs; SDLGPU legs are non-gating. Other selectors are refused by the iOS gate unless its explicit
experimental override is used. That override does not restore globally retired identities or make
desktop/browser APIs available on iOS.

The simulator workflow recognizes exactly SDL's Metal-device hardware-requirement refusal for
SDLGPU and records it without running the pixel probe. A workflow leg accepting that refusal is
not positive rendering evidence. Successful allow-list/configure or final link alone is not a
device/pixel result.

## Runtime behavior

`CNA/TargetPlatform.hpp` reports Desktop for macOS, iOS for iOS, and provides `isApplePlatform()`,
`isMobilePlatform()` and platform-name helpers. `CNA_TARGET_*` platform identities are separate
from the selected `CNA_PLATFORM_*` implementation.

The mobile blocking Run loop waits while suspended. Current event handling sets suspension on
**WillEnterBackground** and clears it on **DidEnterForeground**; resume resets timing and invalidates
the surface. Terminating requests Exit and clears suspension; low-memory events log a warning.
This corrects the older reversed event-pair description. The injected
`GameTest.MobileLifecycleEventsSuspendResumeAndTerminateTheLoop` is a state-machine regression,
not a physical iOS background/resume observation.

SDL finger down/motion/up/cancel events feed TouchPanel and gesture detection through the shared
input bridge. Actual iPhone/iPad touch behavior remains unmeasured.

SupportedOrientations reaches SDL's mobile orientation hint and the UIKit adapter; the bundle's
plist bounds the allowed set. Current orientation comes from resize/window geometry.
The pinned SDL UIKit scene-window-frame patch preserves the actual scene bounds during rotation;
its earlier simulator pixel evidence does not substitute for a current physical rotation test.

Storage resolves independently of the windowing platform: desktop policy checks XDG_DATA_HOME,
then LOCALAPPDATA, then the platform's home-data location (Library/Application Support on Apple),
with a working-directory fallback. On iOS the normal home is the application container.
Root overrides, tests and already-open containers follow the storage contract in
[c-api/STORAGE.md](c-api/STORAGE.md). Bundled content uses the platform's application base path;
the application must supply/package its assets.

## CI definitions

[apple-ci.yml](../.github/workflows/apple-ci.yml) currently declares:

| Job | What it attempts / gating rule |
|---|---|
| `apple-cmake-check` | Linux host-portable Apple CMake/allow-list/deployment/plist/static-SDL/bundle/orientation checks. |
| `macos-build` | macOS-26 SOFTWARE portable platform/lifecycle/storage suites and a separately bundled smoke application. |
| `macos-sdlgpu` | macOS-26 SDLGPU on SDL Metal; the smoke script requires real exit-zero results and treats unavailable devices as failure. |
| `ios-build` SOFTWARE device/simulator | Final-linked bundles and Mach-O/plist/dependency checks; simulator launch requires the smoke marker. |
| `ios-build` SDLGPU device/simulator | Experimental `continue-on-error` legs; simulator can report the exact unsupported Metal device result before pixel testing. |

The focused SDLGPU program list comes from `scripts/ci/run-sdlgpu-smoke.sh --targets`; do not
hard-code an old program count. Workflow configuration, a historical green run and current
hardware qualification are distinct evidence.

## Open qualification and verification

Physical iPhone/iPad rendering, touch, audio, storage, performance and background/resume remain
unqualified. No current Intel/universal runtime, App Store distribution or additional Apple OS
target is established. The default Game window does not request high pixel density, although the
platform window descriptor supports it; Retina backing resolution is not thereby qualified.
DisplayInfo's safe-area extension exists, while XNA TitleSafeArea still reports the full surface;
neither has iOS notch/home-indicator evidence.

Run `scripts/check-apple-platform-cmake.sh` on a non-Apple host for the mechanical layer.
It passes in the 2026-11-01 documentation audit. That source/workflow review did not build with
Xcode, run an Apple application or establish a new remote CI result. Renderer-specific current
limits remain in [KNOWN_ISSUES.md](../KNOWN_ISSUES.md) and their renderer pages.
