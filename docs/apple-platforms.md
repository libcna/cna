# Apple platforms: macOS and iOS

CNA targets two Apple platforms, and they are not equally supported. This document states what
each one is, how to build it, and — most importantly — what evidence exists behind each claim.

| Target | CMake identity | Status |
|---|---|---|
| macOS (Apple silicon; Intel configurable) | `CMAKE_SYSTEM_NAME=Darwin` | arm64 native build/test CI; x86_64 not CI-verified |
| iOS / iPadOS device | `CMAKE_SYSTEM_NAME=iOS`, `iphoneos` sysroot | Experimental; final-linked app build, no device run |
| iOS simulator | `CMAKE_SYSTEM_NAME=iOS`, `iphonesimulator` sysroot | Experimental; one-frame app smoke run |
| tvOS / watchOS / visionOS | — | Rejected at configure time |

The corresponding tasks are `APPLE-1`…`APPLE-15` in [`plans/plan_apple.md`](../plans/plan_apple.md), which
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

`plans/plan_apple_m4.md` `AM4-037` (Mac mini M4, macOS 27, Xcode 27, iOS 27 Simulator on an iPhone 17
profile) added the native `METAL` renderer on iOS and `cna_ios_pixel_probe`, which reads back a
clear colour, a SpriteBatch quad and a BasicEffect triangle: exact on 29 of 31 simulator launches
(two all-zero runs right after first install did not reproduce). That is simulator pixel evidence
for METAL only; SDL_RENDERER refuses exact readback through the letterbox scaling a phone needs.

What does **not** exist is a run on a physical iPhone/iPad, device pixel correctness, real touch,
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

FFmpeg (`VideoPlayer`) is available on macOS through Homebrew and is detected by `pkg-config`,
exactly as on Linux. `CNA_ENABLE_VIDEO=AUTO` enables it when found; `OFF` produces a CNA/Game
binary without FFmpeg while retaining the public video API.

Homebrew builds its bottles for the macOS it runs on, so a binary linked against them does not
start on an older macOS whatever `CNA_MACOS_DEPLOYMENT_TARGET` says: the linker warns "building
for macOS-13.3, but linking with dylib ... built for newer version" (27.0 on the M4 campaign host).
A binary meant to run down to the 13.3 floor either builds with `CNA_ENABLE_VIDEO=OFF` or links an
FFmpeg built for that floor.

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
unit containing `main()` must include `CNA/Platform/Entrypoint.hpp` before SDL headers so SDL can hand
process startup to UIKit.

## Building for iOS

Requires a macOS host with Xcode installed.

```bash
# Device (arm64)
cmake -S . -B cmake-build-ios \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ios.cmake \
      -DCNA_GRAPHICS_RENDERER=SDL_RENDERER \
      -DCNA_ENABLE_NET=OFF \
      -DCNA_BUILD_TESTS=OFF -DCNA_BUILD_EXAMPLES=OFF
cmake --build cmake-build-ios --parallel 4

# Simulator (arm64 on Apple silicon, x86_64 on Intel)
cmake -S . -B cmake-build-ios-sim \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ios.cmake \
      -DCNA_IOS_SIMULATOR=ON \
      -DCNA_GRAPHICS_RENDERER=SDL_RENDERER \
      -DCNA_ENABLE_NET=OFF \
      -DCNA_BUILD_TESTS=OFF -DCNA_BUILD_EXAMPLES=OFF
```

`CNA_ENABLE_NET=OFF` is required on iOS today: networking (`GamerServices` + `Net`) needs a
libcurl for the target, and the iOS SDK ships none, so with networking on the configure stops at
"Could NOT find CURL" (`plans/plan_apple_m4.md` `AM4-105`; Apple CI passes the same flag). Use
`-DCNA_GRAPHICS_RENDERER=METAL` for the native Metal renderer.

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
  renderer, includes `CNA/Platform/Entrypoint.hpp`, selects a landscape orientation and runs one `Game`
  frame. Disable it only with `-DCNA_BUILD_APPLE_SMOKE_APP=OFF`.
- **`main()` is renamed** by `CNA/Platform/Entrypoint.hpp`, which pulls in `<SDL3/SDL_main.h>` on iOS for
  the same reason it already did on Android: UIKit owns the process, and SDL's own `main()` has
  to run `UIApplicationMain` before the game's `main()` is called. A game that does not include
  `CNA/Platform/Entrypoint.hpp` never gets a `UIApplication`, and therefore no window and no events.
