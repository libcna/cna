# glTF API-change review (`GLTF-025`)

`plan_gltf.md` §25's gate: **every proposed public or CNAEXT member is reviewed here, with its
problem, shape, compatibility, migration and test recorded, before it is implemented.** A row that
has not been through this file must not appear in a header.

§25 states the *intent* of each proposed surface in one line. This file states the **member** — the
exact name, type, default and semantics — because that is what a reviewer can actually disagree with
and what a later reader has to live with.

The standing default is unchanged and worth repeating: **no new public glTF API.** Almost every fix
belongs inside `CNA::Internal::GltfImport`, `ContentManager`, or `modules/graphics` internals. A row
reaches this file only after the alternative — keeping it internal — has been rejected for a stated
reason.

---

## 1. Reviewed and approved for implementation

### 1.1 `ModelMeshPart` primitive type — `GLTF-073`

**Problem.** All three loaders compute `PrimitiveCount = numIndices / 3` and every draw is a
triangle list. A glTF `LINES`, `LINE_STRIP` or `POINTS` primitive decodes correctly (`GLTF-071`)
and then has nowhere to be expressed, which is why `GLTF-072` had to leave those four topologies
rejected: importing them would have moved the original defect from the import layer to the draw
layer rather than fixing it.

**Why not internal.** `ModelMeshPart` *is* the boundary. The topology has to survive from the
importer to `Model::Draw`, and both are on opposite sides of the public model API.

**Shape.**

```cpp
// Microsoft::Xna::Framework::Graphics::ModelMeshPart
/** @brief The topology this part's index buffer describes. */
CNAEXT [[nodiscard]] PrimitiveType getPrimitiveTypeEXTProperty() const;
CNAEXT void setPrimitiveTypeEXTProperty(PrimitiveType value);
```

`PrimitiveType` already exists and already carries every member needed —
`TriangleList`/`TriangleStrip`/`LineList`/`LineStrip` are real XNA 4.0, and `PointListEXT` is
already marked as CNA's own addition. **No new enum.**

The property is CNAEXT rather than plain public because real XNA 4.0's `ModelMeshPart` has **no**
primitive type: XNA carries it as an argument to `GraphicsDevice::DrawIndexedPrimitives` instead,
and every XNA `ModelMeshPart` is implicitly a triangle list. Adding it to the part is a CNA
extension and must be labelled as one, even though the *type* it uses is XNA's own.

**Compatibility.** Additive, and the default is `TriangleList` — which is what every part built by
any existing path already is. No existing behaviour changes.

**Migration.** None. A caller that never reads the property sees no difference.

**Test.** `GLTF-073`: a line-mode fixture imports with `LineList` on its part, a triangle fixture
still reports `TriangleList`, and the value survives a `.cnj` round-trip.

---

### 1.2 Topology-aware primitive count — `GLTF-078`

**Problem.** `numIndices / 3` is hardcoded in all three loaders. It is *right* for a triangle list
and silently wrong for everything else, and it is stated three times, so the three can drift.

**Shape.** Not new API — one shared internal helper, plus the `PrimitiveCount` **semantics** on
`ModelMeshPart` widening from "triangles" to "primitives of this part's topology" (§12.3's table:
`LineList` → `n/2`, `LineStrip` → `n-1`, `PointListEXT` → `n`).

**Compatibility.** The value is unchanged for every `TriangleList` part, which today is all of them.
The *meaning* generalises rather than changing.

**Migration.** None for a triangle-list caller. A caller that assumed `PrimitiveCount * 3 ==
numIndices` would be wrong for a line part — documented on the property.

**Test.** `GLTF-078`: §12.3's table asserted per topology at L5.

---

### 1.3 `PbrEffect` alpha state — `GLTF-228`, `GLTF-229`

**Problem.** `alphaMode`, `alphaCutoff` and `doubleSided` are first-class glTF material properties
with no home anywhere in CNA: `MeshOut` has no field and `PbrEffect` has no parameter. They are the
remaining half of defect **D7**, and the only part of a factor-only material that still does not
survive import.

**Why not internal.** They are *runtime shading state*, not import state. A mask threshold has to
reach the shader on every draw, so it must live on the effect.

**Shape.**

```cpp
// Microsoft::Xna::Framework::Graphics
/** @brief glTF's alpha-coverage modes (specification §3.9.4). */
CNAEXT enum class AlphaModeEXT { Opaque, Mask, Blend };

// Microsoft::Xna::Framework::Graphics::PbrEffect / SkinnedPbrEffect
CNAEXT [[nodiscard]] AlphaModeEXT getAlphaModeEXTProperty() const;   // default Opaque
CNAEXT void setAlphaModeEXTProperty(AlphaModeEXT value);
CNAEXT [[nodiscard]] float getAlphaCutoffEXTProperty() const;        // default 0.5
CNAEXT void setAlphaCutoffEXTProperty(float value);
```

Defaults are glTF's own (`OPAQUE`, `0.5`), so an effect nobody configures behaves exactly as it does
today.

