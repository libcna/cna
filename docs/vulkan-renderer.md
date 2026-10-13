# Vulkan graphics renderer

`VULKAN` is one of CNA's curated renderer identities: XNA 4.0 graphics on Vulkan 1.1, with CNA's
graphics extensions (SPIR-V `ShaderEffect`, PBR and skinned PBR, image-based lighting, shadow
reception, float render targets, base-instance drawing) on top. It is a graphics renderer and
nothing else. The former general-purpose modern GPU layer that once sat beside it — compute
shaders, storage buffers and images, the texture-array facade, a public memory-barrier API,
indirect drawing over those resources and a GPU-timer abstraction — was removed in
`plans/plan_modern_retirement.md`, and nothing in this document describes it. Where the renderer
still uses pipeline barriers, descriptor sets or a compute-capable queue family, that is Vulkan's
own machinery for ordinary drawing, not a CNA API.

Select the renderer with:

```bash
cmake -S . -B cmake-build-vulkan -G Ninja -DCNA_GRAPHICS_RENDERER=VULKAN -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-vulkan --target cna_renderer_vulkan
```

Run its tests only through the private runner (`tools/platform/run_gpu_tests_private.sh
cmake-build-vulkan -R '^Vulkan_'`); every registered test inherits the runner's display and never
opens a window on the live desktop.

Every claim below names the test that keeps it true. A claim with no test beside it is not here.

---

## The capability boundary

**Test:** `Vulkan_CapabilitySnapshot` — every row is one line of a snapshot a CTest asserts, so
the table cannot drift from the renderer without a test going red. `Vulkan_CapabilityContract`
holds the same answers to the renderer-neutral contract, and `Vulkan_ProfileLimitsAudit` compares
the published numeric limits with the raw `VkPhysicalDeviceLimits`.

`Origin` says where the answer comes from: **fixed** is a property of this renderer, **device** is
asked of the physical device at runtime and may differ on other hardware than the two this project
measures on (§*Environment* below).

| Capability | Answer | Origin |
|---|---|---|
| 3D pipeline, depth/stencil, stencil independent of depth | supported | fixed |
| `SpriteBatch`, render targets, render-target cube, MRT | supported | MRT: device |
| MSAA (backbuffer and per-target) | supported | device |
| Anisotropic filtering | supported | device |
| Wire-frame rasterization | supported | fixed |
| Occlusion queries, precise pixel counts | supported | fixed |
| Instanced drawing | supported | fixed |
| Base-instance drawing | supported | fixed |
| Additive blending | supported | fixed |
| `Texture3D` storage **and** sampling | supported | fixed |
| Source-based `ShaderEffect` (SPIR-V) | supported | fixed |
| `ShaderEffect` **source execution** | unsupported | fixed |
| Multi-stream vertex input | supported | fixed |
| Compiled XNA `.fx` effects | **unsupported** here | fixed |
| Float32 / Float16 `RenderTarget2D`, half-float linear filtering | supported | device |
| Stock-effect shadow sampling | supported | fixed |
| Stock PBR image-based lighting | supported | fixed |
| Shader dialect | `SpirV` | fixed |
| Display colour space | `Srgb` only | fixed |

Two entries need their sentence rather than a cell:

- **`ShaderEffectSourceExecution` is `false` while `ShaderEffects` is `true`**, and that is not a
  contradiction. A `ShaderEffect` object is accepted and its program runs; what this renderer does
  not do is compile the *source* it is handed. It takes SPIR-V — see the `ShaderEffect` section
  below, which is the whole of that contract.
- **Compiled `.fx` effects are a build option, not an absence.** `CNA_VULKAN_COMPILED_EFFECTS`
  exists and `plans/plan_fx.md` owns it; the capability reads `unsupported` in the ordinary build
  this document describes.

### Limits

| Limit | Value here | Origin |
|---|---|---|
| `MaxTextureDimension` | 16384 | device |
| `MaxVertexStreams` | 16 | fixed — public streams of each input rate are packed into one immutable native snapshot |
| `MaxUniformBufferBytes`, `MinUniformBufferOffsetAlignment` | exact selected-device range and alignment limits | device |
| `MaxSampledTexturesPerShaderStage` | min(12 implemented slots, native limits) | device |
| `MaxVertexInputBindings`, `MaxVertexInputAttributes` | clamped to the smaller of the native ceiling and CNA's implemented path | device |
| `MaxColorAttachments` | clamped the same way | device |

### Multi-stream vertex input

Vulkan accepts CNA's full 16-slot `VertexBufferBinding` surface. Because this renderer already
copies vertex bytes into a host-visible arena when the draw is queued, it combines all public
per-vertex records into one immutable interleaved snapshot and does the same separately for several
per-instance streams. That preserves the one-native-binding-per-input-rate pipeline model and adds
neither an arena buffer nor a submit.

