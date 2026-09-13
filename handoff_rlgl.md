# RLGL Renderer Handoff

## Purpose

This handoff records the completed RLGL classic-XNA workstream so a later maintainer does not repeat
the parity campaign or mistake explicit platform/CNAEXT limitations for unfinished classic support.
Read `AGENTS.md`, `CHECKLIST.md`, and `plans/plan_rlgl.md`; the plan remains the authoritative task
ledger and capability matrix.

## Repository and Git rules

- Repository: `/rv/data/development/github.com/libcna/cnarlgl`
- Branch: `rlgl`, tracking `origin/rlgl`
- Workstream starting commit: `1b3151f2f1b7b712cfd26637580bebbb2e3602e4`
- Use `git log --oneline --reverse 1b3151f2..HEAD` for the complete workstream history.
- Preserve unrelated changes, stage files by explicit name, and create one commit per ledger task.
- Do not push unless the current user explicitly requests it.
- Use no more than four compilation jobs, and do not run builds concurrently.

## Final status

RLGL-022 completed the final audit and declared:

> RLGL ↔ EasyGL classic XNA parity reached on the validated Linux SDL3-offscreen/Mesa
> configuration.

All classic-XNA rows in `plans/plan_rlgl.md` are validated. The only blocked matrix rows are modern
CNAEXT texture-array/compute/SSBO/image facilities that OpenGL 3.3 cannot provide and that do not
gate classic parity. No open RLGL ledger task remains.

The final audit reconciled EasyGL and RLGL hooks, resources, fixed state, effects, error paths,
common adapter/platform ownership, tests, CI, and documentation. A production TODO/stub/fallback
scan found no hidden classic gap; one unused legacy `Unsupported` helper was removed.

## Non-negotiable architecture

1. Use standalone upstream `rlgl`, never the raylib application framework. CNA owns windowing, its
   OpenGL context, context switching, swap, input, audio, game loop, resources, and public API.
2. The architecture is CNA XNA API → CNA renderer contracts → RLGL backend → standalone rlgl →
   OpenGL 3.3 core → CNA platform GL context.
3. The local FNA tree at `/rv/data/library/github.com/FNA-XNA/FNA` is the behavioral authority;
   EasyGL is the cross-renderer parity reference.
4. CNA owns batching and explicit graphics state. Use rlgl's low-level wrappers; use only a narrow
   renderer-private GL bridge for measured rlgl wrapper gaps.
5. Unsupported behavior must fail observably. Capability flags stay false until the entire public
   route has direct contract or pixel evidence.
6. rlgl has process-global state, so the backend deliberately permits one live RLGL device.

## Pinned dependencies and profile

- Public renderer identity: `RLGL`.
- Profile: desktop OpenGL 3.3 core (`GRAPHICS_API_OPENGL_33`).
- rlgl: standalone `src/rlgl.h` from raylib 6.0 commit
  `dbc56a87da87d973a9c5baa4e7438a9d20121d28`.
- Acquisition: SHA-256-verified FetchContent archive with `FETCHCONTENT_SOURCE_DIR_RLGL` for an
  offline source override.
- Optional classic compiled effects reuse FNA3D revision `3240147` as the MojoShader source
  container, with MojoShader revision `6333f74` and CNA's existing patches.
- `CNA_RLGL_COMPILED_EFFECTS` remains OFF by default. When ON, compiled effects are implemented and
  `GraphicsCapability::CompiledEffects` is true.

## Validated classic boundary

The following paths have runtime evidence on SDL3 offscreen with Mesa llvmpipe 25.0.7:

- CNA-owned context/device lifecycle, thread-context leasing and background loading, deterministic
  child teardown, presentation Reset, conditional robust loss detection, native recreation, and
  transactional recovery of registered resources;
- clear/present, fullscreen platform contract, resize, viewport/scissor, vsync, backbuffer format,
  renderer-owned MSAA, complete fixed-function blend/depth/stencil/rasterizer state, and truthful
  capability/adapter reporting;
- all 20 classic Texture2D formats, Color Texture3D, Color and DXT1/3/5 plain TextureCube transfers,
  exact mip/subresource behavior, native DXT or software decode, samplers, and context recovery;
