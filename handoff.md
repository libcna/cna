# handoff.md — current state of CNA

This file describes the repository as it is now and is kept true. Until 2026-10-11 it was `NEXT.md`
and also carried the dated session log of 2026-07 to 2026-10; that log is in Git history
(`git show 8b5564f1e:NEXT.md`).

## 0. CNA/cna-demos repair campaign handoff — 2026-10-23

This is the current continuation point for a fresh-context agent. It is not a claim that the
overall campaign is complete. Work since the last push (2026-10-23 evening) is committed locally
and **not pushed**; push only on the owner's word, Sharp Runtime before CNA.

Repositories and branches:

- CNA: this repository, branch `next`. Published through `15d5d6f3d`; local since then:
  `c30925496`/`162e07b8a` (ISSUE-010), `a24c75b2e` (storage dangling-symlink escape),
  `6068c1ec4` (avatar converter, galaxy-eggbert UB-001), `fe2eb8f33` (ISSUE-010 closure),
  `3da9ea645` (ISSUE-020), `5d66bb795` (ISSUE-029), `8842b9a50` (ISSUE-062), `1fb593d9e` (buffer
  C smokes expect XNA's Disposing order, missed by `39ef8ea2f`), `41944dd8d` (galaxy-eggbert
  UB-002, C ABI 0.50.0) and the 020/029/062 closure commit.
- cna-demos: `../cna-demos`, branch `next`. Published through `92ee315`; local since then: the
  ISSUE-010 evidence and closure commits, the 020/029/062 demo and evidence commits and their
  closure commit. Its `docs/CNA_ISSUES.md` is the permanent historical
  inventory and its assertions remain the oracle.
- Sharp Runtime: `../sharp-runtime`, branch `next`. Published `2f644cf4`; local commit
  `530e217f` (Directory creation error mapping, ISSUE-010) is required by CNA's container
  construction repair and is not pushed yet -- publish it before the CNA commit that needs it.
  Do not make an incidental dependency edit.
- Museum and cna-engine inventories are consumer-owned. Do not edit them from CNA; their agents
  move pins/findings after published CNA commits are available.

Campaign accounting from `CNA_DEMOS_REPAIR_MATRIX.json`: **156 historical IDs, 137 final
dispositions, 19 pending**. Final dispositions are 100 `FIXED_VALIDATED`, 23
`DOCUMENTATION_CORRECTED`, 8 `ALREADY_FIXED`, 3 `FIXED_PLATFORM_VALIDATION_PENDING`, one
`DEMO_EXPECTATION_BUG`, one `DUPLICATE_ROOT_CAUSE`, and one
`UNSUPPORTED_CONTRACT_CORRECTED`. The 19 remaining historical IDs, each of which must remain
visible until resolved, are:

`005`, `006`, `007`, `008`, `022`, `023`, `031`, `066`, `067`, `081`, `091`, `092`, `093`,
`094`, `095`, `096`, `098`, `104`, `114`.

Their full titles, classification, reproduction and accumulated evidence are in the matrix and
`../cna-demos/docs/CNA_ISSUES.md`. `005/006/007/008/022/023/031/067/081/091/092/093/094/095/096/
098/104/114` are `PENDING_VERIFICATION`; `066` is `CONFIRMED_OPEN`. Two
additional graphics findings outside the historical count remain open: `DC-R1` (SDL_GPU mixed
cube/2D MRT) and `DC-R2` (SDL_GPU multisample mask), both recorded in the matrix and
`../cna-demos/docs/repair-evidence/graphics/DEVICE_CONTEXT_EXTERNAL_RESIDUALS.md`.

### Latest accepted work

- ISSUE-020, ISSUE-029 and ISSUE-062 closed (`FIXED_VALIDATED`, 2026-10-23, not pushed).
  `3da9ea645` resolves a compiled `.cnb` reference beside a relocated build the way XNA's
  `ContentReader.GetPathToReference` does (CMET `contentName` gives the prefix; loads that worked
  are unchanged). `5d66bb795` adds `SoundEffect::FromStream(System::IO::Stream&)`, a port of
  XNA's `WavFile` (the `std::istream` overload stays as a CNAEXT). `8842b9a50` makes the C
  accelerometer/gyroscope routes refuse a disposed sensor. Accepted on a complete checkpoint
  (below). Evidence: `../cna-demos/docs/repair-evidence/content/COMPILED_REFERENCE_RELOCATION.md`
  and `.../api-shape/SOUND_EFFECT_STREAM_REPAIR.md`.
