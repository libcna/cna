# OpenGL4 classic repair and modern CNAEXT Graphics workstream

Task IDs `GL4-0001`…. One row per task, each carrying its own evidence. This file is the evidence
ledger for the branch `opengl4-modern-graphics`; it is not a design document.

The four-digit IDs are deliberately distinct from the historical `GL4-1`…`GL4-33` rows of
`plans/plan_opengl4.md`, which record how the renderer was first built (2026-07-21/22). Where a
row here corrects one of those, it names it.

Two strictly sequential workstreams:

- **A** — repair and complete the classic OpenGL4 renderer until it provides every applicable
  classic capability the EasyGL renderer family provides, preserving measured XNA semantics
  rather than copying EasyGL defects.
- **B** — implement the existing modern CNA/CNAEXT Graphics API on OpenGL4. Starts only after A's
  gate (`GL4-A-GATE` below) is recorded as passed.

Companion documents this ledger does not duplicate:

- `plans/plan_opengl4.md` — the original build-out (`GL4-1`…`GL4-33`).
- `plans/plan_modern.md` — `MOD-2260` (4.1 floor + runtime-discovered 4.3+ subset, done) and
  `MOD-2261`/`MOD-2262`/`MOD-2266` (the OpenGL4 modern rows this workstream closes).
- `plans/plan_sdlgpu.md` (EasyGL parity matrix, SDLGPU-58…132) and
  `plans/plan_sdlgpu_modern_graphics.md`, `plans/plan_vulkan_modern_graphics.md`,
  `plans/plan_webgpu_modern_graphics.md` — the reference campaigns.

---

## Status summary

| | |
|---|---|
| Branch | `opengl4-modern-graphics` |
| Baseline | `b2a0a5671c4db9b7ac1564a7ac2bf9ffd88530e1` (`origin/next` = `next`, 2026-09-24) |
| Workstream A | **COMPLETE** (`GL4-A-GATE`, 2026-09-24) |
| Workstream B | in progress |

---

## GL4-0001 — Baseline commit and branch

```
git fetch origin
git rev-parse HEAD          -> b2a0a5671c4db9b7ac1564a7ac2bf9ffd88530e1   (next)
git rev-parse origin/next   -> b2a0a5671c4db9b7ac1564a7ac2bf9ffd88530e1
```

`next` and `origin/next` are identical; the head is `STREETS-0008` (`b2a0a5671`), eight commits
past the SDL_GPU modern merge `7290dfc58` (the cna-street SDL_GPU bring-up and its follow-ups).
The branch `opengl4-modern-graphics` was created from `origin/next` and its upstream tracking was
removed, so no bare `git push` can reach `next`. Repository-local identity is
`Robert Vokac <robertvokac@robertvokac.com>`.

Pre-existing working-tree state: the untracked `startup-metrics.log` (an unrelated Electron log)
is left alone and never staged.

## GL4-0002 — Environment, measured rather than assumed

Measured inside `tools/platform/run_gpu_tests_private.sh --exec` (private headless Weston with the
GL renderer, plus a private rootful Xwayland with DRI3 on a display number it chose itself):

| | |
|---|---|
| OS / kernel | Debian 13, Linux 6.12.107+deb13-amd64 |
| Physical GPU | **AMD Radeon 780M** (Phoenix, PCI `0x1002:0x15bf`) |
| Driver | **Mesa 25.0.7-2+deb13u1, radeonsi**, LLVM 19.1.7, DRM 3.61 — `Accelerated: yes` |
| GLX (private Xwayland `:2`) | `GL_VENDOR` AMD, `GL_RENDERER` `AMD Radeon 780M (radeonsi, phoenix, LLVM 19.1.7, DRM 3.61, 6.12.107+deb13-amd64)`, core `4.6 (Core Profile) Mesa 25.0.7-2+deb13u1`, GLSL `4.60`, max core 4.6, max compat 4.6, GLES 3.2 |
| EGL (private Weston, Wayland platform) | EGL 1.5 Mesa, client APIs `OpenGL OpenGL_ES`; core profile 4.6 on the same radeonsi device |
| Not authoritative | the agent shell's inherited `DISPLAY=:99` is Xvfb (llvmpipe, no DRI3); `:0`/`wayland-0` are the owner's live desktop and are never used |

Every authoritative run in this ledger records the `GL_RENDERER` its context actually reported,
because a silent llvmpipe run would make "hardware validated" false.

### What "OpenGL4" means in CNA (the version policy, audited rather than chosen)

1. **What the backend requests:** desktop **4.1 core** (`RequestedContext()` in
   `OpenGL4Renderer.cpp`), deliberately — 4.1 is the highest core version macOS's GL exposes.
2. **What CNA promises:** `docs/opengl4-renderer.md` and `plans/plan_modern.md` `MOD-2260`
   ("Preserve OpenGL4's 4.1 XNA floor while discovering a 4.3+ modern subset at runtime"). The
   classic XNA surface must work on any 4.1 core context.
3. **What the modern contract needs:** compute, SSBOs and image load/store need 4.3 core (or their
   individual ARB extensions); indirect draw needs 4.0; timer queries 3.3; texture arrays 3.0.
   `GL4::DiscoverModernCapabilities` already classifies each subset independently from version,
   extension and entry-point facts, and `MOD-2260` proved a forced-4.1 context boots.

So the answer is **not** "OpenGL 4.3 required". The public floor stays 4.1 core; each modern
capability is advertised only when the live context has it and CNA's implementation of it is
tested. Anything newer than a subset's own floor (4.5 DSA, 4.6 SPIR-V, 4.4 buffer storage) may be
used only behind a live check with a correct older path.

## GL4-0003 — Baseline build repair: an SDL-free OpenGL4 configuration could not be generated

**Symptom.** Configuring `CNA_GRAPHICS_RENDERER=OPENGL4` with `CNA_ENABLE_SDL=OFF` failed at the
generate step, once per example:

```
CMake Error at modules/renderers/opengl4/examples/CMakeLists.txt:26 (target_link_libraries):
  Target "cna_test_opengl4_smoke" links to:  SDL3::SDL3  but the target was not found.
```

**Root cause.** `cna_opengl4_test` linked `SDL3::SDL3` unconditionally. OPENGL4 has obtained its
context through `IPlatformGlContext` since the platform migration, so native X11/Wayland without
SDL is a legitimate OPENGL4 configuration — the example macro was the only thing still assuming
SDL. `opengl4_smoke_test.cpp` additionally called `SDL_GetRenderer()` on the window handle.

**Fix (minimal, to obtain an honest baseline).** The macro links SDL only where it is configured
and otherwise defines `CNA_EXAMPLES_NO_SDL`, exactly like `cna_vulkan_test`. The smoke test's
Check A now asks for a non-null native handle, and Check B — formerly "`SDL_GetRenderer` is null" —
asks GL itself for a 4.x context. No renderer code changed.

## GL4-0004 — Nine tests that never reached their assertions (dead on the Reach profile)

`tools/platform/profile_dead_tests.py` named nine tests of the baseline run that died on a
graphics-profile refusal before their first check — five of OpenGL4's own and four shared
renderer-neutral sources:

| test | refusal |
|---|---|
| `OpenGL4_Readback`, `OpenGL4_PreferPerPixelLighting`, `OpenGL4_ShaderEffect3D`, `OpenGL4_ShaderEffectSpriteBatch`, `OpenGL4_InstancedModel` | `GetBackBufferData is not supported by the Reach graphics profile` |
| `…_MsaaChange` (`easygl_msaa_change_test.cpp`) | same |
| `…_RenderTarget_ActiveMsaaReadback` | `SetRenderTargets: 2 render targets exceeds GraphicsProfile.Reach's own maximum of 1` |
| `…_SamplerLodAddressWContract`, `…_ShaderEffect_ReflectionContract` | `Texture3D: GraphicsProfile.Reach does not support volume (3D) textures at all` |

This is the `SOFTWARE-213` class that `GTI-0003` fixed for Vulkan, WebGPU and SDL_GPU; OpenGL4's
own suite was never included in that sweep, and the four shared sources die identically on EasyGL
(measured below, `GL4-0006`), so they are test defects rather than OpenGL4 results. Each test now
declares HiDef as the **program's** profile with a namespace-scope `CNA::ProjectGraphicsProfileEXT`
(a `GraphicsDeviceManager` request made in `Initialize()` arrives after `Game`'s device exists,
`D9-103`). No renderer code changed.

## GL4-0005 — The OpenGL4 classic test corpus: shared fixtures plus EasyGL's own sources

Before this workstream OpenGL4 had 27 registered tests of its own (`OpenGL4_*`), written while the
renderer was built and measuring what it did then. Nothing compared it to EasyGL on the same input.
Two registrations make that comparison direct:

1. **The 32 shared parity fixtures** (`modules/graphics/examples/parity/ParityFixtures.cmake`),
   the renderer-neutral oracles EasyGL, WebGPU and SDL_GPU already run, registered as
   `OpenGL4_Parity_<fixture>`.
2. **EasyGL's own example sources, rebuilt unchanged against OPENGL4** — 346 registrations as
   `OpenGL4_EasyGLParity_<EasyGL test name>`. `tools/opengl4/generate_easygl_parity_corpus.py`
   derives the list (`modules/renderers/opengl4/examples/EasyGLParityCorpus.cmake`) from EasyGL's
   `examples/CMakeLists.txt`: every registration whose source names no SDL API/header and includes
   no EasyGL internals, with EasyGL's own timeout, working directory, extra libraries and include
   directories. Skipped: 4 harness/argument registrations, 5 SDL-using sources, 1 EasyGL-internal.

