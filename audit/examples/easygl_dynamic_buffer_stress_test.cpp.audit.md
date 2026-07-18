# Audit: examples/easygl_dynamic_buffer_stress_test.cpp

## Metadata

- Source file: `examples/easygl_dynamic_buffer_stress_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (Task 238), `examples-tests-easygl` shard
- File type: C++ integration-test executable (`Game` subclass, `main()`), pixel-readback style
- Related production code: `include/Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp`,
  `include/Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp`,
  `src/Microsoft/Xna/Framework/Graphics/VertexBuffer.cpp`,
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EasyGLVertexBufferBackend::uploadWithOptions`/`SetDataWithOptions`,
  `EasyGLIndexBufferBackend::SetData16WithOptions`/`SetData32WithOptions`)
- XNA/FNA relevance: `DynamicVertexBuffer`/`DynamicIndexBuffer` and `SetDataOptions` (`None`/
  `Discard`/`NoOverwrite`) are real XNA 4.0 API surface; the streaming-update *semantics* (orphan
  vs. sub-data vs. full re-upload) are an XNA/D3D9-era performance contract, not merely a CNA
  extension.
- Main related tests: this file only (no separate unit test targets `DynamicVertexBuffer`/
  `DynamicIndexBuffer` streaming semantics directly under `tests/`).

## Purpose

A 12-frame stress test that cycles `SetDataOptions::{None, Discard, NoOverwrite}` on a
`DynamicVertexBuffer` (6 `VertexPositionColor` verts, manually declared 16-byte stride) and a
`DynamicIndexBuffer` (6 16-bit indices) without ever destroying/recreating either buffer, verifying
after each frame that (a) the rendered center pixel matches the frame's intended solid color and
(b) both buffers' reported capacities stay at 6. Its stated purpose (see header comment, lines
1-13) is to exercise EasyGL's orphan strategy (`Discard`) and `glBufferSubData` path
(`NoOverwrite`) "across many consecutive updates without a recreate," for both the vertex and the
index buffer.

## Executive Verdict

**Needs attention** — the vertex-buffer half of the stress test is genuine and well-designed (a
real pixel-level oracle that would catch a dropped/corrupted per-option update), but the
index-buffer half, despite being named in the file's own header comment as verified "via
pixel readback via `DrawIndexedPrimitives`," never actually issues an indexed draw call anywhere
in the file — its "verification" reduces to a static `getIndexCountProperty() == 6` check that
would pass even if `DynamicIndexBuffer::SetData` were a complete no-op. See Finding F1.

## Checklist Results

### API / XNA / FNA parity
N/A in the strict sense (this is a test executable, not an XNA-namespace implementation file), but
the API it exercises is XNA-compatible: `DynamicVertexBuffer(GraphicsDevice&, const
VertexDeclaration&, int, BufferUsage)`, `DynamicIndexBuffer(GraphicsDevice&, IndexElementSize, int,
BufferUsage)`, `SetData(T*, int, int, SetDataOptions)` all match FNA's `DynamicVertexBuffer`/
`DynamicIndexBuffer` constructor and streaming-`SetData` overload shapes.

### Behavioral correctness
- Vertex path: `dvb_->SetData(verts, 0, 6, opt)` (line 142) is called every frame with a genuinely
  **different** color (`kColors[frameCount_ % 4]`, line 132), then `readCenter()`/`colorClose()`
  (lines 88-102, tol=20) checks the rendered pixel actually reflects that frame's new color. A
  regression that silently dropped a `Discard`- or `NoOverwrite`-tagged update (leaving stale GPU
  data) would show up as a color mismatch and fail the test — this is a real, discriminating
  oracle, confirmed against `EasyGLVertexBufferBackend::uploadWithOptions` (`EasyGLGraphicsBackend.cpp`
  lines 2386-2408), which does implement three genuinely distinct code paths (`Discard` → orphan via
  `set_data(nullptr, …)` + `set_sub_data`; `NoOverwrite` → `set_sub_data` only if `gpu_allocated_`;
  `None`/first call → full `set_data`).
- Index path: `dib_->SetData(idx, 0, 6, opt)` (line 146) is called every frame, but `idx` is the
  **same literal array `{0,1,2,3,4,5}` every single frame** (line 145 is inside `Draw()`, not
  varied by `frameCount_`) — see F1/F2 below for why this, combined with the missing indexed draw,
  makes the DIB portion of the stress test non-discriminating.

### Logic
`optionName()` (lines 62-70) is an exhaustive switch over the 3-value `SetDataOptions` enum with a
`"?"` fallback — correct and complete for the enum's current member set. `colorClose()`'s tolerance
(20 per channel) is reasonable for an 8-bit round-trip through blending/rasterization with
`BlendState::Opaque` and `SetDepthTestEnabled(false)`.

