# Audit: examples/sdlgpu_samplerstate_test.cpp

## Metadata

- Source file: `examples/sdlgpu_samplerstate_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — dynamic `SamplerState` proof for direct 3D draws on
  the SDL_GPU backend
- File type: standalone `Game`-subclass executable, CTest-registered (`SdlGpu_SamplerState`,
  `cmake/Tests/SdlGpuTests.cmake:106-109`, `TIMEOUT 60 LABELS "SdlGpu"`)
- XNA/FNA relevance: direct — `GraphicsDevice.SamplerStates[slot]`, `SamplerState.PointWrap/
  PointClamp/LinearWrap/LinearClamp`, `DualTextureEffect`'s two independent texture units.
- FNA reference: `Graphics/SamplerStateCollection.cs`, `Graphics/SamplerState.cs`,
  `Graphics/Effect/DualTextureEffect.cs`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`applySamplerStatesToBackend()`, lines 1592-1604), `src/CNA/Internal/Backends/SdlGpu/
  SdlGpuGraphicsBackend.cpp` (`ApplySamplerState`, line 1200; `GetOrCreateSampler`/
  `SamplerCacheIndex`/`ToAddressMode`, lines 24-40, 1456-1476; `QueueDualTextureDraw`, lines
  2869-2917; `IssueDualTextureDraw`, lines 3142-3166).

## Purpose

Three-check pixel test proving `GraphicsDevice.SamplerStates[slot]` genuinely reaches direct 3D
draws on this backend (the file's own header states this was previously hardcoded to
Linear+Wrap-then-Clamp for every 3D draw family): (A) `TextureAddressMode` Wrap vs. Clamp via a
UV=1.25 sample past the texture's own range, (B) `TextureFilter` Point vs. Linear via a
near-boundary sample, (C) `DualTextureEffect`'s two texture units resolving *independently*
(slot 0 Wrap + slot 1 Clamp at the same shared UV, multiplying to White only if both slots apply
their own address mode; either slot borrowing the other's mode multiplies to Black instead).
Correct placement for a backend sampler-state integration test.

## Executive Verdict

**Healthy.** All three checks were independently traced to real, distinct code paths in
`SdlGpuGraphicsBackend.cpp` and confirmed to exercise what their labels claim. One properly-
disclosed (not stale) production limitation exists — `MaxAnisotropy`/`TextureFilter::Anisotropic`
and the four mixed min/mag/mip `TextureFilter` values are not distinguished from plain `Point` by
this backend — but `plans/plan_sdlgpu.md`'s own SDLGPU-21 entry already states this accurately and
current, so it is not a hidden/undocumented defect, and this test does not claim to cover it.

## Checklist Results

### API / XNA / FNA parity
`GraphicsDevice::getSamplerStatesProperty()[slot] = SamplerState::PointWrap` (lines 151, 158, 163,
etc.) matches FNA's `GraphicsDevice.SamplerStates` indexer-collection API shape.
`DualTextureEffect::setTextureProperty`/`setTexture2Property` (lines 194-195) map to FNA's
`DualTextureEffect.Texture`/`Texture2`. The test correctly restores slot 0/1 back to
`SamplerState::LinearWrap` after each sub-check (lines 163, 183, 206-207) — matches real XNA
`GraphicsDevice` semantics where `SamplerStates[slot]` is sticky state that persists across draws
until explicitly reassigned, so leaving a modified slot behind would let one check's setting leak
into the next check's rendering (the restore calls are load-bearing, not cosmetic).

### Behavioral correctness
- Check A (`RunAddressModeCheck`, lines 147-164): `GetOrCreateSampler` (lines 1456-1476) builds a
  real `SDL_GPUSamplerCreateInfo` with `address_mode_u/v = ToAddressMode(addressU/V)`
  (`ToAddressMode`, lines 32-40: `0→REPEAT, 2→MIRRORED_REPEAT, default→CLAMP_TO_EDGE`) —
  `SamplerState::PointWrap`'s `AddressU/V=Wrap(0)` and `PointClamp`'s `AddressU/V=Clamp(1)` map
  correctly onto this. `DrawTexturedQuad` (lines 132-144) reads `samplerSlots_[0]` at
  `Queue*Draw()` time (e.g. `BasicEffect`'s dispatch sets `command.textureFilter/addressU/addressV
  = samplerSlots_[0].filter/addressU/addressV`, lines 2124-2127 for one of several such call
  sites), so the sampler actually bound at issue time (`GetOrCreateSampler(command.textureFilter,
  command.addressU, command.addressV)`, e.g. line 1579) reflects whatever `SamplerStates[0]` held
  *at draw-call time*, matching real XNA's per-draw (not per-frame) sampler-state read.
- Check B (`RunFilterCheck`, lines 167-184): `GetOrCreateSampler`'s `filter = textureFilter == 0 ?
  SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST` (line 1462) — confirmed against
  `TextureFilter.hpp`'s enum ordinals (`Linear=0, Point=1, ...`) that `SamplerState::PointClamp`
  (`Filter=Point=1`) correctly resolves to `NEAREST` and `LinearClamp` (`Filter=Linear=0`) to
  `LINEAR`.
- Check C (`RunDualTextureCheck`, lines 187-208): `QueueDualTextureDraw` independently captures
  `samplerSlots_[0]` into `texture0Filter/AddressU/AddressV` and `samplerSlots_[1]` into
  `texture1Filter/AddressU/AddressV` (lines 2894-2899); `IssueDualTextureDraw` creates two
  *separate* `GetOrCreateSampler` calls, one per slot (lines 3160, 3162) — this is a real,
  independently-bound pair of `SDL_GPUSampler` objects, not one shared sampler reused for both
  texture units. The test's own differential design (either address mode used for both slots always
  multiplies to Black; only genuinely-independent slots multiply to White) is a sound
  discriminator, and the production code traced above shows the independent-slot code path is what
  actually executes.

### Logic
`Matches()`'s asymmetric tolerances (`tol=12` default at line 71, `tol=15` for filterPoint_/
filterLinear_ at lines 283/285) are deliberately loosened for the point-vs-linear check, which
needs to tolerate real sub-pixel interpolation variance near a texel-center sample — reasonable,
not a hidden weakening of the check's actual discriminating power (Check B's real test is `> 15`
green-channel elevation *and* not-a-Red-snap, line 285, not a fixed-value match).

### C++ correctness
Member sampler-result fields (`addressModeWrap_`, etc., lines 210-214) are declared *after* the
methods that assign them (lines 147-208) but *before* `RunAll`/`Draw()` use them — valid C++ (member
declaration order doesn't gate use inside member functions), just an unusual layout; no correctness
issue.

### Robustness
Frame-1 exercises all three checks inside a `try`/`catch` (lines 263-276), converting any
backend exception during sampler setup/draw into a labeled `FAIL` rather than an unhandled crash —
appropriate given this file is explicitly proving a code path (per-slot dynamic samplers) that the
header states used to not exist at all.
Frames 2-120 re-run `RunAll` unconditionally with no exception guard (line 293) — if a sampler-
related exception were to occur only on a later frame (e.g. an SDL sampler-cache eviction bug),
this would crash the test binary rather than reporting a clean `FAIL`. Low practical risk (the
sampler set is static across frames and the cache is keyed purely by filter/address combination,
lines 24-30/1458-1460), but it is an inconsistency with frame 1's own defensive `try`/`catch`.

### Testing
7 checks total (5 explicit `Matches()` assertions plus the frame-1 "no exception" check plus the
frame-120 "survived 120 frames" check) is proportionate given the feature scope (2 sampler
dimensions × 1 slot, plus 1 cross-slot-independence proof). `maxAnisotropy`/`TextureFilter::
Anisotropic` and the 4 mixed-min/mag `TextureFilter` values are not exercised at all — see F1.

## Detailed Findings

### F1 — `TextureFilter::Anisotropic` and every mixed min/mag/mip `TextureFilter` value are collapsed into plain `Point` (Nearest) by this backend; this test does not (and does not claim to) cover that

- Severity: LOW (accurately pre-disclosed limitation, not a hidden defect; this file's own scope
  never claims Anisotropic coverage)
- Confidence: HIGH (read `SamplerCacheIndex`'s `filterIndex = filter == 0 ? 0 : 1` and
  `GetOrCreateSampler`'s two-way `filter == 0 ? LINEAR : NEAREST` ternary directly — every
  non-Linear `TextureFilter` ordinal, including `Anisotropic=2`, collapses to the same `NEAREST`
  bucket, and `maxAnisotropy` is captured into `samplerSlots_[slot].maxAnisotropy` at
  `ApplySamplerState` (line 1207) but never read by `GetOrCreateSampler`, which takes only
  `(textureFilter, addressU, addressV)` as parameters)
- Category: architecture / feature-completeness
- Location/symbol: `SamplerCacheIndex` (`SdlGpuGraphicsBackend.cpp` lines 24-30),
  `GetOrCreateSampler` (lines 1456-1476), `ApplySamplerState` (line 1200-1207)
- Evidence: `plans/plan_sdlgpu.md` line 442 (SDLGPU-21 entry) explicitly and currently states:
  *"`maxAnisotropy` is stored but not applied — `GetOrCreateSampler()`'s cache has no
  anisotropic-filtering dimension at all, a pre-existing limitation shared with SpriteBatch's own
  sampler path, not introduced here."* This audit independently confirmed the claim is still
  accurate against the current source (no `enable_anisotropy`/`max_anisotropy` field is ever set on
  `SDL_GPUSamplerCreateInfo`) — this is a rare case in this audit's cross-file sampling where a
  plan-doc limitation claim was checked and found *still correct*, not stale.
  This is a positive engineering-honesty signal, not a defect on its own; recorded here as a
  finding only because it is a genuine, currently-live feature gap a reader of just this test file
  could be misled into thinking is fully covered by "SamplerState proof."
- Why it matters: any XNA game or effect relying on `SamplerState.AnisotropicWrap`/`Clamp` (a
  common choice for ground/floor textures at grazing angles) gets silently downgraded to plain
  point sampling on this backend, with no error or warning — ordinary `Anisotropic` code renders,
  just visibly worse (aliased) than on other backends. This test's 3 checks do not exercise the
  gap, so a future regression fixing it (or a future regression in the Wrap/Clamp/Point/Linear path
  this file *does* cover) would be equally invisible to `SdlGpu_SamplerState`.
- FNA/XNA comparison: FNA/real XNA hardware genuinely supports `TextureFilter.Anisotropic` with a
  real `MaxAnisotropy` texel-footprint effect; this backend's behavior is a documented, intentional
  simplification, not an FNA parity bug per se (the CNA project's own decision to defer it).
- Suggested future action (not implemented by this audit): either extend `SamplerCacheIndex`/
  `GetOrCreateSampler` to thread `maxAnisotropy` through to `SDL_GPUSamplerCreateInfo.
  enable_anisotropy`/`max_anisotropy` (SDL_gpu.h does expose this), or add an explicit companion
  test asserting the current degraded behavior so a future silent change (e.g. accidentally
  enabling but miscalibrating anisotropy) has a regression net.

## Cross-File Observations

- This file and `sdlgpu_shadereffect_test.cpp`/`sdlgpu_skinned_test.cpp` all rely on the same
  `samplerSlots_[MaxSamplers]` capture-at-`ApplySamplerState`/read-at-`Queue*Draw()` convention;
  the convention was independently verified correct at 3 different `Queue*Draw` call sites in this
  audit (`QueueColoredDraw`-adjacent dispatch near line 2124, `QueueDualTextureDraw` line 2894,
  `QueueSkinnedDraw` line 3014) — no divergence found across the 5 direct-3D-draw families that
  read slot 0.
- `plans/plan_sdlgpu.md`'s SDLGPU-21 entry (git commits `4578d403`/`807db24a`, "close SDLGPU-21 --
  ApplySamplerState for direct 3D draws") is this file's sole authoring commit per `git log`; no
  later commit touches `ApplySamplerState`/`GetOrCreateSampler` in a way that would contradict this
  file's header claims — consistent with the "documentation rot" cross-cutting watch-item **not**
  applying here (this is one of the minority of cases in this audit where the plan doc's own
  currently-stated limitation was independently re-verified as still true, rather than found
  stale).
- Unlike the systemic fog-formula and skinned-normal-matrix cross-cutting bugs, this file's feature
  area (per-slot `SamplerState`) has no shared-formula equivalent across backends to compare
  against — N/A for those two specific cross-cutting items.

## Missing or Weak Tests

- See F1 — no coverage for `TextureFilter::Anisotropic` or the 4 mixed min/mag/mip filter values
  (`LinearMipPoint`, `PointMipLinear`, `MinLinearMagPointMip*`, `MinPointMagLinearMip*`), all of
  which this backend currently treats identically to plain `Point`.
  `TextureAddressMode::Mirror` is also untested by this file (only Wrap/Clamp are exercised),
  though `ToAddressMode`'s `case 2: return MIRRORED_REPEAT` (line 37) was read directly and appears
  correctly wired.
- Only slot 0 (and slot 1 for the dual-texture check) is exercised; `SamplerStateCollection.
  MaxSamplers` is larger (per `GraphicsDevice::applySamplerStatesToBackend`'s loop, line 1595) —
  reasonable to leave untested here since no effect in this backend currently reads beyond slot 1.

## Positive Findings

- All 3 checks were independently traced through to genuinely distinct, correctly-wired backend
  code (not inferred from the test's own comments) — the Check C differential in particular is a
  well-designed test: it cannot pass by accident the way a single-slot check could, since the
  "both slots share one sampler" bug class it targets produces the *opposite* wrong answer (Black)
  from the correct one (White), not just a slightly-off value.
- The per-sub-check restore-to-`LinearWrap` discipline (lines 163, 183, 206-207) correctly respects
  real XNA's sticky-`SamplerStates` semantics and prevents cross-check state leakage.
- F1's limitation is a rare example in this audit of a plan-doc claim that is **still accurate**
  when independently re-verified against current source — worth noting as a counter-example to the
  "documentation rot" pattern found elsewhere in this codebase.

## Final Assessment

A precise, well-targeted 3-check test whose claims were independently confirmed against the real
`SdlGpuGraphicsBackend` sampler-dispatch code; no defect found in the file or the code paths it
exercises. The one real gap (Anisotropic/mixed-filter collapse, F1) is an accurately pre-disclosed
backend limitation outside this test's stated scope, not a hidden bug.
