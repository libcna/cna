# occlusion-count-spike

Existence gate for the precise-target path in `EasyGLOcclusionQueryRenderer`
(`plans/plan_fx.md`-adjacent work for SAMPLE-041, LensFlare).

## What it proves

XNA's `OcclusionQuery.PixelCount` is a **count of fragments that passed**. OpenGL ES 3.0 and
WebGL 2 have no query target that produces one -- their core occlusion target is the boolean
`GL_ANY_SAMPLES_PASSED` -- so CNA's EasyGL renderer asks the driver for `GL_SAMPLES_PASSED` first
and falls back to the boolean when it is refused.

Without this probe the "precise" arm would be unproven: on the `OPENGLES3` profile this machine
refuses the enum, so nothing in the campaign's own renderer boundary ever executes it.

Measured on this machine, 2026-08-27:

```
GL_VERSION: 4.5 (Compatibility Profile) Mesa 25.0.7-2+deb13u1
GL_SAMPLES_PASSED accepted: 1
fragments counted: 4096 (a boolean target would say 1)
```

4096 is exactly the 64x64 viewport the probe fills, so the value is a real tally rather than a
flag. The **same driver** answers `GL_SAMPLES_PASSED accepted: 0` under the OpenGL ES 3.2 context
CNA's `OPENGLES3` profile creates -- that asymmetry is the whole reason the renderer resolves the
target at run time instead of picking one at compile time.

## Running it

```bash
ccache g++ -O1 occlusion_count_probe.cpp -lGL -lX11 -o occlusion_count_probe
DISPLAY=:122 ./occlusion_count_probe    # exit 0 = precise count available
```

Exit codes: 0 = precise count available, 1 = boolean-only, 2 = no usable GL context.
