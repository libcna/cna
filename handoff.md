# handoff.md — current state of CNA

This file describes the repository as it is now and is kept true. Until 2026-10-11 it was `NEXT.md`
and also carried the dated session log of 2026-07 to 2026-10; that log is in Git history
(`git show 8b5564f1e:NEXT.md`).

**§5 is the one authoritative list of current known bugs and limitations.** No other file keeps a
live bug list: `misc/known_gaps.md` holds only the measurements behind some §5 limitations, the
plans under `plans/` are history, and capability documents (renderer docs,
`docs/gltf-limitations.md`, `docs/gamer-services-known-limitations.md`, `CHECKLIST.md`'s accepted
deviations) describe what a subsystem does by design, not what is broken. When a bug is fixed,
delete its §5 entry (and its measurement record, if it has one) in the same commit.

Last reconciled against the code and a full test run: **2026-10-14** (`next`).

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
behavioural fidelity is held by each module's tests. One member is deliberately left unimplemented
(§6).

**Bindings.** C and C# are the maintained bindings. CNA.NET (`libcna/cna-dotnet` `develop`) admits
exactly C ABI 0.47.0 and pins one CNA `next` commit (`CNA_UPSTREAM_COMMIT` in its
`.github/workflows/build.yml`); its CI builds and loads the native library from that commit. CNA
`next` is ABI 0.48.0 since the 2026-10-13 renderer retirement (four renderer constants removed, no
route changed); CNA.NET names none of them and must admit 0.48.0 before it moves its pin. CNA
`main`/`develop` are the v0.1.0 release (ABI 0.46.0), which CNA.NET refuses. The archived bindings in
`cna-lab` stay pinned to older ABIs and are not maintained.

**Content.** XNB readers (including LZX), CNA's `.cnb`/`.cnj`, direct glTF 2.0 loading, the
`cna-content` build tool and the XNA Content Pipeline façade: `docs/content-pipeline.md`,
`docs/xna-content-pipeline-parity-report.md`, `docs/gltf-limitations.md`. No further formats are
planned.

**Tests.** GoogleTest under CTest; `ctest -N` in a build tree gives its count. Latest full Debug
runs, 2026-10-14 after the renderer-retirement close-out (RRC-040..RRC-053, which added the shared
conformance and cross-family registrations), through the private runner on RADV:
`cmake-build-multi` (`OPENGLES3` default with `OPENGL33`, `WEBGPU`, `SDL_GPU`, `FNA3D`, `SOFTWARE`,
`HEADLESS`, `STUB`; C API) 11,340 tests, 6 failed, 210 skipped; `cmake-build-sdlgpu` 11,189, 27
failed; `cmake-build-software` 10,888, 8 failed; `cmake-build-headless` 8,680, 6 failed;
`cmake-build-fna3d` (its OpenGL driver) 10,707, 5 failed; `cmake-build-webgpu` 10,872, 8 failed.
Every failure is a §5.1 entry (items 5, 6, 7 and 16), except cases that failed once and pass alone:
a GamerServices keyring case in the multi run, `XnaDifferentialBuildTest` (a Wine-hosted XNA build,
which §7 says to run outside the private runtime) in the SDL_GPU run, and in the SOFTWARE run four
ENet/SystemLink discovery cases and `CnaInputTests`, which ran while another ENet test held the
machine-wide discovery port. CI: `general-tests-ci.yml` runs the unfiltered suite for
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

The single authoritative list. Every entry below was checked against the code between 2026-10-10
and 2026-10-14; an entry measured or re-measured after 2026-10-10 says when (the renderer
retirement and its close-out, RRC-030..RRC-052, added and re-measured several on 2026-10-13/14).

### 5.1 Defects

1. **`DIRECTX9` `BasicEffect` draws 12 of its 32 shader variants.** The other 20 (Position-only,
   untextured lighting, lit with vertex colour) throw a named `std::runtime_error` from
   `D3D9EffectDraw.cpp`. Every register table exists; only the dispatch is missing.
