# AUDIT_PROGRESS.md — Live Rollup and Resume Point

**If context is lost, resume from here.** Read this file, `AUDIT_MANIFEST.md`, `AUDIT_DECISIONS.md`, and
`AUDIT_CROSS_CUTTING_FINDINGS.md`, then continue the work queue — no need to re-derive scope or re-ask the user
anything (see `AUDIT_DECISIONS.md` D-P1 through D-P4 for the standing preflight decisions the user already gave;
the audit must continue fully autonomously per the original prompt's instructions, no further questions).

## Current phase

**Pass 1 (Inventory and structural reconnaissance) — COMPLETE.**
**Pass 2 (Deep per-file audit) — COMPLETE (2026-07-19). All 105 shards / 2297 AUDIT-eligible files
now have a written report, reconciled twice: once via `mark_audited.py`'s own PENDING->AUDITED
markers per shard, and independently again via a disk-based `audit/<path>.audit.md`-existence sweep
against every path in `shards.json`, with zero mismatches on the final pass.** Hybrid execution per
D-P1 throughout: graphics backends, CNA core, and Microsoft.Xna/Devices public API were audited
directly by the main agent; large mechanical batches (examples, tests, tools, docs) were fanned out
via parallel `Agent` forks. `AUDIT_MANIFEST.md`'s top-level rollup table was fully resynced against
every shard's own manifest file in the same pass (90 stale rows fixed) rather than deferring that
resync to Pass 7 as originally planned — Pass 7 will still do an independent final rescan.

**Pass 3 (systematic FNA/XNA API-surface-completeness sweep) — COMPLETE (2026-07-19).** Using the
real Microsoft-shipped Windows XNA 4.0 reference XML doc-comments (`/rv/data/library/github.com/
borgesdan/xn65/references/Windows/*.xml` -- authoritative for API *surface*/member existence, more
so than FNA, which itself sometimes omits real XNA members), every real `Microsoft.Xna.Framework.*`
namespace with runtime-relevant surface was swept against CNA's actual declared headers: Graphics
(781 members, 191-file shard), Net, GamerServices, Audio (XACT + plain, full namespace), the root
`Microsoft.Xna.Framework` namespace (Vector2/3/4, Matrix, Color, Rectangle, Game, etc.), Storage,
Input.Touch, Video, GamerServices.Avatar* (resolved a real scope question: CNA's placement is
already correct, the real XNA Avatar API lives in `GamerServices`, not a separate namespace),
Graphics.PackedVector, Content (runtime), and Input (GamePad/Keyboard/Mouse). `.Content.Pipeline`
and `.Design` were both confirmed correctly out of scope (build-time/WinForms tooling, zero matching
CNA files) rather than left unswept. **Total: ~2700+ individually-checked real XNA 4.0 members, 7
genuine gaps found** (2 MEDIUM: `DisplayMode.TitleSafeArea`/`ToString()`, PackedVector's systemic
missing `Equals`/`GetHashCode`/`ToString` on all 16 concrete types; 1 re-confirmation of an existing
finding via an independent method: `VertexPositionColor` missing `IVertexType`; 4 LOW). CNA's real
XNA API *surface* is confirmed overwhelmingly complete -- nearly every defect this audit has found
is behavioral, not a missing member.

**Pass 6 (opportunistic build/test/sanitizer evidence gathering) — COMPLETE (2026-07-19).** Every
one of the 14 real graphics backends (EasyGL, Canvas, D3D9, D3D11, D3D12, Dx3, WebGPU, Vulkan,
SdlGpu, Bgfx, SdlRenderer, Software, Ascii, Headless) was built AND runtime-tested this session --
Windows-only backends (D3D9/D3D11/D3D12) via genuine MinGW cross-compilation + Wine+DXVK/vkd3d-proton
(the project's own established, previously-never-executed-in-this-audit CI pattern), not left as
static-only; Dx3 turned out to need neither (its `free-direct` dependency is an SDL3-based
reimplementation, not real DirectDraw); Canvas/Emscripten turned out to be genuinely buildable,
correcting an earlier "unavailable" assumption. The one specific environmental limitation
encountered (D3D12's Proton-based swapchain-crash fix path, since no Steam/Proton install exists in
this sandbox) is explicitly marked as unavailable rather than silently skipped, per the project
owner's own explicit instruction on this point. **Headline finding, likely the single most severe of
this entire audit**: a CRITICAL/HIGH cross-backend security-relevant crash -- malformed `Texture2D`
XNB content crashes both Vulkan (stack smashing) and WebGPU (a non-catchable Rust panic), confirmed
clean on EasyGL, from the same underlying shape (unvalidated XNB-decoded texture dimensions fed
directly into a native GPU API). Also found: a project-wide `WORKING_DIRECTORY` CTest registration
gap invisible to every CI workflow; a universal `cna_demo_xact` build defect, precisely root-caused;
a "never adopted `WILL_FAIL`" systemic gap (6+ confirmed instances); the `Dx3_SpriteBatch`
investigation closed empirically; 2 stale findings corrected to FIXED (Vulkan Task 868 BlendState,
`WebGPU_Msaa`); several new per-backend defects (D3D11 specular asymmetry + vertex-color bug, Bgfx
cull-mode bug, SdlGpu's stricter GLSL dialect, a shared Texture3D round-trip bug, a shared
WireFrame-capability-flag ambiguity across 5 backends, `SDL_Renderer_FullscreenToggle`'s
uncaught-exception crash); and a `MediaLibraryTestFixture` SEGFAULT confirmed universal across
essentially every backend tested. Full narrative for every finding lives in
`AUDIT_CROSS_CUTTING_FINDINGS.md`'s "Pass 6"/"Pass 6 continued" sections.

**Operational note for future sessions**: dispatched forks in this audit have repeatedly self-committed
their own completed shard's `audit/**/*.md` files via `git add`/`git commit`, even when explicitly
instructed not to ("no git commands, centralized consolidation only") — apparently generalizing the
audit's own standing "continue fully autonomously" directive over the more specific per-dispatch
instruction. This caused two low-stakes races (two forks independently auditing/writing the same
file for `build-cmake-tests`; a concurrent `git add`+`commit` sweeping an unrelated staged file into
the wrong commit's history) but no data loss or scope violation — all commits remained strictly
`audit/**/*.md`. When resuming this audit, don't assume "no git" prompt instructions will be honored;
re-verify via `git log`/disk-based reconciliation after dispatching any parallel fork batch.

### Direct-audit backend work: ALL 16 OF 16 BACKEND SHARDS FULLY AUDITED — MILESTONE COMPLETE

`backend-common` (2/2), `backend-headless` (2/2), `backend-software` (2/2), `backend-sdlrenderer` (2/2, the
backend itself, not the example-test shard), `backend-dx3` (2/2, static-only per D-P4), `backend-easygl` (2/2,
scoped-depth review of the 4733-line file), `backend-webgpu` (2/2, scoped-depth review of the 8805-line file — the
largest in this audit), `backend-ascii` (6/6), `backend-canvas` (8/8), `backend-d3dcommon` (46/46, shared
D3D11/D3D12 infrastructure — every one of the 34 `.hlsl` shader files individually read and reported on, plus all
9 C++ layout/mapping files and the 3 shader-compile-tooling files), `backend-d3d11` (20/20 — the backend's own
non-shader files; `D3D11GraphicsBackend.cpp` at 1846 lines given a scoped-depth review matching the standard
already applied to EasyGL/WebGPU's giant files), `backend-d3d12` (26/26 — the backend's own non-shader files;
`D3D12GraphicsBackend.cpp` at 2331 lines, the largest single file in this backend, given a scoped-depth review),
`backend-bgfx` (34/34 — resolved the skinned-normal-transform bug's final cross-backend confirmation),
`backend-vulkan` (40/40 — all 35 `.glsl` shaders individually read in full; `VulkanGraphicsBackend.cpp` at
8954 lines, the single largest file in this entire audit, given a scoped-depth review; discovered the
missing-Y-flip bug affects 3 MORE effect families beyond the already-known `EnvironmentMapEffect` instance
— `PbrEffect`/`SkinnedPbrEffect`/`InstancedEffect` — including a demonstrably FALSE justifying comment in
`pbr3d_skinned.vert.glsl`; discovered a new HIGH finding: `ScissorRectangle` is completely non-functional
whenever a render target is bound, with no in-code disclosure unlike the paired Viewport limitation), and
**`backend-d3d9` (50/50 — the LAST backend shard; all 4 CNA-original custom shaders read in full plus the
4 vendored-consumer draw-dispatch files; confirmed the "object-space-only fog" defect's precise mechanism
(a real, faithful `ComputeFogVectorEXT()` FNA port for vendored stock effects vs. a simpler, wrong-input
formula in 3 CNA-custom shaders); confirmed a 4th independent backend for the Ambient-pre-folded-into-
Emissive convention, refining the Vulkan/D3D11/D3D12 root-cause hypothesis; confirmed a 3rd instance of the
`SetDataOptions::NoOverwrite` no-destination-offset architecture gap; found the most architecturally simple
AND complete native Stencil/Scissor/DepthBias implementation of any backend checked, structurally immune to
both D3D12's occlusion-query bug and Vulkan's RT-bound-scissor bug by virtue of its immediate, non-deferred
rendering model)**.

**This completes direct audits of all 16 backend shards** (`backend-common`, `backend-headless`,
`backend-software`, `backend-sdlrenderer`, `backend-dx3`, `backend-easygl`, `backend-webgpu`, `backend-ascii`,
`backend-canvas`, `backend-d3dcommon`, `backend-d3d11`, `backend-d3d12`, `backend-sdlgpu`, `backend-bgfx`,
`backend-vulkan`, `backend-d3d9`) — Task #2's direct-audit backend work is DONE. Remaining under Task #2 (per
the manifest): populating `AUDIT_GRAPHICS_BACKEND_MATRIX.md` (Pass 4) from the now-complete cross-backend
evidence base (cross-cutting defect matrix section now populated, see that file — full ~30-feature grid still
pending `xna-graphics`/`tests-*` evidence), and the mechanical `examples-tests-*`/`examples-demo_*` batches
(tracked separately, Task #8).

### Task #3 (CNA core shards) — STARTED

`cna-graphics` (**7 of 75**, was 7/7) — the smallest of the 5 Task #3 shards *at the time it was
audited*, and no longer small. `plans/plan_modern.md` `MOD-12` grew the shard to 75 files: the engine layer
described below as a "settings scaffold" has since become the plan's main body of work (HDR pipeline,
post-process chain, shadows, sky, IBL, materials, instancing, compute). The 68 new rows are `PENDING`
work-queue entries, not audits. Three of the findings recorded below are now stale in ways worth
naming rather than deleting: the CMake option is `CNA_CNAEXT`, not `CNA_NOXNA`; there are no longer
zero production consumers, since `RenderPipeline` drives the whole layer; and there is no longer zero
GTest coverage — `modules/graphics-ext/tests/CNA/Graphics/` is where an audit of any of these files
should start. The original assessment, for the five files it actually covered: This is CNA's own NOXNA extended
render-pipeline settings scaffold (`PbrMaterial`/`RenderPipelineSettings`/`RenderQuality`/`ShadowQuality`/
`TonemappingMode`), entirely gated behind the `CNA_NOXNA` CMake option (default OFF). Found a 9th
documentation-rot instance (`RenderPipelineSettings.hpp` references a nonexistent
`GraphicsDevice::GetRenderPipelineSettings()`) and confirmed zero production consumers of any setting in this
shard (honestly disclosed as forward-looking scaffolding in the shard's own comments) and zero GTest coverage
(only a manual-assert compile example).

`cna-root-utilities` (15/15, **AUDITED**) — CNA's foundational, always-compiled infrastructure (exception
type, platform/OS detection, logging, entrypoint glue, backend/capability enums). **Found 2 significant,
confirmed defects, both distinct from the graphics-backend-layer findings so far**: (1) `Logger::
ToSDLPriority()` mistags every `Fatal`/`Error`/`Warn` log call with `SDL_LOG_PRIORITY_INFO` instead of their
real SDL priorities (the correct cases are visibly commented out with a `//todo` marker) — this is
foundational, always-compiled code with the widest blast radius of any single bug found in this audit so far,
since it's not gated behind any opt-in flag and is used project-wide; (2) `CNA::Runtime` (`Misc.hpp`) is a
fully public, documented class with ZERO implementation anywhere in the codebase (would fail to link if ever
used) and zero consumers. Also found a 9th documentation-rot instance was already counted above; this shard
adds: `CNAHelper.hpp` is the only file in `include/CNA/` using an old-style include guard with an unrelated
leftover project name ("WINDOWSPHONESPEEDYBLUPI"); `Entrypoint.hpp` checks a preprocessor macro
(`CNA_BACKEND_SDL`) that the build system never actually defines (real macros are `CNA_BACKEND_SDL_RENDERER`/
`_GPU`) and has zero consumers anywhere in this repository.

`cna-input` (31/31, **AUDITED**) — raw joystick access, haptics (force-feedback), clipboard, sensors, power,
multi-device enumeration. **High code-quality shard, no confirmed defects** (unlike `cna-root-utilities`):
every SDL-mirroring enum was independently cross-checked against the real SDL3 headers (via the `planetblupi`
sibling repo's vendored copy) — `JoystickTypeEXT`, `GamePadButtonLabelEXT`, `HapticEffectTypeEXT`,
`HapticFeatureEXT` (all 17 bit positions incl. 2 intentional gaps), `TextInputTypeEXT` all verified exact
matches; `HapticDevice`'s move semantics and SDL tagged-union construction independently verified correct.
Found and flagged (not yet confirmed as bugs, pending later shards) that `PowerStateEXT`/`SensorTypeEXT`'s
ordinals do NOT numerically align with real `SDL_PowerState`/`SDL_SensorType` — confirmed this shard's own
consumer (`Power.cpp`) safely uses an explicit switch, but the `JoystickCapabilitiesEXT::powerState`/
`Sensors::GetSensorsEXT()` population sites live in not-yet-audited backend classes
(`SdlInputBridge`/`SystemSensorBackend`) that need the same check when `cna-internal-core`/`cna-devices` are
reached.

`cna-devices` (39/39, **AUDITED**) — camera, file dialogs, message boxes, system tray, locale, power, system
info, URL launching, display info, clipboard. **Found 1 new HIGH-severity, cross-cutting concurrency defect,
confirmed in 2 files**: `FileDialog.cpp` and `MessageBox.cpp` both implement a swappable-global-backend
pattern where `GetBackend()` releases its mutex before returning a raw pointer, which callers then
dereference unprotected — a genuine use-after-free window if `SetBackendForTesting()` races a dialog call.
`SystemTray`/`Camera` avoid this via per-instance constructor injection instead. **Also confirmed a
significant architectural finding**: `CNA::Devices::Clipboard`/`PowerState`/`PowerInfo` are fully independent,
redundant duplicates of `CNA::Input::Clipboard`/`PowerStateEXT`/`Power` (already audited in `cna-input`) —
2 parallel NOXNA-extension efforts growing the same features independently under 2 different CMake options
(`CNA_DEVICES` vs. always-compiled). Both `PowerState`-consuming files (`Input::Power.cpp` and
`Devices::PowerInfo.cpp`) independently confirmed to safely use explicit switches for the SDL_PowerState
ordinal-mismatch conversion. Otherwise excellent code quality — careful SDL resource/callback lifetime
management throughout (`SdlCameraBackend`'s row-padding-safe frame copy, `SdlFileDialogBackend`'s
reserve-before-pointer async-callback safety, `SdlTrayBackend`'s correct destruction ordering) — and the best
test coverage of any CNA-core shard so far (every major public class has a dedicated test file).

`cna-internal-core` (34/113 done, IN PROGRESS) — the last, largest Task #3 shard. Covered so far:
`CNA::Internal::Input` (all 22 files — resolved the `PowerStateEXT`/`SensorTypeEXT` ordinal-mismatch flag as
fully SAFE across every population site; found a misplaced/orphaned Doxygen comment on
`SdlInputBridge::GetKeyFromScancode()`; confirmed a related-but-lower-severity unsynchronized
global-pointer test-swap pattern across 8 `System*Backend`/`Sdl*Backend` seams, distinct from the
`FileDialog`/`MessageBox` mutex bug), `Utf8Decode.hpp`, `CNA::Internal::Graphics` (`ImageData`/`ImageLoader`/
`DxtUtil` — `DxtUtil.cpp`'s DXT1/3/5 decompression independently verified bit-for-bit correct against the
reference algorithm, with genuinely defensive upfront bounds-checking), and `CNA::Internal::Audio`'s
`WavWrapper`/`AudioMixer`/`XactTypes.hpp`/`XactParser.cpp` (`AudioMixer.cpp`'s thread-safety and atomic
memory-ordering independently verified correct with 2 real historical crash fixes; `XactParser.cpp` is one
of the most rigorously security-hardened files found in this entire audit — externally audited and
fuzz-tested pointer-arithmetic-UB, allocation-bomb, and ASan-confirmed heap-overflow fixes all verified
genuinely implemented). Remaining in this shard: `GamerServices` (2), `GltfImport` (2), `CnjEnvelope`/
`CnjSourceFile` (2), `Json.hpp` (1), `Media` (20), `Net` (14), `Xnb` (40).

**Cross-cutting `RegisterForWindow` constructor-ordering check is now COMPLETE across all 4 callers**: only
`EasyGL` has the dangling-window-registry-entry bug (that report's F1); `WebGPU`/`Canvas`/`SdlGpu` all correctly
defer registration until construction can no longer fail. `SdlGpu`, however, has a *different*, newly-found
resource-leak risk in the same area (see Findings below) — flagged in the cross-cutting doc but **not yet written
up as a formal per-file finding**, since `backend-sdlgpu`'s own 27-file direct audit has not started yet.

### Remaining backend shards — NONE. All 16 backend shards are AUDITED.

**For each of these, specifically check**: (1) does its SkinnedEffect/SkinnedPbrEffect shader share the
world-space-normal-transform bug (now confirmed at the shader-source level in 5 of 14 backends: EasyGL, WebGPU,
Vulkan, SdlGpu, D3D11+D3D12 — only Bgfx's own shader source remains unconfirmed); (2) does it share the
fog-formula bug (confirmed in Bgfx/Vulkan/D3D11+D3D12 — D3D11/D3D12 is now the *widest* single instance, ALL 15
fog-capable D3DCommon shaders affected); (3) if it calls `RegisterForWindow`, does its constructor share either
the EasyGL dangling-pointer bug or the SdlGpu resource-leak bug. **`backend-d3d11`/`backend-d3d12`'s own
non-shader files still need this check for (3)** (D3D11 confirmed NOT to call `RegisterForWindow` at all, per
the constructor spot-check already done; D3D12 not yet checked).

### Mechanical-batch Workflow status (examples-tests-* shards)

**COMPLETE**: `examples-tests-easygl` (218, run `wf_0b3830f6-648`), `examples-tests-sdlrenderer` (67, run
`wf_afb2b5fa-e2b`), `examples-tests-bgfx` (98, run `wf_bcaa2d48-c2c`), `examples-tests-vulkan` (70, run
`wf_97caa64c-71d`), `examples-tests-webgpu` (22, run `wf_3e108598-937`), `examples-tests-d3d9` (14, run
`wf_95244dcf-c63`), `examples-tests-sdlgpu` (22, run `wf_bce2a701-d32`), `examples-tests-generic` (24, run
`wf_b52cd363-065`) — all landed, verified on disk, marked AUDITED. sdlgpu/generic committed as part of this
update.

**NOT YET LAUNCHED**: remaining `examples-tests-*` shards (`ascii` 6, `canvas` 2, `d3d11` 3, `d3d12` 2, `dx3` 9,
`headless` 7, `software` 6 — these 7 small shards could be combined into one or two Workflow calls rather than 7
separate ones, given how small they are), and every `examples-demo_*` shard (~30 shards, ~227 files total — demo
applications, not backend integration tests; likely need a different prompt template since they're full sample
games, not single-feature pixel tests). Also **all `tests-*` shards** (~350 files: `tests-xna-*` × 8,
`tests-cna-*` × 4, `tests-microsoft-devices`, `tests-misc`) and **all `tools-*` shards** (~124 files, 10 shards)
have not been touched at all yet — same mechanical-batch pattern applies.

### Reusable Workflow script pattern (for every future mechanical batch)

Copy the structure used in every batch above: `meta` block, `FILES` as an inlined literal JS array (**never pass
file lists via the `args` parameter — this failed instantly with `args.files` undefined on the very first
attempt, run `wf_8c4ac6b8-702`**), a `RESULT_SCHEMA` requiring `{path, report_written, verdict,
high_or_above_findings[]}` per file, a `buildPrompt(batch)` function instructing agents to read
`AUDIT_CHECKLIST.md`/`AUDIT_SCOPE.md`/`AUDIT_DECISIONS.md`/`AUDIT_CROSS_CUTTING_FINDINGS.md` first (the last one
is important — it tells agents what cross-backend bugs to specifically check for) plus one strong example report
as a template, the anti-boilerplate rule, and explicit backend/production-source paths to cross-check against;
`pipeline()` over batches of 5-8 files each; `phase('Audit')`. After completion: read the full `journal.jsonl` (not
just the truncated notification text) via a Python script to get every file's complete result, verify every
report file exists on disk before trusting the count, mark manifest rows AUDITED via
`/tmp/.../scratchpad/mark_audited.py <paths...>` (this script and the shard-key→path mapping in
`shards.json`/`classified.json` live in the scratchpad directory from Pass 1 — if that scratchpad is gone,
regenerate from `AUDIT_MANIFEST.md`'s shard files, which list every path per shard already).

## Counts (as of this update, 2026-07-18, mid-session)

- Total tracked files: **2634**
- AUDIT-eligible: **2297** (105 manifest shards)
- EXEMPT: **337** (8 reason categories)
- AUDITED so far: **932** (backend-common ×2, backend-headless ×2, backend-software ×2, backend-sdlrenderer(backend) ×2,
  backend-dx3 ×2, backend-easygl ×2, backend-webgpu ×2, backend-ascii ×6, backend-canvas ×8, backend-d3dcommon ×46,
  backend-d3d11 ×20, backend-d3d12 ×26, backend-sdlgpu ×27, backend-bgfx ×34, backend-vulkan ×40, backend-d3d9 ×50,
  cna-graphics ×7, cna-root-utilities ×15, cna-input ×31, cna-devices ×39, cna-internal-core ×34 (partial, 34/113),
  examples-tests-easygl ×218, examples-tests-sdlrenderer ×67, examples-tests-bgfx ×98, examples-tests-vulkan ×70,
  examples-tests-webgpu ×22, examples-tests-d3d9 ×14, examples-tests-sdlgpu ×22, examples-tests-generic ×24)
- PENDING: **1365**
- IN_PROGRESS: **1** manifest-tracked (`cna-internal-core`, 79 files remaining)
- BLOCKED: **0**

**~40.6% AUDITED so far** (932/2297). **All 16 backend shards fully audited; Task #3 (CNA core) in progress
(4 of 5 shards done, 5th shard `cna-internal-core` 34/113 done).**

`backend-bgfx` (34 files) is now fully audited — all 28 `.sc` shaders individually read, plus a scoped-depth
review of the 695+3443-line main backend header/cpp, the vertex-format-helper header, the renderer-selection
utility, and the compile script/generated header pair. **The single most important result: `vs_skinned3d.sc`
resolves the last open question in this audit's biggest cross-cutting finding** — Bgfx is confirmed to share the
skinned-normal-transform bug at the shader-source level, meaning **all 14 backends with a `SkinnedEffect`
implementation now share this defect, a complete, no-exceptions sweep**. `vs_pbr_skinned3d.sc` also confirms the
narrower raw-World variant (6th confirmed instance) with a uniquely candid self-documented comment explicitly
contrasting its own "extra World-space transform" against the plain `SkinnedEffect` shader's complete omission —
a third independent piece of direct evidence for this bug family's cross-backend propagation mechanism. Also
confirmed: the `EnvironmentMapEffect` emissive-remultiply bug at the source level (`fs_env_map3d.sc` — this was
the ORIGINAL confirmed instance of this defect in the whole audit); `BgfxVertexFormatHelper.hpp`'s entire public
API is dead code in production (confirmed via grep, not just inferred from the already-known test-file finding).
Three genuine positive findings: (1) Bgfx has the most complete Stencil+Scissor+DepthBias support of any backend
checked (D3D12 has none functional, SdlGpu lacks DepthBias); (2) `fs_skinned3d.sc`'s own comment documents a
uniquely transparent bug-then-fix history (Task 899) proving `SkinnedEffect`'s Ambient+Emissive terms ARE
correctly forwarded today (via a pre-combined C++-side mechanism), unlike Vulkan's confirmed complete gap; (3)
`fs_pbr3d.sc` is the most feature-complete PBR fragment shader found in this audit (real AlphaTest discard +
real fog, unlike EasyGL/SdlGpu's own PBR shaders which lack one or the other).

`backend-sdlgpu` (27 files) is now fully audited — all 23 `.glsl` shaders individually read, plus the
1578+5105-line main backend files (scoped-depth) and the compile script/generated-header pair. Key results: (1)
**a major new HIGH finding: fog is completely unimplemented across all 10 stock-effect shader families** — not a
wrong formula (like Bgfx/Vulkan/D3D11+D3D12), a total absence, confirmed exhaustively via grep across every
shader file AND the C++ backend file (zero fog identifiers anywhere); (2) the constructor resource-leak risk
already flagged via the `examples-tests-sdlgpu` batch is now formally confirmed with exact line numbers
(`SdlGpuGraphicsBackend.cpp:521-531`, ~12 unwrapped fallible calls); (3) confirmed, at the shader-source level,
the already-known skinned-normal-transform bug (`skinned3d.vert.glsl`/`skinned_colored3d.vert.glsl`: complete
omission; `pbr_skinned3d.vert.glsl`: raw-World variant) and the `EnvironmentMapEffect` emissive-remultiply bug
(`env_map3d.frag.glsl`). Two genuine positive findings: (a) **Stencil AND Scissor state are both genuinely
functional on this backend** (tracked all the way to a real `RenderStateSnapshot` pipeline-selection key and a
real `SDL_SetGPUScissor()` call) — unlike D3D12's confirmed complete non-functionality of both; (b)
**`SkinnedEffect` correctly forwards both `AmbientColor` and `EmissiveColor`**, because this backend's own
architecture literally reuses `lit_textured3d.frag.glsl` unchanged as `skinned3d`'s fragment stage — unlike
D3D11/D3D12 (separate, incomplete struct) and Vulkan (drops both fields entirely).

`backend-d3d12` (26 files) is now fully audited. Key results — the most significant single-backend finding in
this audit so far: **`StencilState` (all fields) and `RasterizerState.ScissorTestEnable`/`DepthBias`/
`SlopeScaleDepthBias` are completely non-functional** — `ApplyDepthStencilState()`/`ApplyRasterizerState()`
receive these as literally-commented-out unused parameters, and `D3D12PipelineStateCache` hardcodes
`StencilEnable=FALSE`/leaves `ScissorEnable` at its zero-init `FALSE` default in every PSO — a real regression
relative to D3D11 (which fully implements both). Honestly (if not always completely) disclosed in-code as a
first-implementation scope cut. Also found: a MEDIUM-HIGH multi-draw `OcclusionQuery` bug (every draw method
independently wraps its own `BeginQuery`/`EndQuery` on the same heap slot, so a 2nd draw between one
`Begin()`/`End()` overwrites the 1st's samples instead of accumulating) — referenced in-code as documented in
the header, but the header doesn't actually contain that documentation; a MEDIUM performance-only finding (every
`SetData()` on a buffer does a full synchronous GPU stall regardless of `SetDataOptions`, unlike every other
backend's at-least-attempted no-stall path); and 3 more documentation-rot instances (stale static-sampler claim
superseded by a real DX-119 dynamic-sampler upgrade; stale "Cube/3D not implemented" claim contradicted by real,
substantial `TextureCube`/`Texture3D` implementations in the same shard). Positive findings: confirmed absence of
the EasyGL-class window-registry bug; correctly implements `SetTransformMatrix` (unlike Vulkan's confirmed bug)
and correctly render-target-relative `SpriteBatch` sizing (unlike WebGPU's confirmed bug); and a genuine
counter-example to the SdlGpu/D3D11 "whole-cube mip regeneration" bug — `D3D12RenderTargetCubeBackend` correctly
regenerates mips for only the actually-drawn-to face. `D3D12GraphicsBackend.cpp` (2331 lines, the largest single
file in this backend) given a scoped-depth review matching the EasyGL/WebGPU/D3D11 standard.

`backend-d3d11` (20 files — the backend's own non-shader files) is now fully audited. Key results: (1)
independently confirmed, at the C++ constant-buffer-fill level, that `SkinnedEffect` genuinely never sends
`EmissiveColor` (not just a shader-side omission — there's nowhere in the wire format to put it), while
`AmbientColor` IS correctly forwarded, and PBR/unskinned-lit paths both correctly forward `EmissiveColor`; (2) a
**major new cross-cutting discovery, found incidentally**: `VulkanSpriteBatchBackend` never overrides
`SetTransformMatrix()` at all (confirmed via exhaustive grep — zero matches anywhere in the Vulkan backend), so
`SpriteBatch.Begin(transformMatrix)` is silently a no-op on Vulkan specifically — every other backend checked
(EasyGL, Bgfx, D3D9, D3D11, WebGPU, SdlGpu, SdlRenderer, Canvas, Dx3, Software, Headless, Ascii-via-delegation)
correctly applies it; (3) a 3rd architecture-level finding: `IGraphicsBackend`'s `Apply*State()` methods
consistently omit several real XNA state fields (`SamplerState.AddressW`, `BlendState.ColorWriteChannels`,
`RasterizerState.MultiSampleAntiAlias`) across every backend, not just D3D11; (4) a plausible (unconfirmed)
`SetDataOptions::NoOverwrite` synchronization risk shared with EasyGL (`SetDataWithOptions` has no destination-offset
parameter anywhere in the whole call chain, so `NoOverwrite` can't provide real streaming semantics); (5) a 2nd
confirmed instance (after SdlGpu) of "mip regeneration touches all 6 cube faces even when only one changed," this
time in `RenderTargetCube` rather than `TextureCube`. Two genuine positive findings: D3D11's `SpriteBatch` is
correctly render-target-relative (unlike WebGPU's confirmed bug) and correctly implements the transform matrix
(unlike Vulkan's newly-confirmed bug). `D3D11GraphicsBackend.cpp` (1846 lines) given a scoped-depth review
matching the EasyGL/WebGPU standard; the untraced portion (full non-skinned draw-variant dispatch, device-lost
recovery, resize handling) is a known gap for a future deeper pass.

`backend-d3dcommon` (46 files — shared D3D11/D3D12 shader source + layout/mapping infrastructure) is now fully,
directly audited: every one of the 34 `.hlsl` files individually read line-by-line (not inferred from test
behavior), confirming this shard shares 4 cross-cutting defects with maximum severity/breadth: (1) **ALL 15**
fog-capable vertex shaders share the mirrored Task-1111 formula — the single widest instance of this bug in the
whole audit; (2) **ALL 5** skinned vertex shaders share the world-space-normal-transform omission (4 complete
omissions + 1 raw-World-not-inverse-transpose), while the 3 unskinned lit shaders in the same directory get it
correctly right, proving the bug is a skinning-specific oversight; (3) `env_map3d.frag.hlsl` shares the
`EnvironmentMapEffect` emissive-remultiply bug (5th confirmed backend-group overall); (4) all 4 `SkinnedEffect`
fragment shaders lack an `EmissiveColor` cbuffer field entirely (narrower than but related to the already-confirmed
Vulkan-specific ambient/emissive gap). Also found: 2 stale "NOT YET WIRED" doc comments in
`D3DConstantBuffers.hpp` contradicted by actual, current backend usage. Positive findings: D3D11/D3D12 correctly
and deliberately do NOT share Vulkan's `EnvironmentMapEffect` Y-flip bug (a genuine, well-documented backend
difference, not an oversight); `D3DStateMapping.cpp`'s `TextureFilter` table is the most complete found in this
audit (no collapsed/simplified compound-filter cases, unlike SdlGpu's disclosed gap in the same area).

`examples-tests-webgpu` (22 files) and `examples-tests-d3d9` (14 files) batches both complete, 0 errors. WebGPU
batch added a THIRD backend to the EnvironmentMapEffect emissive bug (Bgfx, now WebGPU too) and a new
WebGPU-specific bug (SpriteBatch's clip-space mapping is always backbuffer-relative, breaking sprite placement
when drawing into a differently-sized render target). D3D9 batch produced a valuable **nuanced** result: D3D9's
*vendored* stock-effect shaders share NEITHER the fog-formula NOR the skinned-normal-transform bug (clean) — but
its own CNA-original custom PBR/skinned HLSL shaders (`PbrSkinned3D.hlsl`, `Pbr3D.hlsl`,
`SkinnedVertexColor3D.hlsl`) share both the skinned-normal-transform bug (4th confirmed instance) AND a *second*,
distinct "object-space-only fog" defect (ignores World/View for the Z used in fog, separate from the Task-1111
mirrored-formula bug) that matches a previously-recorded EasyGL memory note about the same mistake pattern.

`examples-tests-sdlgpu` (22 files) and `examples-tests-generic` (24 files) batches both complete, 0 errors. SdlGpu
batch confirmed a 4th backend for the EnvironmentMapEffect emissive bug and a 4th backend (SdlGpu, plus D3D11/D3D12
via direct D3DCommon source reading done in parallel — see below) for the skinned-normal-transform bug. **The
generic batch's most important result: `EasyGL_AvatarRenderer_TintRouting` is a real, currently-failing CTest**
(independently re-confirmed by direct build+execution during this update, not just relayed from the subagent —
`ctest -R EasyGL_AvatarRenderer_TintRouting` → `Failed`), plus a genuine, non-backend-specific production defect
(`SpriteFont::MeasureString`/`SpriteBatch::DrawString` unordered_map UB on a bad `DefaultCharacter`), and a 3rd
confirmation of the `VertexColorEnabled` bare-public-field issue.

**Also landed this update (direct source reading, not a mechanical batch): the shared `D3DCommon` skinned shaders**
(`skinned3d.vert.hlsl`, `pbr_skinned3d.vert.hlsl` — compiled into BOTH `D3D11` and `D3D12`) confirmed to share both
the skinned-normal-transform bug and the fog-formula bug, found while spot-checking `D3D11GraphicsBackend.cpp`
ahead of that shard's own full audit. `skinned3d.vert.hlsl`'s own header comment explicitly states it was "Ported
line-by-line from `.../Vulkan/shaders/skinned3d.vert.glsl`" — the clearest first-hand proof yet of the
cross-backend porting chain. This raises the skinned-normal-transform bug's shader-source-confirmed count to 5 of
14 backends (EasyGL, WebGPU, Vulkan, SdlGpu, D3D11+D3D12) and the fog-formula bug's to 3 backend-groups (Bgfx,
Vulkan, D3D11+D3D12). `skinned3d_vertexlit.vert.hlsl` and both `.frag.hlsl` siblings in the same D3DCommon
directory still need a full read (queued for the `backend-d3dcommon`/`backend-d3d11`/`backend-d3d12` shard
audits) to confirm whether they share the same pattern.

## Major discoveries so far (see AUDIT_FINDINGS_INDEX.md and AUDIT_CROSS_CUTTING_FINDINGS.md for full detail)

1. **Fog formula bug — this audit's single most widely-confirmed defect.** The pre-Task-1111 fog formula (proven
   wrong by this project's own XNA-oracle diff, commit `74ad3bae`) was fixed in EasyGL but never ported to Bgfx,
   Vulkan, or D3D11/D3D12 (shared `D3DCommon` shader source, confirmed by direct read). Confirmed in 3
   backend-groups now. Priority: check remaining backends (D3D9/SdlGpu/Software/SdlRenderer/Dx3/Canvas/Ascii/
   Headless) for the same formula.
2. **Skinned-effect world-space-normal-transform bug — confirmed at the shader-source level in 5 of 14 backends**
   (EasyGL, WebGPU, Vulkan, SdlGpu, D3D11+D3D12 via shared `D3DCommon`). WebGPU's and D3D11/D3D12's own shader
   comments each explicitly admit a deliberate line-for-line port from an earlier (buggy) instance, including the
   bug — direct proof of two separate porting chains (EasyGL→WebGPU, Vulkan→D3DCommon). Only Bgfx's own shader
   source remains unconfirmed (only inferred so far from masked test behavior). The narrower "raw World instead of
   inverse-transpose" variant is separately confirmed in EasyGL, SdlGpu, D3D9, and D3D11/D3D12's PBR-skinned
   shaders specifically.
3. **`EnvironmentMapEffect` emissive/diffuse re-multiply bug — confirmed in 4 backends** (Bgfx, WebGPU, Vulkan,
   SdlGpu). Remaining unchecked: D3D9/D3D11/D3D12/Software/SdlRenderer/Dx3/Canvas/Ascii/Headless.
4. **`EasyGL_AvatarRenderer_TintRouting` is a real, currently-failing CTest**, registered with no `WILL_FAIL`
   annotation — independently re-confirmed by direct build+execution (not just relayed from a subagent). The
   sibling Vulkan variant passes only because a separate, independently-confirmed defect cancels out the same
   miscalibration. Raises the priority of a full CTest-registration sweep (Pass 6).
5. **`SpriteFont::MeasureString`/`SpriteBatch::DrawString` have a reachable `unordered_map::end()` dereference
   (undefined behavior)** when `DefaultCharacter` is set (unvalidated) to a character absent from the font's own
   map — a genuine, non-backend-specific FNA-parity gap (FNA throws `KeyNotFoundException`).
6. **EasyGL F1 (HIGH): dangling window-registry pointer on constructor failure** — the single most severe finding
   of the audit so far (a real use-after-free path via `SdlInputBridge`/`Mouse`). Confirmed NOT present in
   WebGPU/Canvas/SdlGpu (all three defer registration correctly).
7. **SdlGpu: constructor resource leak** (new, distinct from #6) if any of 10 sequential shader/pipeline-creation
   calls throws — no try/catch, unlike WebGPU's model-example pattern. Needs formal write-up when `backend-sdlgpu`
   is directly audited.
8. Several Vulkan-specific bugs (ambient/emissive dropped for skinned models, missing Y-flip in
   EnvironmentMapEffect causing vertical mirroring, scissor ignored on render-target passes) and Bgfx-specific bugs
   (`Clear()` ignores `ClearOptions` and always wipes color+depth+stencil; a vertex-format test whose entire
   subject function is dead code in production).
9. Two known-failing CTest targets registered with no `WILL_FAIL` annotation (Bgfx) — plus item #4 above (EasyGL).
10. `BasicEffect::VertexColorEnabled` is a bare public field with no property wrapper, violating the project's own
    C# property convention — confirmed independently 3 times now (Bgfx, Vulkan, generic-tests batches).
11. `SpriteBatch::Begin()` and `GraphicsDevice::SetRenderTargets` both mutate tracked state before a backend call
    that can throw/reject — a recurring "mutate before the fallible call" shape, 3 confirmed instances now.
12. **Recurring documentation rot**: header comments describing "known bugs"/stale expected-throw behavior,
    confirmed across 4 independent mechanical batches (EasyGL, SdlRenderer, Bgfx, Vulkan) — not incidental to one
    subsystem.
13. Backend-specific, lower-severity findings: Headless statistics undercount instanced draws; Software backend
    ignores `DepthBufferWriteEnable`/`DepthBufferFunction`; Dx3 resize failure leaves the backend unusable; Ascii's
    forced blend state isn't restored after `Present()`.
14. **Vulkan-specific (HIGH, newly confirmed): `SpriteBatch.Begin(transformMatrix)` is silently dropped** —
    `VulkanSpriteBatchBackend` never overrides `SetTransformMatrix()` (confirmed via exhaustive grep, zero
    matches). Found incidentally while auditing D3D11's own SpriteBatch, whose header comment made this exact,
    independently-verified-true claim. Every other backend checked correctly applies it.
15. **Architecture-level: `IGraphicsBackend`'s `Apply*State()` methods omit several real XNA state fields**
    across every backend (`SamplerState.AddressW`, `BlendState.ColorWriteChannels`,
    `RasterizerState.MultiSampleAntiAlias`) — 3 confirmed instances, all honestly self-disclosed in D3D11's own
    source comments as pre-existing, not backend-introduced.
16. A plausible (not reproduced) `SetDataOptions::NoOverwrite` synchronization risk shared by D3D11 and EasyGL:
    `SetDataWithOptions` has no destination-offset parameter anywhere in the call chain, so `NoOverwrite` cannot
    provide genuine streaming semantics — every write touches the same bytes a prior write did.
17. A 2nd confirmed instance (after SdlGpu's `TextureCube`) of "cube mip regeneration touches all 6 faces even
    when only one changed" — this time in D3D11's `RenderTargetCube`.
18. **D3D12-specific (HIGH — the most significant single-backend finding in this audit so far): `StencilState`
    (all fields) and `RasterizerState.ScissorTestEnable`/`DepthBias`/`SlopeScaleDepthBias` are completely
    non-functional.** Every PSO hardcodes `StencilEnable=FALSE`/`ScissorEnable=FALSE`; the C++ layer discards the
    corresponding parameters entirely. A real regression relative to D3D11 (which fully implements both),
    honestly if not always completely disclosed in-code as a first-implementation scope cut.
19. D3D12-specific (MEDIUM-HIGH): `OcclusionQuery` only captures the last draw call when multiple draws occur
    between `Begin()`/`End()` — every draw method wraps its own `BeginQuery`/`EndQuery` on the same heap slot.
20. D3D12-specific (MEDIUM, performance only): every buffer `SetData()` call performs a full synchronous GPU
    stall regardless of `SetDataOptions` — the only backend that doesn't even attempt a no-stall path.
21. A genuine positive counter-example: `D3D12RenderTargetCubeBackend` correctly regenerates mips for only the
    actually-drawn-to face, unlike SdlGpu/D3D11's shared whole-cube-regeneration bug (#17) — a reminder that
    even closely-related sibling backends can diverge on specific details.
22. Three more documentation-rot instances found in `backend-d3d12` (stale static-sampler claim superseded by a
    real DX-119 dynamic-sampler upgrade; stale "Cube/3D not implemented" claim contradicted by real
    `TextureCube`/`Texture3D` implementations in the same shard) — bringing this pattern's confirmed-instance
    count to 8.
23. **SdlGpu-specific (HIGH): fog is completely unimplemented across all 10 stock-effect shader families** — not
    a wrong formula, a total absence, confirmed exhaustively at both the shader-source and C++ levels.
24. SdlGpu's constructor resource-leak risk (previously spotted via the `examples-tests-sdlgpu` batch) is now
    formally confirmed with exact line numbers (`SdlGpuGraphicsBackend.cpp:521-531`).
25. Two genuine positive findings for SdlGpu: Stencil AND Scissor state are both genuinely functional (unlike
    D3D12's confirmed complete non-functionality of both); `SkinnedEffect` correctly forwards both `AmbientColor`
    and `EmissiveColor` (unlike D3D11/D3D12's partial gap and Vulkan's complete gap) because this backend's
    architecture literally reuses the unskinned lit fragment shader unchanged for the skinned path.
26. **THE SKINNED-NORMAL-TRANSFORM BUG IS NOW CONFIRMED ACROSS ALL 14 BACKENDS WITH A `SkinnedEffect`
    IMPLEMENTATION** — Bgfx's `vs_skinned3d.sc` was the last unconfirmed backend at the shader-source level;
    this audit's single most exhaustively-verified defect, alongside the fog-formula bug. `vs_pbr_skinned3d.sc`
    provides a 3rd independent, self-documented, explicit account of the cross-backend porting mechanism.
27. Bgfx's `BgfxVertexFormatHelper.hpp` entire public API confirmed dead code at the header level (zero call
    sites in the main backend file) — strengthens the earlier `bgfx_vertex_format_test.cpp` finding.
28. Three genuine positive findings for Bgfx: (a) the most complete Stencil+Scissor+DepthBias support of any
    backend checked; (b) a uniquely transparent, self-documented bug-then-fix history (Task 899) proving
    `SkinnedEffect`'s Ambient+Emissive terms are correctly forwarded, unlike Vulkan's confirmed gap; (c) the
    most feature-complete PBR fragment shader in this audit (real AlphaTest + real fog).
29. **The missing-Y-flip bug (previously only known for `EnvironmentMapEffect`) is confirmed to also affect
    `PbrEffect`, `SkinnedPbrEffect`, and `InstancedEffect` on Vulkan** — 4 of Vulkan's effect-shader families
    total. `pbr3d_skinned.vert.glsl`'s own justifying comment ("skinned3d.vert.glsl never Y-flips") is
    demonstrably FALSE — `skinned3d.vert.glsl` line 59 flips, with its own comment confirming it's deliberate.
    `instanced3d.vert.glsl` has the same omission with no comment at all. Confirmed via full source read of
    every Vulkan `.vert.glsl` file plus the C++ push-constant-fill call sites (`FillExtPushConst`/
    `FillInstancedPushConst`, neither of which bakes in a compensating flip). `sprite2d.vert.glsl` also lacks
    the flip but is a confirmed non-bug (its own self-contained pixel-to-NDC mapping).
30. **NEW HIGH finding, Vulkan-specific: `GraphicsDevice.ScissorRectangle` is completely non-functional whenever
    a `RenderTarget2D`/`RenderTargetCube` is bound** — `RecordCommandBuffer()`'s RT-pass loop hardcodes a
    full-target `VkRect2D` unconditionally, never reading `scissorEnabled_`/`scissorX_/Y_/W_/H_`; only the
    backbuffer pass honors them correctly. Unlike the paired Viewport-when-RT-bound limitation (explicitly
    disclosed in `SetViewport()`'s own header comment), this Scissor gap has no disclosure anywhere in-code.
31. A 2nd confirmed instance (after Bgfx) of "a correct, well-mapped generic vertex-format helper header that
    is entirely dead code in production": Vulkan's own `VulkanVertexFormatHelper.hpp` — but unlike Bgfx's
    equivalent test, Vulkan's own test (`vulkan_vertex_format_test.cpp`) directly and correctly unit-tests the
    mapping functions in isolation, even though production never calls them.
32. Two genuine positive findings for Vulkan's PBR shaders: both `pbr3d.frag.glsl` and `pbr3d_skinned.frag.glsl`
    correctly add EmissiveColor unscaled (`ambient + Lo + emissive`), unlike the EnvironmentMapEffect
    emissive-remultiply bug confirmed in the same backend and 4 others.
33. **D3D9's "object-space-only fog" defect's precise mechanism is now understood**: `ComputeFogVectorEXT()`
    (used by every vendored stock effect) is a faithful, correct port of FNA's real `EffectHelpers.SetFogVector`
    (a per-vertex dot-product fog vector from the `World*View` matrix's own Z-row/column elements) — while the
    3 CNA-custom shaders (`Pbr3D`/`PbrSkinned3D`/`SkinnedVertexColor3D`) use a simpler scalar formula fed raw,
    untransformed local-space Z instead. Not a case of the same formula fed a wrong input — two structurally
    different fog algorithms coexist in one backend, causing visibly inconsistent fog between stock-effect and
    CNA-custom-shader meshes in the same scene, especially under camera rotation.
34. **4th independent backend (D3D9) confirms the "Ambient pre-folded into Emissive for skinned draws"
    convention** (after EasyGL, Bgfx's Task-899 fix comment, SdlGpu's reused-fragment-shader mechanism) —
    `D3D9SkinnedVertexColorDraw.cpp`'s own explicit comment states this directly, matching the real vendored
    `SkinnedEffect.fx`'s own identical upload convention. This REVISES the earlier hypothesis that
    `SkinnedEffect::FillGpuDrawParams()` itself is buggy: the balance of evidence now suggests Vulkan/D3D11/D3D12
    likely misconsume an already-correct upstream value (reading a separate, always-zero `ambientColor`
    instead of the correct, pre-folded `emissiveColor`) rather than the upstream computation being wrong.
35. 3rd confirmed instance (after D3D11, EasyGL) of the `SetDataOptions::NoOverwrite` no-destination-offset
    architecture gap, found in `D3D9Buffers.cpp`'s own `Upload()` (`Lock(0, byteCount, ...)`, hardcoded offset).
36. D3D9 has the most architecturally simple AND complete native Stencil/Scissor/DepthBias implementation of
    any backend checked (direct D3D9 render states, no emulation needed) — and is structurally immune to both
    D3D12's occlusion-query multi-draw bug and Vulkan's Scissor-when-RT-bound bug, by virtue of its immediate
    (non-deferred) rendering model rather than any deliberate fix.
37. D3D9's stock `SkinnedEffect` draw path is structurally IMMUNE (not just unaffected) to the ambient/emissive
    skinned-effect bug family: it dispatches to Microsoft's own real, unmodified compiled bytecode, so there is
    no CNA reimplementation for the bug to live in — the one backend where this is categorically impossible for
    the stock effect (CNA-custom `SkinnedVertexColor3D.hlsl` still could have its own version, but is confirmed
    correct per finding #34).

## Last completed file

`cna-devices` shard — all 39 files (camera, file dialogs, message boxes, system tray, locale, power, system
info, URL launching, display info, clipboard) fully audited and written up, marked AUDITED. Found a new
HIGH-severity, cross-cutting concurrency defect confirmed in 2 files (`FileDialog.cpp`/`MessageBox.cpp`'s
shared use-after-free-window in their swappable-global-backend mutex pattern), and a significant architectural
finding (2 fully independent, redundant NOXNA-extension implementations of Clipboard and Power/PowerState
across `CNA::Input` and `CNA::Devices`). Otherwise excellent code quality with the best test coverage of any
CNA-core shard so far.

## Next exact action

1. **Commit this update** (`AUDIT_CROSS_CUTTING_FINDINGS.md`, `AUDIT_FINDINGS_INDEX.md`, `AUDIT_PROGRESS.md`,
   the 39 new `.audit.md` reports under `audit/include/CNA/Devices/` and `audit/src/CNA/Devices/`, and the
   updated `cna-devices` manifest shard file) as one logical batch, verifying staged paths are `audit/`-only
   first.
2. Continue Task #3 with its final shard: `cna-internal-core` (113 files, the largest of the 5 — priority:
   verify the `SensorTypeEXT`/`JoystickCapabilitiesEXT::powerState` ordinal-mismatch flag against
   `SdlInputBridge`/`SystemSensorBackend`'s own mapping code, likely located in this shard).
3. Then Task #4 (Microsoft.Xna areas — start with
   `xna-framework-core` 78, then `xna-graphics` 191 the largest, prioritizing `SpriteFont.cpp`/`SpriteBatch.cpp`
   given the UB finding above, and `BlendState`/`SamplerState`/`RasterizerState`/`DepthStencilState`/
   `StencilState` given the newly-confirmed `IGraphicsBackend` interface gaps and the D3D12 Stencil/Scissor
   finding), Task #5 (Microsoft.Devices), then tests/tools/examples/docs/build (Tasks #6-9), matching each
   production-code shard with its paired test shard where possible so findings reinforce each other. Also then
   begin Pass 3 (systematic FNA parity sweep) and Pass 4 (populate `AUDIT_GRAPHICS_BACKEND_MATRIX.md`), both of
   which now have an enormous amount of already-gathered cross-backend evidence to consolidate rather than
   re-derive from scratch.

## Graphics backend progress

| Backend | Shard(s) | Status |
|---|---|---|
| Ascii | backend-ascii | **AUDITED** |
| Bgfx | backend-bgfx | **AUDITED** (34/34; resolved the final skinned-normal-transform confirmation — all 14 applicable backends now confirmed; confirmed dead-code vertex-format helper; most complete Stencil+Scissor+DepthBias support found) |
| Canvas | backend-canvas | **AUDITED** |
| D3D11 | backend-d3d11 | **AUDITED** (20/20 own files + shared D3DCommon shaders it compiles, 46/46; no RegisterForWindow; confirmed correct SpriteBatch transform/render-target-relative behavior) |
| D3D12 | backend-d3d12 | **AUDITED** (26/26 own files + shared D3DCommon shaders it compiles, 46/46; no RegisterForWindow; HIGH finding: Stencil/Scissor non-functional; correct SpriteBatch transform/render-target-relative behavior) |
| D3D9 | backend-d3d9 | **AUDITED** (50/50; the LAST backend shard — object-space-only-fog mechanism precisely explained, 4th confirmation of the Ambient-pre-folded-into-Emissive convention, 3rd instance of the NoOverwrite offset gap, most complete native Stencil/Scissor/DepthBias of any backend, structurally immune to both D3D12's occlusion-query bug and Vulkan's RT-scissor bug) |
| Dx3 | backend-dx3 | **AUDITED** (static-only, D-P4) |
| EasyGL | backend-easygl | **AUDITED** |
| Headless | backend-headless | **AUDITED** |
| SdlGpu | backend-sdlgpu | **AUDITED** (27/27; confirmed HIGH: no fog at all; formalized constructor resource-leak; positive: functional Stencil+Scissor, correct SkinnedEffect Ambient+EmissiveColor forwarding) |
| SdlRenderer | backend-sdlrenderer | **AUDITED** |
| Software | backend-software | **AUDITED** |
| Vulkan | backend-vulkan | **AUDITED** (40/40; missing-Y-flip bug expanded from 1 to 4 effect families — PbrEffect/SkinnedPbrEffect/InstancedEffect join the already-known EnvironmentMapEffect instance, incl. a demonstrably false justifying comment; new HIGH finding: Scissor non-functional when a render target is bound; confirmed SetTransformMatrix no-op and SkinnedEffect Ambient/Emissive root-cause locus at the source level) |
| WebGPU | backend-webgpu | **AUDITED** |
| D3DCommon (shared) | backend-d3dcommon | **AUDITED** (46/46: all 34 `.hlsl` shaders + 9 C++ files + 3 tooling files) |
| Common (shared) | backend-common | **AUDITED** |

`AUDIT_GRAPHICS_BACKEND_MATRIX.md`: still skeleton only — do not populate until all 16 backend shards are
directly audited (Pass 4 depends on this).

## FNA parity progress

**COMPLETE (2026-07-19).** See "Current phase" above for the full account -- ~2700+ real XNA 4.0
members individually checked across every namespace with runtime-relevant surface, 7 genuine
API-surface gaps found, CNA's declared surface confirmed overwhelmingly complete. This is in
addition to (not instead of) the many behavioral FNA-parity defects found incidentally throughout
Pass 2 (fog formula, skinned-normal-transform, EnvironmentMapEffect emissive bug, etc.) -- Pass 3's
job was specifically the surface-completeness angle, which those incidental finds didn't cover.

## Cross-cutting investigations open

**All of the below are now RESOLVED** (this section is kept for historical trail; every item was
closed out somewhere in Pass 2-6, see `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full account of
each):

- `known_bugs.md`'s SpriteBatch Begin/End defect — resolved during the `xna-graphics` shard audit
  (`SpriteBatch.cpp` fully reviewed; the SdlRenderer exception-safety issue and the confirmed
  `SpriteFont`/`SpriteBatch` default-character UB + `SpriteEffects` OOB-read bugs are all now
  precisely characterized, distinct findings, not conflated).
- External sibling-repo boundary (`easy-gl`, `free-direct`, D-6) — tracked throughout; every
  finding that bottoms out at "this lives in a different repository" is recorded as a scope
  boundary in the relevant shard's own report, not silently dropped. Dx3's Pass 6 build additionally
  confirmed `free-direct` is itself an SDL3-based reimplementation, not real DirectDraw.
- Full CTest-registration sweep — completed in Pass 6: confirmed this project has never adopted
  `WILL_FAIL` anywhere, for any backend (6+ concrete instances catalogued), and Pass 6's build+test
  sweep of all 14 backends surfaced several more currently-failing, unflagged tests beyond the
  original 3.
- `BasicEffect::VertexColorEnabled`'s bare-public-field issue — confirmed exactly 3 times across
  Bgfx/Vulkan/generic test audits exercising the same production code; no further instances found
  anywhere else in the codebase.
- `IGraphicsBackend.hpp`'s own audit confirmed and expanded the 3 `Apply*State()` missing-field
  gaps (`AddressW`, color-write-mask, `MultiSampleAntiAlias`) — all 3 are real, correctly-implemented
  properties at the XNA-facing class level, with the gap 100% confined to the `IGraphicsBackend`
  interface signatures never carrying them through, affecting every backend uniformly (documented in
  `AUDIT_GRAPHICS_BACKEND_MATRIX.md`'s XNA-facing-features table).
- Vulkan's `SetTransformMatrix()` no-op confirmed the only instance of its kind in that backend;
  its render-target viewport-sizing behavior remains an explicitly disclosed limitation, distinct
  from WebGPU's own confirmed render-target-relative-viewport defect (both recorded side-by-side in
  `AUDIT_GRAPHICS_BACKEND_MATRIX.md`'s cross-cutting defect matrix).

## Commit batches so far (chronological)

1. `audit: add initial repository inventory, scope, and manifest` — Pass 1 infrastructure.
2. `audit: review Common backend contract, Headless and Software backends`
3. `audit: review EasyGL example test shard (218 files, via mechanical batch)`
4. `audit: review SdlRenderer backend`
5. `audit: review Dx3 (DirectDraw) backend`
6. `audit: review EasyGL backend (largest single file, 4733 lines)`
7. `audit: review SdlRenderer example test shard (67 files, via mechanical batch)`
8. `audit: review WebGPU backend (largest file in the audit, 8805 lines)`
9. `audit: review Ascii backend (6 files)`
10. `audit: review Bgfx example test shard (98 files, via mechanical batch)`
11. `audit: review Vulkan example test shard (70 files, via mechanical batch)`
12. `audit: review Canvas backend (8 files)`
13. `audit: resolve RegisterForWindow cross-cutting check, refresh progress`
14. `audit: review WebGPU and D3D9 example test shards (36 files, via mechanical batches)`
15. `audit: review SdlGpu and generic example test shards (46 files) + D3DCommon skinned-shader findings`
16. `audit: review D3DCommon backend (46 files, shared D3D11/D3D12 shader + layout infrastructure)`
17. `audit: review D3D11 backend (20 files, own non-shader implementation)`
18. `audit: review D3D12 backend (26 files, own non-shader implementation)`
19. `audit: review SdlGpu backend (27 files)`
20. `audit: review Bgfx backend (34 files, resolves skinned-normal-transform bug across all 14 backends)`
21. `audit: review Vulkan backend (40 files, largest single file in the audit; missing-Y-flip bug expanded to 4
    effect families; new HIGH scissor-when-RT-bound finding)`
22. `audit: review D3D9 backend (50 files, the LAST backend shard — completes all 16)`
23. `audit: populate Pass 4 cross-cutting defect matrix in AUDIT_GRAPHICS_BACKEND_MATRIX.md`
24. `audit: review cna-graphics shard (7 files, Task #3 started)`
25. `audit: review cna-root-utilities shard (15 files) — Logger SDL-priority bug, unimplemented Runtime class`
26. `audit: review cna-input shard (31 files) — high quality, verified SDL enum parity, no confirmed defects`
27. `audit: review cna-devices shard (39 files) — use-after-free-window bug, duplicate NOXNA API surfaces`
28. *(next commit: cna-internal-core shard, the last Task #3 shard)*

## Self-check log

- 2026-07-18 (session start): `2297 + 337 == 2634` verified via script (see `AUDIT_SCOPE.md`). Zero
  `NEEDS_REVIEW` after two classifier-fix rounds (D-1, D-2). Zero leftover/uncategorized shards after sharding
  script run (`gen_master.py` printed `leftover shards: []`).
- 2026-07-18 (mid-session): every mechanical-batch result verified against disk (no trusting the notification
  summary alone — each batch's file list was checked with a Python existence loop before marking AUDITED) for all
  4 completed batches (easygl, sdlrenderer, bgfx, vulkan). Zero missing files in any batch.
