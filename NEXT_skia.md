# Skia backend continuity

## Session status

- Branch: `feature/skia`; commits are pushed through the SSH `origin` remote.
- Scope: experimental CPU-raster `CNA_GRAPHICS_BACKEND=SKIA`, progressing toward pixel-verified
  2D parity with the observable EasyGL/CNA contracts. Do not claim 3D drawing, GPU presentation,
  depth, MSAA, renderable/Texture2D mipmaps, MRT, cube/volume sampling, or arbitrary effects until
  their individual `plan_skia.md` evidence exists. Plain cube/volume CPU transfer storage is
  separately proven by SKIA-80–84, and six-face 2D RenderTargetCube emulation by SKIA-85/86 does
  not widen the sampling/depth/MSAA claims.
- Repository policy for this work: leave the unrelated historical `NEXT.md` unchanged.  Record
  Skia continuity only in this file.
- Build policy: configure persistent in-repository Skia builds in `cmake-build-skia*`; every build
  uses at most eight jobs (`--parallel 8`). No subagents are used; concurrent focused compiles are
  allowed only when their combined active work remains within the global eight-core pool. Windowed
  tests run with `xvfb-run -a` when a real display is unavailable.

## Completed baseline

- The raster backbuffer, SDL presentation, `Texture2D`, `SpriteBatch`, SpriteFont atlas path,
  scissor/viewport, point/linear Clamp/Wrap/Mirror sampling, the four standard blend presets,
  `RenderTarget2D` level-0 readback/upload, and current raster refusal policies are implemented.
- Recent relevant pushed commits include `3811d0a0` (transactional backend construction) and
  `40fdb6ce` (Skia compile-selection identity coverage).
- `docs/skia-backend.md` records 108 Skia CTests: 15 raster-only, 91 display-required, and two
  display-free source audits. Validation uses the persistent in-repository `cmake-build-skia`
  directory, per `CLAUDE.md`.

## Completed in this session: SKIA-80 through SKIA-84

- Added bounded CPU-only `TextureCube` and `Texture3D` backends. Cube storage owns six independent
  zeroed RGBA8 faces at every declared mip; volume storage follows CNA/FNA's width/height-driven
  level count while halving all three allocated axes. Rectangle/box transfers copy top-row-first
  and volume slices front-to-back without retaining caller memory.
- Every axis must be positive and at most 16384; each resource is capped at 256 MiB across all
  faces/levels using checked multiplication and addition. Backend transfers allocate no second
  resource shadow. Live cube/volume counts and exact bytes are exposed in `SkiaResourceStats` and
  return to zero on destruction.
- `GraphicsCapability::Texture3D` now reports true for its documented persistent-storage contract.
  `ThreeD`, `CustomEffects`, and every other GPU capability remain false. Cube/volume sampling is
  explicitly outside the result: there is no native/`BindGL` handle and no supported Skia effect
  or 3D consumer. The decision matrix and peak-storage policy are in
  `docs/skia-texture-storage.md`.
- Added eleven CTests: one direct Raster policy test and ten public Display fixtures. Face, partial
  rectangle, mip, DDS load (7/7), volume slice/box/mip, and the shared exhaustive GetData/SetData
  audits (56/56 each) pass under Xvfb. The complete Debug suite passes 91/91 in 11.76 seconds with
  `--parallel 8` after configuring `CNA_TEST_DISPLAY` to the wrapper's assigned display; the cache
  was restored to `:0` afterward. Both source audits pass with the updated 247-row parity ledger
  and 347-entry matrix (75 direct, 27 emulation, 220 3D, 25 device-dependent).
- Debug, Release, and ASan selected binaries compile with `--parallel 8`. The direct policy and both
  56-check public audits pass in Release and with AddressSanitizer (`detect_leaks=0`, required for
  the existing display-stack baseline). A `detect_leaks=1` run cannot complete in this environment:
  in-sandbox LSan reports that ptrace is unsupported and the escalated invocation hangs before test
  output; exact internal counters independently prove all storage vectors release to zero.
- The SKIA-85/86 follow-up is complete below.

## Completed in this session: SKIA-85 and SKIA-86

- Added a common `SkiaRasterTarget` boundary and a bounded `SkiaRenderTargetCubeBackend`. Every
  declared mip owns six stable CPU `SkSurface` faces; Clear and SpriteBatch route to the selected
  level-zero face, leaving a dirty face synchronizes its bytes and regenerates only that face's
  remaining levels with a deterministic 2x2 RGBA box filter.
- Each surface also owns one canonical straight-RGBA transfer shadow. The complete-suite run found
  that relying on premultiplied SkSurface storage made arbitrary translucent `SetData` round-trip
  only 10/64 exact texels. The final implementation updates both stores, returns the canonical
  bytes, and reuses preallocated shadows during rendered-face/mip synchronization. Surfaces and
  shadows together are checked against the 16384-axis and 256 MiB per-resource limits; live count
  and exact combined bytes return to zero on destruction.
- Singular and normalized plural cube-face binding are pixel-equivalent. Empty bindings restore
  the backbuffer, one 2D or one cube face works, Preserve/Discard affects exactly the selected
  face, viewport/scissor reset to its extent, and multiple attachments still reject before any
  draw. Requested depth remains public metadata with `HasRealDepthBuffer=false`; all MSAA requests
  truthfully apply/report zero. No native cube handle or supported sampler was added.
- Added `Skia_RenderTargetCube_Policy` plus four shared public fixtures for GetData, usage,
  properties, and plural binding. The existing exhaustive transfer SetData contract is now 56/56
  with byte-exact RenderTargetCube upload. Final Debug validation passes 96/96 Skia CTests in
  12.55 seconds with `--parallel 8`; Release and ASan (`detect_leaks=0`) pass all six focused
  RenderTargetCube/SetData tests. The source audits pass with 247 ledger rows and 347 matrix entries
  (75 direct, 31 emulation, 216 3D, 25 device-dependent). CTest display caches were restored to
  `:0` after Xvfb runs.
- No `NEXT.md` content was read or changed. SKIA-87/88 was the next task at this checkpoint and is
  now complete below.

## Completed in this session: SKIA-87 and SKIA-88

- The MRT spike is closed as an evidence-backed refusal rather than a partial emulation. A
  `SkCanvas` draw exposes one destination colour, but CNA's observable MRT contract includes
  `ShaderEffect` programs with distinct fragment outputs at locations 0--3 and independent
  `ColorWriteChannels0--3`. Replaying or duplicating the one raster output would therefore be
  wrong as soon as those public effects are used; command recording cannot recover values that
  were never produced.
- Added `Skia_MRT_Rejection`. Otherwise-valid 2D/cube sets of two, three, and four attachments all
  reject with the active target, viewport, scissor, and pixels unchanged. Dimension, duplicate,
  null, and public >4 errors retain their validation precedence. Failed submission does not clear
  a `DiscardContents` candidate or partially write either cube face; a subsequent premultiplied
  AlphaBlend draw reaches only the prior target. The capability remains false.
- The complete Debug suite builds with `--parallel 8` and passes 97/97 under Xvfb in 12.63 seconds
  (nine Raster, 86 Display, two Audit). The focused MRT contract also passes in Release and with
  AddressSanitizer (`detect_leaks=0`). Both source audits remain clean at 247 ledger rows and 347
  matrix entries (75 direct, 31 emulation, 216 3D, 25 device-dependent); every display cache was
  restored to `:0`. The next task is SKIA-89: audit the stock/custom effect surface and source-
  language expectations without widening the currently unsupported capability.