### Memory/resource lifetime
`dvb_`/`dib_` are constructed once in `Initialize()` and never recreated across all 12 frames —
correctly exercises the "no recreate" contract the header comment claims. `dev.SetVertexBuffer(nullptr)`
at the end of each `Draw()` (line 172) correctly unbinds before the next frame; `dib_` is never
bound at all (see F1), so there is nothing to unbind for it.

### C++ correctness
No issues found. `std::uint16_t idx[6]` stack arrays are correctly sized and never escape scope.
`static_cast<std::size_t>(frameCount_) % kOptions.size()` (line 131) avoids a signed/size_t mixed
comparison warning; correct given `frameCount_ >= 0` is invariant here.

### Performance
N/A — a test executable; the per-frame heap allocation implied by `BasicEffect fx(dev)` being
constructed fresh every `Draw()` call (line 150) is a pre-existing pattern common to nearly every
EasyGL example in this shard, not specific to this file.

### Thread safety
N/A — single-threaded `Game` loop.

### Architecture
Correctly uses the public XNA-facing API only (`DynamicVertexBuffer`/`DynamicIndexBuffer`/
`BasicEffect`/`GraphicsDevice`); no backend-internal types leak into the test.

### Maintainability
Header comment (lines 1-13) documents intent clearly, but as shown in F1, the comment's claim
("pixel readback via DrawIndexedPrimitives for one option per cycle") does not match the actual
code — a stale/aspirational comment that was seemingly never updated after the indexed-draw path
was dropped (or never added) from the implementation.

### Portability
N/A.

### Robustness
N/A — test executable.

### Testing
This file *is* the test. See F1/F2 for coverage gaps within it.

### Cross-file consistency
Consistent with `VertexBuffer::SetDataWithOptions(const VertexPositionColor*, …)`
(`VertexBuffer.cpp` lines 392-407), which packs into the same 16-byte `GpuVertex{float x,y,z;
uint8 r,g,b,a;}` layout the test's manually-constructed `VertexDeclaration` (line 110-113: Vector3
at offset 0, Color at offset 12, stride 16) already matches — the test author clearly understood
the internal packed-vertex convention documented in this project's own audit memory (Color's
virtual-base-class size inflation, worked around by packing to a compact `GpuVertex` before
upload). See F3 for a related but out-of-file doc-accuracy issue this test's own behavior
contradicts.

## Detailed Findings

### F1 — DynamicIndexBuffer's claimed pixel-level verification never happens; no indexed draw call exists in the file

- Severity: HIGH
- Confidence: HIGH
- Category: test-coverage / correctness-of-test
- Location/symbol: whole file; specifically `Draw()` (lines 123-179) and the header comment (line
  10: "capacity check (pixel readback via DrawIndexedPrimitives for one option per cycle)")
- Evidence: `grep`-confirmed there is no call to `GraphicsDevice::SetIndexBuffer` or
  `GraphicsDevice::DrawIndexedPrimitives` anywhere in this file — the only draw call is
  `dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2)` (line 156), which uses the vertex buffer
  only. `dib_` is constructed, primed in `Initialize()`, and updated every frame via `SetData`
  (line 146), but is never bound to the device and never participates in any draw. Both APIs exist
  and are named correctly in `GraphicsDevice.hpp` (`SetIndexBuffer` line 341,
  `DrawIndexedPrimitives` line 370), so this isn't a missing-API problem — the test simply never
  calls them.
- Why it matters: the file's own header comment explicitly claims the DIB's per-option streaming
  updates are validated "via pixel readback via DrawIndexedPrimitives," which is the only way an
  index-buffer content bug (wrong offset, wrong byte count, a `Discard`/`NoOverwrite` code path
  that silently no-ops) could ever be observed. Without an indexed draw, a real regression in
  `EasyGLIndexBufferBackend::SetData16WithOptions`'s `Discard`/`NoOverwrite` branches (lines
  2503-2528 of `EasyGLGraphicsBackend.cpp`) — e.g., if `set_sub_data`'s byte offset were wrong, or
  the `Discard` orphan-then-subdata sequence corrupted content — would pass this test unnoticed.
  The only check performed on `dib_` is `dib_->getIndexCountProperty() == 6` (line 170), a static
  capacity check that is invariant under any content bug and would pass even given a `SetData` that
  is a complete no-op.
- FNA/XNA comparison: N/A (test-authoring issue, not an XNA behavior question).
- Related files: `include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp` (the unused APIs),
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (the untested code path).
- Suggested future action (not implemented by this audit): add a `dev.SetIndexBuffer(dib_.get())` +
  `dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 2)` call (as the header comment
  already promises) at least once per `SetDataOptions` cycle, with index content that actually
  distinguishes correct from stale data (see F2).