**One reconciliation this review forces.** `CNAEXT.md` §5.5 already sketches an `AlphaMode` enum on
`CNA::Graphics::PbrMaterial`, the engine-layer data bag. Two independently declared alpha-mode enums
in one codebase is precisely the kind of duplication that later becomes a conversion function nobody
can delete. **Decision: one enum, declared here in `Microsoft::Xna::Framework::Graphics`, and
`PbrMaterial` uses that type when §5.5's N42 lands.** The effect is the right home because it is the
one both the importer and the renderer already depend on; the data bag depends on the graphics
module, not the reverse.

**Where the carried/applied line now falls** (`GLTF-372`). Carrying these three and *applying* them
are separate steps, and only one of the three has since crossed the line. The **`MASK` cutoff is
applied**: it is fragment-program work — every PBR shader already evaluates a `uAlphaTest` vector
and discards on it — not device state, so `PbrEffect`/`SkinnedPbrEffect` fill
`GpuDrawParams::alphaTest` from `AlphaModeEXT` and the cutoff
(`CNA::Internal::Graphics::AlphaTestVectorForAlphaModeEXT`, one mapping for both effects).
`BLEND`'s compositing stays **carried only**, because it is `BlendState` plus a draw order the
application owns — `GLTF-230` — and so does `OPAQUE`'s "alpha is ignored" rule for the same reason.
An effect whose mode is not `Mask` therefore binds the never-discard `{0,0,1,1}` default, which is
asserted over the whole corpus rather than only on the mask fixture: an implementation that wrote a
reference for every material would cut holes in every opaque surface.

**Compatibility.** Additive. New enum, new properties, defaults preserve current behaviour. Filling
`alphaTest` changes a rendered result only for a material that declares `MASK`, which previously
rendered as though it had declared `OPAQUE`.

**Migration.** None.

**Test.** `GLTF-228`/`GLTF-229`: `mat-factor-only-gold` carries `Blend`; a mask fixture carries
`Mask` with its authored cutoff; both survive the `.cnj` round-trip; the defaults are asserted on a
material that declares neither. `GLTF-372`: `mat-alpha-mask-cutoff` authors a cutoff of `0.75` — not
glTF's `0.5` default, and not the material's own `0.875` alpha — and
`AMaskMaterialsCutoffReachesTheDrawsAlphaTestVector` asserts the captured vector against the
manifest's own `gpuAlphaTest`, then evaluates the shader's discard expression at, just below and
well above the cutoff so the sign convention is asserted as behaviour.

---

### 1.4 `PbrEffect` double-sidedness — `GLTF-231`

**Problem.** Same as above — `doubleSided` has no home.

**Shape.**

```cpp
CNAEXT [[nodiscard]] bool getDoubleSidedEXTProperty() const;         // default false
CNAEXT void setDoubleSidedEXTProperty(bool value);
```

**Scope decision, recorded deliberately.** Carrying the flag and *applying* it are separate, and
this review approves only the first. Double-sidedness is a **rasterizer** concern —
`RasterizerState::CullMode` — which in XNA is per-draw device state set by the application, not
per-part state an effect applies. Having `Model::Draw` mutate the device's rasterizer state as a
side effect of drawing would be a surprising global change that no XNA application expects, and it
interacts with `GLTF-230`'s blend-state and draw-ordering work.

So: **the state is carried end-to-end and is provable at L3 and through the `.cnj`; applying it to
the rasterizer belongs with `GLTF-230`, and its acceptance is an L7 image comparison** (`GLTF-009`),
which does not exist yet. Recording that boundary here is the point of the gate — otherwise the flag
would look implemented while nothing honoured it.

**Compatibility.** Additive; default `false` is XNA's own `CullCounterClockwise` behaviour.

**Migration.** None.

**Test.** `GLTF-231` (this phase): `mat-factor-only-gold` carries `doubleSided = true` through
import and the `.cnj`. The rendering half is `GLTF-230` at L7.

---

### 1.4a Dielectric Fresnel factors on the PBR effects — `GLTF-343`, `GLTF-344`

**Problem.** Core glTF fixes a dielectric's normal-incidence reflectance at 4%, corresponding to
an IOR of 1.5. `KHR_materials_ior` replaces that constant with `((ior-1)/(ior+1))²`, while
`KHR_materials_specular` scales and colours the dielectric Fresnel term. cgltf parses all three
factor values, but CNA currently drops them before `MeshOut`; an application cannot inspect them
and a renderer cannot honour them later.

**Why not internal.** These are runtime shading inputs. Like metallic and roughness, they must live
on the effect so a non-glTF caller can set them and so cloning, offline content and the draw
parameter block have one source of truth. Keeping them only in the importer would strand them at
exactly the boundary the existing defect crosses.

**Shape.** The following members are added identically to `PbrEffect` and `SkinnedPbrEffect`:

```cpp
CNAEXT [[nodiscard]] float getIorEXTProperty() const;              // default 1.5
CNAEXT void setIorEXTProperty(float value);
CNAEXT [[nodiscard]] float getSpecularFactorEXTProperty() const;   // default 1
CNAEXT void setSpecularFactorEXTProperty(float value);
CNAEXT [[nodiscard]] Vector3 getSpecularColorFactorEXTProperty() const; // default One
CNAEXT void setSpecularColorFactorEXTProperty(const Vector3& value);
```