The copy applies every binding's `VertexOffset`, each per-instance `InstanceFrequency`, and the
draw's `vertexStart` or `baseVertex`. Indexed draws scan the selected 16- or 32-bit index range so
that `startIndex`, a declared `minVertexIndex` that differs from the actual minimum, and non-zero
`baseVertex` retain their XNA meanings. Declarations are combined before stock-family or custom
`ShaderEffect` layout selection, and the resulting bytes and declarations are owned by the queued
draw rather than by the source buffers.

**Tests:** the shared `OrdinaryDrawMultiStreamTest` and `InstancedDrawMultiStreamTest` sources
pass 48/48 on Vulkan and on EasyGL: non-indexed and indexed draws, both index widths, offsets,
deferred lifetime, mixed per-instance frequencies and invalid ranges. `Vulkan_ShaderEffect_3D`
covers split declarations on both ordinary routes and a custom instanced draw.

### Surface formats

All 27 `SurfaceFormat` values have an explicit semantic `VkFormat` mapping, and every usage bit
of the detailed profile is classified from the selected physical device (`Vulkan_SurfaceFormatClassification`).
The mapping is not itself a support promise. Nine formats have faithful `Texture2D` allocation
paths: `Color`, `Bgr565`, `Bgra5551`, `Bgra4444`, `NormalizedByte2`, `NormalizedByte4`, `Dxt1`,
`Dxt3` and `Dxt5`. The remaining formats are mapped so Vulkan properties can be inspected, but
their texture storage/sampling/transfer flags stay false until the CNA allocation, transfer and
view semantics exist; construction of such a format defers to `Texture::ValidateFormat` rather than
pretending the renderer attempted it.

DXT1/3/5 use native BC1_RGBA/BC2/BC3 storage for both `Texture2D` and `TextureCube` when the
device exposes `textureCompressionBC` (`Vulkan_DxtFormat`, `Vulkan_Dxt1FromStream`,
`Vulkan_DxtTextureCube`; the identical `EasyGL_DxtTextureCube` source is the parity control). DDS
and XNB loaders preserve the native blocks and complete mip chains on those devices, while a device
without BC retains the shared decode-to-`Color` fallback.

For a format it allocates, the verdict comes from the device's `VkFormatProperties` **and** an
exact `vkGetPhysicalDeviceImageFormatProperties` query for the usage combination CNA creates. The
detailed profile intersects sampling, linear filtering, transfer, mip and attachment facts with
those implemented paths, so native availability alone never becomes a CNA promise.

**Render targets** have a separate exact allocation table: `Color`, `Rgba64`, `Single`, `Vector2`,
`Vector4`, `HalfSingle`, `HalfVector2`, `HalfVector4` and `HdrBlendable`. Each native format is
carried through image/view creation, format-aware render-pass and pipeline cache keys, mixed-format
MRT, sampling, mip generation and byte-width-correct readback. Exact colour and depth formats are
never substituted, but `preferredMultiSampleCount` follows FNA's preference contract: Vulkan
chooses the highest supported count no greater than the request and falls back to a single sample
when there is no multisample match. `Vulkan_FloatRenderTarget` proves the live
clear/draw/sample/readback path, values above 1.0, mixed MRT (`Vulkan_MRT_MixedFormats`), MSAA
resolve and mips with odd dimensions, including the complete `7×5 → 3×2 → 1×1` chain, and pins the
preferred-count fallback for both 2D and cube targets. The same table backs `RenderTargetCube`:
the factory asks the device about the exact cube-compatible six-layer image and complete mip chain
before allocation, negotiates MSAA with the same downgrade rule, and `Color` cubes use canonical
`R8G8B8A8_UNORM` storage rather than inheriting the swapchain's possibly BGRA format
(`Vulkan_RenderTargetCube_*`).

**Colour transfer** — `GetData`/`SetData` shaped as `Color` — refuses `NormalizedByte4` and
`NormalizedByte2` explicitly even though the framework's four-byte rule would admit the first: its
bytes are signed and sample to [-1, 1], so a `Color`-shaped transfer would read the wrong values
while looking well-formed (`Vulkan_Texture2D_GetDataContract`).

### Environment these numbers came from

Measured on the machine `plans/plan_vulkan.md` §7.1 describes: Vulkan instance API **1.4.309**,
`VK_LAYER_KHRONOS_validation` **1.4.309** present and **on** for the whole test suite
(`Vulkan_DeviceValidation`; the output gate fails any CTest whose output contains a
`[Vulkan Validation]` line), and two devices — **AMD Radeon 780M (RADV PHOENIX)**, Mesa 25.0.7,
conformance 1.4.0.0, and **llvmpipe (LLVM 19.1.7)**, conformance 1.3.1.1. The `device`-origin rows
above were read from llvmpipe unless stated; where the two devices differ the difference is in the
plan, not here — the one that matters for a reader is MSAA, where llvmpipe offers up to 4× and RADV
up to 8×.

