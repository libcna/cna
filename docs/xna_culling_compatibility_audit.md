# XNA 4.0 Culling Compatibility Audit

**Status: RESOLVED. CNA's `RasterizerState.CullMode` framework is confirmed correct — verified
directly against real XNA 4.0 (not just FNA source reading) via a dedicated C# reproducer run on
a real Windows 7/XNA 4.0 VM (§6). The SimpleAnimation symptom was a systematic winding reversal
across the ENTIRE converted `tank.fbx` mesh data (all 12 mesh parts, not just `turret_geo`'s own
disc region as first suspected — §5 walks through why that first, narrower conclusion was wrong).
Fixed by reversing triangle winding in all 12 `tank_*_idx.bin` files under
`cna-samples/samples/SimpleAnimation/Content/`. Verified visually against the authoritative XNA
screenshot across multiple rotation angles — see §7.**

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
   to the existing `tank.model.json` (no vertex/index data touched at the time — that came later,
   §7). Full write-up: `cna-samples/samples/SimpleAnimation/missing.md`'s "Flat, untextured
   shading — FIXED" section.
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
**Item 9's own framing ("switching CullMode fixes the turret") turned out to be the single most
important clue — it just needed to be tested at the scale of the whole tank, not just one mesh
(§5.5), before its real implication was clear.**

---

## 3. Live confirmation — CNA vs. real XNA 4.0

All screenshots referenced below are preserved at `docs/xna_culling_compatibility_audit_images/`
(not generated/build output — committed investigation reference material).

**CNA (pre-fix, EasyGL renderer, texture fix applied, no CullMode override), live screenshot:**
`cna_default_bug_present.png`.

A dark, flat, disc-shaped surface is clearly visible poking out from behind/under the turret
dome — matches evidence items 4-9 exactly.

