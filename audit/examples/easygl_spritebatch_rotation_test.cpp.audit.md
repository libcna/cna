# Audit: examples/easygl_spritebatch_rotation_test.cpp

## Metadata

- Source file: `examples/easygl_spritebatch_rotation_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — SpriteBatch rotation-around-origin pixel test ("Task 417")
- File type: single-frame `Game`-subclass pixel-readback executable (hand-rolled `main()`)
- XNA/FNA relevance: exercises `SpriteBatch::Draw`'s rotation/origin pivot, directly comparable to
  FNA's `SpriteBatch.cs` `GenerateVertexInfo` corner-rotation formula.
- Build/registration: `cmake/Tests/EasyGLTests.cmake` → `cna_test_easygl_spritebatch_rotation`
  (`EasyGL_SpriteBatch_RotationAroundOrigin`); **also reused verbatim** by
  `cmake/Tests/VulkanTests.cmake` → `cna_test_vulkan_spritebatch_rotation`
  (`Vulkan_SpriteBatch_Rotation`).
- Main related production file: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EasyGLSpriteBatchBackend::Draw`, lines 1189-1273).
- Reused by: `easygl_spritebatch_rotation_golden_test.cpp` (Task 465), which wraps this exact scene in
  a golden-image comparison — see that file's own audit report for cross-verification.

## Purpose

Verifies that `SpriteBatch::Draw`'s rotation genuinely pivots around the caller-specified `origin`
point (in source-texture pixel space), not the destination rectangle's top-left corner and not the
sprite's own center — the classic XNA rotation-pivot semantics. Uses a 100×100 texture (top-left
20×20 red marker, rest blue) drawn at `(200,150,100,100)` with `origin=(100,100)` (deliberately the
texture's own opposite corner from the marker) rotated 90°.

## Executive Verdict

**Healthy.** Independently re-derived the expected marker relocation from the actual
`EasyGLSpriteBatchBackend::Draw` formula and it matches the file's own stated expectation exactly;
the 3 sample points are well-chosen to discriminate a true pivot-around-origin implementation from
plausible wrong-pivot bugs.

## Checklist Results

### API / XNA / FNA parity
Uses the 8-arg `SpriteBatch::Draw(texture, destinationRectangle, sourceRectangle, color, rotation,
origin, effects, layerDepth)` overload. The header comment's own formula derivation (lines 9-13):
`cornerX = sx - originX` (origin subtracted *before* rotation), then
`X' = destinationX + cornerX*cos(rotation) - cornerY*sin(rotation)` — this matches FNA's real
`GenerateVertexInfo` corner-rotation math conceptually (subtract origin, rotate, translate by
destination), and matches CNA's actual implementation in `EasyGLSpriteBatchBackend::Draw` (lines
1240-1258): `p0x = (0-ox)*scaleX ... rotateAndTranslate` — the same "subtract origin in source space,
scale, rotate, translate by destination" pipeline, confirmed line-by-line.

### Behavioral correctness
Re-derived the marker's expected screen position independently of the file's own comment: source
point `(10,10)` (the marker's center within the 20×20 red block) with `origin=(100,100)`,
`scale=1` (100×100 dest ÷ 100×100 src): `cornerX=10-100=-90`, `cornerY=10-100=-90`; at `rotation=
PiOver2` (`sin=1, cos=0`): `X'=200+(-90)*0-(-90)*1=290`, `Y'=150+(-90)*1+(-90)*0=60` — matches the
test's own check `{290, 60, kRed, ...}` (line 116) exactly. Confirmed the affine map (rotation +
uniform-scale + translation) applies identically to interior points and quad corners, so validating
via a single interior sample point (rather than a quad corner) is mathematically sound, not a lucky
coincidence.

### Logic
3 check points (lines 114-119) are well-chosen to discriminate specific wrong implementations:
`(290,60)` (must be Red — proves genuine pivot, since a naive "rotate around top-left" or "rotate
around center" implementation would place the marker elsewhere), `(250,100)` (must be Blue — inside
the rotated sprite's own axis-aligned bounding box `x:[200,300], y:[50,150]` but away from the
marker, ruling out an accidental "whole sprite recolored" bug), and `(50,50)` (must be the clear
background color — confirming no stray coverage/oversized quad). This 3-point combination is a
meaningfully stronger check than a single hit-test.

