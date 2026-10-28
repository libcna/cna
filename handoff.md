# handoff.md — current state of CNA

This file describes the repository as it is now and is kept true. Until 2026-10-11 it was `NEXT.md`
and also carried the dated session log of 2026-07 to 2026-10; that log is in Git history
(`git show 8b5564f1e:NEXT.md`).

## 0. CNA/cna-demos repair campaign handoff — 2026-10-24

This is the current continuation point for a fresh-context agent. It is not a claim that the
overall campaign is complete. Push only on the owner's word, Sharp Runtime before CNA before
cna-demos; the 2026-10-24 work is published.

Repositories and branches:

- CNA: this repository, branch `next`, published through the commit recording this (2026-10-24,
  pushed on the owner's word together with Sharp Runtime and cna-demos).
- cna-demos: `../cna-demos`, branch `next`, published through the same push (`dd0f284` and the
  commit recording it). Its `docs/CNA_ISSUES.md` is the permanent historical inventory and its
  assertions remain the oracle.
- Sharp Runtime: `../sharp-runtime`, branch `next`, published through `f990d074` (XmlSerializer
  property members, which CNA `bbed8c61e` needs). Do not make an incidental dependency edit.
- Museum and cna-engine inventories are consumer-owned. Do not edit them from CNA; their agents
  move pins/findings after published CNA commits are available.

Campaign accounting from `CNA_DEMOS_REPAIR_MATRIX.json`: **156 historical IDs, 156 final
dispositions**: 119 `FIXED_VALIDATED`, 23 `DOCUMENTATION_CORRECTED`, 8 `ALREADY_FIXED`, 3
`FIXED_PLATFORM_VALIDATION_PENDING` (003 SpriteBatch Textures[0], 101 video URI resolution, 152
StorageContainer FileShare: each needs a native Windows/XNA run this machine cannot do), one
`DEMO_EXPECTATION_BUG`, one `DUPLICATE_ROOT_CAUSE` and one `UNSUPPORTED_CONTRACT_CORRECTED`. The
three graphics findings outside the historical count are resolved and recorded in the matrix:
`DC-R1` `FIXED_VALIDATED` and `DC-R2` `UNSUPPORTED_CONTRACT_CORRECTED` on the owner's decisions,
and `DC-R3` `FIXED_VALIDATED`. No demo carries a `knownIssue` marker any more.

### Latest accepted work

- `BENCH-001` (2026-10-27; CNA commit `421a38e4f`, consumer commit `ee012a8`): the external
  `cna-benchmark` now
  builds against the current CNA tree and runs all seven scenes on SDL_GPU and OPENGLES3. Its
  streaming scene unbinds the sampled texture before the next upload. CNA reuses an unshared
  single-level texture renderer for full `SetData` while preserving copy isolation for shared
  renderers. The Texture2D slice passes 55/55, benchmark CTest 10/10, and both 300-frame GPU runs
  complete on the private display.
- `cna-rts` consumer intake (RTS-001/RTS-002, 2026-10-27; CNA commit `3b43bd868`, consumer commit
  `5b5dc24`): its September findings on
  render-target PNG/JPEG saves, EasyGL shader-dialect reporting and raw Color texture uploads are
  already answered by the current CNA code. The EasyGL render-target effect-source C1 fixture now
  exercises a real custom 3D shader, its `uRtFlipV` row correction, and ordinary-texture versus
  render-target pixels (4/4 focused checks; full registered fixture passed). The consumer's first
  100k draw exposed its default Reach profile, whose 65,535-instance limit matches Microsoft XNA
  4.0; `cna-rts` now selects HiDef and saves screenshots directly from its RenderTarget2D. Its
  private-GPU 10k/100k/1M sweep completes with one draw call per scale, and the 10k/100k/1M
  deterministic worker-count checks match.
- The final cna-demos handoff audit (2026-10-24) reconciled the repair matrix's third
  platform-pending row: ISSUE-152 now uses the same
  `RESOLVED_PENDING_PLATFORM_VALIDATION` status and structured `PENDING_NATIVE_WINDOWS`
  requirement as ISSUE-003 and ISSUE-101. No product source changed. A fresh cna-demos validation
  against this CNA tree passed all 732 enabled CTests (687 PASS, 45 declared SKIP; three manual
  tests disabled), and the complete 719-ID sweeps were SOFTWARE 671/0/45/3 and OPENGLES3
  709/0/7/3. The only remaining campaign actions are the three native-Windows checks and the
  consumer-owned museum reruns described under "Exact next task".
- Consumer intake, museum `docs/CNA_FINDINGS.md` Open section (2026-10-24): every entry was checked
  against HEAD; the museum reran them all on 2026-10-25 against `9d49828f4` and moved each one it
  verified to its "Fixed in CNA" section (see "Exact next task"). Fixed here: MouseState/GamePadState text as XNA's IL
  formats it (`0ef875619`), six Pipeline Workshop divergences 3/4/5/10/11/12 (`889a31be4`), TaskItem
  equality (`48a5c75eb`), the unsigned ENet fragment bitmaps (`03b92269c`), a readable refused
  vertex type (`a80534927`), the WAV/devices.h and content-task link documentation (`acc2ee55b`,
  `626de40e6`), and on the owner's decisions `RendererFeature::RenderTargetReadback` (36,
  `e357bc4c4`), PresentationParameters with XNA's defaults (0x0, full screen; CNAEXT
  `CreateWindowedEXT`, `e460e0ed6`) and XNA's TextureImporter/TextureProcessor answering an
  XNA-typed `BuildAndLoadAsset<TextureContent, TextureContent>` (`671d8478e`). Found on the way:
  `17cd5d3c3` (EasyGL drew a colourless PBR record black since `bb6a2f44e`) and `b747faca2` (the
  input gate's filter picked up a media test). Then the three items that needed design: XNA's
  `XmlSerializer` text for Color, Rectangle and Point (`bbed8c61e`, Sharp Runtime `f990d074` adds
  property members and document-order reads; oracle `../cna-demos/docs/repair-evidence/content/
  XmlColorRectangleOracle.cs`), and generated nested names chosen against the whole build:
  `98aa655f2` shares one asset's names with the contexts `Convert` creates, and `419723275`
  resolves two nodes that generated one name for different assets in node order,
  rebuilding the later node with the name reserved, whatever the worker count and under
  incremental builds. The pending-pan TSan race was already fixed by
  `594d7d328`. Checkpoint after all of it: full cmake-build-multi CTest 12,542 of 12,563 (the 21
  others are the multi-tree compiled-effect and audio-driver artefacts, which pass with their
  renderer, and the faults fixed above); demos tree rebuilt from CNA `17cd5d3c3` plus the texture
  route, 719-ID sweeps SOFTWARE 670/1/45/3 and OPENGLES3 708/1/7/3 with the one failure a demo check
  corrected in demos `8855ae3` and then passing, and serial CTest 687 PASS/45 SKIP/0 FAIL/3 disabled
  (838 s); reports `../cna-demos/build/linux-full/reports/repair-museum-closure-*`.
- handoff §5.1's one defect, `SpriteSortMode::Immediate` batched on `SDL_GPU` and `WEBGPU`: an
  upload to a texture a queued draw samples is now ordered after that draw (SDL_GPU submits the
  queued work first, WEBGPU writes Texture2D into a fresh texture), and
  `EasyGL_SpriteBatch_SortModeSemantics` leg B asserts XNA's red/blue on SDL_GPU, WEBGPU,
  OPENGLES3, OPENGL33, SOFTWARE and FNA3D. WEBGPU's TextureCube and Texture3D, found by reading,
  rename the same way since the commit after it, and `*_Conformance_texture_setdata_after_draw`
  checks a Texture2D and a TextureCube changed between two draws on every pixel renderer.
- DC-R1 (`51fb39d5a`, owner: implement): SDL_GPU binds a RenderTargetCube face in any slot of a
  target set. DC-R2 (`c7f6c68e5`, owner: all-or-none exact, refuse others): SDL_GPU applies a
  MultiSampleMask keeping every sample or none and refuses any other, and
  `RendererFeature::MultiSampleMask` (35, C ABI 0.50.0) reports each renderer's mask support,
  checked against a 4x probe. `cube_mrt_binding` passes M1-M4 on SDL_GPU. Serial cna-demos CTest
  after both (demos tree rebuilt from CNA `c7f6c68e5`): 732 enabled, 687 PASS/45 SKIP/0 FAIL/3
  disabled (870.97 s), the batch 9 outcome; reports
  `../cna-demos/build/linux-full/reports/repair-dcr-closure-ctest.*`.
- DC-R3 (`9ef292993`): `cna_shader_effect_create` copies a whole SPIR-V module byte for byte
  instead of refusing it as non-UTF-8 text, so a C caller can give SDL_GPU (dialect SPIRV) a custom
  effect it runs. Found when `c_api.graphics.shader_effect_state`, which had used a GLSL
  placeholder there, was given a SPIR-V pair (demos `77444d7`); it passes 17/17 on SDL_GPU.
- Batch 9 closed (`FIXED_VALIDATED`, 2026-10-24): 005 (a vertex holding CNA's 16-byte Color is a
  compile error in the raw user draws), 007 (SpriteBatch refuses a ColorMatrixEffect where the
  renderer does not apply it), 008 (no Prop.hpp macros from CNA's public headers), 067 (C colour
  space names), 066 (owner: SetBlendEnabled installs non-premultiplied blending on every
  renderer), 022 (owner: a glTF's extracted textures are always Texture2D children, CNB and
  XNB), 031 (STEP hold keys and CUBICSPLINE samples survive linear clip playback), 023 (glTF
  cameras in the Model CNJ and an optional MCAM CNB chunk) and 006 (RendererFeature
  SamplerLevelOfDetailBias, PbrEffectShading, SkinnedPbrEffectShading, each checked against
  every renderer's pixels; this fixed FNA3D's SpriteBatch dropping the batch's mip state and
  corrected headers that called SOFTWARE's PBR a fallback). `30afa280b` bumps the glTF Model
  pipeline component versions so existing content builds recompile. C ABI 0.50.0 (published 2026-10-24)
  carries the camera routes and feature constants; 3,212 exports.
- galaxy-eggbert UB-003 (`f8e2b8cc6`): the avatar converter writes XNA's clockwise front faces;
  galaxy-eggbert re-verified it.
- Batch 8 (081, 091-096, 098, 104, 114) and ISSUE-010/020/029/062 closed earlier on 2026-10-23/24.

The latest complete cross-subsystem checkpoint (2026-10-24, CNA `96577db26`): `cmake-build-multi`
fully rebuilt, full CTest 12,553 of 12,569 passed in 927 s; the 16 others are the 15 SDL_GPU/FNA3D
compiled-effect entries and `CApi_AudioUnavailableSmoke`, and pass when rerun with
`CNA_GRAPHICS_RENDERER=SDL_GPU` (62/62), `=FNA3D` (37/37) and without `SDL_AUDIO_DRIVER=dummy` (1/1).
Demos tree rebuilt from the same CNA: 719-ID sweeps SOFTWARE 671 PASS/0 FAIL/45 SKIP/3 NOTRUN and
OPENGLES3 709/0/7/3, against the consumer intake checkpoint only
`game.graphics_device_manager.preparing_device_settings` FAIL-to-PASS (the demos `8855ae3`
correction); serial CTest 732 enabled, 687 PASS/45 SKIP/**0 FAIL**/3 disabled (791.75 s). Reports:
`../cna-demos/build/linux-full/reports/repair-final-checkpoint-*` and
`cmake-build-multi/pp-validate/final-full-ctest.log`. Before it, the consumer intake checkpoint
under "Latest accepted work", and batch 9 (2026-10-24, demos
tree rebuilt from CNA `30afa280b`): full 719-ID SOFTWARE 669 PASS/2 FAIL/45 SKIP/3 NOTRUN, OPENGLES3
707 PASS/2 FAIL/7 SKIP/3 NOTRUN; the two failures on each were `content.model.pipeline_contract` and
`content.xnb.output_pipeline` pinning the old identities, corrected in demos `dcc3811` and passing
on both renderers. Against batch 8 only FAIL-to-PASS and SKIP-to-PASS rows changed otherwise. Serial
CTest: 735 registered/732 enabled, 687 PASS/45 SKIP/**0 FAIL**/3 disabled (954.26 s). Reports:
`../cna-demos/build/linux-full/reports/repair-batch9-closure-{full,ctest}*`.

Native suites must run through the private runner with `SDL_AUDIO_DRIVER=dummy` (and from the
source directory for Content/Media): without it SDL's ALSA device reports "Host is down" and the
SoundEffect/video-audio tests fail or abort for environmental reasons. In `cmake-build-multi`
the SDL_GPU/FNA3D compiled-effect tests need `CNA_GRAPHICS_RENDERER=SDL_GPU`/`FNA3D`. A change to
what a content importer, processor or writer produces must bump its identity version, or
incremental builds keep stale outputs.

### Exact next task

The museum Pipeline Workshop items are all answered: 1 (build-wide nested names, `98aa655f2` and
`419723275`), 9 (`bbed8c61e` on Sharp Runtime `f990d074`, measured against
genuine XNA, demos `9598b43`) and 16 (owner: the documentation half is enough; the task sources
stay in `modules/content-pipeline`). What remains needs a host this machine does not have: the
three Windows/XNA platform validations (003, 101, 152). The museum's reruns of
2026-10-25 against `9d49828f4` are in `../museum-simulator/docs/CNA_FINDINGS.md`: every finding it
can run here is verified fixed or documented, the ThreadSanitizer races and ENet's UBSan shift
included (a `-fsanitize=thread,undefined` museum tree: no report), and so is the Android emulator's
Float16 readback (`53292f147`: the half-float target reads 2.0). None stays open there.
The museum repair log `CNA_MUSEUM_REPAIR_HANDOFF.md` was removed then; it is in Git history
(`git show 9d49828f4:CNA_MUSEUM_REPAIR_HANDOFF.md`).
Triaged, not a defect: in `cmake-build-multi`, `cna_c_api_effect_smoke` run with
`CNA_GRAPHICS_RENDERER=WEBGPU` fails its stage 3 because a WEBGPU effect on the WEBGPU-76 WGSL
contract has no cube sampler and refuses one with `NotSupportedException`
(`WebGPUDescriptorEffect.cpp` `BindTextureCube`), which the C route reports as
`CNA_RESULT_NOT_SUPPORTED`; the test, registered under the tree's HEADLESS default, assumes cube
binding. `effects.h` now documents that refusal on the three texture-binding routes. STUB fails
the same stage creating textures, an expected STUB artefact.

Consumer-only work still matters. cna-engine's `upstream-issues.md` reports UP-001, UP-006, UP-018,
UP-019 and UP-020 resolved; its UP-002 (CNAEXT on C++ spellings of implicit XNA semantics) is
answered by `99b54079e` in `docs/migration-guide.md` and awaits cna-engine's own reading. The
museum has rerun its findings (above); the
consumers also need to know that `new PresentationParameters()` is now XNA's 0x0 full-screen value,
so a device built from it directly must state a size. Do not claim UBSan/TSan/hardware/service
validation when its required tree or hardware is absent.

Build discipline remains: existing build directories only, shared
`CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv`, at most 8 jobs and preferably one compiler under
the established 4 GiB high / 6 GiB max / no-swap scope. All GPU/UI tests go through
`tools/platform/run_gpu_tests_private.sh`. Never weaken a demo/test oracle, and update the matrix,
permanent issue history and handoff after every accepted batch.

**§5 plus `CNA_DEMOS_REPAIR_MATRIX.json` are the authoritative current defect inventories during
this campaign.** §5 records the ordinary CNA maintenance list; the matrix additionally records
every historical cna-demos finding, including pending items that are not yet confirmed defects.
Capability documents describe supported behavior, tests are executable evidence, and plans/Git
history preserve measurement history. Do not let an issue disappear from either applicable list.

Repository state last reconciled: **2026-10-24** (`next`, `96577db26`). The last full test run is
the checkpoint identified above; do not upgrade later focused evidence into a full claim.

## 1. Project summary

CNA is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`) on a
CNA-owned platform abstraction (SDL3 by default) and a curated set of graphics renderers. FNA's
source is the day-to-day behavioural reference; where FNA and XNA disagree, XNA wins (`CLAUDE.md`).

The project is leaving its AI-driven feature-expansion phase for long-term maintenance by a human
C++ developer. The renderer set, the platform layer and the content formats are deliberately kept as
they are: no new renderers, asset formats, graphics engines or subsystems. Work from here is fixing
§5, keeping behaviour faithful to XNA, and keeping validation deterministic.

## 2. Current status

**Renderers.** 10 public identities over 8 implementation families — `OPENGLES3`, `OPENGL33`,
`WEBGL2`, `WEBGPU`, `HEADLESS`, `SOFTWARE`, `STUB`, `DIRECTX9`, `SDL_GPU`, `FNA3D`; the three GL
identities share the internal EasyGL implementation. `SDL_GPU` is CNA's modern native path: SDL
owns its Vulkan, Direct3D 12 and Metal backends. The canonical list is
`cmake/RendererIdentities.cmake` (held to the C++ enum and the C ABI by
`scripts/check_renderer_identities.py`); the table with gates and factories is
[`docs/renderer-registry.md`](docs/renderer-registry.md). 41 identities are retired — `VULKAN`,
`DIRECTX11`, `METAL` and `SDL_RENDERER` on 2026-10-13 — and refused by name at configure time and at
run time; their reserved ABI values are in [`docs/removed-renderers.md`](docs/removed-renderers.md).
Do not add or restore a renderer without an explicit owner decision.

**Graphics layer.** Every renderer implements one contract, `IGraphicsRenderer`, for XNA graphics
plus CNA's graphics extensions (SPIR-V/GLSL/WGSL `ShaderEffect` with vertex/fragment
`ShaderPackageEXT`, PBR and skinned PBR, image-based lighting, shadow reception, float render
targets, instancing, compiled effects, `Texture3D`/`TextureCube`). There is no
second, general-purpose GPU layer underneath: the compute-shader, storage-buffer/image,
texture-array, memory-barrier, indirect-draw and GPU-timer abstractions were retired end to end on
2026-10-13 (MRT; C ABI 0.47.0, `docs/c-api/ABI_VERSIONING.md`), including their common interfaces,
backend implementations, capability/limit identities, C API routes and tests.
`modules/graphics-ext/` remains (`docs/cnaext-engine-layer.md`).

**Platforms.** `CNA_PLATFORM` is `SDL3` (default; Windows, X11, Wayland, macOS, iOS, Android and the
browser are all reached through SDL3's drivers), `HEADLESS`, or POSIX-only `TERMINAL`. CNA's own
`WIN32`/`X11`/`WAYLAND` backends were retired on 2026-10-06 and are refused by name.
`CNA_AUDIO_PLATFORM` is `SDL3`, `NULL` or `ALSA`. Evidence differs per target:

| Target | What is actually verified |
|---|---|
| Linux x86_64 | Everything; this is where the suites run. |
| Windows | MinGW-w64 cross builds run under Wine: `DIRECTX9` through DXVK, `SDL_GPU`'s Direct3D 12 backend through vkd3d-proton, `SOFTWARE`; the manual MSVC workflow builds and runs the `DIRECTX9` suite and, since RRC-051, an `SDL_GPU` Direct3D 12 smoke leg (`scripts/ci/run-sdlgpu-smoke.sh`; not run yet, the workflow is manual). No real-Windows hardware validation is current (§5.2). |
| Web (Emscripten) | `WEBGL2` and `WEBGPU` builds run in Chrome (`docs/web-emscripten-graphics-limitations.md`). |
| Android | `OPENGLES3`, the default there, on an x86_64 emulator; `SDL_GPU` on Android is not qualified (`docs/android-graphics-limitations.md`). |
| macOS / iOS | macOS CI builds CNA with `SOFTWARE` and runs the portable platform suites and a bundle smoke app, and (gating, since RRC-051, not run yet) builds `SDL_GPU` and runs its smoke set on SDL's Metal backend; iOS is a `SOFTWARE` simulator smoke app, with non-gating `SDL_GPU` legs. `SDL_GPU` is the default on both but is not re-qualified there (§5.2, `docs/apple-platforms.md`). |

**XNA API surface.** Every public type and every documented member of the Microsoft XNA 4.0 runtime
reference is represented; the generated census is
[`docs/xna-4-runtime-member-coverage.md`](docs/xna-4-runtime-member-coverage.md)
(`python3 tools/audit_xna_runtime_surface.py --write-reports`). Representation is not behaviour:
behavioural fidelity is held by each module's tests.

**Bindings.** C and C# are the maintained bindings. CNA.NET (`libcna/cna-dotnet` `develop`) admits
exactly C ABI 0.47.0 and pins one CNA `next` commit (`CNA_UPSTREAM_COMMIT` in its
`.github/workflows/build.yml`); its CI builds and loads the native library from that commit. CNA
`next` is ABI 0.50.0: 0.48.0 (the 2026-10-13 renderer retirement) removed four renderer constants,
0.49.0 (2026-10-17) renamed the renderer classification typedefs, constants and eight routes
from `graphics_backend` to `graphics_renderer`, and 0.50.0 (2026-10-23, galaxy-eggbert UB-002)
added the two `cna_skinned_model_ext_{get,set}_part_diffuse_color_at` routes. CNA.NET names none
of them and must admit 0.50.0 before it moves its pin. CNA `main`/`develop` are the v0.1.0 release (ABI 0.46.0), which CNA.NET
refuses. The archived bindings in `cna-lab` stay pinned to older ABIs and are not maintained.

**Dependencies.** FNA3D and MojoShader are CNA's forks, `libcna/FNA3D` and `libcna/mojoshader`,
pinned at `10f650b9d0f6` / `03b686fe1a33` on their `cna` branches (`docs/fna3d-renderer.md`). Both
carry the fixes for the 2026-10-26 dependency audit (reports in
`/rv/tmp/cna-dep-migration-codex-audit-2026-10-26/reports/`) and for the independent review of those
fixes the same day (`/rv/tmp/cna-audit-remediation-codex-review-2026-10-26/raw/`, passes R1-R4,
R5-R8 and R9-R10 in three rounds;
each fix names its finding in a `CNA-Audit-Finding:` trailer, CNA's own in its commit subject) and
regression suites of their own:
`tests/cna` in each, built with `-DMOJOSHADER_CNA_TESTS=ON` / `-DFNA3D_CNA_TESTS=ON` (FNA3D's need a
display: run them through the private runner). SDL is `86296ac8` with five CNA patches
(`cmake/ThirdPartySDL.cmake`), ENet upstream master `5a9c537`.

**Content.** XNB readers (including LZX), CNA's `.cnb`/`.cnj`, direct glTF 2.0 loading, the
`cna-content` build tool and the XNA Content Pipeline façade: `docs/content-pipeline.md`,
`docs/xna-content-pipeline-parity-report.md`, `docs/gltf-limitations.md`. No further formats are
planned.

**Tests.** GoogleTest under CTest; `ctest -N` in a build tree gives its count. At the FNA3D pin
`10f650b9d0f6` (2026-10-26, end of the dependency-audit remediation and its review),
`cmake-build-multi`'s `CnaTests` with the audit's graphics filter plus `XnaMultisamplePixelCenter*`
and `GltfRendererPbrFallbackPolicy*` ran 1,683 tests with no failure on each of `OPENGLES3`,
`OPENGL33`, `WEBGPU`, `SOFTWARE`, `SDL_GPU` (Vulkan) and `FNA3D` with its OpenGL and its SDL_GPU
driver (the audit had measured 6 failures on `SDL_GPU` and 15 to 25 on each of the others, all
fixed or test defects); its network
filter 574/574 and the platform suites 1,553 on X11 and 1,553 on Wayland, none failed. The family
example suites of the renderers B2-04 changed, each in its own default tree (Release; `FNA3D` at
`10f650b9d0f6`, the others at `aa3139087e69` with MojoShader `bbee0a5fab1f`): `FNA3D`
63/63; `SDL_GPU` 65 selected (MSAA, render-target, cube, readback, effect and
`SdlGpu_TextureDefragment`), every pixel check passes, two debug-layer assertions need a Debug tree;
`WEBGPU` 41/41, `SOFTWARE` 34/34; `render_target_own_msaa` and `cube_mrt_binding` pass on all five
families. Fork
suites: MojoShader `tests/cna` 16/16 normal and under clang ASan+UBSan+LSan with no check excluded,
with its 14 SDL_GPU device cases on RADV; FNA3D `tests/cna` 21 (27 with `TRACING_SUPPORT`) under
ASan+UBSan on OpenGL, OpenGL ES 3 and SDL_GPU, and its eight executables on D3D11 under Wine with
DXVK 3.1.1. Cross builds at `10f650b9d0f6`: `cmake-build-mingw` under Wine with DXVK 3.1.1
ran `DirectX9CompiledEffect*` 31/32 (one declared skip) and `XnaMultisamplePixelCenter*` /
`RenderTargetReadbackLayout*` 5/6 (one declared skip) on `DIRECTX9`; the Emscripten
`cna_webgpu_compiled_effect_page` passed 21/21 in headless Chrome; Android was not rebuilt at this
pin (at `aa3139087e69` Android x86_64, NDK 29, built SDL with its patches and `cna_platform`, not
run). Latest full Debug runs, 2026-10-17 at the end of the handoff 5.1 stabilization pass (branch `stabilization`, worktree
`cna-stab`), through the private runner on RADV: `cmake-build-multi` (`OPENGLES3` default with
`OPENGL33`, `WEBGPU`, `SDL_GPU`, `FNA3D`, `SOFTWARE`, `HEADLESS`, `STUB`; C API) 11,433 tests, 1
failed (`EasyGL_Conformance_render_target_own_msaa`, §5.3), 195 skipped;
`cmake-build-sdlgpu` 11,284, none failed, 256 skipped; `cmake-build-webgpu` 10,964, none failed, 256
skipped; `cmake-build-software` 10,982, none failed, 101 skipped; `cmake-build-fna3d` (its OpenGL
driver) 10,806, 2 failed, 400 skipped -- `ContentPipelineCliTest.ContentBuildGraphReportsALongCycleDeterministically`
and `Sdl3InputTest.MouseReadsThePointerBesideTheWindowAsXnaDoes`, which pass alone (three of three);
the second reads the pointer of the private display that parallel tests also move, and failed the
same way once in an earlier SOFTWARE run. Under Wine, `cmake-build-mingw` (Release, `DIRECTX9` with
DXVK 2.6.0): `CnaGraphicsTests.exe` in eight shards 2,688/2,688, and the `DIRECTX9` ctest suite
59/59 without `DirectX9_CompiledEffects` (§5.3); with DXVK 3.1.1 the whole suite 60/60, twice.
`cmake-build-headless` was not re-run. Every run excluded `XnaPipelineGenuineRuntime` and
`XnaDifferentialBuildTest` (Wine-hosted XNA builds), which §7 says to run outside the private
runtime. CI: `general-tests-ci.yml` runs the unfiltered suite for
`OPENGLES3` and fails on a dirty checkout; the other workflows are subsystem- or platform-specific.

## 3. Where current information lives

| Question | Source of truth |
|---|---|
| Is X broken right now? | §5 of this file |
| Which renderers exist, how are they selected? | `docs/renderer-registry.md`, `docs/runtime-renderer-selection.md` |
| Does renderer R support feature F? | `docs/graphics-renderer-feature-matrix.md`, `docs/renderer-capability-profiles.md`, each renderer's own doc |
| Compiled XNA effects per renderer | `docs/fx-compiled-effects.md` §10 |
| Platform contract | `docs/platform-abstraction.md` |
| API-surface coverage | `docs/xna-4-runtime-member-coverage.md` (generated) |
| C API generation maintenance model and 2026-10-12 audit | `docs/c-api/GENERATED_ROUTES.md`, `docs/c-api/GENERATION_AUDIT.md` |
| Deliberate deviations from FNA/XNA | `CHECKLIST.md`, `docs/xna-fna-behavior-decisions.md` |
| GamerServices / Net / Avatar boundary | `docs/gamer-services-known-limitations.md` |
| Why is a subsystem in (or out of) an executable? Link boundaries, size options, footprint gates | `docs/link-size-architecture.md` |
| Build rules (ccache, build directories, display isolation) | `CLAUDE.md` / `AGENTS.md` |
| Index of the other documents | `docs/README.md`, `plans/README.md` |

## 4. Working rules for this phase

- Fix defects, do not add surface. A fix comes with a focused regression test.
- Never run tests on the live desktop: use `tools/platform/run_gpu_tests_private.sh`.
- A test may write only to its build tree or a temporary directory. `scripts/check_clean_checkout.sh`
  enforces that a validation run leaves tracked files unchanged.
- Build into the existing `cmake-build-<variant>/` trees with ccache; do not create one-off build
  directories (`CLAUDE.md`, "Build locations & caching").

## 5. Known bugs and limitations

The single authoritative list. The stabilization pass of 2026-10-16/17 fixed or removed every
reproduced CNA defect that was on it; an entry says when it was last measured.

### 5.1 Defects

- `Microphone::Start` reports `MicrophoneState::Started` even when the capture device failed to
  open: `modules/audio/src/Xna/Microphone.cpp` swallows the failure (`catch (...)`, following FNA)
  and sets the state unconditionally. Microsoft XNA's `Microphone.Start` hands the native start's
  error code to `Helpers.ThrowExceptionFromErrorCode`, so a failed start throws there, and XNA
  wins. Found 2026-10-28 by reading XNA's decompiled source (former AUD-13-001); not yet pinned by
  a test.
- `CnaGraphicsTests.exe` on `DIRECTX9` (`cmake-build-mingw`, Wine with DXVK 3.1.1, eight shards,
  2026-10-28) fails three cases that also fail on `ed724c017` without that day's null-texture
  change: `PresentationLifecycle.EveryPresentIntervalRoundTripsThroughReset` and
  `.IsFullScreenRoundTripsThroughResetBothWays` -- `GraphicsDevice::Clear` after a presentation
  `Reset` throws `DeviceLostException` ("The graphics device was lost.",
  `PresentationLifecycleTests.cpp:223`/`246`) -- and
  `ShadowTargetOrientation/2.RenderedUpperHalfOccluderShadowsOnlyUpperHalf` (`PbrEffect`), where
  DirectX9 refuses the 68-byte PBR vertex layout ("stride 68 has no matching CNA vertex layout,
  expected 48 or 60"). The shards last passed 2,688/2,688 under DXVK 2.6.0 (see §2); not bisected.
- `WEBGPU` fails two shared presentation programs it is not registered for, which EasyGL and
  `SDL_GPU` pass (`cmake-build-multi` run with `CNA_GRAPHICS_RENDERER=WEBGPU`, 2026-10-28):
  `presentation_mode_contract_test.cpp` under Letterbox and Overscan, where a sprite covering the
  40x40 logical canvas after a mid-frame `SetPresentationMode` lands unscaled (40x40 at the
  letterbox origin instead of filling 96x96), and `spritebatch_presentation_test.cpp`'s two
  Deferred batches under different custom viewports, which both read black. Not investigated.
- `WebGPU_BindingCost` aborts with "The texture resource is in use." (`cmake-build-webgpu`,
  2026-10-28): `webgpu_binding_cost_test.cpp`'s `DrawRenderTargets` calls `SetData` on a texture
  the previous SpriteBatch left in `Textures[0]`, which `Texture.cpp` refuses as XNA does
  (SOFTWARE-246). The test needs to unbind the slot first; the other 187 `WebGPU` tests pass. When
  it began to fail is not bisected.

### 5.2 Limitations

- `SDL_GPU` applies `BlendState.MultiSampleMask` on a multisampled target only when it keeps every
  sample or none: the pinned SDL reserves `SDL_GPUMultisampleState::sample_mask`/`enable_mask`, so
  a mask of none drops the draw and any partial mask throws `System::NotSupportedException`
  (DC-R2, owner decision, `c7f6c68e5`). `RendererFeature::MultiSampleMask` reports each renderer's
  answer; `ColorWriteChannels` is honoured.

- Native `SurfaceFormat::Alpha8` render-target storage is unavailable on every renderer:
  `GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Alpha8)` is false, and a
  render target preferring Alpha8 falls back to `Color` as XNA's preferred-format rule allows
  (`GraphicsCapabilityFloatRenderTargetTests.cpp`). Alpha is kept; the one-byte-per-pixel storage
  is not (`Color` is four). How the Xbox 360 allocated an Alpha8 target was never captured.
- `FNA3D` refuses a `PbrEffect` or `SkinnedPbrEffect` draw whose vertex declaration has no
  `TANGENT0` with `System::NotSupportedException` (`FNA3D-MISSING-STOCK-INPUT`,
  `modules/renderers/fna3d/src/Fna3dDraw.cpp`) wherever FNA3D picks its SDL_GPU or Direct3D 11
  driver, which cannot build a pipeline with a missing input; its OpenGL driver and every other
  renderer supply the GL neutral tangent (`91612a0a3`). Measured by the museum's `rv-shading` on
  RADV, 2026-10-25.
- Online sessions and invitations are native-only; a browser build refuses them explicitly
  (`docs/browser-network-readiness.md`, `docs/gamer-services-known-limitations.md` §26).
- `DIRECTX9` is validated under Wine (+DXVK) only, and `SDL_GPU`'s Direct3D 12 backend under Wine
  (+vkd3d-proton) only; real-Windows hardware validation is not current. The C API's pure-C suites
  run under Wine from the MinGW-w64 build; `MediaLibrarySmoke` reports a skip on Windows, whose
  known folders cannot be pointed at its fixture. The MinGW-w64 test binaries also skip on
  Windows what it cannot provide: a Python interpreter where none can be started, `/dev/full`,
  aging a directory through `utime`, symbolic links, a renderer without 3D, and the ordering guard
  for tests that need an uninitialized `GamerServicesDispatcher`.
- **`SDL_GPU` is the default renderer on Windows, macOS and iOS but is not qualified there** since
  the 2026-10-13 retirement of `DIRECTX11` and `METAL` (`docs/removed-renderers.md`). Evidence that
  exists: the full tree passed with `SDL_GPU` (Metal backend) on a Mac mini M4 in the AM4 campaign,
  before the retirement; Direct3D 12 runs under Wine. Not done: a native Windows (Direct3D 12) run,
  a re-run on Apple hardware, and any iOS build (`SDL_GPU` has never been built for iOS; CI tries it
  as a non-gating leg). Known gaps against the retired native backends, each to be implemented or
  capability-gated in that campaign: `ShaderEffect` (`CustomEffects`) is compiled out on Windows,
  Apple and the browser because no runtime GLSL-to-SPIR-V-to-DXIL/MSL path exists; occlusion queries
  are unavailable (SDL_gpu 3.5 has none). `RendererFeature::BaseInstanceDrawing` is implemented by
  `SDL_GPU` since RRC-043 (its stock, instanced-matrix and compiled-effect routes expand per-instance
  streams on the host from the first instance); no other survivor claims it.
- iOS has simulator smoke evidence only, with `SOFTWARE`.
- `WEBGPU` is supported (promoted from experimental on 2026-10-17) and verified on Linux x86_64
  and in headless Chrome only; its Windows, macOS and linux-aarch64 wgpu-native packages are
  unverified (`WEBGPU-1`, `plans/plan_webgpu.md`).
- `SOFTWARE` is a bounded CPU renderer, and `HEADLESS` and `STUB` make no pixel claim
  (`docs/renderer-registry.md`, "Capability classes").
- Per-subsystem capability boundaries, by design rather than defect, are in the documents listed
  in §3.
- In a `CNA_SHARED_LIBRARY=ON` tree, `CMAKE_LINK_DEPENDS_NO_SHARED` (`CMakeLists.txt`) keeps the
  executables from relinking when `libcna.so` changes. After a commit that changes a class layout
  or a vtable, the executables linked before it run against a library they were not linked for
  (the loader warns `Symbol ... has different size in shared object`) and fail for no reason of
  their own; delete and relink them before reading the suite. Seen on 2026-10-13 in
  `cmake-build-multi` after the link-boundary work's virtual-function additions: 237 bogus
  failures from 1,245 stale executables.
- **Pixel-centre bias.** Every renderer moves 3D triangles 63/128 of a pixel, not 1/2, to emulate
  Direct3D 9's integer pixel centres (2026-10-26 audit B2-07). The 1/128 keeps a fill edge that
  falls exactly on a host sample on XNA's side; it is the value DXVK itself applies, and the XNA 4.0
  runs that settled pixel centres on this machine go through DXVK. Interpolated attributes are
  therefore evaluated 1/128 pixel off D3D9's centre. Native Direct3D 9 was not available to
  measure whether it differs.
- **Pixel centres of lines and points.** The XNA correction above moves triangles only, in every
  FNA3D driver; lines and points are drawn at the host's half-integer centres. The MSAA measurement
  that extended the correction to multisampled targets (B2-04) drew triangles, so nothing measured
  says what XNA does for lines and points there.
- **MojoShader SPIR-V gaps** (FNA3D's SDL_GPU driver, CNA's SDL_GPU and WEBGPU compiled effects):
  ps_1_4 `BEM` and `TEXDEPTH` refuse by name ("unimplemented in spirv profile"); GLSL implements
  both.
- **MojoShader FOG and other vertex semantics** (after the review's fixes): FOG in any lane, alone
  or packed, translates in every profile, and SPIR-V's vertex and pixel FOG interfaces match in
  type; GLSL ES and Metal place FOG lanes positionally, so a vertex/pixel pair declaring FOG in
  different lanes behaves differently there than in the other profiles (not measured on Direct3D 9).
  A vs_3_0 PSIZE alone in a lane other than x is still narrowed to x. HLSL refuses FOG, SAMPLE and
  TESSFACTOR vertex inputs (FNA3D's D3D11 table also names Sample `SV_SampleIndex`, invalid as a
  vertex input), and the D3D11 glue's integer-format rewrite looks for `PSIZE0` while HLSL emits
  `PSIZE`. The ARB1 profile still names oFog/oPts FOG1/PSIZE2 and binds no pixel FOG input (no CNA
  path uses ARB1). Vulkan pipeline creation and GL program linking of FOG pairs were not run;
  spirv-val and interface-type comparison were.
- **FNA3D OpenGL ES 3 readback** reads RGBA8, `ColorSrgbEXT` (stored as `GL_SRGB8_ALPHA8` since the
  review; the unsized format FNA3D used before was refused by ES 3 at upload, so such a texture
  never had storage), the float formats, and any other format only where the implementation's own
  read layout is the format's; it refuses the rest by name. On Mesa 25.0.7 (radeonsi) Rgba1010102,
  Rgba64, ByteEXT and UShortEXT read, Rg32 and `ColorBgraEXT` are refused for their layout.
  Desktop GL reads every uncompressed format.
- **FNA3D pixel-store state**: readbacks and uploads save and restore every pack/unpack value they
  set, so another user of the context keeps its own; an upload therefore makes six
  `glGetIntegerv` calls, each a synchronization point under Mesa's glthread.
- **FNA3D tracing with several devices** shares one trace session among overlapping devices
  without telling them apart: the second device's creation is not recorded and its calls replay on
  the first device's state (2026-10-26 review R9-02), and its SwapBuffers with its own window
  override still trips the tracer's assertion. Trace one device at a time.
- **MojoShader's D3D11 glue** (FNA3D's D3D11 driver; found in the review's third round, not
  fixed): a ps_3_0 register packing semantics that the vertex shader writes to separate registers
  (FOG in `o1.x`, TEXCOORD0 in `o2.yzw`, read together from `v0`) produces HLSL naming a member
  that does not exist and fails to compile -- also before the review (R9-01); and the integer
  input-layout rewrite finds a semantic by substring, so `DEPTH1` can match an earlier `DEPTH10`
  and swap their integer/float declarations (R9-03). CNA's committed effects contain neither.
- **SDL_shadercross initialization is the application's** (MojoShader's SDL_GPU glue): the glue
  calls neither `SDL_ShaderCross_Init()` nor `_Quit()`, which act on the whole process without
  reference counting, so DXBC is offered only after the application initialized ShaderCross
  (`mojoshader.h`); FNA3D's SDL_GPU driver forwards DXBC to SDL when it is offered. CNA's SDL_GPU
  renderer initializes ShaderCross; a standalone FNA3D build with `MOJOSHADER_STATIC_SPIRVCROSS`
  must. FNA3D's test of that selector links a stand-in static package (archive shape, C++ runtime),
  not the real one: the only static SDL_shadercross package on this machine was built without DXC
  and its package config requires DirectXShaderCompiler anyway.
- **A shared SDL prebuilt can change under a running build** when a tree with another manifest
  rebuilds it (2026-10-26 review R3-01/R4-01; the watched stamp protects only the next build). Give
  such a tree its own `CNA_SDL_PREBUILT_ROOT` (`docs/development-environment.md`).
- **Audio output has no device-change or device-loss handling of CNA's own.** The SDL3 backend
  opens `SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK`, so a change of the system default is whatever SDL3
  does with a default-device stream; the ALSA backend does not reopen a PCM that disappears.
  Neither case has been measured (former AUD-04-010/011; capture devices do report a lost device).
- **Three audio reports from the 2026-07-17 audit stay unconfirmed, not open:** high-pitched sound
  effects, missing audio files, and distorted or sped-up gameplay audio. They were never
  reproduced against the game they came from, which is not available. The audit narrowed the
  pitch report to a wrong sample rate reaching CNA before the mixer -- the class CBIND-146 fixed
  (`51d9c84cc`, 2026-10-02, an XNA-built sound's content sample rate).

### 5.3 External and driver limitations

Behaviour CNA does not own, measured and reported rather than worked around.

- **Conformance programs on drivers and capability boundaries** (`*_Conformance_*`,
  `modules/graphics/examples/conformance/`, measured 2026-10-15/17):
  - `render_target_own_msaa` D (2x) failed on EasyGL in 2026-10-17's run and on SDL_GPU and FNA3D
    once B2-04 gave multisampled targets XNA's integer pixel centres: its edge sat at the
    half-integer centre, beyond both 2x samples. The edge now runs through the integer centre and D
    passes on EasyGL, SDL_GPU, WEBGPU, SOFTWARE and FNA3D (2026-10-26).
  - FNA3D: `SkinnedEffect.VertexColorEnabled` is ignored outright (B equals A) and saturates before
    the fold; its back buffer keeps a 3, 5 or 7 sample request unrounded while its render targets
    round it down; `GraphicsDeviceManager`'s default `Depth24` request is not applied in
    `graphics_adapter_query`. A `TextureCube` changed between two draws of one frame shows its new
    contents in the earlier draw too (`texture_setdata_after_draw` B, 2026-10-24): FNA3D's SDL_GPU
    driver uploads on a command buffer it submits ahead of the frame's draws and cycles only a whole
    single-level 2D texture, so by its source a `Texture3D` or a mipmapped `Texture2D` does the
    same. FNA3D's driver owns those answers.
  - WEBGPU and DIRECTX9: cube faces in a multi-render-target set are refused with
    `System::NotSupportedException` while `MultipleRenderTargets` is true (`cube_mrt_binding` M1-M4
    and the plural cube-face `RenderTargetSemantics` cases skip on that refusal); HEADLESS accepts
    them. A capability boundary. SDL_GPU binds them since `51fb39d5a` (DC-R1).
- **LeakSanitizer reports leaks in the pinned SDL** (measured 2026-10-22/23 during the museum
  repairs): about 50 KB in about 910 allocations from initialization to destruction with no CNA code
  linked (lavapipe and RADV), so an SDL_GPU sanitizer run does not end at exit 0 under
  LeakSanitizer; CNA suppresses none of it. The MojoShader SPIR-V attribute-fixup leak measured then
  (8,792 bytes in 513 allocations per device), FNA3D's SDL_GPU device-array and vertex-binding
  cache leaks, and the dumpspirv and trace-lock leaks are fixed on the forks (2026-10-26; the owner
  authorized fork changes that day). MojoShader's `tests/cna` runs leak-free under
  AddressSanitizer; FNA3D's does on OpenGL and OpenGL ES 3, and on SDL_GPU every remaining report
  is SDL's own (X11/xkb and Vulkan ICD initialization reached through device creation).
- **`DIRECTX9` under Wine with DXVK 2.6.0: `DirectX9_CompiledEffects` hangs or aborts in DXVK's
  device teardown; DXVK 3.1.1 does not** (measured 2026-10-17, `cmake-build-mingw`, Release,
  `CNA_DIRECTX9_COMPILED_EFFECTS=ON`). Its 31 gtests were run 30 times on each version in one Wine
  prefix that differed only in the DXVK DLLs: 2.6.0 (Debian's package, `/usr/lib/dxvk`) failed 9 of
  30 -- hangs inside the first test, `CapabilityAndPublicRuntimeContract`, and `std::system_error`
  "Thread not joinable", a string of DXVK's own thread wrapper -- while 3.1.1 passed 30 of 30, and
  with it the whole `DIRECTX9` ctest suite passes 60/60 twice. On 2026-10-15 teardown markers had
  shown every renderer object released and the final `IDirect3DDevice9::Release` never returning.
  CNA was not changed for it. `scripts/run-wine-dxvk9.sh` still defaults to `~/.wine-cna-d3d11`,
  which links the system 2.6.0; `CNA_D3D9_WINEPREFIX=~/deps/wineprefix-cna-dxvk-3.1.1` selects a
  copy with 3.1.1 (machine-local, outside Git); the script's DXVK gate accepts the `v` official
  builds print before the version. `CnaGraphicsTests.exe` is run as eight
  `GTEST_TOTAL_SHARDS` shards because one 2,956-test process under 2.6.0 dies or hangs at a varying
  point.

## 6. Reserved for the human author

- **Licence-header convention.** The tree carries 30 MIT and ~3,150 MS-PL SPDX headers, two
  `storage` source/header pairs with mismatched headers, and ~170 header-less `.hpp`/`.cpp` files
  (mostly examples). Which header CNA-original files carry, and whether `CNA::Internal::Net` stays
  MIT on purpose, is the owner's policy call; a one-line rule next to `CHECKLIST.md`'s header
  section and a lint would follow from it.

## 7. Build, test and environment

```bash
# Configure once per variant (reuse the tree afterwards; see CLAUDE.md for the directory rules).
cmake -S . -B cmake-build-headless -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_PLATFORM=HEADLESS -DCNA_AUDIO_PLATFORM=NULL -DCNA_GRAPHICS_RENDERER=HEADLESS \
      -DCNA_BUILD_TESTS=ON \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
cmake --build cmake-build-headless -j8

# Run the suite on a private display and prove the run left the checkout clean.
scripts/check_clean_checkout.sh -- tools/platform/run_gpu_tests_private.sh cmake-build-headless -j8 \
      -E 'XnaPipelineGenuineRuntime|XnaDifferentialBuildTest'

# The six Wine-based XNA interop tests hang inside the private runtime; run them on their own
# (their harness starts its own Xvfb :99).

# Renderer and platform boundary gates.
python3 scripts/check_renderer_identities.py
python3 tools/platform/sdl_inventory.py --check && python3 tools/platform/sdl_classify.py --check
```

- GPU renderers are tested in their own trees (`cmake-build-sdlgpu`, `cmake-build-webgpu`,
  `cmake-build-multi`, …) through the same private runner. Never run several renderers' full suites at the same time: GPU and driver
  contention produces false failures.
- `CMakePresets.json` has the focused unit, sanitizer, web, Apple and multi-renderer presets.
- System packages for a fresh Debian machine: [`docs/development-environment.md`](docs/development-environment.md).
