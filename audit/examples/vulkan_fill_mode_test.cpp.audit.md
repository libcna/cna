# Audit: examples/vulkan_fill_mode_test.cpp

## Metadata

- Source file: `examples/vulkan_fill_mode_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `RasterizerState.FillMode` (`WireFrame`) integration test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_fill_mode …)` / `cna_register_backend_test(NAME Vulkan_FillMode_WireFrame …)`,
  `cmake/Tests/VulkanTests.cmake:432-434`).
- XNA/FNA relevance: direct — `Microsoft.Xna.Framework.Graphics.RasterizerState.FillMode`/`FillMode` enum.
- FNA reference: FNA's `RasterizerState.FillMode` maps to `GL.PolygonMode`/D3D `D3DFILLMODE`; this Vulkan
  backend maps it to `VkPolygonMode` (`VK_POLYGON_MODE_FILL`/`VK_POLYGON_MODE_LINE`).
- Related production code: `include/Microsoft/Xna/Framework/Graphics/FillMode.hpp` (2-value enum:
  `Solid`/`WireFrame`), `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`ApplyRasterizerState()` lines 7828-7842, `fillModeWireframe_`/`fillModeNonSolidSupported_` gating).

## Purpose

Three-subtest pixel test on a single hardcoded CW (in final Vulkan NDC) triangle: (1) default `RasterizerState`
(`Solid`) → the triangle's interior must rasterize red; (2) `RasterizerState.FillMode=WireFrame` → the same
interior must now read as background black (edges are 1px-thin lines that a single centre-pixel sample is not
expected to hit); (3) reset back to `Solid` → interior must be red again, proving the state change is not
"sticky"/one-way. The file's own header comment gives an explicit, self-consistent derivation of the triangle's
final NDC winding after the vertex shader's Y-flip, explaining why `CullCounterClockwiseFace` (the default)
leaves this particular triangle visible without needing a `CullNone` override (unlike several sibling tests in
this batch).

## Executive Verdict

**Healthy** — the three-subtest structure (baseline → change → revert) is a genuinely more rigorous check than
a single before/after pair, since it also rules out a "state change breaks all subsequent rendering" failure
mode; the production `ApplyRasterizerState()`/pipeline-creation path was inspected and correctly gates
`VK_POLYGON_MODE_LINE` behind the `VkPhysicalDeviceFeatures.fillModeNonSolid` device feature, matching the
file's own stated hardware assumption.

## Checklist Results

### API / XNA / FNA parity
`RasterizerState rs; rs.setFillModeProperty(FillMode::WireFrame);` (lines 120-121) and the corresponding
`FillMode::Solid` reset (lines 132-133) exercise the exact XNA/FNA `RasterizerState.FillMode` property surface;
`FillMode`'s 2-value enum (`Solid=0`, `WireFrame=1`) matches FNA's own `FillMode` enum values, confirmed by
`FillMode.hpp`.

### Behavioral correctness
- `ApplyRasterizerState()` (`VulkanGraphicsBackend.cpp` line 7828) correctly maps XNA's `FillMode=1` to
  `fillModeWireframe_ = (fillMode == 1) && fillModeNonSolidSupported_` — i.e. it defensively silently falls
  back to solid fill if the device lacks `fillModeNonSolid`, rather than crashing or producing a Vulkan
  validation error. This matches the header comment's own caveat ("Requires `VkPhysicalDeviceFeatures.
  fillModeNonSolid`") and means the test would silently report a false failure (not a crash) on hardware
  lacking that feature — an acceptable, documented limitation rather than a hidden one.
- The comment's own vertex-winding derivation (`top(0,0.8)→clip(0,-0.8)` after the Y-flip, etc., producing CW
  final NDC winding) is self-consistent arithmetic (a Y-flip negates the Y component of each vertex, and
  reversing the sign of one coordinate while keeping the other two the same does invert the signed area's
  orientation), and is consistent with the file's claim that no `CullNone` override is needed here — this is
  the one test in this batch's `EnvironmentMapEffect` group of siblings that constructs its own triangle instead
  of reusing the common quad-with-normal, so its "no CullNone needed" claim rests on a different, independently
  re-derived geometric argument rather than the shared "Task 896 finding" citation used elsewhere in this batch.

### Logic
`drawAndRead()` (lines 71-80) is a clean, minimal per-subtest helper: clear → opaque blend → apply effect → bind
VB → draw → unbind VB → read back center pixel. The explicit `dev.SetVertexBuffer(nullptr)` after each draw
(line 78) is a defensive habit (clearing bound-VB state between subtests) though not strictly required given
each subtest immediately re-binds its own VB before the next draw.

### C++ correctness
`GpuVPC` (`struct { float x,y,z; uint8_t r,g,b,a; }`, `static_assert(sizeof(GpuVPC)==16)`) is a manually packed
POD matching the `VertexDeclaration`'s explicit 16-byte stride (`VertexElement(0,...Position,0)`,
`VertexElement(12,...Color,0)`) — no padding/alignment concerns since all declared fields are consumed and the
struct's natural alignment (4-byte `float` members followed by 4 `uint8_t` members) already produces exactly 16
bytes with no compiler-inserted padding on any common ABI.

### Robustness
The pass/fail thresholds (`getRProperty() >= 200 && getGProperty() <= 30 && getBProperty() <= 30` for "red", and
symmetric `<= 30` on all 3 channels for "black") are appropriately loose for a single anti-aliasing-free solid
triangle fill, without being so loose they would accept a genuinely wrong color.

### Testing
The 3-subtest (baseline/change/revert) structure is a stronger design than most single-transition tests in this
codebase's history: it specifically also validates that `RasterizerState` changes are not sticky/hysteretic
across draws — a state-management class of bug that a simple 2-state test would miss entirely.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — No check verifies wireframe mode actually draws the triangle's *edges* (only that the interior does *not* fill) — a fully-culled/invisible triangle would also pass subtest 2

- Severity: LOW
- Confidence: HIGH (the single centre-pixel sample used cannot distinguish "wireframe edges drawn, interior
  correctly empty" from "triangle not rendered at all due to an unrelated bug")
- Category: test-coverage
- Location/symbol: subtest 2 (lines 118-128), `readCenter()` (lines 62-69, always samples the exact viewport
  centre, i.e. deep in the triangle's interior, never near an edge)
- Evidence: the only pixel ever sampled across all 3 subtests is the exact viewport centre
  (`vp.getWidthProperty()/2, vp.getHeightProperty()/2`), which by the file's own geometric derivation sits well
  inside the triangle's interior, never on or near an edge line. Subtest 2's assertion ("centre is black") would
  pass identically whether `VK_POLYGON_MODE_LINE` genuinely drew thin white/red edge lines around a hollow
  triangle, or whether the triangle failed to rasterize at all (e.g. due to a completely unrelated pipeline
  bug that happened to also produce an all-black frame).
- Why it matters: this weakens the test's actual claim ("`WireFrame` mode causes the Vulkan backend to use
  `VK_POLYGON_MODE_LINE`, so triangle interiors are not rasterized") to something closer to "wireframe mode
  causes the interior not to be solid-filled" — which is necessary but not sufficient evidence that edges are
  actually being drawn as intended by `WireFrame` mode (as opposed to, say, the triangle being culled/discarded
  entirely, which would also leave the interior black).
- FNA/XNA comparison: N/A — this is a test-design completeness question, not an XNA/FNA behavior question
  (FNA's own `FillMode.WireFrame` semantics are unambiguous: draw only the primitive's edges).
- Suggested future action (not implemented by this audit): additionally sample a pixel known to sit exactly on
  one of the triangle's edges (or use the already-established diagonal-edge-row methodology from this batch's
  MSAA test) during subtest 2, expecting a non-black (edge-colored) result there, to positively confirm edges
  are actually rasterized rather than merely confirming the interior is empty.

## Cross-File Observations

- The `fillModeNonSolidSupported_` device-feature gate (`VulkanGraphicsBackend.cpp` line ~1510,
  `fillModeNonSolidSupported_ = true;` set only when the physical device reports the feature, logged at
  device-creation time per line ~1551) is a real, defensive guard against a Vulkan validation error/undefined
  behavior on hardware lacking `fillModeNonSolid` — consistent with the header comment's explicit hardware
  caveat ("AMD Radeon 780M and all modern desktop Vulkan GPUs support this"), and this audit confirmed the
  fallback path (`fillMode==1 && fillModeNonSolidSupported_`) degrades silently to solid rather than crashing.
- Shares the `GpuVPC`/`VertexDeclaration` manual-vertex-layout pattern with `vulkan_instanced_test.cpp` in this
  same batch (both define a `struct GpuVPC { float x,y,z; uint8_t r,g,b,a; }` with an identical
  `static_assert(sizeof==16)`), though each file defines its own independent copy rather than sharing a common
  header — minor duplication, not a defect.

## Missing or Weak Tests

- See F1 — no positive confirmation that wireframe edges are actually rasterized, only that the interior is
  not solid-filled.

## Positive Findings

- The 3-subtest baseline/change/revert structure is a genuinely stronger design than a simple 2-state
  comparison, and this audit confirms it correctly exercises the "state change is not sticky" property.
- The device-feature gating for `VK_POLYGON_MODE_LINE` was independently verified in the production backend to
  be a real, non-crashing guard, matching this file's own stated hardware assumptions honestly rather than
  silently assuming universal support.

## Final Assessment

A solid, well-structured 3-subtest fill-mode test whose only real gap is that it never positively confirms
wireframe edges are drawn (only that the interior stops being solid-filled) — a LOW-severity coverage gap, not
a correctness defect, since both the production polygon-mode mapping and the device-feature fallback were
independently confirmed correct.