35 of those sources declare a per-renderer contract table that `#error`s on an unlisted renderer.
Each gained an `#elif defined(CNA_RENDERER_OPENGL4)` branch declaring **EasyGL's (desktop) contract
verbatim** — the target this workstream is held to — so a failure is a parity gap, not a
declaration choice. `shader_effect_reflection_contract_test.cpp` additionally selects its GLSL
source for OpenGL4 as it does for EasyGL.

Because the binaries are built with OPENGL4 as the default and OPENGLES3/OPENGL33 compiled in, the
**same executables** run against EasyGL by runtime selection (`CNA_GRAPHICS_RENDERER=OPENGL33`),
which is what makes the comparison below exact: identical source, identical binary, identical
display, only the renderer differs.

Registered after this task: `ctest -N` **11 062** in `cmake-build-opengl4`, of which
`-R '^OpenGL4_'` **405** (27 original + 32 parity + 346 corpus).

## GL4-0006 — Baseline, measured before any renderer change

Configuration `cmake-build-opengl4`: Debug, Ninja, ccache (`/rv/cnaccache`), `CNA_GRAPHICS_RENDERER=OPENGL4`,
`CNA_GRAPHICS_RENDERERS="OPENGL4;OPENGLES3;OPENGL33"`, `CNA_PLATFORM=WAYLAND`, `CNA_ENABLE_SDL=OFF`,
`CNA_AUDIO_PLATFORM=NULL`, `CNA_CNAEXT=ON`, `CNA_SHARED_LIBRARY=ON`, `CNA_TEST_DISPLAY=` (empty).
Configure clean after `GL4-0003`; full build clean (2 057 steps, 10 min at `-j8`).

Every run: `tools/platform/run_gpu_tests_private.sh` (private headless Weston + private rootful
Xwayland), `GL_RENDERER` = AMD Radeon 780M (radeonsi), context 4.6 core.

### Classic example corpus — `-R '^OpenGL4_'`, 405 tests, same binaries

| renderer (runtime) | pass | fail | notes |
|---|---|---|---|
| **OPENGL4** | **283** | **122** | 9 dead on profile (`GL4-0004`) |
| OPENGL33 (EasyGL desktop) on the 378 shared tests | 355 | 23 | 4 dead on profile |
| OPENGLES3 (EasyGL ES) on the 378 shared tests | 351 | 27 | 4 dead on profile; ES has no native wireframe |

On the 378 tests both renderers run: **95 fail on OpenGL4 only**, 20 fail on both, 3 fail on
EasyGL only (`DxtTextureCube` — a working-directory registration defect in the corpus generator,
fixed; `ViewSpace_Fog` skinned pre-skin fog; `Parity_basic_effect_vertex_color` saturation).

The OpenGL4-only failures cluster as follows (first diagnostic of each test, `LastTest.log`):

| cluster | tests | what fails |
|---|---|---|
| stride-keyed vertex input | ~20 | `this VertexDeclaration cannot be represented` for Position+Normal, Position+Color, reordered/offset declarations; multi-stream refused; `UnknownStride_Rejected`, `DrawRangeValidation` |
| null stock texture | 6 | a null texture samples an uninitialised unit instead of XNA's opaque black (`GSC-0001` rule) |
| render-target orientation | ~14 | a rendered target is sampled/read vertically mirrored (`REMED-GFX-147` never reached OpenGL4) |
| surface formats | ~8 | `SurfaceFormat 13 (Single) / 4 (Dxt1) is not implemented`; float/half/Rgba64 targets absent; cube/volume byte transfers |
| sampler | ~12 | MaxMipLevel, LOD bias, AddressW, Point-mip, 27-state descriptor capacity, pixel-centre point sampling |
| SpriteBatch | ~12 | hard-coded `SrcAlpha/InvSrcAlpha` blend and depth-off, device Viewport ignored, 16-bit index overflow past 16 384 sprites, Immediate mode, sub-pixel |
| render state | ~12 | Opaque blend, depth compare at the XNA clip range, stencil reference/two-sided/write-mask restore, degenerate scissor, `ColorWriteChannels.None`, cull golden, XNA pixel centre |
| stock effect math | ~10 | lit vertex colour not saturated, fog not premultiplied by alpha, EnvironmentMapAmount not saturated, skinned inverse-transpose normal, dual-UV PBR, glTF transforms |
| MSAA / presentation | ~8 | applied sample count, `MultiSampleMask`, Letterbox, Reset to `DepthFormat::None`, swap interval not reported |
| lifetime / threading | 3 | `AcquireThreadContextLeaseEXT` returns null; `MoveSemantics` |

The 20 shared failures are recorded per test in `GL4-0007` and each is investigated before being
classified; a shared failure is not an excuse, and several are XNA-contract defects EasyGL has too.

### gtest suites (bounded runner, 200 tests/shard)

