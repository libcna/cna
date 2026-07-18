# Audit: examples/easygl_model_json_reader_test.cpp

## Metadata

- Source file: `examples/easygl_model_json_reader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `.model.json`/`.cnj` `ModelTypeReader` baseline
  vertex-correctness regression test (also compiled/run under `examples-tests-vulkan`,
  `cmake/Tests/VulkanTests.cmake` line 812)
- File type: C++ example/integration-test executable (`ModelJsonReaderTest : Game`, `main()`)
- Related production code: `ModelTypeReader::Read()` (`ContentManager.cpp`, class starting line 2086),
  `BuildVertexBufferFromRawBytes` (lines 1620-1691, stride-32 branch specifically)
- XNA/FNA relevance: the resulting `Model`, `BasicEffect` defaults (`LightingEnabled=false`, `DiffuseColor=white`,
  untextured), and `Model.Draw` are real XNA 4.0 behavior; the `.model.json` schema itself is `NOXNA`
- Main related tests: this file (Task 927) — the foundational test the `32bit_indices`/`texture`/
  `bone_hierarchy`/`skeleton` sibling tests in this batch all explicitly build on and reference in their own header
  comments

## Purpose

Regression test for a specific, confirmed historical bug (per header comment, DEFERRED.md item #26):
`ModelTypeReader::Read()` used to compare the JSON-declared `"vertexStride"` directly against `sizeof(...)` of
CNA's own vertex structs, then `reinterpret_cast` tightly-packed file bytes as an array of those structs — silently
reading every vertex's fields from the wrong byte offset, because every CNA vertex struct publicly inherits the
polymorphic `IVertexType` (adding a vtable pointer XNA's own C# interface never carried), inflating `sizeof()` past
the file's "clean" packed stride. Writes a real `.cnj`/binary stride-32 `VertexPositionNormalTexture` fixture (a
flat quad covering NDC `[-0.5,0.5]`), loads via `Content.Load<Model>()`, renders with identity transforms and
`BasicEffect` defaults, and samples center (must be White) vs. outside-quad-bounds (must be Blue background)
pixels. Correct placement per `AUDIT_SCOPE.md`.

## Executive Verdict

**Healthy** — the fixture's byte layout and the production reader's actual stride-32 handling
(`BuildVertexBufferFromRawBytes`) were independently checked field-by-field in this audit and match exactly; this
is the foundational, correctly-designed regression test for a real, well-documented prior data-corruption bug.

## Checklist Results

### API / XNA / FNA parity
Renders with `BasicEffect` in its post-`ModelTypeReader::Read()` default state (`LightingEnabled=false`,
`DiffuseColor=white`, untextured — set by the reader itself, not this test) — matches real XNA's `BasicEffect`
default-constructed field values (`FNA/src/Graphics/Effect/StockEffects/BasicEffect.cs`'s own field initializers),
so a correctly-positioned, untextured white quad rendering as solid White is the XNA-correct expectation, not an
arbitrary choice.

### Behavioral correctness
Verified the vertex fixture (`verts[4]`, lines 99-104: stride-32 `{x,y,z, nx,ny,nz, u,v}` per vertex) against
`BuildVertexBufferFromRawBytes`'s `stride == 32` branch (`ContentManager.cpp` lines 1669-1676):
`readVec3(o)` (position, offset 0), `readVec3(o+12)` (normal, offset 12), `readVec2(o+24)` (texcoord, offset 24) —
exact field-offset match to the fixture's own byte-packing order (`AppendFloat` calls at lines 107-109 write
`x,y,z,nx,ny,nz,u,v` sequentially, i.e. position@0, normal@12, texcoord@24) — this is precisely the offset
arithmetic whose *absence* (a direct `sizeof(VertexPositionNormalTexture)`-based reinterpret_cast, which would have
been 40 bytes, not 32, due to the vtable pointer) caused the original bug the test's header describes. The
quad's NDC extent (`[-0.5, 0.5]`, smaller than the full `[-1,1]` screen) is a deliberate design choice (noted in
the header comment) so a corrupted/scrambled position would very likely miss the sampled center point entirely,
maximizing the test's discriminating power against silent corruption rather than a coincidental pass.

### Logic
Two-point sampling (center=White, `(0.8,0.8)`=Blue-background) with `isWhite`/`isBackground` threshold predicates
(`R/G/B >= 200` / `R/G <= 30 && B >= 200` respectively) — non-overlapping, unambiguous thresholds for the two
colors actually in play (pure white vs. pure blue), a sound check design.

### Memory/resource lifetime
Standard shard pattern: unique per-process temp directory (line 89-91), `GraphicsDeviceManager` constructed in the
test class's own constructor with an explicit small 64×64 backbuffer (lines 204-207) — appropriately small for a
single-quad pixel-readback test, reducing per-run cost without affecting correctness (the two sample points are
resolution-independent NDC coordinates).

### C++ correctness
`AppendFloat`/`AppendUint16` (lines 62-74) use `std::memcpy`, consistent with the production reader's own
`std::memcpy`-based reads — no aliasing UB on either side of the round trip.

### Performance
N/A — a single 4-vertex/6-index quad, trivially cheap for a `TIMEOUT 30` test.

### Thread safety
N/A.

### Architecture
Correctly placed; exercises the real `ContentManager`/`ModelTypeReader` path end-to-end through public
`Content.Load<Model>()`.

### Maintainability
218 lines; the header comment (lines 1-28) is precise about the bug's root cause (`IVertexType`'s vtable pointer
inflating `sizeof()`), the fixture's deliberate design rationale, and the two-check pass/fail contract.

### Portability
N/A.

### Robustness
Same shared shard-wide unguarded-`Load<Model>()`-exception characteristic noted in sibling reports in this batch
(not re-scored as a distinct finding here; see `easygl_model_json_reader_32bit_indices_test.cpp`'s F1 for the full
writeup, equally applicable to this file's line 156 `Load<Model>()` call).

### Testing
This is itself a test file.

### Cross-file consistency
This file's own header comment explicitly distinguishes its scope from `easygl_model_draw_test.cpp` ("that path is
already covered... Task 144") — correctly avoiding redundant test intent while still building the identical
rendering path (`Model::Draw` → `BasicEffect` → pixel readback) on top of a different loading mechanism
(`Content.Load<Model>()` vs. hand-built `Model`).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. No new findings distinct from the shared, shard-wide observations already
recorded in this batch's other reports (unguarded `Load<Model>()` exception path — LOW severity, already written up
in full against `easygl_model_json_reader_32bit_indices_test.cpp`'s F1 rather than repeated verbatim here).

## Cross-File Observations

- This file is the structural/byte-layout template every other `.model.json`-loading test in this shard extends:
  `easygl_model_json_reader_32bit_indices_test.cpp` reuses its exact `isWhite`/`isBackground`/`sample()` pattern for
  a >65535-vertex variant, `easygl_model_json_reader_texture_test.cpp` reuses it for a textured variant. All three
  were independently cross-checked in this audit against the same `BuildVertexBufferFromRawBytes` stride-32 branch
  and are mutually consistent.

## Missing or Weak Tests

- No case in this file for a **stride other than 32** (e.g. 16/20/24) — covered instead by other files elsewhere in
  the broader Model test family (not part of this batch), so not a gap specific to this file's own stated scope.

## Positive Findings

- Textbook regression-test construction: the fixture's byte layout was chosen specifically to distinguish
  "reads at the correct packed offset" from "reads at the inflated `sizeof()` offset" — independently verified in
  this audit to actually do so against the real production code, not merely asserted by the header comment.
- Correctly and explicitly scopes itself against the sibling hand-built-model test to avoid duplicate test intent.

## Final Assessment

A precise, well-documented, and independently-verified regression test for a real, previously-confirmed vertex-
data-corruption bug. It is also the structural foundation the shard's other `.model.json`-loading tests (32-bit
indices, texture binding) correctly build upon rather than duplicate. No correctness issues found in this file
itself.