7. **XNA behaviour first proved on the retired renderers that a surviving renderer does not meet.**
    The `*_Conformance_*` programs (`modules/graphics/examples/conformance/`) carry the XNA knowledge
    of the retired `VULKAN`, `DIRECTX11`, `METAL` and `SDL_RENDERER` suites, measured on Linux through
    the private runner with `OPENGLES3` (EasyGL), `SDL_GPU` (Vulkan), `WEBGPU`, `SOFTWARE`, `FNA3D`
    (its OpenGL driver) and `HEADLESS` (whose pixel-oracle programs skip). Closed on 2026-10-15,
    verified on SOFTWARE, SDL_GPU, WEBGPU and EasyGL: a stock effect over a declaration missing an
    element it reads throws XNA's `InvalidOperationException` from `GraphicsDevice` on every renderer
    (`basic_effect_declaration_inputs` C, `stock_effect_unsupplied_input` C, which also refuses the
    undeclared skinned draw that drew nothing); `SkinnedEffect.VertexColorEnabled` folds the vertex
    colour into the diffuse before specular and before the saturate on EasyGL, SDL_GPU and WebGPU
    (`skinned_effect_vertex_color` F/G); the adapter's multisample answer is the count `Reset` and
    `RenderTarget2D` apply (SOFTWARE 4, WebGPU 4, SDL_GPU and EasyGL the largest of 2/4/8 not above
    the request, as their devices apply on this hardware; `graphics_adapter_query`,
    `multisample_count_clamp`); WebGPU's `Reset` applies the Color back buffer the query selected;
    an SDL_GPU `ShaderEffect` uniform array beyond its capacity is refused by name
    (`shader_effect_refusal` E). Still open:
    - EasyGL: a `RenderTarget2D` requested and applied at 2x resolves with no partially covered edge
      texel (`render_target_own_msaa` D); 4x resolves as expected, so this is the GL driver's 2x
      sample pattern meeting the probe's edge, not measured further.
    - FNA3D: `SkinnedEffect.VertexColorEnabled` is ignored outright (B equals A) and saturates before
      the fold; its back buffer keeps a 3, 5 or 7 sample request unrounded while its render targets
      round it down; `GraphicsDeviceManager`'s default `Depth24` request is not applied in
      `graphics_adapter_query`. FNA3D's driver owns those answers.
    - SDL_GPU and DIRECTX9: cube faces in a multi-render-target set are refused with
      `System::NotSupportedException` while `MultipleRenderTargets` is true (`cube_mrt_binding` M1-M4
      and the plural cube-face `RenderTargetSemantics` cases skip on that refusal); HEADLESS accepts
      them. A capability boundary, reported as such.
    - SDL_GPU `ShaderEffect` 3D route: the portable probe draws nothing on the one-stream routes
      (`shader_effect_3d_layout` A-C, G), two vertex streams are refused (D/E), and the
      first-instance leg J fails for G's reason -- the route binds no per-instance stream.
    - SDL_GPU compiled XNA effects (`SdlGpuCompiledEffectDrawTest`, the shared helpers in
      `tests/support/CNA/TestSupport/CompiledEffectConformance.hpp`): a quad covering pixels 1-2
      under Direct3D 9's pixel-centre convention lands half a pixel up and left
      (`SharedHalfPixelCoverageContract`); a texture set on the device slot after `Apply()` is not
      the one the draw samples (`SharedDeviceTextureSlotAfterApplyContract`); a pixel shader writing
      `COLOR2` into a one-target pass draws with a Vulkan validation warning, which fails
      `SdlGpu_CompiledEffectRuntime`. WebGPU's compiled effects (`CNA_WEBGPU_COMPILED_EFFECTS`, off
      by default) fail the same two contracts on the same MojoShader SPIR-V route.
    - WebGPU: a `ShaderEffect` disposed after `End()` and before the flush drops its draw
      (`deferred_effect_query_lifetime` E2-E4; `webgpu_effect_outlived_by_draw_test` Check B asserts
      the drop, contrary to XNA); two vertex streams into a `ShaderEffect` are refused, and a missing
      input is refused only at replay, after which the queued draw throws again from a destructor
      and terminates the process (`shader_effect_3d_layout` D/E, F/H, run last for that reason).

