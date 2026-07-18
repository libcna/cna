# Audit: examples/webgpu_drawprimitivesex_test.cpp

## Metadata

- Source file: `examples/webgpu_drawprimitivesex_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `DrawPrimitivesEx()`/`DrawIndexedPrimitivesEx()` real
  `GpuDrawParams` forwarding test (stride-16 `VertexPositionColor`), WebGPU backend (experimental,
  per `CLAUDE.md`).
- Test executable: `cna_test_webgpu_drawprimitivesex`, CTest target `WebGPU_DrawPrimitivesEx`
  (`cmake/Tests/WebGpuTests.cmake:47-48`).
- XNA/FNA relevance: `BasicEffect.DiffuseColor`/`VertexColorEnabled` combined with
  `GraphicsDevice.DrawPrimitives`/`DrawIndexedPrimitives` (the `SetVertexBuffer`+bound-effect path,
  distinct from the untyped `DrawUserPrimitives` path).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`DrawPrimitives()` lines 564-589 — calls `currentEffect_->FillGpuDrawParams(p)` then
  `backend_->DrawPrimitivesEx(...)`), `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`DrawPrimitivesEx()` stride-16 branch → `QueueColoredDraw(vb, nullptr, ..., &params)` line 6047,
  `FillExtUniforms()` lines ~391-421).

## Purpose

Three checks specifically proving that `DrawPrimitivesEx()`'s stride-16 dispatch consumes the caller's
*real* `GpuDrawParams` (via `BasicEffect::FillGpuDrawParams()`) rather than a hardcoded
`diffuseColor=white`/`vertexColorEnabled=true` fallback: (A) `VertexColorEnabled=false` +
`DiffuseColor=red` on white-vertex-coloured geometry must render red, not white; (B) the
`DrawIndexedPrimitives` counterpart, `DiffuseColor=blue`; (C) `VertexColorEnabled=true` (the common
case) must still work unregressed.

## Executive Verdict

**Healthy.** Directly confirmed, by tracing `GraphicsDevice::DrawPrimitives()`/`DrawIndexedPrimitives()`
and the backend's `QueueColoredDraw(..., &params)` branch, that this file exercises a genuinely
different code path from `webgpu_colored3d_test.cpp` (which uses the untyped `DrawUserPrimitives` API
and always hits the hardcoded-uniform fallback) — this file's own header/check design is a real,
necessary complement to that other file, not a duplicate.

## Checklist Results

### API / XNA / FNA parity
`fx.VertexColorEnabled = false; fx.setDiffuseColorProperty(Vector3(1,0,0));` (lines 111-112) and the
indexed counterpart (lines 140-141) are the correct FNA `BasicEffect` surface for this scenario.
`PosColorDecl()` (lines 63-69) hand-declares a `VertexDeclaration` matching `VertexPositionColor`'s
real stride-16 layout (position offset 0, color offset 12) rather than using the typed struct's own
static declaration — consistent with this file's purpose of exercising the generic
`VertexDeclaration`-driven path.

### Behavioral correctness
Traced `GraphicsDevice::DrawPrimitives()` (`GraphicsDevice.cpp` lines 564-589): unlike
`DrawUserPrimitives()` (used by `webgpu_colored3d_test.cpp`), this function **does** call
`currentEffect_->FillGpuDrawParams(p)` and forwards the real `p` into
`backend_->DrawPrimitivesEx(...)`. Confirmed `WebGPUGraphicsBackend::DrawPrimitivesEx()`'s stride-16
branch (line 6045-6049) calls `QueueColoredDraw(vb, nullptr, ..., &params)` — the **non-null**
`params` overload — which selects `FillExtUniforms()` (not `FillColoredUniforms()`), and that function
(lines ~413-420) copies `p.diffuseColor`/`p.vertexColorEnabled` from the real, caller-supplied
`GpuDrawParams` rather than hardcoding them. This confirms check A/B's premise is architecturally real:
a hypothetical regression that made `DrawPrimitivesEx()` fall through to the
`DrawColoredPrimitives()`/hardcoded-white fallback instead (e.g. an incorrectly-ordered `if` in the
dispatch chain) would make checks A and B fail (rendering white/white-tinted instead of red/blue),
which is exactly the scenario these checks are designed to catch.

