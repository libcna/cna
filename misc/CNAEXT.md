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

The following is **implemented and tested today** (Phases 13–14 of `plans/plan_cnj.md`, CNB‑56…CNB‑123,
all closed 2026‑07‑17). The final design builds *on top of* this — do not re‑plan it.

### 3.1 PBR effects — `Microsoft::Xna::Framework::Graphics`, `CNAEXT`

| Type | Notes |
|---|---|
| `PbrEffect` | `Effect` + `IEffectMatrices` + `IEffectFog` + `IEffectLights`. Base‑color tint + alpha via `DiffuseColor`/`Alpha`. Maps: `Texture` (base color), `NormalMap`, `MetallicRoughnessMap`, `EmissiveMap`, `OcclusionMap` (all `Texture2D*` `get*Property`/`set*Property` + `SetOwned*` owning variants). Factors: `MetallicFactor`/`RoughnessFactor` (`float`), `EmissiveFactor` (`Vector3`), plus factor-only `IorEXT`, `SpecularFactorEXT` and `SpecularColorFactorEXT`. The latter derive dielectric F0/F90 in `GpuDrawParams`, consumed by all PBR-capable renderers; the two optional specular texture inputs remain unsupported. BRDF = glTF 2.0 Appendix B reference (GGX distribution + Smith‑Schlick‑GGX visibility + Schlick Fresnel). Lit with the **3 directional lights + `AmbientLightColor`** convention (the same one `BasicEffect`/`SkinnedEffect` use) — **not** image‑based lighting. |
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
imports, which read as a completeness claim; the `plans/plan_gltf.md` campaign (`GLTF-448`) replaced it
with a table that states what is **partial** and what is **not carried at all**, because a reader
choosing CNA for a glTF pipeline needs the second list more than the first.

Legend: ✅ implemented · ⚠️ partial, with the limit named · ❌ not carried, and *reported* rather
than dropped in silence. Every ⚠️/❌ row's full story — including the report field that names the
loss at run time — is `docs/gltf-limitations.md`.

