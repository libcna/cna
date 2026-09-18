# Changelog

All notable changes to CNA are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). While the major version is
0, the public API may change in any release — see the pre-1.0 note in
[`docs/releasing.md`](docs/releasing.md).

## [Unreleased]

### Added

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
  [`docs/platform-win32.md`](docs/platform-win32.md) for the capability boundary and
  [`plans/plan_win32.md`](plans/plan_win32.md) for the task log.

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

[Unreleased]: https://github.com/openeggbert/cna/compare/v0.1.0-alpha.1...HEAD
[0.1.0-alpha.1]: https://github.com/openeggbert/cna/releases/tag/v0.1.0-alpha.1
