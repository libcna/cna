# Skia Graphics Backend — Implementation Plan

> **Status: ACTIVE — raster vertical slice in progress.** The initial `SKIA` CMake selection,
> externally-built Skia dependency adapter, and CPU-raster presentation path are implemented.
> Every remaining row stays unchecked until its stated acceptance evidence exists.
>
> **Goal:** add `CNA_GRAPHICS_BACKEND=SKIA` to CNA. The first usable delivery is a complete,
> pixel-verified **2D** backend. Its target is every part of `EASYGL` that Skia can express
> directly. A missing one-to-one Skia API is *not* by itself a reason to omit an XNA feature: an
> emulation spike must first determine whether the public CNA behaviour can be reproduced.
> Features that neither Skia nor a tested emulation can provide are reported honestly through
> `SupportsCapability()` and deterministic `NotSupportedException`/`runtime_error` paths; they
> must never become silent no-ops.

Skia is a 2D graphics library, although it can render through raster, OpenGL, Vulkan, and other
device backends. `SkCanvas` has 2D images, geometry, transforms, clipping, compositing, shaders,
and surfaces; that does not turn it into XNA's vertex/index/depth 3D pipeline. The implementation
therefore starts from native Skia 2D operations and treats every EasyGL 3D feature as a separate,
evidence-based emulation decision. See Skia's [API overview](https://skia.org/docs/user/api/),
[surface-creation guide](https://skia.org/docs/user/api/skcanvas_creation/), and
[SkSL/runtime-effect model](https://docs.skia.org/docs/user/sksl/).

## Non-negotiable rules

1. **Direct first, emulation second, refusal last.** Implement any XNA behaviour Skia already
   represents. If it has no direct equivalent, write and execute a narrowly scoped emulation
   spike before refusing it. The spike must preserve observable CNA behaviour, not merely produce
   something visually similar.
2. **No accidental dependency on EasyGL.** `SKIA` owns its presentation/context path and its
   resources. It may share backend-neutral CNA code and tests, but must not call into the EasyGL
   backend or require the sibling `easy-gl` repository at run time.
3. **Preserve XNA contracts.** Argument validation, disposal, `SetData`/`GetData` transfer
   ranges, `RenderTargetUsage`, state lifetime, pixel origin, alpha convention, exceptions, and
   device-reset events are part of the feature. A superficially correct image is insufficient.
4. **Capabilities are measured, not aspirational.** `SupportsCapability()` returns true only
   after the relevant native path or emulation has passed its registered tests on the supported
   execution modes. The backend shall explicitly report its actual MSAA, anisotropy, and
   depth/stencil availability.
5. **One backend, two execution modes.** The architecture must support a deterministic raster
   `SkSurface` for headless/unit tests and an accelerated surface for normal presentation. The
   accelerated mode is selected only after the integration spike proves ownership, flushing,
   resize, and context-loss handling are sound. Both modes execute the same CNA resource and
   SpriteBatch code paths.
6. **No unbounded CPU fallback.** A CPU emulation is allowed only when its affected API surface,
   complexity/memory limits, synchronization, and pixel-level acceptance tests are recorded in
   this plan and the backend documentation. It must be selectable/observable, not a hidden
   per-frame surprise.

## Definition of parity

“EasyGL parity” below means the observable public CNA/XNA contract currently exercised by the
EasyGL suite, not the incidental fact that EasyGL happens to call OpenGL. A task may use a Skia
operation, a GPU-capability-dependent Skia operation, or an explicit CNA emulation. It is complete
only when it meets all of the following:

- builds with `CNA_GRAPHICS_BACKEND=SKIA` without enabling EasyGL;
- validates the same success and failure cases as its EasyGL counterpart;
- passes a focused Skia test plus the relevant backend-agnostic/CNA public-API test;
- has pixel tests compared against checked-in oracle data or a deliberately documented tolerance;
- records the final capability result and any device-dependent condition in
  `docs/skia-backend.md` and `docs/graphics-backend-feature-matrix.md`.

## Initial capability ledger

This is a planning classification, not a claim that code exists. “Direct” means Skia exposes the
building blocks; a task still has to establish XNA-equivalent semantics. “Emulation investigation”
means it may become supported only after the corresponding spike passes. “No direct path” never
authorizes a silent stub.

| EasyGL/CNA surface | Skia relationship | Planned first decision | Tracking tasks |
|---|---|---|---|
| Window surface, clear, present, viewport, resize | Direct through `SkSurface`/`SkCanvas`; GPU surfaces require an application-owned current API context | Implement, with raster and accelerated probes | SKIA-1–SKIA-20 |
| `Texture2D`, sprite image sampling, source rectangles, transforms, tint, flips, SpriteFont atlas | Direct image/canvas operations | Implement as the baseline 2D path | SKIA-21–SKIA-40 |
| Scissor/clip, 2D transform, layer ordering | Direct canvas state/clip operations | Implement and match XNA coordinate conventions | SKIA-41–SKIA-42 |
| Point/linear sampling; clamp/wrap/mirror addressing | Direct sampling/tile-mode building blocks | Implement after exact edge/seam tests | SKIA-43–SKIA-46 |
| Standard blend modes and alpha conventions | Direct compositing building blocks | Implement presets, then prove each factor/equation mapping | SKIA-47–SKIA-52 |
| Arbitrary blend factors/equations, independent alpha, colour write masks | No universal one-call `SkPaint` mapping | Test native `SkBlendMode`/`SkBlender`; otherwise isolated-layer or CPU emulation spike | SKIA-53–SKIA-57 |
| `RenderTarget2D`, readback, preserve/discard, mip chains | Direct surfaces/snapshots/readback building blocks | Implement, including no fabricated readback | SKIA-61–SKIA-75 |
| MSAA and anisotropic sampling | Supported only where the selected Skia surface/device supports it | Probe and expose actual device result; emulate only if contract permits | SKIA-76–SKIA-79 |
| `TextureCube`/`Texture3D` storage and transfer | Skia images are 2D; no direct cube/volume sampling path | Investigate bounded CPU storage emulation separately from shader sampling | SKIA-80–SKIA-84 |
| Multiple render targets | No direct `SkCanvas` MRT draw call | Command-replay/layer-emulation spike; otherwise clear refusal | SKIA-87–SKIA-88 |
| Custom `ShaderEffect` and SpriteBatch custom effects | SkSL runtime effects are not a GLSL vertex/fragment pipeline | Map a proven safe subset only; do not claim arbitrary GLSL compatibility | SKIA-89–SKIA-94 |
| Vertex/index buffers, primitive draws, stock 3D effects, depth/stencil, wireframe, instancing | No direct SkCanvas/XNA 3D pipeline | Audit and prototype only explicit emulators; v1 rejects unsupported calls consistently | SKIA-95–SKIA-103 |
| Occlusion queries | No direct XNA-style query surface | Establish whether a bounded emulation is semantically sound; otherwise unsupported | SKIA-104–SKIA-105 |

## Proposed source layout

The names are intentionally parallel to the existing backends so ownership is obvious during
review. They are targets, not files to create during the planning phase.

```text
include/CNA/Internal/Backends/Skia/
  SkiaGraphicsBackend.hpp           # device/surface/context ownership
  SkiaSurface.hpp                   # backbuffer and off-screen target abstraction
  SkiaTextureBackend.hpp            # Texture2D and CPU shadow/mips
  SkiaRenderTargetBackend.hpp       # RenderTarget2D
  SkiaSpriteBatchBackend.hpp        # XNA SpriteBatch -> SkCanvas
  SkiaStateMapping.hpp              # sampler/blend/raster mapping
  SkiaInterop.hpp                   # selected GPU presentation interop only
src/CNA/Internal/Backends/Skia/
  SkiaGraphicsBackend.cpp
  SkiaSurface.cpp
  SkiaTextureBackend.cpp
  SkiaRenderTargetBackend.cpp
  SkiaSpriteBatchBackend.cpp
  SkiaStateMapping.cpp
  SkiaInterop.cpp
cmake/Tests/SkiaTests.cmake
tests/CNA/Internal/Backends/Skia/
docs/skia-backend.md
```

`SkiaSurface` is the key boundary: it owns an `SkSurface`, exposes an `SkCanvas`, normalizes the
top-left CNA pixel convention, flushes before ownership crosses to SDL/the GPU, and produces
readback pixels. Texture and render-target wrappers never expose raw Skia pointers through the
public CNA API.

---

## Phase S0 — reconnaissance and design spikes

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-1 | Inventory every `IGraphicsBackend`, resource interface, `GraphicsCapability`, and public `GraphicsDevice` call against the EasyGL implementation; produce a row-per-entry parity ledger. | ⬜ | `docs/skia-backend.md` lists each entry, current EasyGL semantics/tests, Skia plan, and final status field. |
| SKIA-2 | Inventory all EasyGL-specific test registrations, golden images, XNA-oracle scenes, and backend-neutral tests; tag each as 2D-direct, 2D-emulation, 3D, or device-dependent. | ⬜ | Reusable Skia test matrix with no unclassified EasyGL graphics test. |
| SKIA-3 | Pin a reproducible Skia revision and license/NOTICE policy; decide vendored source, approved prebuilt package, or explicitly versioned external build without network activity during ordinary CNA configure. | ✅ | `docs/skia-backend.md` pins `ebf50520d720a1ce9d842d942d04c6c39c3fbc7b`, records the BSD-style license/notice requirement, and gives an external GN raster build recipe. |
| SKIA-4 | Run a minimal compile/link spike against the selected Skia build with CNA's compiler, standard library, warning policy, sanitizers, and debug/release configurations. | ⬜ | C++23 raster smoke compiled and linked the six static archives; the CNA `SKIA` static target also completed on Linux. Debug/sanitizer matrix remains required before completion. |
| SKIA-5 | Compare raster, Ganesh/OpenGL, and any relevant platform GPU path for CNA's supported targets; select one initial accelerated presentation strategy and one deterministic raster fallback. | ⬜ | ADR records ownership of context/device, supported OSes, and why alternatives were rejected. |
| SKIA-6 | Prove accelerated backbuffer wrapping: create an SDL window/context owned by `SKIA`, wrap the default framebuffer in an `SkSurface`, clear, flush/submit, swap, and read pixels. | ⬜ | Visible/manual smoke plus automated pixel test; no EasyGL link dependency. |
| SKIA-7 | Prove raster presentation: draw through an `SkSurface`, transfer to the selected SDL presentation path, and verify alpha/channel order, row order, and one-pixel updates. | ⬜ | Deterministic headless test and windowed smoke; no format or stride mismatch. |
| SKIA-8 | Prove resize, minimize/zero-size, fullscreen, virtual-resolution, and context/device-loss recovery for the chosen presentation strategy. | ⬜ | Repeated resize/loss test preserves validity or follows CNA reset contract with no leak/use-after-free. |
| SKIA-9 | Set documented support policy for GPU unavailable/driver failure: controlled raster fallback, initialization failure, and diagnostic message. | ⬜ | Tested decision; no implicit fallback that changes capabilities mid-frame. |

## Phase S1 — backend registration and lifecycle

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-10 | Add `SKIA` to `CNA_GRAPHICS_BACKEND` validation/selection, backend target construction, and source registration. | ⬜ | Configure succeeds only with the resolved dependency; all existing backend selections remain unchanged. |
| SKIA-11 | Add `CNA_BACKEND_SKIA`, `GraphicsBackendType::Skia`, current-type/current-name mapping, and compile-definition tests. | ⬜ | `GraphicsBackendTypeTests` passes in a `SKIA` build and names the backend exactly `SKIA`. |
| SKIA-12 | Implement `SkiaGraphicsBackend` construction/destruction, window registry lifecycle, error translation, and idempotent cleanup. | ⬜ | Construct/dispose loops pass ASan/LSan; null/failed initialization reports a useful exception. |
| SKIA-13 | Implement `Clear`, `Present`, viewport-size query, and all virtual-resolution/presentation-mode mappings through `SkiaSurface`. | ⬜ | Exact clear/readback and logical/physical coordinate round-trip tests. |
| SKIA-14 | Implement `TransformWindowToLogical` and `TransformLogicalToWindow`, including fixed-height dynamic-width/letterbox behavior. | ⬜ | Same input-coordinate tests as EasyGL, including resize after mode switch. |
| SKIA-15 | Implement swap interval/present interval policy for accelerated mode and document raster-mode behavior. | ⬜ | Requested interval is applied, clamped, or rejected explicitly; smoke test has no busy-loop regression. |
| SKIA-16 | Wire `DeviceResetting`/`DeviceReset` and resource recreation to resize/context-loss events. | ⬜ | Event-order test and rendered texture/target recovery test pass. |
| SKIA-17 | Add a startup diagnostic/capability report (Skia revision, surface mode, colour type, actual samples, optional anisotropy). | ⬜ | One concise, testable report; never logs private pointers or floods per frame. |
| SKIA-18 | Add backend-local ownership assertions for active surface, current thread/context, and backend destruction ordering. | ⬜ | Negative tests fail safely rather than corrupting state. |
| SKIA-19 | Add `cmake/Tests/SkiaTests.cmake` with separate raster, accelerated, and display-required registration helpers. | ✅ | `cmake/Tests/SkiaTests.cmake` provides separate helpers and registers five SKIA-only CTests. `Skia_Surface_Raster` runs without a display; the four windowed tests are explicitly labelled `Display` and use `CNA_TEST_DISPLAY`. |
| SKIA-20 | Verify clean configuration and a full non-Skia compile matrix after the new selection branch exists. | ⬜ | Each currently supported selected backend still configures and builds its CNA target. |

## Phase S2 — 2D surfaces, images, and Texture2D transfers

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-21 | Define `SkiaSurface` APIs for canvas access, flush/submit, snapshots, normalized RGBA8 readback, and target identity. | ⬜ | Unit tests cover lifetime and all pixel-origin conversions. |
| SKIA-22 | Implement `SkiaTextureBackend` for immutable/mutable `Texture2D` images and `UpdatePixels`/`UpdatePixelsLevel`. | ✅ | C++23 raster smoke created, drew, and fully updated a `SkiaTextureBackend`; each RGBA8 pixel exactly matched the post-draw `SkiaSurface` readback. Level 0 is implemented; mip uploads reject explicitly pending SKIA-27. |
| SKIA-23 | Preserve or deliberately synchronize the CPU pixel shadow required by public `Texture2D::GetData`; do not return invented transparent pixels. | ✅ | `Skia_Texture2D_GetDataContract` passes 40 checks: full/partial reads, CPU updates, two live textures, target reads, and malformed/disposed requests all preserve the declared contract. |
| SKIA-24 | Implement exact `SetData` transfer-range/offset/rectangle rules and `SetDataOptions` decision for 2D textures. | ✅ | `Skia_Texture2D_GetDataTransferRange` passes 70 checks for full/rectangle reads, destination offsets, excess/undersized capacity, overflow, and the explicit mip-construction rejection policy. |
| SKIA-25 | Establish the texture `SurfaceFormat` matrix against CNA's current public validation; map supported format/alpha/color-space storage without widening API claims accidentally. | ✅ | `Skia_Texture2D_Constraints` round-trips `Color` and verifies every other current `SurfaceFormat` is rejected by the shared validation before a Skia allocation. |
| SKIA-26 | Implement NPOT images, one-pixel textures, and large-but-valid dimensions with device-limit validation. | ✅ | `Skia_Texture2D_Constraints` covers 1×1, 3×5, a valid 4096×1 axis, zero dimensions, and an exact above-limit rejection; `Skia_Texture2D_NpotSampling` pixel-tests both 3×5 and 7×11 images. |
| SKIA-27 | Implement or explicitly reject 2D mip-chain allocation, upload, sampling, and generation after a Skia API/device probe. | ⬜ | Mip-level round-trip and visual minification test, or documented `NotSupportedException` before any data is accepted. |
| SKIA-28 | Implement texture recreation after device/context loss from CPU shadows, including all stored mip levels. | ⬜ | Context-loss test shows correct post-recovery pixels and no stale Skia handle. |
| SKIA-29 | Implement Texture2D disposal/double-disposal/bound-resource safeguards. | ⬜ | Existing disposed-resource, move-semantics, and leak tests pass under ASan/LSan. |
| SKIA-30 | Add direct tests for RGBA premultiplication, unpremultiplication, colour-space conversion, and row stride at the CNA–Skia boundary. | ⬜ | Hand-authored semi-transparent pixels match exact expected channels. |

## Phase S3 — SpriteBatch and SpriteFont baseline

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-31 | Implement `SkiaSpriteBatchBackend` session lifecycle and reject invalid Begin/End state transitions consistently with shared SpriteBatch semantics. | ✅ | `Skia_SpriteBatch_BeginEnd` verifies End/Draw-before-Begin, a duplicate Begin, successful draw, and re-entrant sessions against the real Skia backend. |
| SKIA-32 | Implement point-position sprite drawing and destination/source-rectangle drawing with XNA's inclusive/exclusive rectangle convention. | ✅ | `Skia_SpriteBatch_SourceRect` and `Skia_SpriteBatch_Overloads` pixel-test native-size point draws, all public destination/source rectangle overloads, cropped corners, and outside-sprite pixels. |
| SKIA-33 | Implement tint multiplication with exact XNA alpha convention; do not let Skia's native premultiplication alter RGB edges. | ✅ | `Skia_SpriteBatch_TintAlpha` verifies semi-transparent tint against both a premultiplied `AlphaBlend` source and a straight-alpha `NonPremultiplied` source. |
| SKIA-34 | Implement rotation, origin, negative/positive scale, and both `SpriteEffects` flips using canvas save/restore and transforms. | ✅ | `Skia_SpriteBatch_Rotation`, `Skia_SpriteBatch_Scale`, `Skia_SpriteBatch_NegativeScale`, and `Skia_SpriteBatch_Effects` verify origin pivoting, positive/non-uniform and negative X/Y scale, plus horizontal and vertical flips. |
| SKIA-35 | Apply the `SpriteBatch::Begin` transform matrix in the same order as EasyGL/XNA, including non-identity affine transforms. | ⬜ | Matrix/order test and golden diagnostic scene pass. |
| SKIA-36 | Preserve `SpriteSortMode` and `layerDepth` ordering semantics in shared batching/backend submission. | ✅ | `Skia_SpriteBatch_DeferredOrder`, `Skia_SpriteBatch_ImmediateFlush`, `Skia_SpriteBatch_LayerDepth`, and `Skia_SpriteBatch_TextureSort` verify call order, immediate dispatch, both depth orders, and texture grouping against Skia pixels. |
| SKIA-37 | Implement clipping of destination/source rectangles and ensure no sampling outside a requested source rectangle under linear filtering. | ⬜ | Bleed/seam tests pass at each source edge. |
| SKIA-38 | Verify the existing SpriteFont atlas path through Skia without replacing XNA glyph metrics/layout with Skia text APIs. | ⬜ | Single glyph, spacing, newline, default-character, rotation, and flip tests pass. |
| SKIA-39 | Audit every public SpriteBatch `Draw`/`DrawString` overload against the common layer and run it under `SKIA`. | ⬜ | No overload is a backend-local no-op; all relevant existing tests run. |
| SKIA-40 | Add a stress test for many Begin/Draw/End blocks, many textures, and repeated target changes. | ⬜ | Stable pixel hash and bounded resource/cache growth over repeated frames. |

## Phase S4 — 2D state mapping

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-41 | Implement viewport and top-left coordinate normalization for backbuffer and every off-screen `SkSurface`. | ⬜ | Identical source/target orientation and `Project`/`Unproject` 2D checks. |
| SKIA-42 | Implement `SetScissorRect` with save/restore discipline so a changed scissor cannot leak between draws or targets. | ⬜ | Nested scissor/target/sprite pixel tests pass. |
| SKIA-43 | Map `SamplerState` point/linear filters to Skia sampling options and prove magnification/minification behavior. | ✅ | `Skia_TextureFilter_PointVsLinear` proves the texel-boundary difference under magnification; `Skia_TextureFilter_Minification` proves the same Point/Linear distinction when 2 source texels cover one destination pixel. |
| SKIA-44 | Map `TextureAddressMode::Clamp`, `Wrap`, and `Mirror` independently on U/V, including negative coordinates and partial source rectangles. | ⬜ | Clamp/wrap/mirror edge and seam pixel suite passes. |
| SKIA-45 | Investigate mip filter modes and LOD selection on the selected Skia surface; implement only paths whose mip availability is established by SKIA-27. | ⬜ | Mip-filter matrix test either passes or rejects unsupported mode before draw. |
| SKIA-46 | Establish the exact treatment of `SamplerState` changes within and between Begin blocks, including state cache invalidation. | ⬜ | Per-draw-switch test proves the next draw sees the requested sampler. |
| SKIA-47 | Map `BlendState::Opaque` and prove it is an overwrite of the affected sprite pixels, not a whole-target clear. | ✅ | `Skia_BlendState_Opaque` overwrites an affected low-alpha sprite pixel over a coloured background. |
| SKIA-48 | Map `BlendState::AlphaBlend` with correct premultiplied source algebra. | ✅ | `Skia_BlendState_AlphaBlend` verifies a genuinely premultiplied source; `Skia_SpriteBatch_TintAlpha` verifies it remains correct after a semi-transparent tint. |
| SKIA-49 | Map `BlendState::NonPremultiplied` without treating it as AlphaBlend. | ✅ | `Skia_BlendState_NonPremultiplied` verifies a straight-alpha source; `Skia_SpriteBatch_TintAlpha` proves its tint result remains distinct from SKIA-48. |
| SKIA-50 | Map `BlendState::Additive` and establish saturation/colour-space policy compatible with CNA's existing oracle. | ✅ | `Skia_BlendState_Additive` verifies a source-alpha-scaled non-saturating sum and an over-range sum that clamps to 255. |
| SKIA-51 | Implement a table-driven conversion of XNA blend factors/functions to a direct Skia mode wherever one exists. | ⬜ | Mapping unit test covers all enum values and gives actionable error text for unmapped combinations. |
| SKIA-52 | Implement `SetBlendEnabled`/the default blend-state lifecycle and ensure Clear is always an unconditional clear. | ⬜ | Clear-after-blend and state-reset tests pass. |
| SKIA-53 | Investigate independent colour/alpha factors and equations using Skia's current blender APIs. | ⬜ | Written proof identifies exact direct mappings and unmappable cases. |
| SKIA-54 | Prototype isolated-layer compositing for a non-direct blend state; measure correctness, destination-read behaviour, and target interaction. | ⬜ | One non-preset blend test either matches reference pixels or records a precise blocker. |
| SKIA-55 | If SKIA-54 is exact, implement bounded emulation for all proven non-preset blend combinations; otherwise reject each unsupported combination before drawing. | ⬜ | Full blend matrix either passes or deterministically throws; no fallback silently uses SourceOver. |
| SKIA-56 | Investigate `ColorWriteChannels` and `MultiSampleMask`; prototype a destination-preserving channel-mask composition only if it survives alpha/blend tests. | ⬜ | Per-channel before/after byte tests and documented sample-mask result. |
| SKIA-57 | Implement a native or emulated colour-write-mask path only for combinations proven by SKIA-56. | ⬜ | RGBA-mask matrix passes on backbuffer and RenderTarget2D. |
| SKIA-58 | Audit RasterizerState fields that can affect the 2D implementation and ensure ignored 3D-only fields cannot falsely advertise support. | ⬜ | State object construction succeeds; unsupported draw paths fail consistently. |
| SKIA-59 | Audit all state changes across surface switches, Clear, Present, and exceptions. | ⬜ | State-leak regression suite passes. |
| SKIA-60 | Add a backend-facing state trace enabled only by a diagnostic flag. | ⬜ | Trace identifies surface, sampler, blend, and scissor transitions without changing rendering. |

## Phase S5 — RenderTarget2D, readback, and presentation robustness

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-61 | Implement `SkiaRenderTargetBackend` with a distinct `SkSurface`, texture/snapshot view, and safe ownership relation to `SkiaGraphicsBackend`. | ⬜ | Bind/draw/unbind/sample test proves no alias/lifetime bug. |
| SKIA-62 | Implement `SetRenderTarget2D(nullptr)` restoration and target-switch finalization/flush behavior. | ⬜ | Target A → B → backbuffer ordering test has expected independent pixels. |
| SKIA-63 | Implement `RenderTargetUsage::PreserveContents` and `DiscardContents` according to CNA's pass boundary contract. | ⬜ | Rebind/partial-redraw test proves both paths. |
| SKIA-64 | Implement RenderTarget2D `GetData` for full and partial rectangles, normalizing row order once and only once. | ⬜ | Rendered-pixel and partial-region readback tests pass; invalid request leaves caller data untouched. |
| SKIA-65 | Implement RenderTarget2D `SetData`, mip behavior, and device recreation, or reject each unsupported level precisely. | ⬜ | Round-trip tests cover CPU upload, rendered content, and loss recovery. |
| SKIA-66 | Establish and implement RenderTarget2D depth-format behavior for the initial 2D scope; never claim a real depth buffer merely because one was requested. | ⬜ | `HasRealDepthBuffer`, ClearOptions, and construction tests agree. |
| SKIA-67 | Implement/verify target multi-sample-count selection, resolve timing, readback, and `MultiSampleCount` reporting on accelerated surfaces. | ⬜ | Sample-count clamp plus resolve/readback test passes, or target creation fails/reports zero by documented policy. |
| SKIA-68 | Implement `GetBackBufferData` with active-target and post-unbind semantics matching `GraphicsDevice`. | ⬜ | Backbuffer/target readback tests verify both destinations and exact row order. |
| SKIA-69 | Validate target disposal while bound, backend destruction before target destruction, and leaking snapshot/image avoidance. | ⬜ | ASan/LSan lifetime suite and negative tests pass. |
| SKIA-70 | Investigate native 2D target mipmap generation and quality; implement generation only after it survives readback and sampling verification. | ⬜ | Mip target test passes or construct/draw/read clearly refuses unavailable mips. |
| SKIA-71 | Add resize/fullscreen/presentation-parameter regression tests while a RenderTarget2D and SpriteBatch exist. | ⬜ | Device reset events and post-resize rendering remain correct. |
| SKIA-72 | Test high-DPI/backbuffer scale interaction so logical coordinates and pixel readback are never conflated. | ⬜ | Display-scale diagnostic test documents and verifies conversion. |
| SKIA-73 | Test presentation after zero draws, Clear only, and a failed draw call. | ⬜ | No stale frame, invalid flush, or state leakage. |
| SKIA-74 | Add bounded cache/resource budgets for images, snapshots, and off-screen surfaces; expose counters in debug builds. | ⬜ | Stress test demonstrates release/reuse and no unbounded cache growth. |
| SKIA-75 | Cross-check RenderTarget2D goldens with EasyGL and the existing SDL_Renderer 2D semantics where applicable. | ⬜ | Checked-in expected images and documented tolerances. |

## Phase S6 — device-dependent 2D features

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-76 | Probe actual accelerated-surface MSAA support and select the requested-count clamp algorithm. | ⬜ | Runtime probe result, test at 0/1/2/4/oversized requests, and capability documentation. |
| SKIA-77 | Implement/use MSAA only when the probe succeeds; make raster-mode behavior explicit. | ⬜ | `GraphicsCapability::MultiSampleAntiAliasing` and `RenderTarget2D.MultiSampleCount` match actual results. |
| SKIA-78 | Probe Skia anisotropic sampling support for the selected build/API and requested `MaxAnisotropy`. | ⬜ | State/readback or native-query test distinguishes supported, clamped, and unavailable drivers. |
| SKIA-79 | Implement anisotropic filtering only where SKIA-78 proved it; otherwise use CNA's documented fallback or reject according to public contract. | ⬜ | `GraphicsCapability::AnisotropicFiltering` and sampler tests match the recorded device behaviour. |

## Phase S7 — non-2D texture/resource emulation investigations

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-80 | Audit `TextureCube` API use independently from shader sampling; determine whether six bounded CPU-backed faces can provide all transfer/readback behaviour safely. | ⬜ | Written storage-versus-sampling matrix and memory-limit policy. |
| SKIA-81 | If viable, implement cube texture CPU storage with all faces, mip levels, rectangles, validation, disposal, and `GetData`/`SetData` fidelity. | ⬜ | Existing cube partial-rectangle/mip/DDS tests pass without fabricated content. |
| SKIA-82 | Audit `Texture3D` API use independently from shader sampling; prototype bounded CPU voxel storage. | ⬜ | Memory accounting and per-slice transfer spike meets contract or documents why it cannot. |
| SKIA-83 | If viable, implement Texture3D storage/mips/sub-volume `SetData`/`GetData` and recreation. | ⬜ | Existing volume slice/mip/partial-transfer tests pass. |
| SKIA-84 | Decide capability reporting for storage-only cube/volume resources and explicitly reject sampling through unsupported effects. | ⬜ | Public calls either work as documented storage or throw; no false shader support claim. |
| SKIA-85 | Prototype a six-surface `RenderTargetCube` for 2D draw-to-face, readback, and transfer operations. | ⬜ | Face selection and pixel isolation test; report exact depth/MSAA/mip support. |
| SKIA-86 | Implement RenderTargetCube only if SKIA-85 meets all lifecycle and transfer contracts; otherwise reject construction/binding consistently. | ⬜ | Face round-trip, preserve/discard, disposal, and readback tests pass. |
| SKIA-87 | Prototype MRT emulation by command recording/replay or proven duplicated 2D submissions; test blending, state, target sizes, errors, and ordering. | ⬜ | A two-target test matches its reference pixels without double-applying side effects. |
| SKIA-88 | Implement MRT only if SKIA-87 is exact for the public draws claimed; otherwise `SetRenderTargets(count > 1)` throws before drawing. | ⬜ | Four-target validation/negative tests and capability result are explicit. |

## Phase S8 — effects and programmable 2D extensions

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-89 | Audit CNA's stock `SpriteEffect`, custom `ShaderEffect`, uniform setters, texture bindings, and EasyGL GLSL expectations. | ⬜ | Parameter/type/source-language compatibility table. |
| SKIA-90 | Prototype a built-in SpriteBatch Skia paint/shader path that preserves default sprite output before accepting any custom effect. | ⬜ | Default SpriteBatch goldens are unchanged when the effect plumbing exists. |
| SKIA-91 | Investigate a restricted GLSL-to-SkSL path or an explicit SkSL-only extension, including diagnostics, uniform layout, texture child shaders, and security/resource limits. | ⬜ | At least one real CNA effect compiles/renders or the incompatibility is demonstrated with a clear error. |
| SKIA-92 | Implement only the custom-effect subset proven in SKIA-91, with exact compile errors for unsupported shader stages/features. | ⬜ | Uniform/texture/custom-sprite tests pass; arbitrary GLSL is never silently accepted then ignored. |
| SKIA-93 | Evaluate a multipass emulation of currently supported 2D-like effects (alpha test, dual texture, colour transforms) using Skia shader/blender operations. | ⬜ | Per-effect visual oracle and documented performance/precision boundary. |
| SKIA-94 | Promote a stock effect only when its XNA properties and output meet the oracle; retain clear unsupported status otherwise. | ⬜ | Property-combination and golden test registration per promoted effect. |

## Phase S9 — 3D, depth/stencil, and query boundary

This phase intentionally prevents “2D-only” from becoming an imprecise catch-all. Skia's 2D
geometry and SkSL fragment-stage hooks may assist a narrowly defined emulation, but they do not
directly supply XNA vertex processing, depth/stencil buffers, cubemap/volume sampling, instancing,
or general GLSL vertex+fragment programs. No task below may advertise `ThreeD` until the complete
listed contract, including depth/stencil and stock-effect tests, passes.

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-95 | Build a complete EasyGL 3D call/effect matrix: vertex layouts, primitive types, index sizes, transformations, textures, states, lights, fog, skins, instancing, and queries. | ⬜ | Every existing EasyGL 3D test maps to a required renderer feature or a documented rejection. |
| SKIA-96 | Prototype projected `SkVertices` rendering for one `VertexPositionColorTexture` triangle list; compare interpolation, clipping, winding, and transform semantics to EasyGL. | ⬜ | Controlled test demonstrates exactness or records the first observable mismatch. |
| SKIA-97 | Prototype a CPU depth buffer plus triangle rasterization only if SKIA-96 shows a feasible bounded bridge; define its compositing handoff to Skia. | ⬜ | Depth ordering, clear, and target-switch spike with memory/performance measurements. |
| SKIA-98 | Prototype stencil compare/operations/masks and colour-write interaction only if the depth bridge passes. | ⬜ | Full stencil micro-suite matches EasyGL reference or records a hard blocker. |
| SKIA-99 | Prototype vertex/index buffer uploads, all declared layouts, primitive expansion, culling, wireframe, and DrawUser calls on top of the chosen bridge. | ⬜ | Layout/primitive/range-validation suite passes without treating unsupported data as bytes to ignore. |
| SKIA-100 | Prototype one textured `BasicEffect` route, then separately validate lighting/fog/alpha-test/dual-texture/environment/skinning/PBR requirements. | ⬜ | One test matrix per effect family identifies exact reusable components and gaps. |
| SKIA-101 | Decide whether a complete, maintainable 3D emulation exists. Do not treat a single coloured-triangle spike as support for `GraphicsCapability::ThreeD`. | ⬜ | ADR either defines a fully funded 3D phase with acceptance suite or confirms 2D-only rejection. |
| SKIA-102 | If SKIA-101 rejects full emulation, implement uniform failures for every 3D entry point and set `ThreeD`, depth/stencil, wireframe, and unsupported effect capabilities false. | ⬜ | Exhaustive negative test covers buffers, draws, clears, effects, models, cube binding, and queries. |
| SKIA-103 | If SKIA-101 accepts full emulation, create a successor plan before implementation, with individual tasks for each matrix row rather than bundling “3D support” into one task. | ⬜ | Approved successor plan and no premature capability flip. |
| SKIA-104 | Investigate whether an occlusion query can be implemented with the accepted 3D bridge while preserving Begin/End/nonblocking/PixelCount semantics. | ⬜ | Correct visible/occluded/availability spike, or a documented reason it is unsound. |
| SKIA-105 | Implement occlusion query only after SKIA-104; otherwise throw at Begin/End and keep capability false. | ⬜ | Query test or deterministic unsupported-path test. |

## Phase S10 — verification, documentation, and release gate

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-106 | Register the reusable 2D EasyGL examples under `SKIA`: textured quad, SpriteBatch rotation/source rect/layer depth, sprite fonts, sampler modes, blend presets, scissor, clear, render target, resize, disposal, and readback. | ⬜ | CTest registrations compile and run in their correct raster/GPU environment. |
| SKIA-107 | Add Skia-specific tests for surface ownership, context loss, CPU/GPU mode parity, alpha conversion, state leakage, and diagnostic capability reporting. | ⬜ | Each test fails under a targeted sabotage of its implementation. |
| SKIA-108 | Create/update a 2D XNA-oracle corpus comparison and a tolerance policy that distinguishes expected antialiasing variance from real semantic differences. | ⬜ | CI-friendly image comparison report with checked-in references and per-test tolerance rationale. |
| SKIA-109 | Run API contract tests (exceptions, disposal, transfer ranges, validation) under `SKIA`, then compare results with EasyGL where backend-independent. | ⬜ | No unexplained contract discrepancy. |
| SKIA-110 | Run raster and accelerated suites under ASan/UBSan/LSan where available, including repeated target/context recreation. | ⬜ | No new sanitizer report or documented external-tool false positive. |
| SKIA-111 | Update `docs/skia-backend.md`, `docs/graphics-backend-feature-matrix.md`, backend selection docs, and the capability ledger with only verified facts. | ⬜ | Every advertised feature links to task/test evidence; every exclusion states whether direct support or emulation was evaluated. |
| SKIA-112 | Add developer build instructions for the pinned Skia artifact, accelerated prerequisites, raster fallback, test environment, and common diagnostics. | ⬜ | Fresh checkout build succeeds by following the instructions verbatim. |
| SKIA-113 | Perform a clean-worktree, fresh-configure smoke across `SKIA`, EasyGL, SDL_Renderer, Software, and all platform-gated backends available in CI. | ⬜ | No backend-selection regression and all failures are pre-existing/documented. |
| SKIA-114 | Hold a release gate: verify every supported capability is demonstrated, every unimplemented EasyGL feature has an explicit direct/emulation decision, and no plan row is incorrectly marked complete. | ⬜ | Signed-off checklist plus final status summary in the documentation. |

## Suggested implementation order

1. SKIA-1 through SKIA-9 — settle dependency and surface ownership before adding a selectable
   backend.
2. SKIA-10 through SKIA-30 — make the backend buildable and establish correct Texture2D pixels.
3. SKIA-31 through SKIA-75 — complete and verify the usable 2D path, including state and targets.
4. SKIA-76 through SKIA-94 — activate device-dependent features and only the effect/resource
   emulations that pass their spikes.
5. SKIA-95 through SKIA-105 — make an explicit, evidence-backed 3D decision; do not intermingle
   this work with a stable 2D release.
6. SKIA-106 through SKIA-114 — regression, documentation, and release gate.

## Explicit initial exclusions (until a later task proves otherwise)

At the time this plan is written, `SKIA` must **not** advertise or silently approximate the
following as an initial 2D delivery: generic EasyGL GLSL vertex/fragment `ShaderEffect`, full XNA
3D primitive rendering, depth/stencil state, occlusion queries, instancing, wireframe, arbitrary
cube/volume texture *sampling*, or MRT. These are not permanent declarations that Skia can never
participate in; SKIA-80–SKIA-105 exist specifically to test safe emulations. They remain unavailable
unless and until that evidence is present.

Conversely, the initial delivery is not allowed to omit Texture2D/SpriteBatch/SpriteFont,
transforms, source rectangles, flip/rotation/origin, sampler addressing/filtering, standard blend
states, scissor, RenderTarget2D, readback, presentation lifecycle, and all associated contracts on
the grounds that it is “only 2D”: those are directly in the scope of this backend and must meet the
EasyGL/CNA parity bar above.