| Capability | Status | Notes and evidence |
|---|---|---|
| Runtime load — `Content.Load<Model>("character.glb")` | ✅ | No offline step. `ContentManager`'s `ModelTypeReader` resolves `.gltf`/`.glb` through `CNA::Internal::GltfImport::GltfImportCore`. |
| Offline conversion — `tools/gltf_to_cnj` | ✅ | Produces `.cnj` + sidecars (`.skeleton.bin`, `_morph.bin`, textures). The two loaders are held to the same output by per-fixture parity sweeps (`GltfToCnjToolTest`), including all 14 material fixtures at the L6 effect boundary. |
| Structured import diagnostics | ✅ | `Model::getGltfImportReportEXTProperty()` exposes summary counts plus stable coded warnings, drops and approximations on both direct and offline paths. Non-glTF models and old `.cnj` files return an empty report. |
| Geometry: `POSITION`, `NORMAL`, `TEXCOORD_0`, indices | ✅ | Byte-exact against committed L5 goldens for every corpus fixture. |
| Whole-model bounds | ✅ | `Model::getBoundingSphereEXTProperty()` merges every mesh's XNA bounding sphere after its current absolute parent-bone transform. The result is in model-root space before the caller's draw-time `world`; imported mesh spheres cover all primitives and authored default morph weights. |
| Material variants | ✅ | `KHR_materials_variants` names and sparse primitive mappings survive direct glTF and offline `.cnj`. `Model::getMaterialVariantNamesEXTProperty()` exposes source order and `setMaterialVariantEXTProperty(index)` selects a complete part state; `-1` restores the core/default mapping. |
| Multiple skins | ✅ | `Model::getSkinsEXTProperty()` exposes every independent glTF `ModelSkinEXT` with its `SkinningData` and exact mesh set. The first skin remains on `Model::Tag` for compatibility; bind-pose application and effect caching keep each palette isolated. |
| Topology: `TRIANGLE_STRIP`, `TRIANGLE_FAN`, `LINE_LOOP` | ✅ | Converted to lists at import, exactly (same triangles, same winding); the source mode is carried so the conversion is checkable. `LINES`/`LINE_STRIP`/`POINTS` keep their own `PrimitiveTypeEXT`. |
| Missing `NORMAL` | ✅ | A real geometric normal is computed per face; a vertex shared between differently-oriented faces is averaged rather than duplicated, and the count is reported. |
| Tangents | ⚠️ | Generated (angle-weighted) when absent. An **authored** `TANGENT` is carried only at the PBR strides 48/60 and 68/76 — no other vertex layout has a tangent slot — and is otherwise dropped and reported. EasyGL's PBR vertex programs preserve its `w` and multiply the per-draw world/instance/skinning determinant sign under mirrors (`GLTF-175`/`176`). |
| `COLOR_0` vertex colours | ⚠️ | Carried, but not alongside a tangent: a primitive with `COLOR_0` **and** a metallic-roughness material imports through `BasicEffect` with its colours and **without** its material, because no layout carries both and no PBR shader reads a colour stream. Reported, not silent. |
| `COLOR_1` and beyond | ❌ | XNA's layouts carry exactly one colour channel. Counted. |
| `TEXCOORD_1` (second sampled UV set) | ✅ | PBR strides 60/76 carry two authored sets simultaneously and each of the five material maps selects its own packed channel. A material sampling a third distinct authored set falls back to packed channel 0 and is named in `uvSetMismatchedMapsEXT`. EasyGL OPENGLES2/3 is framebuffer-verified with independent base-colour and emissive coordinates. |
| PBR materials — factors + 5 maps | ✅ | `baseColorFactor`, `metallic`, `roughness`, `emissive`, `normalTexture.scale`, `occlusionTexture.strength` all reach `PbrEffect`/`SkinnedPbrEffect`; asserted at the effect boundary (L6) over the whole corpus, not only at import. |
| `alphaMode` | ⚠️ | `MASK` is **applied** — the cutoff reaches `GpuDrawParams::alphaTest` and every PBR shader discards on it. `BLEND` is carried and **application-applied**: select `BlendState::NonPremultiplied` (PBR emits straight RGB) and draw transparent parts back-to-front. `Model::Draw` preserves state/source order and does not sort. The full path is framebuffer-verified by `EasyGL_Gltf_AlphaBlend` on OPENGLES2/3. |
| `doubleSided` | ⚠️ | Carried and **application-applied**: select `RasterizerState::CullNone` when the effect property is true; otherwise retain the glTF front-face state, reversing it for mirrored placement. `Model::Draw` preserves caller state. A real imported back face plus its culling control are framebuffer-verified by `EasyGL_Gltf_AlphaBlend` on OPENGLES2/3; `docs/gltf-api-change-review.md` §1.4 records why this remains explicit application policy. |
| Skinning | ⚠️ | Four influences per vertex, which is what `BlendIndices`/`BlendWeight` carry. Additional `JOINTS_n`/`WEIGHTS_n` sets are dropped, counted, and the largest discarded influence is reported; weights that do not sum to 1 are renormalised, with the worst deviation recorded. |
| Animation — LINEAR / STEP / **CUBICSPLINE** | ✅ | Real Hermite basis for cubic spline. Channels on paths CNA cannot import, or on nodes outside the scene, are skipped **and counted** per clip. |
| Morph targets | ✅ | CPU-blended (`MorphTargetDataEXT`, `MorphWeightTrackEXT`). Position, normal and tangent xyz deltas travel both direct glTF and offline `.cnj`; the base tangent's handedness remains unchanged. The sidecar keeps its old position/normal prefix and adds tangent data in a backward-compatible versioned trailer (`GLTF-289`). |
| Cameras | ✅ | `Model::CamerasEXT` / `ModelCameraEXT` — a property rather than `Tag`, which `SkinningData` and `ModelAnimationsEXT` already contend for. Perspective, orthographic and the view matrix all match the specification's own formulae; an absent `aspectRatio` is flagged rather than guessed. |
| Lights — `KHR_lights_punctual` | ⚠️ | Up to **three** directional lights, which is XNA's whole lighting model. Point and spot lights become directional lights aimed at the origin; ranges and cone angles are ignored; out-of-gamut intensity clamps. Every one of those is counted. |
| `KHR_texture_transform` | ✅ | All five PBR maps retain independent offset/rotation/scale plus `texCoord` override state. Direct and offline paths agree, every PBR renderer consumes the affine rows, and an EasyGL L7 fixture proves different base/normal transforms on one authored UV stream. |
| `KHR_materials_emissive_strength` | ✅ | Applied on the PBR path (a non-PBR material has no emissive term to scale). |
| `KHR_materials_unlit` | ⚠️ | `LightingEnabled = false` on `BasicEffect`. `SkinnedEffect` has no such flag — real XNA's has none either — so a skinned unlit material is approximated. |
| `KHR_materials_transmission` | ⚠️ | Approximated as `alpha = 1 - transmissionFactor`; explicitly not physical, and **not claimed**, so a file that *requires* it is refused rather than drawn as tinted alpha. The caller owns opaque-first/back-to-front ordering and selects straight-alpha `BlendState::NonPremultiplied` (`GLTF-340`). |
| `KHR_materials_pbrSpecularGlossiness` | ⚠️ | Archived by Khronos, so converted rather than refused: diffuse → base colour, metallic 0, roughness `1 - glossiness`. The coloured specular term has no equivalent and its magnitude is reported. |
| `KHR_materials_ior` | ✅ | IOR survives both loaders and `.cnj`; shader-ready dielectric F0/F90 are analytically verified and consumed by all 15 PBR renderers. The extension is fully implemented and claimed. |
| `KHR_materials_specular` | ⚠️ | Factor and colour likewise reach every PBR renderer. The optional `specularTexture` and `specularColorTexture` inputs remain unsupported, so the extension is not claimed when required and optional use is warned by name. |
| `KHR_draco_mesh_compression` | ⚠️ | Decoded when the build has `libdraco` (`CNA_DRACO_AVAILABLE`); claimed only in such a build, so a file requiring Draco is refused rather than arriving empty. |
| `EXT_meshopt_compression`, `KHR_texture_basisu`, `EXT_texture_webp` | ❌ | No decoder. A texture's plain PNG/JPEG fallback is used when the file provides one; meshopt is refused at validation, because reading such a view without a decoder yields undefined bytes rather than an error. |
| `EXT_mesh_gpu_instancing` | ❌ | The node's own single placement imports; the per-instance transforms do not, so the file renders one copy where it describes many. Reported per file. |
| `KHR_materials_variants` | ✅ | Fully imported and claimed; selection swaps effects, compatible vertex layouts, textures and samplers while preserving sparse default fallbacks. |
| `KHR_materials_clearcoat`, `_sheen`, `_volume` | ❌ | Parsed and ignored, each for a stated reason. None is claimed, so a file requiring one is refused by name. |
| Any other extension | — | The full classification of all 20 the registry knows is `docs/gltf-limitations.md` §1, generated from `GltfExtensionRegistryEXT()` — the same registry the `extensionsRequired` gate reads. |

