# Does XNA scale a specular highlight by the per-vertex colour?

`plans/plan_vulkan.md` **VULKAN-205**.

## Why the question needed a probe at all

CNA's Vulkan renderer has two vertex-colour lit paths and they disagree about where the colour
multiplies:

* **BasicEffect** (`VULKAN-200`) follows FNA — `vout.Diffuse *= vin.Color` in the vertex stage,
  inside Direct3D 9's `oD0` saturate, and the pixel stage's `AddSpecular` then adds
  `Specular * color.a`, so the colour's **RGB never touches the highlight**.
* **SkinnedEffect** (`CNB-67`, a CNA extension) multiplies the whole fragment by `vc.rgb`
  **after** the specular has been added, which tints the highlight, and — in the per-vertex
  variant — applies the colour after the clamp rather than inside it.

XNA has no `SkinnedEffect` vertex-colour variant, so there is no direct measurement to make. There
**is** one for `BasicEffect`, which XNA does have, and that settles the pattern the extension should
follow instead of an argument from resemblance.

## What it does

The scene makes the specular the *only* contribution, so the answer cannot be diluted by a diffuse
term:

* `AmbientLightColor = 0`, every light's `DiffuseColor = 0`, `EmissiveColor = 0` — the diffuse term
  is exactly zero, so every lit pixel **is** the highlight;
* `DirectionalLight0.SpecularColor` and `BasicEffect.SpecularColor` both white, `SpecularPower = 1`;
* normal `(0,0,1)`, eye on `+Z`, light direction `(0,0,-1)` — the half-vector is the normal, so the
  highlight is at full strength.

Then the quad is drawn twice: with a **white** vertex colour (the control — is there a highlight at
all?) and with a **20 % grey** one.

`VertexPositionColor` is not enough: `LightingEnabled` makes XNA refuse the draw with
*"Normal0 is missing"*, so the probe declares its own `Position + Normal + Colour` vertex type.

## Result, 2026-09-07

Prefix `~/.wine-cna-xna40`, D3D9 through DXVK, AMD Radeon 780M (RADV PHOENIX):

```
vertex colour white -> (249,249,249)   20% grey -> (249,249,249)   black -> (249,249,249)
VERDICT: XNA does NOT scale the specular highlight by the vertex colour
BLACK LEG: a black vertex colour leaves the highlight standing -- the colour zeroes the
           DIFFUSE only, and AddSpecular adds on top of it.
```

### The black leg, and why it was added afterwards

The first two columns settle the question the probe was written for. The third settles a *different*
one that only surfaced once the fix landed: `easygl_skinnedeffect_vertexcolor_test.cpp` asserts that
a **black** per-vertex colour zeroes the result *"regardless of the lighting math"*, and that
expectation started failing on Vulkan the moment the highlight stopped being multiplied by the
colour.

It would have been easy, and wrong, to adjust the test to match the new renderer behaviour. The
black leg is what decides whose expectation is XNA's — and it is the renderer's. A black vertex
colour zeroes the diffuse and leaves the highlight at full strength, because `AddSpecular` adds
`Specular * color.a` **after** the colour has been folded into the diffuse, and a black colour still
carries alpha 1. The shared test's claim is true of the diffuse and false of the specular; it now
switches specular off for that quad so it asserts the half that is true everywhere.

## The control leg earned its place on the first run

With `Projection = Matrix.Identity` the quad sits at view **z = −3** and is clipped away entirely,
so **both** legs read `(0,0,0)`. The probe printed `INCONCLUSIVE` rather than a verdict — because
"white and grey agree" is exactly what a scene rendering nothing also looks like. Without the
"is there a highlight at all?" check it would have reported *"XNA does not scale the highlight"*
from two black pixels, and the row built on it would have been confidently wrong for the right
reason. The projection is a real perspective one now.

## Running it

```bash
cd spikes/xna-vertex-color-specular-spike
DISPLAY=:131 ./build-and-run.sh          # WINEPREFIX=~/.wine-cna-xna40, set by the script
```

`*.exe` and `probe-output.txt` are gitignored.