## Completed in this session: SKIA-89

- Added `docs/skia-effects.md`, mapping stock `SpriteEffect`, untagged/backend-specific
  `ShaderEffect` strings, both stages, coordinate/alpha rules, every uniform setter, numeric 2D
  sampler units, named SkSL child shaders, cube/volume sampling, content descriptors, diagnostics,
  and bounded-compilation requirements. The decisive mismatches are structural: runtime SkSL has
  no user vertex stage, accepts one premultiplied fragment result, and binds 2D image shaders by
  child name rather than GLSL sampler unit. Existing GLSL/SPIR-V strings must never be guessed as
  SkSL.
- Added `Skia_Effect_Boundary`: invalid untagged GLSL retains source/clone identity, does not
  fabricate `CustomEffects`, tolerates the existing no-backend setters without drawing, rejects
  custom SpriteBatch Begin, and leaves the same batch reusable on its proven stock path. The
  focused test builds with `--parallel 8` and passes in Debug, Release, and AddressSanitizer
  (`detect_leaks=0`) under Xvfb; all display caches were restored to `:0`. SKIA-90 is the next
  implementation task after this checkpoint.

## Completed in this session: SKIA-90

- `SkiaSpriteBatchBackend::SetCustomEffect` now accepts only null or the exact runtime type
  `SpriteEffect`. CNA's stock object has no backend program, so this is an alias for the existing
  pixel-proven paint path. Runtime type equality, rather than a broad dynamic cast, ensures a
  derived effect cannot have overridden behavior silently discarded. All other effects retain an
  actionable error pointing to the required explicit SkSL contract.
- Added `Skia_SpriteEffect_Alias`: it renders default, explicit stock, and cloned stock routes to
  separate targets with PointClamp, tint, affine transform, rotation and both flips, then compares
  every pixel and proves the oracle contains draw and clear regions. A derived type rejects before
  Begin and the same batch remains reusable.
- A direct concrete-RTTI check initially linked the Skia backend back into the earlier common
  static archive and broke generic tools such as `cna_reference_dump`. The final design adds an
  inline virtual `Effect::IsExactStockSpriteEffectEXT()` query: the base returns false and
  `SpriteEffect` compares its own dynamic type. Backends no longer reference concrete RTTI, while
  derived types still return false.
- The complete Debug build succeeds with `--parallel 8` and the Skia suite passes 99/99 under Xvfb
  in 12.84 seconds (nine Raster, 88 Display, two Audit). Both effect tests pass in Release and ASan
  (`detect_leaks=0`). The three existing `SpriteEffectTest`/`ShaderEffectTest` cases require a
  display and pass 3/3 under Xvfb; their first display-free invocation failed only at expected SDL
  video initialization. All display caches were restored to `:0`. SKIA-91 follows.

## Completed in this session: SKIA-21

- Added a distinct non-zero process-local identity to every logical `SkiaSurface`. The identity
  remains stable when `Resize` transactionally replaces the raster storage; copying and moving are
  forbidden because the active-target binding intentionally retains stable surface addresses.
- `SkiaRenderTargetBinding` records both backbuffer and active identities alongside its pointers
  and validates them before access. `CNA_SKIA_STATE_TRACE=1` now prints those identities, making a
  transition from backbuffer to target and back directly observable.
- Expanded `Skia_Surface_Raster` from 10 to 14 checks: unallocated lifetime behavior, safe Clear
  rejection, canvas/snapshot absence, distinct/stable identity, and the existing exact pixel-
  origin/alpha/bounds contracts. Surface and binding tests pass in Debug and Release and under
  ASan/LSan with `detect_leaks=1`; the eleven-check windowed state-transition suite passes normally
  and under ASan (`detect_leaks=0` for the known display-stack exit baseline).

## Completed in this session: SKIA-1

- Added `docs/skia-easygl-parity-ledger.md`, a 247-row inventory of every public method in eleven
  backend/resource interfaces, all nine `GraphicsCapability` values, and all 109 public non-deleted
  `GraphicsDevice` declarations. Each row records the EasyGL behavior/test surface, current Skia
  result or plan, final status, and evidence/follow-up task.
- Added a standard-library lexical validator that derives the live public declaration set from the
  source headers. It rejects missing, stale, duplicated, malformed, empty, or unknown-status rows;
  overloads are tracked by arity and stable declaration order where arity is ambiguous.
- Registered the validator as display-free `Skia_ParityLedger_Audit`, distinct from raster pixel
  tests. This brings the selected configuration to 79 Skia tests: seven Raster, 71 Display, and
  one Audit.

## Completed in this session: SKIA-2

- Added `docs/skia-easygl-test-matrix.md` with every current EasyGL graphics-suite input: 289 CTest
  registrations, two manually run comparison tools, 17 checked-in golden PNGs, and all 39
  XNA-oracle scenes. Classification follows the fixture's most demanding mandatory leg, so a mixed
  SpriteBatch/stock-effect fixture is 3D rather than overstating raster portability.
- The exact distribution is 75 2D-direct, 19 2D-emulation, 228 3D, and 25 device-dependent items.
  The matrix records the applicable existing Skia evidence or follow-up task for each.
- Added display-free `Skia_TestMatrix_Audit`. It extracts live EasyGL registrations and directory
  contents, then rejects missing, stale, duplicate, malformed, or unclassified entries. Together
  with SKIA-1 this makes two source audits and 80 selected Skia CTests total.

## Completed in this session: SKIA-20

- Ran fresh GNU 14/Debug configurations and complete `CNA` target builds for all ten native Linux
  non-Skia selections: HEADLESS, SOFTWARE, SDL_RENDERER, ASCII, EASYGL, DX3, VULKAN, SDL_GPU,
  BGFX, and WEBGPU. Every build used `--parallel 2`, tests/examples/networking were disabled, and
  compiler caching was disabled.
- BGFX resolved `bgfx.cmake` at `99752df38e40179cf998bb880fe4c16c0b3d60ca`; WebGPU used the
  already-pinned `wgpu-native v29.0.1.1` archive with SHA-256
  `95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a`. Both downloads stayed
  inside ignored build directories. Vulkan built with the system 1.4.309 loader despite absent
  optional shader tools, and SDL_GPU resolved the installed versioned `libshaderc.so.1`.
- Fresh native D3D9, D3D11, D3D12, and Canvas configure probes were rejected by their intentional
  Windows/Emscripten gates with actionable toolchain diagnostics. They are recorded as
  host-incompatible probes, not successful builds. Full commands and results are in
  `docs/skia-nonskia-build-matrix.md`.
- A final incremental `--target CNA --parallel 2` check returned success/no-work in all ten build
  directories. Both display-free audits also remain green (2/2 via `ctest -L Audit --parallel 2`).

## Completed in this session: SKIA-69

- Added `SkiaRenderTargetBinding`: the graphics backend owns the active-surface record and every
  `SkiaRenderTargetBackend` references it weakly.  A target destructor now detaches a dying active
  target to the backbuffer before releasing its surface; target destruction after backend
  destruction is a no-op rather than an invalid callback.
- Added `Skia_RenderTargetBinding_Raster` (seven direct checks, including 128 snapshot lifetimes)
  and `Skia_RenderTarget2D_Lifetime` (public Clear/SpriteBatch recovery and a fresh target cycle).
