# RLGL Renderer Continuation Handoff

## Purpose

This handoff gives a new AI agent enough measured context to continue CNA's RLGL renderer without
repeating completed work or crossing the renderer boundary. It is not the task ledger. Read
`AGENTS.md`, `CHECKLIST.md`, and `plans/plan_rlgl.md` first; the plan is authoritative and must be
updated with evidence whenever a task changes state.

## Repository and Git state

- Repository: `/rv/data/development/github.com/libcna/cnarlgl`
- Branch: `rlgl`, tracking `origin/rlgl`
- Workstream starting commit: `1b3151f2f1b7b712cfd26637580bebbb2e3602e4`
- Latest implementation commit: `ef802311805b42b32ec29686398072af7f43a151`
  (`feat(RLGL-052): implement occlusion queries`)
- Upstream remote: `origin` (`libcna/cna`)
- Handoff date: 2026-09-12

At the start of a continuation, run `git status -sb`, compare local and remote heads, and inspect the
recent log. Preserve unrelated changes. Stage files by explicit name, create one coherent commit per
completed task, and include its `RLGL-*` identifier in the commit message. Do not push unless the
current user instruction authorizes it.

## Non-negotiable constraints

1. Use standalone upstream `rlgl`, never the raylib application framework. CNA owns windowing, its
   OpenGL context, context switching, swap, input, audio, game loop, resources, and public API.
2. The architecture is CNA XNA API -> CNA renderer contracts -> RLGL backend -> standalone rlgl ->
   OpenGL 3.3 core -> CNA platform GL context. Do not create a raylib window or a second framework.
3. EasyGL is the renderer parity oracle. The local FNA tree at
   `/rv/data/library/github.com/FNA-XNA/FNA` is the authoritative behavioral reference.
4. Classic XNA 4.0 parity comes before CNAEXT. Do not add public API to make RLGL work.
5. Unsupported behavior must fail by name. Keep capability flags false until the full public route
   has direct contract or pixel evidence.
6. CNA owns batching and explicit graphics state. Use rlgl's low-level resource/state/draw wrappers;
   its default immediate batch is not the production renderer architecture.
7. Use at most three compilation jobs. Every build must use `-j 3` or less, and builds must not run
   concurrently.
8. Follow the repository's SPDX, Doxygen, test, living-ledger, and one-task-per-commit rules.

## Dependency and profile decisions

- Public identity: one `RLGL` renderer. Do not create a renderer identity per GL profile.
- Initial profile: desktop OpenGL 3.3 core.
- rlgl: standalone `src/rlgl.h` from raylib 6.0 commit
  `dbc56a87da87d973a9c5baa4e7438a9d20121d28`.
- Acquisition: SHA-256-verified CMake FetchContent archive with
  `FETCHCONTENT_SOURCE_DIR_RLGL` for reproducible offline builds.
- License: raylib/rlgl zlib/libpng; notices and update steps are already recorded.
- rlgl uses its bundled desktop GL loader, initialized through CNA's platform proc-address callback.
- Optional classic compiled Effects reuse FNA3D revision `3240147` as the source container for
  MojoShader revision `6333f74` plus CNA's existing patches. RLGL links the shared MojoShader effect
  layer, not FNA3D itself. Both dependencies are pinned and zlib licensed.
- `CNA_RLGL_COMPILED_EFFECTS` remains OFF by default. When ON, the runtime, ordinary draws, and
  SpriteBatch composition are implemented and `GraphicsCapability::CompiledEffects` is true.

## Current implemented boundary

The following areas are implemented and runtime-validated on Linux through SDL3 offscreen and Mesa
llvmpipe. Exact evidence and counts live in `plans/plan_rlgl.md`.

- renderer registration, dependency integration, CNA-owned GL context lifecycle, clear/present,
  resizing, viewport/scissor, selective planes, default framebuffer, and sequential shutdown;
- all 20 classic Texture2D formats, exact transfer/readback, native DXT and software fallback;
- transferable plain TextureCube formats exposed by the current CNA contract: Color and DXT1/3/5;
- all classic sampler modes across 16 slots, independent GL sampler objects, anisotropy, and reset;
- complete classic blend, depth/stencil, rasterizer, cull, fill, color-mask, and depth-bias state;
- fixed/dynamic vertex buffers and 16/32-bit index buffers, declarations, all primitive topologies,
  ordinary user/bound indexed/non-indexed routes, and stock-effect hardware instancing with up to
  sixteen independently bound per-vertex/per-instance streams;
