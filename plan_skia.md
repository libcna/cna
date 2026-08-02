# Skia Graphics Backend — Implementation Plan

> **Status: COMPLETE — verified CPU-raster 2D backend.** The `SKIA` CMake selection,
> externally-built Skia dependency adapter, CPU-raster presentation path, bounded 2D feature set,
> cross-backend matrix, and release audit are complete. Accelerated Skia was explicitly deferred
> by the surface-mode ADR and may not be inferred from the SDL presenter.
>
> **Expansion status: ACTIVE — SKIA-115 through SKIA-170.** The completed SKIA-1–114 release
> remains the regression baseline while a successor phase pursues additional EasyGL-compatible
> raster features, bounded sampling/effect emulations, and an opt-in Ganesh/OpenGL mode. New
> features are not advertised until their individual rows and the successor release gate pass.
>
> **Goal:** add `CNA_GRAPHICS_BACKEND=SKIA` to CNA. The first usable delivery is a complete,
> pixel-verified **2D** backend. Its target is every part of `EASYGL` that Skia can express
> directly. A missing one-to-one Skia API is *not* by itself a reason to omit an XNA feature: an
> emulation spike must first determine whether the public CNA behaviour can be reproduced.
> Features that neither Skia nor a tested emulation can provide are reported honestly through
> `SupportsCapability()` and deterministic `NotSupportedException`/`runtime_error` paths; they
> must never become silent no-ops.

## Baseline release snapshot and active expansion

- **Baseline:** SKIA-1 through SKIA-114 are complete and remain protected by the signed checklist
  in [`docs/skia-release-gate.md`](docs/skia-release-gate.md). SKIA-115 through SKIA-170 form the
  active successor scope requested after that release.
- **Execution mode:** the accepted release is deterministic CPU raster. The pinned artifact has no
  Ganesh/Graphite/GL/Vulkan/Dawn path, and the selected build registers zero `Accelerated` tests.
  Future Ganesh/OpenGL work requires a successor plan and must reopen
  [`docs/skia-surface-mode-adr.md`](docs/skia-surface-mode-adr.md).
- **Capabilities:** all nine enum values are audited against the implementation. Only
  `GraphicsCapability::Texture3D` is true, strictly for bounded CPU transfer/readback storage;
  3D rendering and volume sampling remain unsupported.
- **Validation:** the complete Debug suite passes 133/133 on real display `:0` in 61.14 seconds
  with at most eight workers (16 Raster, 113 Display, four Audit). The final MSAA and anisotropy
  policy pair passes 2/2 in Release and under ASan+UBSan; the post-gate MSAA diagnostic assertion
  additionally passes in Debug, Release, and ASan+UBSan.
- **Continuity:** the authoritative current boundaries are the release gate, the 248-entry API
  parity ledger, the 347-entry EasyGL test matrix, and the 37/37 3D decision audit. Historical
  per-task test counts below intentionally record the checkpoint at which each row closed. The
  new rows must update those inventories as their public surface changes.

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
5. **Execution mode is explicit.** The selected release mode is a deterministic raster `SkSurface`
   for tests and normal presentation. An accelerated successor is allowed only after its
   integration spike proves ownership, flushing, resize, context loss, and CPU/GPU pixel parity;
   it must reuse the CNA resource and SpriteBatch contracts without silently replacing this mode.
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
| Window surface, clear, present, viewport, resize | Direct through `SkSurface`/`SkCanvas`; GPU surfaces require an application-owned current API context | Implement raster; record the conditional future accelerated gate | SKIA-1–SKIA-20 |
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
| Vertex/index buffers, primitive draws, stock 3D effects, depth/stencil, wireframe, instancing | No direct SkCanvas/XNA 3D pipeline | SKIA-101 rejects the full emulator; retain prototypes as evidence and make public refusal uniform | SKIA-95–SKIA-103 |
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
| SKIA-1 | Inventory every `IGraphicsBackend`, resource interface, `GraphicsCapability`, and public `GraphicsDevice` call against the EasyGL implementation; produce a row-per-entry parity ledger. | ✅ | `docs/skia-easygl-parity-ledger.md` classifies all 248 current entries (130 backend/resource methods, nine capabilities, 109 public device declarations) by EasyGL behavior/test surface, Skia result/plan, final status, and evidence/follow-up. `Skia_ParityLedger_Audit` lexically extracts the live headers and rejects missing, stale, duplicate, malformed, or unclassified rows. |
| SKIA-2 | Inventory all EasyGL-specific test registrations, golden images, XNA-oracle scenes, and backend-neutral tests; tag each as 2D-direct, 2D-emulation, 3D, or device-dependent. | ✅ | `docs/skia-easygl-test-matrix.md` classifies all 347 live inputs: 289 EasyGL CTests, two manual comparison tools, 17 golden PNGs, and 39 XNA-oracle scenes (69 2D-direct, 33 2D-emulation, 220 3D, 25 device-dependent). `Skia_TestMatrix_Audit` rejects unclassified/stale registrations and assets. |
| SKIA-3 | Pin a reproducible Skia revision and license/NOTICE policy; decide vendored source, approved prebuilt package, or explicitly versioned external build without network activity during ordinary CNA configure. | ✅ | `docs/skia-backend.md` pins `ebf50520d720a1ce9d842d942d04c6c39c3fbc7b`, records the BSD-style license/notice requirement, and gives an external GN raster build recipe. |
| SKIA-4 | Run a minimal compile/link spike against the selected Skia build with CNA's compiler, standard library, warning policy, sanitizers, and debug/release configurations. | ✅ | The persistent GNU 14/C++23 Debug, AddressSanitizer, and Release configurations all compile `cna_backend_graphics_skia` and link the six pinned static archives into CNA test executables. Release `Skia_Surface_Raster` passes all ten surface/stride/alpha checks; the same raster tests pass Debug and ASan/LSan. The only compiler output from pinned headers is the recorded GCC-ignored `clang::reinitializes` attribute warning. |
| SKIA-5 | Compare raster, Ganesh/OpenGL, and relevant platform GPU paths; select the release surface mode and the first candidate if acceleration is later reopened. | ✅ | `docs/skia-surface-mode-adr.md` selects the deterministic raster surface for this release, records backend/window/context ownership and the validated GNU/Clang ELF boundary, compares Ganesh GL/Vulkan and Graphite platform routes against pinned headers, and names Ganesh/OpenGL only as the first future candidate. There is no fabricated GPU fallback. |
| SKIA-6 | If SKIA-5 selects acceleration for this release, prove SDL/default-framebuffer wrapping, flush/submit, swap, readback, resize, and context loss; otherwise record the reopening gate without claiming GPU support. | ✅ | Conditionally not applicable: SKIA-5 selects raster-only. The surface ADR defines all six successor requirements; the pinned artifact disables Ganesh/Graphite/GL/Vulkan/Dawn, `ctest -L Accelerated -N` is empty, startup reports `surface=raster`, and no EasyGL link exists. A future implementation must reopen this row rather than inherit its status. |
| SKIA-7 | Prove raster presentation: draw through an `SkSurface`, transfer to the selected SDL presentation path, and verify alpha/channel order, row order, and one-pixel updates. | ✅ | Display-free `Skia_Surface_Raster` and `Skia_Texture_AlphaBoundary` prove tightly packed/strided RGBA8, top-row-first orientation, premultiplied conversion, exact rows, and one-pixel writes. Windowed `Skia_PresentationModes`, the shared target golden, and `Skia_Demo2D_Smoke` prove the same raster reaches the SDL streaming presenter without channel/row mismatch in all selected presentation mappings. |
| SKIA-8 | Prove resize, minimize/zero-size, fullscreen, virtual-resolution, and context/device-loss recovery for the chosen presentation strategy. | ✅ | `Skia_WindowLifecycle` preserves the last valid surface and exact pixel through deterministic 0×0 output and real hide/show, recalculates and renders through eight physical resizes, then preserves pixels through four SDL presenter rebuilds (12/12 normal/ASan, including non-mutating invalid override rejection). `Skia_Resize_Presentation`, `Skia_PresentationModes`, and `Skia_ContextRecovery` independently cover public fullscreen/reset storage, all virtual mappings, live Texture2D/target/snapshot resources, and ordered reset events. |
| SKIA-9 | Set documented support policy for GPU unavailable/driver failure: controlled raster fallback, initialization failure, and diagnostic message. | ✅ | `SKIA` currently selects raster explicitly and never attempts or falls back from a GPU surface. Missing/mismatched Skia archives fail configuration; SDL presenter failures unwind construction and rethrow a stage-specific error; success emits the immutable raster capability line. `Skia_Lifecycle`, the missing-dependency configure probe, and startup diagnostic test cover all three outcomes. A future accelerated mode must be a construction-time observable choice and may not switch mid-frame. |

