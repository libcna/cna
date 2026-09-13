# Does `SpriteBatch` leave its sampler in `GraphicsDevice.SamplerStates[0]`?

`plans/plan_vulkan.md` **VULKAN-194**.

## The question

FNA's `PrepRenderState` assigns `GraphicsDevice.SamplerStates[0] = samplerState`
(`SpriteBatch.cs:1426`). CNA carries the batch's sampler to the renderer down the
`ISpriteBatchRenderer` path instead and never touches the device's collection — so a game reading
`SamplerStates[0]` sees the `LinearWrap` default, and, the half with pixels attached, a **3D draw
issued after a sprite batch** samples with `LinearWrap` where XNA would use the batch's state.

`VULKAN-166` published slots 1–15 from the sprite path and deliberately left slot 0 alone, because
the batch already writes it down its own route. Changing that touches all twelve renderers at once,
so it wanted a measurement first.

## Result, 2026-09-07

Prefix `~/.wine-cna-xna40`, D3D9 through DXVK, AMD Radeon 780M (RADV PHOENIX). Pre-set
`PointClamp`, batch sampler `PointWrap`, `SpriteSortMode.Deferred`:

```
before Begin: SamplerStates[0] = SamplerState.PointClamp AddressU=Clamp Filter=Point
after  Begin: SamplerStates[0] = SamplerState.PointClamp AddressU=Clamp Filter=Point
after  End  : SamplerStates[0] = SamplerState.PointWrap  AddressU=Wrap  Filter=Point
control: u = 0.25 -> (255,0,0)   u = 0.75 -> (0,255,0)   (want red then green)
3D draw after the batch, sampled at u = 1.25 -> (255,0,0)
VERDICT: the 3D draw used the BATCH's sampler (Wrap)
```

Three things, and the middle one is not what FNA's source reads like at a glance:

1. **`Begin` does not assign it.** The value is still `PointClamp` immediately after `Begin`.
2. **The flush does.** `PrepRenderState` runs at `Begin` only for `SpriteSortMode.Immediate`; for
   `Deferred` it runs at `FlushBatch`, so the assignment lands at `End`.
3. **It reaches the next draw.** A 3D quad sampled at `u = 1.25` wraps, so this is not merely a
   property that changed value.

## The first draft measured nothing, and said so anyway

The sample point was `u = 1.5`. On a **2×1** texture that is a non-discriminator: `Wrap(1.5) = 0.5`
selects texel 1 and `Clamp(1.5) = 1.0` selects texel 1 as well, so *both* modes read green — and the
probe, having no control leg for that question, printed `VERDICT: the 3D draw used the PRE-SET
sampler (Clamp)`. The **opposite** of the truth, stated confidently.

`u = 1.25` separates them (`Wrap → 0.25 →` texel 0, red; `Clamp → 1.0 →` texel 1, green), and two
control legs inside `[0,1]` now run first so that a leg reading the wrong texel for any other reason
reports `INCONCLUSIVE` instead of voting. Every probe in this directory has that shape for this
reason; this one had to learn it twice.

## Running it

```bash
cd spikes/xna-spritebatch-sampler0-spike
DISPLAY=:131 ./build-and-run.sh          # WINEPREFIX=~/.wine-cna-xna40, set by the script
```

`*.exe` and `probe-output.txt` are gitignored.
