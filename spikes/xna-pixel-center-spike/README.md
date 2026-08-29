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

## What this settles

`xnaPixelCenterScale_` is **not** a divergence to be removed. It is what makes EasyGL agree with
XNA. Leg U2 of the point-sampling contract encodes the OpenGL/Direct3D 10 convention, which XNA
does not use, and the six renderers that pass it are passing an expectation XNA never held.

Not answered here: whether the other three tests that the correction breaks
(`EasyGL_DescriptorCapacityContract`, `ShadowVisibilityTest.TheFilterRadiusChangesHowSoftTheEdgeIs`,
`CnaGltfConformanceL6`) encode the same wrong convention, or fail for unrelated reasons. Each needs
the same treatment: state the expectation in terms of a pixel-centre convention, then ask XNA.
