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
- Latest implementation commit: `f5cc00db06b5de45376b62701d3b67f2860f3f87`
  (`feat(RLGL-016): implement stock effect instancing`)
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
- optional classic compiled-Effect parsing/reflection/state, ordinary draws, 2D/cube sampling,
  render-target correction, and SpriteBatch composition.

Do not infer final parity from this list. `GraphicsCapability::MultiStreamVertexInput` and
`GraphicsCapability::Instancing` are true for the validated stock paths, but the umbrella `ThreeD`
capability remains false while queries, reset/loss, representative workloads, and final audits
remain open. Compiled-effect multi-stream and instanced routes remain explicitly refused by
`RLGL-049`.

## Latest completed slice: RLGL-016

Commit `f5cc00db0` completed classic stock-effect hardware instancing:

- `RlglRenderer::DrawInstancedPrimitivesEx` is a true indexed instanced route, not a CPU loop. The
  existing rlgl wrapper handles zero-offset 16-bit triangle lists; a narrow call through rlgl's
  already-loaded GL 3.3 dispatch preserves every other topology, 32-bit indices, `startIndex`, and
  `baseVertex`.
- The stock GLSL program reserves locations 12-15 for four positional `Vector4` matrix columns. The
  instance transform runs after skinning and before the effect World matrix, matching EasyGL for
  position, normal, lighting, fog, environment mapping, and effect-World composition.
- Each per-vertex and per-instance binding keeps its own native VBO, declaration, stride, element
  offset, public slot, `VertexOffset`, and exact `InstanceFrequency` divisor. Matrix columns may be
  split across several instance streams. No CPU concatenation, upload, or per-instance draw loop was
  introduced.
- The public `firstInstance == 0` classic path is supported. CNAEXT base-instance remains
  unadvertised, while compiled-effect multi-stream/divisor execution remains a named `RLGL-049`
  refusal.
- Attribute shapes, locations, buffers, offsets, and divisors are validated before native state
  mutation. Every stock draw rebuilds the exact VAO state and resets nonzero divisors after drawing.
- `GraphicsCapability::Instancing` is now true. `GraphicsCapability::ThreeD` remains false pending
  the representative workload and final capability audits.
- Existing shared instancing tests and `parity_instanced_draw.cpp` were reused. The renderer-local
  primitive fixture adds only the missing native buffer/offset/divisor snapshot.

Validation on SDL3-offscreen/Mesa-llvmpipe:

- 70/72 tests passed across `InstancedDrawMultiStreamTest.*`, `InstancedDrawRangeTest.*`,
  `InstancedVertexColorTest.*`, `InstancedDiffuseColorTest.*`, and
  `OrdinaryDrawMultiStreamTest.*`; the only two skips are explicitly EasyGL- and D3D-specific;
- the matrix covers every topology, both index widths, start/base offsets, multiple per-vertex and
  per-instance streams, frequencies, dynamic replacement, resource lifetime, stock-effect World
  composition, color/diffuse behavior, and ordinary/instanced state transitions;
- `cna_test_rlgl_instanced_parity` passed 6/6 pixel checks and `cna_test_rlgl_primitive` passed 28/28
  focused gates, including exact native buffers, byte offsets, divisors, and rlgl wrapper selection;
- both focused executables passed rebuilt AddressSanitizer runs with `detect_leaks=0`;
- complete compiled-effects-on and option-off graphs built with `-j 3`;
- renderer identity, combination, target/runtime discipline, 46-family descriptor, CNAEXT Doxygen,
  and whitespace gates passed.

## Build and test caches

The `/tmp` paths are convenient caches, not project state. Recreate them from the pinned sources if
they disappear.

`/tmp/cna-rlgl-build3` is a Debug, examples-on, tests-off, compiled-effects-on build. Its offline pins
currently point to `/tmp/cna-raylib-6.0-source` and `/tmp/cna-fna3d-audit.gFfGPT`:

```bash
CCACHE_DIR=/tmp/cna-ccache cmake --build /tmp/cna-rlgl-build3 -j 3
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-build3/cna_test_rlgl_compiled_effect_runtime
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-build3/cna_test_rlgl_compiled_effect_draw
```

`/tmp/cna-rlgl-tests` has `CNA_BUILD_TESTS=ON` and compiled effects enabled:

```bash
CCACHE_DIR=/tmp/cna-ccache cmake --build /tmp/cna-rlgl-tests \
  --target CnaTests CnaRendererTests cna_test_rlgl_multi_stream \
  cna_test_rlgl_instanced_parity cna_test_rlgl_primitive -j 3
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-tests/CnaTests \
  --gtest_filter='InstancedDrawMultiStreamTest.*:InstancedDrawRangeTest.*:InstancedVertexColorTest.*:InstancedDiffuseColorTest.*:OrdinaryDrawMultiStreamTest.*' \
  --gtest_color=no --gtest_brief=1
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  ctest --test-dir /tmp/cna-rlgl-tests -R 'Rlgl_(Primitive|InstancedParity)$' --output-on-failure
```

`/tmp/cna-rlgl-ci-check-008` is the option-off build used to prove guarded source compatibility.
`/tmp/cna-rlgl-asan` has `-fsanitize=address -fno-omit-frame-pointer`, examples enabled, tests off,
and compiled effects enabled:

```bash
CCACHE_DIR=/tmp/cna-ccache cmake --build /tmp/cna-rlgl-asan \
  --target cna_test_rlgl_primitive cna_test_rlgl_instanced_parity -j 3
ASAN_OPTIONS=detect_leaks=0 SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-asan/cna_test_rlgl_primitive
ASAN_OPTIONS=detect_leaks=0 SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-asan/cna_test_rlgl_instanced_parity
```

LeakSanitizer is disabled for Mesa processes because this environment's ptrace restrictions make it
unreliable. AddressSanitizer itself passes. The sandbox cannot write CNA's configured ccache directory
under `/rv`; use `CCACHE_DIR=/tmp/cna-ccache` (or disable ccache) when needed. The locally available
SDL3 supports the offscreen GL driver but not the expected X11 route.

## Next recommended task: RLGL-052 occlusion queries

The next unblocked classic-XNA resource gap is `RLGL-052`. Standalone rlgl 6.0 has no public query
object wrapper, but desktop GL 3.3 core provides exact `GL_SAMPLES_PASSED` query objects through the
same loader already used for narrowly measured rlgl gaps. Keep the bridge renderer-private and do not
add or alter public XNA/CNAEXT surface.

Start with these sources:

- `IOcclusionQueryRenderer` and the default `CreateOcclusionQuery()` in
  `modules/graphics/include/CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp`;
- the thin XNA owner in `modules/graphics/src/Xna/OcclusionQuery.cpp`; it already delegates
  `Begin`, `End`, `IsComplete`, `PixelCount`, and existing CNAEXT precision reporting;
- EasyGL's `EasyGLOcclusionQueryRenderer` and `CreateOcclusionQuery()` in
  `modules/renderers/easygl/src/EasyGLRenderer.cpp`, especially desktop
  `GL_SAMPLES_PASSED`, nonblocking availability, result-before-ready behavior, and context-loss
  handling;
- the already-loaded private GL declarations and lifecycle boundaries in
  `modules/renderers/rlgl/src/RlglBridge.*`, plus resource ownership patterns in
  `modules/renderers/rlgl/src/RlglResources.*` and `RlglRenderer`;
- `modules/graphics/tests/Microsoft/Xna/Framework/Graphics/OcclusionQueryPixelCountPrecisionTests.cpp`
  and `GraphicsDeviceCapabilityTests.cpp`;
- the existing shared workloads `modules/graphics/examples/occlusion_query_test.cpp` and EasyGL's
  visible/occluded-quad query examples. Reuse them as RLGL targets instead of cloning their logic.

Required behavior for RLGL-052:

1. Own a real GL query name whose creation/destruction occurs with the CNA context current. Return a
   real renderer object from `CreateOcclusionQuery()` and only then advertise
   `GraphicsCapability::OcclusionQuery`.
2. Use `GL_SAMPLES_PASSED` on the selected desktop GL 3.3 profile so `PixelCount()` is a precise
   tally and `PixelCountIsPreciseEXT()` remains true. Do not replace the count with
   `GL_ANY_SAMPLES_PASSED` when exact desktop support is present.
3. `IsComplete()` must use result availability without blocking. `PixelCount()` must return zero
   until complete, then the completed count, with overflow behavior measured against the existing
   renderer contract rather than guessed.
4. Preserve FNA/CNA's existing public call-sequence behavior. The XNA layer deliberately adds no
   Begin/End guards, and the shared integration test exercises End-before-Begin, double-Begin, and
   double-End. Measure EasyGL/native GL behavior and keep diagnostics truthful; do not invent a new
   public state machine.
5. Validate zero-draw, visible, occluded, repeated, overlapping-resource, early destruction, device
   shutdown, and subsequent normal drawing. Check that query state never desynchronizes rlgl's
   program/VAO/framebuffer/state caches.
6. Add focused native-state instrumentation only if the shared pixel/result tests cannot prove a
   required lifetime or target fact. Run the affected tests normally and under AddressSanitizer,
   rebuild both compiled-effects-on and option-off graphs with `-j 3`, update the plan/docs, and make
   one `RLGL-052` commit.

After RLGL-052, proceed to RLGL-017 lifecycle/reset hardening. RLGL-049 is also unblocked, but its
compiled-effect multi-stream/instancing and vertex-stage sampler work should not delay classic query
and device-lifecycle parity.

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
- RLGL-052: next classic task, real desktop GL occlusion-query resource and exact sample count;
- RLGL-049: compiled multi-stream/instancing, vertex samplers, and remaining conformance after its
  global prerequisites;
- RLGL-050: CNAEXT source `ShaderEffect`, deliberately unable to delay classic parity;
- RLGL-043: distinct custom MRT outputs after a custom Effect path exists;
- RLGL-017: disposal, context lease, loss/reset, restoration, and diagnostics;
- RLGL-018: representative XNA workload ladder;
- RLGL-019: systematic EasyGL parity campaign;
- RLGL-020: measured performance work after correctness;
- RLGL-021: CI runtime/platform validation and final documentation;
- RLGL-022: final audit and the only task allowed to declare classic XNA parity.

Append a stable concrete task when implementation reveals real new work. A task is complete only when
its public behavior and validation evidence are recorded, not when the code merely compiles.

## Recent milestone commits

```text
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
