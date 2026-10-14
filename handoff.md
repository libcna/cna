# handoff.md — current state of CNA

This file describes the repository as it is now and is kept true. Until 2026-10-11 it was `NEXT.md`
and also carried the dated session log of 2026-07 to 2026-10; that log is in Git history
(`git show 8b5564f1e:NEXT.md`).

**§5 is the one authoritative list of current known bugs and limitations.** No other file keeps a
live bug list: `misc/known_bugs.md` and `misc/known_gaps.md` hold only the measurements behind some
§5 entries, the plans under `plans/` are history, and capability documents (renderer docs,
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
| Windows | MinGW-w64 cross builds run under Wine: `DIRECTX9` through DXVK, `SDL_GPU`'s Direct3D 12 backend through vkd3d-proton, `SOFTWARE`; the manual MSVC workflow builds and runs the `DIRECTX9` suite. No real-Windows hardware validation is current (§5.2). |
| Web (Emscripten) | `WEBGL2` and `WEBGPU` builds run in Chrome (`docs/web-emscripten-graphics-limitations.md`). |
| Android | `OPENGLES3`, the default there, on an x86_64 emulator; `SDL_GPU` on Android is not qualified (`docs/android-graphics-limitations.md`). |
| macOS / iOS | macOS CI builds CNA with `SOFTWARE` and runs the portable platform suites and a bundle smoke app; iOS is a `SOFTWARE` simulator smoke app. `SDL_GPU` is the default on both but is not re-qualified there (§5.2, `docs/apple-platforms.md`). |

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

**Tests.** GoogleTest under CTest; `ctest -N` in a build tree gives its count. Full Debug runs on
2026-10-14 through the private runner on RADV. After the renderer retirement: `cmake-build-webgpu`
10,854 tests, 7 failed; `cmake-build-headless` 8,633, 3 failed; `cmake-build-software` 10,843, 4
failed; the Release multi-renderer tree (`build-probe`) 11,316, 5 failed. After the FNA3D fixes
(`plans/plan_fna3d.md` FNA3D-51, -54 to -57): `cmake-build-multi` (`OPENGLES3` default with
`OPENGL33`, `WEBGPU`, `SDL_GPU`, `FNA3D`, `SOFTWARE`, `HEADLESS`, `STUB`) 11,319, 5 failed, 197
skipped; `cmake-build-sdlgpu` (with the C API and compiled effects) 11,028, 22 failed;
`cmake-build-fna3d` 10,665 on FNA3D's SDL_GPU driver, 0 failed, and on its OpenGL driver (the
tree's own lane; 23 failures before), 1 failed. Every failure is a §5.1 entry (items 5, 6, 7 and
16), except SOFTWARE's three C API inventory gates, which measured another session's uncommitted
header change and pass on the committed tree, and two cases that failed once and pass alone: in
the SDL_GPU run `CnaInputTests`, and in the FNA3D OpenGL run a SystemLink discovery case, whose
UDP port is machine-wide. CI: `general-tests-ci.yml` runs the unfiltered suite for
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

The single authoritative list. Every entry below was checked against the code on 2026-10-10.

### 5.1 Defects

1. **`DIRECTX9` `BasicEffect` draws 12 of its 32 shader variants.** The other 20 (Position-only,
   untextured lighting, lit with vertex colour) throw a named `std::runtime_error` from
   `D3D9EffectDraw.cpp`. Every register table exists; only the dispatch is missing.
2. **Test defects, not renderer defects.** With `SDL_GPU` as the default renderer, two
   `SdlGpuIndexedDrawRangeTest` buffer-rewrite cases fail, and with `WEBGPU` two cases of the same
   class: they rewrite a buffer that is still bound, which the device correctly refuses
   (`RRC-026`, `a18a5d374`).
3. **The XACT natural-completion tests are timing-dependent.** `CueTests.cpp`, `SoundBankTests.cpp`
   and `WaveBankTests.cpp` wait with fixed 50 ms sleeps; under parallel load a rotating subset fails
   (`misc/known_bugs.md` §5). They pass on an idle machine.
4. **`SendDataOptions` is not a flags enum, so `Chat` cannot be combined** (checked 2026-10-11;
   found by `GS-AUDIT-016`). XNA declares it `[Flags]` and documents `Chat` combined with
   `Reliable` and/or `InOrder`, with ordered chat packets ordered only among themselves
   (`xna4-spec/Microsoft.Xna.Framework.Net/SendDataOptions.xml`). CNA's
   `SendDataOptions.hpp` is an `enum class` without bitwise operators, and its comment wrongly says
   the values are not powers of two (`Reliable` 1, `InOrder` 2, `Chat` 4; `ReliableInOrder` is
   1|2). `NetPacketCodec::SendDataOptionsToEnetFlags` sends `Chat` exactly like `Reliable`, and a
   combined value 5-7 falls through to the same default.
