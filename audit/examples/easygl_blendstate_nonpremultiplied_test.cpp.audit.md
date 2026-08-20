# Audit: examples/easygl_blendstate_nonpremultiplied_test.cpp

## Metadata

- Source file: `examples/easygl_blendstate_nonpremultiplied_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ integration-test executable source (hand-rolled `Game` subclass + `main()`)
- Lines: 121
- Registered as: `cna_test_easygl_blendstate_nonpremultiplied` (`cmake/Tests/EasyGLTests.cmake:1350-1354`, CTest
  name `EasyGL_BlendState_NonPremultiplied`). **Also cross-compiled for Vulkan**
  (`cna_test_vulkan_blendstate_nonpremultiplied`, `cmake/Tests/VulkanTests.cmake:112-118`, CTest name
  `Vulkan_BlendState_NonPremultiplied`).
- Related production code: `Microsoft::Xna::Framework::Graphics::BlendState::NonPremultiplied` (`BlendState.cpp:8`),
  `EasyGLGraphicsBackend::ApplyBlendState` (`EasyGLGraphicsBackend.cpp:1904-1922`).
- XNA/FNA relevance: exercises `BlendState::NonPremultiplied`; judged against
  `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/BlendState.cs`.

## Purpose

Task 305: verifies `BlendState::NonPremultiplied` implements the raw-alpha equation
(`colorSourceBlend=alphaSourceBlend=SourceAlpha`, `colorDestinationBlend=alphaDestinationBlend=InverseSourceAlpha`)
— i.e. it multiplies a RAW (non-premultiplied) source colour by alpha itself, the complementary case to
`AlphaBlend` (Task 304), which expects an already-premultiplied source. Draws raw 50%-alpha red
(`Color(255,0,0,128)`) over green and expects `~(128,127,0)` — numerically similar to Task 304's `AlphaBlend`
result, deliberately, so the two tests together prove *which stage* performs the alpha multiplication.

## Executive Verdict

**Needs attention** — again, not for the assertion logic (correct, matches FNA's preset and CNA's own real blend
math), but for an unusually strongly-worded, now-stale "CRITICAL CAVEAT" block that actively instructs the reader
not to trust a passing Vulkan result and asserts Vulkan's blend handling "doesn't work in general" — a claim
`plans/plan_graphics.md`'s Task 868 closure entry directly contradicts (all 7 `Vulkan_BlendState_*` tests, this one
included, are recorded as passing after the fix).

## Checklist Results

### API / XNA / FNA parity
`BlendState::NonPremultiplied` = `{colorSourceBlend=SourceAlpha, alphaSourceBlend=SourceAlpha,
colorDestinationBlend=InverseSourceAlpha, alphaDestinationBlend=InverseSourceAlpha}` (`BlendState.cpp:8`) —
confirmed identical to FNA's own preset (`Blend.SourceAlpha, Blend.SourceAlpha, Blend.InverseSourceAlpha,
Blend.InverseSourceAlpha`).

### Behavioral correctness
Math: source `(255,0,0,128)`, dest `(0,255,0,255)`. `NonPremultiplied` equation:
`out = src*(srcA/255) + dst*(1-srcA/255)`. `srcA/255 ≈ 0.502`. R = `255*0.502 + 0*0.498 ≈ 128`.
G = `0*0.502 + 255*0.498 ≈ 127`. Test checks `rInBand`/`gInBand` as `[110,145]` (line 92-93) — same band width as
the `AlphaBlend` sibling test, correctly reused since the numeric target is nearly identical. `CullNone` correctly
applied for the same CCW-winding reason verified in every other file of this batch.

### Logic
Unlike `easygl_blendstate_alphablend_test.cpp`, this file has no explicit "double-multiply" discriminator check
(no analogue of `notDoubleMultiplied`) — see Finding F2. Given the numeric overlap between `AlphaBlend`'s and
`NonPremultiplied`'s expected outputs (both land near 128/127 by design, per the file's own comment: *"Numerically
similar to Task 304's expected AlphaBlend result"*), this test alone cannot distinguish "NonPremultiplied correctly
applied the SourceAlpha equation" from "the code actually ran AlphaBlend's One/InverseSourceAlpha equation on an
already non-premultiplied source and got a coincidentally similar number" — only the *pair* of tests (304+305)
together, with their different *source* colours, prove which stage multiplies by alpha. This is the file's own
explicitly stated rationale ("the two tasks together prove *which* stage... performs the alpha multiplication, not
just that 'some blend happened'") — an honest, self-aware design choice rather than an oversight, so this is not
scored as a defect on its own, but is flagged because it means this file in isolation has slightly weaker
discriminating power than its sibling `AlphaBlend` test.

### Memory/resource lifetime
Same pattern as sibling files — stack-allocated, single-run latch. No issues.

### C++ correctness
No casts, no UB.

### Performance
N/A.

### Thread safety
N/A.

### Architecture
Hand-rolled `Game` subclass, consistent with the batch.

### Maintainability
See Finding F1 — the stalest and most emphatically-worded Vulkan claim in this entire batch.

### Portability
No platform-conditional code in the file itself.

### Robustness
No preflight GPU/display check (accepted limitation shared with sibling hand-rolled files).

### Testing
Correctly designed as one half of a two-test pair (with `AlphaBlend`) rather than a standalone proof — a
legitimate test-architecture choice, documented as such.

### Cross-file consistency
Its expected values and reasoning are consistent with `easygl_blendstate_alphablend_test.cpp` (cross-checked
directly) and with `BlendState.cpp`'s real preset definitions.

## Detailed Findings

### F1 — "CRITICAL CAVEAT" block is the most strongly-worded stale Vulkan claim in the batch (Task 868 closed)

- Severity: MEDIUM
- Confidence: HIGH
- Category: maintainability / documentation-accuracy
- Location/symbol: lines 8-18: *"*** CRITICAL CAVEAT, carried from Task 304/868 — READ BEFORE TRUSTING A PASSING
  VULKAN RESULT *** Task 304 found that Vulkan's ApplyBlendState is almost entirely broken... This means: a PASSING
  result on Vulkan is NOT evidence that Task 868 is fixed, or that Vulkan's blend state handling works in general —
  it doesn't, for Additive or any custom BlendState... Do not close Task 868 based on this test passing."*
- Evidence: `plans/plan_graphics.md` line 393 records Task 868 as `✅ CLOSED`, with a verification note explicitly listing
  `Vulkan_BlendState_NonPremultiplied` as one of the 7 tests reconfirmed passing after a real fix (not a
  coincidental one) was implemented and verified by reverting/re-testing/restoring. The comment's core claim —
  "Vulkan's blend state handling... doesn't work in general" — is the single most sweeping and now most
  incorrect statement of the "stale Vulkan comment" pattern found across this batch (also present, more mildly, in
  `additive_test`, `blendfactor_test`, `separate_factors_test`, `separate_functions_test`).
- Why it matters: the ALL-CAPS "CRITICAL CAVEAT... READ BEFORE TRUSTING A PASSING VULKAN RESULT" framing is
  designed to override a reader's own observation of a passing test — exactly the failure mode that would let a
  genuinely-fixed bug's fix go unrecognized, or worse, cause a future engineer to distrust or "fix" a
  now-correctly-passing test to match this stale expectation.
- FNA/XNA comparison: N/A.
- Related files: `cmake/Tests/VulkanTests.cmake:112-118` carries a milder version of the same stale claim
  ("NOTE: a passing result here does NOT mean Task 868 is fixed").
- Suggested future action: this is the highest-priority comment to update of all six affected files in this batch,
  given its emphatic, reader-directive phrasing — replace with a note that Task 868 was subsequently fixed and
  this test (along with its six `Vulkan_BlendState_*` siblings) now passes for real, while retaining the still-true
  explanation of *why* `NonPremultiplied` and `AlphaBlend` need to be read together to prove which stage
  multiplies by alpha.

### F2 — No standalone double-multiply/wrong-preset discriminator (contrast with `AlphaBlend`'s `notDoubleMultiplied`)

- Severity: LOW
- Confidence: MEDIUM
- Category: test-coverage
- Location/symbol: `pass = rInBand && gInBand;` (line 94) — no third guard analogous to
  `easygl_blendstate_alphablend_test.cpp`'s `notDoubleMultiplied`.
- Evidence: as discussed in Logic above, this test's own header comment acknowledges its expected result is
  "numerically similar" to `AlphaBlend`'s, meaning this file alone cannot prove which specific equation actually
  ran; it currently relies on the reader / test-suite-level knowledge that the *pair* of tests together is
  necessary.
- Why it matters: low real-world risk since the sibling `AlphaBlend` test already carries the sharper
  discriminator and both tests always run together in the same CI suite — but a future refactor that moves or
  disables one test without the other would silently lose that discriminating power without any single test file
  indicating it depends on the other.
- FNA/XNA comparison: N/A.
- Suggested future action: consider a code comment cross-reference (`// see also
  easygl_blendstate_alphablend_test.cpp — together these prove which stage multiplies by alpha`) directly in the
  assertion logic, not just the file header, so the dependency survives a future partial-refactor.

## Cross-File Observations

- Forms a genuine, deliberate test pair with `easygl_blendstate_alphablend_test.cpp` — verified both files' actual
  source colours and expected outputs are consistent with the "same output, different input, proves which stage
  multiplies" design the header comment claims.
- Shares the "Task 896"/`CullNone` finding and the general hand-rolled-`Game` structure with the rest of the batch.

## Missing or Weak Tests

See Finding F2. No other coverage gap identified.

## Positive Findings

- Test math is correctly derived and verified against both FNA's real preset and the actual blend equation.
- Self-aware, explicitly documented test-pairing design (not an accidental duplicate of `AlphaBlend`).
- `CullNone` requirement correctly identified and applied.

## Final Assessment

Correct test logic let down by a comment block whose urgency ("CRITICAL CAVEAT... READ BEFORE TRUSTING") is now
actively counterproductive, since the bug it warns about has been fixed and verified. This is the single highest-
priority documentation-staleness fix among the eight files in this batch.
