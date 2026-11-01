# C effects contract

The public C surface for effects, reflection, stock materials and the core CNA material extensions
is declared in [`effects.h`](../../modules/c-api/include/CNA/C/effects.h). This page describes the
consumer-visible ownership and behavior that is not obvious from individual declarations. The
generated [`COVERAGE.md`](COVERAGE.md) remains the symbol-by-symbol XNA/C++ mapping, while
[`LIMITATIONS.md`](LIMITATIONS.md) records open C-surface gaps and test boundaries.

## Handles, ownership and lifetime

`CNA_EffectHandle` is an owned game-child handle. Creation routes borrow a graphics-device handle;
the effect retains its native device and must still be released with `cna_effect_destroy`. Clone
preserves the concrete runtime type and effect state. Dispose leaves the C handle queryable, but a
later Apply returns `CNA_RESULT_INVALID_STATE`; destroy releases the handle itself.

An effect loaded by `cna_content_manager_load_effect` has one additional owner: unloading or
destroying its ContentManager disposes the native effect, matching XNA. Its C handle remains valid
for metadata queries and cleanup, but Apply then reports `System.ObjectDisposedException` through
the C error channel.

Parameter, technique, pass, annotation and directional-light handles returned from an effect are
owned views, not borrowed native pointers. They retain the native owner, game-child relationship
and relevant texture sidecars. A live descendant therefore remains usable after the parent effect
handle or an intermediate collection-view handle is destroyed. Collection growth does not
invalidate an existing element view.

All routes still follow the general handle, callback-window and thread rules documented in
[`HANDLES.md`](HANDLES.md), [`OWNERSHIP.md`](OWNERSHIP.md) and
[`CALLBACKS_AND_THREADING.md`](CALLBACKS_AND_THREADING.md).

## Reflection values and collections

`CNA_EffectParameterClass` and `CNA_EffectParameterType` are fixed-width `uint32_t` identities.
All five parameter classes and ten storage types have stable C constants; they describe reflected
metadata and never expose a native renderer object.

### Annotations

`CNA_EffectAnnotationHandle` owns an immutable native annotation created from copied UTF-8 name,
semantic, cached string and raw float-storage input. Row and column metadata are preserved.
Boolean and Int32 annotations interpret the first four bytes of the first float slot as an integer
bit pattern; there is no numeric float-to-integer conversion. Single and vector values use ordinary
floats. Native column-major matrix storage is returned as the established row-major `CNA_Matrix`.
Missing data produces the native defaults: false, zero, zero vectors or identity matrix.

String routes use the common exact count/copy convention: no terminator and no partial write when
capacity is insufficient. Annotation collections copy values on add and return new owned immutable
copies from index or exact-name lookup. Absence is a successful false result with an invalid output
handle, and returned copies are independent of the source annotation and collection.

### Parameters

`CNA_EffectParameterHandle` owns either a standalone mutable parameter or a stable alias into a
collection. `CNA_EffectValueType` selects the native scalar and array overload without placing a
C++ variant or container in the ABI. Boolean arrays require canonical `CNA_Bool` values; ordinary
and transposed matrix tags deliberately select different native overloads while both carry
row-major `CNA_Matrix` values. Get operations take a requested count, capacity and `out_count` so a
capacity failure performs no partial write.

Texture tags preserve the native base, Texture2D, Texture3D and TextureCube setter overloads and
the three typed getters. Each typed slot is independent. Assigned C texture handles are retained
until that slot is cleared or replaced, or until the parameter hierarchy is released. The base
`Texture*` overload remains setter-only, like the native API.

Parameter collections can own standalone entries or expose mutable element/member views. Add
constructs directly in stable native storage. Index, exact-name and exact-semantic lookup return
owned aliases that share mutations. Nested element, member and annotation collections transitively
retain their owner. Native const/non-const indexers and iterators collapse to one C count/lookup
surface because they do not represent distinct operations in a C ABI.

## Techniques, passes and Apply

Technique and pass handles likewise own standalone values or stable collection aliases. Default
technique construction produces an empty name and empty pass collection; named construction adds
the canonical `P0` pass. Reflected constructors expose the shape created by compiled effects.

A technique identity and a runtime index are different values:

- `cna_effect_technique_get_identity` returns a unique opaque `uint64_t` token for an instance.
- `cna_effect_technique_get_index_ext` and `cna_effect_pass_get_index_ext` return reflection
  positions used to select a compiled runtime pass. Hand-built values report index zero.

`cna_effect_set_current_technique` accepts only a technique owned by that effect. The selection
cannot be cleared: `CNA_INVALID_HANDLE` returns `CNA_RESULT_INVALID_ARGUMENT` and leaves the
current technique unchanged, matching XNA's null-assignment `ArgumentNullException`. A technique
added through that effect's own collection belongs to the effect and can be selected; a standalone
foreign technique cannot.

