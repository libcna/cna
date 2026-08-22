# NEXT_modern.md — running ledger for the CNAEXT engine layer

Continuity file for [`plans/plan_modern.md`](plans/plan_modern.md) (the `MOD-*` backlog implementing
[`CNAEXT.md`](misc/CNAEXT.md)). Same role `NEXT_skia.md` has for the Skia renderer: read this first,
do not reconstruct the layer's state from the general `NEXT.md`.

---

## 1. Where the work stands

**Phases 0–19 are closed** (2026-08-19): every one of `MOD-1`–`MOD-1924` carries a verdict — ✅
done, 🟨 done-but-bounded with the bound stated, or ⛔ refused with the reason. Phases 0–15 and 17–19
are complete; **Phase 16 is measured rather than implemented**, which is the honest description of
what a per-renderer rollout turned into once every renderer was actually run.

**Phase 21 (`MOD-2101`–`MOD-2199`) is new and open** (2026-08-20), opened by the same method that
produced Phase 20 and with the same owner constraint — EasyGL only. Every gap in it was **verified
absent by grep before being written down**, not assumed, and the largest one the layer states about
itself today: `MaterialBinding.hpp` says *"Draw order still belongs to the application: CNA does not
sort."* The layer has **no transparency story at all** — every subsystem from Phase 0 to Phase 20
assumes opaque geometry, because the prepass writes one depth per pixel and SSAO, SSR, fog and
motion blur all reconstruct from that one depth. A game with a window in it has nothing to use.

Also absent, and each checked: contact shadows, 3D-LUT grading, output debanding, aerial perspective
on geometry, any debug drawing at all, and any GPU timing. The last two were felt directly in Phase
20 — `docs/cnaext-perf.md` is measured with a CPU wall clock around a read-back, and every frustum,
probe grid, cluster and light bound was verified by arithmetic because there is no way to look at
one.

**One boundary the phase does not cross.** `OQ-6` decided the scene-draw contract is app-driven, so
nothing here becomes a scene graph: `TransparentDrawList` is a sorting aid an application fills and
the layer orders — it decides *when* draws happen, never *what* they are.

Four things are refused up front, and one of them as a **class**: anything temporal (TAA, temporal
denoising, temporal upscaling) is refused once rather than five times, because five items in this
phase would otherwise each ask for the same reprojected history. The others are hardware occlusion
culling (a latency problem that needs per-object identity across frames, which is scene management),
virtual shadow maps (the sparse-texture hardware `MOD-2099` already refused) and screen-space GI —
that last one **refused as redundant rather than unreachable**, since its diffuse answer is what
`LightProbeVolumeEXT` already gives without screen space's failure modes.

**Phase 21 progress.** §21.1 transparency (`MOD-2101`–`MOD-2110`), §21.2 contact shadows
(`MOD-2120`–`MOD-2123`), §21.3 grading and output (`MOD-2130`–`MOD-2133`), §21.4 aerial perspective
(`MOD-2140`–`MOD-2142`) and §21.5 debug drawing and GPU timing (`MOD-2160`–`MOD-2165`) are all done.
**Phase 21 is complete**, every row verdicted, §21.6's four refusals included.

**§21.5 is the phase's most useful section and its three lessons are all about measurement.**
`GL_EXT_disjoint_timer_query` turned out to be *present* on this machine, so GPU timing is real here
rather than a refusal path with tests around it. Then three things went wrong in a row, each of them
the measurement rather than the code. Timing ten `Clear` calls against a hundred made the hundred
come back faster, because a driver may collapse repeated full-target clears — a workload the driver
can optimise away measures the optimiser. Collecting per-pass results *after* the chain ran reported
a number for the first pass and zero samples for every pass after it, forever: a query object holds
one result, and reopening it discards what the last range put there, so the poll must come before the
frame's ranges open. And the whole point of `MOD-2165` inverted: the GPU total was expected to come
out *below* the CPU wall clock and it does not — the two agree to within 2–6%, because a software
rasteriser has no asynchrony for a CPU clock to miss. That validates the existing table **on this
machine** and says nothing about a real GPU, which is what the doc now says.

**§21.4's test found the physics rather than a bug.** The far-end claim — that a surface distant
enough is *replaced* by the sky the same model draws — was written as "a black surface and a white
one converge", and it failed. They converge in blue and they do not in red: Rayleigh's red
coefficient is 0.0464 against blue's 0.2650, so the horizon's ~38 air masses still pass 17% of a
surface's red while its blue is gone to four decimal places. The right response was not a wider
tolerance but a second case stating the asymmetry, because that asymmetry *is* the effect — it is why
a distant mountain goes blue-grey rather than sky-coloured. A first version of the same test was also
wrong for a duller reason: its far plane was 100 km and the horizon cap needs 38 × 8400 ≈ 319 km, so
nothing was ever at the cap.

**§21.3 opened with a wrong premise in its own row and the correction is the useful part.**
`MOD-2130` said `ColorGradePass` has lift/gamma/gain a lookup table could not express. It does not —
it has been a 3D-LUT pass since `MOD-1400`. What was actually missing was the **format**: the file a
colourist delivers had no way in. That is a smaller task than the row described and a more useful
one. Two measurements in that section also came out against expectation. `MOD-2131` set out to trade
tetrahedral accuracy against tetrahedral cost and found there is no trade **on this machine** —
tetrahedral is both more accurate (0/255 neutral tint against 18/255) and *cheaper*, because it reads
four table entries where trilinear reads eight, and llvmpipe's bilinear filter costs about five point
fetches. On a GPU that filter is nearly free and the ranking should reverse, so the cost argument for
keeping `Trilinear` as the default does not hold here; the default stays where it is on
frame-compatibility grounds alone, and the row says so.

Two things §21.1 settled that were not in the row when it was written. The published weighted-blended
technique needs a **different blend function per draw buffer** — `glBlendFunci`, GL ES 3.2, above
this layer's floor and absent from CNA's `BlendState` besides — so revealage is accumulated as the
**sum of `log(1 - alpha)`** instead, which is additive and shares one blend state with the colour
target, and the resolve exponentiates it back. That kept the technique to one geometry pass rather
than two. And the approximation was **measured against the exact sorted frame** rather than described:
35/255 mean and 142/255 worst over 40 708 covered pixels, on a scene close to the worst case for it.
That number is what makes the choice between the two paths a real one rather than a preference.

§21.2's lesson was about the measurement, not the code. The first contact-shadow benchmark reported
**0.04 ms at every step count and every resolution** — a plausible-looking figure that meant the
driver had not run the march at all, because `apply()` only submits it. A one-texel `GetData` inside
the timed loop, the convention `cnaext_ssao_test` already used, turned that into 16–346 ms with a
slope that tracks pixels times steps to within 1%. **A benchmark whose numbers do not move with the
dial being measured is reporting submission, not work** — and it is the second time in two phases
that a measurement, not a test, was the thing that was wrong.

**Phase 20 (`MOD-2000`–`MOD-2100`) is complete** (2026-08-20) — the modern-renderer scope the first nineteen
phases never covered: screen-space reflections, depth of field, the lens and grading passes, motion
blur, clustered lighting for many lights, volumetrics, area lights, the glTF material extensions
beyond core, probe-based GI, and indirect draw. **EasyGL only, by owner decision**, with no
per-renderer rollout section: Phase 16 established that no other renderer executes this layer's
shader source, so rollout rows would wait on those renderers' own plans rather than on this one.
Four items are refused up front against that profile — hardware ray tracing, mesh shaders, temporal
upscalers and virtual texturing — each with the specific reason rather than silence.

**Phase 20 progress** (updated as sections close): 20.1 render-target coordinates, 20.2
screen-space reflections, 20.3 the lens and grade passes, 20.4 motion blur and depth of field,
20.5 clustered forward lighting (`MOD-2040`–`MOD-2048`), 20.6 volumetrics (`MOD-2050`–`MOD-2054`),
20.7 area lights (`MOD-2060`–`MOD-2063`), 20.8 the material extensions (`MOD-2070`–`MOD-2077`),
20.9 probe-based GI (`MOD-2080`–`MOD-2087`) and **20.10 GPU-driven rendering and display output
(`MOD-2090`–`MOD-2095`)** are done. Every Phase 20 row now carries a verdict except the two that are
open by design (below).

**What 20.10 turned out to be about: where an answer lives.** Five of its six rows are the same
question asked five ways — does a number the GPU produced have to come back to the CPU before it can
be used? `MOD-2090` put `DrawPrimitivesIndirectEXT`/`DrawIndexedPrimitivesIndirectEXT` on the
renderer boundary so a draw can read its counts out of GPU memory; `MOD-2091` made the GPU culler
`atomicAdd` into **the indirect command's own `InstanceCount` word**, so the same atomic that
reserves an instance's slot is what tells the draw how much to draw; `MOD-2095` gave the particle
system the same shape. In all three the honest measure is not "it is faster" — nothing here was
benchmarked against a readback — but that the readback is *gone from the frame path*, and each class
names the one method that still stalls (`readVisibleCountEXT`, `readParticlesEXT`) so it cannot be
reached for by accident.

**The requirement that keeps appearing, and that nobody expects.** A compute shader cannot write a
vertex buffer in this profile, so anything the GPU decides has to reach the draw through a storage
buffer the **vertex shader** reads. GL ES 3.1 permits `GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS` to be
**zero** — a device can implement compute in full and still refuse an SSBO in a vertex stage — so it
is a separate probe (`GetMaxVertexShaderStorageBlocksEXT`), and both `GpuInstanceCuller` and
`ParticleSystem` list it among their requirements rather than assuming compute implies it.

**Two of the rows fall back and two refuse, and the difference is not taste.** `ParticleSystem`
falls back to a CPU simulation because that produces the same particles more slowly — a device
without compute gets a correct effect. `GpuInstanceCuller` refuses, because there is no CPU
equivalent of "the draw call itself came from the GPU": a silent fallback would report success for a
frame that never removed the stall. `MOD-2092` is the same reasoning at the display: **no CNA
platform back end offers an HDR swap chain, so every renderer answers `Srgb` and refuses anything
else**, and that refusal is the deliverable. A renderer that accepted the request without
reconfiguring a swap chain would have its caller encode for a display that is not there, and
PQ-encoded pixels shown as sRGB are washed out and grey — a worse frame than SDR that is simply
correct.

**Three rows in 20.10 are pinned by an identity rather than by an effect**, which turned out to be
the assertion that matters most in each: `SpatialUpscalePass` at a 1:1 scale, `HdrDisplayOutput` in
`Srgb`, and (differently) a decal box that reaches neither surface. All three are `EXPECT_EQ` per
channel, not a tolerance. The reason is the same each time — a pass with nothing to do that changed
the image anyway cannot be left in a chain, and a resolution or display dial cannot be calibrated
against a frame the pass did not touch.

