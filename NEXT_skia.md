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
- The most recent pushed commits are `50670060` (detach a destroyed bound render target),
  `280d05e8` (raster MSAA policy), and `042be59a` (preserve a RenderTarget2D backend during
  `SetData`).
- `docs/skia-backend.md` records 65 Skia CTests: five raster-only and 60 display-required tests.
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

## Current task

SKIA-55: replace SKIA-54's one-state runtime-blender prototype with a bounded, table-driven
generator for every proven factor/function combination, or retain deterministic rejection for a
combination that fails the expanded pixel matrix.

## Next candidates

1. Complete SKIA-55's public custom-blend matrix, starting with opaque source/destination cases
   from the existing EasyGL/Vulkan tests and preserving explicit alpha-convention boundaries.
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
