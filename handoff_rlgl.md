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
- Latest implementation commit: `b15c3223b09fef32caa95f05e568904e725498c2`
  (`feat(RLGL-032): implement ordinary multi-stream input`)
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
  and ordinary user/bound indexed/non-indexed routes with up to sixteen per-vertex streams;
- CNA-owned SpriteBatch and SpriteFont scheduling, sorting, transforms, rotation/origin/flips,
  sampler/blend/scissor/viewport behavior, render-target orientation, and capacity flushing;
- AlphaTestEffect, BasicEffect including lighting/fog, DualTextureEffect, EnvironmentMapEffect, and
  SkinnedEffect through shared EasyGL/XNA pixel oracles;
- all eleven classic render-target formats for RenderTarget2D and RenderTargetCube, depth/stencil,
  MSAA resolve, mipmaps, Preserve/Discard, MRT, face switching, orientation, and readback;
- optional classic compiled-Effect parsing/reflection/state, ordinary draws, 2D/cube sampling,
  render-target correction, and SpriteBatch composition.

Do not infer final parity from this list. `GraphicsCapability::MultiStreamVertexInput` is now true,
but the umbrella `ThreeD` capability remains false while instancing, queries, reset/loss,
representative workloads, and final audits remain open.

## Latest completed slice: RLGL-032

Commit `b15c3223b` completed ordinary classic-XNA multi-stream vertex input:

- `RlglPrimitiveRenderer` resolves every stock semantic across CNA's combined declaration, maps the
  combined offset back to its owning `GpuVertexStreamBinding`, and installs that stream's native VBO,
  stride, declaration offset, and residual `VertexBufferBinding.VertexOffset`.
- Draw-level `firstVertex` or `baseVertex` remains common to every per-vertex stream, so independent
  stream offsets are added exactly once while `startIndex`, 16/32-bit indices, and topology remain
  unchanged. No CPU concatenation or interleaving was introduced.
- The route accepts two through sixteen per-vertex streams, including non-contiguous public slots,
  and validates binding shape, declaration/stride agreement, native resource capacity, and required
  effect semantics before submission. Per-instance frequencies still fail under RLGL-016.
- Every primitive submission disables all sixteen attributes and resets their divisors before
  rebuilding the exact attribute/VBO association. Test snapshots capture each location's native
  buffer, stride, and byte offset.
- CNA's existing FNA-compatible semantic-composition behavior is preserved: a completely duplicate
  later stream is dropped, while a partially colliding stream throws before native submission.
- `GraphicsCapability::MultiStreamVertexInput` is now true. `GraphicsCapability::ThreeD` and
  `GraphicsCapability::Instancing` remain false.
- The existing shared split-stream parity sample is now an RLGL executable and CTest target; no
  parallel renderer-specific application was invented.

Validation on SDL3-offscreen/Mesa-llvmpipe:

- 21/21 `OrdinaryDrawMultiStreamTest.*` shared tests passed with no skips, including independent
  offsets, non-indexed and 16/32-bit indexed start/base handling, secondary-range rejection, dynamic
  updates, two/three streams, public slot 15, resource recreation, and repeated single/multi draws;
- `cna_test_rlgl_multi_stream` passed 6/6 pixel checks and `cna_test_rlgl_primitive` passed 27/27
  focused checks, including semantic-collision recovery and exact per-location VBO state;
- 22 related renderer/compiled-effect tests remained green after the capability was enabled;
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
  --target CnaTests CnaRendererTests cna_test_rlgl_multi_stream cna_test_rlgl_primitive -j 3
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-tests/CnaTests \
  --gtest_filter='OrdinaryDrawMultiStreamTest.*' --gtest_color=no --gtest_brief=1
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  ctest --test-dir /tmp/cna-rlgl-tests -R 'Rlgl_(Primitive|MultiStream)$' --output-on-failure
```

`/tmp/cna-rlgl-ci-check-008` is the option-off build used to prove guarded source compatibility.
`/tmp/cna-rlgl-asan` has `-fsanitize=address -fno-omit-frame-pointer`, examples enabled, tests off,
and compiled effects enabled:

```bash
CCACHE_DIR=/tmp/cna-ccache cmake --build /tmp/cna-rlgl-asan \
  --target cna_test_rlgl_primitive cna_test_rlgl_multi_stream -j 3
