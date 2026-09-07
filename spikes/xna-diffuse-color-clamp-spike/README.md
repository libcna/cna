# `BasicEffect.DiffuseColor` above 1 on the real XNA 4.0 runtime

`plans/plan_vulkan.md` **VULKAN-197**, the CNA-wide sibling of finding **F-36**.

## The question

`VULKAN-196` established, by measurement, that XNA saturates the value
`EnvironmentMapEffect` writes to `Specular : COLOR1`. The same Direct3D 9 rule applies to
`vout.Diffuse : COLOR0`, which the **unlit** `BasicEffect` path fills with `DiffuseColor` (and, with
`VertexColorEnabled`, `vin.Color * DiffuseColor`). `BasicEffect.DiffuseColor` is a `Vector3` with no
clamp in its setter (`BasicEffect.cs:117`), so a game can hand the shader a value above 1.

Neither CNA renderer clamps it — `colored3d.vert.glsl` and `textured3d.vert.glsl` on Vulkan,
and EasyGL's `FX-123` fix touched only its two vertex-**lit** programs — so this is CNA-wide rather
than a renderer parity gap, and it needed its own measurement rather than an inherited one.

## What it does

A full-screen quad, **grey** base texture `(100,100,100)`, `LightingEnabled = false`,
`VertexColorEnabled = false`, `Alpha = 1`, at `DiffuseColor` ∈ {0.5, 1.0, 2.0, 3.0}, read back at
the centre pixel.

The texture is grey and not white on purpose: with a white texture both answers saturate at the
output and the probe could never fail. That is the trap `FX-123`'s own test records.

## Result, 2026-09-07

Prefix `~/.wine-cna-xna40`, D3D9 through DXVK, AMD Radeon 780M (RADV PHOENIX):

```
DiffuseColor 0.5 -> (50,50,50)   1.0 -> (100,100,100)   2.0 -> (100,100,100)   3.0 -> (100,100,100)
VERDICT: XNA SATURATES DiffuseColor
```

The 0.5 leg is the positive control: without it, "2.0 renders as 1.0" cannot be told apart from
"`DiffuseColor` does nothing on this stack".

### The gradient leg, and why it exists

A quad with **no texture** cannot separate the two clamp orders when it is a flat colour: the render
target write saturates either way, so `saturate(vc·d)` and `min(vc·d, 1)` land on the same pixel.
The distinction only appears where the interpolator runs between two vertices that disagree — which
is what `FX-123` is actually about. Left edge white, right edge 20 % grey, sampled at the centre:

```
gradient midpoint, DiffuseColor 1.0 -> (153,153,153)   2.0 -> (178,178,178)
GRADIENT VERDICT: saturated per vertex, then interpolated
```

`178` is the midpoint of `saturate(2.0) = 255` and `saturate(0.4) = 102`. Interpolating the raw
product first gives `1.2` at the midpoint and clips to `255`, so the two orders are **77 levels**
apart here. The `1.0` row is the geometry control: both orders agree there, and `153` is simply the
midpoint of 255 and 51.

**The caveat, stated rather than hidden:** D3D9 here is DXVK. A *"same"* answer is strong — XNA's
own shader contains no clamp, so something below it clamped, and that something is the register
semantic. A *"different"* answer would have been inconclusive, and the probe says so in its own
output.

## Running it

```bash
cd spikes/xna-diffuse-color-clamp-spike
DISPLAY=:131 ./build-and-run.sh          # WINEPREFIX=~/.wine-cna-xna40, set by the script
```

`*.exe` and `probe-output.txt` are gitignored.
