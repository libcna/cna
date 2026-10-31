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

Campaign accounting from the repair matrix `CNA_DEMOS_REPAIR_MATRIX.json` (deleted on 2026-10-29
once every row had its final disposition; `git show 9c2b3e2d4:CNA_DEMOS_REPAIR_MATRIX.json`):
**156 historical IDs, 156 final dispositions**: 119 `FIXED_VALIDATED`, 23
`DOCUMENTATION_CORRECTED`, 8 `ALREADY_FIXED`, 3 `FIXED_PLATFORM_VALIDATION_PENDING` (003
SpriteBatch Textures[0], 101 video URI resolution, 152 StorageContainer FileShare: each needs a
native Windows/XNA run this machine cannot do; §8.2 says what to run), one `DEMO_EXPECTATION_BUG`,
one `DUPLICATE_ROOT_CAUSE` and one `UNSUPPORTED_CONTRACT_CORRECTED`. The three graphics findings
outside the historical count were resolved too:
`DC-R1` `FIXED_VALIDATED` and `DC-R2` `UNSUPPORTED_CONTRACT_CORRECTED` on the owner's decisions,
and `DC-R3` `FIXED_VALIDATED`. No demo carries a `knownIssue` marker any more.

### Latest accepted work

- Maintainer study workflow (2026-10-31): `docs/maintainer-study-guide.md` replaces two personal
  August study-plan drafts with a rolling thirteen-week method grounded in current sources of
  truth, `loc.sh`, generated-code boundaries, the current module/renderer set and the private GPU
  runner. It keeps a ten-hour core study track with an optional ten-hour production track, leaves
  personal notes outside the product repository, and deliberately introduces no new backlog,
  release calendar or AI line/file quota. `docs/README.md` now routes maintainers to it.
- Emscripten `Game` loop parity (2026-10-30): native `Tick()`, application-style Asyncify
  `Run()` and host-driven `RunFrameEXT()` now use one timing/update/draw state machine. Browser
  scheduling disables only native fixed-step waiting; it retains the high-resolution clock,
  `GameTime` ordering, the 500 ms cap, slow-frame history, reset, draw suppression, exit, disposal,
  renderer-context and exception behavior. The separate static `Game*`, copied `GameTime` and
  floating-point millisecond accumulator are gone. Eleven deterministic non-blocking tick
  regressions plus the existing lifecycle/clock/reset set pass 22/22; the clean WEBGL2 C ABI build
  and real Chrome probe pass 14/14, including a second sequential fixed-step game; and the WebGPU
  application demo completes 30 `Run()`/Asyncify frames with clean teardown. The full
  `CnaRuntimeTests` target passes 255/258 with the three environment-classified skips.
- Renderer identity normalization (2026-10-30): CNA's public renderer key is now consistently
  `SDLGPU`, including CMake selection, compile definitions, runtime names, presets, CI and the C
  ABI. The former `SDL_GPU` selector, feature options and C constant are deliberately refused
  rather than retained as aliases; the C ABI is therefore 0.51.0, while the renderer constant's
  numeric value remains 31. Actual SDL API identifiers such as `SDL_GPUDevice` and the
  `SDL_GPU_DRIVER` hint are unchanged. The migration guide and changelog record the source-level
  replacements. A follow-up preserves SDL's literal `SDL_GPU*` API prefix and Metal diagnostic in
  the two explanatory texts that the rename had overreached, and directly tests both refused old
  feature-option spellings. The full SDLGPU tree and the eight-renderer `CnaTests` target build;
  21 focused
  standalone identity/ABI/selector tests, 20 multi-renderer identity/registry tests and four
  SDLGPU smoke/parity tests on the private GPU display pass.
- `BENCH-001` (2026-10-27; CNA commit `421a38e4f`, consumer commit `ee012a8`): the external
  `cna-benchmark` now
  builds against the current CNA tree and runs all seven scenes on SDLGPU and OPENGLES3. Its
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
  709/0/7/3. The only remaining campaign actions are the three native-Windows checks (§8.2) and
  the consumer-owned museum reruns described under "Exact next task".
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
- handoff §5.1's one defect, `SpriteSortMode::Immediate` batched on `SDLGPU` and `WEBGPU`: an
  upload to a texture a queued draw samples is now ordered after that draw (SDLGPU submits the
  queued work first, WEBGPU writes Texture2D into a fresh texture), and
  `EasyGL_SpriteBatch_SortModeSemantics` leg B asserts XNA's red/blue on SDLGPU, WEBGPU,
  OPENGLES3, OPENGL33, SOFTWARE and FNA3D. WEBGPU's TextureCube and Texture3D, found by reading,
  rename the same way since the commit after it, and `*_Conformance_texture_setdata_after_draw`
  checks a Texture2D and a TextureCube changed between two draws on every pixel renderer.
