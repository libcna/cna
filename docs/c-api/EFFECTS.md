# C Effect Metadata Contract

## Identities

`effects.h` exposes the native `EffectParameterClass` and `EffectParameterType` ordinals as
fixed-width `uint32_t` identities. All five class values and ten storage-type values are frozen by
strict C17 and C++23 assertions. These identities are metadata only; they do not expose native
shader or renderer objects.

## Annotation values

`CNA_EffectAnnotationHandle` owns an immutable native annotation built from copied UTF-8 name,
semantic, cached string and raw float-storage input. Row and column metadata are preserved verbatim,
matching the native constructor. The adapter validates only ABI structure prefixes, identity
domains, UTF-8 and caller-buffer safety.

Boolean and Int32 annotations preserve CNA's native representation: the first four bytes of the
first float slot are interpreted as an `int`. Callers construct such input by copying an `int32_t`
bit pattern into a float slot; no numeric float-to-integer conversion occurs. Single/vector values
consume ordinary floats. Matrix storage is native column-major annotation storage and the returned
`CNA_Matrix` is the established row-major value. Missing data preserves native defaults: false,
zero, zero vectors and identity Matrix.

Names, semantics and cached strings use exact count/copy operations with no terminator and no
partial write on insufficient capacity. Typed getters return copied C values; annotation handles
have no native pointers or mutable backing storage.

## Collections and evidence

`CNA_EffectAnnotationCollectionHandle` owns a mutable collection of annotation value copies.
Adding copies the source annotation. Index and exact-name lookup return new owned immutable copies;
absence is a successful false result with an invalid output handle. This collapses mutable/const
native index overloads and begin/end iterators into stable count/lookup operations. Returned copies
remain valid after the source annotation or collection is destroyed.

## Parameters and values

`CNA_EffectParameterHandle` owns either a standalone mutable parameter or a stable alias to a
parameter stored in a collection. Creation copies UTF-8 name/semantic metadata and preserves row,
column, class and type values. Exact count/copy operations expose the immutable strings without a
terminator or partial writes.

`CNA_EffectValueType` selects each native scalar and vector-array overload without placing a C++
variant or container in the ABI. Boolean arrays use validated `CNA_Bool` elements; every other
array uses its documented fixed C value. Get operations accept a requested native count and use
capacity plus `out_count` for atomic caller-buffer transfer. Ordinary and transposed Matrix tags
dispatch the distinct native overloads while keeping `CNA_Matrix` row-major. String values use the
same exact count/copy contract as metadata. Missing values retain the native false, zero, identity,
unit-W Quaternion/Vector4 and empty-array defaults.

Texture tags preserve all four native setter overloads and the three typed getters. Assigned C
texture handles are retained against disposal or destruction until the corresponding overload slot
is cleared, replaced or its parameter hierarchy is released. The base `Texture*` overload is
setter-only, matching the native API. The typed Texture2D, Texture3D and TextureCube slots remain
independent.

## Parameter collections and nested views

`CNA_EffectParameterCollectionHandle` owns an empty collection or a mutable view of a parameter's
elements/members. `add_create` constructs directly in native unique-pointer storage; index, exact
name and exact semantic lookup return owned aliases to stable elements. These aliases share
mutation state and remain valid across later collection growth and after the collection-view handle
is destroyed. Nested element/member collections and annotation collections similarly retain their
native owner. Native mutable/const indexers and iterator types collapse into count and lookup
operations because C has no reference or iterator ABI.

## Techniques and passes

`CNA_EffectTechniqueHandle` and `CNA_EffectPassHandle` own standalone values or stable aliases to
unique-pointer collection elements. Default technique construction preserves the empty name and
empty pass collection; named construction preserves the canonical automatically created `P0` pass.
Technique identities are opaque non-pointer `uint64_t` tokens from the native implementation.

Pass and technique collections expose construction-plus-add, count, index and exact-name lookup.
Returned aliases survive later collection growth and destruction of the collection-view handle.
Pass/technique annotation views and technique pass views retain their owner and share mutation.
The C Apply operation always invokes `EffectPass::Apply()`: ownerless D4 passes preserve its native
successful no-op, while effect-owned views supplied by the effect lifecycle use the same route and
therefore enforce current-technique identity before applying.

## Effect lifecycle and concrete types

`CNA_EffectHandle` is an owned game-child handle. `cna_effect_create_empty` supplies a minimal
concrete adapter for CNA's abstract `Effect` base contract; `cna_effect_material_create`,
`cna_shader_effect_create` and `cna_sprite_effect_create` construct the corresponding native
types. Clone preserves the concrete runtime type. Dispose keeps the handle queryable and maps a
later Apply to `CNA_RESULT_INVALID_STATE`; destroy releases the handle.