## Phase S1 — backend registration and lifecycle

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-10 | Add `SKIA` to `CNA_GRAPHICS_BACKEND` validation/selection, backend target construction, and source registration. | ✅ | The persistent Skia build configures and links `cna_backend_graphics_skia` against SDL3 and the resolved `CNA::Skia` archive group. A separate configuration with intentionally missing source/build paths fails at configure time with the actionable `CNA_SKIA_ROOT` diagnostic; no fallback/backend substitution occurs. |
| SKIA-11 | Add `CNA_BACKEND_SKIA`, `GraphicsBackendType::Skia`, current-type/current-name mapping, and compile-definition tests. | ✅ | The common exactly-one-backend test now counts `CNA_BACKEND_SKIA` (the audit found it had been omitted) and adds an explicit macro-to-`GraphicsBackendType::Skia`/`SKIA` assertion. All six `GraphicsBackendTypeTest` and both Skia compile-definition cases pass in the selected build. |
| SKIA-12 | Implement `SkiaGraphicsBackend` construction/destruction, window registry lifecycle, error translation, and idempotent cleanup. | ✅ | Construction is transactional: failures after SDL renderer creation, backbuffer/streaming-texture creation, or window registration unwind every acquired object and registry entry before rethrowing a stage-specific diagnostic. `Skia_Lifecycle` verifies null rejection, all three rollback points with immediate successful retry, and 16 construct/present/destroy cycles; all 11 checks pass normally and under ASan. LSan observes only the already-recorded 2,864-byte external X11 exit residual. |
| SKIA-13 | Implement `Clear`, `Present`, viewport-size query, and all virtual-resolution/presentation-mode mappings through `SkiaSurface`. | ✅ | `Skia_PresentationModes` verifies exact raster Clear/readback/Present and the requested 40×30 surface through Letterbox, Overscan, Stretch, and NativeBackBuffer. FixedHeightDynamicWidth derives its CPU-surface width from the measured renderer-output aspect and reallocates after a real window resize; leaving that mode restores the preferred width instead of retaining the derived one. |
| SKIA-14 | Implement `TransformWindowToLogical` and `TransformLogicalToWindow`, including fixed-height dynamic-width/letterbox behavior. | ✅ | `Skia_PresentationModes` measures each mode's scale/centred offset in SDL window coordinates, checks bidirectional round trips before and after real resize, and rejects an invalid mode without mutating the raster. `Skia_DisplayScale` independently verifies a known letterbox offset and the DPI-aware SDL coordinate route. Both tests pass normally and under ASan. |
| SKIA-15 | Implement swap interval/present interval policy for accelerated mode and document raster-mode behavior. | ✅ | The selected raster presenter maps Immediate→0 and One/Default→1 through SDL; Two requests 2 and deterministically records SDL's supported result, falling back to 1 only when the driver rejects 2. `Skia_PresentInterval` verifies all four public requests, stored and actual values, persistence across presenter recovery, and exact Clear/readback/Present output (15 checks normal/ASan). A future accelerated Skia mode must independently probe its native swap contract. |
| SKIA-16 | Wire `DeviceResetting`/`DeviceReset` and resource recreation to resize/context-loss events. | ✅ | `Skia_ContextRecovery` rebuilds the actual SDL presenter/streaming texture through both debug recovery entries and observes one ordered `DeviceResetting`/`DeviceReset` pair per rebuild. The selected CPU-raster Skia resources never become context-invalid, so recovery truthfully emits no `DeviceLost`; it preserves and then renders the live Texture2D, RenderTarget2D, and target snapshot. |
| SKIA-17 | Add a startup diagnostic/capability report (Skia revision, surface mode, colour type, actual samples, optional anisotropy). | ✅ | A successfully constructed backend emits one stable line containing pinned revision `ebf50520…`, `surface=raster`, `RGBA_8888/premultiplied`, `samples=0`, and unsupported anisotropy. `Skia_StartupDiagnostic_Raster` verifies all fields, single-line form, stable storage, and absence of pointer-shaped values. |
| SKIA-18 | Add backend-local ownership assertions for active surface, current thread/context, and backend destruction ordering. | ✅ | A shared owner-thread token protects every raster/SDL entry and a checked binding rejects null/aliased active surfaces without mutation. `Skia_Ownership` proves foreign-thread backend and SpriteBatch calls fail before state changes, owner-thread reuse succeeds, the window owns the expected SDL renderer, and a late SpriteBatch fails safely after backend destruction. `Skia_RenderTargetBinding_Raster` proves invalid bindings preserve the backbuffer and both backend-before-target/target-before-backend orders are safe. Raster has no fabricated GPU-current-context claim. |
| SKIA-19 | Add `cmake/Tests/SkiaTests.cmake` with separate raster, accelerated, and display-required registration helpers. | ✅ | `cmake/Tests/SkiaTests.cmake` provides distinct Raster, Display, and future Accelerated/Display helpers and now registers 78 SKIA-only CTests. Seven raster tests run without a display; 71 windowed tests are explicitly labelled `Display` and use `CNA_TEST_DISPLAY`. |
| SKIA-20 | Verify clean configuration and a full non-Skia compile matrix after the new selection branch exists. | ✅ | Fresh GNU 14/Debug configurations and complete two-job `CNA` target builds pass for all ten native Linux selections: HEADLESS, SOFTWARE, SDL_RENDERER, ASCII, EASYGL, DX3, VULKAN, SDL_GPU, BGFX, and WEBGPU. Fresh D3D9/D3D11/D3D12/CANVAS probes stop at their intentional Windows/Emscripten gates. `docs/skia-nonskia-build-matrix.md` records commands, dependency revisions, archive hash, and the precise platform boundary. |

## Phase S2 — 2D surfaces, images, and Texture2D transfers

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-21 | Define `SkiaSurface` APIs for canvas access, flush/submit, snapshots, normalized RGBA8 readback, and target identity. | ✅ | `SkiaSurface` now has a non-zero process-local identity that survives storage replacement; copying/moving is forbidden because target bindings retain its address. The binding records and validates both backbuffer and active-target identities. `Skia_Surface_Raster` covers unallocated lifetime, canvas/snapshot behavior, distinct/stable identity, top-left RGBA8 orientation, premultiplication conversion, bounds, and resize (14/14 Debug, Release, and ASan/LSan); `Skia_RenderTargetBinding_Raster` covers both destruction orders. |
| SKIA-22 | Implement `SkiaTextureBackend` for immutable/mutable `Texture2D` images and `UpdatePixels`/`UpdatePixelsLevel`. | ✅ | C++23 raster smoke created, drew, and fully updated a `SkiaTextureBackend`; each RGBA8 pixel exactly matched the post-draw `SkiaSurface` readback. Level 0 is implemented; mip uploads reject explicitly pending SKIA-27. |
| SKIA-23 | Preserve or deliberately synchronize the CPU pixel shadow required by public `Texture2D::GetData`; do not return invented transparent pixels. | ✅ | `Skia_Texture2D_GetDataContract` passes 40 checks: full/partial reads, CPU updates, two live textures, target reads, and malformed/disposed requests all preserve the declared contract. |
| SKIA-24 | Implement exact `SetData` transfer-range/offset/rectangle rules and `SetDataOptions` decision for 2D textures. | ✅ | `Skia_Texture2D_GetDataTransferRange` passes 70 checks for full/rectangle reads, destination offsets, excess/undersized capacity, overflow, and the explicit mip-construction rejection policy. |
| SKIA-25 | Establish the texture `SurfaceFormat` matrix against CNA's current public validation; map supported format/alpha/color-space storage without widening API claims accidentally. | ✅ | `Skia_Texture2D_Constraints` round-trips `Color` and verifies every other current `SurfaceFormat` is rejected by the shared validation before a Skia allocation. |
| SKIA-26 | Implement NPOT images, one-pixel textures, and large-but-valid dimensions with device-limit validation. | ✅ | `Skia_Texture2D_Constraints` covers 1×1, 3×5, a valid 4096×1 axis, zero dimensions, and an exact above-limit rejection; `Skia_Texture2D_NpotSampling` pixel-tests both 3×5 and 7×11 images. |
| SKIA-27 | Implement or explicitly reject 2D mip-chain allocation, upload, sampling, and generation after a Skia API/device probe. | ✅ | Skia's mip builder is a private `src/core` facility, while the public raster image construction used here accepts only level 0. CNA therefore rejects `mipMap=true` for both `Texture2D` and `RenderTarget2D` with `System::NotSupportedException`; `Skia_Texture2D_MipmapPolicy` proves both rejections and a subsequent level-0 upload/draw. |
| SKIA-28 | Implement texture recreation after device/context loss from CPU shadows, including all stored mip levels. | ✅ | Raster `SkImage` values are CPU-owned and not tied to the rebuilt SDL presentation renderer, so they remain valid rather than receiving fabricated GPU-handle recreation. `Skia_ContextRecovery` proves a live level-0 Texture2D still draws exact pixels after two presenter recreations. Mip levels remain precisely rejected by SKIA-27; no unimplemented level is claimed recoverable. |
| SKIA-29 | Implement Texture2D disposal/double-disposal/bound-resource safeguards. | ✅ | `Skia_Texture2D_Dispose`, `Skia_DisposedGuards`, and `Skia_DoubleDispose` run the shared copy/move ownership, disposed-consumption, and idempotent-release contracts against Skia. The headless lifetime tests pass with AddressSanitizer/LeakSanitizer and leak detection enabled. On this GCC/Xvfb display stack, the existing `Skia_Presentation_Edge` and the new SKIA-69 window test both finish with the same unsymbolized 2,864-byte process-exit residual after the existing SDL/font/runtime suppressions; it is recorded rather than broadly suppressed, so CNA and Skia allocations remain checked. |
| SKIA-30 | Add direct tests for RGBA premultiplication, unpremultiplication, colour-space conversion, and row stride at the CNA–Skia boundary. | ✅ | Headless `Skia_Texture_AlphaBoundary` checks exact semi-transparent compositing for both declared source-alpha conventions and verifies an RGBA8 strided upload ignores row padding in public readback. |

