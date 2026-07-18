# Audit: examples/bgfx_texturefilter_split_minmag_test.cpp

## Metadata

- Source file: `examples/bgfx_texturefilter_split_minmag_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — Task 743, `SamplerState` → `BGFX_SAMPLER_*` flag mapping
  completeness for the 6 split Min/Mag/Mip `TextureFilter` values
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_texturefilter_split_minmag …)` /
  `cna_register_backend_test(NAME Bgfx_TextureFilter_SplitMinMag …)`,
  `cmake/Tests/BgfxTests.cmake:106-109`).
- XNA/FNA relevance: direct — all 9 `TextureFilter` enum values, `SamplerState.Filter`,
  `DualTextureEffect`.
- FNA reference: `Graphics/States/TextureFilter.cs` (9-value enum, `Linear=0` through
  `MinPointMagLinearMipPoint=8`).
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`ApplySamplerState`, lines 1890-1939 — this file's own header comment states this test was written
  *because of* a real bug this task found in this exact function).

## Purpose

Verifies `BgfxGraphicsBackend::ApplySamplerState`'s filter-value switch correctly distinguishes all 6
XNA `TextureFilter` values that encode a *split* Min/Mag/Mip combination (`LinearMipPoint`,
`PointMipLinear`, `MinLinearMagPointMipLinear`, `MinLinearMagPointMipPoint`,
`MinPointMagLinearMipLinear`, `MinPointMagLinearMipPoint`) — not just the 3 "flat" values
(`Linear`/`Point`/`Anisotropic`) already covered by sibling tests. The file's own header comment
documents the real bug this task found: before the fix, all 6 split values fell through to the
`default` (plain Linear, no flags) branch in `ApplySamplerState`, silently discarding the Min/Mag
distinction entirely — the same bug class already fixed on EasyGL, used here as the reference mapping
to derive the correct bgfx per-axis flag combination for each of the 6 previously-broken values.

## Executive Verdict

**Healthy** — this audit independently re-derived the correct MIN/MAG/MIP flag combination for all 6
filter values from the XNA naming convention (`Min<X>Mag<Y>Mip<Z>`) and confirmed the production
`ApplySamplerState` switch (lines 1912-1929) matches exactly, case by case; the test's own
Mag-only assertions were independently confirmed to match those same 6 derivations.

## Checklist Results

