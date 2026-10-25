# Avatar v3 completion and acceptance (AVA)

Owner brief, 2026-10-24: a focused completion and acceptance pass over Avatar v3, not another
feature campaign. Input: the read-only audit `../avatar.md` (made against `714bfd9bc`), whose
findings are verified here against the current HEAD before anything changes, and the original
campaign brief's acceptance criteria. Out of scope unless an acceptance failure needs it: more
catalog breadth, large art rewrites, photorealism, a new renderer or avatar architecture, AO or
contact-shadow systems, open-ended face or fold polish, and native Windows/macOS certification
(no such hardware here; it stays pending, see `docs/gamer-services-known-limitations.md` 33).

Task ids `AVA-*`; one task, one commit.

| Task | What | Status |
|---|---|---|
| AVA-01 | Reproducible baseline on the current HEAD | done |
| AVA-02 | The two STUB failures the baseline found | done |
| AVA-03 | Documentation drift (revision, sizes, folds, proportions) | done |
| AVA-04 | Visual regression: checked-in manifest and goldens, legacy script retired | done |
| AVA-05 | One Avatar v3 acceptance command | done |
| AVA-06 | Earrings fitted to every ear shape | done |
| AVA-07 | Planted-foot drift across bodies, heights and proportions | done: narrowed and pinned |
| AVA-08 | Deep-crouch clothing show-through | done |
| AVA-09 | Severe hair/headwear intersections | done |
| AVA-10 | Final acceptance run and report | open |

## AVA-01 baseline (2026-10-24)

Commit `ba707082e` (`next`), clean checkout. `cmake-build-multi`: Debug, GCC 14.2.0, Ninja, ccache,
`OPENGLES3` default with `OPENGL33;WEBGPU;SDL_GPU;FNA3D;SOFTWARE;HEADLESS;STUB` compiled in, SDL3
platform; every target up to date at that commit. Each renderer selected with
`CNA_GRAPHICS_RENDERER` and run through `tools/platform/run_gpu_tests_private.sh --exec` (headless
Weston + private Xwayland, RADV), `SDL_AUDIODRIVER=dummy`. Evidence: `/rv/tmp/avatar-acceptance/baseline`.

`CnaGamerServicesTests` (all 737 Gamer Services tests, 90 suites, among them every avatar,
Guide-editor and avatar-service suite):

| Renderer | Passed | Skipped | Failed | Time |
|---|---:|---:|---:|---:|
| OPENGLES3 | 736 | 1 | 0 | 82 s |
| OPENGL33 | 736 | 1 | 0 | 82 s |
| SDL_GPU (Vulkan) | 736 | 1 | 0 | 84 s |
| WEBGPU | 736 | 1 | 0 | 90 s |
| FNA3D (SDL_GPU Vulkan driver) | 736 | 1 | 0 | 87 s |
| SOFTWARE | 736 | 1 | 0 | 358 s |
| HEADLESS | 736 | 1 | 0 | 85 s |
| STUB | 733 | 2 | 2 | 84 s |

