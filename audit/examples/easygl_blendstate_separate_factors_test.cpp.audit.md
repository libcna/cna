# Audit: examples/easygl_blendstate_separate_factors_test.cpp

## Metadata

- Source file: `examples/easygl_blendstate_separate_factors_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ integration-test executable source (hand-rolled `Game` subclass + `main()`)
- Lines: 143
- Registered as: `cna_test_easygl_blendstate_separate_factors` (`cmake/Tests/EasyGLTests.cmake:1368-1372`, CTest
  name `EasyGL_BlendState_SeparateFactors`). **Also cross-compiled for Vulkan**
  (`cna_test_vulkan_blendstate_separate_factors`, `cmake/Tests/VulkanTests.cmake:135-141`, CTest name
  `Vulkan_BlendState_SeparateFactors`).
- Related production code: `Microsoft::Xna::Framework::Graphics::BlendState` color/alpha factor setters
  (`BlendState.cpp:40-53`), `GraphicsDevice::setBlendStateProperty` (`GraphicsDevice.cpp:1667-1682`),
  `EasyGLGraphicsBackend::ApplyBlendState` (`EasyGLGraphicsBackend.cpp:1904-1922`).
- XNA/FNA relevance: exercises independence of `ColorSourceBlend`/`ColorDestinationBlend` from
  `AlphaSourceBlend`/`AlphaDestinationBlend`; judged against FNA's `BlendState.cs` property model (four
  independent backing fields, no cross-coupling in the C# source).

## Purpose

Task 308: verifies that `ColorSourceBlend`/`ColorDestinationBlend` operate independently from
`AlphaSourceBlend`/`AlphaDestinationBlend` — i.e. setting the colour factors doesn't leak into the alpha
computation or vice versa. None of the 4 static `BlendState` presets vary colour/alpha factors independently (each
uses the same factor pair for both channels), so this test constructs two custom `BlendState`s with the colour and
alpha factors deliberately swapped relative to each other (`Check A`: colour=`One`/`Zero` (source only), alpha=
`Zero`/`One` (destination only); `Check B`: the exact reverse) and confirms the *colour* channel output always
tracks only the *colour* factors regardless of what the alpha factors are doing.

## Executive Verdict

**Needs attention** — the test design itself is sound and its math is correct, but it carries the same stale
Vulkan/Task-868 narrative pattern found in four sibling files, here specifically asserting a per-check
pass/fail split ("expect Check A to pass and Check B to fail") that `plans/plan_graphics.md`'s Task 868 closure entry
directly contradicts (records `SeparateFactors` as one of the 5 tests that failed before the fix and passed,
in full, afterward).

## Checklist Results

### API / XNA / FNA parity
Confirmed `BlendState`'s four factor setters (`setColorSourceBlendProperty`, `setColorDestinationBlendProperty`,
`setAlphaSourceBlendProperty`, `setAlphaDestinationBlendProperty`, `BlendState.cpp:40-53`) are backed by four fully
independent private fields (`colorSourceBlend_`, `colorDestinationBlend_`, `alphaSourceBlend_`,
`alphaDestinationBlend_`) with no cross-field coupling in the setters themselves — matching FNA's own
`BlendState.cs` property model exactly (four independent auto-properties over a `FNA3D_BlendState` struct's
distinct fields). This test targets the correctness of the *backend* honoring that independence, not the
`BlendState` class itself (which trivially satisfies it by construction).

### Behavioral correctness
`MakeState(colorSrc, colorDst, alphaSrc, alphaDst)` helper (lines 47-58) builds each custom state. `Check A`:
colour=`(One,Zero)` ⇒ colour output = source only; alpha=`(Zero,One)` ⇒ alpha output = destination only (never
read back, since this project has "no established, verified alpha-channel backbuffer-readback pattern" per the
file's own comment — an honest, accurate scope limitation, not an oversight). Background `(50,200,0,255)`, source
`(200,50,0,255)`. `Check A` expects pure source colour `(200,50,0)`; `Check B` (colour=`(Zero,One)`, alpha
reversed) expects pure destination colour `(50,200,0)`. Both expectations are the mathematically correct result of
the stated blend equation, and the *opposite* expected colours between the two checks (plus the alpha-factor swap)
is a genuinely well-reasoned way to prove the colour path doesn't accidentally read the alpha factors (if it did,
one of the two checks would silently pick up the *other* check's expected result).

### Logic
`DrawAndSample` (lines 65-96) is called twice within a single `Draw()` invocation — `Clear()` → `setBlendStateProperty`
→ draw → `GetBackBufferData` readback, sequentially, before the next `Clear()` overwrites the same backbuffer. This
relies on `GetBackBufferData` synchronously reflecting the just-completed draw before the next `Clear()` — a valid
technique for a single-threaded, non-double-buffered-readback test harness (the same pattern is independently used
by the sibling `separate_functions_test.cpp`), and consistent with how `GetBackBufferData` is implemented
elsewhere in this project (a synchronous readback, not an async/fenced one, per `GraphicsDevice.cpp:1778` and
callers throughout this batch).

### Memory/resource lifetime
`DrawAndSample` stack-constructs a fresh `BasicEffect fx(dev)` on each of its two calls — no leak/lifetime issue;
each `BasicEffect` is scoped to its own function call and destructed normally at the end of `DrawAndSample`.

### C++ correctness
No casts, no UB. `Color got(0,0,0,0)` correctly pre-initialized before each of the two independent readbacks.

### Performance
N/A — single-shot correctness test; two draw calls total, not a hot path.

### Thread safety
N/A.

### Architecture
Hand-rolled `Game` subclass with a private `DrawAndSample` helper method — a slightly more structured variant than
the simpler single-draw sibling files in this batch (justified here since the test genuinely needs two independent
draw+readback cycles).

### Maintainability
See Finding F1 (stale Vulkan per-check pass/fail prediction).

### Portability
No platform-conditional code in the file itself.

### Robustness
No preflight GPU/display check (accepted limitation shared with sibling hand-rolled files).

### Testing
Well-targeted independence test — the swapped-alpha-factor design genuinely proves cross-channel independence
rather than merely re-confirming each channel's factors work in isolation.

### Cross-file consistency
Shares its `DrawAndSample`-twice-per-`Draw()` structural pattern with `easygl_blendstate_separate_functions_test.cpp`
(confirmed near-identical helper method shape between the two files) — a reasonable, deliberate mirroring given the
file's own header comment states it "mirrors Task 307's approach... but for the blend *factors* instead."

## Detailed Findings

### F1 — Header comment predicts a stale per-check Vulkan pass/fail split (Task 868 closed)

- Severity: MEDIUM
- Confidence: HIGH
- Category: maintainability / documentation-accuracy
- Location/symbol: lines 21-26: *"NOTE: Vulkan's ApplyBlendState (Task 868, see plans/plan_graphics.md) hardcodes
  colorSrcBlend=SourceAlpha, colorDstBlend=InverseSourceAlpha regardless of what's requested. At alpha=255 that
  collapses to 'source only'... which happens to match Check A's expectation by coincidence, but NOT Check B's...
  Expect Check A to pass and Check B to fail on Vulkan; do not read a passing Check A as evidence Task 868 is
  fixed."*
- Evidence: `plans/plan_graphics.md` line 393 (Task 868, `✅ CLOSED`) lists `SeparateFactors` among the 5 tests
  (`AlphaBlend`/`Additive`/`SeparateFunctions`/`SeparateFactors`/`BlendFactor`) that failed with "the exact
  previously-documented wrong values" before the fix and were reconfirmed to fully pass (both checks, not just
  Check A) afterward.
- Why it matters: same class of risk as the other stale-Vulkan-comment findings in this batch — a future reader
  would be told, incorrectly, that `Check B` is *expected* to fail on Vulkan, potentially causing them to dismiss a
  currently-passing result as anomalous, or worse, treat a future genuine regression in the *opposite* direction
  (Check A newly failing) as unremarkable since the comment only primed them to watch for Check B.
- FNA/XNA comparison: N/A.
- Related files: `cmake/Tests/VulkanTests.cmake:135-141` carries the matching stale inline comment ("expect Check A
  to coincidentally pass and Check B to fail per Task 868").
- Suggested future action: update or remove this NOTE to reflect that both checks now pass on Vulkan following
  Task 868's fix, while preserving the still-valid explanation of why the swapped-alpha-factor design is needed to
  prove independence.

## Cross-File Observations

- Fifth instance (of six in this batch) of the same stale-Vulkan-narrative pattern; taken together with
  `additive_test`, `nonpremultiplied_test`, `blendfactor_test`, and `separate_functions_test`, this suggests the
  staleness is systemic to this entire test generation (all written/commented around the same time, before Task
  868's fix landed) rather than an isolated one-off — worth a single consolidated documentation-cleanup pass across
  all five/six files plus their `VulkanTests.cmake` counterparts rather than five separate piecemeal edits.
- The `DrawAndSample`-twice-per-frame technique (also used by `separate_functions_test.cpp`) is a reusable pattern
  worth noting for anyone writing a similar independence test in another shard.

## Missing or Weak Tests

None beyond the acknowledged (and honestly documented) alpha-channel-readback gap, which the file itself explains
is a project-wide limitation ("this project has no established, verified alpha-channel backbuffer-readback pattern
to rely on") rather than an oversight specific to this file.

## Positive Findings

- Genuinely well-designed independence proof via deliberately swapped, opposite-expectation checks.
- Honest, explicit acknowledgment of the alpha-channel-readback scope limitation rather than silently ignoring it.
- Correct math, correct FNA-consistent understanding of the four independent blend-factor fields.

## Final Assessment

Sound test design and correct assertions, undermined by a stale, specifically-wrong per-check Vulkan prediction
that should be updated now that Task 868 has been fixed and both checks are confirmed passing on that backend.