- DC-R1 (`51fb39d5a`, owner: implement): SDLGPU binds a RenderTargetCube face in any slot of a
  target set. DC-R2 (`c7f6c68e5`, owner: all-or-none exact, refuse others): SDLGPU applies a
  MultiSampleMask keeping every sample or none and refuses any other, and
  `RendererFeature::MultiSampleMask` (35, C ABI 0.50.0) reports each renderer's mask support,
  checked against a 4x probe. `cube_mrt_binding` passes M1-M4 on SDLGPU. Serial cna-demos CTest
  after both (demos tree rebuilt from CNA `c7f6c68e5`): 732 enabled, 687 PASS/45 SKIP/0 FAIL/3
  disabled (870.97 s), the batch 9 outcome; reports
  `../cna-demos/build/linux-full/reports/repair-dcr-closure-ctest.*`.
- DC-R3 (`9ef292993`): `cna_shader_effect_create` copies a whole SPIR-V module byte for byte
  instead of refusing it as non-UTF-8 text, so a C caller can give SDLGPU (dialect SPIRV) a custom
  effect it runs. Found when `c_api.graphics.shader_effect_state`, which had used a GLSL
  placeholder there, was given a SPIR-V pair (demos `77444d7`); it passes 17/17 on SDLGPU.
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
fully rebuilt, full CTest 12,553 of 12,569 passed in 927 s; the 16 others are the 15 SDLGPU/FNA3D
compiled-effect entries and `CApi_AudioUnavailableSmoke`, and pass when rerun with
`CNA_GRAPHICS_RENDERER=SDLGPU` (62/62), `=FNA3D` (37/37) and without `SDL_AUDIO_DRIVER=dummy` (1/1).
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
the SDLGPU/FNA3D compiled-effect tests need `CNA_GRAPHICS_RENDERER=SDLGPU`/`FNA3D`. A change to
what a content importer, processor or writer produces must bump its identity version, or
incremental builds keep stale outputs.

### Exact next task

The museum Pipeline Workshop items are all answered: 1 (build-wide nested names, `98aa655f2` and
`419723275`), 9 (`bbed8c61e` on Sharp Runtime `f990d074`, measured against
genuine XNA, demos `9598b43`) and 16 (owner: the documentation half is enough; the task sources
stay in `modules/content-pipeline`). What remains needs a host this machine does not have: the
three Windows/XNA platform validations (003, 101, 152; §8.2). The museum's reruns of
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

Consumer-only work still matters. cna-engine's `upstream-issues.md` reports every finding CNA
answered resolved at its pin `cb377fa04`: UP-001, UP-002, UP-006, UP-018, UP-019 and UP-020 (the
CNA-side intake log, `CNA_ENGINE_REPAIR_HANDOFF.md`, was deleted on 2026-10-29 and is in Git
history). The museum has rerun its findings (above); the
consumers also need to know that `new PresentationParameters()` is now XNA's 0x0 full-screen value,
so a device built from it directly must state a size. Do not claim UBSan/TSan/hardware/service
validation when its required tree or hardware is absent.

Build discipline remains: existing build directories only, shared
`CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv`, at most 8 jobs and preferably one compiler under
the established 4 GiB high / 6 GiB max / no-swap scope. All GPU/UI tests go through
`tools/platform/run_gpu_tests_private.sh`. Never weaken a demo/test oracle, and update the
permanent issue history and handoff after every accepted batch.

**§5 is the authoritative current defect inventory; the three fixes still awaiting a native
Windows run are §8.2.** The repair matrix that recorded every historical cna-demos finding is in
Git history (above). Capability documents describe supported behavior, tests are executable
evidence, and Git history preserves measurement history. Every implementation plan has been retired
and `plans/` removed (2026-10-29); `docs/task-id-index.md` says where each one's content went. Do
not let an issue disappear from §5 or §8.

Ready to pick up next, when the owner asks: the near-future tasks of §8, starting with `XNB-61b`
(§8.1).

