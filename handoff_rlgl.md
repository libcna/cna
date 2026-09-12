# RLGL Renderer Continuation Handoff

## Purpose

This document gives a future AI agent enough concrete context to continue the CNA RLGL renderer
without repeating the completed investigation or accidentally changing its architectural boundary.
It is a handoff, not the authoritative task ledger. Read `AGENTS.md`, `CHECKLIST.md`, and
`plans/plan_rlgl.md` before changing code; keep `plans/plan_rlgl.md` current as work proceeds.

## Repository and Git state at handoff

- Repository: `/rv/data/development/github.com/libcna/cnarlgl`
- Branch: `rlgl`, tracking `origin/rlgl`
- Workstream starting commit: `1b3151f2f1b7b712cfd26637580bebbb2e3602e4`
- Latest implementation commit: `7525abdb9b72d75e6c58f7fcfc0525285bbdee5b`
  (`feat(RLGL-047): bootstrap compiled effect runtime`)
- Remote: `origin`, repository `libcna/cna`
- Date of this handoff: 2026-09-12

Run `git status -sb` and inspect recent history before starting. Preserve unrelated user changes.
Stage files by explicit name, make one coherent commit per completed RLGL task, and include the task ID
in implementation commit messages. Do not push unless the current user instruction explicitly asks for
it.

## Non-negotiable constraints

1. Use standalone upstream `rlgl`, not the raylib application framework. CNA owns its window, OpenGL
   context, context switching, swap, input, audio, game loop, resource management, and public XNA API.
2. The architecture is CNA XNA API -> CNA graphics contracts -> RLGL renderer -> standalone rlgl ->
   OpenGL 3.3 core -> CNA platform GL context. Never introduce a raylib window or second framework
   stack.
3. EasyGL is the primary parity oracle. The local FNA tree at
   `/rv/data/library/github.com/FNA-XNA/FNA` is the authoritative XNA/FNA behavioral reference.
4. Classic XNA 4.0 parity comes before CNAEXT work. Do not add public CNAEXT API to make RLGL work.
5. Do not report fake success for unsupported behavior. Keep capability flags false until the complete
   public route has pixel or contract evidence.
6. Use low-level rlgl resource and draw wrappers. CNA owns scheduling and explicit state; rlgl's default
   immediate-mode batching is not the renderer architecture.
7. Use at most three compilation jobs. Every build command must use `-j 3` or less, and do not run two
   builds concurrently.
8. Follow the repository's public Doxygen, SPDX, test, task-ledger, and one-task-per-commit rules.

## Dependency and profile decisions

- Public renderer identity: one `RLGL` identity.
- Initial profile: desktop OpenGL 3.3 core.
- rlgl source: standalone `src/rlgl.h` from raylib 6.0 commit
  `dbc56a87da87d973a9c5baa4e7438a9d20121d28`.
- Acquisition: pinned and SHA-256-verified CMake FetchContent archive, with
  `FETCHCONTENT_SOURCE_DIR_RLGL` for offline builds.
- License: raylib/rlgl zlib/libpng license; existing notices and `docs/rlgl-renderer.md` cover it.
- rlgl uses its bundled desktop GL loader, initialized from CNA's platform proc-address callback.
- Optional classic compiled effects use pinned FNA3D revision `3240147` only as the source container
  for MojoShader revision `6333f74` and CNA's existing patches. The RLGL target links the shared
  MojoShader effect translation, not FNA3D itself. Both dependencies use the zlib license.
- `CNA_RLGL_COMPILED_EFFECTS` is intentionally OFF by default until the public compiled-effect draw
  matrix is complete.

Do not create separate renderer identities per rlgl GL profile. A future ES or GL 4.3 profile should be
a measured build configuration under the same identity only after it has real implementation and tests.

## Implemented renderer boundary

The plan contains exact evidence and test counts. At a high level, these areas are implemented and
runtime-validated on Linux with SDL3 offscreen and Mesa llvmpipe:

- renderer registration, pinned standalone-rlgl build, CNA-owned GL context lifecycle, clear, present,
  resize, selective planes, viewport/scissor mapping, and clean sequential device shutdown;
