# Audit: examples/bgfx_sprite_effects_test.cpp

## Metadata

- Source file: `examples/bgfx_sprite_effects_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SpriteBatch::Draw`'s `SpriteEffects::FlipHorizontally`/
  `FlipVertically` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_sprite_effects …)` / `cna_register_backend_test(NAME Bgfx_SpriteEffects_Flip …)`,
  `cmake/Tests/BgfxTests.cmake:792-794`).
- XNA/FNA relevance: direct — `Microsoft::Xna::Framework::Graphics::SpriteEffects` and
  `SpriteBatch::Draw(..., SpriteEffects, float layerDepth)`.
- FNA reference: `HLSL`/`SpriteBatch.cs` — `effects` is masked to `(SpriteEffects)0x03` and folded into the
  per-vertex texture-coordinate table (`SpriteBatch.cs` line ~464, `(int)(effects & (SpriteEffects)0x03)`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`pushSprite`/`flushSingle`),
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`SubmitSprite`, lines 1421-1529, flip logic at
  1448-1455).

## Purpose

8-check pixel test (Task 807, a Bgfx port of Task 167's EasyGL/Vulkan test) proving `FlipHorizontally` and
`FlipVertically` genuinely mirror the *sampled texture region*, not just a no-op or a destination-rectangle
mirror. Uses two 2-texel textures with an unambiguous color split (2×1 `[Red|Blue]` for the horizontal case,
1×2 `[Red/Blue]` for the vertical case) and `SamplerState::PointClamp` so every screen pixel maps to exactly
one texel, eliminating any bilinear-blur ambiguity at the check points. Each of the 4 sections (no-flip-H,
flip-H, no-flip-V, flip-V) is read back via its own fully independent `Clear`+`Begin`/`Draw`/`End`+
`GetBackBufferData` pass (`RunCheck`, lines 67-92), explicitly because Bgfx's `GetBackBufferData` only reliably
reflects the first read per rendered frame (the file's own comment cites "Task 406").

## Executive Verdict

**Healthy** — the flip logic in `BgfxGraphicsBackend::SubmitSprite` (a simple `std::swap(u1,u2)` /
`std::swap(v1,v2)` on the already-computed UV extents, lines 1448-1455) is straightforward, correctly gated on
the `SpriteEffects` bitmask, and this audit's own trace of all 8 check points against that logic produces
exactly the colors the test expects. `SamplerState::PointClamp`'s filter value (`TextureFilter::Point = 1`) was
independently confirmed to map to `BGFX_SAMPLER_MIN_POINT|MAG_POINT|MIP_POINT` (`BgfxGraphicsBackend.cpp:1904-1908`),
so the test's own stated precondition ("no bilinear ambiguity") genuinely holds in this backend.

## Checklist Results

### API / XNA / FNA parity
`SpriteEffects::None/FlipHorizontally/FlipVertically` used via the
`Draw(texture, destRect, srcRect, color, rotation, origin, effects, layerDepth)` overload — this exact overload
signature matches FNA's `SpriteBatch.Draw(Texture2D, Rectangle, Rectangle?, Color, float, Vector2,
SpriteEffects, float)`. The test does not exercise `SpriteEffects::FlipHorizontally | FlipVertically` combined
(bit `3`) — a legitimate, if minor, coverage gap (see Missing Tests).

### Behavioral correctness
Traced `SubmitSprite`'s texture-coordinate computation for each of the 8 checks:
- S0 (no flip, 2×1 `[Red|Blue]`): `u1=0/2=0, u2=2/2=1`, no swap ⇒ `x<50%` samples near `u1=0` (Red),
  `x>50%` near `u2` (Blue) — matches checks "S0-left→Red" (x=25) / "S0-right→Blue" (x=75) within the section's
  own `[0,100)` destination range.
- S1 (`FlipHorizontally`, same texture): `std::swap(u1,u2)` ⇒ `u1=1, u2=0` — left half of the destination now
  samples near the *swapped* `u1=1` (i.e. the texture's right/Blue texel), right half samples near `u2=0`
  (Red) — matches "S1-left→Blue" / "S1-right→Red".
- S2/S3 (vertical, `1×2 [Red/Blue]`): identical reasoning on `v1`/`v2`, confirmed matching "S2-top→Red /
  S2-bot→Blue" and "S3-top→Blue (FlipV) / S3-bot→Red".
- All 8 check coordinates fall safely inside their half (25%/75% of a 100px section), well clear of the exact
  midpoint, so no off-by-one edge-sampling ambiguity affects the assertions.

### Robustness
`colourMatch`'s `tol=60` is generous but appropriate here — the two colors being discriminated (pure Red vs.
pure Blue) differ by 255 in two channels simultaneously, so a tolerance of 60 cannot conflate them; this is not
a "passes by accident" situation like the sibling EasyGL specular test's stale-constant issue this audit was
primed to look for.

### Testing
Each of the 4 sections gets 2 independent checks (one per half), correctly discriminating "flip did nothing"
from "flip mirrored correctly" — a naive no-op flip implementation would fail exactly the flipped-section
checks while still passing the no-flip section's checks, which is the right structure to catch a regression
that silently drops the `SpriteEffects` parameter.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. One minor coverage gap, not a defect:

### F1 — `SpriteEffects::FlipHorizontally | FlipVertically` (both bits set) is never exercised
- Severity: LOW
- Confidence: HIGH (file inspected in full; only `None`, `FlipHorizontally`, `FlipVertically` appear in the
  `checks[]` table, lines 124-133)
- Category: test-coverage
- Location/symbol: `checks[]` array, lines 124-133
- Evidence: `SubmitSprite`'s flip logic (lines 1448-1455) uses two independent `if` blocks (one per axis), so
  the combined-bits case is very likely correct by construction (each `if` is independently gated on its own
  bit via bitwise AND) — but no test in this file (or, per a supplementary grep, in the sibling EasyGL/Vulkan
  originals this file explicitly reuses) actually renders with both bits set and checks all 4 texel corners.
- Why it matters: low risk given the independent-per-axis implementation, but a future refactor that replaced
  the two `if`s with a single `switch` over the 4 `SpriteEffects` values could regress the combined case
  without any current test catching it.
- FNA/XNA comparison: FNA's own `effects & (SpriteEffects)0x03` masking (`SpriteBatch.cs`) explicitly supports
  the combined-bits value as a valid input (both flips applied together is a legal call), so this is a genuine
  gap in the ported test family, not an XNA/FNA-only combination CNA has no obligation to support.
- Suggested future action (not implemented by this audit): extend this test (or add a 9th check) with
  `SpriteEffects::FlipHorizontally | SpriteEffects::FlipVertically` on a 2×2 four-color texture.

## Missing or Weak Tests

See F1. Otherwise this file's own scope (proving the flip axis and direction, not every combination) is
adequately covered.

## Positive Findings

- The `RunCheck`-per-region restructuring (vs. the original EasyGL/Vulkan single-frame, multi-read design) is
  correctly and consistently applied to accommodate Bgfx's documented readback quirk, and does not alter the
  logical assertions being made.
- `SamplerState::PointClamp`'s effect was independently verified against the actual `SetSamplerFilter`
  implementation rather than assumed from the header comment's claim alone.
- The test's own header comment accurately describes the section layout and expected colors; this audit's
  independent trace through `SubmitSprite` reproduced every one of the 8 expected outcomes without needing to
  correct the comment.

## Final Assessment

A correctly-designed, genuinely discriminating test for both flip axes on the Bgfx backend; production logic
and test expectations agree in every traced case, with only a minor (combined-bits) coverage gap that mirrors
a project-wide gap rather than being specific to this file.
