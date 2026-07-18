# Audit: examples/vulkan_depth_bias_test.cpp

## Metadata

- Source file: `examples/vulkan_depth_bias_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `RasterizerState.DepthBias`/`SlopeScaleDepthBias`
  integration test (Task 328)
- File type: standalone `Game`-subclass executable, CTest-registered integration test
- XNA/FNA relevance: direct — `RasterizerState.DepthBias`/`SlopeScaleDepthBias`,
  `DepthStencilState.DepthBufferFunction`.
- FNA reference: `Graphics/States/RasterizerState.cs` (default ctor sets
  `CullMode = CullMode.CullCounterClockwiseFace`, `DepthBias`/`SlopeScaleDepthBias` both default `0`).
- Related production code: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`ApplyRasterizerState()` line 7949, `vkCmdSetDepthBias()` call at line 6498,
  `GetOrCreatePipeline3D`'s `VK_FRONT_FACE_CLOCKWISE`/cull-mode mapping lines 3255-3265),
  `include/Microsoft/Xna/Framework/Graphics/RasterizerState.hpp`.

## Purpose

Four-scenario, single-frame "shadow acne"-style coplanar-redraw test: draws a red triangle then an
identically-positioned green triangle on top, with `DepthStencilState.DepthBufferFunction` forced to
`CompareFunction::Less` (the file's header comment explains why the real XNA default, `LessEqual`,
cannot discriminate anything here — an equal-depth redraw always passes under `LessEqual`). Under
`Less`, the second draw only wins if a negative `DepthBias`/`SlopeScaleDepthBias` pulls its depth
toward the camera. All four strips (flat/no-bias, flat/biased, tilted/no-bias, tilted/slope-biased) are
rendered into one frame and read back with four single-pixel `GetBackBufferData` calls.

## Executive Verdict

**Healthy** — the `CompareFunction::Less` rationale, the `vkCmdSetDepthBias` wiring, and the
raw-float DepthBias/SlopeScaleDepthBias forwarding path were all independently traced and confirmed
correct; the only notable finding is an unverified (but well-corroborated) geometric assumption about
triangle winding, not a defect.

## Checklist Results

### API / XNA / FNA parity
`RasterizerState()`'s default (`rsNoBias`, line 138) is used to draw the first ("A") triangle in each
pair with the *actual XNA default* rather than an explicit `CullNone`/no-bias override — confirmed
this matches FNA: `RasterizerState.cs`'s parameterless ctor sets `CullMode =
CullMode.CullCounterClockwiseFace` (the real XNA default) with `DepthBias`/`SlopeScaleDepthBias` left
at their C# field defaults (`0f`). `CompareFunction` enum ordering was independently checked against
`include/.../CompareFunction.hpp` (`Always=0, Never=1, Less=2, LessEqual=3, Equal=4, GreaterEqual=5,
Greater=6, NotEqual=7`) and against `ToVkCompareOp()`'s switch (`VulkanGraphicsBackend.cpp:2991-3001`)
— the mapping is exact, so `dss.setDepthBufferFunctionProperty(CompareFunction::Less)` really does
reach `VK_COMPARE_OP_LESS`.

### Behavioral correctness
Traced the full pipeline: `GraphicsDevice::setRasterizerStateProperty()`
(`GraphicsDevice.cpp:1715-1725`) forwards `getDepthBiasProperty()`/`getSlopeScaleDepthBiasProperty()`
as raw, unconverted floats into `ApplyRasterizerState()`, which stores them verbatim
(`depthBias_`/`slopeScaleDepthBias_`, `VulkanGraphicsBackend.cpp:7960-7961`) — no scaling/clamping
along the way. `vkCmdSetDepthBias(cb, draw.depthBias, 0.0f, draw.slopeScaleDepthBias)`
(line 6498) is called before every 3D draw regardless of whether the pipeline's own
`depthBiasEnable=VK_TRUE` static state would otherwise leave it at whatever the previous draw set —
correct, since Vulkan dynamic state persists across draws within a command buffer unless reset. This
matches the Vulkan spec's `depthBias = constantFactor × r + slopeFactor × maxDepthSlope` formula, where
`r` (minimum resolvable depth-buffer difference) is implementation-defined and typically tiny (e.g.
~6e-8 for a 24-bit depth buffer) — this independently explains why the test's magnitudes
(`-1e6`/`-2e3`) are "deliberately large" as its own header comment states, rather than that comment
being an unverified guess.

### Logic
The triangle-winding assumption underlying strip 0/2 (`rsNoBias`, expecting the front-facing triangle
to render at all under the *default* `CullCounterClockwiseFace`) was traced but not independently
re-derived from first principles for the Vulkan Y-flip + `VK_FRONT_FACE_CLOCKWISE` convention this
codebase uses (see F1). It is, however, strongly corroborated: this exact file (Task 328) is the
*original* of a cross-backend family — `easygl_depth_bias_test.cpp`'s own header comment states
"Direct adaptation of examples/vulkan_depth_bias_test.cpp (Task 328)", confirming this Vulkan file's
geometry was the one validated first, with EasyGL's version later mechanically copying the exact same
vertex ordering and winding-direction comment.

### C++ correctness
`isBlack()`/`isRed()`/`isGreen()` thresholds (`<30`/`>=200,<=60`) are generous enough to tolerate MSAA
edge blending or minor colour-space rounding without false negatives, and don't overlap each other
(a pixel can't simultaneously satisfy `isRed` and `isGreen`).

### Robustness
The retry-until-non-blank loop (lines 150-171) is the best-documented instance of this pattern in the
shard: it explicitly names the specific hardware (AMD RADV PHOENIX iGPU) and driver behavior being
worked around, explains why the workaround cannot mask a real depth-bias bug ("a real depth-bias bug
would render a wrong colour and stop the retry"), and uses an all-four-strips-blank check (not
per-strip) to distinguish "nothing rendered this frame" from "rendered but wrong" — a materially
better-reasoned version of the same pattern seen elsewhere in this shard with less justification (e.g.
`vulkan_basiceffect_textured_msaa_test.cpp`).

### Testing
Four independent checks (`isRed(p0)`, `isGreen(p1)`, `isRed(p2)`, `isGreen(p3)`) genuinely discriminate
constant-bias-off/on and slope-bias-off/on as four separate assertions in one frame, rather than
collapsing to a single pass/fail.

## Detailed Findings

### F1 — Triangle winding under default `RasterizerState` is asserted by comment, not independently re-derived against the Vulkan Y-flip + `VK_FRONT_FACE_CLOCKWISE` convention
- Severity: LOW
- Confidence: LOW (plausible, well-corroborated by cross-file precedent and by this test presumably
  having been run when originally authored, but not independently re-derived from the vertex-shader
  Y-flip through to the rasterizer's actual front-face determination in this audit session, and not
  re-verified by an actual Vulkan run in this audit sandbox — see D-P4 in `AUDIT_DECISIONS.md`)
- Category: correctness-of-test / geometry
- Location/symbol: `drawTri()` (lines 71-81), header comment lines 68-70 ("CW-winding triangle (front
  face under default CullCounterClockwiseFace)")
- Evidence: computed the 2D cross product of the triangle's math-space vertices (top, bottom-right,
  bottom-left as literally ordered in `drawTri`) by hand: cross = -0.48 (clockwise in standard
  math-space orientation), consistent with the comment's claim. Confirmed
  `GetOrCreatePipeline3D`/`GetOrCreatePipelineFogColored3D` hardcode `rs.frontFace =
  VK_FRONT_FACE_CLOCKWISE` and map XNA `CullCounterClockwiseFace` (enum value 2) to
  `VK_CULL_MODE_BACK_BIT` (lines 3255-3259: "CullCounterClockwiseFace=2... cull CCW (back) faces"). Did
  **not** independently re-verify how the vertex shader's `pos.y = -pos.y` Y-flip (present in
  `colored3d.vert.glsl`) interacts with Vulkan's own screen-space winding determination to confirm the
  net effect is "this specific vertex order renders as front-facing" — this is architecture-wide,
  established-by-precedent behavior (shared by every other CW-winding-assuming test in this codebase),
  not something specific to this file that could be wrong without every sibling test also being wrong.
- Why it matters: if the winding assumption were ever wrong, strips 0 and 2 (the `rsNoBias`/
  `rsSlope0` "expect RED" cases) would render nothing (background stays black), `isRed()` would fail,
  and the test would show a clear, loud failure — not a silent false-pass — so the practical risk this
  finding represents is low; it is flagged for completeness because the audit did not independently
  re-derive it from first principles (per D-P4, opportunistic runtime verification was not performed
  in this session for this file), not because there is concrete evidence it is wrong.
- FNA/XNA comparison: N/A — this is a Vulkan-backend rasterizer convention question, not an FNA
  behavior question; FNA's own default (`CullCounterClockwiseFace`) is confirmed correctly referenced.
- Related files: every other Vulkan pixel test in this codebase family that relies on the same
  CW-front/CCW-back convention (not independently re-audited here).
- Suggested action: none needed unless a future regression in this exact test surfaces; noting it here
  satisfies the audit's obligation to distinguish "independently re-derived" from "corroborated by
  precedent but not re-derived."

## Cross-File Observations

- This file is the **origin** of the cross-backend depth-bias test family (EasyGL's
  `easygl_depth_bias_test.cpp` explicitly cites it as the template it adapted), which is useful context
  for weighing F1: the geometry/winding choice was presumably validated once here and then trusted by
  every derivative, rather than independently re-derived per backend.
- Shares the "blank-frame retry loop" pattern with other files in this shard, but with meaningfully
  better in-file documentation of *why* — worth using as the reference example if that pattern is ever
  consolidated (see `vulkan_basiceffect_textured_msaa_test.cpp`'s audit report, F1).

## Missing or Weak Tests

None identified beyond F1's scope note (an assumption, not a gap).

## Positive Findings

- Correctly identifies and works around the real reason a naïve "just use the XNA default
  `DepthBufferFunction`" version of this test would be meaningless (`LessEqual` never discriminates a
  coplanar redraw) — this is a substantive, XNA-semantics-aware design decision, not an arbitrary
  choice.
- The bias-magnitude rationale (`-1e6`/`-2e3` being "deliberately large" to swamp
  format-dependent scaling) is independently confirmed consistent with the Vulkan spec's own
  `vkCmdSetDepthBias` formula.
- All four scenarios packed into a single frame/single command-buffer submission is an efficient,
  well-constructed test structure that avoids 4x the frame-capture overhead of separate draws.

## Final Assessment

A well-engineered, XNA-semantics-aware Vulkan integration test. No confirmed defects; one LOW-severity,
LOW-confidence note that its triangle-winding assumption was corroborated by cross-file precedent and
by-hand cross-product math rather than independently re-derived end-to-end through the Vulkan Y-flip
convention in this audit session.