- galaxy-eggbert UB-002 (`41944dd8d`, the owner chose full support): the avatar converter keeps a
  part's glTF `baseColorFactor` (`diffuseColor`) and `baseColorTexture`;
  `SkinnedModelEXT::PartEXT::DiffuseColor` carries it for the caller's `SkinnedEffect`; C ABI
  0.50.0 adds its two routes. All 132 C API tests pass in `cmake-build-multi`. Building them also
  exposed that `39ef8ea2f`'s XNA Disposing order (IsDisposed first) had not been applied to the
  vertex/index buffer smokes; `1fb593d9e` corrects them.
- ISSUE-010 closed (`FIXED_VALIDATED`, 2026-10-23, not pushed). `162e07b8a` completes the
  residual paths against genuine XNA (`StorageResidualPathsOracle`): a device's `DeleteContainer`
  removes only its own player folder and creates nothing; Exists queries answer false on any
  failure; operations beneath an unsearchable directory report `UnauthorizedAccessException`; an
  unresolvable root is a disconnected device. The C ABI keeps refusing an empty title name. Three
  older CNA tests and the `storage_root` demo encoded whole-folder deletion and were corrected to
  XNA's scope; `path_rules`/`directories` now require XNA exception types. Accepted on a complete
  checkpoint (below). `a24c75b2e` separately refuses a dangling symlink in a container, through
  which `CreateFile` used to create a file outside it. Evidence:
  `../cna-demos/docs/repair-evidence/storage/STORAGE_RESIDUAL_PATHS.md`.
- ISSUE-010 StorageContainer construction (2026-10-23, not pushed): genuine XNA
  `StorageContainerCreationOracle` (real `EndOpenContainer` in an isolated Wine overlay profile)
  shows End creating the whole chain and a file at any level escaping as an exact `IOException`,
  an unwritable parent as `UnauthorizedAccessException`. CNA's `EndOpenContainer` now only
  resolves the root and the constructor creates the chain through Sharp `Directory` (Sharp
  `530e217f`); the extension root routes keep `StorageDeviceNotConnectedException`. Native
  storage 62/62 (ASan/UBSan/leaks too), Sharp 18,230/18,230, storage 7/7 and C storage 2/2 on
  SOFTWARE/OPENGLES3. Evidence: `../cna-demos/docs/repair-evidence/storage/
  STORAGE_CONTAINER_CREATION.md`.
- `39ef8ea2f` fixes CNA-DEMOS-ISSUE-011 and the real GraphicsResource event defect found while
  correcting ISSUE-082. ContentManager records/disposes materialized resources in XNA order;
  nested/model children and failed partial reads are covered; value wrappers have an internal
  ownership group; content texture Name/Tag metadata shares at the XNA boundary. Loaded C
  Effect/Model handles follow XNA/C++ lifetime after Unload/Destroy. Transferred C texture/font/
  sound handles remain independently owned. IsDisposed is published before Disposing and
  reentrant/throwing handlers remain one-shot.
- `65e69cc5e` corrects the historical ISSUE-082 oracle using genuine XNA. Dispose preserves an
  open SpriteBatch pair, queue and device accounting. Immediate End can balance the interval;
  Immediate Draw observes the disposed DynamicVertexBuffer. CNA refuses at a safe SpriteEffect
  boundary where genuine XNA crashes rather than reproducing a use-after-free.
- `c303d3ad9` plus closure `68471a7a3` fix/close ISSUE-113 overlapping occlusion intervals and
  XNA DWORD wrapping counts. `4dba781c3` preserves the already-repaired ISSUE-010 storage path
  regression tests. These commits are independent and must remain in `next`.

Validation for the latest lifetime batch:

- Native: Core 119 PASS; Content 2,057 PASS / 4 unchanged SKIP; Graphics 3,051 PASS / 115
  unchanged SKIP; Media 355 PASS; Audio 775 PASS / 4 unchanged SKIP.