- CNA-owned SpriteBatch and SpriteFont scheduling, sorting, transforms, rotation/origin/flips,
  sampler/blend/scissor/viewport behavior, render-target orientation, and capacity flushing;
- AlphaTestEffect, BasicEffect including lighting/fog, DualTextureEffect, EnvironmentMapEffect, and
  SkinnedEffect through shared EasyGL/XNA pixel oracles;
- all eleven classic render-target formats for RenderTarget2D and RenderTargetCube, depth/stencil,
  MSAA resolve, mipmaps, Preserve/Discard, MRT, face switching, orientation, and readback;
- exact desktop `GL_SAMPLES_PASSED` occlusion queries with nonblocking availability, precise counts,
  permissive FNA call sequencing, and disposal/device-lifetime coverage;
- optional classic compiled-Effect parsing/reflection/state, ordinary draws, 2D/cube sampling,
  render-target correction, and SpriteBatch composition.

Do not infer final parity from this list. `GraphicsCapability::MultiStreamVertexInput` and
`GraphicsCapability::Instancing` are true for the validated stock paths, but the umbrella `ThreeD`
capability remains false while reset/loss, representative workloads, and final audits remain open.
Compiled-effect multi-stream and instanced routes remain explicitly refused by
`RLGL-049`.

## Latest completed slice: RLGL-052

Commit `ef8023118` completed classic occlusion queries:

- `RlglOcclusionQueryRenderer` owns one real GL query name and is returned by
  `RlglRenderer::CreateOcclusionQuery()`. `GraphicsCapability::OcclusionQuery` is now true.
- rlgl 6.0 has no public query-object API, so the narrow renderer-private bridge uses the GL 3.3
  functions already loaded by standalone rlgl. Its target is exact `GL_SAMPLES_PASSED`, never the
  boolean `GL_ANY_SAMPLES_PASSED`; the inherited `PixelCountIsPreciseEXT()` contract remains true.
- Begin and End drain rlgl's immediate batch before changing query state. Completion uses
  `GL_QUERY_RESULT_AVAILABLE`; `PixelCount()` returns zero before availability and the exact result
  afterward.
- The public XNA layer intentionally has no call-order guards. The bridge therefore preserves the
  permissive FNA/FNA3D behavior without forwarding invalid nested/unmatched GL calls: only one
  sample target may be active, End closes the globally active target, and duplicate/unmatched calls
  remain safe.
- Active query destruction ends the target before deleting its name. GraphicsDevice disposal
  releases the tracked renderer while the CNA context is current; a later C++ destructor performs
  no GL call after context shutdown.
- Existing shared cycle and EasyGL visible/occluded workloads are registered directly as RLGL tests.
  The only new executable is a focused device/query lifetime probe.

Validation on SDL3-offscreen/Mesa-llvmpipe:

- `Rlgl_OcclusionQuery_Cycle`, `Visible`, `Occluded`, and `Lifetime` passed; state and primitive
  regressions passed in the same final CTest run (6/6 total);
- the visible workload proved positive coverage and exact two-draw = 2 x one-draw additivity; the
  empty query and a depth-occluded draw returned zero;
- relevant capability/precision GTests passed 3/3, including a precise result above 4000 samples;
- all four query executables passed AddressSanitizer with `detect_leaks=0`;
- complete compiled-effects-on and option-off graphs built successfully with `-j 3`;
- renderer identity, combination, target/runtime discipline, 46-family descriptor, CNAEXT Doxygen,
  and whitespace gates passed.

## Build and test caches

The `/tmp` paths are convenient caches, not project state. Recreate them from the pinned sources if
they disappear.

`/tmp/cna-rlgl-build3` is a Debug, examples-on, tests-off, compiled-effects-on build. Its offline pins
currently point to `/tmp/cna-raylib-6.0-source` and `/tmp/cna-fna3d-audit.gFfGPT`:

```bash
cmake --build /tmp/cna-rlgl-build3 -j 3
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-build3/cna_test_rlgl_compiled_effect_runtime
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-build3/cna_test_rlgl_compiled_effect_draw
```

`/tmp/cna-rlgl-tests` has `CNA_BUILD_TESTS=ON` and compiled effects enabled. It contains the complete
RLGL-052 test set:

```bash
cmake --build /tmp/cna-rlgl-tests --target CnaTests \
  cna_test_rlgl_occlusion_query cna_test_rlgl_occlusion_query_visible \
  cna_test_rlgl_occlusion_query_occluded cna_test_rlgl_occlusion_query_lifetime -j 3
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-tests/CnaTests \
  --gtest_filter='GraphicsDeviceCapabilityTest.SupportsOcclusionQuery:RendererCapabilityProfileTest.DeviceSnapshotMapsEveryLegacyCapabilityExplicitly:OcclusionQueryPixelCountPrecisionTest.*' \
  --gtest_color=no --gtest_brief=1
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  ctest --test-dir /tmp/cna-rlgl-tests \
  -R '^Rlgl_(State|Primitive|OcclusionQuery_)' --output-on-failure -j 1
```

`/tmp/cna-rlgl-ci-check-008` is the option-off build used to prove guarded source compatibility.
`/tmp/cna-rlgl-asan` has `-fsanitize=address -fno-omit-frame-pointer`, examples enabled, tests off,
and compiled effects enabled:

```bash
cmake --build /tmp/cna-rlgl-asan \
  --target cna_test_rlgl_occlusion_query cna_test_rlgl_occlusion_query_visible \
  cna_test_rlgl_occlusion_query_occluded cna_test_rlgl_occlusion_query_lifetime -j 3
ASAN_OPTIONS=detect_leaks=0 SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-asan/cna_test_rlgl_occlusion_query
ASAN_OPTIONS=detect_leaks=0 SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-asan/cna_test_rlgl_occlusion_query_visible
ASAN_OPTIONS=detect_leaks=0 SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-asan/cna_test_rlgl_occlusion_query_occluded
ASAN_OPTIONS=detect_leaks=0 SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-asan/cna_test_rlgl_occlusion_query_lifetime
```

LeakSanitizer is disabled for Mesa processes because this environment's ptrace restrictions make it
unreliable. AddressSanitizer itself passes. The repository's configured compiler cache is
`/rv/cnaccache`; the user explicitly granted read/write access to it. If a restricted execution tool
does not inherit that access, request the required build escalation rather than silently changing
the cache. The locally available SDL3 supports the offscreen GL driver but not the expected X11
route.

## Next recommended task: RLGL-017 lifecycle and reset hardening

The next classic-parity blocker is `RLGL-017`: disposal robustness, owning-thread/context leases,
diagnostics, achieved capabilities, context loss/reset, and resource restoration. It spans several
subsystems, so first audit the concrete gaps and append stable child tasks to `plans/plan_rlgl.md` if
the evidence supports splitting the work. Do not mark the umbrella complete after only one lifecycle
case passes.

Start with these sources and tests:

- `RlglRenderer` construction/destruction and `PlatformGlContextOwner` in
  `modules/renderers/rlgl/src/RlglRenderer.cpp`; current shutdown makes the context current and
  destroys renderer-owned pipelines, compiled-effect state, MRT FBOs, samplers, then rlgl;
- resource implementations under `modules/renderers/rlgl/src/Rlgl*Renderer.cpp` and the bridge's
  global state in `RlglBridge.cpp`; measure which resources retain enough CPU description/data for
  recreation and which currently assume an immortal context;
- `AcquireThreadContextLeaseEXT`, `SetContextRecoveryEnabled`, `CanBeginDrawEXT`,
  `DebugSimulateContextLoss`, `DebugRestoreContext`, and the graphics-device event sink contracts in
  `modules/graphics/include/CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp`;
- `GraphicsDevice` construction/reset/disposal and resource tracking in
  `modules/graphics/src/Xna/GraphicsDevice.cpp` plus the matching XNA events/tests;
- EasyGL's `GlContextOwner`, context lease, resource registry, renderer teardown, context-loss
  simulation/restoration, and every `release_gl_handle_only`/`recreate_gl_resource` implementation
  in `modules/renderers/easygl/src/EasyGLRenderer.cpp`. Use it as behavioral evidence, not code to
  copy wholesale;
- existing renderer lifecycle/context-loss tests found via
  `rg -n 'AcquireThreadContextLeaseEXT|DebugSimulateContextLoss|DeviceResetting|context loss' modules tests`.

Required outcomes for RLGL-017:

1. Establish an owning-thread/context lease that serializes a complete GL operation, safely moves
   CNA's one RLGL context between threads through the platform service, and restores/releases the
   caller's previous binding exactly as the common contract requests. rlgl's process-global state
   still forbids concurrent RLGL devices.
