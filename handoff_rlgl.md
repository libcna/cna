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
- Latest implementation commit: `87cc6f317bc3a4c244658fd194fc412551991efc`
  (`feat(RLGL-051): execute compiled SpriteBatch effects`)
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
  and ordinary user/bound indexed/non-indexed single-stream routes;
- CNA-owned SpriteBatch and SpriteFont scheduling, sorting, transforms, rotation/origin/flips,
  sampler/blend/scissor/viewport behavior, render-target orientation, and capacity flushing;
- AlphaTestEffect, BasicEffect including lighting/fog, DualTextureEffect, EnvironmentMapEffect, and
  SkinnedEffect through shared EasyGL/XNA pixel oracles;
- all eleven classic render-target formats for RenderTarget2D and RenderTargetCube, depth/stencil,
  MSAA resolve, mipmaps, Preserve/Discard, MRT, face switching, orientation, and readback;
- optional classic compiled-Effect parsing/reflection/state, ordinary draws, 2D/cube sampling,
  render-target correction, and SpriteBatch composition.

Do not infer final parity from this list. `GraphicsCapability::ThreeD` remains false while global
multi-stream, instancing, queries, reset/loss, representative workloads, and final audits remain open.

## Latest completed slice: RLGL-051

Commit `87cc6f317` completed classic compiled-Effect SpriteBatch:

- `modules/renderers/rlgl/CMakeLists.txt` embeds only the existing
  `modules/renderers/fna3d/effects/SpriteEffect.fxb` when compiled effects are enabled. It reuses
  `embed_effects.py`; it does not link or initialize the FNA3D renderer.
- `RlglSpriteBatchRenderer` owns an option-guarded compiled stock SpriteEffect runtime and locates its
  `MatrixTransform` parameter. Applying this pass first gives pixel-only custom passes XNA's stock
  sprite vertex transformation.
- Compiled batches upload to retained renderer-owned VBO/IBO resources with Position0 Vector2,
  TextureCoordinate0 Vector2, Color0 Vector4, and a 32-byte stride.
- One texture run is submitted once per current Effect pass, preserving FNA/EasyGL pass-major order.
- The drawn sprite always overrides custom-effect sampler slot zero. Other unassigned pixel sampler
  slots fall back to `GraphicsDevice.Textures`; assigned effect textures still win there.
- The complete SpriteBatch sampler state is applied to slot zero.
- A rendered source is not CPU-flipped on this route because the existing compiled binder already
  makes a same-format row-corrected copy. This avoids a double flip.
- The dedicated compiled VAO/program/buffer/texture/depth-range state is restored after success and
  after a named sampler-dimension exception. CPU batch data is cleared on either exit.
- CNAEXT source `ShaderEffect` is still rejected by the stable RLGL-050 diagnostic.

Validation on SDL3-offscreen/Mesa-llvmpipe:

- 20/20 `RlglCompiledEffectTest.*` tests passed, including all four shared SpriteBatch contracts:
  basic, multipass, slot-zero precedence, and multi-hop render-target source;
- renderer-specific tests passed pixel-only stock vertex inheritance, device texture slot fallback,
  stock draws after compiled success, and stock draws after a dimension-mismatch exception;
- `cna_test_rlgl_compiled_effect_draw` passed 12/12 checks and now executes the compiled SpriteBatch
  route; `cna_test_rlgl_spritebatch` remained 28/28;
- both focused executables passed rebuilt AddressSanitizer runs with `detect_leaks=0`;
- complete option-on graph and option-off renderer builds passed with `-j 3`;
- renderer identity, combination, target/runtime discipline, 46-family descriptor, CNAEXT Doxygen,
  workflow YAML, and whitespace gates passed.

## Build and test caches

The `/tmp` paths are convenient caches, not project state. Recreate them from the pinned sources if
they disappear.

`/tmp/cna-rlgl-build3` is a Debug, examples-on, tests-off, compiled-effects-on build. Its offline pins
currently point to `/tmp/cna-raylib-6.0-source` and `/tmp/cna-fna3d-audit.gFfGPT`:

```bash
CCACHE_DISABLE=1 cmake --build /tmp/cna-rlgl-build3 -j 3
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-build3/cna_test_rlgl_compiled_effect_runtime
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-build3/cna_test_rlgl_compiled_effect_draw
```

`/tmp/cna-rlgl-tests` has `CNA_BUILD_TESTS=ON` and compiled effects enabled:

```bash
CCACHE_DISABLE=1 cmake --build /tmp/cna-rlgl-tests --target CnaRendererTests -j 3
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-tests/CnaRendererTests \
  --gtest_filter='RlglCompiledEffectTest.*' --gtest_color=no --gtest_brief=1
```