Repository state last reconciled: **2026-10-30** (`next`, VSIDE-001..006), when the native Windows
IDE, preset, runtime-deployment and automatic-CI baseline was added and §5 brought up to date. The
last plans were retired on 2026-10-29 (`225e88b09`). The campaign accounting above dates from
2026-10-24 (`96577db26`), and the test runs in §0 and §2 carry their own dates. The last full test run is the
checkpoint identified above; do not upgrade later focused evidence into a full claim.

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
`WEBGL2`, `WEBGPU`, `HEADLESS`, `SOFTWARE`, `STUB`, `DIRECTX9`, `SDLGPU`, `FNA3D`; the three GL
identities share the internal EasyGL implementation. `SDLGPU` is CNA's modern native path: SDL
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
| Windows | MinGW-w64 cross builds run under Wine: `DIRECTX9` through DXVK, `SDLGPU`'s Direct3D 12 backend through vkd3d-proton, `SOFTWARE`. `windows-msvc-ci.yml` now automatically builds and tests an SDL-free native-MSVC contract and an SDLGPU Direct3D 12 smoke set; it was added on 2026-10-30 and no successful run is recorded yet. The deeper `DIRECTX9` workflow remains manual. No real-Windows hardware validation is current (§5.2). |
| Web (Emscripten) | `WEBGL2` and `WEBGPU` builds run in Chrome (`docs/web-emscripten-graphics-limitations.md`). |
| Android | `OPENGLES3`, the default there, on an x86_64 emulator; `SDLGPU` on Android is not qualified (`docs/android-graphics-limitations.md`). |
| macOS / iOS | macOS CI builds CNA with `SOFTWARE` and runs the portable platform suites and a bundle smoke app, and (gating, since RRC-051, not run yet) builds `SDLGPU` and runs its smoke set on SDL's Metal backend; iOS is a `SOFTWARE` simulator smoke app, with non-gating `SDLGPU` legs. `SDLGPU` is the default on both but is not re-qualified there (§5.2, `docs/apple-platforms.md`). |

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
`OPENGL33`, `WEBGPU`, `SOFTWARE`, `SDLGPU` (Vulkan) and `FNA3D` with its OpenGL and its SDLGPU
driver (the audit had measured 6 failures on `SDLGPU` and 15 to 25 on each of the others, all
fixed or test defects); its network
filter 574/574 and the platform suites 1,553 on X11 and 1,553 on Wayland, none failed. The family
example suites of the renderers B2-04 changed, each in its own default tree (Release; `FNA3D` at
`10f650b9d0f6`, the others at `aa3139087e69` with MojoShader `bbee0a5fab1f`): `FNA3D`
63/63; `SDLGPU` 65 selected (MSAA, render-target, cube, readback, effect and
`SdlGpu_TextureDefragment`), every pixel check passes, two debug-layer assertions need a Debug tree;
`WEBGPU` 41/41, `SOFTWARE` 34/34; `render_target_own_msaa` and `cube_mrt_binding` pass on all five
families. Fork
suites: MojoShader `tests/cna` 16/16 normal and under clang ASan+UBSan+LSan with no check excluded,
with its 14 SDL_GPU device cases on RADV; FNA3D `tests/cna` 21 (27 with `TRACING_SUPPORT`) under
ASan+UBSan on OpenGL, OpenGL ES 3 and SDLGPU, and its eight executables on D3D11 under Wine with
DXVK 3.1.1. Cross builds at `10f650b9d0f6`: `cmake-build-mingw` under Wine with DXVK 3.1.1
ran `DirectX9CompiledEffect*` 31/32 (one declared skip) and `XnaMultisamplePixelCenter*` /
`RenderTargetReadbackLayout*` 5/6 (one declared skip) on `DIRECTX9`; the Emscripten
`cna_webgpu_compiled_effect_page` passed 21/21 in headless Chrome; Android was not rebuilt at this
pin (at `aa3139087e69` Android x86_64, NDK 29, built SDL with its patches and `cna_platform`, not
run). Latest full Debug runs, 2026-10-17 at the end of the handoff 5.1 stabilization pass (branch `stabilization`, worktree
`cna-stab`), through the private runner on RADV: `cmake-build-multi` (`OPENGLES3` default with
`OPENGL33`, `WEBGPU`, `SDLGPU`, `FNA3D`, `SOFTWARE`, `HEADLESS`, `STUB`; C API) 11,433 tests, 1
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
| Index of the other documents | `docs/README.md`; what a task ID belonged to: `docs/task-id-index.md` |

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
  `SDLGPU` pass (`cmake-build-multi` run with `CNA_GRAPHICS_RENDERER=WEBGPU`, 2026-10-28):
  `presentation_mode_contract_test.cpp` under Letterbox and Overscan, where a sprite covering the
  40x40 logical canvas after a mid-frame `SetPresentationMode` lands unscaled (40x40 at the
  letterbox origin instead of filling 96x96), and `spritebatch_presentation_test.cpp`'s two
  Deferred batches under different custom viewports, which both read black. Not investigated.
