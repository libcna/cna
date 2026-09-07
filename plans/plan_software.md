# Software (CPU) Rasterizer Graphics Backend — Implementation Plan

> **Current status (Software <-> EasyGL XNA/Core parity campaign, begun 2026-09-07): ACTIVE.**
> The original `SOFTWARE-1..86` rows below remain the historical record of the deliberately small
> v1 renderer. They are not the current completion boundary. The authorized target is now an
> independent, deterministic CPU implementation of the classic XNA 4.0/core graphics behavior on
> which an application can rely wherever it can rely on EasyGL, except for explicitly classified
> non-applicable window/GPU behavior and deferred CNAEXT-modern facilities.
>
> A source-and-test reconciliation at start commit `cde325ecd4d771d50b7d8ec3ef99f1657ad217ac`
> found that later `REMED-GFX-*` work had already made the implementation substantially stronger
> than this plan's v1 prose: complete blend equations/factors, color write masks, multisample mask,
> point/linear and mip filtering, independent Wrap/Clamp/Mirror U/V addressing, texture/cube mip
> storage, viewport/scissor, wireframe, depth bias, 16/32-bit indexed addressing, dynamic buffer
> updates, and ordinary multi-stream vertex input all have implementation and focused tests.
> Conversely, similarly named methods do not establish parity: verified real gaps include
> classic alpha test, classic lighting/fog, 3D/two-sided stencil application, Texture3D,
> RenderTargetCube, MRT, and OcclusionQuery. The executable task table is in the new parity campaign
> section below; the evidence ledger is `docs/software-easygl-parity-ledger.md`.
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.

---

## Software <-> EasyGL XNA/Core parity campaign

### Scope and evidence rules

This section is the current executable plan. It supersedes the old v1 authorization boundary while
preserving `SOFTWARE-1..86` below as history. Classic XNA 4.0 behavior and renderer-agnostic CNA
core plumbing are authorized. `CNA::Graphics`, `modules/graphics-ext`, arbitrary `ShaderEffect`
execution, PBR effects, compute/storage buffers, HDR/post-processing, modern shadows/IBL and other
CNAEXT engine features are explicitly deferred and must not be used to inflate the parity target.

Evidence authority is Microsoft XNA 4.0 measurements/IL, then FNA, then EasyGL, then existing
Software behavior, then prose. A green row requires behavioral proof; the existence of a method or
a test that merely records an unsupported boundary is not parity. The durable family-by-family
classification and evidence is in `docs/software-easygl-parity-ledger.md`.

### Baseline record

- Branch: `software`.
- Start commit: `cde325ecd4d771d50b7d8ec3ef99f1657ad217ac`.
- Starting tracked worktree: clean.
- Build dependency note: CNA at this commit requires sharp-runtime's `Xml.Serialization`
  component, which is absent from the sibling `develop` checkout (`df1b42ab`) but present in the
  existing sibling `next` worktree (`7e9c58fd`). Baseline builds therefore use
  `-DCNA_SHARP_RUNTIME_ROOT=/rv/data/development/github.com/openeggbert/sharp-runtimenext`.
- The first configure also established that all pinned CNA submodules were absent; they were
  initialized at the revisions recorded by this repository. This changes no tracked CNA file.
- The OPENGLES3/EasyGL comparison configuration now exists at `cmake-build-easygl` with the same
  sharp-runtime and ccache inputs. Its first targeted build completed, and the shared XNA pixel-
  center and top-left-fill tests both pass under Mesa GLES 3.2 through Xvfb. The full EasyGL suite
  remains part of `SOFTWARE-100`.
- Baseline build/test results will be recorded in `SOFTWARE-100` after the clean configuration has
  completed; failed environment bootstrap attempts are retained here so they are not mislabeled as
  renderer regressions.

### Executable backlog

The order is dependency-led: truthful reporting, primitive/input foundations, raster correctness,
classic effects/state, resources, then high-level orchestration. Each implementation row includes
its own tests and plan evidence in the same commit.

