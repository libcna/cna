# Audit: examples/easygl_rasterizerstate_cullmode_test.cpp

## Metadata

- Source file: `examples/easygl_rasterizerstate_cullmode_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test — `examples-tests-easygl` shard
- File type: C++ example/integration-test executable (Task 323, subsumes Task 324/325 coverage)
- Related production code: `Microsoft::Xna::Framework::Graphics::RasterizerState`
  (`src/Microsoft/Xna/Framework/Graphics/RasterizerState.cpp`), `CullMode`
  (`include/Microsoft/Xna/Framework/Graphics/CullMode.hpp`), `EasyGLGraphicsBackend::ApplyRasterizerState`
  (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:1976-2005`)
- XNA/FNA relevance: `RasterizerState.CullMode`'s three values and its default
  (`CullMode.CullCounterClockwiseFace`) are real XNA 4.0 semantics this test verifies directly against rendered
  pixels.
- Main related tests: `easygl_rasterizerstate_cullmode_golden_test.cpp` (same shard) reuses this file's scene and
  one of its six checks via a golden-image comparison instead of a raw pixel-color range check.

## Purpose

Draws two triangle-pair quads with deliberately opposite vertex winding (`DrawQuadCW`/`DrawQuadCCW`) side by side,
then re-renders the identical scene three times under `CullMode::None`, `CullMode::CullCounterClockwiseFace`
(XNA's default), and `CullMode::CullClockwiseFace`, sampling one pixel from each column each time — six checks
total — to verify that `RasterizerState.CullMode` actually gates rasterization by winding order rather than being a
no-op or a state that's ignored/bypassed by the EasyGL backend.

## Executive Verdict

**Healthy** — this is a rigorous, self-aware test: its own header comment documents a prior authoring mistake
(borrowed, unverified winding labels from a sibling test) being caught and corrected by actually running the test
and observing the empirical result, and its 6-check design specifically defeats the "every check expects the same
outcome" failure mode the same comment calls out from an earlier version. All mapping claims were independently
re-derived against the EasyGL backend and default `RasterizerState` and found consistent.

## Checklist Results

### API / XNA / FNA parity
`RasterizerState.CullMode` (`None`, `CullClockwiseFace`, `CullCounterClockwiseFace`) matches FNA's `CullMode` enum
exactly (verified in `include/Microsoft/Xna/Framework/Graphics/CullMode.hpp`). Default `RasterizerState` constructor
sets `cullMode_(CullMode::CullCounterClockwiseFace)` (`RasterizerState.cpp:11`), matching XNA/FNA's documented
default rasterizer state (`RasterizerState.CullCounterClockwise` is the standard XNA default). PASS.

### Behavioral correctness
Traced `EasyGLGraphicsBackend::ApplyRasterizerState` (lines 1976-2005): `cullMode==0` (`None`) disables face culling
entirely; otherwise culling is enabled and `cullMode==1` (`CullClockwiseFace`) maps to `CullFace::Back`, `cullMode==2`
(`CullCounterClockwiseFace`) maps to `CullFace::Front` — with OpenGL's front-face winding left at its default
(`GL_CCW`, confirmed no `glFrontFace`/`FrontFace` call exists anywhere in this backend file). Given the test's own
`World=View=Projection=Identity` setup (`BasicEffect` with all three matrices set to `Matrix::getIdentityProperty()`),
vertex NDC coordinates equal the raw `Vector3` values passed to `DrawQuadCW`/`DrawQuadCCW`, and the viewport
transform used by `SetViewport` (`EasyGLGraphicsBackend.cpp:2034-2053`, `fbH - y - h`) is a pure Y-axis offset, not a
mirror — so the signed-area/winding-orientation of a triangle as authored in NDC survives unchanged into GL window
space. Working through `DrawQuadCW`'s vertex order (signed shoelace area of the first triangfle:
`(x1,-1),(x0,-1),(x0,1)` is negative for `x1>x0`, i.e. CW by the standard math convention) against
`CullCounterClockwiseFace→cull GL_FRONT(=CCW by default)`: a CW-wound quad is a GL back-face under the default
front-face convention, so it survives `CullFace::Front` culling — precisely matching the test's own
"CW survives the default state" empirical claim and its `CullCounterClockwiseFace: column0 (CW): RED` expectation.
The symmetric case (`CullClockwiseFace→cull GL_FRONT-mapped-to-Back`, i.e. `CullFace::Back`) correctly flips which
column survives. All six checks are internally consistent with the traced backend mapping. PASS.