ASAN_OPTIONS=detect_leaks=0 SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-asan/cna_test_rlgl_primitive
ASAN_OPTIONS=detect_leaks=0 SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-asan/cna_test_rlgl_multi_stream
```

LeakSanitizer is disabled for Mesa processes because this environment's ptrace restrictions make it
unreliable. AddressSanitizer itself passes. The sandbox cannot write CNA's configured ccache directory
under `/rv`; use `CCACHE_DIR=/tmp/cna-ccache` (or disable ccache) when needed. The locally available
SDL3 supports the offscreen GL driver but not the expected X11 route.

## Next recommended task: RLGL-016 instancing slice

The next unblocked classic-XNA work is the instancing half of RLGL-016. Keep the task in progress until
its separate occlusion-query half is also validated, or first split the ledger into two stable concrete
tasks if the audit proves that produces cleaner one-task commits. Do not start RLGL-049's compiled
instancing route before the global stock route works, and do not enable `GraphicsCapability::Instancing`
until the public pixel/range/state suite passes.

Start with these sources:

- `modules/graphics/include/CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp`, especially
  `GpuVertexStreamBinding`, `FirstInstanceStream`, `PerVertexStreamCount`,
  `MapCombinedOffsetToStream`, and the default `DrawInstancedPrimitivesEx` refusal;
- `modules/graphics/src/Xna/GraphicsDevice.cpp` around `DrawInstancedPrimitivesCore` and
  `ValidateInstanceStreamRanges`; shared code already computes exact public ranges and populates
  `instanceCount`, `firstInstance`, `baseVertex`, `startIndex`, offsets, and frequencies;
- `modules/renderers/rlgl/src/RlglPrimitiveRenderer.cpp`; factor the proven RLGL-032 per-vertex mapping
  rather than forking it, but keep ordinary single-stream behavior byte-identical;
- `modules/renderers/rlgl/src/RlglBridge.*`; divisors are already reset at all sixteen locations, while
  the draw bridge needs exact indexed-instanced submission for arbitrary topology, both index widths,
  nonzero index offsets, and base vertex;
- EasyGL's `DrawInstancedPrimitivesEx`, `PlaceInstanceStreams`, declaration configuration, and reverse
  attribute cleanup in `modules/renderers/easygl/src/EasyGLRenderer.cpp`;
- `modules/graphics/tests/Microsoft/Xna/Framework/Graphics/InstancedDrawMultiStreamTests.cpp`, including
  the classic single-instance-stream tests and the mixed multi-stream oracle. Read the file's capability
  and renderer-oracle gates before adding RLGL; enabling the capability activates a broad matrix.

Required behavior for the instancing slice:

1. Implement `RlglRenderer::DrawInstancedPrimitivesEx` as a real indexed instanced route; do not loop
   instances on the CPU and do not silently fall back to an ordinary draw.
2. Reuse the RLGL-032 mapping for every per-vertex stream. Add each per-instance declaration at stable,
   non-colliding attribute locations with its own VBO, stride, declaration offset, `vertexOffset`, and
   exact public `instanceFrequency` divisor.
3. Apply `baseVertex` only to per-vertex streams. Apply `firstInstance` only to per-instance record
   selection if the route supports the existing CNAEXT base-instance call naturally; classic XNA
   `firstInstance == 0` is the parity gate and must remain correct.
4. Preserve `startIndex`, both index widths, every primitive topology, and exact consumption arithmetic:
   `1 + (instanceCount - 1) / instanceFrequency` records from each instance stream.
5. Validate declarations, location count, resources, capacities, offsets, and frequencies before native
   state changes. A missing effect semantic or malformed layout must throw by name, never sample another
   stream or produce a fake success.
6. Deterministically clear all enabled attributes and divisors after success and failure. Run sequences
   that alternate ordinary single-stream, ordinary multi-stream, instanced, SpriteBatch, stock effects,
   and compiled effects to detect leaked VAO/VBO/EBO/program state.
7. Only then advertise `GraphicsCapability::Instancing`, add RLGL to the appropriate shared oracle, add
   one focused executable if existing samples do not expose exact native state, and record all evidence
   in `plans/plan_rlgl.md`.

After global instancing is complete, either finish RLGL-016 with a real occlusion-query resource or use
the already-recorded task split. RLGL-049 can then consume RLGL-032/RLGL-016 for compiled-effect
multi-stream/instancing and audit vertex-stage samplers plus the remaining compiled matrix.

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
- RLGL-016: next task, instancing and occlusion queries; split coherently if the initial audit proves
  two stable tasks are cleaner;
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