`/tmp/cna-rlgl-ci-check-008` is the option-off build used to prove guarded source compatibility.
`/tmp/cna-rlgl-asan` has `-fsanitize=address -fno-omit-frame-pointer`, examples enabled, tests off,
and compiled effects enabled:

```bash
CCACHE_DISABLE=1 cmake --build /tmp/cna-rlgl-asan \
  --target cna_test_rlgl_spritebatch cna_test_rlgl_compiled_effect_draw -j 3
ASAN_OPTIONS=detect_leaks=0 SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-asan/cna_test_rlgl_compiled_effect_draw
```

LeakSanitizer is disabled for Mesa processes because this environment's ptrace restrictions make it
unreliable. AddressSanitizer itself passes. The sandbox cannot write CNA's configured ccache directory
under `/rv`; set `CCACHE_DISABLE=1` when needed. The locally available SDL3 supports the offscreen GL
driver but not the expected X11 route.

## Next recommended task: RLGL-032

RLGL-032 is the next unblocked classic-XNA task. Implement global ordinary multi-stream vertex input
before instancing or the remaining compiled-effect matrix. Do not start RLGL-049 by bypassing its
prerequisites and do not turn on `MultiStreamVertexInput` merely to enter a compiled-effect test.

Start with these sources:

- `modules/graphics/include/CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp`, especially
  `GpuVertexStreamBinding`, `MapCombinedOffsetToStream`, `PerVertexStreamCount`,
  `HasMultipleVertexStreams`, and `VertexStreamByteOffset`;
- `modules/graphics/src/Xna/GraphicsDevice.cpp`, which populates the complete binding array and rejects
  same-rate multi-stream draws while the capability remains false;
- `modules/renderers/rlgl/src/RlglPrimitiveRenderer.cpp`, where `RequireBaselineEffect` currently
  rejects `params.vertexStreamCount > 1` and `BuildStockAttributes` assumes one resource/stride;
- `modules/renderers/rlgl/src/RlglBufferRenderer.cpp` and `RlglBridge.*` for native VBO/VAO ownership,
  attribute pointer setup, integer attributes, and state restoration;
- EasyGL's measured ordinary implementation around `ConfigureMultiStreamAttributes`,
  `FirstLocationForStream`, and `RestoreSingleStreamAttributes` in
  `modules/renderers/easygl/src/EasyGLRenderer.cpp`;
- `modules/graphics/tests/Microsoft/Xna/Framework/Graphics/OrdinaryDrawMultiStreamTests.cpp` and
  `modules/graphics/examples/parity/parity_multi_stream_split.cpp`.

Required behavior for RLGL-032:

1. Bind every active per-vertex `GpuVertexStreamBinding` using that stream's own buffer, declaration,
   stride, `combinedByteBase`, and `vertexOffset`. Never concatenate CPU data and never reuse stream
   zero's stride or offset for another stream.
2. Resolve each stock shader semantic from the combined declaration to its owning stream. The common
   helper maps a combined byte offset to `{streamIndex, byteOffsetInStream}`; keep a single-stream draw
   byte-identical to the existing path.
3. Apply `firstVertex`/`baseVertex` to every per-vertex stream in that stream's element units. Preserve
   `startIndex`, 16/32-bit indices, and every primitive topology.
4. Validate each resource and range before changing native bindings. Reject empty/missing declarations,
   per-instance streams, excess slots, and missing semantics by name rather than reading stream zero.
5. Restore or deterministically overwrite every enabled attribute, VBO, EBO, VAO, divisor, and program
   state so following single-stream, SpriteBatch, stock-effect, and compiled-effect draws remain clean.
6. Advertise `GraphicsCapability::MultiStreamVertexInput` and its renderer feature/limit only after all
   ordinary multi-stream tests and a focused pixel executable pass. Enabling it activates a broad shared
   suite; run that suite before committing.

Keep compiled-effect multi-stream support in RLGL-049. RLGL-032 should prove the global resource and
ordinary stock path first. Once RLGL-032 is complete, the instancing half of RLGL-016 is the next
prerequisite: implement per-instance streams, attribute divisors/frequencies, indexed base offsets, and
divisor reset before enabling `Instancing`. Occlusion queries may be split into a concrete task if that
keeps commits coherent. RLGL-049 then consumes both facilities for compiled Effects and audits
vertex-stage samplers plus the remaining shared matrix.

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
- RLGL-032: next task, global ordinary multi-stream vertex input;
- RLGL-016: instancing and occlusion queries; split coherently if necessary;
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