- Selected SpriteBatch/event tests: 52 PASS on each of SOFTWARE, OPENGLES3, OPENGL33, WEBGPU,
  SDL_GPU and FNA3D's default SDL_GPU/Vulkan driver.
- Sanitizers: ownership 62/62 PASS with affected CNA plus 129 Sharp Runtime translation units
  instrumented; SpriteBatch/event 106/106 PASS under ASan, UBSan, float-cast-overflow and leak
  detection. Third-party dependencies are not claimed instrumented.
- Final external consumer checkpoint: 191 PASS / one unchanged capability SKIP. Exactly ten
  intended rows changed FAIL-to-PASS: C content manager on SOFTWARE/OPENGLES3, SpriteBatch
  begin/end on four renderers, and C resource lifecycle on four renderers. Six exact visual
  references pass; frozen sources/artifacts did not drift. Peak memory 4,301,119,488 bytes;
  `memory.max`, OOM and OOM-kill counters stayed zero.
- C API coverage: 8,103 symbols = 7,018 implemented, 15 partial, 620 planned and 450 not
  applicable. Coverage, limitations and release-gate generators/checks pass. The release gate
  remains honestly not ready because the 620 unrelated planned symbols remain.
- Final closure verification refreshed CMake/CTest metadata, rebuilt all affected cna-demos
  consumers serially, and passed the six repaired SOFTWARE scenarios plus both catalog gates
  (8/8 CTests). Their labels no longer contain `known-issue`.
- Genuine SpriteBatch reference: six fresh Wine/DXVK processes, not native Windows. Four
  deferred/native-effect paths crash inside XNA; two Immediate cases complete. Do not report this
  as six passing tests. See `../cna-demos/docs/repair-evidence/graphics/
  SPRITE_BATCH_DISPOSAL_REVIEW.md`.

The first native lifetime driver used the build directory for asset-dependent Content/Media
tests and consequently recorded 102/234 setup failures. The same unchanged binaries pass when
run from the source directory required by `cmake/UnitTests.cmake`. Those failed logs are retained
and are not product regressions.

The last complete cross-subsystem checkpoint is the 020/029/062 closure (2026-10-23, demos tree
rebuilt from the tree committed as `8842b9a50`): full 719-ID SOFTWARE 654 PASS/14 FAIL/48 SKIP/3
NOTRUN, OPENGLES3 696 PASS/13 FAIL/7 SKIP/3 NOTRUN; serial CTest 735 registered/732 enabled,
684 PASS/48 SKIP/**0 FAIL**/3 disabled (723.84 s). Against the ISSUE-010 closure checkpoint
(`162e07b8a`: 653/15/48/3, 695/14/7/3, same CTest) only `sensor_lifecycle` changed FAIL-to-PASS
and three demos gained their intended checks. The remaining sweep failures are rows whose
`knownIssue` markers name still-open findings; the CTest gate counts them as expected. Reports:
`../cna-demos/build/linux-full/reports/repair-batch3-closure-{full,ctest}*`.

### Exact next task