2. Prove idempotent resource and device disposal for every implemented resource family, including
   an active occlusion query and optional compiled effects. No resource destructor may issue GL
   calls after the context is gone; no native child may outlive its required loader/context.
3. Separate ordinary `GraphicsDevice::Reset()` presentation changes from genuine native context
   loss. Preserve XNA event order and current presentation parameters; do not advertise recovery
   merely because a new context can be created.
4. For real loss/restoration, invalidate old-context names without deleting them through the new
   context, reinitialize rlgl and every renderer-owned cache/pipeline, recreate registered resources
   from measured CPU shadows, then deterministically reapply CNA state. Optional MojoShader objects
   require explicit teardown/recreation ordering around the context.
5. Make unavailable periods and unsupported configurations observable through existing diagnostics
   and `CanBeginDrawEXT`; never fake success or add new CNAEXT surface.
6. Validate normal and failure-injected lifecycles, repeated reset/loss/restore, disposed resources,
   state/resource contents after restoration, another ordinary draw after recovery, and both
   compiled-effects-on/off graphs. Use no more than `-j 3`, run focused ASan coverage, update the
   authoritative plan/docs, and commit each completed ledger task separately.

`RLGL-049` is also unblocked, but its compiled-effect multi-stream/instancing and vertex-stage sampler
work should not displace this classic device-lifecycle blocker. `RLGL-050` is CNAEXT-only and remains
deferred behind classic parity.

## State-synchronization warning

rlgl has one process-global cached state object. MojoShader's GL adapter independently binds programs,
uploads uniforms, and changes vertex arrays. CNA must deterministically establish and restore program,
VAO, VBO, EBO, attributes/divisors, active textures, samplers, framebuffer, viewport, and depth range.
Do not assume either cache observes direct changes made by the other. Prefer rlgl wrappers where exact;
use the smallest renderer-private GL 3.3 bridge only for a measured missing wrapper.

The backend deliberately permits only one live RLGL device because upstream rlgl's global state is not
multi-context safe.

## Remaining ledger

The exact task states and dependency graph are in `plans/plan_rlgl.md`. Important open work is:

- RLGL-010/RLGL-012/RLGL-038: umbrellas whose completed stock/compiled slices are recorded separately;
- RLGL-017: next classic task, disposal, context lease, loss/reset, restoration, and diagnostics;
- RLGL-049: compiled multi-stream/instancing, vertex samplers, and remaining conformance after its
  global prerequisites;
- RLGL-050: CNAEXT source `ShaderEffect`, deliberately unable to delay classic parity;
- RLGL-043: distinct custom MRT outputs after a custom Effect path exists;
- RLGL-018: representative XNA workload ladder;
- RLGL-019: systematic EasyGL parity campaign;
- RLGL-020: measured performance work after correctness;
- RLGL-021: CI runtime/platform validation and final documentation;
- RLGL-022: final audit and the only task allowed to declare classic XNA parity.

Append a stable concrete task when implementation reveals real new work. A task is complete only when
its public behavior and validation evidence are recorded, not when the code merely compiles.

## Recent milestone commits

```text
ef8023118 feat(RLGL-052): implement occlusion queries
f5cc00db0 feat(RLGL-016): implement stock effect instancing
b15c3223b feat(RLGL-032): implement ordinary multi-stream input
87cc6f317 feat(RLGL-051): execute compiled SpriteBatch effects
87a90ee31 feat(RLGL-048): execute compiled effect draws
7525abdb9 feat(RLGL-047): bootstrap compiled effect runtime
dce1dfe35 feat(RLGL-036): implement EnvironmentMapEffect
09a0dd534 feat(RLGL-046): implement RenderTargetCube
45573d45c feat(RLGL-040): implement multiple render targets
c34bb5b4a feat(RLGL-037): implement SkinnedEffect
6f25bc410 feat(RLGL-035): implement DualTextureEffect
8a86c6dba feat(RLGL-034): implement BasicEffect lighting
779fbdfa2 feat(RLGL-033): add unlit stock effects
```

Use `git log --oneline --reverse 1b3151f2..HEAD` for the complete development history.

## Completion rule

Do not claim `RLGL <-> EasyGL classic XNA parity reached` until RLGL-022 has rescanned both renderers,
reconciled the capability matrix, run representative workloads, searched TODO/stub/fallback and error
paths, inspected state/effect/resource/CI/documentation coverage, and converted every discovered classic
gap into a completed or technically justified task.
