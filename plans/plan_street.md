# cna-street on current CNA: EasyGL baseline, then Vulkan

Owner brief of 2026-09-22: `cna-street` (`../cna-street`, the city-street demo built on the CNAEXT
engine layer) had only ever run on the EasyGL renderer family and had stopped working against
current `next`. Restore and verify the EasyGL baseline first -- visually, including the pedestrians'
faces -- then build and run it on the Vulkan renderer on the real GPU and compare the two.

Branch `street` from `next` `29cfe0869`. Task IDs `STREET-0001`, … . Defects that belong to
cna-street are fixed in cna-street and listed here only for the record; defects that belong to CNA
are fixed here, on the layer they belong to, with a regression test.

## Status

| ID | Task | Status |
|---|---|---|
| STREET-0001 | Consuming CNA with `add_subdirectory()` failed at configure (test display policy) | ✅ |
| STREET-0002 | `CascadedShadowMap` could not create a High atlas under any profile | ✅ |
| STREET-0003 | Vulkan: the first 3D draw refused -- the lazily created default white could not get a descriptor set | ✅ |
| STREET-0004 | Vulkan: a frame with more stock draws than a uniform ring holds read past the buffer and lost the device | ✅ |
| STREET-0005 | Vulkan: an engine-layer effect allocated device memory on every draw (shadow pass 14.8 s a frame) | ✅ |
| STREET-0006 | Vulkan: the depth/normal prepass was the scene mirrored top to bottom; SSAO darkened mirrored geometry | ✅ |

---

## STREET-0001 — the display policy broke every consumer's configure

**Symptom.** `cmake --build build` in cna-street stopped in the regenerate step:
`cna_apply_test_display_policy_to Function invoked with incorrect arguments`, once per test.

**Root cause.** `cmake/TestDisplayPolicy.cmake` (GTI-0001/GTI-0006) defers its sweep to the end of
`CMAKE_SOURCE_DIR`. Stand-alone that is CNA's own root; as a subproject it is the consumer's, and a
deferred call runs in the scope of the directory it is deferred to. `CNA_TEST_WAYLAND_GUARD_APPLIES`
and `CNA_TEST_WAYLAND_GUARD` are ordinary variables of CNA's directory, so there they were unset and
the unquoted argument vanished (five arguments for six parameters). The sweep also walked the
consumer's whole tree and would have rewritten the consumer's own tests.

**Fix.** The deferred call receives CNA's root and both values, fixed when it is scheduled
(`cmake_language(EVAL)` with bracket arguments -- a plain `DEFER CALL` evaluates its arguments when it
runs), and sweeps CNA's directory tree only. **Test:** `CnaTestDisplayPolicyAsSubproject` configures
a two-level consumer fixture (`cmake/Tests/TestDisplayPolicySubproject`) and checks the framework
test lost its empty `DISPLAY=`, gained the Wayland guard, and the consumer's own test was left
alone. It fails on the old file with the exact error cna-street hit.

## STREET-0002 — a High cascade atlas exceeded XNA's HiDef ceiling

**Symptom.** cna-street logged `no shadow map: RenderTarget2D exceeds the active graphics profile's
maximum texture size` and rendered with no shadows at all.

**Root cause.** `CascadedShadowMap` lays its cascades side by side in one `RenderTarget2D`
(MOD-907), `cascadeSize × cascadeCount` wide: 6144 for three High cascades, 8192 for four. Since
SOFTWARE-215 (2026-09-09) `RenderTarget2D` enforces XNA's profile ceiling (4096 for HiDef), so every
atlas from three High cascades up is refused on every renderer and profile. The strip layout is baked
into every receiver (EasyGL GLSL, Vulkan SPIR-V), so re-laying the atlas is not a local change.
Every `CascadedShadowMapTests` case used `ShadowQuality::Low`, which is why nothing noticed.

