# Changelog

All notable changes to CNA are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). While the major version is
0, the public API may change in any release — see the pre-1.0 note in
[`docs/releasing.md`](docs/releasing.md).

## [Unreleased]

### Fixed

- **A game without a `GraphicsDeviceManager` gets a depth buffer.** CNA lets a game run with no
  `IGraphicsDeviceService` — a C API game owns no manager unless it creates one — and gave that
  implicit device `PresentationParameters`' own default, `DepthFormat::None`, so its
  `Clear(color, depth)` was refused since the SOFTWARE-333 contract. The implicit device now
  carries `Depth24`, the default `PreferredDepthStencilFormat` of an XNA `GraphicsDeviceManager`,
  as every XNA game's device does; `new PresentationParameters()` keeps XNA's `None`. Pinned by
  `CApi_GameDefaultDepthBufferSmoke`. Found by cna-c-template 0.1.0's 3D path.

## [0.1.0] — 2026-10-10

The final 0.1.0 release, cut from `next`. The entries below describe what changed since
`0.1.0-alpha.1`. For the first time the sibling checkouts CNA builds against are **required**
rather than only recorded: see *Dependencies*.

### Dependencies

CNA consumes three separately versioned sibling checkouts with `add_subdirectory`, so checking out
this tag does not select them. This release requires, and was built and tested against:

| Sibling | Required | Verified against |
|---|---|---|
| sharp-runtime (`../sharp-runtime`) | **0.1.0** | [`v0.1.0`](https://github.com/libcna/sharp-runtime/releases/tag/v0.1.0), `c20373bae5be1f02d1fe2fa33c4cbba8ad05e4a0` |
| easy-gl (`../easy-gl`) | **0.1.1** | [`v0.1.1`](https://github.com/libcna/easy-gl/releases/tag/v0.1.1), `f4d6afaf16cdc7e7e73130950bc45e6d865b41bd` |
| meta-gl (easy-gl's `../meta-gl`) | **0.4.1** | [`v0.4.1`](https://github.com/libcna/meta-gl/releases/tag/v0.4.1), `dd3b30055d55fdc42240de7af248259338256992` |

`cmake/DependencyVersions.cmake` now enforces this at configure time. Each sibling generates a
`Version.hpp` from its own single-source version, and CNA reads it right after
`add_subdirectory`. A checkout that declares the required version or a later patch release of the
same minor line is accepted. Anything else — another minor line, a pre-release of the required
version, or a checkout too old to declare a version — stops configuration with the `git checkout`
that fixes it. `-DCNA_CHECK_DEPENDENCY_VERSIONS=OFF` downgrades the error to a warning. The check
compares versions, not commits; the commits above record exactly what was verified.

Every other dependency is still pinned by this repository through submodule gitlinks and the
`GIT_TAG` values in `cmake/ThirdParty*.cmake` and `cmake/RendererSelection.cmake`.

### Added

- GamerServices and Net against CNA's own account service (`cna-gamer-services-server`; not Xbox
  LIVE compatible): accounts and a console-style Guide, profiles, friends, presence, messages,
  reviews, achievements, leaderboards with Ranked arbitration, PlayerMatch/Ranked sessions with
  invitations, host migration, `AddLocalGamer` and parties over an authenticated TLS/WSS relay, push
  hints, network voice (optional libopus), and the standard avatar API drawn from original CNA
  catalogs. See `docs/gamer-services-server.md`, `docs/avatars.md` and, for what remains,
  `docs/gamer-services-known-limitations.md`.
- The `RLGL` renderer identity and standalone raylib 6.0 `rlgl.h` dependency baseline, pinned to
  an immutable commit without building or initializing the raylib application framework. The
  GraphicsDevice clear/readback/present/resize slice, all 20 classic Texture2D formats (including
  exact packed, signed-normalized, half/full-float, XNA channel expansion, and DXT1/3/5 with
  native-S3TC or software-decoded storage), and independent XNA sampler objects are
  runtime-validated on Linux. Complete blend, depth/stencil, rasterizer, write-mask, sample-mask,
  scissor, and depth-bias state is also pixel- and native-state-validated. A CNA-owned low-level
  rlgl SpriteBatch path now covers built-in texture and SpriteFont drawing, transforms, sorting,
  blending, clipping, and sampler forwarding. Fixed-capacity rlgl VBO/EBO resources now cover
  static/dynamic vertex data, every classic vertex declaration format, 16/32-bit indices, and
  `None`/`Discard`/`NoOverwrite` updates. Primitive submission covers every indexed/non-indexed
  XNA topology, public bound/user draw routes, semantic-driven declarations, vertex/index/base
  offsets, and WVP transforms. The first shared stock shader path covers unlit BasicEffect and
  AlphaTestEffect with texture/default-white sampling, vertex/material color, all alpha compares,
  fog, and the XNA pixel-center convention; lighting and the remaining effect/resource families
  are under active development in `plans/plan_rlgl.md`. The append-only C
  renderer identity advances the experimental C ABI to 0.27.0.
  **Retired before this release** with the renderer-set curation below; the entry is kept because
  the C ABI passed through 0.27.0 and value 51 is permanently reserved.
- A native Win32 platform backend, selected with `CNA_PLATFORM=WIN32` on Windows targets and
  refused loudly everywhere else. It is built directly on user32/gdi32/opengl32/ole32/shell32 and
  uses no SDL for windowing, events, keyboard, mouse, text input, timing, clipboard, displays or
  dialogs, so `CNA_PLATFORM=WIN32 + CNA_AUDIO_PLATFORM=NULL + CNA_GRAPHICS_RENDERER=DIRECTX11`
  (or `DIRECTX12`) is a supported configuration with no SDL in it at all. The platform and
  renderer axes stay independent: DirectX receives the window through the existing generic
  `NativeWindowHandle`, and no renderer changed. See
  [`plans/plan_win32.md`](plans/plan_win32.md) for the task log.
  **Retired before this release**, with CNA's direct X11 and Wayland platforms (see Removed); the
  entry is kept as the record of what `next` carried in between.

### Removed

- **Twenty-five public renderer identities**, leaving a curated set of 25 over 21 implementation
  families: `BGFX`, `MAGNUM`, `BLEND2D`, `DIRECTX1`, `DIRECTX2`, `DIRECTX3`, `DIRECTX5`,
  `DIRECTX6`, `DIRECTX7`, `DIRECTX8`, `DIRECTX10`, `OPENGLES1`, `OPENGL1`, `OPENGL2`, `WICKED`,
  `SOKOL`, `DILIGENT`, `GLIDE`, `LLGL`, `OPENVG`, `TINYGL`, `IGL`, `PIXIJS`, `NANOVG` and `RLGL`,
  with their implementations, dependencies, patches, CI jobs and documentation
  (`plans/plan_renderer_cleanup.md`, [`docs/removed-renderers.md`](docs/removed-renderers.md)).
  Selecting one is now a configure error naming the identity and its reserved C ABI value, never a
  silent fallback to another renderer. No surviving identity is renumbered: their C ABI values are
  unchanged, the retired values are permanently reserved, and the C ABI is `0.28.0` with
  `CNA_GRAPHICS_RENDERER_MAXIMUM` at 46 (`PORTABLEGL`).
- **The SpriteBatch 2D triangle-mesh entry point**, which the curation above left with no
  implementer: `SpriteBatch::DrawMeshEXT` (a CNAEXT extension, never part of XNA 4.0),
  `ISpriteBatchRenderer::DrawMeshEXT`, and the C route `cna_sprite_batch_draw_mesh_ext` with its
  `CNA_SpriteMeshEXT` structure. It existed for the retired Skia renderer's `SkVertices`/SkSL mesh
  ABI, and after the curation every renderer refused it — the C route could be called but never
  succeeded on any supported renderer. Deleted rather than left as a permanently dead ABI branch
  while the ABI is still experimental; there is no replacement and no compatibility stub. This
  advances the experimental C ABI to `0.29.0`, with exported symbols 4,056 → 4,055 and recorded
  struct layouts 222 → 221 (`plans/plan_renderer_cleanup.md` `RRC-009`).
- **Three more renderer identities**, `DIRECT2D`, `FREEDIRECT` and `PORTABLEGL`, leaving 22 public
  identities over 18 implementation families: their implementation families, the PortableGL
  FetchContent pin, the `../free-direct` sibling checkout, the Direct2D Wine/Proton runners,
  debug-log gates and Windows CI leg, their tests and their documentation
  (`plans/plan_renderer_cleanup.md` `RRC-012`, [`docs/removed-renderers.md`](docs/removed-renderers.md)).
  They are refused by name at configure time like the earlier retirements. `GDI` and `SOFTWARE` are
  unchanged. Their C ABI values 16, 21 and 46 are permanently reserved,
  `CNA_GRAPHICS_RENDERER_MAXIMUM` moves to 44 (`SVG_DOM`), and the experimental C ABI goes to
  `0.31.0`; no surviving identity is renumbered.
- **Four more renderer identities**, `GDI`, `HTML_DOM`, `SVG_DOM` and `OPENGL4`, leaving 18 public
  identities over 14 implementation families: their implementation families, examples and tests,
  the GDI Windows and HTML DOM browser CI jobs, the DOM browser scripts, the OpenGL4 GL-error test
  gate, the GDI-only reduced Software build (`CNA_SOFTWARE_2D_ONLY`) and their documentation
  ([`docs/removed-renderers.md`](docs/removed-renderers.md)). None is replaced: `OPENGL33` stays
  CNA's desktop OpenGL identity with its 3.3 contract, and there is no EasyGL OpenGL 4 profile.
  `SOFTWARE` keeps its full CPU renderer; the Software contracts only GDI's suite had tested moved
  into the Software suite. `ColorMatrixEffect` is now documented as a `SOFTWARE` extension. Their C
  ABI values 18, 33, 40 and 44 are permanently reserved, `CNA_GRAPHICS_RENDERER_MAXIMUM` moves to
  43 (`FNA3D`), and the experimental C ABI goes to `0.33.0`; no surviving identity is renumbered.
- **CNA's direct `WIN32`, `X11` and `WAYLAND` platform implementations**, with the code only they
  used (XKB key tables, the freedesktop.org D-Bus/portal services, Linux evdev controllers, POSIX
  helpers), their tests, desktop-validation harnesses, the standalone platform harness (broken since
  2026-09-28 and used only by the Win32 lanes), Windows validation scripts, CMake discovery
  modules (`cmake/PlatformX11.cmake`, `cmake/PlatformWayland.cmake`), CI cells, spikes and
  capability documents (`plans/plan_platform.md` §11c, PLAT-142). **Windows, X11 and Wayland remain
  supported, through SDL3**: SDL3 is CNA's one graphical platform, reaching each window system
  through SDL's own `windows`, `x11` and `wayland` video drivers, and `IPlatformWindow::
  GetNativeHandle()` still hands renderers the `HWND`, `Display*` + XID or `wl_display*` +
  `wl_surface*` (`NativeWindowSystem::Win32/X11/Wayland`, `TryGetWin32/X11/Wayland` unchanged). No
  renderer and no contract header changed. `CNA_PLATFORM=WIN32|X11|WAYLAND` is now a configure
  error that names `SDL3` as the replacement; there is no alias. `CNA_ENABLE_SDL=OFF` remains, as a
  windowless configuration (`HEADLESS`/`TERMINAL`, `NULL` or `ALSA` audio). New `CnaPlatformSdl3X11Tests`
  and `CnaPlatformSdl3WaylandTests` run SDL3 on a private Xvfb and a private headless Weston, and
  the Direct3D 11 window stress fixtures now drive the `HWND` of an SDL3 window.
- **Four more renderer identities**, `DIRECTX12`, `CANVAS`, `OPENGLES2` and `WEBGL1`, leaving 14
  public identities over 12 implementation families (`RRC-018`,
  [`docs/removed-renderers.md`](docs/removed-renderers.md)). None is replaced or aliased:
  `DIRECTX11` and `DIRECTX9` cover Windows, `WEBGL2` and `WEBGPU` the browser, and EasyGL keeps
  `OPENGLES3`, `OPENGL33` and `WEBGL2` without its ES 2.0 profiles and GLSL ES 1.00 lowering. Their
  C ABI values 2, 5, 15 and 17 are permanently reserved and the experimental C ABI goes to
  `0.45.0`; no surviving identity is renumbered.

### Changed

- The root configure no longer builds vendored SDL3 for a configuration that does not use it. The
  gate is conservative — every unset or SDL3-valued axis keeps SDL3, so the default build is
  unaffected — and applies only when the platform, audio and renderer selections have all
  explicitly said otherwise with tests and examples off.

- Capability-gated CNAEXT base-instance drawing through
  `GraphicsDevice::DrawInstancedPrimitivesBaseInstanceEXT`, with the append-only
  `RendererFeature::BaseInstanceDrawing` / C ABI 0.26.0 feature identity and a Vulkan
  implementation verified on RADV and llvmpipe.
- Device-gated Vulkan indirect drawing through both canonical command layouts, including
  compute-generated arguments, non-zero command/geometry/base-instance offsets, automatic command
  visibility and fence-safe deferred argument-buffer lifetime, verified on RADV and llvmpipe.
- Vulkan GPU timing through recycled timestamp query pools, device-derived timestamp periods and
  nonblocking result polling, plus debug-utils pass regions and one-copy structured logger output,
  verified on RADV and llvmpipe.

### Fixed

- **A game can run in a terminal again.** The renderer-set curation retired `BLEND2D`, the only
  renderer that presented CPU frames through `IPlatformSurfacePresenter`, which left
  `CNA_PLATFORM=TERMINAL` with a complete, tested presenter and nothing feeding it — the
  configuration built and displayed nothing. `SOFTWARE` now requests a presenter and hands it each
  finished frame; its framebuffer is already RGBA8, row-major, top row first and tightly packed, so
  the handover costs no conversion and no copy. Whether a CPU renderer is given a window is the
  platform's answer rather than the renderer's — it gets one where the platform reports surface
  presentation and no native window handle, which is a terminal and nothing else today — so
  `SOFTWARE` on a windowing platform, under `HEADLESS`, or with its output piped behaves exactly as
  before. The end-to-end pseudo-TTY test disabled by the curation is enabled and passing
  (`plans/plan_terminal_capi_repair.md`).
- **`-DCNA_BUILD_C_API=ON` builds again**, at three independent root causes rather than by
  suppression: the effect collections' `operator[](int)` returns `T*` since XNA's null-index
  semantics were restored, and `CnaCApiEffects.cpp` still took its address; the C API links
  `CNA_GamerServices` unconditionally while `CNA_ENABLE_NET=OFF` does not build it, which is now a
  configure-time refusal naming the option instead of a missing-include failure in every
  translation unit; and both ABI walls still asserted `0.27.0` against a header declaring `0.29.0`.
  With the library buildable, the ABI baseline is measured by its own generator against the real
  `.so` for the first time since the break — 221 struct layouts and 4,055 exported symbols,
  identical to the values recorded by hand, with the ABI unchanged at `0.29.0` — and
  `docs/c-api/RELEASE_GATE.md` regenerates normally instead of publishing a traceback.

### Known limitations

- Pre-release quality remains: a minor release may change the public API, renderer coverage is
  uneven by design (`docs/<renderer>-renderer.md`), the known per-renderer bugs are in `NEXT.md`
  §5, and `Content` has no general `.xnb` reader.
- Three tests fail in the release gate's tree and failed in the same tree before this release
  (its 2026-10-07 run): `CnaInputTests`
  (`KeyboardOrientationTest.LateEnableHonorsDefaultLandscapeAndPreparingParameters`),
  `EasyGL_PresentationMsaaContract`, and `CApi_InstalledConsumer`, which cannot link an installed
  consumer against an external `wgpu_native` (`NEXT.md` §5, item 6). They are carried, not fixed,
  by this release.
- The three `XnaPipelineGenuineRuntime*` tests run the genuine XNA 4.0 runtime under Wine and
  need an X11 display Wine can drive; inside the private Weston/Xwayland runner of
  `tools/platform/run_gpu_tests_private.sh` Wine loads no window driver and they fail. They pass
  under Xvfb.

### Verification

- `cmake-build-multi` — `OPENGLES3` as the default renderer plus `OPENGL33`, `VULKAN`, `WEBGPU`,
  `SDL_GPU`, `FNA3D`, `SDL_RENDERER`, `SOFTWARE`, `HEADLESS` and `STUB` in one binary, Debug,
  tests on — configures with `CNA: version 0.1.0` and one accepted-version line for each of
  sharp-runtime 0.1.0, easy-gl 0.1.1 and meta-gl 0.4.1, and builds its 2,771 steps with 0 errors.
  The log carries 9 warnings, all `ignoring return value` in two test sources
  (`XnaMaterialContentTests.cpp`, `GamerServicesGamerTests.cpp`), present before this release.
- The complete suite on the private display: **11,509 tests, 11,310 passed, 192 skipped,
  7 failed** in 24.5 minutes at `-j8`. The 192 skips are the tests' own renderer and profile
  conditions (`Fna3dCompiledEffect*`, `SdlGpuIndexedDrawRangeTest`, `WebGpuWireFrameContract`, …).
  Of the 7 failures, 3 are the pre-existing ones above, 3 are the Wine display limitation above
  (all three pass under Xvfb, 22–24 s each), and
  `KeyboardGamepadPumpTest.GamePublishesBeforeUpdateAndClearsWhenKeyboardServiceDisappears`
  passed 3 of 3 re-runs in isolation — it is timing-sensitive under the parallel run. No failure
  is new to this release.
- The dependency check was exercised on purpose: a checkout declaring a later patch release is
  accepted; a pre-release of the required version, another minor line, an older patch release and
  a checkout with no version are refused with the `git checkout` that fixes it, and
  `-DCNA_CHECK_DEPENDENCY_VERSIONS=OFF` turns the refusal into a warning.
- Verified on Debian GNU/Linux 13 with GCC 14.2.0, CMake 3.31.6, Ninja and Mesa 25.0.7, with the
  siblings checked out clean at their tags: sharp-runtime `v0.1.0`, easy-gl `v0.1.1`,
  meta-gl `v0.4.1`.

## [0.1.0-alpha.1] — 2026-08-20

First tagged release. CNA has been developed continuously since 2025-02-22; this tag names a
state of `develop` rather than introducing new work, so the entries below describe what the
release contains, not what changed since a previous tag.

### Added

- **XNA 4.0 API surface.** 227 of the 245 public FNA types are present (`Graphics`, `Audio`,
  `Input`/`Touch` and `Storage` complete); `Content` and `Media` are the known gaps. See
  [`docs/xna-4-api-coverage.md`](docs/xna-4-api-coverage.md).
- **Graphics.** Every one of the ~26 major `Microsoft::Xna::Framework::Graphics` classes is
  implemented and test-covered, at a qualified ~90% XNA/FNA behavioural compatibility.
- **49 renderer identities** selected at compile time through `CNA_GRAPHICS_RENDERER`, plus the
  opt-in multi-renderer build that chooses between several at runtime
  ([`docs/runtime-renderer-selection.md`](docs/runtime-renderer-selection.md)). `VULKAN`,
  `BGFX`, `FNA3D`, `OPENGL4` and the EasyGL-backed GL family (`OPENGLES3`, `OPENGL33`, `WEBGL1`,
  `WEBGL2`, `OPENGLES2`) are the mature ones; `WEBGPU`, `SKIA`, `SOKOL`, `DILIGENT`, `IGL`,
  `PIXIJS` and the legacy DirectX identities carry documented, narrower capability boundaries.
  (That was this release's set; 25 of those identities were retired after it — see the Unreleased
  section above.)
- **Platform abstraction.** `CNA::Platform::IPlatform` with `SDL3`, `SDL2`, `HEADLESS` and
  `TERMINAL` implementations, on independent CMake axes from the renderer and audio choices
  ([`docs/platform-abstraction.md`](docs/platform-abstraction.md)).
- **Audio, input, media, storage, networking and device extensions** as physical modules under
  `modules/` ([`docs/physical-modules.md`](docs/physical-modules.md)).
- **Compiled XNA effects** — XNB `EffectReader` and Direct3D 9 Effect Framework bytecode
  execution on the `FNA3D` renderer.
- **Experimental native C ABI** (`CNA_BUILD_C_API`, ABI version 0.7.0), versioned independently
  of this product version.
- **`CNA/Version.hpp`** — the release identity generated from the build's single source of
  truth, exposing `CNA::getVersionString()` and the `CNA_VERSION_*` macros.

### Dependency pins

Third-party dependencies are pinned by this repository: the submodules
(`third_party/SDL` `cbe3fbe9f`, `third_party/SDL_image` `fcb9d0b15`, `third_party/SDL_mixer`
`3075d3eda`, `third_party/draco` `8786740086`, `vendor/googletest` `7e2c425db`) through their
gitlinks, and the FetchContent dependencies through the `GIT_TAG` values in
`cmake/ThirdParty*.cmake` and `cmake/RendererSelection.cmake`. Checking out this tag therefore
selects them.

**`sharp-runtime` is the exception and is not pinned by the build.** It is a sibling checkout
consumed with `add_subdirectory` from `../sharp-runtime` (overridable with
`-DCNA_SHARP_RUNTIME_ROOT`), so a build takes whatever revision that checkout happens to be on.
This release was developed and verified against:

    sharp-runtime  625476d5b5fff5fa89f392c3c9af8638ff237692  (develop, 2026-08-19)
    https://github.com/openeggbert/sharp-runtime

Recording the revision here is a stopgap — it documents the pin without enforcing it. A real
mechanism (a configure-time check against a recorded pin, or a submodule) is planned.

### Known limitations

- Pre-release quality: interfaces are expected to change before 1.0, and renderer coverage is
  uneven by design — each renderer's boundary is documented in `docs/<renderer>-renderer.md`.
- Per-renderer bugs and gaps that are known and tracked are listed in `NEXT.md` §5.
- `Content` has no general `.xnb` reader by design, and 14 of the `Media` types are shells.

[Unreleased]: https://github.com/libcna/cna/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/libcna/cna/compare/v0.1.0-alpha.1...v0.1.0
[0.1.0-alpha.1]: https://github.com/libcna/cna/releases/tag/v0.1.0-alpha.1