The raw properties retain the extension's authored factors. `FillGpuDrawParams` derives the two
shader-ready quantities shared by both effects: RGB `pbrDielectricF0` and scalar
`pbrDielectricF90`. The derivation follows the Khronos interaction rule exactly: multiply the IOR
reflectance by `specularColorFactor`, clamp that product per channel to 1, then multiply by
`specularFactor`; F90 is `specularFactor`. Keeping F90 separate is essential — a reduced specular
factor must also reduce grazing reflectance, which cannot be reconstructed from F0 alone.

**Compatibility.** Additive. The defaults derive F0 = `(0.04,0.04,0.04)` and F90 = 1, exactly the
constants every CNA PBR shader uses today. Existing callers and old `.cnj` files therefore keep
their current parameter block byte-for-byte. Setters follow the existing metallic/roughness effect
properties and do not silently clamp caller input; imported glTF values have already passed glTF
validation. The only clamp is the one the extension specification requires in the F0 equation.

**Migration.** None.

**Deliberate boundary.** This review approves factor transport only. The extension's two optional
textures need two additional texture bindings and per-map colour-space/UV handling on every PBR
renderer, and the shader must replace its core `mix(0.04, albedo, metallic)` formulation with the
extension's dielectric/metal BRDF mix. Neither change is accepted without a rasterising renderer
test. Until that work lands the registry remains `PARSED_BUT_IGNORED`, with a more precise reason:
factors reach L6 but no renderer consumes them, and texture inputs are still reported as absent.

**Test.** Effect default/setter/clone tests cover both classes. `mat-factor-only-gold` authors IOR
2, specular factor 0.3 and colour `(0.25,1,12)`, making the derived F0
`(1/120,1/30,0.3)` and F90 `0.3`; the blue channel proves the clamp happens before the strength
multiply. L3, direct L6 and `.cnj` parity compare those values against the fixture manifest. The
remaining shader/texture work stays explicitly open on `GLTF-343`/`GLTF-344`.

---

### 1.5 A home for rigid (non-joint) animation clips — `GLTF-294`

**Problem.** `GLTF-293` imports rigid node animation correctly, but the clip has nowhere to be
stored or played. `SkinningData::AnimationClips` is the only clip container that exists and its
track indices are **palette** slots; a rigid clip's are **scene-node** indices (§15.1.2). Putting
one in the other would let a reader apply a scene index as a palette slot — a silent corruption in
place of the silent drop `GLTF-293` removed, which is exactly why the converter currently reports
the clip instead of writing it.

**Why not internal.** A clip has to be playable by the application. `Model::Tag` is where CNA
already attaches per-model imported data (`SkinningData`, `MorphTargetDataEXT`), so this follows an
established convention rather than inventing one.

**Shape.**

```cpp
// Microsoft::Xna::Framework::Graphics
/** @brief Which index space a clip's track bone indices are in. */
CNAEXT enum class ClipTargetSpaceEXT { JointPalette, SceneNode };

// on the existing AnimationClipEXT
CNAEXT ClipTargetSpaceEXT TargetSpace = ClipTargetSpaceEXT::JointPalette;

/** @brief Clips whose tracks drive a Model's own bones rather than a joint palette. */
CNAEXT struct ModelAnimationsEXT : public System::Object
{
    CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;
    std::unordered_map<std::string, AnimationClipEXT> Clips;
};

/** @brief Poses a model's bones from a scene-node clip at a given time. */
CNAEXT void ApplyClipToBonesEXT(Model& model, const AnimationClipEXT& clip,
                                System::TimeSpan time);
```

`TargetSpace` on the clip is what makes the two spaces impossible to confuse: a container can hold
either kind and every consumer can tell which it has. It defaults to `JointPalette`, so every clip
that exists today keeps its meaning.

**Compatibility.** Additive. A model with no rigid clips has no `ModelAnimationsEXT` and an
untouched `Tag`.

**Migration.** None.

**A boundary this review names rather than hides.** `Model::Tag` holds one object, and a skinned
model already uses it for `SkinningData`. A glTF file with **both** a skin and rigid node animation
therefore has nowhere to put the rigid clips today. That is a real limitation, not an oversight: the
alternatives are a breaking change to what `Tag` means, or a second attachment point, and neither is
worth deciding for a case no fixture yet exercises. The importer **reports** it by name rather than
dropping it silently, and the follow-up is `GLTF-295`.

**Test.** `GLTF-294`: `anim-rigid-node` round-trips through the `.cnj` with its `targetSpace`, and
posing the model at `t = 1` rotates the animated node's bone by the quarter turn the file authors.

---

### 1.6 Bind-pose posing for a freshly loaded skinned model — `GLTF-262`

**Problem.** A skinned effect's bone palette defaults to `MaxBones` identity matrices. That is not
a neutral value and it is not "no skinning": it means *every joint matrix is the identity*, so the
mesh is posed in joint space and glTF's own `inverse(globalTransform(meshNode))` cancellation
(§3.7.3, `GLTF-247`) never applies. A skinned model that had been loaded and not yet animated
therefore rendered **wrong**, not merely still — and nothing in the API said so. The L6 capture
(`GLTF-008`) is what turned that from a suspicion into a measurement.