## Phase S3 — SpriteBatch and SpriteFont baseline

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-31 | Implement `SkiaSpriteBatchBackend` session lifecycle and reject invalid Begin/End state transitions consistently with shared SpriteBatch semantics. | ✅ | `Skia_SpriteBatch_BeginEnd` verifies End/Draw-before-Begin, a duplicate Begin, successful draw, and re-entrant sessions against the real Skia backend. |
| SKIA-32 | Implement point-position sprite drawing and destination/source-rectangle drawing with XNA's inclusive/exclusive rectangle convention. | ✅ | `Skia_SpriteBatch_SourceRect` and `Skia_SpriteBatch_Overloads` pixel-test native-size point draws, all public destination/source rectangle overloads, cropped corners, and outside-sprite pixels. |
| SKIA-33 | Implement tint multiplication with exact XNA alpha convention; do not let Skia's native premultiplication alter RGB edges. | ✅ | `Skia_SpriteBatch_TintAlpha` verifies semi-transparent tint against both a premultiplied `AlphaBlend` source and a straight-alpha `NonPremultiplied` source. |
| SKIA-34 | Implement rotation, origin, negative/positive scale, and both `SpriteEffects` flips using canvas save/restore and transforms. | ✅ | `Skia_SpriteBatch_Rotation`, `Skia_SpriteBatch_Scale`, `Skia_SpriteBatch_NegativeScale`, and `Skia_SpriteBatch_Effects` verify origin pivoting, positive/non-uniform and negative X/Y scale, plus horizontal and vertical flips. |
| SKIA-35 | Apply the `SpriteBatch::Begin` transform matrix in the same order as EasyGL/XNA, including non-identity affine transforms. | ✅ | `Skia_SpriteBatch_TransformMatrix` applies scale then translation and verifies both transformed and original coordinates; flips under an affine translation are also covered by `Skia_SpriteBatch_Effects`. |
| SKIA-36 | Preserve `SpriteSortMode` and `layerDepth` ordering semantics in shared batching/backend submission. | ✅ | `Skia_SpriteBatch_DeferredOrder`, `Skia_SpriteBatch_ImmediateFlush`, `Skia_SpriteBatch_LayerDepth`, and `Skia_SpriteBatch_TextureSort` verify call order, immediate dispatch, both depth orders, and texture grouping against Skia pixels. |
| SKIA-37 | Implement clipping of destination/source rectangles and ensure no sampling outside a requested source rectangle under linear filtering. | ✅ | `Skia_SpriteBatch_SourceRectLinear` uses a one-texel red source rectangle surrounded on every side by distinct colours and verifies its four magnified edges, corners, and exterior destination pixels under `LinearClamp`. |
| SKIA-38 | Verify the existing SpriteFont atlas path through Skia without replacing XNA glyph metrics/layout with Skia text APIs. | ✅ | `Skia_SpriteFont_SingleGlyph`, `MultiGlyphSpacing`, `Newline`, `DefaultChar`, and `Effects` verify the existing atlas glyph metrics, fallback, spacing, line advance, scale/origin, and flip path through Skia SpriteBatch. |
| SKIA-39 | Audit every public SpriteBatch `Draw`/`DrawString` overload against the common layer and run it under `SKIA`. | ✅ | `Skia_SpriteBatch_Overloads` covers the first nine texture overloads; `Skia_SpriteBatch_RemainingOverloads` covers the tenth plus all six `DrawString` variants (string/StringBuilder × basic/scalar/vector scale). The audit confirms all funnel through shared `pushSprite`/glyph layout submission rather than a backend-local overload. |
| SKIA-40 | Add a stress test for many Begin/Draw/End blocks, many textures, and repeated target changes. | ✅ | `Skia_SpriteBatch_Stress` creates a fixed twelve-texture/two-target fixture, runs 64 actual frames with 1,664 Begin/Draw/End blocks and 192 target changes, and requires a stable complete-backbuffer FNV-1a hash plus target-anchor pixels on every frame. |

## Phase S4 — 2D state mapping

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-41 | Implement viewport and top-left coordinate normalization for backbuffer and every off-screen `SkSurface`. | ✅ | `Skia_SpriteBatch_Viewport` verifies a non-zero, asymmetric viewport's local coordinates, all four boundaries, and identical backbuffer/target top-left orientation; `Skia_Viewport_ProjectUnproject` verifies the live viewport's 2D projection and round trips. |
| SKIA-42 | Implement `SetScissorRect` with save/restore discipline so a changed scissor cannot leak between draws or targets. | ✅ | `Skia_SpriteBatch_RasterizerState` verifies scissor enable/disable across SpriteBatch modes, while `Skia_RenderTarget2D_Scissor` verifies an asymmetric target-local clip, its boundaries, target switching, and the disabled-scissor control. |
| SKIA-43 | Map `SamplerState` point/linear filters to Skia sampling options and prove magnification/minification behavior. | ✅ | `Skia_TextureFilter_PointVsLinear` proves the texel-boundary difference under magnification; `Skia_TextureFilter_Minification` proves the same Point/Linear distinction when 2 source texels cover one destination pixel. |
| SKIA-44 | Map `TextureAddressMode::Clamp`, `Wrap`, and `Mirror` independently on U/V, including negative coordinates and partial source rectangles. | ✅ | `Skia_TextureAddressAxes` proves positive and negative U overflow, V mirror, and both mixed U/V pairs with Point sampling and distinct 2×2 grid colours; `Skia_SpriteBatch_SourceRectLinear` remains green for the strict in-bounds Clamp crop. |
| SKIA-45 | Investigate mip filter modes and LOD selection on the selected Skia surface; implement only paths whose mip availability is established by SKIA-27. | ✅ | SKIA-27 established no raster mip availability, so `Skia_Sampler_MipmapFilterPolicy` requires all six mip-dependent `TextureFilter` values to raise `System::NotSupportedException` during `SpriteBatch::Begin`, before a draw. It also proves the batch is reusable afterward. |
| SKIA-46 | Establish the exact treatment of `SamplerState` changes within and between Begin blocks, including state cache invalidation. | ✅ | `Skia_SpriteBatch_SamplerTransition` performs Point → Linear → Point across three Begin blocks in one frame and verifies each draw receives its requested filter. |
| SKIA-47 | Map `BlendState::Opaque` and prove it is an overwrite of the affected sprite pixels, not a whole-target clear. | ✅ | `Skia_BlendState_Opaque` overwrites an affected low-alpha sprite pixel over a coloured background. |
| SKIA-48 | Map `BlendState::AlphaBlend` with correct premultiplied source algebra. | ✅ | `Skia_BlendState_AlphaBlend` verifies a genuinely premultiplied source; `Skia_SpriteBatch_TintAlpha` verifies it remains correct after a semi-transparent tint. |
| SKIA-49 | Map `BlendState::NonPremultiplied` without treating it as AlphaBlend. | ✅ | `Skia_BlendState_NonPremultiplied` verifies a straight-alpha source; `Skia_SpriteBatch_TintAlpha` proves its tint result remains distinct from SKIA-48, and SKIA-108's sort oracle/public alpha regression verify XNA's independent output-alpha equation. |
| SKIA-50 | Map `BlendState::Additive` and establish saturation/colour-space policy compatible with CNA's existing oracle. | ✅ | `Skia_BlendState_Additive` verifies a source-alpha-scaled non-saturating sum and an over-range sum that clamps to 255; SKIA-108's public alpha regression verifies the independent Additive output-alpha equation. |
| SKIA-51 | Implement a table-driven conversion of XNA blend factors/functions to a direct Skia mode wherever one exists. | ✅ | `Skia_BlendMapping_Raster` covers every current `Blend` ordinal in every factor position, every `BlendFunction` pair, all four presets, and actionable factor/function names. `Opaque`/`AlphaBlend` use direct modes; SKIA-108 corrected `NonPremultiplied`/`Additive` to bounded runtime routes because Skia's superficially similar modes have different alpha equations. `Skia_BlendMapping_Policy` proves unsupported public `Begin` calls name the offending value and leave `SpriteBatch` reusable. The table deliberately does not guess a source-alpha convention for arbitrary custom tuples; SKIA-53–SKIA-55 own the native/emulation investigation. |
| SKIA-52 | Implement `SetBlendEnabled`/the default blend-state lifecycle and ensure Clear is always an unconditional clear. | ✅ | `Skia_BlendEnabled_State` disables blending for an `AlphaBlend` draw, clears while disabled, re-enables it, and proves source replacement, unconditional clear, and restoration of the stored alpha blend mapping. |
| SKIA-53 | Investigate independent colour/alpha factors and equations using Skia's current blender APIs. | ✅ | Pinned Skia exposes `SkRuntimeEffect::MakeForBlender`/`SkPaint::setBlender`: its `main(half4 src, half4 dst)` can express all thirteen XNA factors (including a uniform blend factor and source-alpha-saturation) and all five component-wise equations with independent RGB/alpha branches. `Skia_RuntimeBlender_Raster` compiles and executes one independent equation on a raster surface and proves Skia preserves a non-premultiplied result, required by XNA's independent-alpha states. Fixed `SkBlendMode` and `SkBlenders::Arithmetic` remain insufficient for general separate RGB/alpha equations. The runtime-effect API is experimental and source-alpha labelling still belongs to CNA, so SKIA-54 must prove the public SpriteBatch/target route before it is exposed. |
| SKIA-54 | Prototype a direct `SkRuntimeEffect` blender for a non-preset public BlendState; measure correctness, destination-read behaviour, and RenderTarget2D interaction. Use an isolated layer only if this direct route fails a public contract. | ✅ | `Skia_RuntimeBlender_Policy` proves `ColorSource=DestinationColor`, `ColorDestination=Zero`, `AlphaSource=One`, `AlphaDestination=Zero` reaches a custom `SkPaint` blender: it produces `(100,0,0,255)` from opaque source/destination on the backbuffer, public RenderTarget2D `GetData`, and a subsequent target sample. The backend accepts only this evidence-bounded tuple in addition to SKIA-51's four presets; SKIA-55 owns the full generator and matrix. |
| SKIA-55 | If SKIA-54 is exact, implement bounded emulation for all proven non-preset blend combinations; otherwise reject each unsupported combination before drawing. | ✅ | A single table now drives all accepted tuples: the four source-alpha-labelled presets and SKIA-54's pixel-proven `DestinationColor` runtime blender. `Skia_BlendMapping_Raster` exhaustively tests all 714,025 factor/function tuples: those five pass selection, every other tuple fails deterministically, and `Skia_BlendMapping_Policy` proves rejection remains public and leaves SpriteBatch reusable. No path silently falls back to SourceOver. Wider custom states remain unproven because their `BlendState` does not label straight versus premultiplied source bytes. |
| SKIA-56 | Investigate `ColorWriteChannels` and `MultiSampleMask`; prototype a destination-preserving channel-mask composition only if it survives alpha/blend tests. | ✅ | `Skia_ColorWriteMask_Raster` compiles a post-blend runtime blender and verifies all 16 RGBA masks retain each disabled premultiplied destination byte after a source-over calculation, including zero-mask preservation. The raster surface has no samples (requests 0/1 apply 0; real MSAA is already rejected), so a non-default `MultiSampleMask` has no exact meaning and remains a deterministic public rejection. `Skia_BlendMapping_Policy` covers both public rejections and batch recovery. |
| SKIA-57 | Implement a native or emulated colour-write-mask path only for combinations proven by SKIA-56. | ✅ | `Skia_ColorWrite_Policy` passes all sixteen masks after each of the five accepted blend routes on the backbuffer, all sixteen on the destination-reading runtime route through RenderTarget2D public readback, a separate alpha-bit matrix, and SKIA-108 regressions for the independent NonPremultiplied/Additive alpha equations. The backend uses one bounded post-blend runtime effect for non-`All` target-0 masks; ColorWriteChannels1-3 and non-default `MultiSampleMask` still reject because this raster backend has one zero-sample target. |
| SKIA-58 | Audit RasterizerState fields that can affect the 2D implementation and ensure ignored 3D-only fields cannot falsely advertise support. | ✅ | `Skia_RasterizerState_Policy` shows that only scissor has a SpriteBatch effect; cull/depth-bias/MSAA state remains constructible but does not change a solid sprite, while `FillMode::WireFrame` is unadvertised and rejected with an actionable error. The test also proves the rejected Begin leaves a subsequent solid draw usable; `Skia_GraphicsCapability` covers 3D buffer construction failure. |
| SKIA-59 | Audit all state changes across surface switches, Clear, Present, and exceptions. | ✅ | `Skia_StateTransition` exercises target-local scissor followed by unbind reset, unconditional Clear without state loss, rejected WireFrame Begin recovery, and custom viewport preservation over same-size Present; all pixel assertions pass. |
| SKIA-60 | Add a backend-facing state trace enabled only by a diagnostic flag. | ✅ | `CNA_SKIA_STATE_TRACE=1` emits readable surface, sampler, blend, and scissor transitions from the Skia backend; the same `Skia_StateTransition` pixel suite passes with and without it, and default runs emit no trace. |