- **FFmpeg is disabled** (`CNA_ENABLE_VIDEO=AUTO` resolves unavailable): `pkg-config` on a macOS
  host resolves to Homebrew's *macOS* FFmpeg, which cannot be linked into an iOS binary. The public
  `Video`/`VideoPlayer` and content-reader surface remains link-complete; file probing/playback
  reports `System::NotSupportedException`. `CNA_ENABLE_VIDEO=ON` fails configuration until a real
  target-native iOS integration exists.
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
| `OPENGLES3` | Refused by default. It requires the sibling `easy-gl` and `meta-gl` repositories, which the iOS workflow does not provide or validate. |
| `HEADLESS`, `SOFTWARE`, `STUB` | Refused by default. They may be useful for experiments, but they are not final-linked by Apple CI and therefore are not advertised as supported iOS configurations. |
| `METAL` | Allowed (`plans/plan_apple_m4.md` `AM4-037`). CNA's native Metal renderer on a UIKit view in SDL's `UIWindow`. Built for device and simulator; the simulator runs `cna_ios_pixel_probe`, which checks a clear colour, a SpriteBatch quad and a BasicEffect triangle read back from the frame. |
| Everything else | Refused. Desktop APIs cannot exist on iOS; other third-party-backed renderers have not been configured for an iOS sysroot. |

"Allowed" means configure accepts it, CI final-links it for both iOS sysroots, and the simulator
smoke app launches one frame. It does not mean correct pixels have been observed.
`CNA_APPLE_ALLOW_UNVALIDATED_RENDERER=ON` downgrades any refusal to a warning for experimentation;
expect build or runtime failures, and nothing about such a configuration is supported.

## Mac mini M4 qualification (2026-10-07/08)

