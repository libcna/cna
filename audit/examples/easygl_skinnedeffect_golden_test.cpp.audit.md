# Audit: examples/easygl_skinnedeffect_golden_test.cpp

## Metadata

- Source file: `examples/easygl_skinnedeffect_golden_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 469, golden-image consumer for the Task 409
  `SkinnedEffect` capstone scene
- File type: C++ example/integration test, built on the shared `CNA::Examples::PixelTestGame`
  base (`examples/common/PixelTestGame.hpp`)
- Related production code: `SkinnedEffect.hpp`/`.cpp`, `PixelTestGame::ExpectPixel()`/
  `CompareGoldenImage()` (Task 463), `Texture2D::FromStream`/`SaveAsPng`
- XNA/FNA relevance: exercises real XNA `SkinnedEffect` API (`SetBoneTransforms`,
  `EnableDefaultLighting`), same as its `easygl_skinnedeffect_combined_test.cpp` sibling.
- Main related tests: explicitly reuses only "quad A" (the identity-bone case) from
  `easygl_skinnedeffect_combined_test.cpp`'s 3-quad scene, per this file's own header.

## Purpose

Re-verifies the same identity-bone `SkinnedEffect` scene as Task 409's combined test, but via
`PixelTestGame::CompareGoldenImage()` (an 8x8-pixel-region PNG comparison) rather than Task 409's
own hand-rolled "red-dominant" predicate — plus a supplementary `ExpectPixel` single-pixel
cross-check against a previously live-observed value. Placement/naming (`*_golden_test.cpp`
alongside a checked-in `examples/golden/*.png`) matches this shard's established golden-image test
convention (per `PixelTestGame.hpp`'s own Task 463 documentation).

## Executive Verdict

**Healthy**, with one disclosed, deliberate weakening of assertion rigor worth surfacing plainly
(Finding F1): the `ExpectPixel` cross-check's expected value is not analytically derived (unlike
most of this shard's other checks) but captured from a single prior live run — a materially weaker
form of verification that the file's own header is honest about, but which still deserves flagging
per this audit's job of judging whether tests validate real semantics vs. just "matches whatever
ran once."

## Checklist Results

### API / XNA / FNA parity
`SkinnedEffect::SetBoneTransforms({Matrix::getIdentityProperty()})`,
`setWeightsPerVertexProperty(1)`, `EnableDefaultLighting()` — all genuine XNA API usage, matching
Task 409's own setup for its "quad A" case exactly (bone 0 = identity, `w0=1, i0=0`).

### Behavioral correctness
`RunTest()` (lines 63-111) reconstructs *only* quad A from Task 409's 3-quad scene (verified: same
`xMin=-1.0, xMax=-0.5` geometry, same texture color `(255,0,0,255)`, same
`RasterizerState::CullNone` workaround, same bone/weight setup) and issues 2 checks against the
same sample column (`W/8`, NDC x ~ -0.75):
1. `ExpectPixel("quadA-identity-vs-task409-observed", ..., Color(174,0,0,255), tolerance=40)` —
   a **wide** ±40 tolerance around a value the header explicitly says was "captured once via
   `CNA_UPDATE_GOLDEN=1` and independently confirmed red-dominant" rather than hand-derived from
   the lighting formula. See Finding F1.
2. `CompareGoldenImage("skinnedeffect-quadA-identity", ..., "examples/golden/
   easygl_skinnedeffect_golden_test.png", tolerance=40)` — the primary golden-image check, an 8x8
   region around the same sample point, also at a wide ±40 tolerance.
Both checks target the *same* pixel neighborhood with the *same* wide tolerance, which is somewhat
redundant (see Finding F2) but not incorrect — a genuine regression large enough to fail one would
very likely fail the other too, given they sample overlapping data.

### Logic
Correctly reuses `PixelTestGame`'s templated `RunPixelTest<SkinnedEffectGoldenTest>()` entry point
(line 116) rather than hand-rolling a `Game` subclass with its own `main()` boilerplate — matches
the shared-infrastructure intent `PixelTestGame.hpp`'s own header describes (Task 461).

### Memory/resource lifetime
No explicit resource management needed beyond what `PixelTestGame`/`Texture2D` already provide;
`AppendQuad`'s local `std::vector<SkinnedGpuVertex>` and the locally-scoped `VertexBuffer` both
follow standard RAII with no dangling-reference risk.

### C++ correctness
`static_assert(sizeof(SkinnedGpuVertex) == 52)` (line 44) — consistent with every sibling in this
batch.

### Architecture
Correct use of the shared `PixelTestGame` base rather than duplicating the `Game`-subclass/`main()`
boilerplate every other file in this batch hand-rolls — this is the *only* one of the 8 files in
this batch built on that shared infrastructure, which is itself informative: golden-image tests
specifically were the motivating use case for `PixelTestGame` per its own header comment.

### Robustness
`CompareGoldenImage()` (in `PixelTestGame.hpp`, not this file) already handles the
`CNA_UPDATE_GOLDEN` environment-variable regeneration path and a width/height mismatch guard —
this file correctly relies on that shared behavior rather than reimplementing it.

### Testing
This file is itself a test, specifically re-testing a scene already covered by
`easygl_skinnedeffect_combined_test.cpp` under a stricter (pixel-region) comparison mechanism —
legitimate, non-duplicative reuse (different verification *mechanism*, not a copy-paste of the
same assertion).

### Cross-file consistency
Confirmed the geometry (`xMin=-1.0,xMax=-0.5`), sample column (`W/8`), and bone/weight setup are
byte-for-byte identical to "quad A" in `easygl_skinnedeffect_combined_test.cpp` — this file's claim
of reusing that scene is accurate, not just asserted.

## Detailed Findings

### F1 — `ExpectPixel`'s expected value is a captured live observation, not an analytically derived one

- Severity: MEDIUM
- Confidence: HIGH (the file's own header states this directly; not inferred)
- Category: testing / assertion rigor
- Location/symbol: `RunTest()` line 105-106 (`ExpectPixel(..., Color(174, 0, 0, 255),
  /*tolerance=*/40)`); header comment lines 9-13
- Evidence: the header explicitly says this value was "captured once via `CNA_UPDATE_GOLDEN=1`
  and independently confirmed red-dominant... rather than an analytically-derived number," because
  `EnableDefaultLighting()`'s real 3-point Phong lighting math "isn't practical to derive
  analytically by hand." Combined with a ±40 tolerance (the widest tolerance seen across this
  entire batch of 8 files — every other file's tightest check uses ≤8 and its loosest ≤30), this
  check would still pass for a meaningfully broken lighting computation (e.g. R anywhere from 134
  to 214) as long as it stays roughly in that neighborhood and the golden-image comparison
  (running against the *same* captured-once reference in PNG form) doesn't independently catch it.
- Why it matters: this is a materially weaker form of verification than the shard's norm — most
  sibling `SkinnedEffect` tests in this batch (fog, multilight, bones, identity-bones) hand-derive
  their expected values from the actual lighting/skinning formula and can therefore prove a
  specific regression class is or isn't present. This check instead only proves "the renderer
  still produces roughly what it produced once before," which would not distinguish "correct" from
  "a different-but-still-plausible-looking lighting bug that happened to also get baked into the
  golden PNG at the same time" (a real risk for *any* golden-image-style test, not unique to this
  file, but worth naming directly rather than letting the wide tolerance look accidental).
- FNA/XNA comparison: N/A — this is a test-methodology observation, not an XNA API-parity issue.
- Related files: `easygl_skinnedeffect_combined_test.cpp` (the same scene's `EnableDefaultLighting`
  origin), `PixelTestGame.hpp` (the shared golden-image mechanism, which is itself honest about
  this trade-off in its own header comment: "Exact pixel-perfect reproduction... is already known
  unreliable... pass a non-zero tolerance for any golden image that isn't a single flat,
  unblended colour").
- Suggested future action (not implemented by this audit): if `SkinnedEffect`'s default-lighting
  formula is ever independently hand-derived elsewhere (as `easygl_skinnedeffect_multilight_test.
  cpp` does for the *custom*-light case in this same batch), consider tightening this check's
  tolerance or replacing the `ExpectPixel` cross-check's captured value with an analytically
  derived one for full 3-point default lighting.

### F2 — `ExpectPixel` and `CompareGoldenImage` are redundant checks over overlapping data

- Severity: LOW
- Confidence: MEDIUM
- Category: maintainability / test design
- Location/symbol: lines 105-110
- Evidence: both checks sample the same pixel neighborhood (single pixel at `(samplePx, sampleY)`
  vs. an 8x8 region centred on the same point) at the same ±40 tolerance.
- Why it matters: not wrong, but the single-pixel `ExpectPixel` adds little independent signal
  over the region-based `CompareGoldenImage` that already covers that exact pixel — a genuine
  regression large enough to fail one would very likely fail both. Low-value redundancy rather
  than a defect.
- Suggested future action (not implemented by this audit): none required; could be left as
  intentional belt-and-suspenders given F1's own admission that the golden PNG's origin is the
  same "captured once" event as the `ExpectPixel` value, so having a second value not sourced from
  the PNG file itself (i.e., not just re-reading the same file twice) does have some standalone
  value as a sanity check that the PNG file itself hasn't silently drifted/been corrupted.

## Cross-File Observations

The only file in this 8-file batch built on `CNA::Examples::PixelTestGame` rather than a hand-rolled
`Game` subclass + `main()` — confirms golden-image tests are the intended use case for that shared
helper, per `PixelTestGame.hpp`'s own header.

## Missing or Weak Tests

See Finding F1 — the file's own single check whose expected value isn't analytically grounded.
No other gaps found; quads B/C from the combined test are correctly left uncovered here (this
file's own stated, narrower scope is "quad A only").

## Positive Findings

- Refreshingly honest header comment that explicitly names its own methodological compromise
  (captured-once value instead of a derived one) rather than presenting the check as
  equivalent in rigor to an analytically-verified one — this is exactly the kind of disclosed
  limitation this audit wants to see recorded, not silently glossed over.
- Correct, verified reuse of an existing scene (byte-for-byte matching geometry/bone setup)
  rather than a superficially-similar-looking but subtly different reconstruction.

## Final Assessment

A legitimate golden-image regression test whose only real weakness — an expected value sourced
from a single past observation rather than a derived formula, at an unusually wide tolerance — is
already disclosed by the file's own header. Recorded as a MEDIUM finding because it meaningfully
weakens this specific check's power to catch a subtle lighting regression, not because anything is
functionally broken today.