## Phase S5 — RenderTarget2D, readback, and presentation robustness

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-61 | Implement `SkiaRenderTargetBackend` with a distinct `SkSurface`, texture/snapshot view, and safe ownership relation to `SkiaGraphicsBackend`. | ✅ | `Skia_RenderTarget2D_SampleAfterUnbind` renders a non-uniform target, unbinds it, and samples its distinct snapshot as a SpriteBatch texture. |
| SKIA-62 | Implement `SetRenderTarget2D(nullptr)` restoration and target-switch finalization/flush behavior. | ✅ | `Skia_RenderTarget2D_Switch` renders target A then B, restores the backbuffer, and samples distinct expected pixels from both targets. |
| SKIA-63 | Implement `RenderTargetUsage::PreserveContents` and `DiscardContents` according to CNA's pass boundary contract. | ✅ | `Skia_RenderTarget2D_Usage` verifies a rebind discards the prior contents only for `DiscardContents`, preserving the same contents otherwise. |
| SKIA-64 | Implement RenderTarget2D `GetData` for full and partial rectangles, normalizing row order once and only once. | ✅ | `Skia_RenderTarget2D_Readback` verifies rendered full-level top-row-first data and a partial rectangle; target readback transfer contracts also run in `Skia_Texture2D_GetDataContract`. |
| SKIA-65 | Implement RenderTarget2D `SetData`, mip behavior, and device recreation, or reject each unsupported level precisely. | ✅ | `Skia_RenderTarget2D_SetData` covers full/partial level-0 upload, live-surface readback, and preserved untouched pixels; higher levels and mipmapped targets reject precisely. `Skia_ContextRecovery` then rebuilds the SDL presenter twice while a target and immutable sampling snapshot stay live, proving exact target readback and later sampling without a stale raster handle. |
| SKIA-66 | Establish and implement RenderTarget2D depth-format behavior for the initial 2D scope; never claim a real depth buffer merely because one was requested. | ✅ | `Skia_RenderTarget2D_DepthPolicy` constructs requested `Depth24Stencil8` metadata, verifies both `HasRealDepthBuffer` queries are false and the device capability is false, then proves a depth/stencil-only clear preserves the target's colour. |
| SKIA-67 | Implement/verify target multi-sample-count selection, resolve timing, readback, and `MultiSampleCount` reporting on accelerated surfaces. | ✅ | Raster policy is explicit: `Skia_RenderTarget2D_MsaaPolicy` reports 0 for requested 0/1, rejects normalized real requests 2/3/4 before creation, and confirms `MultiSampleAntiAliasing` is false. Accelerated surfaces remain a separate unimplemented mode. |
| SKIA-68 | Implement `GetBackBufferData` with active-target and post-unbind semantics matching `GraphicsDevice`. | ✅ | `Skia_GetBackBufferData_ActiveTarget` proves active target readback agrees with `RenderTarget2D::GetData` and reverts to the independent default backbuffer after unbind; `Skia_GetBackBufferData_AfterRtUnbind` covers full-buffer dimensions and row order after a smaller target cycle. |
| SKIA-69 | Validate target disposal while bound, backend destruction before target destruction, and leaking snapshot/image avoidance. | ✅ | `Skia_RenderTargetBinding_Raster` validates a bound target's detach, 128 snapshot lifetimes, and target destruction after its backend-owned binding has expired. `Skia_RenderTarget2D_Lifetime` proves through public APIs that a destroyed bound target is followed by safe Clear/SpriteBatch backbuffer output and a fresh target cycle. The direct raster lifetime test passes clean under ASan/LSan; the windowed test's identical 2,864-byte unsymbolized Xvfb exit residual is also present in the pre-existing `Skia_Presentation_Edge` ASan run and is documented without widening suppressions. |
| SKIA-70 | Investigate native 2D target mipmap generation and quality; implement generation only after it survives readback and sampling verification. | ✅ | The pinned Skia revision publicly offers immutable `SkImage::withDefaultMipmaps()`, but does not expose per-level raster readback or mutable `SkSurface` target invalidation/resolve semantics. `Skia_Texture2D_MipmapPolicy` therefore verifies precise `NotSupportedException` refusal for both mipmapped constructors, followed by a fresh level-0 target bind/Clear/readback/unbind/sample cycle. |
| SKIA-71 | Add resize/fullscreen/presentation-parameter regression tests while a RenderTarget2D and SpriteBatch exist. | ✅ | `Skia_Resize_Presentation` performs a resize with an active preserve target and an existing SpriteBatch, proves ordered `DeviceResetting`/`DeviceReset` events and old/new presentation values, then proves target readback/sampling and backbuffer drawing. It also verifies fullscreen/windowed stored parameters and presentation-mode transitions across further resets (without requiring Xvfb to honor physical fullscreen). |
| SKIA-72 | Test high-DPI/backbuffer scale interaction so logical coordinates and pixel readback are never conflated. | ✅ | `Skia_DisplayScale` puts a 40×30 Letterbox backbuffer in a 120×60 window, records the actual SDL output/window pixel ratio, verifies offset-aware SDL window↔logical conversion, and reads logical raster pixels independently. Skia now delegates both transforms to SDL renderer APIs, which are DPI-aware on displays where output pixels differ from window points. |
| SKIA-73 | Test presentation after zero draws, Clear only, and a failed draw call. | ✅ | `Skia_Presentation_Edge` confirms deterministic transparent-black zero-draw presentation, Clear-only presentation, and retained current colour after a disposed-texture draw failure; backbuffer creation now explicitly clears its new raster surface. |
| SKIA-74 | Add bounded cache/resource budgets for images, snapshots, and off-screen surfaces; expose counters in debug builds. | ✅ | Each live Texture2D has exactly two alpha-labelled images, each live RenderTarget2D owns one raster surface, and each target now caches at most one immutable sampling snapshot, invalidated before every target write. `SkiaGraphicsBackend::GetResourceStatsEXT()` exposes live counts/bytes; `Skia_ResourceBudget` verifies cache invalidation and 64 target create/sample/release cycles returning counters to zero. |
| SKIA-75 | Cross-check RenderTarget2D goldens with EasyGL and the existing SDL_Renderer 2D semantics where applicable. | ✅ | The shared `RenderTarget2D_Golden` source contains a checked-in top-row-first 4×4 RGBA oracle. Skia, EasyGL, and SDL_Renderer each match all 16 target-readback pixels and all 64 Point-sampled 2× backbuffer pixels exactly (zero tolerance) under Xvfb. |

## Phase S6 — device-dependent 2D features

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-76 | Probe MSAA for the selected release surface; if acceleration is selected, query native counts, otherwise prove the zero-sample raster clamp/refusal algorithm. | ✅ | `Skia_RenderTarget2D_MsaaPolicy` drives backbuffer requests 0/1/2/4/4096 through Reset (all truthfully write back 0), target requests 0/1/2/3/4/4096 (0/1→0, real counts reject before allocation), and post-probe readback. The surface ADR records why there is no accelerated probe to claim. |
| SKIA-77 | Implement/use MSAA only when SKIA-76 proves it; otherwise make the selected raster behavior explicit. | ✅ | Raster owns zero samples, reports `GraphicsCapability::MultiSampleAntiAliasing=false`, clamps backbuffer and cube requests to 0, rejects real `RenderTarget2D` requests, and exposes no fake resolve or sample mask. The capability table, startup diagnostic, public properties, and focused test agree. |
| SKIA-78 | Probe anisotropic sampling for the selected build/API and requested `MaxAnisotropy`; distinguish native support, clamp, fallback, and unavailability. | ✅ | The pinned raster API exposes no device anisotropy control. `Skia_Sampler_MipmapFilterPolicy` renders a non-uniform 2×2 source and proves byte-identical Linear fallback for Anisotropic with `MaxAnisotropy` 1/4/9999 while the capability remains false. Mip-dependent filters still reject. |
| SKIA-79 | Implement anisotropic filtering only where SKIA-78 proved it; otherwise use CNA's documented fallback or reject according to public contract. | ✅ | SpriteBatch level-zero Anisotropic uses the exact tested Linear fallback and ignores the unavailable quality request; `GraphicsCapability::AnisotropicFiltering=false`. Mip/LOD and all stock-3D sampler routes reject instead of claiming hardware anisotropy. `docs/skia-surface-mode-adr.md` records the boundary and future re-probe rule. |

