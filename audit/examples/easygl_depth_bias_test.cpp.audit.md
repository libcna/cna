# Audit: examples/easygl_depth_bias_test.cpp

## Metadata

- Source file: `examples/easygl_depth_bias_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test, registered as CTest `EasyGL_DepthBias`
  (`cmake/Tests/EasyGLTests.cmake:1486-1488`, target `cna_test_easygl_depth_bias`)
- Related production code: `EasyGLGraphicsBackend::ApplyRasterizerState`
  (`EasyGLGraphicsBackend.cpp:1976-2005`), `RasterizerState::DepthBias`/`SlopeScaleDepthBias`
- XNA/FNA relevance: `RasterizerState.DepthBias`/`SlopeScaleDepthBias`, mapped to real
  `glPolygonOffset(factor, units)` semantics — matches this project's own established Vulkan
  `ApplyRasterizerState` convention (`glPolygonOffset(slopeScaleDepthBias, depthBias)`)
- Main related tests: direct adaptation of `examples/vulkan_depth_bias_test.cpp` (Task 328), reusing
  its "shadow acne"-style coplanar-redraw methodology

## Purpose

`EasyGLDepthBiasTest` (Task 767) verifies that `RasterizerState.DepthBias` and
`RasterizerState.SlopeScaleDepthBias` are actually wired to `glPolygonOffset` in the EasyGL backend and
genuinely change depth-test outcomes — previously an unused stub per the header comment (line 8). It
renders 4 side-by-side strips in one frame: flat/no-bias (expect fail→RED), flat/constant-bias
(expect pass→GREEN), tilted/no-slope-bias (expect fail→RED), tilted/slope-bias (expect pass→GREEN).

## Executive Verdict

**Healthy.** The production code (`ApplyRasterizerState`, lines 1998-2004) directly confirms this test's
central claim: `DepthBias`/`SlopeScaleDepthBias` map onto real `device.set_polygon_offset(
slopeScaleDepthBias, depthBias)`, called unconditionally with `set_polygon_offset_fill_enabled(true)` —
exactly matching the header comment's own description of what it expects to find, not an assumption.

## Checklist Results

### API / XNA / FNA parity
`RasterizerState::setDepthBiasProperty`/`setSlopeScaleDepthBiasProperty` are correct XNA property-style
setters. `DepthStencilState::setDepthBufferFunctionProperty(CompareFunction::Less)` (line 129) is used
deliberately instead of the real XNA default (`LessEqual`) — the header comment (lines 15-17)
explicitly explains why: under `LessEqual`, a coplanar redraw always passes regardless of bias, which
would make the whole test method incapable of discriminating anything. This is a documented,
justified test-methodology choice, not an FNA-parity violation (the production `DepthStencilState`
default is not being changed, only this test's own local `DepthStencilState` instance).

### Behavioral correctness
Confirmed `ApplyRasterizerState` (`EasyGLGraphicsBackend.cpp:1976-2005`) actually implements what the
test expects:
```cpp
device.set_polygon_offset_fill_enabled(true);
device.set_polygon_offset(slopeScaleDepthBias, depthBias);
```
called unconditionally (not gated on non-zero bias) — matching the header comment's own claim
(lines 27-30) that "a large negative factor gives an unambiguous, format-independent result" and
"overshoot is harmless." The GL argument order (`factor=slopeScaleDepthBias`, `units=depthBias`)
matches standard `glPolygonOffset(factor, units)` semantics and the project's own stated Vulkan
convention referenced in the comment (line 1999-2001).

`drawPair()` (lines 82-90) draws red (`A`, no bias, writes depth) then green (`B`, scenario bias) at
identical geometry — with `DepthBufferFunction::Less` (strict), `B` at equal depth to `A` fails unless
bias pulls it closer to the camera. This is the correct test methodology for isolating "does bias
actually change what passes," not just "does the API call not crash."

### Logic
`drawTri()` (lines 70-80) parameterizes `tilted` via `zt`/`zb` (top/bottom NDC-Z), producing a genuine
depth slope for the `SlopeScaleDepthBias` scenarios (columns 2/3) while flat geometry (columns 0/1)
isolates the constant-bias-only case — correct separation of the two bias types under test. `isRed`/
`isGreen` (lines 102-109) use asymmetric thresholds (`>=200` on the dominant channel, `<=60` on the
others) that correctly reject a blended/anti-aliased boundary pixel as neither pure color, avoiding a
false pass at a scenario's edge.

### Memory/resource lifetime
No dynamically-allocated GPU resources beyond `gdm_` (`unique_ptr`) and the implicit `BasicEffect`
(stack-local in `Draw()`); `DrawUserPrimitives` is used directly with stack-array vertex data, no buffer
lifetime concerns.

### C++ correctness
`static_cast<int>((ndcX + 1.0f) * 0.5f * vp.getWidthProperty())` (line 95) correctly converts NDC-X to
pixel-X; truncation (not rounding) is acceptable here since each strip is queried at its own center with
generous margin from strip boundaries (strip half-width `0.15` in NDC vs. strip spacing `0.4`).

### Performance / Thread safety
N/A — single-frame test with 4 scenario pairs.

### Architecture
Correct XNA API surface (`RasterizerState`, `DepthStencilState`, `BasicEffect`,
`GraphicsDevice::DrawUserPrimitives`) — no direct backend symbols referenced from the test itself.

### Maintainability
Header comment (lines 1-33) is thorough and specifically explains *why* `DepthBufferFunction::Less`
(not the real default) is necessary for this methodology, and *why* the bias magnitudes are
deliberately extreme (`-1e6`/`-2e3`) rather than realistic production values — both non-obvious choices
that would otherwise look like bugs on a first read.

### Portability
No platform-specific code; relies on `glPolygonOffset` being universally available in GLES 3.0/GL 3.0+,
which is a safe assumption for this backend's minimum target.

### Robustness
N/A (test file); `check()` (lines 61-65) accumulates pass/fail rather than aborting on first failure,
consistent with the suite's established pattern.

### Testing
This file is itself the dedicated test for `RasterizerState.DepthBias`/`SlopeScaleDepthBias` on EasyGL.
Coverage: 2 bias types × (no-bias-control, biased) = 4 scenarios in one frame. Does not test a
*positive* `DepthBias` (which would push `B` further away, making an already-failing case fail more
obviously — lower marginal value, reasonable to omit) nor an intermediate bias magnitude (only 0 vs.
extreme values) — acceptable given the stated goal is "prove the wiring exists and works," not
characterize `glPolygonOffset`'s precise scale-to-depth-unit mapping.

### Cross-file consistency
Directly parallels `examples/vulkan_depth_bias_test.cpp`'s methodology (per the header's own
attribution, line 6-8) — worth confirming both files stay in sync if the shared "shadow acne" technique
is ever revised, though that Vulkan file is out of this shard's scope.

## Detailed Findings

No HIGH/MEDIUM findings.

### F1 — Front-face winding assumption is asserted in a comment, not re-derived in this audit

- Severity: INFO
- Confidence: LOW
- Category: correctness (unverified assumption)
- Location/symbol: `drawTri()` comment, line 67 ("CW-winding triangle (front face under default
  CullCounterClockwiseFace)")
- Evidence: the vertex order `{(cx,0.8), (cx+0.15,-0.8), (cx-0.15,-0.8)}` produces a clockwise winding
  in standard screen-space NDC (cross-product sign check by this audit: negative, i.e. CW), and this
  test relies on that triangle being front-facing under CNA's real default `RasterizerState` (no
  explicit `CullNone` is set anywhere in this file, unlike the sibling `DepthStencilState` tests in this
  batch, which do call `RasterizerState::CullNone` explicitly per their own "Task 896 finding" comments).
- Why it matters: if CNA's actual default front-face convention differs from what this comment asserts,
  the triangles would be back-face-culled and every strip would read back as the clear color, not
  red/green — this audit did not independently re-derive CNA's coordinate-system/winding convention
  from first principles (out of scope for this file's own review) to confirm the comment's claim, so
  this is flagged as an assumption inherited from the test author, not independently re-verified.
- Suggested action (not implemented by this audit): none needed unless this test is observed failing in
  CI — recorded here only as a disclosed gap in this audit's own verification depth, not a claimed
  defect.

## Cross-File Observations

- Confirmed `ApplyRasterizerState`'s bias-mapping code is unconditional (always calls
  `set_polygon_offset_fill_enabled(true)`), meaning a `RasterizerState` with `DepthBias=0` still issues a
  real (no-op) `glPolygonOffset(0,0)` call every time state is applied — consistent with the comment's
  own "genuine no-op in GL" justification, and not a hidden performance concern given how infrequently
  rasterizer state changes relative to draw calls.

## Missing or Weak Tests

- No positive-`DepthBias` scenario (lower priority, as noted above).
- No intermediate bias-magnitude scenario to characterize the bias-to-depth-unit relationship
  (out of scope for a "wiring exists" proof).

## Positive Findings

- Directly and successfully cross-verified against the actual `ApplyRasterizerState` implementation —
  the production code does exactly what this test's header comment says it should.
- Well-reasoned, explicitly-justified departure from the real XNA `DepthStencilState` default
  (`Less` instead of `LessEqual`) specifically to make the test methodology work, with the reasoning
  documented rather than left implicit.
- Deliberately extreme bias magnitudes chosen for format-independence, explained in-comment.

## Final Assessment

A well-designed, methodologically sound test whose central claim (EasyGL's `DepthBias`/
`SlopeScaleDepthBias` genuinely reach `glPolygonOffset`) is directly confirmed by the production code;
the only open item is an unverified (by this audit) winding/front-face assumption inherited from the
test's own design, not a demonstrated defect.