- `WebGPU_BindingCost` aborts with "The texture resource is in use." (`cmake-build-webgpu`,
  2026-10-28): `webgpu_binding_cost_test.cpp`'s `DrawRenderTargets` calls `SetData` on a texture
  the previous SpriteBatch left in `Textures[0]`, which `Texture.cpp` refuses as XNA does
  (SOFTWARE-246). The test needs to unbind the slot first; the other 187 `WebGPU` tests pass. When
  it began to fail is not bisected.
- On Android, the landscape remap `Accelerometer`, `Gyroscope` and `Motion` apply by default
  (`Detail::ConvertAndroidPortraitToXnaLandscape`, a CNAEXT convenience that
  `SetAndroidLandscapeRemapEnabled(false)` turns off) is, for both landscape rotations, a
  reflection (`diag(1,-1,1)` / `diag(-1,1,1)`, determinant -1) rather than a rotation: a real 90°
  turn of the device exchanges X and Y. Windows Phone itself never remaps; it reports the
  device-fixed frame. Found by derivation on 2026-07-17 (devices plan `MOT2-001`, recorded in
  `AndroidMotionMath.hpp`) after `ACCEL-008` had kept the remap on 2026-07-07; never checked on a
  phone.
- Three `SpriteBatch::Begin` overloads take `BlendState` by value (`SpriteBatch.hpp`: the
  two-argument one, one of the two five-argument ones and the six-argument one; the other
  five-argument overload takes `const BlendState*`), where XNA's `BlendState` is a class passed
  by reference.
  Every such call copies the state twice, each copy allocating its own state on the heap. Found
  by the Inspector's first end-to-end run (`AUD-DIAG-INSP-0004`, 2026-09-18) and left open
  because the fix changes a public signature.
- EasyGL (`OPENGLES3`, `OPENGL33`, `WEBGL2`) disables blending whenever colour and alpha both use
  `Blend::One` / `Blend::Zero`, without looking at the blend functions
  (`EasyGLRenderer::ApplyBlendState`). With `BlendFunction::ReverseSubtract` XNA evaluates
  `0 - source`, but EasyGL copies the source; the same shortcut also skips `Max` and `Min`, which
  ignore the factors (not measured). Found by SDLGPU's blend-equation oracle (`SDLGPU-58`,
  formerly `EASYGL-PARITY-1`, 2026-09-10); SDLGPU evaluates the equation correctly, and EasyGL
  has no regression leg for it yet.
- An LZ4-compressed `.xnb` reached through a type-erased external reference fails to load:
  `ContentManager::LoadXnbAssetUntyped` (`ContentManagerUntyped.cpp`) still throws "uses MonoGame's
  Lz4 compression, which CNA does not yet support", while the typed path decodes LZ4 since
  `CP-047` (`XnbCanonicalAsset.cpp`). Its LZX branch also skips the typed path's truncation check
  and decompression limits. Found by reading on 2026-10-29; not yet pinned by a test.
- In browser fullscreen the drawable a GL renderer letterboxes into is not the rectangle the browser
  presents. Measured 2026-09-08 with SAMPLE-071 (Yacht, 480x800 back buffer) on a headless Chrome
  with an 800x600 screen: the canvas reported 800x600 while its CSS box was 800x544, and the game
  covered about 71 % of its logical area in both axes, uniformly scaled, its bottom and right
  outside the visible frame. `GlPresentationSurfaceState` computes the letterbox correctly from what
  it is given (`EasyGLSurfaceState.LetterboxKeepsTheLogicalSizeAtTheBackbufferAndCentresTheRect`); the
  wrong input is the drawable from `Sdl3Window::GetPixelSize()`'s web path
  (`webDrawableOverride_`, which already special-cases leaving fullscreen). Not re-measured since;
  not reproducible natively here, where X11 fullscreen has no window manager to resize the window.
- An EasyGL `Texture3D` does not survive a GL context loss (WebGL 2, Android): unlike every other
  EasyGL texture, buffer, render target and query, `EasyGLTexture3DRenderer` is not an
  `easygl::RecoverableResource`, so it is never registered for restoration and keeps a dead handle.
  `TextureCube` gained recovery on 2026-09-04. Found by reading; no context-loss test covers a
  volume texture.

### 5.2 Limitations

