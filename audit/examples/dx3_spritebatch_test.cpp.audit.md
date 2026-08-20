# Audit: examples/dx3_spritebatch_test.cpp

## Metadata
- Source file: `examples/dx3_spritebatch_test.cpp` (279 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-tests-dx3` shard
- File type: C++ source (standalone backend integration/smoke test)
- XNA/FNA relevance: exercises `SpriteBatch::Draw` (identity fast-path, general blend path, rotation, scale, flip, custom transform), `BlendState::Opaque`/`AlphaBlend`, `SpriteSortMode`
- Main related tests: N/A (this IS the test; registered as `Dx3_SpriteBatch` in `cmake/Tests/Dx3Tests.cmake`, already confirmed a live CTest registration in this session's `build-cmake-tests` audit)

## Purpose
10-check CPU-compositor/SpriteBatch draw-path test for the DX3 (DirectDraw) backend: Begin/End
contract, identity BltFast fast-path, general-path full/zero-alpha blending, horizontal flip,
scale, 180°-rotation-about-center, `SetTransformMatrix`, non-Deferred `SpriteSortMode`, and
custom-Effect rejection.

## Executive Verdict
**High-value cross-check completed, per this project's own persistent memory of a previously-
confirmed real bug (`Dx3_SpriteBatch` 2/10 checks failing — one a likely genuine rotation-math
bug, "Check G"; the other likely a test-authoring bug around premultiplied-alpha semantics,
"Check D").** Direct static analysis of this exact source confirms both prior characterizations
independently:

**Check D (zero-alpha under `BlendState::AlphaBlend`) — likely a test-authoring bug, not a backend
bug.** The test constructs a texture with texels `Color(255, 0, 0, 0)` — i.e. a **non-premultiplied
(straight-alpha)** red pixel with zero alpha — and asserts the destination is left completely
untouched after drawing it with `BlendState::AlphaBlend`. Real XNA/FNA's `BlendState.AlphaBlend`
preset uses **premultiplied-alpha blend factors** (`ColorSourceBlend = Blend.One`,
`ColorDestinationBlend = Blend.InverseSourceAlpha`) — it assumes the source color has already been
multiplied by its own alpha at content-import time (XNA's Content Pipeline premultiplies texture
alpha by default). For a genuinely non-premultiplied `(255,0,0,0)` texel (as this test constructs
directly via `SetData`, bypassing content-pipeline premultiplication), a *faithful* implementation
of XNA's real `AlphaBlend` blend equation would compute `dest*1 + source_color*1 = dest + (255,0,0)`
(clamped) — a visibly non-black contribution from the "zero-alpha" pixel — NOT "destination
untouched." The test's own expectation (destination fully unchanged) is only correct under
*straight*-alpha "over" blend semantics (`SrcAlpha`/`InvSrcAlpha`), which is not what
`BlendState.AlphaBlend` actually is in real XNA. This test appears to conflate "AlphaBlend" with
"straight-alpha over," which is a test-construction assumption error, not necessarily a DX3
backend defect — if DX3 faithfully implements the real premultiplied-style equation, this specific
check would legitimately fail while the backend itself is behaviorally correct.

**Check G (180° rotation about center) — the test's own math is sound; a failure here would
indicate a genuine backend bug, not a test-authoring error.** Independently re-derived: the
destination-rectangle-with-non-zero-origin overload correctly anchors the sprite's bounding box
centered on the given position (comment's own `[56,64]x[36,44]` bounding box for a `(60,40,8,8)`
dest rect with a texture-center origin is consistent with XNA's documented anchor semantics). A
180° rotation about that center is a point reflection: the originally-top-left texel (Red) must
land in the bottom-right quadrant and the originally-bottom-right texel (Yellow) must land in the
top-left quadrant — exactly what the test asserts (`ReadPixel(62,42)` = bottom-right = Red;
`ReadPixel(58,38)` = top-left = Yellow). This reasoning holds regardless of any premultiplied-alpha
question (the test draws with full white tint and `BlendState::Opaque`, so blending is not a
confound here). If this check fails against the real DX3 backend, the most likely cause is a real
implementation defect in how rotation is applied (e.g. an incorrect pivot/origin scaling, a sign
error in the rotation angle, or rotating destination geometry without correspondingly adjusting
source-texel sampling) — not a flaw in the test's own expected-value derivation.

This audit could not re-run the binary in this sandbox (no DX3/Wine build environment available in
this audit-only pass), so this assessment is a static-analysis confirmation of the prior
session's empirical finding, not a fresh empirical re-verification.

## Checklist Results
- Check A (Begin/End contract), Check B (identity BltFast), Check C (general-path full-opacity),
  Check E (flip), Check F (scale), Check H (custom transform), Check I (SpriteSortMode), Check J
  (custom Effect rejection) all have straightforward, uncontroversial expected values matching
  standard XNA `SpriteBatch` semantics — no issues found in their own test logic.
- The file's own top-of-file comment enumerates all 10 checks with their `plans/plan_dx3.md` task IDs
  (DX3-30 through DX3-39) — a clear, traceable per-check specification.

## Detailed Findings
- **MEDIUM (test-authoring)** — Check D's expected value assumes straight-alpha "over" semantics
  for `BlendState::AlphaBlend`, which does not match real XNA/FNA's actual premultiplied-alpha
  blend-factor definition for that preset. Recommend either constructing the test texture with
  genuinely premultiplied data (`Color(0,0,0,0)` for a zero-alpha, no-color-contribution case) or
  explicitly documenting that this check targets straight-alpha semantics as a deliberate CNA
  behavior choice, if that is intentional.
- **Unresolved, needs actual test execution to confirm current status**: whether Check G currently
  passes or fails against DX3 in this exact source state was not independently re-verified by
  running the binary in this pass — this audit's job was a static-code cross-check of the already-
  recorded finding, which it confirms is architecturally plausible (a real rotation bug, not a test
  bug) rather than re-measuring pass/fail directly.

## Cross-File Observations
`cmake/Tests/Dx3Tests.cmake` (already audited in `build-cmake-tests`) confirms `Dx3_SpriteBatch` is
registered as a genuine, live CTest — this already-known 2/10-failing state is not hidden by any
build-level exclusion.

## Missing or Weak Tests
Not applicable — this file's own checklist is thorough for its stated scope (10 checks covering
the documented `plans/plan_dx3.md` Phase X4 task range).

## Positive Findings
The test's own top-of-file per-check documentation (mapping each check to a specific task ID) is a
clear, valuable specification that made this independent re-derivation possible without needing to
consult external documentation.

## Final Assessment
Confirms, via independent static analysis, this project's own prior finding: Check D's failure (if
any) most likely reflects a test-construction assumption mismatch with real XNA `AlphaBlend`
premultiplied semantics (test-authoring issue), while Check G's failure (if any) most likely
reflects a genuine DX3 rotation-handling defect (real backend bug) — the two failures are of
different natures and should be triaged differently, not treated as a single combined regression.