| # | Task | Status | Acceptance criteria / evidence |
|---|---|---|---|
| SOFTWARE-100 | Reconcile current Software implementation, documentation, build configuration and test baseline against repository reality | 🟨 | Record the start state above; complete a clean Software build, all `Software` CTests and `CnaTests`; configure/build/run the practical EasyGL baseline; replace every stale current-status limitation with measured truth without erasing the S1-S9 history. |
| SOFTWARE-101 | Inventory EasyGL's classic XNA/core feature evidence independently of filenames | ✅ | Initial inventory completed 2026-09-07 across all 246 EasyGL example/test translation units, renderer interfaces and the public XNA graphics surface. Every family is classified in the ledger; modern custom-shader/PBR/glTF engine examples and physical GL/window behavior are separated from XNA/Core candidates instead of silently expanding scope. Keep verdicts current as probes refine them. |
| SOFTWARE-102 | Maintain the Software-vs-EasyGL parity ledger | ✅ | `docs/software-easygl-parity-ledger.md` records EasyGL evidence, Software evidence, scope, verdict, task and verification for every renderer-contract/public-XNA family found in the initial three-way audit. It explicitly treats boundary-only fixtures as gap evidence. The ledger remains living evidence and must be updated with each task. |
| SOFTWARE-103 | Generalize shared Software/EasyGL differential infrastructure beyond the one vertex-color triangle | 🟨 | First reusable contract landed: `top_left_fill_contract_test.cpp` runs unchanged on Software and EasyGL and proves exact shared-edge/order/MSAA behavior (5/5 each), beside the pre-existing shared XNA pixel-center fixture (2/2 each). Continue across effects, resources, validation and query values before closure. |
| SOFTWARE-104 | Make capability reporting truthful during the campaign | ✅ | Completed 2026-09-07. `SoftwareRenderer::SupportsCapability` now reports false for `AnisotropicFiltering`, `OcclusionQuery`, and `CustomEffects`: anisotropic still uses the isotropic linear path, query creation is still null, and accepted shader source is not executed. The new public `Software_CapabilityContract` pairs every pending false entry with its real API/factory boundary and protects the seven existing positive promises; 16/16 checks pass. The implementation tasks (`SOFTWARE-117`, `SOFTWARE-122`) must opt their capabilities back in only with behavioral proof. |
| SOFTWARE-105 | Implement `TriangleStrip` for all indexed/non-indexed, user/buffer and colored/effect-aware paths | ✅ | Completed 2026-09-07. One topology helper assembles `primitiveCount + 2` elements and reverses corners 0/1 on odd triangles; all four CPU triangle entry points use it without changing validated vertexStart/startIndex/baseVertex or index-width handling. The shared public `Software_TriangleStripWinding` fixture now requires every path and passes 299/299 checks, 0 boundaries and all 36 user/buffer, indexed/non-indexed, Uint16/Uint32, count and cull cases. Indexed-addressing, front-face, culling and wireframe regressions also pass (5/5 CTests). |
| SOFTWARE-106 | Replace eye/near-W-only clipping with complete homogeneous frustum clipping | ✅ | Completed 2026-09-07. All four CPU triangle routes now use a bounded Sutherland-Hodgman clip against XNA/D3D's `-W <= X,Y <= W`, `0 <= Z <= W` volume before divide; effect-aware lines and points use the same six plane distances. Every varying is interpolated in clip space, winding is retained, arbitrary clipped polygons fan-triangulate up to seven triangles, wireframe hides every internal fan diagonal, and solid fill retains the pre-`SOFTWARE-107` one-side diagonal exclusion. The rewritten public `Software_Clipping` fixture passes 11/11: each plane (near/far with depth disabled), indexed and non-indexed paths, six fully rejected cases, a multi-plane polygon, line, point and clipped-wireframe boundary. Twelve related culling/winding/effect/depth/viewport/scissor/input CTests pass. The old stock-effect fixture's identity-projection `Z=-2` geometry was correctly exposed as invalid and repaired with a real perspective projection. |
| SOFTWARE-107 | Implement the XNA/D3D top-left fill convention | ✅ | Completed 2026-09-07. Both colored and shaded raster loops, including all four MSAA coverage locations, now normalize winding and apply the D3D top/left boundary predicate; all manual solid-fill diagonal masks were removed. The renderer-independent `TopLeftFill` fixture uses two low-red additive triangles split along an exact sample-center diagonal and passes 5/5 on both Software and EasyGL: a precise 48x48 square, single ownership, order independence, reversed-winding equivalence within one byte, and 4x resolve. Clipping, wireframe, culling and the wider 18-test Software raster/effect/input set pass. |
| SOFTWARE-108 | Replace stride-keyed vertex reinterpretation with declaration-driven vertex input | ✅ | Completed 2026-09-07. The CPU reader now resolves `(VertexElementUsage, UsageIndex)` through each stream's stored declaration and stream-local record, then decodes all 12 XNA `VertexElementFormat` values with their float/default-component conversions. Arbitrary offsets, ordering, padding and strides no longer select or reinterpret a layout; only CNAEXT's deliberately empty legacy buffer retains the canonical stride fallback. `Software_VertexDeclaration` passes 16/16 public pixel cases: every format, equal-stride reordered data, `VertexBufferBinding` offset, 16/32-bit user-indexed draws and split Position/Color streams. Eight affected effect/topology/sampler regressions pass, including the formerly aborting DualTexture contract. |
| SOFTWARE-109 | Complete vertex/index buffer mutation and draw validation parity | 🟨 | Audit `SetDataOptions`, partial/dynamic updates, user primitive lifetime, buffer readback, 16/32-bit indices and exception behavior against shared/EasyGL fixtures. Existing REMED-GFX addressing and multi-stream coverage is retained; every remaining mismatch gets an implementation split before closure. |
| SOFTWARE-110 | Make 4x MSAA depth/stencil and coverage sample-correct | ⬜ | Coverage, depth and stencil are tracked/applied per sample, `MultiSampleMask` gates the same samples, resolve is deterministic, and single-sample behavior stays byte-identical. Add shared-edge, depth-intersection and stencil probes plus existing RT MSAA regressions. |
| SOFTWARE-111 | Implement complete `AlphaTestEffect` comparison semantics | ✅ | Completed 2026-09-07. Software now evaluates FNA's encoded four-value alpha-test expression after texture × vertex × effect alpha and before every depth/stencil operation, colour write, blend or fog-stage work. The renderer-neutral `AlphaTestEffectContract` passes 30/30 on both Software and Mesa EasyGL: all eight `CompareFunction` values at below/equal/above byte-reference alpha, half-byte equality tolerance, texture/effect/vertex composition, disabled vertex colour, implicit opaque-white null texture, and discarded-near-fragment depth preservation. Full observable stencil-state coverage remains correctly owned by `SOFTWARE-121`; the shared fragment ordering already places discard before that state path. |
| SOFTWARE-112 | Implement classic stock-effect fog | ⬜ | `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect` and `SkinnedEffect` honor their FNA/XNA fog vectors and ordering; start/end/disabled/degenerate and view-space probes compare against EasyGL within a justified tight tolerance. |
| SOFTWARE-113 | Implement full `BasicEffect` lighting | ⬜ | Lighting disabled/default lighting/all three directional lights, ambient/diffuse/emissive/specular/specular power, alpha, vertex color, texture and per-vertex/per-pixel preference behave per XNA/FNA; non-uniform World uses the correct normal transform. |
| SOFTWARE-114 | Complete `EnvironmentMapEffect` classic lighting and normal semantics | ⬜ | Directional/ambient/diffuse/emissive/specular, environment amount/fresnel/eye position, texture/cube sampling, fog and inverse-transpose normal handling match XNA/FNA/EasyGL probes. |
| SOFTWARE-115 | Complete `SkinnedEffect` classic lighting and normal semantics | ⬜ | Bone position/normal transforms, 1/2/4 weights, directional/ambient/diffuse/emissive/specular, per-pixel preference, vertex color, texture, alpha and fog pass focused and model-level probes. |
| SOFTWARE-116 | Give `DualTextureEffect` two declaration-driven texture-coordinate channels | ⬜ | Texture0 uses `TextureCoordinate0`, Texture1 uses `TextureCoordinate1`; transforms, independent slot samplers, doubling/modulation, alpha and fog match FNA/EasyGL. |
| SOFTWARE-117 | Implement or precisely emulate anisotropic filtering on the CPU | ⬜ | `TextureFilter::Anisotropic` uses `MaxAnisotropy` and differs from isotropic linear sampling on an oblique minified texture in the XNA-consistent direction; per-slot independence and mip/address interaction are tested. Do not claim the capability until this passes. |
| SOFTWARE-118 | Implement classic `Texture3D` storage, transfer and sampling plumbing | ⬜ | Full mip storage, partial boxes, format/range validation, SetData/GetData and W addressing/filter/mip behavior pass the shared EasyGL corpus where it exercises XNA/core APIs. CNAEXT-only custom ShaderEffect sampling remains deferred. |
| SOFTWARE-119 | Implement `RenderTargetCube` | ⬜ | Six isolated faces, mip chains, preservation/discard, depth/stencil formats, MSAA face-local resolve, sampling and GetData pass the already-registered shared cube-target tests with no unsupported boundary. Float CNAEXT formats are not required. |
| SOFTWARE-120 | Implement multiple render targets | ⬜ | Up to XNA's four bindings are stored and written in a defined CPU fragment-output contract, with dimension/MSAA/depth validation, per-target color write masks, preservation and lifecycle parity. If the classic stock effects expose only color0, prove the remaining attachments' XNA-defined result rather than inventing shader outputs. |
| SOFTWARE-121 | Apply complete stencil state to every 3D path, including two-sided stencil | ⬜ | Colored/effect-aware/indexed/non-indexed/line/point/strip paths honor read/write masks, every compare/op, `ReferenceStencil`, and clockwise vs counter-clockwise state; rejected alpha does not mutate depth/stencil. Reuse/extend EasyGL shared state fixtures. |
| SOFTWARE-122 | Implement deterministic CPU `OcclusionQuery` | ⬜ | Begin/End/IsComplete/PixelCount count raster samples that survive clipping, coverage, alpha, scissor and depth/stencil tests without changing render results; visible/occluded and lifecycle/validation fixtures pass. |
| SOFTWARE-123 | Close SpriteBatch and SpriteFont behavioral coverage | ⬜ | Audit every Draw/DrawString overload, rectangles, origin/scale/rotation/flips/layer depth/sort modes/transform/viewport/scissor/state interactions/RTs, font spacing/newline/fallback and disposal; convert reusable EasyGL fixtures to shared sources and repair every observed mismatch. |
| SOFTWARE-124 | Close GraphicsDevice/resource lifecycle and validation parity | ⬜ | Resize/virtual resolution and meaningful presentation parameters, creation/disposal/double-dispose, bound-resource disposal, reset/device events where applicable, argument/range validation and exception types are tested. Physical-window/context-only behavior is classified `NOT-APPLICABLE-SOFTWARE`. |
| SOFTWARE-125 | Prove classic `Model.Draw()` end to end on Software | ⬜ | Rigid and skinned model mesh/effect orchestration, hierarchy, multiple meshes/effects and 32-bit indices render through public `Model.Draw()`; low-level SkinnedEffect tests alone do not close this row. |
| SOFTWARE-126 | Audit and complete classic texture/render-target formats | ⬜ | NPOT and every XNA-relevant supported packed/compressed format has correct validation, SetData/GetData and sampling; unsupported formats reject at construction/transfer instead of being silently treated as RGBA8. Modern HDR-only requirements remain deferred. |
| SOFTWARE-127 | Reconcile meaningful Software presentation/reset behavior | ⬜ | Backbuffer resize, virtual-resolution viewport reset, applied MSAA/depth formats and target switching/restoration match public XNA/core semantics without inventing a physical window, swap chain or GPU context. |
| SOFTWARE-128 | Reconcile current Software documentation and feature matrix | ⬜ | `docs/software-renderer.md`, the graphics renderer feature matrix and current plan summary agree with code/tests; old statements such as two blend modes, unconditional bilinear clamp, and permanently excluded classic lighting/fog are retained only as clearly labeled history. |
| SOFTWARE-129 | Implement classic XNA hardware instancing on the CPU | ⬜ | Consume all per-instance streams with `VertexBufferBinding.InstanceFrequency`, apply instance transforms/colors through the same stock-effect contract as EasyGL/FNA, honor offsets/base vertex/index widths and validate ranges. Public indexed/non-indexed multi-stream instances must produce deterministic pixels; `Instancing` is advertised only after they pass. |
| SOFTWARE-130 | Reconcile EasyGL's stock-input format guard with XNA/FNA declaration conversion | ⬜ | FNA3D binds each declared format directly to a semantic-matched shader input and supplies missing vector components through native defaults; EasyGL currently rejects most non-canonical formats before its own general attribute binder can do so. Replace the exact-format guard with conversion-compatible validation and run the declaration format fixture on both EasyGL and Software. |
| SOFTWARE-131 | Match XNA/D3D9 integer pixel-center mapping in Software's 3D viewport transform | ✅ | Completed 2026-09-07. The corner-origin CPU raster coordinates now apply the same just-under-half-pixel `63/128` screen displacement used by EasyGL/Wine/MonoGame to reproduce D3D9 integer pixel centers. The measured shared XNA one-pixel-triangle fixture passes 2/2 on both Software and EasyGL. This also corrected the formerly failing non-integer 3D PointClamp U2 case: the full shared `PointSamplingContract` now passes 146/146 instead of 145/146. |

