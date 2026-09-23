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