The one skip everywhere but STUB is `AvatarRendererTest.AFailedGpuUploadFailsEveryDrawCleanlyRatherThanHalfBuilding`
(it needs a renderer without a 3D pipeline). On STUB, which has none, it runs and fails: Draw no
longer throws, because the renderer it was written against (the retired 2D-only SDL_RENDERER)
refused vertex buffers and STUB accepts them. Also on STUB,
`GuideAvatarEditorDeviceTest.AKeyboardReachesEveryPartOfTheEditor` fails with "SetRenderTargets:
this renderer does not support RenderTarget2D" thrown out of the Guide's draw: the editor stage
(and the Guide's portraits) use a render target unconditionally. STUB also skips the two 3D draw
tests by design. Both failures are AVA-02.

Also passing: `GamerServices_AvatarCatalogUpToDate` (85 s), `EasyGL_AvatarRenderer_Standard`,
`CApi_AvatarsSmoke`, `CApi_AvatarIdentitiesSmoke`, `CApi_AvatarExitWhileLoadingSmoke` (ctest
through the private runner), and `avatar_audit.py --version 3` (8 min): female 1.679 m and 4.78
heads, male 1.797 m and 4.94 heads; no garment, hat or hairstyle shows skin through it beyond the
audit's threshold, the open sandals by design.

Planted-foot drift (AVA-07), measured on HEAD with the CPU preview, which skins as the renderer
does: on the authoring body at its authored height a planted ankle stays within 2 mm; on the other
body at its authored height up to 3-4 mm sideways and 6 mm vertically (FemaleIdleFixShoe); at the
description extremes (1.45 or 2.05 m, proportions 0 or 255) up to 30 mm (FemaleIdleFixShoe), 21 mm
(Celebrate), 19 mm (MaleSurprised) and 11-14 mm (the weight shifts, Stand5, FemaleShocked).
Presets without root motion do not drift.

## AVA-02 STUB failures

- The Guide's avatar editor stage and its portraits (`GuideAvatarEditor.cpp`, `GuidePortraits.cpp`)
  bound a `RenderTarget2D` unconditionally; on a renderer without off-screen targets the bind threw
  out of the Guide's draw, which only the device tests whose avatar happened to finish loading in
  time reached. They now leave the picture out when the target reports no renderer behind it
  (`GetRenderTargetRenderer()`), and the editor's `stateForTesting` names the stage drawn, refused
  or not yet drawn. `TheStageIsDrawnOrLeftOutOnEveryRenderer` and
  `PortraitsAreDrawnOrLeftOutOnEveryRenderer` wait for the avatar, so every renderer reaches the
  draw; both fail on STUB without the fix.
- `AFailedGpuUploadFailsEveryDrawCleanlyRatherThanHalfBuilding` skips unless the device refuses a
  vertex buffer, which is what provokes the failed upload it pins; STUB accepts them.

Result: `CnaGamerServicesTests` 739 tests -- STUB 736 passed and 3 skipped (the upload test and
the two 3D draw tests), OPENGLES3 738 passed and 1 skipped; nothing failed.

## AVA-03 documentation drift

Checked against `catalog.json` (catalog v3 revision 4, reader 2: 143 items, 22 feature items, 24
face styles, 59 presets, 10 outfits, 359 files and 45,959,118 bytes with the manifest), the built
`libcna_gamerservices_avatar_catalog.a` (22,324,016 bytes), the generator and `avatar_audit.py`:

- `gamer-services-known-limitations.md` 41 described revision 2 (4.7 heads) as the current art and
  said garments have no modelled folds; the garments carry low-frequency folds in their surface
  (`garments.py` `Folds`, `hang_folds`, `sleeve_folds`, trouser folds), without fine creases or
  normal maps. 42 said the catalog ships at revision 2. 33 described the 2026-10-15 revision 2
  review as the current renderer evidence; it now gives AVA-01's revision 4 review on six Linux
  renderers, the 2026-10-16 Wine/DXVK and Wine/vkd3d-proton renders as translation-layer results,
  and native Windows and macOS as pending external qualification.
- `link-size-architecture.md` gave the embedded catalog as ~13 MB (revision 1's); it is 22.3 MB
  deflated. The 30 MB of 2026-10-12, measured with the development catalogs, stays as history.
- The CMake comment's "~30 MB, a third of that compiled in" no longer held (46 MB, about half);
  it now names no figure and points at `docs/avatars.md`.
- `avatars.md`: the manifest lists 358 files (359 with itself); contract level 2 is what every
  revision since 2 needs, stated as such; revision 4's manifest, 189 KB, is the one over the
  64 KiB message limit now, not "revisions 2 and 3's".

Revision 2 and 3 statements that describe their own time (the art direction's revision sections,
the format rules for what revision 2 added, compatibility with builds from before revision 2) stay.

## AVA-04 visual regression

`avatar_review.py` already drew deterministic job lists through `cna_avatar_review` and compared
renderers, but nothing kept a reference: `scripts/avatar_visual_regression_check.py`, three
`demo_avatar` screenshots under `xvfb-run` measured in fixed pixel boxes of the 2026-07 catalog, was
the only gate. It is removed. `tools/avatar_builder/avatar_visual_regression.py` builds on the review
tools instead:

- `regression/manifest.json`: 35 jobs generated from readable specs (eight described avatars, three
  `CreateRandom` avatars from `cna_avatar_review --random 3 --seed 2026`, new here), covering the
  representative matrix of the original brief and stating what each job covers; `check` fails if
  the manifest stops covering it, if a job is missing or if `cna_avatar_review` fails. Regenerating
  it is byte-identical. Job names say what they show, so adding one renames no golden.
- `regression/golden/`: their renders on SOFTWARE, 256 px, 1.0 MB in all.
- Tolerances from measurement, against SOFTWARE's goldens: OPENGLES3, OPENGL33, SDL_GPU, FNA3D and
  Mesa llvmpipe (`LIBGL_ALWAYS_SOFTWARE=1`) within mean 0.081/255, worst 8x8 block 1, silhouette
  0.9985, area 0; WEBGPU differs in skinned shading on posed frames (block up to 21, mean 0.49) and
  has its own. Injected changes -- a hoop, ring or watch removed, a stud for a hoop, a round ear for a
  long-lobed one, other glasses or shoes, 2 cm of height, a 20-level hair or 10-level top colour
  change, the hat removed -- fail on every renderer; removing a thin eyeliner fails only on SOFTWARE.
  A wrong golden fails with report.json and a golden/render/difference sheet.
- `GamerServices_AvatarVisualRegression` (labels `avatar`, `visual`) runs `check` with a build's
  default renderer; it passes in `cmake-build-multi` (OPENGLES3) through the private runner, and
  `check --renderer` passes on all six drawing renderers.

## AVA-05 acceptance command

`python3 tools/avatar_builder/avatar_acceptance.py <build> [--all-renderers | --renderer X ...]`
(stages in `docs/avatars.md`, "Acceptance"). It needs no new CTest entry: the avatar gtests are
already discovered individually, and the command runs the test binary itself with a JSON report so
it can tell a test that ran from one that skipped or vanished. Ten components are pinned at
today's counts (226 tests); only three skips are allowed, with their reasons. Every GPU run goes
through the private runner.

On `cmake-build-multi` at `a1b4b6e18`, `--all-renderers` passed in 14 min 50 s: catalog, docs (16
claims), generator, and on OPENGLES3, OPENGL33, WEBGPU, SDL_GPU, FNA3D, SOFTWARE and HEADLESS 225
passed and 1 allowed skip, on STUB 223 and 3; the visual regression on the six that draw (HEADLESS
and STUB reported as drawing no pixels). Pinning one more codec test than exists, or a component
whose suite is gone, fails the tests stage.

## AVA-06 earrings on every ear shape

Reproduced on HEAD: earrings are made for the first ear shape's lobes (`accessories.earrings`,
`head.ear_lobe(body, side)`) and were only moved by the face controls' displacement at the ear's
root. The other shapes' lobes lie elsewhere: small 5.3-5.5 mm higher (the earring hung under the
lobe), large 4.4-4.6 mm lower, long lobes 10.6-11.0 mm lower (the stud sat up inside the lobe),
attached 2.2 mm, protruding 3.7 mm out and back; and at the ear controls' ends the root's
displacement differed from the lobe's by up to 2 cm. Estimating a lobe from the ear mesh (lowest
vertex, or 82 % of the way to it from the centroid) left 3.5-5 mm on the long-lobed and protruding
ears, as large as the defect, so the generator, which knows each shape exactly, now says it.