- `SDLGPU` applies `BlendState.MultiSampleMask` on a multisampled target only when it keeps every
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
  `modules/renderers/fna3d/src/Fna3dDraw.cpp`) wherever FNA3D picks its SDLGPU or Direct3D 11
  driver, which cannot build a pipeline with a missing input; its OpenGL driver and every other
  renderer supply the GL neutral tangent (`91612a0a3`). Measured by the museum's `rv-shading` on
  RADV, 2026-10-25.
- Online sessions and invitations are native-only; a browser build refuses them explicitly
  (`docs/browser-network-readiness.md`, `docs/gamer-services-known-limitations.md` §26).
- `DIRECTX9` is validated under Wine (+DXVK) only. `SDLGPU`'s Direct3D 12 backend is validated
  under Wine (+vkd3d-proton); an automatic hosted native-MSVC smoke workflow was added on
  2026-10-30, but no successful run is recorded yet and hosted smoke is not real-hardware
  qualification. The C API's pure-C suites
  run under Wine from the MinGW-w64 build; `MediaLibrarySmoke` reports a skip on Windows, whose
  known folders cannot be pointed at its fixture. The MinGW-w64 test binaries also skip on
  Windows what it cannot provide: a Python interpreter where none can be started, `/dev/full`,
  aging a directory through `utime`, symbolic links, a renderer without 3D, and the ordering guard
  for tests that need an uninitialized `GamerServicesDispatcher`.
- The installed C++ SDK (`CNA_INSTALL_CXX`, `docs/installed-cpp-sdk.md`) is validated on native
  Linux only, shared and static; the static SDK was validated with the C API off, so a mixed static
  C/C++ adapter is unproven. The MinGW-w64 and WebAssembly/WebGPU trees only configure and export
  its package: no relocated Windows SDK or installed WebAssembly SDK has been run.
- The content and content-pipeline *tests* are not Unicode-clean: about 1,150 `.string()` calls,
  most narrowing a fixture path before handing it to CNA, so under a non-ASCII `TEMP` on Windows
  they throw in the test body (Windows portability WINPORT-F5, measured on native Windows
  2026-09-16). The library is not implicated; its path contract is
  `docs/filesystem-path-model.md`.
- **`SDLGPU` is the default renderer on Windows, macOS and iOS but is not qualified there** since
  the 2026-10-13 retirement of `DIRECTX11` and `METAL` (`docs/removed-renderers.md`). Evidence that
  exists: the full tree passed with `SDLGPU` (Metal backend) on a Mac mini M4 in the AM4 campaign,
  before the retirement; Direct3D 12 runs under Wine. The automatic hosted native-MSVC Direct3D 12
  smoke gate was added on 2026-10-30, but its first successful run is still pending. Not done:
  native Windows hardware qualification, a re-run on Apple hardware, and any iOS build (`SDLGPU`
  has never been built for iOS; CI tries it
  as a non-gating leg). Known gaps against the retired native backends, each to be implemented or
  capability-gated in that campaign: `ShaderEffect` (`CustomEffects`) is compiled out on Windows,
  Apple and the browser because no runtime GLSL-to-SPIR-V-to-DXIL/MSL path exists; occlusion queries
  are unavailable (SDL_gpu 3.5 has none). `RendererFeature::BaseInstanceDrawing` is implemented by
  `SDLGPU` since RRC-043 (its stock, instanced-matrix and compiled-effect routes expand per-instance
  streams on the host from the first instance); no other survivor claims it.
- iOS has simulator smoke evidence only, with `SOFTWARE`.
- `Microsoft::Devices` sensors and vibration have never run on a phone: the Android axis, compass
  and attitude mappings are derived and host-tested only. The device checks still open are listed
  at the top of `docs/devices-hardware-checklist.md`.
- `WEBGPU` is supported (promoted from experimental on 2026-10-17) and verified on Linux x86_64
  and in headless Chrome only; its Windows, macOS and linux-aarch64 wgpu-native packages are
  unverified (`WEBGPU-1`, `docs/webgpu-renderer.md`).
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
- **MojoShader SPIR-V gaps** (FNA3D's SDLGPU driver, CNA's SDLGPU and WEBGPU compiled effects):
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
  (`mojoshader.h`); FNA3D's SDLGPU driver forwards DXBC to SDL when it is offered. CNA's SDLGPU
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
- **EasyGL re-sends per-draw state the previous draw already set** (performance only; no picture
  is wrong). GLB-41 removed the sampler, viewport and polygon-mode traffic; two costs remain.
  GLB-42: the stock-program semantic remap re-specifies a vertex buffer's VAO before each draw and
  restores the buffer's own layout after it (`ConfigureDeclarationForStockProgramEXT` /
  `RestoreDeclarationLayoutEXT`) -- in cna-car-simulator's `WEBGL2` scene (1,156 draws, 2026-09-19)
  about 23,000 `disableVertexAttribArray` and 23,000 `vertexAttribDivisor` calls a frame plus the
  matching enable/pointer/VAO binds; caching the remapped layout needs every path that relies on the
  restored one (custom and compiled effects) audited first. GLB-43 (cna-street CNA-F19,
  2026-09-30): `BindDrawParams` uploads every uniform the stock program declares and rebinds every
  texture unit on every draw, about 9 µs a draw on either GL renderer, because easy-gl's
  `Program::set_uniform` and `Texture::active_bind` keep no record. Proposed: a per-program record
  of the last value at each uniform location (sampler-unit uniforms set once at link) and a per-unit
  bound-texture record, dropped on context loss, texture deletion and relink as GLB-41's records
  are, with real-context tests like `EasyGLRedundantStateTests.cpp`.