**Why not leave it to the application.** Real XNA's Skinned Model Sample does set the palette every
frame, so "the application always does it" is a defensible reading. It is the wrong one here for a
concrete reason: in that sample the *bind pose is the identity palette*, because the sample's
content pipeline bakes the mesh into skeleton space. glTF does not — its mesh node's transform must
be cancelled by the joint matrix — so CNA's identity default and glTF's bind pose are different
poses. Requiring game code to fix that would make "load a model and draw it" wrong by default for
every conforming glTF asset, which is exactly the class of silent-wrongness this campaign exists to
remove.

**Shape.**

```cpp
// Microsoft::Xna::Framework::Graphics
/** @brief Poses a skinned model in its bind pose, so it is drawable before any clip plays. */
CNAEXT std::size_t ApplyBindPoseBoneTransformsEXT(Model& model, const SkinningData& skinningData);
```

Both glTF loaders call it once, after the model is built. It computes the palette exactly the way
an application would — an `AnimationPlayer` over the model's own `SkinningData` with no clip
started — rather than deriving a second, parallel notion of "bind pose" that could disagree with
what playback produces at `t = 0`.

**Compatibility.** Additive, and behaviour-changing only where the behaviour was wrong: a model
with no skin is untouched, and any `SetBoneTransforms` an application already makes simply
overwrites the palette on its first frame, as it always did. The function holds no state.

**Why a free function and not a `Model` member.** `Model` is XNA 4.0 API and has no skinning
concept at all; the skeleton lives on `Model::Tag` by the sample's own convention. A free CNAEXT
function beside `ApplyClipToBonesEXT` keeps the XNA type unchanged and puts the two posing
operations in one place.

**Test.** `GLTF-262`: `GltfConformanceL6.AFreshlyLoadedSkinnedModelIsAlreadyPosedInItsBindPose`
asserts the captured palette of an untouched model equals the bind-pose palette, on every skinned
corpus fixture, and that at least one of them differs from the identity default — so the assertion
cannot pass on a model that was never posed. It is deliberately a corpus-wide claim: the bind pose
of `skin-armature-ancestor` **is** all-identity, and that is the point of `GLTF-260`.

---

### 1.7 Colour space — `GLTF-209` … `GLTF-212`

**Problem.** glTF §3.9.2 assigns each material texture a colour space: `baseColorTexture` and
`emissiveTexture` are **sRGB-encoded**, `normalTexture`, `occlusionTexture` and
`metallicRoughnessTexture` are **linear**. Lighting is defined in linear space and the result is
encoded for display. CNA did none of it: every image became `SurfaceFormat::Color` (RGBA8 UNORM),
the PBR shader sampled all five maps raw, and the lit result was written unencoded. Two errors that
partly cancel to the eye and are quantitatively wrong everywhere — an sRGB mid-grey albedo of `0.5`
was being lit as if it were linear `0.5` rather than `0.2140`, **2.3× too bright**.

**The decision: option B, shader-side.** The three options §13.3 records:

| | Approach | Why not chosen |
|---|---|---|
| A | Hardware sRGB texture formats plus an sRGB framebuffer | Correct and free at runtime, but needs a new `SurfaceFormat` across **41 renderers** — a change of that blast radius to fix a glTF defect is the wrong order of work |
| **B** | **Shader-side decode of the two sRGB maps, encode on output, gated per map** | **Chosen.** Renderer-local, no format change, adoptable one renderer at a time, and observable at L6 without a renderer at all |
| C | Decode baked into the texture bytes at import | Precision-lossy at 8 bits, and it would make the imported texture disagree with the file it came from |

**Shape.**

```cpp
// CNA::Internal::Renderers::GpuDrawParams
bool pbrBaseColorTextureIsSrgb = true;   // GLTF-210
bool pbrEmissiveTextureIsSrgb  = true;   // GLTF-210
bool pbrEncodeOutputToSrgb     = true;   // GLTF-212

// Microsoft::Xna::Framework::Graphics — on PbrEffect and SkinnedPbrEffect
CNAEXT bool getBaseColorTextureIsSrgbEXTProperty() const;   CNAEXT void set…(bool);
CNAEXT bool getEmissiveTextureIsSrgbEXTProperty() const;    CNAEXT void set…(bool);
CNAEXT bool getEncodeOutputToSrgbEXTProperty() const;       CNAEXT void set…(bool);
```

**Three flags, not one, and this is the part worth arguing about.** They are three different kinds
of statement. The first two describe *what a bound texture contains* — facts, defaulting to glTF's
own rule, and properties only because these effects are reachable from content that is not glTF and
may bind an already-linear texture. The third describes *where the fragment is going* — a genuine
policy choice, because an application rendering into an sRGB target or running its own tone map
must switch off the encode without thereby claiming its textures are linear. A single
"colour management on/off" flag could not express that, and a test pins the three as independently
settable so a later simplification cannot quietly collapse them.

**There is deliberately no flag for the three linear maps.** §3.9.2 leaves no choice there. A flag
would invent one, and an invented choice is a thing someone eventually sets wrong.

**Factors are never transferred.** `baseColorFactor` and `emissiveFactor` are linear values in the
file; only the *texture samples* are encoded. The shader decodes the sample and then multiplies by
the factor — transferring both would apply the curve twice to one of them. This matters
concretely for emissive, where `KHR_materials_emissive_strength` (`GLTF-222`) legitimately pushes
the factor above 1; the transfer functions therefore do not clamp either.

