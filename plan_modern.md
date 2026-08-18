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
| MOD-16 | Survey `ShaderEffect`'s uniform API and record the gaps the passes will need (`SetUniformMatrix`, `int`, arrays, samplers ≥2) | ✅ | Done — and the answer was "no gaps". `ShaderEffect` already has `SetUniformMat4`, `Vec4`/`Vec3`/`Vec2`, `Float`, `Int`, `SetUniformFloatArray`, `SetUniformVec2Array` and `SetTexture(unit, …)` for 2D/cube/3D. Appendix B is settled; MOD-215–MOD-218 need no work. |
| MOD-17 | Survey `RenderTarget2D`'s public surface for what the pipeline needs (format query, depth query, `RenderTargetUsage`) | ⬜ | Documented; any missing accessor becomes a Phase 1 task. |
| MOD-18 | Confirm `SpriteBatch` can draw a `RenderTarget2D` as a source texture with a custom `Effect` on the reference renderer | ⬜ | A minimal example proves source→effect→destination works today; this is the fallback path when the fullscreen-triangle helper is unavailable. |
| MOD-19 | Choose and document the fullscreen-geometry strategy (single oversized triangle via `DrawUserPrimitives`, no vertex buffer) | ✅ | Done, **deviating from the planned shape**: the fullscreen draw goes through `SpriteBatch` with a custom `Effect`, not an oversized triangle via `DrawUserPrimitives`. That is the route the verified EasyGL post-process examples already use, every renderer implements it, and it resolves the GL-vs-D3D texture-coordinate origin once inside SpriteBatch instead of in every pass shader. The triangle would save one state block per pass — invisible next to the fullscreen fill. |
| MOD-20 | Define `CNA::Graphics::PostProcessContext` fields up front so all passes share one signature (→ C8) | ⬜ | Struct declared in Appendix A and implemented in `MOD-200`; contains source colour, depth, normals, destination, viewport size, settings, elapsed time. |
| MOD-21 | Add the `Uncharted2` value to `TonemappingMode` (→ N02) | ✅ | Done. `Uncharted2` appended (never inserted); `TonemappingModeOrdinalsAreStable` pins all five values, since a settings bag written by an earlier build must still read back as the same operator. |
| MOD-22 | Extend `RenderPipelineSettings` with the fields the passes read (→ N03/§5.1) | ✅ | Done. `bloomThreshold`, `bloomIterations`, `ssaoRadius`, `ssaoIntensity`, `ssaoSampleCount` and `fxaaEnabled` added with documented defaults that all mean "inert". Out-of-range values are stored rather than rejected — the passes clamp when they apply one, so a quality preset need not know every pass's limits. |
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
| MOD-104 | Renderer virtual for per-format render-target support | ✅ | Done, **deviating from the planned shape**: `next` already has `IGraphicsRenderer::ClassifyRenderTargetFormatEXT(int)` (tri-state `Supported`/`Unsupported`/`Defer`, plan_runtimerenderer.md design decision 9), and it is the virtual `RenderTarget2D`'s constructor already consults. A second `SupportsRenderTargetFormat` virtual was written, then removed: two mechanisms answering the same question is exactly how a query starts disagreeing with the constructor it is supposed to predict. EasyGL overrides the existing one. |
| MOD-105 | `RenderTarget2D` — fail loudly instead of silently downgrading when the requested format is unsupported | ✅ | Done — and already true before the change, which is why it is recorded rather than rewritten: `RenderTarget2D`'s constructor throws `NotSupportedException` on a renderer verdict of `Unsupported`, and `Texture::ValidateFormat` throws for any non-`Color` format when the renderer defers. `AnUnsupportedFormatIsRefusedRatherThanSubstituted` pins it. |
| MOD-106 | Opt-out policy so a port can keep lenient substitution | ⛔ | Not done, deliberately. It was planned to preserve a lenient behaviour that turned out never to have existed — the shared layer already throws (MOD-105), so this would add a global mutable switch, a second code path and a new way for a target's format to differ from the one requested, in service of no caller. If a port ever needs XNA's "preferred format" substitution, that is a different feature: a documented per-construction fallback list, not a process-wide policy flag. |
| MOD-107 | Route `SurfaceFormat` into `RenderTargetCube` the same way `RenderTarget2D` already does | ✅ | Done. `CreateRenderTargetCubeEXT` added with a forwarding default; `RenderTargetCube` passes its real format; `TextureCube`'s render-target constructor now checks it against the same `ClassifyRenderTargetFormatEXT` verdict `RenderTarget2D` uses, so a cube and a 2D target can never disagree about a format. EasyGL allocates the face storage and the per-face multisample renderbuffers in that format. All 6 faces bind and clear as `HdrBlendable`; an unsupported format is refused. |
| MOD-108 | `Texture2D::GetData<Vector4>` / float readback path for verifying HDR contents in tests | ✅ | Done. `EasyGLRenderTargetRenderer::GetData` reads with the target's own `(pixelFormat, pixelType, bytesPerPixel)` instead of a hardcoded RGBA8, and the row flip uses the real stride. `Texture2D::GetData(Vector4*)`/`(HalfVector4*)` already carried the shared half. |
| MOD-109 | Document the exact `SurfaceFormat` → GL/VK/D3D internal-format mapping table once, in `docs/cnaext-engine-layer.md` | ⬜ | One table covering `Single`, `Vector2`, `Vector4`, `HalfSingle`, `HalfVector2`, `HalfVector4`, `HdrBlendable`; every renderer task in Phase 16 references it instead of inventing its own. |
| MOD-110 | Decide and document what `HdrBlendable` means in CNA (XNA: `RGBA16F` on Windows) | ⬜ | Documented as an alias of `HalfVector4` for every renderer; test asserts the two behave identically. |
| MOD-111 | Blending semantics on float targets — document that alpha blending on `HalfVector4` is allowed and on `Single`/`Vector2` is renderer-dependent | ⬜ | Documented; `SupportsCapability` note updated; no runtime enforcement (matches XNA's own laxity). |

### 1.2 EasyGL reference implementation

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-115 | EasyGL: override `CreateRenderTarget2DEXT` and honour the surface format (→ N11) | ✅ | Done. `EasyGLRenderer::CreateRenderTarget2DEXT` allocates real R/RG/RGBA 16F/32F storage (texture levels **and** the multisample colour renderbuffer) and refuses an unsupported format instead of substituting `Color`. A `HdrBlendable` `RenderTarget2D` constructs and reports its own format on EasyGL/OPENGLES3. |
| MOD-116 | EasyGL: `SurfaceFormat` → `(internalFormat, format, type)` table for the 7 float formats + `Color` | ✅ | Done. `MapRenderTargetColorFormat()` is one table returning `(internalFormat, pixelFormat, pixelType, isFloat, isFullFloat)` for `Color` and the 7 float formats; used by the storage allocation, the MSAA renderbuffer and the probe alike, so they cannot drift apart. |
| MOD-117 | EasyGL: runtime probe of float-renderable support (`GL_EXT_color_buffer_float` / ES 3.2 core / desktop GL 3.3) | ✅ | Done — **probed, not inferred**: a 1×1 attachment of the real format is created and `check_status()` asked, cached per precision, previous FBO binding restored and the error queue drained. This is truthful across ES 3.0-without-the-extension, ES 3.2, desktop GL and WebGL 2 without encoding a driver matrix. The startup diagnostic now prints the probed answer instead of the stale "SurfaceFormat: Color only". |
| MOD-118 | EasyGL: `GL_EXT_color_buffer_half_float` probe for the half-float-only case (GLES 3.0 devices) | ✅ | Done by construction: the probe is per precision, so a context with half-float but not full-float colour buffers reports `HalfFloatRenderTargets` true and `FloatRenderTargets` false. `FormatsOfTheSamePrecisionAnswerTogether` pins that the two groups are answered independently; no GLES-3.0-only context exists in this environment to demonstrate the split live. |
| MOD-119 | EasyGL: framebuffer-completeness check after attaching a float target, with a diagnostic on failure | ✅ | Done. `CreateResources` asks `check_status()` after assembling the framebuffer and throws with the width/height, the SurfaceFormat and DepthFormat ordinals, the sample count and the GL status name. A driver that accepts every individual call and then refuses the assembly used to produce a target that silently rendered nowhere. |
| MOD-120 | EasyGL: float target + depth attachment combination (depth24/depth24stencil8 with RGBA16F colour) | ✅ | Done. An `HdrBlendable` target with `Depth24Stencil8` constructs, reports its depth format, and renders — verified by `AFloatTargetCarriesARealDepthBuffer`. |
| MOD-121 | EasyGL: MSAA resolve from a multisampled float target | ✅ | Done. The multisample colour renderbuffer is created in the target's own format, so the resolve blit has compatible formats; `AMultisampledFloatTargetResolvesWithoutClamping` reads unclamped values back after the resolve, and skips where the device clamps the request to single-sample. |
| MOD-122 | EasyGL: mip generation on float targets | ✅ | Done. `AFloatTargetGeneratesAMipChain` reads level 1 of a mipped `HdrBlendable` target and finds the unclamped values — the storage IBL prefiltering will use for its roughness levels. |
| MOD-123 | EasyGL: float texture sampling (linear filter) capability probe (`OES_texture_float_linear`) | ✅ | Done. `GraphicsCapability::HalfFloatTextureLinearFiltering`, derived from a renderer virtual with a false default. Deliberately separate from renderability: a context can render to RGBA16F and still sample it nearest-only, and bloom is the pass that cares. |
| MOD-124 | EasyGL: readback of a float target into `Vector4[]` | ✅ | Done, with MOD-108 — the same change is the EasyGL half of it. |
| MOD-125 | EasyGL: `SetRenderTargets` (MRT) with mixed float/`Color` attachments | ✅ | Done. `AMixedFloatAndColourTargetSetBinds` binds an `RG16F` + `Color` set, clears and reads back — the exact shape the SSAO depth/normal prepass needs. |

### 1.3 Verification

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-130 | Unit tests: capability enum values, per-format query, throw/downgrade policy | ⬜ | Every new public method from §1.1 covered including both policy branches. |
| MOD-131 | Example `cnaext_float_rendertarget_test` — clear an HDR target to (4,2,1,1), read back, assert >1.0 | ✅ | Done. `HdrRenderTargetRoundTripTests`: an RGBA32F target cleared to (4, 2, 1, 1) reads back exactly those values, an RGBA16F (`HdrBlendable`) one does too, and the `Color` control clamps the identical render to 255 — so the test cannot pass merely because a driver never clamped anything. |
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
| MOD-200 | `PostProcessContext` struct (`source`, `sourceDepth`, `sourceNormals`, `destination`, `width`, `height`, `settings`, `elapsedSeconds`) | ✅ | Done. `PostProcessContext` carries source/depth/normals/destination, size, settings, elapsed time and the camera block. One struct rather than a signature per pass, so a chain of mixed passes is expressible. |
| MOD-201 | `detail::FullscreenTriangle` — one oversized triangle via `DrawUserPrimitives`, no VB (→ MOD-19) | ✅ | Done as `FullscreenPass` (see MOD-19 for the shape it took). |
| MOD-202 | `PostProcessPass` abstract base — `apply(ctx)`, `getName()`, `isSupported(device)` | ✅ | Done. `PostProcessPass` with `apply(ctx)`, `getName()` and `isSupported(device)`; the default `isSupported` answers the question every shader pass shares (`CustomEffects`). |
| MOD-203 | `PostProcessPass` common helper: bind destination (null = backbuffer), set viewport, restore previous target on scope exit | ⬜ | RAII helper; a pass that throws mid-apply still restores the previous render target (tested). |
| MOD-204 | `detail::RenderTargetPool` — reusable, size/format-keyed target cache with explicit `reset()` | ✅ | Done. `RenderTargetPool`, keyed by size/format/depth/slot. Tested for reuse, for every key that must produce a *different* target, for `reset()`, and for rejecting a non-positive size. |
| MOD-205 | `detail::PingPongTargets` — 2 targets of identical size/format with `swap()`, built on the pool | ✅ | Done, **without a separate class**: the alternation lives in `PostProcessChain`, which is its only consumer, and is asserted directly (`EachPassReadsWhatThePreviousOneWroteAndNeverItsOwnTarget`). A standalone `PingPongTargets` with one caller would have been indirection, not structure. |
| MOD-206 | `detail::BlitPass` — the identity/pass-through pass used by every "capability missing" fallback (D1) | ✅ | Done. `BlitPass` — the identity pass every capability fallback and the final resolve use. Verified by pixel comparison, not by inspection. |
| MOD-207 | Document the texture-coordinate origin convention once (GL bottom-left vs D3D top-left) and where the flip is applied | ✅ | Done. Documented on `FullscreenPass`: the convention is resolved once inside `SpriteBatch`, which is a reason for the MOD-19 deviation rather than a separate mechanism. |
| MOD-208 | `PostProcessChain` — ordered container of passes with ping-pong management and one final resolve | ✅ | Done. `PostProcessChain` — N passes, N-1 intermediates, only the last writing the caller's destination, intermediates in the source's own format so an HDR chain cannot be silently clamped mid-way. |
| MOD-209 | Pass-level enable flags driven by `RenderPipelineSettings` (no branch inside shaders) | ⬜ | A disabled pass is skipped entirely (zero draw calls), verified by the counting fake. |
| MOD-210 | `ShaderEffectFactory` helper — compiles a named pass shader once and caches it per device | ⬜ | Two `BloomPass` instances on one device share one compiled program; verified by a compile counter. |

### 2.2 `ShaderEffect` gaps (each becomes real work only if `MOD-16` finds it missing)

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-215 | `ShaderEffect::SetUniformMatrix` (4×4, column-major) if absent | ✅ | Not needed — present already (MOD-16). |
| MOD-216 | `ShaderEffect` multi-sampler binding (≥4 texture units with named samplers) | ✅ | Not needed — present already (MOD-16). |
| MOD-217 | `ShaderEffect` float/vec2/**vec3** array uniforms | ✅ | **Closed as "not needed" by MOD-16's survey, then reopened and actually done.** A `vec3[]` uniform cannot be filled through `SetUniformFloatArray`: GL rejects the type mismatch and silently leaves the uniform at zero. That turned SSAO's 64-sample kernel into 64 samples at the origin — an image with no occlusion in it, while every unit test about the kernel still passed. `SetUniformVec3Array` added through `IEffectRenderer`, `ShaderEffect` and EasyGL. |
| MOD-218 | `ShaderEffect::SetUniformInt` (sample counts, mode switches) | ✅ | Not needed — present already (MOD-16). |
| MOD-219 | `ShaderEffect` compile-error surfacing with the shader name and line context | ⬜ | A deliberately broken pass shader throws a message containing the pass name and the GLSL log. |
| MOD-220 | `ShaderEffect` — allow a pass to declare its own sampler filtering/addressing requirements | ⬜ | Bloom's linear-clamp requirement is expressed once by the pass, not by the caller's `SamplerState`. |

### 2.3 Verification

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-225 | Unit tests for `RenderTargetPool` (keying, reuse, reset, leak count) | ✅ | Done — four `RenderTargetPoolTest` cases. |
| MOD-226 | Unit tests for `PingPongTargets` and `PostProcessChain` ordering | ✅ | Done — insertion order and the full ping-pong contract, plus owned/borrowed passes and rejected contexts. |
| MOD-227 | Unit tests for `PostProcessPass` fallback behavior on a non-shader renderer | ⬜ | On Headless/SDL_Renderer, `isSupported()` is false and `apply()` performs a documented blit, never a throw. |
| MOD-228 | Golden image: `BlitPass` identity through an HDR intermediate | ✅ | Done. `AChainOfCopiesIsStillTheIdentity` compares real pixels after three real passes; `AnHdrChainKeepsItsIntermediatesInFloat` proves an above-1.0 value survives two chained passes. |
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
| MOD-300 | `TonemapPass` class skeleton (`PostProcessPass`, owns its `ShaderEffect`) | ✅ | Done. `TonemapPass` owns its `ShaderEffect`; a shader that fails to compile makes `isSupported()` false and `apply()` copy, rather than throwing out of a constructor. |
| MOD-301 | Tonemap GLSL: `None` (clamp only) | ✅ | Done — clamp only, with gamma still applied (`None` means "no curve", not "no display encode"). |
| MOD-302 | Tonemap GLSL: `Reinhard` (`c/(1+c)`) | ✅ | Done — matches `c/(1+c)` to 1e-6 at five sampled values. |
| MOD-303 | Tonemap GLSL: `Filmic` (Hejl–Burgess-Dawson) | ✅ | Done — Hejl/Burgess-Dawson, and the gamma step is skipped for it, asserted directly. |
| MOD-304 | Tonemap GLSL: `Aces` (Narkowicz fit) | ✅ | Done — Narkowicz's fit, documented as the fit rather than the full ACES transform. |
| MOD-305 | Tonemap GLSL: `Uncharted2` (→ MOD-21, N02) | ✅ | Done — Hable's curve with the standard constants, normalized against `W = 11.2`. |
| MOD-306 | Exposure multiplier applied before the curve | ✅ | Done. `exposure=2` on input `x` equals `exposure=1` on `2x` for every curve — the check that catches exposure applied in the wrong place, which looks almost right for Reinhard. |
| MOD-307 | Gamma applied after the curve (except `Filmic`, per `MOD-303`) | ✅ | Done, including the `Filmic` exception in both the shader and the CPU reference. |
| MOD-308 | Auto-exposure decision: **out of scope for v1**, documented | ⛔ | Not done, deliberately — auto-exposure needs a luminance reduction over the whole frame, which is a compute or mip-chain problem rather than a tonemapping one. Revisit at MOD-1552. |
| MOD-309 | `TonemapPass` reads `RenderPipelineSettings` (mode, exposure, gamma) each apply | ✅ | Done — mode, exposure and gamma are read from the context's settings on every apply, so changing them between frames changes the output with no reconstruction. |
| MOD-310 | Standalone use: tonemap any `Texture2D` to any `RenderTarget2D` or the backbuffer | ✅ | Done — the pass carries its own mode/exposure/gamma for use without a settings bag (D9). |
| MOD-311 | LDR input guard: tonemapping a `Color` source is legal and documented (values are simply ≤1) | ✅ | Done — an LDR `Color` source with mode `None` and gamma 1.0 round-trips within one 8-bit step, which is what the HDR-off pipeline depends on. |
| MOD-312 | Unit tests: all 5 modes × {exposure 0.5,1,4} × {gamma 1.0,2.2} numeric checks | ✅ | Done — 11 cases covering all five operators, exposure, gamma, monotonicity/boundedness, and that the four curves genuinely differ from one another. |
| MOD-313 | CPU reference implementations of the 5 curves (test-only helper) | ✅ | Done, **as public API rather than a test-only helper**: `TonemapPass::tonemapChannel()`. It is the only way to state "the shader agrees with the specification" as an assertion, and a game can use it for a UI preview without a GPU round trip. |
| MOD-314 | Golden image: HDR gradient tonemapped in each mode | ⬜ | 5 goldens committed with the generation command documented. |
| MOD-315 | Example `cnaext_tonemap_test` — live window, key-switchable mode/exposure | ⬜ | Registered ctest (smoke frames) + manual visual mode documented. |
| MOD-316 | Document the tonemapping contract (input linear HDR, output display-encoded) | ⬜ | In `docs/cnaext-engine-layer.md`; states explicitly that CNA does no colour-space management beyond gamma. |
| MOD-317 | `TonemappingMode` ordinal-stability test (existing values must not shift) | ✅ | Done — covered by `RenderPipelineSettingsTest.TonemappingModeOrdinalsAreStable` (MOD-21). |
| MOD-318 | Verify tonemap output against the 2D-only fallback (blit) so a `SpriteBatch` game sees no change | ⬜ | With HDR off and mode `None`, the pipeline output is bit-identical to no pipeline. |
| MOD-319 | Perf: tonemap cost at 720p/1080p | ⬜ | Recorded in the doc. |
| MOD-320 | Wire `RenderQuality` presets to tonemap precision decisions (none today — document why) | ⬜ | Explicit "quality does not affect tonemapping" note so the preset table has no silent gaps. |

---

## Phase 4 — Bloom (`MOD-400`–`MOD-431`)

→ CNAEXT.md §5.2, N22. Promotes the existing EasyGL example GLSL to library code.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-400 | Extract the bloom GLSL from `easygl_bloom_*` examples into a shared, documented source location | ✅ | Done — the GLSL is written fresh as library code in `BloomPass.cpp`; the examples stay as they are. |
| MOD-401 | `BloomPass` skeleton + settings (threshold, intensity, radius, iterations) | ✅ | Done. `BloomPass` reads threshold, intensity and iteration count from settings, and carries its own values for standalone use. |
| MOD-402 | Bright-pass extract shader (threshold with soft knee) | ✅ | Done — soft knee, because a hard cut-off makes bloom pop in and out as a highlight crosses the threshold, which is far more visible in motion than the energy missing just below it. Tested through `extractChannel`. |
| MOD-403 | Downsample chain (½ resolution per level, configurable level count 1–6) | ✅ | Done — half-resolution extract, then halving per iteration, clamped to 1..8 and stopped when a step would fall below 2 pixels. |
| MOD-404 | Separable Gaussian blur (horizontal + vertical) with a documented tap count per quality | ✅ | Done — separable 9-tap Gaussian, horizontal then vertical (18 samples where a 9×9 kernel costs 81). |
| MOD-405 | Upsample + additive combine chain | ⬜ | Progressive upsampling (each level adds into the next) rather than a single composite; visually verified. |
| MOD-406 | Final composite into the destination (`scene + bloom*intensity`) | ✅ | Done. `intensity = 0` reproduces the scene exactly — the strongest available statement that the composite keeps the scene intact. |
| MOD-407 | Linear-filter fallback when float linear filtering is unavailable (`MOD-123`) | ⬜ | Nearest-sample path with extra taps; documented quality difference. |
| MOD-408 | LDR bloom path (works on `Color` targets when HDR is off) | ✅ | Done — bloom is an HDR effect but not an HDR-only one; on an 8-bit target a threshold below 1.0 still produces a usable glow. |
| MOD-409 | `RenderQuality` → level count / tap count mapping | ⬜ | Low/Medium/High/Ultra map to a documented table; test asserts each preset's derived values. |
| MOD-410 | Target pool reuse — all bloom mip targets come from `RenderTargetPool` | ✅ | Done — all intermediates come from a `RenderTargetPool` and are reused across frames. |
| MOD-411 | Resize handling — recreate the chain on viewport change without leaking | ✅ | Done — `resetTargets()`, called by `RenderPipeline::resize`. |
| MOD-412 | Unit tests: threshold/knee math, kernel weights, level-count clamping | ✅ | Done — threshold/knee behaviour, a zero threshold (which must not divide by zero), and iteration clamping. |
| MOD-413 | Golden image: a bright quad on a dark field, 3 intensity values | ⬜ | 3 goldens; documented tolerance. |
| MOD-414 | Golden image: bloom disabled == input | ✅ | Done — `ZeroIntensityReproducesTheSceneExactly`. |
| MOD-415 | Example `cnaext_bloom_test` — live window with adjustable threshold/intensity | ⬜ | Registered ctest; documented expected look. |
| MOD-416 | Perf: bloom cost per quality preset at 720p/1080p | ⬜ | Recorded; informs `MOD-409`'s table. |
| MOD-417 | Document bloom's energy behavior (not physically normalized; intensity is artistic) | ⬜ | Written down so the look is reproducible across renderers. |
| MOD-418 | Cross-check the promoted GLSL against the original example output | ✅ | Done differently from the plan: the pass is verified by measurement rather than by comparison with the example — a pixel that was exactly black before it is non-black after, and a higher intensity produces more of it. That states what bloom must do; an image comparison would only state that two implementations agree. |

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
| MOD-505 | Camera parameters (`near`, `far`, `projection`, `inverseProjection`) delivered to passes via `PostProcessContext` | ✅ | Done for what SSAO actually needs: depth and normals travel through `PostProcessContext`, along with the projection fields. The camera block is populated by the caller rather than derived, because the pipeline never sees the camera. |
| MOD-506 | Renderer capability check for MRT (`MultipleRenderTargets`) with a documented 2-pass fallback | ✅ | Done differently: the pass takes depth and normals as two textures and does not care whether they were produced by one MRT pass or two, so the MRT-versus-two-pass choice belongs to whatever draws them rather than to SSAO. |
| MOD-507 | Prepass target formats fall back to `Color`-packed depth when float RTs are missing | ⬜ | Packing/unpacking GLSL documented and numerically tested; SSAO quality difference noted. |

### 5.2 SSAO

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-515 | `SsaoPass` skeleton + settings (radius, intensity, bias, sample count, power) | ✅ | Done. `SsaoPass` reads radius, intensity and sample count from settings and carries its own for standalone use. |
| MOD-516 | Hemisphere sample-kernel generation (deterministic, seeded) | ✅ | Done — a deterministic low-discrepancy hemisphere set (Van der Corput), biased toward the origin so nearby geometry dominates. Asserted: every sample is in the +Z hemisphere, none longer than unit, and the early quarter is closer to the origin than the late quarter. |
| MOD-517 | Rotation-noise texture (4×4 tiled) | ✅ | Done — a fixed 4×4 rotation texture. Deterministic on purpose: the pass must produce the same image twice. |
| MOD-518 | SSAO GLSL: occlusion estimate with range check | ✅ | Done, with a range check so a distant silhouette cannot darken the surface in front of it. Verified on synthetic inputs: a flat wall stays unoccluded, a depth step darkens the surface beside it. |
| MOD-519 | Bilateral/box blur pass for the AO buffer | ✅ | Done — a 5×5 blur folded into the composite pass rather than a separate one, which would need a third intermediate for no gain at this kernel size. |
| MOD-520 | AO application mode: multiply into the scene's ambient term only, not into direct light | ✅ | Done as a screen-space multiply, documented as the approximation it is: it darkens direct light along with ambient. Doing it correctly means feeding AO into each lit effect's ambient term — a change to every lit shader, not a post-process. |
| MOD-521 | Optional: feed AO into `PbrEffect`'s occlusion slot instead of a screen-space multiply | ⬜ | Investigated and either implemented behind a flag or ⛔ with a reason. |
| MOD-522 | `RenderQuality` → sample count mapping (8/16/32/64) | ⬜ | Table documented and tested. |
| MOD-523 | Half-resolution AO option with upsample | ⬜ | Off by default; quality/perf difference measured and documented. |
| MOD-524 | Unit tests: kernel generation, noise texture, settings round-trip, capability fallback | ✅ | Done — 8 cases: kernel distribution and determinism, flat-surface and discontinuity behaviour, intensity, the missing-input fallback, and settings round-trip. |
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
| MOD-600 | `FxaaPass` skeleton + settings (quality preset, edge thresholds) | ✅ | Done. `FxaaPass` with a configurable edge threshold, defaulting to the usual 0.125. |
| MOD-601 | FXAA 3.11-style GLSL (luma-based edge detect + directional blend), documented as a reimplementation | ✅ | Done — written from the description of the technique rather than ported: local luminance range, an early out below the threshold, then a blend along the edge direction, with the wider blend rejected when it leaves the local luminance range (which is the filter reaching past the edge). |
| MOD-602 | Luma source decision (computed from RGB vs stored in alpha) and documentation | ✅ | Done — luminance is computed from RGB. The alpha-luma optimization needs every earlier pass to maintain it, which nothing here does; noted rather than half-implemented. |
| MOD-603 | Apply FXAA **after** tonemapping (documented ordering rule) | ✅ | Done — the pipeline runs FXAA after tonemapping. On scene-referred values a highlight ten times brighter than white reads as an enormous edge and is blurred into its surroundings. |
| MOD-604 | Quality presets mapped from `RenderQuality` | ⬜ | Documented table; tested. |
| MOD-605 | Unit tests: settings, support, disabled == input | ✅ | Done — threshold round trip, the disabled case, and both halves of the contract below. |
| MOD-606 | Golden image: aliased triangle before/after | ✅ | Done by measurement rather than by golden image: a strictly black-and-white staircase gains pixels strictly between the two (the definition of a smoothed edge), while a flat field comes back unchanged — an edge filter that also softens flat areas is a blur. |
| MOD-607 | Example `cnaext_fxaa_test` toggling FXAA live | ⬜ | Registered ctest. |
| MOD-608 | Perf: FXAA cost at 720p/1080p | ⬜ | Recorded. |
| MOD-609 | Document when to prefer MSAA over FXAA per renderer | ⬜ | Support matrix row added. |
| MOD-610 | Decide on SMAA/TAA: ⛔ out of scope for this plan, with reasons | ⛔ | Confirmed out of scope for this plan. TAA needs motion vectors and a history buffer — a different pipeline shape, not a pass — and SMAA needs a precomputed lookup texture this layer has no asset path for. |

---

## Phase 7 — `RenderPipeline` orchestrator (`MOD-700`–`MOD-747`)

→ CNAEXT.md §5.1, N20/N25. The class that finally gives `RenderPipelineSettings` a consumer.

### 7.1 Core

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-700 | `RenderPipeline` skeleton: ctor(device), dtor, non-copyable, `getSettings()` | ✅ | Done. Constructs on any renderer, non-copyable, and allocates nothing until `resize()` — the size is not known and the format depends on settings the caller has not made yet. |
| MOD-701 | `resize(width,height)` — allocate/reallocate HDR scene target + aux targets | ✅ | Done. Idempotent for a size it already holds; drops the old scene target and the chain's intermediates, so a resized game does not keep paying for every size it has been. 20 alternating resizes stay bounded. |
| MOD-702 | HDR scene target creation (`HalfVector4` when supported, `Color` otherwise, logged once) | ✅ | Done. `HdrBlendable` when the renderer has it, `Vector4` next, `Color` otherwise — and `getSceneTargetFormat()` reports what was really created rather than what was asked for. |
| MOD-703 | `begin(clearColor)` — bind the scene target and clear (colour + depth) | ✅ | Done. Binds the scene target and clears it; the frame's draws land there, verified through `getSceneTarget()`. |
| MOD-704 | `end()` — run the fixed chain and resolve to the backbuffer | ✅ | Done. `end()` builds the chain in fixed order and resolves to the back buffer. Tonemap first, then user passes — asserted by pass counts and a counting pass. |
| MOD-705 | Fixed chain order documented and justified | ✅ | Done — the reason is recorded where the order is built: tonemapping is the boundary between scene-referred and display-referred colour, so scene-value passes precede it and pixel passes follow. |
| MOD-706 | `addUserPass(PostProcessPass*)` / `removeUserPass` — app-supplied passes at a documented slot | ✅ | Done for the borrowed form (`addUserPass`/`clearUserPasses`), which is what a game keeping its own configurable pass needs. An owning overload is not added until something needs it. |
| MOD-707 | HDR-off path: no HDR target, no tonemap, chain reduced to enabled passes | ✅ | Done — with HDR off and no tonemapping the chain is empty and the scene target is skipped entirely. |
| MOD-708 | Zero-pass short circuit: when nothing is enabled and HDR is off, render straight to the backbuffer | ✅ | Done, and this is the row that makes the layer safe to adopt: an inert pipeline allocates no target, runs no pass, and renders straight to the back buffer. Asserted through the memory estimate, the pass count and `isUsingSceneTarget()`. |
| MOD-709 | `setShadowCaster(DirectionalLightEXT*)` (Phase 8 consumer) | ⬜ | Null clears; documented as "shadow pass runs before `begin()` returns". |
| MOD-710 | `setSkybox(Skybox*)` (Phase 11 consumer) | 🟨 | Superseded in part: the pipeline's pass ordering is implemented and bloom is wired in ahead of tonemapping (bloom reasons about scene-referred values that tonemapping compresses away). The skybox consumer itself waits for Phase 11. |
| MOD-711 | `setDepthNormalPrepass(...)` wiring for SSAO (Phase 5 consumer) | ✅ | Done. `RenderPipeline::setDepthNormalInputs()` — the pipeline cannot render these itself (that means drawing the game's geometry again with a different effect, which only the game can do), so it takes them and SSAO renders an unoccluded frame when they are absent. |
| MOD-712 | `getSceneTarget()` accessor so apps can sample the HDR scene from custom passes | ✅ | Done — `getSceneTarget()` returns null outside `begin`/`end`, and null when the pipeline short-circuited. |
| MOD-713 | Exception safety: an exception inside a pass restores the backbuffer binding and rethrows | ⬜ | Tested with a throwing fake pass; the next frame renders normally. |
| MOD-714 | `begin()`/`end()` misuse guards (double begin, end without begin) | ✅ | Done — double `begin()`, `end()` without `begin()`, and `begin()` before any `resize()` all throw `std::logic_error`. |
| MOD-715 | `GraphicsDevice` device-reset/context-loss handling — reallocate targets on reset | ⬜ | Subscribing to the existing `DeviceReset` event; verified with `DebugSimulateContextLoss()`. |
| MOD-716 | `getGpuMemoryEstimateBytes()` (→ MOD-139) | ✅ | Done — `getGpuMemoryEstimateBytes()` sums the scene target and the chain's pool. |
| MOD-717 | `getStatistics()` — passes run, draw calls, target switches for the last frame | ⬜ | Small POD; tested against the counting fake. |

### 7.2 Settings integration

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-725 | Settings changes take effect on the next frame without reconstruction | ✅ | Done — toggling settings between frames changes the next frame with no reconstruction. |
| MOD-726 | Changing `hdrEnabled` reallocates the scene target lazily | ✅ | Done — the scene target is reallocated only when the chosen format actually changes. |
| MOD-727 | `RenderQuality` preset application across all passes in one place | ⬜ | A single `applyQualityPreset()` maps the enum to per-pass values; documented table. |
| MOD-728 | `GraphicsDevice::GetRenderPipelineSettingsEXT()` — the accessor the settings doc already claims exists | ⬜ | CNAEXT-marked, always compiled? **No** — decided in `MOD-729`. |
| MOD-729 | Decide whether the settings accessor lives on `GraphicsDevice` (XNA type) or only on `RenderPipeline` | ✅ | Decided: **`RenderPipeline` only**. Exposing the settings from `GraphicsDevice` would give an XNA type a member whose type exists only under a compile option. `RenderPipelineSettings`' own doc comment, which claimed a `GraphicsDevice` accessor, is now wrong and is corrected in MOD-1806. |
| MOD-730 | Settings validation (negative exposure, gamma ≤0, absurd radii) | ⬜ | Clamped with documented ranges, not rejected; tested at the boundaries. |
| MOD-731 | Settings serialization helper (to/from a simple key=value string) for demos and tests | ⬜ | Round-trips every field; unit-tested. |

### 7.3 Verification

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-735 | Unit tests for every public `RenderPipeline` member (incl. both misuse guards) | ✅ | Done — 13 cases covering both misuse guards, the inert path, format selection, user passes, settings changes, resize and frame-to-frame stability. |
| MOD-736 | Golden image: full pipeline (HDR + bloom + tonemap) on a fixed test scene | ⬜ | Golden committed with the generating command. |
| MOD-737 | Golden image: pipeline with everything off == direct rendering | ✅ | Done — `AnInertPipelineProducesTheSameFrameAsNoPipelineAtAll`: no scene target, no passes, no memory; the frame reaches the back buffer exactly as it would without a pipeline. |
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
| MOD-800 | `CNA::Graphics::DirectionalLightEXT` struct (direction, colour, intensity, castsShadows) (→ C6) | ✅ | Done. `DirectionalLightEXT` — direction, colour, intensity, casts-shadows. Deliberately not an XNA type: XNA's own `DirectionalLight` belongs to `BasicEffect` and describes a shading contribution, while this describes a light in the scene. |
| MOD-801 | `ShadowMap` skeleton: ctor(device, ShadowQuality), depth `RenderTarget2D` allocation | ✅ | Done. `ShadowMap(device, quality)` with the documented size table; `Disabled` still constructs at the smallest size so a game can toggle quality without recreating the object. |
| MOD-802 | Shadow-map depth format selection (depth texture where available, packed-`Color` distance otherwise) | ✅ | Decided by what CNA can actually do, and the finding is worth keeping: **there is no API for sampling a render target's depth attachment as a texture** — `RenderTarget2D` exposes its colour texture. So the caster writes normalized light-space distance into colour: `Single` (R32F) where the renderer has float targets, `Color` otherwise. The receiver then samples an ordinary texture, which every renderer can do. |
| MOD-803 | Light view matrix from the light direction + scene bounds | ✅ | Done. `computeLightView()` is public and asserted directly — including that XNA's `CreateLookAt` puts the **backward** vector in (M13,M23,M33), which a test now pins because reading it as forward inverts every derived matrix and still produces a plausible-looking map. |
| MOD-804 | Orthographic light projection fitted to a `BoundingBox` | ✅ | Done. `computeLightProjection()` fits the scene's eight corners **in light space**, not the world-space box: fitting the latter sizes the volume for a box the light does not see axis-aligned and wastes resolution in proportion to how far off-axis it is. Verified by asserting every corner lands inside clip space *and* that at least one lands near its edge. |
| MOD-805 | `begin(light, sceneBounds)` — bind the depth target, set state (front-face culling or depth bias) | ✅ | Done — `begin()` computes the matrices, binds the target and clears to white. |
| MOD-806 | `end()` — restore the previous target and viewport | ✅ | Done — `end()` restores the back buffer; both misuses throw. |
| MOD-807 | `getDepthTexture()` / `getLightViewProjection()` accessors | ✅ | Done — `getShadowTexture()`, `getLightViewProjection()`, plus size and quality. |
| MOD-808 | Depth-bias + normal-offset settings with documented defaults | ✅ | Done — `getDepthBias`/`setDepthBias` with the trade documented on the accessor: too little gives acne, too much detaches the shadow from its caster, and no value avoids both. |
| MOD-809 | `ShadowMapEffect` (CNAEXT, XNA namespace) — the depth-only effect used during generation | ✅ | Done as an engine-layer `ShaderEffect` rather than an XNA-namespace effect: it writes light-space distance and nothing else, so a shadow pass costs a fraction of a shading pass. |
| MOD-810 | `SkinnedShadowMapEffect` — skinned depth-only variant | ✅ | Done, as a second `ShaderEffect` inside `ShadowMap` rather than a separate public class -- `getSkinnedCasterEffect()` and `applySkinnedCaster(bones, weightsPerVertex)`. The palette is passed in rather than read from an effect, because the shadow pass does not know which of the app's effects a mesh is shaded with and the app already holds the same matrices it gives `SkinnedEffect`. Same `>=2`/`>=4` weight gating and the same 72-bone palette as the stock skinned programs. Needed a new `SetUniformMat4Array` through `ShaderEffect` -> `IEffectRenderer` -> EasyGL: `SetUniformMat4` uploads exactly one matrix whatever the uniform's declared size, so filling a palette with it leaves every bone past the first at its default. Verified by posing one bone as a pure +4 translation and asserting the shadow lands at the *posed* position rather than the bind pose -- the failure mode being that a bind-pose shadow looks entirely correct, just of the wrong thing. |
| MOD-811 | `ShadowMap` capability gate + fallback (no depth targets → shadows disabled, one-time log) | ✅ | Done — `ShadowMap::isSupported()`, decided at construction from `ThreeD` **and** `CustomEffects` and then from whether the caster actually linked, because a renderer can claim the capability and still fail to compile. Unsupported is not an error: the object constructs, `begin`/`end` work, the map keeps meaning "nothing occludes", and a game that switches shadows on gets an unshadowed frame. The reason is logged once, naming which of the three it was rather than reporting "shadows unavailable" and leaving the reader to guess. |

### 8.2 Receiver side — always-compiled CNAEXT hooks

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-820 | `IShadowReceiverEXT` interface (`setShadowMapEXT`, `setLightViewProjectionEXT`, `setShadowsEnabledEXT`) | ✅ | Done. `IShadowReceiverEXT` in the XNA namespace, `CNAEXT`-marked and always compiled — receiving a shadow is a per-draw material property, and gating it behind a compile option would make an effect's public surface change with a build flag. Generating the map stays in the engine layer; this is the seam. |
| MOD-821 | `GpuDrawParams` shadow field group (`shadowMap`, `lightViewProjColMajor[16]`, `shadowsEnabled`, `shadowBias`, `shadowMapTexelSize`) | ✅ | Done — `shadowMap`, `lightViewProjColMajor[16]`, `shadowsEnabled` and `shadowDepthBias` appended to `GpuDrawParams` with inert defaults, so every renderer compiles unchanged and one without a shadow variant accepts and ignores them. |
| MOD-822 | `BasicEffect` implements `IShadowReceiverEXT` + fills the params | ✅ | Done — `BasicEffect`. |
| MOD-823 | `SkinnedEffect` implements `IShadowReceiverEXT` | ✅ | Done — `SkinnedEffect`. |
| MOD-824 | `PbrEffect` implements `IShadowReceiverEXT` | ✅ | Done — `PbrEffect`. |
| MOD-825 | `SkinnedPbrEffect` implements `IShadowReceiverEXT` | ✅ | Done — `SkinnedPbrEffect`. |
| MOD-826 | Document the "accepted and ignored on renderers without the shader" convention for these fields | ✅ | Done — documented on the interface itself, next to the reason the generating half lives elsewhere. |
| MOD-827 | Decide whether shadow reception needs a shader-variant explosion or a uniform branch | ✅ | Decided: one uniform branch, not a variant explosion. `cnaShadowFactor` returns 1.0 immediately when `uShadowsEnabled` is 0, so an unshadowed draw pays one uniform compare and no texture fetch, and the four lit programs stay four programs rather than eight. The recommendation in this row assumed a per-fragment cost that the early-out removes; doubling the program count would also double compile time on every renderer that builds its shaders at run time, which EasyGL does. |

### 8.3 EasyGL reference shaders

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-835 | EasyGL: shadow-map generation path (depth-only draw into the shadow target) | ✅ | Done — the caster path lands the silhouette in the map and a receiving draw reads it back: `ShadowVisibilityTest.TheCastersShadowIsVisibleOnTheGround` renders a floating quad into the map, then renders the ground plane and finds the centre at the ambient floor (38/255) against a fully lit corner (255/255). |
| MOD-836 | EasyGL: `BasicEffect` shadow-receiving variant with 3×3 PCF | ✅ | Done — `uShadowMap`/`uLightViewProj`/`uShadowsEnabled`/`uShadowBias` on the per-pixel lit program, 3x3 PCF over the map's own texel size, bound at unit 7. Shadow multiplies direct diffuse and specular only, so a fully shadowed surface keeps its ambient rather than going black (asserted). An effect with no map attached renders the frame it rendered before this existed. |
| MOD-837 | EasyGL: `SkinnedEffect` shadow-receiving variant | ✅ | Done — the skinned program. It has no ambient uniform of its own (ambient is folded into emissive before the shader sees it), so the shadow multiplies `lightSum` and the specular sum only. `ShadowVisibilityTest.SkinnedEffectReceivesTheShadow` renders it with one identity bone. |
| MOD-838 | EasyGL: `PbrEffect` shadow-receiving variant | ✅ | Done — both PBR programs, which share one fragment source. `Lo`, the direct-lighting accumulation, is multiplied by the shadow; the ambient/occlusion term is not, because it stands for light arriving from the rest of the environment, which one occluder between the surface and one light does not block. Asserted: the shadowed centre is darker than the corner and still above zero. |
| MOD-839 | EasyGL: `SkinnedPbrEffect` shadow-receiving variant | ✅ | Done — same shared source, plus its own test at stride 68 (`SkinnedPbrEffectReceivesTheShadow`), which is what catches the two PBR copies drifting apart. |
| MOD-840 | PCF kernel size from `ShadowQuality` (1/3×3/5×5/poisson) | ✅ | Done — `ShadowMap::filterRadiusForQuality` (Disabled/Low 0, Medium 1, High/Ultra 2), carried by `IShadowReceiverEXT::setShadowFilterRadiusEXT` and `GpuDrawParams::shadowPcfRadius`. The kernel is a fixed 5x5 loop with the radius deciding how many taps count, because GLSL ES 1.00 needs a statically countable loop. Deviation from this row: no Poisson disc. Past 5x5 a box filter blurs a shadow rather than resolving it, so Ultra buys quality from resolution instead. Verified by counting partially-shadowed pixels: radius 0 produces exactly zero, radius 2 produces many. |
| MOD-841 | Shadow-map border handling (outside the light frustum = lit, never shadowed) | ✅ | Done — an explicit range check, not a clamp-to-border sampler mode: a sampler clamped to the edge texel would smear the caster silhouette outward as four dark bands. `ShadowVisibilityTest.NothingOutsideTheLightVolumeIsShadowed` fits the light to the caster alone and asserts every pixel of the frame border is fully lit while the centre is shadowed. |
| MOD-842 | Cross-check against the existing `easygl_shadowmapping_*` examples | ✅ | Done, and it found the one difference that matters. The example ports the XNA sample verbatim, including its `ShadowTexCoord.y = 1 - y`: correct against a D3D9 shadow map, wrong against a CNA render target, whose texel memory already matches the clip space it was rendered in. The library therefore samples with the raw NDC-derived UV and no flip. `ShadowVisibilityTest.TheShadowLandsWhereTheCasterIs` proves it by moving the caster off centre along +X, -X, +Z and -Z in turn and comparing the shadow centroid against where the camera matrices alone put that point -- within 2 pixels, against the ~32 a flip or an axis swap would cost. The centred scene the other cases use cannot fail either way, which is why this one exists. |

### 8.4 Verification

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-850 | Unit tests: light matrices, ortho fitting, quality→size table, settings | ✅ | Done — 12 cases: the size table, the view/backward-vector convention, the straight-down and unnormalized-direction cases, containment and tightness of the fitted volume, a degenerate scene, both misuse guards, and that an empty pass leaves the map meaning "nothing occludes". |
| MOD-851 | Unit tests: `IShadowReceiverEXT` on all four effects (setters, params, disabled default) | ✅ | Done — 7 cases, exercised through an `IShadowReceiverEXT&` rather than the concrete types (a shadow subsystem should not need to know which effect it is talking to). Includes the two that matter beyond round-tripping: the defaults leave `GpuDrawParams` exactly as before this existed, and enabling shadows without attaching a map does not tell the renderer to sample a texture that is not there. |
| MOD-852 | Golden image: single cube on a plane, sun at 45° | ✅ | Done as an analytic check rather than a committed PNG, and the reason is worth the deviation: a golden records that a shadow was in *some* place on the machine that made it, while `modules/graphics-ext/examples/cnaext_shadowmap_test.cpp` records that it is in the *right* place and says where. A cube centred `h` above the plane under a sun tilted `theta` casts its shadow at world `x = -h*tan(theta)`; the camera looks straight down, so the measured centroid is compared against that formula. At 0/25/45 degrees the error is 0.7/0.7/0.5 pixels. The frame is also printed as ASCII so a failure can be looked at. |
| MOD-853 | Golden image: shadows disabled == unshadowed render | ✅ | Done — `AnAttachedButDisabledMapChangesNoPixel` compares all 4096 pixels of a frame rendered by an effect that has never heard of shadows against one with the map attached and reception switched off. Zero differ. Comparing every pixel rather than a sample is the point: the failure worth catching is a lookup that runs anyway and returns something very close to, but not exactly, 1.0. |
| MOD-854 | Golden image: skinned character self-shadowing | ⬜ | Deferred, and deliberately not claimed by `MOD-837`'s test: a skinned quad with one identity bone proves the shader receives the shadow, not that a character shadows *itself*. Self-shadowing needs a real animated mesh, which belongs with the glTF fixtures rather than with a synthetic scene. |
| MOD-855 | Golden pair documenting acne (bias=0) and peter-panning (bias too high) at the chosen default | ✅ | Done as measurements rather than a committed image pair — deviation recorded here, and the reason is that a golden PNG of acne records that it looked wrong on one machine, while a fraction records how wrong and can be argued with. Scene: a sun at 45 degrees over a ground plane written into the map as well as read from it. Self-shadowed area, one tap, printed by the test: **bias 0 -> 0.549**, **default 0.0015 -> 0.093** (which is the caster's real shadow, and all that is left), **bias 0.2 -> 0.000** (the shadow has detached from its caster entirely -- peter-panning at its limit). That is the evidence `MOD-808`'s default sits on. |
| MOD-856 | Example `cnaext_shadowmap_test` — live window, movable sun | ✅ | Done — `cnaext_shadowmap_test`, registered as ctest `CNAEXT_ShadowMap` for every renderer, SKIPping (77) from inside on a renderer without 3D or custom effects and on a machine with no video subsystem, with the reason printed. Deviation from "live window, movable sun": there is no interactive loop. The sun moves via `--sun-degrees A,B,C` (default 0,25,45), one rendered frame per angle. An interactive control loop cannot be verified in this environment, and shipping one nobody has run is worse than shipping none. The sweep is also what makes the fourth check possible -- a shadow pinned correctly at one angle could be pinned by accident, so tilting the sun further must move it further, every time. |
| MOD-857 | Perf: shadow pass cost per quality at 720p/1080p | ✅ | Recorded — `cnaext_shadowmap_test --benchmark`, 12 casting triangles, Mesa llvmpipe (OpenGL ES 3.2) under Xvfb, so these are software-rasterizer figures and a recording rather than a budget: **Low (512) 0.10 ms**, **Medium (1024) 0.12 ms**, **High (2048) 0.20 ms**, **Ultra (4096) 0.52 ms** per pass. The shape is what transfers: cost tracks map area, and at these caster counts the clear dominates the draw. Deviation from this row's "720p/1080p": the screen resolution is absent on purpose. The pass renders into the map and never into the frame, so its cost is a function of map size and caster count alone -- a two-column table would print the same number twice and imply a dependency that does not exist. |
| MOD-858 | `RenderPipeline` integration: shadow pass runs inside `begin()` when a caster is set | ✅ | Done — `RenderPipeline::setShadowScene(map, light, bounds, drawCasters)`, run at the top of `begin()` **before** the scene target is bound. Ordering is the assertion, not a detail: `ShadowMap::end()` restores the back buffer, so a pass run after the scene target was bound would silently unbind it and send the whole frame to the screen, with post-processing quietly doing nothing. The test reads the actually-bound target from inside the caster callback. Three separate reasons not to run a pass (settings off, no map, no callback) each leave `didShadowPassRun()` false, and a shadow pass alone never forces an off-screen target on a game that wants no post-processing. |
| MOD-859 | Document the app-side contract: the scene must be drawable twice (a `renderScene` callback pattern) | ✅ | Documented in `docs/cnaext-engine-layer.md` under "Shadows, and the contract they put on the app", with both call shapes side by side and the receiving half spelled out. The contract is stated as the cost it is -- the scene must be drawable twice per frame -- and attributed to shadow mapping rather than to this implementation. Three things that make a first frame look wrong are named there too: the map holds distance rather than depth, `getLightViewProjection()` is only valid after `begin()`, and the bias trade measured in `MOD-855`. |
| MOD-860 | Decide the scene-callback shape (`std::function<void(Effect&)>` vs app-driven begin/end) | ✅ | Decided as recommended: **app-driven**. `ShadowMap::begin`/`end` is the primitive and is usable entirely on its own, with no callback anywhere in its API. `RenderPipeline::setShadowScene` adds the callback as a convenience on top, for an app that already routes its frame through the pipeline; it takes `std::function<void()>` and not `std::function<void(Effect&)>` because the caster effect is `ShadowMap`'s own business and handing it to the app would invite the app to reconfigure it mid-pass. |
| MOD-861 | Flip `N30` in CNAEXT.md | ✅ | Done — `CNAEXT.md` N30 flipped, with the one correction the implementation forced: the row said "depth RT" and the map holds light-space *distance*, because CNA cannot sample a depth attachment as a texture on every renderer. |

---

## Phase 9 — Cascaded shadow maps (`MOD-900`–`MOD-929`)

→ CNAEXT.md §5.3, N31.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-900 | `CascadedShadowMap` skeleton (N cascades, N ∈ 2..4, from `ShadowQuality`) | ✅ | Done — `CNA::Graphics::CascadedShadowMap`, 2 to 4 cascades, each at the full resolution `ShadowQuality` implies rather than a share of one map. Worth pinning and tested: the tempting implementation, splitting one map between the cascades, would make "High with 4 cascades" quietly worse than "High" everywhere, which is the reverse of what the setting promises. |
| MOD-901 | Practical split scheme (logarithmic/uniform blend with a documented λ) | ✅ | Done — Zhang's practical scheme, `d(i) = lambda*near*(far/near)^(i/N) + (1-lambda)*(near + (far-near)*i/N)`, default lambda 0.75. Tested against hand-computed values at lambda 0, 0.5 and 1, plus the properties the formula alone does not give: splits ascend, the last one is *exactly* the far plane (a cascade stopping a hair short would leave a permanently unshadowed sliver at the horizon), and an unusable range is rejected rather than returning NaNs from the logarithm. |
| MOD-902 | Per-cascade frustum-corner extraction from the camera view/proj | ✅ | Done — `computeFrustumCorners`, tested against the projection's own definition (half-height = near*tan(fov/2)). It found one thing worth recording: XNA's projection matrices are the Direct3D ones, so NDC z runs **0..1**, not -1..1. Taking the GL convention put the "near" corners half way to the camera and shrank every cascade toward it. |
| MOD-903 | Per-cascade light-space fitting (sphere-based, to avoid shimmering on rotation) | ✅ | Done — sphere-based, and asserted the way the row asks: the fitted radius is measured at 22 camera orientations across a full turn and does not move by more than 0.1%. A box fit would grow and shrink with every rotation, and a cascade whose extents change each frame shimmers along every edge. |
| MOD-904 | Texel-snapping to remove shimmering on translation | ✅ | Done — `snapToTexelGrid`, applied in **light space**, where a texel is an axis-aligned square; snapping the world-space centre would quantize along axes the map is not aligned to. Asserted in both directions: a nudge of a fifth of a texel produces a bit-identical centre, and a full texel moves it by exactly one texel -- without the second half, "snapped" could mean "frozen" and the cascade would stop following the camera. |
| MOD-905 | Cascade selection in the receiver shader (by view depth) | ✅ | Done — by view depth, computed in the shader from the view matrix's third column (carried in `GpuDrawParams::cascadeViewZRow`) rather than from distance to the eye, so the GPU rule and `CascadedShadowMap::selectCascade` are the same rule and can be tested against each other. The selection is written with four constant-index comparisons instead of `uCascadeMatrices[index]`, because the ES 1.00 form these shaders are also compiled in forbids dynamically indexing a uniform array in a fragment shader. |
| MOD-906 | Cascade blending band to hide the seam | ✅ | Done — `setBlendBand(width)` in view-depth units, defaulting to 0. Near a split the lookup runs in both cascades and mixes. Asserted as the property that distinguishes a cross-fade from a bug: it may soften the seam but must not move the shadow, so the shadowed area with a 6-unit band stays within a quarter of the area without one. |
| MOD-907 | Texture-array vs atlas decision for cascade storage | ✅ | Decided as recommended: **atlas**. One `RenderTarget2D` `cascadeSize * count` wide with a viewport per cascade, needing nothing CNA's renderer interface does not already have -- a texture array would have to be added to every renderer before one cascade could be stored. Each cascade's atlas sub-rectangle is baked into its own matrix, so a receiver transforms and samples with no separate UV offset and therefore no way to apply one cascade's offset to another's lookup. The caster gets the same product with that factor divided back out, because it renders into clip space rather than atlas space. |
| MOD-908 | `GpuDrawParams` extension for cascades (split distances + per-cascade matrices, or atlas UV rects) | ✅ | Done — `cascadeCount` (0 = single map, and every existing draw takes that path), `cascadeMatricesColMajor`, `cascadeSplits`, `cascadeViewZRow`, `cascadeBlendBand`, `cascadeDebugTint`. Carried from the effects through a new always-compiled `ShadowCascadeStateEXT` on `IShadowReceiverEXT` -- one value rather than a dozen setters, because these fields are only meaningful together: a matrix from this frame beside a split from the last one puts fragments in the wrong cascade, which reads as a resolution artefact rather than as the torn update it is. |
| MOD-909 | EasyGL cascade receiver shaders for the 4 lit effects | ✅ | Done — one shared `CNA_GL_SHADOW_DECL`, so the single-map and cascade paths are the same code in all four lit programs and cannot drift. Two things the atlas forces: the PCF texel step became a `vec2` (an atlas is N times wider than tall, and one scalar would step N times too far in X and smear each cascade into its neighbour), and every lookup is clamped to its own slice or a tap at a seam reads the next cascade. Verified by rendering rather than by golden: a caster seen through the atlas, and 2/3/4 cascades agreeing on the shadow to within a fifth -- the count is a quality knob, not a visual one. |
| MOD-910 | Debug visualization mode (cascade tint) exposed as a setting | ✅ | Done — `setDebugTintEnabled`, off by default, tinting each cascade. Asserted in both directions: with it on more than a quarter of the frame is non-grey, with it off *nothing* is -- a tint leaking into ordinary rendering would be a colour bug nobody would think to look for in a shadow test. |
| MOD-911 | Unit tests: splits, fitting, snapping, selection math | ✅ | Done — 26 cases covering the splits (including both extremes of lambda and every rejection), the frustum corners, the sphere fit's rotation invariance, the snap in both directions, the atlas slicing, cascade selection at and around each boundary, and every misuse guard. Selection is on the CPU (`selectCascade`) precisely so it can be checked against hand-picked depths rather than inferred from an image; the shader mirrors it. |
| MOD-912 | Golden image: large outdoor scene with 3 cascades | ✅ | Done as a rendered check with the frame printed rather than as a committed PNG -- same deviation and same reason as `MOD-852`, recorded here. `cnaext_csm_test` builds the scene this row asks for (a long strip of ground, three casters spread down it, a 45-degree sun) and asserts what a golden of it would have been inspected *for*: every caster casts through the atlas, at 2, 3 and 4 cascades, to within a fifth of the same shadowed area. The ASCII rendering of each frame goes to the log so a failure can be looked at. |
| MOD-913 | Golden image: cascade debug tint | ✅ | Done, same way — `--` the debug-tint frame is rendered and asserted rather than committed. The assertion is the one a tinted golden exists to make: a sample near the bottom of the frame (close ground) and one near the middle (distant ground) carry *different* tints, so the cascades genuinely tile the view in depth. A broken depth term collapses everything into one cascade, which a single-colour golden would show and a single-pixel check would not. |
| MOD-914 | Example `cnaext_csm_test` | ✅ | Done — `cnaext_csm_test`, registered as ctest `CNAEXT_CascadedShadowMap`, SKIPping (77) from inside on a renderer without 3D or custom effects and on a machine with no video subsystem. Five checks: the atlas casts at each cascade count, the tint bands the frame in depth order, and the same scene with shadows off renders uniformly lit. |
| MOD-915 | Perf: N-cascade cost vs single map | ✅ | Recorded — `cnaext_csm_test --benchmark`, 6 casting triangles, Mesa llvmpipe under Xvfb, so software-rasterizer figures and a recording rather than a budget: **single Medium map 0.12 ms**, **2 cascades 0.20 ms**, **3 cascades 0.49 ms**, **4 cascades 0.43 ms** per frame. Roughly linear in cascade count, with per-pass overhead dominating at this triangle count -- which is the honest reading of 3 and 4 landing within noise of each other rather than 4 being cheaper. |
| MOD-916 | Document CSM's app contract (scene drawn N+1 times) and its cost | ✅ | Documented in `docs/cnaext-engine-layer.md` under "Cascades, and what the second contract costs", stated as the cost it is: the casting geometry is drawn once *per cascade*, on top of the camera pass, which is why the count is a quality setting rather than something the library picks. Also records the four things that surprise: each cascade is a full-resolution map rather than a share of one, storage is an atlas, the fit is sphere-based and texel-snapped, and the debug tint is the fastest way to see whether the splits suit the scene. |
| MOD-917 | Flip `N31` in CNAEXT.md | ✅ | Done — `CNAEXT.md` N31 flipped, with two corrections the implementation forced: the range is **2**--4 rather than 3--4 (two is a legitimate low setting and the shader carries four either way), and storage is an atlas rather than the texture array the row implied. |

---

## Phase 10 — Point and spot shadows (`MOD-1000`–`MOD-1027`)

→ CNAEXT.md §5.3, N32 (marked long-term there; scoped here so it is not open-ended).

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1000 | `PointLightEXT` / `SpotLightEXT` engine-layer structs | ✅ | Done — `PointLightEXT` and `SpotLightEXT`, engine-layer only like `DirectionalLightEXT` and for a stronger reason: XNA 4.0 has neither, so there is no name to preserve. `Range` is documented as what it actually is -- the far plane of every shadow face *and* the divisor that turns a distance into the 0..1 a colour texture holds -- so a range far larger than the light reaches costs precision everywhere. The spot cone is stored as **half**-angles, because that is what a projection matrix wants and one conversion site is one fewer factor of two to get wrong. |
| MOD-1001 | Decide the point-shadow storage: `RenderTargetCube` depth vs dual-paraboloid | ✅ | Decided as recommended: **cube**. Dual-paraboloid halves the passes but warps geometry in the vertex shader, so a triangle spanning the seam is wrong unless it is tessellated -- a correctness problem that gets worse with the large triangles a shadow pass wants. `RenderTargetCube` already exists with real render-into-a-face support, so the cube costs six passes and no new concepts. |
| MOD-1002 | `CubeShadowMap` — 6-face depth generation into a `RenderTargetCube` | ✅ | Done — `CubeShadowMap`, `update(light)` then `begin(face)`/`end()` per face. Each face is cleared as it is bound (unlike the cascade atlas, the faces are separate images), and the cube is allocated `PreserveContents` or binding face 1 would discard face 0. Face size is capped at 1024 whatever the quality asks: six faces at 4096 is 100 million texels for one light, and the quality table was written for a single 2D map. Six view matrices unit-tested against the backward-vector convention, against degeneracy on the two Y faces (whose view direction is parallel to the obvious up vector), and for coverage -- every sampled direction lands inside at least one face. |
| MOD-1003 | Linear-distance storage instead of projected depth (documented reason) | ✅ | Done, and it is the reason the point path is usable at all: the caster writes `length(worldPos - lightPos) / range`, not projected depth. Projected depth is non-linear *and defined by the projection the face used*, so comparing against it means recovering which face a direction came from and re-deriving that projection. Distance over range is the same number whichever face it landed on, so the receiver samples the cube by direction and compares directly. Clamped to 1 in the shader, because a caster past the range still rasterizes where it overlaps a face and a stored value above 1 would read as further than infinitely far. `SpotShadowMap` stores the same thing for the same receiver rule. |
| MOD-1004 | `SpotShadowMap` — single perspective depth map from the cone | ✅ | Done — `SpotShadowMap`, one perspective map with the field of view at **twice** the outer half-angle. That factor is unit-tested directly: a point on the cone's rim must land on the edge of clip space. Built from the half-angle instead, the projection covers half the cone and leaves its whole rim permanently unshadowed -- which reads as the light not reaching far enough rather than as a shadow bug. A cone at or past a right angle is refused rather than silently clipped. |
| MOD-1005 | Receiver hooks for point/spot on the four lit effects | ✅ | Done, and it needed more than a hook: XNA's lit effects carry three *directional* lights and nothing else, so a point shadow had nothing to attenuate. `PunctualLightEXT` (always compiled, CNAEXT) therefore carries the light *and* its shadow, and the four lit effects gained a punctual lighting term for it to modulate. Defaults are inert -- `Kind == None` fills `GpuDrawParams` exactly as before, asserted directly. One struct rather than a dozen setters, same reasoning as `ShadowCascadeStateEXT`: a matrix from one light beside a position from another is a shadow in the wrong place, which reads as a bias problem. |
| MOD-1006 | EasyGL shaders for cube-shadow lookup with PCF | ✅ | Done — `CNA_GL_PUNCTUAL_DECL`, shared by all four lit programs. The cube is sampled by direction with a single tap; the spot map gets 3x3 PCF. That asymmetry is deliberate and documented: filtering across a cube face's edge needs seamless sampling, which is not available on every profile these shaders compile in, and a tap that silently wrapped to the wrong face would draw a stripe of shadow along every cube seam. The spot filter has **its own** texel size -- borrowing the directional map's meant that a draw with no sun attached filtered with a texel of 1.0, clamped every tap to a corner, and produced a spot shadow that silently never appeared. |
| MOD-1007 | Light-count limits documented (how many shadowed lights per draw; recommendation: 1 shadowed + the 3 XNA directional slots) | ✅ | Documented on `PunctualLightEXT` and enforced by shape: the struct holds one light, so a second cannot be attached. Three directional slots plus one shadowed punctual light is the budget, and the reason is stated where a reader will meet it -- each extra shadowed light is another full generation pass, six of them for a point light, so a second doubles the frame's shadow cost before a pixel is drawn. |
| MOD-1008 | Unit tests for all new math | ✅ | 22 cases. The 15 generation ones assert matrices, because every failure there renders a convincing shadow of the wrong thing. The 7 reception ones render, because none of what they check is visible in a matrix: that the light reaches the surface at all (without which every later shadow assertion passes for the wrong reason), that it falls off with distance, that the cone confines a spot, that each shadow darkens what it occludes, that a light with no map attached is lit rather than black, and that a draw never told about a punctual light is byte-for-byte unchanged. |
| MOD-1009 | Golden images: point light in a box; spot light on a plane | ✅ | Done as rendered checks with the frame printed, not committed PNGs -- the same deviation as `MOD-852`/`MOD-912` and recorded here too. The scenes are the ones the row asks for: a lamp inside a box, and a spot on a plane. What is asserted is what a golden of the box would have been *inspected for* and what a single probe could never see: all four walls brighter than the ambient floor, which means every horizontal cube face carries its share. A one-face bug leaves a wall dark and the picture still looks like a lit room. |
| MOD-1010 | Example `cnaext_pointshadow_test` | ✅ | Done — `cnaext_pointshadow_test`, registered as ctest `CNAEXT_PointShadow` for every renderer, SKIPping (77) from inside on a renderer without 3D or custom effects and on a machine with no video subsystem. Four checks, with an ASCII rendering of each frame in the log. |
| MOD-1011 | Perf: 6-face generation cost | ✅ | Recorded, and the number is worse than the row assumed. `cnaext_pointshadow_test --benchmark`, 2 casting triangles, Mesa llvmpipe: one directional map **0.05 ms**, a spot map **0.04 ms**, a point light's six faces **5.42 ms**. That is not six times a single map, it is a hundred times -- each face rebinds a different cube attachment and clears it, so per-pass overhead dominates completely at low triangle counts. Software-rasterizer figures, so a recording rather than a budget, but the ratio is the reason point shadows are something a game opts into per light rather than a quality setting. |
| MOD-1012 | Flip `N32` in CNAEXT.md | ✅ | Done — `CNAEXT.md` N32 flipped, noting the two things the implementation added beyond the row: spot maps came with it, and the four lit effects gained the punctual *light* the shadow attenuates, because XNA's own carry only directional slots. |

---

## Phase 11 — Skybox (`MOD-1100`–`MOD-1127`)

→ CNAEXT.md §5.4, N40. Small, self-contained, and a prerequisite for IBL being visually meaningful.

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1100 | `Skybox` skeleton (ctor(device, TextureCube*), `draw(view, projection)`, `getEnvironment()`) | ✅ | Done — `CNA::Graphics::Skybox`, borrowing its cube by default and `setOwnedEnvironment` for the generated case, following the `PbrEffect` precedent. Attaching a borrowed cube over an owned one releases the owned one, which is asserted: without it the owned cube stays alive with nothing referring to it, a leak with no symptom. |
| MOD-1101 | `SkyboxEffect` (CNAEXT, XNA namespace) — cube-sampling effect with the translation-stripped view | ✅ | Done, with a recorded deviation: the sky is an internal `ShaderEffect` inside `Skybox`, **not** a new `SkyboxEffect` class in the XNA namespace. A new stock effect would need a matching program added to every renderer before the sky could draw at all, while `ShaderEffect` already compiles exactly this shader on any renderer with `CustomEffects`. The property the row is really about -- the view's translation stripped, so the sky stays infinitely distant -- is implemented and asserted directly: a camera moved 500 units produces the same ray, and a camera *turned* produces a different one. |
| MOD-1102 | Fullscreen-triangle sky rendering (ray direction from the inverse view-projection) — no cube mesh | ✅ | Done — one fullscreen draw, no cube mesh and no vertex buffer. The ray comes from the inverse of (rotation-only view * projection), and `Skybox::computeViewRay` is the CPU twin the shader mirrors, so the reconstruction is checked against arithmetic rather than an image: the centre ray is the camera forward, the top edge sits at exactly half the vertical field of view, and yaw takes -Z onto -X. A reconstruction that is merely close still renders a sky -- one that turns at the wrong rate. |
| MOD-1103 | Depth state: draw at far plane with `LessEqual` and depth-write off | ✅ | The guarantee is delivered and asserted, by ordering rather than by depth state -- deviation recorded. The sky is drawn *first*, not last at the far plane with `LessEqual`, because the engine layer's fullscreen mechanism is `SpriteBatch`-based and carries no depth configuration. `SkyboxRenderTest.TheSkyNeverOccludesGeometry` draws a foreground quad after the sky and checks both that the quad survived and that the sky is still around it. What is given up is the optimisation of skipping sky pixels the scene covers. |
| MOD-1104 | Draw ordering inside `RenderPipeline` (after opaque, before transparent/post) | ✅ | Done — `RenderPipeline::setSkybox` + `setSkyboxCamera`, drawn inside `begin()` right after the scene target is bound and cleared. `didSkyboxDraw()` reports it, because an app cannot otherwise tell "drawn before my geometry" from "not drawn at all". A sky alone does not force an off-screen target: a game wanting a sky and no post-processing still renders straight to the back buffer, asserted. |
| MOD-1105 | HDR skybox support (float cube map source) | ✅ | Done — verified by readback into an RGBA32F target rather than by inspection: a cyan sky at intensity 4 arrives as 4.0 in the green and blue channels. Against an 8-bit target the same draw clamps to 255 and the range is gone before the first post-process pass ever sees it. |
| MOD-1106 | Rotation/orientation setting (yaw offset) | ✅ | Done — `setYaw`, applied to the sampled direction rather than to a matrix, so the same environment can be turned per draw without rebuilding anything. Tested twice over: against the CPU twin (a quarter turn takes -Z onto -X) and through the renderer (a quarter yaw brings the green -X face into view of a camera that has not moved). |
| MOD-1107 | Intensity/tint multiplier | ✅ | Done — `setIntensity` and `setTint`, both verified by readback: half intensity halves the sampled channel, a quarter-green tint quarters it. Negative intensity is clamped to zero rather than propagated -- it is a sign error, not a dark sky. |
| MOD-1108 | Fallback when cube maps or custom effects are unsupported (skip + one-time log) | ✅ | Done — `isSupported()` from `CustomEffects` and from whether the shader actually linked. Where it is false, or where no environment is attached, `draw` returns after logging once and the scene renders without a sky. A game switching the sky on before its environment has loaded gets a frame, not an exception. |
| MOD-1109 | Equirectangular → cube helper (`EnvironmentProcessor::convertEquirectangular`) | ✅ | Done — `EnvironmentProcessor::convertEquirectangular`, on the CPU rather than as six render passes. That is a deliberate trade: a render-to-cube version would be faster but would need float targets, cube targets and custom effects all present, and this has to work where none of them are -- it is a load-time cost paid once. The two coordinate mappings are exposed and **tested as inverses**, so the converter cannot disagree with itself, and a marked panorama (colour encoding its own longitude and latitude) pins each face's placement: +Y from the top of the image, -Y from the bottom, -Z at the centre, +X a quarter along. Longitude wraps rather than clamps -- the panorama's two edges are the same meridian, and clamping smears a stripe down the seam of the sky. |
| MOD-1110 | Cube-face orientation conformance test (each face samples the expected direction) | ✅ | Done — six cameras down six axes at a six-colour cube, each expected colour named individually. This is the only test here that can catch a cube whose faces are wired in the wrong order, because a cube map with two faces swapped still renders a sky from every angle. It passed on the first run, so CNA's face convention and the standard cube-map one agree. |
| MOD-1111 | Unit tests for every public member | ✅ | 17 cases: 5 for the ray against arithmetic, 5 for ownership and settings including the release-on-replace path, and 7 rendered -- the six-face conformance, yaw through the renderer, intensity and tint by readback, the foreground-object guarantee, the HDR round trip, and the two pipeline-ordering ones. |
| MOD-1112 | Golden image: skybox with a foreground object | ✅ | Done as a rendered check with the frame printed, same deviation as `MOD-852`/`MOD-912`/`MOD-1009`. The property a golden of this scene would have been inspected for is asserted directly and at **eight camera angles** rather than one: the foreground object stays in front of the sky at every angle, and the sky behind it changes as the camera orbits -- which is what separates a sky around the world from one painted on the screen. |
| MOD-1113 | Example `cnaext_skybox_test` — orbit camera | ✅ | Done — `cnaext_skybox_test`, registered as ctest `CNAEXT_Skybox`, orbit camera at eight angles, SKIPping (77) from inside where the shader will not compile or there is no video subsystem. Includes the yaw check with the camera standing still. |
| MOD-1114 | Perf: sky cost (should be ~1 fullscreen pass) | ✅ | Recorded — `cnaext_skybox_test --benchmark`, 128x128, Mesa llvmpipe: **0.020 ms/frame** for the sky against 0.005 ms for a clear alone. One fullscreen pass, which is what the row expected. |
| MOD-1115 | Document the skybox contract and the cube-map coordinate convention CNA uses | ✅ | Documented in `docs/cnaext-engine-layer.md` under "The sky": the usage, the two deviations (internal `ShaderEffect` rather than a new XNA effect class, drawn first rather than last), and the cube-map coordinate convention -- longitude across, latitude down, **-Z at the centre of the panorama**. That convention is written once, in `EnvironmentProcessor::faceDirection` and its inverse, and referenced rather than restated, which is what `MOD-1115` asks for so IBL can rely on it. |
| MOD-1116 | Flip `N40` in CNAEXT.md | ✅ | Done — `CNAEXT.md` N40 flipped, noting the fullscreen-ray approach and that the equirectangular converter came with it, since panoramas ship equirectangular and renderers sample cubes. |

---

## Phase 12 — Image-based lighting (`MOD-1200`–`MOD-1263`)

→ CNAEXT.md §5.4, N41–N44. The largest single quality win for the existing `PbrEffect`, and the one
place PBR meaningfully grows (CNAEXT.md's own words).

### 12.1 Precompute infrastructure

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1200 | `EnvironmentProcessor` skeleton (ctor(device), capability gate) | ✅ | `modules/graphics-ext/{include,src}/CNA/Graphics/EnvironmentProcessor.{hpp,cpp}`. **Deviation, deliberate:** the generators are CPU-side, not render-to-cube. A GPU path needs float render targets *and* cube render targets *and* custom effects present at once, which no renderer in the committed scope offers together; a CPU implementation is instead correct everywhere and needs no capability gate at all, so the row's `EngineException` never has to be thrown. Cost is paid once at load (`MOD-1211`). |
| MOD-1201 | Cube-face render loop helper (bind face, set view, draw fullscreen triangle) | ⛔ | Superseded by `MOD-1200`'s CPU decision — there is no cube-face render loop to share. Its role is filled by the `CubeSampler`/`faceDirection` pair, both unit-tested. |
| MOD-1202 | `generateIrradiance(env, size=32)` — cosine convolution (→ N41) | ✅ | `generateIrradiance(env, size = 32, sampleCount = 32)`. `AConstantEnvironmentHasTheSameConstantIrradiance` asserts a constant environment reproduces itself on all six faces within 3/255 (≈1 %). |
| MOD-1203 | Irradiance sample-count / quality setting | ✅ | `sampleCount` is the quality knob (cost is its square); documented in the header and in `docs/cnaext-engine-layer.md`. `MoreIrradianceSamplesConvergeTowardTheAnalyticResult` asserts the finer sweep gathers the bright face a coarse one under-reads. |
| MOD-1204 | `generatePrefilteredSpecular(env, baseSize=128, mips=5)` — GGX split-sum prefilter (→ N42) | ✅ | `generatePrefilteredSpecular(env, baseSize = 128, mipCount = 5, sampleCount = 64)`. `PrefilteringKeepsMipZeroSharpAndFlattensTheLast` asserts mip 0 reproduces its input (bright face >200, dark face <40) and the roughest mip both gathers onto a perpendicular face and dims the bright one. **The row's acceptance text is wrong on one point** and was not followed: a GGX lobe is centred on the normal, so even at roughness 1 the face 180° opposite stays black. Spread is asserted where it physically occurs, at 90°. |
| MOD-1205 | Roughness→mip mapping documented and shared with the sampling shader | ✅ | `EnvironmentProcessor::mipForRoughness` / `roughnessForMip`, one public static pair, round-trip-tested. Generation calls `roughnessForMip`; the sampling shader (`MOD-1225`) will call `mipForRoughness`. |
| MOD-1206 | Hammersley/Van-der-Corput + importance-sampling GLSL helpers | ✅ | `hammersley` and `importanceSampleGgx` are public statics, tested directly: the radical-inverse sequence against hand-computed values, and the GGX half-vector for the mirror case (roughness 0 → the normal) and for the lobe staying in the normal's hemisphere. |
| MOD-1207 | `generateBrdfLut(size=512)` — the 2D scale/bias LUT (→ N42) | ✅ | `generateBrdfLut(size = 128, sampleCount = 128)`. `TheBrdfLutMatchesACpuReferenceAtSampledPoints` pins the axis order, the texel-centre convention and the channel assignment — the failure mode that yields a plausible table making every rough surface behave smooth. |
| MOD-1208 | LUT format decision (`HalfVector2` where available, `Color` fallback with documented precision loss) | ⛔ | **Collapsed to one format, not a choice.** `Texture::ValidateFormat` admits `SurfaceFormat::Color` and nothing else, so `HalfVector2` is not creatable in CNA at all and there is no second path to test. The 8-bit quantisation is a real precision limit and is stated in the `generateBrdfLut` doc comment rather than hidden. |
| MOD-1209 | Seamless-cube-filter capability use (`SeamlessCubeMapFilter`) and the fallback | ✅ | Nothing to fall back to: the CPU sampler picks the face *from the direction*, so a sample crossing an edge reads the neighbouring face rather than clamping to its own border — seamless by construction, at every mip, on every renderer. `SeamlessCubeMapFilter` remains relevant to the sampling shader in 12.2, not to generation. |
| MOD-1210 | Ownership model for generated products (who deletes the returned textures) | ✅ | **Decided against the row's recommendation:** every generator returns `std::unique_ptr`, so the *caller* owns the products and the processor holds no state. The recommendation's `release*` methods would make a stateless helper into a resource manager for no gain, and a caller that wants the products to outlive the processor (the normal case — the processor is a load-time object) would be fighting it. Ownership is stated in each `@return`. |
| MOD-1211 | Generation cost measurement + a "generate once, reuse" note in the docs | ✅ | `GenerationCostIsLoadTimeWork` measures and prints all three. Measured in the **Debug** `cmake-build-cnaext` build, single-threaded CPU: irradiance 32/32 **3.31 s**, prefilter 128/5/64 **2.38 s**, BRDF LUT 128/128 **0.49 s** — 6.2 s for the three. Slow enough to be worth stating plainly: an optimised build is several times faster, but this is load-time work either way, and the docs say generate once and reuse (and drop `sampleCount` when a load screen is not wanted). |
| MOD-1212 | Optional: cache generated products to disk between runs | ⛔ | Not done, per D7 — caching the products to disk means a new asset format, which this plan does not add. Regenerating costs the `MOD-1211` numbers once per run. |

### 12.2 Consumption

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1220 | `ImageBasedLightEXT` struct (irradiance, prefilteredSpecular, brdfLut, intensity) | ✅ | `modules/graphics/include/Microsoft/Xna/Framework/Graphics/ImageBasedLightEXT.hpp`. Irradiance, prefiltered specular, BRDF LUT, mip count, intensity; `IsValidEXT()` is the all-or-nothing test, and `AnIncompleteImageBasedLightStaysInert` asserts a partial bundle leaves the flat ambient term in charge. |
| MOD-1221 | `PbrEffect::setImageBasedLightEXT` / `getImageBasedLightEXT` (→ N43) | ✅ | `setImageBasedLightEXT`/`getImageBasedLightEXT` on `PbrEffect`, CNAEXT-marked and always compiled. The row's parenthesis is out of date: the struct is in the XNA namespace, not `CNA::Graphics` — see `MOD-1222`. |
| MOD-1222 | Decide whether `ImageBasedLightEXT` should instead live in the XNA namespace to avoid the XNA→CNA include direction | ✅ | **Decided: the XNA namespace**, which is the third option this row itself names and neither of the two OQ-4 listed. Three separate texture setters would have kept the layering clean but split a value whose parts are only meaningful together — the same argument that made `ShadowCascadeStateEXT` and `PunctualLightEXT` single structs in the XNA namespace. Following those two neighbours removes the inversion *and* keeps the bundle whole. §OQ-4's recorded answer is superseded by this row. |
| MOD-1223 | `SkinnedPbrEffect::setImageBasedLightEXT` | ✅ | Identical surface on `SkinnedPbrEffect`; `ImageBasedLightRoundTripsAndReachesTheDrawParams` covers it. |
| MOD-1224 | `GpuDrawParams` IBL field group (3 texture slots + intensity + prefiltered mip count) | ✅ | `iblEnabled`, `iblIrradiance`, `iblPrefilteredSpecular`, `iblBrdfLut`, `iblPrefilteredMipCount`, `iblIntensity`. Inert defaults; every renderer builds unchanged (whole-project build clean). |
| MOD-1225 | EasyGL: split-sum ambient shader replacing the flat `AmbientLightColor` term when IBL is bound | ✅ | `CnaGlIblDecl()` + the ambient line in both PBR programs, units 10-12. **Deviation:** verified by measurement rather than by a golden — with no bundle bound `uIblEnabled` is 0 and `cnaIblAmbient` returns zero before reading anything, and the unbound path is exercised by every existing PBR test, all of which still pass. **Limitation:** GLSL ES 1.00 fragment shaders have no `textureLod`, so WEBGL1/OPENGLES2 read the prefiltered cube's base level and a rough surface reflects a sharp environment; documented in the shader and in the docs rather than silently wrong. |
| MOD-1226 | Ambient-term switch documented (flat ambient vs IBL are exclusive, not additive) | ✅ | Stated in `ImageBasedLightEXT`, in `PbrEffect::setImageBasedLightEXT` and in `docs/cnaext-engine-layer.md`. Enforced where it cannot be got wrong: `FillGpuDrawParams` zeroes `ambientColor` when a valid bundle is bound, so even a renderer that ignores the IBL group cannot double-count. `cnaext_ibl_test` check C asserts flat+IBL equals IBL alone. |
| MOD-1227 | Occlusion map interaction (AO multiplies the IBL ambient, not the direct light) | ✅ | The occlusion sample multiplies the ambient/IBL term only; `cnaext_ibl_test` check E measures 255 → 66 with a 0.25 occlusion map. |
| MOD-1228 | IBL + shadows interaction (shadow attenuates direct only) | ✅ | `cnaext_ibl_test` check G: sun + environment 216, fully shadowed 132, environment alone 132 — the shadow removes exactly the direct light and leaves the environment. |
| MOD-1229 | Energy-conservation sanity test (a white furnace test: uniform environment, albedo 1, no light loss beyond a documented margin) | ✅ | `cnaext_ibl_test` check F, at half intensity so the measurement is two-sided (at full intensity a white environment saturates and only energy *loss* would be visible). Against an exact 128/255: roughness 0.1 → 159, 0.4 → 139, 0.7 → 129, 1.0 → 155. A small energy gain at both ends, near-exact in the middle. |

### 12.3 Verification

| ID | Task | Status | Acceptance criterion |
|---|---|---|---|
| MOD-1240 | Unit tests: every public `EnvironmentProcessor` method + `ImageBasedLightEXT` validation | ✅ | 16 `EnvironmentProcessorTest` cases plus four `PbrEffectDefaultsTest` and one `SkinnedPbrEffectDefaultsTest` covering the bundle, its validation, the draw-params fill and `Clone`. |
| MOD-1241 | Unit tests: CPU reference implementations of irradiance/prefilter/BRDF-LUT | 🟨 | **Deviation.** An independent CPU reference would be the same code written twice — the generators *are* the CPU implementation (`MOD-1200`), so a reference test would assert a function against itself. What the tests pin instead is what such a reference would have caught: the axis order, the texel-centre convention and the channel assignment of the BRDF table, the analytic constant-environment irradiance, and the mirror/rough ends of the prefilter. `hammersley` and `importanceSampleGgx` *are* checked against hand-computed values, which is the part of this row that survives intact. |
| MOD-1242 | Golden image: metal/roughness sphere grid under IBL (the canonical PBR chart) | ⛔ | No golden, the same deviation as `MOD-852`/`MOD-912`/`MOD-1009`/`MOD-1112`: a committed image would pin llvmpipe's exact rasterisation rather than the property. `cnaext_ibl_test` measures the properties a reader would have inspected the chart for — that a metal's reflection changes with roughness, that the environment lights an otherwise unlit surface, and the white-furnace energy balance. |
| MOD-1243 | Golden image: same grid with IBL off (flat ambient) | ⛔ | Same deviation; the switch is asserted numerically instead (check C, and `ACompleteImageBasedLightReachesTheDrawParamsAndReplacesFlatAmbient`). |
| MOD-1244 | Golden image: irradiance and prefiltered mips dumped as a debug sheet | ⛔ | Same deviation. The precompute's own regressions are caught by the 16 unit tests, which read the generated texels directly rather than through a rendered sheet. |
| MOD-1245 | Example `cnaext_ibl_test` — skybox + sphere grid + adjustable intensity | ✅ | `modules/graphics-ext/examples/cnaext_ibl_test.cpp`, registered as ctest `CNAEXT_ImageBasedLighting`; 7/7 checks pass. **Deviation:** a lit quad, not a sphere grid with a skybox — every check here is a *difference between two renders*, and a grid would add geometry without adding a measurement. The showcase role is the skybox example's (`MOD-1112`). |
| MOD-1246 | Perf: per-frame IBL sampling cost vs flat ambient | ✅ | Measured (96×96, Mesa llvmpipe, clear+draw with no read-back): flat ambient **0.064 ms/frame**, image-based **0.066 ms/frame** — about 3 %, which is three more texture reads per fragment. |
| MOD-1247 | Document the complete IBL pipeline in `docs/cnaext-engine-layer.md` | ✅ | Two sections in `docs/cnaext-engine-layer.md`: the precompute and the consumption, with the code, the ownership rule, the exclusivity rule, the ES 1.00 limitation, and the measured furnace and cost numbers. |
| MOD-1248 | Flip `N41`–`N43` in CNAEXT.md | ✅ | `N41`, `N42` and `N43` flipped to ✅ with their own correction notes. `N44` (the other renderers) stays ⬜ — Phase 16's work. |

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