- **Three audio reports from the 2026-07-17 audit stay unconfirmed, not open:** high-pitched sound
  effects, missing audio files, and distorted or sped-up gameplay audio. They were never
  reproduced against the game they came from, which is not available. The audit narrowed the
  pitch report to a wrong sample rate reaching CNA before the mixer -- the class CBIND-146 fixed
  (`51d9c84cc`, 2026-10-02, an XNA-built sound's content sample rate).
- Three content-pipeline residues against genuine XNA output, none visible in the 7,726-reference
  sample sweep (`tools/xna-sample-sweep/README.md`, *What is left*): a generated `.x` normal can
  carry the opposite sign of a zero (13 of 1,291 probe normals, `XNASWEEP-231`); FBX bone
  transforms keep an ulp-level residue that grows with depth, inside the float tolerance
  (`XNASWEEP-228`..`232`); and `MeshHelper.OptimizeForCache`'s strip termination is empirical
  (9 of 20 probes exact, `XNASWEEP-149`).
- The CNAEXT content manifest lists no reader names for a compressed `.xnb`:
  `ContentManager::ScanXnbReaderNames` reads only an uncompressed type-reader table, so an LZX or
  LZ4 file's `ContentManifestEntry::xnbReaderNames` is empty and `GetXnbReaderUsageSummary()` skips
  it, although `Load` decompresses both (`XNB-61b`; the fix is planned in §8.1).

### 5.3 External and driver limitations

Behaviour CNA does not own, measured and reported rather than worked around.

- **Conformance programs on drivers and capability boundaries** (`*_Conformance_*`,
  `modules/graphics/examples/conformance/`, measured 2026-10-15/17):
  - `render_target_own_msaa` D (2x) failed on EasyGL in 2026-10-17's run and on SDLGPU and FNA3D
    once B2-04 gave multisampled targets XNA's integer pixel centres: its edge sat at the
    half-integer centre, beyond both 2x samples. The edge now runs through the integer centre and D
    passes on EasyGL, SDLGPU, WEBGPU, SOFTWARE and FNA3D (2026-10-26).
  - FNA3D: `SkinnedEffect.VertexColorEnabled` is ignored outright (B equals A) and saturates before
    the fold; its back buffer keeps a 3, 5 or 7 sample request unrounded while its render targets
    round it down; `GraphicsDeviceManager`'s default `Depth24` request is not applied in
    `graphics_adapter_query`. A `TextureCube` changed between two draws of one frame shows its new
    contents in the earlier draw too (`texture_setdata_after_draw` B, 2026-10-24): FNA3D's SDLGPU
    driver uploads on a command buffer it submits ahead of the frame's draws and cycles only a whole
    single-level 2D texture, so by its source a `Texture3D` or a mipmapped `Texture2D` does the
    same. FNA3D's driver owns those answers.
  - WEBGPU and DIRECTX9: cube faces in a multi-render-target set are refused with
    `System::NotSupportedException` while `MultipleRenderTargets` is true (`cube_mrt_binding` M1-M4
    and the plural cube-face `RenderTargetSemantics` cases skip on that refusal); HEADLESS accepts
    them. A capability boundary. SDLGPU binds them since `51fb39d5a` (DC-R1).
- **LeakSanitizer reports leaks in the pinned SDL** (measured 2026-10-22/23 during the museum
  repairs): about 50 KB in about 910 allocations from initialization to destruction with no CNA code
  linked (lavapipe and RADV), so an SDLGPU sanitizer run does not end at exit 0 under
  LeakSanitizer; CNA suppresses none of it. The MojoShader SPIR-V attribute-fixup leak measured then
  (8,792 bytes in 513 allocations per device), FNA3D's SDLGPU device-array and vertex-binding
  cache leaks, and the dumpspirv and trace-lock leaks are fixed on the forks (2026-10-26; the owner
  authorized fork changes that day). MojoShader's `tests/cna` runs leak-free under
  AddressSanitizer; FNA3D's does on OpenGL and OpenGL ES 3, and on SDLGPU every remaining report
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

## 8. Near-future tasks

Small, fully specified pieces of work that are ready to pick up. Each one names its evidence, its
plan and the point at which it is done; take one only when the owner asks for it or the current
campaign leaves room.

### 8.1 `XNB-61b` — reader names of a compressed `.xnb` in the content manifest

**The gap.** `ContentManager`'s CNAEXT content manifest (`RefreshContentManifest()`,
`GetContentManifest()`, `GetXnbReaderUsageSummary()`; described in
[`docs/xnb-content-pipeline-support.md`](docs/xnb-content-pipeline-support.md), *Content manifest*)
lists the type-reader names each `.xnb` needs. For a **compressed** file it lists none:
`ContentManager::ScanXnbReaderNames` (`modules/content/src/Xna/ContentManager.cpp`) parses the
header and returns an empty list whenever `header.compression != XnbCompression::None`, with a
comment saying the work was "not yet picked up". So `ContentManifestEntry::xnbReaderNames` is
empty for every LZX or LZ4 file and `GetXnbReaderUsageSummary()` counts none of them, although
`Load` decompresses both. It matters because real content is largely compressed: Microsoft's
pipeline writes LZX, and 729 of the sample sweep's references differ from CNA's only inside the
LZX stream (`tools/xna-sample-sweep/README.md`). A tool asking "which readers does this content
need that the game has not registered" therefore misses most of a real XNA game.