## Phase S7 — non-2D texture/resource emulation investigations

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-80 | Audit `TextureCube` API use independently from shader sampling; determine whether six bounded CPU-backed faces can provide all transfer/readback behaviour safely. | ✅ | `docs/skia-texture-storage.md` separates exact storage from unsupported sampling and fixes positive/16384-axis plus 256 MiB checked-allocation limits. |
| SKIA-81 | If viable, implement cube texture CPU storage with all faces, mip levels, rectangles, validation, disposal, and `GetData`/`SetData` fidelity. | ✅ | Six-face, partial-rectangle, mip, DDS content-load, and exhaustive shared 56-check read/write fixtures pass without fabricated content. |
| SKIA-82 | Audit `Texture3D` API use independently from shader sampling; prototype bounded CPU voxel storage. | ✅ | Exact per-level byte accounting is debug-visible; backend copies rows/slices directly and the one public RGBA scratch is bounded by the same 256 MiB policy. |
| SKIA-83 | If viable, implement Texture3D storage/mips/sub-volume `SetData`/`GetData` and recreation. | ✅ | Slice, mip, partial-box/readback, atomic failure, disposal, and exhaustive shared transfer fixtures pass. CPU storage survives presenter recreation by construction. |
| SKIA-84 | Decide capability reporting for storage-only cube/volume resources and explicitly reject sampling through unsupported effects. | ✅ | Only `GraphicsCapability::Texture3D` (whose contract is persistent storage) is true; `ThreeD`/`CustomEffects` remain false, no sampling binding exists, and 3D calls still throw. |
| SKIA-85 | Prototype a six-surface `RenderTargetCube` for 2D draw-to-face, readback, and transfer operations. | ✅ | Direct raster policy proves six stable isolated faces, exact two-level surface-plus-RGBA-shadow accounting, dirty-face mip generation, and checked 16384-axis/256 MiB limits. Depth is metadata-only, `HasRealDepthBuffer` is false, requested MSAA applies zero, and cube sampling stays unavailable. |
| SKIA-86 | Implement RenderTargetCube only if SKIA-85 meets all lifecycle and transfer contracts; otherwise reject construction/binding consistently. | ✅ | Four shared public fixtures plus the exhaustive SetData audit pass: asymmetric rendered/uploaded face and mip readback, arbitrary translucent byte-exact uploads, Preserve/Discard, depth colour interaction, zero-sample requests, properties, disposal, singular/plural equivalence, multi-object switching, viewport/scissor reset, and deterministic MRT rejection. |
| SKIA-87 | Prototype MRT emulation by command recording/replay or proven duplicated 2D submissions; test blending, state, target sizes, errors, and ordering. | ✅ | Exact replay is impossible at the SkCanvas boundary: one raster draw has one result colour, while CNA `ShaderEffect` can emit distinct values to MRT locations 0–3 with independent write masks. Duplicating output 0 would be observably wrong, so the spike correctly retains refusal. |
| SKIA-88 | Implement MRT only if SKIA-87 is exact for the public draws claimed; otherwise `SetRenderTargets(count > 1)` throws before drawing. | ✅ | `Skia_MRT_Rejection` covers otherwise-valid 2/3/4-target 2D/cube sets, mismatch/duplicate/null/>4 error precedence, unchanged binding/viewport/scissor, no Discard clear or partial face write, a post-refusal AlphaBlend draw to the prior target only, and false capability reporting. |

## Phase S8 — effects and programmable 2D extensions

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-89 | Audit CNA's stock `SpriteEffect`, custom `ShaderEffect`, uniform setters, texture bindings, and EasyGL GLSL expectations. | ✅ | `docs/skia-effects.md` records the stage/language/coordinate and parameter/texture table: untagged GLSL/SPIR-V strings cannot be guessed as SkSL, runtime effects have no vertex stage, numeric samplers differ from named 2D children, and cube/volume sampling remains unavailable. `Skia_Effect_Boundary` proves the invalid custom route cannot silently draw or poison the next stock batch. |
| SKIA-90 | Prototype a built-in SpriteBatch Skia paint/shader path that preserves default sprite output before accepting any custom effect. | ✅ | Only exact `typeid(SpriteEffect)` aliases the existing paint path; derived/custom effects reject. `Skia_SpriteEffect_Alias` compares all target pixels for default, explicit stock, and cloned stock routes across transform/rotation/both flips/tint/PointClamp, proves the oracle drew, and verifies reuse after derived-type refusal. |
| SKIA-91 | Investigate a restricted GLSL-to-SkSL path or an explicit SkSL-only extension, including diagnostics, uniform layout, texture child shaders, and security/resource limits. | ✅ | Exact marker `CNA_SKIA_SKSL_V1` opts only the fragment string into SkSL 100. The 64 KiB source/16 KiB uniform bounds and exact `cnaTexture0` + `cnaTint` prototype ABI reject all other tagged shapes with retained diagnostics; untagged GLSL still returns null. `Skia_SkSL_Effect_Prototype` compiles and pixel-renders a channel-transform effect, covers compiler/ABI/limit failures, and proves stock reuse. |
| SKIA-92 | Implement only the custom-effect subset proven in SKIA-91, with exact compile errors for unsupported shader stages/features. | ✅ | `Skia_SkSL_UniformTexture` drives float/int/float2/3/4, a non-symmetric column-major float4x4 element, float/float2 arrays, reserved draw tint and an updated `cnaTexture1` through one exact pixel equation. Units 1–7 are weak lifetime-tracked 2D children; missing/type/count/null/unit/dispose/clone errors are deterministic, unsupported reflection/cnaTexture8/cube/volume reject, and untagged GLSL remains invalid. |
| SKIA-93 | Evaluate a multipass emulation of currently supported 2D-like effects (alpha test, dual texture, colour transforms) using Skia shader/blender operations. | ✅ | `Skia_Effect_Emulation_Spike` proves all 24 below/equal/above alpha decisions through a binary clip (and proves transparent colour is not discard), the dual-texture RGB×2 formula with independent Repeat/Mirror children, and a one-pass swizzle/scale/bias filter. Alpha costs two source evaluations plus a clip; dual/filter need no intermediate target. `docs/skia-effects.md` records CPU and premultiplied-RGBA8 precision boundaries without promoting stock 3D wrappers. |
| SKIA-94 | Promote a stock effect only when its XNA properties and output meet the oracle; retain clear unsupported status otherwise. | ✅ | No new stock type is promoted: `Skia_StockEffect_Boundary` forwards all eight alpha compare modes and 16 dual texture/fog/vertex-colour combinations, then proves every public draw rejects atomically at the shared `GraphicsDevice::DrawUserPrimitives` 3D guard, both SpriteBatch errors identify the stock-3D route, and 2D reuse succeeds. The 78 existing property/clone/forwarding tests pass. Existing goldens stay classified 3D; registering a Skia golden for an unpromoted fragment subset would be false evidence. |

## Phase S9 — 3D, depth/stencil, and query boundary