**Authoritative original XNA 4.0 reference (provided 2026-07-11 by the project owner: the real
C# `SimpleAnimation` sample, built and run on Windows 7 inside a VirtualBox VM):**
`xna4_reference_authoritative.png`.

The turret in the real XNA screenshot is visually compact and correctly seated on the hull — the
dark underside/disc artifact is **not present**. The two screenshots are at different animation
times/world rotations (the sample continuously rotates the whole scene and independently animates
wheels/steering/turret/cannon/hatch), so no pixel-level comparison of unrelated geometry (wheel
angle, cannon angle, hatch state) is meaningful — only face-visibility/effective-CullMode
behavior was compared, per the task's own explicit instruction.

**This was direct, live, authoritative evidence of an observable XNA-vs-CNA rendering mismatch**
— not a comparison against Blender, DirectXTK, or any other non-XNA reference.

---

## 4. Phase 1 — minimal CNA culling reproducer (no Model/FBX/texture/lighting/bones/animation)

Two new permanent regression tests were added to `cna_graphics`, both registered on all 3
runnable renderers (EasyGL, Vulkan, Bgfx):

### 4.1 `examples/rasterizerstate_cullmode_camera_test.cpp`

Extends the pre-existing, already-passing
`examples/easygl_rasterizerstate_cullmode_test.cpp`/`bgfx_rasterizerstate_cullmode_test.cpp`
(Tasks 323-325/765, identity-transform only) with a **real** `Matrix.CreateLookAt` view and
perspective/orthographic projection, matching SimpleAnimation's own exact camera
(`eye=(1000,500,0)`, `target=(0,150,0)`, `up=(0,1,0)`, `FOV=PiOver4`, `near=10`, `far=10000`).

**Method** (deliberately avoids ever hand-predicting an expected screen position):
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

Five scenarios: (a) identity anchor, (b) orthographic + `CreateLookAt`, (c) perspective +
`CreateLookAt` (SimpleAnimation's exact camera), (d) same camera + positive-determinant World
(`CreateRotationY`), (e) same camera + negative-determinant World (`CreateScale(-1,1,1)`,
documented separately — a real flip is *expected* here).

**Result: 30/30 checks PASS on EasyGL, Vulkan, and Bgfx — identical outcome on all 3 renderers.**

### 4.2 `examples/rasterizerstate_cullmode_indexed_basiceffect_test.cpp`

Same methodology, but drives `GraphicsDevice::DrawIndexedPrimitives` with a **real**
`VertexBuffer`/`IndexBuffer`/`BasicEffect` (`VertexPositionNormalTexture`, stride 32) plus
`EnableDefaultLighting()` — the exact code path `ModelMesh::Draw()` uses, not the simpler
`DrawUserPrimitives`/`DrawColoredPrimitives` path every pre-existing CullMode test happened to
use.

**Result: 6/6 checks PASS on EasyGL, Vulkan, and Bgfx.**

An early draft used one shared `VertexBuffer`/`IndexBuffer` at two `startIndex` offsets, which
surfaced a real, separate Bgfx bug (§6 old numbering removed — see the standalone note in §9) —
worked around in the test using two dedicated buffers, matching how every real `ModelMeshPart`
actually works.

### 4.3 Conclusion of Phase 1 (still correct, but incomplete on its own — see §5)

**CNA's `CullMode`/`RasterizerState` implementation is self-consistent** across identity,
orthographic, and perspective projection; a real `Matrix.CreateLookAt` view; positive- and
negative-determinant World transforms; the simple and the real indexed/`BasicEffect` dispatch; and
all 3 renderers. This ruled out an *internal* framework inconsistency (an enum mapped backward on
just one renderer, or a Y-flip applied inconsistently with the cull-state mapping on one specific
renderer). **It could not, on its own, rule out CNA's entire convention being self-consistently
different from real XNA's — see §5.5 and §6 for why, and how that was resolved.**

---

## 5. Phase 3/5 — tracing the real turret_geo transform, and the (first, too narrow) asset-level
conclusion

### 5.1 Why the original evidence's own normal-vs-winding check (item 12) is a weaker signal than
it first appears

A per-triangle check of "does this triangle's geometric winding (from its 3 vertex positions)
agree with its own stored vertex normal" was re-run for `turret_geo` specifically: **0
disagreements across all 1674 triangles.** This is a genuinely useful data point, but on its own
it is **not proof the mesh is correctly wound** — it only proves *internal* consistency between
each triangle's winding and its own normal. If normals were derived from (rather than
independently authored against) the same winding data during FBX export/conversion, a mesh with
its **entire** winding convention reversed relative to true intent would pass this exact check
with 0 disagreements too. Per the task's own explicit instruction ("do not use lighting normals
as evidence for culling correctness"), this check was not relied on for the final conclusion.

### 5.2 Edge-adjacency orientation consistency (a real, non-circular check) — run on all 12 meshes

A proper closed/manifold mesh has every internal edge shared by exactly 2 triangles, each
traversing that shared edge in the **opposite** direction from the other — a standard polygon-
orientation invariant, independent of stored normals entirely. Checked directly against every one
of the 12 real shipped `tank_*_verts.bin`/`tank_*_idx.bin` pairs (`canon_geo`, `hatch_geo`, both
`{l,r}_{back,front}_wheel_geo`, both `{l,r}_{engine,steer}_geo`, `tank_geo`, `turret_geo`):

**Result: 0 orientation-inconsistent triangles in every single one of the 12 meshes** (positions
matched with a small tolerance to account for legitimately duplicated seam/UV-boundary vertices).
Every mesh is individually a genuinely coherent, consistently-oriented manifold — no random
per-triangle indexing errors anywhere in the asset.

**This is exactly why the investigation's first conclusion (below) was wrong in scope but not in
kind**: a mesh can be perfectly self-consistent (this check) while its *entire* winding
convention is reversed relative to true intent — the check cannot distinguish those two cases,
and neither could the NDC-signed-area analysis in §5.4, which is *also* entirely internal to
CNA's own matrix pipeline.

### 5.3 The real runtime World transform for turret_geo

Instrumented `cna-samples/samples/SimpleAnimation/src/Tank.hpp`'s `Draw()` (temporarily, reverted
before any commit) to print the actual `BasicEffect.World` matrix used for `turret_geo` during a
real running frame: **`World.Determinant() = 1.000000`** — a plain rotation (the small
`TurretRotation`-driven `Matrix.CreateRotationY`) times a pure translation (the rest-pose offset
from `cna-samples`' own `ApplyRestTransforms()`). Positive-determinant, confirmed — rules out a
transform-composition bug (wrong parent chain, an accidental reflection introduced by
`ModelBone`/`Model` composition) as the cause. This matches Phase 1's own scenario (d).

### 5.4 The first (too narrow) conclusion: NDC-signed-area prediction for turret_geo alone

Using the same NDC-signed-area methodology from §4.1, applied directly to `turret_geo`'s own real
vertex/index data under the real transform (§5.3): a specific, obviously-underside triangle
(`tri#0`, average Y ≈ 0.00, face normal ≈ straight down) predicted to **survive** the default
`CullCounterClockwiseFace` — front-facing from the real camera given its actual stored winding.
Broadened to a ~100-triangle "underside region" candidate set (78 of 100 predicted to survive) and
to the full 1674-triangle mesh (52.1%/47.9% survive/cull split — the expected roughly-even split
for *any* correctly-wound closed/convex-ish mesh, and — this was the reasoning error — also
exactly what a *uniformly reversed* mesh would show, since reversing every triangle's winding
does not change which ~half face toward vs. away from the camera, only which of those halves is
labeled "front").

**This is where the investigation's first write-up stopped and drew too narrow a conclusion**:
"the underside/disc sub-region of `turret_geo` has a real, internally consistent but
XNA-incompatible winding, isolated to that sub-region" — treating the 52/48% full-mesh split as
evidence the *rest* of the mesh was fine. It was not fine; it just wasn't *visually obvious* that
it was wrong, because a convex-ish exterior shell's outward silhouette barely changes when its
winding is uniformly reversed (only concave/interior-facing detail exposes it) — exactly why the
turret's own dome looked correct in every screenshot while its one exposed interior surface (the
underside disc) did not.

