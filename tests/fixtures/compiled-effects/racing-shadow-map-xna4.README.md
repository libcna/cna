<!-- SPDX-License-Identifier: MS-PL -->

# Authentic XNA 4 Racing ShadowMap effect

`racing-shadow-map-xna4.fxb` is the exact `EffectReader` payload extracted from
`Shaders/ShadowMap.xnb` after building the original Microsoft XNA 4 Racing Game Kit with
Microsoft XNA Game Studio 4.0 on Windows 7. The source asset is
`RacingGameContent/Shaders/ShadowMap.fx`; no CNA, FNA, MonoGame, or modern-repository content
tool participated in its production.

The fixture deliberately retains the complete legacy technique set emitted by the XNA 4
EffectProcessor. It covers repeated Effect Framework object records, type-1 auxiliary records for
empty pass values, and the Shader Model 1 `TEXCRD` opcode.

- Size: 17,404 bytes
- SHA-256: `0014e2286cdd67c96f7ab24c7bf311c533e1b12461d1cb9a5453a068c8842d5d`
- Expected raw Effect Framework reflection: 19 parameters, 4 techniques, 4 passes, 20 objects
- Expected XNA public reflection: 16 parameters (the three sampler objects are internal)