### Logic
`colored3d.wgsl`'s vertex shader (traced earlier via the shared `AlphaTest3D`/`ColoredTextured3D`
family's `select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5)` formula,
same pattern at `WebGPUGraphicsBackend.cpp` line 2415) applies identically here: check A/B
(`vertexColorEnabled=false`) → colour = `diffuseColor` alone (white vertex colour ignored); check C
(`vertexColorEnabled=true`) → colour = `vertexColor × diffuseColor(default white) = vertexColor`
(lime). All three checks' expected values are exact consequences of this formula, not approximations.

### C++ correctness
No `static` leak idiom in this file (unlike `webgpu_colored3d_test.cpp`'s `ApplyBasicEffect()`) —
each check constructs a fresh, stack-local `BasicEffect fx(dev);` (lines 110, 139, 169), which is
destroyed normally at the end of each block's scope.

### Robustness
Check C is the correct "no regression" companion to A/B: without it, a hypothetical fix that made
`DrawPrimitivesEx()` always ignore vertex colour (over-correcting to always use `DiffuseColor`) would
make A/B pass while silently breaking the far more common `VertexColorEnabled=true` case. Its presence
means all three of "ignored", "wired backwards", and "always true" wiring bugs would be caught by at
least one of the three checks.

### Testing
Well-targeted 3-check design that specifically isolates the exact regression class its own header
describes (`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` forwarding real `GpuDrawParams` vs. a hardcoded
fallback) — the header's framing ("the old `DrawColoredPrimitives` fallback's hardcoded diffuseColor=
white/vertexColorEnabled=true") is corroborated by this audit's own reading of `DrawColoredPrimitives`'s
actual `FillColoredUniforms()` call, so this is not an aspirational or exaggerated test-purpose
description.

### Cross-file consistency
Confirmed this file and `webgpu_colored3d_test.cpp` deliberately exercise two different `GraphicsDevice`
entry points for the same stride-16 vertex shape (`DrawUserPrimitives` vs. `SetVertexBuffer`+
`DrawPrimitives`), which converge on the same backend `QueueColoredDraw()` function but with a
materially different `params` argument (`nullptr` vs. real) — a deliberate and correctly-designed split
of test responsibility across the two files, not an accidental duplication.

## Detailed Findings

None at HIGH or above.

## Cross-File Observations

- Directly complements `webgpu_colored3d_test.cpp` (see that file's own audit report) — together they
  cover both of this backend's two distinct stride-16 draw dispatch entry points
  (`DrawColoredPrimitives`/hardcoded-uniforms via `DrawUserPrimitives`, and `DrawPrimitivesEx`/
  real-`GpuDrawParams` via `DrawPrimitives`), with no gap or overlap between the two files' actual
  scope.
- No `SkinnedEffect`/fog/`EnvironmentMapEffect` code paths are exercised — the cross-cutting bugs this
  audit is watching for do not apply to this file.

## Missing or Weak Tests

None significant — the 3-check design fully covers the specific regression class it targets without
redundancy.

## Positive Findings

- Precisely targeted regression test: this audit independently confirmed (by tracing both
  `GraphicsDevice::DrawPrimitives()` and the backend's `QueueColoredDraw`/`FillExtUniforms()`
  functions) that the specific bug class this file's header describes is a real, previously-relevant
  distinction in this codebase's dispatch design, not a hypothetical concern invented for test-writing
  purposes.
- Check C's "no regression on the common case" companion check is good, disciplined test design.

## Final Assessment

A correct, precisely-scoped test with no defects found in either its own logic or the
`DrawPrimitivesEx()`/`FillExtUniforms()` production path it exercises. Its three checks together fully
and non-redundantly cover the specific real/hardcoded-`GpuDrawParams` distinction the file exists to
verify.