- Catalog v3 revision 5: each `ears` feature item names its `lobes` (per body, left and right).
  Only `catalog.json` changes (+1,808 bytes; every model is revision 4's, so the pinned assembly
  digests are unchanged). Older CNA builds and the service ignore the key; the service's
  `avatar_validation_tests` (209 assertions, the CNA catalog imported) and `avatar_tests` (200) and
  `cna_avatar_e2e.py` (catalog pack built from it installed and validated, editor saves) pass
  against revision 5 without a service change.
- `AvatarModel.cpp` (and the CPU preview) carries earrings by the worn shape's lobes minus the
  first shape's, and moves them by the face controls' displacement at the lobe.
- `EarringsHangFromTheLobeOfEveryEarShape`: every shape's lobes lie on its ear, and a stud keeps
  its place on the first shape's lobe on every shape, both bodies and the ear-size, height and
  angle controls at their ends, within 1.5 mm; without the assembly change it is off by up to 22.6 mm.
- The visual regression's three ear close-ups changed as intended (the protruding ear's hoop now
  hangs from its lobe, the long-lobe stud sits at the lobe); goldens updated.

`CnaGamerServicesTests` 740: 739 passed, 1 allowed skip (OPENGLES3).

## AVA-08 deep-crouch show-through

Measured on HEAD as the existing inner-layer test measures (in the lower back's frame, a vertex of
the bottom the top covers in the bind pose that the top lies inward of in a pose): over every
preset's key frames only FemaleIdleFixShoe's deepest frames let a bottom through, and far more than
"a few millimetres of trouser front": jeans through the back of the tank top in patches (103 of its
covered waist vertices female, 89 male; up to 6-15 mm out) and every skirt through the back of every
top (33-76). The other tops over trousers: 1-13 vertices.

