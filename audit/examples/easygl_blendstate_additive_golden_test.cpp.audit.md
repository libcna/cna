# Audit: examples/easygl_blendstate_additive_golden_test.cpp

## Metadata

- Source file: `examples/easygl_blendstate_additive_golden_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ integration-test executable source (single `Game`/`PixelTestGame` subclass + `main()`)
- Lines: 75
- Registered as: `cna_test_easygl_blendstate_additive_golden` (`cmake/Tests/EasyGLTests.cmake:92-95`), CTest name
  `EasyGL_BlendState_Additive_Golden`, `TIMEOUT 30`, `WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"` (repo root —
  required so the relative golden-image path resolves), `SDL_VIDEODRIVER=x11`. **EasyGL-only** — unlike five of its
  seven sibling files in this batch, this file is not reused by any other backend's CMake test registration (grep
  confirmed no `vulkan`/`d3d9`/`d3d11`/`bgfx` test references this filename).
- Related production code: `Microsoft::Xna::Framework::Graphics::BlendState` (`BlendState.cpp`/`.hpp`),
  `GraphicsDevice::setBlendStateProperty` (`GraphicsDevice.cpp:1667-1682`),
  `EasyGLGraphicsBackend::ApplyBlendState` (`EasyGLGraphicsBackend.cpp:1904-1922`), the shared
  `CNA::Examples::PixelTestGame` harness (`examples/common/PixelTestGame.hpp`).
- XNA/FNA relevance: exercises `BlendState::Additive`, an XNA-facing preset; judged against
  `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/BlendState.cs`.
- Golden fixture: `examples/golden/easygl_blendstate_additive_golden_test.png` — confirmed present, valid PNG
  (signature `89 50 4E 47 0D 0A 1A 0A`), 8×8 pixels, matching the 8×8 region requested by `CompareGoldenImage`.

## Purpose

Task 467: a golden-image regression test that reuses the exact scene from Task 306
(`easygl_blendstate_additive_test.cpp`, audited separately in this batch) but validates the rendered result two
ways in one file — a literal-value `ExpectPixel` check (the file's own docstring calls this "independent of the
golden PNG's own contents", matching the stated rationale for Tasks 464-466's identical pattern) and a
`CompareGoldenImage` check against a checked-in reference PNG via the shared `PixelTestGame` harness. Placement
under `examples/` as a backend-named integration test is correct per `AUDIT_SCOPE.md`'s example-treatment rule.

## Executive Verdict

**Healthy.** The test is a faithful, well-reasoned reuse of an already-audited scene, its dual validation strategy
(literal value + golden image) is a genuine belt-and-suspenders design rather than duplicated boilerplate, and the
golden fixture actually exists, is well-formed, and matches the requested region size.

## Checklist Results

### API / XNA / FNA parity
`BlendState::Additive` is used, not defined, here — parity for the preset itself was checked in the sibling
`easygl_blendstate_additive_test.cpp.audit.md` report and in `BlendState.cpp`/FNA cross-check (both this file and
its sibling assert the identical (255,150,0) expectation derived from `colorDestinationBlend=One`, confirmed
against FNA's own `BlendState.Additive` preset: `Blend.SourceAlpha, Blend.SourceAlpha, Blend.One, Blend.One`).

### Behavioral correctness
Same scene as Task 306: `Clear(Color(200,50,0,255))` → `BlendState::Additive` → draw an opaque
`Color(255,100,0,255)` full-screen quad. At source alpha=255, `SourceAlpha` collapses to a scale of 1.0, so
R=255+200 (saturates to 255) and G=100+50=150 exactly — the file's own comment correctly identifies this as "the
distinguishing check that catches Task 868's Vulkan bug" (see Cross-File Observations for this reference's current
accuracy). `RasterizerState::CullNone` is set before drawing (line 58) — verified necessary: the six vertices
(`(-1,1),(-1,-1),(1,-1),(-1,1),(1,-1),(1,1)`) form a CCW-wound triangle in NDC, and FNA's own
`RasterizerState()` constructor defaults `CullMode = CullMode.CullCounterClockwiseFace` (confirmed directly in
`RasterizerState.cs:127`), so without `CullNone` this quad would be culled and the test would read back the clear
colour instead — the "Task 896 finding" comment is accurate.

### Logic
`ExpectPixel` samples a single centre pixel; `CompareGoldenImage` samples an 8×8 block also centred on the
viewport. Since the quad is genuinely full-screen (`-1..1` NDC on both axes, identity `BasicEffect` matrices), no
part of an 8×8 region anywhere near the centre can straddle an edge — the comment's own reasoning
("A full-screen quad means any crop is safe") is correct and matches the actual vertex data.

### Memory/resource lifetime
No manual resource management — `BasicEffect fx(device)` is stack-constructed and torn down normally at scope
exit; `PixelTestGame`'s `Draw()` override runs `RunTest()` exactly once (`done_` latch) then calls `Exit()`. No
leak/UAF surface in this file.

### C++ correctness
`const VertexPositionColor verts[6]` — plain aggregate array, no lifetime issues (passed by pointer/count into
`DrawUserPrimitives`, which is documented to complete synchronously within the call). No casts, no UB, no
signed/unsigned concerns in this file's own code.

