# Audit: examples/webgpu_clear_readback_test.cpp

## Metadata

- Source file: `examples/webgpu_clear_readback_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `GraphicsDevice::GetBackBufferData()`/on-demand-frame-
  submit readback, `SpriteBatch` alpha-blend, and sampler `TextureAddressMode` test, WebGPU backend
  (experimental, per `CLAUDE.md`).
- Test executable: `cna_test_webgpu_clear_readback`, CTest target `WebGPU_Clear_Readback`
  (`cmake/Tests/WebGpuTests.cmake:37-38`).
- XNA/FNA relevance: `GraphicsDevice.GetBackBufferData<T>()`, `SpriteBatch.Draw()` overloads
  (destination rect, source rect, tint colour), `SamplerState.AddressU/AddressV`.
- Related production code: `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`EnsureFrameRendered()` lines 5027+, `ReadBackbuffer()` line 5528, sprite pipeline blend state
  lines 2340-2358, `TextureAddressMode`→`WGPUAddressMode` mapping lines 217-221,
  `TextureFilter`→`WGPUFilterMode` mapping lines 360-373).
- Referenced tasks: WEBGPU-91/92/132 (`plans/plan_webgpu.md`).

## Purpose

Ten checks (labelled A-J in the header) proving: (A/B) `GetBackBufferData()` observes the current
frame's `Clear()` result within the same `Draw()` call, with no stale previous-frame content, via this
backend's on-demand `EnsureFrameRendered()`/`ReadBackbuffer()` submit-before-map design; (C/D) a
`SpriteBatch`-drawn quad affects only its destination rectangle; (E) a fully-transparent sprite is a
true no-op; (F) `sourceRectangle` cropping selects the correct texel, not the whole texture; (G) a
50%-alpha sprite genuinely attenuates colour (the header discloses this check itself found and fixed a
real premultiplied/straight-alpha blend-equation mismatch, WEBGPU-132); (H/I/J) `TextureAddressMode`
Wrap/Clamp/Mirror each produce a distinct, exactly-predictable colour at an out-of-`[0,1)` UV.

## Executive Verdict

**Healthy**, and unusually strong: nearly every check's expected value was independently re-derivable
by hand from the actual current production formulas (address-mode wrapping arithmetic, blend
equation), and every one matched.

## Checklist Results

### API / XNA / FNA parity
`GetBackBufferData(&region, &pixel, 0, 1)` (line 80, `readPixel()`) matches FNA's
`GraphicsDevice.GetBackBufferData<T>(Rectangle?, T[], int, int)` overload shape.
`SpriteBatch::Draw()`'s destination-rect, destination+source-rect, and tint-colour overloads (lines
138, 149, 158, 192, 207, 222) are all exercised, plus the `Begin(SpriteSortMode, BlendState,
SamplerState*, …)` overload (lines 191, 206, 221) used specifically to inject a non-default
`SamplerState` per check H/I/J — correct FNA `SpriteBatch.Begin` overload usage for that purpose.

### Behavioral correctness
Re-derived checks H/I/J's expected sample points and colours directly from the actual UV math
(`sourceRectangle=(0,0,4,1)` against a real 2-texel-wide `redBlueTex_`, destination the full 64px
width, so `u` genuinely spans `[0,2)`, matching the header's own claim that `SpriteBatch` never clamps
UV to the source rectangle):
- Check H (x=36): pixel-center UV `= (36+0.5)/64 × (4/2) = 1.1406`. `Wrap` uses `fract(u)=0.1406`,
  inside the red texel's `[0,0.5)` half → **red**. Matches `kExpected=Color::Red` (line 195).
- Check I (x=36, `Clamp`): same `u=1.1406 ≥ 1.0` clamps to the last texel (blue). Matches
  `Color::Blue` (line 210).
- Check J (x=56, `Mirror`): pixel-center UV `= (56+0.5)/64 × 2 = 1.7656`, inside `[1,2]` which
  mirrors `[0,1]` reversed → `u' = 2-1.7656 = 0.2344`, inside the red texel's half → **red**, while
  the header's own claim that Wrap/Clamp would both read blue at this same `u` is also independently
  confirmed (`fract(1.7656)=0.7656`→blue for Wrap; `≥1.0` clamps to blue for Clamp) — the check is
  genuinely discriminating, not just a third assertion that happens to also pass under Wrap/Clamp
  semantics. Matches `Color::Red` (line 225).
