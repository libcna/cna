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
| AVA-03 | Documentation drift (revision, sizes, folds, proportions) | open |
| AVA-04 | One Avatar v3 acceptance command | open |
| AVA-05 | Visual regression: checked-in manifest and goldens, legacy script retired | open |
| AVA-06 | Earrings fitted to every ear shape | open |
| AVA-07 | Planted-foot drift across bodies, heights and proportions | open |
| AVA-08 | Deep-crouch clothing show-through | open |
| AVA-09 | Severe hair/headwear intersections | open |
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