**Fix.** The VMG-0006 precedent: the engine layer is not XNA code, and is held to what the renderer
really does rather than to XNA's portability ceiling. `CNA::Internal::EngineLayerTextureSizeScope`
(`modules/graphics/include/CNA/Internal/Graphics/EngineLayerTextureSize.hpp`) lets a
`RenderTarget2D` created while it is open use `GraphicsDevice::GetMaxTextureDimension()`;
`CascadedShadowMap` opens it around the atlas allocation and nowhere else. A game's own render target
keeps the XNA ceiling. **Test:** `CascadedShadowMapTest.AnAtlasWiderThanTheHiDefCeilingIsStillAllocated`
(three and four High cascades allocate; a plain 6144-wide `RenderTarget2D` still throws) — passes on
Vulkan (RADV) and OPENGLES3, fails without the fix.

## STREET-0003 — the default white texture could not get a descriptor set

**Symptom.** On Vulkan, the first reflection-probe capture failed with `the default white texture's
descriptor set could not be allocated`; the exception left a cascade pass open and the application
died at the next `CascadedShadowMap::update`.

**Root cause.** `EnsureDefaultWhiteTexture()` creates the renderer's white 1x1 lazily, on the first
3D draw that needs it, and allocated its set from the base `descriptorPool_` alone. Every texture's
set comes from the same pool through `AllocateTexSamplerDescSetEXT`, which chains a fresh pool when
one is full (VULKAN-390); the white did not. In a real scene the white is first needed after hundreds
of textures and dozens of ShaderEffect bound sets have used the base pool.

**Fix.** The white's set comes from the same chaining allocator (a hard failure still refuses by
name, VULKAN-391). **Test:** `Vulkan_DescriptorPoolOverflow` leg E injects the allocation failure
into exactly the white's set (the first allocation `DrawPrimitivesEx` makes) and requires the draw
to succeed and reach the back buffer; the old code fails it with cna-street's exact message.

## STREET-0004 — per-frame uniform rings overflowed into device loss

**Symptom.** With STREET-0003 fixed, the probe bake lost the device (`VK_ERROR_DEVICE_LOST` from
`vkQueueSubmit`) after the layer reported `VUID-vkCmdBindDescriptorSets-pDescriptorSets-01979`:
`pDynamicOffsets[0] is 262144, which when added to the buffer descriptor's range (512) ... is greater
than the size of the buffer (262144)`.

**Root cause.** Every stock 3D family takes its per-draw uniforms from a per-frame dynamic-uniform
ring of fixed size: 512 blocks for PbrEffect, 32 bone palettes for SkinnedEffect and
SkinnedPbrEffect, 512 for the shadow receivers. Past the end the copy was skipped but the set was
bound with the out-of-range offset anyway, so the GPU read past the buffer. Only the lit-textured
family refused by name. cna-street draws ~1200 PBR batches and ~150 skinned figures a frame, and a
probe face more.