All three derivations independently confirm the address-mode mapping
(`case 0→Repeat/Wrap, case 2→MirrorRepeat, default→ClampToEdge`, `WebGPUGraphicsBackend.cpp` lines
219-221) is wired correctly and that `TextureFilter::Point` (line 366: `case 1 → Nearest`) genuinely
disables bilinear blur at the texel seam, which is what makes an exact (not merely "close") colour
assertion valid here.

### Logic
Confirmed the WEBGPU-132 blend-equation fix this file's own header (lines 20-26) claims is actually
present in current source: `WebGPUGraphicsBackend.cpp` lines 2348-2354 pairs
`srcFactor=WGPUBlendFactor_SrcAlpha`/`dstFactor=WGPUBlendFactor_OneMinusSrcAlpha` for colour (a
straight-alpha "over" blend) with an adjacent comment explicitly describing the prior, buggy
`One`/`OneMinusSrcAlpha` (premultiplied-equation) pairing that silently ignored partial alpha — this
is not a stale claim, the fix is genuinely in the code the test exercises, cross-checked against
`plans/plan_webgpu.md`'s WEBGPU-132 entry which independently corroborates the same before/after description.

### Robustness
Check E (alpha=0 must be a true no-op) and check G (alpha=128 must land strictly between black and
full colour) together are the correct pair of checks to catch exactly the WEBGPU-132 bug class: a
premultiplied/straight mismatch happens to still produce correct results at the two boundary alphas
(0 and 255) — the checks in this file explicitly note this (header lines 22-26) and check G is the one
that would have failed pre-fix. Check G's own comment (lines 163-176) is honest that the exact
resulting value is swapchain-format/blend-space-dependent and asserts direction/magnitude
(`60<=G<=220`, `R,B<=20`) rather than a precise value — an appropriately wide but not meaninglessly
wide tolerance (it still excludes both "no attenuation" (~255) and "fully discarded" (~0)).

### Testing
Ten checks, each verifying a genuinely distinct code path (on-demand frame submission twice in a row
for A/B, partial-coverage geometry for C/D, an alpha-gating edge case for E, source-rect cropping for
F, a real historical bug's regression check for G, and three-way sampler address-mode discrimination
for H/I/J). No redundant or metadata-only checks found — every check reads back actual rendered pixel
content, not a capacity/state getter.

### Architecture
`EnsureFrameRendered()` (lines 5027+) is the shared on-demand-submit mechanism this file's checks A/B
specifically probe — confirmed it correctly branches on `hasAcquiredTexture_`/`framePending_` so a
same-`Draw()`-call `Clear()`+readback sequence forces a real GPU submission rather than only being
observable on the next real `Present()`, matching this file's own stated purpose.

## Detailed Findings

None at HIGH or above.

## Cross-File Observations

- Corroborates (rather than merely repeats) `plans/plan_webgpu.md`'s WEBGPU-132 entry: both sources describe
  the same bug, the same fix, and the same "value depends on blend colour-space" caveat, with this
  audit independently re-deriving the blend-factor pairing from the actual current source rather than
  taking the plan doc's claim at face value.
- This file uses only 2D `SpriteBatch` draws — no `BasicEffect`/`SkinnedEffect`/3D lighting — so the
  cross-cutting skinned-normal-transform and fog-formula bugs this audit is watching for do not apply
  to any code path this file exercises.

## Missing or Weak Tests

- Check G's necessarily-wide tolerance band means a regression that shifted the blend result by a
  moderate amount within `[60,220]` (e.g. a genuine but smaller-magnitude blend-space error) would not
  be caught — an acceptable trade-off given the legitimate backend/format-dependent ambiguity the
  header discloses, not a defect.
- No check exercises `AddressMode` values on the `V` axis independently from `U` (all three of H/I/J
  set both `AddressU`/`AddressV` identically and sample along a single row) — a minor, low-priority
  coverage gap given `U`/`V` share the same enum and almost certainly the same code path
  (`ToWGPUAddressMode()` is axis-agnostic).

## Positive Findings

- Three separate numeric derivations (H/I/J) were independently reproduced by this audit from the raw
  UV/wrap-mode arithmetic and all matched the asserted colours exactly — a genuinely verified, not
  merely plausible, test.
- Check G is a rare example in this codebase of a test whose own header transparently documents that
  it caught a real, previously-shipped bug (WEBGPU-132), with the "before" and "after" behaviour both
  described precisely enough to be independently checked against `git`/plan-doc history, which this
  audit did.

## Final Assessment

A strong, thoroughly cross-checked test file. Every numerically-asserted check was independently
re-derived against the current production formulas and found correct; the one real historical bug it
was written to catch (WEBGPU-132) is confirmed fixed in the current source, not merely claimed fixed by
a stale comment.
