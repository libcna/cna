This project contains code derived from or based on portions of FNA.
FNA is licensed under the Microsoft Public License (Ms-PL).
FNA copyright: Ethan Lee and the MonoGame Team.

## Avatar real-rendering extension content (NOXNA)

The optional, opt-in Avatar real-rendering extension (`AvatarRenderer::EnableRealRenderingEXT`,
see `docs/avatar-real-rendering-ext.md`) is designed to use content from the following sources.
This is a CNA-original addition, not a reproduction of Microsoft's proprietary Xbox Avatar art —
see that document for details. As of this writing the asset-acquisition step
(`tools/avatar_asset_pipeline/`) requires manual GUI/browser interaction and has not yet been run;
no third-party binary content is currently bundled in this repository.

- **MakeHuman** (https://www.makehumancommunity.org/) — the software itself is licensed AGPL, but
  mesh/rig data exported via the file-export function of an official, unmodified MakeHuman
  binary (no third-party plugins) is released by the MakeHuman project under a CC0 (public
  domain) exception. Any base body mesh produced this way and bundled with CNA will be CC0.
- **Mixamo** (https://www.mixamo.com/, Adobe Inc.) — animation clips are free/royalty-free for use
  baked into a shipped project (personal, commercial, or non-profit), per Adobe's Mixamo terms.
  Mixamo's terms do not permit redistributing the raw downloaded FBX/animation assets as a
  standalone, swappable asset pack. Consistent with this, only CNA's own converted binary
  (`.skeleton.bin`/`.clip.bin`) and JSON (`.skinnedmodel.json`) content is intended to ship —
  never the original Mixamo FBX files themselves.

## Microsoft XNA 4.0 Stock Effects HLSL (D3D9 backend)

`src/CNA/Internal/Backends/D3D9/shaders/xna/` contains 10 HLSL source files
(`BasicEffect.fx`, `AlphaTestEffect.fx`, `DualTextureEffect.fx`, `EnvironmentMapEffect.fx`,
`SkinnedEffect.fx`, `SpriteEffect.fx`, `Macros.fxh`, `Common.fxh`, `Lighting.fxh`,
`Structures.fxh`) copied **verbatim, byte-for-byte**, from FNA's own
`src/Graphics/Effect/StockEffects/HLSL/` directory, which in turn vendors them from Microsoft's
Stock Effects sample project. Not one line has been edited (`plan_dx9.md` design decision 3);
`scripts/verify-d3d9-stock-effects-vendored.sh` diffs them against the FNA tree and fails on any
delta. Licensed under the Microsoft Permissive License (Ms-PL) — the full license text is also
checked in at `src/CNA/Internal/Backends/D3D9/shaders/xna/LICENSE`.

CNA compiles these sources itself (`D3DCompile`, Microsoft's own `vs_2_0`/`ps_2_0` targets) rather
than shipping Microsoft's pre-built `.fxb` bytecode — see `plan_dx9.md`'s "CNA's divergences from
XNA 4.0" section and design decision 4 for why, and `src/CNA/Internal/Backends/D3D9/shaders/xna/README.md`
for the full list of compiled entry points.

## FNA3D (FNA3D renderer dependency)

The `FNA3D` graphics renderer links FNA3D, the 3D graphics library for FNA, by Ethan Lee, at the
pinned revision `3240147` (release tag 26.08). FNA3D is licensed under the **zlib license**; see
the fetched checkout's own `LICENSE`. It carries MojoShader (by Ryan C. Gordon, zlib license) as a
git submodule, which FNA3D's own CMake compiles. `cmake/ThirdPartyFNA3D.cmake` fetches an
unmodified upstream checkout at that revision and builds it from source; nothing is copied into
the CNA source tree and no local patch is carried.

## Microsoft XNA 4.0 Stock Effects compiled binaries (FNA3D renderer)

`modules/renderers/fna3d/effects/` contains six compiled Direct3D 9 Effect Framework binaries
(`BasicEffect.fxb`, `AlphaTestEffect.fxb`, `DualTextureEffect.fxb`, `EnvironmentMapEffect.fxb`,
`SkinnedEffect.fxb`, `SpriteEffect.fxb`) copied **verbatim** from FNA's own
`src/Graphics/Effect/StockEffects/FXB/` directory at FNA revision
`30a427365a1684d6599329560efcfb90233701a9`, together with that directory's own license file
(`LICENSE.StockEffects`). They are the `fxc`-compiled form of Microsoft's Stock Effects sample
sources and are licensed under the Microsoft Permissive License (Ms-PL).

Unlike the D3D9 renderer above, which compiles the HLSL sources itself, this renderer ships the
pre-built bytecode because it has no alternative: `FNA3D_CreateEffect` — FNA3D's only shader entry
point — takes exactly this format, and compiling `.fx` to `.fxb` requires `fxc` from the DirectX
SDK, which exists on no platform CNA builds on. See
`modules/renderers/fna3d/effects/README.md` for the full provenance table and
`docs/fna3d-renderer.md` for what it means for the renderer's capability boundary.

## Skia (optional Skia 2D raster / Ganesh backend dependency)

The experimental `SKIA` graphics backend links against Skia, Google's 2D graphics library, at the
pinned revision `ebf50520d720a1ce9d842d942d04c6c39c3fbc7b`. Skia is licensed under the BSD 3-Clause
License; copyright Google Inc. CNA's CMake integration (`cmake/ThirdPartySkia.cmake`,
`cmake/ThirdPartySkiaGanesh.cmake`) links a separately built, unmodified upstream static-archive
artifact; Skia's source is not copied into the CNA source tree, matching `wgpu-native`'s pattern
below. See the upstream checkout's own `LICENSE` file for the complete license text.

Two mutually exclusive GN-built artifacts of the same pinned checkout exist: the validated CPU
raster artifact (`docs/skia-developer-build.md`, the release-gated `CNA_GRAPHICS_BACKEND=SKIA`
selection) and a separately pinned Ganesh/OpenGL artifact (`docs/skia-ganesh-artifact.md`, SKIA-159,
not yet wired into any backend selection). Both are the same upstream project and license.

## wgpu-native (optional WebGPU backend dependency)

The experimental `WEBGPU` graphics backend uses `wgpu-native`, a native implementation of the
WebGPU C API maintained by the gfx-rs project. CNA's CMake integration downloads or consumes an
unmodified upstream binary release; the library is not copied into the CNA source tree.
`wgpu-native` is available under the Apache License 2.0 and MIT License. See the upstream release
package for the complete license texts and notices that apply to the selected binary.

## DiligentCore (DILIGENT backend dependency)

The experimental `DILIGENT` graphics backend uses DiligentCore, the render-device/swap-chain layer
of Diligent Engine by Diligent Graphics LLC. CNA's CMake integration fetches an unmodified upstream
checkout at the pinned tag `v2.5.6` (recursive submodules included) at configure time and builds it
from source; nothing is copied into the CNA source tree and no local patch is carried. DiligentCore
is available under the Apache License 2.0. Its own vendored third-party components -- glslang,
SPIRV-Tools, SPIRV-Cross, SPIRV-Headers, Vulkan-Headers, volk and xxHash -- carry their own
licenses; see `License.txt` and `ThirdParty/` in the fetched upstream checkout for the complete
license texts and notices.

## sokol (SOKOL backend dependency)

The experimental `SOKOL` graphics backend uses `sokol_gfx.h` and `sokol_log.h` from the sokol
single-header library collection by Andre Weissflog. CNA's CMake integration fetches an unmodified
upstream checkout at a pinned commit at configure time; the headers are not copied into the CNA
source tree. sokol is available under the zlib/libpng license. See `LICENSE` in the fetched
upstream checkout for the complete license text.

The `SOKOL` backend's shaders are compiled offline by `sokol-shdc`, from the companion sokol-tools
project (MIT licensed). The generated header checked in at
`src/CNA/Internal/Backends/Sokol/shaders/sokol_shaders.hpp` is machine-generated output derived
from CNA's own shader sources; the `sokol-shdc` binary itself is not vendored and is not required
for an ordinary build.

## TinyGL (TINYGL renderer dependency)

The `TINYGL` graphics renderer uses TinyGL, originally by Fabrice Bellard and currently maintained
as the [C-Chads/tinygl](https://github.com/C-Chads/tinygl) fork by Gek (DMHSW) and the C-Chads.
CNA's CMake integration (`cmake/ThirdPartyTinyGL.cmake`) fetches an unmodified upstream checkout at
a pinned commit at configure time and builds its own `tinygl-static` target; no TinyGL source is
copied into the CNA source tree. See `LICENSE` in the fetched upstream checkout for the complete
license text.

TinyGL is distributed under a zlib-style license with one clause that differs from plain zlib and is
the reason this section is required rather than merely courteous:

> Copyright (C) 1997-2021 Fabrice Bellard, Gek (DMHSW), C-Chads
>
> The origin of this software must not be misrepresented; you must not claim that you wrote the
> original software. If you use this software in a product, an acknowledgment in the product and its
> documentation *is* required.

This section, together with `docs/tinygl-renderer.md` and `plan_tinygl.md`, is that acknowledgment.
A build configured with any other `CNA_GRAPHICS_RENDERER` value does not fetch, build or link
TinyGL at all.
