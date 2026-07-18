# Audit: examples/bgfx_texture_address_mode_test.cpp

## Metadata

- Source file: `examples/bgfx_texture_address_mode_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — Task 750, `SpriteBatch`-driven `SamplerState.AddressU`/
  `AddressV` (`Wrap` vs `Clamp`) on Bgfx
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_texture_address_mode …)` /
  `cna_register_backend_test(NAME Bgfx_TextureAddressMode …)`, `cmake/Tests/BgfxTests.cmake:432-435`).
- XNA/FNA relevance: direct — `SamplerState.AddressU`, `SpriteBatch::Begin`'s `samplerState`
  parameter, `SamplerState::PointWrap`/`PointClamp` static presets.
- FNA reference: `Graphics/States/TextureAddressMode.cs`, `Graphics/States/SamplerState.cs`
  (`PointWrap`/`PointClamp` static presets).
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`ApplySamplerState`, lines 1890-1939); `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`'s
  `ISpriteBatchBackend` base (the default no-op `SetSamplerFilter`/`SetSamplerAddressMode` this file's
  own header comment says was previously silently accepted).

## Purpose

Simplest member of this shard's texture-address-mode family: draws a 2×1 (Red|Blue) texture with a
`sourceRectangle` twice the texture's width (U spans `[0,2]`), samples once at `U=1.25` (`5/8` of the
viewport width), and asserts `SamplerState::PointWrap` reads Red (`fract(1.25)=0.25` → left texel)
while `SamplerState::PointClamp` reads Blue (clamps to the last/right texel). Its own header comment
documents the real, now-fixed bug this test targets: before this task, `BgfxSpriteBatchBackend` never
overrode `SetSamplerFilter`/`SetSamplerAddressMode` at all, so `SpriteBatch::Begin()`'s `SamplerState`
argument had zero effect on Bgfx — the same bug shape already independently found and fixed on EasyGL
(Task 269) and Vulkan (Task 665).

## Executive Verdict

**Healthy** — the `U=1.25` math for both `Wrap` and `Clamp` was independently re-derived and confirmed
correct, and the claim that this exercises a previously-real (now-fixed) bug is independently
corroborated by the production `ApplySamplerState` code and by this shard's sibling `_mirror_test.cpp`
file crediting the identical Task 750 fix.

## Checklist Results

### API / XNA / FNA parity
`SamplerState::PointWrap`/`PointClamp` are genuine FNA static presets
(`SamplerState.cpp` lines 10-11: `PointClamp = {Point, Clamp,Clamp,Clamp}`,
`PointWrap = {Point, Wrap,Wrap,Wrap}`); `SpriteBatch::Begin(SpriteSortMode, BlendState, SamplerState*,
DepthStencilState*, RasterizerState*, Effect*)` (the 6-argument overload used at line 75) matches
FNA's corresponding `SpriteBatch.Begin` overload's parameter shape and order.

### Behavioral correctness
Re-derived the sample math: source rectangle `(0,0,4,1)` against a 2-texel-wide texture gives U range
`[0, 4/2] = [0,2]` across the full-viewport destination `(0,0,W,H)`; sampling at `x = W*5/8` maps to
`U = 2 * (5/8) = 1.25`. `PointWrap`: `fract(1.25) = 0.25` → left half of `[0,1]` → texel 0 (Red) —
matches `wrapPass` expecting `(255,0,0)`. `PointClamp`: any `U>1` clamps to the texture's last texel
(texel 1, Blue) — matches `clampPass` expecting `(0,0,255)`. Both independently confirmed correct
against `TextureAddressMode`'s documented semantics (`Wrap`: tile at every integer junction; `Clamp`:
saturate to the 0.0/1.0 texel).

### Logic
`SampleAtUOnePointTwoFive` (lines 63-84) retries up to 20 times per call but — unlike every other file
in this batch — breaks on `pixel.R > 10 || pixel.G > 10 || pixel.B > 10` (line 80) rather than
`!= 0`; functionally equivalent for this test's fully-saturated Red/Blue expected values, but a
slightly different (looser) threshold than the shared `!=0` idiom used elsewhere in the shard — worth
noting only as a minor inconsistency, not a defect (see F1).

### Cross-file consistency
The bug this test targets (`ISpriteBatchBackend`'s shared no-op `SetSamplerFilter`/
`SetSamplerAddressMode` defaults) was independently confirmed still documented as the reason this test
exists: `ApplySamplerState`'s real address-mode mapping (`BgfxGraphicsBackend.cpp` lines 1933-1937,
`addressU==1` → `BGFX_SAMPLER_U_CLAMP`) is the fix this test now exercises, matching the header
comment's claim precisely.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Retry-break threshold (`>10`) differs from the shared `!=0` idiom used by every sibling file in this batch

- Severity: LOW
- Confidence: HIGH
- Category: maintainability / consistency
- Location/symbol: `SampleAtUOnePointTwoFive`, line 80:
  `if (pixel.getRProperty() > 10 || pixel.getGProperty() > 10 || pixel.getBProperty() > 10) break;`
- Evidence: every other file in this batch (mirror, anisotropic, point-vs-linear, mip-filter,
  split-minmag, transform-matrix, spritefont) uses the stricter `!= 0` break condition for the
  identical Bgfx "first read per rendered frame" retry pattern. This file alone uses `>10`.
- Why it matters: functionally harmless for this test's own fully-saturated expected colors
  (255 or 0 per channel — never a value that would fall in the `(0,10]` gap between the two
  thresholds), but the inconsistency means a reader cross-referencing this file against its siblings
  (as this audit did) has to separately verify each threshold choice doesn't silently change behavior,
  rather than being able to assume one shared, uniform retry idiom across the shard.
- Related files: every other `bgfx_*_test.cpp` file using the `RunCheck`-style retry loop.
- Suggested future action: none required for correctness; a purely cosmetic unification would align
  this file's threshold with the shard-wide `!=0` convention if this file is touched again.

## Cross-File Observations

- This is the "base case" of the address-mode family in this shard: `bgfx_texture_address_mode_test.cpp`
  (`Wrap` vs `Clamp` only, `U=1.25`) vs. `bgfx_texture_address_mode_mirror_test.cpp` (adds `Mirror`,
  `U=1.6`, three-way comparison) — the two files are complementary, not duplicative; the mirror test's
  own header comment explicitly cites this file's `U=1.25` point as the reason it needed a different
  sample point.
- Both this file and its EasyGL/Vulkan ancestors (Task 269/665, cited in the header) independently
  found and fixed the identical bug shape (`SetSamplerFilter`/`SetSamplerAddressMode` silently
  no-op'd) in each backend — a recurring bug pattern across all three backends' `ISpriteBatchBackend`
  implementations, now closed on all three per the task history.

## Missing or Weak Tests

None found for this file's narrow, single-sample-point scope; the `Mirror` case is correctly deferred
to the sibling `_mirror_test.cpp` file rather than duplicated here.

## Positive Findings

- Both expected pixel values (`Wrap`→Red, `Clamp`→Blue at `U=1.25`) were independently re-derived from
  `TextureAddressMode`'s documented semantics and confirmed correct.
- Correctly attributes its own existence to a real, previously-fixed silent-no-op bug rather than an
  invented scenario — corroborated by the production `ApplySamplerState` code actually handling
  address modes today.

## Final Assessment

A small, focused, and behaviorally-correct test; its only notable trait is a minor, harmless
inconsistency in its retry-loop break threshold relative to the rest of the shard (F1), not a
correctness defect.