**The fix.**

1. Pull the decompression out of `DecodeXnbCanonicalAsset` (`modules/content/src/Xnb/XnbCanonicalAsset.cpp`)
   into a helper that takes the file bytes, its origin and `XnbReadLimits` and returns the parsed
   header and the (possibly decompressed) body. Today that switch -- `None`, `Lzx` through
   `DecompressXnbPayload`, `Lz4` through `DecompressXnbLz4Payload`, each with its truncation and
   size checks -- exists only inline there. `DecodeXnbCanonicalAsset` then calls the helper.
2. In `ScanXnbReaderNames`, replace the early `return names;` with the helper and read the
   type-reader table from the body it returns. Keep the existing rule that a malformed file is not
   a scan failure: a `ContentLoadException` from the helper still means an empty list for that one
   entry.
3. Tests in `modules/content/tests/Microsoft/Xna/Framework/Content/ContentManagerManifestTests.cpp`,
   over fixtures already committed: the MonoGame LZX files in `tests/assets/xnb/monogame/windows/lzx/`
   (`Explosion.xnb`, `FontCalibri14.xnb`), the LZ4 oracle `tests/assets/xnb/monogame/windows/lz4/white-1.xnb`
   and CNA's own LZX corpus in `tests/assets/xnb/cna/windows/lzx/`. Assert the reader names of a
   compressed entry equal those of its uncompressed twin where one exists, that
   `GetXnbReaderUsageSummary()` counts compressed files, and that a truncated compressed file
   yields an empty list without throwing.
4. Remove the *Known gaps* row in `docs/xnb-content-pipeline-support.md` and the §5.2 entry here,
   and correct the `xnbReaderNames` doxygen in `ContentManifestEntry.hpp`.
5. Build and test in `cmake-build-multi`: `cmake --build cmake-build-multi --parallel 8 --target
   CnaTests CnaContentTests`, then `CnaContentTests` and the XNB filter of `CnaTests` through
   `tools/platform/run_gpu_tests_private.sh --exec`. Where ALSA cannot open a device (it could
   not on 2026-10-29: "Host is down"), the `SoundEffect` tests need `SDL_AUDIO_DRIVER=dummy`.

**Estimate.** About 1.5-2.5 hours, roughly one of them active work and the rest build and test
time (helper 30 min, scan 15 min, tests 30-45 min, documents 10 min, build and runs 30-60 min).

**Worth doing in the same change.** The helper also fixes the §5.1 defect in
`ContentManager::LoadXnbAssetUntyped` (`ContentManagerUntyped.cpp`), the type-erased
external-reference path, which still refuses LZ4 and skips the typed path's truncation check and
limits on LZX. That adds about 1-1.5 hours, most of it a test fixture: an LZ4-compressed asset
reached through a type-erased external reference, which CNA's own writer can produce with
`--xnb-compress lz4`.