**Fix.** The rings grow the way the per-frame vertex arenas already do (`GrowFrame3DArenaEXT`): at
the top of `RecordCommandBuffer`, when every draw is queued and the frame slot's fence has signalled,
each family's draws are counted -- mirroring the recording chain's precedence -- and its ring for
this slot is grown before any command names it; every descriptor set of that family for this slot
(its cache plus the queued draws' own) is repointed at the new buffer. The lit-textured refusal is
replaced by the same growth. **Test:** `Vulkan_UniformRingGrowth` -- one frame of 1500 PbrEffect and
100 SkinnedPbrEffect draws, each lit only by its own emissive factor in its own cell: 1500/1500 and
100/100 with the layer silent; the old renderer draws 512/1500 and 8/100 and reports the VUID above.

## STREET-0005 — an engine-layer effect allocated device memory on every draw

**Symptom.** cna-street on Vulkan ran at 0.3 fps: `shadow 14773 ms` a frame for 925 caster draws, and
the 29-probe reflection bake took 592 s (7 s on EasyGL). Every sampled stack was in
`VulkanEffectRenderer::GetOrCreateBoundTextureSetEXT` -> `CreateBuffer` -> the driver's VA allocator.

**Root cause.** The engine-layer programs (shadow casters, the depth/normal prepass) read their
matrices from binding 19. `SetUniformMat4("uWorld")` -- once per caster draw -- marked the effect's
whole uniform-array block dirty, and the next draw copied all ~9 KB of it into a freshly created
VkBuffer with its own VkDeviceMemory: one `vkAllocateMemory` and one retired buffer per draw.

**Fix.** The engine matrices (384 bytes) are suballocated from a renderer-wide arena of 4 MiB chunks
each time an effect's set is built; the array buffer is recreated only when an array really changes.
A full chunk is retired on the usual frame fence; an effect remembers which chunk its set names and
rebuilds the set once that chunk is no longer current, so no set can outlive the chunk it names.
Measured on cna-street: 23.8 fps (shadow 37 ms), probe bake 20 s. **Test:**
`Vulkan_EngineMatrixArena` -- 1500 prepass draws placed only by `uWorld` all land in their own cells
and retire 0 buffers; the old renderer retires 1499.

## STREET-0006 — the Vulkan prepass was mirrored, and SSAO leaned on it

**Symptom.** cna-street on Vulkan showed a translucent ghost of the city over itself: roofs doubled
from above the junction, dark rectangles with vertical stripes on the asphalt, a pillar's outline
across a pedestrian's shirt. `--no-ssao` (which also skips the prepass) removed all of it.

**Root cause.** Every stock 3D vertex program in the Vulkan renderer ends with
`gl_Position.y = -gl_Position.y` (REMED-GFX-011): it takes D3D-style clip space into Vulkan's, and
the pipelines' `frontFace` (clockwise) assumes it. The engine layer's SPIR-V prepass programs
(`depth_normal_prepass/{rigid,skinned}.vulkan.vert.glsl`) did not, so the depth, normal and velocity
images were the scene mirrored top to bottom, with mirrored winding. The SSAO estimate only ever
compares the prepass with itself, and its kernel offsets view-space Y straight into texture Y -- which
agrees with GL's bottom-up storage, and so also with the mirrored image -- so it "worked", and its
result was composed mirrored over the scene. The consumers that combine the prepass with the scene
(aerial perspective, contact shadows, SSR, decals) are written for top-down storage and were wrong
against a real prepass. `CNAEXT_Showcase` passed on both counts: it counts darkened pixels.
Measured: a quad drawn by BasicEffect at the top of a target and the same quad drawn by the prepass
landed at opposite ends.

**Fix.** The two prepass programs flip like every other 3D program, after `vCurrentClip` so the
velocity output keeps its convention; the Vulkan SSAO estimate negates the kernel's Y when it turns
it into a texture offset, the one place it assumed bottom-up storage. SPIR-V regenerated with
`tools/shader_package/generate_shader_package.py` (only the three payloads changed). Showcase's
SSAO darkening on Vulkan is now 4954/942/38 pixels at >=2/8/20 against EasyGL's 4762/882/45
(before the SSAO half of the fix: 47215/23503/13018 -- the count Showcase fails on). **Test:**
`Vulkan_EngineMatrixArena` check D draws the same quad with BasicEffect and the prepass and requires
the same place, and check A now reads the prepass's own encoded normal rather than "not the value
at one corner" (which the mirrored image satisfied). Both fail on the old shaders. Not changed:
the Vulkan motion-blur camera path reconstructs clip space from `TexCoord` as if Y pointed up;
cna-street does not use motion blur.

## cna-street's own defects (fixed in cna-street)

* **Every tiling surface stretched down the street.** Since FX-126 `SpriteBatch.Begin` publishes its
  sampler into `SamplerStates[0]`, as XNA and FNA do; cna-street never set a sampler for its 3D passes
  and had been inheriting the device's untouched `LinearWrap`. After the overlay's SpriteBatch or a
  post pass, the asphalt, paving and car atlases were sampled clamped. cna-street now sets
  `LinearWrap` on its material slots at the start of each pass that samples materials.
* **Every reflection probe failed** with `The render target must be resolved before its data can be
  transferred` (SOFTWARE-246, XNA's rule): the capture read a face back while the target was still
  set. It now unsets the target first.
