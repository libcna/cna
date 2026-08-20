# Changelog

All notable changes to CNA are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). While the major version is
0, the public API may change in any release — see the pre-1.0 note in
[`docs/releasing.md`](docs/releasing.md).

## [Unreleased]

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

### Known limitations

- Pre-release quality: interfaces are expected to change before 1.0, and renderer coverage is
  uneven by design — each renderer's boundary is documented in `docs/<renderer>-renderer.md`.
- Per-renderer bugs and gaps that are known and tracked are listed in `NEXT.md` §5.
- `Content` has no general `.xnb` reader by design, and 14 of the `Media` types are shells.

[Unreleased]: https://github.com/openeggbert/cna/compare/v0.1.0-alpha.1...HEAD
[0.1.0-alpha.1]: https://github.com/openeggbert/cna/releases/tag/v0.1.0-alpha.1