### F2 — Even if drawn, the index buffer's uploaded content never varies frame-to-frame, so a dropped update would be invisible

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `Draw()` line 145: `std::uint16_t idx[6] = { 0, 1, 2, 3, 4, 5 };`
- Evidence: this literal array is identical on every one of the 12 `Draw()` calls — it is not a
  function of `frameCount_`, `opt`, or anything else that changes across frames. Compare with the
  vertex path, where `col` genuinely changes every frame (line 132), which is exactly what makes
  the vertex-side pixel check discriminating (F1's sibling contrast).
- Why it matters: independent of F1, even a fixed version of this test that *did* add an indexed
  draw would still not meaningfully validate the `Discard`/`NoOverwrite` code paths for the index
  buffer, because the "new" data being uploaded each frame is byte-identical to what is already
  resident on the GPU from the previous frame — a silently-dropped update produces the same
  rendered result as a correctly-applied one.
- FNA/XNA comparison: N/A.
- Related files: same as F1.
- Suggested future action (not implemented by this audit): vary the index pattern per frame (e.g.,
  alternate between `{0,1,2,3,4,5}` and a reversed/rotated ordering) so a dropped/corrupted update
  is visible in the rendered geometry, mirroring how the vertex-color cycling already makes the DVB
  check discriminating.

### F3 — Test's real, differentiated per-option backend behavior contradicts a stale Doxygen claim elsewhere (cross-file, informational)

- Severity: LOW (impact is on a different file, not this one)
- Confidence: HIGH
- Category: documentation / cross-file consistency
- Location/symbol: this test exercises `EasyGLVertexBufferBackend::uploadWithOptions`
  (`EasyGLGraphicsBackend.cpp` lines 2386-2408), which implements genuinely distinct GL call
  sequences per `SetDataOptions` value. `include/Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp`'s
  own Doxygen comments (e.g. above `SetData(const VertexPositionColor*, int, int, SetDataOptions)`)
  state: "The options hint is stored for API conformance but is currently ignored by all CNA
  backends — all writes go to the buffer beginning." This is not accurate for EasyGL (confirmed
  above) nor for at least D3D9/D3D11/D3D12/Headless/SdlGpu/Software/WebGPU, which each implement
  their own `SetDataWithOptions` override with option-dependent behavior (grep-confirmed across
  `src/CNA/Internal/Backends/*/`).
- Why it matters: not a defect in this test file, but worth recording here since this is the file
  that actually demonstrates the doc comment is wrong — `DynamicVertexBuffer.hpp`/
  `DynamicIndexBuffer.hpp` (out of this shard) should have their Doxygen updated when audited.
- Related files: `include/Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp`,
  `include/Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp`.
- Suggested future action: flag for whichever shard audits `DynamicVertexBuffer.hpp`.

## Cross-File Observations

- The `// Task 896 finding` comment (line 154) about needing `RasterizerState::CullNone` because
  the shared full-screen quad's winding is back-facing under CNA's real default rasterizer state
  recurs verbatim (or near-verbatim) across several other files in this batch
  (`easygl_env_map_test.cpp`, `easygl_environmentmapeffect_amount_one_test.cpp`,
  `easygl_environmentmapeffect_amount_zero_test.cpp`,
  `easygl_environmentmapeffect_combined_test.cpp`) — consistent, deliberate convention, not a
  one-off workaround.

## Missing or Weak Tests

- See F1/F2 — the DynamicIndexBuffer half of this stress test needs a real indexed draw with
  content that varies frame-to-frame to actually validate what the header comment claims it
  validates.
- No test in this file (or found elsewhere in a quick search) exercises `SetDataOptions::Discard`/
  `NoOverwrite` on a *32-bit* index buffer's streaming path (`SetData32WithOptions`,
  `EasyGLGraphicsBackend.cpp` lines 2530-2555) — this file only uses
  `IndexElementSize::SixteenBits`.

## Positive Findings

- The vertex-buffer half of this test is a genuine, well-constructed regression oracle: distinct
  color per frame + tight pixel tolerance + capacity assertion, cycling through all three
  `SetDataOptions` values across enough frames (12) to exercise the "no recreate" claim
  meaningfully, verified directly against the differentiated GL-call-sequence implementation in
  `EasyGLGraphicsBackend.cpp`.
- Correct understanding of the project's internal packed-vertex convention (manually-declared
  16-byte `VertexDeclaration` matching the `GpuVertex` layout `VertexBuffer::SetData`/`SetDataWithOptions`
  actually upload).

## Final Assessment

A test whose name and header comment promise dual coverage (vertex *and* index dynamic-buffer
streaming) but only delivers on the vertex half. The vertex-buffer stress testing is genuinely
solid and should be trusted as real regression coverage for EasyGL's `Discard`/`NoOverwrite`/`None`
handling. The index-buffer stress testing, despite updating `dib_` every frame with rotating
`SetDataOptions`, never draws with it and never varies its content — so a real bug in
`EasyGLIndexBufferBackend`'s streaming-update code paths (Discard orphan-then-subdata, NoOverwrite
subdata) would currently go completely undetected by this file, contrary to what its own header
comment claims.