- all eleven classic RenderTarget2D and RenderTargetCube formats, depth/stencil, MSAA, resolve,
  mipmaps, Preserve/Discard behavior, mixed cube/2D MRT, and exact readback/orientation;
- fixed/dynamic vertex and 16/32-bit index buffers, every classic topology, user/bound draws,
  multi-stream input, hardware instancing, and occlusion queries;
- SpriteBatch/SpriteFont plus BasicEffect, AlphaTestEffect, DualTextureEffect,
  EnvironmentMapEffect, SkinnedEffect, and internal SpriteEffect behavior;
- optional compiled effects with ordinary/multi-stream/instanced draws, SpriteBatch, uniforms,
  fragment/vertex Texture2D/TextureCube/Texture3D samplers, state transitions, orientation, and
  recovery;
- CNAEXT source `ShaderEffect`, including the four-output MRT oracle, as a separately tested
  extension;
- representative clear-to-animated-Model workloads and the systematic EasyGL shared-test/golden
  campaign.

Exact per-feature evidence is in `plans/plan_rlgl.md`; cross-renderer results are in
`docs/rlgl-parity-campaign.md`; user-facing build/profile/debug/performance guidance is in
`docs/rlgl-renderer.md`.

## Final validation snapshot

The compiled-effects-on build at `/tmp/cna-rlgl-tests` and clean option-off build at
`/tmp/cna-rlgl-off-tests` were used for RLGL-022. All compilation used at most four jobs.

- direct RLGL smoke: passed two complete device lifecycles;
- complete CTest label: 141/141 passed;
- compiled-effect GTests: 24/24 passed when run from the repository root, which is required for the
  committed `CnaConformanceEffect.fxb` fixture;
- common GraphicsAdapter/capability GTests: 41 passed, five named non-applicable profile/interface
  probes skipped;
- SDL3 fullscreen/platform-capability tests: 6/6 passed;
- renderer identity, target discipline, runtime dispatch, combinations, descriptors, CNAEXT
  Doxygen, workflow YAML, and whitespace gates: passed;
- descriptor gate: 42 renderer families compiled; BGFX and DirectX 9/11/12 were structure-only
  because this Linux host lacks their SDK headers.

Representative reproduction:

```bash
CCACHE_DISABLE=1 cmake --build /tmp/cna-rlgl-tests \
  --parallel 4 --target cna_test_rlgl_all
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-tests/cna_test_rlgl_smoke
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  ctest --test-dir /tmp/cna-rlgl-tests \
  --output-on-failure --label-regex '^Rlgl$' --parallel 1
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  /tmp/cna-rlgl-tests/CnaRendererTests --gtest_filter='Rlgl*'
```

Run the final `CnaRendererTests` command from the repository root so its committed effect fixture is
found. LeakSanitizer remains disabled for Mesa processes in this environment because local ptrace
restrictions make it unreliable; prior focused AddressSanitizer campaigns passed.

## Explicit limitations and future work

- Windows, macOS, on-screen Linux, and SDL2 RLGL runtime behavior are unvalidated. The SDL2 build
  path compiles, but the focused runtime fixtures are SDL3-only. Do not claim those platform results
  without real execution evidence.
- Emscripten/browser is unsupported by the desktop OpenGL 3.3 profile.
- One process-global rlgl state object means concurrent RLGL devices are intentionally refused.
- Robust asynchronous reset polling exists only when the platform grants robust-access plus
  lose-context-on-reset semantics and exposes a reset-status function; bind/swap failures remain
  detectable on the tested non-robust context.
- CNA's current public Texture3D and TextureCube transfer surfaces cannot express every
  format-native payload. RLGL refuses unsupported labels rather than silently substituting storage.
- Modern CNAEXT texture arrays, compute, SSBO, and image-load/store need a separately authorized
  profile/architecture campaign. They are not classic XNA parity regressions.

If future work finds a real classic regression, add a stable new task after RLGL-062, record direct
observable evidence, and do not weaken the RLGL-022 declaration silently.
