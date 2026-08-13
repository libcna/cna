# CNAEXT — CNA Extended Graphics Layer (Final Design)

> **Naming note (2026-08):** this document was `NOXNA.md` until the CNA naming
> normalization renamed the extension marker macro `NOXNA` -> `CNAEXT` and the
> engine-layer gate `CNA_NOXNA` -> `CNA_CNAEXT` (see `docs/RendererNamingMigration.md`).
> Historical task IDs (N01, N-007, ...) and the historical ledgers `input_noxna.md`,
> `input_noxna_progress.md` and `noxna_devices.md` keep their original names.

> **CNA/XNA 4.0 zůstává stabilní kompatibilní základ. CNAEXT je moderní engine vrstva pro Nova‑3D.**
>
> **Status of this document:** This is the *final, authoritative design* for how CNA grows beyond
> XNA 4.0 into a modern renderer. It supersedes the earlier "scaffolding / not started" draft.
> A large part of the original backlog **has already shipped** (PBR, skinned PBR, morph targets,
> runtime glTF loading, instancing, punctual lights, Draco) — see §3. What remains is the
> *engine‑orchestration* half: an HDR render pipeline, post‑processing, real shadow maps, IBL,
> skybox, and compute. §5 specifies exactly which new classes, methods, enums, structs, and
> renderer‑interface additions implement it.

---

## 1. Two meanings of "CNAEXT" — read this first

There are **two unrelated things** in this codebase that share the name "CNAEXT". Conflating them
is the single most common source of confusion, so the final design nails down the boundary:

| | **CNAEXT marker convention** | **`CNA_CNAEXT` engine layer** |
|---|---|---|
| What it is | The `CNAEXT` macro (`CNAHelper.hpp`) + `*EXT` method/type suffix | A CMake option `CNA_CNAEXT` gating a whole `CNA::Graphics` engine namespace |
| Compiled? | **Always** — the macro is a documentation/lint marker only (optionally `[[deprecated]]` under the strict‑API check), never a compile guard | **Opt‑in** — everything is wrapped in `#ifdef CNA_CNAEXT` |
| Where the code lives | `Microsoft::Xna::Framework::Graphics` (alongside the XNA types it extends) | `CNA::Graphics` |
| Granularity | Individual members / effects that extend the XNA API surface | Heavy, self‑contained *subsystems* that orchestrate above `GraphicsDevice` |
| Examples (already shipped) | `PbrEffect`, `SkinnedPbrEffect`, `MorphTargetDataEXT`, `SkinnedModelEXT`, `ShaderEffect`, `VertexPositionNormalTangentTexture`, `GraphicsDevice::DebugSimulateContextLoss()` | `RenderPipelineSettings`, `PbrMaterial`, `TonemappingMode`/`RenderQuality`/`ShadowQuality` enums |

**The rule this design commits to:**

- **Per‑object / per‑draw shading extensions** that naturally take the shape of an XNA `Effect`,
  vertex format, or `GraphicsDevice`/`Texture`/`Model` member → ship as **always‑compiled
  `CNAEXT`/`*EXT`** members in `Microsoft::Xna::Framework::Graphics`. This is the precedent already
  set by `PbrEffect` (Phase 13A) and everything after it. They cost nothing when unused and keep
  the porting story simple.

- **Scene‑ and frame‑level orchestration** — an HDR framebuffer chain, post‑process passes, a
  shadow subsystem, IBL precompute, a skybox renderer, compute dispatch — is genuinely heavy,
  pulls in float render targets and extra GPU memory, and does **not** map onto a single XNA
  `Effect`. This is the **`CNA::Graphics` engine layer, gated by `CNA_CNAEXT`**. This is what
  finally gives `RenderPipelineSettings` a real consumer.

So: the "per‑object" half of the original CNAEXT vision already exists via the marker convention;
this document designs the "engine orchestration" half that `CNA_CNAEXT` was created for.

---

## 2. Two API layers

```
┌──────────────────────────────────────────────────────────────────────────┐
│  CNA::Graphics engine layer      (opt-in, CNA_CNAEXT=ON)                    │
│  RenderPipeline · HdrSceneTarget · BloomPass · SsaoPass · TonemapPass      │
│  ShadowMap · CascadedShadowMap · Skybox · EnvironmentProcessor (IBL)       │
│  ComputeShader · StorageBuffer                                             │
├──────────────────────────────────────────────────────────────────────────┤
│  XNA 4.0 + CNAEXT-tagged extensions   (always built)                       │
│  GraphicsDevice · Texture2D/3D/Cube · Effect · SpriteBatch · Model         │
│  BasicEffect · SkinnedEffect · RenderTarget2D/Cube · BlendState · ...      │
│  CNAEXT: PbrEffect · SkinnedPbrEffect · ShaderEffect · MorphTargetDataEXT   │
│         · VertexPositionNormalTangent(Skinned) · SkinnedModelEXT           │
├──────────────────────────────────────────────────────────────────────────┤
│  IGraphicsRenderer   (compile-time selection: CNA_GRAPHICS_RENDERER)        │
│  EasyGL · Vulkan · Bgfx · SdlGpu · WebGPU · D3D9/11/12 · SDL_Renderer      │
│  · Software · Canvas · FreeDirect · Headless                             │
└──────────────────────────────────────────────────────────────────────────┘
```

### Layer 1 — XNA 4.0 + CNAEXT markers (always on)

