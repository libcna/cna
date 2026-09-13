# `EnvironmentMapAmount` above 1 on the real XNA 4.0 runtime

`plans/plan_vulkan.md` **VULKAN-196**, finding **F-36**.

## The question

`EnvironmentMapEffect.EnvironmentMapAmount` is not clamped by its property setter
(`EnvironmentMapEffect.cs:283`). The value reaches the shader, is written to `vout.Specular.rgb`
(`EnvironmentMapEffect.fx`, `ComputeEnvMapVSOutput`), and `Structures.fxh:156` declares that member
`Specular : COLOR1`. Direct3D 9 saturates a vertex shader's colour output registers to `[0,1]`
*before* interpolation, so the pixel stage's `lerp(color, envmap, Specular.rgb)` should **replace**
with the environment map rather than extrapolate past it.

`plans/plan_fx.md` FX-123 established that saturation against real XNA frames — but for the *lit*
programs' `oD0`/`oD1`. This probe asks the same question of the env-map factor directly, on the
effect that carries it, so the fix rests on a measurement of its own subject.

## What it does

A full-screen quad, base texture `(60,60,60)`, environment cube `(200,200,200)`,
`FresnelFactor = 0` (so `Specular.rgb` carries the amount unmodified), rendered at
`EnvironmentMapAmount` ∈ {0.5, 1.0, 2.0, 3.0} and read back at the centre pixel. The legs are
relational, never absolute — the base colour a lit `EnvironmentMapEffect` produces is not the
subject:

* **A** 0.5 and 1.0 must differ (the positive control; without it the probe cannot distinguish
  "saturated" from "the amount does nothing here", and it prints `INCONCLUSIVE`);
* **B** 1.0 and 2.0 must agree;
* **C** 1.0 and 3.0 must agree too, so B is not one value's coincidence.

## Result, 2026-09-07

Prefix `~/.wine-cna-xna40`, D3D9 through DXVK, AMD Radeon 780M (RADV PHOENIX):

```
amount 0.5 -> (100,100,100)   1.0 -> (200,200,200)   2.0 -> (200,200,200)   3.0 -> (200,200,200)
VERDICT: XNA SATURATES the amount
```

CNA's Vulkan renderer gave `(255,255,255)` at 2.0 and 3.0 before `VULKAN-196`, and reproduces all
four of XNA's values byte-for-byte after it
(`modules/graphics/examples/environmentmapeffect_amount_clamp_test.cpp`).

**The caveat, stated rather than hidden:** D3D9 here is DXVK. A *"same"* answer is strong — XNA's
own shader contains no clamp, so something below it clamped, and that something is the register
semantic. A *"different"* answer would have been inconclusive rather than proof, and the probe says
so in its own output.

## Running it

```bash
cd spikes/xna-envmap-amount-clamp-spike
DISPLAY=:131 ./build-and-run.sh          # WINEPREFIX=~/.wine-cna-xna40, set by the script
```

The prefix, the GAC layout and the `csc.exe` invocation are the ones
`spikes/xna-pixel-center-spike/` established. `*.exe` and `probe-output.txt` are gitignored.
