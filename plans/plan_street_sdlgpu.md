# cna-street on the SDL_GPU renderer

Owner brief of 2026-09-23: run `cna-street` (`../cna-street`) on CNA's SDL_GPU renderer and fix
what goes wrong, in cna-street, CNA or sharp-runtime -- wherever the defect actually is.

Branch `street-sdlgpu` from `next` `7290dfc58`. Task IDs `STREETS-0001`, … . Defects that belong
to cna-street are fixed in cna-street and listed here only for the record; defects that belong to
CNA are fixed here, on the layer they belong to, with a regression test.

The predecessors are [`plan_street.md`](plan_street.md) (Vulkan) and
[`plan_street_webgpu.md`](plan_street_webgpu.md) (WebGPU), which did the same comparison for the
other two modern renderers.

## How the comparison is run

cna-street's one build tree gained `SDL_GPU` beside the three renderers it already carried:

```sh
export CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv
cmake -S . -B build -DCNA_GRAPHICS_RENDERERS="OPENGL33;VULKAN;WEBGPU;SDL_GPU"   # default stays OPENGL33
cmake --build build -j8 --target cna-street compare-images

R=../cna/tools/platform/run_gpu_tests_private.sh     # never the live desktop
CNA_GRAPHICS_RENDERER=SDL_GPU $R --exec ./build/bin/cna-street --no-audio --no-overlay --capture out/sdlgpu
CNA_GRAPHICS_RENDERER=VULKAN  $R --exec ./build/bin/cna-street --no-audio --no-overlay --capture out/vulkan
./build/bin/compare-images out/vulkan/<view>.png out/sdlgpu/<view>.png
```

SDL_GPU runs on SDL_gpu's Vulkan backend here (`SDL_gpu backend 'vulkan'`, RADV, Radeon 780M), so
Vulkan is the natural reference: same driver, same GPU.

## Status

| ID | Task | Status |
|---|---|---|
| STREETS-0001 | SDL_GPU: every instanced stock draw took the position-only `instanced3d` module, whatever effect was applied | ✅ |
| STREETS-0002 | SDL_GPU: no image-based lighting -- the street fell back to a flat hemisphere ambient | ✅ |
| STREETS-0003 | SDL_GPU: half-float textures reported unfilterable, so FXAA and bloom read the HDR scene with point sampling | ✅ |
| STREETS-0004 | SDL_GPU: a ShaderEffect's texture units ignored `SamplerStates[unit]` -- SSAO's tiled noise was clamped | ✅ |

---

## STREETS-0001 — an instanced stock draw was rendered by the wrong program

**Symptom.** The street started, built its scene and wrote all 18 captures without an error, but
every tree, parked car, bench, chair, planter and piece of street furniture was a flat white
silhouette, while the buildings, the road, the moving vehicles and the pedestrians were right.
44-99 % of pixels differed from the Vulkan capture at every viewpoint (mean 15-80/255).

**Root cause.** The one WebGPU had (`STREETW-0001`): `SdlGpuRenderer::DrawInstancedPrimitivesEx`,
after its compiled-effect and custom-`ShaderEffect` branches, always built an
`InstancedDrawCommand`, which `IssueInstancedDraw` rendered with `instanced3d.vert.glsl` +
`colored3d.frag.glsl` -- position, an optional COLOR0, `DiffuseColor` out. No texture, no normal,
no light, whatever effect was applied. The street draws everything it places many times through
`CNA::Graphics::InstancedRendererEXT` and a `PbrEffect`, and everything else one draw at a time
through the same effect, which is why the frame split exactly along that line.

**Fix.** The Vulkan design (`VULKAN-222`..`VULKAN-232`), not a new one:

* Every stock vertex shader gains the optional `CNA_INSTANCED` prologue -- four per-instance
  matrix columns at locations 12..15, and `CNA_INSTANCE_POSITION()` / `CNA_INSTANCE_WORLD()`,
  which are the identity without it. `compile_shaders.py` compiles each module a second time with
  the define: 15 instanced modules, one per ordinary vertex module a family selects. Regenerating
  the header left **every ordinary module's SPIR-V byte-identical** (checked word for word).
* `DrawInstancedPrimitivesEx` validates the streams as before, then goes through the same
  `DispatchStockDrawEXT` the ordinary route uses, with the instance count pending
  (`pendingInstanceCountEXT_`, the `pendingIndirectArgumentsEXT_` pattern). The family is chosen by
  the one selector, so the instanced and ordinary routes cannot disagree about it.
