# Direct2D vs EasyGL 2D differential corpus

plans/plan_direct2d.md D2D-129. The same public 2D commands, issued by one renderer-agnostic source, must
produce the same framebuffer under Direct2D and under EasyGL. Where they legitimately cannot, the
difference is named here rather than absorbed into a wide tolerance.

`modules/graphics/examples/cross_renderer_2d_corpus.cpp` is that source. It is the 2D counterpart of
`cross_renderer_diagnostic_scene.cpp`, which cannot serve the purpose: that one draws a `BasicEffect`
triangle through a `VertexBuffer`, and Direct2D is a permanently 2D-only renderer that rejects every
one of those calls. Both dump a 64x64 RGBA8 backbuffer in the same byte layout, so the existing
`cna_diag_compare` comparator judges the 2D corpus too and no second comparator exists to drift.

## Running it

`CNA_GRAPHICS_RENDERER` is a compile-time choice, so one build only ever links one renderer.
Comparing two therefore needs two builds' dumps plus the comparator — exactly the three-command
shape the software/EasyGL diagnostic already uses:

```bash
# EasyGL half (native Linux build)
./cmake-build-debug/cna_corpus2d_easygl /tmp/corpus2d-easygl.rgba

# Direct2D half (Windows cross-build, through the canonical virtual display)
scripts/run-direct2d-virtual-display.sh \
  bash scripts/run-wine-direct2d.sh \
  ./cmake-build-direct2d-integration/cna_corpus2d_direct2d.exe /tmp/corpus2d-direct2d.rgba

# Compare
./cmake-build-debug/cna_diag_compare /tmp/corpus2d-easygl.rgba /tmp/corpus2d-direct2d.rgba 4
```

## What the corpus covers

Four 64x16 rows, all point-sampled at integer scale over an opaque cleared background:

| Row | Commands |
|---|---|
| 1 | Whole-atlas blit, horizontal flip, vertical flip, single-texel crop |
| 2 | Four tints applied to four different atlas texels |
| 3 | The same visual colour drawn twice: premultiplied under `AlphaBlend`, straight under `NonPremultiplied` |
| 4 | `RenderTarget2D` round trip (fill, unbind, blit back at 1:1) and a scissor rectangle |

Row 3 is the sharpest check in the corpus: both halves must land on the same composited colour, so a
renderer that premultiplies twice, or not at all, differs from the other half of its own dump as well
as from the other renderer.

## The whitelist

These constructs are deliberately **outside** the corpus. Each is one where two conforming 2D
renderers may legitimately disagree pixel-for-pixel, so including any of them would force a
tolerance wide enough to hide real regressions:

| Excluded | Why |
|---|---|
| Linear filtering of a magnified sprite | The filter kernel and its edge behavior are unspecified. Direct2D's own kernel is separately pinned by D2D-81's numeric oracle. |
| Mip selection and mip-linear blending | Direct2D has authored levels and no implicit chain; EasyGL has a GL sampler chain. The selection rule is not a shared contract. |
| Anisotropic filtering, MSAA, `BlendState::Additive` | Capability-dependent; Direct2D reports all three unsupported and rejects Additive by name. |
| Rotation by a non-right angle | The rasterized coverage of a rotated quad edge is implementation defined. |
| Non-opaque destinations | Presentation-time alpha handling differs; the corpus composites onto an opaque background only. |

The tolerance argument above is `4` per channel, not a large number: with point sampling at integer
scale, every pixel in the corpus has one exact expected value, and the only legitimate spread is the
1-2 LSB of an 8-bit tint or blend rounding differently between an integer and a float pipeline. A
difference beyond that is a finding, not noise. Widening the tolerance instead of adding a row to the
whitelist table above would defeat the purpose of the corpus.