- A top's hem gave the thighs a 0.4 share at the back below the hips; under a deep hip flexion that
  swings the back of the hem forward into the waistband, which follows the pelvis. Raising the share
  made it worse (171); now only the front follows the thighs (`garments.hem_weights`).
- A skirt's weights were the pelvis alone plus the thighs' share; a top's start from the torso's, hip
  share included, so the two moved apart. Skirts now start from the torso's weights (`_skirt`); the
  body under them is unchanged (6-9 vertices before and after). Making the skirt's back follow the
  pelvis, or starting its thigh share lower, was worse (61-83).
- The tank top, lofted tight from the body, has 6 mm more ease round the waist and hips.

Catalog v3 revision 5 (not yet released): 56 top and 8 skirt models change, nothing else.
`TheTrousersStayUnderTheTopInTheDeepestCrouch` (every top over jeans, shorts and joggers, budget 18,
and over two skirts, budget 24, both bodies, the two worst frames) finds at most 16 and 21; on the
pre-fix catalog it finds 105. The deep-crouch close-ups of every top over jeans and over the
pleated skirt were compared before and after on OPENGL33; what remains is a few-millimetre sliver at
the side of a few tops. The back of a top's hem now rides up off the seat in that crouch and shows
the waistband, as cloth does. Pinned assembly digests re-pinned (three of the four wear a top or a
skirt that changed). `CnaGamerServicesTests` 741: 740 passed, 1 allowed skip (OPENGLES3); visual
regression goldens updated where the crouch views changed.

## AVA-09 hair through hats

Every hat over every hairstyle on both bodies (2,220 renders, `/rv/tmp/avatar-acceptance/hats`)
and a geometric measure (a hair vertex is beyond the hat's cloth when the segment from the head's
centre to it crosses the hat's surface an odd number of times) found 168 of 1,110 pairs with more
than 10 such vertices: high ponytails and ponytails spiking out through sun hats, western hats,
bucket hats and backward caps, pigtails through a bucket hat, spikes, curls, twists and the afro
speckling through the beret, the afro through a bucket hat's brim, a headband or visor sunk into
voluminous hair. Flattening under a hat pulled such hair in towards the head while its neighbours
outside stayed put, so its triangles stretched out through the brim or crown.

- `AvatarModel.cpp` hides hair a hat passes through or over (that test, on the hair as authored and
  again after flattening, for hair flattening moved into a sloping brim), whatever the hat, the
  open ones included; the hat's triangles are filed by direction from the head's centre and the
  test uses the catalog's binary32 arithmetic. The CPU preview does the same. Debug assembly of an
  avatar wearing a hat costs about 14 ms more.
- `HairNeverComesThroughAHat` (every hat, the eight offending styles, both bodies, measured with its
  own implementation of that test) finds at most 4 (rounding at the afro's edge under a headband);
  with the previous assembly 293.
- A new visual-regression view (a high ponytail under a sun hat, from behind). What remains: a
  very wide style is cut where it meets a brim and shows a ragged edge just under it from behind
  (the afro under a bucket hat); nothing of it is through the hat.

`CnaGamerServicesTests` 742: 741 passed, 1 allowed skip (OPENGLES3). One pinned assembly digest
(a ponytail under a headband) re-pinned.

## AVA-07 planted-foot drift

The documented "a preset authored for one body lets a planted foot drift 1-3 cm on the other" does
not match HEAD: composed as the renderer draws, a planted ankle moves at most 6.6 mm on the other
body at its authored height (FemaleIdleFixShoe); 1-3 cm happens at the ends of the height and
proportion ranges, on either body, up to 31.3 mm (FemaleIdleFixShoe; Celebrate and MaleSurprised
about 2 cm). Presets without root motion do not drift.

Cause: the presets are solved with planted feet on one rig, and the renderer applies the clip's root
translation unchanged while every other joint comes from the avatar's own, height-scaled bind pose.
Scaling the root motion (or re-solving the legs) at draw time would keep the feet planted but break
XNA's `BoneTransforms[i] * BindPose[i] * parent` composition that games use to attach objects (the
`ObjectPlacementOnAvatar` sample; `AttachmentCompositionAgreesWithDrawnJointsForEveryPresetAndBody`),
since `AvatarAnimation` cannot know the avatar; a single avatar-independent clip cannot be exact on
every leg length. Not fixed: the limitation is stated with these numbers in
`gamer-services-known-limitations.md` 41 and `avatars.md`, and
`PlantedFeetDriftOnlyAsFarAsTheRootMotionAllows` pins it (other body under 8 mm, extremes under
33 mm).
