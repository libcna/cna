# `RasterizerState.MultiSampleAntiAlias` on the real XNA 4.0 runtime

`plans/plan_vulkan.md` **VULKAN-099**.

## Why this exists

`VULKAN-096` measured all 34 `RasterizerState` fields and found exactly one that reaches no
renderer: `MultiSampleAntiAlias`. The value is stored on `RasterizerState`, read by
`EffectTranslation.cpp`'s `.fx` state parsing and by the C ABI's own round-trip, and dropped at
`IGraphicsRenderer::ApplyRasterizerState`, whose signature is
`(cullMode, fillMode, scissorTestEnable, depthBias, slopeScaleDepthBias)` — five parameters, no
sixth.

Adding a sixth parameter changes an interface twelve renderer families implement, so `VULKAN-099`
refuses to do it on a reading of the name. What the flag *promises* is narrower than it sounds: on
D3D9 it is `D3DRS_MULTISAMPLEANTIALIAS`, which gates multisample rasterization for the draw, while
D3D11's nearest equivalent (`RasterizerDesc.MultisampleEnable`) affects only line and point
antialiasing — the render target's sample count governs everything else, independently. Which of
those XNA 4.0 actually does is a measurement, not a reading, and this is the measurement.

## What it measures

One triangle with a shallow diagonal edge (4,60)→(60,20), and one line along the same edge, each
rendered three ways and scored by counting the pixels that come back as a *blend* of the fill and
clear colours — which is what antialiasing produces and what its absence does not:

| Configuration | Target | `MultiSampleAntiAlias` |
|---|---|---|
| `NOMS`    | no multisampling | `true` (irrelevant without samples) |
| `MS4-on`  | 4× multisampled  | `true`  |
| `MS4-off` | 4× multisampled  | `false` |

`MS4-on` versus `NOMS` is reported **first and separately**, because nothing below it can be
concluded without it: this prefix runs D3D9 through DXVK, so a flag that looks inert may be inert
in the translation layer rather than in XNA. If `MS4-on` shows no more antialiasing than `NOMS`,
the run prints `INCONCLUSIVE` and stops there rather than reporting the flag as ignored.

The line leg is separate on purpose. If the triangle and the line disagree, that disagreement *is*
the answer — it is exactly the D3D9-versus-D3D11 narrowing described above.

## Running it

```bash
cd spikes/xna-multisample-antialias-spike
DISPLAY=:131 ./build-and-run.sh          # WINEPREFIX=~/.wine-cna-xna40, set by the script
```

The prefix, the GAC layout and the `csc.exe` invocation are the ones
`spikes/xna-pixel-center-spike/` established; see its README for how the prefix was built. Output
goes to stdout and to `probe-output.txt` next to the executable.

`*.exe` and `probe-output.txt` are gitignored; the `.cs` source and this file are the record.

## Result

Recorded in `plans/plan_vulkan.md` VULKAN-099 and, once the row closes, in
`docs/vulkan-renderer.md`. This file deliberately does not duplicate the numbers — a spike README
that carries its own copy of a measurement is how two versions of it start to exist.
