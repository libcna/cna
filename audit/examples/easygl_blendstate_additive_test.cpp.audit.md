# Audit: examples/easygl_blendstate_additive_test.cpp

## Metadata

- Source file: `examples/easygl_blendstate_additive_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ integration-test executable source (hand-rolled `Game` subclass + `main()`)
- Lines: 111
- Registered as: `cna_test_easygl_blendstate_additive` (`cmake/Tests/EasyGLTests.cmake:1356-1360`, CTest name
  `EasyGL_BlendState_Additive`). **This exact source file is also cross-compiled for Vulkan**:
  `cna_test_vulkan_blendstate_additive` (`cmake/Tests/VulkanTests.cmake:120-125`, CTest name
  `Vulkan_BlendState_Additive`) — confirmed no `vulkan_blendstate_additive_test.cpp` exists separately; the Vulkan
  test target literally compiles `examples/easygl_blendstate_additive_test.cpp`.
- Related production code: `Microsoft::Xna::Framework::Graphics::BlendState::Additive` (`BlendState.cpp:6`),
  `GraphicsDevice::setBlendStateProperty` (`GraphicsDevice.cpp:1667-1682`),
  `EasyGLGraphicsBackend::ApplyBlendState` (`EasyGLGraphicsBackend.cpp:1904-1922`).
- XNA/FNA relevance: exercises `BlendState::Additive`; judged against
  `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/BlendState.cs`.

## Purpose

Task 306: verifies `BlendState::Additive` both saturates correctly (no colour wraparound past 255) and always adds
the full destination regardless of source alpha (`colorDestinationBlend=One`, not `InverseSourceAlpha` as
`AlphaBlend`/`NonPremultiplied` use). Clears to `Color(200,50,0,255)`, draws an opaque
`Color(255,100,0,255)` full-screen quad under `BlendState::Additive`, and checks the centre pixel's R channel
saturates (`>=250`) and G channel lands in `[140,160]` (i.e. `100+50=150` exactly, not saturated).

## Executive Verdict

**Needs attention** — not for the test's own assertion logic, which is correct and well-reasoned against real
production code and FNA's own preset definition, but because its header comment makes a definite, present-tense
claim about Vulkan's blend-state implementation ("Vulkan's blend state is almost entirely fake") that is
demonstrably **stale**: `plans/plan_graphics.md`'s own Task 868 entry records this exact bug as fixed and this exact test
(`Vulkan_BlendState_Additive`) as now passing. Since this file is literally the Vulkan test's source too, the
stale comment sits inside the very file whose behavior it mischaracterizes.

## Checklist Results

### API / XNA / FNA parity
`BlendState::Additive` is defined in `BlendState.cpp:6` as
`{colorSourceBlend=SourceAlpha, alphaSourceBlend=SourceAlpha, colorDestinationBlend=One, alphaDestinationBlend=One}`
— confirmed identical (same four `Blend` values, same order) to FNA's own
`Blend.SourceAlpha, Blend.SourceAlpha, Blend.One, Blend.One` (`BlendState.cs`). The test's expected-value math (R
saturates, G=150 exactly at full source alpha) is therefore derived from the *actual* current preset definition,
not an assumption.

### Behavioral correctness
`Draw()` clears, sets `BlendState::Additive`, draws the quad, and reads back a single centre pixel via
`GetBackBufferData` (line 79). `RasterizerState::CullNone` (line 74) is required and correctly applied — the six
vertices form a CCW-wound triangle in NDC and FNA's `RasterizerState()` defaults `CullMode` to
`CullCounterClockwiseFace` (confirmed in `RasterizerState.cs:127`), so without `CullNone` the quad would be
back-face-culled and the readback would just be the clear colour, a false negative the comment ("Task 896 finding")
correctly documents.

### Logic
Two independent boolean checks (`rSaturated`, `gAddsFully`) combined with `&&` — both must hold for `pass`.
`rSaturated` only checks `>=250` (not `==255`), a sensible tolerance-free-but-still-lenient bound given saturation
clamps to exactly 255 in a correct implementation, and any value in `[250,255)` would already indicate a real (if
smaller) rounding/precision issue worth investigating, not a false pass for genuinely broken code. `gAddsFully`
checks a tight symmetric band `[140,160]` around the exact expected 150 — correctly chosen to be far enough from
both "source only" (~100, the bug this test targets) and 255 (saturated) to be an unambiguous discriminator.

### Memory/resource lifetime
`BasicEffect fx(dev)` stack-constructed per `Draw()` call — trivial, no resource leak surface. `done_`/`result_`
member latch pattern correctly ensures the test body runs exactly once even though `Draw()` may be invoked by the
game loop more than once before `Exit()` takes effect.

### C++ correctness
No casts, no UB. `Color got(0,0,0,0)` initialized before the out-parameter `GetBackBufferData` call — correct
defensive initialization (matches the same pattern in all seven sibling files in this batch).

### Performance
N/A — single-shot correctness test, not a hot path.

### Thread safety
N/A — single-threaded example executable.

### Architecture
Hand-rolled `Game` subclass (not `PixelTestGame`) — an older-style example predating Task 461's shared harness
(per `PixelTestGame.hpp`'s own comment, existing files were deliberately not retrofitted). Architecturally
consistent with the rest of the pre-Task-461 `examples/*_test.cpp` population; no backend-internal types leak into
this file.

### Maintainability
See Detailed Findings F1 — the header comment's Vulkan claim is now inaccurate and should be updated or removed if
this file is touched again for any other reason, to avoid misleading a future reader who greps for "Task 868" and
finds an "almost entirely fake" claim next to a test that has passed on Vulkan since the fix landed.

### Portability
No platform-conditional code in this file itself; its cross-compilation onto both EasyGL and Vulkan test targets is
handled entirely by CMake (see Metadata), not by any `#ifdef` in the source.

### Robustness
No preflight GPU/display check (unlike `PixelTestGame`-based files) — if the environment prevents SDL from
initializing video, `Game`'s constructor is documented elsewhere in this codebase to throw `std::runtime_error`
uncaught, crashing the test binary rather than reporting `[SKIP]`/exit-77. This is a known, accepted limitation of
the pre-Task-470 test style shared by all six hand-rolled-`Game` files in this batch, not unique to this file.

### Testing
This is itself a test; see Finding F1 for the one concrete issue found (documentation accuracy, not test logic).

### Cross-file consistency
Cross-checked its own math against `BlendState.cpp`'s actual `Additive` preset values (see API/FNA parity above)
and against `EasyGLGraphicsBackend::ApplyBlendState` (`EasyGLGraphicsBackend.cpp:1904-1922`), which does implement
a real per-parameter `set_blend_func_separate`/`set_blend_equation_separate` (not a hardcoded equation like the
now-fixed Vulkan bug this file's comment describes) — confirming the test is checking genuine, parameterized
backend behavior on EasyGL, not a coincidence.

## Detailed Findings

### F1 — Header comment asserts Vulkan's blend state is "almost entirely fake," but this has since been fixed (Task 868 closed)

- Severity: MEDIUM
- Confidence: HIGH
- Category: maintainability / documentation-accuracy
- Location/symbol: file header comment, lines 13-18: *"This test also happens to re-expose Task 868 (Vulkan's
  blend state is almost entirely fake — see plans/plan_graphics.md) for real this time, unlike Task 305's
  NonPremultiplied test, which coincidentally passed on Vulkan. Task 868's hardcoded equation uses
  InverseSourceAlpha for the destination factor..."*
- Evidence: `plans/plan_graphics.md`'s own Task 868 row (line 393) is marked `✅ CLOSED` with a detailed fix description:
  `VulkanGraphicsBackend::ApplyBlendState` previously discarded all 6 real blend parameters and hardcoded one
  equation across 9 pipeline-creation functions; the fix added real `ToVkBlendFactor`/`ToVkBlendOp` mapping and a
  shared `FillBlendAttachmentState()` helper. The closure note explicitly states: *"reverted... reran the 7
  `Vulkan_BlendState_*` tests — the exact same 5 (`AlphaBlend`/`Additive`/`SeparateFunctions`/`SeparateFactors`/
  `BlendFactor`) failed with the exact previously-documented wrong values; restored and reconfirmed all 7 pass."*
  This confirms `Vulkan_BlendState_Additive` — which compiles this exact `.cpp` file — now passes, directly
  contradicting the file's own comment that it "re-exposes" a bug that is "almost entirely fake."
- Why it matters: this file is literally the source compiled into the Vulkan test target the comment discusses,
  so the misinformation is embedded in the same file whose behavior it mischaracterizes, not merely a stale
  reference in a separate doc. A future engineer investigating a Vulkan blend regression (or verifying the current
  test suite's health) who reads this comment first would reasonably but incorrectly conclude that a currently-green
  `Vulkan_BlendState_Additive` result is untrustworthy, or spend time re-confirming an already-fixed, already-closed
  bug.
- FNA/XNA comparison: N/A (comment-accuracy issue, not an FNA parity issue).
- Related files: `cmake/Tests/VulkanTests.cmake:120-125` carries an equivalent stale inline comment ("NOTE: expected
  to genuinely re-expose Task 868 here... unlike Task 305's coincidental pass") that should be updated in the same
  pass if this is addressed; `plans/plan_graphics.md` line 393 is the authoritative current-state source that resolves the
  discrepancy.
- Suggested future action (not implemented by this audit): update or remove the stale Vulkan-bug narrative in this
  file's header comment (and the identical pattern in five sibling files in this batch — `nonpremultiplied`,
  `blendfactor`, `separate_factors`, `separate_functions`, and more mildly `additive_golden`) to reflect that Task
  868 is closed, while keeping the still-valid distinguishing-value reasoning (G=150 vs G~100) that motivates the
  test's actual assertions.

## Cross-File Observations

- This exact `.cpp` file is compiled into two different CTest targets (EasyGL and Vulkan) — worth keeping in mind
  when auditing `examples-tests-vulkan`: some "Vulkan" test failures/passes in that shard will trace back to a file
  that physically lives in, and is nominally scoped to, this `examples-tests-easygl` shard.
- Six of the eight files in this batch independently carry a near-identical `// Task 896 finding:` comment about
  `RasterizerState::CullNone` being required — verified consistent and accurate across all of them (same vertex
  winding, same FNA default `CullMode`).

## Missing or Weak Tests

None for this file's own coverage of `BlendState::Additive` — the two checks (saturation, non-saturated add) are a
reasonable, tight pair for what the preset is meant to prove. A finer-grained test could additionally verify the
B channel (always 0 here) or a partial-source-alpha case (this preset with `As<255` would no longer collapse
`SourceAlpha` to a clean 1.0 scale), but that is arguably better suited to a distinct test given this file's
narrow, single-purpose design (consistent with the rest of the batch).

## Positive Findings

- Test assertions are correctly derived from the actual current `BlendState::Additive` preset values in
  `BlendState.cpp`, not from an assumption — verified by direct comparison against FNA's own preset definition.
- `RasterizerState::CullNone` requirement is correctly identified and applied, backed by a verifiable FNA default
  (`CullCounterClockwiseFace`).
- Clear PASS/FAIL diagnostic printf messages that explain *what a failure would mean* (e.g. "G~100 would mean the
  destination was incorrectly dropped"), aiding a future debugger.

## Final Assessment

The test itself is correct and well-grounded in real production behavior and FNA parity. Its only defect is
documentation staleness (Finding F1): a comment describing a now-fixed Vulkan bug in the present tense, which — given
this file is also the Vulkan test's actual compiled source — risks actively misleading future maintainers rather
than merely being outdated trivia.