- all 20 classic Texture2D formats, exact transfers/readback, DXT native storage and software fallback;
- plain TextureCube for every payload shape CNA's current cube API can express: Color plus
  DXT1/DXT3/DXT5, including faces, mips, regions, sampling, and readback;
- all sampler filter/address modes, anisotropy, independent sampler objects, and deterministic resets;
- BlendState, DepthStencilState, RasterizerState, color masks, culling, wireframe, scissor, stencil,
  depth bias, and state transitions;
- fixed/dynamic vertex and 16/32-bit index buffers, all XNA primitive topologies, user and bound,
  indexed and non-indexed routes, semantic-driven vertex declarations;
- CNA-owned SpriteBatch and SpriteFont path, including sorting, transforms, rotation/origin/flips,
  blending, clipping, address modes, target orientation, and capacity flushes;
- AlphaTestEffect, complete BasicEffect lighting/fog, DualTextureEffect, EnvironmentMapEffect, and
  SkinnedEffect with their shared EasyGL/XNA pixel oracles;
- all eleven classic FNA render-target formats for RenderTarget2D and RenderTargetCube, exact depth and
  stencil formats, MSAA resolve, mip generation, Preserve/Discard behavior, MRT, target switching,
  orientation, and readback;
- the RLGL-047 optional compiled-effect runtime bootstrap described below.

Do not infer final parity from this list. `GraphicsCapability::ThreeD` and
`GraphicsCapability::CompiledEffects` remain false while their remaining gates are open.

## RLGL-047: latest completed task

Commit `7525abdb9` added the optional classic XNA Effect Framework bootstrap:

- `cmake/RendererSelection.cmake` defines `CNA_RLGL_COMPILED_EFFECTS` and configures the existing pinned
  MojoShader dependency only when opted in.
- `modules/renderers/rlgl/include/CNA/Internal/Renderers/Rlgl/RlglCompiledEffect.hpp` and
  `modules/renderers/rlgl/src/RlglCompiledEffect.cpp` implement `ICompiledEffectRuntime` using CNA's
  shared `MojoShaderEffect::EffectTranslation` and MojoShader's OpenGL adapter.
- One renderer-owned MojoShader GL context uses CNA's proc loader and is destroyed before rlgl and the
  CNA-owned GL context.
- Runtime objects parse authentic `.fxb` bytecode, reflect parameters/techniques/passes, validate and
  copy parameter storage, accept same-device Texture2D/RenderTarget2D and
  TextureCube/RenderTargetCube resources, translate legacy pass and sampler states, retain assigned
  native resources, clone current state, and clean up deterministically.
- Texture3D is rejected because RLGL has no corresponding resource implementation yet.
- MojoShader's current adapter path uses `MOJOSHADER_PROFILE_GLSL120`, matching EasyGL's OpenGL path;
  do not change this by guesswork. Audit generated source and adapter behavior before changing it.
- The new focused executable is
  `cna_test_rlgl_compiled_effect_runtime`, using the committed
  `modules/renderers/fna3d/effects/CnaConformanceEffect.fxb` fixture.
- CI independently checks out the exact FNA3D/MojoShader pin and compiles this opt-in target.
- Parsing is deliberately not exposed as public support: capability advertisement remains false until
  RLGL-048 proves actual public draw output.

## Validation already completed for RLGL-047

The normal option-on focused test passed 16 checks. It covered capability honesty, valid and invalid
bytecode entry, reflection order, technique bounds, parameter bounds, blend/depth/raster translation,
unassigned-state preservation, same-device texture assignment, independent cloning after source
destruction, and clone application.

The following additional validation passed:

- full option-on repository build: 374 incremental steps, exit 0, `-j 3`;
- option-off `cna_renderer_rlgl` build, proving the guarded source is inert by default;
- focused AddressSanitizer build and all 16 runtime checks;
- option-on regressions: `cna_test_rlgl_effect` 15 checks,
  `cna_test_rlgl_environment_map` 4 checks, and `cna_test_rlgl_skinned_effect` 6 checks;