5. **Shared-test failure with `WEBGPU` as the default renderer** (full `cmake-build-webgpu` runs on
   RADV through the private runner, 2026-10-13 and 2026-10-14; it fails identically on
   `9edbf314e`; the run's other failures are the conformance programs of item 7): `WEBGPU`
   counts only the draws after a `Clear` inside one occlusion run
   (`OcclusionQueryLifecycleTest.ACountSpansAClearBetweenItsDraws`: 192000 of 384000), although
   `AM4-108` recorded it passing on 2026-10-08.
6. **Shared-test failures with `SDL_GPU` as the default renderer** (full `cmake-build-sdlgpu` runs,
   2026-10-13 and 2026-10-14; each also fails or passes identically on `9edbf314e`; the other
   failures of those runs are items 7 and 16):
   - A game on a non-video platform asks `SDL_GPU` for its device and gets "no installed SDL_gpu
     driver accepts SPIR-V": `GameEventSemanticsGoldenTest` `/Headless` and `/Terminal`, and
     `GameWindowPlatformTest.DelegatesStateAndGeometryToTheSelectedPlatformWindow` inside
     `CnaPlatformWindowTests`.
   - `DynamicSoundEffectInstanceTest.BufferNeededFiresExactlyTheStarvedCount` fails only under the
     full parallel run (3 buffers requested instead of 2); alone it passed 40 of 40 on both commits.

7. **XNA behaviour first proved on the retired renderers that a surviving renderer does not meet.**
    The `*_Conformance_*` programs (`modules/graphics/examples/conformance/`) carry the XNA knowledge
    of the retired `VULKAN`, `DIRECTX11`, `METAL` and `SDL_RENDERER` suites; measured 2026-10-13 on
    Linux through the private runner with `OPENGLES3` (EasyGL), `SDL_GPU` (Vulkan) and `WEBGPU`, and
    since RRC-045 (2026-10-14) also registered and measured on `SOFTWARE`, `FNA3D` (its OpenGL
    driver) and `HEADLESS` (whose pixel-oracle programs skip: it makes no pixel claim):
    - `SkinnedEffect.VertexColorEnabled` folds the vertex colour after the specular term and after
      the saturate on all three (`*_Conformance_skinned_effect_vertex_color` F/G: 44 instead of
      218, 128 instead of 230); XNA folds it in first (`VULKAN-200`/`205`). FNA3D ignores the
      flag outright (B equals A) and saturates before the fold (G: 255 instead of 230); SOFTWARE
      passes.
    - A textured `BasicEffect` over a declaration without `TextureCoordinate0` is accepted by
      EasyGL, SDL_GPU, WebGPU, SOFTWARE and FNA3D (measured 2026-10-14; FNA3D was earlier recorded as
      refusing); XNA throws `InvalidOperationException`
      (`*_Conformance_basic_effect_declaration_inputs` C).
    - `GraphicsAdapter`'s format queries answer a different multisample count (0) from what `Reset`
      and `RenderTarget2D` apply (EasyGL 8, SDL_GPU 8, WebGPU 4, SOFTWARE 4, FNA3D 8, HEADLESS 1024 for
      a render target), WebGPU's and HEADLESS's `Reset` apply back-buffer format 9 where the query
      says `Color`, and `GraphicsDevice::Reset` does not clamp `MultiSampleCount` through the query
      (`*_Conformance_graphics_adapter_query`). FNA3D also does not apply `GraphicsDeviceManager`'s
      default `Depth24` request in that program. HEADLESS reports `MultiSampleAntiAliasing` while
      applying 0 samples (`Headless_Conformance_capability_truth`). `*_Conformance_multisample_count_clamp`
      (RRC-047, FNA's `ClosestMSAAPower` rule for requests 0-16) passes on EasyGL, SDL_GPU, WebGPU
      and SOFTWARE; FNA3D's back buffer keeps a 3, 5 or 7 request unrounded while its render
      targets round it down, and HEADLESS's render targets echo every request unclamped while its
      back buffer applies 0.
    - EasyGL: a `RenderTarget2D` requested and applied at 2x resolves with no partially covered
      edge texel (`render_target_own_msaa` D). EasyGL, SOFTWARE and FNA3D: a `SkinnedEffect` over a
      declaration without normal, weights or bone indices draws nothing and reports success
      (`stock_effect_unsupplied_input` C).
    - SDL_GPU: cube faces in a multi-render-target set throw `std::runtime_error` while
      `MultipleRenderTargets` is true (`cube_mrt_binding` M1-M4 and the three `RenderTargetSemantics`
      plural cube-face cases; DIRECTX9 has the same refusal, and HEADLESS refuses the three
      `RenderTargetSemantics` cases with "cannot bind a cube face alongside other render targets",
      `cmake-build-headless` 2026-10-14); a
      `ShaderEffect` uniform array beyond its declared capacity is truncated silently
      (`shader_effect_refusal` E); the portable 3D `ShaderEffect` probe draws nothing on the
      one-stream routes (`shader_effect_3d_layout` A-C, G) and two vertex streams are refused (D/E);
      the first-instance leg J, which runs since `SDL_GPU` reports `BaseInstanceDrawing` (RRC-043),
      fails for G's reason -- the `ShaderEffect` route binds no per-instance stream.
    - SDL_GPU, compiled XNA effects (`SdlGpuCompiledEffectDrawTest`, the shared helpers in
      `tests/support/CNA/TestSupport/CompiledEffectConformance.hpp`, 2026-10-14): a quad covering
      pixels 1-2 under Direct3D 9's pixel-centre convention lands half a pixel up and left
      (`SharedHalfPixelCoverageContract`); a texture set on the device slot after `Apply()` is not the
      one the draw samples (`SharedDeviceTextureSlotAfterApplyContract`); and a pixel shader writing
      `COLOR2` into a one-target pass draws with a Vulkan validation warning (output location 2 has
      no attachment), which fails `SdlGpu_CompiledEffectRuntime`. WebGPU's compiled effects
      (`CNA_WEBGPU_COMPILED_EFFECTS`, off by default) fail the same two contracts on the same
      MojoShader SPIR-V route (measured 2026-10-14).
    - WebGPU: a `ShaderEffect` disposed after `End()` and before the flush drops its draw
      (`deferred_effect_query_lifetime` E2-E4; `webgpu_effect_outlived_by_draw_test` Check B asserts
      the drop, contrary to XNA); two vertex streams into a `ShaderEffect` are refused, and a missing
      input is refused only at replay, after which the queued draw throws again from a destructor and
      terminates the process (`shader_effect_3d_layout` D/E, F/H, run last for that reason).
    - SDL_GPU and WebGPU: an unlit `BasicEffect` over the 36-byte Position+Normal+Colour+TexCoord
      record does not saturate vertex colour × `DiffuseColor` at the vertex, as Direct3D 9's `oD0`
      does (`*_Conformance_basiceffect_unlit_vertex_color_36byte` B: 255 instead of XNA's 178; EasyGL
      passes). The program was registered only by the retired `VULKAN` suite until 2026-10-13.
    - SDL_GPU and WebGPU, in three `CnaTests` cases first pinned on the retired `METAL` (now gated to
      the renderers that pass them): a `Vector4` `RenderTargetCube` face's typed `GetData` throws
      "did not return the complete declared-format cube region"
      (`HdrRenderTargetRoundTripTest.AFloatCubeTargetTransfersItsTypedTexels`); `Short2` positions
      are not read as their integer values (SDL_GPU draws the wrong pixel, WebGPU aborts;
      `IntegerVertexElementTest`); and `SkinnedEffect` refuses `Short4` blend indices, which XNA
      accepts (`SkinnedEffectBoneIndexFormatTest`). Measured 2026-10-13 by selecting each renderer
      at run time in `cmake-build-multi`.
    - SDL_GPU and WebGPU ignore `SpecularTextureCoordinateSetEXT` (81 instead of 66, measured by
      selecting them at run time against `pbr_texture_slot_mapping_test.cpp` in the EasyGL-default
      multi-renderer tree; on SDL_GPU registered as `SdlGpu_CrossFamily_PbrTextureSlotMapping` since
      RRC-046).
    - SDL_GPU, in programs that had run on EasyGL alone (and on the retired `DIRECTX11`) until
      RRC-046 gave them SDL_GPU as a second family (`SdlGpu_CrossFamily_*`, measured 2026-10-14;
      145 of 148 pass): the back buffer's clear colour is gone after switching to two render
      targets and back (`RenderTarget_Roundtrip`: black, where FNA, EasyGL and `DIRECTX11` keep it;
      XNA leaves a `DiscardContents` back buffer undefined, FNA preserves it); and a positive
      `DepthBias` (+3e-4) does not push a primitive behind a coplanar-ish one
      (`DepthBiasMagnitudeContract`; the two negative sizes pass).

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
12. **Test-coverage defect: `DeclarationGuardTest`'s refusal legs never run on a renderer that also
    translates declarations.** `TranslatesDeclarations()` (`VertexDeclarationLayoutTests.cpp`) is a
    per-renderer boolean, true for `OpenGLES3`, `OpenGL33`, `WebGL2`, `WebGPU`, `Software` and
    `SdlGpu`, and each refusal leg opens with `if (TranslatesDeclarations()) GTEST_SKIP()`. Those
    renderers translate what they can and still refuse the rest by name, so none of them asserts
    that a refused draw rasterizes nothing, leaves the device usable, and is refused identically by
    every upload route. Fix: probe candidate declarations for one the renderer really refuses and
    run the legs against it. (`VULKAN-203`, finding F-38.)

13. **Compiled XNA effects on the MojoShader SPIR-V route (`SDL_GPU`, `WEBGPU`) get two Shader
    Model 1-3 cases wrong** (found 2026-10-08 by `AM4-144` on the retired `METAL` route, fixed only
    in that renderer's adapter, not re-measured since): `vPos` is not the integer pixel Direct3D 9
    defines (a half-pixel offset), and `TEXBEM`/`TEXBEML` read zero bump matrices because neither
    adapter fills the two float4s per sampler MojoShader appends to the uniform block. Shared with
    every SPIR-V backend; the oracles are EasyGL's Shader Model 1-3 compiled-effect sections.
14. **The MinGW-w64 cross-build cannot compile `CnaTests.exe`** (found 2026-10-13 configuring
    `cmake-build-mingw`; none of the files is renderer code). GamerServices and content-pipeline
    tests call POSIX `setenv`/`unsetenv`/`getpid` and include `sys/wait.h`, and
    `tools/net/service_session_client_harness.cpp` includes `poll.h`; MinGW has none of them.
    `scripts/check_no_posix_setenv.py` fails on the same files. The rest of the tree (the
    `DIRECTX9` and `SDL_GPU` suites, the C API smokes, the examples) builds.

15. **`DIRECTX9` under Wine+DXVK fails part of its own and the shared suite** (first run of a
    `DIRECTX9`-default MinGW tree, `cmake-build-mingw`, 2026-10-13; not diagnosed, and some may be
    DXVK's). The `DIRECTX9` suite passes 43 of 57: eleven example programs (`DirectX9_Smoke`,
    `_CompiledEffects`, `_Draw`, `_DrawEx`, `_Instanced`, `_DrawOffset`, `_Pbr`,
    `_SkinnedVertexColor`, `_SpriteBatch`, `_EffectRenderer`, `_SpriteBatch_CustomEffect`) read the
    back buffer on a Reach device and abort (a test defect: they never request HiDef), and three
    shared contracts fail on pixels -- `TextureFilterMipContract` L3/L9 (negative `MaxMipLevel`),
    `GraphicsDevice_OrderedClear` V3/V4 (a depth or stencil `Clear` under a scissor is scissored;
    XNA clears the whole attachment) and `Backbuffer_PassOrder` C5/C6/D1/D2/O1-O4 (depth across
    back-buffer cycles). `CnaGraphicsTests.exe`: 2,612 pass, 288 skip, 36 fail (among them the
    cube-face MRT refusal of item 7 and occlusion-query lifecycle cases); `CnaContentTests.exe`:
    1,703 pass, 29 skip, 1 fail (`RendererStrideConformance`, an unlit stride-32 record). Under the
    same Wine, `CApi_MediaPlayerSmoke` fails with `SOFTWARE`, and a pre-campaign build of it hangs.
16. **A C API game still alive at process exit crashes `SDL_GPU` in a Debug build** (first C API run
    with `SDL_GPU`, `cmake-build-sdlgpu`, 2026-10-14): the five `CApi_TeardownLifetime_*` cases that
    leave a game alive segfault. The C API's static `RuntimeState` destroys the game during
    `exit()`, `SdlGpuRenderer`'s destructor calls `SDL_ReleaseWindowFromGPUDevice`, and SDL's
    `VULKAN_Wait` calls into `libVkLayer_khronos_validation.so`, which has already been finalised.
    The validation layer is loaded because Debug builds create the device in debug mode: with
    `SDL_GPU` selected at run time the same case segfaults in the Debug multi-renderer tree and all
    seven teardown cases pass in the Release one (`build-probe`, 2026-10-14). Every other C API case
    passes on `SDL_GPU`, including `CApi_GraphicsExtSmoke` with the extension layer.
### 5.2 Limitations

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
