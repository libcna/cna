# CNA handoff — 2026-11-07

## Owner-authorized Galaxy follow-up — ordered key presses (2026-11-08)

After explanation, the owner explicitly approved optional Keyboard::KeyDownEXT with regression
coverage. It raises a Keys event for each non-repeat platform key-down in the existing platform
bridge, before the next Update. It requires no IME/text-input session and preserves event order
and presses released between updates. Keyboard.GetState, key mapping, public state snapshots and
text-input dispatch are unchanged; games opt in by subscribing. ResetAllForTests clears the new
delegate. A test covers givecopter's repeated E, down/up entirely between updates, repeat exclusion
and subscriber removal. The existing CnaInputModuleTests suite passed five shuffled iterations:
654 cases each,653 passed and1 explicit harness-default skip. No full renderer sweep was claimed.
Galaxy consumes this event for cheat letters and keeps its snapshot path for autoplay/replay.


## Owner-authorized Galaxy follow-up — UB-009 (2026-11-08)

The Galaxy Eggbert owner explicitly authorized this CNA repair. EasyGL Texture2D recovery
allocated higher mip levels without restoring their authored pixels. Ordinary non-base levels
now retain their uploaded declared-format bytes while recovery is available, and recovery
replays those bytes. Existing exact DXT shadows are replayed for level0 and every authored mip
as well. No downsampling or replacement mip generation was introduced; allocation/max-level
rules and the public Texture2D API are unchanged.

GPU regression samples real Color/DXT1 mip contents through textureLod before and after two
native context losses, including preservation outside a partial mip update. Before the fix,
Color levels1-3 sampled zero after each recovery. The fixed build passes all15
EasyGLShaderEffectLifecycleTest cases on private Weston/Xwayland (OpenGL33, real GPU).
Focused evidence: Galaxy build-probe/ub009/. This is not a new full seven-tree qualification.


## 0. Read this before continuing

The owner replaced the earlier open-ended campaign with a bounded request: finish the
nearest prepared and in-progress tasks, write an English handoff sufficient for a new AI
context to continue the next roughly 100 hours, then **stop and wait for instructions**.
This document is the result of that request. Do not automatically resume the former
all-feasible-work objective. The work queue below is continuation context, not permission
to keep this session running after handoff.

The bounded tasks are finished and committed locally. All jobs started for this wrap-up
have terminated. CNA has no pending tracked edits after the handoff commit. Untracked
reusable build trees remain; they are not task changes. No push, installation, new language
binding, sub-agent, Claude Code invocation or owner-desktop test was performed in this wrap-up.
The remaining failures and unqualified behavior are explicitly retained in §§0.3, 5 and 8.

The CNA source checkpoint immediately before this documentation commit is
`84c8bb2a3e965890b5b4db0ce33b110b27738265` on `next`. The handoff commit itself is found with
`git log -1 -- handoff.md`; a document cannot contain its own commit SHA.
The stable seven-tree full qualification belongs to the earlier `b7ab4afa4` source;
later focused qualification must not be presented as another full seven-tree pass.

### 0.1 Locations, authority and recovery

| Item | Location and use |
|---|---|
| CNA checkout | `/rv/data/development/github.com/libcna/cna`, branch `next` |
| Durable continuation queue | `../cnaplan/test/HANDOFF_CODEX_QUEUE.json`: nine workstreams, 79 historical repair rows, 60 follow-ups, three native-Windows obligations and accepted/discovered tasks |
| Campaign ledger | `../cnaplan/test/TQ_TASKS.json`, including the original 60 `open_followups` and task commit/evidence records |
| Original repair inventory | `CNA_REPAIR_STATUS.json`; historical status is not fresh qualification |
| Campaign rules and tools | `../cnaplan/test/tools/README.md`, `../cnaplan/test/tools/codex/TQ_RULES.md`, `PHASE1w6.md`, `PHASE2.md` |
| Owner/hardware queue | `../cnaplan/owner-tasks/OWNER_TASKS.md`, with `oracles/`; its Nov 6 assertion that all Linux work is finished is superseded by this handoff |
| Native XACT preparation notes | `../cnaplan/test/XACT_PREPARATION_RUNTIME_NOTES.md` |
| Current evidence root | `../cnaplan/evidence/rev002/`; each receipt identifies scope, source, commands and results |
| Frozen context bundle | `../cnaplan/evidence/rev002/context-handoff-2026-11-07/`: queue/ledger/inventory snapshots, previous handoff, repository identities, task evidence index and SHA-256 manifest |
| Normal readable reference | `/rv/data/development/github.com/FNA-XNA/FNA`; use the local FNA checkout, not old CNA implementations as authority |
| Microsoft reference | `~/deps/xna40-windows-assemblies`, Refresh 4.0.30901; record the actually loaded assembly hash. Original Wine GAC 4.0.20823 is a different reference |
| Numeric IL index | [docs/xna-fna-behavior-decisions.md](docs/xna-fna-behavior-decisions.md), with checked method signatures/offsets and local regeneration instructions |

`cnaplan` is outside Git and outside this session's normal writable root. Use the normal
approval mechanism for its writes; its retained files are not pushed by a CNA commit.
The context bundle preserves the complete historical inventories, so a new context does not
need to reconstruct them from chat. Check the bundle's manifest before relying on a snapshot.
The prior campaign handoff, including the long XACT measurement history, is also recoverable
with `git show 84c8bb2a3e965890b5b4db0ce33b110b27738265:handoff.md`. Finished work belongs in Git and subsystem docs;
§5 below retains the outstanding inventory.

### 0.2 Exact repository separation

| Checkout | Branch / state | HEAD at handoff |
|---|---|---|
| CNA | `next`, local commits, published dependency pins retained | `84c8bb2a3e965890b5b4db0ce33b110b27738265` before this doc commit |
| `../mojoshader` | `cna`, tracked clean, unpublished candidate | `be4787330415a5f180e3a27a671c5425a5de61e6` |
| `~/deps/libcna-FNA3D-candidate` | `cna-audit-fixes`, tracked clean, unpublished candidate | `6e3d1b9ae62d1551de6086bf3be67aeeb5e81b92` |
| Candidate's `MojoShader` | detached linked worktree, tracked clean | `be4787330415a5f180e3a27a671c5425a5de61e6` |
| `~/deps/libcna-FNA3D` | detached published checkout, tracked clean; used by existing CNA builds | `5c0b9b8c13f5cca36a336d481cd5f696d0223275` |
| Published checkout's `MojoShader` | detached, tracked clean | `6ae750b59e6d6e08eadf14d241e38d051fae45a2` |
| `../cna-gltf-viewer` | `develop`, tracked clean, local diagnostic fix | `32f3f4b512615f7f8a9a07a316b47879fd0ea271` |
| `/tmp/cna-sweep12-forks` | source-only CNA worktree, branch `codex/tq-sweep12-forks` | six prepared integration commits, table below |

The source-only `/tmp` worktree contains no builds. Its commits survive in the CNA repository;
do not depend on `/tmp` persistence. Do not alter `../FNA3D`'s pre-existing owner submodule change,
the published detached worktree, or consumer-owned dirty reports. Candidate MojoShader is a
linked worktree: its Git object store is shared, but its source files are separate. A branch
movement in the isolated candidate is not a CNA dependency update.

**Publication has no approval.** An earlier concrete question is pending for exactly seven
MojoShader commits ending at `cc58b0dfadc43598574cc2efac23c65f58374c8f` and eight FNA3D commits
ending at `73fb3376d57a683a0302c21b316454f1bffc98df`. Its review is
`fork-publication-review/review.json` under the evidence root. The later `e0c0b0e`, `3588784`,
`be47873`, `3a09660` and `6e3d1b9` commits are **outside that question**. Never interpret silence
as approval, push a newer HEAD under the old question, force-push, or push CNA/server branches.
The review's observed remote refs are MojoShader `cna=6ae750b59e6d6e08eadf14d241e38d051fae45a2`
and FNA3D `cna-audit-fixes=5c0b9b8c13f5cca36a336d481cd5f696d0223275`; recheck remote identity
if publication is later authorized. No current remote execution is implied by local results.

### 0.3 Stable full sweep and remaining failures

