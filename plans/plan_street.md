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

## cna-street's own defects (fixed in cna-street)

* **Every tiling surface stretched down the street.** Since FX-126 `SpriteBatch.Begin` publishes its
  sampler into `SamplerStates[0]`, as XNA and FNA do; cna-street never set a sampler for its 3D passes
  and had been inheriting the device's untouched `LinearWrap`. After the overlay's SpriteBatch or a
  post pass, the asphalt, paving and car atlases were sampled clamped. cna-street now sets
  `LinearWrap` on its material slots at the start of each pass that samples materials.
* **Every reflection probe failed** with `The render target must be resolved before its data can be
  transferred` (SOFTWARE-246, XNA's rule): the capture read a face back while the target was still
  set. It now unsets the target first.