### Current dependency order

`SOFTWARE-104` through `SOFTWARE-108` and `SOFTWARE-131` are complete foundations; `SOFTWARE-109`
continues the independent buffer-mutation and validation audit.
`SOFTWARE-108` unlocks correct dual-UV and arbitrary input work. `SOFTWARE-110/121` establish fragment correctness
before lighting/query conclusions. Resource tasks `SOFTWARE-118..120` precede the high-level
SpriteBatch/Model closure. Independent lifecycle and documentation work continues around any
blocked feature rather than waiting for it.

---

## Why this backend, in the owner's own words

> "Toto není backend pro běžné hraní, ale mohl by být překvapivě hodnotný... Pro kvalitu CNA by
> mohl mít větší hodnotu než šestý hardwarový backend."
> (This isn't a backend for regular gameplay, but could be surprisingly valuable... For CNA's
> quality, it could have more value than a sixth hardware backend.)

Stated use cases (verbatim, translated): headless CI; deterministic screenshot tests; testing
without a GPU; verifying basic XNA primitive operations; server environments; diagnosing
differences between backends; fallback when GPU initialization fails.

**How this differs fundamentally from the `HEADLESS` backend** (see `plan_headless.md`): Headless
proves "this code ran, with these arguments, this many times, and nothing leaked" but never
produces a real pixel — `ReadBackbuffer()` just reports the last `Clear()` color for every pixel.
Software is the opposite: it actually rasterizes real triangles into a real, CPU-owned RGBA8
framebuffer, so `GetBackBufferData()`/`ReadBackbuffer()` return genuinely correct pixels — this is
the entire point. It gives this project golden-image-style pixel tests (see
`docs/graphics-backend-feature-matrix.md`) that need **no GPU, no display server, and no driver**
at all, unlike the existing EasyGL/BGFX/Vulkan pixel tests which all need a real GPU context even
when run under Xvfb.

---

## Historical v1 design decisions (superseded where the parity campaign says so)

1. **Correctness and determinism over performance.** This is explicitly not a real-time gameplay
   backend — no SIMD, no multithreading, no tiling/binning in v1. Plain scalar loops are fine; a
   slow-but-obviously-correct rasterizer is more valuable here than a fast one that's subtly wrong,
   since the whole point is to be a trustworthy reference/diagnostic tool.
2. **Vertex format inference by stride, not a new common-interface change.** `IVertexBufferBackend`
   only ever receives raw bytes + a stride (`SetData(const void*, int vertex_count,
   std::size_t stride_in_bytes)`) — no `VertexDeclaration` is threaded through it. This project
   already has an established precedent for inferring vertex layout from stride alone (see the
   WebGPU backend's own stride-keyed pipeline selection: 16/20/24/32-byte strides map to
   `VertexPositionColor`/`VertexPositionTexture`/`VertexPositionColorTexture`/
   `VertexPositionNormalTexture` respectively). Software reuses that exact convention rather than
   inventing a new one or making a larger, riskier shared-interface change. v1 supports the 16/20/24
   strides (position+color, position+texture, position+color+texture); 32-byte
   (position+normal+texture) is deferred since lighting is out of scope for v1 anyway.
