# Audit: examples/vulkan_instanced_test.cpp

## Metadata

- Source file: `examples/vulkan_instanced_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `GraphicsDevice.DrawInstancedPrimitives` (hardware instancing) test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_instanced …)` / `cna_register_backend_test(NAME Vulkan_DrawInstanced_3Instances
  …)`, `cmake/Tests/VulkanTests.cmake:29-32`).
- XNA/FNA relevance: direct — `GraphicsDevice.DrawInstancedPrimitives`, `VertexBufferBinding` (instance
  frequency).
- FNA reference: `GraphicsDevice.DrawInstancedPrimitives` signature and `VertexBufferBinding`'s
  `InstanceFrequency` semantics.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`DrawInstancedPrimitives()` lines 634-690, `SetVertexBuffers()` lines 1966-1978),
  `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` (`DrawInstancedPrimitivesEx()` lines 7830-7877),
  `src/CNA/Internal/Backends/Vulkan/shaders/instanced3d.vert.glsl`/`.frag.glsl`.

## Purpose

Single-frame test drawing a small red quad 3 times via `DrawInstancedPrimitives` with a 3-entry per-instance
world-matrix buffer (left/centre/right X translations), verifying: (1) all 3 instance positions render red, and
(2) a background pixel between the left instance and the screen edge remains the clear color (green), i.e. the
quad does not accidentally cover the whole screen or fail to instance at all (which would leave that background
pixel red or the instance positions green).

## Executive Verdict

**Healthy** — the production dispatch path (`GraphicsDevice::DrawInstancedPrimitives` → per-instance-VB lookup
via `instanceFrequency>0` → `VulkanGraphicsBackend::DrawInstancedPrimitivesEx`) was traced end-to-end and matches
this test's exact API usage; the instanced vertex shader's transform (`gl_Position = pc.vp * world * vec4(aPos,
1.0)`) was independently confirmed to match the file's own header-comment claim verbatim.

## Checklist Results

### API / XNA / FNA parity
`device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 3)` (lines 128-134) matches FNA's
`DrawInstancedPrimitives(PrimitiveType, int baseVertex, int minVertexIndex, int numVertices, int startIndex, int
primitiveCount, int instanceCount)` signature exactly, including argument order. `VertexBufferBinding(&instVb, 0,
1)` (line 118) correctly uses the 3-argument constructor's `instanceFrequency=1` slot to mark the per-instance
buffer, matching FNA's `VertexBufferBinding(VertexBuffer, int vertexOffset, int instanceFrequency)` overload.

### Behavioral correctness
Traced `GraphicsDevice::DrawInstancedPrimitives()` (lines 634-690): it throws if `currentVertexBuffer_`,
`currentIndexBuffer_`, or `currentEffect_` is null (lines 650-660) — none of which trigger here since the test
sets all three beforehand — then scans `currentVertexBuffers_` for a binding with `getInstanceFrequencyProperty()
> 0` (lines 675-682) to locate `p.instanceVb`. The test's `device.SetVertexBuffers(bindings)` call (line 120)
populates exactly this list with the per-instance binding at index 1, so `p.instanceVb` is correctly discovered.
`VulkanGraphicsBackend::DrawInstancedPrimitivesEx()` (lines 7830-7877) then copies the per-vertex data (all 4
vertices, `pvStride` bytes each), the index buffer (respecting `params.startIndex`), and the per-instance data
(`instCountClamped = max(1, instanceCount)` entries, `instStride` bytes each) into a `Pending3DDraw` for
deferred submission — no off-by-one or stride-mismatch issues found in this copy logic given the test's 16-byte
`GpuVPC` per-vertex stride and 64-byte `InstMat4` per-instance stride, both of which match the buffers' actual
`SetDataRaw(..., sizeof(...))` calls (lines 87, 101).

### Logic
`TranslateMat()` (lines 51-57) builds a column-major identity-plus-translation `mat4` by hand
(`m[0]=m[5]=m[10]=m[15]=1`, translation in `m[12..14]`) — correctly matches column-major layout (translation
occupies the last column, indices 12-14, in a column-major 4×4 array), consistent with the vertex shader's own
`mat4 world = mat4(aInstCol0, aInstCol1, aInstCol2, aInstCol3);` construction from 4 column vectors.

### C++ correctness
`GpuVPC`/`InstMat4` are both `static_assert`-verified PODs (16 and 64 bytes respectively) with no
padding/alignment surprises on standard ABIs. `device.SetVertexBuffer(&perVertVb)` (line 114) followed
immediately by `device.SetVertexBuffers(bindings)` (line 120, whose first entry is the same `perVertVb`) is
redundant but harmless — see F1.