**Alpha is never encoded.** §3.9.4 makes alpha a coverage value, not a colour. Both GLSL functions
take `vec3` rather than `vec4` so this cannot be got wrong by accident, and a test asserts the text
contains no `vec4`.

**One formula, two languages.** The transfer lives in
`modules/graphics/include/CNA/Internal/Graphics/SrgbTransfer.hpp` as a macro of string literals; the
GLSL is that macro, and the C++ functions beside it are the same arithmetic. Two hand-written copies
of a piecewise curve with a `0.0031308` knee and a `1/2.4` exponent would drift, and the drift would
be a subtle brightness error rather than a crash.

**Compatibility.** Additive on every renderer: the fields are new, so a renderer that does not read
them behaves exactly as it did before — the established accepted-and-ignored pattern. Adoption is
therefore per renderer rather than a flag day. `EasyGLRenderer` implements it (and with it the five
GL profiles it backs); the other renderers are unchanged and still show the old behaviour.

**A boundary this review names rather than hides.** The output encode is applied *by the PBR
shader*, so in a scene mixing `PbrEffect` with the stock XNA effects the two write differently
encoded colour into the same render target. That is real, and it is the price of B over A. It is
also not a regression — the stock effects behave exactly as they always have, and XNA content never
had colour management to lose. The proper fix is a colour-managed render target, which is option A's
other half and belongs with the `SurfaceFormat` work, not here.

**What could not be verified here, stated plainly.** `EasyGLRenderer.cpp` **was not compiled** while
making this change. The EasyGL family builds against sibling checkouts (`../easy-gl`, itself needing
`../meta-gl`) that this working tree does not have, so a renderer-configured build cannot be
configured at all and the STUB build the test suite uses does not compile that file. What *was*
verified: the header compiles, the C++ transfer is tested against the specification, and the one
genuinely non-obvious construct — splicing a macro of string literals into a shader-source
concatenation — is reproduced by `SrgbTransferTest.TheMacroSplicesIntoShaderSourceConcatenation`.
That test exists because the first version of the header used a `const char*` variable, which
**cannot** join adjacent literals and would not have compiled. The remaining risk is ordinary
compile error in a file whose changes are string literals, one `int` member and one `set_uniform`
call whose 3-float overload the same file already uses; a session with the sibling checkouts should
build `OPENGLES3` once to close it.

**Test.** `SrgbTransferTests` holds the C++ transfer to the specification's own values — endpoints,
mid-grey, both knees evaluated branch-against-branch, a 256-level round trip, and the no-clamp
property — and checks the GLSL declares both functions with the same constants. `GltfConformanceL6`
asserts every PBR draw in the corpus declares both maps sRGB and the encode on, and that the three
flags move independently. What cannot be tested here is the shader executing: `GLTF-009` is blocked
on a 3D-capable renderer (§5.3 of `docs/gltf-conformance.md`), so the pixel-level proof waits.

---

### 1.8 Per-part sampler state — `GLTF-202`, `GLTF-203`, `GLTF-207`

**Problem.** `cgltf_sampler`, `mag_filter` and `wrap_s` had **zero occurrences** in CNA. Every
imported texture was drawn with whatever `SamplerState` the device happened to have, which defaults
to `LinearWrap`. For an asset authored `CLAMP_TO_EDGE` with UVs outside `[0,1]` — which
`KHR_texture_transform` routinely produces — that is a large, obvious error, not a subtle one.

**Shape.**

```cpp
// Microsoft::Xna::Framework::Graphics::ModelMeshPart
CNAEXT const std::array<SamplerState, 5>& getSamplerStatesEXTProperty() const;
CNAEXT void setSamplerStateEXTProperty(int slot, const SamplerState& value);
```

**A property, not a `Tag` payload — overruling `GLTF-207`'s own sketch.** The task proposed attaching
a `SamplerStateArrayEXT` to `ModelMeshPart::Tag`, mirroring `MorphTargetDataEXT`. That cannot work:
`Tag` already *is* `MorphTargetDataEXT` for a morphing part, so a morphing part with clamped
textures could not express both. This is the same one-object-per-`Tag` collision §1.5 records for
rigid clips, and the right answer here is the one `GLTF-073` already established for
`PrimitiveTypeEXT` — a plain CNAEXT property on the part.

**Per slot, not per material.** glTF attaches a sampler to a *texture*, so a material may
legitimately clamp its base colour and repeat its normal map. One shared value could not say that.
The slots are named by a `TextureSlotEXT` enum rather than bare indices, because an array indexed
by an untyped `int` is exactly what acquires an off-by-one when a slot is added.

**A finding that corrected the plan.** §14.2 states that "XNA's `SamplerState` cannot express
independent min/mag filters for the four mixed combinations" and asks `GLTF-204` to document the
approximation. That is **wrong**: XNA's nine `TextureFilter` values —
`MinLinearMagPointMipLinear` and its three siblings among them — cover all eight min×mag×mip
combinations exactly. There is no approximation to document there.

The one real approximation is elsewhere and is now recorded where it happens: glTF's `NEAREST` and
`LINEAR` minFilters mean *no mipmapping*, and XNA has no `TextureFilter` value for "base level only"
— that is a property of the texture's level count, not of the sampler. `SamplerOut` carries a
`minFilterHasNoMipStage` flag so the arbitrary choice (point, the least-blending mip mode) is
visible rather than implied, and it becomes observable the day `GLTF-206` starts generating levels.