Parameter, technique, current-technique and nested pass views alias the native effect storage.
Destroying the parent effect handle does not invalidate a live descendant view: the descendant
retains the native effect, its game-child ownership and any shader-bound texture until the final
view is released. A current technique must originate from the same effect; the invalid handle
explicitly selects null, after which applying one of that effect's passes reports invalid state.

Type names and vertex/fragment shader sources use exact count/copy operations. Compiled XNA `.fx`
bytecode construction reaches CNA's current native limitation and returns
`CNA_RESULT_NOT_SUPPORTED`; it does not silently reinterpret bytecode as source. Renderer program
pointers and `GpuDrawParams` remain private C++ implementation details consumed through Apply and
draw paths.

ShaderEffect exposes all named scalar/vector/matrix/array uniform setters, Texture2D/TextureCube/
Texture3D bindings, world/view/projection properties and distinct renderer-present/program-valid
queries. Source creation remains successful when a backend has no custom-shader renderer; callers
inspect those queries instead of inferring compilation. Texture bindings require the same graphics
device and retain the currently bound C resource per unit.

## BasicEffect and reusable effect interfaces

`cna_basic_effect_create` returns an ordinary `CNA_EffectHandle`, so the base lifecycle, clone,
current-technique and GraphicsResource operations apply unchanged. Generic
`cna_effect_matrices_*`, `cna_effect_fog_*` and `cna_effect_lights_*` functions dispatch through
the native IEffectMatrices, IEffectFog and IEffectLights contracts. A valid effect that does not
implement the requested interface returns `CNA_RESULT_NOT_SUPPORTED`.

BasicEffect adds vertex-color and per-pixel-lighting flags; diffuse, emissive and specular colors;
specular power; alpha; and texture-enabled/Texture2D state. Texture assignment calls the native
owned-texture route, requires the same graphics device and retains the C texture handle. Clone
copies both native state and C lifetime retention, so the texture cannot be destroyed until every
assigning clone clears or releases it.

`CNA_DirectionalLightHandle` owns either a standalone default light or a stable mutable view of one
of an IEffectLights object's three members. Repeated views share mutations. Each nested view aliases
the native member and transitively retains its parent effect and game ownership after the parent
effect handle is destroyed. Default lighting delegates the native three-point preset and preserves
its exact ambient, direction, diffuse, specular and enabled values.

## Alpha-test, dual-texture and environment-map effects

`cna_alpha_test_effect_create`, `cna_dual_texture_effect_create` and
`cna_environment_map_effect_create` return ordinary owned `CNA_EffectHandle` game children. Base
lifecycle, exact type names, Apply, cloning and the reusable matrix/fog/light interface functions
therefore remain uniform across stock effects. Type-specific functions expose every remaining
material, vertex-color, alpha-test, two-layer texture and environment-map property.

AlphaTestEffect validates the fixed `CNA_CompareFunction` identity at the C boundary. Its signed
reference alpha deliberately remains unclamped, including values below zero or above 255, matching
the native contract. EnvironmentMapEffect always reports lighting enabled; setting false returns
`CNA_RESULT_INVALID_STATE` instead of allowing the native exception to cross the ABI.

Each Texture2D layer and TextureCube environment map requires the effect's graphics device and is
retained independently. A native clone copies both property state and the C ownership sidecars, so
clearing or destroying the source effect cannot invalidate the clone's resources. The invalid
handle clears one slot without disturbing the others.

## SkinnedEffect

`cna_skinned_effect_create` returns the same owned effect handle and reuses every matrix, fog and
lighting operation. Concrete functions expose diffuse, emissive and specular material state,
alpha, per-pixel-lighting preference, weights per vertex, the CNA vertex-color extension and a
same-device retained Texture2D. Lighting is always enabled; a false setter returns
`CNA_RESULT_INVALID_STATE`.

`CNA_SKINNED_EFFECT_MAX_BONES` is 72. Bone transforms cross the ABI as copied `CNA_Matrix` arrays:
set accepts one through 72 matrices, while copy takes an explicit requested count and caller
capacity. Insufficient capacity reports the required count without a partial write. New effects
initialize all 72 entries to identity, and native cloning copies both the palette and retained
texture ownership.

## Color-matrix and PBR extensions

`cna_color_matrix_effect_create` exposes CNA's extension-only fixed CPU color transform as an
ordinary owned effect. `CNA_ColorMatrix4x4` stores the native row-major 4-by-4 transform without a
C++ array type; offset, Rec. 709 grayscale and reset operations complete its public state. Matrix
and offset setters reject every non-finite component. Clone preserves both values, while renderer
draw parameters remain private behind Apply and SpriteBatch routing.