* The declaration-resolved families (Basic colour/textured/lit, AlphaTest, DualTexture,
  EnvironmentMap) resolve their inputs over the per-vertex streams alone -- an
  `InstancedRendererEXT` matrix is TEXCOORD1..4, which would otherwise answer a dual-texture
  draw's second UV set -- and then gain the four columns at 12..15 in the same resolved layout,
  so the existing capture, upload and bind code carries them unchanged.
* The fixed-record families (SkinnedEffect, PbrEffect, SkinnedPbrEffect) bind the columns as a
  second, per-instance buffer at slot 1 (`InstanceTransformStreamEXT`), keyed into their pipeline
  caches; an ordinary draw's key is unchanged.
* The instanced modules are created on first use through the same SPIR-V-or-shadercross route
  construction uses (`CreateSdlGpuShaderEXT`, extracted from `ConstructionResources`), so they
  cost nothing until an instanced draw exists and cannot take a different route on D3D12/Metal.
* `instanced3d`, its pipeline cache, `InstancedDrawCommand`, `IssueInstancedDraw`,
  `ResolveInstancedVertexLayoutEXT` and the `InstancedVertexShaderCreation` failure point are gone:
  nothing reaches them any more.

**Tests.** WebGPU's two witnesses use only the XNA API, so they are compiled again for this
renderer rather than copied: `SdlGpu_InstancedPbr3D` and `SdlGpu_InstancedStockFamilies` (every
stock family, rigid and skinned). Both **fail on the unmodified renderer** (all 11 checks, each
family drawing `DiffuseColor`) and pass with the fix. Two rows of
`GltfRendererPbrFallbackPolicy` quoted the PBR vertex shader literally; they now quote the current
spelling, exactly as `VULKAN-232` did for Vulkan's rows.

**Regression runs**, `cmake-build-sdlgpu` rebuilt whole first:

| suite | before | after |
|---|---|---|
| classic `-R '^SdlGpu'` | 204 / 27 of 231 | 204 / 27 of 231 + the two new tests passing; **the same 27 by name** (A/B: the 27 re-run on the stashed baseline, all 27 fail there too) |
| every instancing test in the tree (`-R '[Ii]nstanc'`, minus audio/sensors) | — | 132 / 0 |
| modern `CnaGraphicsExtTests` | 931 / 0 / 31 | **931 / 0 / 31** |
| CNAEXT examples `-L CnaExt` | 27 / 1 / 4 | 27 / 1 / 4 (only `CNAEXT_NoPosixSetenv`, as before) |
| boundary gates (`sdl_inventory`, `sdl_classify`, `renderer_sdl_audit`, `sdl_ratchet`, `hot_path_lint`) | pass | pass |

**The street after the fix:** mean difference against Vulkan fell from 15-80/255 to 7-22/255 at
every viewpoint; every prop has its material. What still differs is lighting, not geometry -- see
the next task.

**Found, not caused, by this task:** three `GltfRendererPbrFallbackPolicy` cases already fail on
`next` (`EveryPbrShaderConsumesAllFiveTextureTransforms`,
`SdlGpuSamplesBothKhrMaterialsSpecularTextures`, `EveryPbrShaderUsesTheGltfPackedTextureChannels`):
their SDL_GPU evidence still quotes `samplerLodBias.slots…` and `fsInfo.num_samplers = 7`, which
SMG-0032 renamed to `pbrp.lodBias…` and raised to 10.

---

## STREETS-0002 — no image-based lighting on SDL_GPU

**Symptom.** With every prop fixed, the SDL_GPU frame was still visibly colder than Vulkan's:
blue-grey facades in shade, shop windows showing their interiors instead of the sky, darker
canopies. The street said why at start-up: `renderer has no image based lighting; falling back to
a hemisphere ambient term`.

**Root cause.** Not a street defect: `SupportsImageBasedLightingEXT()` was the interface default,
`false`, and truthfully so -- SDL_GPU's PBR fragment stage had no environment term at all. The
street asks exactly the right question and takes its documented fallback. `docs/cnaext-engine-layer.md`
listed it as not implemented.

**Fix.** The WebGPU route (`WMG-0022`), for the same equation:

* `pbr3d.frag.glsl` gains `cnaIblAmbient`, Vulkan's `CnaIblAmbient` term for term (split-sum:
  irradiance x `kD`, prefiltered specular at `roughness * (mipCount - 1)`, BRDF table), added to
  the ambient line as a sum -- `PbrEffect` already zeroes `AmbientLightColor` when an environment is
  bound (MOD-1226) -- and multiplied by occlusion only (MOD-1227). Both rigid and skinned PBR use
  this fragment stage, so SkinnedPbrEffect gets it too.