### Logic
The three-mode-times-two-column matrix genuinely produces different pass/fail signatures per mode (column 0 flips
between RED/BACKGROUND, column 1 flips between GREEN/BACKGROUND depending on mode) — a broken/always-off/always-on
cull implementation would necessarily fail at least one of the six checks, exactly as the file's own header comment
claims. Verified by hand-tracing all three `runCheck(...)` invocations against the mapping above; no combination
produces a false pass. PASS.

### Robustness
`IsRed`/`IsGreen`/`IsBackground` helpers use a coarse per-channel threshold (`>=200`/`<=30`), tolerant of blending or
GPU/driver-specific rounding — reasonable given the test draws flat, unblended `BlendState::Opaque` colors, so a
correct render should be near-exact and a 30-unit tolerance is generous without risking a false pass against an
unrelated color.

### Testing
This file's own 6-check design is itself thorough for the feature under test; see Missing/Weak Tests for adjacent
gaps.

## Detailed Findings

No CRITICAL/HIGH findings. One INFO-level cross-cutting observation:

### F1 — `glFrontFace` is never set by the EasyGL backend; the CW/CCW mapping this test verifies is only proven correct for default-framebuffer rendering

- Severity: INFO
- Confidence: MEDIUM (verified no `FrontFace` call exists anywhere in the backend; did not verify whether any
  render-target/FBO Y-flip in this backend would require one)
- Category: architecture / cross-cutting
- Location/symbol: `EasyGLGraphicsBackend.cpp` — no `glFrontFace`/`FrontFace` call in the entire file (grep-confirmed)
- Evidence: this test (and its golden-image sibling) render directly to the default backbuffer, where the traced
  Y-offset-only viewport transform (`fbH - y - h`, no sign flip) preserves NDC winding orientation into window
  space, so `GL_CCW` (the OpenGL default, never overridden) is a consistent, correct default-front-face definition
  for what this test measures. Whether that same assumption holds when a `RasterizerState.CullMode` is applied
  while rendering *into* a render target (where some engines flip `glFrontFace` to compensate for a Y-flipped FBO
  attachment) was not exercised by this test and is out of scope for this file.
- Why it matters: not a defect in this file — it correctly validates cull-mode behavior for the scenario it
  actually renders (backbuffer). Recorded here as a cross-cutting note for whichever file/shard covers
  render-target + cull-mode combinations together (no such combined test was found in this shard).
- Suggested action: none for this file; flag for `AUDIT_CROSS_CUTTING_FINDINGS.md` / the `backend-easygl` shard's
  own audit of `ApplyRasterizerState`.

## Cross-File Observations

- `easygl_rasterizerstate_cullmode_golden_test.cpp` explicitly reuses only this file's `CullMode::None` /
  column-0 check and its exact `DrawQuadCW`/`DrawQuadCCW` vertex data (duplicated, not shared via a common header) —
  see that file's own audit report for whether the duplication has drifted.
- This file's own header comment candidly documents a **prior authoring error** (Task 318's borrowed
  front/back naming being empirically wrong) being caught by actually running the test rather than assuming the
  labels — a genuinely positive engineering practice worth calling out explicitly (see Positive Findings).

## Missing or Weak Tests

- No render-target-bound variant of this test (see F1) — all cull-mode coverage in this shard is backbuffer-only.
- No test of `CullMode` combined with `RasterizerState.FillMode = FillMode.WireFrame` (the EasyGL backend's own
  `wireframe_` flag, set independently in the same `ApplyRasterizerState` call, `EasyGLGraphicsBackend.cpp:1997`) —
  the two states are applied together in production code but never exercised together by any test in this shard.

## Positive Findings

- Exemplary test-design self-correction: the file's own header comment documents that an earlier draft's assumed
  winding labels were proven wrong by actually running the test, and the file was renamed/re-derived from the
  empirically-measured signed area rather than re-asserting the old assumption — exactly the kind of "verify, don't
  assume" discipline this audit is meant to reward.
- The "6 checks with genuine result contrast" design is explicitly built to defeat a documented prior failure mode
  (a same-outcome-every-check test that can't distinguish "works" from "bypassed") — a real, applied engineering
  lesson, not just a comment.

## Final Assessment

A rigorously self-verified test whose winding/cull-mode claims were independently re-derived here against the
EasyGL backend's actual `ApplyRasterizerState` mapping and found fully consistent. No correctness defects found;
the one INFO-level note (F1) marks a legitimate scope boundary (backbuffer-only), not a gap in this file's own
claims.
