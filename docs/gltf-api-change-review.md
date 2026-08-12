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

**Compatibility.** Additive. New enum, new properties, defaults preserve current behaviour.

**Migration.** None.

**Test.** `GLTF-228`/`GLTF-229`: `mat-factor-only-gold` carries `Blend`; a mask fixture carries
`Mask` with its authored cutoff; both survive the `.cnj` round-trip; the defaults are asserted on a
material that declares neither.

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

### 2.3 `Model` imported-camera list — `GLTF-320`; import diagnostics — `GLTF-034`

Deferred. Both are additive CNAEXT containers with no current consumer. `GLTF-034` is worth noting
as *partially pre-empted*: `GLTF-024`'s ignored-extension reporting already emits warnings through
`CNA::Logger` on the runtime path and the tool's own warning list offline, so the eventual
`GltfImportReportEXT` should collect what already exists rather than introduce a second channel.

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