### Performance
N/A for a single-shot correctness test — one draw call, one readback, one small-image comparison; not a hot path.

### Thread safety
N/A — single-threaded example executable.

### Architecture
Correctly written against the public `Microsoft::Xna::Framework` API surface only (`BlendState`, `BasicEffect`,
`RasterizerState`, `GraphicsDevice`), plus the shared `CNA::Examples::PixelTestGame` NOXNA test harness — no
backend-internal types leak into this file, appropriate for an examples-level integration test.

### Maintainability
Small (75 lines), single-purpose, well-commented. One thing worth flagging: this file's own docstring references
"Task 868's Vulkan bug" as design rationale for *why* the G-channel check was chosen this way — see Cross-File
Observations; unlike its sibling `easygl_blendstate_additive_test.cpp`, the reference here is phrased as
"what the check would catch" rather than "this bug is currently present," which is a meaningfully lower-risk framing
even though Task 868 has since been closed.

### Portability
N/A — no platform-conditional code.

### Robustness
`RunPixelTest<TGame>()` (in the shared harness, not this file) already handles the no-GPU/no-display case via a
preflight `SDL_InitSubSystem` probe and a documented `kSkipExitCode` (77) — this file inherits that robustness for
free by using `PixelTestGame`/`RunPixelTest` rather than hand-rolling its own `main()`, unlike five of its six
sibling files in this batch which do hand-roll a `Game` subclass without that preflight check (see Cross-File
Observations).

### Testing
This *is* a test file; there is no "test of this test" beyond CTest itself. The dual ExpectPixel+golden-image
strategy is a genuine strengthening of Task 306's original two-range-check test, not mere duplication — the
golden-image comparison catches differences the two hand-picked range checks wouldn't (e.g. B-channel or edge
artifacts across the sampled 8×8 block).

### Cross-file consistency
Reuses Task 306's own tolerance (10) and literal expected value (255,150,0) verbatim, correctly kept in sync with
`easygl_blendstate_additive_test.cpp`'s own assertions (cross-checked directly — both files assert `>=250` /
`[140,160]`-equivalent bands around the same numbers).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings for this file specifically — its Vulkan/Task 868 reference is a lower-risk,
rationale-only framing (see Purpose/Maintainability above), and it is not itself cross-registered to run on
Vulkan, so the staleness discussed in the sibling `easygl_blendstate_additive_test.cpp` report does not directly
misrepresent *this* file's own current CI behavior.

### F1 — Golden-image tolerance (10) does not obviously account for a full-screen-quad scene's near-zero variance

- Severity: LOW
- Confidence: LOW
- Category: testing / robustness
- Location/symbol: `CompareGoldenImage("blendstate-additive", ..., /*tolerance=*/10)` (line 65-68)
- Evidence: for a genuinely flat, unblended, full-screen colour region (as this scene is, per the file's own
  reasoning), a tolerance of 10 is generous — reasonable given the project's own documented cross-backend/driver
  precision variance (per `PixelTestGame.hpp`'s comment referencing "Task 462's own 98-file tolerance survey"), so
  this is not a defect, just a note that this specific golden image is one of the "flat colour" cases where a
  tighter tolerance would likely still pass and give slightly more regression-catching power.
- Why it matters: purely a suggestion for tightening test sensitivity, not a correctness gap.
- FNA/XNA comparison: N/A.
- Suggested future action: none required; noted for completeness only.

## Cross-File Observations

- This file demonstrates the project's own `PixelTestGame`/`RunPixelTest<TGame>()` convention working as intended
  (headless-safe skip, single `RunTest()` override, `ExpectPixel`/`CompareGoldenImage` helpers) — a good comparison
  point against the other seven files in this batch, six of which still hand-roll their own `Game` subclass with a
  duplicated `done_`/`result_`/`Draw()`/`main()` shape that `PixelTestGame` exists specifically to replace (per that
  header's own comment, existing files were deliberately *not* retrofitted — this file is one of the newer ones
  that did opt in).
- Confirms (via `EasyGLTests.cmake:90-95`) that `WORKING_DIRECTORY` is explicitly set to
  `${CMAKE_CURRENT_SOURCE_DIR}` for this test, which is what makes the file's hardcoded relative golden-image path
  (`"examples/golden/easygl_blendstate_additive_golden_test.png"`) resolve correctly at `ctest` runtime.

## Missing or Weak Tests

None specific to this file — it is itself the strengthening test for Task 306's original coverage. A logical
follow-up (not a gap in this file) would be an equivalent golden-image test for `BlendState::Opaque`/
`AlphaBlend`/`NonPremultiplied`, none of which currently have a golden-image counterpart in this shard (only
`Additive` does).

## Positive Findings

- Genuine dual-validation design (literal pixel check + golden image), not boilerplate duplication.
- Correctly reasons about geometry (full-screen quad ⇒ any crop is edge-safe) rather than asserting it.
- Golden PNG fixture verified to actually exist, be well-formed, and match the requested region dimensions (8×8).
- Uses the newer, less error-prone `PixelTestGame` shared harness instead of hand-rolling boilerplate.

## Final Assessment

A well-constructed regression test that correctly reuses and strengthens an already-audited scene, backed by a
verified real golden-image fixture. No correctness issues found in the test itself.
