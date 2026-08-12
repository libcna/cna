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