`plans/plan_apple_m4.md` (`AM4-*`) is the task-by-task record; this is its summary. Host: Mac mini
M4 (arm64, 16 GB), macOS 27.0.1, Xcode 27.0 / Apple clang 21, CMake 4.4.4, `CNA_PLATFORM=SDL3`
(SDL's Cocoa and UIKit drivers -- CNA has no Cocoa/AppKit/UIKit backend of its own). Every number
below was measured on that machine. Statuses: **PASS**, **PASS WITH KNOWN LIMITATION**,
**BUILD ONLY**, **FAIL**, **NOT APPLICABLE**, **NOT TESTED**, **OUT OF SCOPE**.

The final full-suite run (`ctest -j3`, one tree per renderer) ran while the console session was
**locked**, which makes macOS report every window occluded. Renderers that draw the back buffer
offscreen (Metal, EasyGL, SOFTWARE) are unaffected; WebGPU dropped such frames until `AM4-089`.
Tests sharing network ports or a profile store also interfered under `-j3`; every failure counted
below as interference passes when run alone or serially.

| Component | Status | Evidence on the M4 | Known limitations / notes |
|---|---|---|---|
| sharp-runtime (standalone) | PASS WITH KNOWN LIMITATION | 18,084 tests: 18,058 pass, 25 skip (native float `from_chars` absent below macOS 26, live SOAP, `/rv` fixtures, x86-64 layout pin), 1 intermittent (`ServiceHostTest`, passes alone); an independent rerun on 2026-10-08: 18,059 pass, 0 fail, 25 skip | FileSystemWatcher has a kqueue backend since this campaign (AM4-055, hardened by AM4-101: per-file descriptors capped at half the soft limit, FIFOs never opened); see sharp-runtime `docs/Platform-macOS.md` |
| SDL3 platform baseline (`SDL_RENDERER`) | PASS WITH KNOWN LIMITATION | `SDL_Renderer_*` 77/80; full CnaTests 10,671/10,892 | 205 of the 221 are CnaTests assuming a 3D-capable default renderer (they pass on SOFTWARE; CI runs the unfiltered suite only on OPENGLES3); 3 `SDL_Renderer_*` expectation conflicts await an owner decision |
| `METAL` | PASS WITH KNOWN LIMITATION | Phase 3 (`AM4-134`..`AM4-159`): `ctest -L Metal` 261/261 with `MTL_DEBUG_LAYER`/`MTL_SHADER_VALIDATION` (101 shared pixel fixtures, 146 renderer-neutral oracles, 14 native); full CnaTests 11,015: 10,867 pass, 142 skip, 6 fail (`AM4-158`), none of the failures Metal's; Release tree 261/261 (`AM4-156`); compiled effects (`CNA_METAL_COMPILED_EFFECTS=ON`) 43 Metal and 1,237 effect tests (`AM4-144`); cna-samples 90/91 with compiled effects on (Yacht fails on every renderer); iOS device build links; cna-examples 248/248 and cna-car-simulator at `AM4-076` | MSAA, every XNA surface format (float, packed, DXT as BC or decoded), instancing, multi-stream input, compiled XNA effects, MRT outputs and the backbuffer's own depth format are native since Phase 3. Left: compiled effects are opt-in (off by default); custom `ShaderEffect`s are SpriteBatch-scoped MSL; `PresentInterval.Two` presents as `One`; the backbuffer is always BGRA8 (reported as `Color`); LOD bias needs macOS/iOS 26; a multisampled RenderTarget2D loses a `SetData` under `PreserveContents`. Phase 3 verdict: **SUPPORTED** -- not primary-production, because on-screen presentation, a physical iOS device and a soak run are unmeasured (`docs/metal-renderer.md`) |
| `OPENGL33` (EasyGL) | PASS WITH KNOWN LIMITATION | `EasyGL_*` 399/400; compiled effects 1,875/1,875 (`CNA_EASYGL_COMPILED_EFFECTS=ON`); CnaTests 11,834/11,863 in the final run, 7 left at the campaign head (all classified); cna-samples 90/91 | Apple's GL ("4.1 Metal") stores 24-bit depth as float32, so a constant `DepthBias` is exact only for depths in [0.5, 1) (proved by a clear/readback probe); `EasyGL_Anisotropic_GlState` asserts Mesa's clamp-on-store (owner call) |
| `SDL_GPU` (SDL's Metal driver) | PASS WITH KNOWN LIMITATION | `SdlGpu_*` 195/197; cna-samples 89/91; CnaTests 10,861/10,914, every failure classified in `AM4-092` | 2 `SdlGpu_*` are design gaps on every platform (draw-range throwing, unimplemented float `Texture2D` formats); point lists draw nothing on Metal because no stock vertex shader writes `gl_PointSize` (fix described in `AM4-092`; regenerating the SPIR-V needs libshaderc); custom GLSL effects need libshaderc |
| `WEBGPU` (wgpu-native v29.0.1.1, Metal backend) | PASS WITH KNOWN LIMITATION | `WebGPU_*` 147/149 under the locked session after `AM4-088`/`089` (before `AM4-088` the launcher skipped them all on macOS); cna-samples 90/91; full CnaTests 10,904/10,928 at the campaign head (`AM4-089`..`096`; 120 failures in the final run), the rest classified in `AM4-092` | wgpu-native's Metal backend ignores `MultiSampleMask` (Metal pipelines have no sample-mask state) and counts occlusion as a boolean (`AM4-091`); platform-independent WebGPU gaps found here are listed in `AM4-092` |
| `SOFTWARE` | PASS WITH KNOWN LIMITATION | `Software_*` 159/159; CnaTests 11,008/11,036 in the final run, 6 left at the campaign head (shared gates) plus one timeout | A Debug build draws the Guide below two frames a second (input fixed in `AM4-086`); one exhaustive click-scan Guide test exceeds its timeout |
| `FNA3D` (FNA3D's Metal driver) | PASS WITH KNOWN LIMITATION | `Fna3d_*` 13/13; CnaTests 10,784/10,827 (8 failures unique to this tree, all the driver's boundaries); cna-samples 89/91 | FNA3D's Metal driver has no occlusion queries (LensFlare); MojoShader's Metal profile lacks `TEXCRD`; its driver accepts NormalizedByte2/4 where the GL driver refuses them; FNA's compiled stock effects leave COLOR0/COLOR1 unsaturated (MojoShader; `BasicEffectVertexLightingSaturationTest`, `AM4-106`); the registered tests run FNA3D's Metal driver -- on the legacy GL 2.1 driver `Fna3d_RenderTarget_Advanced` leaks a draw into an unbound cube face (`AM4-051`, not root-caused) |
| `OPENGLES3` | NOT APPLICABLE | -- | macOS has no native OpenGL ES |
| `WEBGL2` | NOT TESTED | -- | Emscripten/browser; no emsdk on this host |
| `HEADLESS`, `STUB` | NOT TESTED | -- | Not part of this campaign's matrix |
| `VULKAN` (MoltenVK) | OUT OF SCOPE | -- | Excluded on Apple by the campaign brief |
| `DIRECTX9`, `DIRECTX11` | NOT APPLICABLE | -- | Windows only |
| iOS Simulator, SDL3 + `METAL` | PASS | `cna_ios_pixel_probe` (iOS 27 Simulator, iPhone 17 profile) exact on frame 1 and on the 10 frames after it in 8 of 8 launches at `AM4-120`, `cna_ios_smoke` OK | The all-zero runs seen at `AM4-037` were SDL's UIKit view-frame swap (`AM4-120`): once the window rotated to the landscape-only set, every frame drew into a 1346x0 view. The probe used to stop at the first good frame, which hid it in most launches |
| iOS device (arm64) | BUILD ONLY | Final-linked `.app` for `iphoneos` (rebuilt at `49bdf63b8`: Mach-O arm64, platform 2, minos 16.3, system frameworks only) | No physical iPhone, signing identity or Apple account available |
| C API (`modules/c-api`) | PASS WITH KNOWN LIMITATION | 111/115 on SOFTWARE, 108/115 on `SDL_RENDERER` (`AM4-072`..`075` fixed the first run's 13) | 3 documentation gates need Doxygen 1.9.8 (Homebrew has 1.18, which crashes on the limitations parse); the media-library smoke reads the real `~/Music` (SDL's user folders ignore `HOME`); 3 `SDL_RENDERER` smokes exercise features that 2D renderer refuses (they pass on SOFTWARE) |
| cna-examples | PASS | 248/248 demos render on Metal | Net demos fixed in cna-examples `ede3473` |
| cna-samples (multi-renderer Release) | PASS WITH KNOWN LIMITATION | per-renderer counts above | Yacht fails on every platform |
| house-simulator | PASS WITH KNOWN LIMITATION | Builds and runs on `OPENGL33`; content-smoke renders all six content types | World content needs the Blender pipeline, so most integration tests cannot run here |
| cna-car-simulator | PASS WITH KNOWN LIMITATION | Runs on Metal (chase, cockpit, night rain, mirrors); unit 320/321 | One test depends on `std::uniform_int_distribution`, whose algorithm differs between libc++ and libstdc++ |
| cna-gamer-services-server | PASS WITH KNOWN LIMITATION | 37/46 pass with Homebrew curl | 7 skip (Linux namespaces); 2 need `lo0` aliases, which require `sudo`; the system libcurl 8.7.1 has no WebSocket support |

## Runtime behavior on Apple platforms

### Platform identification

`CNA/TargetPlatform.hpp` answers the compile-time questions:

```cpp
CNA::getCurrentPlatform();      // TargetPlatform::Desktop on macOS, TargetPlatform::iOS on iOS
CNA::isApplePlatform();         // true on both
CNA::isMobilePlatform();        // true on iOS and Android
CNA::getCurrentPlatformName();  // "macOS", "iOS", "Linux", "Windows", "Android", "Web"
CNA::getCurrentDesktopOS();     // DesktopOS::MacOSX on macOS; throws on iOS
```

The `CNA_TARGET_APPLE`, `CNA_TARGET_MACOS` and `CNA_TARGET_IOS` macros are defined by the same
header for preprocessor conditions. They use the `CNA_TARGET_` prefix, not `CNA_PLATFORM_`, so
they cannot be confused with `CNA_PLATFORM_<NAME>` — that names the selected platform
*implementation* (`SDL3`, `HEADLESS`, `TERMINAL`), an independent build axis. macOS deliberately
reports `TargetPlatform::Desktop` — it is a desktop — so `isApplePlatform()` is the query that
spans both Apple targets.

### Application lifecycle

iOS terminates an application that submits GPU work after entering the background, and Android
destroys the rendering surface at the same moment. `Game`'s loop therefore stops between the
operating system's "did enter background" and "will enter foreground" notifications: it blocks
on the SDL event queue instead of ticking, and neither updates nor draws. This is a deliberate
deviation from FNA, which tracks only `IsActive` on those events — it is documented in
`Game.cpp` at the site and covered by `plans/plan_apple.md` APPLE-7.

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

A narrowed set rotates the window asynchronously. SDL's `UIKit_ComputeViewFrame` swapped a
portrait window's bounds for a landscape-only set before that rotation, and UIKit's autoresizing
then turned the rotation into a 1346x0 root view (iPhone 17 Simulator started in portrait): the
game ran on, drawing into a drawable one pixel high. CNA carries
`cmake/patches/sdl-cbe3fbe9-0005-uikit-scene-window-frame.patch` for iOS builds, which keeps a
scene window's bounds as UIKit reports them (`plans/plan_apple_m4.md` `AM4-120`; SDL main still
swaps).

### Storage locations

`StorageDevice` resolves its root itself, independently of the windowing platform
(`modules/storage/src/StorageDevice.cpp`): `XDG_DATA_HOME/<app>` when that variable is set (tests
and tools use it for isolation, on Apple too), otherwise `$HOME/Library/Application Support/<app>`
on both Apple platforms -- on iOS `$HOME` is the app container, so saves stay inside it and survive
an app update. The Linux layout (`~/.local/share`) is never used on Apple.

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
- No Mac Catalyst, tvOS, watchOS or visionOS target.