- renderer identity, combination, target-discipline, runtime-discipline, descriptor, CNAEXT Doxygen,
  YAML parsing, and whitespace gates.

Representative normal commands are:

```bash
cmake -S . -B /tmp/cna-rlgl-build3 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_GRAPHICS_RENDERER=RLGL \
  -DCNA_RLGL_COMPILED_EFFECTS=ON \
  -DCNA_BUILD_TESTS=OFF \
  -DFETCHCONTENT_SOURCE_DIR_RLGL=/tmp/cna-raylib-6.0-source \
  -DFETCHCONTENT_SOURCE_DIR_FNA3D=/tmp/cna-fna3d-audit.gFfGPT
cmake --build /tmp/cna-rlgl-build3 --target cna_test_rlgl_compiled_effect_runtime -j 3
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-build3/cna_test_rlgl_compiled_effect_runtime
cmake --build /tmp/cna-rlgl-build3 -j 3
```

The `/tmp` source and build paths are convenient caches, not durable project state. Recreate or replace
them with the pinned sources if they no longer exist.

For AddressSanitizer, the working build used `-fsanitize=address -fno-omit-frame-pointer` and:

```bash
CCACHE_DISABLE=1 cmake --build /tmp/cna-rlgl-asan \
  --target cna_test_rlgl_compiled_effect_runtime -j 3
ASAN_OPTIONS=detect_leaks=0 SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-asan/cna_test_rlgl_compiled_effect_runtime
```

LeakSanitizer is disabled for these Mesa processes because the environment's ptrace restrictions make
it unreliable. AddressSanitizer itself passed. The sandbox also cannot write the configured ccache
directory `/rv/cnaccache`; set `CCACHE_DISABLE=1` when that error appears. The locally available SDL3
build supports the offscreen GL driver but not the expected X11 path. A tests-on configure may also fail
when the vendored GoogleTest source is unavailable, so the renderer's standalone test executables were
built and run with `CNA_BUILD_TESTS=OFF`; CI still registers them when its test dependencies exist.

## Next recommended task: RLGL-048

RLGL-048 is the next unblocked classic-XNA task. Its goal is compiled-effect drawing for ordinary and
indexed user/buffer routes, with honest capability activation only after public pixel evidence.

Start by auditing these exact reference paths rather than designing a new shader system:

- `modules/renderers/easygl/src/EasyGLCompiledEffect.cpp`, especially
  `EasyGLRenderer::BindCompiledEffectForDrawEXT`;
- the compiled-effect branches in `modules/renderers/easygl/src/EasyGLRenderer.cpp`;
- `modules/renderers/easygl/include/CNA/Internal/Renderers/EasyGL/EasyGLCompiledEffect.hpp`;
- `ICompiledEffectRuntime`, `EffectTranslation`, the current RLGL primitive pipeline, RLGL resource
  wrappers, vertex declarations, samplers, render-target orientation code, and every shared compiled
  effect fixture before modifying interfaces.

The task should cover at least:

1. Feed MojoShader's reflected vertex attributes from the existing RLGL VBO/EBO/VAO and vertex
   declaration path. Preserve semantic index, element format, stride, offset, `vertexStart`,
   `baseVertex`, and XNA integer/normalized rules. EasyGL's
   `MOJOSHADER_glSetVertexAttribute` handling is the measured reference.
2. Make the applied MojoShader program current, upload uniforms through
   `MOJOSHADER_glProgramReady`, and supply the correct viewport/depth/clip conventions. Verify matrix
   layout and XNA pixel-center behavior with tests, not shader-specific patches.
3. Bind reflected 2D and cube samplers to retained RLGL resources with existing sampler state. Preserve
   render-target orientation, independent slots, null behavior, and state assignments. Do not pretend
   Texture3D works.
4. Integrate non-indexed and indexed, user and bound-buffer draws through the existing low-level RLGL
   primitive submission. Do not route them through rlgl's immediate batch.