This phase intentionally prevents “2D-only” from becoming an imprecise catch-all. Skia's 2D
geometry and SkSL fragment-stage hooks may assist a narrowly defined emulation, but they do not
directly supply XNA vertex processing, depth/stencil buffers, cubemap/volume sampling, instancing,
or general GLSL vertex+fragment programs. No task below may advertise `ThreeD` until the complete
listed contract, including depth/stencil and stock-effect tests, passes.

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-95 | Build a complete EasyGL 3D call/effect matrix: vertex layouts, primitive types, index sizes, transformations, textures, states, lights, fog, skins, instancing, and queries. | ✅ | `docs/skia-3d-call-effect-matrix.md` defines 37 stable contract IDs with current results and decision tasks. After correcting three stale classifications against their sources, the extended `Skia_TestMatrix_Audit` maps all 213/213 current primary 3D entries plus 16 exact device-dependent depth/MSAA/anisotropy/query cross-cuts, rejects an unmapped live entry or unused/undocumented feature, and `--dump-3d` emits the full entry-to-feature expansion. |
| SKIA-96 | Prototype projected `SkVertices` rendering for one `VertexPositionColorTexture` triangle list; compare interpolation, clipping, winding, and transform semantics to EasyGL. | ✅ | Headless `Skia_ProjectedVertices_Spike` proves exact CPU WVP/viewport and equal-W colour interpolation, then records the first material gap: unequal clip W `(1,4,1)` yields texture byte 88 versus EasyGL's perspective-correct 30. It also proves that SkVertices draws an apex outside `z >= -w` and both windings. `docs/skia-skvertices-spike.md` limits any SKIA-97 follow-up to a CPU rasterizer that owns clipping/varyings/depth and hands Skia only the completed colour image. |
| SKIA-97 | Prototype a CPU depth buffer plus triangle rasterization only if SKIA-96 shows a feasible bounded bridge; define its compositing handoff to Skia. | ✅ | `Skia_CpuDepthRaster_Spike` owns bounded RGBA8+float depth at exactly eight bytes/pixel, proves order-independent LessEqual/write, depth-only clear, reciprocal-W byte 30, A→B→A isolation, exact same-size `WritePixels` handoff and atomic size rejection. At 640×360 it owns 1,843,200 B; Release measured 64 µs clear, 193,043 µs for 128 overlapping triangles and 616 µs handoff. `docs/skia-cpu-depth-spike.md` records Debug/Release/ASan measurements and remaining non-production boundaries. |
| SKIA-98 | Prototype stencil compare/operations/masks and colour-write interaction only if the depth bridge passes. | ✅ | Headless `Skia_CpuStencil_Spike` matches EasyGL's reference-vs-stored compare and 8-bit Depth24Stencil8 operation contract across 4,194,304 compare and 4,194,304 operation/mask cases. It proves fail/depth-fail/pass ordering, disabled bypass, narrow read/write masks, the exact two-sided `0x06`/one-sided `0x04` fixture, all 16 colour-write masks, and full clear behavior. `docs/skia-cpu-stencil-spike.md` records the nine-byte CPU target cost and keeps all public depth/stencil/3D capabilities false. |
| SKIA-99 | Prototype vertex/index buffer uploads, all declared layouts, primitive expansion, culling, wireframe, and DrawUser calls on top of the chosen bridge. | ✅ | Headless `Skia_CpuGeometry_Spike` retains all seven built-in declarations, all 12 formats and 13 usages, bounded upload/source-offset semantics, 16/32-bit indices (including 70000), all five primitive types, alternating strip winding, all cull modes, post-cull wire expansion, and raw/four typed/16- and 32-bit indexed DrawUser inputs. Invalid layouts/ranges/states reject atomically. `docs/skia-cpu-geometry-spike.md` records that exact line/point pixel rules, instancing, clipping/coverage, effects and public ownership remain SKIA-100/101 work; `ThreeD` stays false. |
| SKIA-100 | Prototype one textured `BasicEffect` route, then separately validate lighting/fog/alpha-test/dual-texture/environment/skinning/PBR requirements. | ✅ | Headless `Skia_CpuStockEffect_Spike` matches all four `EasyGL_BasicEffect_Combined` pixels, retains perspective UV/depth, hands completed RGBA8 to Skia, and rejects missing texture, lighting/fog or unclipped input atomically. `docs/skia-stock-effect-feasibility.md` separately classifies Basic, AlphaTest, DualTexture, EnvironmentMap, Skinned, PBR and SkinnedPBR requirements as reusable/prototype/gap. Exact coverage, lighting/fog, cube sampling, skinning, PBR, public ownership and mixed ordering remain SKIA-101 costs; `ThreeD` stays false. |
| SKIA-101 | Decide whether a complete, maintainable 3D emulation exists. Do not treat a single coloured-triangle spike as support for `GraphicsCapability::ThreeD`. | ✅ | Accepted `docs/skia-3d-emulation-adr.md` keeps the backend 2D-only. `Skia_3DDecision_Audit` requires an explicit disposition for all 37 live renderer IDs: 16 prototype-only, seven 2D-only, two transfer-only and 12 rejected. A complete CPU route would be a new software renderer; hidden Software delegation or EasyGL hybrid ownership would be broader still. SKIA-102 is required; all 3D/depth/stencil/wireframe capabilities remain false. |
| SKIA-102 | If SKIA-101 rejects full emulation, implement uniform failures for every 3D entry point and set `ThreeD`, depth/stencil, wireframe, and unsupported effect capabilities false. | ✅ | `Skia_3D_Refusal` covers buffers, public/backend draws, clears/state, seven effects, models, cube/volume binding and query Begin/End under one stable diagnostic. A sentinel target proves atomicity; 2D drawing and CPU transfers recover. |
| SKIA-103 | If SKIA-101 accepts full emulation, create a successor plan before implementation, with individual tasks for each matrix row rather than bundling “3D support” into one task. | ✅ | Obsolete/not applicable: SKIA-101 rejected full emulation. Any future reversal requires a replacement ADR and a new successor plan before implementation or capability changes. |
| SKIA-104 | Investigate whether an occlusion query can be implemented with the accepted 3D bridge while preserving Begin/End/nonblocking/PixelCount semantics. | ✅ | `Skia_OcclusionQuery_Feasibility` proves a fully covered same-colour or destination-preserving draw and a zero-coverage draw can have byte-identical framebuffers; one/two submissions also have the same final pixels. Raster SkCanvas exposes no per-draw coverage/depth query, and the disabled Graphite/Vulkan submission statistic cannot observe this CPU surface. `docs/skia-occlusion-query-feasibility.md` documents why framebuffer diff, mask replay and a hidden GPU context are unsound. |
| SKIA-105 | Implement occlusion query only after SKIA-104; otherwise throw at Begin/End and keep capability false. | ✅ | SKIA-104 rejects emulation. The SKIA-102 refusal object safely reports `IsComplete=false`/`PixelCount=0`, throws the stable 3D diagnostic at Begin/End, and leaves `GraphicsCapability::OcclusionQuery=false`; `Skia_3D_Refusal` verifies the full deterministic path and subsequent 2D recovery. |

## Phase S10 — verification, documentation, and release gate

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-106 | Register the reusable 2D EasyGL examples under `SKIA`: textured quad, SpriteBatch rotation/source rect/layer depth, sprite fonts, sampler modes, blend presets, scissor, clear, render target, resize, disposal, and readback. | ✅ | `docs/skia-2d-easygl-registration.md` maps every requested category to live CTests. Eleven exact EasyGL 2D sources now run under Skia; broader shared fixtures cover their edge matrices, while inherently 3D sampler/blend/clip implementations remain correctly excluded. The exact resize source found and now guards an asynchronous SDL shrink/readback defect. |
| SKIA-107 | Add Skia-specific tests for surface ownership, context loss, CPU/GPU mode parity, alpha conversion, state leakage, and diagnostic capability reporting. | ✅ | `docs/skia-verification-boundary.md` maps each boundary to direct sabotage-sensitive assertions. New `Skia_RasterMode_Coherence` ties the runtime report and exact capability set to straight-RGBA8 pixels across presenter recovery. CPU/GPU image parity is explicitly not claimed because only raster Skia exists; any accelerated mode must reopen the gate and run the listed corpus. |
| SKIA-108 | Create/update a 2D XNA-oracle corpus comparison and a tolerance policy that distinguishes expected antialiasing variance from real semantic differences. | ✅ | `Skia_XNA_2D_Oracle` renders all nine and only the shared corpus's SpriteBatch-only scenes against checked-in real-XNA PNGs. Seven are exact; flip/rotation allow only measured RGB ±1 bilinear rounding, exact alpha, at most 1,591 raw pixels, and their transformed sprite footprints. `docs/skia-xna-oracle.md` records the policy and the independent-alpha blend defect it found. |
| SKIA-109 | Run API contract tests (exceptions, disposal, transfer ranges, validation) under `SKIA`, then compare results with EasyGL where backend-independent. | ✅ | `docs/skia-api-contract-comparison.md` records 13 same-source Skia/EasyGL pairs: eight newly registered validation/transfer/readback/state/target tests plus five existing disposal and exhaustive transfer contracts. All pass on both backends. Six mixed lifecycle fixtures were corrected from 2D to 3D because mandatory buffer construction reaches SKIA-102; the only live-binding difference and Skia's MSAA exclusion are explicit, tested boundaries. |
| SKIA-110 | Run raster and accelerated suites under ASan/UBSan/LSan where available, including repeated target/context recreation. | ✅ | `docs/skia-sanitizer-validation.md` records the full 132/132 ASan+UBSan pass, clean 16/16 display-free LSan pass, and clean dummy/software LSan isolation. `Skia_ResourceBudget` pairs 64 target/snapshot lifetimes with 64 presenter reconstructions and exact pixel/counter/event checks. The only windowed LSan report is the identical non-growing 100,956-byte `libGLX_mesa.so.0` residual in both stress and one-presenter controls. `vptr` alone is disabled at the no-RTTI Skia boundary; no accelerated suite exists in the pinned raster-only build. |
| SKIA-111 | Update `docs/skia-backend.md`, `docs/graphics-backend-feature-matrix.md`, backend selection docs, and the capability ledger with only verified facts. | ✅ | `docs/skia-backend.md` and the feature matrix now provide evidence-linked CPU-raster capability tables covering every advertised 2D family and every unsupported GPU/3D family, including each direct/emulation decision. README, docs index, CLAUDE backend-selection guidance, `GraphicsCapability` comments, and the corrected 248-row ledger agree that only bounded CPU `Texture3D` transfer storage is reported beyond the 2D route. All three source audits and the complete 132/132 Debug Skia suite pass. |
| SKIA-112 | Add developer build instructions for the pinned Skia artifact, accelerated prerequisites, raster fallback, test environment, and common diagnostics. | ✅ | `docs/skia-developer-build.md` gives the exact revision/GN arguments/six-archive check, sibling/submodule prerequisites, global eight-worker limit (including configure-time SDL), offline CNA configure, display-free/Xvfb/real-display tests, working-directory-safe demo commands, sanitizer/state diagnostics, explicit non-fallback behavior, future GPU prerequisites, and failure remedies. A clean `git archive` source with fresh CMake/Ninja state completed the full build, 19/19 Audit+Raster tests, 132/132 Skia tests under Xvfb, zero Accelerated registrations, and the traced three-frame demo by following the corrected procedure. |
| SKIA-113 | Perform a clean-worktree, fresh-configure smoke across `SKIA`, EasyGL, SDL_Renderer, Software, and all platform-gated backends available in CI. | ✅ | From clean baseline `77c3a604`, fresh Debug `CNA` builds pass for Skia, EasyGL, SDL_Renderer, Software, Vulkan and BGFX. Fresh MinGW `RelWithDebInfo` builds pass for CNA, D3D11/D3D12, and every backend-owned executable; Wine runtime passes D3D11 41/41 under DXVK and D3D12 2/2 under vkd3d-proton. `docs/skia-nonskia-build-matrix.md` records exact commands, the unavailable Emscripten/native-MSVC boundaries, and the pre-existing `CNA_ENABLE_NET=OFF`/monolithic-`CnaTests` ENet registration failure. No selection fell back to another renderer. |
| SKIA-114 | Hold a release gate: verify every supported capability is demonstrated, every unimplemented EasyGL feature has an explicit direct/emulation decision, and no plan row is incorrectly marked complete. | ✅ | `docs/skia-release-gate.md` signs off all 114 rows, all nine live capability values, every direct/bounded/refused EasyGL family, the raster/acceleration and 3D ADRs, validation results, and known boundaries. `Skia_ReleaseGate_Audit` enforces the contiguous completed plan, exact capability table/implementation agreement, accepted raster decision, required release markers, and zero accelerated registrations. Final validation passes 133/133 Debug tests (16 Raster, 113 Display, four Audit), the Release and ASan+UBSan policy pair, all four source audits, the 248-entry parity ledger, the 347-entry test matrix, and the 37/37 3D decision audit. |