---

## Device, queue and swapchain

**Discovery.** The instance requests Vulkan 1.1, and the selected device is queried once through
`vkGetPhysicalDeviceProperties2` and `vkGetPhysicalDeviceFeatures2`. CNA keeps the supported and
enabled feature records separate: a native feature bit is enabled only when a complete renderer
path uses it — `fillModeNonSolid`, `samplerAnisotropy`, `independentBlend`,
`occlusionQueryPrecise` and `textureCompressionBC`, each conditional on device support. The
optional `VK_EXT_4444_formats` feature is queried and enabled through the features2 `pNext` chain
only when the extension is advertised and its `formatA4R4G4B4` feature is true. A device missing
any optional feature still starts and the corresponding public capability under-claims or refuses.

**One queue.** Every command — clears, `SpriteBatch`, 3D draws, uploads, readbacks, mip
generation, presentation — is recorded on the one graphics queue the device was created with, in
the order the public API issued it. There is no second queue lifecycle and no asynchronous
promise; the queue family's other flags are recorded at selection and not advertised as anything.

**Swapchain.** One acquire, one submit and one present per frame. `VK_SUBOPTIMAL_KHR` and
`VK_ERROR_OUT_OF_DATE_KHR` from acquire or present take the production recreation branch, which
preserves image, view, pipeline and descriptor-pool identities and keeps queued work queued rather
than discarded (`Vulkan_SwapchainOutOfDate`, `Vulkan_SwapchainChurn`, `Vulkan_Swapchain_Sync`,
`Vulkan_BackbufferResize`, `Vulkan_RealWindowResize`). `ApplyMultiSampleCount` really tears the
swapchain-dependent state down and rebuilds it (`Vulkan_MsaaRuntimeChange`).

**Validation and debug labels.** Validation is a configuration of the test suite, not of the
renderer: a test configured with the layer fails on any validation message. `VK_EXT_debug_utils` is
discovered independently of validation, so RenderDoc-style markers remain useful in ordinary builds:
each recorded render-pass segment carries one balanced region and `SetStringMarkerEXT` inserts an
in-stream marker. Validation and debug messages are classified into `CNA::Logger`'s GPU category
exactly once. Without the extension these operations are honest no-ops.

### Ordering and lifetime contract

The rules every renderer follows are in `docs/graphics-resource-lifetime.md` section 1. Vulkan
does not define a separate application-visible ordering model: XNA work and presentation form one
public-call order on the graphics queue. The renderer, not the application, owns pipeline
barriers, image layouts, render-pass breaks and fence values.

Draw calls snapshot their mutable state when queued and are recorded into command buffers at frame
submission. Deferred render-target destinations are retained independently of their public
wrappers (`Vulkan_DeferredResourceLifetime`, `Vulkan_DeferredSourceLifetime`,
`Vulkan_BoundTargetLifetime`, `Vulkan_RenderTargetGetDataLifetime`), descriptor entries that
mention a dying view are evicted (`Vulkan_DisposedTextureEviction`,
`Vulkan_EffectDescriptorCacheIdentity`), and images, buffers, views, pipelines, layouts,
descriptors and query pools are retired only after the consuming frame fence
(`Vulkan_ResourceLeak`, `Vulkan_HandleRelease`, `Vulkan_ResourceOutlivesDevice`,
`Vulkan_DeviceDisposeOrder`). Resize preserves native handles; terminal device teardown releases
them before destroying `VkDevice`. Applications neither receive the native device nor wait it idle
before disposal.

There is no one-time `vkQueueWaitIdle` anywhere in the renderer. A synchronous
`RenderTarget2D::GetData` flushes only the target's dependency closure and waits on a short-lived
fence attached to that specific submission: `Vulkan_RtFlushStallCost` pins off-screen readback at
zero device waits, zero extra one-time submissions and exactly one dependency-closure fence, and
`Vulkan_OneTimeCommandCost` and `Vulkan_BufferDisposeStall` keep uploads and disposal on the same
footing. Normal `Dispose()` never adds a queue or device idle; device teardown, loss and a
requested synchronous readback are the only completion boundaries.

---

## Clip-space depth range: `[0, 1]`, and EasyGL differs

**Test:** `Vulkan_DepthRangeContract`.

XNA is a Direct3D 9 programming model, and D3D9 maps clip space to depth over **`[0, 1]`** — a
vertex at `z = 0` is on the **near** plane, `z = 1` on the far plane. Vulkan's native range is the
same, so this renderer matches XNA without doing anything.

EasyGL, the reference renderer, did not until SOFTWARE-336 (2026-09-11): it left OpenGL's
`[-1, 1]` clip depth in place, so the same `z = 0` vertex landed at depth 0.5. Its stock programs
now rescale `gl_Position.z = 2z - w`, as MojoShader does for FNA3D; a `ShaderEffect`'s own GLSL is
compiled as written and keeps OpenGL's range unless it does the same.