**The GLSL-and-C++ pattern named below claimed two more.** `HdrDisplayOutput`'s first agreement test
passed for the wrong reason: scRGB scales by `paperWhite/80`, so at 80-nit paper white the encoding
*is* the identity and the comparison would have held for a pass that did nothing at all. The guard
that caught it — "did the pass change anything?" — is now in the test beside the agreement. And
`ParticleSystem`'s GLSL deliberately writes `1.0 + (cos(a) - 1.0) * u` rather than
`mix(1.0, cos(a), u)`, because `mix` is `x*(1-a) + y*a`, a different float expression; with that one
line aligned, the two simulations agree on spawn values bit for bit and only the integration can
drift.

Two rows in the closed sections carry a bound rather than a tick, and both bounds are the same
shape — **the engine layer cannot put code in `PbrEffect`**. `PbrEffect` owns no shader source: it
fills a `GpuDrawParams` and the *renderer* generates the program, so a light loop or an area-light
term there would be a change to EasyGL's built-in effect family, compiled into every game whether
`CNA_CNAEXT` is on or off. Clustered shading (`MOD-2045`) and area lights (`MOD-2062`) are therefore
delivered in `ClusteredForwardEffect`, the layer's own `ShaderEffect`-based PBR effect. A game using
it gives up `PbrEffect`'s texture set and its one shadowed punctual light, and gains the light
count and the area lights. **Anything else in Phase 20 that the plan words as "in `PbrEffect`" will
hit the same wall** — `MOD-2070`–`MOD-2074` did, and `MOD-2082` is worded that way today.

**Section 20.8 met it and produced a second rule.** `PbrMaterial` is *lossless* against `PbrEffect`
by design (Phase 13), so a field for a lobe `PbrEffect` cannot shade would be silently dropped by
its round trip. The extensions therefore live in a **separate** `PbrMaterialExtensions` carried
beside a material rather than inside one, and `ClusteredForwardEffect` — which owns its shader —
consumes them. `MOD-2082` will want the same shape for probe lighting.

**Section 20.9 refused two of its own rows, and both refusals are arithmetic rather than
preference.** Lightmaps need a UV unwrapper, an atlas packer and a bake that rasterises into UV
space — three mesh-processing problems a runtime does not gain by adding code, and glTF assets in
the wild almost never ship a lightmap UV to read. Voxel cone tracing needs image stores into a 3D
texture, which `MOD-1514` established GL ES refuses; the slice-atlas workaround that saved froxel
fog does not scale, because a usable 128³ volume is a 128×16384 atlas and cone tracing needs
mipmapped 3D filtering a slice atlas cannot provide. The probe volume stands in their place and the
difference is stated: it cannot produce a glossy bounce or a sharp indirect shadow, and it *can*
light moving objects, which a lightmap never could.

**A pattern worth naming, because six rows in a row hit it.** Every lobe added in 20.7 and 20.8 was
written twice — once in GLSL and once in C++ — and every one was compared against the other on the
GPU rather than against a screenshot. That is what found: the area light's tube seen edge-on, a NaN
at exactly normal incidence, a BRDF table read on the texture's wrap seam, a NaN when a light sits
exactly behind a surface, an `isfinite` guard that let cgltf's `FLT_MAX` sentinel through, and a
zero-thickness thin film that was not quite the material without it. **None of those produced a
broken frame.** Every one produced a plausible one.

**`MOD-2035` is closed — and this ledger has to record that it was closed wrongly first.** Earlier
in the same session it was marked done on the strength of a measurement showing the format
bisection no longer reproduced. **That measurement did not survive.** It reverted on the same
machine with the same code, `CNAEXT_Showcase` check E went back to 0, and re-running the comparison
now gives the original result every time. The bisection four earlier measurements supported was
right, and the one contradicting it was the anomaly.

The lesson is worth more than the row: **a measurement that contradicts four earlier ones is a
reason to measure again, not a reason to close a ticket.** Two runs agreeing is not two independent
confirmations when both come from the same machine minutes apart.

The fix is to stop using the format that fails. `DepthNormalPrepass` now packs depth into an 8-bit
target on every renderer: SSAO from the real prepass occludes **0 pixels of 16384 with half-float
depth and 2101 with packed**, and check E goes from **0 strongly-occluded pixels to 1022**. Packing
is not settling for a workaround — this class's own documentation already called it the *more*
precise encoding, and it needs no capability, so it removes a per-renderer branch.

**The mechanism is no longer open** (2026-08-20, `HalfFloatDepthMechanismTests`). It was found by
building the failing shape instead of reducing towards it — the real `DepthNormalPrepass` over real
perspective geometry — and then removing one thing at a time:

| Ruled out | Measurement |
|---|---|
| the stored values | both encodings hold 50 distinct depths spanning 24..255, identical |
| the format's precision | the half-float image's **own values**, repacked into the packed layout, darken 644 pixels |
| the binding | `textureSize` names the right texture; the two prepasses are different sizes on purpose |
| the comparison itself | the identical `<= 0.0` test passes on all 1024 texels in a shader without the rest of the estimator around it |

What is left is the **sky early-out every screen-space pass in this layer opens with**. Cut
`if (centerDepth <= 0.0) { …; return; }` out of the *shipped* estimator's emitted source and change
nothing else, and the half-float image goes from **0 darkened pixels to 669**; the packed one is 644
either way. The early-out is taken on every pixel although every value in the image is positive.

It is a shader-compiler defect, not a CNA logic error, and it explains the original symptom exactly:
every screen-space pass here begins with that guard on that read, so they all went blank at once.
The packing policy stays, now for a stated reason rather than an unexplained measurement — the layer
cannot work around this without deleting a guard every pass needs.

**The audit that follows is clean, and the finding refuses to become a rule.**
`FloatTargetEarlyOutTests` asks the obvious next question — the layer reads `HdrBlendable` targets
everywhere, so is any *other* pass silently doing nothing? Of the sixteen early-outs in this layer's
shaders exactly one guards on a value sampled from a float target, FXAA's over the HDR scene image,
and it filters identically with and without that block on all four formats (1995 pixels changed in
every case). The rest guard on packed depth, on uniforms, or on the `Color` velocity image.

Then the rule was attempted and failed. *Do not guard before a sampling loop on a float target* is
the natural generalisation, and a shader built to exactly that shape behaves identically with and
without the guard on `Color`, `HdrBlendable`, `Vector4` and `HalfSingle`. Two structural
descriptions have now been tried — FXAA's guard-without-loop and this one's guard-with-loop — and
neither is the trigger. The only thing that reproduces every time is the shipped estimator over a
real half-float prepass depth image. That is why the layer's answer is a policy about where depth
lives rather than a convention about how to write a shader, and the failed generalisation is kept as
a test so nobody has to try it a second time.

**One more thing worth keeping, because it nearly produced a second wrong closure.** A hand-written
replica of the estimator reproduced the collapse, and then *stopped* reproducing it when unrelated
arithmetic was simplified out of the same shader. Had the write-up been done at that moment it would
have claimed a clean one-line cause. The final experiment therefore operates on the production
string — `SsaoPass::getOcclusionGlsl` with one block erased — rather than on a copy, and the test
asserts the *invariant* (where the defect is present, the layer must be packing) rather than the
defect. `HalfFloatDepthSamplingTests` is kept, and its header now says why it could never have found
this: it has no early-out, and building upwards from a fullscreen fill would only have reached one
by accident.

Two real bugs surfaced on the way, both fixed, and both of the kind this phase keeps producing —
they made plausible frames. **`SsaoPass` read the depth channel raw** instead of decoding it, so on
every renderer without half-float render targets it had been comparing the top byte of a packed
value against itself. And the packed/half-float decision was **re-derived independently in seven
places**; it is single-sourced now, without which this change would have left six passes decoding an
encoding the prepass had stopped writing. Seventeen tests across eight files hand-built depth images
in the old encoding and now go through one shared helper.

The showcase fix from the wrong closure stands and is unaffected: `drawScene()` really did replace
the prepass program, so the "depth" target held the shaded frame's red channel. Both defects were
real and independent — only one of them was the whole story.

**`MOD-2033` is closed too, which makes Phase 20 complete and every `MOD-*` row from `MOD-1` to
`MOD-2099` carry a verdict.** The row was right that per-object velocity is an obligation on the
application rather than a pass — so what was built is the *expression* of that obligation, not a
switch that improves motion blur on its own. `DepthNormalPrepass::setVelocityEnabledEXT` is off by
default; with it on, the app supplies a previous world matrix per draw and a previous camera per
frame, and `docs/cnaext-engine-layer.md` no longer has to say that a car crossing a static shot does
not blur. The cost `MOD-2030` declined to pay is paid explicitly and asserted: with MRT it is a third
target in one pass, and without MRT the two-pass fallback really does become three.

Two decisions there are worth carrying forward because both are the kind that produce a plausible
wrong frame. The velocity target's **alpha flag is inverted** — below 0.5 means "written" — because
the MRT path issues a single clear for the whole bound set and depth must clear to white; a second
clear would mean re-binding a discard-contents target, which is not safe. And both clip positions
reach the fragment stage **undivided**, because the perspective divide is not affine and
interpolating divided values puts a large triangle's velocity in the wrong place everywhere except
at its vertices. The tests hold the camera identical in both frames throughout, which removes the
camera-reprojection path from every answer and is asserted in both directions: the object smear
appears, *and* the camera-only run is pixel-for-pixel the unblurred source.
A third bound is recorded in `MOD-2090`'s row rather than left as a surprise: CNA's only indirect
argument buffer is a `StorageBuffer`, which is an SSBO and needs ES 3.1 / GL 4.3, while the indirect
draw itself needs only GL 4.0 — so on a desktop context between 4.0 and 4.2 the capability
truthfully reports `true` and there is still no CNA type able to hold the arguments. Reporting a
higher floor to hide that would be the wrong repair.

**Three sections in a row shipped with a bug the tests caught and a test that was itself wrong**,
which is worth stating plainly: light shafts measured a black occluder against black, volumetric fog
computed the backward phase lobe and then asserted the frame got *brighter*, and the atmospheric sky
summed two path lengths that do opposite jobs while its tests placed the sun where the question it
was asking could not be true. All six produced frames that looked like what they claimed to be. The
lesson these rows keep repeating is that a plausible frame is not evidence, and that when a test
fails the model is not automatically the thing that is wrong.