## Phase S11 — successor scope and regression guardrails

The following rows extend the completed CPU-raster release. They do not retroactively weaken its
capability claims. Each feature begins with the exact FNA/EasyGL contract, preserves the existing
resource limits and atomic-refusal rules, and must pass both focused and complete Skia regression
tests before it can change capability or compatibility documentation.

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-115 | Reopen the Skia plan as a successor expansion while retaining SKIA-1–114 as the immutable release baseline. | ✅ | The active SKIA-115–170 banner, baseline release snapshot, `docs/skia-release-gate.md`, and `NEXT_skia.md` distinguish completed baseline evidence from unadvertised successor work. Eight phases cover every requested feature without changing current capability claims. |
| SKIA-116 | Extend plan/release validation for contiguous successor rows and their independent completion status. | ✅ | `validate_skia_release_gate.py` still requires SKIA-1–114 exactly once and complete, rejects any gap/duplicate/unknown status through the highest successor ID, verifies the banner endpoint and completion state, and reports 2/56 successor rows after this checkpoint. Direct execution and registered `Skia_ReleaseGate_Audit` pass. |
| SKIA-117 | Inventory the exact FNA/CNA/EasyGL contracts and existing fixtures for mipmaps, every `SurfaceFormat`, arbitrary blend states, custom effects, MSAA, anisotropy, MRT, and cube/volume sampling. | ✅ | `docs/skia-successor-contract-matrix.md` routes 87 exact contracts: all 27 `SurfaceFormat`, 13 `Blend`, five `BlendFunction`, nine `TextureFilter`, nine `GraphicsCapability`, and 24 cross-feature rows. `Skia_SuccessorContracts_Audit` derives every enum row from live headers, rejects missing/stale/duplicate/malformed/unrouted entries, and proves complete SKIA-118–170 routing; all five display-free audits pass. |
| SKIA-118 | Define shared successor resource budgets, checked-size helpers, pixel/precision tolerance rules, and cross-feature oracle scenes. | ✅ | `SkiaResourcePolicy.hpp` centralizes the 16,384-axis/256-MiB and SkSL compiler/cache ceilings plus result-preserving checked add/multiply/2D/3D/accumulation helpers; existing cube/volume/target paths consume them and 2D readback no longer risks signed size overflow. `SkiaSuccessorOracle.hpp` fixes exact, localized ±1 byte, bit-exact float-transfer, and finite shader tolerances across eight multi-feature scenes. `Skia_SuccessorResource_Policy` and related storage tests pass in Debug, Release, and ASan+UBSan; the complete Debug suite passes 135/135. |

## Phase S12 — complete blend-state coverage on raster Skia

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-119 | Establish a deterministic straight/premultiplied source convention for every public blend-state route, including textures, targets, tint, and custom effects. | ✅ | `docs/skia-source-alpha-contract.md` and the code-level storage/working-route enums distinguish canonical Texture2D bytes, already-premultiplied targets, exact tint scaling, and the tagged effect's premultiplied output. `Skia_SourceAlpha_Policy` reads raw translucent working bytes, verifies all five accepted mappings declare a convention, and passes with the established boundary tests in Debug, Release and ASan+UBSan; six public preset/effect regressions pass under Xvfb. |
| SKIA-120 | Generate a bounded runtime `SkBlender` for all 13 blend factors and five blend functions with independent RGB/alpha equations. | ⬜ | Every selector compiles once or reports a deterministic construction error; constant colour, source-alpha saturation, min/max, reverse-subtract, and separate alpha match scalar reference math. |
| SKIA-121 | Implement `BlendFactor`, `BlendEnabled`, and all `ColorWriteChannels` interactions for generated blend states. | ⬜ | Blend constants update without stale state, disabled blending performs source replacement, all 16 target-0 masks preserve destination bytes, and unsupported multisample masks still reject. |
| SKIA-122 | Integrate generated blenders into Deferred, Immediate, texture-sort, and effect-backed SpriteBatch paths without state leakage. | ⬜ | Translucent texture/target/custom-effect inputs produce the same pixels in all batch modes and stock state is reusable after success or failure. |
| SKIA-123 | Add exhaustive selector tests and a discriminating public pixel corpus for arbitrary blend combinations. | ⬜ | All 714,025 tuples have a stable direct/generated/refusal result; a minimized pixel set exercises every factor/function branch against EasyGL/reference output in Debug, Release, and ASan+UBSan. |
| SKIA-124 | Promote exactly the proven arbitrary blend surface and synchronize the parity ledger, feature matrix, diagnostics, and release audit. | ⬜ | No proven tuple remains rejected, no unproven tuple silently draws, and complete Skia plus reusable EasyGL blend regressions pass. |

## Phase S13 — Texture2D and RenderTarget2D mip chains

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-125 | Design a checked CNA-owned 2D mip-chain abstraction with stable per-level storage, dimensions, offsets, and accounting. | ⬜ | Odd/NPOT dimensions halve to 1×1, all arithmetic is overflow-checked, allocation stays within documented limits, and construction failure leaves counters unchanged. |
| SKIA-126 | Enable mipmapped `Texture2D` construction and exact level-count/property reporting. | ⬜ | Zero-initialized levels, one-dimensional chains, maximum legal chains, invalid dimensions, and disposal match the public FNA/CNA contract. |
| SKIA-127 | Implement full/partial `Texture2D::SetData` and `GetData` for every mip level. | ⬜ | Shared transfer fixtures cover offsets/counts/rectangles, arbitrary translucent bytes, untouched texels, invalid levels/ranges, and caller-memory lifetime. |
| SKIA-128 | Implement deterministic mip generation and invalidation after level-0 upload while preserving explicitly written levels according to the public contract. | ⬜ | Exact box-filter or contract-selected generation handles odd edges and alpha once, rebuilds only dirty descendants, and produces stable checked-in bytes. |
| SKIA-129 | Implement `PointMipPoint`, `PointMipLinear`, `LinearMipPoint`, and `LinearMipLinear` sampling with correct LOD selection. | ⬜ | Minification matrices discriminate level choice and inter-level interpolation; Clamp/Wrap/Mirror, source rectangles, transforms, and magnification remain exact. |
| SKIA-130 | Load mipmapped 2D content and compressed-source assets into the mutable chain without losing source format or level boundaries. | ⬜ | XNB/DDS fixtures round-trip every declared level, reject malformed/truncated chains, and do not fabricate absent mips. |
| SKIA-131 | Implement mipmapped `RenderTarget2D` storage using stable per-level raster surfaces or an equivalently exact bounded representation. | ⬜ | Binding, Clear, draw, upload, readback, viewport/scissor reset, Preserve/Discard, and snapshots identify the correct level without aliasing. |
| SKIA-132 | Define and implement render-target mip invalidation/generation at pass boundaries and after `SetData`. | ⬜ | Unbind/sample/readback order is deterministic, descendant levels refresh exactly once when dirty, failed binds leave prior target and chain unchanged, and presenter recovery preserves the resource. |
| SKIA-133 | Complete mip lifecycle, resource-budget, performance, Release, and sanitizer validation and update capability documentation. | ⬜ | Focused Texture2D/RenderTarget2D mip suites and the complete Skia suite pass; counters return to baseline and no old level-0 behavior regresses. |

## Phase S14 — SurfaceFormat expansion

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-134 | Map every `SurfaceFormat` enum to exact CNA byte layout, Skia colour/raw representation, sampling semantics, renderability, and fallback/refusal route. | ⬜ | The matrix covers all current enum values exactly once and is checked against headers and EasyGL/FNA behavior. |
| SKIA-135 | Implement direct packed/unorm formats where Skia storage is layout-compatible: Bgr565, Bgra4444, Rgba1010102, and compatible RGBA/BGRA variants. | ⬜ | Full/partial transfers, clear/draw conversion, sampling, readback, endian/channel order, alpha quantization, and disposal pass format-specific oracles. |
| SKIA-136 | Implement `ColorBgraEXT`, `ColorSrgbEXT`, and explicit colour-space handling without accidental double conversion. | ⬜ | Raw transfer bytes remain exact while sampled/rendered colours match EasyGL tolerances in linear and sRGB destinations. |
| SKIA-137 | Implement Alpha8, ByteEXT, UShortEXT, Rg32, Rgba64, and compatible one/two/four-channel unorm data paths. | ⬜ | Raw shaders or explicit swizzles preserve missing-channel defaults, normalized sampling, Set/Get layout, and render-target restrictions. |
| SKIA-138 | Implement Single, Vector2, Vector4, HalfSingle, HalfVector2, HalfVector4, and HdrBlendable where Skia float storage and raster operations are exact. | ⬜ | IEEE/half bit patterns round-trip, extended range/NaN/Inf policy is explicit, and sampling/blending precision has bounded oracle tolerances. |
| SKIA-139 | Implement NormalizedByte2/4 and Bgra5551 through checked conversion/shadow storage when no layout-compatible `SkColorType` exists. | ⬜ | Signed normalization, packing, channel order, sampling, partial transfer, and exact original-byte readback are independently proven. |
| SKIA-140 | Implement Dxt1/Dxt3/Dxt5 sampled textures with preserved compressed CPU blocks and deterministic decompression. | ⬜ | Block alignment/range validation, alpha modes, NPOT edge policy, mip chains, exact compressed `GetData`, decoded sampling, malformed data, and memory accounting pass. |
| SKIA-141 | Evaluate and implement BC7/Bc7SrgbEXT sampled textures only with a bounded, license-compatible decoder. | ⬜ | Decoder choice/license/build impact is documented; conformance assets, sRGB handling, block transfers, invalid input, and deterministic fallback/refusal are tested. |
| SKIA-142 | Establish per-format RenderTarget2D support and refuse non-renderable/compressed formats before allocation without weakening texture support. | ⬜ | Every format has a demonstrated render/readback route or an exact actionable refusal; format failure does not alter active target or counters. |
| SKIA-143 | Run exhaustive format contracts, content loading, cross-backend comparisons, resource limits, Release/sanitizer tests, and synchronize documentation. | ⬜ | All promoted formats have public transfer and pixel evidence; capability prose never equates texture-only formats with render-target support. |