| suite | OPENGL4 pass / fail / skip | EasyGL OPENGL33 |
|---|---|---|
| `CnaRendererTests` (241) | 231 / 0 / 10 — the ten skips are EasyGL-internal tests | aborts in `EasyGLRedundantStateTest` (`terminate called without an active exception`) — a reference-side crash, recorded, not OpenGL4's |
| `CnaGraphicsTests` (2 861) | 2 473 / 12 / 376 | 2 778 / 9 / 74 |
| `CnaGraphicsExtTests` (962, **modern**, Workstream B's initial row) | **798 / 44 / 120** | 922 / 7 / 33 |

The twelve `CnaGraphicsTests` failures on OpenGL4: four DualTextureEffect null-sampler tests
(declaration refusal), EnvironmentMap amount clamp, two render-target format agreements (no float /
classic numeric targets), four cube/volume declared-format transfers, and
`GraphicsDeviceRendererTest.StartupDiagnosticNeverWritesToStdout` — OpenGL4 prints its GL version to
stdout with `std::cout` instead of the renderer log channel.

## GL4-0007 — What the EasyGL reference itself fails, and why OpenGL4 skips 302 more tests

**EasyGL's own failures on the shared gtests are mostly not EasyGL's.** Seven of OPENGL33's nine
`CnaGraphicsTests` failures (`GraphicsRendererSelectionTest.*`, `GraphicsRendererFallbackTest.*`,
`GraphicsDeviceRendererTest.Get*MatchesFreeFunction`) assert that the active renderer is the
build's compile-time default, which a runtime-selected OPENGL33 in an OPENGL4-default build is by
construction not — an artefact of the A/B method, not of either renderer. Its seven
`CnaGraphicsExtTests` failures are six `ModernGpuConformance` cases whose compute package carries no
desktop-GLSL variant (`ShaderPackageEXT: no usable shader variant`) plus
`ClusteredForwardEffectTest.ATransmissiveMaterialWithoutAnOpaqueFrameIsRefused`, which OpenGL4
fails identically. Both belong to Workstream B.

**OpenGL4 skips 302 `CnaGraphicsTests` that EasyGL runs** (376 against 74). They are not
capability-truthful skips of a missing feature so much as coverage OpenGL4 was never admitted to:

| skip reason | tests |
|---|---|
| `needs one of the listed renderers; the active renderer is OPENGL4` (`CNA_SKIP_IF_RENDERER_IS_NONE_OF` lists that name EasyGL's identities and not OPENGL4) | 153 |
| `no rasterizing/readback oracle for this draw path` / outside the `MultiStreamOracle` / `BindingOffsetOracle` sets | 84 |
| format capability absent (float/half/normalized/packed/DXT cube/volume, RGBA16F/RGBA32F targets) | ~45 |
| `OPENGL4 implements no virtual resolution` (presentation rectangle) | 6 |
| null stock texture, declaration translation, vertex-sampleable textures, other single cases | ~14 |

About 200 renderer gates across 32 gtest files name OPENGLES3/OPENGL33 and not OPENGL4. Each is
revisited when OpenGL4 reaches the behaviour it gates, and OPENGL4 is added to it only with the test
then passing — the enabling commit is part of Workstream A, not a way of hiding the gap now.

`CnaRendererTests` aborts on OPENGL33 inside `EasyGLRedundantStateTest` in this configuration
(`terminate called without an active exception`, no XML written). It is EasyGL-internal, does not
occur on OpenGL4 (231 / 0 / 10), and is recorded here as reference-side evidence only.

## GL4-0008 — One GL stock-effect shader corpus instead of two

**Decision (Workstream A architecture).** OpenGL4 stays its own renderer family — its own context
(desktop 4.x core), its own loader, native wireframe and exact occlusion counts — but it stops
hand-maintaining its own copy of XNA's stock-effect shading. Its 23 GLSL 410 programs were ported
from EasyGL in July 2026; EasyGL's have since absorbed the corrections the baseline measured
OpenGL4 missing (null texture → opaque black, `COLOR0` saturation, fog premultiplied by alpha,
render-target V orientation, inverse-transpose skin normals, dual-UV PBR, the Direct3D clip-depth
conversion, shadow reception, IBL). Porting each correction again would leave two corpora to drift
apart a second time.

`modules/graphics/include/CNA/Internal/Renderers/Common/GlStockShaderSources.hpp` now holds the
GLSL ES 3.00 sources of every stock program (`Colored3D`, `Textured3D`, `ColoredTextured3D`,
`Lit3D`, `Lit3DVertexLit`, `DualTextured3D`, `DualTexturedColored3D`, `EnvMapped3D`, `Skinned`,
`SkinnedVertexLit`, `Pbr`/`PbrSkinned` single- and dual-UV, `Sprite`), the shared declarations
(`CNA_GL_*_DECL`, `CnaGlIblDecl`) and the clip-depth adaptation. The text was **moved verbatim**
out of `EasyGLRenderer.cpp` by script; the only edit is that the PBR builders take
`explicitLodAvailable` as a parameter instead of reading EasyGL's active profile. EasyGL compiles
the same strings through the same adaptation as before.

**EasyGL is unchanged by the move (B39), measured on the same binaries:**

| suite | before | after |
|---|---|---|
| corpus `-R '^OpenGL4_(Parity|EasyGLParity)_'`, OPENGL33 | 355 / 23 of 378 | no new failure; 3 newly pass (the `GL4-0004` HiDef fixes and the corpus working-directory fix) |
| same, OPENGLES3 | 351 / 27 of 378 | no new failure; same 3 newly pass |
| `CnaGraphicsExtTests`, OPENGL33 | 922 / 7 / 33 | 922 / 7 / 33 |
| `CnaGraphicsExtTests`, OPENGLES3 | — | 954 / 0 / 8 |

The uniform contract these programs declare is part of the corpus: every uniform a program
declares is resolved by EasyGL (checked by script — no declared-but-unresolved uniform in any of
the twelve 3D programs), so a second binder may resolve the complete name set generically and
obtain EasyGL's exact per-program behaviour.

## GL4-0009 — Loader: the tokens and entry points the EasyGL-parity paths need

`GL4Loader.hpp/.cpp` gained the `#ifndef`-guarded tokens the rewrite below uses (sample mask,
program point size, subpixel bits, context profile/flags, `KHR_debug`, texture swizzle, draw-buffer
and attachment limits, the S3TC/snorm/packed/float/integer formats, LOD/level parameters, PBO and
copy-buffer targets, framebuffer status codes, …) and, as **required** 4.1-core entry points,
`glUniform{3,4}fv`/`glUniform1iv`/`glUniformMatrix3fv`, `glDrawElementsInstancedBaseVertex`,
`glDrawArraysInstanced`, `glSampleMaski`, the compressed-texture and buffer copy/readback calls,
`glClearBuffer*`, `glFramebufferTexture`, `glGetFramebufferAttachmentParameteriv`,
`glGetSamplerParameteriv` and `glIsProgram`. `KHR_debug` (`glDebugMessageCallback/Control/Insert`,
`glPush/PopDebugGroup`, `glObjectLabel`) is loaded **optionally**: it is core only from 4.3, and a
4.1 context without it keeps working with debug output simply unavailable.

## GL4-0010 — One presentation transform for both GL families

`EasyGLSurfaceState` — virtual resolution, `FixedHeightDynamicWidth`/`Stretch`/`Letterbox`/
`Overscan`, window↔logical mapping and the physical default viewport — moved **verbatim** into
`modules/graphics/include/CNA/Internal/Renderers/Common/GlPresentationSurfaceState.hpp`;
`EasyGLRenderer.hpp` keeps `using EasyGLSurfaceState = GlPresentationSurfaceState;`. OpenGL4
presented with a fill-the-window-only mapping before (`PresentationRectangle*`, Letterbox and the
`OPENGL4 implements no virtual resolution` skips of `GL4-0007`); it now uses the same object.
EasyGL rebuilt unchanged (see the regression line in `GL4-0018`).

## GL4-0011 — The renderer rewrite: EasyGL's measured semantics over raw desktop GL

**Decision.** OpenGL4 stays its own family (own context request, own loader, native polygon mode,
exact occlusion counts), but everything that decides *what XNA means* is EasyGL's measured code,
ported line by line onto `gl4_*`, instead of the July 2026 implementation that had drifted from it
by 95 failing tests. The new structure:

| file | owns |
|---|---|
| `OpenGL4Common.hpp` | the bound-target record (`REMED-GFX-168`, shared with every target through `weak_ptr`), `SampledRowOrderIsBottomUp` (`REMED-GFX-147`), GL error-queue discipline, `ScopedScissorTestDisabled`, `AdaptGlslEs300ForDesktopCore` |
| `OpenGL4Renderer.cpp` | context/version check, debug output, presentation and MSAA, clears, render state, samplers, render targets and MRT, buffers, custom effects, occlusion queries, the context lease |
| `OpenGL4StockDraw.cpp` | stock program selection, uniform binding, declaration-driven attributes, multi-stream and instancing, the base-vertex fallback, every draw route |
| `OpenGL4SpriteBatch.cpp` | the SpriteBatch |
| `OpenGL4Resources.hpp`, `OpenGL4Textures.cpp`, `OpenGL4RenderTargets.cpp`, `OpenGL4Formats.cpp` | textures, cube/volume textures, render targets, the surface-format layer and its runtime probes |

**Context (A7).** The constructor reads back `GL_MAJOR/MINOR_VERSION` and
`GL_CONTEXT_PROFILE_MASK` and **throws** when the platform granted less than a 4.1 core context,
naming what it got, instead of running as some other GL. The startup line moved from `std::cout` to
the renderer log (`GraphicsDeviceRendererTest.StartupDiagnosticNeverWritesToStdout`).

**Render state (A10/A18–A20), each an EasyGL rule and each a failure of the old code:**
`BlendState.Opaque` disables blending, factors/equations are always rewritten, per-slot colour
masks through `glColorMaski`, `MultiSampleMask` through `glSampleMaski`; depth/stencil gated by the
bound target's real depth format; two-sided stencil with XNA's clockwise tuple on `GL_BACK` (the old
code had the faces swapped); the CCW tuple applied only to triangles (D3D9's rule); reference
stencil reissued on its own; every clear neutralises and restores scissor, colour masks and the
depth/stencil write masks (`REMED-GFX-237` — `ClearColorAndStencil` restores the stencil mask, which
EasyGL's twin of that route does not); zero-extent scissor rectangles reach `glScissor`
(`SOFTWARE-310`); depth bias converted to polygon-offset units by the bound depth format's
precision; the XNA pixel-centre displacement (63/128 of a pixel, capped by subpixel precision,
suppressed on multisampled destinations — `REMED-GFX-235`); D3D clip-depth conversion in every stock
vertex program (`SOFTWARE-336`).

**Samplers (A16).** One sampler object per XNA slot bound to its own unit; every filter ordinal
carries its mip term (`REMED-GFX-175` — the old code mapped Point/Linear to mip-less filters);
anisotropy, W, MinLod/MaxLod/LodBias and compare mode are rewritten on every application
(`REMED-GFX-174`, `FX-092`); `MaxMipLevel` → `GL_TEXTURE_MIN_LOD` with XNA's UInt32 conversion.

## GL4-0012 — Textures, render targets and formats

Ported from EasyGL against a fixed header contract, then integrated and measured here.
Every EasyGL Texture2D format with the same storage choice (packed 16-bit with its alpha rotation,
RG8/RGBA8 SNORM, RGB10_A2, RGBA16, half/float, channel-expanded Alpha8/Single/Vector2/…, DXT native
when S3TC exists and exact CPU decode otherwise); cube and volume textures with declared-format
transfers; RenderTarget2D and RenderTargetCube with MSAA (clamped to `GL_MAX_SAMPLES` and to the
format's own `GL_SAMPLES`), resolve and mip regeneration on unbind, bottom-up storage mapped on
readback, and the runtime float/normalized render-target probes. Deliberate desktop deviations,
each measured by the corpus: every transfer restores the unit, framebuffer, pack/unpack and PBO
bindings it touched; cube and volume readback uses `glGetTexImage` instead of EasyGL's ES-only CPU
copy; framebuffer completeness is checked and a failure names its status; every resolve blit runs
with the scissor test off (`ScopedScissorTestDisabled`) — EasyGL's resolves are clipped by an active
partial scissor, which XNA's are not.

## GL4-0013 — The stock draw path

Program **shape** from the effect state, never from the stride (`REMED-GFX-218`); the programs are
the shared corpus of `GL4-0008`, adapted to `#version 410 core`. Attributes are bound by semantic
from the caller's declaration, across every per-vertex stream (`REMED-GFX-201`), with per-instance
streams at locations 12–15 for stock programs and after the per-vertex streams for a
`ShaderEffect` (`REMED-GFX-202`); a negative `baseVertex` is folded into a scratch index buffer.

Two defects found and fixed while bringing this up, both measured:

- **`REMED-GFX-234` / `WEBGPU-158` — a declaration that names no Normal cannot be lit.** Vulkan and
  WebGPU implement it; EasyGL lost it when its selection moved to effect state, which is why both GL
  families failed `Parity_unlit_position_color` and the clamp leg of
  `Parity_basic_effect_vertex_color`. OpenGL4 now draws such a BasicEffect draw unlit
  (`DeclarationRulesOutLighting`). EasyGL still fails both (recorded, not changed here).
- **Lazily created fallback textures clobbered the active unit.** Creating the white
  metallic-roughness fallback bound it on the unit that already held the flat-normal fallback, so
  the first PBR draw of a process was lit with a `(1,1,1)` normal: `PbrEffect_Golden` quad A read
  162 against 137, while B/C/D matched EasyGL to one step. Fallback creation now restores the
  unit's binding. The same defect explained the three `Gltf_*Tangent*`/`SkinnedPbrNonUniformJoint`
  failures.

## GL4-0014 — SpriteBatch

EasyGL's: the device's own blend/depth/rasterizer/sampler state (the old code hard-coded
`SrcAlpha/InvSrcAlpha` and depth off), projection from the device Viewport including game-set
sub-viewports and letterboxing (`REMED-GFX-072`), render-target V mirrored in the quad
(`REMED-GFX-147`), clamp-constant UV reduction, 2 048-sprite submissions, Immediate mode flushing
per Draw, a custom effect applied at submission with the Effect's own program. One EasyGL defect
**not** carried over: its "is a target bound" question ignores a bound cube face, so a sprite drawn
into a cube face is projected onto the window instead (`GetBoundRenderTargetSize` answers for cube
faces here). That single defect is behind ten corpus failures EasyGL still has
(`RenderTargetCube_*`, `ColorSpace_MidTone` G1, `OrderedClear` K1, `RenderTarget_FirstUse`/
`PassBoundary`/`BackbufferConsumer`, `Backbuffer_PassOrder`).

## GL4-0015 — XNA's Point filter is mip-point too

`OpenGL4_Mipmap` Check B asserted that Point "never mip-selects on this renderer" — the old defect
written down as behaviour. It now asserts XNA's answer (GREEN at a minified size).

## GL4-0016 — GL debug output and the `[OpenGL4 GL Error]` gate (A8/A9)

With `KHR_debug` available (Debug builds, or `CNA_OPENGL4_DEBUG_OUTPUT=1`), the renderer installs a
synchronous callback. An error, undefined behaviour or high-severity message prints
`[OpenGL4 GL Error] <source>/<type>/<severity> id=<n>: <message>`; informational messages are
dropped unless `CNA_OPENGL4_DEBUG_OUTPUT=verbose`. Shader-compiler messages are not errors here: a
game's broken GLSL reaches its caller through `ShaderEffect`'s compile diagnostics, and a stock
program that fails to build prints its own `[OpenGL4 GL Error]` line and throws.

`cmake/TestHelpers.cmake` gains `cna_append_opengl4_gl_error_gate_pattern` beside the Vulkan
validation gate, applied by `cna_register_renderer_test` wherever an OpenGL4 context can exist, by
`cna_apply_opengl4_gl_error_gate()` at the end of the OpenGL4 examples directory, and to the
PRE_TEST-discovered `CnaTests` cases (one alternation with the Vulkan pattern). The exemption list
is empty.

**Measured on the corpus: 0 serious messages** in the final 405-test run.

## GL4-0017 — Tests that encoded OpenGL4's old non-XNA behaviour

Corrected, each to XNA's measured answer, each with a comment at the change:

| test | was asserting | now |
|---|---|---|
| `OpenGL4_RenderState` E, `OpenGL4_RenderTarget2D` C, `OpenGL4_RenderTargetCube_MRT` D | a "near" quad at z=−0.5 wins the depth test | both quads inside XNA's `[0,w]` clip range (0.25 / 0.75); −0.5 is clipped by D3D and XNA |
| `OpenGL4_Fog` C–H | full fog at Z=FogStart=−0.9 | FogStart=0.9/FogEnd=−0.9: same two oracles, inside the clip range |
| `OpenGL4_Readback` G | `Color(255,255,255,128)` over black is half green | the premultiplied half tint `Color(128,128,128,128)` — `AlphaBlend` is One/InvSrcAlpha |
| `OpenGL4_Smoke`, `OpenGL4_RenderState` | stencil clears with the default `Depth24` | request `Depth24Stencil8`; XNA refuses a stencil clear without stencil |
| `OpenGL4_ShaderEffect3D`, `OpenGL4_ShaderEffectSpriteBatch` | — (`GL4-0004`'s project-profile opt-in never reached these: they have no `GraphicsDeviceManager`) | HiDef set on the Game's own device, as EasyGL's copies do |

## GL4-0018 — Shared tests brought in line with measured XNA; OpenGL4 admitted to the shared gates

**Shared test corrections** (each failed on every renderer, EasyGL included, before this):

- `draw_line_topology_test` asserted zero-capacity buffers are accepted; Microsoft XNA refuses them
  with `ArgumentOutOfRangeException` (`SOFTWARE-204`) and the framework does.
- `rendertarget_active_msaa_readback_test` accepted only `NotSupportedException`; reading an active
  target throws XNA's `InvalidOperationException` (`SOFTWARE-246`) from the shared guard.
- `rendertarget_surface_format_contract_test` asserted a `Dxt1` render target is refused; XNA
  substitutes `Color` for an unavailable preferred format (`SOFTWARE-216`). The check (from `DX-215`)
  was never reconciled with that rule.
- `easygl_viewspace_fog_test`'s SkinnedEffect leg multiplied by an unset texture, which reads
  opaque black under `GSC-0004`; it now supplies a white texel.
- `easygl_shader_effect_test` hard-coded the GLSL ES dialect; it now expects the context's own. The
  `PortableTint` shader package gained a desktop-GLSL pair (regenerated reproducibly with
  `tools/shader_package`) — without it no desktop GL context had a usable variant.
- `sampler_lod_addressw_contract_test`, `rendertarget_surface_format_contract_test` picked HLSL for
  OpenGL4; they now take the GLSL branch. Ten sampler/format contract tests printed `UNKNOWN` for
  the renderer name and now name OPENGL4 (they already ran every check).
- `GltfRendererPbrFallbackPolicy` (source-evidence audit) failed 19 cases after `GL4-0008` moved the
  shaders — a regression of that commit, caught here. The GL families' audited text now includes the
  shared corpus header (the D3D families' `common/d3d` precedent), and every OpenGL4 row names the
  new binder and the shared shader text.

**Renderer gates.** 191 `CNA_SKIP_IF_RENDERER_IS_NONE_OF`/`CNA_RENDERER_IS` lists in 33 shared test
files named OPENGL33 and not OPENGL4; OPENGL4 was added to each and the suites run. One gate was
EasyGL-internal (a `dynamic_cast` to EasyGL's buffer) and was left alone. Three compile-time
`#if defined(CNA_RENDERER_EASYGL)` groups now include OpenGL4, and three generic tests pinned to
Vulkan/Software or OPENGL33 alone (`IndexedTopologiesRenderExactDistinctGeometry`,
`PublicThirtyTwoBitTopologiesRenderExactDistinctGeometry`,
`ThirtyTwoBitDrawDoesNotPoisonNextDesktopContext`) were admitted and pass.

**Found, not changed (outside this workstream):** `ContentManagerVideoXnbTest.TheObjectReferencedFormLoadsToTheSameValuesAsTheInlineOne`
writes and then deletes the **committed** fixture `tests/assets/media/video/video_xnb_object_fixture.xnb`
in the source tree on every run; it is restored with `git checkout` after each `CnaContentTests` run
here and never staged.

### Results after GL4-0009..GL4-0018 (same hardware, private runner)

| suite | baseline (`GL4-0006`) | now |
|---|---|---|
| corpus `-R '^OpenGL4_'` on OPENGL4 | 283 / 122 of 405 | **405 / 0** |
| same binaries, OPENGL33 (378 shared) | 355 / 23 | 364 / 14 — no test that passed before fails |
| same binaries, OPENGLES3 (378 shared) | 351 / 27 | 358 / 20 — no test that passed before fails |
| `CnaRendererTests` OPENGL4 | 231 / 0 / 10 | 231 / 0 / 10 (the ten are EasyGL-internal) |
| `CnaGraphicsTests` OPENGL4 | 2 473 / 12 / 376 | **2 815 / 0 / 75** of 2 890, then +3 admitted above (run individually, 3/3) |
| `CnaContentTests` OPENGL4 | not run at baseline | **1 850 / 0 / 4** |
| `CnaGraphicsExtTests` OPENGL4 (modern, Workstream B) | 798 / 44 / 120 | 856 / 1 / 105 — the one failure is `ClusteredForwardEffectTest.ATransmissiveMaterialWithoutAnOpaqueFrameIsRefused`, which OPENGL33 fails identically |

The 72 `CnaGraphicsTests` skips left after those three admissions, classified: 27 `SdlGpu*`,
6 `Software*` non-indexed, 2 `Software*` indexed, 1 `D3D*`, 1 SDL_GPU point list, 1 SDL_GPU
layout — tests owned by another renderer; 15
compiled-effect tests (`SupportsCompiledEffects` is false — see the parity matrix); 4
`VertexDeclarationLayoutTests` for renderers that refuse colliding declarations (OpenGL4 translates
them); 1 wireframe-refusal leg (OpenGL4 has native polygon mode); 5 fake-window platform tests
(OpenGL4 needs a real GL window); Texture3D-absent/present complements (2); three presentation-region
tests for STUB/SOFTWARE/HEADLESS; GLES/WebGL adapter contract (1); adapter flag selection (1); two
multi-renderer fallback configuration tests. None is an OpenGL4 gap except compiled effects.

## GL4-0019 — X11 hardware path, SDL-free proof, and an OPENGL4-only configure defect

**Configure defect.** An OPENGL4-only configuration could not build `CnaRendererTests`:
`cmake/UnitTests.cmake` counted OPENGL4 among the EasyGL identities, so EasyGL's own suites (which
include `<metagl/metagl.hpp>` from the easy-gl checkout only EasyGL brings) were compiled without
their include root. OPENGL4 is removed from that list; the multi-renderer tree, where EasyGL is
present, is unaffected.

**X11 (GLX) on hardware.** `cmake-build-opengl4-x11` — the Wayland tree's settings with
`CNA_PLATFORM=X11` and OPENGL4 alone (named after the `cmake-build-webgpu-x11` precedent), built
target by target (the corpus, `CnaGraphicsTests`, `CnaRendererTests`), run on the private rootful
Xwayland with DRI3, `GL_RENDERER` AMD Radeon 780M (radeonsi), 4.6 core:

| suite | X11 / GLX | Wayland / EGL (`GL4-0018`) |
|---|---|---|
| corpus `-R '^OpenGL4_'` | **404 / 1** of 405, then the one under openbox **1 / 0** | 405 / 0 |
| `CnaGraphicsTests` | **2 800 / 0 / 71** of 2 871 (single-renderer inventory) | 2 815 / 0 / 75 |
| `CnaRendererTests` | **214 / 0 / 0** (no EasyGL suites in this configuration) | 231 / 0 / 10 |

The one corpus failure is an environment boundary, not a renderer result: the private Xwayland runs
no window manager, and the X11 backend correctly refuses `BorderlessFullscreen` without EWMH
(`FullScreenField`). Run again with `~/deps/openbox` managing the same private display it passes.

**SDL-free.** Both trees configure with `CNA_ENABLE_SDL=OFF`. `readelf -d libcna.so` `NEEDED`:

- Wayland: `libwayland-client libxkbcommon libOpenGL` (+ FFmpeg, zstd, libstdc++/libc) — no SDL,
  no X11. `ldd` shows `libX11`/`xcb` only transitively through FFmpeg's `libva-x11`.
- X11: `libX11 libXext libXi libXrandr libXcursor libXau libXss libOpenGL` (+ the same) — no SDL.

Test executables `NEEDED` only `libcna.so` and the C++ runtime. `WaylandIsSdlFree.*` 5/5.
`ModuleLinkClosure_*` skip under the Ninja generator (they read Makefiles link files), so the ELF
listings above are the evidence.

## GL4-0020 — Compiled XNA Effect bytecode on OpenGL4

The one classic capability the EasyGL family had and OpenGL4 lacked: compiled `.fxb` effects through
MojoShader's OpenGL adapter (`plans/plan_fx.md` FX-062/080/082/083/088/099/118/128). Now behind
`CNA_OPENGL4_COMPILED_EFFECTS` (off by default, shaped like `CNA_EASYGL_COMPILED_EFFECTS`), ported
from EasyGL's desktop profile against a fixed contract:

- `OpenGL4CompiledEffect.{hpp,cpp}` — the runtime (techniques, parameters, textures, pass
  application with shader-model-1 sampler inference, the FX-129 link-failure check, bound-sampler
  reporting, `Clone`); `OpenGL4CompiledEffects.cpp` — the renderer side: the MojoShader context with
  `MOJOSHADER_PROFILE_GLSL120` set explicitly (FX-128: `glspirv` crashes on pixel-only passes), the
  shared compiled-effect VAO, multi-stream/instanced/base-vertex draw routes, vertex samplers on
  units 16–19, render-target orientation by flipped source copies (FX-099/FX-118), and the
  SpriteBatch route with the embedded XNA SpriteEffect for vertex-shader inheritance (FX-080).
- Deliberately different from EasyGL: no context-loss path (desktop OpenGL4 has none); the flipped
  copy carries the D3D9 channel expansion (`Single` samples as (R,1,1,1)); an effect that outlives
  its renderer is detached and refuses further use by name (EasyGL's destructor then calls into the
  destroyed renderer — recorded as an EasyGL defect, not changed).
- **Shared build change:** `cna_configure_mojoshader()` refused every configuration without an SDL3
  target, and OPENGL4's natural configuration is SDL-free. With `CNA_ENABLE_SDL=OFF` MojoShader is
  now built on the C standard library without its SDL_GPU adapter; with SDL on, the configuration is
  unchanged and a missing SDL3 target is still fatal. `cna_mojoshader_effect_probe` links SDL3 only
  when it exists; the SDL_GPU/GL probes are skipped without SDL3. The configure re-applied the
  pinned MojoShader patch series in the shared `~/deps/FNA3D` checkout (its stamp predated the
  current series) — other trees pointing at that checkout will recompile MojoShader once.

`cmake-build-opengl4` now carries the option ON; `cmake-build-opengl4-x11` keeps it OFF and still
builds, so both sides of the guard stay compiled.

| suite (OPENGL4, Radeon 780M, private runner) | before | after |
|---|---|---|
| corpus `-R '^OpenGL4_'` | 405 / 0 | **405 / 0** |
| `CnaRendererTests` | 231 / 0 / 10 | **318 / 0 / 10** — 84 new `OpenGL4Compiled*` (every renderer-neutral case of EasyGL's compiled-effect suite, the shared `Run*Contract` conformance, draw and SpriteBatch golden pixels) + 3 probe cases |
| `CnaGraphicsTests` | 2 815 / 0 / 75 | **2 833 / 0 / 57** — the 15 compiled-effect skips now run and pass; `SupportsCompiledEffectsOnlyOnCompletedBackends` passes |
| `cna_mojoshader_effect_probe` | — | 1 / 1, built without SDL |
| `[OpenGL4 GL Error]` lines | 0 | 0 |

## GL4-0021 — Each device's GL work stays in its own context

**Found by the GL error output of `GL4-0016`,** not by any assertion: `CnaGraphicsExtTests`'
`MultiDeviceTest` (two `GraphicsDevice`s, two contexts) produced 105
`[OpenGL4 GL Error] api/error/high id=1: GL_INVALID_VALUE in glDeleteProgram` while passing — the
tests compare memory estimates, which cannot see where GL calls land. OpenGL4 made its context
current only when *creating* a resource, so the first device's later work — render-target binds,
clears, uploads, uniform writes, deletions — ran in whichever context was current, the second
device's after it was created. `GL_INVALID_VALUE` is the lucky outcome; an unrelated object that
happens to carry the same name in the other context is the unlucky one.

**Fix.** `PlatformGlContextOwner` gains `IsCurrent()`/`EnsureCurrent()`. OpenGL4's
`EnsureCallingThreadContext()` uses the cheap form (it used to call `MakeCurrent` unconditionally)
and every GL-issuing renderer entry point calls it (33: draws, clears, state, samplers, targets,
presentation, readback). Every resource that owns GL names (textures, cube/volume textures, render
targets, vertex/index buffers, occlusion queries, custom-effect programs, the SpriteBatch) derives
from `OpenGL4ContextResource`, is attached to its creating renderer's context by a **weak**
reference, and enters that context for each GL-issuing operation, restoring the previous binding
afterwards. A resource whose context is already gone issues no GL at all — its names died with the
context. The compiled-effect runtime's MojoShader switch now also ensures the GL context.

**Tests** (`OpenGL4MultiDeviceTests.cpp`, with an in-process capture of `[OpenGL4 GL Error]` lines):
interleaved clears into two devices' targets land in the right targets; destroying one device's
target and device while the other is current leaves the other's target intact; renderer resources
outliving their device release without GL. **With the fix disabled all three fail** — the second
case measurably corrupts the surviving device's render target: the first device's deletions,
issued in the second context, destroyed the second device's own objects.

| suite | after `GL4-0020` | now |
|---|---|---|
| corpus, Wayland / X11 (openbox) | 405 / 0, 405 / 0 | 405 / 0, 405 / 0 |
| `CnaRendererTests`, Wayland / X11 | 318 / 0 / 10, 214 / 0 / 0 | **321 / 0 / 10**, **217 / 0 / 0** |
| `CnaGraphicsTests` | 2 833 / 0 / 57 | 2 833 / 0 / 57 |
| `CnaGraphicsExtTests` (B) | 856 / 1 / 105, **105 GL errors** | 856 / 1 / 105, **0 GL errors** |

EasyGL has the same single-context assumption (its renderer, too, only switches context when
creating resources); recorded here, not changed.

## GL4-0022 — EasyGL ↔ OpenGL4 classic parity matrix (A32/A33)

Every classic capability of the EasyGL family, with OpenGL4's status and the evidence that decides
it. "Same binaries" means the `OpenGL4_EasyGLParity_*`/`OpenGL4_Parity_*` corpus of `GL4-0005`,
run on OpenGL4 and, by runtime selection, on EasyGL. No row is unknown, stubbed or silently
skipped; the only classic rows that are not "implemented" are the two marked **n/a**, each with its
reason. Modern (CNAEXT) rows are Workstream B's and are listed at the end only so the partition is
visible.

| area | EasyGL | OpenGL4 | evidence |
|---|---|---|---|
| Context | ES 3.0/WebGL2 or desktop 3.3 core | desktop **4.1 core** floor, refuses less by name (`GL4-0011`) | `OpenGL4_Smoke`, `OpenGL4_ModernFeatureDiscovery` (forced 4.1) |
| Platforms | SDL3, X11, Wayland | X11 (GLX) and Wayland (EGL), SDL-free | `GL4-0019`: 405/405 on each |
| Presentation | virtual resolution, 5 modes, window↔logical | same object (`GlPresentationSurfaceState`) | `PresentationRectangleTests` (admitted), corpus `Presentation*`, `Letterbox*` |
| Backbuffer MSAA, apply/reset | yes | yes (4.x guarantees ≥4 samples) | corpus `MsaaChange`, `Backbuffer*`, `MsaaFragmentContract` (EasyGL fails, OpenGL4 passes) |
| Swap interval (recorded) | yes | yes | corpus `SwapIntervalForwarding` |
| Clears (6 routes, masks/scissor restored) | yes | yes, plus the stencil-mask restore EasyGL omits | corpus `GraphicsDevice_OrderedClear`, `Clear*` |
| BlendState (Opaque, factors, equations, per-slot masks, MultiSampleMask) | yes | yes | `Parity_blend_states`, `BlendState*`, `GraphicsProfileBlendStateTests` |
| DepthStencilState (depth formats, two-sided stencil, CCW on triangles only, reference) | yes | yes | `Parity_depth_states`, `Parity_stencil_*`, `TwoSidedStencilTests` |
| RasterizerState (cull, native wireframe, scissor incl. zero extent, depth bias in depth units, MSAA toggle) | yes (desktop native wireframe) | yes | `Parity_fill_mode_wireframe`, `Parity_rasterizer_viewport`, `RasterizerDepthBiasContractTests` |
| Viewport, XNA pixel centre, D3D clip depth | yes | yes | `Parity_rasterizer_viewport`, `OpenGL4_RenderState` |
| Samplers (9 filter ordinals with mip terms, anisotropy, W, MaxMipLevel, LOD bias) | yes | yes (LOD bias as desktop GL) | `Parity_sampler_*`, `SamplerLodAddressWContract`, `TextureFilter*Contract` |
| Texture2D formats (Color, packed 16-bit, SNORM, RGB10A2, RGBA16, half/float, expanded channels, DXT native or decoded) | yes | yes | `ClassicTextureFormatTests`, `Texture2DTests` (lists admitted), corpus `DxtTexture*`, `ColorSpace_*` |
| TextureCube / Texture3D (formats, transfers, sampling) | yes | yes; readback via `glGetTexImage` | `Texture3DTextureCubeRenderTargetTests`, corpus `TextureCube*`, `Texture3D*` |
| RenderTarget2D / RenderTargetCube (formats, MSAA, resolve, mips, preserve, readback orientation) | yes, but a SpriteBatch drawn into a cube face is projected onto the window | yes, including cube faces | corpus `RenderTarget*` (10 cube tests EasyGL fails), `NormalizedRenderTargetRoundTripTests` |
| MRT (≤4, `GL_MAX_DRAW_BUFFERS`/`COLOR_ATTACHMENTS` queried, cube faces, completeness) | yes | yes | corpus `RenderTargetCube_PluralBinding`, `OpenGL4_RenderTargetCube_MRT` |
| Vertex input (declaration semantics, any order, multi-stream, stride fallback, REMED-GFX-234) | yes, but lit draws with no Normal are not unlit | yes | `VertexDeclaration*Tests`, `BuiltInVertexLayoutTests`, `OrdinaryDrawMultiStreamTests`, `Parity_vertex_semantics`, `Parity_unlit_position_color` |
| Integer GLSL inputs of a `ShaderEffect` (`ivecN`/`uvecN`) | read through the float path (undefined) | `glVertexAttribIPointer` from integer storage; float storage refused by name (`GL4-0023`) | `OpenGL4IntegerAttribute.*` 2/2 |
| Index buffers 16/32, base vertex (negative folded), draw ranges | yes | yes (`glDrawElementsBaseVertex`) | `IndexedDrawDeferredTests`, `NonIndexedDrawRangeTests`, `IndexBuffer*` |
| Instancing (per-instance streams, frequency, offsets) | yes | yes | `InstancedDraw*Tests`, `Parity_instanced_draw` |
| Point lists | yes | yes (`gl_PointSize`) | `PointListPrimitiveTests` |
| Stock effects (Basic incl. per-vertex/per-pixel, AlphaTest, DualTexture, EnvironmentMap, Skinned) | shared corpus | same corpus (`GL4-0008`) | `Parity_*` (32), corpus `*Effect*` |
| Fog, null-texture black, stock sampler contract | yes | yes | `ViewSpace_Fog`, `StockEffectNullTextureTests`, `StockEffectSamplerContract` |
| PBR / glTF materials (CNAEXT stock) | yes | yes | `PbrEffect_Golden`, `Gltf_*`, `GltfRendererPbrFallbackPolicy` 30/30 |
| SpriteBatch (device state, viewport, sort modes, Immediate, 2 048 chunks, custom effect at submission) | yes | yes | `SpriteBatchRasterizationTests`, `Parity_sprite_*`, corpus `SpriteBatch*` |
| SpriteFont | yes | yes | `Parity_sprite_font`, corpus `SpriteFont*` |
| Custom GLSL `ShaderEffect` (desktop dialect, diagnostics on the author's lines, render-target sources) | yes | yes | `ShaderEffect_GLSL`, `ShaderEffect_ReflectionContract`, `RenderTarget_EffectSource` |
| Compiled XNA Effect bytecode | opt-in `CNA_EASYGL_COMPILED_EFFECTS` | opt-in `CNA_OPENGL4_COMPILED_EFFECTS` (`GL4-0020`) | 84 `OpenGL4Compiled*`, `EffectTests`, `EffectMaterialTests` |
| Occlusion queries | yes | yes, exact counts | `OpenGL4_OcclusionQuery`, corpus `OcclusionQuery*` |
| Backbuffer / target readback | yes | yes | `OpenGL4_Readback`, corpus `BackbufferFirstRead`, `BackbufferReadbackDimension`, `BackbufferResize` |
| Background content loading (context lease) | yes | yes | corpus `ThreadContextLease_Exclusion`, `TextureCube_ContentLoad` |
| Multiple devices | single-context assumption (recorded, `GL4-0021`) | each device in its own context | `OpenGL4MultiDevice.*` 3/3 |
| GL debug output as a failure gate | — | yes (`GL4-0016`) | 0 serious messages in every suite |
| Context-loss recovery (`SetContextRecoveryEnabled`, `DebugSimulateContextLoss`, `CanBeginDrawEXT`) | yes — for WebGL/Android, where the browser or OS takes contexts away | **n/a** — a desktop 4.x core context without `GL_ARB_robustness` reset notification is never taken away; XNA's DeviceLost/Reset stays served by `GraphicsDevice.Reset` | base-class answers (`CanBeginDrawEXT` true) |
| ES 2.0 / WebGL 1 fallbacks (sampler state on textures, ES 1.00 shader rewriting, pointer rebase) | yes | **n/a** — no ES 2 generation in a desktop 4.x context | — |
| **Modern (Workstream B):** compute, storage buffers, image load/store, indirect draw, GPU timers, `SupportsShadowSamplingEXT`, `SupportsImageBasedLightingEXT`, texture arrays, limits | EasyGL implements where the context allows | **not yet** — every query answers false; `CnaGraphicsExtTests` 856/1/105 | `GL4-0006` baseline, Workstream B |

## GL4-0023 — Integer shader inputs through `glVertexAttribIPointer` (A11)

Every stock program and every XNA-ported effect declares float inputs — Direct3D 9 vertex inputs are
float registers, and `Byte4` blend indices are read as floats by design (`FX-127`) — so the
declaration-driven layout binds every element through the converting float path, as EasyGL does. A
GLSL-authored `ShaderEffect` may declare an `ivecN`/`uvecN` input, and the float path leaves it
undefined. After linking, `OpenGL4RawProgram` records its integer-typed attribute locations
(`glGetActiveAttrib`, `glGetAttribLocation` — added to the loader as required 2.0-core entry points).
A custom-effect draw re-points those locations through `glVertexAttribIPointer` from the element's
integer storage (`Byte4`/`Color` as unsigned bytes, `Short2`/`Short4` and their normalized forms as
raw shorts) — across single, multi-stream and per-instance streams — and restores the buffer's
layout afterwards. A float-stored element bound to an integer input is refused with
`NotSupportedException` naming the location and element, instead of rendering undefined values.

`OpenGL4IntegerAttribute.AnIntegerShaderInputReadsTheStoredIntegers` renders a `Byte4`
(200,100,50,255) through a `uvec4` input exactly; with the re-binding disabled it reads garbage
(`FF FF FF 00`, `FF FF FF FE`, … per pixel). The refusal case is the second test. Corpus 405/405;
`CnaRendererTests` 323/0/10, `CnaGraphicsTests` 2 833/0/57, `CnaGraphicsExtTests` 856/1/105, zero
GL errors in each.

## GL4-A-GATE — WORKSTREAM A COMPLETE

Recorded 2026-09-24 at `d86e4e7e4` + this commit, after the final classic regression below. Every
Workstream A requirement, with the task that satisfies it:

| requirement | where |
|---|---|
| A1 EasyGL inventory · A2 OpenGL4 audit | `GL4-0005` (corpus derived from EasyGL's own registrations), `GL4-0006` (baseline classified by cluster), `GL4-0022` (matrix) |
| A3 compile baseline | `GL4-0003` (SDL-free configure), `GL4-0019` (OPENGL4-only configure) |
| A4 registration integrity — no dead / profile-aborted test | `GL4-0004`, `GL4-0017`; `profile_dead_tests.py` finds **0** on both trees |
| A5 EasyGL reference · A6 OpenGL4 baseline | `GL4-0006`, `GL4-0007` |
| A7 core-profile context, refused below 4.1 core | `GL4-0011` |
| A8 GL debug output, serious messages fail tests, 0 unexplained | `GL4-0016`; **0** `[OpenGL4 GL Error]` lines in every suite below |
| A9 GL error discipline | `GL4-0011` (drain-then-judge transfers, MRT setup checks), `GL4-0012` (completeness named), `GL4-0021` (the one error class the gate found, fixed) |
| A10 state isolation | `GL4-0011` (clears restore every mask they override), `GL4-0013` (declaration layout restored after every draw), `GL4-0021`; corpus `Backbuffer_PassOrder`, `FullscreenSpriteThen3D`, `SpriteBatch3DOrder`, `SpriteBatch_BlendStateLeak`, `ResourceLeak`, gtests `*DoNotLeakBetweenFrames`, `SharedStockDrawIsolationContract` |
| A11 vertex declarations / VAOs / integer inputs | `GL4-0013`, `GL4-0023` |
| A12 buffers · A13 texture formats, swizzles, transfers · A14 cube · A15 volume | `GL4-0011`, `GL4-0012` |
| A16 samplers | `GL4-0011`, `GL4-0015` |
| A17 render targets · A18 MRT (limits queried) | `GL4-0011`, `GL4-0012`, `GL4-0014` |
| A19 depth/stencil incl. two-sided · A20 blending · A21 rasterizer · A22 viewport conventions | `GL4-0011`, `GL4-0017` |
| A23 stock effects from the canonical corpus | `GL4-0008`, `GL4-0013` |
| A24 missing stock textures per the measured XNA contract | `GL4-0013` (opaque black / neutral PBR fallbacks), `GL4-0018` |
| A25 SpriteBatch (Effect::Apply at submission) · A26 SpriteFont · A27 custom effects | `GL4-0014`, `GL4-0018`, `GL4-0020` |
| A28 queries | `GL4-0011` |
| A29 presentation and resize | `GL4-0010` |
| A30 X11 hardware · A31 Wayland hardware · A32 SDL-free | `GL4-0019` and the table below |
| A33 parity matrix with no unknown / stub / silently skipped entry | `GL4-0022` |
| A34 this gate · A35 coherent commits | this row; `git log b2a0a5671..HEAD` |

### Final classic regression (AMD Radeon 780M, Mesa 25.0.7 radeonsi, GL 4.6 core, private runner)

| suite | Wayland / EGL | X11 / GLX |
|---|---|---|
| corpus `-R '^OpenGL4_'` (405) | **405 / 0** | **405 / 0** (openbox managing the private display) |
| `CnaRendererTests` | **323 / 0 / 10** (the 10: EasyGL-internal suites) | **219 / 0 / 0** |
| `CnaGraphicsTests` | **2 833 / 0 / 57** | **2 800 / 0 / 71** |
| `CnaContentTests` | 1 850 / 0 / 4 (`GL4-0018`) | — |
| `[OpenGL4 GL Error]` lines | 0 | 0 |
| profile-dead tests | 0 | 0 |
| EasyGL on the same corpus binaries | OPENGL33 364 / 14, OPENGLES3 358 / 20 — nothing that passed at baseline fails | — |

EasyGL's remaining corpus failures are EasyGL defects this workstream found and OpenGL4 does not
share (cube-face SpriteBatch projection, the lost REMED-GFX-234 rule, `MsaaFragmentContract`,
`SkinnedEffectVector4BoneIndices`); they are recorded, not changed, because EasyGL is the reference
and outside this workstream. The one `CnaGraphicsExtTests` failure is a modern (Workstream B) case.

---

# Workstream B — the modern CNA/CNAEXT Graphics API on OpenGL4

## GL4-0024 — Inventory and baselines (B1/B2)

### The modern surface, and where OpenGL4 stood when B began

Taken from the renderer contract (`IGraphicsRenderer.hpp`: the compute/storage/timer interfaces and
the renderer virtuals from `CreateComputeShader` onward), the CNAEXT members of `GraphicsDevice`
(`GetRendererCapabilityProfileEXT` and friends), `docs/modern-gpu-baseline.md`, and the live profile
the checked-in `cna_probe_modern_gpu_capabilities` printed for OPENGL4 on the Radeon 780M at
`a0ae5d056`. The last column names the task that closes the row; it is filled in as they land.

| Group | Entry points | OpenGL4 at `a0ae5d056` | Native fact (`GL4::DiscoverModernCapabilities`, Mesa 4.6 core) | Closed by |
|---|---|---|---|---|
| Capability discovery | `GraphicsCapability` 17/18, `RendererFeature` (Compute, ComputeImageBinding, Indirect, BaseInstance, ShadowSampling, IBL, GpuTimers), 22 `RendererLimit`s, 13 `RendererFormatUsage` bits × 27 formats | every modern feature **unsupported**; every modern limit 0; format usages only the shared derivation (no Sampled/Filterable/Storage*/Multisample answers) | all native | |
| Shader payloads | `SupportsShaderLanguageEXT` | GlslDesktop vertex/fragment only | 4.30 compute available | |
| Compute | `CreateComputeShader`, `DispatchCompute`, scalar uniforms, storage/constant buffer bindings, sampled textures, image binding, storage textures | absent (renderer default: null) | `glDispatchCompute`, `glBindImageTexture`, `glMemoryBarrier` resolved | |
| Buffers | `CreateStorageBuffer(EXT)` — 6 roles × CPU access, ranged transfers, `CopyToEXT`, constant buffers | absent | SSBO, UBO, `glCopyBufferSubData` | |
| Ordering | `MemoryBarrierEXT`, ADR 0001 (results visible without caller barriers, no routine global stalls) | absent | `glMemoryBarrier` | |
| Draws | `DrawPrimitivesIndirectEXT`, `DrawIndexedPrimitivesIndirectEXT`, `DrawInstancedPrimitivesBaseInstanceEXT`, `BindStorageBufferForDrawEXT` | absent / base instance refused | `glDraw*Indirect`, `glDrawElementsInstancedBaseVertexBaseInstance` | |
| Timing / debug | `CreateGpuTimerEXT`, `SetStringMarkerEXT`, `GetTimestampPeriodPicosecondsEXT` | absent; the marker is the base no-op | `GL_TIME_ELAPSED` 64-bit, `KHR_debug` markers | |
| Textures | `StorageTexture2D` (15 exact formats), `Texture2DArray`, float/HDR targets, `Texture3D` sampling | float/HDR targets and 3D sampling already done (Workstream A); storage textures and arrays absent | image load/store, `GL_TEXTURE_2D_ARRAY` | |
| Stock-shader queries | `SupportsShadowSamplingEXT`, `SupportsImageBasedLightingEXT` | false, although the stock programs already bind shadow and IBL resources (`GL4-0013`) | — | |
| Engine layer on top | shadows, IBL, clustered forward, GPU culling, particles, auto-exposure, WBOIT, post-process | runs where no modern query gates it; clustered forward has **no desktop GLSL variant** at all | — | |
| Effect lifetime | an effect disposed while work that names it is outstanding | SpriteBatch holds the custom effect by raw pointer between `Begin` and `End` | — | |
| Not applicable | command buffers, descriptors, queues, native handles | CNA exposes none (ADR 0001); nothing is added because GL could | — | — |

Outside the modern API, so not implemented here: mesh/task shaders, bindless, VRS, sparse
resources, ray tracing, asynchronous compute (`MOD-2266`).

### Baselines — before any Workstream B change

`cmake-build-opengl4` (Wayland, SDL-free, `CNA_CNAEXT=ON`, `CNA_GRAPHICS_RENDERERS=OPENGL4;OPENGLES3;OPENGL33`),
one binary per suite, runtime renderer selection, private runner, Radeon 780M, `a0ae5d056`.

| Suite | OPENGL4 | EasyGL OPENGLES3 | EasyGL OPENGL33 |
|---|---|---|---|
| `CnaGraphicsExtTests` (962, bounded runner, 5 shards) | **856 / 1 / 105** | 954 / 0 / 8 | 922 / **7** / 33 |
| CNAEXT oracles `-R '^CNAEXT_\|^ModernGpuCapabilityProbe$'` (33) | 21 / 1 / 11 | 32 / 1 / 0 | — |

The other families' recorded closeouts on the same hardware, for comparison (their own ledgers):
Vulkan 928 / 0 / 32 of 960 (`VMG-0019`), WebGPU 933 / 0 / 29 (`plan_webgpu_modern_graphics.md`
closeout), SDL_GPU 931 / 0 / 31 of 962 (`SMG-0042`).

**OpenGL4's 105 `CnaGraphicsExtTests` skips, by the reason the test printed:**

| skips | reason | cause |
|---|---|---|
| 31 | "this renderer's lit shaders do not sample shadow maps" | `SupportsShadowSamplingEXT` false |
| 17 + 16 + 7 | "does not support / has no compute shaders" | no compute |
| 15 | "this renderer cannot run the clustered effect" | `clustered_forward` package has no GlslDesktop variant |
| 6 | "has no indirect draw route" | no indirect |
| 6 | "has no GPU timer query" | no timer |
| 4 | "ShaderPackageEXT: no usable shader variant" | no compute, and the test packages carry no desktop compute variant |
| 1 + 1 + 1 | SPIR-V compute / StorageBuffer refused / no argument buffer | no compute, no storage buffers |

**The one failure** — `ClusteredForwardEffectTest.ATransmissiveMaterialWithoutAnOpaqueFrameIsRefused`
— is the same missing desktop variant: the case does not ask `isSupported()`, so on a renderer that
cannot build the effect its "accepted with a frame" leg throws. OPENGL33 fails it identically.

**EasyGL OPENGL33's seven failures** are the same two gaps, seen from a renderer that *has* compute:
Mesa grants EasyGL's 3.3 request a 4.6 core context, so `SupportsComputeShadersEXT` is true, and six
`ModernGpuConformance` cases then throw "no usable shader variant" because
`modern_conformance`'s package offers GLSL ES, SPIR-V and WGSL but no desktop GLSL. The seventh is the
clustered case above.

**CNAEXT oracles.** OpenGL4's eleven skips are the same capability answers (shadow sampling ×5, IBL
×3 with them, compute, indirect+compute, timer, and `CNAEXT_ClusteredLights` for the missing desktop
package). `CNAEXT_NoPosixSetenv` fails on **every** renderer — a source scan finding `setenv`/`unsetenv`
in `modules/platform/src/Wayland/` and the Wayland platform tests. It is not an OpenGL4 or modern-graphics
defect, is outside this workstream's scope, and is recorded here rather than changed.

## GL4-0025 — Compute, storage and constant buffers, ordering, limits (B4–B9, B18)

**What OpenGL4 implements** (`OpenGL4Modern.hpp/.cpp`):

- `OpenGL4StorageBufferRenderer` — one GL buffer object per `StorageBuffer`, every usage/CPU-access
  mask validated. Uploads, `glGetBufferSubData` reads and `glCopyBufferSubData` copies go through the
  copy binding points, which no draw reads, and restore what was bound there. Overlapping same-buffer
  copies are refused.
- `OpenGL4ComputeShaderRenderer`:
  - Desktop GLSL compute programs; a failed build keeps its `CS:`/`Link:` log for the structured
    diagnostics.
  - **Nothing it does touches GL binding state when it is called.** Uniforms go straight to the
    program object (`glProgramUniform1i/1f`, core 4.1).
  - Storage, constant-buffer, sampled-texture and image bindings are *recorded*, and the resources
    they name are held alive. The dispatch installs them, runs, and **restores** everything it
    changed: the program, the active unit, each unit's 2D texture and sampler object, and each
    indexed and generic SSBO/UBO binding.
  - Sampled inputs read through the renderer's own nearest/clamp sampler, not whatever sampler object
    a draw left on the unit.
  - An image is bound in the texture's *actual* GL storage format, asked of GL, because the texture
    layer widens some XNA formats. A storage format that is not an image-unit format is refused with
    `NotSupportedException`.
- **Ordering (ADR 0001, B5).**
  - Every dispatch ends with one `glMemoryBarrier(GL_ALL_BARRIER_BITS)`, so a following dispatch,
    draw (as vertices, indices, indirect arguments, uniforms or textures) or transfer sees what the
    dispatch wrote, without the caller naming a barrier. It is a GPU-side barrier, and nothing waits
    on the CPU: no `glFinish` or `glClientWaitSync` anywhere on the path.
  - `MemoryBarrierEXT` still translates CNA's bits one by one for explicit requests.
- **Where compute is promised.** `SupportsComputeShadersEXT` requires the native compute and SSBO facts
  **and** a 4.3+ context. The native classifier also accepts a 4.2 context with
  `GL_ARB_compute_shader`, but every desktop compute payload CNA ships is `#version 430 core`, which
  such a context cannot compile. `SupportsComputeImageBindingEXT` adds image load/store; desktop GL,
  unlike ES 3.1, needs no immutable storage. `SupportsShaderLanguageEXT(GlslDesktop, Compute)` follows
  compute.
- **Limits, all asked of the context:**

  | Limit | Source |
  |---|---|
  | work-group count, size, invocations | `GL_MAX_COMPUTE_*` |
  | vertex SSBO blocks | `GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS` |
  | storage and uniform block bytes | 64-bit `glGetInteger64v` |
  | compute SSBO bindings | min(compute blocks, buffer bindings) |
  | sampled textures per stage | smallest of fragment, vertex and compute units |
  | storage images per stage | min(compute image uniforms, image units) |
  | vertex attributes | `GL_MAX_VERTEX_ATTRIBS` |
  | vertex input bindings | the 16-stream ceiling; each stream is its own buffer (GL4-0013) |
  | colour attachments | the MRT ceiling |
  | SSBO and UBO offset alignment | `GL_*_OFFSET_ALIGNMENT` |
- `BindStorageBufferForDrawEXT` binds the indexed SSBO for the next draw's shaders.

**Shared test changes (B18, "desktop variants").**

- `modern_conformance`, `modern_resource_interop` and `constant_buffer` gain a `#version 430 core`
  compute variant. Each is the ES text with the version line and ES's precision statements and
  qualifiers replaced, the convention of the engine layer's own `*.desktop.comp.glsl`.
- The headers were regenerated with the pinned shaderc and naga; `--check` reproduces them, and their
  SPIR-V and WGSL payloads are byte-identical. The test packages offer the variant.
- `ComputeShaderTests`' and `ComputeCullingTests`' legacy GLSL ES string payloads now take
  `CnaTest::EngineLayer::LegacyComputeSource`, which is the same program as GLSL 4.30 on a desktop
  core renderer. They skip only where the renderer takes neither dialect.
- `opengl4_modern_feature_discovery_test` no longer asserts that compute is unclaimed. It asserts
  that compute is promised exactly where a 4.3+ context provides it.

**New OpenGL4 tests** (`OpenGL4ComputeIsolationTests.cpp`): a dispatch between `effect.Apply()` and
the draw leaves the effect's texture on its unit, and leaves the draw's storage block on its binding.
Each case also checks that the dispatch really read or wrote its own binding. **Mutation check:**
with the dispatch's restore loops disabled, both fail on all 16 pixels; restored, both pass.

**Results** (same binaries, private runner, Radeon 780M):

| Suite | Before (GL4-0024) | After |
|---|---|---|
| `CnaGraphicsExtTests` OPENGL4 | 856 / 1 / 105 | **893 / 1 / 68** |
| `CnaGraphicsExtTests` OPENGLES3 | 954 / 0 / 8 | 954 / 0 / 8 |
| `CnaGraphicsExtTests` OPENGL33 | 922 / 7 / 33 | 938 / 2 / 22 |
| `CnaGraphicsTests` OPENGL4 | 2 833 / 0 / 57 | 2 833 / 0 / 57 |
| `CnaRendererTests` OPENGL4 | 323 / 0 / 10 | 325 / 0 / 10 |
| CNAEXT oracles OPENGL4 | 21 / 1 / 11 | 22 / 1 / 10 (`CNAEXT_ComputeParticles` runs) |
| `[OpenGL4 GL Error]` lines | 0 | 0 |

OPENGL33's six `ModernGpuConformance` failures are gone, because the package now has the variant its
4.6 context compiles. One OPENGL33 case changed from skipped to **failing**:
`ComputeTest.ImageBindingEitherWorksOrRefusesWithItsReason`. It used to skip as "GLSL ES payload", and
now runs EasyGL's desktop image path, which reads back zero for every texel.

- The same test passes on OpenGL4.
- EasyGL's texture is sized `RGBA8` and its `GL_TEXTURE_MAX_LEVEL` is clamped, so texture completeness
  is not the cause.
- It is an EasyGL defect newly made visible, not a regression. EasyGL is the reference and outside
  this workstream, so it is recorded, not changed.

**The remaining 68 OpenGL4 skips:**

| skips | reason | where it is handled |
|---|---|---|
| 31 | "lit shaders do not sample shadow maps" | later B task |
| 15 | clustered effect | later B task |
| 12 | indirect draw | later B task |
| 6 | GPU timer | later B task |
| 1 | Color storage image (format usage unclassified) | later B task |
| 1 + 1 + 1 | refusal paths for capabilities OpenGL4 has, and SPIR-V-only intake | stay skipped |

## GL4-0026 — Indirect draws and base-instance drawing (B12, B15)

- **Indirect draws.** `DrawPrimitivesIndirectEXT` and `DrawIndexedPrimitivesIndirectEXT` issue
  `glDrawArraysIndirect`/`glDrawElementsIndirect` from the argument buffer's GL name.
  - They take the instanced route's stream configuration, because the record always carries an
    instance count: per-instance streams, multi-stream custom effects, integer shader inputs,
    `REMED-GFX-218` validation before the VAO is touched, and restoration of every location claimed.
  - The indirect-buffer binding is put back afterwards.
  - A compiled XNA effect is refused, as on EasyGL, because its passes carry the primitive count this
    route reads from GPU memory.
  - Nothing reads the counts on the CPU. A dispatch that wrote them ended with a barrier covering
    command reads (`GL4-0025`).
  - `SupportsIndirectDrawEXT` is the native fact (core 4.0, so every accepted context once the entry
    points resolve). `IndirectArguments`-only buffers are therefore creatable without compute.
- **Base instance.** `SupportsBaseInstanceDrawingEXT` is the native 4.2 fact. A non-zero
  `firstInstance` draws through `glDrawElementsInstancedBaseVertexBaseInstance`, on the stock route
  and on the compiled-effect route alike. GL's base instance offsets every per-instance attribute's
  fetch, which is exactly Vulkan's contract (`vulkan_shader_effect_3d_test` check J);
  `gl_InstanceID` still counts from zero.
- **Test** `OpenGL4BaseInstanceTests.cpp`. No renderer-neutral suite draws through base instance —
  the shared case only observes a refusal on a mock. This test draws one instance from logical
  instance 0, 1 and 2 and requires that instance's per-instance colour on all 16 pixels, and a
  negative first instance is refused.
  - **Mutation check:** with the base-instance call disabled, instances 1 and 2 fail.
  - A first instance *past* the stream is not refused on OpenGL4, deliberately: like every XNA draw
    range it reaches the native API unvalidated, because
    `RequiresManagedBufferedDrawRangeValidationEXT` is false (`GL4-0011`).
  - While writing the test, a trap: `Color` is a 24-byte polymorphic object, so `SetDataRaw` on a
    `Color` array uploads vtable bytes. The stream receives packed values.

**Results:**

| Suite | GL4-0025 | GL4-0026 |
|---|---|---|
| `CnaGraphicsExtTests` OPENGL4 | 893 / 1 / 68 | **904 / 1 / 57** — all twelve indirect cases run; one of them is now the capability's refusal leg ("does support indirect drawing") |
| CNAEXT oracles OPENGL4 | 22 / 1 / 10 | 23 / 1 / 9 — `CNAEXT_GpuDriven` (GPU culling → indirect draw) passes |
| `CnaGraphicsTests` capability/profile/instancing/indirect/limit subset | — | 200 / 0 / 2 |
| `[OpenGL4 GL Error]` lines | 0 | 0 |
