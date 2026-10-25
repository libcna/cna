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
| AVA-10 | Final acceptance run and report | done |

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

## AVA-10 final acceptance (2026-10-25, `1bf504b26`)

- `python3 tools/avatar_builder/avatar_acceptance.py cmake-build-multi --all-renderers`: PASSED in
  17 min 33 s. Catalog (v3 revision 5, reader 2, 189 ids, every reference resolves), docs (16
  claims), generator byte-for-byte; per renderer 230 avatar tests in the ten pinned components --
  OPENGLES3, OPENGL33, WEBGPU, SDL_GPU, FNA3D, SOFTWARE and HEADLESS 229 passed and 1 allowed skip,
  STUB 227 and 3 -- and the 36-view visual regression on the six that draw (SOFTWARE exact; GPUs
  within mean 0.044/255, WEBGPU 0.485/255 and block 21; silhouettes 0.9996).
- The whole `cmake-build-multi` CTest suite through the private runner (12,570 tests, excluding the
  Wine-hosted XNA builds as every earlier full run did): 12,555 passed. The 15 failures are SDL_GPU
  and FNA3D compiled-effect suites (`SdlGpuCompiledEffect*`, one `Fna3dCompiledEffectTest`) that
  assume their renderer is the active one; this tree's default is OPENGLES3. They pass when their
  renderer is selected (62 and 37), came with `e460e0ed6` before this pass, and this pass changed
  nothing under `modules/renderers`, `modules/graphics` or `cmake`. Not an avatar matter; left as is.
- The service against revision 5: `avatar_validation_tests` 209 and `avatar_tests` 200 assertions,
  `cna_avatar_e2e.py` (catalog imported, a pack built from it installed, editor saves) passed.
