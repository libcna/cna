# glTF PBR bindings, defaults and renderer evidence

Current contract checked against renderer source and GltfRendererPbrFallbackPolicyTests.cpp on
2026-11-01. The GLTF-373…379/465…477 migration/debugging record remains in Git history;
old shader register counts, blob sizes and environment blockers are not current limits.

## Inventory and texture roles

The source-policy inventory contains six PBR-rendering families: directx9, easygl, fna3d,
sdl-gpu, software and webgpu. EasyGL supplies OPENGLES3/OPENGL33/WEBGL2 identities. HEADLESS
references maps for ownership/validation only; STUB has no 3D shading. Neither is pixel evidence.

PbrEffect/SkinnedPbrEffect use five core logical maps, followed by specular strength and color:

| Logical slot | Meaning/channel | Missing-map identity |
|---|---|---|
| 0 | Base RGB and alpha | Opaque white |
| 1 | Tangent-space normal RGB, decoded with 2*x-1 | Flat normal (128,128,255,255) for GPU UNORM textures |
| 2 | Roughness in G, metallic in B | White |
| 3 | Emissive RGB | White, leaving the emissive factor unchanged |
| 4 | Occlusion in R | White, fully unoccluded |
| 5 | Specular strength in A | White |
| 6 | Specular color in RGB | White |

SOFTWARE uses exact CPU semantic defaults (normal 0.5,0.5,1 before decoding), rather than allocating
neutral GPU texels. The other five families bind fallback textures. A missing normal must not use
white: decoded white points along (1,1,1), about 54.7 degrees from flat. RGBA8's conventional
128,128,255 is about 0.32 degrees from flat because exact 0.5 is not representable.

| Family | Current binding route |
|---|---|
| DirectX9 | HLSL pixel samplers s0…s6 |
| EasyGL | Explicit sampler uniforms and texture units 0…6 |
| FNA3D | Applied Effect Framework sampler registers s0…s6, resolved by sampler name |
| SDLGPU | Fragment sampler bindings 0…6; shader set 2 |
| SOFTWARE | Base surface plus a six-entry optional-map array and semantic defaults |
| WEBGPU | Bind group 1 sampler at 0, textures at 1…7 |

Native register/uniform-block layout is implementation-owned, not a common byte ABI. Additional
IBL/shadow resources are separate from these seven map roles; their existence does not make them
mandatory glTF image slots. See the current renderer guides for those extended features.

Both specular maps are sampled by all six current PBR families; the source-policy partition's
factor-only set is empty. This does not make KHR_materials_specular a claimed required extension:
importer extension acceptance and renderer shading are separate contracts
([glTF limitations](gltf-limitations.md)).

## Material equations and transforms

Base and emissive textures have independent sRGB decode flags. Output RGB encoding is a separate
destination policy; normal, metallic/roughness, occlusion and strength are linear data. Factors are
linear and are multiplied after the relevant decode. Alpha is not transferred. Specular-color
sampling has its own decode policy. When fog/output encoding is enabled, mixing belongs in linear
space before the final encode.

The historical mid-grey oracle distinguishes decode/encode using bytes 128 versus 188, rather than
only endpoints. Scalar-factor and separate-map witnesses likewise distinguish a transported
uniform from one actually consumed by the shader.

Core factors multiply base RGB/alpha, emissive, roughness and metallic channels. Normal scale
affects tangent-space X/Y before normalization; scaling all three components would cancel out.
Occlusion strength uses 1 + strength*(sample.r - 1). Independent UV set/affine transform state is
retained for each map; a shared source UV does not imply shared transformed coordinates.

The effect transports dielectric F0 and F90 separately after its material-factor calculations.
PBR mixes their dielectric/metal endpoints and evaluates Schlick as
F0 + (F90-F0)*(1-VdotH)^5; assuming grazing F90 is always one loses reduced specular strength.
Texture sampling and factor transport need complementary tests.