The whole orchestration layer exists and is verified on EasyGL: `RenderPipeline`, the post-process
chain, all four shadow types, skybox and IBL, materials, instancing/LOD/culling, compute and
auto-exposure — and, since Phase 20, clustered lighting, volumetrics, area lights, the glTF material
extensions, probe-based indirect light, indirect draws, GPU culling, particles, decals, spatial
upscaling, display-space encoding and per-object velocity. Current sweep (2026-08-20, after Phase
20): **8294 ran · 8229 pass · 65 skip · 0 fail**, with every `CNAEXT_` gate green for the first time
(26/26). Phase 19's own final sweep, which the rest of §3 is written against, was 7944 ran · 7880
pass · 64 skip · 0 fail. See
`docs/cnaext-engine-layer.md` for the capability boundary per subsystem and per renderer, and §3
below for the measured baselines including the twenty-renderer table (`MOD-1906`).

*(This paragraph said "Phase 0 — in progress, nothing of the orchestration layer exists yet" for
long after that stopped being true, while the table immediately below it listed fifteen completed
phases. A summary that contradicts its own table is worse than no summary, so it is dated now.)*

| Done | Task |
|---|---|
| ✅ | `MOD-1` — `CNA/Graphics/CNAEXT.hpp` master include + `CnaExtMasterIncludeTests` |
| ✅ | `MOD-2` — `cnaext` CMake configure/build preset |
| ✅ | `MOD-3` — `scripts/check_cnaext_guards.sh` |
| ✅ | `MOD-4` — `docs/cnaext-engine-layer.md` |
| ✅ | `MOD-14` — this file |
| ✅ | `MOD-100`/`101`/`103`/`104` — float render-target capabilities, derived from a per-format renderer verdict |
| ✅ | `MOD-115`/`116`/`117` — EasyGL really allocates RGBA16F/RGBA32F targets, probed at runtime |
| ✅ | `MOD-105`, `MOD-108`/`124`, `MOD-131` — unsupported formats refused, format-aware readback, and the end-to-end proof that values above 1.0 survive |
| ⛔ | `MOD-106` — lenient-substitution opt-out, dropped (it would preserve a behaviour that never existed) |
| ✅ | `MOD-107` — `RenderTargetCube` carries its `SurfaceFormat` to the renderer too (the path IBL needs) |
| ✅ | `MOD-118`–`MOD-123`, `MOD-125` — completeness diagnostic, depth/MSAA/mip/MRT on float targets, half-float filtering query |
| ✅ | `MOD-21`/`MOD-22` — `Uncharted2` and the settings fields the passes read |
| ✅ | Phase 2 core — `PostProcessContext`, `FullscreenPass`, `PostProcessPass`, `RenderTargetPool`, `BlitPass`, `PostProcessChain` (`MOD-16`, `19`, `200`–`208`, `225`–`228`) |
| ✅ | Phase 3 — `TonemapPass`, all five operators, shader verified against a CPU reference (`MOD-300`–`313`, `317`) |
| ✅ | Phase 7 core — `RenderPipeline`, the consumer `RenderPipelineSettings` never had (`MOD-700`–`737`) |
| ✅ | Phase 4 — `BloomPass`, wired ahead of tonemapping (`MOD-400`–`418`) |
| ✅ | Phase 6 — `FxaaPass`, wired after tonemapping (`MOD-600`–`606`) |
| ✅ | Phase 5 — `SsaoPass` and its pipeline wiring (`MOD-505`/`506`/`515`–`524`, `MOD-711`); depth and normals are caller-supplied |
| ✅ | **Phase 8 — directional shadows, complete end to end** (`MOD-800`–`861`, all but `MOD-854`) |
| ✅ | **Phase 9 — cascaded shadow maps, complete** (`MOD-900`–`917`, 18/18) |
| ✅ | **Phase 10 — point and spot shadows, complete** (`MOD-1000`–`1012`, 13/13) |
| ✅ | **Phase 11 — skybox, complete** (`MOD-1100`–`1116`, 17/17) |
| ✅ | **Phase 12 — image-based lighting, complete** (`MOD-1200`–`1248`; 4 rows ⛔ with reasons, 1 🟨) |
| ✅ | **Phase 13 — material system reconciliation, complete** (`MOD-1300`–`1315`, 16/16) |
| ✅ | **Phase 14 — instancing, LOD and culling, complete** (`MOD-1400`–`1414`, 15/15) |
| ✅ | **Phase 15 — compute shaders and storage buffers, complete** (`MOD-1500`–`1555`; 2 rows 🟨, 1 ⛔ with its measurement) |

**The HDR spine is complete and verified end to end.** A game can wrap its draw calls in
`RenderPipeline::begin`/`end`, enable HDR, bloom, a tonemapping operator and FXAA, and get them --
on EasyGL, in software, under Xvfb. With everything off it allocates nothing and renders exactly
what it would have rendered without a pipeline.

**Every post-process subsystem in the plan now exists and is wired into the pipeline**: SSAO,
bloom, tonemapping and FXAA, in that fixed order, each with a reason for its position.

**Shadows are visible.** `ShadowMap` generates the map (rigid and skinned casters), the four lit
effects carry the state through `IShadowReceiverEXT`, and every lit EasyGL program samples it
through a shared 3x3/5x5 PCF snippet. `RenderPipeline::setShadowScene` runs the pass at the top of
`begin()`, before the scene target is bound. The pieces worth remembering:

- The map holds **light-space distance, not depth** — CNA cannot sample a depth attachment as a
  texture on every renderer. `Single` (R32F) where the renderer has one, `Color` otherwise.
- **No V flip** when sampling it, unlike the XNA sample `easygl_shadowmapping_*` ports: a CNA
  render target's texel memory already matches the clip space it was rendered in. Pinned by moving
  the caster off centre along each axis and comparing the shadow centroid against the camera
  matrices alone (`ShadowVisibilityTest.TheShadowLandsWhereTheCasterIs`).
- Shadow attenuates **direct light only** — never ambient, never PBR's IBL term.
- A receiving draw is forced onto the **per-pixel** program whatever `PreferPerPixelLighting` says;
  a shadow evaluated at four corners and interpolated is a gradient, not a shadow.
- **Bias evidence** (`ShadowVisibilityTest.TheDefaultBiasSitsBetweenAcneAndPeterPanning`, printed):
  self-shadowed area 0.549 at bias 0, 0.093 at the default 0.0015, 0.000 at 0.2.
- **Shadow pass cost** (`cnaext_shadowmap_test --benchmark`, 12 casting triangles, Mesa llvmpipe):
  Low 0.10 ms, Medium 0.12 ms, High 0.20 ms, Ultra 0.52 ms. Independent of screen resolution.
- `MOD-854` (a skinned character self-shadowing golden) is **deliberately deferred**: a skinned quad
  with one identity bone proves the shader path, not self-shadowing, which needs a real animated
  mesh from the glTF fixtures.

**Cascades work too.** `CascadedShadowMap` splits the camera frustum 2-4 ways, fits each slice
sphere-based (rotation-stable) and texel-snapped (translation-stable), stores them in **one atlas**
rather than a texture array, and the same shared shader path samples them. `applyToReceiver` hands
an effect everything at once, because these values are only meaningful together. Facts worth
keeping:

- **XNA projection matrices are the Direct3D ones, so NDC z runs 0..1**, not -1..1. Assuming GL
  there put the "near" frustum corners half way to the camera.
- A cascade atlas must be allocated `RenderTargetUsage::PreserveContents`. With the default,
  binding it for cascade 1 discards cascade 0, and the finished atlas holds only the last cascade.
- Per-cascade frustum corners scale the **near** corners by `depth/near`. Scaling the far corners
  applies that ratio twice and fits each cascade to a volume tens of times too large -- every
  cascade then comes out covered edge to edge by the first caster.
- The PCF texel step is a **vec2**: an atlas is N times wider than tall.
- **Cost** (`cnaext_csm_test --benchmark`, 6 casting triangles, Mesa llvmpipe): single Medium map
  0.12 ms, 2 cascades 0.20, 3 cascades 0.49, 4 cascades 0.43 per frame.

**Punctual lights work too**, and they needed more than a shadow: XNA's lit effects carry three
*directional* lights and nothing else, so a point light's shadow had nothing to attenuate. The four
lit effects therefore gained a punctual lighting term (`PunctualLightEXT`, always compiled) and its
cube/spot lookup. Facts worth keeping:

- Both punctual maps store **distance from the light over its range**, not projected depth. A cube
  face's projected depth is defined by that face's own projection; distance is face-independent, so
  the receiver samples the cube by direction and compares directly. The range used to light must be
  the range the map was generated with.
- A cube render target needs `PreserveContents`, same trap as the cascade atlas.
- The cube face size is **capped at 1024** whatever the quality asks: six faces at 4096 is a hundred
  million texels for one light.
- The spot PCF needs **its own texel size**. Borrowing the directional map's meant a draw with no
  sun attached filtered with a texel of 1.0, clamped every tap to a corner, and produced a spot
  shadow that silently never appeared.
- **Cost** (`cnaext_pointshadow_test --benchmark`, 2 casting triangles, Mesa llvmpipe): directional
  1 map 0.05 ms, spot 1 map 0.04 ms, **point 6 faces 5.42 ms** -- a hundred times a single map, not
  six, because each face rebinds a cube attachment and clears it. That ratio is why point shadows
  are opt-in per light.

**One earlier defect fixed on the way** (`MOD-520`): SSAO produced *no* occlusion at all on this
container's Mesa build, at every radius, silently. The occlusion test offset the comparison depth by
the sample's z times `uRadius` -- a view-space formulation in a pass that has no view space, where
`uRadius` is a UV offset and the depths are a normalized texture. The offset swamped the difference
it was compared against. Only a narrow band of radii made both terms work at once, which is why it
had passed before. `AHigherIntensityDarkensMore` used `EXPECT_LE` and so passed throughout; it is
strict now.

**The sky and the environment are in.** `Skybox` draws an environment cube in one fullscreen pass;
`EnvironmentProcessor` converts an equirectangular panorama to a cube and convolves that cube into
the three split-sum products; `ImageBasedLightEXT` carries them to `PbrEffect`/`SkinnedPbrEffect`,
and EasyGL's two PBR programs light with them. Four decisions worth remembering:

- **The precompute is CPU-side.** A render-to-cube version needs float render targets, cube render
  targets and custom effects present *at once*, which no renderer in the committed scope offers
  together. On the CPU the *arithmetic* needs no capability gate and is seamless by construction
  (the sampler picks the face from the direction). It costs 6.2 s in a Debug build for a full set —
  load-time work, stated plainly in the docs rather than hidden. **Corrected in Phase 16.6:** this
  entry used to say it "works everywhere including Headless". It does not. The results still have to
  be written into a `TextureCube`, and Headless and Stub accept that call and store nothing, so the
  generators throw there. Measuring beat assuming.
