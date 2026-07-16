# Samples Task Plan

> Goal: track, for every sample in the official XNA Game Studio 4.0 collection catalogued by
> `../cna-samples` (153 total), what CNA-side gap or re-verification work (if any) remains before
> that sample can be considered genuinely done — not just "builds and runs."

> **History:** this content originally lived inline in `plan_graphics.md` as Phase 79 (added
> 2026-07-11), numbered Tasks `957`–`1076`. Moved to this dedicated file and renumbered to
> `SAMPLE-1`–`SAMPLE-120` (2026-07-16) — same convention as the WebGPU split
> (`plan_graphics.md` Phases 56–69 → `plan_webgpu.md`, `WEBGPU-1`+). No task content changed in
> the move, only the numbering and file location. Notes below still cross-reference
> `plan_graphics.md`'s own task numbers (e.g. "Task 954", "Task 938", "Phase 78") for the CNA
> engine bugs/features found while porting these samples — those tasks were **not** moved and stay
> tracked in `plan_graphics.md`.

## Legend

| Symbol | Meaning |
|--------|---------|
| ⬜ | Not started / needs action or re-verification |
| ✅ | Done |
| ⛔ | Permanently out of scope (structural reason, no CNA action conceivable) |

---

> **Scope and purpose.** Per explicit project-owner request (2026-07-11, after Tasks 954/955
> closed 2 real CNA bugs found while re-checking `SimpleAnimation`): every one of the **153**
> sample directories `../cna-samples/PLAN.md`/`ignored.md` catalogues (from the official XNA Game
> Studio 4.0 archive) gets its own row here, re-framed as *"what, if anything, does CNA itself
> still need so this sample can be ported or finished — and does its currently-recorded status
> still hold up?"* This phase exists because Tasks 954/955 showed a sample marked "✅ Done" in
> `../cna-samples/PLAN.md` can still hide a real, unfound CNA-level rendering bug — "done" so far
> has generally meant "builds and runs," not "pixel-verified against real XNA." **Only samples
> with a hard, structural, non-CNA reason (a WinForms tool, an XNA 2.0/3.0 archive, an Xbox LIVE
> service, phone-only hardware, art-only data, a redundant duplicate) are marked `⛔` (permanently
> out of scope, no CNA action conceivable, no revisit needed unless the reason itself stops
> applying — see each row). Every other sample is `⬜` — actionable, either as a port, a CNA-gap
> fix, or (for already-"Done" samples) a re-verification pass — until it's individually confirmed
> correct against a real XNA/FNA reference, the way Tasks 954/955 did for `SimpleAnimation`.
>
> **Source of truth for per-sample status**: `../cna-samples/PLAN.md` (own "Complete Sample Task
> List", updated 2026-07-10) and `../cna-samples/ignored.md`. **Source of truth for known CNA
> gaps**: `../cna-samples/DEFERRED.md`'s own Summary Table (30 items, most already ✅ resolved —
> the still-open ones referenced below are items **#10** (GamePad button shortcut, cosmetic,
> workaround exists), **#18** (content-pipeline processor extensibility), **#22** (EasyGL
> `BlendState.ColorWriteChannels` ignored), **#27** (`NetworkSession.SessionProperties` no mutable/
> replicated accessor), **#28**/**#29** (EasyGL SpriteBatch-before-3D / `DualTextureEffect`
> vertex-layout gaps, both already worked around in their one affected sample). The **Phase
> 78** shader-conversion umbrella (item #11, 14 samples) is now **RESOLVED on the CNA side, as of
> 2026-07-16** — see the status block immediately below. **Actual sample-porting work itself
> (writing/fixing `.cpp`/`.hpp`/`Content/` files under `../cna-samples/samples/<Name>/`) belongs
> in that sibling repo, not here** (same convention as Task 938) — a row below only tracks the
> **CNA-side** gap/re-verification; where a sample needs no CNA change at all, its row says so
> explicitly and the only remaining work is the port itself (tracked in `../cna-samples/PLAN.md`,
> not duplicated here).
>
> **Status update, 2026-07-16 — Phase 78 (DEFERRED.md item #11, HLSL→GLSL sample shader
> conversion) is now fully CLOSED on the CNA side, EasyGL only.** All 14 samples originally
> blocked purely by this item now have every one of their custom shaders ported to GLSL and
> pixel-verified: `BloomSample` (Task 946, closed before this update) plus, this session, all 13
> of `NetRumble`, `PerPixelLighting`, `VertexLighting`, `DistortionSample`, `NonPhotoRealistic`,
> `ShadowMapping`, `NormalMapping`, `BillboardSample`, `ShatterEffect`, `Particles3D`,
> `XmlParticles`, `ShipGame`, `InstancedModel` (`plan_graphics.md` Task 947, now 13/13 — see that
> task's own row for the full per-shader chronology, exact expected pixel values, and
> discriminating-power mutation testing done for every single one). Getting there required 4 new,
> additive EasyGL-only backend capabilities, each its own closed task in `plan_graphics.md`: **Task
> 1079** (wires `ShaderEffect` into `GraphicsDevice`'s 3D draw path, not just `SpriteBatch`),
> **Task 1080** (genuinely custom vertex layouts for that path — needed by `NormalMapping.fx`,
> `Billboard.fx`, `ShatterEffect.fx`, particle shaders), **Task 1081** (`TextureCube` sampling for
> custom shaders — needed by `ShipGame`'s own `NormalMapping.fx` reflection map), **Task 1082**
> (real GPU hardware instancing via `glVertexAttribDivisor` — needed by `InstancedModel.fx`'s
> `HardwareInstancing` technique). Below, each of the 13 samples' own rows now says **"No longer
> CNA-blocked"** with its own shader/test detail — they are **deliberately still marked `⬜`, not
> `✅`**, in this file: the CNA-side gap is closed, but the actual sample port itself (the
> `.cpp`/`.hpp`/`Content/` files under `../cna-samples/samples/<Name>/`) has not been written —
> per this file's own established convention (see the paragraph above), that work is tracked in
> the sibling `../cna-samples` repo's own plan file, not here, and was **not** started this
> session (confirmed out of `cna_graphics` scope). **What remains for Phase 78 specifically**: (a)
> the 13 actual sample ports themselves, in `../cna-samples`; (b) Vulkan/Bgfx/SDL_Renderer/D3D11
> parity for the 4 new backend capabilities above — deliberately not attempted, EasyGL-only was
> this session's explicit, consistent scope for every one of them, same as every other Phase 78
> task. **This does not touch any of this file's other ~88 open `⬜` rows** — those are unrelated
> to shaders (re-verification passes, other DEFERRED.md items) and remain exactly as they were.
>
> **`SimpleAnimation` (#050) is deliberately still `⬜`, not `✅`**, despite Tasks 954/955 both
> closing real bugs this session — flagged for a **future** re-review: (a) pixel-perfect comparison
> against a real XNA screenshot once convenient (today's verification was "closely matches,"
> visual/qualitative, not a pixel-diff against a like-for-like animation frame); (b) `CameraShake`
> (#030), `CustomModelClass` (#052), and `ReachGraphicsDemo` (#005) each have their own independent
> copy of a `tank`-family FBX/mesh and were flagged, not checked, for the same winding defect
> Task 954 found and fixed only in `SimpleAnimation`'s own copy; (c) `TankOnHeightmap` (#074) and
> `SplitScreen` (#076) share the exact same `tank.fbx`/`Tank.cs` source asset and per-mesh-bone gap
> and should get the identical winding + depth-occlusion scrutiny once their own Task 938 asset-regen
> follow-up is picked up; (d) the still-open `fbx_ascii2model.py` winding root-cause (Task 954 §5.6)
> and the two still-open, unrelated bugs found along the way (Bgfx `DrawIndexedPrimitivesEx`
> `startIndex` bug, Task 954 §8; EasyGL `SpriteBatch` blend-state leak, Task 956) are all
> candidates to fold into whichever future session re-opens this row.

## Phase 1 — Foundation (#001–012)

| #    | Sample (PLAN.md #) | Status | CNA-side action needed |
| ---- | ------------------- | ------ | ----------------------- |
| SAMPLE-1 | PrimitivesSample (001) | ⬜ | Re-verify against current CNA; `../cna-samples/PLAN.md` marks Done, no known CNA gap. |
| SAMPLE-2 | Primitives3D (002) | ⬜ | Re-verify; ships via the `VertexPositionNormalTexture`-with-dummy-UV workaround (DEFERRED #5) — no CNA change wanted (Task 935 closed, not implemented, by explicit project-owner decision). |
| SAMPLE-3 | TexturesAndColors (003) | ⬜ | Re-verify against current CNA; no known CNA gap. |
| SAMPLE-4 | StockEffects (004) | ⛔ | Ships only an effect-source + CLI compiler, no runnable `Game` — structural, not a CNA gap. No revisit trigger. |
| SAMPLE-5 | ReachGraphicsDemo (005) | ⬜ | 5/6 demo scenes done; `SkinnedDemo` blocked on DEFERRED #13 (skeletal animation, partially done — re-check once Phase 77 fully lands). Also flagged (see phase intro) to check its own `saucer.fbx`/`model.fbx` for the same winding defect Task 954 found in `SimpleAnimation`'s `tank.fbx` (DEFERRED #30 already suspects this). |
| SAMPLE-6 | SpriteEffects (006) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-7 | SpriteSheet (007) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-8 | ShapeRendering (008) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-9 | InputReporter (009) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-10 | InputSequence (010) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-11 | SafeArea (011) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-12 | GeneratedGeometry (012) | ⬜ | Re-verify; no known CNA gap. |

## Phase 2 — 2D Games & Gameplay (#013–030)

| #    | Sample (PLAN.md #) | Status | CNA-side action needed |
| ---- | ------------------- | ------ | ----------------------- |
| SAMPLE-13 | Platformer (013) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-14 | Spacewar (014) | ⬜ | `RenderTarget2D` itself now resolved (DEFERRED #12) — re-check whether Model + custom shaders (#11, Phase 78) + XACT audio are still the only remaining blockers before re-confirming Placeholder status. |
| SAMPLE-15 | TicTacToe (015) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-16 | Bounce (016) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-17 | CollisionSample (017) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-18 | PerPixelCollision (018) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-19 | RectangleCollision (019) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-20 | TransformedCollision (020) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-21 | PathDrawing (021) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-22 | Pathfinding (022) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-23 | WaypointSample (023) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-24 | FlockingSample (024) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-25 | ChaseAndEvade (025) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-26 | AimingSample (026) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-27 | FuzzyLogic (027) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-28 | ColorReplacement (028) | ⬜ | Blocked on custom `ReplaceColor.fx` shader (DEFERRED #11, Phase 78) — model conversion itself already unblocked (#6). |
| SAMPLE-29 | ParticleSample (029) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-30 | CameraShake (030) | ⬜ | Re-verify; **also check this sample's own independent `tank`-family mesh copy for the same winding defect Task 954 fixed in `SimpleAnimation`** (see phase intro, point b). |

## Phase 3 — 3D Graphics & Shaders (#031–049)

| #    | Sample (PLAN.md #) | Status | CNA-side action needed |
| ---- | ------------------- | ------ | ----------------------- |
| SAMPLE-31 | BloomSample (031) | ⬜ | Blocked on 3 custom shaders (DEFERRED #11, Phase 78 Task 946 — the planned first conversion target). `RenderTarget2D` itself already resolved (#12). |
| SAMPLE-32 | DistortionSample (032) | ⬜ | **No longer CNA-blocked (2026-07-16)** — its shader gap (#11) is now fully cleared: all 5 shader techniques the sample actually uses are ported and pixel-verified (`Distort.fx`'s `Distort`/`DistortBlur`; `Distorters.fx`'s `DisplacementMapped`/`HeatHaze`/`PullIn` — its 4th technique, `ZeroDisplacement`, is unused by the sample, "provided for reference" per the shader's own comment, not ported). Only the sample port itself remains (no `src/`/`CMakeLists.txt` yet in `../cna-samples/samples/DistortionSample` — tracked there, not here). |
| SAMPLE-33 | NonPhotoRealistic (033) | ⬜ | **No longer CNA-blocked (2026-07-16)** — its shader gap (#11) is now fully cleared: all 3 `CartoonEffect.Fx` techniques (`Lambert`/`Toon`/`NormalDepth`) and all 5 `PostprocessEffect.Fx` techniques (`EdgeDetect`/`EdgeDetectMonoSketch`/`EdgeDetectColorSketch`/`MonoSketch`/`ColorSketch`, one parameterized pixel shader ported as real runtime uniforms instead of 5 separately-compiled static HLSL variants — a documented, behaviourally-equivalent adaptation) are ported and pixel-verified. Only the sample port itself remains (no `src/`/`CMakeLists.txt` yet in `../cna-samples/samples/NonPhotoRealistic` — tracked there, not here). |
| SAMPLE-34 | NormalMapping (034) | ⬜ | **No longer CNA-blocked (2026-07-16)** — its shader gap (#11) is now fully cleared: `NormalMapping.fx` (tangent-space normal mapping + Phong specular) is ported and pixel-verified (`EasyGL_NormalMapping_Shader`), using Task 1080's new custom-vertex-layout capability (Position+TexCoord+Normal+Binormal+Tangent, stride 56 — matches none of CNA's 5 built-in strides). Only the sample port itself remains (no `src/`/`CMakeLists.txt` yet in `../cna-samples/samples/NormalMapping` — tracked there, not here). |
| SAMPLE-35 | PerPixelLighting (035) | ⬜ | **No longer CNA-blocked (2026-07-16)** — its shader gap (#11) is now fully cleared: all 5 effect/technique combinations this sample cycles through at runtime (`PerPixelLighting.fx`'s `PerPixelDiffuseAndPhong`/`PerPixelDiffuse`/`PerVertexDiffuseAndPerPixelPhong`, `VertexLighting.fx`'s `PerVertexDiffuse`/`PerVertexDiffuseAndPhong`) are ported and pixel-verified (`EasyGL_PerPixelLighting_Shader`/`_DiffuseOnly_Shader`/`_VertexDiffusePixelPhong_Shader`, `EasyGL_VertexLighting_Diffuse_Shader`/`_DiffusePhong_Shader`). Only the sample port itself remains (no `src/`/`CMakeLists.txt` yet in `../cna-samples/samples/PerPixelLighting` — tracked there, not here). |
| SAMPLE-36 | VertexLighting (036) | ⬜ | **No longer CNA-blocked (2026-07-16)** — its shader gap (#11) is now fully cleared: both effects the sample loads (`VertexLighting.fx`'s own `VertexLighting` technique, a directional light — not to be confused with the same-named file already ported for the `PerPixelLighting` sample; and `FlatShaded.fx`'s `FlatShaded` technique) are ported and pixel-verified (`EasyGL_VertexLighting_Directional_Shader`, `EasyGL_FlatShaded_Shader`). Only the sample port itself remains (no `src/`/`CMakeLists.txt` yet in `../cna-samples/samples/VertexLighting` — tracked there, not here). |
| SAMPLE-37 | RimLighting (037) | ⬜ | Re-verify; ported via a `Content.Load<TextureCube>`/`Content.Load<Model>` bypass — re-check whether items #14/#26 being since fully resolved lets this drop the bypass. |
| SAMPLE-38 | ShadowMapping (038) | ⬜ | **No longer CNA-blocked (2026-07-16)** — its shader gap (#11) is now fully cleared: both `DrawModel.fx` techniques (`CreateShadowMap`, `DrawWithShadowMap`) are ported and pixel-verified (`EasyGL_ShadowMapping_CreateShadowMap_Shader`, `EasyGL_ShadowMapping_DrawWithShadowMap_Shader`). Only the sample port itself remains (no `src/`/`CMakeLists.txt` yet in `../cna-samples/samples/ShadowMapping` — tracked there, not here). |
| SAMPLE-39 | BillboardSample (039) | ⬜ | **No longer CNA-blocked (2026-07-16)** — its shader gap (#11) is now fully cleared: `Billboard.fx` (view-facing billboard expansion + wind sway + alpha-tested diffuse lighting) is ported and pixel-verified (`EasyGL_Billboard_Shader`), using Task 1080's custom-vertex-layout capability (Position+Normal+TexCoord+Random, stride 36). Only the sample port itself remains (no `src/`/`CMakeLists.txt` yet in `../cna-samples/samples/BillboardSample` — tracked there, not here). |
| SAMPLE-40 | InstancedModel (040) | ⬜ | **No longer CNA-blocked (2026-07-16)** — its shader gap (#11) is now fully cleared: `InstancedModel.fx`'s `HardwareInstancing` technique is ported and pixel-verified (`EasyGL_InstancedModel_Shader`), using new Task 1082 real GPU hardware-instancing support (per-instance vertex stream via `glVertexAttribDivisor`, wired into `GraphicsDevice::DrawInstancedPrimitives`/`SetVertexBuffers`/`VertexBufferBinding`). This was Task 947's last remaining sample — the whole 13-sample shader-conversion effort (DEFERRED.md #11) is now complete. Only the sample port itself remains (no `src/`/`CMakeLists.txt` yet in `../cna-samples/samples/InstancedModel` — tracked there, not here). |
| SAMPLE-41 | LensFlare (041) | ⬜ | Re-verify; cosmetic gap open (DEFERRED #22, EasyGL ignores `BlendState.ColorWriteChannels`, not started) — confirm still non-blocking. |
| SAMPLE-42 | ShatterEffect (042) | ⬜ | **No longer CNA-blocked (2026-07-16)** — its shader gap (#11) is now fully cleared: `ShatterEffect.fx` (per-triangle rotation-and-fall shatter animation + Phong lighting) is ported and pixel-verified (`EasyGL_ShatterEffect_Shader`), using Task 1080's custom-vertex-layout capability (Position+Normal+TexCoords+TriangleCenter+RotationalVelocity, stride 56). Only the sample port itself remains (no `src/`/`CMakeLists.txt` yet in `../cna-samples/samples/ShatterEffect` — tracked there, not here). |
| SAMPLE-43 | Particles3D (043) | ⬜ | **No longer CNA-blocked (2026-07-16)** — its shader gap (#11) is now fully cleared: `ParticleEffect.fx` (GPU-animated billboarded particles) is ported and pixel-verified (`EasyGL_ParticleEffect_Shader`), using Task 1080's custom-vertex-layout capability (Corner+Position+Velocity+Random+Time, stride 52). Only the sample port itself remains (no `src/`/`CMakeLists.txt` yet in `../cna-samples/samples/Particles3D` — tracked there, not here). |
| SAMPLE-44 | Particles2DPipeline (044) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-45 | XmlParticles (045) | ⬜ | **No longer CNA-blocked (2026-07-16)** — shares `ParticleEffect.fx` byte-identical with `Particles3D` (confirmed via `diff`), so it's cleared by the same port (`EasyGL_ParticleEffect_Shader`). Only the sample port itself remains (no `src/`/`CMakeLists.txt` yet in `../cna-samples/samples/XmlParticles` — tracked there, not here). |
| SAMPLE-46 | Graphics3D (046) | ⬜ | Re-verify; shipped with a component-lifecycle workaround (DEFERRED #23, now confirmed correct XNA/FNA behavior, not a CNA bug — workaround should stay). |
| SAMPLE-47 | PickingSample (047) | ⬜ | Re-verify; same #23 workaround note as Graphics3D. |
| SAMPLE-48 | TrianglePicking (048) | ⬜ | Re-verify; uses the `fbx_ascii2model.py --picking` sidecar (DEFERRED #25 workaround) since `VertexBuffer`/`IndexBuffer::GetData()` didn't exist at the time — that gap is now resolved (Task 930); re-check whether the sidecar can be retired in favor of the real `GetData()` API. |
| SAMPLE-49 | HeightmapCollision (049) | ⬜ | Re-verify; no known CNA gap (hand-built runtime terrain mesh). |

## Phase 4 — Models & Animation (#050–058)

| #    | Sample (PLAN.md #) | Status | CNA-side action needed |
| ---- | ------------------- | ------ | ----------------------- |
| SAMPLE-50 | **SimpleAnimation (050)** | ⬜ | **Deliberately not marked Done — see this phase's own intro for the full future-re-review list** (points a–d): pixel-perfect XNA comparison still pending; `CameraShake`/`CustomModelClass`/`ReachGraphicsDemo`'s own independent tank-mesh copies not yet checked for Task 954's winding defect; `TankOnHeightmap`/`SplitScreen` share the same asset family; `fbx_ascii2model.py`'s winding root cause (Task 954 §5.6), the Bgfx `startIndex` bug (Task 954 §8), and the `SpriteBatch` blend-leak bug (Task 956) are all still open. Update `../cna-samples/PLAN.md`'s own stale "🚧 Placeholder" line for #050 to ✅ Done (it already has real, working source — this status line was not kept in sync with Tasks 954/955). |
| SAMPLE-51 | CustomModelAnimation (051) | ⬜ | Blocked on skeletal animation (DEFERRED #13 — partially done, loader/`Model::Draw` wiring landed Phase 77, but this sample's own port not yet attempted against it). |
| SAMPLE-52 | CustomModelClass (052) | ⬜ | Re-verify; **check for the same tank/model-family winding defect per this phase's intro** if it shares an asset with `SimpleAnimation`'s family (confirm asset identity first — `../cna-samples/PLAN.md` doesn't currently say). |
| SAMPLE-53 | CustomModelEffect (053) | ⬜ | Blocked on content-pipeline processor extensibility (DEFERRED #18, not started). |
| SAMPLE-54 | SkinningSample (054) | ⬜ | Blocked on skeletal animation (DEFERRED #13, partially done — re-check against current Phase 77 state). |
| SAMPLE-55 | SkinnedModelExtensions (055) | ⬜ | Blocked on skeletal animation (DEFERRED #13, same as above). |
| SAMPLE-56 | CPUSkinning (056) | ⬜ | Blocked on skeletal animation (DEFERRED #13, same as above). |
| SAMPLE-57 | InverseKinematics (057) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-58 | ChaseCamera (058) | ⬜ | Re-verify; no known CNA gap. |

## Phase 5 — Audio (#059–060)

| #    | Sample (PLAN.md #) | Status | CNA-side action needed |
| ---- | ------------------- | ------ | ----------------------- |
| SAMPLE-59 | Audio3D (059) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-60 | SoundAndMusic (060) | ⬜ | Re-verify; no known CNA gap. |

## Phase 6 — Full Games & Starter Kits (#061–074)

| #    | Sample (PLAN.md #) | Status | CNA-side action needed |
| ---- | ------------------- | ------ | ----------------------- |
| SAMPLE-61 | MarbleMaze (061) | ⬜ | Re-verify; no known CNA gap (confirmed a 2nd `assimp`-export winding-inversion quirk — asset-tooling, not CNA). |
| SAMPLE-62 | NetRumble (062) | ⬜ | **No longer CNA-blocked (2026-07-16)** — its shader gap (#11) is now fully cleared: the bloom trio was proven by Task 946, and its 4th and last blocking shader, `Clouds.fx`, was proven by Task 947 (`EasyGL_Clouds_Shader`). Networking was already unblocked. Only the sample port itself remains (no `src/`/`CMakeLists.txt` yet in `../cna-samples/samples/NetRumble` — tracked there, not here). |
| SAMPLE-63 | HoneycombRush (063) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-64 | HoneycombRushTrainingKit (064) | ⛔ | Redundant multi-exercise variant of already-ported `HoneycombRush` (063) — structural, no CNA gap. No revisit trigger. |
| SAMPLE-65 | NinjAcademy (065) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-66 | ShipGame (066) | ⬜ | **No longer CNA-blocked (2026-07-16)** — its shader gap (#11) is now fully cleared: all 4 of its distinct custom shaders (`AnimSprite.fx`, `Blur.fx`, `NormalMapping.fx`, `Particle.fx` — confirmed via `diff` that `NormalMapping.fx`/`Particle.fx` are NOT the same files already ported for `NormalMappingSample`/`Particles3DSample`) are ported and pixel-verified. `Particle.fx` uses real GPU point sprites (`PSIZE`/`gl_PointSize`/`gl_PointCoord`) — needed no new backend capability, `PrimitiveType::PointListEXT` was already wired to `GL_POINTS`. Only the sample port itself remains (no `src/`/`CMakeLists.txt` yet in `../cna-samples/samples/ShipGame` — tracked there, not here). |
| SAMPLE-67 | CatapultWars (067) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-68 | CatapultWarsTrainingKit (068) | ⛔ | Redundant multi-exercise variant of already-ported `CatapultWars` (067) — structural, no CNA gap. No revisit trigger. |
| SAMPLE-69 | CardsStarterKit (069) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-70 | RolePlayingGame (070) | ⬜ | Re-verify; shipped with some combat/screens simplified — confirm that simplification isn't masking a real CNA gap. |
| SAMPLE-71 | Yacht (071) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-72 | GameStateManagement (072) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-73 | SoccerPitch (073) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-74 | TankOnHeightmap (074) | ⬜ | Same `tank.fbx`/`Tank.cs` per-mesh `ModelBone` gap as SplitScreen (076) — Task 938's own asset-regen follow-up still open. **Shares `SimpleAnimation`'s asset family — apply the same winding + depth-occlusion scrutiny (Tasks 954/955) once ported.** |

## Phase 7 — Advanced, UI, Misc (#075–083, #102)

| #    | Sample (PLAN.md #) | Status | CNA-side action needed |
| ---- | ------------------- | ------ | ----------------------- |
| SAMPLE-75 | NGSMSample (075) | ⛔ | Even with Xbox LIVE lobby networking (#17, done), the sample's own "Single Player" path is an intentionally empty stub per the original's own documentation — structural, no CNA gap could ever unblock real gameplay here. No revisit trigger. |
| SAMPLE-76 | SplitScreen (076) | ⬜ | Needs per-mesh `ModelBone` support (DEFERRED #6 addendum) — landed (Tasks 936/937); Task 938's own asset-regen follow-up for this sample specifically still open. Shares `tank.fbx` family with `SimpleAnimation`/`TankOnHeightmap` — same future scrutiny applies. |
| SAMPLE-77 | DynamicMenu (077) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-78 | LocalizationSample (078) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-79 | GesturesSample (079) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-80 | TouchThumbsticks (080) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-81 | PerformanceMeasuring (081) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-82 | UISample (082) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-83 | SnowShovel (083) | ⬜ | Re-verify; no known CNA gap. |
| SAMPLE-84 | Orientation (102) | ⬜ | Re-verify; miscategorized as phone-hardware originally, has zero accelerometer/sensor dependency — no known CNA gap. |

## Deferred appendix — Phone Hardware / Avatar / WinForms / Xbox LIVE Networking (#084–111)

| #    | Sample (PLAN.md #) | Status | CNA-side action needed |
| ---- | ------------------- | ------ | ----------------------- |
| SAMPLE-85 | AccelerometerSample (084) | ⬜ | Re-verify; ported via the original's own emulator keyboard-tilt fallback code (user go/no-go approved) — no CNA gap. |
| SAMPLE-86 | AvatarAnimationBlending (085) | ⛔ | Xbox LIVE Avatar body/animation content system, permanently retired — CNA's opt-in substitute-body `AvatarRenderer::EnableRealRenderingEXT` path exists but was judged not faithful enough (user go/no-go, 2026-07-10). Revisit trigger: only if the substitute rendering quality is ever substantially improved and the project owner chooses to revisit that go/no-go. |
| SAMPLE-87 | AvatarMultipleAnimations (086) | ⛔ | Same Xbox LIVE Avatar dependency and same permanent-skip decision as 085. |
| SAMPLE-88 | AvatarShadows (087) | ⛔ | Same Xbox LIVE Avatar dependency and same permanent-skip decision as 085. |
| SAMPLE-89 | BingMaps (088) | ⛔ | External Bing Maps web API/service — not a CNA framework capability. No revisit trigger. |
| SAMPLE-90 | BingMapsPathFinding (089) | ⛔ | Same external Bing Maps dependency as 088. |
| SAMPLE-91 | BitmapFontMaker (090) | ⛔ | WinForms design-time tool, not a runnable `Game` — structural. No revisit trigger. |
| SAMPLE-92 | ClientServerSample (091) | ⬜ | Re-verify; shipped with 3 networking workarounds (DEFERRED #19–21, all now independently ✅ resolved at the CNA level too) — confirm the sample's own workarounds can be simplified/removed now that the underlying gaps are fixed. |
| SAMPLE-93 | ContentManifestExtensions (092) | ⛔ | Content-pipeline extension only, no executable — structural. No revisit trigger. |
| SAMPLE-94 | CurveEditor (093) | ⛔ | WinForms animation-curve editing tool, not a `Game` — structural. No revisit trigger. |
| SAMPLE-95 | CustomAvatarAnimation (094) | ⛔ | Same Xbox LIVE Avatar dependency and same permanent-skip decision as 085. |
| SAMPLE-96 | GeolocationSample (095) | ⛔ | Real phone GPS hardware; SDL has no portable geolocation API (unlike the accelerometer, covered generically by `SDL_Sensor`) — structural. Revisit trigger: only if SDL or a portable geolocation shim ever becomes available. |
| SAMPLE-97 | InvitesSample (096) | ⛔ | Xbox LIVE friends/invite/presence system tied to a real Xbox LIVE account, not LAN `NetworkSession` discovery — structural, unlike `ClientServerSample`/`NetworkPrediction`/`PeerToPeer`. No revisit trigger. |
| SAMPLE-98 | MemoryMadnessLab (097) | ⛔ | WP7 teaching-lab exercise + accompanying document, not a standalone sample — structural. No revisit trigger. |
| SAMPLE-99 | MicrophoneEcho (098) | ⬜ | Re-verify; no known CNA gap (DEFERRED #16 resolved). |
| SAMPLE-100 | ModelImporterSample (099) | ⛔ | Content-pipeline extension only, no executable — structural. No revisit trigger. |
| SAMPLE-101 | NetworkPrediction (100) | ⬜ | Re-verify; confirmed zero networking workarounds needed — flagged that `NetworkSession.SessionProperties` has no mutable/replicated accessor (DEFERRED #27, not started, worked around via an explicit options packet) — re-check whether #27 is worth fixing at the CNA level now. |
| SAMPLE-102 | ObjectPlacementOnAvatar (101) | ⛔ | Same Xbox LIVE Avatar dependency and same permanent-skip decision as 085. |
| SAMPLE-103 | PeerToPeer (103) | ⬜ | Re-verify; confirmed zero networking workarounds needed, doesn't use `SessionProperties` (item #27 not applicable here) — no known CNA gap. |
| SAMPLE-104 | PerformanceUtility (104) | ⛔ | Utility library only, no standalone executable — structural. No revisit trigger. |
| SAMPLE-105 | PushNotifications (105) | ⛔ | Windows Phone push notification service, no desktop analog — structural. No revisit trigger. |
| SAMPLE-106 | SavingEmbeddedImages (106) | ⛔ | Windows Phone media library API, no desktop analog — structural. No revisit trigger. |
| SAMPLE-107 | TiltPerspective (107) | ⬜ | Re-verify; ported via a genuinely invented keyboard-tilt scheme (user go/no-go approved, no original fallback existed) — no CNA gap. |
| SAMPLE-108 | WinFormsContent (108) | ⛔ | WinForms host window, not a `Game` — structural. No revisit trigger. |
| SAMPLE-109 | WinFormsGraphics (109) | ⛔ | WinForms host window, not a `Game` — structural. No revisit trigger. |
| SAMPLE-110 | WP7MusicManagement (110) | ⛔ | Windows Phone 7 media-library management API, no desktop analog (contrast with the already-ported `SoundAndMusic`/`Audio3D`, which use the portable `SoundEffect`/`Song` APIs) — structural. No revisit trigger. |
| SAMPLE-111 | XnaGraphicsProfileChecker (111) | ⛔ | WinForms diagnostic tool, not a `Game` — structural. No revisit trigger. |

## Everything else (#112–153) — 42 samples with no individual `PLAN.md` number, grouped by identical reason

> These never got an individual numbered `PLAN.md` entry (see `../cna-samples/ignored.md`) because
> each one's exclusion reason is shared identically by every other sample in its group — grouping
> them keeps this phase's table from padding out with 42 rows that would each say the exact same
> thing. Every reason below is structural (XNA version, file format, licensing/authorship, or
> platform), not a CNA capability gap — **none of these 42 can ever become CNA tasks**, matching
> `ignored.md`'s own framing exactly.

| #    | Group | Samples | Status | Reason |
| ---- | ----- | ------- | ------ | ------ |
| SAMPLE-112 | XNA 2.0/3.0/3.1 archives | BasicEffectShader, Catapult, MaterialsAndLights, Minjie, MultipassLighting, Pickture, RobotGame, SpriteBatchShader, VectorRumble, SpaceShooter, TiledSprites, RedistributableTTFs (12) | ⛔ | Pre-4.0 XNA API versions (or font files only) — this repo ports the XNA Game Studio 4.0 collection exclusively. No revisit trigger. |
| SAMPLE-113 | Avatar asset/rig packs | AvatarAnimPack ×4 (BIN/FBX/Maya/Mod Tool), AvatarRig ×3 (3ds Max 2010/Maya 2009/SoftImage Mod Tool 7.5) (7) | ⛔ | Art/animation/DCC-rigging data only, no C# game code at all. No revisit trigger. |
| SAMPLE-114 | Phone/Mango duplicates | GSMSample (Mango/Mango VB/Phone), ModelViewerDemo (Mango), PaddleBattle (Mango/Mango VB), RolePlayingGame (Phone) (7) | ⛔ | Each duplicates an already-ported desktop sample (or is phone-only with no desktop equivalent). No revisit trigger. |
| SAMPLE-115 | VB language duplicate | CardsStarterKit (VB) (1) | ⛔ | Visual Basic duplicate of already-ported `CardsStarterKit` (069, C#). No revisit trigger. |
| SAMPLE-116 | Silverlight/WP7-native code | CustomIndeterminateProgressBar, NonLinear WP SL Navigation, PushRecipe WP7, SilverlightMicrophone, TombstoningSample, LevelStarterKit (6) | ⛔ | Silverlight controls/apps or WP7 app-lifecycle demos, not XNA `Game`s at all. No revisit trigger. |
| SAMPLE-117 | Image/resource-only directories | ButtonImages, ControllerImages, LobbyChatImages (3) | ⛔ | Image assets only, consumed by other already-catalogued samples — no code. No revisit trigger. |
| SAMPLE-118 | Third-party/community kits | Riemers Tutorials, XNA-4-Racing-Game-Kit, Movipa (3) | ⛔ | Not official Microsoft samples — outside this repo's stated scope (the official XNA Game Studio 4.0 collection). No revisit trigger. |
| SAMPLE-119 | Unversioned/incomplete starter kit | UnitConverterStarterKit (1) | ⛔ | Directory contains only a license file and an empty stub subfolder — no real sample content to port. No revisit trigger. |
| SAMPLE-120 | Misc/non-code | XNA XNB Format (docs), SoundLab (standalone tool) (2) | ⛔ | Documentation-only or a standalone authoring tool, not an XNA game sample. No revisit trigger. |