`cna_effect_pass_apply` always calls the native pass Apply operation. An ownerless hand-built pass
keeps the native successful no-op behavior. An effect-owned pass requires its owning technique to
be current; otherwise it returns `CNA_RESULT_INVALID_STATE` with the state-category
`System.InvalidOperationException` diagnostic. Restoring the owning technique makes the same pass
usable again.

## Effect construction

`cna_effect_create_empty` supplies a minimal concrete implementation of CNA's abstract Effect base
contract. `cna_effect_material_create`, `cna_shader_effect_create` and
`cna_sprite_effect_create` create their corresponding concrete types. Type names and shader source
use exact count/copy operations.

### Compiled Effect Framework bytecode

`cna_effect_create_compiled` accepts compiled XNA/FNA Direct3D 9 Effect Framework bytes, including
the XNA wrapper and the same payload carried in an Effect XNB. It does not accept HLSL source,
source-shader payloads or MonoGame's distinct MGFX container. Empty or structurally invalid data is
`CNA_RESULT_INVALID_ARGUMENT`; recognized MGFX is `CNA_RESULT_NOT_SUPPORTED`. These argument
decisions happen before renderer capability is consulted.

Acceptance of structurally valid bytecode is renderer-specific. Query
`CNA_GRAPHICS_CAPABILITY_COMPILED_EFFECTS` on the device:

| Renderer family | When the capability is available |
|---|---|
| `FNA3D` | Always |
| `SDLGPU` | `CNA_SDLGPU_COMPILED_EFFECTS=ON` |
| EasyGL (`OPENGLES3`, `OPENGL33`, `WEBGL2`) | `CNA_EASYGL_COMPILED_EFFECTS=ON` |
| `WEBGPU` | `CNA_WEBGPU_COMPILED_EFFECTS=ON` |
| `SOFTWARE` | `CNA_SOFTWARE_COMPILED_EFFECTS=ON` |
| `DIRECTX9` | `CNA_DIRECTX9_COMPILED_EFFECTS=ON` |

All other cases report the capability as false and refuse valid bytecode instead of substituting a
stock shader. [`fx-compiled-effects.md`](../fx-compiled-effects.md) is the authoritative capability
and conformance matrix.

`cna_effect_get_is_compiled_ext` reports whether an effect owns a compiled runtime. A compiled
clone preserves that runtime and its parameter values. This is distinct from
`cna_effect_has_renderer`, which reports the source-based program object used by ShaderEffect. A
compiled bytecode Effect can therefore report compiled=true and has-renderer=false.

### Source-based ShaderEffect

`cna_shader_effect_create` accepts the payload dialect reported by
`cna_graphics_device_get_shader_dialect_ext`; callers must not infer a dialect from the selected
renderer name. Text dialects use copied UTF-8 GLSL, WGSL, HLSL or MSL source as identified by that
query. Under `CNA_SHADER_DIALECT_SPIRV`, each stage is one complete host-order SPIR-V module: a
multiple of four bytes, at least the five-word header, beginning with `0x07230203`. It is copied
byte-for-byte and may contain NUL bytes.

Creation success means that the ShaderEffect object exists, not necessarily that the renderer
compiled or even inspected the source. `cna_shader_effect_is_valid` must be read accordingly:
false is a strong renderer rejection, while true can merely mean that no validation was performed.
`cna_shader_effect_copy_compile_error_ext` copies a renderer diagnostic when one exists. An empty
diagnostic can mean either success or that no compilation happened, so it is not proof that the
effect will draw. Custom source effects use `CNA_GRAPHICS_CAPABILITY_CUSTOM_EFFECTS`, which is
independent of compiled-effect capability.

The ShaderEffect surface includes scalar, vec2/3/4, matrix and array uniforms; world, view and
projection properties; and Texture2D, TextureCube and Texture3D bindings. The vec3-array route is
tightly packed at three floats per element. The mat4-array route takes 16 floats per matrix in
`CNA_Matrix` order. SPIR-V targets require
`cna_shader_effect_declare_uniform_block_ext` for the std140 block holding named parameters; the
same declaration is harmlessly ignored by dialects with loose uniforms.

Texture bindings require the effect's graphics device and retain the currently assigned C resource
per unit. If the active source-effect contract cannot bind a requested sampler kind, the call
returns `CNA_RESULT_NOT_SUPPORTED` and preserves both the previous binding and its retention.
Renderer program pointers and draw parameters never cross the C boundary. See
[`shader-effect-vs-fx-bytecode.md`](../shader-effect-vs-fx-bytecode.md) for the two shader contracts.

## Stock XNA effects and reusable interfaces

`cna_basic_effect_create`, `cna_alpha_test_effect_create`, `cna_dual_texture_effect_create`,
`cna_environment_map_effect_create` and `cna_skinned_effect_create` return ordinary owned effect
handles. Base lifecycle, cloning, techniques, Apply and graphics-resource operations therefore work
uniformly.

The generic `cna_effect_matrices_*`, `cna_effect_fog_*` and `cna_effect_lights_*` families dispatch
through IEffectMatrices, IEffectFog and IEffectLights. A valid effect that does not implement the
requested interface returns `CNA_RESULT_NOT_SUPPORTED`. Directional-light handles can be standalone
defaults or stable mutable aliases to the three lights owned by an effect. Nested aliases share
mutations and retain their effect after its parent handle is destroyed.