- Updated `plan_skia.md` (SKIA-69 complete) and `docs/skia-backend.md` (61 Skia CTests).

## Completed in this session: SKIA-51

- Added the header-only `SkiaBlendMapping` table. It is the sole conversion point used by
  `SkiaGraphicsBackend::ApplyBlendState`, preserving the proven `Opaque`, `AlphaBlend`,
  `NonPremultiplied`, and `Additive` mappings and their source-byte conventions.
- All other factor/equation combinations now fail before drawing with an error that spells out the
  requested colour and alpha source/destination factors and functions. This is intentional: a
  general public `BlendState` does not label whether sampled RGBA bytes are straight or
  premultiplied, so a guessed Skia Porter-Duff mode could alter pixels.
- Added `Skia_BlendMapping_Raster` (seven checks: four mappings, 52 factor-position cases,
  25 function pairs, diagnostics) and `Skia_BlendMapping_Policy` (unsupported factor/equation
  diagnostics plus `SpriteBatch` reuse after failed `Begin`).

## Completed in this session: SKIA-53

- The pinned source's `SkRuntimeEffect::MakeForBlender` produces an `SkBlender` with direct
  source/destination RGBA access; unlike fixed `SkBlendMode` and `SkBlenders::Arithmetic`, it can
  express independent colour/alpha factors and equations. All thirteen `Blend` factors and the
  five `BlendFunction` values have a direct SkSL expression, including a uniform blend constant
  and source-alpha saturation. This is an API audit, not yet a public feature claim.
- Added `Skia_RuntimeBlender_Raster`: its four checks compile and execute an independent RGB/alpha
  equation on the real CPU raster surface and establish that a non-premultiplied result is retained.
  That result is necessary for XNA separate-alpha states and must be carried through the upcoming
  public target/readback probe.
- Updated SKIA-54 to prototype this direct runtime blender first. An isolated layer is now a
  fallback only if the direct public SpriteBatch path cannot preserve the contract.

## Completed in this session: SKIA-54

- Added an evidence-bounded public `SkRuntimeEffect` blender for one non-preset state:
  `ColorSource=DestinationColor`, `ColorDestination=Zero`, `AlphaSource=One`,
  `AlphaDestination=Zero`, with Add equations. Its alpha branch is independent of RGB, and its
  destination term comes from Skia's actual active canvas.
- `Skia_RuntimeBlender_Policy` proves `(100,0,0,255)` from source tint `(128,255,128,255)` over
  a red `(200,0,0,255)` destination on the backbuffer, through `RenderTarget2D::GetData`, and
  after sampling the target again. The test's final Opaque draw also proves no custom blender leaks
  into the following standard batch.
- The prototype deliberately uses the established premultiplied source label and opaque test data.
  It does not make arbitrary custom BlendStates supported; SKIA-55 must generate and test the
  complete validated matrix, including non-opaque conventions and blend constants.

## Completed in this session: SKIA-55

- Centralized every accepted blend tuple in `SkiaBlendMapping`: the four established presets plus
  SKIA-54's single runtime-blender state. The graphics backend no longer has a separate ad-hoc
  condition, so a supported tuple always carries its route and source-alpha convention together.
- Expanded `Skia_BlendMapping_Raster` to nine checks, including exhaustive verification of all
  714,025 possible current factor/function combinations. Exactly the five table entries pass;
  all others reach actionable rejection before drawing. The exhaustive test also passes with
  AddressSanitizer and LeakSanitizer enabled.
- This is a bounded correctness result, not an assertion that arbitrary user states are equivalent
  to EasyGL: without a source-byte alpha label, accepting them would require an unverified choice
  between straight and premultiplied input. A future public/API decision or an independently
  proven source convention is required before widening the table.

## Completed in this session: SKIA-56

- `Skia_ColorWriteMask_Raster` proves all sixteen post-blend RGBA write masks on the selected
  premultiplied raster path. It records raw output bytes, so alpha and zero-mask preservation are
  not obscured by unpremultiplied readback conversion.
- The public policy stays intentionally strict pending SKIA-57: a non-`All` ColorWriteChannels mask
  and every non-default MultiSampleMask reject before drawing, and the shared public policy test
  proves the SpriteBatch is still reusable. Raster has no applied MSAA samples, so no honest
  per-sample interpretation exists for a coverage mask.

## Completed in this session: SKIA-57

- Implemented `ColorWriteChannels` for the five existing accepted blend routes. A non-`All`
  target-0 mask selects the result only after its route's blend equation, preserving disabled
  destination bytes through a bounded `SkRuntimeEffect`; `All` retains the prior direct mode path.
- `Skia_ColorWrite_Policy` passes all 16 masks after all five routes on the backbuffer, all 16
  masks for the destination-reading runtime route on RenderTarget2D readback, and a distinct
  alpha-source/alpha-destination matrix. It also passes under AddressSanitizer.
- `ColorWriteChannels1-3` continue to reject because raster supports one target; non-default
  `MultiSampleMask` continues to reject because that target has zero raster samples.

## Completed in this session: SKIA-70

- Audited the pinned Skia headers: `SkImage::withDefaultMipmaps()` is public, but applies only to
  an immutable image snapshot. It provides no public per-level raster readback and no target
  lifecycle contract for invalidation after a mutable `SkSurface` bind or public `SetData` upload.
  CNA must not invent those observable target semantics from Skia internals, so the existing
  raster `mipMap=true` refusal remains the correct bounded outcome.
- Extended `Skia_Texture2D_MipmapPolicy` beyond constructor rejection: after a rejected mipmapped
  target it creates a normal level-0 target, Clear-draws it, reads it through `RenderTarget2D`,
  unbinds it, and samples it through SpriteBatch. This confirms no partial target state leaks from
  the refusal.

## Completed in this session: SKIA-71

- Added `Skia_Resize_Presentation`, a 16-assertion public regression that resizes the backbuffer
  while a preserve target is bound and a SpriteBatch already exists. It proves the target's
  content, then its sampling on the fresh backbuffer, survive the resize; it also proves ordered
  old/new `DeviceResetting`/`DeviceReset` observations.
- The same test drives fullscreen on and off plus `Stretch`/`NativeBackBuffer` presentation modes.
  It verifies the stored presentation contract and continued rendering, deliberately not assuming
  that Xvfb implements the physical fullscreen window transition.

## Completed in this session: SKIA-72

- Fixed Skia's direct window/logical transform fallback: it had divided SDL window-space points by
  the physical renderer output size, which mis-scaled coordinates on HiDPI displays and omitted
  Letterbox/Overscan offsets. It now delegates to `SDL_RenderCoordinatesFromWindow` and
  `SDL_RenderCoordinatesToWindow`, the same renderer mapping that input and mouse-warp use.
- Added `Skia_DisplayScale`: a 40×30 logical Letterbox surface in a 120×60 window validates the
  20-pixel offset in both directions, keeps `GetBackBufferData` in logical pixels, and reports
  actual output/window scale. Xvfb reports 1×; the test's conversion checks use SDL's shared
  DPI-aware path on hardware displays too.

## Completed in this session: SKIA-74

- Added a one-entry immutable sampling-snapshot cache to each live `RenderTarget2D`. It is
  invalidated before Clear, SpriteBatch target drawing, target `SetData`, or discard-on-bind, so
  repeated sampling does not allocate unbounded snapshots and never returns stale target pixels.
