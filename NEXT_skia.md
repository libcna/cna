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
- The most recent pushed commits are `280d05e8` (raster MSAA policy), `042be59a` (preserve a
  RenderTarget2D backend during `SetData`), and `947a6eba` (presentation edge cases).
- `docs/skia-backend.md` records 59 Skia CTests: two raster-only and 57 display-required tests.
  Earlier runs used `/tmp/cna-skia-tests-build`; future work must migrate validation to an
  incremental in-repository `cmake-build-skia` directory, per `CLAUDE.md`.

## Completed in this session: SKIA-69

- Added `SkiaRenderTargetBinding`: the graphics backend owns the active-surface record and every
  `SkiaRenderTargetBackend` references it weakly.  A target destructor now detaches a dying active
  target to the backbuffer before releasing its surface; target destruction after backend
  destruction is a no-op rather than an invalid callback.
- Added `Skia_RenderTargetBinding_Raster` (seven direct checks, including 128 snapshot lifetimes)
  and `Skia_RenderTarget2D_Lifetime` (public Clear/SpriteBatch recovery and a fresh target cycle).
- Updated `plan_skia.md` (SKIA-69 complete) and `docs/skia-backend.md` (61 Skia CTests).

## Validation this session

- Configured persistent `cmake-build-skia` and `cmake-build-skia-asan` with `CNA_USE_CCACHE=OFF`.
  The sandbox's global ccache directory is read-only; disabling it is required here and does not
  alter source behaviour.
- Debug build: the two new targets compile.  The new raster CTest passes.  The new display test,
  and the five relevant existing display regressions (`DisposedGuards`, `DoubleDispose`, target
  switch, target `SetData`, presentation edge) all pass under `xvfb-run -a`.
- ASan/LSan: the direct raster lifetime test passes 7/7 with `detect_leaks=1`.  The public windowed
  test passes all four checks.  LSan then reports an unsymbolized 2,864-byte exit residual; the
  existing `Skia_Presentation_Edge` has the byte-identical residual under the same GCC/Xvfb and
  suppressions.  It is an external display-stack baseline, not hidden by a broader suppression.

## Current task

SKIA-51: audit XNA blend factors/functions and make the existing four preset mappings into a
table-driven, testable direct-Skia mapping with actionable rejection text for unsupported cases.

## Next candidates

1. Complete SKIA-51 and then investigate independent colour/alpha mappings (SKIA-53).
2. SKIA-71/72: resize and display-scale regressions with live targets, once lifecycle ownership is
   sound.
3. Keep SKIA-65 open: level-0 `SetData` is complete, but device/context recreation belongs to
   SKIA-16/SKIA-28 and is not yet implemented.

## Known boundaries / assumptions

- The Skia path is raster-only.  Its current requested MSAA 0/1 reports 0; requests normalizing
  to 2+ are rejected and the capability remains false.
- Mipmapped textures and render targets are intentionally rejected by the public raster policy.
- `docs/graphics-backend-feature-matrix.md` currently contains no Skia entry.  Update it only
  with verified facts during the documentation/release-gate tasks; do not copy aspirational claims.
- `NEXT.md` deliberately remains untouched at the user's request.