8. **`EasyGL`'s per-pixel `SkinnedEffect` draws a textured, ambient-only quad white.** With a real
    red `Texture2D` bound and `PreferPerPixelLighting=true` the draw reads (255,255,255,255); the
    per-vertex program draws red, as Software did when measured. It does not depend on the lights,
    the specular colour, the matrices or the draw order (Radeon 780M, `OPENGL33`, 2026-09-21). The
    fault is in the diffuse path, `litRGB * texColor.rgb` in `EnsureSkinnedProgram`: either
    `uEmissiveColor` arrives wrong for this program or it does not see the bound texture.
    `StockEffectNullTextureTest.SkinnedEffectSamplesOpaqueBlack` skips exactly this case
    (`CNA_RENDERER_IS(OpenGL33, OpenGLES3)`); `WEBGL2` shares the program and was not measured.
    (`GSC-F1`, `VKPAR-0011`.)
9. **The stock PBR programs of `SDL_GPU`, `WEBGPU` and `DIRECTX9` have no guard for a zero normal or
   tangent.** They normalise the interpolated normal and the Gram-Schmidt tangent without checking
   its length (`sdl-gpu/src/shaders/pbr3d.frag.glsl`, the two WGSL PBR programs in
   `webgpu_shaders.hpp`, `directx9/src/shaders/cna/Pbr3D.hlsl` and `PbrSkinned3D.hlsl`). A singular
   World or a missing, zero or normal-parallel tangent reaches `normalize(0)`: NaN on drivers that
   compute `x*inversesqrt(0)`, or a surface that loses all direct light on llvmpipe. EasyGL falls
   back to a fixed normal and a perpendicular tangent; `easygl_pbr_degenerate_basis_test` is the
   oracle. (`GSC-0009`, found by living-room-simulator.)
10. **Only EasyGL honours glTF `doubleSided` on `PbrEffect`/`SkinnedPbrEffect`.**
    `GpuDrawParams::pbrDoubleSided` is read only by `EasyGLRenderer.cpp`, which flips N, T and B on
    the back face and accounts for a mirroring World. `SDL_GPU`, `WEBGPU` and `DIRECTX9` ignore it,
    so the back of a leaf is shaded with the normal of the side facing away. Each needs the flag,
    the mirror sign and its own front-facing input checked against its winding convention; the
    oracle is `easygl_pbr_double_sided_test`. (`GSC-0010`, living-room-simulator R-3.)
11. **Test defect: the EasyGL `EnvironmentMapEffect` golden is stale and its tolerance hides it.**
    `examples/golden/easygl_environmentmapeffect_golden_test.png` predates `FX-123`/`FX-124`, which
    moved EasyGL's environment-map program to D3D9's vertex-colour clamp and precision.
    `EasyGL_EnvironmentMapEffect_Golden` (tolerance 20) measured a maximum difference of 20 on
    2026-09-07, one step from failing; the other golden scenes measure 0-1. Confirm today's render
    is the corrected picture, re-capture with `CNA_UPDATE_GOLDEN=1` and lower the tolerance in the
    same commit. (`VULKAN-186`, finding F-33.)
