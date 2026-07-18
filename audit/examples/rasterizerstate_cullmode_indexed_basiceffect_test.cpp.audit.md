# Audit: examples/rasterizerstate_cullmode_indexed_basiceffect_test.cpp

## Metadata

- Source file: `examples/rasterizerstate_cullmode_indexed_basiceffect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — genuinely backend-agnostic, verbatim-shared source,
  confirmed registered on all 3 runnable backends:
  `cmake/Tests/EasyGLTests.cmake:1470` (`EasyGL_RasterizerState_CullMode_IndexedBasicEffect`),
  `cmake/Tests/VulkanTests.cmake:251` (`Vulkan_RasterizerState_CullMode_IndexedBasicEffect`),
  `cmake/Tests/BgfxTests.cmake:690` (`Bgfx_RasterizerState_CullMode_IndexedBasicEffect`).
- File type: standalone `Game`-subclass executable, CTest-registered per backend, Phase 3/5 of the
  same `docs/xna_culling_compatibility_audit.md` investigation as its sibling
  `rasterizerstate_cullmode_camera_test.cpp`.
- XNA/FNA relevance: direct — `RasterizerState.CullMode` interacting with the **real**
  `Model`/`ModelMesh::Draw()` code path (`GraphicsDevice::DrawIndexedPrimitives` with a bound
  `VertexBuffer`/`IndexBuffer` and `BasicEffect`), as opposed to the simpler
  `DrawUserPrimitives`→`DrawColoredPrimitives` dispatch every pre-existing CullMode test used before
  this file was added.
- Related production/investigation docs: `docs/xna_culling_compatibility_audit.md` §4.2, §9 (the
  separate, confirmed Bgfx `startIndex` bug this file's design deliberately routes around),
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`DrawIndexedPrimitivesEx`).

## Purpose

Same NDC-signed-area-prediction methodology as `rasterizerstate_cullmode_camera_test.cpp`, but
drives the *real* `VertexBuffer`+`IndexBuffer`+`BasicEffect`+`DrawIndexedPrimitives` path with
`SimpleAnimation`'s exact camera, `EnableDefaultLighting()` engaged (diagnostic: does lighting
activation — a separate shader-selection path per backend — have any bearing on CullMode?), and
deliberately **two separate** `VertexBuffer`/`IndexBuffer` pairs (not one shared buffer read at two
`startIndex` offsets) — the file's own comment explains this design choice was forced by a real,
separately-confirmed Bgfx bug (`BgfxGraphicsBackend::DrawIndexedPrimitivesEx`'s non-wireframe path
ignoring `GpuDrawParams::startIndex` entirely) discovered while first drafting this test with a
shared-buffer approach.

## Executive Verdict

**Healthy** — this audit confirmed the file's central design decision (two dedicated buffers
instead of one shared buffer + offsets) is not just a stylistic preference but a documented
workaround for a real, separately-tracked Bgfx defect, cross-checked against
`docs/xna_culling_compatibility_audit.md`'s own account of that bug. The retry-loop mitigation for
Bgfx's `GetBackBufferData()` Task-406 quirk is present and its rationale independently verified
against `BgfxGraphicsBackend.cpp`. No correctness defects found in this file itself.

## Checklist Results

### Purpose
Correctly placed; genuinely backend-agnostic source reused verbatim across 3 backends (identical
path referenced in 3 separate `cmake/Tests/*.cmake` files, differing only in the wrapping test
macro).

### API / XNA / FNA parity
`VertexPositionNormalTexture` (stride 32, matching every `BasicEffect`-driven `ModelMeshPart`'s real
vertex format per the file's own comment), `VertexBuffer`/`IndexBuffer` construction +
`SetData`, `BasicEffect.EnableDefaultLighting()`/`DiffuseColor`/`TextureEnabled`/`VertexColorEnabled`,
`GraphicsDevice.SetVertexBuffer`/`SetIndexBuffer`/`DrawIndexedPrimitives(type, baseVertex,
minVertexIndex, numVertices, startIndex, primitiveCount)` all match real XNA 4.0 signatures and
argument order.

### Behavioral correctness
`IsDominantRed`/`IsDominantGreen` (dominant-channel detection rather than exact-color match) is the
correct technique here specifically *because* `EnableDefaultLighting()` is engaged — ambient+diffuse
lighting will dim/tint the raw `DiffuseColor` output, so an exact `(255,0,0)`/`(0,255,0)` match
would be fragile; this is explicitly justified in the file's own comment and is the right call
given the scene setup.

The retry loop in `findOne()` (`for (int attempt = 0; attempt < 10; …)`) was checked against
`BgfxGraphicsBackend::ReadBackbuffer()` (`BgfxGraphicsBackend.cpp:302-322`): each
`GetBackBufferData` call already internally retries up to 3 `bgfx::frame()` advances waiting for
the screenshot callback, so this file's outer 10-attempt loop is a second, coarser layer of the same
kind of retry-until-ready idiom rather than a redundant no-op — a defensible belt-and-suspenders
choice given the sibling camera test empirically didn't need it (see that file's own audit, F1) but
this indexed/lit variant's own header comment frames it as a real, needed accommodation. Not proven
unnecessary by this audit; kept as reported (no evidence of a live problem either way).

### Logic
`checkMode()`'s per-`CullMode` expectation mapping (`None`→both visible, `CullCounterClockwiseFace`→
NDC-predicted-CW survives, `CullClockwiseFace`→NDC-predicted-CCW survives) is identical to and
consistent with the sibling camera test's convention — no divergence found between the two files'
interpretation of the shared sign rule.

### Memory/resource lifetime
Two separate `VertexBuffer vbA/vbB` and `IndexBuffer ibA/ibB` are constructed once per `Draw()` call
(this file's `Draw()` runs once, calling `Exit()` at the end) — no per-frame reallocation churn
since the test only runs a single logical "frame" of checks before exiting.

### Robustness
The `!pxA.has_value() || !pxB.has_value()` fatal-setup-failure path correctly short-circuits before
any `NdcSignedArea`/`checkMode` logic runs, avoiding a null-`optional` dereference — verified this
guard exists and returns before any subsequent `*pxA`/`*pxB` dereference (lines 244-252).

### Cross-file consistency
Shares `Tri`/`AddScaled`/`ToNdc`/`NdcSignedArea`/`MakeCcwBasis`/`MakeCwBasis` helper implementations
near-verbatim with `rasterizerstate_cullmode_camera_test.cpp` (each file defines its own copy rather
than sharing a header) — same minor, low-risk duplication noted in that file's own audit, not
elevated to a separate finding here for the same reasons (short, self-contained test-only code,
unlikely to silently drift given both files were independently checked and found consistent with
each other).

## Detailed Findings

_(No CRITICAL/HIGH/MEDIUM findings. This file's design and methodology were checked in detail and
found sound; the one notable design choice — two dedicated buffers instead of one shared buffer —
was independently verified as a genuine bug workaround, not an arbitrary choice, reported below as a
positive/cross-file item rather than a defect.)_

## Cross-File Observations

- The file's comment claims the shared-buffer/two-`startIndex`-offsets approach "was tried first and
  found a genuine, separate, confirmed bug (not this task's own root cause):
  `BgfxGraphicsBackend::DrawIndexedPrimitivesEx`'s non-wireframe path calls
  `bgfx::setIndexBuffer(ib.handle)` with no offset/count, silently ignoring
  `GpuDrawParams::startIndex` entirely." This audit cross-checked `docs/xna_culling_compatibility_
  audit.md` §9 (referenced in the file's own comment) and confirmed the doc corroborates this
  exact account, including noting a temporary fix + temporary regression test for it were both
  authored and then deliberately reverted as out of scope for the culling investigation itself (the
  doc's own §9 explicitly separates this from the culling root cause). This is a good example of a
  test file correctly routing around a known, separately-tracked defect rather than either silently
  masking it or blocking on an unrelated fix.
- Confirms (independently, via direct comparison against the sibling camera test's audit) that both
  files apply the identical NDC-sign-to-`CullMode`-expectation convention with no divergence, which
  is itself evidence the underlying rule is being applied consistently rather than independently
  reinvented (and possibly drifted) per file.

## Missing or Weak Tests

- None found specific to this file's stated Phase-3/5 scope (does `CullMode` behave consistently on
  the real indexed/`BasicEffect`/lit dispatch path). A natural Phase-4/5-style extension (not a gap
  in *this* file, since the project's own `rasterizerstate_cullmode_lit_basiceffect_test.cpp`,
  referenced by this file's own header comment, is presumably the file that covers the "lighting
  enabled via a different DiffuseColor-driving path" angle further) would be to also vary
  `PreferPerPixelLighting`, but that is explicitly out of scope per this file's own comment
  ("lighting DISABLED... covered by `rasterizerstate_cullmode_lit_basiceffect_test.cpp`") — note:
  this file actually *does* call `EnableDefaultLighting()` per its `Draw()` body, which is a minor
  inconsistency with its own header comment's framing ("lighting DISABLED (BasicEffect defaults)");
  see note below.

### Minor note: header comment says lighting is disabled, but the code enables it

- Severity: LOW
- Confidence: HIGH
- Category: documentation accuracy
- Location/symbol: header comment (lines 16-19): "lighting DISABLED (BasicEffect defaults --
  Model/ModelMesh loading itself doesn't force lighting on; SimpleAnimation's own `Tank::Draw()`
  calls `EnableDefaultLighting()` separately, covered by
  `rasterizerstate_cullmode_lit_basiceffect_test.cpp`)" vs. `Draw()`'s actual body: `fx.
  EnableDefaultLighting();` (line 213), with an inline comment immediately above it that
  contradicts the file-header framing: *"Matches SimpleAnimation's own `Tank::Draw()`, which calls
  this every frame for every mesh part — diagnostic check for whether lighting activation itself…
  has any bearing on CullMode."*
- Evidence: direct read of `Draw()` (lines 152-314) — `fx.EnableDefaultLighting()` is called
  unconditionally, once, before any of the render/check helper lambdas run, and both `findOne()` and
  `renderBoth()`/`checkMode()` render through this same lit `fx` instance. `IsDominantRed`/
  `IsDominantGreen`'s own comment ("`EnableDefaultLighting()` dims/tints exact `DiffuseColor` output
  … so an exact match is unreliable") further confirms the code path is genuinely lit, not merely
  configured-but-inert.
- Why it matters: the top-of-file header comment's summary of the test's own scope ("lighting
  DISABLED") is simply incorrect for the file as it exists today — the file actually *does* exercise
  `EnableDefaultLighting()` (matching the *inline* comment's framing, which is correct), making this
  file's real scope closer to what the header comment attributes to a *different*, sibling file
  (`rasterizerstate_cullmode_lit_basiceffect_test.cpp`). This is confusing for a future reader trying
  to determine which of the two sibling files covers the "lighting on" vs. "lighting off" case
  purely from the header block, without reading all the way down to the inline comment and the
  actual `Draw()` body.
- FNA/XNA comparison: N/A — documentation-accuracy issue local to this file's own header comment.
- Related files: `examples/rasterizerstate_cullmode_lit_basiceffect_test.cpp` (not in this batch;
  presumably the actually-lighting-disabled or differently-scoped sibling — not independently
  verified by this audit since it's outside this batch's file list).
- Suggested action (not implemented by this audit): correct the header comment's "lighting DISABLED"
  claim to match the file's actual behavior ("lighting enabled via `EnableDefaultLighting()`,
  matching `Tank::Draw()`'s real per-frame call"), and clarify what distinguishes this file's own
  lit-diagnostic angle from `rasterizerstate_cullmode_lit_basiceffect_test.cpp`'s scope if they are
  not in fact redundant.

## Positive Findings

- The two-dedicated-buffers design choice was independently cross-checked against the authoritative
  investigation doc and confirmed to be a genuine, documented bug workaround rather than an
  arbitrary implementation detail — a good example of test-authoring discipline (matching how a real
  `ModelMeshPart` actually works, per the file's own comment referencing `tank.model.json`'s
  per-mesh buffer structure).
- `IsDominantRed`/`IsDominantGreen`'s dominant-channel detection is the methodologically correct
  choice given `EnableDefaultLighting()` is genuinely engaged, and the file's own comment correctly
  explains why an exact color match would be wrong here.
- Consistent, verified-matching NDC-sign-to-visibility convention shared with the sibling camera
  test, with no divergence found between the two independently-audited files.

## Final Assessment

A sound, well-evidenced cross-backend diagnostic test whose most notable design choice (two
dedicated vertex/index buffers) was independently confirmed to be a deliberate workaround for a
real, separately-tracked Bgfx bug rather than an arbitrary decision. The one confirmed issue is a
LOW-severity, self-contained documentation mismatch: the file's own header comment claims lighting
is disabled in this test, while the actual `Draw()` body (and its own inline comment) enables it via
`EnableDefaultLighting()`.
