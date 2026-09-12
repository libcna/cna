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
- Latest implementation commit: `87a90ee3189da3df9fe1df1cf995d518dc60b9e4`
  (`feat(RLGL-048): execute compiled effect draws`)
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
- `CNA_RLGL_COMPILED_EFFECTS` remains OFF by default, matching CNA's other optional MojoShader
  backends. When enabled, ordinary compiled-effect draws are implemented and advertised; the exact
  remaining advanced routes are listed under RLGL-049 below.

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
- the RLGL-047/048 optional compiled-effect runtime and ordinary draw path described below.

Do not infer final parity from this list. `GraphicsCapability::ThreeD` remains false while its
remaining gates are open. `GraphicsCapability::CompiledEffects` is true only when
`CNA_RLGL_COMPILED_EFFECTS=ON`; this means the ordinary public user/buffer draw routes proven by
RLGL-048, not the advanced routes still assigned to RLGL-049.

## RLGL-047/048: latest completed tasks

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
- RLGL-048 adds a dedicated rlgl-owned VAO and deterministic MojoShader program/attribute rebinding,
  reflected uniform upload, exact Texture2D/TextureCube and sampler binding, same-format row-corrected
  copies for rendered Texture2D sources, XNA-compatible depth-range mapping, and clean state recovery.
- Every ordinary public route now has pixel evidence: indexed/non-indexed user data and bound buffers,
  typed/raw declarations, 16/32-bit indices, and non-zero vertex/start/base offsets.
- SpriteBatch composition, multi-stream input, instancing, vertex-stage samplers, and the rest of the
  shared compiled-effect matrix remain explicit RLGL-049 work. Texture3D first needs the global RLGL
  resource implementation; it is never substituted with a texture of another dimension.
- Option-on builds now advertise `GraphicsCapability::CompiledEffects`; option-off builds remain inert.

## Validation already completed through RLGL-048

The normal option-on runtime test passed 16 checks. It covered capability truth, valid and invalid
bytecode entry, reflection order, technique bounds, parameter bounds, blend/depth/raster translation,
unassigned-state preservation, same-device texture assignment, independent cloning after source
destruction, and clone application.

The ordinary public draw executable passed 11 pixel gates over all indexed/non-indexed user and bound
routes. The RLGL GTest integration passed 13 tests reusing the shared runtime, draw, orientation,
switching, sampler-pixel, pass-selection, stock-isolation, rendered-source, many-draw, and truncation
contracts, plus renderer-specific cube sampling, named texture-dimension refusal/recovery, and depth
convention/restoration tests.

The following additional validation passed on SDL3 offscreen/Mesa llvmpipe:

- full option-on repository build, exit 0, `-j 3`;
- option-off `cna_renderer_rlgl` build, proving the guarded source is inert by default;
- focused AddressSanitizer builds and all 16 runtime plus 11 draw checks (`detect_leaks=0`);
- unchanged state, sampler, primitive, SpriteBatch, RenderTarget2D, TextureCube, Basic/AlphaTest,
  EnvironmentMapEffect, and SkinnedEffect regressions;
- renderer identity, combination, target-discipline, runtime-discipline, descriptor, CNAEXT Doxygen,
  workflow YAML, and whitespace gates.

Representative normal commands are:

```bash
cmake -S . -B /tmp/cna-rlgl-build3 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_GRAPHICS_RENDERER=RLGL \
  -DCNA_RLGL_COMPILED_EFFECTS=ON \
  -DCNA_BUILD_TESTS=OFF \
  -DFETCHCONTENT_SOURCE_DIR_RLGL=/tmp/cna-raylib-6.0-source \
  -DFETCHCONTENT_SOURCE_DIR_FNA3D=/tmp/cna-fna3d-audit.gFfGPT
cmake --build /tmp/cna-rlgl-build3 --target \
  cna_test_rlgl_compiled_effect_runtime cna_test_rlgl_compiled_effect_draw -j 3
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-build3/cna_test_rlgl_compiled_effect_runtime
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-build3/cna_test_rlgl_compiled_effect_draw
cmake --build /tmp/cna-rlgl-build3 -j 3
```

The tests-on build containing the shared conformance contracts can be resumed with:

```bash
CCACHE_DISABLE=1 cmake --build /tmp/cna-rlgl-tests --target CnaRendererTests -j 3
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-tests/CnaRendererTests \
  --gtest_filter='RlglCompiledEffectTest.*' --gtest_color=no --gtest_brief=1
```

The `/tmp` source and build paths are convenient caches, not durable project state. Recreate or replace
them with the pinned sources if they no longer exist.

For AddressSanitizer, the working build used `-fsanitize=address -fno-omit-frame-pointer` and:

```bash
CCACHE_DISABLE=1 cmake --build /tmp/cna-rlgl-asan \
  --target cna_test_rlgl_compiled_effect_runtime cna_test_rlgl_compiled_effect_draw -j 3
ASAN_OPTIONS=detect_leaks=0 SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-asan/cna_test_rlgl_compiled_effect_runtime
ASAN_OPTIONS=detect_leaks=0 SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-asan/cna_test_rlgl_compiled_effect_draw
```

LeakSanitizer is disabled for these Mesa processes because the environment's ptrace restrictions make
it unreliable. AddressSanitizer itself passed. The sandbox also cannot write the configured ccache
directory `/rv/cnaccache`; set `CCACHE_DISABLE=1` when that error appears. The locally available SDL3
build supports the offscreen GL driver but not the expected X11 path. The tracked
`vendor/googletest` submodule was initialized at its committed revision `7e2c425d`, allowing the
tests-on `/tmp/cna-rlgl-tests` build and the 13-test RLGL compiled-effect GTest suite to run. The
standalone executables remain useful with `CNA_BUILD_TESTS=OFF`, and CI registers them when its test
dependencies exist.

## Next recommended task: RLGL-049

RLGL-049 is the next unblocked classic-XNA task. Its goal is to complete the advanced compiled-effect
draw routes and remaining shared conformance matrix without weakening the ordinary path proven by
RLGL-048.

Start by auditing these exact reference paths rather than designing a new shader system:

- EasyGL's `BindCompiledEffectForDrawEXT`, SpriteBatch compiled-effect branches, stream/divisor setup,
  and context-restoration behavior;
- `RlglCompiledEffect.cpp`, `RlglPrimitiveRenderer.cpp`, and the dedicated resources in
  `RlglBridge.cpp` before changing their state boundary;
- every currently uncalled contract in `tests/support/CNA/TestSupport/CompiledEffectConformance.hpp`.

The task should cover at least:

1. Run and implement `RunCompiledEffectSpriteBatchContract`, its multi-pass and texture-slot variants,
   and its render-target-source contract. The SpriteBatch texture must win slot 0 exactly as it does in
   EasyGL, while the effect owns its other parameters and pass sequence.
2. Implement `RunCompiledEffectMultiStreamDrawContract` using every `GpuVertexStreamBinding`, preserving
   per-stream stride/offset and semantic mapping on the dedicated VAO. Never collapse streams into a
   guessed packed declaration.
3. Implement `RunCompiledEffectInstancingDrawContract`, including divisor setup/reset, instance count,
   base instance where the GL 3.3 profile permits it, and refusal text for a genuine profile limit.
4. Audit vertex-stage sampler reflection/binding under `MOJOSHADER_XNA4_VERTEX_TEXTURES`; reuse the
   renderer's texture/sampler objects and keep pixel-stage slots independent.
5. Prove context and all GL/rlgl state restoration on success and exception paths. The named
   Texture2D/samplerCube refusal test already proves the dedicated draw state can recover; extend this
   to every advanced route.
6. Run every remaining shared compiled-effect contract. Do not broaden `CompiledEffects` claims beyond
   measured behavior, and append a concrete task for any independent Texture3D resource prerequisite.

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

## Remaining ledger after RLGL-048

The exact state is in `plans/plan_rlgl.md`. The open or umbrella tasks at handoff are:

- RLGL-010 and RLGL-012: SpriteBatch/effect umbrellas; built-in paths are complete, custom composition
  remains open.
- RLGL-032: classic multi-stream vertex input.
- RLGL-038: custom/compiled effect umbrella.
- RLGL-043: distinct custom MRT shader outputs after custom effects exist.
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
87a90ee31 feat(RLGL-048): execute compiled effect draws
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
