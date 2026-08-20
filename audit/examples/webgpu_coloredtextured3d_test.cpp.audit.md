# Audit: examples/webgpu_coloredtextured3d_test.cpp

## Metadata

- Source file: `examples/webgpu_coloredtextured3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — stride-24 (`VertexPositionColorTexture`) combined
  vertex-colour + texture-sampling shader test, WebGPU backend (experimental, per `CLAUDE.md`).
- Test executable: `cna_test_webgpu_coloredtextured3d`, CTest target `WebGPU_ColoredTextured3D`
  (`cmake/Tests/WebGpuTests.cmake:57-58`).
- XNA/FNA relevance: `BasicEffect.TextureEnabled`/`VertexColorEnabled`/`DiffuseColor` combined.
- Related production code: `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`CreateTexturedResources()`'s `coloredTexturedShaderSource` WGSL, lines 2631-2670;
  `DrawPrimitivesEx()`'s stride-20/24 `QueueTexturedDraw` branch, lines 6050-6055).

## Purpose

Three checks proving the combined colored+textured shader mixes vertex colour and texture sampling
correctly, and that `VertexColorEnabled` genuinely gates whether the per-vertex colour or
`DiffuseColor` is used: (A) white texture × red vertex colour → red; (B) green texture × white vertex
colour → green (proves texture sampling still runs in the *combined* shader, not just vertex colour);
(C) `VertexColorEnabled=false` with a deliberately-mismatched vertex colour (blue) must use
`DiffuseColor` (red) instead, proving the toggle is genuinely respected.

## Executive Verdict

**Healthy.** Directly read the WGSL shader this file exercises and hand-traced all three checks against
it; all three match, including the `select()`-based `vertexColorEnabled` gate that this file's check C
specifically targets.

## Checklist Results

### API / XNA / FNA parity
`setTextureEnabledProperty(true)`/`setTextureProperty(&tex)`/`fx.VertexColorEnabled = true`/
`setDiffuseColorProperty(Vector3)` (lines 130-132, 146-148, 163-166) are the correct FNA `BasicEffect`
surface for this scenario. Note `VertexColorEnabled` is accessed as a bare public field
(`fx.VertexColorEnabled = true;`, not `fx.setVertexColorEnabledProperty(true)`) — this is not a defect
in this test file (it is simply using the class's actual current public surface as declared), but it is
worth cross-referencing: `AUDIT_CROSS_CUTTING_FINDINGS.md` already documents
`BasicEffect::VertexColorEnabled` as a bare public field with no `getXProperty()`/`setXProperty()`
wrapper, a direct violation of this project's own C# property convention (`CLAUDE.md`), independently
confirmed via two other backends' tests. This file is a third independent instance of the same
underlying `BasicEffect.hpp` API surface being consumed as-is by a test — not a new discovery, but
worth folding into that cross-cutting entry's evidence list since it recurs in a third backend's test
batch.

### Behavioral correctness
Hand-traced the `coloredTexturedShaderSource` WGSL (`WebGPUGraphicsBackend.cpp` lines 2635-2670)
against all three checks:
- `vs_main`: `output.tint = select(u.diffuseColor, input.color * u.diffuseColor,
  vertexColorEnabled > 0.5)` — when enabled, `tint = vertexColor × diffuseColor`; when disabled,
  `tint = diffuseColor` alone (vertex colour ignored entirely).
- `fs_main`: `return sampled * input.tint` where `sampled` is the real texture sample (gated on
  `textureEnabled`, defaulting to `vec4f(1.0)` i.e. white when texturing is off).
- Check A: `sampled=white`, `tint = red × diffuseColor(default white) = red` → `white×red=red`.
  Matches `kExpected=Color::Red`.
- Check B: `sampled=green`, `tint = white × white(default) = white` → `green×white=green`. Matches
  `Color::Lime`.
- Check C: `vertexColorEnabled=false` → `tint = diffuseColor = red` (vertex colour `blue` genuinely
  ignored, not merely overridden by a coincidentally-equal value), `sampled=white(tex)` →
  `white×red=red`. Matches `Color::Red`.
All three derivations are exact (not approximate), and check C in particular is doing real
discriminating work: if `vertexColorEnabled` were wired backwards or ignored, check C would render
blue (the vertex colour) instead of red, which the test would correctly catch.

### Logic
Confirmed the dispatch precedence comment in `DrawPrimitivesEx()` (lines 6012-6018): this file's stride
is 24 with no alpha test/dual texture/env map/skinning flags set, so it correctly falls through to the
`stride==20||24 && texture0!=nullptr` branch (`QueueTexturedDraw`, line 6050-6055), which is the
function whose shader was traced above — confirmed by dispatch-condition reading, not assumed from the
file's own header comment.

### Robustness
Check C's specific technique — setting the "wrong" vertex colour deliberately (blue) rather than simply
omitting `VertexColorEnabled` — is the correct way to prove the toggle actively suppresses vertex
colour rather than merely defaulting to a value that happens to look like `DiffuseColor`.

### Testing
Three focused checks, each isolating a genuinely distinct combination (vertex-colour-dominant,
texture-dominant, vertex-colour-disabled) with no redundancy.

## Detailed Findings

None at HIGH or above.

## Cross-File Observations

- `BasicEffect::VertexColorEnabled`'s bare-public-field API (no `getXProperty`/`setXProperty` wrapper)
  is the same class-level API surface already flagged in
  `AUDIT_CROSS_CUTTING_FINDINGS.md` (via `bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp` and
  `vulkan_basiceffect_vertexcolor_enabled_test.cpp`) — this file is a third, independent confirmation
  of the same underlying `BasicEffect.hpp` design gap, now via a third backend's test, not a
  WebGPU-specific issue.
- No `SkinnedEffect`/fog code paths are exercised — the skinned-normal-transform and fog-formula
  cross-cutting bugs this audit is watching for do not apply to this file. (Consistent with
  `plans/plan_webgpu.md`'s tracked statement that none of the core WebGPU 3D shaders, including this one's
  `colored_textured3d.wgsl`, implement fog — a known, deliberate deferral, not silently missing.)

## Missing or Weak Tests

None significant for this file's own stated scope.

## Positive Findings

- All three checks were independently traced against the actual WGSL shader source (not just the
  test's own comments) and confirmed exact, not approximate.
- Check C's "deliberately wrong vertex colour" technique is good test design, correctly isolating
  "does the toggle suppress vertex colour" from "does the toggle merely happen not to matter here."

## Final Assessment

A correct, precisely-verified test with no defects found in its own logic or the
`colored_textured3d.wgsl` shader / `QueueTexturedDraw` dispatch it exercises. The one item worth
carrying forward is corroborating evidence (not a new finding) for the already-documented
`BasicEffect::VertexColorEnabled` bare-public-field API inconsistency.