| | Vulkan | EasyGL (before SOFTWARE-336) |
|---|---|---|
| 5×4 truth table (`z` × cleared depth, `LessEqual`) | **20/20** cells | 15/20 |
| `z = 0` survives a depth cleared to `0.4` | drawn | rejected |
| `z = -0.5` | **clipped** | drawn, as an ordinary depth of 0.25 |

Ordering is monotonic under both ranges, which is why this went unnoticed for so long: a test that
only asks "does `0.25` occlude `0.75`" passes on either. It takes an absolute comparison to see it,
and a shared fixture therefore cannot assert a depth value — `Vulkan_DepthRangeContract` is
registered for Vulkan only, on purpose.

---

## Cross-renderer conformance: what must match, what may differ, and how it is judged

**Tools:** `scripts/compare-easygl-vulkan-diagnostic.sh` and the golden CTests.

**What must match exactly.** The golden scenes are compared against the *same* PNGs under
`modules/renderers/easygl/examples/golden/` that EasyGL is compared against: the stock effects,
`EnvironmentMapEffect` and `SkinnedEffect`, the blend/depth-write/cull state goldens, the 2D
rotation and linear-filter goldens, the two PBR families and the golden-harness smoke test. There is
no Vulkan-specific golden and there must not be one. The 2D corpus
(`cross_renderer_2d_corpus.cpp`) is built once per renderer and its dumps compared byte for byte:
max diff 0 between EasyGL and Vulkan, which is the expected result — the corpus is built out of
constructs where two conforming 2D renderers have no licence to differ.

**What may differ.** Rasterized coverage of a diagonal edge, by at most a channel step: the 3D
diagnostic scene measured max diff 1 at one pixel. Excluded from the corpus, and staying excluded:
linear filtering of a magnified sprite, mip selection and mip-linear blending, anisotropy, MSAA,
additive blending and rotation by a non-right angle — constructs where two conforming renderers may
legitimately disagree pixel-for-pixel. What must never be added to that list is anything this
renderer gets wrong: `EnvironmentMapEffect`'s Fresnel term was once computed per fragment here
instead of per vertex, and the resolution was to fix the shader.

**The golden-image policy.** Thirteen `*_Golden` CTests, all comparing against an EasyGL-authored
PNG, with per-channel tolerances written in the **shared test source** rather than in either
renderer's registration — so a tolerance can never be widened *for Vulkan*:

| Tolerance | Scenes | What the number is paying for |
|---|---|---|
| 0 | `GoldenImage_Smoke` | flat, unblended colour: nothing may differ |
| 8 | `BasicEffect`, `DualTextureEffect` | texture sampling and a single interpolated term |
| 10 | `BlendState_Additive`, `TextureFilter_Linear` | one blend or one filter step per pixel |
| 20–35 | `AlphaTestEffect`, `EnvironmentMapEffect`, `PbrEffect`, `SkinnedPbrEffect` | lit and PBR shading: several floating-point terms per pixel |
| 30–40 | `RasterizerState_CullMode`, `SkinnedEffect` | shading plus geometry that lands on the same pixels by a different route |
| 60 | `DepthStencilState_WriteEnable`, `SpriteBatch_Rotation` | a rasterized *edge* moves |

The diagnostic script's gate uses tolerance 40, inherited from the Software↔EasyGL comparison, and
the same run passes at `--tolerance 2`; `--perturb` corrupts two pixels and exits 3 if the
perturbation is not detected, because a gate that cannot fail is a failure of the gate.

**Where EasyGL is not the authority.** EasyGL is the reference for coverage and maturity, never for
semantics: EasyGL cannot change MSAA post-construction and this renderer can;
`PresentationParameters` stores what the device *applied*; and surface-format verdicts come from the
renderer's own `ClassifySurfaceFormatEXT` rather than GL profile macros.

---

## `ShaderEffect` takes SPIR-V

**Tests:** `Vulkan_ShaderDialectContract`, `Vulkan_ShaderEffect_SpirV`,
`Vulkan_ShaderEffect_PerUnitSampler`, `Vulkan_ShaderEffect_UniformArrays`,
`Vulkan_ShaderEffect_3D`, `Vulkan_Texture3DAddressW`, `Vulkan_MRT_MsaaResolve`.

`ShaderEffect` takes a **renderer-specific payload** by contract, and this renderer's is compiled
**SPIR-V**. `GraphicsDevice::GetShaderDialectEXT()` reports `CNA::ShaderDialectEXT::SpirV`,
naming this bytecode intake rather than a source-compiling dialect, and a `ShaderPackageEXT`
selects its SPIR-V vertex/fragment variants here. Hand the effect GLSL text and it is refused with a
message that says so; the check is the SPIR-V magic word, not the payload's length.