ISSUE-098 (disposed media library types must refuse their members with `ObjectDisposedException`,
as XNA's do) is in progress in the working tree: a shared guard in
`modules/media/include/CNA/Internal/Media/MediaObjectDisposal.hpp`, guards in the seven entities,
seven collections and `MediaLibrary`, reference-first equality, `MediaPlayer` reading songs
through friendship, and `MediaDisposalTests.cpp`. The C media smokes and the `media.h`/
`media_library.h` disposal comments still describe the old non-refusing contract and must follow.
Then the remaining PENDING_VERIFICATION IDs above.
ISSUE-066 waits for the owner's answer on what enabling blending installs.

Consumer-only work still matters. cna-engine's current `upstream-issues.md` reports UP-001,
UP-006, UP-018, UP-019 and UP-020 resolved; UP-002 remains an informational documentation request
about `CNAEXT` on C++ spellings of implicit XNA semantics. The museum's `docs/CNA_FINDINGS.md`
Open section mixes actionable items with fixes awaiting museum reruns; read each entry's dated
status before changing CNA. Do not claim UBSan/TSan/hardware/service validation when its required
tree or hardware is absent.

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

Repository state last reconciled: **2026-10-23** (`next`). The last genuinely full test run is
still the older checkpoint identified above; do not upgrade focused evidence into a full claim.

Owner-requested museum repair intake (2026-10-22), focused validation and next task:
[`CNA_MUSEUM_REPAIR_HANDOFF.md`](CNA_MUSEUM_REPAIR_HANDOFF.md). This is additional
consumer evidence, not a claim that the full 2026-10-14 checkpoint was rerun.

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
2026-10-13 (`plans/plan_modern_retirement.md`), including their common interfaces, backend
implementations, capability/limit identities, C API routes and tests. `modules/graphics-ext/`
remains (`docs/cnaext-engine-layer.md`).

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

**Content.** XNB readers (including LZX), CNA's `.cnb`/`.cnj`, direct glTF 2.0 loading, the
`cna-content` build tool and the XNA Content Pipeline façade: `docs/content-pipeline.md`,
`docs/xna-content-pipeline-parity-report.md`, `docs/gltf-limitations.md`. No further formats are
planned.

**Tests.** GoogleTest under CTest; `ctest -N` in a build tree gives its count. Latest full Debug
runs, 2026-10-17 at the end of the handoff 5.1 stabilization pass (branch `stabilization`, worktree
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
| Deliberate deviations from FNA/XNA | `CHECKLIST.md`, `plans/plan_bindings_upstream.md` |
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

- **`SpriteSortMode::Immediate` is batched on `SDL_GPU` and `WEBGPU`** (low priority). Both accept
  the mode but defer its sprites, so a texture changed between two `Draw` calls of one Immediate
  batch shows its new contents in both sprites. XNA draws each Immediate sprite with the state and
  texture contents current at its `Draw`, so the first keeps the old contents. `OPENGLES3`
  (EasyGL), `SOFTWARE` and `FNA3D` do. Reproducer: `EasyGL_SpriteBatch_SortModeSemantics` (on
  `SDL_GPU` also `SdlGpu_CrossFamily_SpriteBatch_SortModeSemantics`, elsewhere the same program
  with `CNA_GRAPHICS_RENDERER`); the test passes, and leg B's `[INFO]` line prints blue/blue where
  XNA gives red/blue. Reproduced 2026-10-17.

### 5.2 Limitations

- `SDL_GPU` ignores `BlendState.MultiSampleMask`: the pinned SDL (3.4.0 headers) reserves
  `SDL_GPUMultisampleState::sample_mask`/`enable_mask`, so the mask reaches the renderer and is
  deliberately not applied; `ColorWriteChannels` is honoured.

- Native `SurfaceFormat::Alpha8` render-target storage is unavailable on every renderer:
  `GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Alpha8)` is false, and a
  render target preferring Alpha8 falls back to `Color` as XNA's preferred-format rule allows
  (`GraphicsCapabilityFloatRenderTargetTests.cpp`). Alpha is kept; the one-byte-per-pixel storage
  is not (`Color` is four). How the Xbox 360 allocated an Alpha8 target was never captured.
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

### 5.3 External and driver limitations

Behaviour CNA does not own, measured and reported rather than worked around.

- **Conformance programs on drivers and capability boundaries** (`*_Conformance_*`,
  `modules/graphics/examples/conformance/`, measured 2026-10-15/17):
  - EasyGL: a `RenderTarget2D` requested and applied at 2x resolves with no partially covered edge
    texel (`render_target_own_msaa` D); 4x resolves as expected, so this is the GL driver's 2x
    sample pattern meeting the probe's edge. It is the one failure of the `cmake-build-multi` run.
  - FNA3D: `SkinnedEffect.VertexColorEnabled` is ignored outright (B equals A) and saturates before
    the fold; its back buffer keeps a 3, 5 or 7 sample request unrounded while its render targets
    round it down; `GraphicsDeviceManager`'s default `Depth24` request is not applied in
    `graphics_adapter_query`. FNA3D's driver owns those answers.
  - SDL_GPU and DIRECTX9: cube faces in a multi-render-target set are refused with
    `System::NotSupportedException` while `MultipleRenderTargets` is true (`cube_mrt_binding` M1-M4
    and the plural cube-face `RenderTargetSemantics` cases skip on that refusal); HEADLESS accepts
    them. A capability boundary.
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
