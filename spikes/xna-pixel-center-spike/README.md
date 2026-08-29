# XNA 4.0 rasterization-convention probe

Answers, on the **real XNA 4.0 runtime**, a question CNA could not settle from its own tests:
when a point-sampled texture is magnified by a non-integer factor onto a 3D quad, which pixel-centre
convention does XNA follow?

Two CNA expectations contradict each other, and each is defended by a passing test:

* `EasyGL_XnaPixelCenter` demands that a 1x1 screen-space triangle cover one pixel. Only EasyGL is
  held to it. It is satisfied by `EasyGLRenderer`'s `xnaPixelCenterScale_` (63/128 of a window
  pixel, the Wine/MonoGame value), applied to every 3D draw.
* `*_PointSamplingContract` leg U2 demands that destination pixel *x* select texel
  `floor((x+0.5)/w * texw)` -- half-integer pixel centres. Seven renderers are held to it. EasyGL is
  the only one that fails it, by 19 pixels of 100, *because of* the correction above.

## How to run

    ./build-and-run.sh              # WINEPREFIX=~/.wine-cna-xna40, DISPLAY=:131

Needs the XNA 4.0 redistributable in that prefix (it is in the GAC there) and `csc.exe` from
.NET 4.0. The prefix routes Direct3D 9 through **DXVK**, not wined3d -- so what is measured is a
faithful D3D9 implementation, not a D3D9-over-OpenGL translation that might itself introduce the
half-pixel shift under investigation.

## Result

    LEG-A XNA 1x1 screen-space triangle: covered=1
    LEG-A control triangle: covered=136
            y=16 ..#...                      <- the covered pixel is (16,16), the top-left vertex

    U1 8x4 -> 16x8: matches HALF-INTEGER 128/128, matches INTEGER 128/128
    U2 3x3 -> 10x10: matches HALF-INTEGER  81/100, matches INTEGER 100/100
            row0 selected i: 0000111222

**XNA follows the Direct3D 9 integer-pixel-centre convention**, exactly and without exception:

* Leg A reproduces on the real runtime the behaviour `EasyGL_XnaPixelCenter` was written to defend.
  The single covered pixel is the triangle's top-left vertex, which is what D3D9's top-left fill
  rule predicts.
* U1 is *blind* to the question: at an integer magnification both conventions select the same
  texels (128/128 for both). This is why U1 passes on EasyGL while U2 does not.
* U2 separates them, and XNA lands on **integer centres, 100/100**. Row 0 selects
  `0 0 0 0 1 1 1 2 2 2` = `floor(x/10*3)`. The half-integer convention would give
  `0 0 0 1 1 1 1 2 2 2` = `floor((x+0.5)/10*3)`.

The two conventions differ on exactly **19 of the 100** pixels -- the same 19 that
`EasyGL_PointSamplingContract` reports as mismatches. EasyGL's output is XNA-correct and the test
marks it wrong.

## Second question: the 2x2 identity fixture

`descriptor_capacity_contract_test.cpp` draws a 2x2 texture onto a 2x2 target -- "one texel to one
pixel" -- and requires every channel of the readback to be a clean 0 or 255, for all 27 sampler
states it sweeps. With the correction on, EasyGL fails 15 of the 27, and 142 of 256 in the companion
leg. Those counts are not arbitrary: exactly five of the nine filters magnify with LINEAR
(`Linear`, `Anisotropic`, `LinearMipPoint`, `MinPointMagLinearMipLinear`, `MinPointMagLinearMipPoint`),
and 5x3 = 15, while 256 x 5/9 = 142. Only the linear-magnifying states fail.

    LEG-C 2x2 -> 2x2 POINT : dirty=0/4   (255,0,0) (0,255,0) (0,0,255) (255,255,0)
    LEG-C 2x2 -> 2x2 LINEAR: dirty=3/4   (255,0,0) (128,128,0)* (128,0,128)* (128,128,64)*

XNA blends three of the four pixels, at exactly 128 -- the 50/50 weight that integer pixel centres
predict, because a pixel centre at window x=1 lands on texture coordinate 1.0, halfway between the
texel centres at 0.5 and 1.5. The contract's "one texel to one pixel" holds in XNA only for
magnifying filters that do not interpolate.

So this fixture encodes the same OpenGL convention leg U2 does, and EasyGL fails it for the same
reason: it is right and the expectation is not.

## The one real defect

Disabling the correction and rebuilding **every** dependent binary (a stale test executable will
otherwise keep the old renderer linked in and report the opposite result) gives:

| test | correction off | correction on |
|---|---|---|
| `EasyGL_XnaPixelCenter` | FAIL | pass |
| `EasyGL_PointSamplingContract` | pass | FAIL -- test is wrong, see above |
| `EasyGL_DescriptorCapacityContract` | pass | FAIL -- test is wrong, see above |
| `ShadowVisibilityTest.TheFilterRadiusChangesHowSoftTheEdgeIs` | pass | **FAIL -- genuine** |

`GltfConformanceL6.ViewAndProjectionReachEveryDrawUnaltered` was previously counted with these. It
is not: it fails only under `ctest -j` and passes serially every time.

The shadow test is the one failure the correction actually causes. Its assertion is
`countPartials(2) > 0` -- a 5x5 PCF kernel must produce at least one partially shadowed pixel -- and
with the correction on it produces none, while `TheCastersShadowIsVisibleOnTheGround` still passes,
so the shadow is present and only its softness is gone. XNA cannot arbitrate this one: it has no
shadow-map API. A half-pixel geometry shift should not flatten a PCF kernel, so this is a real
interaction defect in the CNAEXT shadow layer, not a wrong expectation.

## What this settles

`xnaPixelCenterScale_` is **not** a divergence to be removed. It is what makes EasyGL agree with
XNA. Leg U2 of the point-sampling contract encodes the OpenGL/Direct3D 10 convention, which XNA
does not use, and the six renderers that pass it are passing an expectation XNA never held.

Not answered here: why a half-pixel geometry shift removes every intermediate value from a 5x5
PCF kernel. That is the shadow layer's own defect and needs its own investigation -- the correction
only exposes it.