EasyGL's suite carries 37 tests whose payload is GLSL. They are **not** ported, and that is a
decision rather than a backlog: the shader in each is the part that cannot cross renderers. What
this renderer owes instead is an equally capable custom-effect surface, tested with SPIR-V payloads
that exercise the same capabilities:

| What the EasyGL family needs | Where this renderer proves it |
|---|---|
| A uniform reaches the shader and decides pixels | `Vulkan_ShaderEffect_SpirV` — two values, two colours |
| A texture bound explicitly, not the one the draw supplied, with its own sampler state | `Vulkan_ShaderEffect_PerUnitSampler` — descriptor set 1, binding = unit |
| A `TextureCube` and a `Texture3D` in a custom shader | the same family — bindings 4+unit and 8+unit |
| Volume sampling that honours `SamplerState` | `Vulkan_Texture3DAddressW` — wrap vs clamp outside `[0,1]` |
| Array uniforms: kernels, palettes, coefficient sets | `Vulkan_ShaderEffect_UniformArrays` — four uniform-buffer ranges, 72 elements each |
| A 3D draw with the caller's own vertex layout | `Vulkan_ShaderEffect_3D` — a 48-byte five-element declaration |
| Multiple render targets from a custom shader | `Vulkan_MRT_MsaaResolve` — a four-output `ShaderEffect` |

The shader source itself is never translated (`plans/plan_csl.md`). An instanced draw with a custom
effect works; `Vulkan_ShaderEffect_3D` covers the ordinary and base-instance routes, reflected
removal of unused declaration fields, and refusal when a shader-required location is genuinely
missing.

### Writing a `ShaderEffect` for this renderer

- **Ordinary named values** live in one 128-byte push-constant block with fixed slots. SPIR-V
  resource and vertex-input reflection exist, but the setter's *type* selects the slot:
  `vec2 vpSize` at bytes 0–7, `mat4 uMatrix` at 16–79 (`SetUniformMat4`), `vec4 uColor` at 80–95
  (`SetUniformVec4`/`Vec3`/`Vec2`), eight floats at 96–127 (`SetUniformFloat`/`SetUniformInt`).
- **Textures** are descriptor set 1: `sampler2D` at bindings 0–3, `samplerCube` at 4–7,
  `sampler3D` at 8–11, by `SetTexture` unit. The `SpriteBatch` draw's own texture stays at set 0
  binding 0. A unit nothing was bound to reads the renderer's white 1×1 rather than undefined
  memory.
- **Sampler state** for unit *u* is `GraphicsDevice.SamplerStates[u]`, as in XNA. Unit 0 is the
  exception: a `SpriteBatch` owns slot 0 and publishes the state passed to `Begin()` there, and
  also assigns it into `SamplerStates[0]`, matching XNA, so the **next 3D draw** samples with it.
  Set `SamplerStates[1..15]` **before** `Begin()`: an `Immediate` batch's only publication point is
  `Begin()`.
- **Array uniforms** are four more bindings in set 1 — 12 `float`, 13 `vec2`, 14 `vec3`, 15 `mat4`
  — each holding 72 elements, which is XNA's own `SkinnedEffect.MaxBones`. std140 pads a `float`,
  `vec2` or `vec3` array element to 16 bytes and this renderer writes them the same way.
- **Engine matrices** for renderer-driven geometry such as shadow casters and a depth/normal
  prepass (light or face view-projection, world and the related matrices, six in all) are set 1
  binding 19. Bindings 16–18 are reserved and never declared, so 19 keeps its meaning for packages
  built against it.
- **Uniform blocks** declared through `IEffectRenderer::DeclareUniformBlockEXT` are ordinary
  uniform buffers in the same descriptor contract; they are how a package carries more parameters
  than the push-constant slots hold.
- **A 3D draw** binds the buffer's own `VertexDeclaration`, with attribute location = the
  element's index in that declaration. A buffer with no declaration is refused: a custom shader's
  inputs cannot be inferred from a stride. A **per-instance** stream continues at the locations
  after the mesh declaration's element count, from binding 1.
- **The transform** arrives in `uMatrix`, column-major, from the effect's `IEffectMatrices`
  properties — unless the game called `SetUniformMat4` itself.
- **Clip space is Vulkan's**, not OpenGL's or D3D9's: the shader is written for this renderer, so
  nothing flips Y for it.

---

## Stock effects, PBR, image-based lighting and shadows

The five XNA stock effects and the two CNA PBR families compile from the GLSL under
`modules/renderers/vulkan/src/shaders/` into SPIR-V modules selected by vertex declaration and
effect state. Their behaviour is held to the shared stock-effect suites registered on this renderer
(`Vulkan_BasicEffect*`, `Vulkan_AlphaTest*`, `Vulkan_DualTexture*`, `Vulkan_EnvironmentMap*`,
`Vulkan_SkinnedEffect*`) and to the golden policy above.