**Deliberately not part of it.** A full decompression per compressed file makes the first
`GetContentManifest()` over a large content root slower. The type-reader table sits at the start
of the body, so decoding only the first LZX frame would be enough, but that is 1-2 more hours and
should wait for a measurement that shows the scan is too slow.

### 8.2 Three cna-demos fixes awaiting a native Windows run

The CNA/cna-demos repair campaign closed every finding except these three, which are fixed and
validated on Linux but still owe a run on native Windows -- a host this machine does not have.
None of them is a known defect: each is a fix whose Windows execution is unproven. Wine does not
count for any of them. Each one is done when its run passes on native Windows; then delete its
paragraph, and this section with the last one. The evidence files are in `../cna-demos`.

**`CNA-DEMOS-ISSUE-003` -- `SpriteBatch` leaves its texture in `GraphicsDevice::Textures[0]`.**
SpriteBatch kept renderer handles but never published the public texture wrapper. `3b6a2eaf3`
(2026-10-22) publishes `Textures[0]` after each custom pass through lifetime-safe movable
identities, tells a live disposed wrapper from a destroyed C++ one, discards renderer staging of a
failed submission without replaying it, and makes WEBGPU and DIRECTX9 honour `Immediate`. Its 22
regressions pass on SOFTWARE, OPENGLES3 and OPENGL33, with compiled-effect coverage on SDLGPU,
FNA3D and WEBGPU; for DIRECTX9 the five affected translation units only compile in the MinGW
configuration. *To run on native Windows with `DIRECTX9` and
`-DCNA_DIRECTX9_COMPILED_EFFECTS=ON`:* the 22 tests in
`modules/graphics/tests/Microsoft/Xna/Framework/Graphics/` -- `SpriteTextureBindingTests.cpp`
(6), `SpriteTextureBindingLifetimeTests.cpp` (7), `SpriteCompiledTextureBindingTests.cpp` (3),
`SpriteCompiledBindingLifetimeTests.cpp` (5) and `SpriteCompiledRetryPixelTests.cpp` (1) -- and
the cna-demos demos `graphics.texture2d.set_data` and `graphics.texture2d.set_data_rectangle`.
Evidence: `docs/repair-evidence/graphics/SPRITE_BINDING_FINAL_FOCUS.md`.

**`CNA-DEMOS-ISSUE-101` -- video URI resolution and deferred file failures.** URI spellings
bypassed Sharp Runtime's decoding and `TitleLocation` resolution, and a deferred `Play` of a
missing file stopped silently where XNA throws `ArgumentException`. `abbd8a4ea` (2026-10-21, on
Sharp Runtime `c26614e85`) fixed both; the tests are `VideoFilePathBoundaryTests.cpp` and
`VideoMissingFileXnaTests.cpp` in `modules/media/tests/Microsoft/Xna/Framework/Media/Video/`.
*Still unmeasured:* when and how genuine XNA reports an existing file it cannot decode. That needs
a working XNA media reference on native Windows: first a valid WMV as the positive control, then
the undecodable files and the error timing of the public `Play`. Under Wine the valid control
aborted in the unimplemented `mfplat.dll` `MFCreateVideoMediaType`, so no Wine result settles it.
Match CNA to the measurement; nothing was changed on FNA's word. Evidence:
`docs/repair-evidence/media/VIDEO_CODEC_REFERENCE_LIMITATION.md`.

**`CNA-DEMOS-ISSUE-152` -- `StorageContainer` honours the requested `FileShare`.**
`StorageContainer` discarded an explicit `FileShare`, Sharp Runtime had no constructor taking
one, and the shorter overloads used the wrong defaults (XNA opens them exclusively, and `Append`
write-only). `21b4cd64c` (2026-10-23; C contract `169470ee7`; Sharp Runtime `c6984ecef` and
`f51bf9acf`) fixed it with platform-native sharing. The genuine-XNA reference oracle and the Linux
kernel coverage are complete, and Sharp's sharing tests pass 16/16 under Win64 Wine.
*To run on native Windows against these CNA and Sharp Runtime commits or later:* the four sharing
tests of `modules/storage/tests/Microsoft/Xna/Framework/Storage/StorageDeviceTests.cpp`
(`FileSharingIsForwardedToTheNativeStream`, `ShortFileOverloadsAndCreateFileAreExclusive`,
`DeniedSharingDoesNotTruncateStoredData`, `TwoArgumentAppendUsesWriteOnlyAndPreservesThePrefix`)
and the cna-demos demo `storage.storage_container.file_modes`. Evidence:
`docs/repair-evidence/storage/FILE_SHARE.md`.
