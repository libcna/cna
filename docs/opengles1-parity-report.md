# OpenGL ES 1.1 cross-renderer pixel-parity measurement (OPENGLES1-78)

**Status: 2026-07-22.** Same 39-scene corpus (`tools/xna-oracle/scenes/*.scene`), same
`tools/xna-oracle/CnaOracleRender.cpp`, same checked-in real XNA 4.0 reference images
(`tools/xna-oracle/reference/*.png`) and same `scripts/xna-diff.py` that the D3D9 (`D9-A5`) and
EasyGL (`D9-A6`) measurements already use — now also run through the OpenGL ES 1.1 renderer.

| renderer | tolerance 0 | tolerance 1 |
| --- | --- | --- |
| EasyGL (already-verified GL renderer, same machine/driver stack) | 10/39 exact | — |
| **OPENGLES1** | **6/39 exact** | **11/39 exact** |

Reproduce with:

```bash
scripts/opengles1-test-env.sh scripts/run-oracle-corpus-diff-opengles1.sh \
    ./cmake-build-opengles1/cna_oracle_render_opengles1 [tolerance]
```

This is a **measurement, not a pass/fail gate**, for the same reason `D9-A6` gives for EasyGL: the
real XNA reference images were produced through D3D9-over-DXVK, so any diff here mixes genuine
rendering differences with GPU/driver-stack differences. The script exits non-zero only if a scene
fails to *render*, never merely because it differs.

## What this found

Running the corpus was worth it on its own: it exposed two real renderer defects that the seven
targeted runtime tests (OPENGLES1-77/79) had all missed.

### 1. Sampler state was applied to the wrong texture object (3D paths)

`GraphicsDevice::applySamplerStatesToRenderer()` pushes sampler state down **before** calling
`DrawPrimitivesEx`, but GL keeps filter and wrap mode on the *texture object*, and the 3D draw
paths bind their textures **inside** that call. The `glTexParameteri` calls therefore landed on
whatever texture happened to be bound at push time, and the texture the draw actually used kept its
constructor defaults (`GL_LINEAR`/`GL_REPEAT`).

Every scene that asks for `texturefilter=Point` was consequently rendered blurred. `textured_quad`
sampled `(228,112,139)` where the reference has pure red `(255,0,0)`.

Fixed by remembering the pushed state per slot and re-applying it immediately after each bind in
the draw paths (`ApplySamplerToBoundTextureEXT`). The `SpriteBatch` path was already correct — it
binds before it applies — which is exactly why the existing sampler test passed: it went through
`SpriteBatch::Begin`, not the 3D path.

Effect on the corpus:

| scene | before | after |
| --- | --- | --- |
| `textured_quad` | 23716 px differ | **459** |
| `lit_textured_quad` | 23716 | **459** |
| `alphatest_quad` | 13587 | **382** |
| `alphatest_greaterequal_quad` | 11707 | **382** |

### 2. `TextureAddressMode::Mirror` silently degraded to `Wrap`

ES 1.1 core has only `GL_REPEAT` and `GL_CLAMP_TO_EDGE`, so `ToGLWrapMode` mapped Mirror to
`GL_REPEAT`. But mirrored repeat *is* available as the optional
`GL_OES_texture_mirrored_repeat`, which this driver exposes. Now used when present, still degrading
to Wrap (a documented deviation) when it is not. `sprite_mirror_quad` went from 2560 differing
pixels at delta 255 to **exact**.

## Where OPENGLES1 now matches EasyGL exactly

For a large part of the corpus the two renderers produce byte-identical output, i.e. the residual
difference from XNA is the *shared* GL-versus-XNA divergence rather than anything ES1-specific:

`textured_quad` (459/459), `lit_textured_quad` (459/459), all seven `alphatest_*` scenes,
`multilight_textured_quad` (307/307), `envmap_quad` (307/307), `colored3d` (12859/12859),
`cullmode_ccwface_quad`/`cullmode_none_quad` (12859/12859), plus every scene both render exactly
(`alphatest_never_quad`, `cullmode_cwface_quad`, `sprite_basic_quad`, `sprite_mirror_quad`,
`sprite_multitexture_quad`, `sprite_wrap_quad`).

## Where OPENGLES1 is still worse than EasyGL, and why

| scene(s) | EasyGL | OPENGLES1 | explanation |
| --- | --- | --- | --- |
| `skinned_*` (6 scenes) | 306–8569 | 23716–27694 | **Expected, permanent.** No fixed-function skinning (no `GL_OES_matrix_palette` implemented); these fall back to the plain colored path by design — see the deviation table in `plans/plan_opengles1.md`. |
| `envmap_specular_quad` | 307 | 23716 | **Expected, documented.** `EnvironmentMapEffect`'s specular tint from the cube alpha has no fixed-function analogue. |
| `envmap_fresnel_quad` | 18820 | 23716 | **Expected, documented.** Fresnel edge-weighting needs a per-vertex-varying blend factor ES1 cannot express. Note EasyGL diverges heavily here too. |
| `lit_textured_quad_pixellighting`, `skinned_pixellighting_*` | 7956–8569 | 23563–27694 | **Expected.** "Pixel lighting" is per-pixel by definition; ES 1.1 lighting is per-vertex and interpolated. EasyGL is closer because it has a real fragment shader. |
| `dualtexture_quad` | 307 | ~~23716~~ **307** | **Explained and fixed** (`OPENGLES1-81`): the second UV set never reached unit 1, and the 28-byte dual-UV layout did not even pass the dispatch gate. Now byte-identical to EasyGL. |
| `fog_gradient_quad` | 19661 | 23716 | **Explained, not fixable** (`OPENGLES1-82`): the scene uses an inverted fog range (`fogstart=0`, `fogend=-1`). FNA's signed-viewZ formula still yields a gradient; fixed-function fog is driven by an unsigned eye distance and clamps. EasyGL diverges here too, so it is not ES1-specific. The `FogStart == FogEnd` degenerate case, which *is* expressible, was found and fixed while investigating. |
| `colored_trianglestrip_quad` | 23716 | 23716 | Identical to EasyGL — shared divergence, not ES1-specific. |

The `sprite_flipped_quad`, `sprite_rotated_quad` and three `sprite_sortmode_*` scenes differ only
by **max per-channel delta = 1** — sub-LSB rounding, which is why the tolerance-1 column jumps from
6/39 to 11/39.

## Honest limits of this measurement

- It runs against a locally built software Mesa (`softpipe`), not real ES1 hardware, so rasterisation
  tie-breaking and interpolation may differ from a vendor driver.
- Both previously-unexplained rows have since been resolved: `dualtexture_quad` was a real defect
  (fixed, `OPENGLES1-81`) and `fog_gradient_quad` is a documented inexpressible case
  (`OPENGLES1-82`). No row in the table is unexplained any more.
- Widening the tolerance to make a red row green without a per-scene reason is not acceptable —
  the same discipline `scripts/xna-diff.py`'s own header states.