* The three resources sit at fragment samplers 10..12, after the shadow maps, read through
  `GraphicsDevice.SamplerStates[10..12]` as on Vulkan, EasyGL and WebGPU. `PbrParams` gains
  `iblParams` (enabled, mip count, intensity) and the three slots' LOD biases, applied in the shader
  like every other slot here because this renderer's samplers carry none.
* Resolved at the public draw, like every other map (REMED-GFX-152); a draw with no environment
  binds neutral white and the term switches off, because a pipeline uses every sampler its shader
  declares.
* `SupportsImageBasedLightingEXT()` answers true while a device exists.

**Tests.** `CNAEXT_ImageBasedLighting` **8/8**, white furnace included; `CNAEXT_GltfPbr` 5/5 and
`CNAEXT_Showcase` 8/8 -- all three were **skipped** on SDL_GPU before and now run and pass. No
modern `CnaGraphicsExtTests` skip was IBL-gated (every one of the 31 is inline GLSL ES, a GPU timer
or a refusal path), so that suite stays 931 / 0 / 31. Classic `-R '^SdlGpu'`: the same 27 by
name. `GltfRendererPbrFallbackPolicy`: the three pre-existing failures only.

**The street after the fix:** 2-21 % of pixels differ from Vulkan (mean 0.5-4.8/255), from 44-99 %
before STREETS-0001; the environment is baked (`environment baked -- peak radiance 1.445`) and the
fallback message is gone.

---

## STREETS-0003 — the engine layer read the HDR scene with point sampling

**Symptom.** After STREETS-0002 the two frames agreed in tone to a fraction of a level, but 2-21 %
of pixels still differed, concentrated on foliage and distant roof edges. Found by measurement, in
the order STREETW-0003 used:

* **Not a shift.** A sub-pixel search over +-1 px puts the optimum at exactly (0, 0).
* **Not tone.** Frame means agree to 0.3/255 per channel.
* **Sharpness.** The SDL_GPU frame carried 12-20 % more high-frequency energy than Vulkan's.
* **Not MSAA.** `multiSample: 0` changes neither renderer's capture (it only affects the back
  buffer, which receives the final composite).
* **FXAA.** With `fxaa: false` in both, the high-frequency energy matches (8.06 vs 7.97). With it,
  Vulkan's drops 33 % and SDL_GPU's only 19 %: the pass ran, and did less.

**Root cause.** FXAA's taps sit at sub-texel offsets, so it depends on a linear filter.
`CNA::Graphics::FullscreenPass` degrades a filtered read of a half-float source to `PointClamp`
wherever `EngineLayerFloatFilteringScope::RendererFiltersFormat` says the renderer cannot filter
it, and on SDL_GPU that fell through to `HalfFloatTextureLinearFiltering`, which the renderer never
implemented: `SupportsHalfFloatTextureLinearFilteringEXT()` was the interface default `false`, and
its own capability switch hardcoded the same. So every engine-layer pass sampling the HDR scene --
FXAA, and `BloomPass`, which asks the same capability -- read nearest texels. `plans/plan_sdlgpu.md`
had recorded "half filtering still inherited" as owned by later sampler/effect tasks; none took it.

**Fix.** `SupportsHalfFloatTextureLinearFilteringEXT()` answers true where an RGBA16F render
target exists on the device. SDL_gpu has no filterability query, unlike Vulkan's
`vkGetPhysicalDeviceFormatProperties`, and does not need one: every backend it drives guarantees
linear filtering of 16-bit float colour -- Vulkan's required-format table mandates
`SAMPLED_IMAGE_FILTER_LINEAR` for R16G16B16A16_SFLOAT, D3D12 from feature level 11, Metal on every
GPU family. 32-bit float formats carry no such guarantee and are untouched. The XNA-level refusal
of a filtered float read outside the engine layer (SOFTWARE-217) is unaffected: it does not
consult this capability.

**Tests.** `SdlGpu_HalfFloatFiltering` (new): a 2x1 HdrBlendable target, black then white,
stretched through `FullscreenPass` with `LinearClamp`. **A/B:** with the old answer the row is
0 / 0 / 0 / 255 / 255 with **0** intermediate pixels; with the fix it ramps 0 / 4 / 124 / 131 /
251 / 255 with 26. `SdlGpu_Smoke` pinned the old `false` ("half-float storage is absent"), which
was never true of its render targets; the check now pins the new contract (filtering exactly where
RGBA16F targets exist) and passes -- the test still fails on its two older stale checks, as on the
baseline. Classic `-R '^SdlGpu'`: the same 27 by name; `-L CnaExt` 27/1/4; `CnaGraphicsExtTests`
931/0/31; the 79 capability/format/bloom/FXAA cases in `CnaTests` pass except
`ClassicTextureFormat.PointSamplingExpandsChannelsAndPreservesDeclaredRanges`, which fails
identically with the change stashed (a NormalizedByte4 BasicEffect expectation, not filtering).