**PBR and glTF.** `PbrEffect` and `SkinnedPbrEffect` consume the glTF metallic-roughness material
model: base colour, metallic/roughness, normal, occlusion and emissive maps with per-map texture
transforms, the KHR specular and IOR Fresnel endpoints, alpha coverage, tangent handedness and the
72-bone palette (`Vulkan_PbrEffect_Golden`, `Vulkan_SkinnedPbrEffect_Golden`,
`Vulkan_PbrEffect_HandDerived`, `Vulkan_PbrMaterialMaps`, `Vulkan_PbrTextureSlots`,
`Vulkan_PbrFresnelFactors`, `Vulkan_PbrSrgbTransfer`). A model imported from `.gltf`/`.glb`
reaches them through the ordinary `Model`/`ModelMesh` path (`Vulkan_Gltf_*`, `Vulkan_Model*`,
`Vulkan_SkinnedModel_AttachPart`), and the L7 corpus reports in `docs/gltf-l7-vulkan-*.json` are
generated against this renderer. The documented PBR vertex record is
`VertexPositionNormalTangentTexture`: Vulkan refuses a shader input no vertex element feeds rather
than reading an unbound attribute, where EasyGL draws a tangent-less record through GL's
generic-attribute default.

**Image-based lighting.** `PbrEffect` and `SkinnedPbrEffect` consume the same `ImageBasedLightEXT`
bundle as EasyGL. A valid bundle binds the irradiance cube, prefiltered-specular cube and BRDF LUT
after the seven ordinary PBR material images; the final `vec4` of the 512-byte PBR parameter stride
carries the enabled flag, prefiltered mip count and intensity. Both fragment programs execute the
reference split sum, with ambient occlusion applied only to the environment term; a complete bundle
suppresses the flat ambient term and an incomplete or cleared one restores it through neutral
fallback descriptors. Deferred draws retain all ten sampled-image identities and their
render-target producers, so replacing or destroying an IBL product cannot leave a descriptor set
pointing at a stale view. `SupportsImageBasedLightingEXT()` answers true; the shared
`CNAEXT_ImageBasedLighting` oracle passes on RADV, llvmpipe and EasyGL.

**Shadow reception.** The per-pixel `BasicEffect`, `SkinnedEffect`, `PbrEffect` and
`SkinnedPbrEffect` programs consume the same directional, cascaded, point and spot state as EasyGL
through one common descriptor set holding the three maps and a dynamic std140 record. Disabled or
missing maps bind valid white 2D/cube fallbacks. Directional PCF has radius 0–2, cascades select
and optionally blend 2–4 atlas slices, the point path compares one cube distance sample, and the
spot path takes a 3×3 projected filter; directional visibility multiplies direct diffuse and
specular only. A directional map forces `BasicEffect` and `SkinnedEffect` onto their per-pixel
sibling even when `PreferPerPixelLighting` is false. `SupportsShadowSamplingEXT()` answers true;
`CNAEXT_ShadowReceiver` is the caster-independent pixel oracle. Shadow *generation* is the
application's: a caster is an ordinary `ShaderEffect` package drawn into an ordinary depth or
distance target, and the off-screen caster keeps the XNA light-space Y orientation.

**Retained retro effects.** `CRTEffect` and `DepthEffect` (`modules/graphics-ext/`) select the
SPIR-V pair of their shared fullscreen package; their settings travel in the typed push slots, and
Vulkan converts upper-left fragment coordinates to EasyGL's physical row convention before scanline,
mask and Bayer indexing, so the shared suites pass identically on EasyGL, RADV and llvmpipe.

### Instancing

`DrawInstancedPrimitives` adds an **optional per-instance world matrix to every stock 3D program**,
which is EasyGL's design reached by the one route Vulkan allows: a declared vertex input cannot be
left unbound, so where EasyGL toggles one program with a uniform, this renderer compiles **two
SPIR-V modules from the one source**. `compile_shaders.py` injects the four columns at
**locations 12..15** — EasyGL's own locations — and a family's instanced module is that family's
`.glsl` compiled again with `CNA_INSTANCED`. Without the define every macro expands to the text that
was there before, so each family's ordinary module is byte-identical SPIR-V.

`useInstanced` therefore selects **no program at all**. It adds binding 1, and the effect family a
draw belongs to is decided by exactly the cascade a non-instanced draw uses:

| Effect / declaration | Program family | Instanced module |
|---|---|---|
| `BasicEffect`, Position only (or a stride the table does not list) | `colored3d` | `colored3d` + `CNA_INSTANCED` + `CNA_NO_VERTEX_COLOR` |
| `BasicEffect`, Position + Colour | `colored3d` | `colored3d` + `CNA_INSTANCED` |
| `BasicEffect`, Position + TextureCoordinate | `textured3d` | `textured3d` + `CNA_INSTANCED` |
| `BasicEffect`, Position + Colour + TextureCoordinate | `colored_textured3d` | same source + `CNA_INSTANCED` |
| `BasicEffect` with `LightingEnabled` — all three lit shapes, each in both `PreferPerPixelLighting` variants | `lit_textured3d` / `lit_untextured3d` / `lit_textured3d_color` and their `_vertexlit` siblings | same sources + `CNA_INSTANCED` |
| `AlphaTestEffect`, both vertex shapes | `alpha_test3d`, `alpha_test_colored3d` | same sources + `CNA_INSTANCED` |
| `DualTextureEffect`, both vertex shapes | `dual_texture3d`, `dual_texture_colored3d` | same sources + `CNA_INSTANCED` |
| `EnvironmentMapEffect` | `env_map3d` | same source + `CNA_INSTANCED` |
| `SkinnedEffect`, stride 52/56, both lighting variants | `skinned3d*` | same sources + `CNA_INSTANCED` |
| `PbrEffect` / `SkinnedPbrEffect`, every vertex record (48/60, 68/76/80) | `pbr3d`, `pbr3d_skinned` | same sources + `CNA_INSTANCED` |

Fog works on all of them: routing every instanced draw into its ordinary family is what gave the
instanced fragment shaders the fog uniform block. Which shape a `BasicEffect` instanced draw takes
is decided by the declaration when there is one, and by the stride only when there is not. The
per-instance matrix composes with `BasicEffect.World` — the shader computes
`World × View × Projection × instanceMatrix × position` — and for a skinned draw the instance
matrix applies after the bone skin. One deliberate divergence from EasyGL: EasyGL rotates a
skinned normal by `mat3(instanceMatrix)` directly, while this renderer folds the instance matrix
into the world normal matrix, `transpose(inverse(mat3(World × instance)))`, which is correct under a
non-uniformly-scaled instance; for PBR the instance matrix is folded into the tangent matrix and
into the direction handedness as well (`Vulkan_InstancedTexturedDraw`,
`Vulkan_InstancedVertexColor_Cardinality`, `Vulkan_DeclaredInstancedLayout`).

**Base instance.** `DrawInstancedPrimitivesBaseInstanceEXT` retains `firstInstance` in the same
deferred record as the ordinary draw and supplies it to `vkCmdDrawIndexed`. Because the renderer
already expands `InstanceFrequency` into a divisor-one staging stream, for a non-zero base it
retains the required prefix as well, so the native instance number and the selected per-instance
record advance together; the public layer range-checks every bound instance stream over the shifted
interval. `SupportsBaseInstanceDrawingEXT()` answers true and `Vulkan_ShaderEffect_3D` draws one
instance at base one.

**Pipeline-variant cost.** Every family's instanced pipeline is a distinct cache entry, exactly one
per (family, vertex shape) — never one per runtime value. `BasicEffect.VertexColorEnabled` travels
in the push constant and creates no variant (`Vulkan_PipelineCacheBound`).

### Which vertex layouts a lit `BasicEffect` can draw

**Tests:** `Vulkan_BasicEffect_PositionNormal`, `Vulkan_BasicEffectCombinations`,
`Vulkan_BasicEffect_UnlitVertexColor36Byte`.

This renderer picks a stock program from the vertex buffer's **declaration**, falling back to its
stride when there is none, and matches a declaration as a whole element set rather than by "has a
Normal": a looser rule would let Position+Normal+Colour satisfy the two-input untextured program
and render with the vertex colour silently discarded.

| Declaration | Lit `BasicEffect` |
|---|---|
| Position + Normal + TexCoord (32 bytes) | ✅ |
| Position + Normal (24 bytes) | ✅ |
| Position + Normal + Colour + TexCoord (36 bytes) | ✅ when lighting is on |
| the same 36 bytes with lighting **off** | ✅ routed to the unlit colour+texture program, whose input table has no `Normal` |
| Position + Normal + anything else | ❌ refused by name |
| no declaration at all | the stride decides |

The 24-byte case is XNA's own Primitives3D sample, and it is 24 bytes exactly as
`VertexPositionColorTexture` is — which is why the stride cannot decide it and the declaration must.
The unlit 36-byte route leaves the declared `Normal` bound by nothing, the one case where dropping
an element has a nameable reason: with lighting off there is nothing for it to feed. The rule pins
the element set, not the offsets.

---

## Where this renderer differs from EasyGL, and why

**Differences that are deliberate and permanent:**

| Difference | Why |
|---|---|
| `ShaderEffect` takes **SPIR-V**, EasyGL takes GLSL | `ShaderEffect` carries renderer-specific source by contract; see above. |
| Clip space is `[0, 1]`, EasyGL's was `[-1, 1]` | Vulkan's own convention; see the depth-range section. |
| Set numbering for a custom effect's textures | Set 0 is the `SpriteBatch` texture, set 1 the effect's own. EasyGL's unit 0 *is* the sprite texture. |
| Array uniforms hold **72** elements | A fixed uniform-buffer block; past 72 this renderer refuses by name rather than truncating. |
| A stock `PbrEffect` record must supply a `Tangent` input (or be the stride-48/60 record) | Vulkan refuses a shader input no vertex element feeds rather than read an unbound attribute. |