- Added `SkiaResourceCounters` and `SkiaGraphicsBackend::GetResourceStatsEXT()` for debug
  diagnostics: live textures (two alpha-labelled images each), target surfaces, target snapshots,
  and their estimated RGBA8 byte counts. Resources share the backend counter safely even if a
  wrapper outlives the backend.
- `Skia_ResourceBudget` verifies snapshot reuse/invalidation and 64 create/sample/release target
  cycles, with all tracked counters returning to zero. Existing RenderTarget sample and 64-frame
  SpriteBatch stress regressions still pass after cache introduction.

## Completed in this session: SKIA-75

- Added one shared `rendertarget2d_golden_test.cpp` to the Skia, EasyGL, and SDL_Renderer test
  registrations. Its checked-in, top-row-first 4×4 RGBA oracle contains four opaque colour
  quadrants. It first compares `RenderTarget2D::GetData`, then unbinds and Point-samples the
  target into an 8×8 backbuffer. This compactly detects target row orientation, stale snapshots,
  target restoration, and scaling/sampling errors without a driver-dependent image format.
- All three render paths match the same oracle exactly: 16/16 target-readback pixels and 64/64
  sampled backbuffer pixels, with an intentional tolerance of zero. EasyGL's isolated validation
  build sets `CNA_BUILD_EXAMPLES=ON`, because its historical CMake test block is explicitly
  guarded by that option.

## Completed in this session: SKIA-16, SKIA-28, and SKIA-65

- The Skia raster resources are CPU-owned, whereas its SDL renderer and streaming texture are
  presentation-only resources. `DebugSimulateContextLoss()` and `DebugRestoreContext()` now each
  reconstruct that SDL presenter without replacing or clearing the raster backbuffer, images,
  RenderTarget2D surfaces, or their bounded snapshots. This is an intentionally narrow emulation:
  it does not claim that the CPU Skia surface experienced a GPU/context loss.
- The factory now passes the graphics-device lifecycle callback to Skia. Each synchronous presenter
  reconstruction reports exactly `DeviceResetting` then `DeviceReset`, with no fabricated
  `DeviceLost`, and leaves the public device status `Normal` when it returns.
- Added `Skia_ContextRecovery` (13 assertions): it holds a source texture, render target, and
  cached target snapshot across both debug entries; verifies event order/status and unchanged
  resource counters; then verifies target readback, texture drawing, target sampling, and actual
  presentation. This closes the remaining recovery portion of SKIA-16, SKIA-28, and SKIA-65.

## Completed in this session: SKIA-17

- Added an immutable startup capability line with the pinned Skia revision, selected raster mode,
  exact `RGBA_8888/premultiplied` storage, zero samples, and unsupported anisotropic filtering.
  The backend emits it once, only after successful construction and window-registry registration.
- Added the display-free `Skia_StartupDiagnostic_Raster` test. Its six checks verify every field,
  the one-line/no-pointer-value policy, and stable static storage; the normal and ASan/LSan builds
  pass. A real Xvfb backend run prints exactly the expected single line and retains all 13 context-
  recovery assertions.

## Completed in this session: SKIA-12

- Made `SkiaGraphicsBackend` construction transactional. A constructor exception now removes a
  committed window-registry entry, destroys any streaming texture, destroys the SDL renderer, and
  preserves the caller-owned window before rethrowing the original stage diagnostic. Successful
  destruction uses the same idempotent texture cleanup helper.
- Added three deterministic internal failure points after renderer creation, backbuffer creation,
  and backend registration. They are constructor-only test seams and do not alter the production
  factory path.
- Added `Skia_Lifecycle` (11 checks): null-window diagnostic, exact failure diagnostic/rollback at
  all three points, an immediately usable retry after each, and 16 complete create/Clear/readback/
  Present/destroy cycles with renderer and registry absence checked after every teardown.

## Completed in this session: SKIA-10 and SKIA-11

- Confirmed the selected `SKIA` branch constructs and links the dedicated backend only after
  resolving the six-archive external Skia dependency. An isolated negative configuration with
  intentionally absent source/build paths stops at configure time with the documented
  `CNA_SKIA_ROOT` error instead of substituting another backend.
- Found and fixed a stale generic test omission: `ExactlyOneGraphicsBackendIsSelected` counted
  every current `CNA_BACKEND_*` macro except `CNA_BACKEND_SKIA`, so the real Skia build would
  report zero selected backends. Added the missing count plus an explicit assertion that the macro
  maps to public type `GraphicsBackendType::Skia` and exact name `SKIA`.

## Completed in this session: SKIA-15

- Exposed the actual selected SDL presenter interval through a read-only internal diagnostic.
  Immediate applies 0; One and Default apply 1; Two first requests 2 and records either 2 or the
  existing documented driver fallback to 1. Presenter reconstruction reapplies that actual value.
- Added `Skia_PresentInterval` (15 checks) for all four public Reset requests, public parameter
  round trips, actual backend values, recovery persistence, and exact Clear/readback/Present after
  all transitions. This closes the selected raster policy only; a future accelerated mode needs
  an independent native-surface probe.

## Completed in this session: SKIA-13 and SKIA-14

- Fixed two stale presentation defects found by auditing the early lifecycle plan: a real window
  resize now reallocates the CPU raster width in `FixedHeightDynamicWidth`, and leaving that mode
  restores the stored preferred virtual width instead of retaining the previously derived width.
- NativeBackBuffer presentation now copies the raster texture into an unscaled logical-sized
  destination after clearing the remaining physical output black. Letterbox, Overscan, and Stretch
  retain SDL's logical-presentation mapping; all coordinate conversion remains delegated to SDL's
  DPI-aware window/render transform.
- Added `Skia_PresentationModes`: 25 checks cover every mode's raster size, measured scale and
  centred offset, bidirectional coordinate round trips, exact Clear/readback/Present, a real SDL
  window resize after mode switches, surface reallocation, preferred-width restoration, and safe
  rejection of an invalid mode. It passes under Xvfb normally and under AddressSanitizer.

## Completed in this session: SKIA-18

- Added a shared owner-thread token and presenter assertion to the Skia graphics backend. Raster
  canvas/SDL operations now reject a foreign thread before reading or mutating active state, and
  validate that the backend's SDL renderer is still the renderer registered for its window.
- Hardened `SkiaRenderTargetBinding`: null backbuffers/targets/surfaces and attempts to alias the
  backend backbuffer as a target are rejected without changing the active route. Every active-
  surface access verifies backbuffer/target consistency and exact target surface ownership.
- Replaced SpriteBatch's raw target-binding lifetime with a weak binding. A batch that outlives its
  graphics backend now fails with an actionable destruction-order diagnostic before touching raw
  blend/raster/surface pointers. Target destruction remains non-throwing and weakly detaches in
  either backend/target destruction order.
- `Skia_Ownership` passes 8/8 normally and under ASan; the expanded display-free
  `Skia_RenderTargetBinding_Raster` passes 10/10 normally and under ASan/LSan with leak detection
  enabled outside the ptrace sandbox. Lifecycle, context recovery, target lifetime, and
  SpriteBatch Begin/End regressions also pass under ASan.

## Completed in this session: SKIA-4, SKIA-7, and SKIA-9

- Added persistent `cmake-build-skia-release` with GNU 14, C++23, ccache disabled, and the same
  pinned six-archive raster dependency. `cna_backend_graphics_skia`, the complete CNA static
  library, and `Skia_Surface_Raster` compile/link with two jobs; the Release raster test passes all
  ten surface/orientation/stride/alpha/bounds/resize checks.