13. **Compiled XNA effects on the MojoShader SPIR-V route (`SDL_GPU`, `WEBGPU`) get two Shader
    Model 1-3 cases wrong** (found 2026-10-08 by `AM4-144` on the retired `METAL` route, fixed only
    in that renderer's adapter, not re-measured since): `vPos` is not the integer pixel Direct3D 9
    defines (a half-pixel offset), and `TEXBEM`/`TEXBEML` read zero bump matrices because neither
    adapter fills the two float4s per sampler MojoShader appends to the uniform block. Shared with
    every SPIR-V backend; the oracles are EasyGL's Shader Model 1-3 compiled-effect sections.
14. **The MinGW-w64 cross-build compiles and runs every test binary** (2026-10-15; before, the
    POSIX `setenv`/`unsetenv`/`getpid` calls in the GamerServices and content-pipeline tests stopped
    `CnaTests.exe` compiling). Those calls go through `System::Environment`, the fork-based child
    tests include their POSIX headers only on POSIX, the net harness polls stdin with `_kbhit` on
    Windows, `libcurl-x64.dll` is copied next to the executables at configure time, and the two
    helper programs the tests copy elsewhere (`cna_fake_effect_compiler`, `cna_argv_echo`) link
    libstdc++ statically. First Windows runs under Wine: `CnaGamerServicesTests.exe` 623 pass,
    `CnaContentPipelineTests.exe` 434 pass, no failures; the tests that need a Python interpreter
    (XMA stub encoders, the large-model generator, `compare.py`) skip where none can be started.
    Two Windows defects came out of it: the compiler's writer registries were `std::map` keyed by
    `std::type_index`, whose libstdc++ ordering mixes pointer and name order for translation-unit
    local types on MinGW and is not a strict weak ordering (a user writer registered in an anonymous
    namespace was "not registered"; now hashed, as is the vertex type registry), and `BuildContent`
    keyed an asset by the project's spelling where the case-folding filesystem said it existed, so
    the build keyed by the name on disk produced nothing (the real spelling is now read from the
    directory on Windows). The remaining Windows-only skips: `/dev/full`, a renderer without 3D,
    aging a directory through `utime`, symbolic links, and the ordering guard for tests that need
    an uninitialized `GamerServicesDispatcher`.

15. **`DIRECTX9` under Wine+DXVK: one teardown flake and one content gtest failure remain**
    (re-measured 2026-10-15). The `DIRECTX9` ctest suite passes 58/58 and `CnaGraphicsTests.exe`
    passes every test (2,660, measured as eight `GTEST_TOTAL_SHARDS` shards because one 2,956-test
    Wine process dies or hangs at a varying point). The flake is `DirectX9_CompiledEffects`, which
    about every other run hangs at the end of its first test inside DXVK 2.6.0's
    `IDirect3DDevice9::Release` (measured 2026-10-15 with teardown markers: every other renderer
    object -- shaders, caches, the depth surface, the MojoShader context -- is already released, the
    final device release is the call that never returns, the window is still alive, and every other
    thread of the hung process is DXVK's, Mesa's disk cache, Wine's or googletest's watcher). A
    standalone D3D9 program with the same object order (240 create/compile/draw/release cycles under
    the same Wine, DXVK and private display) never hangs, so the trigger is this process's timing
    rather than anything CNA asks of the API; a rerun passes. `CnaContentTests.exe`: 1,706 pass, no failures; its two `GltfPerformance` budgets (50 ms a
    parse, 50 ms a load/unload cycle) hold on an idle machine and miss under a concurrent build. Fixed on the way to green, all
    under Wine: static vertex/index buffers live in `D3DPOOL_MANAGED` so the
    resize `Reset` at the first `Present` keeps them and reports the dynamic buffers it empties as
    content-lost (`DirectX9_BufferResetSurvival`, the `Backbuffer_PassOrder` root cause);
    `OcclusionQuery.IsComplete` polls with `D3DGETDATA_FLUSH`; `Clear` saturates a float colour
    before packing it (4.0 wrapped to 252); a depth-less target disables the depth and stencil
    tests while it is bound; `DrawInstancedPrimitives` without an instance stream repeats the draw;
    a stock effect over a declaration missing its inputs throws XNA's `InvalidOperationException`
    (`DualTextureEffect` without `TextureCoordinate1` included, which the glTF stride conformance
    accepts as the named refusal);
    compiled effects take their reflection from MojoShader's bytecode profile (the HLSL re-emitter
    has no `TEXCRD`) and read only the sampler slots the device exposes; the adapter's render-target
    format query agrees with the renderer, which builds Color targets only (float and classic
    numeric targets are its declared boundary, like FNA3D's); `SetContextRecoveryEnabled` is a
    no-op because managed textures need no recovery registry; `MaxMipLevel`, whole-target `Clear`,
    the device texture slot after `Apply()`, HiDef in back-buffer-reading examples and
    `DirectX9_FrameRecovery`'s window were fixed earlier in the same pass.
16. **A C API game still alive at process exit crashes `SDL_GPU` in a Debug build** (first C API run
    with `SDL_GPU`, `cmake-build-sdlgpu`, 2026-10-14): the five `CApi_TeardownLifetime_*` cases that
    leave a game alive segfault. The C API's static `RuntimeState` destroys the game during
    `exit()`, `SdlGpuRenderer`'s destructor calls `SDL_ReleaseWindowFromGPUDevice`, and SDL's
    `VULKAN_Wait` calls into `libVkLayer_khronos_validation.so`, which has already been finalised.
    The validation layer is loaded because Debug builds create the device in debug mode: with
    `SDL_GPU` selected at run time the same case segfaults in the Debug multi-renderer tree and all
    seven teardown cases pass in the Release one (`build-probe`, 2026-10-14). Every other C API case
    passes on `SDL_GPU`, including `CApi_GraphicsExtSmoke` with the extension layer.
19. **`NetworkSession.SimulatedPacketLoss`/`SimulatedLatency` act on the receiving machine** (found
    2026-10-16 while making `SendDataOptions` a flags enumeration). XNA applies both on the sending
    machine, to every recipient including local gamers, and gives unordered packets a normally
    distributed latency (`xna4-spec/Microsoft.Xna.Framework.Net/NetworkSession.xml`, the decompiled
    `NetworkSimulator`). CNA's `ENetBackend::HandleAppData` applies them to AppData arriving for a
    local gamer, so the receiver's setting governs, local-to-local packets are never affected and
    the latency is a constant. Packets sent with `Reliable` are exempt from the loss, as in XNA.
### 5.2 Limitations

- `SDL_GPU` ignores `BlendState.MultiSampleMask`: the pinned SDL (3.4.0 headers) reserves
  `SDL_GPUMultisampleState::sample_mask`/`enable_mask`, so the mask reaches the renderer and is
  deliberately not applied; `ColorWriteChannels` is honoured.

- `BoundingFrustum::Intersects(Ray)` throws `System::NotImplementedException` for the case it does
  not compute — reserved for the human author, see §6.
- Native `SurfaceFormat::Alpha8` render-target storage is unavailable; an Alpha8 request falls back
  to `Color` as XNA's preferred-format rule allows (`misc/known_gaps.md` §1).
- Online sessions and invitations are native-only; a browser build refuses them explicitly
  (`misc/known_gaps.md` §4, `docs/browser-network-readiness.md`).
- `DIRECTX9` is validated under Wine (+DXVK) only, and `SDL_GPU`'s Direct3D 12 backend under Wine
  (+vkd3d-proton) only; real-Windows hardware validation is not current. The C API's pure-C suites
  run under Wine from the MinGW-w64 build; `MediaLibrarySmoke` reports a skip on Windows, whose
  known folders cannot be pointed at its fixture.
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
- `WEBGPU` is experimental; its open items are in the status summary of `plans/plan_webgpu.md`.
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

## 6. Reserved for the human author

- **`BoundingFrustum::Intersects(Ray)`** (`modules/math/src/BoundingFrustum.cpp`). It answers a ray
  whose origin is outside (`nullopt`) or inside (`0`) the frustum, and throws
  `System::NotImplementedException` in the remaining case. The owner reserved the real
  implementation as hands-on C++ work: do not implement it, generate it, or remove the exception.
  `BoundingFrustumTests.cpp` covers only the two answered cases.
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
- System packages for a fresh Debian machine: [`programs.md`](programs.md).
