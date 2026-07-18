# Audit: examples/rasterizerstate_cullmode_camera_test.cpp

## Metadata

- Source file: `examples/rasterizerstate_cullmode_camera_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — a genuinely backend-agnostic, verbatim-shared source
  file, confirmed registered identically on **all 3 runnable backends**:
  `cmake/Tests/EasyGLTests.cmake:1464` (`EasyGL_RasterizerState_CullMode_Camera`),
  `cmake/Tests/VulkanTests.cmake:246` (`Vulkan_RasterizerState_CullMode_Camera`),
  `cmake/Tests/BgfxTests.cmake:685` (`Bgfx_RasterizerState_CullMode_Camera`) — this is one of the
  clearest true cross-backend-diagnostic files in the whole batch, exactly matching the task
  framing's expectation for this shard.
- File type: standalone `Game`-subclass executable, CTest-registered per backend.
- XNA/FNA relevance: direct — `RasterizerState.CullMode`
  (`CullMode.None`/`CullClockwiseFace`/`CullCounterClockwiseFace`), interacting with a real
  `Matrix.CreateLookAt`/`CreatePerspectiveFieldOfView`/`CreateOrthographic` camera pipeline.
- Related production/investigation docs: `docs/xna_culling_compatibility_audit.md` (the
  authoritative, RESOLVED investigation this file is Phase 1 of), `src/Microsoft/Xna/Framework/
  Graphics/RasterizerState.cpp`, `Matrix.cpp` (`CreateLookAt`).

## Purpose

Extends the pre-existing identity-transform-only `easygl_rasterizerstate_cullmode_test.cpp`
(Tasks 323-325/765) with a **real** camera (perspective/orthographic projection +
`Matrix::CreateLookAt`, using `SimpleAnimation`'s exact `eye`/`target`/`up`/FOV values) to determine
whether CNA's cull-mode-to-native mapping is genuinely correct once a non-trivial
World/View/Projection chain is involved, or only *appears* correct under
`World=View=Projection=Identity`. Method: (1) find each triangle's own on-screen pixel by rendering
it alone under `CullMode::None` (never hardcoding a screen coordinate), (2) independently compute
each triangle's NDC-space signed area via CNA's own `Vector4::Transform` + manual perspective
divide to derive a "predicted" winding label, (3) render both triangles together under 4
`RasterizerState` configurations and check actual visibility against the step-2 prediction. Five
scenarios: (a) identity anchor, (b) orthographic+`CreateLookAt`, (c) perspective+`CreateLookAt`,
(d) same camera + positive-determinant World, (e) same camera + negative-determinant (mirrored)
World — (e) is documented as an intentionally-expected winding flip, not a bug.

## Executive Verdict

**Healthy** — this audit independently re-derived the NDC signed-area math for the identity-anchor
scenario and confirmed it matches the file's own established rule, confirmed the file's own
documented methodology and results (30/30 PASS on all 3 backends) against the authoritative
`docs/xna_culling_compatibility_audit.md`, and confirmed the file's cross-basis-vector math (camera
`right`/`up` derivation) is internally consistent with `Matrix::CreateLookAt`'s own convention. No
correctness defects found. One documentation-completeness observation (F1, LOW/INFO) about an
open question the project's own Bgfx CMake comment raises but which this audit was able to resolve
by cross-referencing the authoritative investigation doc.

## Checklist Results

### Purpose
Correctly placed; genuinely backend-agnostic C++ (no backend-specific include or macro anywhere in
the file) reused verbatim across 3 CMake registrations — confirmed by direct comparison of the
`EasyGLTests.cmake`/`VulkanTests.cmake`/`BgfxTests.cmake` snippets, all pointing at the identical
source path with only the wrapping macro (`cna_easygl_test`/`cna_vulkan_test`/`cna_bgfx_test`)
differing.

### API / XNA / FNA parity
`RasterizerState`, `CullMode::{None,CullClockwiseFace,CullCounterClockwiseFace}`,
`BlendState::Opaque`, `Matrix::CreateLookAt`/`CreatePerspectiveFieldOfView`/`CreateOrthographic`,
`Vector4::Transform`, `GraphicsDevice::DrawUserPrimitives`/`GetBackBufferData` all used per their
real XNA 4.0 signatures.

### Behavioral correctness
Independently re-derived scenario (a)'s NDC signed area for both anchor triangles under identity
World/View/Projection using the file's own `NdcSignedArea` formula (standard shoelace on NDC X/Y):
for `MakeCw(Vector3(-0.5,0,0), 0.45, 0.45)` (vertices `v0=(-0.95,-0.45)`, `v1=(0,0.45)`,
`v2=(-0.05,-0.45)` after the CW vertex swap `{v0,v2,v1}`), hand-computing the shoelace sum
independently gives a negative signed area — matching the file's own inline comment ("predicted to
survive default") and its own established sign convention.

Verified the camera-basis math used to place scenario (b)-(e)'s triangles
(`camBack=Normalize(eye-target)`, `camRight=Normalize(Cross(up,camBack))`,
`camUp=Cross(camBack,camRight)`) is the same right-handed look-at basis construction
`Matrix::CreateLookAt` itself uses internally, so the triangles are guaranteed to sit in the
camera's actual screen-right/up plane rather than an arbitrary world-axis offset that might miss
the frustum — this reasoning was checked conceptually against `Matrix.cpp`'s `CreateLookAt`
implementation and found consistent (same `Cross(up, forward)`-style right-vector derivation).

Scenario (e)'s framing (negative-determinant World is an *expected* flip, checked against its own,
also-flipped, prediction rather than (c)/(d)'s) is the methodologically correct way to test a
winding-preservation property without conflating "flips because of a real geometric mirror" with
"flips because of a framework bug."

### Logic
`RunScenario()`'s `checkUnderMode` closure correctly maps `CullMode::None` to
"both always visible," `CullCounterClockwiseFace` to "only NDC-predicted-CW survives," and
`CullClockwiseFace` to "only NDC-predicted-CCW survives" — internally consistent with the
established sign convention and applied identically across all 5 scenarios via the shared
`RunScenario()` helper (no per-scenario copy-paste divergence risk).

### Robustness
`FindPixel()` scans the **entire** framebuffer rather than assuming a screen coordinate, and the
`fatal_` counter distinguishes "geometry didn't even land on screen" (a setup failure, always
counted as a hard failure via `getResult()`) from an ordinary `PASS`/`FAIL` — a meaningfully
different, correctly-separated failure mode from an actual cull-mode mismatch.

### Testing
This file (unlike its sibling `rasterizerstate_cullmode_indexed_basiceffect_test.cpp`) does **not**
carry a Task-406-style retry loop around its `GetBackBufferData()` calls — see F1 for why this was
investigated and found not to be a live problem.

### Cross-file consistency
Shares the `Tri`/`NdcSignedArea`/`ToNdc`/`AddScaled` helper shapes near-verbatim with
`rasterizerstate_cullmode_indexed_basiceffect_test.cpp` (both files independently define the same
small set of local math helpers rather than sharing a common header — minor duplication, see
Cross-File Observations, not elevated to a numbered finding since both are self-contained CTest
executables and the duplication is small, low-risk, and localized to test-only code).

## Detailed Findings

_(No CRITICAL/HIGH/MEDIUM correctness findings. One LOW/INFO documentation-completeness item.)_

### F1 — This file (unlike its sibling) has no Bgfx Task-406 retry-loop mitigation for `GetBackBufferData()`, but the project's own investigation doc already confirms this was not needed

- Severity: INFO
- Confidence: HIGH
- Category: documentation completeness / test-design consistency
- Location/symbol: `FindPixel()` (lines 174-188) and `SampleAt()` (lines 190-195) — both call
  `dev.GetBackBufferData(...)` exactly once, with no retry loop, unlike
  `rasterizerstate_cullmode_indexed_basiceffect_test.cpp`'s `findOne` lambda, which wraps the same
  operation in a `for (int attempt = 0; attempt < 10; …)` retry explicitly citing "Bgfx's
  `GetBackBufferData()` only reliably reflects the FIRST read per rendered frame (Task 406)."
- Evidence: `cmake/Tests/BgfxTests.cmake:680-683`'s own comment for this exact file's Bgfx
  registration says *"first tried on Bgfx as-is; see the audit doc for whether Bgfx's own
  multiple-reads-per-frame `GetBackBufferData` limitation, Task 406, required a restructure
  here"* — i.e. the CMake comment itself leaves this as an open question. This audit cross-checked
  `docs/xna_culling_compatibility_audit.md` §4.1, which states: *"Result: 30/30 checks PASS on
  EasyGL, Vulkan, and Bgfx — identical outcome on all 3 backends"* — a real, already-executed
  empirical result confirming no restructure was in fact needed for this specific file's call
  pattern (each `checkUnderMode`/`findOne` call does a full `Clear`+state-set+`Draw`, and
  `BgfxGraphicsBackend::ReadBackbuffer()` (confirmed by reading `BgfxGraphicsBackend.cpp:302-322`)
  internally drives its own up-to-3-attempt `bgfx::frame()` advance loop as part of every single
  `GetBackBufferData` call regardless of caller-side retry logic, which is a plausible mechanism for
  why this file's single-call-per-check pattern did not in practice need an outer retry).
- Why it matters: not a live defect — the empirical 30/30 result already settles the open question
  the project's own CMake comment raises, so a future reader encountering that comment does not need
  to re-investigate from scratch; this finding exists to close that loop and connect the CMake
  comment to the doc that already answers it, per this audit's mandate to cross-check
  known-open-question comments against reality.
- FNA/XNA comparison: N/A.
- Related files: `cmake/Tests/BgfxTests.cmake`, `docs/xna_culling_compatibility_audit.md`,
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`ReadBackbuffer`).
- Suggested action (not implemented by this audit): optionally update the `BgfxTests.cmake`
  comment to state the question was resolved ("no restructure needed, confirmed 30/30") rather than
  leaving it phrased as still-open, to save a future reader the doc cross-reference this audit just
  performed.

## Cross-File Observations

- This file and `rasterizerstate_cullmode_indexed_basiceffect_test.cpp` both independently define
  `Tri`, `AddScaled`, `ToNdc`, `NdcSignedArea` (near-identical implementations) rather than sharing
  a common header — small, low-risk duplication local to test-only executables; not worth a
  separate numbered finding given both files are short, self-contained, and unlikely to drift
  silently (any drift would immediately show up as a mismatch between the two files' otherwise
  identical math, which this audit's independent re-derivation would have caught).
- `docs/xna_culling_compatibility_audit.md`'s "Status: RESOLVED" framing and its account of the
  investigation's actual conclusion (a genuine triangle-winding-data bug in `tank_*_idx.bin` mesh
  assets, not a CNA framework bug) is fully consistent with this file's own conclusion in §4.3:
  "CNA's `CullMode`/`RasterizerState` implementation is self-consistent … across all 3 backends" —
  this file's role was correctly scoped to ruling out an *internal* framework inconsistency, and it
  did so successfully; the actual root cause (found later in the doc, §5-§6) required different
  evidence this file was never meant to provide.

## Missing or Weak Tests

- None found specific to this file; its scope (winding self-consistency across projection types,
  World transforms, and backends) is fully and correctly covered for the stated Phase-1 goal.

## Positive Findings

- The pixel-scan-the-whole-framebuffer approach (`FindPixel`) and the NDC-signed-area-prediction
  methodology are both genuinely rigorous techniques that avoid the common test-authoring trap of
  hand-computing an expected screen coordinate (which would silently bake in the same assumption
  being tested).
- Scenario (e)'s explicit "this flip is expected, not a bug" framing, checked against its own
  scenario-local prediction rather than a shared one, is methodologically correct and avoids a
  false-failure trap.
- This audit's independent re-derivation of the identity-scenario NDC signed area, and its
  cross-check of the CMake-comment's open question against the authoritative investigation doc,
  both corroborate that this file's own claims (30/30 PASS, cross-backend self-consistency) are
  real, not just asserted.

## Final Assessment

A rigorous, well-evidenced cross-backend diagnostic test whose own claims this audit was able to
independently verify via hand-derivation and cross-referencing the authoritative investigation
document. No correctness defects; only an informational note connecting an open CMake comment to
its already-existing answer.