- The existing display-free surface/alpha tests plus windowed presentation-modes, target golden,
  and 2D demo evidence now close the initial raster presentation spike (SKIA-7). No channel, row,
  stride, or one-pixel transfer remains merely inferred.
- Documented the selected-mode failure policy (SKIA-9): the current backend is explicitly raster,
  never attempts an implicit GPU fallback, fails configure on absent/mismatched archives, unwinds
  SDL presenter construction failures transactionally, and emits one immutable raster capability
  diagnostic on success. A future accelerated path must be selected observably at construction.

## Completed in this session: SKIA-8

- Centralized presenter-output measurement so a real or simulated 0×0 output remains unavailable
  instead of being silently replaced by stale window dimensions. FixedHeightDynamicWidth keeps
  its last valid CPU surface until a positive output returns; construction/reset still use the
  requested virtual dimensions when no output exists.
- Added owner-thread-only debug output-size override solely for deterministic minimized/restore
  tests. It never resizes the SDL window and is not a public XNA API.
- `Skia_WindowLifecycle` passes 12/12 normally and under ASan: non-mutating invalid-override
  rejection, exact sentinel preservation through 0×0 Present, real synchronized hide/show,
  positive restore reallocation, eight real physical resize/render/readback cycles, and four
  actual presenter rebuilds preserving the live raster.
  `Skia_PresentationModes` (25/25) and `Skia_ContextRecovery` (13/13) pass beside it under ASan.

## Validation this session

- Configured persistent `cmake-build-skia` and `cmake-build-skia-asan` with `CNA_USE_CCACHE=OFF`.
  The sandbox's global ccache directory is read-only; disabling it is required here and does not
  alter source behaviour.
- Debug build: the two new targets compile.  The new raster CTest passes.  The new display test,
  and the five relevant existing display regressions (`DisposedGuards`, `DoubleDispose`, target
  switch, target `SetData`, presentation edge) all pass under `xvfb-run -a`.
- SKIA-51 debug build: `Skia_BlendMapping_Raster` passes through CTest and the display policy test
  passes under `xvfb-run -a`. Existing Opaque, AlphaBlend, NonPremultiplied, Additive, and
  blend-enable state pixel regressions also pass under `xvfb-run -a`.
- ASan/LSan: the direct raster lifetime test passes 7/7 with `detect_leaks=1`.  The public windowed
  test passes all four checks.  LSan then reports an unsymbolized 2,864-byte exit residual; the
  existing `Skia_Presentation_Edge` has the byte-identical residual under the same GCC/Xvfb and
  suppressions.  It is an external display-stack baseline, not hidden by a broader suppression.
- SKIA-51 ASan/LSan: the direct raster mapping test passes with `detect_leaks=1`; the public
  display test passes with AddressSanitizer (`detect_leaks=0`) under Xvfb. The known display-stack
  residual above is unchanged and is not suppressed more broadly.
- SKIA-53 debug build: `Skia_RuntimeBlender_Raster` passes through CTest (four assertions). Its
  direct output verifies both source/destination access and the non-premultiplied-result boundary.
- SKIA-54 debug build: `Skia_RuntimeBlender_Policy` passes all three public assertions under
  `xvfb-run -a`; the existing unmapped-state policy still passes after the new narrow mapping.
- SKIA-55 debug/ASan: the exhaustive nine-check blend selector passes through CTest and with
  `detect_leaks=1`; the public runtime-blender and rejected-state display regressions pass under
  Xvfb after table centralization.
- SKIA-56 debug/ASan: all sixteen low-level masks pass through CTest and with `detect_leaks=1`;
  the public rejection/recovery test passes all five assertions under Xvfb.
- SKIA-57 debug/ASan: the 112-case public channel-mask matrix passes under Xvfb in both normal and
  AddressSanitizer builds; the BlendState policy regression still rejects only the unproven blend
  tuples and non-default sample mask.
- SKIA-70 debug/ASan: `Skia_Texture2D_MipmapPolicy` passes all six checks under Xvfb in the normal
  and AddressSanitizer builds. A bare CTest invocation has no X11 server in this environment, as
  expected for the test's registered display requirement; the Xvfb invocation is the valid check.
- SKIA-71 debug/ASan: `Skia_Resize_Presentation` passes all 16 checks under Xvfb in normal and
  AddressSanitizer builds (`detect_leaks=0` only for the known process-exit display-stack residual).
- SKIA-72 debug/ASan: `Skia_DisplayScale` passes all 10 checks under Xvfb in normal and
  AddressSanitizer builds; its diagnostic records `logical=40x30`, `window=120x60`, and 1× output
  scale in this virtual display.
- SKIA-74 debug/ASan: `Skia_ResourceBudget` passes all nine checks under Xvfb in normal and
  AddressSanitizer builds. Existing `Skia_RenderTarget2D_SampleAfterUnbind` and
  `Skia_SpriteBatch_Stress` also pass after snapshot caching (the latter reports 64 stable frames,
  1,664 Begin/Draw/End blocks, and 192 target changes).
- SKIA-75: `Skia_RenderTarget2D_Golden` passes under Xvfb in the normal and AddressSanitizer
  builds (`detect_leaks=0`, matching the known display-stack exit baseline). The independently
  configured SDL_Renderer and EasyGL targets also pass under Xvfb with the identical 16/16 and
  64/64 exact-RGBA results.
- SKIA-16/SKIA-28/SKIA-65: `Skia_ContextRecovery` passes all 13 checks under Xvfb in normal and
  AddressSanitizer builds (`detect_leaks=0`). Following its presenter-recreation change,
  `Skia_Resize_Presentation` (16 checks), `Skia_ResourceBudget` (9 checks), and
  `Skia_RenderTarget2D_Golden` (16/16 target plus 64/64 sampled pixels) also pass under Xvfb.
- SKIA-17: `Skia_StartupDiagnostic_Raster` passes 6/6 normally and under ASan/LSan with
  `detect_leaks=1`. LeakSanitizer requires execution outside the workspace ptrace sandbox; the
  first sandboxed launch stopped before the test with LSan's explicit ptrace diagnostic, while
  the unrestricted rerun completed cleanly. `Skia_ContextRecovery` confirms the line is emitted
  once during a real Xvfb backend startup.
- SKIA-12: `Skia_Lifecycle` passes all 11 checks under Xvfb normally and under ASan with
  `detect_leaks=0`. With leak detection enabled and the existing narrow suppression file, all
  assertions complete before LSan reports the byte-identical known external X11 residual:
  2,864 bytes in 2,696 + 128 + 40 byte allocations. No broader suppression was added.
- SKIA-10/SKIA-11: the positive persistent Skia configuration remains valid; the missing-
  dependency configuration fails with the expected actionable CMake diagnostic. The monolithic
  `CnaTests` target compiles successfully with two jobs, and the six backend-type plus two compile-
  definition tests pass 8/8 in 1.37 seconds.
- SKIA-15: `Skia_PresentInterval` passes all 15 checks under Xvfb in normal and AddressSanitizer
  builds (`detect_leaks=0` for the known display-stack exit baseline).
- SKIA-13/SKIA-14: `Skia_PresentationModes` passes 25/25 under Xvfb in normal and AddressSanitizer
  builds. `Skia_DisplayScale` (10/10), `Skia_Resize_Presentation` (16/16), and
  `Skia_Presentation_Edge` (4/4) also pass in both builds; the 2D demo completes its three-frame
  smoke run from the build directory.