### Robustness
The 4-way pass/fail check (`leftOk`/`centOk`/`rightOk`/`bgOk`, lines 148-151) uses asymmetric thresholds
appropriate to each expected color (`R>=200 && G<=50` for red instances, `G>=200 && R<=50` for the green
background) — tight enough to catch a genuinely wrong color, loose enough for ordinary rasterization/blend
edge noise.

### Testing
The background-pixel check (`bgReg` at `W/12`, deliberately positioned "between left and screen edge") is a
good design choice: it specifically catches a hypothetical bug where instancing silently renders only 1 (very
wide, screen-covering) quad rather than 3 distinct small ones, which the 3 instance-position checks alone would
not catch if such a bug happened to also cover those 3 x-positions.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Redundant `SetVertexBuffer(&perVertVb)` call immediately superseded by `SetVertexBuffers(bindings)`

- Severity: LOW
- Confidence: HIGH (traced both call sites in `GraphicsDevice.cpp`)
- Category: maintainability / dead-code-adjacent
- Location/symbol: lines 114, 120; `GraphicsDevice::SetVertexBuffers()` line 1977
  (`if (!vertexBuffers.empty()) currentVertexBuffer_ = vertexBuffers[0].getVertexBufferProperty();`)
- Evidence: the test's own comment on line 113 states `SetVertexBuffer(&perVertVb)` is "needed by
  DrawInstancedPrimitives", but `SetVertexBuffers()` (called 6 lines later with `bindings[0]` being that same
  `perVertVb`) unconditionally overwrites `currentVertexBuffer_` with `vertexBuffers[0].getVertexBufferProperty()`
  regardless of what the prior `SetVertexBuffer()` call set it to. Since `bindings[0]` is exactly `perVertVb`,
  the net effect is identical either way, making the first call redundant (not incorrect, just superfluous).
- Why it matters: purely stylistic/maintainability — a reader unfamiliar with `SetVertexBuffers()`'s own
  `currentVertexBuffer_`-overwriting side effect (documented nowhere in this test file) might reasonably
  believe the first call is load-bearing, when it is not.
- FNA/XNA comparison: N/A — this is CNA-internal state-management plumbing, not an XNA-facing behavior
  question.
- Suggested future action (not implemented by this audit): remove the redundant `SetVertexBuffer(&perVertVb)`
  call, or replace its comment to note that `SetVertexBuffers()` alone is now sufficient.

## Cross-File Observations

- The instanced vertex shader (`instanced3d.vert.glsl`) was independently confirmed to match the file's own
  header-comment claim exactly: `gl_Position = pc.vp * world * vec4(aPos, 1.0);` with `world` built from 4
  per-instance column vectors, and the fragment shader (`instanced3d.frag.glsl`) confirmed to read only
  `pc.diffuseColor` (a push-constant, set from the effect's flat `DiffuseColor`), never per-vertex color — this
  independently corroborates the test's own comment ("per-vertex colour is not read... Set diffuse to red
  explicitly").
- Shares the `GpuVPC` manual-vertex-layout struct (byte-for-byte identical definition) with
  `vulkan_fill_mode_test.cpp` in this same batch — both independently declare and `static_assert` the same
  16-byte layout rather than sharing a common header; minor duplication, not a defect.
- `RasterizerState::CullNone` (line 125) is applied per the same Task 896 rationale used throughout this test
  family, for this quad's specific winding.

## Missing or Weak Tests

- No check verifies `DrawInstancedPrimitives`'s own input-validation paths (e.g. `instanceCount=0` throwing via
  `ArgumentOutOfRangeException::ThrowIfNegativeOrZero`, confirmed present at `GraphicsDevice.cpp` line 665, or a
  null-vertex/index-buffer throwing per lines 650-660) — this file exercises only the successful-instancing
  happy path, not the documented exception behavior around it. Per `CLAUDE.md`'s testing rules this would
  ideally have separate exception-path coverage, though as an `examples/`-style integration/pixel test (not a
  Google Test unit test under `tests/`) this may be out of this specific file's intended scope.

## Positive Findings

- The dual-purpose background-pixel check is a well-designed discriminator against a "instancing silently
  collapses to one giant quad" failure mode that the 3 instance-position checks alone would miss.
- The production dispatch chain (per-instance-VB discovery via `instanceFrequency>0`, stride handling, index
  buffer offset-by-`startIndex` copy) was traced end-to-end and found to correctly and precisely match this
  test's exact API usage, with no off-by-one or stride mismatches found.

## Final Assessment

A correct, well-designed instancing smoke test whose production code path was independently traced and
confirmed to match its documented behavior exactly; the only issues found are a cosmetic redundant API call
(F1) and an absent (but likely out-of-scope for this file) exception-path check.
