<!-- SPDX-License-Identifier: MS-PL -->

# Authentic XNA 4 Racing NormalMapping effect

`racing-normal-mapping-xna4.fxb` is the exact `EffectReader` payload extracted from
`Shaders/NormalMapping.xnb` after building the original Microsoft XNA 4 Racing Game Kit with
Microsoft XNA Game Studio 4.0 on Windows 7. The source asset is
`RacingGameContent/Shaders/NormalMapping.fx`; no CNA, FNA, MonoGame, or modern-repository content
tool participated in its production.

The fixture retains the Shader Model 1 techniques whose passes assign sampler parameters directly
to pixel sampler registers and calculate pixel shader constants through pass-state preshaders. It
therefore covers the legacy path that ordinary Shader Model 2 sampler reflection does not exercise.

- Size: 82,656 bytes
- SHA-256: `35d2d00b6f122343825c6434b70c3f6a192233f50aa3d38f9ffbbdbf427dcae6`