**Differences where this renderer does more:**

| Difference | Why it is not parity |
|---|---|
| MSAA can change at runtime | `ApplyMultiSampleCount` really tears down and rebuilds; EasyGL cannot change MSAA after construction. |
| Per-target MSAA sample counts | A `RenderTarget2D` carries its own count rather than the device's (`Vulkan_RenderTargetOwnMsaa`). |
| Real depth bias | `vkCmdSetDepthBias` as dynamic state (`Vulkan_DepthBias`). |
| `SpriteSortMode::Immediate` honoured at the renderer boundary | EasyGL does not override `SetImmediateMode` at all. |
| Precise occlusion counts on real hardware | `VK_QUERY_CONTROL_PRECISE_BIT` with the feature enabled, answered honestly through `PixelCountIsPreciseEXT` (`Vulkan_OcclusionQuery_Precision`). |

---

## Device loss: reported, never reset

**Tests:** `Vulkan_DeviceLostContract`, `Vulkan_DeviceResetEvents`.

XNA gives a game three events — `DeviceLost`, `DeviceResetting`, `DeviceReset` — plus
`GraphicsDeviceStatus` and `ContentLost` on its resources. Every call that can report a loss —
`vkQueueSubmit`, `vkQueuePresentKHR`, `vkAcquireNextImageKHR` and the fence waits, on the deferred
and the ordinary present path alike — is checked. On a loss the renderer reports
`RendererDeviceEvent::Lost` to the shared layer **once**, so the game's own `DeviceLost` handler
runs and `GraphicsDeviceStatus` becomes `Lost`, and the call then fails with a message naming the
entry point.

There is no reset. A lost `VkDevice` is unrecoverable by specification: the device and every object
created from it must be destroyed and rebuilt, which under live `Texture2D`, `RenderTarget2D`,
`Effect` and buffer wrappers is a different feature from D3D9's `Reset` — the one XNA's event pair
was designed around. `DeviceReset` is therefore never raised here, and the three GL entry points
(`SetContextRecoveryEnabled`, `DebugSimulateContextLoss`, `DebugRestoreContext`) describe an OpenGL
context loss this API does not have.

---

## `SpriteSortMode::Immediate` does not submit per sprite, and will not

**Test:** `Vulkan_SpriteBatch_SortModeSemantics`.

XNA defines `Immediate` as each sprite drawing at its own draw call, so a texture mutated in place
between two `Draw` calls must leave the first sprite showing the **old** contents. On this renderer
both sprites show the new contents, exactly as they do under `Deferred`: `Immediate` sprites are
forwarded straight through but recorded for replay at `Present`, while `Texture2D::SetData`
reaches the image through an upload that submits and waits immediately, so the upload always wins
the race.

This is not a Vulkan-vs-EasyGL gap — EasyGL's sprite renderer appends to a vertex batch flushed
only on a texture change or at `End()` — so the divergence is CNA-wide and fixing it here alone
would only make the two renderers disagree. What honouring it would cost: on an off-screen target
the forced-flush route measured 2.7× to 3.4× the batched cost per sprite on llvmpipe and ×11.6 on
RADV; on the backbuffer there is no such path at all, because the one-acquire-one-submit-one-present
contract must not change. What a game can rely on: `Immediate` still draws every sprite, in issue
order, each into its own destination rectangle. A game that needs the old contents must `End()`
the batch before mutating the texture.

---

## `RasterizerState.MultiSampleAntiAlias` is not carried, and XNA does carry it

**Test:** `Vulkan_PipelineKeyStateCoverage`. **Probe:** `spikes/xna-multisample-antialias-spike/`.

`IGraphicsRenderer::ApplyRasterizerState` takes `(cullMode, fillMode, scissorTestEnable, depthBias,
slopeScaleDepthBias)`; there is no sixth parameter, so `RasterizerState.MultiSampleAntiAlias`
reaches this renderer — and every other one — not at all. XNA does not drop it: setting the
property to `false` makes the D3D9 layer write render state 161, `D3DRS_MULTISAMPLEANTIALIAS`. What
it does to a rendered edge is unmeasured, because this machine reaches D3D9 only through DXVK, which
logs state 161 as unhandled.

It stays dropped here because a Vulkan graphics pipeline's `rasterizationSamples` must match the
render pass attachment's sample count, so "do not multisample this draw" is not a per-draw property
in this API; honouring it would mean changing the attachment. It is not a parity gap: no renderer
receives the field, so no two renderers disagree. `Vulkan_PipelineKeyStateCoverage` asserts that the
field does not fragment the pipeline cache *and* that `ApplyRasterizerState` has exactly five
parameters, with a positive control so a renamed method cannot make the negative assertion pass
vacuously.
