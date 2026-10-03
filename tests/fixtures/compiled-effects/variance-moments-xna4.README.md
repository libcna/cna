<!-- SPDX-License-Identifier: MS-PL -->

# Two-tap blur of variance shadow map moments

`variance-moments-xna4.fxb` is the exact `EffectReader` payload of `VarianceMoments.xnb`, built
from `variance-moments-xna4.fx` (CNA's own source) by Microsoft XNA Game Studio 4.0's
`EffectImporter`/`EffectProcessor` (Windows/HiDef) under Wine, through cna-cs-samples'
`scripts/build-xna-content.sh`.

It is a sprite pixel shader with no vertex shader: SpriteBatch supplies both the vertex shader and
the texture in sampler 0, and the test sets `Weight` and `Offset`. It averages two taps of a
Vector2 render target, as willcraftia's LiSPSM demo blurs its variance shadow map. Direct3D 9
sampled a Vector2 texture at full 32-bit precision; read at fp16, moments near 1 lose the
difference between depth squared and the square of depth that a variance shadow map is made of
(cna-cs CSX-136, `mojoshader-6333f74-glsles3-sampler-precision.patch`).

- Size: 720 bytes
- SHA-256: `f1828e7eb9d2b2fa432f23dc941886b53aae2c011f28f14263b572435872d5d4`
