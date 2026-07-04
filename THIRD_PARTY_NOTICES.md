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