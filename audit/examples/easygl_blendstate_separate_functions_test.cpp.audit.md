# Audit: examples/easygl_blendstate_separate_functions_test.cpp

## Metadata

- Source file: `examples/easygl_blendstate_separate_functions_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ integration-test executable source (hand-rolled `Game` subclass + `main()`)
- Lines: 143
- Registered as: `cna_test_easygl_blendstate_separate_functions` (`cmake/Tests/EasyGLTests.cmake:1362-1366`, CTest
  name `EasyGL_BlendState_SeparateFunctions`). **Also cross-compiled for Vulkan**
  (`cna_test_vulkan_blendstate_separate_functions`, `cmake/Tests/VulkanTests.cmake:127-133`, CTest name
  `Vulkan_BlendState_SeparateFunctions`).
- Related production code: `Microsoft::Xna::Framework::Graphics::BlendState::setColorBlendFunctionProperty`/
  `setAlphaBlendFunctionProperty` (`BlendState.cpp:37-38`, `46-47`), `Microsoft::Xna::Framework::Graphics::BlendFunction`
  (`BlendFunction.hpp`), `EasyGLGraphicsBackend::ApplyBlendState`'s `ToEasyGLBlendEquation` mapping
  (`EasyGLGraphicsBackend.cpp:1822-1834`, `1918-1920`).
- XNA/FNA relevance: exercises independence of `ColorBlendFunction` from `AlphaBlendFunction`; judged against FNA's
  `BlendFunction` enum semantics (`BlendFunction.cs`).

## Purpose

Task 307: verifies `ColorBlendFunction` and `AlphaBlendFunction` operate independently — setting one must not
affect the other. None of the 4 static presets vary `BlendFunction` from the default `Add`, so this test builds two
custom `BlendState`s with `colorSrc=alphaSrc=colorDst=alphaDst=One` (so blend *factors* stay neutral and don't
complicate the *function* check) and opposite `BlendFunction` assignments per channel: `Check A`
(`ColorBlendFunction=Subtract, AlphaBlendFunction=Add`) and `Check B` (the reverse). Background
`(50,200,0,255)`, source `(200,50,0,255)`. `Check A` expects `Subtract`'s colour math (`R=200-50=150`,
`G=50-200→0` clamped); `Check B` expects `Add`'s colour math (`R=250, G=250`).

## Executive Verdict

**Needs attention** — same finding class as its `separate_factors_test.cpp` sibling: correct test design and math,
but a stale Vulkan/Task-868 comment that specifically predicts Check A must fail on Vulkan
("Vulkan... always hardcodes VK_BLEND_OP_ADD"), a claim `plans/plan_graphics.md`'s Task 868 closure entry directly
contradicts by name (`SeparateFunctions` listed among the 5 tests now fully passing post-fix).

## Checklist Results

### API / XNA / FNA parity
FNA's `BlendFunction` enum (`BlendFunction.cs`) documents `Subtract` as
`(srcColor * srcBlend) - (destColor * destBlend)` and `Add` as `(srcColor * srcBlend) + (destColor * destBlend)` —
confirmed these are exactly the equations this test's expected-value comments use. Ordinal values (`Add=0,
Subtract=1, ReverseSubtract=2, Max=3, Min=4`) were cross-checked against `EasyGLGraphicsBackend::ToEasyGLBlendEquation`
(`EasyGLGraphicsBackend.cpp:1824-1834`) and match exactly (`case 1: FuncSubtract`, `default: FuncAdd`, etc.) — the
production mapping this test exercises is ordinally correct, not coincidentally so.

### Behavioral correctness
With `colorSrc=colorDst=One` for both checks, `Check A`'s `Subtract`: `R = 200*1 - 50*1 = 150`,
`G = 50*1 - 200*1 = -150`, correctly clamped to 0 by the pipeline (the test asserts `subA.getGProperty() <= 20`,
line 112, a reasonable near-zero band accounting for clamp-boundary rounding). `Check B`'s `Add`:
`R = 200+50 = 250`, `G = 50+200 = 250` (asserted `>=235`, line 116-117 — appropriately close to 255's practical
ceiling without demanding exact saturation, since 250 doesn't actually saturate 8-bit colour and shouldn't be
expected to equal 255). Both checks swap `AlphaBlendFunction` relative to `ColorBlendFunction` specifically to
prove the colour computation doesn't leak in the alpha function — sound design, same rationale pattern as the
`separate_factors_test.cpp` sibling test but for functions instead of factors.

### Logic
Same `DrawAndSample`-called-twice-within-one-`Draw()` structure as `separate_factors_test.cpp` (verified: both
files' `DrawAndSample` helper methods are structurally identical modulo the `BlendState` construction call) —
sequential `Clear()`/set-state/draw/readback pairs relying on synchronous `GetBackBufferData`, a valid technique
consistent with the rest of this project (see that sibling report for the full reasoning, not re-derived here to
avoid duplication).

### Memory/resource lifetime
Same per-call stack-constructed `BasicEffect` pattern — no issues.

### C++ correctness
No casts, no UB.

### Performance
N/A — single-shot correctness test.

### Thread safety
N/A.

### Architecture
Hand-rolled `Game` subclass with a private `DrawAndSample` helper, structurally mirroring its
`separate_factors_test.cpp` sibling as its own header comment states ("mirrors Task 307's approach... for the blend
*factors* instead" — note: read literally from *this* file's perspective, Task 307 is *this* file and the mirrored
one is 308/`separate_factors`, i.e. the two files' headers cross-reference each other by task number, confirmed
consistent in both directions).

### Maintainability
See Finding F1 (stale Vulkan Check-A-fails prediction).

### Portability
No platform-conditional code in the file itself.

### Robustness
No preflight GPU/display check (accepted limitation shared with sibling hand-rolled files).

### Testing
Correctly isolates function-independence from factor-independence by neutralizing all four blend factors to `One`
— a deliberate, well-reasoned test-isolation technique, not an oversight of "should have also varied the factors."

### Cross-file consistency
Its `Add`/`Subtract` math is independently confirmed against FNA's own `BlendFunction.cs` doc comments and against
`EasyGLGraphicsBackend.cpp`'s real ordinal mapping — a genuine three-way match (test / FNA / CNA production code).

## Detailed Findings

### F1 — Header comment predicts a stale Vulkan Check-A failure (Task 868 closed)

- Severity: MEDIUM
- Confidence: HIGH
- Category: maintainability / documentation-accuracy
- Location/symbol: lines 18-21: *"NOTE: Vulkan's ApplyBlendState (see plans/plan_graphics.md Task 868) takes
  colorBlendFunc/alphaBlendFunc as unused parameters (commented out in the signature) and always hardcodes
  VK_BLEND_OP_ADD — so Check A (expecting Subtract) is expected to fail there, while Check B (expecting Add) is
  expected to coincidentally pass, mirroring the Task 305/306 pattern."*
- Evidence: `plans/plan_graphics.md` line 393 (Task 868, `✅ CLOSED`) explicitly names `SeparateFunctions` among the 5
  tests reconfirmed to fully pass (not just Check B) after the fix added real `ToVkBlendOp` mapping, described as
  mirroring "EasyGL's already-correct `ToEasyGLBlendEquation` tables exactly."
- Why it matters: identical risk profile to the other five stale-Vulkan-comment findings in this batch — a reader
  is told to expect a specific, now-incorrect failure pattern (Check A fails, Check B coincidentally passes),
  which could cause a currently fully-passing Vulkan result to be second-guessed, or a future genuine regression in
  the *opposite* channel to go unnoticed since attention was pre-directed at Check A only.
- FNA/XNA comparison: N/A.
- Related files: `cmake/Tests/VulkanTests.cmake:127-133` carries the matching stale inline comment.
- Suggested future action: update or remove this NOTE to reflect Task 868's closure and that both checks now pass
  on Vulkan, in the same consolidated pass suggested for the other affected files in this batch.

## Cross-File Observations

- Sixth and final instance (of six in this batch) of the systemic stale-Vulkan-comment pattern — see the
  `separate_factors_test.cpp` report's Cross-File Observations for the recommendation to fix all affected files
  (`additive_test`, `nonpremultiplied_test`, `blendfactor_test`, `separate_factors_test`, `separate_functions_test`,
  and the milder `additive_golden_test`) plus their `VulkanTests.cmake` comment counterparts in one consolidated
  documentation pass rather than piecemeal.
- Confirms the project's own claim (via direct FNA/production-code cross-check) that EasyGL's blend-equation
  mapping table (`ToEasyGLBlendEquation`) was the reference implementation Vulkan's later fix was explicitly built
  to mirror — useful corroborating context for the `backend-easygl` shard audit of `EasyGLGraphicsBackend.cpp`
  itself, confirming this table is treated as the project's "correct answer" for this feature.

## Missing or Weak Tests

None found beyond the shared, honestly-documented alpha-channel-readback limitation already discussed in the
`separate_factors_test.cpp` sibling report.

## Positive Findings

- Deliberately neutralizes blend factors to isolate function-independence specifically — good test-design
  discipline.
- Math independently verified correct against both FNA's `BlendFunction` semantics and CNA's actual EasyGL ordinal
  mapping table.
- Appropriately-toleranced boundary assertions (clamped-to-zero band, near-255-but-not-quite band) that reflect
  real integer/clamp arithmetic rather than idealized exact values.

## Final Assessment

Sound, well-verified test design once again undermined only by a now-stale Vulkan failure prediction that should be
corrected alongside its five sibling files' equivalent comments following Task 868's confirmed closure.