**The street after the fix:** 1.0-7.2 % of pixels differ from Vulkan at 17 of the 18 viewpoints
(mean 0.3-2.0/255), and high-frequency energy matches (06: 4.82 vs 4.68; 13: 5.40 vs 5.33).
`06-above-the-junction` stays at 18.9 % -- see the next task.

---

## STREETS-0004 — a ShaderEffect's texture units ignored their own sampler states

**Symptom.** After STREETS-0003, `06-above-the-junction` still differed from Vulkan at 18.9 %, the
rest at 1-7 %. A bisect over the street's optional passes, each capture taken on both renderers:

| passes | pixels differing from Vulkan, 18 viewpoints |
|---|---|
| all optional passes off (`--no-ssao --no-light-shafts --no-probes --no-bloom --no-fog --no-clouds`) | **0.000 %** at 14 of 18, at most 0.23 % |
| everything on except SSAO (`--no-ssao`) | **0.000 %** at 13 of 18, at most 0.23 % |

So with IBL, instancing and filtering fixed the two renderers already agreed pixel for pixel, and
SSAO was the whole remaining difference.

**Root cause.** `SsaoPass` draws through `FullscreenPass` (a SpriteBatch with a `ShaderEffect`) and
binds two more textures on the effect: the normals at unit 1 and a 4x4 rotation-noise texture at
unit 2, which the shader tiles across the screen and so reads through `SamplerStates[2]`'s Wrap.
In XNA texture unit N samples through `SamplerStates[N]`; the Vulkan renderer fixed exactly this in
`VULKAN-166`. SDL_GPU's sprite route bound the **SpriteBatch's own sampler** (here `LinearClamp`) to
every unit of the effect, so the noise was clamped to its edge texel almost everywhere and the
occlusion kernel stopped rotating. Its 3D `ShaderEffect` route was worse: every unit got
`AcquireComputeSamplerEXT()`, nearest/clamp, whatever the game had set.

**Fix.** `GraphicsDevice.SamplerStates[0..3]` is captured with each custom-effect draw
(`SnapshotEffectUnitSamplersEXT`, shared by every command queued with it) -- the draw replays at
`Present()`, so it cannot read the live state -- and `BindCustomEffectSamplersEXT` binds unit N
through state N, its LOD bias carried on the native sampler because a `ShaderEffect` applies none
of its own. Unit 0 of a SpriteBatch draw keeps the batch's sampler: SpriteBatch assigns
`SamplerStates[0]` through its private renderer channel, which never reaches `samplerSlots_[0]`.
The sprite route now uses the same binder as the 3D route instead of its own copy, so the two
cannot resolve a unit differently again.

**Tests.** Vulkan's `VULKAN-166` witness is compiled again for this renderer
(`SdlGpu_ShaderEffect_PerUnitSampler`): two 1x1x2 volumes sampled at W = 1.25 through two units
whose states differ only in AddressW. Legs A-D are XNA-only; `CNA_PER_UNIT_SAMPLER_NO_VULKAN_VALIDATION`
leaves out leg E, which reads the Vulkan validation layer, and changes nothing when undefined.
**A/B:** on the old renderer A and B fail with exactly the diagnosed shape -- "unit 0 read slice 0
(Wrap), unit 1 read slice 0 (Wrap)", both units on the batch's sampler; with the fix 5/5.
Classic `-R '^SdlGpu'` the same 27 by name, `-L CnaExt` 27/1/4, `CnaGraphicsExtTests` 931/0/31.

## Result

The street on SDL_GPU against the same street on Vulkan, same GPU and driver, all 18 viewpoints:

| | pixels differing (> 8/255 in any channel) | mean difference |
|---|---|---|
| as found | 44 - 99 % | 15 - 80 /255 |
| after STREETS-0001 (instancing) | 41 - 99 % | 7 - 22 /255 |
| after STREETS-0002 (IBL) | 2.0 - 21.4 % | 0.5 - 4.8 /255 |
| after STREETS-0003 (half-float filtering) | 1.0 - 18.9 % | 0.3 - 4.1 /255 |
| after STREETS-0004 (per-unit samplers) | **0.000 - 0.005 %** | **0.00 /255** |

The two renderers now produce the same picture. Nothing in cna-street needed changing: every
defect was CNA's, and the street's own capability questions (`SupportsImageBasedLightingEXT`,
`GetShaderDialectEXT`, `SupportsShadowSamplingEXT`) were asked correctly and answered wrongly or
not at all.
