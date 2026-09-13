# `vulkan-depth-bias-spike`

Evidence for `plans/plan_vulkan.md` **VULKAN-091**. Everything here has been run on this machine
(2026-09-04) and the numbers below are its actual output.

## The question

`Vulkan_DepthBias` failed exactly one leg of four: a flat, coplanar redraw with
`RasterizerState.DepthBias = -1e6` did not move in front of the first draw, while the tilted
`SlopeScaleDepthBias` leg of the same test — same pipeline, same `vkCmdSetDepthBias` call — did.
`EasyGL_DepthBias` passed, from an all-but-identical source, on the same virtual display.

`plan_vulkan.md` forbids re-attributing a result to the driver "without reproducing the pass on a
second driver", and the renderer itself cannot reach the second driver here: under Xvfb, RADV
reports `No DRI3 support detected - required for presentation`, so `PickPhysicalDevice` — which
requires a present-capable queue — rejects it and selects llvmpipe.

**Presentation is the only thing that blocks it.** This probe therefore does the same coplanar
experiment off screen, with no surface and no swapchain, on every physical device the loader
offers, and reads the result back with `vkCmdCopyImageToBuffer`. That is what makes RADV reachable
here at all, and the same trick is available to any other Vulkan question this environment appears
to block.

## What it proves

```
2 physical device(s)

== AMD Radeon 780M (RADV PHOENIX) (driver 104857607, subPixelPrecisionBits=8)
  depth format: D32_SFLOAT_S8_UINT (float, r per-primitive)
  [PASS] flat,   constant 0     -> (255,  0,  0) RED   (expected RED)
  [PASS] flat,   constant -1e6  -> (  0,255,  0) GREEN (expected GREEN)
  [PASS] tilted, slope 0        -> (255,  0,  0) RED   (expected RED)
  [PASS] tilted, slope -2000    -> (  0,255,  0) GREEN (expected GREEN)

== llvmpipe (LLVM 19.1.7, 256 bits) (driver 1, subPixelPrecisionBits=8)
  depth format: D24_UNORM_S8_UINT (fixed point, r = 2^-24)
  [PASS] flat,   constant 0     -> (255,  0,  0) RED   (expected RED)
  [PASS] flat,   constant -1e6  -> (  0,255,  0) GREEN (expected GREEN)
  [PASS] tilted, slope 0        -> (255,  0,  0) RED   (expected RED)
  [PASS] tilted, slope -2000    -> (  0,255,  0) GREEN (expected GREEN)

all legs behaved as Vulkan specifies
```

`vkCmdSetDepthBias`'s **constant** factor behaves exactly as the specification says on both
drivers, including the one CNA's failing test actually ran on and with the very depth format that
renderer picks. So the failure was not the driver, and it was not the renderer either.

## What it found instead

The probe places its flat triangles at depth **0.5**. `vulkan_depth_bias_test.cpp` placed its at
`z = 0` under an identity projection and described that as "depth 0.5" — which is **OpenGL's**
mapping. GL clips `z` to `[-1,1]` and maps it to `[0,1]`, so `z = 0` lands mid-range. XNA uses
Direct3D 9's convention, `z` in `[0,w]` mapping to depth `[0,1]`, and so does Vulkan. Under both,
`z = 0` is the **near plane**: the viewport depth range clamps at 0, the biased redraw stays at
exactly 0, the `LESS` test fails, and the strip stays red.

The renderer was right and the test's premise was wrong. `EasyGL_DepthBias` passes from the same
premise only because EasyGL leaves GL's `[-1,1]` clip depth in place, so the same XNA `z = 0`
lands at depth 0.5 there — a genuine cross-renderer divergence from XNA, recorded as finding F-19
in `plans/plan_vulkan.md` and owned by `VULKAN-098`.

`vulkan_depth_bias_test.cpp` now places its flat scenarios at `z = 0.5`, spans its tilted ones
0.2 to 0.8 instead of straddling the near plane, and carries a fifth leg that pins the convention:
a flat triangle **at** `z = 0` with the same `-1e6` bias must stay red.

## Running it

```bash
cd spikes/vulkan-depth-bias-spike
python3 make_spirv.py                      # regenerates probe_spirv.hpp via libshaderc
ccache g++ -std=c++17 -O1 depth_bias_probe.cpp -lvulkan -o depth_bias_probe
./depth_bias_probe
```

`glslc` and `glslangValidator` are not installed here; `libshaderc.so.1` is, and `make_spirv.py`
drives it through ctypes exactly as the renderer's own
`modules/renderers/vulkan/src/shaders/compile_shaders.py` does. `probe_spirv.hpp` is checked in so
the probe builds without it.

Exit code 0 means every leg behaved as Vulkan specifies.
