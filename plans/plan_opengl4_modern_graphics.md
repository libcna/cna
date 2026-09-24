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
| Workstream A | in progress |
| Workstream B | not started (gated on A) |

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