**Malformed input** is refused by name rather than imported wrongly: structural validation
(§3.6.2.4 alignment, accessor spans, `extensionsRequired`), a container fuzz, and a rule that every
refusal is a `std::runtime_error` naming the file and the problem.

**Diagnostics API (CNAEXT):** include
`Microsoft/Xna/Framework/Graphics/GltfImportReportEXT.hpp` and query
`model.getGltfImportReportEXTProperty()`. `AnythingLost()` is the quick display decision;
`Diagnostics` contains stable `Code`, `Severity`, `Kind`, `Count`, `WorstMagnitude`, `Subject` and
`Details` fields. Do not branch on `Message` or vector order. `getWarningCountProperty()` counts
entries, while dropped/approximation helpers sum occurrence counts.

**Vertex formats (CNAEXT):** `VertexPositionNormalTangentTexture` (stride 48, tangent as `vec4` with
glTF bitangent‑handedness sign in `w`), `VertexPositionNormalTangentTextureSkinned` (stride 68),
their internal dual-UV extensions at strides 60/76, and the stride‑56 skinned+color layout used by
`SkinnedEffect.VertexColorEnabled` (CNAEXT field). Which
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

> **Correction C1 (`plans/plan_modern.md` §0.2).** The float-render-target factory below was proposed as a
> *new* virtual named `CreateRenderTarget2DEx`. It already exists, as `CreateRenderTarget2DEXT`
> (uppercase, added by `SKIA-142`). **Do not add a second virtual** — the work is implementing the
> existing one in the 3D renderers, which is `plans/plan_modern.md` Phase 1 (`MOD-100`–`MOD-141`). The
> block below is kept as the shape that was wanted, not as an instruction to add it.
>
> **Correction C2.** The "small but required plumbing" at the end of this section — routing
> `RenderTarget2D`'s `SurfaceFormat` into the factory instead of dropping it — was already done in
> `RenderTarget2D.cpp` before this plan began. The remaining work was 100% renderer-side.

```cpp
// ---- HDR / float render targets ----
// ALREADY EXISTS as CreateRenderTarget2DEXT -- see correction C1 above. Do not add this virtual.
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
constructor parameter simply stops being ignored. *(Correction C2: already done — see above.)*

**One capability that did not survive contact.** `StorageBuffers` and `SeamlessCubeMapFilter` were
proposed above as separate enumerators. Only `ComputeShaders` was added: storage buffers exist
exactly where compute does and a second enumerator would have been a synonym, and the IBL
convolution runs on the CPU picking the cube face from the direction, so it is seamless by
construction with no capability to ask about. See `plans/plan_modern.md` `MOD-102`, `MOD-1500`.

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

> **Correction C8 (`plans/plan_modern.md` §0.2).** The signature above became
> `apply(const PostProcessContext&)`. SSAO needs depth, normals and the camera projection, and a
> second entry point taking those would make a chain of *mixed* passes impossible to express — the
> one thing this base class exists to make possible. One struct, every pass reads what it needs, and
> a pass documents its own requirements (`MOD-200`).
>
> A second thing this block does not show: `isSupported(device)` is on the base and answers **two**
> questions, not one. `GraphicsCapability::CustomEffects` means the renderer *accepts* an effect;
> `ExecutesShaderEffectSourceEXT()` means it will actually run the source. SOFTWARE and HEADLESS
> answer the first yes and the second no, and a pass that believed only the first reported success
> while copying its input (`MOD-1699`).

class BloomPass  : public PostProcessPass { /* extract → down/up Gaussian pyramid → composite */ };
class SsaoPass   : public PostProcessPass { /* hemisphere-kernel AO from depth+normal, blurred   */ };
class TonemapPass: public PostProcessPass { /* HDR→LDR via TonemappingMode + exposure + gamma     */ };
class FxaaPass   : public PostProcessPass { /* cheap post-AA for renderers without MSAA on RTs      */ };

} // namespace CNA::Graphics
```

`TonemapPass` reads the existing `TonemappingMode` enum (`None`/`Reinhard`/`Filmic`/`Aces`). Add one
value `Uncharted2` for parity with common engines. *(Done, `MOD-21` — appended, never inserted, so a
settings bag written by an earlier build still reads back as the same operator.)*