**Compatibility.** Additive. Every entry is `LinearWrap` for a part built by any other content path,
which is exactly what those parts got before.

**Test.** `GltfSamplerMappingTests` covers §14.2 exhaustively — every min×mag combination against
the XNA value that means exactly it, every wrap value on both axes independently, both
non-mipmapped minFilters, and the undefined-sampler default. The whole table is testable directly
because `MapGltfSamplerEXT` takes raw glTF enum values rather than a `cgltf_sampler`, which no
realistic number of fixtures could match for coverage.

**What is not proved here.** There is no end-to-end fixture, because **no corpus asset carries a
texture at all** — the generator has no image support yet, and `GLTF-190`'s reference checkerboard
is what several texture tasks are waiting on. So the mapping and the wiring are tested, and "an
imported textured part ends up with the sampler its file declared" is not. `GLTF-203`'s other half,
`uv-out-of-range-*` at L7, is blocked on `GLTF-009` regardless.

---

### 1.9 A second UV channel — `GLTF-181`, and the shape of every "new stride" refusal

**Problem.** Each of a glTF material's five texture references independently selects its own
`TEXCOORD` set. CNA's PBR effects sample all five from **one** shared channel — the one the base
colour names — so a material whose maps disagree renders some of them from the wrong set. It has
been *detected* since CNB-97 (`MeshOut::pbrUv2Mismatch`) and, until now, reported only by the
offline tool: the runtime path was silently wrong on exactly the same file.

**Decision: the single-channel limit stands, and is reported on both paths.** What supporting a
second channel actually costs, since `GLTF-181` asks for it in those terms:

* **Vertex stride.** A second `vec2` takes the unskinned PBR layout from 48 to **56** and the
  skinned one from 68 to **76**. 56 is *already taken* — it is the skinned+coloured layout — so
  this is not an additive change but a collision in the exact dispatch space
  `CNA/Internal/Graphics/VertexDeclarationFidelity.hpp` exists to police. Resolving it means either
  renumbering an existing layout or abandoning stride-keyed dispatch.
* **Shader.** A second attribute, a second varying, and per-map channel selection — five maps × two
  sets — so either five uniforms or a bitfield, in both PBR programs.
* **Renderers.** An input layout and a shader variant on each, the same blast radius that decided
  §1.7 against colour-space option A.

Against that: **no corpus asset uses a second UV set**, and CNA already knows when one is present.
The honest trade is to keep the limit and make it loud, which is what §1.5, §1.6 and `GLTF-241`
each concluded in their own way — a limitation that names itself is a different thing from a bug.

**No API change.** The detection already exists; only the reporting moved.

### 1.10 An authored tangent basis with nowhere to live — `GLTF-086`

**Problem.** Only strides 48 and 68 carry a tangent, and those are exactly the PBR layouts. A file
that authored `TANGENT` on any other primitive had it dropped in silence.

**Decision: reported, because it cannot be carried.** Unlike the material properties `GLTF-219`
ungated — which `MeshOut` could hold even when no effect consumed them — there is literally nowhere
to put a tangent in a stride-32 vertex. `MeshOut::droppedTangentForStrideEXT` names it and both
loaders log it, matching `GLTF-241`'s treatment of the dropped normal.

Worth reporting rather than shrugging at: a file that went to the trouble of authoring tangents did
so for a reason, and the reason is usually a normal map the material also declares.

**No API change.** A new `MeshOut` field, which is `CNA::Internal` and not public surface.

### 1.11 `Model` imported-camera list — `GLTF-317` … `GLTF-322`, `GLTF-324`

**Problem.** `cgltf_camera` had zero occurrences in CNA: a file's cameras were dropped entirely, so
an asset framed by its author arrived with no framing and every viewer had to invent one. This entry
was **deferred** in §2.3 as "additive with no current consumer"; `GLTF-317` overtook that, and the
shape below is what the gate approved rather than what deferral left unspecified.

**Decision: approved, as `std::vector<ModelCameraEXT>` on `Model`.** The reviewed shape, and why
each part of it is the way it is:

* **One entry per camera-bearing *node*, not per `cameras[]` entry.** A camera only exists in the
  render if a node in the default scene instances it, and one camera may be instanced by several
  nodes — each a distinct placement. Walking the camera array would both import cameras nobody
  placed and collapse the multi-instance case to one.
* **`Projection` is built at import, not at use.** An application should not have to reimplement
  glTF's infinite-far-plane case to draw what the author framed.
* **`WorldTransform` is a snapshot and says so.** The view matrix is its inverse (`GLTF-321`). A
  camera node is an ordinary node and can be animated, so the *live* placement is the absolute
  transform of the bone `SceneNodeIndex` names — the doc comment carries that warning because a
  consumer reading the stored matrix every frame renders an animated camera as a stationary one.
* **`HasInfiniteFarPlane`, `HasAuthoredAspectRatio` — the two assumptions, made explicit**
  (`GLTF-319`, `GLTF-322`). Both record something the *file* did or did not say, not something the
  importer computed. `aspectRatio`'s absence means "use the viewport's", which an importer has no
  viewport to satisfy: one is assumed, and without the flag a consumer cannot tell an author who
  framed a square shot from one who left the decision to the runtime, and would either stretch the
  first or letterbox the second.
