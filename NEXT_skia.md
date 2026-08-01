# Skia backend continuity

## Session status

- Branch: `feature/skia`; commits are pushed through the SSH `origin` remote.
- Scope: experimental CPU-raster `CNA_GRAPHICS_BACKEND=SKIA`, progressing toward pixel-verified
  2D parity with the observable EasyGL/CNA contracts.  Do not claim 3D, GPU presentation, depth,
  MSAA, mipmaps, MRT, or arbitrary effects until their individual `plan_skia.md` evidence exists.
- Repository policy for this work: leave the unrelated historical `NEXT.md` unchanged.  Record
  Skia continuity only in this file.
- Build policy: configure persistent in-repository Skia builds in `cmake-build-skia*`; every build
  uses at most two jobs (`--parallel 2`).  No subagents are used, so the global two-core limit is
  preserved.  Windowed tests run with `xvfb-run -a` when a real display is unavailable.

## Completed baseline

- The raster backbuffer, SDL presentation, `Texture2D`, `SpriteBatch`, SpriteFont atlas path,
  scissor/viewport, point/linear Clamp/Wrap/Mirror sampling, the four standard blend presets,
  `RenderTarget2D` level-0 readback/upload, and current raster refusal policies are implemented.
- Recent relevant pushed commits include `3811d0a0` (transactional backend construction) and
  `40fdb6ce` (Skia compile-selection identity coverage).
- `docs/skia-backend.md` records 75 Skia CTests: seven raster-only and 68 display-required tests.
  Validation uses the persistent in-repository `cmake-build-skia` directory, per `CLAUDE.md`.

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

## Current task

Audit the remaining early lifecycle rows SKIA-13, SKIA-14, and SKIA-18 against the actual raster
implementation. Several predate the completed vertical slice and may already be fulfilled by
current code/tests; validate each claim before correcting a stale status or selecting a missing
safe implementation task.

## Next candidates

1. Audit the remaining incomplete SKIA-10 through SKIA-18 rows in dependency order, update stale plan evidence, and implement
   only the independently observable gaps found by that audit.
2. Do not begin accelerated-MSAA/anisotropy rows until an accelerated Skia surface exists to
   probe; the selected raster path has no truthful capability to expose there.
3. Keep the recovery boundary precise: raster resources survive SDL presenter reconstruction;
   only a future accelerated Skia mode may acquire a genuine context-loss lifecycle.

## Known boundaries / assumptions

- The Skia path is raster-only.  Its current requested MSAA 0/1 reports 0; requests normalizing
  to 2+ are rejected and the capability remains false.
- Mipmapped textures and render targets are intentionally rejected by the public raster policy.
- `docs/graphics-backend-feature-matrix.md` currently contains no Skia entry.  Update it only
  with verified facts during the documentation/release-gate tasks; do not copy aspirational claims.
- `NEXT.md` deliberately remains untouched at the user's request.