### API / XNA / FNA parity
Confirmed `TextureFilter.cs`'s 9-value enum order (`Linear=0, Point=1, Anisotropic=2,
LinearMipPoint=3, PointMipLinear=4, MinLinearMagPointMipLinear=5, MinLinearMagPointMipPoint=6,
MinPointMagLinearMipLinear=7, MinPointMagLinearMipPoint=8`) matches CNA's
`include/Microsoft/Xna/Framework/Graphics/TextureFilter.hpp` exactly, and matches the numeric case
labels used in `ApplySamplerState`'s switch statement's own inline comment (`BgfxGraphicsBackend.cpp`
lines 1896-1898).

### Behavioral correctness
Independently re-derived each of the 6 split values' Min/Mag/Mip components from the XNA naming
convention and cross-checked against the actual `ApplySamplerState` switch (lines 1912-1929):

| Filter value | Min | Mag | Mip | Flags set (verified) |
|---|---|---|---|---|
| `LinearMipPoint` | Linear | Linear | Point | `MIP_POINT` only — correct (Min/Mag default to Linear) |
| `PointMipLinear` | Point | Point | Linear | `MIN_POINT\|MAG_POINT` — correct (Mip defaults to Linear) |
| `MinLinearMagPointMipLinear` | Linear | Point | Linear | `MAG_POINT` only — correct |
| `MinLinearMagPointMipPoint` | Linear | Point | Point | `MAG_POINT\|MIP_POINT` — correct |
| `MinPointMagLinearMipLinear` | Point | Linear | Linear | `MIN_POINT` only — correct |
| `MinPointMagLinearMipPoint` | Point | Linear | Point | `MIN_POINT\|MIP_POINT` — correct |

All 6 rows independently confirmed correct — every case sets exactly the flags implied by its own
name and no others, resolving the exact bug class (silent fallthrough to plain Linear) the header
comment describes.

The test's own `checks[6]` table (lines 160-167) asserts only the **Mag** component of each value
(`magIsPoint` → `IsPure` if Point, `IsBlended` if Linear) against a full-screen-stretched (i.e.,
magnified) 2-texel Red|Green texture — independently confirmed each of the 6 `magIsPoint` values in
the test table matches the derivation above exactly (`LinearMipPoint`→false/blend, `PointMipLinear`→
true/pure, `MinLinearMagPointMipLinear`→true/pure, `MinLinearMagPointMipPoint`→true/pure,
`MinPointMagLinearMipLinear`→false/blend, `MinPointMagLinearMipPoint`→false/blend).

### Logic
The file's own comment (lines 16-22) correctly acknowledges this test cannot independently observe the
Mip component (no real generated mip chain exists for `CreateFromPixels` textures by convention), but
correctly argues every one of the 6 values differs from at least one "neighbor" in its Mag component —
independently verified true from the derivation table above (no two of the 6 rows share an identical
Mag value alongside an identical "which case fires" ambiguity), so the claim "fully discriminates 'the
split is applied at all' from 'everything collapses to Linear'" holds for the specific previously-broken
symptom (blanket fallthrough to `default`), though it does not fully discriminate a hypothetical
*different* new bug that scrambled the Mag bit specifically while leaving Min/Mip alone in a way that
happened to still match — a residual, low-probability gap inherent to testing only 1 of 3 axes (see F1).

### Cross-file consistency
`RunCheck` (lines 86-134) follows the shard's established per-check-fresh-frame retry pattern; shares
the `DiffuseColor=0.5` Task-383 doubling-compensation idiom verbatim with
`bgfx_texture_filter_point_vs_linear_test.cpp` (same batch), both independently confirmed correct
against FNA's `DualTextureEffect.fx` `color.rgb *= 2` in the sibling file's own audit.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Test verifies only the Mag axis; a hypothetical Min-axis-only or Mip-axis-only regression in these 6 cases would not be caught here

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `checks[6]` (lines 160-167); `RunCheck`'s single Mag-sensitive sample point
  (full-screen-stretched 2-texel texture, always magnified, line 127 comment "texel boundary (u=0.5)")
- Evidence: the file's own header comment explicitly discloses this as a known, deliberate scope
  limit ("the Mip half can't be independently observed without a real generated mip chain… this test
  verifies the Mag-filter half only"), and the Min axis is never independently exercised either (no
  minification draw is performed in this file — that is the sibling
  `bgfx_texture_filter_point_vs_linear_test.cpp`'s job for the flat `Point`/`Linear` values only, not
  for these 6 split values).
- Why it matters: a regression that, say, swapped the `MIN_POINT` and `MAG_POINT` bits for exactly one
  of the 6 cases while coincidentally preserving that case's correct Mag-observable behavior (e.g., a
  bug that also set `MIN_POINT` for `MinLinearMagPointMipLinear`, which already expects `MAG_POINT`
  correctly) would not be caught by this file, since Min is never independently sampled (would require
  a minification draw, which this file's full-screen-quad-stretch geometry never performs for these
  6 values). This is an honestly-scoped limitation given the real constraint (no default mip chain to
  observe Mip; no minification draw present to observe Min), not a hidden defect.
- Related files: `bgfx_texture_filter_point_vs_linear_test.cpp` (magnification+minification for the 3
  flat values only), `bgfx_texture_mip_filter_effect_test.cpp` (mip-level selection, but only for
  `LinearMipPoint`/`Point`, not all 6 split values).
- Suggested future action: a follow-up test adding a minification draw (mirroring
  `bgfx_texture_filter_point_vs_linear_test.cpp`'s 256-texel texture technique) for these same 6 split
  values would close the Min-axis gap; not implemented by this audit.

## Cross-File Observations

- This file, `bgfx_texture_anisotropic_effect_test.cpp`, and
  `bgfx_texture_address_mode_mirror_test.cpp` were all landed together in commit `5f431f14`
  ("fix(Task 743): complete Bgfx SamplerState split Min/Mag/Mip filter mapping"), whose own commit
  message states it "Closes the entire Phase 72 Foundational (741-742) and SamplerState/
  TextureAddressMode (743-749) row groups" — independently confirmed via `git log --oneline --follow`
  that this file has exactly one commit in its history, consistent with being newly authored as part
  of that batch closure rather than a suspicious single-commit anomaly.
- The bug this file targets and the fix it verifies were independently confirmed by reading
  `ApplySamplerState` directly, not merely trusted from the header comment or the commit message.

## Missing or Weak Tests

See F1 — the Min-axis and Mip-axis components of these 6 filter values remain unverified by direct
observation in this specific file, an honestly-disclosed and reasonably-justified scope limit rather
than an oversight.

## Positive Findings

- Independently re-derived all 6 filter values' correct Min/Mag/Mip flag combinations from first
  principles (the XNA naming convention) and confirmed the production switch statement matches exactly,
  case by case — not merely trusted from the file's or the production code's own comments.
- The test's own Mag-only expectation table was cross-checked against that same independent derivation
  and found to match in all 6 rows.
- Honestly and explicitly discloses its own Mip-axis (and, by omission, Min-axis) blind spot rather
  than overclaiming full 3-axis coverage.

## Final Assessment

A correctly-targeted regression test for a real, previously-confirmed bug (6 split `TextureFilter`
values silently collapsing to plain Linear); its Mag-only assertions were independently verified
correct against both the XNA naming convention and the live production switch statement. The one
noted gap (Min/Mip axes unobserved, F1) is a low-severity, honestly-scoped limitation rather than a
correctness defect.