* **`FieldOfView`, `NearPlaneDistance`, `FarPlaneDistance` carried rather than recoverable.**
  Acting on `HasAuthoredAspectRatio == false` means rebuilding the projection at the real viewport
  aspect. Inverting a matrix to get back values the file stated outright is work no consumer should
  do, and is not possible at all for the infinite variant without first knowing it is the infinite
  variant.

**API change: additive, CNAEXT, no XNA type altered.** `Model` gains a getter and setter pair for a
new `CNAEXT` struct. Nothing in the XNA 4.0 surface changes, and a `Model` from any other source
simply has an empty list — which is also what a glTF file with no camera produces, deliberately, so
"no camera" and "a default camera" are never confused.

### 1.12 Whole-model bounding sphere — `GLTF-128`

**Problem.** `ModelMesh` exposes XNA's per-mesh `BoundingSphere`, but `Model` exposes no union of
them. A caller framing or culling the whole imported glTF scene therefore has to know the private
vertex-sidecar format, re-read every position and duplicate the node-hierarchy composition the
loader already performed. The viewer does exactly that today. It is both a layer violation and a
second transform implementation waiting to drift from `Model::Draw`.

**Why not internal.** Framing and whole-model culling are application decisions. An internal value
would leave the viewer on the same sidecar-dependent path, while putting glTF-specific bounds in
`Model::Tag` would collide with `SkinningData` and `ModelAnimationsEXT` and would not help ordinary
XNB or hand-built models whose meshes already carry XNA bounding spheres.

**Shape.** One computed property on `Model`, reusing XNA's existing type:

```cpp
// Microsoft::Xna::Framework::Graphics::Model
CNAEXT [[nodiscard]] std::optional<BoundingSphere>
getBoundingSphereEXTProperty() const;
```

The property transforms every `ModelMesh::BoundingSphere` by the same current absolute parent-bone
matrix `Model::Draw` uses, then merges the results with `BoundingSphere::CreateMerged`. It is
therefore **live for rigid/node animation**, not an import-time snapshot. A composed hierarchy can
contain shear (a rotated child below a non-uniformly scaled parent), where XNA's ordinary
`BoundingSphere::Transform` longest-basis rule is not conservative; the accessor bounds the
matrix's largest stretch by the maximum absolute row sum of `A*A^T`, which stays exact for an
orthogonal TRS basis and cannot exclude geometry under shear. The result is in model root space:
for a glTF model that is the file's composed scene space, after all node transforms, but before the
caller-provided `world` matrix passed to `Model::Draw` (which the model cannot know). The
no-`ParentBone` case follows `Model::Draw` and uses bone zero when there is one, otherwise the
identity. A model with no meshes returns `std::nullopt`; a zero-radius sphere is valid geometry and
is not overloaded as an empty sentinel.

**Why a sphere, not a new bounds struct or an AABB.** The source data already has one sphere per
`ModelMesh`, XNA already supplies exact two-sphere merging, and a centre plus radius is precisely
what a framing camera consumes. An AABB reconstructed from the spheres would be looser without
recovering the source vertices; reading vertex-buffer shadows inside `Model` would bypass the
existing mesh-bounds abstraction and would fail for write-only or GPU-generated data. No new type
is justified.

**Required importer repair.** Both glTF loaders currently leave every mesh sphere at its default
zero value. They must build one local-space sphere from the positions of **all** primitives grouped
into that `ModelMesh`; doing it per primitive would reintroduce the shape bug `GLTF-139` removed.
This is internal wiring, not another public member. The offline `.cnj` reader derives the same
sphere from its vertex sidecars, so old and new `.cnj` files need no format or version change.

**Boundary.** This property has exactly the semantics of the mesh spheres it aggregates. Changing a
bone transform is reflected on the next call. GPU skinning and a later CPU morph re-upload do not
magically rewrite `ModelMesh::BoundingSphere`; a caller that deforms geometry outside its imported
mesh sphere must update that existing read-write XNA property, just as it must for per-mesh culling.
The glTF loaders initialise the sphere from the imported default vertex pose, including authored
default morph weights, but cannot predict arbitrary future or overdriven animation.

**Compatibility.** Additive and CNAEXT. Existing draw behaviour and every existing mesh sphere are
unchanged. XNB and hand-built models participate automatically through the property they already
have; an empty model now has an explicit no-result rather than a fabricated origin sphere.

**Migration.** None. A caller doing private sidecar traversal can replace it with the one property
read. A caller that needs application world space transforms the returned sphere by its own `world`
matrix, the same matrix it passes to `Model::Draw`.

**Test.** Graphics unit tests use two hand-built mesh spheres on different bones to discriminate
bone composition, merge order and a live pose change, plus the empty-model result. A separate
sheared absolute transform proves the sphere remains conservative where the ordinary longest-basis
rule does not. A two-primitive glTF proves one mesh sphere covers both parts; a translated placement
proves the whole-model sphere contains every independent L4 world position. The offline tool's
`.cnj` result and the direct glTF result must have component-identical centre and radius.