- Namespace `Microsoft::Xna::Framework`.
- Ports existing XNA/FNA games unchanged; runs on any renderer, including 2D‑only ones.
- CNAEXT‑tagged members add modern per‑object shading (PBR, tangents, morphs) **without** a compile
  guard. They are inert on renderers that have no shader for them (accepted and rendered by that
  renderer's fallback path — never an error).

### Layer 2 — CNA::Graphics engine layer (opt‑in)

- Namespace `CNA::Graphics`; every declaration guarded by `#ifdef CNA_CNAEXT`.
- Enabled by CMake `-DCNA_CNAEXT=ON`.
- Must **not** break XNA 4.0 compatibility when enabled and must **not** make any single renderer
  mandatory. Each subsystem degrades gracefully (`SupportsCapability()` → documented fallback)
  where a renderer can't support it.
- Target consumers: new CNA / Nova‑3D games that want a modern deferred/forward‑plus‑style pipeline.

---

## 3. What already ships (reality, not backlog)

The following is **implemented and tested today** (Phases 13–14 of `plan_cnj.md`, CNB‑56…CNB‑123,
all closed 2026‑07‑17). The final design builds *on top of* this — do not re‑plan it.

### 3.1 PBR effects — `Microsoft::Xna::Framework::Graphics`, `CNAEXT`

| Type | Notes |
|---|---|
| `PbrEffect` | `Effect` + `IEffectMatrices` + `IEffectFog` + `IEffectLights`. Base‑color tint + alpha via `DiffuseColor`/`Alpha`. Maps: `Texture` (base color), `NormalMap`, `MetallicRoughnessMap`, `EmissiveMap`, `OcclusionMap` (all `Texture2D*` `get*Property`/`set*Property` + `SetOwned*` owning variants). Factors: `MetallicFactor`/`RoughnessFactor` (`float`), `EmissiveFactor` (`Vector3`), plus factor-only `IorEXT`, `SpecularFactorEXT` and `SpecularColorFactorEXT`. The latter derive dielectric F0/F90 in `GpuDrawParams`; renderers do not consume them yet. BRDF = glTF 2.0 Appendix B reference (GGX distribution + Smith‑Schlick‑GGX visibility + Schlick Fresnel). Lit with the **3 directional lights + `AmbientLightColor`** convention (the same one `BasicEffect`/`SkinnedEffect` use) — **not** image‑based lighting. |
| `SkinnedPbrEffect` | `PbrEffect`'s full material surface + `SkinnedEffect`'s bone API (`MaxBones = 72`, `SetBoneTransforms`/`GetBoneTransforms`, `WeightsPerVertex`). Game code feeds `AnimationPlayer::GetSkinTransforms()` each frame. |

**Vertex formats (CNAEXT):** `VertexPositionNormalTangentTexture` (stride 48, tangent as `vec4` with
glTF bitangent‑handedness sign in `w`), `VertexPositionNormalTangentTextureSkinned` (stride 68), and
the stride‑56 skinned+color layout used by `SkinnedEffect.VertexColorEnabled` (CNAEXT field).

**Renderer shader coverage for PBR:**

| Renderer | `PbrEffect` | `SkinnedPbrEffect` | Verification |
|---|---|---|---|
| EasyGL (OpenGL ES 3.2) | ✅ | ✅ | Golden‑image (reference) |
| Vulkan | ✅ | ✅ | Hand‑derived BRDF + goldens |
| Bgfx | ✅ | ✅ | Analytic BRDF |
| SdlGpu | ✅ | ✅ | Hand‑derived |
| WebGPU | ✅ (unskinned) | ⬜ (no skinning path yet) | Hand‑derived |
| D3D11 | ✅ | ✅ | GPU‑verified (Wine+DXVK, real HW) |
| D3D9 / D3D12 | ✅ | ✅ | Compile‑verified (Windows‑only) |
| SDL_Renderer / Software / Canvas / FreeDirect / Headless | fallback | fallback | 2D‑only / non‑shader by design |

### 3.2 glTF / GLB import

**Status of every capability, with its evidence.** This section used to be a bullet list of what
imports, which read as a completeness claim; the `plan_gltf.md` campaign (`GLTF-448`) replaced it
with a table that states what is **partial** and what is **not carried at all**, because a reader
choosing CNA for a glTF pipeline needs the second list more than the first.

Legend: ✅ implemented · ⚠️ partial, with the limit named · ❌ not carried, and *reported* rather
than dropped in silence. Every ⚠️/❌ row's full story — including the report field that names the
loss at run time — is `docs/gltf-limitations.md`.

| Capability | Status | Notes and evidence |
|---|---|---|
| Runtime load — `Content.Load<Model>("character.glb")` | ✅ | No offline step. `ContentManager`'s `ModelTypeReader` resolves `.gltf`/`.glb` through `CNA::Internal::GltfImport::GltfImportCore`. |
| Offline conversion — `tools/gltf_to_cnj` | ✅ | Produces `.cnj` + sidecars (`.skeleton.bin`, `_morph.bin`, textures). The two loaders are held to the same output by per-fixture parity sweeps (`GltfToCnjToolTest`), including all 13 material fixtures at the L6 effect boundary. |
| Geometry: `POSITION`, `NORMAL`, `TEXCOORD_0`, indices | ✅ | Byte-exact against committed L5 goldens for every corpus fixture. |
| Whole-model bounds | ✅ | `Model::getBoundingSphereEXTProperty()` merges every mesh's XNA bounding sphere after its current absolute parent-bone transform. The result is in model-root space before the caller's draw-time `world`; imported mesh spheres cover all primitives and authored default morph weights. |
| Material variants | ✅ | `KHR_materials_variants` names and sparse primitive mappings survive direct glTF and offline `.cnj`. `Model::getMaterialVariantNamesEXTProperty()` exposes source order and `setMaterialVariantEXTProperty(index)` selects a complete part state; `-1` restores the core/default mapping. |
| Topology: `TRIANGLE_STRIP`, `TRIANGLE_FAN`, `LINE_LOOP` | ✅ | Converted to lists at import, exactly (same triangles, same winding); the source mode is carried so the conversion is checkable. `LINES`/`LINE_STRIP`/`POINTS` keep their own `PrimitiveTypeEXT`. |
| Missing `NORMAL` | ✅ | A real geometric normal is computed per face; a vertex shared between differently-oriented faces is averaged rather than duplicated, and the count is reported. |
| Tangents | ⚠️ | Generated (angle-weighted) when absent. An **authored** `TANGENT` is carried only at the PBR strides 48 and 68 — no other vertex layout has a tangent slot — and is otherwise dropped and reported. |
| `COLOR_0` vertex colours | ⚠️ | Carried, but not alongside a tangent: a primitive with `COLOR_0` **and** a metallic-roughness material imports through `BasicEffect` with its colours and **without** its material, because no layout carries both and no PBR shader reads a colour stream. Reported, not silent. |
| `COLOR_1` and beyond | ❌ | XNA's layouts carry exactly one colour channel. Counted. |
| `TEXCOORD_1` (second UV set) | ❌ | Both PBR effects sample every map from one shared UV channel. A map selecting another set is sampled with the base colour's coordinates, and named. |
| PBR materials — factors + 5 maps | ✅ | `baseColorFactor`, `metallic`, `roughness`, `emissive`, `normalTexture.scale`, `occlusionTexture.strength` all reach `PbrEffect`/`SkinnedPbrEffect`; asserted at the effect boundary (L6) over the whole corpus, not only at import. |
| `alphaMode` | ⚠️ | `MASK` is **applied** — the cutoff reaches `GpuDrawParams::alphaTest` and every PBR shader discards on it. `BLEND` is **carried, not applied**: compositing needs `BlendState` and a back-to-front draw order, which in XNA the application owns. |
| `doubleSided` | ⚠️ | Carried on the effect, not applied: `RasterizerState::CullMode` is per-draw device state an XNA application sets. `docs/gltf-api-change-review.md` §1.4 records that scope decision. |
| Skinning | ⚠️ | Four influences per vertex, which is what `BlendIndices`/`BlendWeight` carry. Additional `JOINTS_n`/`WEIGHTS_n` sets are dropped, counted, and the largest discarded influence is reported; weights that do not sum to 1 are renormalised, with the worst deviation recorded. |
| Animation — LINEAR / STEP / **CUBICSPLINE** | ✅ | Real Hermite basis for cubic spline. Channels on paths CNA cannot import, or on nodes outside the scene, are skipped **and counted** per clip. |
| Morph targets | ⚠️ | CPU-blended (`MorphTargetDataEXT`, `MorphWeightTrackEXT`). Position and normal deltas travel both paths; **tangent deltas are imported but not written to the `.cnj` sidecar**, so the offline path loses them (`GLTF-289`). |
| Cameras | ✅ | `Model::CamerasEXT` / `ModelCameraEXT` — a property rather than `Tag`, which `SkinningData` and `ModelAnimationsEXT` already contend for. Perspective, orthographic and the view matrix all match the specification's own formulae; an absent `aspectRatio` is flagged rather than guessed. |
| Lights — `KHR_lights_punctual` | ⚠️ | Up to **three** directional lights, which is XNA's whole lighting model. Point and spot lights become directional lights aimed at the origin; ranges and cone angles are ignored; out-of-gamut intensity clamps. Every one of those is counted. |
| `KHR_texture_transform` | ⚠️ | Applied with the specification's formula, baked into the one shared UV channel. A second, different transform on another map cannot be baked and is named. |
| `KHR_materials_emissive_strength` | ✅ | Applied on the PBR path (a non-PBR material has no emissive term to scale). |
| `KHR_materials_unlit` | ⚠️ | `LightingEnabled = false` on `BasicEffect`. `SkinnedEffect` has no such flag — real XNA's has none either — so a skinned unlit material is approximated. |
| `KHR_materials_transmission` | ⚠️ | Approximated as `alpha = 1 - transmissionFactor`; explicitly not physical, and **not claimed**, so a file that *requires* it is refused rather than drawn as tinted alpha. |
| `KHR_materials_pbrSpecularGlossiness` | ⚠️ | Archived by Khronos, so converted rather than refused: diffuse → base colour, metallic 0, roughness `1 - glossiness`. The coloured specular term has no equivalent and its magnitude is reported. |
| `KHR_materials_ior` / `KHR_materials_specular` | ⚠️ | Raw factors survive both loaders and `.cnj`, and shader-ready dielectric F0/F90 are analytically verified at L6. Every renderer still ignores those endpoints, and the specular extension's two texture inputs are absent, so neither extension is claimed. |
| `KHR_draco_mesh_compression` | ⚠️ | Decoded when the build has `libdraco` (`CNA_DRACO_AVAILABLE`); claimed only in such a build, so a file requiring Draco is refused rather than arriving empty. |
| `EXT_meshopt_compression`, `KHR_texture_basisu`, `EXT_texture_webp` | ❌ | No decoder. A texture's plain PNG/JPEG fallback is used when the file provides one; meshopt is refused at validation, because reading such a view without a decoder yields undefined bytes rather than an error. |
| `EXT_mesh_gpu_instancing` | ❌ | The node's own single placement imports; the per-instance transforms do not, so the file renders one copy where it describes many. Reported per file. |
| `KHR_materials_variants` | ✅ | Fully imported and claimed; selection swaps effects, compatible vertex layouts, textures and samplers while preserving sparse default fallbacks. |
| `KHR_materials_clearcoat`, `_sheen`, `_volume` | ❌ | Parsed and ignored, each for a stated reason. None is claimed, so a file requiring one is refused by name. |
| Any other extension | — | The full classification of all 20 the registry knows is `docs/gltf-limitations.md` §1, generated from `GltfExtensionRegistryEXT()` — the same registry the `extensionsRequired` gate reads. |

**Malformed input** is refused by name rather than imported wrongly: structural validation
(§3.6.2.4 alignment, accessor spans, `extensionsRequired`), a container fuzz, and a rule that every
refusal is a `std::runtime_error` naming the file and the problem.

**Vertex formats (CNAEXT):** `VertexPositionNormalTangentTexture` (stride 48, tangent as `vec4` with
glTF bitangent‑handedness sign in `w`), `VertexPositionNormalTangentTextureSkinned` (stride 68), and
the stride‑56 skinned+color layout used by `SkinnedEffect.VertexColorEnabled` (CNAEXT field). Which
layout a primitive lands on — and therefore what it can carry — is a table, not a rule spread across
the loaders: `CNA::Internal::Graphics::InferredLayoutForStride`.

### 3.3 Instancing

- `GraphicsDevice::DrawInstancedPrimitives(...)` — **real XNA 4.0 API** (not CNAEXT), already public.
- Renderer hook `IGraphicsRenderer::DrawInstancedPrimitivesEx(...)` with per‑instance `VertexBuffer`
  streaming via `GpuDrawParams::instanceVb`/`instanceCount`. Implemented on the GPU renderers;
  2D‑only renderers throw a clear "not supported" message.

### 3.4 Building blocks that already exist for the engine layer

- **`ShaderEffect` (CNAEXT):** runtime‑compiled custom GLSL `Effect` — the vehicle for every
  post‑process shader below. Backed by `IGraphicsRenderer::CreateEffectRenderer(vert, frag)` and
  `IEffectRenderer::SetUniform*`.
- **`RenderTarget2D` / `RenderTargetCube`:** off‑screen FBOs with mipmaps + MSAA resolve.
- **MRT:** `IGraphicsRenderer::SetRenderTargets(...)` (needed for a G‑buffer / deferred path).
- **HDR pixel formats already in the `SurfaceFormat` enum** (real XNA values): `Single`, `Vector2`,
  `Vector4`, `HalfSingle`, `HalfVector2`, `HalfVector4`, `HdrBlendable`. Supported as **textures**
  today; **not yet as render targets** in the renderers (that is the first task of §5).
- **Proof‑of‑concept post effects** exist as *example‑level* code only
  (`modules/renderers/easygl/examples/easygl_bloom_pipeline_test.cpp`, `easygl_shadowmapping_*`): they demonstrate that
  `ShaderEffect` + `RenderTarget2D` + `SpriteBatch` are sufficient, but there is **no reusable
  library class**. Turning that example logic into first‑class `CNA::Graphics` passes is exactly
  what §5.3–§5.4 specify.

### 3.5 What is deliberately NOT there yet (the remaining scope)

Float **render targets**, a real **render pipeline / frame graph**, **bloom/SSAO/tonemap** as
library passes, **directional/cascaded/point shadow maps**, **skybox** rendering, **IBL**
(irradiance + prefiltered env + BRDF LUT), **compute shaders / SSBO**, and **LOD** helpers.
The rest of this document is their final design.

---

## 4. Intended use cases

| Use case | Layer |
|---|---|
| Port an existing XNA 4.0 / FNA game | XNA 4.0 only |
| 2D game with `SpriteBatch` | XNA 4.0 only |
| Basic 3D with `Model` / `BasicEffect` | XNA 4.0 only |
| PBR meshes from glTF (metallic‑roughness, normal maps, skinning) | **CNAEXT markers** (ships today) |
| Morph‑target / blend‑shape animation | **CNAEXT markers** (ships today) |
| GPU instancing | XNA 4.0 (`DrawInstancedPrimitives`) |
| HDR + tonemapping + bloom + SSAO | **`CNA_CNAEXT` engine layer** (§5) |
| Real directional / cascaded shadow maps | **`CNA_CNAEXT` engine layer** (§5) |
| Skybox + image‑based lighting (IBL) | **`CNA_CNAEXT` engine layer** (§5) |
| Compute‑driven particles / GPU culling | **`CNA_CNAEXT` engine layer** (§5, long term) |
| Nova‑3D / Urho3D‑style renderer | XNA 4.0 base + all of the above |

---

## 5. Final design of the remaining work

Design rules for everything below:

1. **Renderer‑agnostic public API.** Every `CNA::Graphics` class talks to the GPU **only** through
   `IGraphicsRenderer` / `Effect` / `RenderTarget2D` — never raw GL/VK/D3D. New renderer capability is
   added as **new `IGraphicsRenderer` virtuals with a safe default** (no‑op or `throw`), following
   the established pattern (see `DrawInstancedPrimitivesEx`, `CreateRenderTarget2D`).
2. **Raw‑int ordinals across the renderer boundary.** Renderer virtuals take `int` ordinals of XNA
   enums (as `CreateRenderTarget2D(..., int depthFormat, ...)` already does) to avoid coupling the
   renderer‑agnostic header to the XNA namespace.
3. **Capability‑gated, never crashing.** Each subsystem checks `GraphicsDevice::SupportsCapability()`
   and documents its fallback. EasyGL is the reference renderer implemented first; others follow as
   independent tasks (the CNB‑61/CNB‑103…109 precedent).
4. **One enum / class per file**, `.hpp` under `include/CNA/Graphics/`, `.cpp` under
   `src/CNA/Graphics/`, `// SPDX-License-Identifier: MS-PL` on both, full Doxygen on every public
   member (CLAUDE.md rules apply verbatim to this layer too).

### 5.0 Foundation — renderer capability additions

New `CNA::GraphicsCapability` enumerators (append to the existing enum, same doc style):

```cpp
FloatRenderTargets,   ///< RGBA16F/RGBA32F color render targets (HDR pipeline). RG/DEPTH float too.
ComputeShaders,       ///< Compute dispatch + storage buffers (SSBO/UAV).
StorageBuffers,       ///< Read/write GPU storage buffers independent of a compute stage.
SeamlessCubeMapFilter ///< Trilinear filtering across cube-face seams (IBL prefilter quality).
```

New `IGraphicsRenderer` virtuals (all with safe defaults so existing renderers compile unchanged):

```cpp
// ---- HDR / float render targets ----
// Extend the existing factory with the requested color SurfaceFormat ordinal (currently the
// renderer always creates an 8-bit Color target). Default keeps today's behavior.
virtual std::unique_ptr<IRenderTargetRenderer>
CreateRenderTarget2DEx(int w, int h, int surfaceFormat, int depthFormat,
                       bool preserveContents = false, bool mipMap = false,
                       int multiSampleCount = 0)
{ return CreateRenderTarget2D(w, h, depthFormat, preserveContents, mipMap, multiSampleCount); }

// ---- Compute (long term) ----
virtual std::unique_ptr<IComputeShaderRenderer> CreateComputeShader(const std::string& src) { return nullptr; }
virtual std::unique_ptr<IStorageBufferRenderer> CreateStorageBuffer(std::size_t byteSize) { return nullptr; }
virtual void DispatchCompute(IComputeShaderRenderer*, int groupsX, int groupsY, int groupsZ) {}
virtual void MemoryBarrierEXT(int barrierBits) {}   // ordinal bitmask, renderer-mapped
```

New renderer interfaces (in `IGraphicsRenderer.hpp`, mirroring `IEffectRenderer`/`IVertexBufferRenderer`):

```cpp
class IComputeShaderRenderer {
public:
    virtual ~IComputeShaderRenderer() = default;
    virtual bool CompileProgram(const std::string& computeSrc) = 0;
    virtual void Bind() = 0;
    virtual void SetUniformInt(const char* name, int value) {}
    virtual void SetUniformFloat(const char* name, float value) {}
    virtual void BindStorageBuffer(int binding, IStorageBufferRenderer* ssbo) {}
    virtual void BindImageTexture(int unit, ITextureRenderer* tex, int accessMode) {}
    [[nodiscard]] virtual bool IsValid() const = 0;
    [[nodiscard]] virtual std::string GetCompileError() const = 0;
};

class IStorageBufferRenderer {
public:
    virtual ~IStorageBufferRenderer() = default;
    virtual void SetData(const void* data, std::size_t byteSize) = 0;
    virtual void GetData(void* out, std::size_t byteSize) const = 0;
    [[nodiscard]] virtual std::size_t GetByteSize() const = 0;
};
```

**Plumbing task (small but required for HDR):** `RenderTarget2D` already carries a `SurfaceFormat`;
route it into `CreateRenderTarget2DEx` instead of dropping it. No XNA API change — an existing XNA
constructor parameter simply stops being ignored.

### 5.1 `RenderPipeline` — the orchestrator (`CNA::Graphics`)

The central new class. It **consumes the existing `RenderPipelineSettings`** (which is why that
settings bag exists) and drives an HDR forward pipeline: scene → optional shadow pass → HDR color
target → post‑process chain → tonemap/gamma → back buffer.

```cpp
namespace CNA::Graphics {

/**
 * @brief Frame-level renderer that ties HDR, shadows, and post-processing together.
 *        Renderer-agnostic: uses only GraphicsDevice / RenderTarget2D / Effect.
 */
class RenderPipeline {
public:
    explicit RenderPipeline(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
    ~RenderPipeline();

    /** @brief Mutable pipeline configuration (HDR, bloom, SSAO, tonemapping, quality). */
    [[nodiscard]] RenderPipelineSettings& getSettings();

    /** @brief (Re)allocates internal HDR/aux targets for the given backbuffer size. Call on resize. */
    void resize(int width, int height);

    /**
     * @brief Begins a frame: binds the HDR scene target and clears it. All scene draws
     *        (SpriteBatch, PbrEffect, Model, ...) issued between Begin/End render into HDR.
     */
    void begin(const Microsoft::Xna::Framework::Color& clearColor);

    /** @brief Ends the frame: runs SSAO → bloom → tonemap → gamma and resolves to the backbuffer. */
    void end();

    /** @brief Optional: attach the light whose shadow map is generated before scene draw. */
    void setShadowCaster(DirectionalLightEXT* light);

    /** @brief Optional: skybox drawn behind the scene and used as the IBL source. */
    void setSkybox(Skybox* skybox);

private:
    // HdrSceneTarget (RenderTarget2D, HalfVector4), ping-pong post targets, owned passes...
};

} // namespace CNA::Graphics
```

`RenderPipelineSettings` gains the fields the passes actually read (append, keeping the current API):
`getBloomThreshold()/setBloomThreshold(float)`, `getSsaoRadius()/setSsaoRadius(float)`,
`getSsaoIntensity()/setSsaoIntensity(float)`, and an `isFxaaEnabled()/setFxaaEnabled(bool)` toggle.

### 5.2 Post‑processing passes (`CNA::Graphics`)

A tiny abstract base plus concrete passes. Each pass is a thin wrapper around a `ShaderEffect` + a
fullscreen triangle, portable to every shader‑capable renderer. (The GLSL already exists in
`examples/easygl_bloom_*`; this promotes it to library code.)

```cpp
namespace CNA::Graphics {

/** @brief Base for a fullscreen post-process pass. Owns its ShaderEffect + a fullscreen quad. */
class PostProcessPass {
public:
    virtual ~PostProcessPass() = default;
    /** @brief Runs the pass: samples @p source, writes @p destination (null = backbuffer). */
    virtual void apply(Microsoft::Xna::Framework::Graphics::Texture2D* source,
                       Microsoft::Xna::Framework::Graphics::RenderTarget2D* destination) = 0;
};

class BloomPass  : public PostProcessPass { /* extract → down/up Gaussian pyramid → composite */ };
class SsaoPass   : public PostProcessPass { /* hemisphere-kernel AO from depth+normal, blurred   */ };
class TonemapPass: public PostProcessPass { /* HDR→LDR via TonemappingMode + exposure + gamma     */ };
class FxaaPass   : public PostProcessPass { /* cheap post-AA for renderers without MSAA on RTs      */ };

} // namespace CNA::Graphics
```

`TonemapPass` reads the existing `TonemappingMode` enum (`None`/`Reinhard`/`Filmic`/`Aces`). Add one
value `Uncharted2` for parity with common engines.

### 5.3 Shadows (`CNA::Graphics`)

A directional shadow‑map subsystem built on a **depth `RenderTarget2D`** + PCF, plus a cascaded
variant. Shadow *reception* is exposed as a CNAEXT hook on the lit effects (marker convention), so a
`PbrEffect`/`BasicEffect` mesh can sample the shadow map.

```cpp
namespace CNA::Graphics {

/** @brief Single directional shadow map (depth-only RenderTarget2D + light view/proj). */
class ShadowMap {
public:
    ShadowMap(GraphicsDevice& device, ShadowQuality quality);   // size/PCF from the enum
    void begin(const DirectionalLightEXT& light, const BoundingBox& sceneBounds); // bind depth RT
    void end();                                                                    // restore backbuffer
    [[nodiscard]] Texture2D* getDepthTexture() const;
    [[nodiscard]] Matrix getLightViewProjection() const;
};

/** @brief 3–4 cascade CSM keyed off the camera frustum split scheme. */
class CascadedShadowMap { /* array of ShadowMap + per-cascade split distances */ };

} // namespace CNA::Graphics
```

CNAEXT effect hooks (always‑compiled, in `Microsoft::Xna::Framework::Graphics`) — a shared
`IShadowReceiverEXT` interface implemented by `BasicEffect`, `SkinnedEffect`, `PbrEffect`,
`SkinnedPbrEffect`:

```cpp
CNAEXT void setShadowMapEXT(Texture2D* depth);
CNAEXT void setLightViewProjectionEXT(const Matrix& lightVP);
CNAEXT void setShadowsEnabledEXT(bool enabled);
```

These add three optional uniforms to the existing shader variants and one `GpuDrawParams` field
group (`shadowMap`, `lightViewProjColMajor[16]`, `shadowsEnabled`) — the same "accepted‑and‑ignored
on renderers without the shader" convention already used for the PBR fields.

### 5.4 Skybox + image‑based lighting (`CNA::Graphics`)

```cpp
namespace CNA::Graphics {

/** @brief Renders a TextureCube as an infinitely-distant background behind the scene. */
class Skybox {
public:
    Skybox(GraphicsDevice& device, TextureCube* environment);
    void draw(const Matrix& view, const Matrix& projection);
    [[nodiscard]] TextureCube* getEnvironment() const;
};

/**
 * @brief Precomputes the three IBL products from an environment cube map:
 *  - diffuse irradiance cube  (cosine-convolved)
 *  - specular prefiltered cube (split-sum, roughness per mip)
 *  - BRDF integration LUT      (2D, scale+bias)
 * Built with RenderTargetCube + float RTs + ShaderEffect; no new renderer API beyond §5.0.
 */
class EnvironmentProcessor {
public:
    explicit EnvironmentProcessor(GraphicsDevice& device);
    TextureCube* generateIrradiance(TextureCube* environment, int size = 32);
    TextureCube* generatePrefilteredSpecular(TextureCube* environment, int baseSize = 128, int mipLevels = 5);
    Texture2D*   generateBrdfLut(int size = 512);
};

/** @brief Bundle of the three IBL products, bound to a PBR effect for image-based lighting. */
struct ImageBasedLightEXT {
    TextureCube* irradiance = nullptr;
    TextureCube* prefilteredSpecular = nullptr;
    Texture2D*   brdfLut = nullptr;
    float        intensity = 1.0f;
};

} // namespace CNA::Graphics
```

IBL is exposed to `PbrEffect`/`SkinnedPbrEffect` as a CNAEXT hook (`setImageBasedLightEXT(const
ImageBasedLightEXT&)`) that switches the fragment shader's ambient term from the flat
`AmbientLightColor` to the sampled irradiance + prefiltered‑specular + BRDF‑LUT split‑sum. This is
additive to §3.1's direct‑light BRDF and is the one place PBR meaningfully grows.

### 5.5 Material system — reconcile `PbrMaterial` with `PbrEffect`

`PbrMaterial` (in `CNA::Graphics`) predates `PbrEffect` and currently has **no consumer** — the real
material data lives on `PbrEffect` in the XNA namespace. The final decision:

- **It is not the imported `Model`'s material carrier.** glTF import uses internal
  `CNA::Internal::GltfImport::MaterialOut` to keep all decoded slots/factors together until they
  are bound; the public model then carries the resulting `PbrEffect`/`SkinnedPbrEffect` on each
  part. Exposing `PbrMaterial` there would create two mutable truths for one material and make
  graphics-core depend on this optional graphics-ext layer. This is the `GLTF-236` API-gate
  decision; no new public surface was needed.

- **Keep `PbrMaterial` as the engine‑layer, serialization‑friendly material description** (a plain
  data bag: texture slots + factors + alpha mode), and add a **binding helper** rather than a second
  parallel material model:

  ```cpp
  namespace CNA::Graphics {
      /** @brief Copies this material's slots/factors onto a PbrEffect (or SkinnedPbrEffect):
       *         AlbedoColor→DiffuseColor/Alpha, the 5 maps→Texture/NormalMap/..., the factors. */
      void applyMaterial(const PbrMaterial& material,
                         Microsoft::Xna::Framework::Graphics::PbrEffect& effect);
  }
  ```

- Add the fields `PbrMaterial` is missing relative to `PbrEffect` so the mapping is lossless:
  `EmissiveFactor` as `Vector3` (today it stores an emissive *Color*), and an `AlphaMode` enum
  (`Opaque`/`Mask`/`Blend`) replacing the bare `alphaBlend_`/`alphaCutoff_` pair to match glTF.

- **Node‑based material graphs are explicitly out of scope** (removed from the backlog): they were a
  "long term" wish with no design and no consumer, and CLAUDE.md forbids designing for hypothetical
  future requirements.

### 5.6 Instancing & LOD helpers (`CNA::Graphics`)

The draw call ships (§3.3); what's missing is convenience:

```cpp
namespace CNA::Graphics {

/** @brief Manages a per-instance transform VertexBuffer and issues DrawInstancedPrimitives. */
class InstancedRendererEXT {
public:
    InstancedRendererEXT(GraphicsDevice& device, ModelMeshPart* part);
    void setInstances(const std::vector<Matrix>& transforms);  // uploads instance stream
    void draw(Effect& effect);
};

/** @brief Distance-based LOD picker over a set of ModelMeshParts. */
class LodGroupEXT {
public:
    void addLevel(float maxDistance, ModelMeshPart* part);
    [[nodiscard]] ModelMeshPart* select(float cameraDistance) const;
};

} // namespace CNA::Graphics
```

### 5.7 Compute & storage buffers (`CNA::Graphics`, long term)

Thin XNA‑flavored wrappers over §5.0's renderer interfaces. EasyGL (GLES 3.1 compute), Vulkan, and
D3D11/12 can implement these; the rest report `SupportsCapability(ComputeShaders) == false`.

```cpp
namespace CNA::Graphics {

class ComputeShader {
public:
    ComputeShader(GraphicsDevice& device, const std::string& glslOrTranslatedSource);
    void setUniform(const std::string& name, float value);
    void setUniform(const std::string& name, int value);
    void bindStorageBuffer(int binding, StorageBuffer& buffer);
    void bindImage(int unit, Texture2D& texture, /* GpuAccess */ int access);
    void dispatch(int groupsX, int groupsY = 1, int groupsZ = 1);
};

template <typename T>
class StorageBuffer {   // SSBO / UAV
public:
    StorageBuffer(GraphicsDevice& device, int elementCount);
    void setData(const std::vector<T>& data);
    std::vector<T> getData() const;
    [[nodiscard]] int getElementCount() const;
};

} // namespace CNA::Graphics
```

First real consumers (separate later tasks, not part of the core landing): a compute GPU particle
system and compute frustum culling.

---

## 6. Renderer support & rollout order

Same "EasyGL is the reference, others follow independently" model that Phases 13–14 used.

| Subsystem | Reference (do first) | Follow‑ups | Never (documented fallback) |
|---|---|---|---|
| Float render targets (HDR) | EasyGL | Vulkan, SdlGpu, Bgfx, WebGPU, D3D11/12 | SDL_Renderer, Canvas, FreeDirect, Software, Headless |
| Post‑process passes (bloom/SSAO/tonemap/FXAA) | EasyGL | all shader‑capable renderers | 2D‑only renderers |
| Shadow maps / CSM | EasyGL | all 3D renderers | 2D‑only renderers |
| Skybox + IBL | EasyGL | all 3D renderers | 2D‑only renderers |
| Compute / SSBO | EasyGL (GLES 3.1) | Vulkan, D3D11/12 | GLES‑3.0‑only, WebGPU (until compute lands), 2D‑only |

A renderer that can't support a subsystem must return `false` from `SupportsCapability()` for the
matching enum; the `CNA::Graphics` class then either no‑ops (post‑process) or throws a clear,
documented message (compute) — never an unchecked GL/VK error.

---

## 7. Compile‑time conventions

### CMake

```cmake
# Engine layer ON (float RTs, RenderPipeline, post-processing, shadows, IBL, compute)
cmake -B cmake-build-cnaext -DCNA_GRAPHICS_RENDERER=OPENGLES3 -DCNA_CNAEXT=ON -DCNA_BUILD_TESTS=ON

# Pure XNA + always-compiled CNAEXT markers only (engine layer off — the default)
cmake -B cmake-build-debug -DCNA_GRAPHICS_RENDERER=OPENGLES3
```

`CNA_CNAEXT` defaults **OFF**. Note the marker‑convention extensions (`PbrEffect`, `ShaderEffect`,
morph targets, …) are **always** built regardless of this option — only the `CNA::Graphics` engine
layer is gated.

### C++ guards

```cpp
// CNA::Graphics engine-layer types — entire file guarded:
#ifdef CNA_CNAEXT
namespace CNA::Graphics { class RenderPipeline { /* ... */ }; }
#endif

// CNAEXT marker on an always-compiled XNA-namespace extension member (no #ifdef):
CNAEXT void setImageBasedLightEXT(const CNA::Graphics::ImageBasedLightEXT& ibl);
```

> **Namespace naming:** engine types live in `namespace CNA::Graphics`, **not** `namespace
> CNA::CNAEXT` — `CNAEXT` is a preprocessor macro and cannot be a namespace name.

### File layout

```
include/CNA/Graphics/
    CNAEXT.hpp                    ← master include (pulls in everything below)   [NEW — not yet present]
    TonemappingMode.hpp          ← enum (exists; add Uncharted2)
    RenderQuality.hpp            ← enum (exists)
    ShadowQuality.hpp            ← enum (exists)
    RenderPipelineSettings.hpp   ← config (exists; add bloom/ssao/fxaa fields)
    PbrMaterial.hpp              ← data bag (exists; add EmissiveFactor Vector3, AlphaMode)
    RenderPipeline.hpp           ← orchestrator                                 [NEW]
    PostProcessPass.hpp / BloomPass.hpp / SsaoPass.hpp / TonemapPass.hpp / FxaaPass.hpp  [NEW]
    ShadowMap.hpp / CascadedShadowMap.hpp                                       [NEW]
    Skybox.hpp / EnvironmentProcessor.hpp / ImageBasedLightEXT.hpp             [NEW]
    InstancedRendererEXT.hpp / LodGroupEXT.hpp                                  [NEW]
    ComputeShader.hpp / StorageBuffer.hpp                                       [NEW]

src/CNA/Graphics/
    RenderPipelineSettings.cpp   (exists)   PbrMaterial.cpp (exists)
    RenderPipeline.cpp · *Pass.cpp · ShadowMap.cpp · Skybox.cpp ·
    EnvironmentProcessor.cpp · ComputeShader.cpp · StorageBuffer.cpp           [NEW]
```

**Correction to the prior draft:** `include/CNA/Graphics/CNAEXT.hpp` was listed as done (old N05) but
**does not exist**. Creating it as the master include is task **N05** below.

---

## 8. Task backlog (final)

Renumbered and reconciled with reality. Foundation first; each subsystem's reference (EasyGL)
implementation precedes its per‑renderer follow‑ups.

### Foundation

| # | Task | Status |
|---|---|---|
| N01 | `CNA_CNAEXT` CMake option; builds with and without it | ✅ |
| N02 | `TonemappingMode` / `RenderQuality` / `ShadowQuality` enums | ✅ (add `Uncharted2`) |
| N03 | `RenderPipelineSettings` config bag | ✅ (extend fields in N30) |
| N04 | `PbrMaterial` data bag | ✅ (extend in N42) |
| N05 | `include/CNA/Graphics/CNAEXT.hpp` master include | ⬜ **(mislabeled done; actually missing)** |
| N06 | `modules/graphics-ext/examples/cnaext_settings_example.cpp` compile test | ✅ |

### Renderer foundation for HDR & compute

| # | Task | Status |
|---|---|---|
| N10 | `GraphicsCapability::{FloatRenderTargets,ComputeShaders,StorageBuffers,SeamlessCubeMapFilter}` | ⬜ |
| N11 | Thread `RenderTarget2D`'s `SurfaceFormat` into `CreateRenderTarget2DEx`; EasyGL RGBA16F/32F FBOs | ⬜ |
| N12 | Float render targets on Vulkan / SdlGpu / Bgfx / WebGPU / D3D11 / D3D12 | ⬜ |

### HDR pipeline & post‑processing

| # | Task | Status |
|---|---|---|
| N20 | `RenderPipeline` + `HdrSceneTarget` (HDR begin/end, resolve to backbuffer), EasyGL | ⬜ |
| N21 | `PostProcessPass` base + `TonemapPass` (Reinhard/Filmic/ACES/Uncharted2 + exposure/gamma) | ⬜ |
| N22 | `BloomPass` (threshold extract → Gaussian pyramid → composite) | ⬜ |
| N23 | `SsaoPass` (hemisphere kernel from depth+normal, blur) | ⬜ |
| N24 | `FxaaPass` | ⬜ |
| N25 | Wire `RenderPipelineSettings` toggles → passes; per‑renderer follow‑ups | ⬜ |
| N26 | `DepthEffect` — colour-depth-reduction post-process (`ShaderEffect` subclass): 16-bit/8-bit colour, 4/2/1-bit greyscale, GLSL for EasyGL | ✅ |
| N27 | `DepthEffect::DitherMode` — ordered (Bayer 4x4/8x8) dithering before quantization. Error-diffusion (Floyd-Steinberg/Atkinson) deliberately not offered — inherently sequential, not single-pass-GPU-friendly without compute shaders (see N70) | ✅ |
| N28 | `DepthEffect` `Palette256`/`Palette16` modes — real nearest-colour match against a fixed 216-entry web-safe palette / classic 16-entry EGA/CGA palette (lookup texture + fragment-shader search), composes with `DitherMode` | ✅ |
| N29 | `CRTEffect` — separate CNAEXT post-process class: scanlines, RGB sub-pixel mask (aperture grille / shadow mask), barrel-distortion curvature, corner vignette. Requires a single full-screen source (RenderTarget2D pass), unlike DepthEffect — documented in CRTEffect.hpp | ✅ |

### Shadows

| # | Task | Status |
|---|---|---|
| N30 | `ShadowMap` (depth RT + PCF) + `IShadowReceiverEXT` hooks on the 4 lit effects, EasyGL shader | ⬜ |
| N31 | `CascadedShadowMap` (3–4 cascades) | ⬜ |
| N32 | Point‑light cube shadow maps | ⬜ (long term) |
| N33 | Shadow‑receiver shaders on the other 3D renderers | ⬜ |

### Skybox & IBL

| # | Task | Status |
|---|---|---|
| N40 | `Skybox` renderer (cube map, fullscreen sky pass), EasyGL | ⬜ |
| N41 | `EnvironmentProcessor::generateIrradiance` | ⬜ |
| N42 | `EnvironmentProcessor::generatePrefilteredSpecular` + `generateBrdfLut`; `ImageBasedLightEXT` | ⬜ |
| N43 | `PbrEffect`/`SkinnedPbrEffect` `setImageBasedLightEXT` hook + split‑sum ambient shader | ⬜ |
| N44 | IBL shaders on the other PBR‑capable renderers | ⬜ |

### Geometry helpers

| # | Task | Status |
|---|---|---|
| N50 | `InstancedRendererEXT` (instance‑stream helper over the existing `DrawInstancedPrimitives`) | ⬜ |
| N51 | `LodGroupEXT` distance selection | ⬜ |
| N52 | glTF → `PbrMaterial` bridge (`applyMaterial`) so imported meshes can feed the engine layer | ⬜ |

### Compute (long term)

| # | Task | Status |
|---|---|---|
| N70 | `IComputeShaderRenderer`/`IStorageBufferRenderer` + EasyGL (GLES 3.1) impl | ⬜ |
| N71 | `ComputeShader` / `StorageBuffer<T>` public wrappers | ⬜ |
| N72 | Compute on Vulkan / D3D11 / D3D12 | ⬜ |
| N73 | GPU particle system + GPU frustum culling demos | ⬜ |

### Already shipped (do not re‑plan — tracked in `plan_cnj.md` Phases 13–14)

| Feature | Where |
|---|---|
| `PbrEffect` / `SkinnedPbrEffect` + tangent vertex formats + glTF PBR mapping | CNB‑56…60, 75…79 |
| Morph targets (`MorphTargetDataEXT`, `MorphWeightTrackEXT`, CUBICSPLINE) | CNB‑62…65, 82…87 |
| Runtime glTF/GLB `Content.Load<Model>` + Draco + tangent gen + KHR extensions | CNB‑70…71, 88…102 |
| PBR + skinned‑vertex‑color on all 10+ renderers | CNB‑103…109, 14J |
| `DrawInstancedPrimitives` (real XNA 4.0) + renderer `DrawInstancedPrimitivesEx` | graphics plan |

---

## 9. What CNAEXT is not

- **Not a replacement for Unreal/Unity.** It is an optional layer that lets CNA grow past XNA 4.0.
- **Not a forced upgrade.** XNA 4.0 ports compile and run unchanged; the engine layer is off by default.
- **Not renderer‑specific.** Every `CNA::Graphics` abstraction stays renderer‑agnostic; GPU work lives
  behind `IGraphicsRenderer`. No subsystem makes any single renderer mandatory.
- **Not an ABI guarantee.** The engine‑layer API may change until it stabilizes.
- **Not a node‑based material editor.** Material graphs are explicitly out of scope (§5.5).

---

## 10. Relationship to Nova‑3D

Nova‑3D is a planned CNA‑based 3D framework / Urho3D‑like renderer. It will use:

- **CNA XNA 4.0 + CNAEXT markers** for mesh, texture, camera, sprite, UI, audio, PBR meshes, skinning,
  morphs, glTF loading, instancing.
- **CNA `CNA::Graphics` engine layer** for the HDR pipeline, shadows, IBL, post‑processing, and
  compute.

Nova‑3D never calls OpenGL/Vulkan/D3D/bgfx directly — all GPU access flows through the CNA renderer
interface.

---

## 11. Quick start

```bash
# Engine layer build (once N05/N10–N20 land)
cmake -B cmake-build-cnaext -DCNA_GRAPHICS_RENDERER=OPENGLES3 -DCNA_CNAEXT=ON -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-cnaext
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cmake-build-cnaext/cna_example_cnaext_settings

# Standard build — CNAEXT markers (PBR, glTF, morphs) already work here, engine layer off
cmake -B cmake-build-debug -DCNA_GRAPHICS_RENDERER=OPENGLES3 -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-debug
```