- Full normal Skia milestone: all 78 labelled tests pass on one Xvfb server with CTest parallelism
  2 (71 Display, seven Raster; 48.93 seconds real time). The temporary test display cache value was
  restored to the repository's prior `:0` setting after the run.
- SKIA-4 Release: `cmake-build-skia-release` configured successfully with ccache disabled; the
  two-job build completed 479 initial steps for the backend, full CNA static library, and raster
  smoke. The current `Skia_Surface_Raster` passes all 14 checks and the target-binding companion
  passes 10/10 in Release.

## Completed in this session: SKIA-91

- Added a dedicated `SkiaEffectBackend`. Only the exact opaque-source marker
  `CNA_SKIA_SKSL_V1` constructs it; existing untagged GLSL/SPIR-V payloads keep their prior null
  result. The fragment source compiles through public raster `SkRuntimeEffect::MakeForShader`.
- The v1 prototype ABI is deliberately exact: one `uniform shader cnaTexture0`, one non-array
  `uniform float4 cnaTint`, source-pixel local coordinates, and the active SpriteBatch sampling,
  addressing, source-rectangle, transform, blend, viewport and scissor paths. The per-draw tint
  uses the stock path's already-proven premultiplied/straight convention.
- Source is bounded to 65,536 bytes and reflected uniforms to 16,384 bytes. Tagged compiler,
  child/uniform ABI, and limit failures retain actionable diagnostics. Pinned Skia exposes no
  public compile-time/compiler-memory budget; this limitation is explicit in `docs/skia-effects.md`.
- `Skia_SkSL_Effect_Prototype`, `Skia_Effect_Boundary`, and `Skia_SpriteEffect_Alias` pass together
  under Xvfb. The new test proves a real red/green channel transform, bad compiler text, wrong ABI,
  pre-compile source-size refusal, and stock-path reuse after failure. Both audit validators pass.
- The complete Debug Skia suite passes 100/100 in 12.99 seconds with `--parallel 8` (9 Raster,
  89 Display, 2 Audit). The focused prototype also passes in Release and under AddressSanitizer
  (`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`); all three build caches were restored to `:0`.
- `CustomEffects` remains false because this fragment-only opt-in is not arbitrary EasyGL GLSL.
  General reflected setters and additional 2D children remain SKIA-92 work. `NEXT.md` was not read
  or changed.

## Completed in this session: SKIA-92

- Expanded tagged v1 ABI validation to 1–64 uniforms in at most 16 KiB and one to eight unique
  shader children. Reserved `cnaTexture0`/`cnaTint` remain mandatory. Supported user shapes are
  non-array float, int, float2/3/4 and float4x4 plus float/float2 arrays; unsupported types,
  half/layout flags, wrong child names/types and `cnaTexture8+` reject before the effect is valid.
- All eight `ShaderEffect` setter entry points now validate effect/name/type/array flag/count/data/
  reflected byte range and copy the exact packed bytes. `cnaTint` is draw-reserved. The test reads
  a caller column-major array's index 9 as SkSL `matrix[2][1]`, proving non-symmetric layout rather
  than relying on identity-matrix coincidence.
- `SetTexture(1..7, Texture2D)` maps only to a declared `cnaTexture1..7`; unit 0, undeclared,
  out-of-range, null, cube and volume bindings throw actionable errors. `ITextureBackend` now has
  `enable_shared_from_this`, allowing the effect to retain a weak backend instead of a raw pointer
  or stale SkImage. Draw locks and snapshots current pixels, so post-bind `SetData` is visible and
  Dispose expires safely before Begin without hidden image memory or resource-counter drift.
- Added `Skia_SkSL_UniformTexture`: one pixel equation consumes every setter, cnaTint, a post-bind
  additional-texture update, source rectangle, transform and PointClamp. It also covers every
  negative boundary, disposed binding, missing clone binding, zero-initialized clone uniforms and
  original/clone isolation. The earlier SKIA-91 prototype remains green.
- Full Debug build succeeds and all 101 Skia tests pass under Xvfb in 16.22 seconds with
  `--parallel 8` (9 Raster, 90 Display, 2 Audit). Both SkSL tests pass in Release and ASan with
  `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`; all display caches were restored to `:0`.
- The supported related GTest filter (`ShaderEffectTest.*:Texture2DTest.*`) passes 37/37 under
  Xvfb. A deliberately broader 41-test probe passed 40 and hit the pre-existing expected Skia
  boundary in `Texture2DMipLevelValidationTest.EveryValidMip...`: it constructs `mipMap=true`,
  which Skia rejects by policy; the registered Skia mip-policy tests remain green.
- `CustomEffects` stays false: the implementation is an explicit fragment-only SkSL extension,
  not arbitrary EasyGL GLSL/vertex-stage/cube/volume compatibility. `NEXT.md` remained untouched.

## Current task

SKIA-100 implementation and validation are complete. Commit/push this checkpoint, then perform
SKIA-101 as a complete maintainability/cost decision across all 37 renderer requirement IDs.
Do not promote one successful BasicEffect route into public 3D support.

## Completed in this session: SKIA-93

- Added the display-free `Skia_Effect_Emulation_Spike`, deliberately below the public stock-effect
  route. Its binary runtime clip samples alpha bytes 127/128/129 against reference 128 and matches
  all 24 decisions from the eight `CompareFunction` modes. Failed pixels retain an opaque sentinel
  under source replacement; an explicit transparent-source control erases it, proving that a
  returned zero colour cannot stand in for discard.
- The same fixture performs DualTextureEffect's `texture0.rgb *= 2; texture0 * texture1 * tint`
  equation in one runtime shader. Independent texture-0 Repeat and texture-1 Mirror child shaders
  produce the discriminating 32/193/96/64 four-pixel oracle. A composed color filter independently
  proves two asymmetric swizzle/scale/bias outputs.
- No intermediate target is needed. Dual texture samples two children once in one paint and colour
  transform is one source sample plus one filter. Alpha test evaluates the source for its binary
  clip and again for colour, and creates one clip entry per draw. The final raster is quantized once
  to premultiplied RGBA8; an intermediate design would add another clamp/round/premultiplication
  boundary and is not accepted without separate evidence.
- These are reusable fragment/coverage components, not stock-effect support. AlphaTestEffect and
  DualTextureEffect still require public primitive geometry, world/view/projection, vertex colour,
  vertex-derived fog, sampler-slot state, triangle coverage and depth semantics. SKIA-94 must keep
  them unsupported unless its complete property/oracle audit proves otherwise.
- The focused raster test passes 8/8 in Debug and Release. Its escalated ASan/LSan run passes 8/8
  with `detect_leaks=1`; the first sandboxed LSan invocation stopped before assertions with the
  environment's explicit ptrace diagnostic. The full Debug build succeeds and all 102 Skia CTests
  pass under Xvfb in 16.32 seconds with `--parallel 8` (10 Raster, 90 Display, two Audit). The
  temporary display cache was restored to `:0`. `NEXT.md` was not read or changed.

## Completed in this session: SKIA-94

- No stock effect was promoted. AlphaTestEffect and DualTextureEffect have valid shared CPU-side
  properties and the SKIA-93 fragment pieces are feasible, but their public route begins with a
  transformed vertex stream. Skia rejects `DrawUserPrimitives` at `CreateVertexBuffer` before
  fragment work; matrices, triangle coverage, per-vertex fog/colour and depth are not optional
  properties that can be silently dropped.
