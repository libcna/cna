# `CNA::Graphics` engine-layer revisions

`plans/plan_modern.md` **MOD-1904**. `CNA_CNAEXT_ENGINE_VERSION` only earns its place if a reader can find
out what a number means, so this file says what each revision changed.

The number is a **revision marker, not an ABI promise** — see `CNAEXT.md` §9 and `MOD-1905`. It
exists so a build can notice that the header it compiled against and the library it linked to
disagree (`CNA_CNAEXT_ENGINE_VERSION` versus `CNA::Graphics::getEngineLayerVersion()`), and so a
bug report can say which shape of the layer it is about.

**Bump it whenever a consumer could notice the change.** That includes additions: a consumer that
feature-tests on the revision needs the number to move when the feature arrives, not only when
something breaks.

---

## Revision 2 — 2026-08-18

### Incompatible (`MOD-1900`)

Six names moved to `MOD-6`'s lowerCamelCase rule. Only the first four are reachable from outside the
module; the last two are private.

| Was | Is |
|---|---|
| `CNA::Graphics::detail::RequireCapability` | `detail::requireCapability` |
| `CNA::Graphics::detail::NameOfCapability` | `detail::nameOfCapability` |
| `CNA::Graphics::detail::ReportShaderCompileFailure` | `detail::reportShaderCompileFailure` |
| `EngineException::NotSupported` | `EngineException::notSupported` |
| `DepthEffect::EnsurePaletteTextures` (private) | `ensurePaletteTextures` |
| `RenderPipeline::DrawSkybox` (private) | `drawSkybox` |

Nothing else was renamed, and nothing was removed. Ten XNA/.NET-shaped names are deliberately
**unchanged** and are listed with their reasons in `scripts/check_cnaext_naming.py` — the `Effect`
overrides, `PbrMaterial::GetHashCode`/`ToString`, and `AsciiPostProcessEffect::Draw` /
`GetLastGridDimensions`, which are part of the C ABI surface.

### New types

`EffectPass`, `EngineException`, `RequireCapability` (`detail`), `ScopedRenderTarget`,
`ShaderDiagnostics` (`detail`), `ShaderEffectFactory`, `DepthNormalPrepass`, `AsciiPass`.

### New members on existing types

- `RenderPipeline`: `getStatistics()` and its `FrameStatistics`, `releaseDeviceResourcesEXT()`,
  `getDepthTexture()`/`getNormalTexture()`.
- `RenderPipelineSettings`: `applyRenderQualityPresetEXT()`, `toStringEXT()`/`applyFromStringEXT()`,
  `getFXAAEdgeThresholdEXT()`/`setFXAAEdgeThresholdEXT()`, and the clamping described in `MOD-730`.
- `BloomPass::iterationsForQuality`, `SsaoPass::sampleCountForQuality` /
  `setHalfResolution`/`isHalfResolution`, `FxaaPass::edgeThresholdForQuality` — the quality presets,
  exposed so a caller can ask what a preset will do rather than infer it.
- `PostProcessChain`: `getTargetPool()` (non-const), `contains()`, `clear()`.
- `Skybox`, `ShadowMap`, `CascadedShadowMap`, `SpotShadowMap`, `CubeShadowMap`: `isSupported()` now
  asks the **two-part** question (`MOD-1699`) — the capability *and*
  `ExecutesShaderEffectSourceEXT()`. A renderer that accepts a shader without running it now
  reports `false` instead of rendering a wrong frame. This is a behaviour change, not just an
  addition, and it is the one item here most likely to change what a caller sees.

### Behaviour changes worth knowing

- `RenderPipeline::end()` unbinds the render target before running the post-process chain.
- Both depth encoders clamp to `0.99999994` so a far-plane value does not pack to zeroes
  (`fract(1.0) == 0`).
- `ShadowMap::begin` and `CubeShadowMap::begin` no longer mark the pass open before the bind that
  can refuse, so one refused pass no longer bricks the object.

---

## Revision 1 — 2026-08-18 (`MOD-8`)

The first published shape: `RenderPipeline` and `RenderPipelineSettings`, the post-process chain
(`PostProcessPass`, `PostProcessChain`, `BlitPass`, `BloomPass`, `TonemapPass`, `FxaaPass`,
`SsaoPass`, `FullscreenPass`, `RenderTargetPool`), the shadow casters, `Skybox`,
`EnvironmentProcessor`, the PBR material surface, instancing/LOD/culling, and the compute and
storage-buffer wrappers.
