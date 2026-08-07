# Skia 2D XNA-oracle policy

SKIA-108 promotes the complete 2D-only portion of the shared real-XNA 4.0 image corpus into a
Skia CTest. The gate renders through CNA's public `Game`, `GraphicsDevice`, `Texture2D`, and
`SpriteBatch` APIs, then compares the result with the checked-in PNG produced by the real XNA
runtime. It does not generate a second expectation from Skia implementation details.

## Closed scope

The runner discovers every scene containing `spritebatchmode=true` and requires a unique row for
it in `tools/xna-oracle/skia-2d-policy.tsv`. There are nine such scenes. A newly added SpriteBatch
scene therefore fails the policy audit until its real-XNA result and measured tolerance have been
reviewed. A stale policy row fails as well.

The other 30 corpus scenes use stock effects, vertex declarations, primitives, lighting, fog,
alpha test, skinning, cube sampling, or another 3D draw path. They remain outside this gate because
the accepted 2D-only decision in `skia-3d-emulation-adr.md` requires those Skia calls to reject.
Running them and widening the image tolerance would conceal an intentional capability boundary.

## Per-scene policy

`raw differences` means pixels with any non-zero RGBA delta. The RGB and alpha thresholds are
evaluated independently. A row may also cap the number of raw differences and confine all of them
to a rectangle. Passing the channel threshold alone is therefore insufficient.

| Scene | XNA behavior covered | RGB / alpha tolerance | Raw-difference bound | Rationale |
|---|---|---:|---:|---|
| `sprite_basic_quad` | basic textured SpriteBatch draw | 0 / 0 | 0 | Pixel-exact. |
| `sprite_flipped_quad` | horizontal flip with default linear sampling | 1 / 0 | At most 1,591, all inside `(88,88,80,80)` | Skia and XNA differ only in 8-bit bilinear interpolation rounding. Geometry, clear pixels, and alpha remain exact. |
| `sprite_mirror_quad` | PointMirror source addressing | 0 / 0 | 0 | Pixel-exact. |
| `sprite_multitexture_quad` | texture switch and batch flush | 0 / 0 | 0 | Pixel-exact. |
| `sprite_rotated_quad` | 90-degree rotation and non-zero origin with default linear sampling | 1 / 0 | At most 1,591, all inside `(48,48,80,80)` | The same measured bilinear rounding as the flipped scene, confined to the transformed sprite footprint. |
| `sprite_sortmode_backtofront_quad` | descending layer sort and straight-alpha blend | 0 / 0 | 0 | Pixel-exact, including output alpha. |
| `sprite_sortmode_deferred_quad` | submission order and straight-alpha blend | 0 / 0 | 0 | Pixel-exact, including output alpha. |
| `sprite_sortmode_fronttoback_quad` | ascending layer sort and straight-alpha blend | 0 / 0 | 0 | Pixel-exact, including output alpha. |
| `sprite_wrap_quad` | PointWrap source addressing | 0 / 0 | 0 | Pixel-exact. |

The two one-byte allowances are interpolation-rounding evidence, not a general antialiasing
tolerance. No current scene demonstrates a Skia/XNA geometric coverage-antialias difference, so
none is allowed. Any future variance needs its own policy row, measured footprint, channel split,
pixel budget, and explanation. A geometry shift, address-mode error, draw-order error, clear-color
change, alpha error, or delta greater than one remains a hard failure.

## Defect found by the oracle

The initial sort-scene comparison had exact RGB but alpha 255 instead of XNA's 159 on all 6,400
sprite pixels. Mapping `BlendState::NonPremultiplied` directly to Skia SourceOver had silently used
SourceOver's alpha equation rather than XNA's independent
`Sa*Sa + Da*(1-Sa)` equation. `NonPremultiplied` and `Additive` now use bounded runtime blenders
that compute their independent XNA color and alpha results and premultiply the completed result for
SkSurface storage. `Skia_ColorWrite_Policy` also checks both alpha equations through the public API,
so correctness no longer depends only on image-corpus coverage.

## Running the gate

Build `cna_oracle_render_skia`, provide an X11 display, and run:

```bash
ctest --test-dir cmake-build-skia -R '^Skia_XNA_2D_Oracle$' --output-on-failure
```

For a direct run:

```bash
scripts/run-skia-2d-oracle-diff.sh cmake-build-skia/cna_oracle_render_skia
```

The runner is native Linux code and does not need Wine or an installed XNA runtime. It uses the
real-XNA PNGs already checked into `tools/xna-oracle/reference`. `scripts/xna-diff.py` retains its
original exact `--tolerance` interface for the D3D9/EasyGL workflows and adds the separate
RGB/alpha, raw-count, and footprint controls used by this Skia gate.