- **`ImageBasedLightEXT` is in the XNA namespace**, not `CNA::Graphics` — an always-compiled effect
  surface cannot include a `CNA_CNAEXT`-only header. This overrides §OQ-4's recorded answer; see
  `MOD-1222` for why the third option beat both listed ones.
- **Flat ambient and IBL are exclusive.** `FillGpuDrawParams` zeroes `ambientColor` when a valid
  bundle is bound, so a renderer that ignores the IBL group renders an unlit ambient rather than a
  double-counted one.
- **Everything is 8-bit.** `Texture::ValidateFormat` admits `SurfaceFormat::Color` only, so the
  BRDF table is quantised and an environment brighter than 1.0 carries its brightness in
  `Intensity` instead of in its texels. `MOD-1208`'s two-format plan had nothing to choose between.

The white furnace (half intensity, albedo 1, exact = 128/255) measures 159/139/129/155 across
roughness 0.1/0.4/0.7/1.0 — a small energy gain at both ends, near-exact in the middle. Per-frame
cost is 0.064 ms flat-ambient against 0.066 ms image-based.

**`PbrMaterial` is now a lossless description of `PbrEffect`**, which it was not: it described a
subset and dropped the rest, so it could not be the serialization form it was meant to be.
`applyMaterial` / `extractMaterial` round-trip every field exactly (asserted field by field, and
across all 256 values of the one field that changes representation), `applyMaterialState` applies
the blending and culling a material implies, and `materialFromGltfEXT` builds one from the glTF
importer's own decoded record. What is worth remembering:

- **Emissive became a `Vector3` and alpha coverage became `AlphaModeEXT`** — the existing XNA-layer
  enumeration, not a new `CNA::Graphics::AlphaMode`; a second enum would have met the first in a
  conversion function nobody could delete.
- **Defaults moved to glTF's** (metallic 1, roughness 1, from 0 and 0.5) so that applying a default
  material to a default effect is genuinely a no-op — which is now asserted.
- **The glTF bridge is a template over a concept**, so `graphics-ext` links neither the content
  module nor `cgltf`. Both sides test it: a stand-in source here, the importer's real `MaterialOut`
  there, with a `static_assert` pinning the agreement.
- **The C ABI was not broken.** `docs/c-api/ABI_VERSIONING.md` forbids changing an existing name's
  meaning within a major, so `CNA_PbrMaterial` is frozen exactly as published and the current shape
  arrived as `CNA_PbrMaterialEXT` + `cna_pbr_material_ext_init`. The recorded baseline reports only
  additions, which the policy permits.
- **One value quantises**: glTF's float `baseColorFactor` becomes the material's 8-bit albedo. The
  two draw paths are otherwise identical, asserted at ≤1/255. Making the albedo factor a `Vector4`
  would close it and is an owner decision, because the C mirror is `Color`-shaped too.

**Ten thousand objects now cost one draw call.** `InstancedRendererEXT` owns the per-instance
transform stream (four `Vector4`s at `TextureCoordinate` 1-4, the layout the renderers already
bind to the stock shaders), `FrustumCullerEXT` removes what the camera cannot see before any of it
is uploaded, and `LodGroupEXT` picks a level by distance or by projected pixel size. Measured on
llvmpipe: 1 000 cubes instanced 0.96 ms against 51.5 ms looped (54x), 10 000 cubes 22.7 ms against
538 ms (24x) -- and the culled frame is pixel-identical to the unculled one.

Two decisions worth remembering: the per-instance fallback is **opt-in**, because one draw call per
instance is a different program rather than a slower one; and `LodGroupEXT` orders its levels
finest first, which is ascending distance in one mode and *descending* pixel size in the other, so
changing mode re-sorts.

**Compute is real, and it runs here.** `IComputeShaderRenderer`/`IStorageBufferRenderer` sit
beside the other renderer interfaces with inert defaults, EasyGL implements them against the
*runtime* context version, and `CNA::Graphics::ComputeShader`/`StorageBuffer` are the engine-layer
face. Verified on Mesa llvmpipe's ES 3.2 context, not by inspection: 1024 floats doubled, a 1 MB
buffer round-tripped byte-exact, GPU frustum culling agreeing with `FrustumCullerEXT` on 625 boxes
with zero disagreements, and 100 000 particles integrating at 0.881 ms against 2.401 ms on the CPU
with an exact match.

`AutoExposureEXT` is the first consumer inside the engine layer, and closes `MOD-308`: a log-average
luminance reduction in shared memory, asymmetric adaptation, one line to apply to the pipeline.

Three limits found and written down rather than worked around:

- **Image bindings are desktop-GL only.** GL ES needs an immutable texture (`glTexStorage2D`) and
  CNA allocates mutably, so `ComputeShader::bindImage` refuses with the reason instead of issuing a
  binding the driver drops. Making CNA's textures immutable is a change to the path every draw goes
  through — an owner decision, not a side effect of this phase.
- **A storage buffer cannot be bound as a vertex stream**, so GPU-resident particles come back
  through the CPU at 0.806 ms per 1.6 MB. That number is what a `StorageBuffer`/`VertexBuffer`
  aliasing API would save.
- **`Texture2D::GetData` never shows compute writes** — it answers from the CPU shadow copy.

**The second renderer changed what the layer knows about itself.** Running everything against a
real Vulkan device measured five Phase 16 rows and produced one addition that was not optional:
`GraphicsCapability::CustomEffects` says a renderer can compile *some* custom effect, not that it
takes this layer's shader language — the Vulkan renderer's `ShaderEffect` consumes SPIR-V bytecode
while every pass and caster here hands it GLSL source. Before `MOD-1699` added
`SupportsShadowSamplingEXT()` and `SupportsImageBasedLightingEXT()`, the shadow example on Vulkan
did not fail, it **crashed**: the caster's effect silently failed to compile and the draw went ahead
with no effect applied. Six examples and three test suites now SKIP there with a reason.

The eight Vulkan failures in the row above are pre-existing renderer issues, not engine-layer ones —
two custom-GLSL `Cnj` tests (the same SPIR-V mismatch), one stdout diagnostic, and five
`IndexedDrawDeferred` cases. No Vulkan renderer code was touched this session.

Next: the rest of Phase 16 (the other renderers, and implementing rather than measuring Vulkan),
then what remains of 17-19.

Smaller open rows: `MOD-203` (restore-on-exception around a pass), `MOD-209`/`MOD-210`,
`MOD-405`/`407`/`409`/`413`/`415`–`417` (bloom quality presets, perf, goldens),
`MOD-314`–`316`/`318`–`320` (tonemap goldens, example, docs), `MOD-501`–`504` (a prepass helper —
SSAO takes the textures directly, so this is convenience rather than capability), `MOD-5`, and
Phase 1's `MOD-130`–`MOD-141`.

**Owner decisions in force** (asked 2026-08-17): start with the HDR spine; EasyGL is the
reference renderer with Vulkan and D3D11 as the committed follow-ups and the rest opportunistic;
verify every task in a real `cmake-build-cnaext` build; one task = one commit, pushed as it lands.

---

## 2. Build environment (what a fresh container needs)

A fresh clone cannot configure CNA without these. Recorded here because the first session spent
real time rediscovering them one CMake error at a time.

```bash
# 1. Vendored submodules (non-recursive is sufficient and much faster)
git submodule update --init vendor/googletest third_party/SDL third_party/SDL_image third_party/SDL_mixer

# 2. Sibling checkouts CNA expects next to its own directory (NOT submodules)
cd .. && git clone https://github.com/openeggbert/sharp-runtime.git
        git clone -b develop https://github.com/openeggbert/easy-gl.git
        git clone -b develop https://github.com/openeggbert/meta-gl.git

# 3. System packages (Debian/Ubuntu). SDL3 is built from source at configure time and needs
#    the full X11 set. FFmpeg is optional: this example installs it for VideoPlayer; pass
#    -DCNA_ENABLE_VIDEO=OFF and omit the four libav*/libswresample packages for an audio-only game.
apt-get install -y \
  libxcursor-dev libxi-dev libxrandr-dev libxss-dev libxkbcommon-dev libwayland-dev \
  libdecor-0-dev libxtst-dev libxext-dev libxfixes-dev libxinerama-dev libdrm-dev libgbm-dev \
  libibus-1.0-dev libasound2-dev libpulse-dev libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswresample-dev ccache

# 4. Configure + build (first configure builds SDL3 into .sdl-prebuilt-<system>-<arch>/, ~minutes;
#    the first CnaTests build is ~1055 targets)
cmake --preset cnaext
cmake --build --preset cnaext --target CnaTests -j$(nproc)
SDL_AUDIODRIVER=dummy ./cmake-build-cnaext/CnaTests --gtest_filter='CnaExt*'
```

Notes:

- `CNA_BUILD_EXAMPLES` is OFF in the preset; turn it on when a task needs the module's demos.
- `ccache` is picked up automatically (`CNA_USE_CCACHE=ON`); without it installed, a build tree
  configured with `CMAKE_CXX_COMPILER_LAUNCHER=ccache` fails with `/bin/sh: 1: ccache: not found`.
- Draco is absent here, so Draco-compressed glTF primitives throw at import time by design.

---

## 3. Test baselines

Recorded so "no regressions" is checkable rather than asserted. Update at each phase boundary
(`MOD-1711`).

