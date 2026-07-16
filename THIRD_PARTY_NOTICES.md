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

## wgpu-native (optional WebGPU backend dependency)

The experimental `WEBGPU` graphics backend uses `wgpu-native`, a native implementation of the
WebGPU C API maintained by the gfx-rs project. CNA's CMake integration downloads or consumes an
unmodified upstream binary release; the library is not copied into the CNA source tree.
`wgpu-native` is available under the Apache License 2.0 and MIT License. See the upstream release
package for the complete license texts and notices that apply to the selected binary.