> **Correction C5 (`plans/plan_modern.md` §0.2).** The `lowerCamelCase` method names used throughout §5 —
> `begin`, `end`, `resize`, `apply` — are **kept**, not normalised toward XNA's `UpperCamelCase`.
> `CNA::Graphics` is not the XNA namespace, so there is no XNA name to preserve, and the classes
> that already existed here (`RenderPipelineSettings`, `PbrMaterial`) use this style. The rule is
> written down in `docs/cnaext-engine-layer.md` §Conventions and CLAUDE.md (`MOD-6`).

### 5.3 Shadows (`CNA::Graphics`)

A directional shadow‑map subsystem built on a **depth `RenderTarget2D`** + PCF, plus a cascaded
variant. Shadow *reception* is exposed as a CNAEXT hook on the lit effects (marker convention), so a
`PbrEffect`/`BasicEffect` mesh can sample the shadow map.

> **Correction C6 (`plans/plan_modern.md` §0.2).** `DirectionalLightEXT` did not exist anywhere in the
> tree when this section was written. It was introduced as a small **engine-layer** struct
> (`direction`, `color`, `intensity`, `castsShadows`) in `CNA::Graphics`, deliberately *not* as a new
> XNA type — XNA 4.0 has no such class and adding one would put a CNA invention in the namespace
> that must match XNA exactly (`MOD-800`).
>
> **And the target is not a depth attachment.** The map holds light-space *distance* in a colour
> target, because CNA cannot sample a depth attachment as a texture on every renderer. Everything
> from the caster shader to the PCF kernel follows from that (`MOD-800`–`MOD-842`).

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
class StorageBuffer {   // SSBO / UAV -- see correction C7: split into StorageBuffer + StorageBufferT<T>
public:
    StorageBuffer(GraphicsDevice& device, int elementCount);
    void setData(const std::vector<T>& data);
    std::vector<T> getData() const;
    [[nodiscard]] int getElementCount() const;
};

} // namespace CNA::Graphics
```

> **Correction C7 (`plans/plan_modern.md` §0.2).** `StorageBuffer` could not stay a template: a template's
> implementation has to be in the header, and this project keeps non-template implementation out of
> headers. It is split into a non-template, byte-oriented `StorageBuffer` whose implementation lives
> in a `.cpp`, plus a thin `StorageBufferT<T>` header template that is the typed view of it
> (`MOD-1520`). The refusal on a renderer without compute is `System::NotSupportedException`, not a
> new engine-layer type — CNA already maps that .NET type for exactly this meaning.

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

> **Correction C3 (`plans/plan_modern.md` §0.2).** The block below predates the repository's move to a
> module-oriented layout. The real paths are `modules/graphics-ext/include/CNA/Graphics/` and
> `modules/graphics-ext/src/` — the latter **flat**, since `graphics-ext` is a single-area module.
> The *include spelling* is unchanged and is the part that is API: `#include "CNA/Graphics/X.hpp"`.
> Tests live in `modules/graphics-ext/tests/CNA/Graphics/`, examples in
> `modules/graphics-ext/examples/`.

```
modules/graphics-ext/include/CNA/Graphics/
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

modules/graphics-ext/src/            ← flat, no CNA/Graphics/ subdirectory
    RenderPipelineSettings.cpp   (exists)   PbrMaterial.cpp (exists)
    RenderPipeline.cpp · *Pass.cpp · ShadowMap.cpp · Skybox.cpp ·
    EnvironmentProcessor.cpp · ComputeShader.cpp · StorageBuffer.cpp           [NEW]
```

**Correction to the prior draft:** `CNA/Graphics/CNAEXT.hpp` was listed as done (old N05) but
**did not exist**. It was created as the master include by `plans/plan_modern.md` `MOD-1`, and now also
carries the `@defgroup cnaext_engine` Doxygen group (`MOD-7`).

Three files the layout above did not foresee, each with its own reason:
`EngineLayerVersion.hpp` (`MOD-8`), `EngineException.hpp` (`MOD-9`) and `RequireCapability.hpp`
(`MOD-10`). `ImageBasedLightEXT.hpp` is *not* here at all — it lives in the **XNA** namespace under
`modules/graphics/`, because an always-compiled effect surface cannot include a header that exists
only under `CNA_CNAEXT` (see N42 below).

---

## 8. Task backlog (final)

Renumbered and reconciled with reality. Foundation first; each subsystem's reference (EasyGL)
implementation precedes its per‑renderer follow‑ups.