| Date | Build | Suite | Result |
|---|---|---|---|
| 2026-08-17 | `cmake-build-cnaext` (OPENGLES3, `CNA_CNAEXT=ON`, Debug), base `origin/next` @ `05a9eab0` | full `CnaTests`, from the repo root, under Xvfb (`MOD-1710`) | **7548 ran · 7484 pass · 64 skip · 0 fail** |
| 2026-08-17 | same, with `MOD-105`/`108`/`124`/`131` applied | same | 7565 ran · 7501 pass · 64 skip · **0 fail** |
| 2026-08-17 | same, with `MOD-107` applied | same | 7567 ran · 7503 pass · 64 skip · **0 fail** |
| 2026-08-17 | same, through Phases 2/3/4/6/7 (tonemap, bloom, FXAA, pipeline) | same | 7630 ran · 7566 pass · 64 skip · **0 fail** |
| 2026-08-17 | `cmake-build-debug` — the same branch with **`CNA_CNAEXT=OFF`** (the default) | same | 7544 ran · 7480 pass · 64 skip · **0 fail** |
| 2026-08-17 | `cmake-build-cnaext`, with SSAO added | same | 7640 ran · 7576 pass · 64 skip · **0 fail** |
| 2026-08-17 | same, with shadow generation and reception | same | 7659 ran · 7595 pass · 64 skip · **0 fail** |
| 2026-08-17 | same, with all of Phase 8 (visible shadows, skinned casters, pipeline integration) | same | 7679 ran · 7615 pass · 64 skip · **0 fail** |
| 2026-08-17 | same, with all of Phase 9 (cascaded shadow maps) | same | 7708 ran · 7644 pass · 64 skip · **0 fail** |
| 2026-08-17 | same, with all of Phase 10 (point and spot shadows) and the SSAO fix | same | 7730 ran · 7666 pass · 64 skip · **0 fail** |
| 2026-08-18 | same, with Phase 11 (skybox) and Phase 12.1 (the IBL precompute) | same | 7763 ran · 7699 pass · 64 skip · **0 fail** |
| 2026-08-18 | same, with all of Phase 12 (IBL consumption, shader and tests) | same | 7769 ran · 7705 pass · 64 skip · **0 fail** |
| 2026-08-18 | same, with all of Phase 13 (the material reconciliation) | same | 7787 ran · 7723 pass · 64 skip · **0 fail** |
| 2026-08-18 | same, with all of Phase 14 (instancing, LOD and culling) | same | 7806 ran · 7742 pass · 64 skip · **0 fail** |
| 2026-08-18 | same, with all of Phase 15 (compute, storage buffers, auto-exposure) | same | 7824 ran · 7758 pass · 66 skip · **0 fail** |
| 2026-08-18 | `cmake-build-debug` — **`CNA_CNAEXT=OFF`** again, after Phases 11-15 (`MOD-1709`) | same | 7556 ran · 7492 pass · 64 skip · **0 fail** |
| 2026-08-18 | `cmake-build-cnaext`, with the Phase 17/18 verification and documentation work | same | 7829 ran · 7765 pass · 64 skip · **0 fail** |
| 2026-08-18 | **`cmake-build-vulkan`** — the same branch on a real Vulkan device (Mesa lavapipe 1.4) | Xvfb :99 | 7824 ran · 7652 pass · 164 skip · **8 fail**, none of them the engine layer |
| 2026-08-18 | **`cmake-build-multi`** (`CNA_GRAPHICS_RENDERERS="HEADLESS;SOFTWARE;STUB"`), default HEADLESS — Phase 16.6 | same | 7814 ran · 7419 pass · 395 skip · **0 fail** |
| 2026-08-18 | same binary, `CNA_GRAPHICS_RENDERER=SOFTWARE` | same | 7814 ran · 7499 pass · 302 skip · **13 fail**, none in the engine layer |
| 2026-08-18 | same binary, `CNA_GRAPHICS_RENDERER=STUB` | same | 7814 ran · 7213 pass · 572 skip · **29 fail**, none in the engine layer |
| 2026-08-18 | `cmake-build-cnaext` (EasyGL) re-verified after the Phase 16.6 probes and the shader-execution sweep | Xvfb :99 | 7829 ran · 7765 pass · 64 skip · **0 fail** |
| 2026-08-18 | `cmake-build-debug` — **`CNA_CNAEXT=OFF`** re-verified after the same | Xvfb :99 | 7556 ran · 7494 pass · 62 skip · **0 fail** |
| 2026-08-18 | `cmake-build-cnaext`, after closing Phases 0–3 (the rows those phases had left open) | Xvfb :99 | 7879 ran · 7815 pass · 64 skip · **0 fail** |
| 2026-08-18 | `cmake-build-cnaext`, after the ASan pass and the device-lifecycle tests (`MOD-743`, `MOD-1708`, `MOD-1714`, `MOD-1715`) | Xvfb :99 | 7940 ran · 7876 pass · 64 skip · **0 fail** |
| 2026-08-18 | **`cmake-build-cnaext-release`** — the same tree at `-DCMAKE_BUILD_TYPE=Release` (`MOD-1716`) | Xvfb :99 | 7940 ran · 7876 pass · 64 skip · **0 fail** |
| 2026-08-18 | `cmake-build-cnaext`, after Phase 19's API review and renames (`MOD-1900`, `MOD-1902`, `MOD-1742`) | Xvfb :99 | 7942 ran · 7878 pass · 64 skip · **0 fail** |
| 2026-08-19 | `cmake-build-debug` — **`CNA_CNAEXT=OFF`**, re-verified after the whole Phase 16 sweep and the shared-test-file changes it needed | Xvfb :99 | 7557 ran · 7495 pass · 62 skip · **0 fail** |
| 2026-08-19 | **`cmake-build-d3d11`** — MinGW-w64 cross-build, run under Wine on a real D3D11 device (`MOD-1624`) | Xvfb :99 + Wine 9.0 | 488 ran · 402 pass · 86 skip · **0 fail** |
| 2026-08-19 | **`cmake-build-d3d12`** — MinGW-w64 cross-build, run under Wine on a real D3D12 device (`MOD-1625`) | Xvfb :99 + Wine 9.0 | 426 selected · 323 pass · 86 skip · **0 fail**, 17 crash the process — see below |
| 2026-08-19 | `cmake-build-cnaext` (EasyGL) — **the final regression sweep** (`MOD-1906`), foreground | Xvfb :99 | **7944 ran · 7880 pass · 64 skip · 0 fail** |
| 2026-08-20 | `cmake-build-cnaext`, after Phase 20 §20.10's first three rows (`MOD-2093` spatial upscaling, `MOD-2090` indirect draw) | Xvfb :99 | 8247 ran · 8182 pass · 65 skip · **0 fail** |
| 2026-08-20 | same, after `MOD-2091` (GPU culling into an indirect draw) | Xvfb :99 | 8255 ran · 8190 pass · 65 skip · **0 fail** |
| 2026-08-20 | same, after `MOD-2094` (decals) and `MOD-2095` (particles) | Xvfb :99 | 8275 ran · 8210 pass · 65 skip · **0 fail** |
| 2026-08-20 | same, after `MOD-2092` (HDR display output) — **§20.10 complete** | Xvfb :99 | 8286 ran · 8221 pass · 65 skip · **0 fail** |
| 2026-08-20 | `cmake-build-cnaext`, after closing `MOD-2035` — **every `CNAEXT_` gate green for the first time (25/25)** | Xvfb :99 | 8286 ran · 8221 pass · 65 skip · **0 fail** |
| 2026-08-20 | same, after `MOD-2033` (per-object velocity) — **Phase 20 complete**, 25/25 gates | Xvfb :99 | 8293 ran · 8228 pass · 65 skip · **0 fail** |
| 2026-08-20 | same, after `MOD-2100` (the section's benchmark example and its `CNAEXT_GpuDriven` gate) | Xvfb :99 | 8293 ran · 8228 pass · 65 skip · **0 fail**; `ctest -R 'CNAEXT_'` **26/26** |
| 2026-08-20 | same, after `MOD-2035`'s **real** fix — the prepass packs depth on every renderer, `SsaoPass` decodes it, seventeen tests rebuilt on the shared helper | Xvfb :99 | 8294 ran · 8229 pass · 65 skip · **0 fail**; `ctest -R 'CNAEXT_'` **26/26**, `CNAEXT_Showcase` 8/8 with 1022 strongly-occluded pixels |
| 2026-08-20 | **`cmake-build-cnaext-release`** — the same tree at `-DCMAKE_BUILD_TYPE=Release`, re-verified after all of Phase 20 (this is where `MOD-2035`'s regression was first seen) | Xvfb :99 | 8294 ran · 8229 pass · 65 skip · **0 fail** |
| 2026-08-20 | `cmake-build-debug` — **`CNA_CNAEXT=OFF`**, re-verified after `MOD-2035` | Xvfb :99 | 7567 ran · 7505 pass · 62 skip · **0 fail** (the networking test that timed out under load earlier passes here) |
| 2026-08-20 | `cmake-build-debug` — **`CNA_CNAEXT=OFF`**, re-verified after all of §20.10 touched `GraphicsDevice`, `IGraphicsRenderer`, `GraphicsCapability` and the EasyGL renderer | Xvfb :99 | 7567 ran · 7504 pass · 62 skip · **1 fail** — `TwoProcessLoopbackTest.HostMigration…`, which passes on its own in 715 ms and times out at 30 s under full-suite load: the fourth instance of the load-induced failures §3 already describes, and nothing to do with this work (it spawns two processes and speaks UDP) |
| 2026-08-20 | `cmake-build-cnaext`, after **Phase 21 §21.1 complete** (transparency: the sorted list, the transparent phase, weighted-blended OIT and soft particles) | Xvfb :99 | 8317 ran · 8252 pass · 65 skip · **0 fail**; `ctest -R 'CNAEXT_'` **27/27** |
| 2026-08-20 | same, after **Phase 21 §21.2 complete** (contact shadows) | Xvfb :99 | 8340 ran · 8275 pass · 65 skip · **0 fail**; `ctest -R 'CNAEXT_'` **28/28** |
| 2026-08-20 | same, after **Phase 21 §21.3 complete** (`.cube` grading, the interpolation decision, debanding dither) | Xvfb :99 | 8367 ran · 8301 pass · 66 skip · **0 fail**; `ctest -R 'CNAEXT_'` **29/29** |
| 2026-08-20 | same, after **Phase 21 §21.4 complete** (aerial perspective) | Xvfb :99 | 8381 ran · 8316 pass · 65 skip · **0 fail**; `ctest -R 'CNAEXT_'` **29/29** |
| 2026-08-20 | same, after **Phase 21 §21.5 complete** (`DebugDraw`, gizmos, `GpuTimer`, per-pass timings, the GPU-timed perf table) — **Phase 21 complete** | Xvfb :99 | 8419 ran · 8351 pass · 68 skip · **0 fail**; `ctest -R 'CNAEXT_'` **30/30** |
| 2026-08-20 | **`cmake-build-cnaext-release`** — the same tree at `-DCMAKE_BUILD_TYPE=Release`, at the Phase 21 boundary | Xvfb :99 | 8419 ran · 8351 pass · 68 skip · **0 fail** |
| 2026-08-20 | `cmake-build-debug` — **`CNA_CNAEXT=OFF`**, at the Phase 21 boundary: the layer compiles out and nothing it touched (`IGraphicsRenderer`, the EasyGL renderer, `TonemapPass`, `ColorGradePass`, `AtmosphericSky`) broke the build without it | Xvfb :99 | 7567 ran · 7505 pass · 62 skip · **0 fail** |
| 2026-08-20 | `cmake-build-cnaext`, after **`MOD-2035`'s mechanism was found** — `DepthEncoding`, the diagnostic prepass constructor, `SsaoPass::getOcclusionGlsl`, and the four-case bisection | Xvfb :99 | 8423 ran · 8355 pass · 68 skip · **0 fail**; `ctest -R 'CNAEXT_'` **30/30** |
| 2026-08-20 | same, after the `MOD-2035b` audit — `FxaaPass::getFragmentGlsl`, the format sweep, and the failed generalisation kept as a test | Xvfb :99 | 8425 ran · 8357 pass · 68 skip · **0 fail**; `ctest -R 'CNAEXT_'` **30/30** |

`ctest -R 'CNAEXT_'` through all of §20.10: **24 of 25 pass**, the exception being `CNAEXT_Showcase`,
which was `MOD-2035` and had been red since long before this section began. It is **25 of 25** after
`MOD-2035` was closed (2026-08-20) — see §1 for what the cause turned out to be.

The `CNA_CNAEXT=OFF` row is the one that answers "can this break what already works". It configures,
builds and passes with the whole engine layer compiled out. Its lower test count is expected and not
a sign of anything missing: the pre-existing `DepthEffect`, `CRTEffect` and `AsciiPostProcessEffect`
suites are themselves `CNA_CNAEXT`-guarded, so they compile away alongside the new ones. What does
*not* compile away — the capability queries, the float render-target work and the HDR round trip --
runs in both configurations.

That is a clean run, and it is new. Measuring the first baseline on this branch turned up eight
failures and a segfault at ~7300 tests, all of them pre-existing on `next` (verified by rebuilding
the branch without any engine-layer change and getting the identical set). They were fixed on
`next` itself rather than worked around here — `next` @ `05a9eab0`:

- **`PLAT-46`** — a `Game` whose construction throws never ran `~Game`, so its platform stayed
  installed process-wide while the unwind destroyed it. Every later `Keyboard::GetState` /
  `StorageDevice` / `TitleContainer` call read freed memory through the ambient accessor; that was
  the segfault. Fixed with a scope guard that undoes the installation on the failed-construction
  path only.
- **`GLTF-374`** — filling in the renderer inventories for `igl`/`pixijs` exposed that IGL bound an
  opaque-white 1×1 stand-in into the *normal-map* slot, lighting every PBR material without a
  normal map as though its pixels were tilted 55°. It now binds the flat-normal texel.
- **EasyGL diagnostics** — the GL banner and capability dump moved off `std::cout` onto the logger.
- Two `GameEventSemanticsGoldenTest` cases (only reachable once the segfault was gone) now skip
  where the parameterised platform cannot back the build's renderer.

An earlier baseline in this file recorded 6360/6351 — measured on the **develop**-based tree before
the rebase onto `next`, so it never described this branch; replaced above.

Two things about how the suite is run matter more than they look:

- **Run it from the repository root**, not from the build directory. Content/media/audio tests
  resolve fixtures like `tests/assets/xnb/...` relative to the CWD; from `cmake-build-cnaext/` that
  is 116 failures of pure path noise.
- **A real Vulkan build now exists here** (`cmake-build-vulkan`, `-DCNA_CNAEXT=ON
  -DCNA_GRAPHICS_RENDERER=VULKAN`). It needed three packages this container did not have:
  `libvulkan-dev mesa-vulkan-drivers glslang-tools`. Mesa's **lavapipe** provides a software Vulkan
  1.4 device, so the whole suite and every engine-layer example run against a second renderer for
  real. That is what turned Phase 16's Vulkan rows from guesses into measurements — and what found
  the crash described in `MOD-1699`.

- **`CNA_TEST_DISPLAY` is now `:99` in `cmake-build-cnaext`.** It defaulted to `:0`, which does not
  exist here, so every registered engine-layer ctest skipped and the label suites looked green
  without running. Reconfigured with `-DCNA_TEST_DISPLAY=:99`; `ctest -L CnaExt` now runs all nine
  for real. The examples also catch `PlatformException` and exit 77 rather than aborting, so a tree
  configured without a display still *skips* instead of failing.

- **The C API is off in `cmake-build-cnaext`, and turning it on has a catch.** Phase 13 verified
  the C mirror by configuring that tree with `-DCNA_BUILD_C_API=ON`, then turned it back off. The
  reason: `libcna_c_api_static.a`'s generator reads `CMakeFiles/<target>.dir/link.txt`, which only
  the Makefile generator writes, so under Ninja that one target fails every build and takes
  `CApi_InstalledConsumer` with it. Everything else builds and passes. Two other C API gates fail
  here for environment reasons unrelated to any change: `CApiHeaderCompatibility` (this gcc has no
  `-std=c23`) and `CApiCoverageMatrix` (regenerating the inventory on this machine reports 388
  `planned` rows against the checked-in file's 0 — verified on a pristine tree, so the checked-in
  inventory was generated with a different doxygen).

- **Run it with a display.** Without one, every test whose fixture constructs a `GraphicsDevice`
  fails on "No available video device" (~1000 failures). `Xvfb :99 -screen 0 1280x720x24` plus
  `DISPLAY=:99 SDL_VIDEODRIVER=x11` gives Mesa llvmpipe, which reports **OpenGL ES 3.2** to
  EasyGL — enough for the whole Phase 1–7 HDR spine to be verified for real, in software.

```bash
Xvfb :99 -screen 0 1280x720x24 &
DISPLAY=:99 SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy ./cmake-build-cnaext/CnaTests
```

`MediaLibraryTestFixture.ObjectGraphIsInternallyConsistent` segfaulted in an earlier run started
from the build directory (with no media fixtures resolvable); from the repo root it passes. Worth
remembering if a future run aborts mid-suite.

---


### Running the engine layer under ASan

`cmake-build-asan`, configured `-DCMAKE_BUILD_TYPE=Debug -DCNA_GRAPHICS_RENDERER=OPENGLES3
-DCNA_CNAEXT=ON -DCNA_SANITIZE=address -DCNA_BUILD_EXAMPLES=OFF -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`.
Three things are worth knowing before repeating it:

- **Drive the build in bounded foreground chunks.** This container restarts, and a restart kills a
  detached background build without a trace in the log — the log simply stops. Ninja resumes from
  where it stopped, so `timeout 570 cmake --build cmake-build-asan --target CnaTests -j$(nproc)`, repeated,
  is the reliable shape. The first attempt here lost about 50 minutes to a build that had been dead
  the whole time.
- **Disk.** The ASan tree needs room the three existing build directories did not leave. The
  14 GB `cmake-build-multi` was deleted to make space; its findings are recorded in
  `plans/plan_modern.md` `MOD-1692`/`MOD-1696`/`MOD-1697` and the tree itself is reproducible from the
  recipe in `CLAUDE.md`.
- **The one expected leak.** `1032 bytes in libdbus via SDL_DBus_Init`. It is the *same single
  allocation* whether you run the whole suite once or 3000 pipeline frames, which is what makes it
  identifiable as external rather than as something that scales with the work.

Xvfb also dies periodically in this container. A wrapper that checks `xdpyinfo` and restarts it
before running is worth having; without one, a test run fails with "x11 not available" and looks
like a regression.

### The final sweep (`MOD-1906`), and the two failures that were the harness

The last full run on the reference renderer *at the time of `MOD-1906`* was **7944 ran · 7880 pass ·
64 skip · 0 fail**, exit code 0. (Phase 20 has since taken it to 8294 · 8229 · 65 · 0; this section
is left in the tense it was written in, because what it explains is the two failures below rather
than the count.) Two failures were seen on the way there and neither was a regression; both are recorded
because the next person to see them should not have to rediscover why.

`DynamicSoundEffectInstanceTest.StressSubmitFloatBufferEXTAgainstRepeatedPlayCyclesNeverCorruptsLiveStream`
failed once, in a run made while a Wine D3D9 test run and a MinGW build were also using the machine.
It is a race-window test, and it failed on its own **anti-vacuity** assertion — `callsThrown > 0`,
whose message says that zero *"would mean this test never actually exercised the guard it exists to
verify"*. Under a three-way load the threads did not interleave and the window never opened. Re-run
alone: **3/3 pass**. `modules/audio` has no diff on this branch at all.

`TerminalRestoration.SighupGivesTheTerminalBack` failed in the first final-sweep run and passed
alone, which is the shape of a flake and is not one. The run had been started with `nohup`, and
**`nohup` sets SIGHUP to `SIG_IGN`** — a disposition `fork` preserves. The child that the test
sends SIGHUP to therefore ignores it, exits 0 rather than dying from the signal, and the test's
`WIFSIGNALED`/`WTERMSIG` assertions fail exactly as they should. Confirmed by running that one test
under `nohup`: it fails every time, and passes every time without it. **Do not run this suite under
`nohup`**; use a foreground run, or `setsid`, when a long run has to survive the shell.

### A third load-induced failure, and what the three have in common

`GamePlatformTimingTest.MillisecondTicksAdvanceOverARealDelay` failed once in a full-suite run and
passes 3/3 alone; `modules/platform` has no diff on this branch. That is the third of its kind, after
the audio stress test and `TerminalRestoration.SighupGivesTheTerminalBack`, and the pattern across
them is worth naming rather than re-diagnosing each time.

All three assert something about **wall-clock time or process scheduling** — a race window opening, a
signal being delivered, a millisecond counter advancing — and all three are correct assertions about
a machine that is not also compiling something. They are not flaky tests in the usual sense of a
wrong assertion; they are tests whose subject is the scheduler, run on a shared one. Re-running the
failing test alone is the diagnosis, and it is the *only* diagnosis that distinguishes them from a
real regression, so it is worth doing every time rather than assuming.

What would remove the ambiguity is a label separating scheduler-dependent tests from the rest, so a
full run could report them apart. That has not been done, and this note is here instead of it.

### `MOD-1906`: every renderer the layer could be measured on, in one table

Same engine-layer filter throughout. Pass and skip counts differ between renderers because the
capability gates skip what a renderer cannot do — a *high skip count is the layer working*, not a
gap in the run.

| Renderer | Ran | Pass | Skip | Fail | Executes shader source |
|---|---|---|---|---|---|
| EasyGL (`OPENGLES3`) — reference | 7944 (full suite) | 7880 | 64 | **0** | **yes** |
| `OPENGL4` | 491 | 406 | 85 | 0 | no |
| `OPENGL2` | 491 | 406 | 85 | 0 | no |
| `OPENGL1` | 491 | 402 | 89 | 0 | no |
| `MAGNUM` | 498 | 410 | 88 | 0 | no |
| `SOKOL` | 491 | 403 | 88 | 0 | no |
| `BGFX` | 491 | 402 | 89 | 0 | no |
| `WEBGPU` | 491 | 405 | 86 | 0 | no |
| `SDL_GPU` | 964 | 834 | 130 | 0 | no |
| `LLGL` | 488 | 399 | 89 | 0 | no |
| `DILIGENT` | 491 | 399 | 92 | 0 | no (accepts none) |
| `DIRECTX11` | 488 | 402 | 86 | 0 | no |
| `DIRECTX10` | 476 | 375 | 101 | 0 | no |
| `DIRECTX9` | 489 | 401 | 87 | 0 engine-layer | no |
| `DIRECTX12` | 426 | 323 | 86 | 0, **17 crash the process** | no |
| `SDL_RENDERER` | 491 | 380 | 111 | 0 | no (2D only) |
| `BLEND2D` | 491 | 380 | 111 | 0 | no (2D only) |
| `TINYGL` | 491 | 348 | 143 | 0 | no |
| `PORTABLEGL` | 491 | 348 | 143 | 0 | no |
| `OPENVG` | 491 | 334 | 157 | 0 | no (supports nothing) |
| `HEADLESS` / `SOFTWARE` / `STUB` | whole suite | — | — | 0 | no |

D3D12 is the one entry whose "0 fail" needs its qualifier read: it **segfaults on the copy-through
path** — a `FullscreenPass` draw into a `RenderTarget2D`, the fallback every unsupported pass takes
— so 17 tests kill the process rather than failing. Measuring the other 409 needed a driver that
resumes past each crash with the remaining tests as its filter. The defect is `plans/plan_dx.md`'s;
`MOD-1625` carries the detail.

Also verified on the reference renderer: **`CNA_CNAEXT=OFF`** (7557 ran · 7495 pass · 62 skip ·
**0 fail**) and **Release** (7940 · 7876 · 64 · **0 fail**, identical to Debug including the skip
list).

Not measurable in this container, each with its reason in its own row: `IGL` (its own
dangling-context assert kills the process), `FNA3D` (no driver can create a device), `METAL` (macOS
only), `WICKED` (needs an external clone), `OPENGLES1` (no ES 1.x context from this GLX), `SKIA`
(pinned artifact), `DIRECT2D`/`GDI`/`GLIDE`/`FREEDIRECT` (Windows or a sibling repo), and the four
web DOM identities (they build, but need a browser to run).

**Perf.** `docs/cnaext-perf.md` carries every recorded measurement, all of them on EasyGL. No other
renderer can be timed against it: on all of them the passes report `isSupported() == false` and copy
through, so a timing would measure the copy rather than the pass.

### Phase 16, as a whole: what measuring fifteen renderers actually bought

The sweep was worth far more than the rows it closed. Every renderer measured either confirmed the
capability gates work or produced a defect, and the defects were not in the renderers the sweep was
nominally about.

**Found in the engine layer** (all fixed):

- `InstancedRendererEXT` asked one capability where it needed two (SDL_GPU).
- `DepthNormalPrepass` trusted the `MultipleRenderTargets` capability, which WebGPU promises and
  does not keep; it now probes the bind once at construction and falls back.
- Six test-gating gaps, where a documented renderer boundary produced *failures* instead of skips:
  a fixture building a `VertexBuffer` on a 2D-only renderer, two cube-shadow tests on a renderer
  with no `RenderTargetCube`, an `EffectPass` chain test asking only `CustomEffects`, a
  `RequireCapability` test assuming every renderer supports *something*, and `MOD-1714`'s
  all-subsystems fixture.

**Found outside it** (fixed where small, recorded where not):

- **Three renderer identities that had never compiled** — see below.
- A process-exit segfault in `modules/devices`, renderer-independent and present with `CNA_CNAEXT`
  off (fixed, with a regression test).
- **Three single-context renderers**: TinyGL refused a second device cleanly; Sokol and Magnum
  *aborted the whole test process*, and now refuse by name. The placement matters in both — the
  guard has to run before the constructor creates its platform GL context, or the first renderer's
  objects are torn down against the wrong context and abort anyway.
- Eleven POSIX `setenv` call sites that had crept back after `docs/cnatests-mingw-setenv-proposal.md`
  removed all 62, which is why `CnaTests.exe` would not build for D3D11 again (fixed, now gated).
- LLGL terminates when a second device is attempted, and IGL segfaults on the copy-through path —
  both recorded for their own plans.

**The recurring shape**, in four different enums now: a capability describes what an API *accepts*;
the layer needs to know what a renderer *does*. `CustomEffects` vs `ExecutesShaderEffectSourceEXT`
was the first. `Instancing` vs `MultiStreamVertexInput` was the second. `MultipleRenderTargets`
promised-then-thrown was the third. `IGraphicsRenderer::SupportsCapability`'s own `return true`
default, sitting above a `DrawInstancedPrimitives` that defaults to a refusal, is the mechanism
behind all of them.

### Three renderer identities that had never compiled

`OPENGL1`, `LLGL` and `BGFX` could not be selected at all. Each of their descriptors carried
syntactic damage from one merge, `2f00c201`: OpenGL1 had a stray `} }` pair, LLGL had a
`namespace { return 0; } }` block left after the PLAT-8 merge deleted the enclosing function's
signature, and bgfx had one closing brace too many after `ResolvedWindowKind()`. All three are on
`next`, and all three were invisible because **a renderer identity is only compiled when someone
selects it** — nothing in a normal build touches the other 43 descriptors.

Each was found by trying to build that renderer, one at a time, which is the slowest way to learn
it. `scripts/check_renderer_descriptors.py` is the fast way, and now runs as the
`CNAEXT_RendererDescriptorsParse` ctest: a brace-balance and structure check over all 44
descriptors, no compiler, no renderer selected, milliseconds. It cannot prove a descriptor is
*correct* — only a build does that — but it catches exactly the damage that merge left, and it was
verified against a planted stray brace.

The general point is worth keeping past this plan: **a build configuration nobody selects is a
build configuration nobody compiles**, and 49 renderer identities means 48 of them are unbuilt in
any given build. Cheap structural checks over the whole set are worth more than they look.

### Measuring a third renderer, and the two bugs it found (SDL_GPU)

`cmake-build-sdlgpu` is a real SDL_GPU build of the engine layer (`-DCNA_CNAEXT=ON
-DCNA_GRAPHICS_RENDERER=SDL_GPU`; it needs `libshaderc-dev`, which the renderer requires for its
runtime GLSL compile). The engine-layer suites there end at **0 failures and 130 skips** — the
capability gates doing exactly what they promise on a renderer that runs almost none of this.

Two defects came out of it, and neither was visible on EasyGL:

- **`InstancedRendererEXT` asked one capability where it needed two.** SDL_GPU answers
  `Instancing: yes` — the base class's `default: return true` — while `DrawInstancedPrimitives` is
  the base class's *refusal* and `MultiStreamVertexInput` is `no`. So `draw()` threw where it should
  have taken the per-instance fallback. The fix is the `MOD-1699` shape again: the instanced path
  binds the transforms as a **second vertex stream**, so it needs multi-stream input as much as it
  needs instancing. That the renderer's `Instancing` answer is itself a promise it does not keep is
  a separate, renderer-level finding, recorded for `plans/plan_sdlgpu.md` rather than fixed here.
- **A process-exit segfault with nothing to do with graphics.** `CnaTests
  --gtest_filter=*Instanc*` crashed *after* every test reported, deterministically, on every
  renderer and with `CNA_CNAEXT` off. `__run_exit_handlers → ~VibrateController →
  ~PlatformVibrateBackend → ReleaseService →` a call through address 0: the controller's
  function-local static outlives the platform, and `ReleaseService` trusted the `IPlatform*` it had
  captured. `DevicesShutdownCoordinator`'s flag was meant to cover this and does not on its own —
  it is process-global and a test resets it. Fixed by checking the condition that actually matters
  (the captured platform is still the installed one), with a regression test that pins the guard
  rather than the crash, since a crash test would need ASan to be reliable.

`modules/graphics-ext/examples/cnaext_caps_probe.cpp` (`cna_test_cnaext_caps`) exists because of
this: every Phase 16 row asks what a renderer promises, and answering that by reading its source is
how `MOD-1699` got answered wrongly three times. Build it in whichever renderer's build directory
you are measuring and it prints the answers, `CustomEffects` next to `ExecutesShaderSourceEXT` and
`Instancing` next to `MultiStreamVertexInput` — the pairs where a renderer says yes and then no.

| | EasyGL (GLES3) | SDL_GPU | SDL_Renderer | PortableGL | TinyGL | OpenGL1 | OpenGL2 | OpenGL4 |
|---|---|---|---|---|---|---|---|---|
| ThreeD | yes | yes | no | yes | yes | yes | yes | yes |
| CustomEffects | yes | yes | no | no | no | no | yes | yes |
| Float / half-float RTs | yes | no | no | no | no | no | no | no |
| ComputeShaders | yes | no | no | no | no | no | no | no |
| Instancing / MultiStream | yes / yes | **yes / no** | no / no | no / no | no / no | no / no | **yes / no** | **yes / no** |
| ExecutesShaderSourceEXT | yes | no | no | no | no | no | no | no |
| ShadowSamplingEXT / IBL | yes / yes | no / no | no / no | no / no | no / no | no / no | no / no | no / no |
| Engine-layer suites | 0 fail | 0 fail | 380/111/0 | 348/143/0 | 348/143/0 | 402/89/0 | 406/85/0 | 406/85/0 |

Blend2D and OpenVG were measured too and need no column of their own: they answer **no** to every
capability and every query, `ThreeD` included, and their engine-layer runs are 380/111/0 and
334/157/0. OpenVG is the renderer that supports *nothing*, which turned out to matter — see below.

Two columns are worth reading twice. **`CustomEffects: yes` with `ExecutesShaderSourceEXT: no`**
(SDL_GPU, OpenGL2, OpenGL4) is the pair `MOD-1699` exists for: those renderers accept an effect and
do not run this layer's GLSL, and only the two-part question tells them apart from EasyGL. And
**`Instancing: yes` with `MultiStreamVertexInput: no`** is a promise three renderers make and do not
keep — the base class's `SupportsCapability` defaults `Instancing` to `true` while its
`DrawInstancedPrimitives` defaults to a refusal. Every one of those is a `default: return true` that
nobody revisited, which is the same failure mode in a different enum.

There is no `GraphicsDevice::SupportsComputeShadersEXT()`, despite what a reading of the four-query
rule suggests: compute's device-side answer is `SupportsCapability(ComputeShaders)`, which is
already derived from a false-by-default renderer virtual rather than from a renderer's own
`default: return true` switch. `SupportsComputeShadersEXT()` exists one layer down, on
`IGraphicsRenderer`.

### Release vs Debug, and the 32-bit half of `MOD-1716`

`cmake-build-cnaext-release/` is the third persistent build directory for this work
(`-DCMAKE_BUILD_TYPE=Release -DCNA_CNAEXT=ON -DCNA_GRAPHICS_RENDERER=OPENGLES3`). Both
configurations run the same 7940 tests with the same 7876 passes and the *same 64 skips* -- the two
skip lists were diffed name by name and differ only in per-test milliseconds. That equality is the
point of the row: an optimised build of a layer full of floating-point shader maths and
tolerance-based image assertions is exactly where a `-ffast-math`-shaped difference or an
uninitialised read would show up as a different verdict, and none does. Release is about 22% faster
end to end (230 s vs 294 s), which is unremarkable for a suite dominated by llvmpipe rasterisation
rather than by CNA's own code.

The **32-bit** half of `MOD-1716` is not verifiable in this container and is not claimed. There is
no i386 multilib (`g++ -m32` cannot find `Scrt1.o`, and `/usr/lib/i386-linux-gnu` does not exist),
so a 32-bit build would fail at the C runtime long before reaching a 32-bit SDL3, GL or FFmpeg --
none of which are present either. Installing a full 32-bit sysroot to satisfy one plan row is not
proportionate; the row stays 🟨 with the blocker named rather than being marked done on a
64-bit-only measurement.

### The engine layer on Emscripten (`MOD-1717`), and why Android (`MOD-1718`) is not

`cmake-build-web-cnaext/` is a real Emscripten build of the engine layer (emsdk 6.0.7,
`-DCNA_CNAEXT=ON -DCNA_GRAPHICS_RENDERER=WEBGL2 -DCMAKE_BUILD_TYPE=Release`). `libcna_graphics_ext.a`
compiles, an engine-layer example links, and it **runs** — `node cna_example_cnaext_settings.js`
prints `=== All PASS ===`. That last step is why this row is worth more than a compile check: the
layer's CPU-side behaviour is exercised on a 32-bit wasm ABI with libc++ instead of libstdc++, which
is the closest thing this container has to the 32-bit half `MOD-1716` could not do.

Getting there needed two fixes and turned up one defect that is not ours:

- **`-lembind` was missing** (fixed, `modules/core/CMakeLists.txt`).
  `GraphicsRendererSelectionEmscripten.cpp` reads `Module.cnaPreferredRenderer` through
  `emscripten::val`, and Emscripten does not link `libembind` implicitly. Every Emscripten
  executable in this repository failed at `wasm-ld` with undefined `_emval_*` symbols — not just
  engine-layer ones. The dependency now travels with `cna_core` as an `INTERFACE` link option
  rather than being repeated in each consumer.
- **`EMSCRIPTEN` must be exported** in the environment, not just `emcmake`'s toolchain: vendored
  Draco's `draco_emscripten.cmake` checks for that variable by name and fails the configure without
  it. `export EMSCRIPTEN="$EMSDK/upstream/emscripten"` before `emcmake` is the whole fix.
- **Vendored Draco 1.5.7 does not compile under libc++.** `src/draco/io/ply_reader.cc` calls
  `std::all_of` without including `<algorithm>`; libstdc++ happens to provide it transitively and
  libc++ does not. It is a pinned third-party submodule, so it is recorded rather than patched here,
  and the web build uses the repository's existing `-DCNA_ENABLE_DRACO=OFF` mode — which exists
  precisely as an intentional decoder-free configuration.

**Android is refused, not deferred.** The NDK is distributed only by Google; the agent proxy answers
`403` to `CONNECT dl.google.com:443` and records the denial in its own status endpoint, and the
Ubuntu `google-android-ndk-*-installer` packages are 16 KB shims that fetch from that same host
(their control scripts name `https://dl.google.com`, with `mirrors.neusoft.edu.cn` as the only
alternative — also unreachable, as is `mirrors.cloud.tencent.com`). With no sysroot there is no
bionic and no `libGLESv3`, so an `-DANDROID_ABI=…` configure could only fail at the first header.
Nothing suggests the layer is Android-hostile: `MOD-1717` runs it against a GLES-shaped target and
`MOD-1719` compiles it for a second non-Linux ABI. Claiming the row on that basis would be a paper
claim, so it stays ⛔ with the blocker named.

### What a Windows compiler would have said (`MOD-1719`)

`mingw-cnaext-spike/` cross-compiles the whole engine layer with `x86_64-w64-mingw32-g++` and, more
usefully, includes `<windows.h>` before every public header the way a D3D translation unit does. Two
things came out of it that were not visible from Linux:

- **`near` and `far` are live macros** in `windef.h`. Nothing in the layer trips over them today --
  `DepthNormalPrepass::begin` takes `nearPlane`/`farPlane` -- but that was luck rather than policy,
  and `begin(…, float near, float far)` is the name a reviewer would have suggested. The probe now
  fails loudly if anyone writes it, and it `#error`s if the macros turn out *not* to be defined, so
  it cannot quietly stop testing anything.
- **MinGW cannot reproduce the `min`/`max` hazard at all.** Its `windef.h` guards those two with
  `#ifndef __cplusplus`; MSVC's does not. Reinstating them by hand shows the engine layer's own
  headers are clean and that **sharp-runtime** is not: `SharpRuntimeHelper.hpp` writes
  `std::numeric_limits<T>::max()` unparenthesised in five places, which a real MSVC D3D build would
  reject. That is a different repository, so it is recorded here rather than fixed; the script
  scores the probe on "nothing originating under `modules/graphics-ext`" and prints who is still
  failing, so it will announce the fix if it ever lands.

The spike does not link. SDL3, GL and FFmpeg pre-built for Windows are not in this container, and
installing them to satisfy one row is not proportionate; the row is about the engine layer's paths,
which are header- and source-level.

### Closing Phases 0–3: what the leftover rows were actually hiding

The plan's early phases had been reported as done while 60-odd of their rows were still ⬜. Most were
documentation and verification the implementation had outrun — but working through them found four
things that were not bookkeeping:

- **`TINYGL` did not configure at all.** `MOD-134`'s renderer sweep caught it on its first full run.
  Its dispatch arm called `add_compile_definitions()` directly instead of appending to
  `_cna_identity_defines`, so its `CNA_RENDERER_TARGET_DEFINES` entry was the empty string — which
  makes that list *empty*, not one element long — and `modules/renderers/CMakeLists.txt` died with
  "list GET given empty list". It was the only one of 45 dispatch arms doing this. The sweep now
  covers all 49 identities: 22 configure here, 27 skip by toolchain, 0 fail.

- **A pass that threw left its destination bound** (`MOD-203`). Fixed with `ScopedRenderTarget` —
  and the fix then caused its own regression, which `MOD-318` caught two rows later: the class
  restores what a pass *found* bound, so the chain, entered with the scene target still bound,
  faithfully put it back after the last pass wrote the back buffer, and `Present` refused.
  `RenderPipeline::end()` now unbinds the scene target first, which it should have done regardless.

- **Skia supports every float render-target format**, which is the opposite of what "CPU raster"
  suggests and was about to be written into the docs the other way round. Checked in its source
  rather than assumed.

- **`MOD-220` changed no behaviour, and that is the finding.** `SpriteBatch::Begin` already
  documents a null sampler as `LinearClamp`, so bloom's pyramid was being filtered correctly by a
  default that had nothing to do with bloom. The row's value is the attachment, not a fix.

Two rows were refused rather than done, both with the reason in the row: `MOD-314` (goldens — see
`MOD-1703`) and `MOD-320` (`RenderQuality` deliberately does not touch tonemapping, because there is
nothing to turn down and the operator is an artistic choice). One deviated from its own instruction:
`MOD-219` **logs rather than throws**, because three renderers report `CustomEffects` true and never
compile GLSL source, so throwing on a failed compile would turn a documented capability boundary
into a crash on all three.

### Phase 16.6: what measuring the three no-op renderers actually found

`MOD-1692`/`MOD-1696`/`MOD-1697` were rows saying "same" — an assumption that the engine layer would
construct and pass through on Headless, Software and Stub. Running it produced 21 failures on
Headless alone, and the causes were not capability gaps. They were **limits with no capability to
ask about** and, in one case, an outright wrong promise:

| Renderer | What it will not do | How it fails without a probe |
|---|---|---|
| Headless | read a render target back to the CPU | every test that inspects a pass's output throws |
| Headless, Stub | store `TextureCube` face data | the IBL precompute throws where the docs said it worked |
| Headless | make a cube face the current target — the bind is recorded and ignored | the face-sized viewport that follows is rejected as out of the *back buffer's* bounds |
| Stub | bind a `RenderTarget2D` at all | `RenderPipeline` stops before it renders |
| Software, Headless | run a custom effect's shader source, while reporting `CustomEffects` | the sky renders the placeholder texture's white and calls itself supported |

Four of the five are now probed by `modules/graphics-ext/tests/CNA/Graphics/EngineTestSupport.hpp`,
which asks by *doing* rather than by reading a flag, so a probe cannot drift from the truth. The
fifth is a real API gap and became `ExecutesShaderEffectSourceEXT()`, now consulted by `Skybox` and
all four shadow casters as well as `PostProcessPass`.

Two details are worth keeping:

- **The cube-face probe uses a face larger than the back buffer on purpose.** A 16-pixel cube is
  accepted by a bind that does nothing, because the viewport that follows still fits inside the back
  buffer — the probe would pass and the renderer would draw the shadow map onto the screen. Sizing
  the probe above the back buffer is what makes a fake bind observable.
- **`CubeShadowMap::begin` marked the pass open before binding.** So the first unsupported face threw
  and every later one reported "a face pass is already open" — one refused face turning the object
  into a brick. It now opens the pass only after the bind and clear succeed, and unbinds on the way
  out.

The failures that remain under SOFTWARE (13) and STUB (29) are outside the engine layer and are
recorded rather than fixed: most assert facts about the **build's default** renderer, which cannot
hold when a non-default one is forced through `CNA_GRAPHICS_RENDERER`; the rest are XNA-layer
`TextureCube` and content tests that need real cube storage. Neither set is a regression, and both
predate this phase.

---

## 4. Open questions

The ten `OQ-*` entries at the end of `plans/plan_modern.md` each carry a default that is being followed.
Four were answered by the owner on 2026-08-17 (`OQ-1`, `OQ-7`, `OQ-8`, `OQ-9` — see §1); the rest
stay on their defaults until raised.
