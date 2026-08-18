# What Nova-3D can rely on today

`plan_modern.md` **MOD-1813**. `CNAEXT.md` §10 says Nova-3D will use the engine layer for the HDR
pipeline, shadows, IBL, post-processing and compute. That is the intent; this file is the *inventory*,
written against what is implemented and measured on 2026-08-18, not against the backlog. Where a
subsystem's answer depends on the renderer, the answer here is EasyGL's — see
`docs/cnaext-engine-layer.md` for the per-renderer matrix.

Everything below assumes `-DCNA_CNAEXT=ON`. With the option off none of it exists, by design.

## Rely on it

| Subsystem | What Nova-3D gets | Where the contract is |
|---|---|---|
| HDR + tonemapping | `RenderPipeline::begin`/`end` around your draws; float scene target; five tonemapping operators, the shader verified against a CPU reference | `docs/cnaext-getting-started.md` |
| Post-process chain | Bloom, SSAO, FXAA in a fixed order, each with a reason for its position; plus your own passes through `PostProcessPass` | `docs/cnaext-engine-layer.md` |
| Shadows | Directional, cascaded (4 splits), spot and point, with the four lit effects receiving them through `IShadowReceiverEXT` | same |
| Sky and IBL | `Skybox` and `EnvironmentProcessor` (irradiance + prefiltered specular + BRDF LUT) | same |
| Materials | `PbrMaterial` bound straight to the effect, and `GltfMaterialBridge` from an imported glTF material | `docs/cnaext-ownership.md` for who owns the textures |
| Geometry throughput | `InstancedRendererEXT`, `LodGroupEXT`, `FrustumCullerEXT` | `docs/cnaext-perf.md` for measured costs |
| Compute | `ComputeShader`, `StorageBuffer`/`StorageBufferT<T>`, `AutoExposureEXT` | `docs/cnaext-engine-layer.md` |

The two guarantees behind that table, both tested rather than asserted: a pipeline with nothing
enabled allocates nothing and renders pixel-for-pixel what the same scene renders without one, and
every subsystem survives a `GraphicsDevice` reset and renders again.

## Ask before you rely on it

- **Ask two questions, not one.** `SupportsCapability(CustomEffects)` means the renderer *accepts*
  an effect, not that it runs your shader source. Pair it with the matching device query —
  `ExecutesShaderEffectSourceEXT()`, `SupportsShadowSamplingEXT()`,
  `SupportsImageBasedLightingEXT()`, `SupportsComputeShadersEXT()`. A subsystem asked only the
  capability is how a pass reports success while drawing nothing; it cost three bugs to learn.
- **Every pass answers `isSupported()`, and answering `false` is normal.** The contract on an
  unsupported pass is that it copies its input through rather than failing, so a chain stays
  correct on a renderer that cannot run part of it. Nova-3D should treat `false` as "this look is
  unavailable here", not as an error.

## Do not rely on it yet

- **Any renderer other than EasyGL (`OPENGLES3`/`OPENGL33`).** It is the reference and it is
  complete; the rest of `plan_modern.md` Phase 16 is open. Vulkan and D3D11 are the committed next
  two. Until a renderer's row in `docs/cnaext-engine-layer.md` says otherwise, assume a subsystem
  reports `false` there and takes its documented fallback.
- **The API's spelling.** Engine revision 2 renamed six things. `CNAEXT.md` §9.1 lists what is
  settled and what is not; pin a CNA revision and read
  `docs/cnaext-engine-changelog.md` when you move.
- **Anything the plan lists as ⛔.** TAA and SMAA are out of scope by decision, not by omission —
  TAA needs motion vectors and a history buffer, which is a different pipeline shape. If Nova-3D
  needs them, they are a pipeline change to argue for, not a pass to add.
- **Threading.** Nothing in `CNA::Graphics` is thread-safe, and nothing in it is reference-counted.

## The one lifetime rule

Nothing in `CNA::Graphics` may outlive its `GraphicsDevice`, and nothing detects it if it does.
Everything else about ownership is in `docs/cnaext-ownership.md`, which reduces to three shapes.