### 5.5 The test that actually settled scope: a whole-tank CullMode flip, and all 12 meshes checked

Two things, done in direct response to the project owner pointing out (correctly) that a live
screenshot of the "fixed" turret still showed clearly wrong-looking wheels — solid drums in real
XNA, visibly hollow/inside-out in CNA:

1. **Edge-adjacency was re-run on all 12 meshes, not just `turret_geo`** (§5.2 above) — confirmed
   every mesh is *individually* self-consistent, which (per §5.2's own caveat) is fully compatible
   with *every* mesh sharing the same reversed-relative-to-XNA convention.
2. **A global, whole-model `RasterizerState.CullMode = CullClockwiseFace` override** (not a
   per-mesh one) was set for one diagnostic run, covering every one of the 12 meshes at once —
   *not* the final fix, purely a diagnostic. Result: the turret's disc disappeared **and** all 4
   wheels changed from a hollow/inside-out appearance to solid drums with correctly-visible tread
   spikes, matching the XNA reference at multiple rotation angles.

This is the direct evidence that resolved scope: **the winding reversal is asset-wide (all 12
meshes), not isolated to `turret_geo`'s own disc.** (Diagnostic screenshots: this override was
reverted immediately after — not a proposed fix, since a per-app `CullMode` override would be
exactly the "SimpleAnimation-specific workaround" the task's own instructions prohibit.)

### 5.6 `fbx_ascii2model.py`'s own triangulation logic — checked directly, not just inferred

The tool's `triangulate()` function was read line-by-line against the FBX ASCII format's own
`PolygonVertexIndex` convention (a negative value encodes both `(-n)-1` as the real index **and**
marks the end of a polygon). Confirmed **correct**: fan-triangulation from `polygon[0]`
faithfully preserves whatever vertex order the FBX's own `PolygonVertexIndex` array encodes — it
does not introduce a reversal itself. The reversal most likely originates either in a
coordinate-system/axis-handedness convention difference between this hand-written ASCII parser
and real XNA's actual content-pipeline FBX importer (not yet pinned down to an exact line), or
elsewhere in the tool not yet traced to this level of detail. **Not required for the fix applied
in §7** (a direct, verified data correction), but worth resolving in `fbx_ascii2model.py` itself
so future FBX conversions via this tool don't reproduce the same defect — tracked as a follow-up,
not done here.

---

## 6. The decisive test — resolving "CNA bug" vs. "asset bug" with real XNA 4.0, not FNA reading

Phase 1 (§4) proved CNA's `CullMode` convention is *self-consistent*. It could not prove that
convention matches *real* XNA — that conclusion was drawn from reading FNA's own native renderer
source (`FNA3D_Driver_OpenGL.c`/`FNA3D_Driver_D3D11.c`), which is authoritative for this project's
own conventions (per `CLAUDE.md`) but is still an independent reimplementation, and the specific
derivation involves an easy-to-invert detail (NDC space is Y-up; screen/pixel space is Y-down;
mixing the two silently flips which "clockwise" is meant). The project owner correctly pushed
back on this: a whole-tank fix could equally be explained by CNA's own default `CullMode` being
backward relative to real XNA, not by the asset.

**This is exactly why `tools/xna-reference/CullModeTest/` (§9) existed — to settle it with real
XNA, not more reasoning about FNA source.** The project owner built and ran it on the same
Windows 7/XNA 4.0 VM used for §3's screenshot. Result
(`docs/xna_culling_compatibility_audit_images/` — see the project's own screenshot from that run):

| Quadrant | CullMode | Real XNA 4.0 result |
|---|---|---|
| top-left | `None` | both RED and GREEN triangles visible |
| top-right | `CullClockwiseFace` | GREEN only |
| bottom-left | `CullCounterClockwiseFace` (documented default) | RED only |
| bottom-right | unset (whatever the real default actually is) | RED only — matches bottom-left |

RED is the test's own "negative NDC signed area" triangle (same construction, same labeling
convention as the CNA reproducer, §4.1). **Real XNA's default state keeps RED, culls GREEN —
exactly what CNA's own default state does.** This is a direct, independent, non-FNA-derived
confirmation: **CNA's `CullMode` framework genuinely matches real XNA 4.0.** The "maybe CNA itself
is backward" hypothesis is now ruled out by real hardware/software evidence, not just re-reading
source a second time.

With that resolved, the only remaining explanation for the whole-tank symptom (§5.5) is the one
this document's §7 now applies: the asset data itself.

---

## 7. The fix

**Root cause (confirmed, not just inferred): all 12 of SimpleAnimation's `tank_*_idx.bin` mesh
index buffers have their triangle winding reversed relative to what real XNA's content pipeline
would have produced from the same source `tank.fbx`.** Scope: `cna-samples`
(`samples/SimpleAnimation/Content/`) only — a data/conversion defect, not a `cna` framework
defect (§6 rules that out directly).

**Fix applied**: for every one of the 12 `tank_*_idx.bin` files, every triangle's 2nd and 3rd
index were swapped (`(i0,i1,i2) -> (i0,i2,i1)`), reversing its winding without changing which
vertices it references or the mesh's actual shape. Originals backed up first (not committed,
scratch-only) before any file was modified. No `cna` change, no `Tank.hpp`/
`SimpleAnimationGame.hpp` change, no runtime `CullMode` override anywhere — the fix makes the
*data* correct so the app renders correctly under XNA's real, unmodified default `RasterizerState`.

