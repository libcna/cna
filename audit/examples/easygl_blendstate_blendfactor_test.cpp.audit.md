# Audit: examples/easygl_blendstate_blendfactor_test.cpp

## Metadata

- Source file: `examples/easygl_blendstate_blendfactor_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ integration-test executable source (hand-rolled `Game` subclass + `main()`)
- Lines: 118
- Registered as: `cna_test_easygl_blendstate_blendfactor` (`cmake/Tests/EasyGLTests.cmake:1374-1378`, CTest name
  `EasyGL_BlendState_BlendFactor`). **Also cross-compiled for Vulkan**
  (`cna_test_vulkan_blendstate_blendfactor`, `cmake/Tests/VulkanTests.cmake:143-151`, CTest name
  `Vulkan_BlendState_BlendFactor`).
- Related production code: `Microsoft::Xna::Framework::Graphics::BlendState::setBlendFactorProperty`
  (`BlendState.cpp:67-68`), `GraphicsDevice::setBlendStateProperty` /
  `GraphicsDevice::setBlendFactorProperty` (`GraphicsDevice.cpp:1667-1682`, `1735-1745`),
  `EasyGLGraphicsBackend::SetBlendFactor` (`EasyGLGraphicsBackend.cpp:2028-2032`).
- XNA/FNA relevance: exercises `BlendState.BlendFactor` propagation; judged against FNA's
  `GraphicsDevice.ApplyState()`/`FNA3D_SetBlendState` (`GraphicsDevice.cs:1573-1582`).

## Purpose

Task 309: verifies that a `BlendState`'s own baked-in `BlendFactor` colour is applied atomically as part of
`GraphicsDevice.setBlendStateProperty` — i.e. the caller should NOT need a separate `setBlendFactorProperty` call
for the state's own constant colour to take effect. Builds a custom `BlendState` with
`ColorSourceBlend=Blend::BlendFactor`, `ColorDestinationBlend=Blend::Zero`, and `BlendFactor=Color(200,100,0,255)`,
calls only `setBlendStateProperty`, draws an all-white source quad, and checks the centre pixel is `~(200,100,0)`
(i.e. exactly the constant, since white × constant = constant).

## Executive Verdict

**Healthy** for the test's own logic — it is a precise, correctly-targeted regression test whose expected value is
directly traceable to a real, verified propagation path in `GraphicsDevice.cpp`. Its **header comment about
Vulkan is stale**, same finding class as `easygl_blendstate_additive_test.cpp` (Finding F1 below): it asserts
Vulkan's `ApplyBlendState` "hardcodes a single blend equation that never selects VK_BLEND_FACTOR_CONSTANT_COLOR,"
but `plans/plan_graphics.md`'s Task 868 closure entry explicitly lists this test (`BlendFactor`) among the five that
failed before the fix and now passes after it.

## Checklist Results

### API / XNA / FNA parity
The comment's claim about FNA's own behavior — *"FNA applies a BlendState's own baked-in BlendFactor atomically as
part of FNA3D_SetBlendState whenever GraphicsDevice.BlendState is assigned"* — was independently verified against
`GraphicsDevice.cs:1573-1582` (FNA's `ApplyState()`): `FNA3D_SetBlendState(GLDevice, ref nextBlend.state)` passes
the *entire* `nextBlend.state` struct, which does include the `blendFactor` field per FNA3D's `FNA3D_BlendState`
struct — the comment's FNA citation is accurate, not just plausible-sounding.

### Behavioral correctness
Directly cross-checked against CNA's own implementation, not just against the comment's claim:
`GraphicsDevice::setBlendStateProperty` (`GraphicsDevice.cpp:1667-1682`) does exactly what the test expects —
`blendState_ = value;` then `backend_->ApplyBlendState(...)` then, unconditionally,
`setBlendFactorProperty(value.getBlendFactorProperty())` — with its own comment explicitly citing this same Task
309/FNA-atomicity rationale. The test's "deliberately no separate `dev.setBlendFactorProperty(...)` call" design
(explicitly called out in the file's own comment, line 66-67) is precisely what exercises this propagation path
and not some other one.

### Logic
`ColorSourceBlend=BlendFactor(10)`, `ColorDestinationBlend=Zero(1)`, `AlphaSourceBlend=One(0)`,
`AlphaDestinationBlend=Zero(1)`. Source colour `(255,255,255,255)` (all-ones). Expected: `out = white * constant +
dst*0 = constant = (200,100,0)`. Verified this is exactly what `EasyGLGraphicsBackend::ApplyBlendState`
(`EasyGLGraphicsBackend.cpp:1904-1922`) would produce: `ToEasyGLBlendFactor(10)` maps to `ConstantColor`
(confirmed against the ordinal table comment at lines 1797-1801, itself confirmed to match FNA's real `Blend` enum
ordinals in `Blend.cs`), and `EasyGLGraphicsBackend::SetBlendFactor` (line 2028-2032) calls
`device.set_blend_color(r,g,b,a)` with the values from `getBlendFactorProperty()` — a genuine, non-stubbed
constant-colour blend path, not a hardcoded/faked one.

### Memory/resource lifetime
Same stack-allocated `BlendState state;`/`BasicEffect fx(dev)` pattern as sibling files — no issues.

### C++ correctness
No casts, no UB.

### Performance
N/A — single-shot correctness test.

### Thread safety
N/A.

### Architecture
Hand-rolled `Game` subclass, consistent with the rest of the batch.

### Maintainability
See Finding F1 (stale Vulkan claim).

### Portability
No platform-conditional code in the file; cross-compiled to Vulkan via CMake only.

### Robustness
No preflight GPU/display check (accepted limitation shared with sibling hand-rolled files).

### Testing
Directly and specifically tests the atomic-propagation contract (not merely "does BlendFactor work at all") by
withholding the otherwise-obvious separate `setBlendFactorProperty` call — a well-targeted regression test for
exactly the behavior Task 309 was about.

### Cross-file consistency
Consistent with `GraphicsDevice.cpp`'s real implementation (verified above) and with FNA's documented behavior
(verified above) — a genuine three-way (test / CNA production code / FNA reference) match.

## Detailed Findings

### F1 — Header comment (and NOTE at line 101-102) asserts BlendFactor is expected to fail on Vulkan; this is stale (Task 868 closed)

- Severity: MEDIUM
- Confidence: HIGH
- Category: maintainability / documentation-accuracy
- Location/symbol: the file itself has no explicit Vulkan claim in its own header comment (this file's header,
  lines 1-21, is Vulkan-silent) — the stale claim lives in `cmake/Tests/VulkanTests.cmake:143-151`'s inline comment
  attached to this file's Vulkan registration: *"NOTE: expected to FAIL on Vulkan per Task 868 - ApplyBlendState
  hardcodes a single blend equation that never selects VK_BLEND_FACTOR_CONSTANT_COLOR... Kept registered as a
  further documented confirmation of Task 868, not a new bug."*
- Evidence: `plans/plan_graphics.md` line 393 (Task 868, `✅ CLOSED`) explicitly states all 7 `Vulkan_BlendState_*` tests
  — named individually, including `BlendFactor` — were reconfirmed passing after the fix (`ToVkBlendFactor`
  mapping added, matching `VK_BLEND_FACTOR_CONSTANT_COLOR` for `Blend::BlendFactor`).
- Why it matters: this specific NOTE actively predicts the *opposite* of the test's current real outcome
  (predicts FAIL, current reality is PASS) and instructs a reader not to treat this test's status as informative
  ("not a new bug") — exactly the kind of instruction that could suppress attention to a genuine future regression
  if this test *did* start failing again, since the reader has been told in advance to expect and disregard a
  failure here.
- FNA/XNA comparison: N/A (comment-accuracy issue).
- Related files: `cmake/Tests/VulkanTests.cmake:143-151` is the actual location of the stale text (this `.cpp`
  file's own header is clean); flagging here because this `.cpp` file is the artifact the stale CMake comment
  describes, and any future reader of this file (not the CMake file) investigating "why is this Vulkan test
  supposedly expected to fail" would need to know the CMake-side claim is also outdated.
- Suggested future action: update the `VulkanTests.cmake` NOTE to reflect Task 868's closure, or remove it now that
  the test genuinely passes on Vulkan and needs no special-case framing.

## Cross-File Observations

- Shares the exact "expected to FAIL on Vulkan per Task 868" staleness pattern already documented more directly
  (inside the `.cpp` file itself) in `easygl_blendstate_additive_test.cpp` and
  `easygl_blendstate_nonpremultiplied_test.cpp` — for this file specifically, the stale text is one layer removed
  (in the CMake registration comment, not this file's own header), which is a meaningfully lower direct-impact
  variant of the same finding.
- Confirms (cross-referenced against `GraphicsDevice.cpp`) that the atomic BlendFactor-propagation behavior this
  test targets is real, current, correct production code — a genuinely useful regression test, not a test of a
  coincidence.

## Missing or Weak Tests

None found for this specific propagation behavior. A complementary test that verifies the *reverse* precedence
(i.e. an explicit `setBlendFactorProperty` call made *before* `setBlendStateProperty`, confirming the state's own
factor still wins, matching FNA's "whole struct wins" semantics) would close a small remaining ambiguity, but is a
`LOW`-priority addition given the current test already proves the core atomicity contract.

## Positive Findings

- Test expectation is triple-verified: matches FNA's real behavior, matches CNA's real `GraphicsDevice.cpp`
  propagation code, and matches EasyGL's real (non-stubbed) constant-colour blend implementation.
- Deliberately withholds the "obvious" extra call to isolate exactly the propagation behavior under test — good
  test design discipline.
- Clear, actionable FAIL-branch diagnostic ("A mismatch here means BlendState.BlendFactor is not propagated to
  GraphicsDevice when setBlendStateProperty is called").

## Final Assessment

A precise, well-grounded regression test for a real and correctly-implemented propagation contract. Its only
defect is an indirect one: the Vulkan CMake registration comment attached to this test is now stale following
Task 868's closure, which risks pre-emptively dismissing a genuine future regression on that backend.
