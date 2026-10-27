# cna-rts consumer validation

## RTS-001 — current CNA integration and custom 3D effect coverage

The September 2026 `cna-rts/missing.md` inventory was checked against the current CNA tree.

| Finding | Current disposition |
|---|---|
| Render-target `SaveAsPng`/`SaveAsJpeg` | Already fixed: both overload families call `GetPixelsForSave`, which reads the live render target. `Texture2DTests` includes a resolved-target round trip. |
| EasyGL `GetShaderDialectEXT` | Already fixed: EasyGL reports `GlslEs` or `GlslDesktop` from its active profile. |
| `kCustomEffectIn3D` false on every renderer | Fixed in RTS-001: EasyGL C1 now draws with a custom 3D shader, compares ordinary and rendered textures, and proves its uniform is applied. The shader declares EasyGL's `uRtFlipV` contract for rendered textures. The SDL_GPU SpriteBatch boundary remains. |
| No raw Color texture upload | Already available as the byte overload of `Texture2D::SetData`, including rectangle uploads; `SurfaceFormat::Color` takes exact RGBA bytes. |
| One observed window-size mismatch | No reproducible case in this review. |

`cna-rts` now calls `RenderTarget2D::SaveAsPng` directly, eliminating its obsolete screenshot
readback and second texture. Its raw-upload workaround remains harmless and can be simplified
separately.

Validation: `cna-rts` built against this CNA checkout with `OPENGLES3`; 10k headless simulation and
10k/100k/1M worker-count determinism checks passed. The private GPU runner produced a 640x480 PNG
through the direct render-target save path, and the 10k/100k/1M benchmark completed with one draw
call at each scale. CNA's focused C1 test
passed 4/4 checks and its full `EasyGL_RenderTarget_EffectSource` registration passed. The existing
`SaveAsPngTest.ResolvedRenderTargetSavesItsLivePixels` and `EasyGL_ShaderEffect_GLSL` also passed.

## RTS-002 — select HiDef in cna-rts

The first 100k GPU benchmark failed because `cna-rts` left `GraphicsDeviceManager` at its default
`Reach` profile. Microsoft XNA 4.0's `GraphicsDevice.DrawInstancedPrimitives` checks
`instanceCount` against `ProfileCapabilities.MaxPrimitiveCount`: 65,535 for Reach and 1,048,575 for
HiDef. CNA's guard and its profile-boundary tests are correct. FNA itself does not repeat the check,
which explains why the September benchmark could submit one million instances on an older tree.

`cna-rts/src/RtsGame.cpp` now requests `GraphicsProfile::HiDef` before device creation and uses
the current direct `RenderTarget2D::SaveAsPng` path for capture. A fresh build completed, then the
private GPU runner completed 10 measured frames at each of 10k, 100k and 1M entities; every row
reported one draw call. This fix belongs to the consumer repository.