Evidence: `sweep14-restart-b7ab4afa4/receipt.json`, `summary.txt`, seven build logs and seven
CTest logs/JUnit files. Start and end HEAD are both
`b7ab4afa46afb2665b87d316e9257c807726e952`, tracked dirty count zero throughout the terminal
check. All seven whole configurations built successfully. Suites ran sequentially through the
private GPU runner, generally `ctest -j4`; counts include explicit skips.

| Tree | Registered cases | Failed | Explicit skips | CTest exit |
|---|---:|---:|---:|---:|
| multi | 15,371 | 4 | 477 | 8 |
| sdlgpu | 14,718 | 1 | 495 | 8 |
| webgpu | 14,294 | 2 | 497 | 8 |
| fna3d | 14,187 | 1 | 618 | 8 |
| software | 14,235 | 0 | 234 | 0 |
| headless | 13,162 | 0 | 903 | 0 |
| opengl33 | 15,312 | 0 | 267 | 0 |

The four multi failures remain red on the published dependency pair:
`SdlGpuCompiledEffectDrawTest.SharedLegacyBumpEnvironmentContract`,
`Gap13_CompiledCorpus_WEBGPU`, `Gap13_CompiledPolicy_SDLGPU`,
`Gap13_CompiledCorpus_SDLGPU`. Prepared fork/CNA integration fixes exist below; they are not
merged into `next` and do not make this baseline green.

The SDLGPU failure was
`GpuPresentPixels.RendererDestructionReleasesOverrideResourcesButNeverTheHost`; WEBGPU also
failed `GpuPresentPixels.OverrideResizeUsesFreshClientDimensions`. TQ-SWEEP14-01 fixes the
test observer: all four foreign/resized GPU observation constructors now request Auto
Composite capture, as owned clients already did. A raised private X11 occluder reproduces the
old direct-window read failure. After the patch, **all 17 GpuPresentPixels cases pass with no
skips on each of SDLGPU and WEBGPU under the occluder**; forced `window` capture still fails
the original lifetime witness on each. CnaTests rebuilt in SDLGPU, WEBGPU and multi.
Renderer behavior and exact pixel expectations are unchanged. Commit
`fc8728da7fba06fd30c64c6828794f58ab003782`; evidence `sweep14-occlusion/receipt.json`.
There was no second full seven-tree run after this focused repair.

The FNA3D failure is `XnaPipelineGenuineRuntimeBuiltFamilies`: all three content fixtures
(`effect_fx_minimal`, `song_mp3`, `video_wmv`) print PASS/failed=0, then Wine faults executing
`1EC24A76` on thread `0210` during teardown. The nonzero process result remains a failure.
`tests/interop/xna40/Program.cs` creates a separate `ContentManager(game.Services, root)`
without disposing it before the surrounding Game. This is a real ownership gap, **not a
proven explanation of that native fault**. No cleanup patch, exit suppression or skip was
introduced. TQ-SWEEP14-02 investigation: `sweep14-fna-genuine-cleanup/receipt.json` and
`failure.txt`. **Repaired on 2026-11-08 (TQ-SWEEP14-02):** the harness now unloads and disposes the
fixture ContentManager before the GraphicsDevice and disposes the GraphicsDeviceManager and Game.Content
before Game, prints three ordered `OWNERSHIP:` witnesses and its process exit code, and the wrapper
requires all of them. The focused private run passes (exit 0). The native fault did not reproduce in
three runs of the old harness either, so its cause remains unattributed; evidence
`sweep14-02-harness-lifetime/receipt.json`.

Fourteen static gates passed at the stable checkpoint; see `static-gates-receipt.json` in
the same sweep directory. Those results are scoped to their recorded source and commands.

### 0.4 Unpublished fork repairs and their integration order

Six CNA drafts live on `codex/tq-sweep12-forks`, **not on next**. Their exact object IDs are
in `context-handoff-2026-11-07/repositories.json`; short IDs below are unambiguous in these
repositories. Publish only after concrete approval, MojoShader before FNA3D; verify remote
objects, integrate CNA drafts in the order shown, preserve the current handoff when resolving
their old documentation hunks, then qualify current CNA again.

| Task | CNA draft | MojoShader | FNA3D | Change |
|---|---|---|---|---|
| TQ-SWEEP12-10 | `8e7a06d67` | `100025e` | `f2b34dc` | Matrix-only BEM metadata and neutral SDLGPU binding |
| TQ-SWEEP12-11 | `b94acaa8c` | `b6125e5` | `c3d242f` | Scalar-dot specular reflection |
| TQ-SWEEP12-12 | `f4fe4d8e3` | `1ab34bd` | `d7d8adf` | Sampled-result selectors and typed TEXLDD gradients |
| TQ-SWEEP12-13 | `480a1fbaa` | `599ef15` | `cba2142` | Naga-compatible predicated TEXKILL |
| TQ-SWEEP12-14 | `2258cb263` | `2e281ca` | `856a302` | Typed predicated destination writes |
| TQ-SWEEP12-15 | `c4061d687` | `4984c22` | `e95b485` | Scale ps_1.x results before saturation |

Task 14's candidate passes all eight policy wrappers and both original **44-case** GPU
corpora. Task 15 has its own 456 numeric variants, 21 MojoShader tests and 29 FNA3D tests;
the CNA corpora were not rerun at task 15. The later fork work below likewise does not
substitute for current-CNA integration. Original retained and wrong-pixel mutants are
recorded in the receipts. Do not relax corpus assertions to admit a translation.

| Subsequent task | MojoShader commit | FNA3D commit | Evidence directory |
|---|---|---|---|
| TQ-SWEEP12-16, TEXREG2AR/GB/RGB | `cc58b0dfadc43598574cc2efac23c65f58374c8f` | `7b0f2770119da8705b2c45116dd34e7d4e40f0af` | `fork16-texreg/` |
| TQ-FNA-STARTUP-01, empty startup render pass | unchanged | `73fb3376d57a683a0302c21b316454f1bffc98df` | `fna-startup-empty-pass/` |
| TQ-TEXREG-PIXELS-01, explicitly mapped cube/volume pixels | `e0c0b0e947931884d7faa916360912ce34718e0a` | no new commit at this step | `texreg-mapped-native-pixels/` |
| TQ-TEXDP3-01, TEXDP3/TEXDP3TEX | `3588784b253029a3649e8ecbf6184ad5e437892f` | `3a096605b6d43f07e1ad0b6ed1858d67fad9bed6` | `texdp3-emission/` |
| TQ-TEXREG-ZERO-01, nonzero native clear sentinel | `be4787330415a5f180e3a27a671c5425a5de61e6` | `6e3d1b9ae62d1551de6086bf3be67aeeb5e81b92` | `texreg-zero-observation/` |

The old campaign also reused TQ-SWEEP12-16 for a capture-only follow-up; distinguish
that historical `sweep12-capture-followup/` receipt from this fork translation task
`fork16-texreg/` and its exact commits. The later stable baseline supersedes the old
capture-only qualification gap.

The TEXREG2 translation has 204 legal IR variants and dimension-refusal controls. The
direct MojoShader public sampler-map path checks all 64 pixels in 48 2D/cube/volume draws.
The later dot emission has 96 new literal IR variants; TEXDP3 broadcasts dot(UVW,RGB)
to RGBA, TEXDP3TEX samples the **destination-stage** 2D sampler at `(dot,0)`, including legal
`_bx2`. Latest MojoShader: 22 ordinary CTests, 22 device-free ASan/UBSan/LSan CTests and
72 native draws normally and under ASan/UBSan all pass. Device-free runs enable LSan;
native Vulkan runs use `detect_leaks=0` for known driver retention and make no GPU LSan claim.
Counted MojoShader allocations return to zero.

Latest FNA3D: all 35 native harness tests and 144 full 64x32 pixel draws pass across
OpenGL, ES3 and SDLGPU, no validation errors. No FNA3D sanitizer run is claimed for these
new changes. Startup repair materializes an EndRenderPass only for pending clear work:
eight formerly observed zero-viewport Vulkan VUIDs disappear; all eight clear masks and
four clear/empty-pass mutants are qualified. Existing upload barriers remain intact.

The zero-sentinel follow-up found an **observation hole**, not a real fragment-discard bug.
A valid structured SPIR-V OpKill mutant affecting only signed TEXDP3 passed against the
former RGBA-zero background, then failed all six missing-output cases in each native
harness with the new nonzero clear. GL/ES3 and earlier texture controls stayed green.
Both production emitters were restored byte-for-byte before final qualification and commits.