### 1.13 `Model` material-variant selection — `GLTF-341`, `GLTF-342`

**Problem.** `KHR_materials_variants` describes a model-wide selection over sparse per-primitive
material mappings. Importing those mappings internally is insufficient: the application or viewer
is the party that decides which product colourway to show, and neither XNA's `ModelMeshPart::Tag`
nor `Model::Tag` is available as an undocumented escape hatch — both already carry importer data.

**Shape.** Three additive CNAEXT members on `Model`:

```cpp
CNAEXT [[nodiscard]] const std::vector<std::string>&
getMaterialVariantNamesEXTProperty() const;
CNAEXT [[nodiscard]] int getMaterialVariantEXTProperty() const;
CNAEXT void setMaterialVariantEXTProperty(int value);
```

The name vector preserves the glTF root array's source order. Selection is by that array index,
not by display name: the extension defines identity by index and does not require names to be
unique. `-1` is the core/default material mapping and is always the initial value; an index below
`-1` or outside the name vector throws `std::out_of_range` before changing anything. Models from
other content paths expose an empty vector, report `-1`, and accept the no-op selection `-1`.

**Why the operation belongs on `Model`.** One variant selection applies across every primitive,
and a primitive absent from the selected variant's sparse mapping must return to its own default.
A per-part setter would force every caller to reconstruct that global sparse rule and would expose
the importer's private pairing of default and alternative buffers. A string setter would invent
uniqueness the source format does not promise.

**State and compatibility.** Selection swaps the complete material-dependent part state: effect,
compatible vertex buffer and count, morph carrier, textures and all sampler slots. Indices,
topology, placement and bounds are material-independent and remain unchanged. The implementation
uses the existing `ModelMeshPart::setEffectProperty` last so `ModelMesh::Effects` stays coherent.
`Model` copies share selection because they already share their XNA mesh parts; independent loads
do not. The surface is additive, CNAEXT, and leaves freshly loaded and non-glTF models unchanged.

**Offline contract and tests.** `.cnj` stores the source-order names once and each mapped
alternative as a complete mesh-state record linked to its default. The reader captures those
records as alternatives rather than exposing extra mesh parts. The synthetic witness changes PBR
stride 48 to unlit stride 32 and includes an unmapped third variant after it, proving both the full
state swap and stale-state reset. Direct STUB/HEADLESS tests cover defaults, invalid indices and
copy semantics; a real tool subprocess then compares every direct and offline selection at L6.

---

## 2. Reviewed and deferred

### 2.1 `PbrEffect::NormalScale`, `OcclusionStrength` — `GLTF-224`, `GLTF-225`

**Shape approved** as plain additive CNAEXT float properties on an existing CNAEXT effect, defaults
`1.0` — the glTF defaults, so an unconfigured effect is unchanged.

**Deferred, and why.** Both scale a *texture*, and no corpus fixture carries a texture at all. The
L5 golden packer likewise refuses a textured material rather than emitting a golden nobody has
checked. Implementing them now would add two properties whose only test could be "the setter sets
it", which is not evidence that the value reaches a shader. They land with the fixtures that need
them.

### 2.2 Per-part sampler state — `GLTF-207`

Deferred untouched. A CNAEXT `SamplerStateArrayEXT` on the part is a larger surface than anything
else in this file and has no consumer until texture work begins.

### 2.3 Import diagnostics — `GLTF-034`

Deferred. An additive CNAEXT container with no current consumer. Worth noting as *partially
pre-empted*: `GLTF-024`'s ignored-extension reporting already emits warnings through `CNA::Logger`
on the runtime path and the tool's own warning list offline, so the eventual `GltfImportReportEXT`
should collect what already exists rather than introduce a second channel. Every report added since
— `NodeGraphReportEXT`, `SkinReportEXT`, `MorphReportEXT`, `LightReportEXT`, `AnimationReportEXT` —
is deliberately **internal** for this reason, and is the material that container would gather.

The imported-**camera** list was deferred here alongside it and has since been approved and landed;
see §1.11.

### 2.4 `SkinnedEffect::MaxBones` above 72 — `GLTF-261`

**Not approved, and not a candidate for approval here.** Raising it changes a real XNA 4.0 constant
and every renderer's uniform array — the one **breaking** row in §25. It stays `investigate only`.

---

## 3. What this gate rejected

Nothing in §25's table was rejected outright; the gate's effect was on **shape** rather than
admission:

* the topology property reuses the existing `PrimitiveType` instead of introducing a glTF-flavoured
  one, so a part's topology is expressed in the same vocabulary a draw call already takes;
* the alpha-mode enum is declared **once**, in the graphics module, instead of separately on
  `PbrEffect` and `PbrMaterial`;
* `doubleSided` is admitted as carried state only, with applying it explicitly out of scope and
  assigned to the task that owns the render state — which is the difference between a flag that
  works and a flag that merely exists.

---

## 4. Standing rules for the next row

1. A member that cannot be tested beyond its own setter is not ready — find the fixture first.
2. Prefer an existing type to a new one, and a shared type to two parallel ones.
3. Carrying state and acting on it are separate approvals. Say which one is being granted.
4. Every member approved here must reach `CNAEXT.md` in the Phase 23 documentation tasks
   (`GLTF-448`, `GLTF-456`).