> **This table is a summary, not the backlog.** The work is tracked at task granularity in
> **`plans/plan_modern.md`** (`MOD-1`–`MOD-1924`), which records every deviation and every refusal in the
> row itself; **`NEXT_modern.md`** carries the running ledger and the full-suite baseline after each
> phase. The `N`-numbers below map onto `MOD` ranges as follows, and where the two disagree, the plan
> is right:
>
> | `N` | `MOD` range | Phase |
> |---|---|---|
> | N01–N06 | `MOD-1`–`MOD-24` | 0 — foundation, conventions, build wiring |
> | N10–N12 | `MOD-100`–`MOD-141` | 1 — float/HDR render targets |
> | N20–N21 | `MOD-200`–`MOD-327` | 2–3 — fullscreen passes, tonemapping |
> | N22 | `MOD-400`–`MOD-431` | 4 — bloom |
> | N23 | `MOD-500`–`MOD-539` | 5 — depth/normal prepass, SSAO |
> | N24 | `MOD-600`–`MOD-619` | 6 — FXAA |
> | N25 | `MOD-700`–`MOD-747` | 7 — `RenderPipeline` |
> | N26–N29 | (pre-existing) | `DepthEffect`/`CRTEffect`, shipped before this plan |
> | N30–N33 | `MOD-800`–`MOD-1012` | 8–10 — directional, cascaded, point/spot shadows |
> | N40–N44 | `MOD-1100`–`MOD-1263` | 11–12 — sky, image-based lighting |
> | N50–N52 | `MOD-1300`–`MOD-1414` | 13–14 — materials, instancing/LOD/culling |
> | N70–N73 | `MOD-1500`–`MOD-1565` | 15 — compute and storage buffers |
> | N12, N33, N44, N72 | `MOD-1600`–`MOD-1699` | 16 — the per-renderer rollout matrix |

### Foundation

| # | Task | Status |
|---|---|---|
| N01 | `CNA_CNAEXT` CMake option; builds with and without it | ✅ |
| N02 | `TonemappingMode` / `RenderQuality` / `ShadowQuality` enums | ✅ (add `Uncharted2`) |
| N03 | `RenderPipelineSettings` config bag | ✅ (extend fields in N30) |
| N04 | `PbrMaterial` data bag | ✅ (no longer only a bag: it is a lossless value description of `PbrEffect` — `applyMaterial`/`extractMaterial` round-trip exactly, equality/hash/`ToString` included. Emissive is a `Vector3` so HDR emissive survives; alpha coverage reuses the XNA-layer `AlphaModeEXT` rather than declaring a second enum — see `plans/plan_modern.md` MOD-1300..1315) |
| N05 | `CNA/Graphics/CNAEXT.hpp` master include | ✅ (`MOD-1`; it was genuinely missing when this table said done. It now also carries the `@defgroup cnaext_engine` Doxygen group — `MOD-7`) |
| N06 | `modules/graphics-ext/examples/cnaext_settings_example.cpp` compile test | ✅ |

### Renderer foundation for HDR & compute

| # | Task | Status |
|---|---|---|
| N10 | `GraphicsCapability::{FloatRenderTargets,ComputeShaders,StorageBuffers,SeamlessCubeMapFilter}` | ✅ **as two, not four** (`MOD-102`). `FloatRenderTargets`, `HalfFloatRenderTargets` and `ComputeShaders` exist; `StorageBuffers` was dropped as a synonym of `ComputeShaders`, and `SeamlessCubeMapFilter` because the IBL convolution runs on the CPU picking the face from the direction — seamless by construction, with nothing to ask about. All three are **derived** capabilities, answered by a false-by-default renderer virtual rather than a renderer's own switch, many of which end `default: return true` |
| N11 | Thread `RenderTarget2D`'s `SurfaceFormat` into `CreateRenderTarget2DEXT`; EasyGL RGBA16F/32F FBOs | ✅ (`MOD-100`–`MOD-141`). Note the spelling: the virtual is `CreateRenderTarget2DEXT` and already existed — see correction C1. The threading half was already done too (C2); the real work was EasyGL's float FBOs and the per-format verdict, verified end to end by `HdrRenderTargetRoundTripTests` against Mesa llvmpipe |
| N12 | Float render targets on Vulkan / SdlGpu / Bgfx / WebGPU / D3D11 / D3D12 | ⬜ |

### HDR pipeline & post‑processing

| # | Task | Status |
|---|---|---|
| N20 | `RenderPipeline` + HDR scene target (HDR begin/end, resolve to backbuffer), EasyGL | ✅ (`MOD-700`–`MOD-747`). No separate `HdrSceneTarget` type: the pipeline owns its scene target through `RenderTargetPool`, and a second class holding one target would have been a name, not a boundary |
| N21 | `PostProcessPass` base + `TonemapPass` (Reinhard/Filmic/ACES/Uncharted2 + exposure/gamma) | ✅ (`MOD-200`–`MOD-327`; the base takes a `PostProcessContext` — correction C8) |
| N22 | `BloomPass` (threshold extract → Gaussian pyramid → composite) | ✅ (`MOD-400`–`MOD-431`) |
| N23 | `SsaoPass` (hemisphere kernel from depth+normal, blur) | ✅ (`MOD-500`–`MOD-539`; it is what added `projection`/`inverseProjection`/`nearPlane`/`farPlane` to `PostProcessContext`) |
| N24 | `FxaaPass` | ✅ (`MOD-600`–`MOD-619`) |
| N25 | Wire `RenderPipelineSettings` toggles → passes; per‑renderer follow‑ups | 🟨 (`MOD-700`–`MOD-747` done: a disabled pass is skipped entirely, zero draw calls. The per-renderer follow-ups are Phase 16 and are open for every renderer but EasyGL) |
| N26 | `DepthEffect` — colour-depth-reduction post-process (`ShaderEffect` subclass): 16-bit/8-bit colour, 4/2/1-bit greyscale, GLSL for EasyGL | ✅ |
| N27 | `DepthEffect::DitherMode` — ordered (Bayer 4x4/8x8) dithering before quantization. Error-diffusion (Floyd-Steinberg/Atkinson) deliberately not offered — inherently sequential, not single-pass-GPU-friendly without compute shaders (see N70) | ✅ |
| N28 | `DepthEffect` `Palette256`/`Palette16` modes — real nearest-colour match against a fixed 216-entry web-safe palette / classic 16-entry EGA/CGA palette (lookup texture + fragment-shader search), composes with `DitherMode` | ✅ |
| N29 | `CRTEffect` — separate CNAEXT post-process class: scanlines, RGB sub-pixel mask (aperture grille / shadow mask), barrel-distortion curvature, corner vignette. Requires a single full-screen source (RenderTarget2D pass), unlike DepthEffect — documented in CRTEffect.hpp | ✅ |