- Added `Skia_StockEffect_Boundary`. It checks all eight alpha compare modes and the 24
  below/equal/above forwarded decisions, then proves every corresponding public draw refuses with
  the no-3D `CreateVertexBuffer` diagnostic. It also checks forwarded dual textures,
  premultiplied tint, fog and vertex colour plus all 16 texture-availability/fog/vertex-colour
  combinations. Every failure leaves an opaque sentinel pixel byte-exactly unchanged.
- SpriteBatch's fallback diagnostic now separates stock 3D effects (unsupported primitive route)
  from other custom effects (explicit `CNA_SKIA_SKSL_V1` route), while retaining the `custom
  Effects` wording covered by SKIA-89. The same batch renders normally after AlphaTestEffect and
  DualTextureEffect Begin refusals.
- The new eight-check display fixture and `Skia_Effect_Boundary` pass together under Xvfb. The 78
  existing AlphaTestEffect/DualTextureEffect default, setter, clone, owned-texture,
  reference-scaling and forwarding GTests pass 78/78 under Xvfb. Their first display-free attempt
  failed only at expected SDL video initialization, before assertions.
- Existing EasyGL golden images remain classified as 3D. No Skia golden is registered because no
  additional stock effect passed the complete public gate; a fragment-only golden would be false
  promotion evidence. `ThreeD` and `CustomEffects` remain false.
- `Skia_StockEffect_Boundary` and `Skia_Effect_Boundary` pass together in Debug and Release and
  under AddressSanitizer (`detect_leaks=0` for the known display-stack exit baseline). The full
  Debug build succeeds and all 103 Skia CTests pass under Xvfb in 12.86 seconds with `--parallel 8`
  (10 Raster, 91 Display, two Audit). All Debug/Release/ASan display caches were restored to `:0`.
  `NEXT.md` was not read or changed.

## Completed in this session: SKIA-95

- Added `docs/skia-3d-call-effect-matrix.md` with 37 stable requirement IDs covering fixed/custom
  vertex layouts, vertex/index buffers, 16/32-bit indices, DrawUser, triangle/strip/line topology,
  instancing, transformation/clipping/interpolation, 2D/cube/volume sampling, sampler/mip/
  anisotropy behavior, depth/stencil/cull/fill/blend/MRT/MSAA/order state, viewport/scissor, every
  stock/custom effect family, lighting, fog, model/skinning, queries, resource validation and
  target passes. Each row records the current raster boundary and responsible SKIA-96–105 task.
- Extended `scripts/validate_skia_test_matrix.py` without a generic fallback. It maps the stable
  entry name plus its adjacent evidence, requires every feature ID to be documented and exercised,
  and fails an unrecognized `3d` entry. `--dump-3d` emits the exact live expansion for review.
- The audit initially exposed three stale SKIA-2 classifications during source verification:
  `EasyGL_TexturedQuad_Readback` is SpriteBatch/readback-only, while both `CubeVolume_*DataContract`
  fixtures are transfer-only and already pass against bounded CPU storage. They are now correctly
  `2d-direct`/`2d-emulation`, leaving 213 current primary `3d` entries.
- The audit covers all 213/213 primary `3d` matrix entries plus a closed set of 16 relevant
  device-dependent cross-cuts: occlusion, depth formats, depth/MSAA, resolves/cube MSAA and
  anisotropic sampling. Presentation/reset/window/handle tests and DXT1 remain deliberately in
  their own phases.
- `python3 -m py_compile scripts/validate_skia_test_matrix.py`, the direct validator and both
  registered Skia audits pass. The validator reports 347 total entries and 229 SKIA-95 mappings
  across all 37 features. No product code or capability changed; `ThreeD` stays false.
  `NEXT.md` was not read or changed.

## Completed in this session: SKIA-96

- Added the headless internal `Skia_ProjectedVertices_Spike`; no public Draw call, buffer, effect
  or capability was changed. A non-trivial CNA row-vector WVP produces clip `(0,.25,.25,1)` and
  CPU top-left raster `(32,24)` exactly. With equal clip W, a PCT triangle's RGB interpolation at
  `(24.5,24.5)` is byte-exact `(80,88,88)`.
- The same projected triangle with clip W `(1,4,1)` samples gradient byte 88 through SkVertices'
  affine 2D coordinates, while EasyGL's unqualified GLSL varying requires perspective result 30.
  This 58-byte difference is the first material mismatch and cannot be recovered after W is
  discarded.
- SkVertices also paints `(32,16)` for a vertex outside EasyGL/GL's `z >= -w` near plane, above
  the correct intersections at Y=32, and paints both signs of triangle area. Homogeneous clipping
  and configured culling would therefore be CPU responsibilities.
- `docs/skia-skvertices-spike.md` records the source-level EasyGL comparison and exact measured
  boundary. A direct SkVertices 3D bridge is unsound. SKIA-97 may proceed only as a separate CPU
  rasterizer owning clipping, perspective varyings, coverage and depth, with Skia receiving a
  completed colour image. `ThreeD` remains false and `NEXT.md` was not read or changed.
- The focused test passes in Debug and Release and in an escalated ASan/LSan run with
  `detect_leaks=1`; the sandboxed LSan attempt stopped at the environment's explicit ptrace
  limitation before assertions. The complete Debug Skia suite passes 104/104 under Xvfb in
  17.07 seconds with `--parallel 8` (11 Raster, 91 Display, two Audit). The Debug display cache was
  restored to `:0`.

## Completed in this session: SKIA-97

- Added the internal headless `Skia_CpuDepthRaster_Spike`. A bounded CPU target owns top-row-first
  RGBA8 plus float depth at exactly eight bytes/pixel, accepts axes 1..16384 and refuses combined
  storage above 256 MiB before allocation. No product code or capability changed.
- LessEqual/write is order-independent for opaque far/near triangles. Colour+depth clear and a
  separate depth-only clear behave independently. Retaining reciprocal W recovers byte 30 for the
  SKIA-96 sample instead of SkVertices' affine 88.
- A→B→A switching preserves differently sized targets' colour and depth. Whole-target RGBA8
  `WritePixels` handoff reaches matching Skia surfaces exactly; a size mismatch leaves the Skia
  sentinel unchanged.
- The 640×360 target owns exactly 1,843,200 B. For a single-threaded workload of 128 overlapping
  triangles, Debug measured 6,452/1,244,666/537 µs for clear/raster/handoff, Release measured
  64/193,043/616 µs, and ASan+LSan measured 32,178/2,206,201/534 µs. The escalated leak-enabled
  run passed.
- `docs/skia-cpu-depth-spike.md` records the handoff and limitations: opaque-only, already-clipped
  triangles, incomplete shared-edge rules, float rather than declared depth formats, no stencil,
  states, textures, effects or mixed 2D/3D ordering. The depth prerequisite permits only an
  isolated SKIA-98 stencil spike. `ThreeD` and depth capabilities remain false; `NEXT.md` was not
  read or changed.
- The focused test passes in Debug and Release and in an escalated leak-enabled ASan/LSan run.
  The complete Debug Skia suite passes 105/105 under Xvfb in 13.14 seconds with `--parallel 8`
  (12 Raster, 91 Display, two Audit). Debug, Release and ASan display caches are `:0`.

## Completed in this session: SKIA-98