`cna_pbr_effect_create` and `cna_skinned_pbr_effect_create` share the `cna_pbr_effect_*` material
surface: base-color factor, alpha, metallic, roughness, emissive and five indexed Texture2D slots.
The fixed `CNA_PBR_TEXTURE_*` identities select base color, normal, metallic-roughness, emissive
and occlusion maps. Each slot requires the same graphics device, retains its assigned C resource
and is copied independently by clones. Both types reuse the generic matrix, fog and lighting
functions; lighting is always enabled and a false setter returns `CNA_RESULT_INVALID_STATE`.

SkinnedPbrEffect additionally exposes weights per vertex and the same copied palette contract as
SkinnedEffect. `CNA_SKINNED_PBR_EFFECT_MAX_BONES` is 72; set accepts one through 72 matrices and
copy uses requested count plus capacity with no partial write. These three classes are explicitly
CNA extensions and are not presented as XNA-origin APIs.

`EffectAnnotationSmoke.c` covers all metadata and typed getters, raw Boolean/Int32 storage, empty
fallbacks, exact strings, collection add/count/index/name behavior, copy independence, capacity
atomicity and invalid/stale/wrong-kind/wrong-thread handles. It runs unchanged under HEADLESS and
SDL_RENDERER, while a focused HEADLESS run is checked with ASan+UBSan. C17/C++23 translation tests
freeze both handles and the version-one descriptor layouts.

`EffectParameterSmoke.c` covers every scalar and array overload, ordinary/transposed matrices,
strings, defaults, metadata, nested parameter/annotation collections, collection stability,
texture overload dispatch and Texture2D retention. It also exercises atomic capacity failures and
invalid, stale, wrong-kind and wrong-thread handles under both tested renderers, with a focused
ASan+UBSan run. C17/C++23 assertions freeze value/texture tag ordinals plus handle and descriptor
layouts.

`EffectTechniqueSmoke.c` covers both technique constructors, automatic `P0`, unique identities,
pass Apply dispatch, nested annotation/pass views, both collection families, stable aliases across
growth and collection destruction, exact strings and invalid/stale/wrong-kind/wrong-thread paths.
The same strict-C source runs under HEADLESS and SDL_RENDERER and in a focused ASan+UBSan build;
C17/C++23 assertions freeze all four handle widths.

`EffectSmoke.c` covers the base adapter, EffectMaterial, ShaderEffect and SpriteEffect creation and
same-type cloning, compiled-bytecode refusal, disposal and Apply, device/current-technique/pass
validation, exact type/source strings, every shader uniform and matrix operation, renderer-state
consistency, texture dispatch/retention and descendant lifetime after parent-handle destruction.
It also covers invalid, stale, wrong-kind and wrong-thread calls under HEADLESS and SDL_RENDERER
and in a focused ASan+UBSan build; C17/C++23 assertions freeze `CNA_EffectHandle`.

`BasicEffectSmoke.c` covers exact BasicEffect/DirectionalLight defaults, every generic interface
and BasicEffect property operation, all three stable nested lights, the native default-lighting
preset, same-type clone state, Texture2D retention and nested-light lifetime after parent-effect
destruction. It runs under HEADLESS and SDL_RENDERER plus focused ASan+UBSan; invalid, stale,
wrong-kind and wrong-thread cases are included and C17/C++23 assertions freeze the light handle.

`StockEffectSmoke.c` covers exact defaults and type identities for AlphaTestEffect,
DualTextureEffect and EnvironmentMapEffect; every reusable interface and concrete property;
unclamped reference-alpha boundaries; enum, Boolean and texture-index validation; always-on
environment lighting; same-device Texture2D/TextureCube ownership; independent clone retention;
stable nested-light lifetime; Apply; and stale, wrong-kind and wrong-thread handles. The same
strict-C test runs under HEADLESS and SDL_RENDERER plus focused ASan+UBSan.

`SkinnedEffectSmoke.c` covers all 72 identity defaults, the fixed maximum, exact copied bone
round-trips, cloned palettes, insufficient-capacity atomicity, valid and invalid
weights-per-vertex values, every material/interface/extension property, always-on lighting,
Texture2D clone retention, stable nested-light lifetime, Apply and stale, wrong-kind and
wrong-thread handles. It runs unchanged under HEADLESS and SDL_RENDERER plus focused ASan+UBSan;
C17/C++23 assertions freeze the maximum.

`PbrEffectSmoke.c` covers ColorMatrixEffect identity, finite validation, grayscale, reset and
clone behavior; exact PbrEffect and SkinnedPbrEffect defaults; all shared matrix/fog/light/material
operations; all five texture slots and clone-aware retention; all 72 identity bones, bounded
palette transfer and weights; Apply, nested-light lifetime and invalid/stale/wrong-kind/
wrong-thread paths. It runs unchanged under HEADLESS and SDL_RENDERER plus focused ASan+UBSan;
C17/C++23 assertions freeze the color-matrix layout, slot identities and SkinnedPbr maximum.