## Phase S15 — bounded cube and volume sampling

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-144 | Define explicit cube-direction and volume-coordinate sampling ABI, orientation, addressing, filtering, mip/LOD, precision, and resource limits. | ⬜ | The contract maps XNA face axes and 3D coordinates to SkSL child operations with no dependency on unsupported stock 3D geometry. |
| SKIA-145 | Prototype and implement cube sampling from six 2D child shaders using dominant-axis face selection. | ⬜ | Axis and edge/corner directions choose the exact face/UV orientation; point/linear behavior and arbitrary face updates are observable through pixels. |
| SKIA-146 | Add cube mip selection, cross-face seam policy, filtering, and RenderTargetCube snapshot invalidation. | ⬜ | Mip/LOD and all face transitions have discriminating tests; updates become visible without stale snapshots or hidden unbounded copies. |
| SKIA-147 | Prototype and implement volume sampling through a bounded slice atlas or equivalent 2D-child representation. | ⬜ | Point sampling selects exact voxels/slices for NPOT dimensions and all axes; atlas padding cannot bleed between slices. |
| SKIA-148 | Add volume trilinear interpolation, mip selection, address modes, partial updates, and precision tests. | ⬜ | Eight-voxel interpolation and inter-level selection match scalar references; Clamp/Wrap/Mirror and dirty-range updates are deterministic. |
| SKIA-149 | Integrate cube/volume bindings into the explicit SkSL effect ABI with weak lifetime tracking and actionable type/unit errors. | ⬜ | Bind/update/dispose/clone behavior is safe; 2D/cube/volume children cannot be confused and failed binding leaves the next stock draw reusable. |
| SKIA-150 | Add public sampling oracles, content-loaded resources, target-produced inputs, and cross-backend comparisons. | ⬜ | CPU transfer bytes and sampled pixels agree across generated/content/target inputs in Debug, Release, and ASan+UBSan. |
| SKIA-151 | Finalize cube/volume sampling capability wording, resource budgets, performance bounds, and parity documentation. | ⬜ | `Texture3D` reporting distinguishes storage from the promoted sampling subset, and no stock 3D or general effect capability is implied. |

## Phase S16 — wider programmable 2D effects

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-152 | Inventory all EasyGL GLSL effect sources and classify language, vertex attributes/varyings, uniforms, samplers, discard, derivatives, MRT, depth, and 3D dependencies. | ⬜ | Every checked-in source has a direct SkSL, SkMesh, restricted-translation, 3D-only, or impossible disposition and an evidence task. |
| SKIA-153 | Prototype `SkMeshSpecification` for the complete reusable 2D vertex/fragment subset, including transforms, colour, UV interpolation, clipping, and child sampling. | ⬜ | Perspective/affine interpolation, both windings, SpriteBatch ordering, alpha convention, and raster/GPU behavior match the chosen EasyGL 2D oracle. |
| SKIA-154 | Define and implement a versioned explicit SkSL mesh/effect ABI with bounded attributes, varyings, uniforms, textures, source sizes, and compilation cache. | ⬜ | Public opt-in cannot be mistaken for GLSL; reflection/type/layout/lifetime diagnostics are exact and clones isolate mutable state. |
| SKIA-155 | Implement a deliberately restricted GLSL-to-SkSL translator only for the source constructs proven equivalent by SKIA-152/153. | ⬜ | Accepted grammar and semantic rewrites are documented and differential-tested; unsupported preprocessor, stage, precision, derivative, discard, depth, MRT, cube/volume, or backend-specific constructs reject with source locations. |
| SKIA-156 | Harden effect compilation, caching, resource/time limits, error propagation, and recovery across raster and future GPU modes. | ⬜ | Malicious/oversized/invalid inputs stay bounded, cache keys include ABI/mode/source/layout, failures do not poison later draws, and sanitizer stress passes. |
| SKIA-157 | Integrate the promoted effect subset through `ShaderEffect`, content descriptors, SpriteBatch/SkMesh draws, setters, textures, clones, and disposal. | ⬜ | All accepted constructors/setters and both explicit SkSL/restricted GLSL routes have public tests; arbitrary EasyGL GLSL still cannot silently fall back. |
| SKIA-158 | Compare every promoted 2D effect against EasyGL/XNA-style goldens and publish the final programmable-effect boundary. | ⬜ | Pixel tolerances are feature-specific, `CustomEffects` changes only if its complete enum contract is actually met, and all rejected families remain explicit. |

## Phase S17 — opt-in Ganesh/OpenGL, MSAA, and anisotropy

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-159 | Produce a separately pinned Ganesh/OpenGL Skia artifact and CMake dependency target without changing the validated raster artifact. | ⬜ | Exact GN args, archive list, ABI/license/NOTICE, offline configure, cache behavior, and missing/mismatched artifact diagnostics are reproducible. |
| SKIA-160 | Add an explicit construction-time raster/Ganesh mode selection and mode-specific capability diagnostic with no silent runtime fallback. | ⬜ | Raster remains the default regression mode; requested Ganesh either initializes fully or fails transactionally, and tests can select/label each mode. |
| SKIA-161 | Implement backend-owned SDL OpenGL context, Ganesh context/surface wrapping, flush/submit, swap, readback, and destruction order. | ⬜ | A visible smoke and automated pixel test prove the real default framebuffer path, top/bottom origin, alpha/channel order, and absence of EasyGL delegation. |
| SKIA-162 | Implement resize, fullscreen, presentation mappings, resource synchronization, presenter/context loss, and recovery for Ganesh. | ⬜ | Live textures/targets/effects survive or report deterministic loss/reset events; no stale GPU object or raster-mode switch occurs. |
| SKIA-163 | Run complete raster-versus-Ganesh 2D parity, lifecycle, resource-budget, performance, Release, and sanitizer suites. | ⬜ | Exact/toleranced oracle results, ownership, state leakage, repeated reconstruction, and platform boundaries pass before Ganesh is advertised. |
| SKIA-164 | Probe and implement GPU MSAA for backbuffer and RenderTarget2D, including count normalization, resolve, readback, resize, and capability reporting. | ⬜ | Requests 0/1/2/4/oversized use real device limits; resolved pixels and target properties match applied counts, while raster policy stays unchanged. |
| SKIA-165 | Probe and implement true GPU anisotropic sampling through `SkSamplingOptions::Aniso`, device clamping, mip interaction, and capability reporting. | ⬜ | Minification oracles distinguish anisotropy from Linear fallback at 1/4/max/oversized requests; unsupported devices report false without pixel/state corruption. |

## Phase S18 — MRT decision, integration, and successor release gate

| ID | Task | Status | Acceptance evidence |
|---|---|---|---|
| SKIA-166 | Re-evaluate MRT after SkMesh/Ganesh integration using shaders with distinct outputs, blending, masks, target formats/sizes, and failure ordering. | ⬜ | The spike proves whether public Skia can produce independent locations 0–3; identical-output replay is explicitly insufficient evidence. |
| SKIA-167 | Implement full MRT only if SKIA-166 can reproduce distinct public outputs exactly; otherwise retain atomic refusal and document the fundamental public-Skia boundary. | ⬜ | Two/three/four-target oracles pass with independent values and masks, or all counts above one reject before mutation with a proof that no exact route exists. |
| SKIA-168 | Run combined mip/format/blend/effect/cube/volume/MSAA/anisotropy/MRT interaction tests and update all source audits. | ⬜ | Cross-feature state transitions, target switching, disposal, reset, content, and error precedence pass without combinatorial silent fallbacks. |
| SKIA-169 | Run fresh raster and Ganesh builds, complete Debug/Release/sanitizer suites, reusable EasyGL comparisons, demos, and available platform backend matrix checks. | ⬜ | Commands, exact counts/timings, unsupported hosts, warnings, and any external-stack sanitizer baseline are recorded from clean configurations using at most eight workers. |
| SKIA-170 | Hold the successor release gate and advertise only features with direct public evidence. | ⬜ | All SKIA-115–170 rows have an accurate implemented/emulated/refused disposition; capability code, ledgers, matrices, ADRs, build guide, `NEXT_skia.md`, and release summary agree. |

## Completed dependency order

1. SKIA-1 through SKIA-9 settled dependency, surface ownership, and the raster release mode.
2. SKIA-10 through SKIA-30 made the backend selectable and established exact Texture2D pixels.
3. SKIA-31 through SKIA-75 completed the usable 2D path, state handling, and render targets.
4. SKIA-76 through SKIA-94 closed device-dependent probes and only the bounded resource/effect
   emulations that passed their spikes.
5. SKIA-95 through SKIA-105 made the evidence-backed decision to retain uniform 3D refusal.
6. SKIA-106 through SKIA-114 completed regression coverage, documentation, cross-backend checks,
   and the release gate.

## Baseline exclusions during the active expansion

The completed SKIA-1–114 investigation confirms that the baseline release must **not** advertise or silently
approximate generic EasyGL GLSL vertex/fragment `ShaderEffect`, full XNA 3D primitive rendering,
depth/stencil state, occlusion queries, instancing, wireframe, arbitrary cube/volume texture
*sampling*, MRT, native MSAA, or native anisotropy. SKIA-76–SKIA-105 evaluated the relevant direct
and bounded-emulation routes and recorded their baseline fallback or refusal decisions. The active
successor tasks may replace a decision only after new direct evidence, an updated ADR where
applicable, and the SKIA-170 gate; until then the baseline claim remains authoritative.

The completed CPU-raster delivery includes Texture2D/SpriteBatch/SpriteFont, transforms, source
rectangles, flip/rotation/origin, the documented sampler addressing/filtering subset, standard and
bounded proven blend states, scissor, RenderTarget2D, readback, presentation lifecycle, and their
associated validation/lifetime contracts. These remain the required regression boundary for any
future Skia execution mode.