- Not repeated for revision 5: the Wine/DXVK and Wine/vkd3d-proton translation-layer renders (the
  MinGW tree had been emptied; they remain revision 4's). Native Windows and macOS: pending external
  qualification (`docs/gamer-services-known-limitations.md` 33).

# Technical acceptance hardening (AVH)

Owner brief, 2026-10-25: an independent audit found the acceptance infrastructure above weaker than
its report: it ran whatever binaries a tree held, counted tests instead of naming them, never
checked the visual manifest against the catalog, and left documentation at revision 4. Scope:
technical correctness, test reliability, documentation consistency and evidence; catalog v3
revision 5, the serialization format and the public API stay as they are; no art work.

| Task | What | Status |
|---|---|---|
| AVH-01 | Source and build provenance | done |
| AVH-02 | Expected test identities, exit status | done |
| AVH-03 | Visual manifest reproduced and checked against the catalog | done |
| AVH-04 | Documentation at revision 5, consistency check without a build | done |
| AVH-05 | Size, link-footprint, load-time and cache-lifetime measurements | done |
| AVH-06 | Gamer Services service validation and end-to-end persistence | |
| AVH-07 | Final acceptance matrix and full CTest suite | |

## AVH-01 provenance

`avatar_acceptance.py` ran `<build>/CnaTests` and `cna_avatar_review` as it found them and labelled
the result with `git describe --dirty`: a tree last built at an older commit was reported as that
commit's acceptance. Now a `source` stage records the commit and refuses a checkout that differs
from it (tracked, untracked, or the catalog submodule; `--allow-dirty` runs and validates no commit)
or a tree configured from another checkout; a `build` stage builds every binary it runs and records
compiler, build type and SHA-256 of each (a tree already up to date costs 0.3 s; the always-run
content-fixture steps are not counted as rebuilt), refusing a tree another ninja/make/cmake
--build is working in; `--configure` configures a tree that does not exist yet; `source end`
fails a run whose checkout changed under it except by documentation it did not read. Checked: a
foreign `CMAKE_HOME_DIRECTORY`, a fake concurrent `ninja` and an uncommitted edit are each refused.

## AVH-02 test identities

The tests stage counted, per component, the tests that passed or skipped and failed only when a
count fell below its pin; a renamed test whose count another test made up, a disabled one in a
component with slack, or a crash that still left a clean JSON report would pass, and the exit
status was never read. Now `acceptance_tests.json` names the 232 required tests (the 230 before
plus `ServiceConfigurationTest.AvatarCatalogUpdatesAreOnByDefaultAndCanBeTurnedOff` and
`ServiceRequestRetryTest.AnAvatarReadThatNamesNoCatalogsSendsNoCatalogList`, which the persistence
patterns never matched) with their components' patterns and the three allowed skips. An
`identities` stage reads `CnaTests --gtest_list_tests` (disabled tests included) and fails a listed
test that is missing, or disabled by its own or its suite's `DISABLED_`, and a matching test that is
not listed; each renderer then runs exactly the listed tests and fails on a non-zero exit status, a
test that did not run or was suppressed, a failure, or a skip `allowedSkips` does not allow on that
renderer. A `ctest` stage runs the seven avatar CTest entries outside `CnaTests` (EasyGL avatar
render, three C API smokes, three link-footprint gates; their targets are built by the build stage)
and fails one that is not registered or does not pass. Each fault was injected (a rename, a test
and a suite disabled, a removal, a new test, an unexpected and a wrong-renderer skip, a failure,
exit status 1 with a clean report, a test missing from the report, a suppressed one, an
unregistered CTest entry) and failed the stage; `--write-expected` reproduces the file byte for
byte. `AFailedGpuUploadFailsEveryDrawCleanlyRatherThanHalfBuilding` skips on every renderer of
today (none refuses a vertex buffer): it stays allowed, and is the one required test that runs
nowhere.

## AVH-03 visual manifest

`check` compared renders with goldens and the stored coverage tags with `REQUIRED`, but never asked
whether the manifest was still what its sources make: its `catalogRevision` was written and never
read, a hand-edited tag or a stale golden file passed, and nothing showed that regenerating it gave
the same file. Regenerating it at HEAD did reproduce it byte for byte (39 s). Now the manifest also
records `catalogSha256` (catalog.json's); `stale()` fails a manifest made for another catalog
version, revision or catalog.json, other size/background/golden renderer, a job count other than
`VIEWS`', a job without coverage or of another catalog version, a golden that belongs to no job, a
job without a golden, and a golden of another size; `manifest --check` regenerates and fails unless
the result is byte for byte the file. `check` runs both (`--no-reproduce` leaves the regeneration
to a separate `manifest --check`, which the acceptance runs once as `visual manifest`); `update`
refuses while the manifest is stale. Each fault was injected and failed (revision 4, another
catalog.json, a dropped job, emptied and lost coverage, a description naming catalog 2, an extra
and a resized golden, an added coverage tag the regeneration does not make). The only change to
the checked-in data is the new `catalogSha256` line; the 36 goldens are unchanged. OPENGLES3's
`check` and `GamerServices_AvatarVisualRegression` pass (worst mean 0.043/255, block 1).

## AVH-04 documentation

The docs stage checked 16 claims in three documents and never read `link-size-architecture.md`,
which still gave the catalog "at revision 4" twice, or the visual-regression paragraph, which says
"eight face presets" where the manifest's views use nine. It now checks 20 claims in four documents,
a claim that occurs several times at every occurrence (the link-size page's two), and the visual
paragraph against the regression manifest and script (36 views, nine described and three random
avatars, `--random 3 --seed 2026`, nine face presets); with the old text it fails on exactly those
three statements. Fixed: the link-size page's two revisions, the face-preset count, and
`LinkFootprint.cmake`'s "~13 MB" comment (revision 1's size; it now names no figure). `--static` runs
what needs no build (catalog, docs, the visual manifest's metadata from AVH-03) and is the CTest
entry `GamerServices_AvatarAcceptanceStatic` (labels `avatar`, `configuration`, 0.3 s). The library
size the pages state (22.3 MB) needs a build and is checked with the measurements (AVH-05).
Limitations item 33's revision 4 review is dated history and stays; AVH-07 brings its current
evidence up to date.

## AVH-05 measurements

No stage measured anything: the documented sizes and load times were figures someone once wrote
down. A `measure` stage, reusing what exists, now checks: the built
`libcna_gamerservices_avatar_catalog.a` (22,324,512 bytes) against the three places the
documentation states 22.3 MB; from the link-footprint probes' own `size` output (the ctest stage),
that the Gamer Services game's text exceeds the 2D game's by at least the catalog and that the 2D
game's (10,326,194 bytes) stays within 5 % of that baseline, compared only on the toolchain it was
taken with (GCC 14.2.0 Debug); and, from a new test run in a process of its own,
`AnAvatarIsReusedWhileDrawnAndReleasedWithItsTexturesAfterwards`, the first, next and reused avatar's
load times against twice the figures `avatars.md` states, and that the caches let go. The test reads
a new internal `avatarCacheStatisticsForTesting()` (live assembled avatars, cache keys, parsed models,
live textures, face atlases; no public API) and fails unless a reused avatar is Ready at once, the
avatars and their 9 textures are released when their renderers are disposed, the parsed-model cache
stays within its 96 and a new avatar prunes expired keys. Measured at revision 5 (Debug, five runs
under the shared machine's load): first 352-403 ms, next 138-203 ms, reused 0.21-0.28 ms; 14 parsed
models kept. `avatars.md` stated revision 4's 450-500 and 200-230 ms and now states these, with the
earlier figures as history. Injected: a lower 2D baseline, the catalog in the 2D game, a load-time
factor of 0.1 each fail; another toolchain skips only the baseline comparison.
`acceptance_tests.json` now requires 233 tests.