### Memory/resource lifetime
`tex_` constructed once in `Initialize()`, 100×100 `SetData` call — no lifetime concerns; standard
pattern for this shard.

### C++ correctness
`colourMatch` (lines 53-58) widens to `int` before `std::abs` on each channel — correct, avoids
`uint8_t` underflow.

### Performance
N/A — single-frame test.

### Thread safety
N/A.

### Architecture
Clean, minimal `Game` subclass; no backend-specific code leaks into the test itself (all drawing goes
through the shared `Microsoft::Xna::Framework::Graphics` API), which is exactly why this same source
file compiles unmodified against both EasyGL and Vulkan CTest targets (see Cross-File Observations).

### Maintainability
Header comment (lines 1-30) states the exact formula, works the math for the specific test scene, and
explains why each of the 3 sample points was chosen — audit-friendly documentation.

### Portability
N/A at the file level (backend selection happens at the CMake/build level, not in this source).

### Robustness
`result_` defaults to `0` (pass-until-proven-otherwise) — same minor pattern noted in sibling files
in this shard (see Cross-File Observations); not a live risk given the single-frame `Game::Run()`
control flow, but worth a single project-wide note rather than a per-file one.

### Testing
This file is itself a test; see Missing or Weak Tests.

## Detailed Findings

No correctness defects found in this file.

## Cross-File Observations

- **Verbatim cross-backend reuse**: registered as both `cna_test_easygl_spritebatch_rotation`
  (`cmake/Tests/EasyGLTests.cmake` line 780) and `cna_test_vulkan_spritebatch_rotation`
  (`cmake/Tests/VulkanTests.cmake` line 753) from the identical source file — confirms the rotation
  formula being tested is genuinely shared, backend-agnostic `SpriteBatch` behavior, not an
  EasyGL-specific code path; a regression caught here would equally indicate a Vulkan-side
  regression (or vice versa) since both link the same underlying `SpriteBatch.cpp`.
- Also independently reused as the scene for `easygl_spritebatch_rotation_golden_test.cpp`'s
  golden-image comparison (Task 465) — confirmed identical scene parameters between the two files
  during this audit (see that file's own report).
- Same non-`const` `SamplerState*` API wart noted across this shard applies here too:
  `const_cast<SamplerState*>(&SamplerState::PointClamp)` (line 100) is required because
  `SpriteBatch::Begin`'s `samplerState` parameter is `SamplerState*`, not `const SamplerState*`
  (`include/Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp` lines 118/134/152), even though
  `SamplerState::PointClamp` is itself a `static const` value never mutated by `Begin()`. Recorded here
  as a recurring cross-file observation rather than a defect in any one test file — the actual API
  surface lives in `SpriteBatch.hpp`, outside this shard.

## Missing or Weak Tests

- Only a 90° rotation is exercised; a second angle (e.g. 45°, where `sin`/`cos` are both non-trivial
  and rounding behavior in `EasyGLSpriteBatchBackend::Draw`'s `rotateAndTranslate` lambda is exercised
  more fully than the axis-aligned 90°/180°/270° special cases) is not covered by this file — though it
  may well be covered by a different file in the broader `spritebatch_rotation`-adjacent test family
  not in this batch.

## Positive Findings

- Independently-verified, mathematically correct pivot-around-origin test; formula matches both the
  file's own derivation and the actual EasyGL backend implementation exactly.
- 3-point check design meaningfully discriminates multiple plausible wrong-pivot bugs, not just a
  single "is anything red here" smoke check.
- Shared, verified as byte-identical in intent, across two backend CTest targets from one source file
  — genuine reuse, not incidental duplication.

## Final Assessment

A correct, well-designed rotation-pivot regression test whose math was independently re-derived here
and found to match both its own stated expectation and the real backend implementation. No defects
found.
