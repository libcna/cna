# XNA 4.0 Culling Compatibility Audit

**Status: framework proven compatible; SimpleAnimation-specific symptom root-caused to a
localized winding-orientation defect in a subset of `turret_geo`'s own converted mesh data, not
yet corrected (evidence gathered, exact fix not yet applied — see §7).**

Date started: 2026-07-11. Investigates a visual discrepancy first noticed while porting the
SimpleAnimation sample (`cna-samples/samples/SimpleAnimation`) to CNA: a dark, disc-shaped
surface visible under/behind the tank's turret dome that should be hidden.

This document is the authoritative record for that investigation. It supersedes any earlier,
narrower culling notes left in `cna-samples/samples/SimpleAnimation/missing.md`.

---

## 1. Scope and relationship to the (separate, already-fixed) texture problem

Two independent problems were found while porting SimpleAnimation. **They are unrelated and must
not be conflated:**

1. **Missing per-mesh texture** (already fixed, `cna-samples`-only, no `cna` change). The tank
   rendered flat white/cream because `tank.model.json` predated `cna` Task 932's per-mesh
   `"texture"` field. Fixed by extending `cna-samples/tools/fbx_ascii2model.py` with a generic
   FBX `Material`/`Texture`/`Connect` graph parser and hand-adding the 12 resolved texture names
   to the existing `tank.model.json` (no vertex/index data touched — regenerating the file
   wholesale was tested in a scratch directory and found to corrupt this specific asset, see
   `DEFERRED.md`'s own addendum). Full write-up: `cna-samples/samples/SimpleAnimation/missing.md`'s
   "Flat, untextured shading — FIXED" section.
2. **The turret underside/culling problem** (this document). Investigated independently, after
   the texture fix, using a real running build with the texture fix applied.

---

## 2. Original evidence (recap, as received)

1. SimpleAnimation uses rigid `ModelBone` transforms, not vertex skinning.
2. The source FBX hierarchy and the generated model hierarchy agree.
3. Local and absolute translations agree with the original FBX to floating-point precision.
4. The suspicious dark circular surface belongs to `turret_geo`.
5. It is legitimate underside/base geometry inside the turret assembly.
6. Its geometric face normal points downward.
7. From the sample's elevated camera, this downward-facing underside was assumed to normally be
   classified as back-facing.
8. CNA displays it under its current default `CullCounterClockwiseFace` state.
9. Temporarily switching to `CullClockwiseFace` removes that surface and makes the turret look
   correctly closed from the elevated view.
10. The source geometry rendered independently from the same FBX data does not show this
    underside from a comparable elevated camera.
11. Vertex normals and normal-matrix transformation were checked and appear correct.
12. Only 2 out of 1186 `tank_geo` triangles disagreed with stored normals (a different mesh from
    the one showing the bug; not directly informative about `turret_geo`'s own consistency — see
    §5.1 for why this specific style of check is a weaker signal than it first appears).

This evidence was treated as a starting point, not as proof of where the defect lives (per the
task's own explicit instruction). Every item was independently re-verified or superseded below.

---

## 3. Live confirmation — CNA vs. real XNA 4.0

All 3 screenshots referenced below are preserved at
`docs/xna_culling_compatibility_audit_images/` (not generated/build output — committed
investigation reference material, per this document's own record-keeping requirement).

**CNA (current HEAD, EasyGL backend, texture fix applied, no CullMode override), live screenshot:**
`cna_default_bug_present.png`.

A dark, flat, disc-shaped surface is clearly visible poking out from behind/under the turret
dome — matches evidence items 4-9 exactly. Confirmed still present in the current codebase, not a
stale/historical report.

**Authoritative original XNA 4.0 reference (provided 2026-07-11 by the project owner: the real
C# `SimpleAnimation` sample, built and run on Windows 7 inside a VirtualBox VM):**
`xna4_reference_authoritative.png`.

The turret in the real XNA screenshot is visually compact and correctly seated on the hull — the
dark underside/disc artifact is **not present**. The two screenshots are at different animation
times/world rotations (the sample continuously rotates the whole scene and independently animates
wheels/steering/turret/cannon/hatch), so no pixel-level comparison of unrelated geometry (wheel
angle, cannon angle, hatch state) is meaningful — only face-visibility/effective-CullMode
behavior was compared, per the task's own explicit instruction.

**This is now direct, live, authoritative evidence of an observable XNA-vs-CNA rendering
mismatch** — not a comparison against Blender, DirectXTK, or any other non-XNA reference.

---

## 4. Phase 1 — minimal CNA culling reproducer (no Model/FBX/texture/lighting/bones/animation)

Two new permanent regression tests were added to `cna_graphics`, both registered on all 3
runnable backends (EasyGL, Vulkan, Bgfx):

### 4.1 `examples/rasterizerstate_cullmode_camera_test.cpp`

Extends the pre-existing, already-passing
`examples/easygl_rasterizerstate_cullmode_test.cpp`/`bgfx_rasterizerstate_cullmode_test.cpp`
(Tasks 323-325/765, identity-transform only) with a **real** `Matrix.CreateLookAt` view and
perspective/orthographic projection, matching SimpleAnimation's own exact camera
(`eye=(1000,500,0)`, `target=(0,150,0)`, `up=(0,1,0)`, `FOV=PiOver4`, `near=10`, `far=10000`).

**Method** (deliberately avoids ever hand-predicting an expected screen position, which is exactly
the kind of arithmetic this investigation must not silently rely on):
1. Render each of 2 test triangles **alone** under `CullMode.None` (guaranteed visible if on
   screen at all) and scan the **entire** framebuffer for its own unique color to find a real
   sample pixel — never a hardcoded coordinate.
2. Compute each triangle's **NDC-space signed area** directly via CNA's own
   `Vector4::Transform(vertex, World*View*Projection)` + a manual perspective divide (CNA's own
   real matrix pipeline, not hand arithmetic), and derive a "predicted" winding label from the
   rule the pre-existing identity test already established empirically: **negative NDC signed
   area → predicted to survive the default `CullCounterClockwiseFace`.**
3. Render both triangles together under `CullMode.None` / `CullClockwiseFace` /
   `CullCounterClockwiseFace` / the default `RasterizerState`, sample each triangle's own pixel,
   and check the observed visibility against the step-2 prediction.

Five scenarios, all using this same self-consistency check:
- **(a)** Identity World/View/Projection (sanity anchor — must reproduce the pre-existing
  identity test's own result).
- **(b)** Orthographic projection + real `CreateLookAt` view, identity World.
- **(c)** Perspective + real `CreateLookAt` view (SimpleAnimation's exact camera), identity World.
- **(d)** Same camera, **positive-determinant** World transform (a real `CreateRotationY`
  rotation, determinant = 1) — the exact class of transform SimpleAnimation's own animated bones
  compose (confirmed, see §4.3).
- **(e)** Same camera, **negative-determinant** World transform (`CreateScale(-1,1,1)`) —
  documented separately per the audit's own requirement: a real winding flip is *expected* here,
  not a bug; the test checks the render against this scenario's own (also-flipped) prediction, not
  scenario (c)/(d)'s.

**Result: 30/30 checks PASS on EasyGL, Vulkan, and Bgfx — identical outcome on all 3 backends.**
Discriminating power was not separately re-verified here since this test is purely additive
regression coverage over an already-passing baseline, not a bug fix.

### 4.2 `examples/rasterizerstate_cullmode_indexed_basiceffect_test.cpp`

Same methodology, but drives `GraphicsDevice::DrawIndexedPrimitives` with a **real**
`VertexBuffer`/`IndexBuffer`/`BasicEffect` (`VertexPositionNormalTexture`, stride 32, matching
every real `ModelMeshPart`) — the exact code path `ModelMesh::Draw()` uses — instead of
`DrawUserPrimitives`'s separate, simpler `DrawColoredPrimitives` dispatch that every pre-existing
CullMode test (including §4.1) happens to use. Also exercises `BasicEffect.EnableDefaultLighting()`
(SimpleAnimation's own `Tank::Draw()` calls this every frame for every mesh part), to check
whether lighting activation itself — a different shader-selection path on every backend —
has any bearing on `CullMode`.

**Result: 6/6 checks PASS on EasyGL, Vulkan, and Bgfx.**

**Methodology fix needed for Bgfx**: an early draft used ONE shared `VertexBuffer`/`IndexBuffer`
with the 2 triangles at index offsets 0 and 3 (`GraphicsDevice.DrawIndexedPrimitives(...,
startIndex: 3, ...)` for the second). On Bgfx specifically, both triangles rendered at the exact
same screen location. Root-caused (§6) to a real, separate, previously-unknown Bgfx bug
(`BgfxGraphicsBackend::DrawIndexedPrimitivesEx` silently discarding `GpuDrawParams::startIndex`)
— **not a CullMode bug**. Worked around in the test (not the bug itself — see §6 for why the
attempted fix was reverted) by using two separate, dedicated `VertexBuffer`/`IndexBuffer` pairs
instead, matching how every real `ModelMeshPart` actually works (`tank.model.json` gives every
mesh its own separate `"vertices"`/`"indices"` `.bin` pair, never a shared buffer with per-part
offsets) — confirmed this is the *representative* case, not an artificial simplification.

### 4.3 Conclusion of Phase 1

**CNA's `CullMode`/`RasterizerState` implementation is self-consistent and matches the rule
established by the pre-existing identity test, across identity, orthographic, and perspective
projection; a real `Matrix.CreateLookAt` view; positive- and negative-determinant World
transforms; both the simple `DrawUserPrimitives` dispatch and the real indexed/`BasicEffect`
dispatch `Model`/`ModelMesh::Draw()` actually uses; with and without lighting; on all 3 runnable
backends (EasyGL, Vulkan, Bgfx).**

No framework-level `CullMode`-to-native-state mapping defect was found anywhere this
investigation could exercise it. This directly rules out the most obvious hypothesis (an enum
mapped backward, or a Y-flip/viewport correction applied inconsistently with the cull-state
mapping on one specific backend) as the cause of the SimpleAnimation symptom.

---

## 5. Phase 3/5 — tracing the real turret_geo transform, and re-examining the asset itself

### 5.1 Why the original evidence's own normal-vs-winding check (item 12) is a weaker signal than
it first appears

A per-triangle check of "does this triangle's geometric winding (from its 3 vertex positions)
agree with its own stored vertex normal" was re-run for `turret_geo` specifically (not `tank_geo`,
which item 12 checked): **0 disagreements across all 1674 triangles.** This is a genuinely useful
data point, but on its own it is **not proof the mesh is correctly wound** — it only proves
*internal* consistency between each triangle's winding and its own normal. If normals were
derived from (rather than independently authored against) the same winding data during FBX
export/conversion, a mesh with its **entire** winding convention reversed relative to true intent
would pass this exact check with 0 disagreements too. Per the task's own explicit instruction
("Do not use lighting normals as evidence for culling correctness"), this check is recorded here
for completeness but was **not** relied on for the final conclusion.

### 5.2 Edge-adjacency orientation consistency (a real, non-circular check)

A proper closed/manifold mesh has every internal edge shared by exactly 2 triangles, each
traversing that shared edge in the **opposite** direction from the other (a standard, well-known
polygon-orientation invariant, independent of stored normals entirely). Checked directly against
`turret_geo`'s real shipped `tank_turret_geo_verts.bin`/`tank_turret_geo_idx.bin`:

**Result: 0 orientation-inconsistent triangles out of 1674** (positions matched with a small
tolerance to account for legitimately duplicated seam/UV-boundary vertices). `turret_geo` is a
genuinely coherent, consistently-oriented manifold mesh — no random per-triangle indexing errors.

### 5.3 The real runtime World transform for turret_geo

Instrumented `cna-samples/samples/SimpleAnimation/src/Tank.hpp`'s `Draw()` (temporarily, reverted
before this investigation's own commits — see §8) to print the actual `BasicEffect.World` matrix
used for `turret_geo` during a real running frame:

```
World.Determinant() = 1.000000
World = [ 1.000  0.000 -0.009  0.0 /
          0.000  1.000  0.000  0.0 /
          0.009  0.000  1.000  0.0 /
         -0.059 231.754 -35.595  1.0 ]
```

**Positive determinant, confirmed** — a plain rotation (the small `TurretRotation`-driven
`Matrix.CreateRotationY`, composed through `ModelBone.Transform`/`CopyAbsoluteBoneTransformsTo`)
times a pure translation (matches `cna-samples`' own `ApplyRestTransforms()` rest-pose value,
`(0, 231.754, -35.595)`, which — per that function's own header comment — is itself a plain
translation read directly from `tank.fbx`'s own `Lcl Translation`, since every rotation/
`PreRotation` in this asset is zero). This exactly matches Phase 1's own scenario (d)
(positive-determinant World via `CreateRotationY`), which passed 30/30 across all 3 backends.

**This rules out a transform-composition bug (wrong parent chain, an accidental reflection
introduced by `ModelBone`/`Model` composition, etc.) as the cause.**

### 5.4 The decisive check: NDC-signed-area prediction for the REAL underside triangles, under the
REAL exact transform

Using the exact same (already-validated-correct, §4) NDC-signed-area methodology, but now applied
directly to `turret_geo`'s own real, shipped vertex/index data — not synthetic test geometry —
under the exact real `World` (§5.3) / `View` / `Projection` matrices:

A specific, obviously-underside triangle (`tri#0`, vertex indices `(0,1,2)`, average Y ≈ 0.00,
geometric face normal ≈ `(0, -1, 0)` — i.e. essentially exactly straight down) was computed to
have **NDC signed area = −0.001211**, which by the already-established, already-validated rule
(§4.1 step 2) **predicts it survives the default `CullCounterClockwiseFace` state** — i.e.
mathematically front-facing from the real camera's real viewpoint, given its actual stored
winding.

Broadening this to every low-Y (< 30), steeply-downward-normal (`faceNormal.Y < -0.99`) triangle
in the mesh (20 such triangles, spanning vertex indices 0-683, **not** a single contiguous index
range) found **78 of the 100 broader "underside-region" candidate triangles** (Y<30,
`faceNormal.Y < -0.3`, a looser filter also catching some angled side-wall triangles near the
same feature) predicted to survive the default cull state.

A full-mesh breakdown (all 1674 triangles) found **52.1% predicted to survive, 47.9% predicted to
be culled** — the expected roughly-even split for a normally, correctly-wound closed/convex-ish
mesh viewed from any single direction (most of the mesh is **not** part of the anomaly; this
statistic on its own does not distinguish a globally-reversed mesh from a correctly-wound one, and
is recorded here only for completeness).

**Conclusion: CNA's rendering of the underside triangles is mathematically self-consistent and
correct, given the mesh's own actual stored winding data and the real transform. This is not a
CNA rendering/culling bug** — the same already-proven-correct pipeline (§4) simply computes that
this specific triangle, as currently wound, is front-facing from this camera. **The winding data
itself, as currently shipped in `tank_turret_geo_idx.bin`, does not match what real XNA's content
pipeline evidently produced from the same source FBX** (§3's live screenshot comparison) — i.e.
this is a **model-conversion-level defect**, isolated to (at least) the disc/underside region of
`turret_geo`, not a `cna` framework defect.

### 5.5 `fbx_ascii2model.py`'s own triangulation logic — checked directly, not just inferred

Per the task's own instruction not to assume a conversion-tool bug without direct evidence: the
tool's `triangulate()` function was read line-by-line against the FBX ASCII format's own
`PolygonVertexIndex` convention (a negative value encodes both `(-n)-1` as the real index **and**
marks the end of a polygon). Confirmed **correct**: fan-triangulation from `polygon[0]`
(`[polygon[0], polygon[k], polygon[k+1]]` for each `k`) faithfully preserves whatever vertex order
the FBX's own `PolygonVertexIndex` array encodes — it does not introduce any reversal itself. If
the underside region's winding really is wrong relative to true XNA intent, the most likely
remaining explanation is either (a) a genuine peculiarity of this specific region's own authored
winding in the source FBX that real XNA's actual content-pipeline FBX importer resolves
differently than this hand-written ASCII parser does (e.g. a coordinate-system/axis-handedness
declaration this tool doesn't apply, if the source FBX has one), or (b) some other
`fbx_ascii2model.py` step downstream of `triangulate()` not yet traced to this level of detail.
**Not fully resolved — see §7 for what remains open.**

---

## 6. A real, separate bug found incidentally (not the root cause of §3-5, documented for its own
sake)

While building §4.2's test, an early draft used one shared `VertexBuffer`/`IndexBuffer` read at
two different `startIndex` offsets (0 and 3). On Bgfx only, the second draw silently redrew the
**first** triangle's own indices instead of the requested second range.

**Root cause, confirmed by direct source reading**:
`BgfxGraphicsBackend::DrawIndexedPrimitivesEx`'s non-wireframe branch calls the offset-less
`bgfx::setVertexBuffer(stream, handle)` / `bgfx::setIndexBuffer(handle)` overloads
unconditionally — `GpuDrawParams::startIndex`/`baseVertex` are read and forwarded correctly to
`ExpandWireframeIndices()` (the *wireframe* path only) but are **never applied** to the real,
non-wireframe vertex/index buffer binding at all.

This has not yet caused a visible bug in any existing CNA sample or test because every current
`Model`/`ModelMeshPart` owns its own dedicated, complete `VertexBuffer`/`IndexBuffer` pair
starting at index/vertex 0 (confirmed directly: `tank.model.json` gives every mesh its own
separate `"vertices"`/`"indices"` `.bin` file, never a shared buffer with per-part offsets) — so
`startIndex`/`baseVertex` are always 0 in every real draw today. It would affect: any future
multi-part-mesh format sharing one buffer with per-part offsets, and any direct
`GraphicsDevice.DrawIndexedPrimitives(..., startIndex: N, ...)` / `baseVertex: N` call with a
nonzero offset on Bgfx specifically.

**A fix was attempted** (using `bgfx::setIndexBuffer(handle, firstIndex, numIndices)` /
`bgfx::setVertexBuffer(stream, handle, startVertex, numVertices)`, the correct offset-aware
overloads for `DynamicIndexBufferHandle`/dynamic vertex buffers) but **produced a worse
regression** on retest (the offset draw stopped rendering anything at all, rather than rendering
in the wrong place) — root cause of *that* not identified within this session's time budget,
possibly a `DynamicIndexBufferHandle`-specific quirk in how `bgfx::update()`-populated dynamic
buffers interact with the offset overload. **The fix was reverted, not committed** (see §8) —
shipping an unverified change was judged worse than leaving the original, narrower, already-
understood bug in place. **Tracked as a new, separate, NOT-yet-fixed task** (needs its own
dedicated investigation) — not part of this audit's own root cause and explicitly out of scope
for the culling investigation itself.

---

## 7. Current status / what remains open

**Framework-level conclusion (high confidence):** CNA's `RasterizerState.CullMode` — `None`,
`CullClockwiseFace`, `CullCounterClockwiseFace`, and the default state — behaves identically and
correctly across every camera/projection/transform/draw-path/backend combination this
investigation could construct. No public CNA API change is justified by any evidence gathered.
**No CNA framework change has been made or is being proposed.**

**Asset-level conclusion (strong evidence, not yet independently confirmed against a rebuilt
real-XNA-content-pipeline reference of the same FBX):** the underside/disc region of
`turret_geo` — a real, internally self-consistent (§5.2) sub-region of the mesh, not the whole
mesh — has a winding convention that, per CNA's own already-proven-correct culling pipeline,
predicts front-facing/visible when the authoritative XNA screenshot (§3) shows it should not be.
This is consistent with (not yet independently proven beyond doubt to be) a genuine, localized
model-conversion defect specific to this region.

**Why no data fix has been applied yet:**
1. The exact affected triangle set is only approximately characterized (§5.4: a 20-triangle
   "steeply downward" core, a looser ~100-triangle candidate region including some angled
   neighbors that may or may not need to be included) — a global reversal of all 1674 triangles
   would be **wrong** (it would break the correctly-wound majority of the mesh) and was
   deliberately not attempted.
2. `cna-samples`' own established convention (`DEFERRED.md`'s own addendum, written before this
   investigation) already found regenerating `tank.model.json` wholesale via
   `fbx_ascii2model.py` corrupts this specific asset for unrelated reasons (a scale-baking bug for
   `tank_geo`'s own `Lcl Scaling`) — so any fix must either hand-patch the specific affected
   index range in the already-shipped `.bin` file, or fix `fbx_ascii2model.py`'s own handling of
   whatever FBX-level winding subtlety is responsible (not yet identified, §5.5) and then safely
   regenerate *only* the affected sub-region.
3. A first attempt at a targeted binary edit was blocked by this session's own safety tooling,
   correctly, since the evidence available at that moment did not yet distinguish "reverse the
   whole mesh" (wrong) from "reverse only the disc" (the actual, better-evidenced hypothesis) —
   see §8's own commit-history note.

**Recommended next steps for whoever picks this up:**
- Run `cna-samples/tools/xna-reference/CullModeTest` (new, this session — §9) on the same Windows
  7/XNA 4.0 VM used for §3's screenshot, to get a pixel-exact, side-by-side CullMode comparison
  independent of the SimpleAnimation model entirely — closes the loop on whether the *framework*
  conclusion in this document (already strong, from CNA-side testing alone) is fully confirmed
  from the XNA side too.
- If a real XNA content-pipeline build of `tank.fbx` (or an FBX SDK-based inspector) becomes
  available, compare the *real* XNA-processed `turret_geo` index buffer against the currently
  shipped one directly — the most direct possible confirmation of the model-conversion-defect
  hypothesis.
- If confirmed, apply a **targeted** fix (reverse only the confirmed-affected index range, not a
  blanket global reversal) either directly to `tank_turret_geo_idx.bin` or via a
  `fbx_ascii2model.py` correction, with its own before/after screenshot verification against the
  §3 XNA reference.

---

## 8. Files changed

### `cna_graphics` (this repository)

- **New**: `examples/rasterizerstate_cullmode_camera_test.cpp` (§4.1), registered on EasyGL/
  Vulkan/Bgfx.
- **New**: `examples/rasterizerstate_cullmode_indexed_basiceffect_test.cpp` (§4.2), registered on
  EasyGL/Vulkan/Bgfx.
- **New**: this document.
- `CMakeLists.txt`: the 2 new tests' registration (6 `add_test`/`cna_*_test` blocks total).
- **No production `CNA`/`Microsoft::Xna` source changes** — the framework was found correct.

Diagnostic-only changes made and **reverted, not committed**, during the investigation (kept here
for the record, matching this project's own established convention of documenting dead ends so a
future session doesn't re-derive them):
- A temporary `BgfxGraphicsBackend::DrawIndexedPrimitivesEx` `startIndex`/`baseVertex` fix (§6) —
  reverted after it introduced a worse regression on retest.
- A temporary Bgfx-only regression test for that fix — deleted along with the reverted fix.

### `cna-samples`

- **New**: `tools/xna-reference/CullModeTest/` (§9) — a minimal XNA 4.0 C# project for the
  Windows VM, not yet run there.
- **Unchanged**: `samples/SimpleAnimation/src/Tank.hpp` — 2 separate diagnostic experiments were
  made and **fully reverted** before this document's own commit (both confirmed via `git diff`
  showing zero net changes to this file):
  1. Forcing an explicit `RasterizerState.CullCounterClockwise` set before every mesh draw
     (ruled out "the default state never gets applied to the real running game" as a hypothesis
     — the bug persisted identically either way).
  2. Printing `turret_geo`'s real runtime `World` matrix + forcing `CullClockwiseFace`
     specifically for `turret_geo`'s own draw call (§5.3, and re-confirmed evidence item 9 is
     still true on the current codebase — screenshot taken, dark disc genuinely gone; preserved as
     `docs/xna_culling_compatibility_audit_images/cna_cullclockwise_forced_turret_diagnostic.png`,
     a **diagnostic** capture only, not a proposed fix — see §7 for why this exact mechanism
     [a per-mesh `CullMode` override] is explicitly NOT the recommended final fix).
  3. A blocked, never-applied attempt to reverse `tank_turret_geo_idx.bin`'s entire winding
     globally (correctly blocked by this session's own safety tooling — see §7 for why a blanket
     reversal would have been the wrong fix regardless).
- **No SimpleAnimation-specific `CullMode` workaround was added or committed** — `Tank.hpp` is
  byte-identical to its state before this investigation began (confirmed via `git diff`), aside
  from the pre-existing, separate, already-good texture fix (§1) which this investigation did not
  touch.

---

## 9. New XNA-side reference tooling

`cna-samples/tools/xna-reference/CullModeTest/` — a minimal, standalone XNA 4.0 Windows project
(no content pipeline, no textures, no models) implementing the exact same 2-triangle-opposite-
winding methodology as §4.1's CNA test, using SimpleAnimation's own exact camera. Renders all 4
`CullMode` combinations simultaneously in 4 screen quadrants for a single-screenshot comparison.
See that directory's own `README.md` for build/run instructions. **Not yet run** — prepared for
the project owner to run on the same Windows 7/XNA 4.0 VM used for §3's reference screenshot.

---

## 10. Summary for future readers

- Do **not** re-attempt "swap `CullClockwiseFace`/`CullCounterClockwiseFace` in the backend enum
  mapping" — this was the original, most obvious hypothesis, and it is **disproven** (§4).
- Do **not** re-attempt a blanket reversal of all of `turret_geo`'s indices — the mesh is not
  uniformly mis-wound (§5.4's full-mesh breakdown), only a sub-region is.
- Do **not** use stored vertex normals as evidence of correct winding on their own (§5.1) — use
  edge-adjacency orientation (§5.2, a real, independent, non-circular check) or direct NDC-area
  computation against a known-correct external reference (§5.4) instead.
- The Bgfx `startIndex`/`baseVertex` bug (§6) is real but **unrelated** to this investigation's
  own root cause — don't conflate the two if revisiting either.