### Shadows

| # | Task | Status |
|---|---|---|
| N30 | `ShadowMap` (distance RT + PCF) + `IShadowReceiverEXT` hooks on the 4 lit effects, EasyGL shader | ✅ (the RT holds light-space distance, not depth — CNA cannot sample a depth attachment as a texture on every renderer; see `plans/plan_modern.md` MOD-800..842) |
| N31 | `CascadedShadowMap` (2–4 cascades) | ✅ (2–4, not 3–4: two is a legitimate low setting, and the shader carries four either way; atlas storage rather than a texture array — see `plans/plan_modern.md` MOD-900..917) |
| N32 | Point‑light cube shadow maps | ✅ (and spot maps with them; the cube stores light-space *distance* rather than depth, and the four lit effects gained the punctual light the shadow attenuates — XNA's own have only directional slots. See `plans/plan_modern.md` MOD-1000..1012) |
| N33 | Shadow‑receiver shaders on the other 3D renderers | ⬜ (Phase 16, `MOD-1620`–`MOD-1649`. Vulkan is **measured**, not guessed: its `ShaderEffect` takes SPIR-V while this layer writes GLSL, so it reports `SupportsShadowSamplingEXT() == false` and the examples skip with the reason instead of crashing mid-draw, which is what they did before `MOD-1699`) |

### Skybox & IBL

| # | Task | Status |
|---|---|---|
| N40 | `Skybox` renderer (cube map, fullscreen sky pass), EasyGL | ✅ (one fullscreen draw, no cube mesh: the ray from the inverse rotation-only view-projection *is* the cube lookup. Comes with `EnvironmentProcessor::convertEquirectangular`, since panoramas ship equirectangular and renderers sample cubes — see `plans/plan_modern.md` MOD-1100..1116) |
| N41 | `EnvironmentProcessor::generateIrradiance` | ✅ (CPU-side, not render-to-cube: a GPU path needs float render targets, cube render targets and custom effects present at once, which no renderer in the committed scope offers together — see `plans/plan_modern.md` MOD-1200..1212) |
| N42 | `EnvironmentProcessor::generatePrefilteredSpecular` + `generateBrdfLut`; `ImageBasedLightEXT` | ✅ (`ImageBasedLightEXT` lives in the **XNA** namespace beside `PunctualLightEXT`, not in `CNA::Graphics`: an always-compiled effect surface cannot include a header that exists only under `CNA_CNAEXT`. The BRDF table is 8-bit because `Texture::ValidateFormat` admits `SurfaceFormat::Color` only) |
| N43 | `PbrEffect`/`SkinnedPbrEffect` `setImageBasedLightEXT` hook + split‑sum ambient shader | ✅ (flat ambient and IBL are exclusive, never summed — `FillGpuDrawParams` zeroes the flat colour so even a renderer ignoring the IBL group cannot double-count. GLSL ES 1.00 profiles have no `textureLod` and read the prefiltered cube's base level, a documented limitation of WEBGL1/OPENGLES2) |
| N44 | IBL shaders on the other PBR‑capable renderers | ⛔ **not implemented, and Phase 16 explains why** (plans/plan_modern.md §16.4, `MOD-1650`–`MOD-1674`, measured 2026-08-19). The *precompute* already works anywhere a `TextureCube` really stores data, since it is CPU-side. The shading half stays EasyGL-only because **no other renderer measured runs this layer's shader source**. Every one of them falls into one of two groups: those that answer `CustomEffects: yes` with `ExecutesShaderEffectSourceEXT(): no` — they accept an effect and render with their own fixed path — and those that do not accept a custom effect at all. Writing the IBL shader for them is not the work; giving them a GLSL front end is, and that is each renderer's own plan. `SupportsImageBasedLightingEXT()` is the query that says so, per renderer, at runtime) |

### Geometry helpers

| # | Task | Status |
|---|---|---|
| N50 | `InstancedRendererEXT` (instance‑stream helper over the existing `DrawInstancedPrimitives`) | ✅ (one draw call for 10 000 cubes, 24-54x faster than the same scene looped; the per-instance fallback is opt-in rather than silent, because one call per instance is a different program, not a slower one — see `plans/plan_modern.md` MOD-1400..1414) |
| N51 | `LodGroupEXT` distance selection | ✅ (plus optional hysteresis and a screen-space-error mode, and `FrustumCullerEXT` beside it — culling before uploading is what makes the instance stream cheap) |
| N52 | glTF → `PbrMaterial` bridge (`applyMaterial`) so imported meshes can feed the engine layer | ✅ (`materialFromGltfEXT`, a template over a concept so `graphics-ext` links neither the content module nor `cgltf`; the importer's runtime path is unchanged. glTF's float `baseColorFactor` quantises to the material's 8-bit albedo — the one documented loss, asserted at ≤1/255) |

### Compute (long term)

| # | Task | Status |
|---|---|---|
| N70 | `IComputeShaderRenderer`/`IStorageBufferRenderer` + EasyGL (GLES 3.1) impl | ✅ (support is decided by the **runtime** context, not the compile-time profile — EasyGL asks for ES 3.0 and Mesa hands it 3.2. Verified for real: 1024 floats doubled, a 1 MB buffer round-tripped byte-exact. Binding a `Texture2D` as an image is desktop-GL only and says so, because GL ES needs an immutable texture and CNA allocates mutably — see `plans/plan_modern.md` MOD-1500..1525) |
| N71 | `ComputeShader` / `StorageBuffer<T>` public wrappers | ✅ (`StorageBuffer` is byte-oriented and non-template with its implementation in a `.cpp`; `StorageBufferT<T>` is the typed view. A dispatch is validated against the device's real limits before submission, and `System::NotSupportedException` — not a new `EngineException` — is the refusal, since CNA already maps that .NET type for this meaning) |
| N72 | Compute on Vulkan / D3D11 / D3D12 | ⛔ **not implemented; measured instead** (plans/plan_modern.md §16.5, `MOD-1680`–`MOD-1682`, 2026-08-19). All three answer `ComputeShaders: no`, so `ComputeShader` and `StorageBuffer` refuse by name. On D3D11 and D3D12 that was watched happening: of the twelve `ComputeTest` cases, ten **skip** and the two that pass are precisely the two that assert the refusal — `TheCapabilityAndTheLimitsAgreeWithEachOther` and `WithoutSupportBothWrappersRefuseByName`. On Vulkan the same is verified by `cnaext_compute_particles_test` skipping against a real device. The boundary behaving as designed; implementing compute in each belongs to `plans/plan_vulkan.md` / `plans/plan_dx.md`, not to this layer, which already has the renderer-facing interfaces (`N70`) waiting for them) |
| N73 | GPU particle system + GPU frustum culling demos | ✅ **as subsystems, not demos, and the follow-up this row named was not the one that worked** (`plans/plan_modern.md` `MOD-2091`, `MOD-2095`, 2026-08-20). The gap recorded here was real: a storage buffer cannot be bound as a vertex stream, so both demos had to read their results back — 0.806 ms of pipeline stall for the particles — and this row expected a buffer-aliasing API to fix it. **It was fixed without one.** The vertex shader reads the storage buffer directly by `gl_InstanceID`, which needs no aliasing at all, and both are now subsystems: `GpuInstanceCuller` writes its surviving count straight into an indirect draw command's own `InstanceCount` word with one `atomicAdd`, and `ParticleSystem` carries an emitter, a GPU/CPU simulation compared against each other, and one instanced draw. Neither reads anything back to render a frame. The price of that route is a requirement nobody expects, and it is stated where both classes list it: GL ES 3.1 permits `GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS` to be **zero**, so a device can implement compute in full and still refuse a storage buffer in a vertex stage. `GpuInstanceCuller` refuses when it is missing (there is no CPU equivalent of "the draw call came from the GPU"); `ParticleSystem` falls back, because a CPU simulation produces the same particles more slowly. |

### Already shipped (do not re‑plan — tracked in `plans/plan_cnj.md` Phases 13–14)

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
- **Not an ABI guarantee.** The engine‑layer API may change until it stabilizes. §9.1 says how far
  from stable it actually is, rather than leaving "until it stabilizes" to the reader's optimism.
- **Not a node‑based material editor.** Material graphs are explicitly out of scope (§5.5).

---

## 9.1 How stable is it, actually (2026-08-18)

`plans/plan_modern.md` **MOD-1905** asked for one of two statements — "v1 is stable" or an honest "still
moving". **It is still moving**, and saying otherwise would be the more expensive mistake. But
"still moving" is not the same as "anything might change", so here is the line, drawn where the
evidence puts it.

**Settled, and unlikely to move:**

- The **shape** of the layer: `RenderPipeline` + `RenderPipelineSettings`, a `PostProcessPass` chain
  with a `RenderTargetPool` behind it, one shadow class per light type, `Skybox` and
  `EnvironmentProcessor` for image-based lighting. Every subsystem has landed against this shape and
  none of them fought it.
- The **ownership rules** (`docs/cnaext-ownership.md`): three shapes, no `shared_ptr`, nothing
  outlives its `GraphicsDevice`.
- The **two-part support question** (`MOD-1699`): asking a capability is not asking whether the
  renderer will run your shader. This one cost three separate bugs to learn and is not going to be
  unlearned.
- The **naming and accessor conventions**, now gated by `CNAEXT_NamingRule` and
  `CNAEXT_AccessorConventions` rather than by memory.

**Still moving, and here is what would move it:**

- **Per-renderer behaviour.** EasyGL is the reference and is complete; the rest of Phase 16 is open.
  A renderer picking a subsystem up can force an interface change, and has before.
- **Anything a capability query cannot yet answer.** Every time a renderer turned out to promise
  something it did not do, the answer was a new `…EXT()` query on `GraphicsDevice`. There is no
  reason to think the four that exist are the last four.
- **Compute and storage buffers.** One renderer implements them. A second implementation is the
  usual moment an interface designed against one backend gets corrected.

**What this means for a consumer**, Nova-3D included: build against a pinned CNA revision, read
`CNA_CNAEXT_ENGINE_VERSION` and `docs/cnaext-engine-changelog.md` when you move, and expect renames
of the kind revision 2 already carried. The layer will say when that stops being true — in this
section, with a date on it.

---

## 9.2 What changed and why — the design decisions that did not survive contact

`plans/plan_modern.md` **MOD-1907**. This document was written before any of it was built. Most of it
held. This section lists what did not, so a reader of §5 knows which paragraphs to distrust and, more
usefully, *why* each one was wrong — the reasons repeat.

**1. "Ask the capability" turned out to be the wrong question.** §5.2's `isSupported` design assumed
`GraphicsCapability::CustomEffects` meant the renderer would run your shader. It means the renderer
will *accept* one. SOFTWARE and HEADLESS accept any GLSL and keep rendering with their own fixed
path; Vulkan's `ShaderEffect` takes SPIR-V, not the GLSL this layer writes. The result was passes
reporting success while drawing nothing, three separate times before the lesson stuck
(`MOD-1699`). The fix is four `…EXT()` queries on `GraphicsDevice` and a two-part question every
shader-based subsystem now asks. **The general shape:** a capability enum describes what an API
accepts, and the layer needed to know what a renderer *does*.

**2. Four new capability enumerators became two.** §5.0 proposed `FloatRenderTargets`,
`ComputeShaders`, `StorageBuffers` and `SeamlessCubeMapFilter`. `StorageBuffers` was a synonym of
`ComputeShaders` — no renderer can have one without the other — and `SeamlessCubeMapFilter` had
nothing left to ask about once the irradiance convolution ran on the CPU. Two enumerators that
answer nothing are worse than none: they invite a caller to branch on them.

**3. `CreateRenderTarget2DEx` already existed, spelled `CreateRenderTarget2DEXT`.** §5.0 proposed
adding a virtual that was already there and already plumbed. Not an interesting mistake, but a
recurring one: **the design document was written against a mental model of the renderer interface,
not against the header.**

**4. Golden images were planned eight times and used zero times.** Eight rows proposed a golden
image as the acceptance criterion for a visual subsystem. Every one was met by a *measured property*
instead — the tonemap shader compared against a CPU reference, bloom's energy monotonic in
intensity, shadows compared inside and outside the occluded region. A golden image tells you a frame
changed; it does not tell you the thing you cared about is true, and it fails for reasons (driver
dithering, a one-texel viewport shift) that have nothing to do with the subsystem. `MOD-1703`'s
harness was refused for the same reason: infrastructure for a category with no members.

**5. Two scope calls, decided the same way and landing in opposite places.** Auto-exposure was
deferred at `MOD-308` for a specific reason — a whole-frame luminance reduction is a compute problem,
not a tonemapping one — and shipped at `MOD-1552` once compute existed (`AutoExposureEXT`, a
log-average so a few bright pixels cannot crush the frame). SMAA and TAA were declined at `MOD-610`
and stayed declined, for reasons that did not expire: TAA needs motion vectors and a history buffer,
which is a different pipeline shape rather than a pass, and SMAA needs a precomputed lookup texture
this layer has no asset path for. **A deferral with a named blocker is worth writing down; "later"
is not.**

**6. Two house rules held exactly as written.** Verbs are `lowerCamelCase` and the shader floor is
GLSL ES 3.00. Both were argued in §5.1 and §5.7 and neither needed revisiting — the second is why
every shader in the layer runs on the renderer with the least to offer, rather than on the
developer's machine.

**7. The layer's overall shape held.** `RenderPipeline` + settings, a `PostProcessPass` chain over a
`RenderTargetPool`, one shadow class per light type, `Skybox` and `EnvironmentProcessor` for IBL.
Every subsystem in Phases 3–15 landed against that shape, and none of them had to bend it. That is
the part of §5 a reader can still trust.

---

## 10. Relationship to Nova‑3D

Nova‑3D is a planned CNA‑based 3D framework / Urho3D‑like renderer. It will use:

- **CNA XNA 4.0 + CNAEXT markers** for mesh, texture, camera, sprite, UI, audio, PBR meshes, skinning,
  morphs, glTF loading, instancing.
- **CNA `CNA::Graphics` engine layer** for the HDR pipeline, shadows, IBL, post‑processing, and
  compute.

Nova‑3D never calls OpenGL/Vulkan/D3D/bgfx directly — all GPU access flows through the CNA renderer
interface.

**What Nova‑3D can actually rely on today** — as opposed to what this section intends — is
inventoried in [`docs/cnaext-nova3d.md`](docs/cnaext-nova3d.md) (`MOD-1813`): what is implemented and
measured, what to ask a renderer before using it, and what not to depend on yet.

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