5. Add public `Effect(GraphicsDevice, bytecode)` golden-pixel tests covering parameters, textures,
   state assignments, techniques and passes, plus invalid cases. Compare with unchanged EasyGL/shared
   fixtures where possible.
6. Turn on `GraphicsCapability::CompiledEffects` only when all public routes promised by RLGL-048 pass.
   If capability activation causes `Effect` construction to reach incomplete draw paths, keep it false
   and split the missing evidence into stable tasks.

### Critical state-synchronization warning

MojoShader's GL adapter directly binds programs, uploads uniforms, and changes vertex attribute arrays,
while rlgl maintains process-global cached GL state. Do not assume either cache knows about the other's
changes. Audit every program, VAO, VBO, EBO, active texture, sampler, framebuffer, viewport, and enabled
attribute transition. Explicitly establish the complete compiled-draw state and then the complete next
RLGL draw state. If rlgl has no valid public cache-invalidation hook, use the smallest renderer-private
bridge or deterministic rebind that preserves CNA semantics. Mixing high-level rlgl batching into this
path is unsafe.

Also verify context restoration around MojoShader calls. rlgl itself has one process-global state object,
and the renderer currently permits only one live RLGL device.

## Remaining ledger after RLGL-047

The exact state is in `plans/plan_rlgl.md`. The open or umbrella tasks at handoff are:

- RLGL-010 and RLGL-012: SpriteBatch/effect umbrellas; built-in paths are complete, custom composition
  remains open.
- RLGL-032: classic multi-stream vertex input.
- RLGL-038: custom/compiled effect umbrella.
- RLGL-043: distinct custom MRT shader outputs after custom effects exist.
- RLGL-048: compiled-effect ordinary/indexed draw integration and initial capability gate.
- RLGL-049: compiled-effect SpriteBatch, multi-stream, instancing, vertex samplers, restoration, and the
  remaining conformance matrix.
- RLGL-050: existing CNAEXT source `ShaderEffect`, deliberately after classic compiled effects.
- RLGL-016: instancing and occlusion queries.
- RLGL-017: lifetime, disposal, context lease, device loss/reset, restoration, diagnostics.
- RLGL-018: representative workload ladder.
- RLGL-019: systematic EasyGL parity campaign.
- RLGL-020: measured performance work only after correctness.
- RLGL-021: final CI and supported-platform documentation.
- RLGL-022: final audit and the only task allowed to declare classic parity.

When implementation reveals a real gap, append a concrete stable task rather than hiding it inside an
umbrella. Do not mark a task complete because code compiles; record behavior, validation, tests, and exact
technical limitations in the plan.

## Recent milestone commits

```text
7525abdb9 feat(RLGL-047): bootstrap compiled effect runtime
dce1dfe35 feat(RLGL-036): implement EnvironmentMapEffect
9dcb0a611 docs(RLGL-015): close cube resource umbrella
09a0dd534 feat(RLGL-046): implement RenderTargetCube
4358285de feat(RLGL-045): complete transferable TextureCube formats
e561ebe84 feat(RLGL-044): add Color TextureCube baseline
d0798956e docs(RLGL-014): close RenderTarget2D umbrella
45573d45c feat(RLGL-040): implement multiple render targets
9fc9e2912 feat(RLGL-042): complete render-target formats
a3783b9d1 feat(RLGL-041): implement render-target MSAA
ac14d6b96 feat(RLGL-039): add RenderTarget2D baseline
c34bb5b4a feat(RLGL-037): implement SkinnedEffect
6f25bc410 feat(RLGL-035): implement DualTextureEffect
8a86c6dba feat(RLGL-034): implement BasicEffect lighting
779fbdfa2 feat(RLGL-033): add unlit stock effects
```

Use `git log --oneline --reverse 1b3151f2..HEAD` for the complete development history.

## Completion rule

Do not claim `RLGL <-> EasyGL classic XNA parity reached` until RLGL-022 has rescanned EasyGL and RLGL,
reconciled the capability matrix, exercised representative workloads, searched stubs/TODOs/fallbacks,
and closed every new classic-XNA blocker with evidence. Profile or platform restrictions must remain
explicit even after that gate.
