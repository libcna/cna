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

## glTF reference assets — the procedure, and why none is committed (`GLTF-018`/`GLTF-019`)

**No third-party glTF asset is committed to this repository, and none may be without the review
below.** The conformance corpus is entirely CNA-generated (`tools/gltf_fixtures/`, 141 assets),
which is a deliberate choice rather than an absence — see the decision at the end of this section.

### Pinned optional sources (`GLTF-013`, `GLTF-014`, `GLTF-016`)

These repositories are reference inputs a developer may fetch deliberately. They are not
redistributed, linked, fetched by the build, required by CI, or used by CNA at runtime. The
machine-readable source of truth is `tools/gltf_fixtures/reference-pins.json`.

| Reference | Immutable revision | Licence summary |
|---|---|---|
| `KhronosGroup/glTF-Sample-Assets` | `2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf` | mixed per asset. The repository README is CC-BY-4.0, but each model's own `README.md`/`LICENSE.md` and attribution are authoritative; this pin reviews no individual model |
| `KhronosGroup/glTF-Asset-Generator` | `3d99767e9a67fbfe109f0d298c1e8d909bcac9db` | MIT, Copyright (c) 2017 Khronos Group; its manifests are read only from an explicit external checkout |
| `KhronosGroup/glTF-Sample-Renderer` | `863b981fb755359063e370ff7b6e956bda0716e2` | Apache-2.0; its locked npm dependencies and own third-party notices apply to a disposable developer checkout |

The detailed Asset Generator mapping and reference-renderer capture protocol are in
`docs/gltf-conformance.md` §2.9–2.10. No licence obligation from these optional sources is added to
CNA merely by recording a repository URL and revision here.

### Khronos glTF Validator CI tool (`GLTF-015`)

The generated-corpus CI gate downloads the official `KhronosGroup/glTF-Validator` native Linux
release `2.0.0-dev.3.10`, licensed under Apache-2.0. Its archive is pinned by SHA-256
`168eba887964125abe17ae97899b38d0b3cfd73c266c78424c194929ddcbc522` in
`tools/gltf_fixtures/validator-pin.json` and verified before extraction. The downloaded executable
is transient CI/developer tooling: no Validator source or binary is committed, redistributed, or
loaded by CNA at runtime.

### The procedure, before any asset is committed

Every one of these, per asset, in the commit that adds it:

1. **Identify the exact source and revision.** A repository plus a commit SHA or release tag — not
   "downloaded from the Khronos samples". The same file name has carried different licences across
   revisions of `glTF-Sample-Assets`.
2. **Record the licence and the required attribution verbatim**, from the asset's own metadata
   (`asset.copyright`, the sample repository's per-model `LICENSE.md`), not from the repository's
   top-level licence. `glTF-Sample-Assets` is a mixture: CC0, CC-BY 4.0, CC-BY-SA, and a handful
   under model-specific terms.
3. **Add a row to this file** naming the asset, the source, the revision, the licence, and the
   attribution text a shipped product would have to reproduce.
4. **Check the licence permits redistribution *in this form*.** CC-BY permits it with attribution;
   some sample models are licensed for use *with* the sample viewer rather than for redistribution
   as a standalone asset. If the answer is not clearly yes, the asset does not go in.
5. **Prefer the smallest asset that proves the point.** A licence-clean 200 KB model that exercises
   the same path is worth more than a 40 MB one, because it will still be affordable to keep in
   five years.

An asset that fails any step is **not committed** — it may be fetched at development time with
`scripts/fetch-gltf-sample-assets.sh DEST MODEL [MODEL ...]` or replaced by a generated fixture
that exercises the same feature. The script performs a sparse checkout at the exact pin above,
requires an explicit new destination and model names, and prints the available per-model licence
metadata. It deliberately does **not** declare any model reviewed or safe to redistribute.

### The decision: generated corpus committed, third-party assets fetched (`GLTF-019`)

**Committed:** CNA's own generated corpus. 141 assets and 699 files at **1.79 MiB total**, every one
emitted from a Python description, byte-identical across runs, and covered by a size budget
(`GLTF-419`). It is in the repository because CI must be able to run the whole conformance ladder
with no network at all, and because a fixture whose expected values were computed from the
specification is evidence in a way a downloaded model is not.

**Not committed:** the Khronos sample models. Fetched on demand with
`scripts/fetch-gltf-sample-assets.sh` when a developer wants them, cached outside the repository,
and never a CI dependency. The script is not called by CMake or CI and refuses to modify an
existing destination.

This is a mechanically enforced boundary, not only a statement of intent:
`scripts/check-gltf-asset-provenance.sh` verifies that every Git-tracked `.gltf`/`.glb` container is
byte-for-byte output of CNA's own corpus generator and fails on a container anywhere else. The
required glTF sanitizer workflow runs that check on every applicable change. There is currently no
third-party-asset allow-list because there is no reviewed, committed third-party glTF asset.

The reasoning, weighed as the row asks:

| | Commit a curated subset | Fetch on demand |
|---|---|---|
| Repository size | `glTF-Sample-Assets` is ~1.5 GB; even a "small" curated subset is tens of MB of binary that every clone pays for, forever, including clones that never touch glTF | nothing |
| CI reproducibility | perfect | needs a pinned revision and a network step that can fail |
| Licence exposure | every asset must clear the five steps above, and a licence change upstream is invisible to us | the fetch script records the pin; nothing is redistributed by CNA at all |
| What it proves | real-world shapes the generator does not produce | the same, when run |

The deciding argument is not size — it is that **a third-party asset is an oracle we do not
control.** Its expected values cannot be derived from the specification the way a generated
fixture's can; the most a real model can tell us is "this did not crash and looked right", which is
exactly the standard this campaign exists to replace. So real models are a *supplementary* check run
deliberately (`GLTF-405`, `GLTF-411`), not part of the gate.

**The one asset that would change this** is a large one for the performance budgets `GLTF ROBUST`
§27.2 row 9 asks for — ≥ 50 MB, ≥ 200 k triangles, ≥ 150 joints. Even that should be fetched, not
committed: it is a benchmark input, not evidence.

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