Type-specific routes expose the remaining BasicEffect, AlphaTestEffect, DualTextureEffect,
EnvironmentMapEffect and SkinnedEffect properties. AlphaTestEffect validates
`CNA_CompareFunction`, while its signed reference alpha remains deliberately unclamped.
EnvironmentMapEffect and SkinnedEffect lighting is always enabled; attempting to set it false
returns `CNA_RESULT_INVALID_STATE` instead of allowing a C++ exception to cross the ABI.

Stock texture slots require the same graphics device and retain their C handles independently.
Clones copy both native state and retention sidecars, so releasing or clearing a source effect does
not invalidate its clone's texture. `CNA_INVALID_HANDLE` clears only the selected slot.

SkinnedEffect uses a fixed `CNA_SKINNED_EFFECT_MAX_BONES` of 72. New effects initialize all 72
transforms to identity. Set accepts one through 72 copied `CNA_Matrix` values; copy uses requested
count plus capacity and writes nothing when the destination is too small.

## CNA color-matrix and PBR extensions

These types are CNA extensions, not XNA-origin APIs.

### ColorMatrixEffect

`cna_color_matrix_effect_create` creates an ordinary owned effect with a row-major 4-by-4 color
transform and RGBA post-transform offset. Matrix and offset setters reject non-finite components;
the grayscale helper uses Rec. 709 coefficients and reset restores identity plus zero offset. Clone
preserves both values.

The transform is applied through SpriteBatch, not by generic Effect Apply alone. A renderer must
explicitly advertise that path; otherwise `cna_sprite_batch_begin_with_effect` returns
`CNA_RESULT_NOT_SUPPORTED` instead of silently ignoring the transform. At present only the
`SOFTWARE` renderer advertises and implements this ColorMatrixEffect SpriteBatch path.

### PbrEffect and SkinnedPbrEffect

Both effects share the `cna_pbr_effect_*` material surface: base-color factor, alpha, metallic,
roughness, emissive, vertex-color enablement and seven independently retained Texture2D slots.

| Slot identity | Index | Samples are allowed to carry the sRGB flag |
|---|---:|---|
| `CNA_PBR_TEXTURE_BASE_COLOR` | 0 | yes |
| `CNA_PBR_TEXTURE_NORMAL` | 1 | no |
| `CNA_PBR_TEXTURE_METALLIC_ROUGHNESS` | 2 | no |
| `CNA_PBR_TEXTURE_EMISSIVE` | 3 | yes |
| `CNA_PBR_TEXTURE_OCCLUSION` | 4 | no |
| `CNA_PBR_TEXTURE_SPECULAR_EXT` | 5 | no |
| `CNA_PBR_TEXTURE_SPECULAR_COLOR_EXT` | 6 | yes |

Each slot also has a packed UV selector limited to channel 0 or 1 and a versioned
`CNA_TextureTransformEXT` scale/rotation/offset value. Asking for the sRGB flag on a non-color slot
is `CNA_RESULT_INVALID_ARGUMENT`; it is not answered with a meaningless false value.

The material extensions expose index of refraction, specular strength and color, normal scale,
occlusion strength, output sRGB encoding, alpha mode (`OPAQUE`, `MASK` or `BLEND`), alpha cutoff and
double-sidedness. Scalars follow the native contract and are not generally clamped. Undefined
identities, malformed versioned transforms, UV channels outside 0..1 and non-canonical
`CNA_Bool` values are rejected.

Both PBR variants reuse the generic matrices, fog and lighting routes. Lighting is always enabled,
so setting it false returns `CNA_RESULT_INVALID_STATE`. SkinnedPbrEffect additionally accepts one,
two or four weights per vertex and uses the same fixed 72-bone copied-palette contract as
SkinnedEffect.

## Verification boundary

The eight registered strict-C fixtures `EffectAnnotationSmoke.c`, `EffectParameterSmoke.c`,
`EffectTechniqueSmoke.c`, `EffectSmoke.c`, `BasicEffectSmoke.c`, `StockEffectSmoke.c`,
`SkinnedEffectSmoke.c` and `PbrEffectSmoke.c` cover the reflection graph, lifecycle, stock effects,
ColorMatrixEffect and PBR state described above. `GeneratedRoutesSmoke.c`, `GraphicsExtSmoke.c` and
the C/C++ header baselines provide generated-route refusal and ABI-layout coverage.

Those fixture names describe contract coverage, not a promise that every renderer, platform and
sanitizer configuration runs the whole C suite. Renderer qualification is recorded by the relevant
renderer documents and CI jobs. The C smoke currently has no valid compiled Effect Framework
fixture, so it proves the three input refusals and clone-state relationship but not the successful
compiled constructor path; this open evidence boundary is tracked in
[`LIMITATIONS.md`](LIMITATIONS.md).