CPU World*View*Projection follows XNA row-vector order; each native shader/upload pair maps that
meaning into its own matrix representation. World normal directions use inverse transpose.
A skinned normal uses inverse transpose of the blended joint matrix before the world normal
transform, with the implemented finite fallback for a nearly singular blend. Tangents remain
directions and are re-orthogonalized against the corrected normal.

Tangent handedness composes the authored sign with World, applicable instance and blended-skin
determinant signs. A zero determinant preserves the incoming sign instead of erasing the frame.
The 72-entry palette and WeightsPerVertex gates (1, 2 or 4) must be consumed by the selected
skinned PBR path, not merely uploaded to a generic stock shader.

## Coverage, culling and vertex color

MASK uses the transported alpha-test vector and must stay on the PBR route. The canonical vector
is (cutoff,0,-1,+1); OPAQUE/BLEND use the never-discard form (0,0,+1,+1). Alpha blending still
requires the application's appropriate BlendState; alpha coverage is not blend-state selection.

doubleSided travels with the material but does not silently change RasterizerState in Model.Draw.
The glTF-aware application chooses CullNone for two-sided parts and the proper ordinary/mirrored
front-face policy for single-sided placements. Back-face normal/tangent shading is a separate
renderer responsibility; cull-state transport alone does not prove it.
See [coordinate conventions](gltf-conventions.md).

COLOR_0 is a linear multiplier of base RGB and alpha. Rigid stride 60 stores it at offset 56;
skinned stride 80 stores it at offset 76. All six current PBR families implement the product.
An uncolored primitive uses the white identity and disables VertexColorEnabledEXT.

The standing rule is to evaluate the requested material or refuse it explicitly before incompatible
layout interpretation/native submission. A shader expression, layout row or compiled blob alone
does not prove the dispatcher can reach it. The shared VertexColourPbrSupport guard remains a
tested helper, but the current implementing set is not an unfinished list of renderer refusals.
A deliberate application opt-out of vertex color is distinct from silently ignoring authored data.

## What each test establishes

GltfRendererPbrFallbackPolicy discovers consumers of pbrNormalMap, separates HEADLESS's
ownership-only role and requires a disposition for each PBR family. Its tests cover:

- semantic defaults and CPU/native/shader bindings, including rigid/skinned variants;
- channel use, material factors, alpha coverage, map transforms and color transfer;
- F0/F90, normal scale/occlusion strength, palette/influence gates and direction transforms;
- caller culling, vertex-color product and stride dispatch;
- each universal PBR draw field, with additional FNA3D specular/IBL/double-sided/UV checks.

These are source-fragment regression gates. They can detect a missing known expression without
building that renderer, but are not semantic proofs of arbitrary source or independent native
pixel qualification. A newly named field can still require a discriminating runtime oracle.

RendererStrideConformance executes import/draw/Present for its fixtures. It can detect an
unreachable route, but successful submission does not prove the resulting pixels. The separate
cross-renderer diffuse-color witness and renderer-specific PBR/map/transfer/normal/alpha suites
provide pixel evidence where that renderer supports readback.

Committed L7 golden checks validate their recorded metadata/products, not fresh renders.
Re-rendering the corpus twice under each named policy is separate qualification; the old
146-asset campaign count is not today's complete corpus or a current all-renderer pixel pass.
See [conformance harness](gltf-conformance.md) and
[stride boundary](gltf-renderer-stride-conformance.md).

## 2026-11-01 verification boundary

Content/Graphics test targets build in the existing multi tree. The combined source-policy/buffer
filter passes all 42 cases. All three stride-conformance cases pass for OPENGLES3, OPENGL33,
WEBGPU, SDLGPU, FNA3D, SOFTWARE and HEADLESS; STUB explicitly skips all three for lack of 3D.
HEADLESS's pass remains validation-only. The cross-renderer diffuse-color contract also passes.

DIRECTX9 is not compiled into that tree, and WEBGL2 needs its browser build. Neither was freshly
executed by this audit. No shader regeneration, whole-corpus L7 capture, cross-OS/device
qualification or new conformance milestone follows from these focused results.
