# Audit: examples/bgfx_dualtextureeffect_null_texture2_test.cpp

## Metadata

- Source file: `examples/bgfx_dualtextureeffect_null_texture2_test.cpp` (163 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `DualTextureEffect.Texture2` (slot 1) null-texture
  fallback verification, Bgfx backend. Per the file's own header comment, this test's authoring
  **found and fixed a real bug**.
- CTest registration: `cna_bgfx_test(cna_test_bgfx_dualtextureeffect_null_texture2 …)` /
  `cna_register_backend_test(NAME Bgfx_DualTextureEffect_NullTexture2 …)`
  (`cmake/Tests/BgfxTests.cmake:414-416`).
- XNA/FNA relevance: indirect/`NOXNA`-adjacent, same rationale as
  `bgfx_dualtextureeffect_null_texture0_test.cpp`'s report.
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`texColor3DSampler2_` slot-1 handling, lines 2465-2478 and 2432-2445 for the vertex-color
  variant).

## Purpose

Structurally identical to `bgfx_dualtextureeffect_null_texture0_test.cpp` but targeting slot 1
(`Texture2`) instead of slot 0: a first draw with a distinctive `Texture2` (20,200,20) establishes
stale-state bait, then a second draw sets `Texture2=nullptr` — asserting the result both matches
the white-fallback color and does not match the distinctive first-draw color. Per the header
comment, prior to Task 387's fix, `texColor3DSampler2_` had **no fallback at all**: a null
`Texture2` draw silently left whatever texture the *previous* draw's slot 1 had bound, exactly the
stale-binding failure mode this test's two-draw structure is designed to catch.

## Executive Verdict

**Healthy** — the fix this test locks in was independently confirmed present in the current
production source (both the `if`/`else` fallback structure and its symmetry with slot 0's earlier
fix), and the expected pixel value is an exact re-derivation match.

## Checklist Results

### Behavioral correctness
Confirmed in `BgfxGraphicsBackend.cpp` (lines 2465-2478, within the non-vertex-color dual-texture
branch): `if (bgfx::isValid(texColor3DSampler2_)) { if (params.texture1) {...} else {
bgfx::setTexture(1, texColor3DSampler2_, defaultWhiteTexture3D_, samplerFlags_[1]); } }` — this is
present and mirrors slot 0's fallback exactly, as the comment claims ("adding an else-branch
mirroring slot 0's own Task 379 fix exactly"). The equivalent block for the vertex-color-enabled
variant (`dualTextureColored3DProgram_` path, lines 2432-2445) has the identical fallback,
confirming the fix was applied to both dispatch branches, not just one.
Re-derived the expected pixel: `Texture=kTex(80,40,120)/255=(0.3137,0.1569,0.4706)`,
`Texture2=null → white(1,1,1)` fallback, `DiffuseColor` defaults to white. `base.rgb =
(0.3137,0.1569,0.4706)*2 = (0.6275,0.3137,0.9412)`; `color = (0.6275,0.3137,0.9412) * (1,1,1) *
(1,1,1) = (0.6275,0.3137,0.9412) → (160.0,80.0,240.0)` — matches the asserted
`Color(160,80,240,255)` (lines 136-138) exactly, and (as expected by the doubling-formula's
commutativity noted in the slot-0 report) is numerically identical to slot 0's own expected value
despite the roles of "which slot is null" being swapped.

### Logic
Same two-draw distinctive-texture technique as the slot-0 sibling; specifically well-suited to
catching *this exact* class of bug (the one Task 387 actually found), since a naive
"unconditionally skip the sampler binding call when null" implementation (as slot 1 apparently was,
pre-fix) would have produced exactly the "stale texture from the previous draw" failure this test's
negative assertion (`!colourMatch(got, kDistinctivePrev)`, line 139-141) is built to detect.

### Robustness
Same `RasterizerState::CullNone` requirement and retry-loop pattern as the rest of this shard.

### Testing
The historical framing ("REAL BUG FOUND AND FIXED HERE" in the header) was corroborated by
locating the described fix in the current source and by the git-log-visible progression from
`ab26c591 fix(Task 387): DualTextureEffect second texture null fallback missing on Bgfx` —
consistent with the comment's account rather than an unverified claim.

## Detailed Findings

None. The specific fix this test verifies was independently located, confirmed correct, and
confirmed applied consistently across both the plain and vertex-color-enabled dual-texture
dispatch branches.

## Cross-File Observations

- See `bgfx_dualtextureeffect_null_texture0_test.cpp`'s audit report for the corresponding slot-0
  analysis and the explanation of why both files legitimately assert the identical expected pixel
  value `(160,80,240)`.
- This file is a good example of the shard's general pattern: a "verify" test for slot 0
  (zero bugs expected, confirmed already-fixed) paired with a "found and fixed" test for slot 1
  (a genuine, git-log-confirmed regression) — the two together give complete coverage of both
  texture slots' null-handling without duplicating each other's actual bug-catching value.

## Missing or Weak Tests

None specific to this file. As with the slot-0 sibling, a combined "both slots null
simultaneously" case is not separately tested but is low-value given the two fallback code paths
are textually and logically independent (`if (bgfx::isValid(texColor3DSampler_))` and `if
(bgfx::isValid(texColor3DSampler2_))` are two separate, non-interacting blocks).

## Positive Findings

- The header comment's claim of a real, previously-shipped bug (not just a hypothetical or
  defensively-added fallback) was independently corroborated against both the current source and
  git log, rather than taken at face value.
- The fix is confirmed applied to *both* relevant dispatch branches (plain and vertex-color dual
  texture), not just the one this specific test happens to exercise (`VertexPositionTexture`, no
  vertex color) — reducing the risk that the vertex-color variant was left with the old bug.

## Final Assessment

A well-targeted regression test whose "real bug found and fixed" framing is corroborated by both
the current source and git history; the expected pixel value is an exact match to the current
formula and the negative stale-binding assertion is a genuine, non-redundant check.
