# Graphics Extension ABI

`CNA/C/graphics_ext.h` is the C surface of CNA's extended graphics layer — the renderer-neutral
post-process effects, the PBR material and the render-pipeline settings that live beyond the XNA
4.0 contract.

## One ABI, two builds

The extended layer is an **opt-in CNA build option**. A stable C ABI cannot let exported symbols
appear and disappear with a build flag, so every declaration in this header exists in every build.
What changes is behavior, not shape:

```c
CNA_Bool available = CNA_FALSE;
cna_graphics_ext_is_available(&available);
```

- The **value** routes — the seven identities, `CNA_PbrMaterial`, `CNA_PbrMaterialEXT`,
  `CNA_RenderPipelineSettings` and their initializers — work in either build. They need no native
  extension object.
- The **effect** routes return `CNA_RESULT_NOT_SUPPORTED` when the layer is absent, with the same
  diagnostic every time.

## Settings values

`CNA_PbrMaterial` and `CNA_RenderPipelineSettings` mirror canonical settings bags whose accessors
assign without clamping. There is therefore nothing for a getter/setter pair to do that a struct
field does not already do, and the C mapping is the POD plus an initializer that reproduces the
canonical constructor defaults:

| Value | Canonical defaults |
|---|---|
| `CNA_PbrMaterial` | white albedo, metallic 0, roughness 0.5, opaque black emissive, unit normal scale and occlusion strength, no alpha blending, alpha cutoff 0.5, no textures |
| `CNA_PbrMaterialEXT` | white albedo, metallic 1, roughness 1, zero emissive, unit normal scale and occlusion strength, IOR 1.5, unit specular factor and white specular colour, opaque coverage with cutoff 0.5, single-sided, all seven texture slots empty on UV channel 0 with the identity transform, sRGB decode and encode on |
| `CNA_RenderPipelineSettings` | HDR off, exposure 1, gamma 2.2, no tonemapping, bloom off at intensity 1, SSAO off, medium render quality, disabled shadow quality, shadows off |

`CNA_PbrMaterialEXT` is the current shape of the canonical `CNA::Graphics::PbrMaterial`: seven
texture slots, the `KHR_materials_ior`/`specular` factors, a floating-point emissive factor,
three-way alpha coverage with `doubleSided`, per-slot UV channels and `KHR_texture_transform`, and
the four colour-management flags. `CNA_PbrMaterial` is its predecessor, frozen: an ABI major may
not change what an existing name means, layout or defaults included, so the newer shape arrived
under a new name (`docs/c-api/ABI_VERSIONING.md`). Existing consumers keep working; new code should
use the `EXT` form.

PBR texture slots are handles and are **non-owning** in both, exactly like the canonical
`Texture2D*` slots: storing one here does not keep that texture alive.

## Post-process effects

`CRTEffect` and `DepthEffect` are canonical `ShaderEffect` descendants, so `cna_crt_effect_create`
and `cna_depth_effect_create` return ordinary owned `CNA_EffectHandle` values. Every `cna_effect_*`
operation — clone, dispose, apply, type name, parameters — accepts them, and this header adds only
their own properties. Passing a CRT handle to a depth route (or the reverse) is
`CNA_RESULT_INVALID_HANDLE`.

The canonical CRT setters clamp to 0 through 1; the C routes pass the value through and only reject
non-finite input, so a caller observes the same clamping CNA applies.

`AsciiPostProcessEffect` is **not** a shader `Effect` — it performs its own read-back, quantization
and draw pass — so it has its own owned handle and is not accepted by the `cna_effect_*` routes.
Its two canonical `Draw` overloads collapse into one call whose nullable destination rectangle
selects between them: null fills the viewport, non-null uses the given rectangle.

## Evidence

The strict-C suite runs in both states. Under HEADLESS the extension layer is absent and the test
asserts the complete unavailable contract; under SDL_RENDERER it is present and the test creates
all three effects, exercises every property, and observes a real ASCII draw producing a measured
glyph grid.
