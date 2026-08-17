# plan_modern.md — CNA Modern Engine Layer (`CNA::Graphics`, `CNA_CNAEXT`)

> **Base document:** [`CNAEXT.md`](CNAEXT.md). That file is the *final design*; this file is its
> *executable task backlog*. Every task below traces back to a CNAEXT.md section (`→ §5.1`) or to
> one of its coarse `N`-tasks (`→ N20`). Where this plan contradicts CNAEXT.md, the contradiction
> is listed in §0.2 and this file wins (CNAEXT.md is updated in `MOD-23`).
>
> **Scope:** the *engine-orchestration half* of CNAEXT — float/HDR render targets, a
> `RenderPipeline`, post-processing passes, shadow maps, skybox + IBL, geometry helpers, and
> compute/storage buffers — all in `namespace CNA::Graphics`, all gated by `CNA_CNAEXT`, all
> renderer-agnostic (GPU access only through `IGraphicsRenderer` / `Effect` / `RenderTarget2D`).
>
> **Out of scope** (explicitly, per CNAEXT.md §5.5/§9): node-based material graphs, a scene graph,
> an editor, an ECS, physics, a new asset format, and any change to the XNA 4.0 public contract that
> is not an always-compiled `CNAEXT`/`*EXT` marker member.
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criterion*;
> 🟨 code exists but the criterion is not met/verified; ⬜ not started; ⛔ deliberately not done
> (with a reason). Every task below is ⬜ unless marked otherwise.
>
> **Created:** 2026-08-17. **Branch:** `claude/plan-from-cnaext-m8cjop`.

---

## 0. Reality check — what the repository actually contains today

### 0.1 Verified starting state (checked against the tree, not against CNAEXT.md)

| Fact | Evidence |
|---|---|
| `CNA_CNAEXT` is a real CMake option and is injected as a compile definition | `modules/CMakeLists.txt:106` (`$<$<BOOL:${CNA_CNAEXT}>:CNA_CNAEXT>`) |
| The engine layer lives in **`modules/graphics-ext/`**, not in a top-level `include/CNA/Graphics` | `modules/graphics-ext/{include/CNA/Graphics,src,tests,examples}` |
| Existing `CNA::Graphics` types | `RenderPipelineSettings`, `PbrMaterial`, `TonemappingMode`, `RenderQuality`, `ShadowQuality`, `DepthEffect`(+`DepthEffectMode`,`DitherMode`), `CRTEffect`(+`CRTMaskType`), `AsciiPostProcessEffect`(+`AsciiQuantizeMode`) |
| `RenderPipelineSettings` has **no consumer** — `GraphicsDevice` does not expose it | no `GetRenderPipelineSettings` anywhere in `GraphicsDevice.hpp` |
| `CreateRenderTarget2DEXT(w,h,depthFormat,preserve,mipMap,msaa,surfaceFormat)` **already exists** | `IGraphicsRenderer.hpp:1730` (added by `SKIA-142`) |
| `RenderTarget2D` **already routes** its `SurfaceFormat` into that call | `modules/graphics/src/Xna/RenderTarget2D.cpp:69` |
| …but **no 3D renderer overrides it** — EasyGL/Vulkan/Bgfx/SdlGpu/WebGPU/D3D* all fall through to the format-ignoring `CreateRenderTarget2D` | only Skia, Direct2D, HtmlDom, Fna3d, Metal override it today |
| `GraphicsCapability` has 14 enumerators; **none** of `FloatRenderTargets` / `ComputeShaders` / `StorageBuffers` / `SeamlessCubeMapFilter` exists | `modules/graphics/include/CNA/GraphicsCapability.hpp` |
| `include/CNA/Graphics/CNAEXT.hpp` master include **does not exist** | `find modules -name CNAEXT.hpp` → empty |
| Bloom/shadow GLSL exists only as **example code**, not library code | `modules/renderers/easygl/examples/easygl_bloom_{extract,gaussianblur,combine,pipeline}_test.cpp`, `easygl_shadowmapping_*` |
| `ShaderEffect` (runtime GLSL `Effect`) and `RenderTarget2D`/`RenderTargetCube` with mip+MSAA resolve are available and tested | `modules/graphics/{include,src}/…/ShaderEffect.*` |
| `GpuDrawParams` is the single struct carrying per-draw shading state to renderers | `IGraphicsRenderer.hpp:848` |

### 0.2 Corrections this plan makes to CNAEXT.md

| # | CNAEXT.md says | Reality / decision |
|---|---|---|
| C1 | §5.0 proposes a **new** `CreateRenderTarget2DEx` virtual | It already exists as `CreateRenderTarget2DEXT` (uppercase, `SKIA-142`). **Do not add a second virtual** — implement the existing one in the 3D renderers. |
| C2 | §5.0 calls the plumbing "small but required: route `SurfaceFormat` into the factory" | Already done in `RenderTarget2D.cpp`. The remaining work is 100 % renderer-side (Phase 1). |
| C3 | §7 file layout says `include/CNA/Graphics/` + `src/CNA/Graphics/` | Actual layout is `modules/graphics-ext/include/CNA/Graphics/` + `modules/graphics-ext/src/` (flat, single-area module). Include spelling `"CNA/Graphics/X.hpp"` is unchanged. |
| C4 | §8 `N05` "CNAEXT.hpp master include" | Confirmed missing → `MOD-1`. |
| C5 | §5.1 method names are `lowerCamelCase` (`begin`, `end`, `resize`) | Kept — `CNA::Graphics` is **not** the XNA namespace, and the existing `CNA::Graphics` classes (`RenderPipelineSettings::isHDREnabled`, `PbrMaterial`) already use the `getXProperty`/`isX`/verb-lowercase style. See `MOD-10` for the written-down rule. |
| C6 | §5.3 shows `ShadowMap::begin(light, sceneBounds)` taking `DirectionalLightEXT` | No such type exists in the tree. Decision: introduce `CNA::Graphics::DirectionalLightEXT` as a small engine-layer struct (`direction`, `color`, `intensity`, `castsShadows`), **not** a new XNA type (`MOD-800`). |
| C7 | §5.7 shows `StorageBuffer<T>` as a template | Templates cannot live in `.cpp`; the type is split into a non-template `StorageBuffer` (bytes, in `.cpp`) plus a thin `StorageBufferT<T>` header template (`MOD-1520`). |
| C8 | §5.2 `PostProcessPass::apply(Texture2D* source, RenderTarget2D* destination)` | Extended to `apply(const PostProcessContext&)` so passes that need depth/normals (SSAO) and settings do not need a second, incompatible entry point (`MOD-200`). |

### 0.3 Definition of Done (applies to **every** task in this file)

A task is ✅ only when all of these hold:

1. `// SPDX-License-Identifier: MS-PL` on every new `.hpp` and `.cpp`.
2. Full Doxygen (`@brief`/`@param`/`@return`) on **every** public member (CLAUDE.md rule, verbatim).
3. Engine-layer files fully wrapped in `#ifdef CNA_CNAEXT` … `#endif // CNA_CNAEXT`.
4. Builds clean **both** with `-DCNA_CNAEXT=ON` and `-DCNA_CNAEXT=OFF`.
5. Google Test coverage for every new public method/overload/operator/constant in
   `modules/graphics-ext/tests/CNA/Graphics/` (or the owning module's `tests/`).
6. No new production `.cpp` outside a module directory (the source-partition validator).
7. One task = one commit, message `feat(MOD-nnn): …` / `fix(MOD-nnn): …`, staged by explicit filename.
8. The task's row in this file is flipped to ✅ **in the same commit**, with the acceptance evidence.

---

## 1. Architecture decisions locked in by this plan

| ID | Decision |
|---|---|
| D1 | **No renderer is mandatory.** Every subsystem checks `GraphicsDevice::SupportsCapability()` and has a documented fallback: post-process → silent pass-through blit; shadows → unshadowed render; IBL → the existing flat `AmbientLightColor`; compute → `std::runtime_error` with a renderer-named message. |
| D2 | **Renderer boundary stays enum-free.** New `IGraphicsRenderer` virtuals take `int` ordinals of XNA enums, mirroring `CreateRenderTarget2DEXT`. |
| D3 | **New virtuals always ship with a safe default** so all 47 renderer families keep compiling untouched. |
| D4 | **EasyGL is the reference implementation** for every subsystem; per-renderer follow-ups are separate tasks (Phase 16) and never block the reference landing. |
| D5 | **Per-object shading extensions stay in the XNA namespace** as always-compiled `CNAEXT`/`*EXT` members (shadow receiving, IBL binding). Only frame-level orchestration is `CNA_CNAEXT`-gated. |
| D6 | **Shaders are GLSL-first** (ES 3.0 profile, the `ShaderEffect` contract). Renderers with other shading languages translate in their own follow-up tasks, exactly like the PBR rollout did. |
| D7 | **No new asset/file format.** IBL products are generated at runtime from a `TextureCube`; nothing is baked offline in this plan. |
| D8 | **HDR is opt-in.** `RenderPipelineSettings::isHDREnabled() == false` (today's default) must produce a pipeline that is byte-identical to rendering without `RenderPipeline` at all, except for the extra blit. |
| D9 | **Every pass is independently usable.** `BloomPass`/`TonemapPass`/… work standalone on any `Texture2D`, without `RenderPipeline`. `RenderPipeline` is a convenience orchestrator, not a required owner. |
| D10 | **No dynamic allocation per frame** in pass `apply()` paths — targets/effects are allocated in `resize()`/construction and reused. |
| D11 | **Runtime renderer selection is respected.** `next` can build several renderer families into one binary and choose at run time (`CNA_GRAPHICS_RENDERERS`, `docs/runtime-renderer-selection.md`). Engine-layer code must therefore ask the *live* device (`GraphicsDevice::SupportsCapability()`), never a compile-time `CNA_RENDERER_*` macro, and must tolerate the answer differing between two devices in one process. |

---

## 2. Phase overview

| Phase | Range | Subject | Depends on | Tasks |
|---|---|---|---|---|
| 0 | `MOD-1`–`MOD-24` | Foundation, conventions, build wiring | — | 24 |
| 1 | `MOD-100`–`MOD-141` | Float / HDR render targets | 0 | 35 |
| 2 | `MOD-200`–`MOD-233` | Fullscreen-pass infrastructure | 1 | 26 |
| 3 | `MOD-300`–`MOD-320` | Tonemapping | 2 | 21 |
| 4 | `MOD-400`–`MOD-418` | Bloom | 2, 3 | 19 |
| 5 | `MOD-500`–`MOD-529` | Depth/normal prepass + SSAO | 2 | 23 |
| 6 | `MOD-600`–`MOD-610` | FXAA and post-AA | 2 | 11 |
| 7 | `MOD-700`–`MOD-745` | `RenderPipeline` orchestrator | 1–6 | 36 |
| 8 | `MOD-800`–`MOD-861` | Directional shadow maps + receiver hooks | 1, 2 | 40 |
| 9 | `MOD-900`–`MOD-917` | Cascaded shadow maps | 8 | 18 |
| 10 | `MOD-1000`–`MOD-1012` | Point/spot shadows | 8 | 13 |
| 11 | `MOD-1100`–`MOD-1116` | Skybox | 2 | 17 |
| 12 | `MOD-1200`–`MOD-1248` | IBL (irradiance, prefilter, BRDF LUT) | 1, 11 | 32 |
| 13 | `MOD-1300`–`MOD-1315` | Material system reconciliation | 12 | 16 |
| 14 | `MOD-1400`–`MOD-1414` | Instancing, LOD, culling helpers | — | 15 |
| 15 | `MOD-1500`–`MOD-1555` | Compute shaders + storage buffers | 0 | 24 |
| 16 | `MOD-1600`–`MOD-1698` | Per-renderer rollout matrix | 1–15 | 84 |
| 17 | `MOD-1700`–`MOD-1741` | Tests, golden images, CI | per subsystem | 22 |
| 18 | `MOD-1800`–`MOD-1813` | Documentation, examples, demos | per subsystem | 14 |
| 19 | `MOD-1900`–`MOD-1907` | API stabilization / Nova-3D readiness | all | 8 |

**Total: 498 tasks.** IDs are deliberately sparse inside each phase so follow-up work discovered
during implementation gets a free neighbouring number instead of a renumbering pass.

**Critical path to a first usable HDR frame:** `MOD-1` → `MOD-100`–`MOD-107` → `MOD-200`–`MOD-210`
→ `MOD-300`–`MOD-305` → `MOD-700`–`MOD-712`. Everything else is parallelizable behind that spine.

---

## Phase 0 — Foundation, conventions, build wiring (`MOD-1`–`MOD-24`)

Nothing renders in this phase; it makes the engine layer buildable, testable, and documented so
every later phase has one place to add a header, a test, and a doc line.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1 | Create `modules/graphics-ext/include/CNA/Graphics/CNAEXT.hpp` master include (→ N05, §7) | ✅ | Done. `modules/graphics-ext/include/CNA/Graphics/CNAEXT.hpp` includes all 12 public headers; `CnaExtMasterIncludeTests` (4 cases) passes in `cmake-build-cnaext`; a TU including only this header without `CNA_CNAEXT` compiles to an object file with 0 symbols. |
| MOD-2 | Add a `cnaext` CMake preset (`cmake-build-cnaext`, `OPENGLES3`, `CNA_CNAEXT=ON`, tests ON) | ✅ | Done. `cmake --preset cnaext` configures and `cmake --build --preset cnaext --target CnaTests` links. Note: the environment also needs the sibling checkouts `../sharp-runtime`, `../easy-gl`, `../meta-gl` and the system packages listed in `NEXT_modern.md`. |
| MOD-3 | Verify a clean `CNA_CNAEXT=OFF` build is unaffected by the whole module (guard audit) | ✅ | Done. `scripts/check_cnaext_guards.sh` passes on the tree (27 files) and fails on an unguarded header and on an `add_executable()` registered outside a `CNA_CNAEXT` gate. CTest registration is MOD-1740. |
| MOD-4 | `docs/cnaext-engine-layer.md` skeleton — capability boundary, layer diagram, per-renderer support matrix stub | ✅ | Done. `docs/cnaext-engine-layer.md` covers the two meanings of CNAEXT, what exists today, the layer conventions, and the per-renderer support matrix. Linking it from `docs/README.md` and CLAUDE.md moves to MOD-1807. |
| MOD-5 | Register `modules/graphics-ext/tests/` GTest target under `CNA_CNAEXT=ON` and confirm it is a no-op when OFF | ⬜ | `ctest -R CnaExt` runs the existing `DepthEffect`/`CRTEffect`/`Ascii*` tests in the `cnaext` build; the default build registers zero of them without a CMake warning. |
| MOD-6 | Decide + document the engine-layer naming convention (verbs `lowerCamel`, properties `getX`/`setX`/`isX`) (→ C5) | ⬜ | Written into `docs/cnaext-engine-layer.md` and CLAUDE.md; a review of the 3 existing classes confirms consistency (no renames needed). |
| MOD-7 | Add `CNA::Graphics` Doxygen group (`@defgroup cnaext_engine`) so the layer is one navigable page | ⬜ | `doxygen Doxyfile` produces a "CNA Engine Layer" module page listing every `CNA::Graphics` type; no undocumented-member warnings for the module. |
| MOD-8 | Introduce `CNA/Graphics/EngineLayerVersion.hpp` (`CNA_CNAEXT_ENGINE_VERSION` int + `getEngineLayerVersion()`) | ⬜ | Version constant starts at 1; documented as "not an ABI guarantee" per CNAEXT.md §9; test asserts the value and the string form. |
| MOD-9 | `CNA::Graphics::EngineException` (derives `System::Exception` from sharp-runtime) for engine-layer failures | ⬜ | Thrown by every "renderer cannot do this" path in later phases; message format `"<Subsystem>: <what> is not supported by the <RendererName> renderer"`; tested for message shape. |
| MOD-10 | `CNA::Graphics::detail::RequireCapability(device, capability, subsystem)` helper | ⬜ | Single choke point that either returns, or throws `EngineException` with the renderer name resolved from `GraphicsRendererType`; unit-tested with a fake device on Headless. |
| MOD-11 | Extend `modules/graphics-ext/CMakeLists.txt` for the new subdirectory-free source glob + `cna_graphics_ext` deps (needs `cna_graphics_core` only) | ⬜ | New sources land without CMake edits; the module still links no renderer target directly (verified by `cmake --graphviz`). |
| MOD-12 | Add `audit/` stub entries for every new header/source the plan creates | ⬜ | `audit/include/CNA/Graphics/*.audit.md` present for each new file when it lands; enforced in each task's own checklist, seeded here for existing files. |
| MOD-13 | Teach `loc.sh` to report the engine layer separately (`graphics-ext` already a module — verify) | ⬜ | `./loc.sh` prints a `graphics-ext` row with include/src/tests/examples splits. |
| MOD-14 | `NEXT_modern.md` — running "what to pick up next" ledger for this plan | ✅ | Done. `NEXT_modern.md` carries the current position on the critical path, the owner decisions in force, the full fresh-container build recipe (submodules, the three sibling checkouts, the system packages), and the test baseline. |
| MOD-15 | Decide the engine layer's minimum shader profile and write it down (GLSL ES 3.00 baseline, ES 3.10 for compute) | ⬜ | Documented in `docs/cnaext-engine-layer.md`; every pass shader in later phases starts with the agreed `#version` handling delegated to `ShaderEffect`. |
| MOD-16 | Survey `ShaderEffect`'s uniform API and record the gaps the passes will need (`SetUniformMatrix`, `int`, arrays, samplers ≥2) | ⬜ | Appendix B is filled in; every gap becomes its own task in Phase 2 rather than being discovered mid-pass. |
| MOD-17 | Survey `RenderTarget2D`'s public surface for what the pipeline needs (format query, depth query, `RenderTargetUsage`) | ⬜ | Documented; any missing accessor becomes a Phase 1 task. |
| MOD-18 | Confirm `SpriteBatch` can draw a `RenderTarget2D` as a source texture with a custom `Effect` on the reference renderer | ⬜ | A minimal example proves source→effect→destination works today; this is the fallback path when the fullscreen-triangle helper is unavailable. |
| MOD-19 | Choose and document the fullscreen-geometry strategy (single oversized triangle via `DrawUserPrimitives`, no vertex buffer) | ⬜ | Decision recorded with the reason (no VB lifetime, no UV seam, one fewer draw); implemented in `MOD-201`. |
| MOD-20 | Define `CNA::Graphics::PostProcessContext` fields up front so all passes share one signature (→ C8) | ⬜ | Struct declared in Appendix A and implemented in `MOD-200`; contains source colour, depth, normals, destination, viewport size, settings, elapsed time. |
| MOD-21 | Add the `Uncharted2` value to `TonemappingMode` (→ N02) | ⬜ | Enum value appended (never inserted); existing tests still pass; new test asserts ordinal stability of the pre-existing values. |
| MOD-22 | Extend `RenderPipelineSettings` with the fields the passes read (→ N03/§5.1) | ⬜ | `bloomThreshold`, `bloomRadius`, `ssaoRadius`, `ssaoIntensity`, `ssaoSampleCount`, `fxaaEnabled`, `hdrFormat` accessors added with defaults; every new accessor unit-tested for default + round-trip. |
| MOD-23 | Update `CNAEXT.md` §5.0/§7/§8 with corrections C1–C8 and a pointer to this plan | ⬜ | CNAEXT.md no longer proposes a duplicate `CreateRenderTarget2DEx`, its file-layout block matches the module layout, and its `N`-table links to the `MOD` ranges. |
| MOD-24 | Add this plan to the repo's plan index (`plan.md` / `README.md` plan list) | ⬜ | `plan_modern.md` discoverable from the same place the other 60+ plan files are listed. |

---

## Phase 1 — Float / HDR render targets (`MOD-100`–`MOD-141`)

→ CNAEXT.md §5.0, N10–N12. The single hard prerequisite for HDR, bloom, SSAO, and IBL. The XNA-side
plumbing is already done (C2); this phase is capability reporting + renderer implementations +
verification that a float target actually stores values above 1.0.

### 1.1 Capability and shared layer

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-100 | Add `GraphicsCapability::FloatRenderTargets` with a full Doxygen paragraph in the established style | ✅ | Done (landed together with MOD-101/103/104 — the capability cannot be truthful without its opt-in mechanism). Answered by `GraphicsDevice` from the renderer per-format query, following the `CompiledEffects` precedent, because most renderer capability switches end in `default: return true`. 8 tests in `GraphicsCapabilityFloatRenderTargetTests` pass on EasyGL/OPENGLES3. |
| MOD-101 | Add `GraphicsCapability::HalfFloatRenderTargets` (16-bit is far more widely supported than 32-bit; conflating them would strand GLES 3.0) | ✅ | Done. `HalfFloatRenderTargets` appended as its own enumerator, derived from `HdrBlendable`; the GLES-3.0 half-float-only case is exactly why it is separate. |
| MOD-102 | Add `GraphicsCapability::ComputeShaders`, `StorageBuffers`, `SeamlessCubeMapFilter` (→ N10) | ⬜ | All appended, all default `false`, all documented with the renderers known to lack them. |
| MOD-103 | `GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat)` — per-format query above the coarse capability | ✅ | Done. `GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat)` added; tested for `Color` (true everywhere) and the 7 float formats (false until a renderer implements them). |
| MOD-104 | Renderer virtual `SupportsRenderTargetFormat(int surfaceFormat)` with a default that accepts only `Color` | ✅ | Done. `IGraphicsRenderer::SupportsRenderTargetFormat(int)` with a `Color`-only default — the literal truth for every renderer that leaves `CreateRenderTarget2DEXT` at its forwarding default. All 47 renderer families compile unchanged. |
| MOD-105 | `RenderTarget2D` — fail loudly instead of silently downgrading when the requested format is unsupported | ⬜ | Constructing an `HdrBlendable` target on a `Color`-only renderer throws a clear message naming the renderer and the format; existing `Color` construction unchanged. |
| MOD-106 | Add an opt-out (`RenderTarget2D` static `SetUnsupportedFormatPolicyEXT(Throw\|Downgrade)`) so ports can keep the old lenient behavior | ⬜ | Default is `Throw`; `Downgrade` reproduces today's behavior and logs once; both tested. |
| MOD-107 | Route `SurfaceFormat` into `RenderTargetCube` the same way `RenderTarget2D` already does | ⬜ | New `CreateRenderTargetCubeEXT(..., int surfaceFormat)` virtual with a forwarding default; `RenderTargetCube` passes its real format; needed by IBL prefiltering. |
| MOD-108 | `Texture2D::GetData<Vector4>` / float readback path for verifying HDR contents in tests | ⬜ | Reading back an `HdrBlendable`/`Vector4` target yields values >1.0 unclamped on the reference renderer; throws a clear message where unsupported. |
| MOD-109 | Document the exact `SurfaceFormat` → GL/VK/D3D internal-format mapping table once, in `docs/cnaext-engine-layer.md` | ⬜ | One table covering `Single`, `Vector2`, `Vector4`, `HalfSingle`, `HalfVector2`, `HalfVector4`, `HdrBlendable`; every renderer task in Phase 16 references it instead of inventing its own. |
| MOD-110 | Decide and document what `HdrBlendable` means in CNA (XNA: `RGBA16F` on Windows) | ⬜ | Documented as an alias of `HalfVector4` for every renderer; test asserts the two behave identically. |
| MOD-111 | Blending semantics on float targets — document that alpha blending on `HalfVector4` is allowed and on `Single`/`Vector2` is renderer-dependent | ⬜ | Documented; `SupportsCapability` note updated; no runtime enforcement (matches XNA's own laxity). |

### 1.2 EasyGL reference implementation

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-115 | EasyGL: override `CreateRenderTarget2DEXT` and honour the surface format (→ N11) | ⬜ | An `HdrBlendable` target creates a `GL_RGBA16F` FBO colour attachment; `Color` targets are bit-identical to today. |
| MOD-116 | EasyGL: `SurfaceFormat` → `(internalFormat, format, type)` table for the 7 float formats + `Color` | ⬜ | Table is data, not a switch chain; unit-tested directly. |
| MOD-117 | EasyGL: runtime probe of float-renderable support (`GL_EXT_color_buffer_float` / ES 3.2 core / desktop GL 3.3) | ⬜ | `SupportsCapability(FloatRenderTargets)` reflects the real context, not the compile-time profile; logged once at startup. |
| MOD-118 | EasyGL: `GL_EXT_color_buffer_half_float` probe for the half-float-only case (GLES 3.0 devices) | ⬜ | `HalfFloatRenderTargets` true, `FloatRenderTargets` false on such a context; both paths exercised by a forced-probe unit test. |
| MOD-119 | EasyGL: framebuffer-completeness check after attaching a float target, with a diagnostic on failure | ⬜ | An incomplete FBO throws a message naming the format and the GL status enum instead of rendering silently to nowhere. |
| MOD-120 | EasyGL: float target + depth attachment combination (depth24/depth24stencil8 with RGBA16F colour) | ⬜ | Depth testing works when drawing 3D into an HDR target; verified by a depth-occlusion example. |
| MOD-121 | EasyGL: MSAA resolve from a multisampled float target | ⬜ | 4× MSAA `HalfVector4` target resolves without GL errors; falls back with a documented message where the driver refuses. |
| MOD-122 | EasyGL: mip generation on float targets | ⬜ | `mipMap=true` on an HDR target produces a full chain; needed by IBL prefiltering (Phase 12). |
| MOD-123 | EasyGL: float texture sampling (linear filter) capability probe (`OES_texture_float_linear`) | ⬜ | Reported through a documented accessor; bloom's downsample chain reads it and falls back to nearest + more taps. |
| MOD-124 | EasyGL: readback of a float target into `Vector4[]` | ⬜ | `glReadPixels` with `GL_FLOAT` returns unclamped values; `MOD-108`'s test passes on EasyGL. |
| MOD-125 | EasyGL: `SetRenderTargets` (MRT) with mixed float/`Color` attachments | ⬜ | A 2-attachment set (`HalfVector4` colour + `Color` normals) binds and draws; used by the SSAO prepass (Phase 5). |

### 1.3 Verification

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-130 | Unit tests: capability enum values, per-format query, throw/downgrade policy | ⬜ | Every new public method from §1.1 covered including both policy branches. |
| MOD-131 | Example `cnaext_float_rendertarget_test` — clear an HDR target to (4,2,1,1), read back, assert >1.0 | ⬜ | Registered as a ctest under the `cnaext` build; passes on EasyGL, skips with a clear message on `Color`-only renderers. |
| MOD-132 | Example: HDR target → LDR blit shows the expected clamped colour on screen | ⬜ | Visual test committed under `modules/graphics-ext/examples/`; documented expected output. |
| MOD-133 | Regression: `Color` render targets behave identically before/after the phase (golden image) | ⬜ | An existing EasyGL render-target golden test is re-run and matches byte-for-byte. |
| MOD-134 | Regression: all 47 renderer families still compile with the new virtuals (configure sweep) | ⬜ | A script configures each `CNA_GRAPHICS_RENDERER` value that is buildable in this environment and reports the rest as skipped-by-toolchain, not as failures. |
| MOD-135 | Headless: `FloatRenderTargets=false` and a clear throw from `RenderTarget2D` | ⬜ | Documented as the reference "renderer cannot do this" behavior for D1. |
| MOD-136 | Software renderer: decide `Color`-only and document it | ⬜ | `⛔` with a written reason (CPU raster, no float blending path planned in this plan). |
| MOD-137 | Stub renderer: accepts and ignores, reports `false` | ⬜ | Consistent with its existing contract; test asserts no throw from the renderer itself. |
| MOD-138 | Perf note: measure HDR target cost (fill rate + memory) at 1280×720 on the reference renderer | ⬜ | Numbers recorded in `docs/cnaext-engine-layer.md` so `RenderQuality` presets can be justified rather than guessed. |
| MOD-139 | Memory accounting: expose `RenderPipeline`-owned target bytes for diagnostics | ⬜ | A single `getGpuMemoryEstimateBytes()` on the pipeline (Phase 7) is fed by a helper landed here; tested arithmetically. |
| MOD-140 | Update `docs/cnaext-engine-layer.md` float-RT support matrix | ⬜ | Matrix lists all 44 renderers with ✅/⬜/⛔ and a one-line reason for every ⛔. |
| MOD-141 | Flip `N10`/`N11` in CNAEXT.md §8 with a pointer to this phase | ⬜ | CNAEXT.md's status table no longer diverges from reality. |

---

## Phase 2 — Fullscreen-pass infrastructure (`MOD-200`–`MOD-233`)

→ CNAEXT.md §5.2. Everything the four concrete passes share: the context struct, the abstract base,
a fullscreen-triangle drawer, a ping-pong target pool, and the `ShaderEffect` gaps found in `MOD-16`.

### 2.1 Core types

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-200 | `PostProcessContext` struct (`source`, `sourceDepth`, `sourceNormals`, `destination`, `width`, `height`, `settings`, `elapsedSeconds`) | ⬜ | Header-only aggregate, fully documented; every later pass takes exactly this. |
| MOD-201 | `detail::FullscreenTriangle` — one oversized triangle via `DrawUserPrimitives`, no VB (→ MOD-19) | ⬜ | Draws with the given `Effect`; covers the viewport exactly; unit-tested by a solid-colour fill readback. |
| MOD-202 | `PostProcessPass` abstract base — `apply(ctx)`, `getName()`, `isSupported(device)` | ⬜ | Pure-virtual `apply`; `isSupported` default checks `CustomEffects`; documented as the D9 "standalone usable" contract. |
| MOD-203 | `PostProcessPass` common helper: bind destination (null = backbuffer), set viewport, restore previous target on scope exit | ⬜ | RAII helper; a pass that throws mid-apply still restores the previous render target (tested). |
| MOD-204 | `detail::RenderTargetPool` — reusable, size/format-keyed target cache with explicit `reset()` | ⬜ | Second request of the same key returns the same object; `reset()` frees; no allocation in steady state (D10, asserted by a counter in tests). |
| MOD-205 | `detail::PingPongTargets` — 2 targets of identical size/format with `swap()`, built on the pool | ⬜ | Chained passes alternate correctly; tested with a 3-pass identity chain that must reproduce the input. |
| MOD-206 | `detail::BlitPass` — the identity/pass-through pass used by every "capability missing" fallback (D1) | ⬜ | Copies source to destination with correct flip/UV orientation; golden-image verified. |
| MOD-207 | Document the texture-coordinate origin convention once (GL bottom-left vs D3D top-left) and where the flip is applied | ⬜ | Written in `docs/cnaext-engine-layer.md`; `BlitPass` is the single place the convention is realized; every pass shader inherits it. |
| MOD-208 | `PostProcessChain` — ordered container of passes with ping-pong management and one final resolve | ⬜ | `add(pass)`, `apply(ctx)`; N passes produce N-1 intermediate blits, not 2N; asserted by a counting fake pass. |
| MOD-209 | Pass-level enable flags driven by `RenderPipelineSettings` (no branch inside shaders) | ⬜ | A disabled pass is skipped entirely (zero draw calls), verified by the counting fake. |
| MOD-210 | `ShaderEffectFactory` helper — compiles a named pass shader once and caches it per device | ⬜ | Two `BloomPass` instances on one device share one compiled program; verified by a compile counter. |

### 2.2 `ShaderEffect` gaps (each becomes real work only if `MOD-16` finds it missing)

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-215 | `ShaderEffect::SetUniformMatrix` (4×4, column-major) if absent | ⬜ | Round-trips through the reference renderer; needed by shadows and skybox. |
| MOD-216 | `ShaderEffect` multi-sampler binding (≥4 texture units with named samplers) | ⬜ | Bloom composite (2 samplers) and SSAO (depth+normal+noise = 3) both work. |
| MOD-217 | `ShaderEffect::SetUniformFloatArray` / `Vector2Array` / `Vector4Array` | ⬜ | SSAO's 16–64-sample kernel uploads as one array, not 64 scalars. |
| MOD-218 | `ShaderEffect::SetUniformInt` (sample counts, mode switches) | ⬜ | Used by `TonemapPass` mode selection. |
| MOD-219 | `ShaderEffect` compile-error surfacing with the shader name and line context | ⬜ | A deliberately broken pass shader throws a message containing the pass name and the GLSL log. |
| MOD-220 | `ShaderEffect` — allow a pass to declare its own sampler filtering/addressing requirements | ⬜ | Bloom's linear-clamp requirement is expressed once by the pass, not by the caller's `SamplerState`. |

### 2.3 Verification

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-225 | Unit tests for `RenderTargetPool` (keying, reuse, reset, leak count) | ⬜ | All branches covered including a format change forcing reallocation. |
| MOD-226 | Unit tests for `PingPongTargets` and `PostProcessChain` ordering | ⬜ | A 4-pass chain applies in insertion order (proved by a pass that writes its index). |
| MOD-227 | Unit tests for `PostProcessPass` fallback behavior on a non-shader renderer | ⬜ | On Headless/SDL_Renderer, `isSupported()` is false and `apply()` performs a documented blit, never a throw. |
| MOD-228 | Golden image: `BlitPass` identity through an HDR intermediate | ⬜ | Source and result are bit-identical for `Color`; within 1 ULP-equivalent tolerance for `HalfVector4`. |
| MOD-229 | Example `cnaext_postprocess_chain_test` — 3 stacked trivial passes on a live window | ⬜ | Registered ctest; documented expected visual result. |
| MOD-230 | Perf: measure per-pass cost at 1280×720 on the reference renderer | ⬜ | Numbers in the doc; used to justify `RenderQuality` presets in Phase 7. |
| MOD-231 | Verify the existing `DepthEffect`/`CRTEffect`/`AsciiPostProcessEffect` can be adapted to `PostProcessPass` | ⬜ | Written analysis; if adaptation is clean, `MOD-232` does it; if not, the reason is recorded. |
| MOD-232 | Adapter: expose `DepthEffect`/`CRTEffect`/`AsciiPostProcessEffect` as `PostProcessPass` implementations | ⬜ | The three existing CNAEXT effects become usable inside `PostProcessChain`/`RenderPipeline` with no change to their own public APIs. |
| MOD-233 | `docs/cnaext-engine-layer.md`: "writing your own pass" section with a 20-line example | ⬜ | A reader can add a custom pass without reading the pass sources. |

---

## Phase 3 — Tonemapping (`MOD-300`–`MOD-327`)

→ CNAEXT.md §5.2, N21. The first real pass, and the one that makes HDR visible.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-300 | `TonemapPass` class skeleton (`PostProcessPass`, owns its `ShaderEffect`) | ⬜ | Constructs on the reference renderer, reports `isSupported()` correctly. |
| MOD-301 | Tonemap GLSL: `None` (clamp only) | ⬜ | Output equals `clamp(hdr,0,1)` before gamma; verified by float readback. |
| MOD-302 | Tonemap GLSL: `Reinhard` (`c/(1+c)`) | ⬜ | Matches the reference formula within 1/255 at 16 sampled luminances. |
| MOD-303 | Tonemap GLSL: `Filmic` (Hejl–Burgess-Dawson) | ⬜ | Matches the published curve within 1/255; documented that it bakes in gamma (so the gamma step is skipped for this mode). |
| MOD-304 | Tonemap GLSL: `Aces` (Narkowicz fit) | ⬜ | Matches the fit within 1/255; documented as the fit, not the full ACES transform. |
| MOD-305 | Tonemap GLSL: `Uncharted2` (→ MOD-21, N02) | ⬜ | Matches Hable's curve with the standard constants and the `W=11.2` white-point normalization. |
| MOD-306 | Exposure multiplier applied before the curve | ⬜ | `exposure=2` doubles the pre-curve luminance; verified numerically for every mode. |
| MOD-307 | Gamma applied after the curve (except `Filmic`, per `MOD-303`) | ⬜ | `gamma=2.2` yields the expected encode; a `gamma=1.0` path is exactly linear. |
| MOD-308 | Auto-exposure decision: **out of scope for v1**, documented | ⬜ | ⛔ with the reason (needs luminance reduction — revisit after compute lands, `MOD-1560`). |
| MOD-309 | `TonemapPass` reads `RenderPipelineSettings` (mode, exposure, gamma) each apply | ⬜ | Changing settings between frames changes output without reconstruction. |
| MOD-310 | Standalone use: tonemap any `Texture2D` to any `RenderTarget2D` or the backbuffer | ⬜ | D9 satisfied; example proves it without `RenderPipeline`. |
| MOD-311 | LDR input guard: tonemapping a `Color` source is legal and documented (values are simply ≤1) | ⬜ | No throw; documented as a no-op-ish path used by the `HDR=off` pipeline. |
| MOD-312 | Unit tests: all 5 modes × {exposure 0.5,1,4} × {gamma 1.0,2.2} numeric checks | ⬜ | 30 assertions driven by CPU reference implementations of the same curves. |
| MOD-313 | CPU reference implementations of the 5 curves (test-only helper) | ⬜ | Shared by `MOD-312` and the golden tests; itself unit-tested against hand-computed values. |
| MOD-314 | Golden image: HDR gradient tonemapped in each mode | ⬜ | 5 goldens committed with the generation command documented. |
| MOD-315 | Example `cnaext_tonemap_test` — live window, key-switchable mode/exposure | ⬜ | Registered ctest (smoke frames) + manual visual mode documented. |
| MOD-316 | Document the tonemapping contract (input linear HDR, output display-encoded) | ⬜ | In `docs/cnaext-engine-layer.md`; states explicitly that CNA does no colour-space management beyond gamma. |
| MOD-317 | `TonemappingMode` ordinal-stability test (existing values must not shift) | ⬜ | Asserts `None=0` … and `Uncharted2` last. |
| MOD-318 | Verify tonemap output against the 2D-only fallback (blit) so a `SpriteBatch` game sees no change | ⬜ | With HDR off and mode `None`, the pipeline output is bit-identical to no pipeline. |
| MOD-319 | Perf: tonemap cost at 720p/1080p | ⬜ | Recorded in the doc. |
| MOD-320 | Wire `RenderQuality` presets to tonemap precision decisions (none today — document why) | ⬜ | Explicit "quality does not affect tonemapping" note so the preset table has no silent gaps. |

---

## Phase 4 — Bloom (`MOD-400`–`MOD-431`)

→ CNAEXT.md §5.2, N22. Promotes the existing EasyGL example GLSL to library code.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-400 | Extract the bloom GLSL from `easygl_bloom_*` examples into a shared, documented source location | ⬜ | One header of GLSL string constants under `modules/graphics-ext/src/`; the examples still build (they may keep their own copies or switch — decided in the task). |
| MOD-401 | `BloomPass` skeleton + settings (threshold, intensity, radius, iterations) | ⬜ | Constructs, reports support, reads settings from `RenderPipelineSettings`. |
| MOD-402 | Bright-pass extract shader (threshold with soft knee) | ⬜ | Pixels below threshold produce 0; the knee is documented and numerically tested. |
| MOD-403 | Downsample chain (½ resolution per level, configurable level count 1–6) | ⬜ | Level count clamps to the target size; a 1×1 level is never created. |
| MOD-404 | Separable Gaussian blur (horizontal + vertical) with a documented tap count per quality | ⬜ | Blur kernel weights generated once, not hardcoded per size; verified against a CPU Gaussian. |
| MOD-405 | Upsample + additive combine chain | ⬜ | Progressive upsampling (each level adds into the next) rather than a single composite; visually verified. |
| MOD-406 | Final composite into the destination (`scene + bloom*intensity`) | ⬜ | `intensity=0` reproduces the input exactly (bit-identical for `Color`). |
| MOD-407 | Linear-filter fallback when float linear filtering is unavailable (`MOD-123`) | ⬜ | Nearest-sample path with extra taps; documented quality difference. |
| MOD-408 | LDR bloom path (works on `Color` targets when HDR is off) | ⬜ | Bloom is usable without HDR; documented as the "threshold ≤1" case. |
| MOD-409 | `RenderQuality` → level count / tap count mapping | ⬜ | Low/Medium/High/Ultra map to a documented table; test asserts each preset's derived values. |
| MOD-410 | Target pool reuse — all bloom mip targets come from `RenderTargetPool` | ⬜ | Steady-state allocations = 0 (D10). |
| MOD-411 | Resize handling — recreate the chain on viewport change without leaking | ⬜ | 100 resize cycles leave the pool size bounded (asserted). |
| MOD-412 | Unit tests: threshold/knee math, kernel weights, level-count clamping | ⬜ | CPU-reference-compared. |
| MOD-413 | Golden image: a bright quad on a dark field, 3 intensity values | ⬜ | 3 goldens; documented tolerance. |
| MOD-414 | Golden image: bloom disabled == input | ⬜ | Bit-identical assertion. |
| MOD-415 | Example `cnaext_bloom_test` — live window with adjustable threshold/intensity | ⬜ | Registered ctest; documented expected look. |
| MOD-416 | Perf: bloom cost per quality preset at 720p/1080p | ⬜ | Recorded; informs `MOD-409`'s table. |
| MOD-417 | Document bloom's energy behavior (not physically normalized; intensity is artistic) | ⬜ | Written down so the look is reproducible across renderers. |
| MOD-418 | Cross-check the promoted GLSL against the original example output | ⬜ | The library pass reproduces `easygl_bloom_pipeline_test`'s image within tolerance. |

---

## Phase 5 — Depth/normal prepass + SSAO (`MOD-500`–`MOD-539`)

→ CNAEXT.md §5.2, N23. SSAO needs scene depth and normals, which nothing in CNA currently exposes to
a post-process. Half of this phase is that plumbing; it is also what `MOD-1400`'s culling and future
depth-based effects will reuse.

### 5.1 Depth/normal availability

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-500 | Decide the strategy: **explicit depth+normal prepass into an MRT** vs sampling the depth attachment | ⬜ | Written decision with the reason (portability: not every renderer exposes its depth attachment as a texture); the loser is documented as a possible optimization. |
| MOD-501 | `DepthNormalPrepass` — renders the scene into `RG16F` linear depth + `Color` view-space normals | ⬜ | A helper the app drives (`begin`/`end` around its scene draw), mirroring `ShadowMap`'s shape. |
| MOD-502 | `DepthNormalEffect` (CNAEXT, XNA namespace) — the effect the prepass swaps in | ⬜ | Writes linear view depth and encoded view normal; documented encoding (`n*0.5+0.5`). |
| MOD-503 | Skinned variant of the prepass effect (bone transforms) so skinned meshes occlude correctly | ⬜ | Shares `SkinnedEffect`'s bone API (`MaxBones=72`). |
| MOD-504 | Linear-depth reconstruction helper GLSL (`viewZ` from the stored value, and world position from UV+depth) | ⬜ | One shared GLSL function used by SSAO and any later depth effect; unit-verified against a CPU reimplementation. |
| MOD-505 | Camera parameters (`near`, `far`, `projection`, `inverseProjection`) delivered to passes via `PostProcessContext` | ⬜ | Struct extended; documented that SSAO throws a clear message if they are unset. |
| MOD-506 | Renderer capability check for MRT (`MultipleRenderTargets`) with a documented 2-pass fallback | ⬜ | Where MRT is unavailable, depth and normals are rendered in two passes; both paths tested. |
| MOD-507 | Prepass target formats fall back to `Color`-packed depth when float RTs are missing | ⬜ | Packing/unpacking GLSL documented and numerically tested; SSAO quality difference noted. |

### 5.2 SSAO

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-515 | `SsaoPass` skeleton + settings (radius, intensity, bias, sample count, power) | ⬜ | Settings sourced from `RenderPipelineSettings` (`MOD-22`). |
| MOD-516 | Hemisphere sample-kernel generation (deterministic, seeded) | ⬜ | Same kernel every run for reproducible goldens; distribution unit-tested (all samples in the +Z hemisphere, density biased toward the origin). |
| MOD-517 | Rotation-noise texture (4×4 tiled) | ⬜ | Generated once per pass; contents documented and unit-tested. |
| MOD-518 | SSAO GLSL: occlusion estimate with range check | ⬜ | Flat surfaces yield ~1.0 (unoccluded); a corner yields <1.0; asserted numerically on a synthetic depth/normal input. |
| MOD-519 | Bilateral/box blur pass for the AO buffer | ⬜ | Noise visibly reduced; edge-preserving behavior asserted on a synthetic depth discontinuity. |
| MOD-520 | AO application mode: multiply into the scene's ambient term only, not into direct light | ⬜ | Documented; implemented as a composite pass so no effect shader changes are needed for v1. |
| MOD-521 | Optional: feed AO into `PbrEffect`'s occlusion slot instead of a screen-space multiply | ⬜ | Investigated and either implemented behind a flag or ⛔ with a reason. |
| MOD-522 | `RenderQuality` → sample count mapping (8/16/32/64) | ⬜ | Table documented and tested. |
| MOD-523 | Half-resolution AO option with upsample | ⬜ | Off by default; quality/perf difference measured and documented. |
| MOD-524 | Unit tests: kernel generation, noise texture, settings round-trip, capability fallback | ⬜ | All public members covered. |
| MOD-525 | Golden image: a sphere on a plane, AO visible in the contact region | ⬜ | Golden committed; tolerance documented (AO is noise-sensitive — the seeded kernel makes it deterministic). |
| MOD-526 | Golden image: SSAO disabled == input | ⬜ | Bit-identical. |
| MOD-527 | Example `cnaext_ssao_test` — live window, radius/intensity adjustable | ⬜ | Registered ctest. |
| MOD-528 | Perf: prepass + SSAO + blur cost per quality at 720p/1080p | ⬜ | Recorded. |
| MOD-529 | Document the whole depth/normal contract in `docs/cnaext-engine-layer.md` | ⬜ | Including what a game must do (drive the prepass) and what happens if it does not (SSAO throws a clear message, pipeline continues without AO). |

---

## Phase 6 — FXAA and post-AA (`MOD-600`–`MOD-619`)

→ CNAEXT.md §5.2, N24. Matters most on renderers that cannot MSAA a float render target.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-600 | `FxaaPass` skeleton + settings (quality preset, edge thresholds) | ⬜ | Constructs and reports support. |
| MOD-601 | FXAA 3.11-style GLSL (luma-based edge detect + directional blend), documented as a reimplementation | ⬜ | No third-party source copied; behavior described in the header; a hard diagonal edge is measurably smoothed. |
| MOD-602 | Luma source decision (computed from RGB vs stored in alpha) and documentation | ⬜ | Computed from RGB for v1; the alpha-luma optimization documented as a possible follow-up. |
| MOD-603 | Apply FXAA **after** tonemapping (documented ordering rule) | ⬜ | Enforced by `RenderPipeline`'s fixed chain order; test asserts the order. |
| MOD-604 | Quality presets mapped from `RenderQuality` | ⬜ | Documented table; tested. |
| MOD-605 | Unit tests: settings, support, disabled == input | ⬜ | Covered. |
| MOD-606 | Golden image: aliased triangle before/after | ⬜ | Edge-gradient metric improves by a documented margin. |
| MOD-607 | Example `cnaext_fxaa_test` toggling FXAA live | ⬜ | Registered ctest. |
| MOD-608 | Perf: FXAA cost at 720p/1080p | ⬜ | Recorded. |
| MOD-609 | Document when to prefer MSAA over FXAA per renderer | ⬜ | Support matrix row added. |
| MOD-610 | Decide on SMAA/TAA: ⛔ out of scope for this plan, with reasons | ⬜ | TAA needs motion vectors + history (a much larger design); recorded as a possible future plan file, not a task here. |

---

## Phase 7 — `RenderPipeline` orchestrator (`MOD-700`–`MOD-747`)

→ CNAEXT.md §5.1, N20/N25. The class that finally gives `RenderPipelineSettings` a consumer.

### 7.1 Core

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-700 | `RenderPipeline` skeleton: ctor(device), dtor, non-copyable, `getSettings()` | ⬜ | Constructing on any renderer (including 2D-only) succeeds; unsupported features are simply skipped. |
| MOD-701 | `resize(width,height)` — allocate/reallocate HDR scene target + aux targets | ⬜ | Idempotent for the same size; releases the old set; 100 alternating resizes stay memory-bounded. |
| MOD-702 | HDR scene target creation (`HalfVector4` when supported, `Color` otherwise, logged once) | ⬜ | The chosen format is queryable (`getSceneTargetFormat()`); the downgrade is logged exactly once per pipeline. |
| MOD-703 | `begin(clearColor)` — bind the scene target and clear (colour + depth) | ⬜ | Scene draws between begin/end land in the target, not the backbuffer (verified by readback). |
| MOD-704 | `end()` — run the fixed chain and resolve to the backbuffer | ⬜ | Chain order: SSAO → bloom → tonemap → FXAA → user passes → resolve; asserted by the counting fake pass. |
| MOD-705 | Fixed chain order documented and justified | ⬜ | In `docs/cnaext-engine-layer.md`, with the reason for each position (e.g. FXAA after tonemap). |
| MOD-706 | `addUserPass(PostProcessPass*)` / `removeUserPass` — app-supplied passes at a documented slot | ⬜ | Non-owning by default; an owning overload (`addOwnedUserPass(std::unique_ptr<…>)`) exists; both tested. |
| MOD-707 | HDR-off path: no HDR target, no tonemap, chain reduced to enabled passes | ⬜ | D8 satisfied — output bit-identical to no pipeline when everything is off except the final blit. |
| MOD-708 | Zero-pass short circuit: when nothing is enabled and HDR is off, render straight to the backbuffer | ⬜ | Draw-call count identical to not using the pipeline at all (asserted). |
| MOD-709 | `setShadowCaster(DirectionalLightEXT*)` (Phase 8 consumer) | ⬜ | Null clears; documented as "shadow pass runs before `begin()` returns". |
| MOD-710 | `setSkybox(Skybox*)` (Phase 11 consumer) | ⬜ | Drawn after opaque scene, before post-processing; documented depth-state requirement. |
| MOD-711 | `setDepthNormalPrepass(...)` wiring for SSAO (Phase 5 consumer) | ⬜ | SSAO silently disables itself with a one-time log when no prepass is attached. |
| MOD-712 | `getSceneTarget()` accessor so apps can sample the HDR scene from custom passes | ⬜ | Returns null outside begin/end; documented lifetime. |
| MOD-713 | Exception safety: an exception inside a pass restores the backbuffer binding and rethrows | ⬜ | Tested with a throwing fake pass; the next frame renders normally. |
| MOD-714 | `begin()`/`end()` misuse guards (double begin, end without begin) | ⬜ | Throws `EngineException` with a clear message; tested for both. |
| MOD-715 | `GraphicsDevice` device-reset/context-loss handling — reallocate targets on reset | ⬜ | Subscribing to the existing `DeviceReset` event; verified with `DebugSimulateContextLoss()`. |
| MOD-716 | `getGpuMemoryEstimateBytes()` (→ MOD-139) | ⬜ | Sum of owned target sizes; arithmetic unit-tested. |
| MOD-717 | `getStatistics()` — passes run, draw calls, target switches for the last frame | ⬜ | Small POD; tested against the counting fake. |

### 7.2 Settings integration

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-725 | Settings changes take effect on the next frame without reconstruction | ⬜ | Toggling bloom/SSAO/FXAA/HDR between frames is tested for all 16 combinations. |
| MOD-726 | Changing `hdrEnabled` reallocates the scene target lazily | ⬜ | Exactly one reallocation, on the first frame after the change (asserted). |
| MOD-727 | `RenderQuality` preset application across all passes in one place | ⬜ | A single `applyQualityPreset()` maps the enum to per-pass values; documented table. |
| MOD-728 | `GraphicsDevice::GetRenderPipelineSettingsEXT()` — the accessor the settings doc already claims exists | ⬜ | CNAEXT-marked, always compiled? **No** — decided in `MOD-729`. |
| MOD-729 | Decide whether the settings accessor lives on `GraphicsDevice` (XNA type) or only on `RenderPipeline` | ⬜ | Written decision. Default recommendation: **only on `RenderPipeline`** (keeps the XNA type free of an engine-layer-typed member and avoids a `CNA_CNAEXT`-conditional member on a core class); `RenderPipelineSettings`'s own doc comment is corrected accordingly. |
| MOD-730 | Settings validation (negative exposure, gamma ≤0, absurd radii) | ⬜ | Clamped with documented ranges, not rejected; tested at the boundaries. |
| MOD-731 | Settings serialization helper (to/from a simple key=value string) for demos and tests | ⬜ | Round-trips every field; unit-tested. |

### 7.3 Verification

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-735 | Unit tests for every public `RenderPipeline` member (incl. both misuse guards) | ⬜ | Full coverage per CLAUDE.md's test rule. |
| MOD-736 | Golden image: full pipeline (HDR + bloom + tonemap) on a fixed test scene | ⬜ | Golden committed with the generating command. |
| MOD-737 | Golden image: pipeline with everything off == direct rendering | ⬜ | Bit-identical (D8). |
| MOD-738 | 2D-only renderer behavior: pipeline constructs, passes skip, output equals direct rendering | ⬜ | Verified on SDL_Renderer or Headless; no throw anywhere. |
| MOD-739 | `SpriteBatch` inside `begin/end` works (2D game with HDR bloom) | ⬜ | Example proves an ordinary `SpriteBatch` game gains bloom by wrapping its draw in the pipeline. |
| MOD-740 | 3D `Model`/`BasicEffect`/`PbrEffect` inside `begin/end` works | ⬜ | Example proves the 3D path. |
| MOD-741 | Example `cnaext_render_pipeline_test` — the canonical demo (3D scene + HDR + bloom + tonemap + FXAA) | ⬜ | Registered ctest with smoke frames; documented as the layer's showcase. |
| MOD-742 | Perf: full-pipeline frame cost vs direct rendering at 720p/1080p per quality preset | ⬜ | Table in the docs. |
| MOD-743 | Leak check under ASan/LSan for 1000 frames + 50 resizes | ⬜ | No growth beyond the known external residuals recorded elsewhere in the repo. |
| MOD-744 | Thread-safety statement (pipeline is single-threaded, owner-thread only) | ⬜ | Documented; matches the renderers' own ownership rules. |
| MOD-745 | Flip `N20`/`N25` in CNAEXT.md | ⬜ | Status table updated. |

---

## Phase 8 — Directional shadow maps (`MOD-800`–`MOD-863`)

→ CNAEXT.md §5.3, N30/N33. Split deliberately: the **generation** side is engine-layer
(`CNA::Graphics`), the **reception** side is always-compiled CNAEXT members on the four lit effects.

### 8.1 Engine-layer light + shadow map

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-800 | `CNA::Graphics::DirectionalLightEXT` struct (direction, colour, intensity, castsShadows) (→ C6) | ⬜ | Documented as engine-layer only; explicitly *not* an XNA type; unit-tested including normalization of the direction. |
| MOD-801 | `ShadowMap` skeleton: ctor(device, ShadowQuality), depth `RenderTarget2D` allocation | ⬜ | Map size derived from `ShadowQuality` (documented table: Low 512, Medium 1024, High 2048, Ultra 4096). |
| MOD-802 | Shadow-map depth format selection (depth texture where available, packed-`Color` distance otherwise) | ⬜ | Both paths implemented and tested; the chosen path is queryable and logged once. |
| MOD-803 | Light view matrix from the light direction + scene bounds | ⬜ | Unit-tested against hand-computed matrices for 4 directions incl. the near-degenerate straight-down case. |
| MOD-804 | Orthographic light projection fitted to a `BoundingBox` | ⬜ | Fits tightly; unit-tested for correct extents and for a zero-volume box (documented behavior). |
| MOD-805 | `begin(light, sceneBounds)` — bind the depth target, set state (front-face culling or depth bias) | ⬜ | Documented which peter-panning/acne mitigation is used and why. |
| MOD-806 | `end()` — restore the previous target and viewport | ⬜ | RAII-safe (uses `MOD-203`'s helper); tested with a throwing draw. |
| MOD-807 | `getDepthTexture()` / `getLightViewProjection()` accessors | ⬜ | Documented lifetime; null/identity before the first `begin`. |
| MOD-808 | Depth-bias + normal-offset settings with documented defaults | ⬜ | Exposed as settings; defaults justified by the acne/peter-panning golden pair (`MOD-855`). |
| MOD-809 | `ShadowMapEffect` (CNAEXT, XNA namespace) — the depth-only effect used during generation | ⬜ | Minimal vertex-only shader; unskinned. |
| MOD-810 | `SkinnedShadowMapEffect` — skinned depth-only variant | ⬜ | Shares `SkinnedEffect`'s bone API; a skinned mesh casts a correctly animated shadow. |
| MOD-811 | `ShadowMap` capability gate + fallback (no depth targets → shadows disabled, one-time log) | ⬜ | D1 satisfied; verified on Headless. |

### 8.2 Receiver side — always-compiled CNAEXT hooks

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-820 | `IShadowReceiverEXT` interface (`setShadowMapEXT`, `setLightViewProjectionEXT`, `setShadowsEnabledEXT`) | ⬜ | Header in `Microsoft/Xna/Framework/Graphics/`, `CNAEXT`-marked, no `#ifdef`; includes `CNA/CNAHelper.hpp`. |
| MOD-821 | `GpuDrawParams` shadow field group (`shadowMap`, `lightViewProjColMajor[16]`, `shadowsEnabled`, `shadowBias`, `shadowMapTexelSize`) | ⬜ | Appended with defaults that mean "no shadows"; every renderer compiles and ignores them unchanged. |
| MOD-822 | `BasicEffect` implements `IShadowReceiverEXT` + fills the params | ⬜ | Tests cover the 3 setters and the filled params; no behavior change when disabled. |
| MOD-823 | `SkinnedEffect` implements `IShadowReceiverEXT` | ⬜ | Same. |
| MOD-824 | `PbrEffect` implements `IShadowReceiverEXT` | ⬜ | Same. |
| MOD-825 | `SkinnedPbrEffect` implements `IShadowReceiverEXT` | ⬜ | Same. |
| MOD-826 | Document the "accepted and ignored on renderers without the shader" convention for these fields | ⬜ | Matches the PBR precedent; written in `docs/cnaext-engine-layer.md` and in each effect's header. |
| MOD-827 | Decide whether shadow reception needs a shader-variant explosion or a uniform branch | ⬜ | Written decision (recommendation: one extra variant per lit shader, keyed by `shadowsEnabled`, to avoid a runtime branch on mobile GPUs). |

### 8.3 EasyGL reference shaders

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-835 | EasyGL: shadow-map generation path (depth-only draw into the shadow target) | ⬜ | A cube casts a recognizable depth silhouette (readback-verified). |
| MOD-836 | EasyGL: `BasicEffect` shadow-receiving variant with 3×3 PCF | ⬜ | The shadowed region matches a CPU-computed expectation within a documented tolerance. |
| MOD-837 | EasyGL: `SkinnedEffect` shadow-receiving variant | ⬜ | Same, animated. |
| MOD-838 | EasyGL: `PbrEffect` shadow-receiving variant | ⬜ | Shadow attenuates direct light only (not ambient/IBL) — documented and asserted. |
| MOD-839 | EasyGL: `SkinnedPbrEffect` shadow-receiving variant | ⬜ | Same. |
| MOD-840 | PCF kernel size from `ShadowQuality` (1/3×3/5×5/poisson) | ⬜ | Documented table; each kernel visually verified. |
| MOD-841 | Shadow-map border handling (outside the light frustum = lit, never shadowed) | ⬜ | Clamp-to-border or an explicit range check; a golden proves no dark halo outside the map. |
| MOD-842 | Cross-check against the existing `easygl_shadowmapping_*` examples | ⬜ | Library output matches the example's output within tolerance. |

### 8.4 Verification

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-850 | Unit tests: light matrices, ortho fitting, quality→size table, settings | ⬜ | Full coverage of the public surface. |
| MOD-851 | Unit tests: `IShadowReceiverEXT` on all four effects (setters, params, disabled default) | ⬜ | Both equal and unequal cases per CLAUDE.md's test rules. |
| MOD-852 | Golden image: single cube on a plane, sun at 45° | ⬜ | Committed golden. |
| MOD-853 | Golden image: shadows disabled == unshadowed render | ⬜ | Bit-identical. |
| MOD-854 | Golden image: skinned character self-shadowing | ⬜ | Committed golden. |
| MOD-855 | Golden pair documenting acne (bias=0) and peter-panning (bias too high) at the chosen default | ⬜ | Justifies `MOD-808`'s defaults with evidence rather than taste. |
| MOD-856 | Example `cnaext_shadowmap_test` — live window, movable sun | ⬜ | Registered ctest. |
| MOD-857 | Perf: shadow pass cost per quality at 720p/1080p | ⬜ | Recorded. |
| MOD-858 | `RenderPipeline` integration: shadow pass runs inside `begin()` when a caster is set | ⬜ | Ordering asserted; the app draws its scene exactly once for shading and once for the shadow pass (documented). |
| MOD-859 | Document the app-side contract: the scene must be drawable twice (a `renderScene` callback pattern) | ⬜ | Documented with an example; decided in `MOD-860`. |
| MOD-860 | Decide the scene-callback shape (`std::function<void(Effect&)>` vs app-driven begin/end) | ⬜ | Written decision. Recommendation: **app-driven** `shadowMap.begin/end` (no callback in the public API) to keep the layer non-prescriptive; a callback convenience may be added later. |
| MOD-861 | Flip `N30` in CNAEXT.md | ⬜ | Status table updated. |

---

## Phase 9 — Cascaded shadow maps (`MOD-900`–`MOD-929`)

→ CNAEXT.md §5.3, N31.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-900 | `CascadedShadowMap` skeleton (N cascades, N ∈ 2..4, from `ShadowQuality`) | ⬜ | Constructs; per-cascade `ShadowMap`s allocated once. |
| MOD-901 | Practical split scheme (logarithmic/uniform blend with a documented λ) | ⬜ | Split distances unit-tested against hand-computed values for λ ∈ {0,0.5,1}. |
| MOD-902 | Per-cascade frustum-corner extraction from the camera view/proj | ⬜ | Unit-tested against hand-computed corners for a known perspective matrix. |
| MOD-903 | Per-cascade light-space fitting (sphere-based, to avoid shimmering on rotation) | ⬜ | Rotating the camera does not change the fitted extents (asserted numerically). |
| MOD-904 | Texel-snapping to remove shimmering on translation | ⬜ | Translating the camera by a sub-texel amount does not change the shadow pattern (asserted). |
| MOD-905 | Cascade selection in the receiver shader (by view depth) | ⬜ | Correct cascade chosen; a debug visualization mode tints each cascade. |
| MOD-906 | Cascade blending band to hide the seam | ⬜ | Width configurable; golden shows no hard seam. |
| MOD-907 | Texture-array vs atlas decision for cascade storage | ⬜ | Written decision (recommendation: **atlas** in one `RenderTarget2D`, since texture arrays are not part of CNA's renderer interface today). |
| MOD-908 | `GpuDrawParams` extension for cascades (split distances + per-cascade matrices, or atlas UV rects) | ⬜ | Appended with "no cascades" defaults; renderers unchanged. |
| MOD-909 | EasyGL cascade receiver shaders for the 4 lit effects | ⬜ | Each verified by golden. |
| MOD-910 | Debug visualization mode (cascade tint) exposed as a setting | ⬜ | Off by default; documented as a debugging aid. |
| MOD-911 | Unit tests: splits, fitting, snapping, selection math | ⬜ | CPU-reference-compared. |
| MOD-912 | Golden image: large outdoor scene with 3 cascades | ⬜ | Committed. |
| MOD-913 | Golden image: cascade debug tint | ⬜ | Committed. |
| MOD-914 | Example `cnaext_csm_test` | ⬜ | Registered ctest. |
| MOD-915 | Perf: N-cascade cost vs single map | ⬜ | Recorded. |
| MOD-916 | Document CSM's app contract (scene drawn N+1 times) and its cost | ⬜ | Written. |
| MOD-917 | Flip `N31` in CNAEXT.md | ⬜ | Updated. |

---

## Phase 10 — Point and spot shadows (`MOD-1000`–`MOD-1027`)

→ CNAEXT.md §5.3, N32 (marked long-term there; scoped here so it is not open-ended).

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1000 | `PointLightEXT` / `SpotLightEXT` engine-layer structs | ⬜ | Position, colour, intensity, range, (cone angles for spot); documented as engine-layer only. |
| MOD-1001 | Decide the point-shadow storage: `RenderTargetCube` depth vs dual-paraboloid | ⬜ | Written decision (recommendation: **cube**, since `RenderTargetCube` already exists with real render-into support). |
| MOD-1002 | `CubeShadowMap` — 6-face depth generation into a `RenderTargetCube` | ⬜ | All 6 faces rendered; per-face view matrices unit-tested. |
| MOD-1003 | Linear-distance storage instead of projected depth (documented reason) | ⬜ | Distance/range stored; unit-tested reconstruction. |
| MOD-1004 | `SpotShadowMap` — single perspective depth map from the cone | ⬜ | Projection derived from the cone angle; unit-tested. |
| MOD-1005 | Receiver hooks for point/spot on the four lit effects | ⬜ | New `GpuDrawParams` fields with inert defaults. |
| MOD-1006 | EasyGL shaders for cube-shadow lookup with PCF | ⬜ | Golden-verified. |
| MOD-1007 | Light-count limits documented (how many shadowed lights per draw; recommendation: 1 shadowed + the 3 XNA directional slots) | ⬜ | Written and enforced with a clear message. |
| MOD-1008 | Unit tests for all new math | ⬜ | Covered. |
| MOD-1009 | Golden images: point light in a box; spot light on a plane | ⬜ | Committed. |
| MOD-1010 | Example `cnaext_pointshadow_test` | ⬜ | Registered ctest. |
| MOD-1011 | Perf: 6-face generation cost | ⬜ | Recorded; documented as the reason point shadows default off. |
| MOD-1012 | Flip `N32` in CNAEXT.md | ⬜ | Updated. |

---

## Phase 11 — Skybox (`MOD-1100`–`MOD-1127`)

→ CNAEXT.md §5.4, N40. Small, self-contained, and a prerequisite for IBL being visually meaningful.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1100 | `Skybox` skeleton (ctor(device, TextureCube*), `draw(view, projection)`, `getEnvironment()`) | ⬜ | Non-owning cube by default; an owning `SetOwnedEnvironment` variant follows the `PbrEffect` precedent. |
| MOD-1101 | `SkyboxEffect` (CNAEXT, XNA namespace) — cube-sampling effect with the translation-stripped view | ⬜ | Documented that the view's translation is removed so the sky stays infinitely distant. |
| MOD-1102 | Fullscreen-triangle sky rendering (ray direction from the inverse view-projection) — no cube mesh | ⬜ | One draw call, no vertex buffer; the ray reconstruction is unit-tested against a CPU implementation. |
| MOD-1103 | Depth state: draw at far plane with `LessEqual` and depth-write off | ⬜ | Sky never occludes geometry; asserted by a golden with a foreground object. |
| MOD-1104 | Draw ordering inside `RenderPipeline` (after opaque, before transparent/post) | ⬜ | Documented and asserted. |
| MOD-1105 | HDR skybox support (float cube map source) | ⬜ | An `HdrBlendable` cube renders values >1 into the HDR scene target (readback-verified). |
| MOD-1106 | Rotation/orientation setting (yaw offset) | ⬜ | Documented; unit-tested. |
| MOD-1107 | Intensity/tint multiplier | ⬜ | Documented; unit-tested. |
| MOD-1108 | Fallback when cube maps or custom effects are unsupported (skip + one-time log) | ⬜ | D1 satisfied. |
| MOD-1109 | Equirectangular → cube helper (`EnvironmentProcessor::convertEquirectangular`) | ⬜ | Loads the far more common HDR panorama layout; unit-tested for face orientation with a marked-face source. |
| MOD-1110 | Cube-face orientation conformance test (each face samples the expected direction) | ⬜ | A 6-colour cube renders the expected colour in the expected screen region for 6 camera directions. |
| MOD-1111 | Unit tests for every public member | ⬜ | Covered. |
| MOD-1112 | Golden image: skybox with a foreground object | ⬜ | Committed. |
| MOD-1113 | Example `cnaext_skybox_test` — orbit camera | ⬜ | Registered ctest. |
| MOD-1114 | Perf: sky cost (should be ~1 fullscreen pass) | ⬜ | Recorded. |
| MOD-1115 | Document the skybox contract and the cube-map coordinate convention CNA uses | ⬜ | Written once; referenced by IBL. |
| MOD-1116 | Flip `N40` in CNAEXT.md | ⬜ | Updated. |

---

## Phase 12 — Image-based lighting (`MOD-1200`–`MOD-1263`)

→ CNAEXT.md §5.4, N41–N44. The largest single quality win for the existing `PbrEffect`, and the one
place PBR meaningfully grows (CNAEXT.md's own words).

### 12.1 Precompute infrastructure

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1200 | `EnvironmentProcessor` skeleton (ctor(device), capability gate) | ⬜ | Throws a clear `EngineException` where float RTs / cube RTs are unavailable; verified on Headless. |
| MOD-1201 | Cube-face render loop helper (bind face, set view, draw fullscreen triangle) | ⬜ | Shared by irradiance and prefilter; unit-tested with a per-face constant-colour shader. |
| MOD-1202 | `generateIrradiance(env, size=32)` — cosine convolution (→ N41) | ⬜ | A constant-colour environment produces the same constant irradiance (energy check within 1 %). |
| MOD-1203 | Irradiance sample-count / quality setting | ⬜ | Documented table; higher counts converge toward the analytic result (asserted). |
| MOD-1204 | `generatePrefilteredSpecular(env, baseSize=128, mips=5)` — GGX split-sum prefilter (→ N42) | ⬜ | Mip 0 ≈ the source (roughness 0); the last mip ≈ irradiance-like (roughness 1); both asserted numerically. |
| MOD-1205 | Roughness→mip mapping documented and shared with the sampling shader | ⬜ | One formula defined once; a mismatch between generation and sampling is impossible by construction. |
| MOD-1206 | Hammersley/Van-der-Corput + importance-sampling GLSL helpers | ⬜ | Unit-tested against CPU reimplementations for the first 64 samples. |
| MOD-1207 | `generateBrdfLut(size=512)` — the 2D scale/bias LUT (→ N42) | ⬜ | Compared against a CPU-computed LUT within a documented tolerance at 16 sampled points. |
| MOD-1208 | LUT format decision (`HalfVector2` where available, `Color` fallback with documented precision loss) | ⬜ | Both paths tested. |
| MOD-1209 | Seamless-cube-filter capability use (`SeamlessCubeMapFilter`) and the fallback | ⬜ | Where unavailable, mip 4+ seams are visible and documented, not silently wrong. |
| MOD-1210 | Ownership model for generated products (who deletes the returned textures) | ⬜ | Written decision. Recommendation: the processor **owns** them and returns raw pointers valid for its lifetime, plus explicit `release*` methods; documented and leak-tested. |
| MOD-1211 | Generation cost measurement + a "generate once, reuse" note in the docs | ⬜ | Numbers recorded; the demo generates at load, not per frame. |
| MOD-1212 | Optional: cache generated products to disk between runs | ⬜ | ⛔ for v1 with the reason (D7 — no new asset format in this plan). |

### 12.2 Consumption

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1220 | `ImageBasedLightEXT` struct (irradiance, prefilteredSpecular, brdfLut, intensity) | ⬜ | Documented; validated (all three non-null or the whole bundle is inert). |
| MOD-1221 | `PbrEffect::setImageBasedLightEXT` / `getImageBasedLightEXT` (→ N43) | ⬜ | CNAEXT-marked, always compiled; the struct lives in `CNA::Graphics` so the include is one-way (documented). |
| MOD-1222 | Decide whether `ImageBasedLightEXT` should instead live in the XNA namespace to avoid the XNA→CNA include direction | ⬜ | **Open question — see §OQ-4.** Default: keep it in `CNA::Graphics` and have `PbrEffect` take the three textures individually, avoiding the layering inversion entirely. |
| MOD-1223 | `SkinnedPbrEffect::setImageBasedLightEXT` | ⬜ | Same surface as `PbrEffect`. |
| MOD-1224 | `GpuDrawParams` IBL field group (3 texture slots + intensity + prefiltered mip count) | ⬜ | Appended with inert defaults; every renderer compiles unchanged. |
| MOD-1225 | EasyGL: split-sum ambient shader replacing the flat `AmbientLightColor` term when IBL is bound | ⬜ | With IBL unbound, output is bit-identical to today (asserted by golden). |
| MOD-1226 | Ambient-term switch documented (flat ambient vs IBL are exclusive, not additive) | ⬜ | Written in the effect header and the docs. |
| MOD-1227 | Occlusion map interaction (AO multiplies the IBL ambient, not the direct light) | ⬜ | Documented and asserted numerically. |
| MOD-1228 | IBL + shadows interaction (shadow attenuates direct only) | ⬜ | Consistent with `MOD-838`; asserted. |
| MOD-1229 | Energy-conservation sanity test (a white furnace test: uniform environment, albedo 1, no light loss beyond a documented margin) | ⬜ | Documented result; the margin is the honest measure of the approximation's quality. |

### 12.3 Verification

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1240 | Unit tests: every public `EnvironmentProcessor` method + `ImageBasedLightEXT` validation | ⬜ | Covered. |
| MOD-1241 | Unit tests: CPU reference implementations of irradiance/prefilter/BRDF-LUT | ⬜ | Themselves tested against hand-computed values. |
| MOD-1242 | Golden image: metal/roughness sphere grid under IBL (the canonical PBR chart) | ⬜ | 5×5 grid golden committed; the standard visual reference for every renderer follow-up. |
| MOD-1243 | Golden image: same grid with IBL off (flat ambient) | ⬜ | Proves the switch and gives the regression baseline. |
| MOD-1244 | Golden image: irradiance and prefiltered mips dumped as a debug sheet | ⬜ | Makes precompute regressions visible directly. |
| MOD-1245 | Example `cnaext_ibl_test` — skybox + sphere grid + adjustable intensity | ⬜ | Registered ctest; the showcase for the whole layer alongside `MOD-741`. |
| MOD-1246 | Perf: per-frame IBL sampling cost vs flat ambient | ⬜ | Recorded. |
| MOD-1247 | Document the complete IBL pipeline in `docs/cnaext-engine-layer.md` | ⬜ | From an HDR panorama file to a lit sphere, with code. |
| MOD-1248 | Flip `N41`–`N43` in CNAEXT.md | ⬜ | Updated. |

---

## Phase 13 — Material system reconciliation (`MOD-1300`–`MOD-1335`)

→ CNAEXT.md §5.5, N42/N52. Makes `PbrMaterial` a real, lossless description of what `PbrEffect` can render.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1300 | Audit `PbrMaterial` vs `PbrEffect` field by field; write the mapping table | ⬜ | Table in this file and in the header; every gap becomes one of the tasks below. |
| MOD-1301 | `PbrMaterial::EmissiveFactor` as `Vector3` (replacing the emissive `Color`) | ⬜ | HDR emissive >1 representable; migration note in the header; tests updated. |
| MOD-1302 | `CNA::Graphics::AlphaMode` enum (`Opaque`/`Mask`/`Blend`) replacing `alphaBlend_`/`alphaCutoff_` | ⬜ | Matches glTF semantics; `Mask` keeps a cutoff field; all three round-trip in tests. |
| MOD-1303 | `applyMaterial(const PbrMaterial&, PbrEffect&)` binding helper | ⬜ | Every field lands on the effect; unit-tested field by field. |
| MOD-1304 | `applyMaterial` overload for `SkinnedPbrEffect` | ⬜ | Same coverage. |
| MOD-1305 | Reverse helper `extractMaterial(const PbrEffect&) -> PbrMaterial` | ⬜ | Round-trip `material → effect → material` is lossless (asserted). |
| MOD-1306 | `AlphaMode` → `BlendState`/`AlphaTest` application decision | ⬜ | Documented: `Blend` sets `AlphaBlend`, `Mask` uses the alpha-test path with the cutoff, `Opaque` sets `Opaque`; tested. |
| MOD-1307 | Double-sided flag (glTF `doubleSided`) → `RasterizerState.CullMode` | ⬜ | Present on `PbrMaterial`, applied by `applyMaterial`; tested. |
| MOD-1308 | Texture-transform (`KHR_texture_transform`) carried on `PbrMaterial` | ⬜ | Round-trips from the importer through the material to the effect. |
| MOD-1309 | glTF import → `PbrMaterial` bridge (→ N52) | ⬜ | `GltfImportCore` can produce a `PbrMaterial` alongside the `PbrEffect` it already builds, without changing the default runtime path. |
| MOD-1310 | Keep the bridge out of the always-compiled path (import must not depend on `CNA_CNAEXT`) | ⬜ | The bridge lives in `graphics-ext`; the importer exposes plain data the bridge consumes. |
| MOD-1311 | `PbrMaterial` equality/hash/`ToString` per CLAUDE.md's test rules | ⬜ | Equal and unequal cases; hash consistency; format documented. |
| MOD-1312 | Unit tests for every new/changed `PbrMaterial` member | ⬜ | Full coverage incl. the removed-field migration. |
| MOD-1313 | Golden image: the same glTF model rendered via the direct importer path and via `PbrMaterial`+`applyMaterial` | ⬜ | Bit-identical; proves the bridge is lossless in practice, not just in unit tests. |
| MOD-1314 | Document material ownership (who owns the textures a material references) | ⬜ | Written; consistent with `PbrEffect`'s existing `SetOwned*` precedent. |
| MOD-1315 | Flip `N04`/`N52` notes in CNAEXT.md | ⬜ | Updated. |

---

## Phase 14 — Instancing, LOD and culling helpers (`MOD-1400`–`MOD-1435`)

→ CNAEXT.md §5.6, N50/N51. Pure convenience over APIs that already exist — independent of Phases 1–13.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1400 | `InstancedRendererEXT` skeleton (ctor(device, ModelMeshPart*)) | ⬜ | Validates the part and its declaration; throws clearly where instancing is unsupported. |
| MOD-1401 | `setInstances(const std::vector<Matrix>&)` — per-instance transform stream upload | ⬜ | Uses a dynamic `VertexBuffer` with `SetDataOptions`; re-upload does not reallocate when the count is unchanged (asserted). |
| MOD-1402 | Instance vertex declaration (4×`Vector4` at the documented usage indices) | ⬜ | Documented; matches what the existing `DrawInstancedPrimitivesEx` renderers expect. |
| MOD-1403 | `draw(Effect&)` — issues `DrawInstancedPrimitives` with the correct binding set | ⬜ | 100 instances render in one draw call (asserted by a renderer draw counter). |
| MOD-1404 | Per-instance colour/tint stream (optional second element) | ⬜ | Off by default; documented shader requirement. |
| MOD-1405 | Capability gate + documented fallback (loop of single draws) | ⬜ | Fallback is opt-in, not silent; the choice is queryable. |
| MOD-1406 | `LodGroupEXT` (`addLevel(maxDistance, part)`, `select(distance)`) | ⬜ | Levels sorted on insert; `select` is O(log n) or documented O(n) with a reason; boundary distances tested. |
| MOD-1407 | `LodGroupEXT` hysteresis to avoid per-frame flapping at a boundary | ⬜ | Configurable; off by default; tested with an oscillating distance. |
| MOD-1408 | Screen-space-error LOD selection as an alternative to raw distance | ⬜ | Optional mode; documented formula; unit-tested. |
| MOD-1409 | `FrustumCullerEXT` — `BoundingFrustum`-based visibility filter over a list of bounds | ⬜ | Uses the existing XNA `BoundingFrustum`; unit-tested against hand-computed cases. |
| MOD-1410 | Culling + instancing composition helper (cull, then upload only visible transforms) | ⬜ | Verified by instance-count assertions. |
| MOD-1411 | Unit tests for every public member of all three classes | ⬜ | Covered incl. empty/degenerate inputs. |
| MOD-1412 | Example `cnaext_instancing_lod_test` — 10 000 cubes, 3 LOD levels, live counters | ⬜ | Registered ctest; documented frame-time improvement vs the naive path. |
| MOD-1413 | Perf: 10 000 instances instanced vs looped | ⬜ | Recorded. |
| MOD-1414 | Flip `N50`/`N51` in CNAEXT.md | ⬜ | Updated. |

---

## Phase 15 — Compute shaders and storage buffers (`MOD-1500`–`MOD-1565`)

→ CNAEXT.md §5.0/§5.7, N70–N73. Explicitly long-term: nothing in Phases 1–14 depends on it.

### 15.1 Renderer interfaces

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1500 | `IComputeShaderRenderer` interface in `IGraphicsRenderer.hpp` (→ §5.0) | ⬜ | Exactly the CNAEXT.md shape; every method documented; no XNA types in the signature (D2). |
| MOD-1501 | `IStorageBufferRenderer` interface | ⬜ | Same. |
| MOD-1502 | `IGraphicsRenderer::CreateComputeShader` / `CreateStorageBuffer` / `DispatchCompute` / `MemoryBarrierEXT` with safe defaults | ⬜ | All 47 renderer families compile unchanged; defaults return null / no-op (D3). |
| MOD-1503 | Barrier-bit ordinal enum (`CNA::GraphicsMemoryBarrier`) mapped per renderer | ⬜ | Documented bitmask; renderers translate; unit-tested mapping on the reference. |
| MOD-1504 | Image-access ordinal enum (`ReadOnly`/`WriteOnly`/`ReadWrite`) | ⬜ | Documented; used by `bindImage`. |
| MOD-1505 | Workgroup-size limits query (`getMaxComputeWorkGroupCount/Size/Invocations`) | ⬜ | Exposed through the device; a dispatch beyond the limit throws a clear message before submission. |

### 15.2 EasyGL reference implementation

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1510 | EasyGL: GLES 3.1 / GL 4.3 compute-support probe → `SupportsCapability(ComputeShaders)` | ⬜ | Reflects the real context; logged once. |
| MOD-1511 | EasyGL: compute program compile/link with error surfacing | ⬜ | A broken shader throws with the GLSL log; a valid one links. |
| MOD-1512 | EasyGL: SSBO creation, `SetData`/`GetData`, lifetime | ⬜ | 1 MB round-trip is byte-exact. |
| MOD-1513 | EasyGL: dispatch + memory barrier | ⬜ | A compute shader that doubles 1024 floats produces the expected buffer (asserted). |
| MOD-1514 | EasyGL: image binding (compute writes a `Texture2D`) | ⬜ | A compute-written gradient reads back exactly. |
| MOD-1515 | EasyGL: uniform setters on the compute program | ⬜ | int/float verified. |

### 15.3 Public wrappers

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1520 | `CNA::Graphics::StorageBuffer` (non-template, byte-oriented, impl in `.cpp`) + `StorageBufferT<T>` header template (→ C7) | ⬜ | `StorageBufferT<Vector4>` round-trips a vector of 1024; the non-template core is unit-tested independently. |
| MOD-1521 | `CNA::Graphics::ComputeShader` wrapper (ctor, uniforms, bind buffer/image, dispatch) | ⬜ | Mirrors §5.7's shape; every method documented and tested. |
| MOD-1522 | Capability gate + `EngineException` on unsupported renderers | ⬜ | Message names the renderer; verified on Headless and on a GLES-3.0-only EasyGL context. |
| MOD-1523 | Dispatch-argument validation (non-positive groups, exceeding limits) | ⬜ | Clear messages; tested at the boundaries. |
| MOD-1524 | Synchronization contract documentation (when the app must barrier) | ⬜ | Written; the wrapper inserts the common barriers itself and documents which. |
| MOD-1525 | Unit tests for every public member of both wrappers | ⬜ | Covered. |

### 15.4 First consumers (→ N73)

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1550 | GPU particle system demo (compute-updated positions, instanced draw) | ⬜ | 100 000 particles at an interactive frame rate on the reference renderer; frame time recorded. |
| MOD-1551 | GPU frustum culling demo (compute writes a visible-instance buffer) | ⬜ | Result matches the CPU `FrustumCullerEXT` exactly for a fixed scene (asserted). |
| MOD-1552 | Compute-based luminance reduction for auto-exposure (revisits `MOD-308`) | ⬜ | Either implemented behind a setting or ⛔ with a written reason. |
| MOD-1553 | Compute-based bloom downsample as an optional fast path | ⬜ | Only if measurably faster than the raster path; otherwise ⛔ with the measurement. |
| MOD-1554 | Document the compute contract, limits, and per-renderer availability | ⬜ | Support matrix row added. |
| MOD-1555 | Flip `N70`/`N71`/`N73` in CNAEXT.md | ⬜ | Updated. |

---

## Phase 16 — Per-renderer rollout matrix (`MOD-1600`–`MOD-1698`)

→ CNAEXT.md §6. EasyGL lands each subsystem first (Phases 1–15); this phase carries it to the other
renderers. **Every row here is independently schedulable and never blocks the reference work (D4).**
Each row's acceptance criterion is the same shape: *the subsystem's golden/reference test from its own
phase passes on this renderer, or the renderer reports `false` from the matching capability and its
documented fallback is verified.*

### 16.1 Float render targets (→ N12)

Tiers follow `CNA::GraphicsRendererType` on `next` (49 identities). The 2D-only, fixed-function and
web-DOM identities are handled once in §16.6 rather than repeated per subsystem.

| ID | Renderer | Status | Note |
|---|---|---|---|
| MOD-1600 | Vulkan | ⬜ | Real `VK_FORMAT_R16G16B16A16_SFLOAT` attachments; watch the render-pass-compatibility cache noted in `IGraphicsRenderer.hpp` (Task 911). |
| MOD-1601 | SdlGpu | ⬜ | |
| MOD-1602 | Bgfx | ⬜ | |
| MOD-1603 | WebGPU | ⬜ | `rgba16float` is core in WebGPU; likely the easiest after EasyGL. |
| MOD-1604 | D3D11 | ⬜ | GPU-verifiable via Wine+DXVK per the existing precedent. |
| MOD-1605 | D3D12 | ⬜ | Compile-verified on Windows only, per precedent. |
| MOD-1606 | D3D9 | ⬜ | `D3DFMT_A16B16G16R16F`; document the no-blending caveat on old hardware. |
| MOD-1607 | D3D10 | ⬜ | |
| MOD-1608 | OpenGL4 | ⬜ | |
| MOD-1609 | OpenGL2 | ⬜ | Only via `ARB_texture_float`; document if declined. |
| MOD-1610 | Magnum | ⬜ | |
| MOD-1611 | Diligent | ⬜ | Runtime-selected native API — verify on at least two. |
| MOD-1612 | LLGL | ⬜ | |
| MOD-1613 | Sokol | ⬜ | |
| MOD-1614 | Metal | ⬜ | |
| MOD-1615 | FNA3D | ⬜ | Already overrides `CreateRenderTarget2DEXT` — verify float formats specifically. |
| MOD-1616 | Wicked | ⬜ | |
| MOD-1617 | PortableGL | ⬜ | Software GL — likely ⛔; decide and document. |

### 16.2 Post-process passes (tonemap/bloom/SSAO/FXAA) (→ N25)

| ID | Renderer | Status | Note |
|---|---|---|---|
| MOD-1620 | Vulkan | ⬜ | GLSL→SPIR-V path already exists for `ShaderEffect`. |
| MOD-1621 | SdlGpu | ⬜ | |
| MOD-1622 | Bgfx | ⬜ | Needs `shaderc` regeneration — see CLAUDE.md's bgfx note. |
| MOD-1623 | WebGPU | ⬜ | Depends on `WEBGPU-76` (`ShaderEffect` custom WGSL), which is still open. |
| MOD-1624 | D3D11 | ⬜ | |
| MOD-1625 | D3D12 | ⬜ | |
| MOD-1626 | D3D9 | ⬜ | SM3 limits — document any pass that cannot fit. |
| MOD-1627 | D3D10 | ⬜ | |
| MOD-1628 | OpenGL4 | ⬜ | |
| MOD-1629 | Magnum | ⬜ | |
| MOD-1630 | Diligent | ⬜ | |
| MOD-1631 | LLGL | ⬜ | |
| MOD-1632 | Sokol | ⬜ | |
| MOD-1633 | Metal | ⬜ | |
| MOD-1634 | FNA3D | ⬜ | |
| MOD-1635 | Wicked | ⬜ | |
| MOD-1637 | IGL | ⬜ | |
| MOD-1636 | Cross-renderer pixel-parity test for the tonemap pass | ⬜ | Same HDR input on every ✅ renderer within a documented tolerance; the model is `WEBGPU-123`. |

### 16.3 Shadow-receiver shaders (→ N33)

| ID | Renderer | Status | Note |
|---|---|---|---|
| MOD-1640 | Vulkan | ⬜ | All 4 lit effects. |
| MOD-1641 | SdlGpu | ⬜ | |
| MOD-1642 | Bgfx | ⬜ | |
| MOD-1643 | WebGPU | ⬜ | Unskinned first (no skinning path yet, per CNAEXT.md §3.1). |
| MOD-1644 | D3D11 | ⬜ | |
| MOD-1645 | D3D12 | ⬜ | |
| MOD-1646 | D3D9 | ⬜ | |
| MOD-1647 | D3D10 | ⬜ | |
| MOD-1648 | OpenGL4 | ⬜ | |
| MOD-1649 | Magnum | ⬜ | |
| MOD-1650 | Diligent | ⬜ | |
| MOD-1651 | LLGL | ⬜ | |
| MOD-1652 | Sokol | ⬜ | |
| MOD-1653 | Metal | ⬜ | |
| MOD-1654 | FNA3D | ⬜ | |
| MOD-1655 | Wicked | ⬜ | |
| MOD-1656 | Cross-renderer shadow parity test | ⬜ | Same scene, documented tolerance. |

### 16.4 IBL shaders (→ N44)

| ID | Renderer | Status | Note |
|---|---|---|---|
| MOD-1660 | Vulkan | ⬜ | Precompute + sampling. |
| MOD-1661 | SdlGpu | ⬜ | |
| MOD-1662 | Bgfx | ⬜ | |
| MOD-1663 | WebGPU | ⬜ | |
| MOD-1664 | D3D11 | ⬜ | |
| MOD-1665 | D3D12 | ⬜ | |
| MOD-1666 | D3D9 | ⬜ | Cube-mip prefiltering on SM3 — verify feasibility first. |
| MOD-1667 | D3D10 | ⬜ | |
| MOD-1668 | OpenGL4 | ⬜ | |
| MOD-1669 | Magnum | ⬜ | |
| MOD-1670 | Diligent | ⬜ | |
| MOD-1671 | LLGL | ⬜ | |
| MOD-1672 | Sokol | ⬜ | |
| MOD-1673 | Metal | ⬜ | |
| MOD-1674 | Cross-renderer IBL sphere-grid parity test | ⬜ | The `MOD-1242` golden reproduced within tolerance. |

### 16.5 Compute (→ N72)

| ID | Renderer | Status | Note |
|---|---|---|---|
| MOD-1680 | Vulkan | ⬜ | Native compute queues. |
| MOD-1681 | D3D11 | ⬜ | CS 5.0 + UAV. |
| MOD-1682 | D3D12 | ⬜ | |
| MOD-1683 | WebGPU | ⬜ | Compute is core in WebGPU; gated on the backend's own maturity. |
| MOD-1684 | Metal | ⬜ | |
| MOD-1685 | Diligent | ⬜ | |
| MOD-1686 | OpenGL4 | ⬜ | |

### 16.6 Documented non-support (2D-only / non-shader renderers)

| ID | Renderers | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1690 | SDL_Renderer | ⬜ | `SupportsCapability` false for every new capability; `RenderPipeline` constructs and passes through; documented in the support matrix. |
| MOD-1691 | Canvas, HTML_DOM, SVG_DOM, PixiJs | ⬜ | Same. |
| MOD-1692 | Software, PortableGL | ⬜ | Same; PortableGL's decision from `MOD-1617` recorded here too. |
| MOD-1693 | Skia, Blend2D, Direct2D, OpenVG | ⬜ | Same; note Skia already overrides `CreateRenderTarget2DEXT` for its own reasons. |
| MOD-1694 | GDI, Glide, FreeDirect | ⬜ | Same. |
| MOD-1695 | DirectX 1–8, OpenGL1, OpenGLES1, TinyGL | ⬜ | Same; these are fixed-function/legacy by identity (TinyGL has no shaders, render targets, stencil or scissor at all). |
| MOD-1696 | Headless | ⬜ | Same; the canonical "everything false" reference used by the unit tests. |
| MOD-1697 | Stub | ⬜ | Accepts and ignores per its existing contract. |
| MOD-1698 | Support-matrix completeness check | ⬜ | A test (or script) asserts every identity in `CNA::GraphicsRendererType` (49 on `next`, and growing — derive the list, never hardcode the count) appears in the matrix with an explicit status — no silent omissions. |

---

## Phase 17 — Tests, golden images, CI (`MOD-1700`–`MOD-1741`)

Cross-cutting verification that does not belong to a single subsystem.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1700 | Test fixture: an offscreen `GraphicsDevice` usable without a display for engine-layer unit tests | ⬜ | Reuses the repo's existing headless/test-display convention; documented. |
| MOD-1701 | Fake `IGraphicsRenderer` for pure-logic tests (counts draws, target switches, allocations) | ⬜ | Lives in `tests/`, never in `src/`; used by the pass-ordering and allocation assertions throughout the plan. |
| MOD-1702 | Fake `PostProcessPass` (records order, forces throw) | ⬜ | Used by `MOD-208`/`MOD-704`/`MOD-713`. |
| MOD-1703 | Golden-image harness for the engine layer (naming, tolerance, regeneration command) | ⬜ | Follows the existing EasyGL golden convention; documented in one place. |
| MOD-1704 | Deterministic test scene definition (fixed camera, meshes, lights, materials) shared by all goldens | ⬜ | One header; every golden in this plan uses it so cross-subsystem comparisons are meaningful. |
| MOD-1705 | Float-image comparison utility (per-channel epsilon, HDR-aware) | ⬜ | Unit-tested; used by every HDR golden. |
| MOD-1706 | Tolerance policy documented per subsystem (why SSAO's is looser than tonemap's) | ⬜ | Written; prevents tolerance drift by taste. |
| MOD-1707 | CTest labels: `CnaExt`, `CnaExtDisplay`, `CnaExtGolden` | ⬜ | `ctest -L CnaExt` runs the display-free subset in the default environment. |
| MOD-1708 | ASan/LSan run of the whole engine-layer suite | ⬜ | Clean beyond the repo's recorded external residuals. |
| MOD-1709 | A `CNA_CNAEXT=OFF` regression suite run (the layer must not affect anything) | ⬜ | Full `CnaTests` result identical to the pre-plan baseline. |
| MOD-1710 | Baseline capture: record today's full-suite result before Phase 1 lands | ✅ | Done. 2026-08-17, `cmake-build-cnaext`, full `CnaTests` from the repo root under Xvfb: **6360 ran, 6351 pass, 8 skip, 1 fail** (an X11-connection hiccup in `EffectApplyTest.DisposeIsIdempotentAndDoesNotThrow`, passes standalone). Recorded in `NEXT_modern.md` §3 with the two run conditions that matter (repo-root CWD, a real display). |
| MOD-1711 | Per-phase regression gate: full suite re-run at the end of each phase | ⬜ | Result appended to `NEXT_modern.md` per phase. |
| MOD-1712 | Performance-tracking file (`docs/cnaext-perf.md`) with a stable measurement recipe | ⬜ | Every perf task in this plan appends there in the same format. |
| MOD-1713 | Fuzz/robustness: absurd sizes (0×0, 1×1, 8192×8192), rapid resize, rapid setting flips | ⬜ | No crash; documented clamping behavior. |
| MOD-1714 | Device-loss test across the whole layer | ⬜ | `DebugSimulateContextLoss()` followed by a correct frame for every subsystem. |
| MOD-1715 | Multi-window / multi-device sanity (two pipelines, two devices) | ⬜ | No cross-talk; documented if the layer is single-device-only. |
| MOD-1716 | 64-bit/32-bit and Debug/Release build sanity for the layer | ⬜ | Both configurations build and pass. |
| MOD-1717 | Emscripten build check for the engine layer (WEBGL2 profile) | ⬜ | Either builds, or the exact blocker is documented and the layer is cleanly excluded there. |
| MOD-1718 | Android build check (GLES3 profile) | ⬜ | Same standard as `MOD-1717`. |
| MOD-1719 | MinGW cross-compile check for the D3D renderers' engine-layer paths | ⬜ | Uses the repo's existing MinGW+ccache convention. |
| MOD-1740 | Wire `scripts/check_cnaext_guards.sh` (`MOD-3`) into the test suite | ⬜ | Runs as a ctest; fails on an unguarded engine-layer file. |
| MOD-1741 | Coverage report for `modules/graphics-ext/` | ⬜ | Recorded in `docs/coverage.md` alongside the existing numbers. |

---

## Phase 18 — Documentation, examples, demos (`MOD-1800`–`MOD-1833`)

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1800 | `docs/cnaext-engine-layer.md` — complete capability boundary document | ⬜ | Honest about what is and is not implemented, in the style of `docs/webgpu-renderer.md`. |
| MOD-1801 | Per-renderer support matrix kept current at the end of every phase | ⬜ | The `MOD-1698` completeness check passes. |
| MOD-1802 | "Getting started with the engine layer" tutorial (build flags → first HDR frame) | ⬜ | A reader with no context reaches a bloomed frame by following it. |
| MOD-1803 | Migration note for existing CNA games (nothing changes unless you opt in) | ⬜ | Explicit statement of D8 with the evidence task (`MOD-737`) cited. |
| MOD-1804 | Per-subsystem doc sections (HDR, post, shadows, sky/IBL, geometry, compute) | ⬜ | Each written as its phase completes, not at the end. |
| MOD-1805 | Shader-authoring guide for custom passes | ⬜ | Covers the uniform/sampler contract and the UV-origin rule (`MOD-207`). |
| MOD-1806 | `CNAEXT.md` kept in sync (statuses + corrections) | ⬜ | No divergence between the design doc and the plan at any phase boundary. |
| MOD-1807 | CLAUDE.md pointer to this plan and `docs/cnaext-engine-layer.md` | ⬜ | Future sessions find the engine layer's state without re-deriving it (the `plan_skia.md` precedent). |
| MOD-1808 | Showcase demo: a small 3D scene using every subsystem at once | ⬜ | Builds and runs from a documented command; screenshots committed. |
| MOD-1809 | Demo: 2D `SpriteBatch` game gaining HDR bloom in ~10 lines | ⬜ | Proves the layer's low entry cost. |
| MOD-1810 | Demo: glTF character with PBR + IBL + shadows | ⬜ | Uses the existing runtime glTF path end to end. |
| MOD-1811 | Screenshots/GIFs for the README's feature list | ⬜ | Committed under `docs/`. |
| MOD-1812 | `README.md` mention of the engine layer with its honest maturity label | ⬜ | Matches the language used for WebGPU/Diligent/Skia. |
| MOD-1813 | Nova-3D integration note (what Nova-3D can rely on today) | ⬜ | Written against implemented reality, not the backlog. |

---

## Phase 19 — API stabilization / Nova-3D readiness (`MOD-1900`–`MOD-1924`)

Runs only after Phases 1–13 are ✅. Its purpose is to stop the engine-layer API from drifting once a
real consumer exists.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1900 | Full API review of `CNA::Graphics` against the naming/documentation rules | ⬜ | Every public member re-read; deviations fixed in one pass. |
| MOD-1901 | Remove or rename anything that turned out to be dead (`RenderPipelineSettings` fields nothing reads) | ⬜ | No setting exists without a consumer; each removal recorded. |
| MOD-1902 | Const-correctness and `[[nodiscard]]` sweep | ⬜ | Consistent with the XNA layer's conventions. |
| MOD-1903 | Ownership review (raw vs `unique_ptr` vs borrowed) documented per class | ⬜ | One table; no class is ambiguous about who deletes what. |
| MOD-1904 | Bump `CNA_CNAEXT_ENGINE_VERSION` to 2 and document what changed | ⬜ | Versioning is meaningful rather than decorative. |
| MOD-1905 | Write the "engine layer v1 is stable" statement, or an honest "still moving" statement | ⬜ | Whichever is true; CNAEXT.md §9's "not an ABI guarantee" clause updated to match. |
| MOD-1906 | Final regression + perf sweep across every implemented renderer | ⬜ | Results recorded in `NEXT_modern.md` and `docs/cnaext-perf.md`. |
| MOD-1907 | Retrospective: which CNAEXT.md design decisions did not survive contact | ⬜ | Appended to CNAEXT.md as a short "what changed and why" section. |

---

## Appendix A — Shared type sketches referenced by the tasks

```cpp
// MOD-20 / MOD-200
namespace CNA::Graphics {
    /** @brief Everything a post-process pass may need for one invocation. */
    struct PostProcessContext {
        Microsoft::Xna::Framework::Graphics::Texture2D*      source        = nullptr;
        Microsoft::Xna::Framework::Graphics::Texture2D*      sourceDepth   = nullptr;  // MOD-501
        Microsoft::Xna::Framework::Graphics::Texture2D*      sourceNormals = nullptr;  // MOD-501
        Microsoft::Xna::Framework::Graphics::RenderTarget2D* destination   = nullptr;  // null = backbuffer
        int   width  = 0;
        int   height = 0;
        const RenderPipelineSettings* settings = nullptr;
        float elapsedSeconds = 0.0f;
        // MOD-505 camera block (SSAO and any depth-based pass):
        Microsoft::Xna::Framework::Matrix projection{};
        Microsoft::Xna::Framework::Matrix inverseProjection{};
        float nearPlane = 0.0f;
        float farPlane  = 0.0f;
    };
}
```

## Appendix B — `ShaderEffect` gap list (filled in by `MOD-16`)

| Need | Used by | Present today? | Task |
|---|---|---|---|
| `SetUniformMatrix` (4×4) | shadows, skybox, SSAO | *to verify* | MOD-215 |
| ≥4 named samplers | bloom, SSAO, IBL | *to verify* | MOD-216 |
| float/vec2/vec4 arrays | SSAO kernel | *to verify* | MOD-217 |
| `SetUniformInt` | tonemap mode, sample counts | *to verify* | MOD-218 |
| Named compile errors | every pass | *to verify* | MOD-219 |
| Per-pass sampler state | bloom (linear-clamp) | *to verify* | MOD-220 |

---

## Open questions for the project owner

These are the decisions this plan cannot make alone. Each has a **default** that will be followed if
no answer is given, so nothing is blocked on them.

| # | Question | Default if unanswered |
|---|---|---|
| OQ-1 | **Order of work.** Start on the HDR spine (Phase 0 → 1 → 2 → 3 → 7, first visible result = a tonemapped HDR frame), or on shadows first (Phase 8), which is the more visible feature but has a longer tail? | HDR spine first — everything else depends on float targets. |
| OQ-2 | **Reference renderer.** EasyGL under `OPENGLES3`, per CNAEXT.md §6? | Yes, EasyGL/`OPENGLES3`. |
| OQ-3 | **`CNA_CNAEXT` default.** Stay OFF (CNAEXT.md §7), or flip ON once the layer is real so it is actually compiled in CI? | Stay OFF; add a CI/preset build with it ON (`MOD-2`). |
| OQ-4 | **IBL binding shape** (`MOD-1222`): should `PbrEffect` take a `CNA::Graphics::ImageBasedLightEXT` (XNA header including a CNA header), or three separate `Texture*` setters (no layering inversion)? | Three separate setters. |
| OQ-5 | **Settings accessor** (`MOD-729`): expose `RenderPipelineSettings` from `GraphicsDevice`, or only from `RenderPipeline`? | Only from `RenderPipeline`. |
| OQ-6 | **Scene-draw contract** (`MOD-860`): app-driven `begin`/`end` for the shadow and prepass passes, or a `renderScene` callback the pipeline invokes? | App-driven begin/end. |
| OQ-7 | **Per-renderer rollout ambition.** All 17 shader-capable renderers (Phase 16), or a smaller committed set (EasyGL, Vulkan, D3D11, WebGPU) with the rest opportunistic? | Reference + Vulkan + D3D11 committed; the rest opportunistic. |
| OQ-8 | **Build verification depth.** Is a full `cmake-build-cnaext` configure+build expected in every session (slow from a clean container), or is targeted compilation of the touched module acceptable? | Full build of the affected target, incremental after the first configure. |
| OQ-9 | **Commit granularity.** One commit per `MOD` task (CLAUDE.md's rule), even when that means many small commits? | One commit per task. |
| OQ-10 | **Task-count preference.** Is this 820-task granularity right, or should tasks be coarser (one per class rather than one per method/shader)? | Keep this granularity. |