3. **The CPU framebuffer/depth buffer are the backend's real state, not a fiction.** A color buffer
   (RGBA8) and a depth buffer (float32) sized to the current render target (bound `RenderTarget2D`,
   or the backbuffer's `PresentationParameters` size when none is bound) back every operation for
   real. `Present()` is a no-op by default (matches the headless/server/CI use cases) — see
   `SOFTWARE-70` for an optional, later, opt-in "blit to a real window for visual inspection" mode,
   which is explicitly NOT required for this backend's stated value proposition.
4. **No window by default**, mirroring `HEADLESS`'s own `GraphicsDevice`/`Game::Run()` integration
   (`plan_headless.md` design decision 2): `#ifdef CNA_BACKEND_HEADLESS` guards in
   `GraphicsDevice.cpp`'s constructor and `createOrAttachWindow()` are extended to also cover
   `CNA_BACKEND_SOFTWARE` (`#if defined(CNA_BACKEND_HEADLESS) || defined(CNA_BACKEND_SOFTWARE)`) —
   a small, mechanical extension of an existing pattern, not a new one.
5. **SpriteBatch reuses the same triangle rasterizer as 3D draws.** A `SpriteBatch::Draw()` call is
   just a textured quad (2 triangles) with a transform matrix; feeding it through the same
   rasterizer core used for `DrawPrimitivesEx` gives this backend a real, pixel-correct 2D sprite
   path essentially for free — a capability `HEADLESS` never had (its `SpriteBatch` only records
   call arguments, never renders anything).
6. **`BasicEffect` subset only, no lighting/fog in v1.** `GpuDrawParams`' `vertexColorEnabled`,
   `textureEnabled`/`texture0`, and `diffuseColor` (material color/alpha) drive v1 pixel shading.
   `lightingEnabled`, `fogEnabled`, `dualTexture`, `envMapping`, `skinned` are all explicitly out of
   scope for v1 (matches the owner's own stated minimal first-version list — see "Boundaries").
7. **Only two blend modes in v1**: `Opaque` (direct overwrite, no blend) and `AlphaBlend`
   (`SrcAlpha`/`InvSrcAlpha`, the two most common real XNA/FNA `BlendState` presets). Other
   `ApplyBlendState(...)` factor/op combinations fall back to `AlphaBlend` behavior in v1 rather
   than attempting a fully general blend-equation interpreter — a real scope limitation, recorded
   honestly rather than silently claimed complete (see `SOFTWARE-42`).
8. **Custom `Effect` (GLSL/HLSL/WGSL source) cannot execute on the CPU**, same limitation
   `HEADLESS-16` already documents for that backend: accepts any effect source without compiling
   it, but only actually renders correctly for effects whose `FillGpuDrawParams()` output maps onto
   what v1's fixed pixel-shading path understands (see design decision 6). A custom effect with
   genuinely custom shader logic will not visually match its GPU-backend rendering under Software —
   documented as a known, permanent-for-v1 limitation, not a bug.

---

## Historical v1 execution order

1. Phase S1 (CMake integration + skeleton) unblocks everything else, exactly as `plan_headless.md`
   Phase N1 did for that backend.
2. Phase S2 (framebuffer/render targets) and Phase S3 (vertex/index storage + format inference) are
   independent of each other and can be done in either order, but both must land before Phase S4.
3. Phase S4 (rasterizer core: transform → clip → rasterize → depth test) is the heart of this
   backend — get a single flat-colored, non-textured, non-blended triangle rendering and
   pixel-verified correct (`SOFTWARE-60`'s first test) before adding Phase S5's pixel-shading
   features on top.
4. Phase S5 (blending/texture/vertex-color) builds directly on S4's per-pixel loop.
5. Phase S6 (effect integration, SpriteBatch reuse) is the actual point of this backend — like
   `plan_headless.md` Phase N6, it should be verified continuously against Phase S4/S5's already-
   proven core, not left to the end.
6. Phase S7 (tests) — per this project's own convention (`CLAUDE.md`), add test coverage in the
   same task that implements each capability, not bolted on afterward. `SOFTWARE-60`'s pixel tests
   in particular are this backend's actual reason to exist — do not skip them.
7. Phase S8 (docs) — write `docs/software-backend.md` as capabilities land, not all at the end.

For every task: build the affected target(s), run the relevant tests, and do not mark a task ✅
without both.

---

## Phase S1 — CMake integration and skeleton

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-1 | Add `"SOFTWARE"` to `CNA_GRAPHICS_BACKEND`'s CMake `STRINGS` property and a matching `CNA_BACKEND_SOFTWARE` option flag, following the exact existing pattern for `SDL_RENDERER`/`EASYGL`/`BGFX`/`VULKAN`/`WEBGPU`/`HEADLESS` | ✅ | Verified 2026-07-13: configures cleanly with `-DCNA_GRAPHICS_BACKEND=SOFTWARE`; `-DCNA_BACKEND_SOFTWARE=ON` explicit-option form also wired in. |
| SOFTWARE-2 | `cna_backend_graphics_software` static library target (`elseif(CNA_GRAPHICS_BACKEND STREQUAL "SOFTWARE")` block, mirrors `HEADLESS`'s own) | ✅ | Verified 2026-07-13: builds clean, no external deps beyond SDL3 (windowing types only, via the shared `IGraphicsBackend.hpp` forward declarations -- never touches SDL's actual rendering/GL/Vulkan surface). |
| SOFTWARE-3 | `include/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.hpp` + `.cpp`: class implementing every `IGraphicsBackend` pure virtual — initially real where Phase S1 can make it real (Clear/Present/viewport), honest no-op/throwing stubs elsewhere until later phases replace them | ✅ | Verified 2026-07-13: implements every pure virtual (`Clear`/`Present`/`GetViewportSize`/`SetVirtualResolution`/`SetPresentationMode`/`GetWindowInternal`/`GetRendererInternal`/`CreateTexture`/`CreateSpriteBatch`/all 6 `Clear*` variants/`SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`/`CreateVertexBuffer`/`CreateIndexBuffer16`/`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`) plus the optional extension points a full game needs (render targets, effects, sprite batch). `CnaTests` (the full pre-existing GTest corpus) links and runs cleanly against it (4371/4373 pass, 2 unrelated hardware-sensor skips), confirming interface completeness — the same verification bar `HEADLESS-3` set. `Clear`/`Present`/`GetViewportSize`/`ReadBackbuffer`/render-target binding are genuinely real (Phase S2); `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` are still argument-validating placeholders, honestly not yet rasterizing (Phase S4's job). |
| SOFTWARE-4 | Factory dispatch for `SOFTWARE`; extend the existing `#ifdef CNA_BACKEND_HEADLESS` guards in `GraphicsDevice`'s constructor and `createOrAttachWindow()` to also cover `CNA_BACKEND_SOFTWARE` (design decision 4) | ✅ | Verified 2026-07-13: both guard sites now read `#if defined(CNA_BACKEND_HEADLESS) \|\| defined(CNA_BACKEND_SOFTWARE)`. `Software_Smoke` CTest confirms `SDL_WasInit(SDL_INIT_VIDEO) == 0` and `GetWindowInternal() == nullptr` end-to-end, and the full `CnaTests` suite runs with `DISPLAY`/`WAYLAND_DISPLAY` unset and `SDL_VIDEODRIVER` empty. Rebuilt `EASYGL` and `HEADLESS` after this change to confirm zero regression to either (both still build and pass their own test suites unchanged). |

---

## Phase S2 — Framebuffer and render targets

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-10 | CPU color framebuffer (RGBA8, `std::vector<uint8_t>`) sized to the backbuffer (`PresentationParameters`); real `Clear(r,g,b,a)` fills every pixel | ✅ | Verified 2026-07-13 (`Software_Smoke` Check B): `Clear(Color(20,40,60,255))` followed by `GetBackBufferData()` returns exactly that color for every pixel in the queried region — not a fiction, a real per-pixel write-then-read round trip. |
| SOFTWARE-11 | CPU depth buffer (`std::vector<float>`), real `ClearDepth`/depth-inclusive `Clear*` variants | ✅ | `SoftwareFramebuffer::ClearDepthValue()` implemented and wired into `ClearDepth`/`ClearColorAndDepth`/`ClearDepthAndStencil`/`ClearColorDepthAndStencil`. Not yet exercised by a dedicated depth-readback test (no public API reads the depth buffer directly) — depth *correctness* will be verified properly once Phase S4's depth-test-driven rasterizer lands and can be proven via occlusion (a nearer triangle correctly occluding a farther one), a much stronger test than reading the raw buffer. |
| SOFTWARE-12 | `SoftwareRenderTargetBackend`: `CreateRenderTarget2D`/`SetRenderTarget2D` binds an alternate CPU color+depth buffer pair sized to that target, instead of the default backbuffer pair | ✅ | Verified 2026-07-13 (`Software_Smoke` Check C): binding an 8×8 `RenderTarget2D`, clearing it to a distinct color, then unbinding and reading the backbuffer confirms both framebuffers are genuinely independent — the render target's clear never touched the backbuffer's own pixels. Since 2026-08-01, `mipMap=true` also retains a generated RGBA8 chain after unbind; the shared GDI regression verifies its pixels, readback and minified sampling. |
| SOFTWARE-13 | `ReadBackbuffer()`/`GraphicsDevice::GetBackBufferData()`: real `memcpy` from the currently-bound CPU framebuffer — no faking, this is the backend's core value proposition | ✅ | Implemented as a real per-pixel copy (not literally `memcpy`, since out-of-bounds region requests are zero-filled rather than reading garbage) from `CurrentFramebuffer()`. Verified by the same `Software_Smoke` Checks B/C above. |
| SOFTWARE-14 | `Present()`: no-op by default, matching the headless/CI/server use cases | ✅ | Implemented as a genuine no-op (`{}`), matching design decision 3. |

---

## Phase S3 — Vertex/index storage and format inference

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-20 | `SoftwareVertexBufferBackend`: stores raw bytes + stride (real storage, not discarded, mirroring `HeadlessVertexBufferBackend`'s own `ShadowData()` pattern) | ✅ | Verified 2026-07-13 (`Software_Smoke` Check D): real `VertexBuffer`s at strides 16 (`VertexPositionColor`)/20 (`VertexPositionTexture`)/24 (`VertexPositionColorTexture`) all round-trip real vertex data through `SetData()` without throwing. `Data()`/`Stride()` accessors exist for Phase S4's rasterizer to read from directly. |
| SOFTWARE-21 | `SoftwareIndexBufferBackend`: 16- and 32-bit, real storage | ✅ | Verified 2026-07-13 (`Software_Smoke` Check E): both a 16-bit and a 32-bit `IndexBuffer` round-trip real index data without throwing. A genuine bit-width mismatch (`SetData16` on a buffer declared 32-bit or vice versa) throws `std::runtime_error`, mirroring `HeadlessIndexBufferBackend`'s own precedent. |
| SOFTWARE-22 | Stride-based vertex format inference (design decision 2): 16→`VertexPositionColor`, 20→`VertexPositionTexture`, 24→`VertexPositionColorTexture`. 32-byte (`VertexPositionNormalTexture`) explicitly deferred (needs lighting, out of scope for v1) | ✅ | **Closed** (dispatch logic landed with Phase S4's rasterizer, `BuildGenericClipVertex`/`BuildPositionColorClipVertex`, verified by `Software_Rasterizer`/`Software_Effects`). The 32-byte deferral was itself later lifted by `SOFTWARE-82` (Phase S9, 2026-07-13), which added real stride-32 (`VertexPositionNormalTexture`, `EnvironmentMapEffect`) and stride-52 (`VertexPositionNormalTextureSkinned`, `SkinnedEffect`) dispatch once a real (if lighting-free) use for the normal existed. `GLTF-387` completed the canonical table with 48/60-byte rigid PBR, 56-byte coloured skinning and 68/76-byte skinned PBR, all reached by the glTF native-boundary sweep. The 60/76 forms carry UV1; SOFTWARE consumes bit 0 of the shared PBR selector and slot zero's transform for its base-colour-only fallback. |

---

## Phase S4 — Rasterizer core

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-30 | Per-vertex CPU transform: `World * View * Projection` (matching CNA's existing `Matrix` row/column convention) → clip space | ✅ | Verified 2026-07-13: uses CNA's own `Matrix::operator*` (row-major, row-vector — `combined = world*view*projection`) and the existing `Vector4::Transform(Vector3, const Matrix&)` helper, reusing established codebase math rather than reimplementing the transform. `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` now genuinely rasterize (matching `VertexPositionColor`'s fixed layout — Position at offset 0, packed RGBA8 Color at offset 12, per that method's own documented "equivalent to BasicEffect with VertexColorEnabled=true" contract). |
| SOFTWARE-31 | Perspective divide + viewport transform → screen-space X/Y/Z | ✅ | `ndc = clip.xyz / clip.w`; `screenX=(ndcX*0.5+0.5)*viewportWidth`, `screenY=(1-(ndcY*0.5+0.5))*viewportHeight` (Y-flip: NDC is Y-up, the framebuffer is Y-down/top-left-origin). Depth used directly as `ndcZ` with no remapping, since CNA's projection matrices (like real XNA/FNA/D3D) already produce a 0..1 post-divide Z range, not OpenGL's -1..1. |
| SOFTWARE-32 | Triangle rasterization core: edge-function/barycentric fill, per-pixel depth test (`LessEqual`, matching `DepthStencilState::Default`) against the bound depth buffer, write-on-pass | ✅ | Standard edge-function rasterizer, bounding-box-limited, accepting either triangle winding (no backface culling in v1 — a real, intentional simplification, see Boundaries). Verified via the new `Software_Rasterizer` CTest (5/5): a solid-color triangle renders the exact color at its center pixel; **order-independent depth occlusion proven directly** (Checks C/D draw a near-red and far-blue triangle in both possible orders — red wins both times, proving the depth test is real, not an accidental last-write-wins artifact). |
| SOFTWARE-33 | Perspective-correct attribute interpolation (vertex color, UV) across the triangle | ✅ | Vertex colors are premultiplied by `1/clip.w` before rasterization, interpolated linearly in screen space, then divided by the interpolated `1/w` at the end (the standard technique). Verified via `Software_Rasterizer` Check B: a red/green/blue triangle's centroid pixel is the exact barycentric average of all three vertex colors. UV/texture interpolation follows the same mechanism once Phase S5 adds texture sampling. |
| SOFTWARE-34 | Basic near-plane handling: at minimum, cull triangles entirely behind the near plane; full polygon near-plane clipping (splitting a triangle into 1-2 new triangles) is a known v1 limitation, flagged here for a likely follow-up task once basic rendering is proven correct | ✅ | Implemented as a per-vertex `clip.W <= 1e-5f` check in `TransformPositionColorVertex()` — if any of a triangle's 3 vertices fails it, the whole triangle is culled rather than rasterized with garbage post-divide coordinates. Not yet exercised by a dedicated test with geometry deliberately crossing the near plane (all `Software_Rasterizer` test geometry stays safely in front of the camera) — a real, acknowledged gap, not silently claimed fully verified. |

---

## Phase S5 — Pixel shading

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-40 | Vertex-color modulation (`vertexColorEnabled` path) | ✅ | `TransformGenericVertex()` reads real per-vertex color at stride 16/24 (position+color strides) and forces opaque white when `GpuDrawParams.vertexColorEnabled` is false — matches real XNA: `BasicEffect.VertexColorEnabled` itself defaults to `false`, so a plain `BasicEffect` ignores vertex colors unless a game opts in. Discovered directly while debugging `Software_Rasterizer`'s initial post-Phase-S6 regression (see `SOFTWARE-50`'s notes) — a real behavioral fact, not a rasterizer bug. |
| SOFTWARE-41 | Nearest-neighbor texture sampling (`textureEnabled`/`texture0`), reading the already-stored CPU-side pixel data every `ITextureBackend` implementation already keeps | ✅ | Verified 2026-07-13 (`Software_Effects` Check A): a full-screen quad textured with a 2x2 checker (`Texture2D::CreateFromPixels`) samples the correct texel near two opposite corners. `params.texture0` supports both `SoftwareTextureBackend` and `SoftwareRenderTargetBackend`; a completed mipmapped render target exposes its generated levels through the same sampling path. The same CPU SpriteBatch path also applies `ColorMatrixEffect` after texture/tint and before blending. Bilinear sampling remains a v2 stretch goal. |
| SOFTWARE-42 | Exact XNA blending for the shared shaded raster path: all `Blend` factors, independent colour/alpha `BlendFunction`, `BlendFactor`, and post-blend channel masks. | ✅ | `ApplyBlendState()` stores all six factor/function ordinals and `SetBlendFactor()` stores the dynamic constant; every shaded CPU fragment evaluates them per channel. The shared Release GDI 2D pixel regression covers Opaque, premultiplied AlphaBlend, NonPremultiplied, Additive, independent equations and BlendFactor. `Software_Effects` uses `NonPremultiplied` for its straight-alpha blend case. |
| SOFTWARE-43 | `diffuseColor`/alpha modulation from `GpuDrawParams` (BasicEffect's material color) | ✅ | Verified 2026-07-13 (`Software_Effects` Check B): `BasicEffect.DiffuseColor=(0.5,0,0)` tints a white texture to half-intensity red. Applied after texture sampling, matching the order real BasicEffect/XNA shaders apply material color. |

---

## Phase S6 — Effect integration

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-50 | Wire `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` to the Phase S4/S5 rasterizer using `GpuDrawParams`' `vertexColorEnabled`/`textureEnabled`/`texture0`/`diffuseColor`/`worldColMajor` fields; `lightingEnabled`/`fogEnabled`/`dualTexture`/`envMapping`/`skinned` explicitly out of scope for v1 | ✅ | Verified 2026-07-13 via `Software_Effects` (5/5) and re-verified `Software_Rasterizer` (5/5) still passing through the now-real `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` overrides (previously only reachable via `IGraphicsBackend`'s own default fallback to `DrawColoredPrimitives`). Stride validated (16/20/24 only at closure; later extended by SOFTWARE-82 and `GLTF-387`). The original `textureEnabled && texture0==nullptr` refusal was deliberately replaced by `GLTF-387`'s white optional-base-map fallback: stock PBR and Skinned effects select a textured program without requiring a base map, and the native shader renderers bind white. Missing mandatory DualTexture/environment maps still refuse. **Real bug caught by this exact wiring**: `Software_Rasterizer`'s existing checks briefly went 0/5 the moment this override landed, because `BasicEffect.VertexColorEnabled` defaults to `false` in real XNA/FNA (verified directly in `BasicEffect.hpp`) — the fallback path `DrawColoredPrimitives` had been implicitly always treating color as enabled, masking this. Fixed by updating the test to explicitly set `VertexColorEnabled = true` (matching how a real game would), not by changing the (correct) new behavior. |
| SOFTWARE-51 | `SpriteBatch` reuses the same rasterizer for its quad draws (design decision 5) — a real, pixel-correct CPU `SpriteBatch` path | ✅ | `SoftwareSpriteBatchBackend::Draw()` builds its quad corners using the exact same formula as `EasyGLGraphicsBackend::EasyGLSpriteBatchBackend::Draw()` (destination/source rectangle, origin, rotation, `SpriteEffects` flip), then feeds two triangles directly into `RasterizeTriangleShaded()` in screen-pixel space (no `World*View*Projection` — SpriteBatch never uses one). `transformMatrix_` is applied as a 2D point transform on the already-placed corners. Verified via `Software_Effects` Check E: a solid-color texture drawn via `SpriteBatch::Draw()` lands at the exact requested screen position. Rotation/origin/`SpriteEffects` flip are implemented (reusing the proven formula) but not yet covered by a dedicated test — a real, acknowledged gap. |
| SOFTWARE-52 | Custom `ShaderEffect` (arbitrary GLSL/HLSL/WGSL source): accept without compiling (mirrors `HEADLESS-16`), document that only effects whose `FillGpuDrawParams()` output matches v1's fixed pixel-shading path will render correctly (design decision 8) | ✅ | Already implemented since Phase S1 (`SoftwareEffectBackend::CompileProgram()`): accepts any non-empty vertex/fragment source without compiling, mirroring `HEADLESS-16`. Not yet exercised by a dedicated Software test (Headless has one via `Headless_ResourceBackends`); the underlying behavior is identical and low-risk, so this is a documentation/test-coverage gap, not an implementation gap. |

---

## Phase S7 — Tests

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-60 | Deterministic pixel tests via real `GetBackBufferData()` readback, no GPU/display needed: clear-color test, single flat-colored triangle test, single textured quad test | ✅ | Done, and then some, across `Software_Smoke`/`Software_Rasterizer`/`Software_Effects` (16 checks total): clear-color (`Software_Smoke` B), flat-colored triangle + depth occlusion (`Software_Rasterizer`), textured quad + blending + `SpriteBatch` (`Software_Effects`). Did not need a `PixelTestGame`-style shared harness — each test file is its own small `Game` subclass reading pixels directly via `GetBackBufferData()`, mirroring the exact pattern established for `HEADLESS`'s own test suite. |
| SOFTWARE-61 | (Stretch goal) Cross-backend diagnostic test: render an identical simple scene on `SOFTWARE` and a real GPU backend, compare within a tolerance — directly serves the owner's stated "diagnostika rozdílů mezi backendy" use case, but depends on a real GPU backend being available in the test environment; may not be runnable in every CI | ✅ | **Closed 2026-07-13 via `SOFTWARE-84`** (Phase S9) — see that row for the implementation (a shared scene source built per-backend + a standalone comparator, run manually since `CNA_GRAPHICS_BACKEND` is a compile-time choice). |
| SOFTWARE-62 | CTest registration, mirroring `HEADLESS`'s own `cna_headless_test`-style CMake macro | ✅ | `cna_software_test()` CMake macro (mirrors `cna_headless_test()` exactly), 3 tests registered: `Software_Smoke`, `Software_Rasterizer`, `Software_Effects`, all labeled `"Software"`, no `SDL_VIDEODRIVER`/`DISPLAY` set. |

---

## Phase S8 — Docs

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-70 | `docs/software-backend.md`: what it's for/not for, current capability boundary, how to write a test, known limitations — mirrors `docs/headless-backend.md`/`docs/webgpu-backend.md`'s structure. Optionally document an opt-in "blit the CPU framebuffer to a real window" mode here if it ends up being worth adding (design decision 3) | ✅ | Written 2026-07-13, mirroring `docs/headless-backend.md`'s structure exactly (Status / What it's for-isn't / Writing a test / Known limitations). The opt-in "blit to a real window" mode is documented as a reasonable future addition, not implemented (not needed for this backend's actual value proposition). |
| SOFTWARE-71 | `docs/graphics-backend-feature-matrix.md`: unlike `HEADLESS` (which was deliberately kept OUT of that matrix, since it never renders a real pixel), `SOFTWARE` actually could become a genuine pixel-parity column once mature enough — flag as a real future opportunity rather than deciding now, since v1's feature set is too narrow for a meaningful comparison yet | ✅ | Added a note right after the existing `HEADLESS` note explaining exactly this distinction (Software *does* render real pixels, unlike Headless, so it's a real future-column candidate once its feature set broadens) rather than adding a premature/misleading column now. |

---

## Historical Phase S9 — v2 improvement candidates proposed in 2026-07

Every row below is a real, scoped-down v1 limitation from Phases S1-S8, listed here as concrete
follow-up candidates rather than left as a vague "could be better later" note. The original
"none authorized" rule was superseded on 2026-09-07 by the owner-authorized XNA/Core parity
campaign above. It remains historically useful only for understanding why the initial scope was
small. Ordered roughly by value-for-effort, cheapest/highest-value first.

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-80 | Bilinear texture sampling (currently nearest-neighbor only, `SOFTWARE-41`) | ✅ | **Closed 2026-07-13.** Implemented as `SampleBilinear()`: standard half-texel-offset bilinear with clamp-to-edge at the boundaries (matches this backend's own existing "texture address modes not honored, UVs are simply clamped" simplification rather than adding real Wrap/Mirror support). Always on (not gated by `SamplerState.Filter` — a real, documented v1 simplification; the actual default `SamplerState.LinearWrap` already implies Linear filtering everywhere in real XNA, so this is arguably *more* faithful than the nearest-neighbor it replaces, not less). **Caught and fixed a real bug during implementation**: the first version clamped `x0`/`y0` and then computed `x1=clamp(x0+1,...)`/`y1=clamp(y0+1,...)` *from the already-clamped* `x0`/`y0`, shifting the second sample to the wrong texel right at a texture edge — caught immediately by `Software_Effects`' own corner-sampling check failing with a visibly blended, non-solid color at pixel (0,0)-(4,4) instead of pure `Red`. Fixed by computing both raw (pre-clamp) indices first, then clamping each independently. Re-verified all 3 Software CTests (16/16) and the full `CnaTests` suite (4371/4373, same baseline) after the fix. |
| SOFTWARE-81 | Backface culling (`RasterizerState.CullMode`) — currently both winding orders always accepted (`SOFTWARE-32`'s own noted simplification) | ✅ | **Closed 2026-07-13.** `ApplyRasterizerState()` now stores the raw `CullMode` ordinal (`cullMode_`); a new `ShouldCullTriangle(area, cullMode)` helper is checked in both `RasterizeTriangle`/`RasterizeTriangleShaded` right after the existing degenerate-triangle check. In this backend's screen-space convention (Y grows downward), a negative signed `area` is clockwise-as-displayed and a positive `area` is counter-clockwise — verified **empirically**, not just derived, by the new `Software_Culling` CTest (5/5: default `CullCounterClockwise` keeps CW visible and culls CCW; `CullClockwise` reverses that; `CullNone` disables culling entirely). `SoftwareSpriteBatchBackend`'s quads also read the owner's cull mode via a new `GetCullMode()` accessor — matching real FNA, whose own `SpriteBatch` defaults its `RasterizerState` to `CullCounterClockwise` (not `CullNone`); its quad corner order was checked by hand to already be clockwise-as-displayed, so it survives the real default unaffected. **Existing-test mitigation**: `software_rasterizer_test.cpp`/`software_effects_test.cpp`'s triangles were authored purely for pixel-correctness and turned out to be counter-clockwise-as-displayed (would newly be culled under the real default) — both now explicitly call `dev.setRasterizerStateProperty(RasterizerState::CullNone)` at the top of `Draw()` to keep testing what they were designed to test. Verified: all 4 Software CTests (21/21 checks) plus the full `CnaTests` suite (4371/4373, same baseline) after the change. |
| SOFTWARE-82 | `DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`-specific `GpuDrawParams` fields (second texture, env map, bone transforms) — currently only the `BasicEffect` subset (`vertexColorEnabled`/`textureEnabled`/`diffuseColor`) is read, matching design decision 6 | ✅ | **Closed 2026-07-13.** Deliberately scoped to stay consistent with design decision 6 (no lighting engine in v1): all 3 effects' "lit" base color is `vertexColor*diffuseColor*texture0`, the same simplification the plain `BasicEffect` path already uses — no per-light `NdotL` diffuse sum, ambient, or emissive term for any of them (real `EasyGL`/`Vulkan`/`Bgfx` backends DO compute that sum; this backend still doesn't, for any effect). What's genuinely new: (1) **`DualTextureEffect`**: real second-texture sampling + FNA's `color.rgb*=2; color*=overlay*diffuse` formula, both textures reusing the same UV (this backend has no 2-UV vertex format — matches this codebase's own Vulkan `dual_texture3d` shader precedent, not a new simplification). (2) **`EnvironmentMapEffect`**: a new `SoftwareTextureCubeBackend` gives `CreateTextureCube` real 6-face RGBA8 storage for the first time (previously always `nullptr`); a new 32-byte `VertexPositionNormalTexture` stride carries a per-vertex normal, clipped/interpolated as a `ClipVertex`/`RasterVertex` attribute exactly like color/UV; world-space position/normal are computed via a new `ApplyAffineColumnMajor()` helper applied to `GpuDrawParams::worldColMajor` (using `World` directly rather than the mathematically-correct `WorldInverseTranspose` — exact for uniform-scale/no-shear `World`, a real simplification for non-uniform scale); the reflection vector (`reflect(-eyeVector, worldNormal)`) is sampled against the cube map via a new `SampleCubeMap()` (standard largest-axis face-select + per-face UV projection, nearest-neighbor only, no cross-face bilinear at seams); Fresnel weighting and the specular-tint term both match FNA's own `PSEnvMapSpecular` formula. (3) **`SkinnedEffect`**: a new 52-byte `VertexPositionNormalTextureSkinned` stride (the project's own existing canonical skinned-vertex layout, already used by the Avatar real-rendering path) is read; up to `WeightsPerVertex` bone matrices are blended (column-major, matching `GpuDrawParams::boneTransforms`' own layout) and applied to the vertex position *before* World\*View\*Projection, mirroring FNA's `Skin(vin, boneCount)` step — this needed no new pixel-shading logic at all, since FNA's own `SkinnedEffect.fx` pixel shaders use the exact same `texture*diffuse` formula already implemented for `BasicEffect`. New `Software_DualEnvmapSkinned` CTest (4/4): DualTextureEffect's blend formula against a hand-computed expected color; EnvironmentMapEffect's reflection sampling the correct cube face for a camera-facing surface (derived by hand: eye at world origin, quad facing the camera means `eyeVector==normal`, so `reflect(-eyeVector,normal)` is exactly the view axis) and `EnvironmentMapAmount=0` showing the plain base texture; SkinnedEffect rendering at the bone-translated screen position, not the bind pose. All 4 checks passed on the first run (hand-derivation confirmed empirically, not just assumed). Verified: all 6 Software CTests (29/29 checks) and the full `CnaTests` suite (4371/4373, same baseline). |
| SOFTWARE-83 | Real near-plane polygon clipping (split a triangle crossing the near plane into 1-2 new triangles) instead of culling the whole triangle (`SOFTWARE-34`'s own acknowledged v1 gap) | ✅ | **Closed 2026-07-13; historical implementation superseded by `SOFTWARE-106`.** A new `ClipVertex` (clip-space, un-premultiplied attributes) replaced the old "transform straight to screen-space RasterVertex, fail the whole triangle if any vertex has `clip.W<=~0`" flow. `BuildPositionColorClipVertex`/`BuildGenericClipVertex` built 3 `ClipVertex`; a new Sutherland-Hodgman `ClipTriangleNearPlane()` clipped against the single `w>kNearEpsilon` half-space, returning 0 (fully behind, discarded — same end result as the old whole-triangle cull), 3 (no clip needed, or one corner clipped off), or 4 (two corners clipped off, a quad) vertices, preserving winding so `SOFTWARE-81`'s culling stayed correct on the result. `LerpClipVertex` interpolated position AND color/UV together (clip space is still linear pre-divide, so a plain lerp is exact — unlike screen space). `ClipVertexToRasterVertex` did the perspective divide + viewport transform once, after clipping; a 4-vertex quad was fan-triangulated into 2 draw calls. All 4 draw paths (`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`) were updated identically. This was the verified v1 stepping stone; `SOFTWARE-106` now provides the current six-plane behavior and replacement 11/11 fixture. |
| SOFTWARE-84 | `SOFTWARE-61`'s cross-backend diagnostic test: render an identical scene on `SOFTWARE` and a real GPU backend, compare within a tolerance | ✅ | **Closed 2026-07-13.** `CNA_GRAPHICS_BACKEND` is a compile-time choice, so this isn't a single automated `ctest` — it's a shared, backend-agnostic scene source (`examples/cross_backend_diagnostic_scene.cpp`, a fully unlit vertex-color triangle, no lighting/texture involved) built once per backend needing it (`cna_diag_software` in the `SOFTWARE` section, `cna_diag_easygl` in the `EASYGL` section of `CMakeLists.txt`), dumping a raw 64x64 RGBA8 file, plus a standalone comparator (`cna_diag_compare`, no CNA dependency) that diffs two dumps within a tolerance. Documented with the exact 3-command manual invocation in `docs/software-backend.md`'s new "Cross-backend diagnostic" section. **Actually run end-to-end this session** (this repo's real desktop `:0` session, per the established WebGPU-verification precedent, provided the display `EASYGL` needed): `SOFTWARE` vs. `EASYGL` gave a max per-channel diff of 1 (mean 0.139) — effectively identical. The comparator was also checked against a deliberately corrupted dump to confirm it genuinely fails on a real mismatch (max diff 255), not just always passing. Directly serves the owner's original "diagnostika rozdílů mezi backendy" use case. |
| SOFTWARE-85 | Optional opt-in "blit the CPU framebuffer to a real window" mode, so a `SOFTWARE`-rendered frame can be visually inspected instead of only pixel-asserted (`Present()` is currently a pure no-op) | ⬜ | Would need `#ifdef CNA_BACKEND_HEADLESS \|\| defined(CNA_BACKEND_SOFTWARE)`'s window-skip guard in `GraphicsDevice.cpp` to gain a third, opt-in state for `SOFTWARE` specifically (window created but backend still self-renders in software, then blits) — touches shared code, more invasive than the others on this list. Lowest priority: not needed for this backend's actual value proposition (deterministic, GPU-free pixel tests). |
| SOFTWARE-86 | Performance work (SIMD/multithreading/tiling) | ⬜ | Explicitly NOT a goal per design decision 1 — only revisit if a specific real test becomes impractically slow, not preemptively. |

---

## Historical v1 boundaries (not the current parity boundary)

The bullets below record the decisions under which `SOFTWARE-1..86` landed. Any exclusion of a
classic XNA/Core feature here is superseded by the campaign tasks above. The current hard boundary
is modern CNAEXT engine work, plus behavior that is genuinely inapplicable without a physical
window/GPU.

- **GPU-init-failure fallback** (one of the owner's stated use cases) is a `Game`/
  `GraphicsDeviceManager`-level runtime policy decision (auto-switching the active backend when GPU
  init fails) — a separate, larger, cross-cutting feature, not part of this backend's own scope.
  Flag it as a possible follow-up plan if wanted later; do not fold it into this one.
- Full per-light `BasicEffect` lighting/fog (and the equivalent lighting inputs on
  `EnvironmentMapEffect`/`SkinnedEffect`), MRT, automatic mip generation for ordinary
  textures, anisotropic filtering, 3D
  textures, and render-target cube maps remain explicitly out of scope for v1, matching the
  owner's own minimal first-version list verbatim (clear; render target; triangle list; basic
  blending; depth buffer; simple textures; vertex colors; `BasicEffect` subset).
  **`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect` and plain (non-render-target) cube
  textures were later lifted out of this out-of-scope list by `SOFTWARE-82`** (Phase S9,
  2026-07-13, minus the per-light lighting caveat above) — see that row and
  `docs/software-backend.md`'s Known Limitations for exactly what's supported.
  Render-target 4x MSAA was likewise implemented later by the CPU rasteriser; `GLTF-395` locks its
  active-pass level-zero resolve/readback contract in addition to the existing unbound tests.
  `Model.Draw()` with real skinning specifically hasn't been separately verified end-to-end
  (only the lower-level `SkinnedEffect`/`DrawPrimitivesEx` path was tested).
- Performance/SIMD/multithreading work is explicitly not a goal (design decision 1) — do not
  spend effort here unless a specific test becomes impractically slow.
- Do not let `SOFTWARE`-specific code leak into the shared `IGraphicsBackend`/`GpuDrawParams`
  interface layer beyond what a genuine common-interface need justifies — same backend-locality
  rule the other backends (`CLAUDE.md`, `plan_webgpu.md`/`plan_headless.md`'s own boundaries)
  already follow.
- If Phase S4's rasterizer core turns out to need real polygon near-plane clipping sooner than
  expected (visible artifacts in even simple test scenes), treat that as a legitimate scope
  addition to flag and discuss, not something to silently skip or silently half-implement.
- **`TriangleStrip` remains unsupported.** `GLTF-387` extended the effect-aware indexed and
  non-indexed path to `LineList`, `LineStrip` and `PointListEXT`; near-plane-clipped lines and
  points share the established depth/blend/shading fragment path. Strips and the legacy coloured
  convenience paths retain the clear TriangleList-only refusal rather than silently misrendering.
  See `docs/software-renderer.md`'s Known Limitations for the current boundary.
- **Bilinear texture sampling (`SOFTWARE-80`) is always on, regardless of `SamplerState.Filter`**,
  and there is no real texture address-mode support — `Wrap`/`Mirror` are not implemented, UVs are
  simply clamped to `[0,1]` at the texture bounds regardless of what `SamplerState.AddressU/V`
  requests. This was a deliberate v1 simplification recorded in `SOFTWARE-80`'s own row (arguably
  *more* faithful than the nearest-neighbor sampling it replaced, since real XNA's default
  `SamplerState.LinearWrap` already implies linear filtering almost everywhere) but wasn't restated
  here in Boundaries until now — see `docs/software-backend.md`'s Known Limitations for the same
  point in the user-facing doc.