**Verification**: rebuilt, ran, and screenshotted at 3 different rotation angles (the sample
auto-rotates and independently animates wheels/steering/turret/cannon/hatch, so exact pixel
matching against a single differently-timed XNA screenshot isn't meaningful — face-visibility was
compared, per the task's own instruction). At every angle checked:
- The turret's underside disc is gone — closed, correctly-seated dome, matching the XNA reference.
- All 4 wheels show solid drums with correctly-visible tread spikes — no more hollow/inside-out
  appearance.
- No new holes, gaps, or missing surfaces introduced anywhere else on the hull, engine covers,
  exhaust pipes, cannon, or hatch.
- Animation (wheel rotation, steering, turret rotation, cannon elevation, hatch) unaffected, as
  expected — winding reversal changes only which side of a triangle is considered front-facing,
  never vertex positions or the bone/animation data.

**Files changed** (all in `cna-samples`, `samples/SimpleAnimation/Content/`): `tank_canon_geo_idx.bin`,
`tank_hatch_geo_idx.bin`, `tank_l_back_wheel_geo_idx.bin`, `tank_l_engine_geo_idx.bin`,
`tank_l_front_wheel_geo_idx.bin`, `tank_l_steer_geo_idx.bin`, `tank_r_back_wheel_geo_idx.bin`,
`tank_r_engine_geo_idx.bin`, `tank_r_front_wheel_geo_idx.bin`, `tank_r_steer_geo_idx.bin`,
`tank_tank_geo_idx.bin`, `tank_turret_geo_idx.bin`. Same file sizes before/after (only index
*values* within each triangle were swapped, not resized).

**Deliberately not touched**: `CameraShake`, `CustomModelClass`, and `ReachGraphicsDemo` each
have their **own, independent** copies of the same `tank_*_idx.bin` files (confirmed not
shared/symlinked with SimpleAnimation's) — these almost certainly have the identical defect
(same conversion tool, same source FBX) but were out of this task's own scope. `SplitScreen` and
`TankOnHeightmap` (the other 2 samples that were going to share this rig, per
`missing.md`'s own note) don't have `Content/` mesh files yet — not affected, but whoever ports
them next should be aware `fbx_ascii2model.py`'s own winding handling (§5.6) is still unresolved
at the tool level, so a *fresh* conversion for those samples could reproduce the same defect.

---

## 8. A real, separate bug found incidentally (not the root cause above, documented for its own
sake)

While building §4.2's test, an early draft used one shared `VertexBuffer`/`IndexBuffer` read at
two different `startIndex` offsets (0 and 3). On Bgfx only, the second draw silently redrew the
**first** triangle's own indices instead of the requested second range.

**Root cause, confirmed by direct source reading**:
`BgfxRenderer::DrawIndexedPrimitivesEx`'s non-wireframe branch calls the offset-less
`bgfx::setVertexBuffer(stream, handle)` / `bgfx::setIndexBuffer(handle)` overloads
unconditionally — `GpuDrawParams::startIndex`/`baseVertex` are read and forwarded correctly to
`ExpandWireframeIndices()` (the *wireframe* path only) but are **never applied** to the real,
non-wireframe vertex/index buffer binding at all.

Not visible in any current CNA sample/test because every current `Model`/`ModelMeshPart` owns its
own dedicated, complete `VertexBuffer`/`IndexBuffer` pair starting at index/vertex 0.

**A fix was attempted** (the correct offset-aware `bgfx::setIndexBuffer(handle, firstIndex,
numIndices)` / `bgfx::setVertexBuffer(stream, handle, startVertex, numVertices)` overloads for
`DynamicIndexBufferHandle`/dynamic vertex buffers) but **produced a worse regression** on retest
(the offset draw stopped rendering anything at all) — root cause of *that* not identified within
this session's time budget. **The fix was reverted, not committed.** **Tracked as a new, separate,
NOT-yet-fixed task** — not part of this audit's own root cause and explicitly out of scope for
the culling investigation itself.

---

## 9. New XNA-side reference tooling

`cna-samples/tools/xna-reference/CullModeTest/` — a minimal, standalone XNA 4.0 Windows project
(no content pipeline, no textures, no models) implementing the exact same 2-triangle-opposite-
winding methodology as §4.1's CNA test, using SimpleAnimation's own exact camera. Renders all 4
`CullMode` combinations simultaneously in 4 screen quadrants for a single-screenshot comparison.
**Run by the project owner on a real Windows 7/XNA 4.0 VM (§6)** — the result was the decisive
evidence that closed this investigation.

---

## 10. Files changed (final)

### `cna_graphics`

- **New**: `examples/rasterizerstate_cullmode_camera_test.cpp` (§4.1), registered on EasyGL/
  Vulkan/Bgfx.
- **New**: `examples/rasterizerstate_cullmode_indexed_basiceffect_test.cpp` (§4.2), registered on
  EasyGL/Vulkan/Bgfx.
- **New**: this document + `docs/xna_culling_compatibility_audit_images/` (3 reference
  screenshots, plus the XNA `CullModeTest` result).
- `CMakeLists.txt`: the 2 new tests' registration (6 `add_test`/`cna_*_test` blocks total).
- **No production `CNA`/`Microsoft::Xna` source changes** — the framework was found correct
  (confirmed twice over: internally self-consistent, §4; and independently matching real XNA,
  §6).

Diagnostic-only changes made and **reverted, not committed**:
- A temporary `BgfxRenderer::DrawIndexedPrimitivesEx` `startIndex`/`baseVertex` fix (§8) —
  reverted after it introduced a worse regression on retest.
- A temporary Bgfx-only regression test for that fix — deleted along with the reverted fix.

### `cna-samples`

- **New**: `tools/xna-reference/CullModeTest/` (§9).
- **Fixed**: all 12 `samples/SimpleAnimation/Content/tank_*_idx.bin` files (§7) — triangle
  winding reversed to match real XNA's content-pipeline convention.
- **Unchanged**: `samples/SimpleAnimation/src/Tank.hpp` and `SimpleAnimationGame.hpp` — several
  diagnostic experiments were made in each during the investigation and **fully reverted** before
  any commit (confirmed via `git diff` showing zero net changes to either file each time):
  1. `Tank.hpp`: forcing an explicit `RasterizerState.CullCounterClockwise` set before every mesh
     draw (ruled out "the default state never gets applied to the real running game").
  2. `Tank.hpp`: printing `turret_geo`'s real runtime `World` matrix + forcing
     `CullClockwiseFace` specifically for `turret_geo`'s own draw call (§5.3, and re-confirmed
     evidence item 9 was still true on the pre-fix codebase).
  3. `SimpleAnimationGame.hpp`: a global (whole-model, not per-mesh) `CullClockwiseFace` override
     (§5.5) — the diagnostic that actually revealed the true scope of the defect.
  4. A blocked, then explicitly re-authorized (after §6's independent XNA confirmation) direct
     edit of `tank_turret_geo_idx.bin` alone — superseded by the full, all-12-meshes fix in §7
     once scope was correctly understood.
- **No SimpleAnimation-specific `CullMode` workaround was added or committed anywhere** — the fix
  is entirely at the data level; the app's own code renders correctly under XNA's real, unmodified
  default `RasterizerState`.

---

## 11. Summary for future readers

- CNA's `CullMode` framework is correct — confirmed twice, once against itself (§4) and once
  against real XNA 4.0 running on real hardware (§6). Do not re-investigate the framework itself
  without new evidence.
- The original SimpleAnimation symptom was **not** isolated to `turret_geo` — it was every one of
  the 12 mesh parts in `tank.fbx`'s conversion. If re-deriving this, don't stop at "one mesh's
  disc region is wrong" just because the rest of that one mesh's own statistics look normal
  (§5.4's own cautionary tale) — check whether a *whole-model* CullMode flip fixes everything
  before concluding the defect is localized.
- Do **not** use stored vertex normals as evidence of correct winding on their own (§5.1) — use
  edge-adjacency orientation (§5.2) or a direct comparison against a real external reference
  (§5.5/§6) instead.
- `CameraShake`, `CustomModelClass`, `ReachGraphicsDemo` almost certainly have the same defect in
  their own independent `tank_*_idx.bin` copies — not fixed here, flagged for whoever next
  touches those samples' own turret/wheel rendering.
- `fbx_ascii2model.py`'s own root cause for *why* it produces reversed winding is still not
  pinned down to an exact line (§5.6) — the shipped data fix does not depend on finding it, but a
  future FBX conversion through this same tool could reproduce the defect until that's fixed too.
- The Bgfx `startIndex`/`baseVertex` bug (§8) is real but **unrelated** to this investigation's
  own root cause — don't conflate the two if revisiting either.