- Added the headless, internal `Skia_CpuStencil_Spike`. It models the low eight bits of
  Depth24Stencil8 after SKIA-97's CPU bridge and remains disconnected from every public Draw call,
  attachment and capability.
- All eight `CompareFunction` values run over every reference/stored byte pair and eight
  discriminating read masks. All eight `StencilOperation` values likewise run over every
  stored/reference byte pair and eight write masks, including wrapping, saturation, replacement
  and inversion. Both matrices pass 4,194,304 cases.
- The state-machine checks separately prove stencil-fail, depth-fail and full-pass ordering;
  rejected fragments cannot write colour/depth. Disabled stencil bypass, narrow read/write masks,
  default public state values, and clear independence all match the EasyGL source contract.
- Two-sided selection reproduces the existing EasyGL fixture exactly: the counter-clockwise fail
  operation turns `0x05` into `0x06`, while disabling two-sided mode selects the ordinary pass
  operation and yields `0x04`. All 16 `ColorWriteChannels` masks independently preserve successful
  depth/stencil writes.
- `docs/skia-cpu-stencil-spike.md` records the ordering oracle, matrix and boundary. A candidate
  RGBA8+float-depth+stencil target would own nine bytes/pixel (2,073,600 B at 640×360). This is
  feasibility evidence only: format precision, raster state, MSAA, vertex data, effects and public
  ownership remain unimplemented; `ThreeD` and depth/stencil capabilities stay false.
- The focused test passes without compiler warnings in Debug and Release and in an escalated
  leak-enabled ASan run. The complete Debug Skia suite passes 106/106 under Xvfb in 13.28 seconds
  with `--parallel 8` (13 Raster, 91 Display, two Audit). Debug, Release and ASan display caches are
  `:0`; `NEXT.md` was not read or changed.

## Completed in this session: SKIA-99

- Added the headless, internal `Skia_CpuGeometry_Spike`; no public buffer, draw or capability path
  changed. It preserves the exact seven built-in declarations (strides 16/20/24/32/48/52/68),
  decodes all 12 `VertexElementFormat` values, retains all 13 `VertexElementUsage` values plus
  usage indices, and requires an explicit valid `POSITION0` instead of silently treating unknown
  input as position bytes.
- Bounded CPU vertex uploads preserve source offsets and the logical replacement semantics of
  None/Discard/NoOverwrite. Bounded 16- and 32-bit index uploads preserve source offsets; an
  indexed fixture fetches vertex 70000 without truncation. Bad widths, layouts, capacities,
  ranges and topology counts reject before mutating stored data or emitted primitives.
- Triangle list/strip, line list/strip and PointListEXT expand to exact counts. Odd strip
  triangles swap their first two vertices to retain winding. XNA's clockwise-as-displayed
  front-face convention selects all three cull modes, and wireframe expands three explicit edges
  only after triangle culling.
- Raw DrawUser input applies declaration-driven byte offsets. The four currently public typed
  streams pack their values through the canonical declarations rather than copying C++ ABI bytes.
  Indexed raw paths independently apply vertex/index offsets for both widths. The fixture passes
  17/17 checks in Debug and Release.
- The first leak-enabled ASan run found a test-only use-after-free: a reference bound through
  `Position(vertices.Decode(0))` outlived the temporary decoded vertex. Retaining the decoded value
  fixes the lifetime; the rebuilt leak-enabled ASan/LSan run passes all 17 checks with no leaks.
- `docs/skia-cpu-geometry-spike.md` records the exact contract and remaining costs. The spike
  proves input assembly, not exact line/point pixel coverage, instancing, clipping/raster coverage,
  effects, dynamic-upload performance or mixed 2D/3D ordering. Public `ThreeD`, depth/stencil and
  wireframe capabilities remain false.
- The complete Debug Skia suite passes 107/107 under Xvfb in 13.21 seconds with `--parallel 8`
  (14 Raster, 91 Display, two Audit). Focused Debug/Release/ASan tests and both audits pass; Debug,
  Release and ASan display caches are `:0`. `NEXT.md` was not read or changed.

## Completed in this session: SKIA-100

- Added the headless, internal `Skia_CpuStockEffect_Spike`. Its one accepted path is deliberately
  narrow: already-clipped, unlit, no-fog textured BasicEffect PCT triangles with optional vertex
  colour, point+clamp sampling, LessEqual depth and completed RGBA8 handoff to Skia. Axes are
  1..16384 and each CPU texture/target refuses storage above 256 MiB before allocation.
- Four quadrant draws use the exact 2x2 texture, vertex colour, diffuse/emissive material and
  expected bytes from `EasyGL_BasicEffect_Combined`; all four match `(99,52,23)`, `(25,104,47)`,
  `(49,26,93)` and `(74,78,70)`. A separate clip-W `(1,4,1)` triangle uses a four-texel strip:
  reciprocal-W UV selects red at `(24,24)`, whereas affine interpolation would select green.
  Depth remains exactly `0.5`, and Skia receives the finished pixels without re-shading.
- Missing texture, enabled lighting/fog and a vertex outside homogeneous clip space reject before
  touching sentinel colour or depth. The route does not guess partial clipping or silently omit
  requested effect state.
- Added `docs/skia-stock-effect-feasibility.md` with separate requirement matrices for
  BasicEffect, AlphaTestEffect, DualTextureEffect, EnvironmentMapEffect, SkinnedEffect, PbrEffect
  and SkinnedPbrEffect. A closed executable inventory classifies all 21 requirement groups exactly
  once per family as reusable/prototype/gap and requires exact coverage, public integration and
  mixed ordering to remain explicit gaps.
- Lighting/fog, normal transforms, cube direction sampling, palette evaluation, PBR TBN/five-map
  BRDF, sampler LOD, instancing, production coverage and public ownership are not inferred from
  the unlit result. `ThreeD`, depth/stencil, wireframe and custom-effect capabilities stay false.
- The focused test passes without compiler warnings in Debug and Release and in an escalated
  leak-enabled ASan/LSan run. The complete Debug Skia suite passes 108/108 under Xvfb in 12.99
  seconds with `--parallel 8` (15 Raster, 91 Display, two Audit). Debug, Release and ASan display
  caches are `:0`; `NEXT.md` was not read or changed.

## Next candidates

1. SKIA-101: write the evidence-backed ADR deciding whether the combined CPU raster/input/effect
   design is complete and maintainable. It must account for every one of the 37 stable renderer
   requirement IDs, including line/point coverage, instancing, clipping, sampler LOD, state/mixed
   ordering, every effect family, custom vertex shaders, MRT, MSAA and queries.
2. If SKIA-101 rejects full emulation, proceed to SKIA-102 and make every public 3D entry point
   fail uniformly without disturbing proven 2D/cube-transfer behavior. If accepted, do not
   implement from the current plan: SKIA-103 must first create the funded successor plan.
3. Keep GLSL vertex stages, SPIR-V, cube/volume children, MRT and untagged content unsupported;
   never widen the tagged contract merely to silence a fixture.

## Known boundaries / assumptions

- The Skia path is raster-only.  Its current requested MSAA 0/1 reports 0; requests normalizing
  to 2+ are rejected and the capability remains false.
- Mipmapped textures and render targets are intentionally rejected by the public raster policy.
- `docs/graphics-backend-feature-matrix.md` currently contains no Skia entry.  Update it only
  with verified facts during the documentation/release-gate tasks; do not copy aspirational claims.
- `NEXT.md` deliberately remains untouched at the user's request.
