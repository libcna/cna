# Audit: examples/easygl_blendstate_opaque_test.cpp

## Metadata

- Source file: `examples/easygl_blendstate_opaque_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ integration-test executable source (hand-rolled `Game` subclass + `main()`)
- Lines: 102
- Registered as: `cna_test_easygl_blendstate_opaque` (`cmake/Tests/EasyGLTests.cmake:1338-1342`, CTest name
  `EasyGL_BlendState_Opaque`). **Also cross-compiled for Vulkan, D3D9, and D3D11**
  (`cmake/Tests/VulkanTests.cmake:100-104`, `D3D9Tests.cmake:116`, `D3D11Tests.cmake:49`).
- Related production code: `Microsoft::Xna::Framework::Graphics::BlendState::Opaque` (`BlendState.cpp:9`),
  `EasyGLGraphicsBackend::ApplyBlendState` (`EasyGLGraphicsBackend.cpp:1904-1922`, specifically the
  `blendEnabled` short-circuit at lines 1909-1912).
- XNA/FNA relevance: exercises `BlendState::Opaque`; judged against
  `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/BlendState.cs`.

## Purpose

Task 303: verifies `BlendState::Opaque` (`colorSourceBlend=One, colorDestinationBlend=Zero`, same for alpha)
genuinely discards destination content and ignores source alpha, rather than merely "not looking obviously
broken." The file's own comment explains the reason this specific test exists: an *existing* test (Task 255's
`DrawUserPrimitives<VertexPositionColor>` test) only ever drew fully-opaque colours, which cannot distinguish
`Opaque` from `AlphaBlend` (their math collapses to the same result when source alpha is 255). This test instead
draws a partially-transparent red (`alpha=128`) over a green background and asserts the result is pure red — proof
that alpha is being ignored entirely, not just that blending "looks right" at alpha=255.

## Executive Verdict

**Healthy.** Well-motivated test (explicitly reasons about why a *weaker* existing test wasn't sufficient), correct
math, correct FNA parity, and — notably — no stale Vulkan/Task-868 commentary (this is one of only two files in the
batch, along with `alphablend_test`, that makes no claim about Vulkan's current correctness, appropriately so:
`Opaque`'s `One`/`Zero` factors were never part of the old Vulkan hardcoded-equation bug in the first place, since
`Opaque` bypasses blending in most correct implementations, including this project's EasyGL backend).

## Checklist Results

### API / XNA / FNA parity
`BlendState::Opaque` = `{colorSourceBlend=One, alphaSourceBlend=One, colorDestinationBlend=Zero,
alphaDestinationBlend=Zero}` (`BlendState.cpp:9`) — confirmed identical to FNA's own preset
(`Blend.One, Blend.One, Blend.Zero, Blend.Zero`).

### Behavioral correctness
Math: `out = fragColor*1 + dest*0 = fragColor` exactly, regardless of source alpha. Source `(255,0,0,128)` over
green `(0,255,0,255)` should produce pure red with zero green bleed-through. Test asserts
`R>=200 && G<=50 && B<=50` (line 76) — a full three-channel check (unlike some sibling tests that only assert
R/G), correctly proving the destination is discarded, not merely that R happened to be high. `CullNone` correctly
applied for the same CCW-winding/FNA-default-`CullMode` reason verified across the whole batch.

### Logic
Cross-checked directly against `EasyGLGraphicsBackend::ApplyBlendState`'s actual implementation
(`EasyGLGraphicsBackend.cpp:1904-1922`): the backend special-cases exactly this parameter combination —
`colorSrcBlend==0 && colorDstBlend==1 && alphaSrcBlend==0 && alphaDstBlend==1` (i.e. `One`/`Zero` for both
channels) — to mean `blendEnabled=false`, calling `device.set_blend_enabled(false)` and skipping the
`set_blend_func_separate`/`set_blend_equation_separate` calls entirely. This is a genuinely different code path
from the other presets (not merely "the same blend-factor call with different arguments"), which is exactly what
this test needs to catch if a future refactor broke that special case (e.g. if `blendEnabled` were miscomputed to
`true` for `Opaque`, this test would immediately fail since GPU alpha blending with `One`/`Zero` on both channels
*should* mathematically still equal a discard — but only if the factor/equation wiring itself is bug-free; testing
the actual disabled-blend fast path, not just the equivalent math, is the more robust of the two possible
implementation strategies and this test would catch a regression in either).

### Memory/resource lifetime
Same pattern as sibling files — stack-allocated, single-run latch. No issues.

### C++ correctness
No casts, no UB.

### Performance
N/A — single-shot correctness test. (Note: the production code path it exercises, `ApplyBlendState`'s
`blendEnabled` short-circuit, is itself a legitimate, deliberate performance optimization — disabling `GL_BLEND`
entirely avoids a real per-fragment blend cost on the GPU for the extremely common `Opaque` case — but that's a
production-code observation, not a finding about this test file.)

### Thread safety
N/A.

### Architecture
Hand-rolled `Game` subclass, consistent with the batch.

### Maintainability
Clean, no stale comments found. The comment's own reasoning about *why* Task 255's existing test was insufficient
is a good practice this audit would like to see more of across the codebase — it demonstrates the test author
thought about discriminating power, not just "add a test for X."

### Portability
No platform-conditional code in the file itself.

### Robustness
No preflight GPU/display check (accepted limitation shared with sibling hand-rolled files).

### Testing
Strong test design — explicitly reasons about and closes a real discriminating-power gap left by a prior, weaker
test.

### Cross-file consistency
Consistent with `BlendState.cpp`'s real preset values and with `EasyGLGraphicsBackend.cpp`'s real
`blendEnabled`-shortcut implementation — a genuine, verified match between test expectation and actual code path
exercised, not a coincidence.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM/LOW findings — this file has no correctness, staleness, or coverage gaps identified.

## Cross-File Observations

- The only two files in this batch of eight with no Vulkan/Task-868 commentary at all are this one and
  `easygl_blendstate_alphablend_test.cpp` — consistent with both presets' factor combinations never having
  coincided with (or been exposed by) the old hardcoded Vulkan equation the way `Additive`/`NonPremultiplied`/the
  separate-factor/function tests did.
- Directly demonstrates (via the `blendEnabled` short-circuit in `EasyGLGraphicsBackend::ApplyBlendState`) that
  `Opaque` is handled via a genuinely different code path (blending disabled outright) rather than the same
  parameterized blend-factor path used by every other preset — worth keeping in mind for the `backend-easygl` shard
  audit of `EasyGLGraphicsBackend.cpp` itself, since this special-case`if` is exactly the kind of logic a future
  refactor could accidentally invert or narrow.

## Missing or Weak Tests

None found. This test already improves meaningfully on a documented prior gap (Task 255's weaker test).

## Positive Findings

- Explicitly reasons about, and closes, a real discriminating-power gap left by a prior test — a model example for
  "genuine" test design versus boilerplate.
- Full three-channel assertion (R, G, and B), stronger than several sibling files in this batch that only check
  R/G.
- Test expectation verified to match a real, distinct code path in production (the `blendEnabled` fast-path), not
  just the general parameterized blend-factor mechanism.
- No stale documentation found.

## Final Assessment

The strongest all-around file in this batch: correct, well-motivated, fully channel-checked, and free of the
documentation-staleness issue affecting five of its seven siblings.