Explicitly mapped cube/volume support is **not public FNA3D/CNA legacy sampler inference**.
FNA3D's SDLGPU CreateEffect path currently supplies a null sampler map; MojoShader effects
can forward an explicit map and otherwise default the undeclared legacy RGB sampler to 2D.
Do not describe the isolated public MojoShader mapping test as full framework cube/volume support.

Only **TEXM3X2DEPTH** remains unimplemented among those six SPIR-V emitters in the local
candidate; its exact named refusal is retained. All six remain refused in CNA's published
MojoShader pin. An earlier intermediate `600ab10` dot commit was amended: use `3588784`,
not that obsolete object. No unfinished production mutation remains in either fork.

### 0.5 Other requests and completed work to avoid repeating

The owner-authorized viewer exception is limited to diagnostic reporting and L7:
“Yes, fix diagnostics and verify L7” (translated from the owner's explicit Czech approval). Viewer `32f3f4b` prints retained System.Exception
causes on the same diagnostic line. Two independent 73-joint refusals now expose the cause,
retain exit 1 and create no PNG; ordinary CLI diagnostics are unchanged. All three registered
tests pass privately. L7 has **148 dispositions: 140 deterministic captures and eight
deterministic rejections**, unchanged zero pixel tolerances and goldens. CNA's current workflow
still pins viewer `6061bff`; no publication or fresh remote CI run is claimed. Evidence
`ci06-viewer-inner-cause/receipt.json`; CNA documentation commit `f24503bc5`.

Galaxy Eggbert UB-008 is the missing `<algorithm>` include in WetBus, fixed by CNA
`e9e009b01b4f2783f4291b3655e0c3dfcca8d245` (TQ-CI-01). The consumer's existing dirty
`/rv/data/development/github.com/openeggbert/galaxy-eggbert/upstream_bugs.md` explicitly
confirms its Web relink. This session did not modify that report or rebuild the whole game.
See `galaxy-ub008-intake/`. The owner explicitly said House Simulator was resolved; do not
reopen its old black-scene issue or reuse its narrow data-generator exception elsewhere.

Other local CNA repairs already committed: TQ-CI-03 portable file-clock fixture
`b4ce4f721` (17 Linux picture tests; native MSVC still pending); TQ-CI-04 generated input
report `81d53e33c`; TQ-DOC-01 shader wrong-form result docs `44366d34b`; TQ-CI-02 ALSA
shared harness `5fbb05039` (498 passes/one named skip, file-output separately passes);
TQ-CI-05 explicit offline SDL_mixer usage `b7ab4afa4` (244/243 offline tests, no-SDL ALSA
243). Local compilation evidence does not establish a new Apple/Windows/GitHub run.
Each has a task-named receipt under the evidence root.

TQ-FNA-QUAL-01 (`863b6445a`) qualifies the **published** pair in aggregate/focused
OpenGL/SDLGPU contexts for two-sided stencil, queued RT uploads, original Texture2D/Cube
upload-order conformance and both positive-level mip witnesses. Eight mip executions pass.
The original malformed-symbol regression passes in all four driver/binary contexts;
two symbolized SDLGPU debugger runs show valid allocators/callbacks and normal cleanup.
FNA3D-7's historical crash stays unattributed, not speculatively fixed. TQ-QUAL-080
(`695dbb71f`) qualifies three VertexDeclaration arithmetic regressions with ASan/UBSan/LSan
and compressed cube format selection: 22 passes, two honest driver-capability skips.
See `fna-current-qualification-b7ab/{fna,tq080}-receipt.json`.

HND-DOC-LINKS-01 (`389881127`) replaces 66 broken IL links with 58 checked signature/offset
anchors and a local regeneration recipe. Genuine Refresh assembly SHA-256 is
`38e7093f52d7474bbc6256906519781a1210d7da50a1c667b52716fcf49ca130`;
fresh ikdasm output matches the retained cache. No Microsoft assembly or instruction listing
is redistributed. See `ci07-and-document-links/cna-doc-links-receipt.json`.

TQ-CI-07 (`84c8bb2a3e965890b5b4db0ce33b110b27738265`) adds xvfb/xauth to the CI package list and gives both aggregate commands a private X11/Xvfb display. Both test filters and all assertions remain unchanged. YAML and all nine shell blocks validate. The nine native-window/capture controls fail without a display and pass with this launch shape in the existing multi HEADLESS control; all nine also pass in the single SOFTWARE/SDL3 control, with no skips. These existing configurations are not an exact clean Ubuntu CI replica. An additional broader local multi aggregate, with a different compiled renderer set and a forced renderer, hit its 900-second harness limit; it is not full qualification, and the planned single HEADLESS aggregate never started. Its logs and timeout are retained. No complete local workflow, GitHub execution or publication is claimed. Evidence: `ci07-and-document-links/cna-ci07-receipt.json`.

### 0.6 XACT continuation boundary

Runtime TQ-XACT-32 is `2d924de08`: stopped cue handles retain SoundBank use; resolved wave
references stay owned by SoundBank until disposal, including partial failed preparation.
Twenty-six genuine Microsoft XACT 3.6 x86/x64 reference cells qualify the plain resident PCM
boundary. Current source-derived interactive deferred playback and cross-bank loop selection
have regressions but do not establish native weighted/interactive/streaming preparation or
exact variation-prefetch timing. The safe forced-disposal cascade remains CNA policy; the
failed native in-use-bank destruction prototype does not prove that safety contract.

Read [docs/xact-instance-limits.md](docs/xact-instance-limits.md), especially TQ-XACT-24–32
and their receipts, [docs/xact-pause-clock.md](docs/xact-pause-clock.md), the durable
preparation notes and the previous handoff §0.13 before changing audio. The exact
Refresh-selected Microsoft XACT 3.6 pause measurement confirms CNA's pause exclusion.
An earlier FAudio FACT clock probe really produced different resume accounting; it was a
FAudio observation and never qualified Microsoft behavior. Do not reintroduce that wrong oracle.

Native reference work has no managed-execution or native gain/PCM qualification. Null/file
audio output only; never use the owner's playback device. Preserve positive ordinary-bank
controls, explicit engine/bank identities and actual return/notification ordering. Future
work must distinguish handles, preparation, playback, stopped state and bank ownership.

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
| Linux x86_64 | Primary host for the recorded native builds and private-display suites. Evidence is scoped to their renderer, driver, feature configuration and fixtures; physical peripherals, services and optional modes retain the limitations in §5 and their subsystem pages. |
| Windows | MinGW-w64 cross builds run under Wine: `DIRECTX9` through DXVK, `SDLGPU`'s Direct3D 12 backend through vkd3d-proton, `SOFTWARE`. `windows-msvc-ci.yml` now automatically builds and tests an SDL-free native-MSVC contract and an SDLGPU Direct3D 12 smoke set; it was added on 2026-10-30 and first passed on 2026-11-02 (`4fef632cf`). The deeper `DIRECTX9` workflow remains manual. No real-Windows hardware validation is current (§5.2). |
| Web (Emscripten) | `WEBGL2` and `WEBGPU` builds run in Chrome (`docs/web-emscripten-graphics-limitations.md`). |
| Android | `OPENGLES3`, the default there, on an x86_64 emulator; `SDLGPU` on Android is not qualified (`docs/android-graphics-limitations.md`). |
| macOS / iOS | macOS CI builds CNA with `SOFTWARE` and runs the portable platform suites and a bundle smoke app, and (gating, since RRC-051, not run yet) builds `SDLGPU` and runs its smoke set on SDL's Metal backend; iOS is a `SOFTWARE` simulator smoke app, with non-gating `SDLGPU` legs. `SDLGPU` is the default on both but is not re-qualified there (§5.2, `docs/apple-platforms.md`). |

**XNA API surface.** Every public type and every documented member of the Microsoft XNA 4.0 runtime
reference is represented; the generated census is
[`docs/xna-4-runtime-member-coverage.md`](docs/xna-4-runtime-member-coverage.md)
(`python3 tools/audit_xna_runtime_surface.py --write-reports`). Representation is not behaviour:
behavioural fidelity is held by each module's tests.

**Native C boundary.** The current native C API is experimental ABI **0.60.0**, defined by
`modules/c-api/include/CNA/C/abi.h`; its change ledger is `docs/c-api/ABI_VERSIONING.md` and its
release verdict is `docs/c-api/RELEASE_GATE.md`. The approved planning boundary is the native
C API/C ABI (`docs/binding.md`); language-specific work needs separate owner authorization.
External consumers own their current admission, packaging and platform evidence. Earlier CNA.NET
branch/ABI pins in this section are historical (recoverable at `bdf18a7d5:handoff.md`), not a claim
that the current external consumer admits this ABI or that CNA maintains a C# binding here.

**Dependencies.** FNA3D and MojoShader are CNA's forks, `libcna/FNA3D` and `libcna/mojoshader`,
pinned at `5c0b9b8c13f5` (FNA3D `cna-audit-fixes`) / `6ae750b59e6d` (MojoShader `cna`)
(`docs/fna3d-renderer.md`). The 2026-11-06 owner-authorized fork repairs fix stencil winding,
SDL_GPU upload order, unsigned mip bounds, duplicate object ownership and SPIR-V BEM/TEXDEPTH.
Owner fork GPU witnesses passed with pre-fix/mutant failures; Current published-pair stencil, upload-order, mip and malformed-symbol qualification is
recorded in §0.5. The historical fuzz seeds and owner LSan replays remain separate gates;
the newer unpublished candidates are listed in §0.4. Both
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

**Tests.** The latest complete seven-configuration baseline is §0.3 (2026-11-07,
clean b7ab4afa4). Later focused fixes and fork candidate suites have their own scopes.
The old October test totals are historical, recoverable in Git; do not use them as
the latest full qualification or claim explicit capability skips as passed pixel tests.

## 3. Where current information lives

| Question | Source of truth |
|---|---|
| Is X broken right now? | §5 of this file |
| Which renderers exist, how are they selected? | `docs/renderer-registry.md`, `docs/runtime-renderer-selection.md` |
| Does renderer R support feature F? | `docs/graphics-renderer-feature-matrix.md`, `docs/renderer-capability-profiles.md`, each renderer's own doc |
| Compiled XNA effects per renderer | `docs/fx-compiled-effects.md` §10 |
| Platform contract | `docs/platform-abstraction.md` |
| API-surface coverage | `docs/xna-4-runtime-member-coverage.md` (generated) |
| C API generation maintenance model | `docs/c-api/GENERATED_ROUTES.md` |
| Deliberate deviations from FNA/XNA | `CHECKLIST.md`, `docs/xna-fna-behavior-decisions.md` |
| What other repositories must change because of a CNA change | `docs/downstream-migrations.md` |
| GamerServices / Net / Avatar boundary | `docs/gamer-services-known-limitations.md` |
| Why is a subsystem in (or out of) an executable? Link boundaries, size options, footprint gates | `docs/link-size-architecture.md` |
| Build rules (ccache, build directories, display isolation) | `CLAUDE.md` / `AGENTS.md` |
| Index of the other documents | `docs/README.md`; what a task ID belonged to: `docs/task-id-index.md` |

## 4. Working rules and owner decisions

Read [AGENTS.md](AGENTS.md), [CLAUDE.md](CLAUDE.md), [CHECKLIST.md](CHECKLIST.md) and the
parent `/rv/data/development/github.com/libcna/CLAUDE.md`. Session authorization overrides
local general guidance, but a narrow exception must not silently become permission for
unrelated work. The current viewer exception covers diagnostics and L7 only. Fork policies
begin on December 1, 2026, and the owner also authorized these fork repairs on November 6;
recheck applicable policy and owner instructions if working after that date.

- Preserve XNA API names, namespaces, signatures, SharpRuntime aliases, visibility, behavior
  and Doxygen contracts. Mark non-XNA additions CNAEXT. System concepts belong in sharp-runtime.
  Match measured Microsoft XNA Refresh when it differs from FNA; record the exact evidence,
  intentional deviation and regression in the behavior-decision ledger. Native C API/C ABI
  is the only approved binding planning boundary. Do not create language-specific bindings.
- Keep the curated renderers/platforms/formats. No new renderer, restored retired engine or
  larger engine layer without an explicit owner decision. WEBGPU is supported, with the
  precise capability boundaries in its own document. Call a CNA graphics implementation a
  renderer; SDL owns its underlying graphics APIs.
- Reuse existing build directories. Closed shared names are build, build-asan, build-ubsan,
  build-tsan, build-probe, build-consumer, plus CNA's existing cmake-build-variant trees.
  No builds, compilers or clones in /tmp/session scratchpads; small text scripts are fine.
  Preserve configuration options. Use `CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv` and
  existing ccache launchers. Never set CCACHE_MAXSIZE or CCACHE_HASHDIR. At most eight
  compile jobs, generally two for forks/sanitizers and four for these CNA targets. Do not
  run heavy builds or full renderer suites concurrently or share a build tree between jobs.
- GPU, window, Wine and UI oracles go through `tools/platform/run_gpu_tests_private.sh`.
  Never DISPLAY=:0 or WAYLAND_DISPLAY=wayland-0. Xvfb is appropriate for CPU-only native-window
  CI inside the private runner; it cannot replace Xwayland/DRI3 for Vulkan GPU qualification.
  Do not revive the old handoff's Wine-on-own-Xvfb-:99 exception. The current sweep ran
  genuine XNA tests through the private runner. Serialize a shared Wine prefix across trees.
- Use `SDL_ASSERT=abort` for unattended native tests so an assertion cannot leave a private
  modal dialog. SDL audio output uses dummy; ALSA output uses the established null/file
  harness and task-owned profiles. No owner's audio-device playback. Device-free LSan and
  GPU detect_leaks=0 evidence are different claims. Do not add blanket suppressions.
- One task is one local commit, after the affected build/tests and plan update. Stage explicit
  paths only; inspect the index first. Never git add -A, git add ., reset/stash someone else's
  changes or sweep up unrelated staged files. Do not restore whole directories to undo an
  experiment. Fork commits require a CNA-Patch trailer. No unsolicited push or installation;
  prepare a concrete review first when permission is actually required.
- Tests need an independent expected result: genuine XNA, then readable FNA/FAudio where
  applicable, then hand-derived values. Establish red/fixed and discriminating mutation
  witnesses where useful. Restore mutations byte-for-byte and rebuild before final claims.
  A source match, registration count, fixture PASS line or metadata gate is not proof of
  runtime behavior. Never lower pixel tolerances, copy output into goldens or silently skip
  a missing capability to make a suite green. Re-run only appropriate checks after a change.
- No sub-agents, delegation, Claude calls or external transmission are requested by this
  bounded wrap-up. Earlier permission to share project material with Claude is not a request
  to transmit this handoff now. The earlier actual weekly Claude refusal is historical;
  do not start retrying it or change accounts/paid usage to exhaust or bypass a quota.
- Do not kill broad process patterns, delete shared build trees or modify consumer-owned
  changes. Address actual access failures with the normal escalation mechanism; don't treat
  a sandbox limitation as evidence that a runtime behavior passed or failed.

The ledger retains owner decisions from November 4–6. Decisions already made include
read-only TouchCollection mutators/snapshot IsConnected, XNA disposal behavior for
OcclusionQuery/Effect, tick-only RunOneFrame, XNA null-recipient/empty-packet refusals and
next-Update AddLocalGamer; the lack of a successor after local host departure is still not
settled as a complete native network policy. Keep CNA's chosen AudioEngine.ContentVersion 46,
Song identity by file until measured, extended XNB platform/version acceptance and existing
null-pointer-overload boundary. TQ-080 uses defined unsigned arithmetic to reproduce XNA's
unchecked Int32 wrap. Refer to the ledger for the full exact decisions before reopening a
historical follow-up. Render-target ContentLost behavior follows the later measured decision,
not older reversed campaign notes.

Present override means full native-window adoption; GPU own-window partial Present keeps
history and foreign-window Present uses the platform region presenter. WMA/XMA uses optional
FFmpeg; XMA and WMA Pro retain their named refusals pending lawful fixtures/native timing.
Stereo spatial audio is retained. Compiled effects default ON only where the family is
qualified; native D3D9 bump evidence is still needed. Online guest rules, rolling-seven-day
Recent leaderboards and additive CNQ/guest protocols are recorded CNA policy, not invented
Microsoft behavior. Never reverse these choices by following stale test expectations.

## 5. Known bugs and limitations

This is the current outstanding inventory. Scope-specific execution details and the latest
sweep failures are in §0; each retained entry states its measurement boundary. Historical
queue labels alone neither establish a current defect nor close one.

### 5.1 Defects

- **Compiled-effect baseline failures: resolved on 2026-11-08 (TQ-FORK-PIN-01).** The owner
  published MojoShader be47873 / FNA3D 6e3d1b9; the six TQ-SWEEP12-10..15 drafts are on `next`
  (feded6a8a..7f0ef4f07) and the pin follows the published pair. In cmake-build-multi all ten
  `Gap13_` tests (eight policies, both exact 44-case corpora), the shared BEM contract and the new
  full-image TEXREG2AR/GB/RGB/TEXDP3/TEXDP3TEX witnesses pass privately on SDLGPU, WEBGPU and both
  FNA3D drivers (evidence: cnaplan/evidence/rev002/fork-pin-01/). TEXM3X2DEPTH stays
  refused by name at this pin; its fork implementation is TQ-TEXM3X2DEPTH-01.
- **Genuine-XNA native teardown fault (TQ-SWEEP14-02), unattributed.** The harness ownership gap is
  repaired (2026-11-08, §0.3); the one-off Wine fault of sweep 14 did not reproduce with the old or the
  new harness. If it recurs, capture `+loaddll,+seh` and a WineDbg stack as INTEGRATOR_COMMANDS.md in the
  evidence directory describes; never suppress the nonzero result.

- **A local host that leaves while another local gamer stays leaves the network session without a host**
  (found 2026-11-04 by TQ-043, confirmed by TQ-054; open: needs an owner policy decision or a native XNA
  measurement). `NetworkSession::RemoveGamer` clears `Host`, as XNA's `ProcessGamerLeft` IL_006d..007e
  does, and nothing supplies a successor; a SystemLink host then welcomes later joiners with no host, so
  their `EndJoin` waits, and existing peers are not told about a CNAEXT local `RemoveGamer`. Managed IL
  does not say whether the session continues, which survivor becomes host or how `AllowHostMigration`
  applies. A conditional design and five candidate tests are in the TQ-054 lane notes
  (`TQ054ExplicitLocalSuccessorIsVisibleToTheEarlierLeave` pins the explicitly supplied case).
- **Signing out never removes a gamer from its network session** (found 2026-11-04 by TQ-043,
  confirmed by TQ-054; open, same evidence gap). The gamer-services dispatcher disposes the identity and
  raises `SignedOut` (`HandlePlayerSignInChanged` IL_0022..0035 / IL_0087..009b), but nothing in
  `modules/net` listens: a session can keep a disposed local identity, and a pending `AddLocalGamer` can
  still join it. The kernel's removal and end policy is not in managed IL.
- **FNA3D dependency repairs (2026-11-06).** FNA3D-1 two-sided stencil is fixed in fork
  `912e26d`; the OpenGL skip is removed from
  `Fna3dPipelinePixels.TwoSidedStencilUsesClockwiseOrdinaryState`. FNA3D-2/-6 duplicate small/large
  Effect object ownership is fixed in MojoShader `04616e1`, delivered through the current pins.
  Historical seeds 20261103001801/20261103032166 (MTH-SAFE-007) and 20261104107187 (TQ-018) remain
  required replay inputs with the repaired dependency identity and LSan active. Fork ownership
  checks and mutants passed; sandbox `detect_leaks=0` runs do not qualify LSan. Full attribution,
  regressions and outstanding evidence: `docs/fna3d-dependency-defects.md`, Fixed on the forks.
  FNA3D-3 and FNA3D-8 remain open. TQ-FNA-QUAL-01 qualifies the positive-level
  SpriteBatch witnesses on both drivers and both binaries; the unchanged malformed-symbol
  regression also passes in all four contexts, with successful symbolized SDLGPU cleanup
  observations. FNA3D-7's historical crash cause remains unattributed; no current crash or
  speculative fork fix is claimed. See the dependency document's requalified reports.

Before these, none was open. The FNA3D OpenGL-driver null EnvironmentMapEffect defect was fixed
on 2026-11-03 (MTH-REV-001): the renderer now creates its opaque-black null cube before binding
s0, because FNA3D's OpenGL driver creates every texture through texture unit 0. Before it,
CNA-FIX-D05 was resolved on 2026-11-02 by reproducing the measured XNA behaviour for overflowing
bitmap regions; see `docs/xna-content-pipeline-compat-api.md`.

### 5.2 Limitations

- **XACT native variation/prefetch qualification remains limited** (TQ-XACT-32,
  2026-11-07). The measured plain resident PCM ownership defect is repaired:
  stopped cue handles retain SoundBank use and resolved wave references remain
  owned by the SoundBank until its disposal, including partial failed preparation.
  Twenty-six genuine Microsoft3.6 x86/x64 reference cells qualify that boundary.
  Source-derived deferred interactive playback and later cross-bank loop selection
  have regressions, but weighted/interactive/streaming native preparation and exact
  variation-prefetch timing still need their own controls. Native Windows, managed
  execution and native gain/PCM remain unqualified. CNA keeps its existing safe
  forced-disposal cascade; the failed native in-use bank-destruction prototype
  does not qualify that safety policy. See `docs/xact-instance-limits.md`.

- **FNA3D cannot pass the physical-coordinate presentation witness** (measured 2026-11-03,
  MTH-CONF-005). FNA3D renders into and reads back its logical back buffer, and FNA3D_SwapBuffers
  scales it into the window. `SpriteBatch_Presentation` reads physical window coordinates (letterbox
  bars, far edge), so six of its checks fail on both FNA3D drivers. It is not registered for the FNA3D
  family. The destination layouts in `docs/fna3d-renderer.md` are implemented; physical-drawable
  readback is not. Closing it needs a physical rendering/readback surface or a separate
  final-image observation contract. SOFTWARE has no physical presentation layout at all
  (`SetPresentationMode` is inert); it presents its logical framebuffer.
- **Projective SpriteBatch matrices are unqualified on WEBGPU and SDLGPU** (MTH-EXIT-002 review,
  2026-11-03). Both expand sprite corners on the CPU, without a homogeneous clip-W vertex stream.
  Affine Begin transforms, including a Z component, are covered by `spritebatch_transform_depth`.
  A matrix with a perspective row is not.
- **The static C API archive with the C++ SDK is unsupported and refused at configure time**
  (decided 2026-11-03 by the owner, MTH-PLAT-004). `libcna_c_api_static.a` remains the encapsulated
  pure-C form; mixed C/C++ programs use the shared adapter and SDK, sharing one runtime.
  Installed CMake targets enforce this rule; pkg-config/plain linker consumers must follow it.

- The retained XNA/Wine wma/mp3_mono_44100_128k.mp3 record returns AudioContent labelled Wma
  with readable metadata, then fails Data access. CNA's AudioContent::ReadThroughMediaDecoder
  instead enforces a Windows Media codec and rejects that MP3 during Import. The current
  XnaWmaImporter.RefusalsMatchXna test covers only missing/empty/truncated WMA, so it does not
  qualify the cross-format call stage. docs/xna-content-pipeline-media.md records the discrepancy;
  obtain native-Windows measurements with working WMA positive controls before treating the
  prefix-limited result as the complete Microsoft contract. No new regression/runtime change
  was added in this documentation task.
- XnaPipelineFinalAuditIsGreen remains an evidence-metadata gate, not runtime qualification
  (CNA-FIX-L05 / MTH-PRE-005). Both it and CApiRouteTestCoverage now separate declaration presence,
  configured CTest registration and validated retained execution. Source scans exclude comments,
  literals and literal false branches; unknown conditions, macro expansion and build inclusion
  remain unevaluated. Optional --execution-record joins ctest_evidence.py artifacts; the explicit
  --require-execution gate refuses missing/disabled/skipped witnesses and wrong evidence categories.
  Passing empty bodies still establish no assertion coverage: runtime qualification stays
  not-established and assertion counts unknown. The 23 new CPU-only regression cases and five
  selected metadata CTests pass in the software tree; no broader runtime/interop/sanitizer or GPU
  qualification is claimed. See docs/xna-content-pipeline-final-audit.md and the route-presence
  evidence section in docs/c-api/README.md for scope and capture identity limits.
- CNB read limits are not a total process-memory budget. Owned input, expanded chunks, decoded
  objects and GPU resources can coexist, and concurrent asset loads remain application-owned.
  Shared animation ranges additionally obey materialized track/key element totals; see
  `docs/cnb-format.md` §12.1 for the bounds and their scope.
- Original CNBF-105/108 payload hashes and raw timing samples are absent, so their historical
  tables cannot be replayed exactly. Corrected probes validate covered I/O/codec paths for new
  stable inputs; they do not establish full CNB-load performance. Production CRC tests compare
  selected dispatch against the table, not every forced hardware/fallback architecture; see
  `docs/cnb-compression-measurements.md` and `docs/cnb-mmap-measurements.md`.
- The historical August clean-build matrix cannot be reconstructed from current defaults alone.
  Current benchmark/report tools record effective cache values and source/dependency identities;
  those hashes are not source/package archives or a capture of every transitive configuration
  input. Matching before/after snapshots cannot exclude transient source changes. RSS remains a
  sampled process-tree sum; the CI report's cache counts are cumulative and its executed-edge
  counts parse console descriptions. See `docs/build-performance.md` for measurement scope.
- SkinnedModelEXT's indexed-skeleton and device checks cannot prove semantic joint identity:
  joint names are absent, and callers still need pieces from a common source rig. Raw clips have
  file/count/range preflight, but input and decoded arrays coexist and do not constitute a total
  process/GPU budget. Vertex joints/weights are not exhaustively validated. Legacy skeleton/vertex
  scalars remain native copies of little-endian converter output, without big-endian execution
  qualification; see `docs/skinned-model-ext.md` for the current validation scope.
- No real XMA encoder output or console playback is qualified: the external adapter's XMA2
  checks, measured loop, fingerprint, processor version and Xbox WAV routing (L16, 2026-11-02) are
  verified with stub and fake encoders only (`docs/xma-encoder-backend.md`).
- Native Camera has no public reopen/device-selector route after Lost. SDL3 removal and denial
  events and a revoked permission now end a session (CNA-FIX-L18), verified with pushed SDL events
  only: no physical unplug or revocation has been run (`docs/cna-devices-camera-design.md`).
- `RasterizerState.MultiSampleAntiAlias` is honored by `SOFTWARE`, desktop `OPENGL33` (also by
  runtime selection) and, since CNA-FIX-L20, `FNA3D`'s OpenGL driver; `DIRECTX9` writes render
  state 161 as XNA does, but its pixel effect is unmeasured because DXVK reports the state
  unhandled. `OPENGLES3`/`WEBGL2`, `WEBGPU`, `SDLGPU` and FNA3D's SDL_GPU-based drivers have no
  per-draw multisample switch, so the property has no effect there. This is separate from
  multisampled target storage, which those families still allocate and resolve
  (`docs/rasterizerstate-support.md`).
- `SDLGPU` applies `BlendState.MultiSampleMask` on a multisampled target only when it keeps every
  sample or none: the pinned SDL reserves `SDL_GPUMultisampleState::sample_mask`/`enable_mask`, so
  a mask of none drops the draw and any partial mask throws `System::NotSupportedException`
  (DC-R2, owner decision, `c7f6c68e5`). `RendererFeature::MultiSampleMask` reports each renderer's
  answer; `ColorWriteChannels` is honoured.

- `SurfaceFormat::Alpha8` render targets are stored natively by SOFTWARE only (L22, 2026-11-02:
  one-byte declared texels, sampled as (0, 0, 0, A)). The GPU renderers answer
  `SupportsSurfaceFormatAsRenderTargetEXT(Alpha8)` false and fall back to `Color` as XNA's
  preferred-format rule allows (`GraphicsCapabilityFloatRenderTargetTests.cpp`): GL has no
  renderable alpha-only format, SDL maps `A8_UNORM` to a swizzled `R8` on Vulkan that a colour
  attachment does not honour, and the shaders would have to route alpha to the stored channel.
  How the Xbox 360 allocated an Alpha8 target was never captured.
- Online sessions and invitations are native-only; a browser build refuses them explicitly
  (`docs/browser-network-readiness.md`, `docs/gamer-services-known-limitations.md` §26).
- `DIRECTX9` is validated under Wine (+DXVK) only. `SDLGPU`'s Direct3D 12 backend is validated
  under Wine (+vkd3d-proton); the automatic hosted native-MSVC smoke workflow first
  passed on 2026-11-02 (`4fef632cf`, eight SDLGPU programs). Hosted smoke is not
  real-hardware qualification, and current local fixes still need a post-publication CI run. The C API's pure-C suites
  run under Wine from the MinGW-w64 build; `MediaLibrarySmoke` reports a skip on Windows, whose
  known folders cannot be pointed at its fixture. The MinGW-w64 test binaries also skip on
  Windows what it cannot provide: a Python interpreter where none can be started, `/dev/full`,
  aging a directory through `utime`, symbolic links, a renderer without 3D, and the ordering guard
  for tests that need an uninitialized `GamerServicesDispatcher`.
- The installed C++ SDK (`CNA_INSTALL_CXX`, `docs/installed-cpp-sdk.md`) is validated relocated on
  native Linux (shared, all-static with the C API off, video OFF) and, for WEBGPU, as a moved
  Emscripten SDK whose application ran three frames in headless Chrome 155 (MTH-PLAT-004,
  2026-11-03); Firefox, Safari and WEBGL2 installed consumers are not run. Its pure-C
  `CNA::CApiStatic` (+ `CNA::CApiStaticAsyncify`) is validated the same way, for WEBGPU only:
  Node and one Chrome page (`docs/c-api/CONSUMING.md`, Emscripten). MinGW-w64 only
  configures and exports the package: no relocated Windows SDK has been run.
- googletest's own stream capture and death tests create temporary files through the ANSI API, so
  any gtest binary aborts under a Windows `TEMP` holding a character outside the ANSI code page
  (measured under Wine, ACP 1252, 2026-11-02). The content and content-pipeline suites are
  otherwise Unicode-clean: under MinGW+Wine they give identical results with an ASCII and a
  non-ASCII (code-page-representable) `TEMP`. No MSVC run has repeated that measurement.
- **`SDLGPU` is the default renderer on Windows, macOS and iOS but is not qualified there** since
  the 2026-10-13 retirement of `DIRECTX11` and `METAL` (`docs/removed-renderers.md`). Evidence that
  exists: the full tree passed with `SDLGPU` (Metal backend) on a Mac mini M4 in the AM4 campaign,
  before the retirement; Direct3D 12 runs under Wine. The automatic hosted native-MSVC Direct3D 12
  smoke gate (added 2026-10-30) first passed on 2026-11-02 (`4fef632cf`): all eight SDLGPU programs on
  the hosted runner's Direct3D 12, which multisamples `D32_FLOAT` but neither stencil format. Not done:
  native Windows hardware qualification, a re-run on Apple hardware, and any iOS build (`SDLGPU`
  has never been built for iOS; CI tries it
  as a non-gating leg). Known gaps against the retired native backends, each to be implemented or
  capability-gated in that campaign: the legacy `ShaderEffect` GLSL-text route (`CustomEffects`)
  is compiled out on Windows, Apple and the browser because no runtime GLSL compiler is present;
  with `CNA_CNAEXT=ON`, explicitly typed SPIR-V remains available on a live SDLGPU device and uses
  ShaderCross for D3D12/Metal. Occlusion queries are unavailable (SDL_gpu 3.5 has none).
  `RendererFeature::BaseInstanceDrawing` is implemented by
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
  (`docs/renderer-registry.md`, "Categories and maturity").
- Per-subsystem capability boundaries, by design rather than defect, are in the documents listed
  in §3.
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
- **MojoShader SPIR-V envelope** (FNA3D's SDLGPU driver, CNA's SDLGPU and WEBGPU compiled effects):
  the published `6ae750b59e6d` pin implements BEM and TEXDEPTH. TEXREG2AR, TEXREG2GB,
  TEXREG2RGB, TEXDP3TEX, TEXM3X2DEPTH and TEXDP3 remain refused by name in that pin.
  The unpublished local candidate implements the five dependent-read/dot opcodes, with the
  numeric/native qualifications in §0.4; TEXM3X2DEPTH alone remains refused there. Public
  FNA3D/CNA cube/volume type inference and current-CNA integration are separate gaps.
  Enabled native/browser pixel gates
  now require BEM and independent register depth output (44 shared corpus cases); OFF builds
  retain all eight capability refusals. See `docs/fx-compiled-effects.md` for qualification limits.
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
- **Audio output device changes are measured nowhere.** The SDL3 backend opens
  `SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK`, so a change of the system default is whatever SDL3 does
  with a default-device stream. The ALSA backend reopens a PCM that disappears (CNA-FIX-L45,
  `docs/audio-alsa.md`), tested with injected `ENODEV` failures only; no physical unplug or
  default-device switch has been run on either backend (former AUD-04-010/011).
- **Three audio reports from the 2026-07-17 audit stay unconfirmed, not open:** high-pitched sound
  effects, missing audio files, and distorted or sped-up gameplay audio. They were never
  reproduced against the game they came from, which is not available. The audit narrowed the
  pitch report to a wrong sample rate reaching CNA before the mixer -- the class CBIND-146 fixed
  (`51d9c84cc`, 2026-10-02, an XNA-built sound's content sample rate).
- Two content-pipeline residues against genuine XNA output, none visible in the 7,726-reference
  sample sweep (`tools/xna-sample-sweep/README.md`, *What is left*): a generated `.x` normal can
  carry the opposite sign of a zero (13 of 1,291 probe normals, `XNASWEEP-231`) -- deciding it
  needs a genuine-importer run with `CNA_MODEL_ORACLE_BITS=1` over probes that separate the
  candidate orders (generation in the file's space before the basis change, or an accumulator
  that starts at `-0`); and FBX bone transforms keep an ulp-level residue that grows with depth,
  inside the float tolerance (`XNASWEEP-228`..`232`), which is the FBX SDK's own arithmetic.
  (`MeshHelper.OptimizeForCache` is exact on all 1,210 probes since `XNASWEEP-149`'s round six.)

### 5.3 External and driver limitations

Behaviour CNA does not own, measured and reported rather than worked around.

- **FNA3D's SDL GPU driver applies no MultiSampleMask** (2026-11-03, MTH-CONF-005). It writes the mask
  into `SDL_GPUMultisampleState.enable_mask`/`sample_mask`, which SDL documents as reserved
  (`FNA3D_Driver_SDL.c` ~1640-1650 at the pinned fork). CNA reports
  `RendererFeature::MultiSampleMask=Unsupported` for that driver (Supported on FNA3D's OpenGL
  driver), and `blend_msaa_boundaries` skips the mask cases with that reason. SDLGPU itself is
  Restricted: only all/none masks.
- **Conformance programs on drivers and capability boundaries** (`*_Conformance_*`,
  `modules/graphics/examples/conformance/`, measured 2026-10-15/17):
  - FNA3D: `SkinnedEffect.VertexColorEnabled` is ignored outright (B equals A) and saturates before
    the fold; its back buffer keeps a 3, 5 or 7 sample request unrounded while its render targets
    round it down; `GraphicsDeviceManager`'s default `Depth24` request is not applied in
    `graphics_adapter_query`. FNA3D-4 upload ordering (ordinary cube/volume/mipmapped textures
    and render-target updates after queued clears/resolves) is fixed by fork `912e26d` at the
    current pin. The owner qualified native fork pre/fixed/mutant witnesses. CNA's interim
    synchronous one-texel render-target upload readback is removed; the existing RT cube/mip
    tests and `texture_setdata_after_draw` conformance have since passed on both drivers
    against the published repaired pair (TQ-FNA-QUAL-01). There is no CPU stall or implicit Present in the fork repair;
    extra submissions per texture update are deliberate. TQ-FNA-QUAL-01 now passes those
    two RT witnesses in aggregate/focused binaries and both ordinary texture conformance legs
    on each driver. FNA3D-5's negative/native-bound conversion and positive-level CNA
    SpriteBatch witnesses pass on both drivers; imported GL textures, bias/minification and
    D3D11 remain unqualified by that point-filtered 4x4 test.

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

- **Licence-header convention** -- decided 2026-11-02: CNA's own sources are MS-PL, including
  `CNA::Internal::Net`, examples, tests, tools and spikes. MIT remains only on the copies of other
  projects that `scripts/check_licence_headers.py` names (DirectXMesh's adapted face optimizer and
  the cna-gamer-services-server protocol files, which keep that project's LICENCE); the
  `LicenceHeadersAreMsPl` CTest enforces it and `CHECKLIST.md` states the rule.

## 7. Build, test and environment recipes

Use the existing trees and cache; there is no CMake target named CNA.

```bash
export CCACHE_DIR=/rv/cnaccache
export CCACHE_BASEDIR=/rv
cmake --build cmake-build-sdlgpu --target CnaTests --parallel 4
env -u TMPDIR -u DISPLAY -u WAYLAND_DISPLAY SDL_AUDIODRIVER=dummy SDL_ASSERT=abort \
  tools/platform/run_gpu_tests_private.sh cmake-build-sdlgpu \
  -R '^GpuPresentPixels\.' -j4 --output-on-failure
```

Existing trees: multi (SDL3, eight compiled renderers, default OPENGLES3), sdlgpu, webgpu,
fna3d, software, opengl33, debug (OPENGLES3), headless (HEADLESS platform/renderer, NULL audio),
mingw (DIRECTX9/Wine), build-asan (SOFTWARE/SDL3), build-probe (HEADLESS/ALSA, SDL disabled,
coverage retained), plus established optional device/web/consumer trees. Read CMakeCache
before use; some old optional trees carry retired SDL_GPU selectors. Do not silently
reconfigure another tree or claim its old executable is current. The refreshed CnaTests
targets after the capture repair are multi, sdlgpu and webgpu; the other stable sweep
binaries still represent b7ab4afa4. Focused targets are not necessarily built by all.

For a whole sweep, build each existing configuration once, keep source/pins clean and
unchanged, then run its complete private CTest suite sequentially. Archive actual return
codes, named skips, JUnit, source/pin and executable/library identities. Do not force one
renderer across the entire multi suite: runtime-selection/fallback tests intentionally
test choices other than an environment-forced identity. Use an explicit renderer only
for the relevant focused wrappers. Tests must not dirty tracked files.

The fork commands below use the isolated candidate; they do not update CNA's published pair.

```bash
# Device-free MojoShader suite (22 registered tests).
env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_ASSERT=abort \
  MOJOSHADER_CNA_SDLGPU_DEVICE=0 \
  ctest --test-dir ../mojoshader/build -j2 --output-on-failure

# Direct mapped native texture pixels (72 draws in two named cases).
env -u TMPDIR -u DISPLAY -u WAYLAND_DISPLAY SDL_AUDIODRIVER=dummy SDL_ASSERT=abort \
  MOJOSHADER_CNA_SDLGPU_DEVICE=1 MOJOSHADER_CNA_SHADERCROSS_STUB=1 \
  LD_LIBRARY_PATH=/rv/data/development/github.com/libcna/mojoshader/build/tests/cna/shadercross-stub \
  tools/platform/run_gpu_tests_private.sh --exec \
  /rv/data/development/github.com/libcna/mojoshader/build/tests/cna/cna_sdlgpu_tests \
  texreg2_native_pixels texdp3_native_pixels

# All 35 candidate FNA3D native harness tests; registered wrappers select their drivers.
env -u TMPDIR -u DISPLAY -u WAYLAND_DISPLAY SDL_AUDIODRIVER=dummy SDL_ASSERT=abort \
  tools/platform/run_gpu_tests_private.sh --exec ctest \
  --test-dir /home/robertvokac/deps/libcna-FNA3D-candidate/build \
  -j2 --output-on-failure
```

After a MojoShader commit, its generated version header changes. Regenerate the existing
build configuration without changing options, rebuild cna_build_tests, then run
`^cna_(build_tests|version_header_merge)$` in both normal and applicable sanitizer trees.
The zero-sentinel task passed both two-test post-commit gates. For device-free sanitizers
use ASAN_OPTIONS=detect_leaks=1 and halt-on-error; direct native Vulkan evidence explicitly
uses detect_leaks=0. Reuse the archived command/environment receipts rather than guessing.

DirectX9 under Wine uses `scripts/run-wine-dxvk9.sh` through the same private runner and
`CNA_D3D9_WINEPREFIX=$HOME/deps/wineprefix-cna-dxvk-3.1.1`; the default prefix still uses
DXVK 2.6.0 with a measured teardown problem. The retained MinGW WINEPATH includes
`Z:\usr\lib\gcc\x86_64-w64-mingw32\14-win32;Z:\usr\x86_64-w64-mingw32\lib`.
Missing SDL3/libcurl DLLs in C smokes are an environment failure, not successful API execution.
Wine/DXVK native-window capture is not real Windows qualification.

Service E2E uses the server's existing build (RelWithDebInfo, /usr/bin/python3 for websockets)
and matched CNA client harnesses; sanitizer suspicions use the established build-asan pair.
Set the CNA_SERVICE_*_HARNESS variables and avatar catalog paths from the archived service
recipes; guest C API clients additionally need the C API library directory. NAT exit 77
without slirp4netns is a named prerequisite gap, not an E2E pass.

Use the relevant static gates, not a report rewrite as a substitute for a repair:
renderer identities/combinations/runtime discipline/targets/descriptors, SDL inventory,
classification/ratchet/renderer audit, hot-path/nonproduction SDL audits, licence headers,
and C API coverage/limitations/release gates. Their authoritative scripts and mappings are
in tools/platform, tools/c-api and scripts. Header line changes can alter generated source
inventories; new test files can alter input-test-coverage.md. Regenerate only when warranted,
then recheck. Metadata presence still does not certify assertion quality or runtime coverage.

## 8. Continuation queue for the next owner-authorized context

These are ordered work packages, not a promise that all hardware and publication dependencies
can be completed within 100 hours. Keep every queued item, act on current evidence and report
progress at least once a minute during active work. When the owner asks for status or an
estimate, answer directly; elapsed time is not evidence of progress. Do not start any item
until the owner resumes work after this handoff.

### 8.0 Start here, then choose feasible work

1. Read this §0, §5, AGENTS.md, CLAUDE.md, CHECKLIST.md, the frozen queue and campaign rules.
   Check `git status`, HEADs, linked worktrees and actual live jobs. Do not assume a remembered
   session handle is live or restart work merely because its output handle is gone. Review
   receipt files for completed work before rebuilding it. Preserve unrelated dirty files.
2. Publication/integration is the highest-value path to the four known multi failures, but
   requires the owner's concrete push authorization. The pending question covers only the
   old exact cc58/73fb endpoints; prepare a new exact review if newer endpoints are requested.
   Once permitted, publish MojoShader first, verify its object on the remote, publish FNA3D
   with the matching gitlink, verify that remote object, then integrate the six CNA drafts
   in order. Resolve their old handoff hunks in favor of current facts. Add a separate
   current full-SHA pin/integration task for later fork work. Never put an unavailable local
   dependency SHA into published CNA or claim that candidate evidence qualifies next.
3. Rebuild affected aggregate and focused targets with the integrated identities. Run all
   eight policies, both unchanged 44-case GPU corpora and the shared BEM draw privately;
   also replay OPENGL33 compiled effects and the relevant default/refusal configurations.
   Add public CNA pixel witnesses for newly admitted legacy instructions. Current 2D FNA3D
   draws and explicit MojoShader sampler-map tests are independent controls, not substitutes
   for the framework path. Verify OFF builds still refuse unsupported capabilities by name.
4. Implement the last emitter, TEXM3X2DEPTH, as its own fork task if still authorized. Begin
   with legal D3D token/refusal controls, literal numeric/depth oracles and native output,
   then normal/sanitized fork suites and production mutations. Do not weaken the exact
   missing-emitter refusal before a real implementation is ready. Source-only findings:
   `state_TEXM3X2DEPTH` needs implicit DEPTHOUT registration and consumed-destination guards;
   current PAD evaluation is deferred in GLSL/SPIR-V. Investigate legal `_bx2` rules for each
   member of the PAD/depth pair before deciding whether a deferred dot is valid. Microsoft's
   instruction contract is z/w with w=0 producing 1; values outside [0,1] are undefined.
   A proposed independent byte fixture is source RGBA `(32,96,160,224)`, numerator row
   `(1,0,0)`, denominator row `(0,4,0)`: plain depth `1/12`, signed depth `191/252`, plus an
   all-zero denominator control expecting 1. This is a proposed oracle, not executed evidence.
   Verify PAD-time capture if mixed modifiers are legal. No implementation was started here.
5. Separately investigate public legacy cube/volume sampler-type inference. Trace the actual
   FNA3D/CNA Effect creation and metadata path; use an API-compatible map/contract rather
   than a test-only private override. Prove 2D/cube/volume full pixels with independent
   wrong-type/wrong-coordinate mutants. Keep unsupported forms explicitly refused until
   the public path has the needed information. Preserve the explicit-map controls.
6. TQ-SWEEP14-02: qualify ownership cleanup in the genuine XNA harness. The separate content
   manager should have an explicit nested lifetime ending before Game/GraphicsDevice.
   Establish a positive ownership/disposal witness and real process-exit evidence; unchanged
   fixture PASS lines alone are insufficient. Preserve the original Wine fault log and
   distinguish fixing a definite lifetime gap from proving that fault's cause. Build through
   the existing interop targets/tree. The Program.cs mcs recipe is retained in build-probe's
   build.ninja with four genuine XNA references, sdk4, x86; do not compile in /tmp. Capture
   loaded DLL identities and a symbolized failing stack if the native fault recurs.
   CTest's `xna-interop-scratch` lock is only per test configuration: never overlap Wine
   runs using the same prefix across trees. No blanket skip, exit-code relaxation or crash
   suppression is acceptable. This task was deliberately deferred at the owner's stop boundary.
7. Continue XACT only after reading the TQ-XACT-24–32 docs and native receipts. Next questions
   are weighted/interactive/streaming preparation, exact variation-prefetch timing, cross-bank
   selection and native notification/resource lifetimes. Begin each shape with a supported
   ordinary resident PCM control; use the exact Refresh-selected XACT3.6 x86/x64 identities.
   The current plain stopped-handle/bank ownership fix is already complete. Managed execution,
   native gain/PCM, random event offsets, overlapping PlayWave, filter-ADD and ramp timing
   retain their separate measurement boundaries. Do not infer them from FAudio or from a
   count hook that does not observe the actual playback/resource state.
8. Reconcile the original 79 repair rows and 60 quality follow-ups, one evidence-backed task
   at a time. They include already fixed historical findings and owner decisions. Do not count
   all 139 as remaining bugs or blindly repeat their old fixes. Join IDs to Git history,
   subsystem docs, accepted tasks and current-source witnesses. High-value clusters include
   XML importer identity/line data, content-reader state and 16-bit images, stock-effect clone
   parameters, weak inspector/optional-mode coverage, sensor/storage/microphone events,
   network transition timing and historical sanitizer seeds. Retain C++ reference/null and
   explicit owner-policy boundaries. Update only claims actually established by a test.
9. Owner-policy/native-evidence questions remain in the two §5.1 network entries: local-host
   departure with surviving locals and sign-out/session removal. Managed IL establishes only
   part of the behavior. Reuse TQ-054's lane notes and explicit-successor control; measure the
   native transition or obtain the owner's choice instead of inventing migration/sign-out
   policy. Service E2E needs loopback access and matched client harnesses. NAT requires the
   still-unapproved slirp4netns installation; do not install it as an incidental fix.
10. Recheck consumer reports and current CI only when that work is resumed. UB-008 and viewer
    L7 are done locally. Remaining Apple/MSVC/CI results need publication and real execution;
    no local syntax test establishes them. Revisit each owner task for actual local feasibility,
    preserving genuine Windows/hardware requirements and the three §8.1 obligations. Do not
    inherit the old OWNER_TASKS claim that everything feasible on Linux is already pushed.
11. Once the integrated source has passed focused gates, freeze a clean HEAD and run the seven
    existing whole builds, then sequential full private CTests. Archive exact sources/pins,
    cache identities, logs, JUnit, return codes and named skips. Do not modify source while a
    baseline sweep is running. Use full terminal results, not grep of intermediate PASS lines.
    Avoid another multi-hour sweep after every prose-only task or before focused failures
    are fixed. Resolve each genuine failure separately and keep capability boundaries honest.

The nine preserved workstreams are HND-SWEEP, HND-OWNER, HND-FORK, HND-XACT, HND-NET,
HND-GALAXY, HND-CI, HND-QUALITY and HND-LIMITATIONS. The frozen queue retains every original
row and its historical text. The 45 older handoff inventory entries are snapshots, not an
alternative current §5. If sources conflict, investigate and record the measured resolution.

### 8.1 Three cna-demos fixes awaiting a native Windows run

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
