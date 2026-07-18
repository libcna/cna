# Audit: examples/bgfx_basiceffect_texture_enabled_test.cpp

## Metadata

- Source file: `examples/bgfx_basiceffect_texture_enabled_test.cpp`
- Audit status: AUDITED (static; Bgfx is not in the D-P4 opportunistic-build feasibility list for this
  sandbox — no `cmake-build*` directory exists here)
- Subsystem: `examples-tests-bgfx` shard — `BasicEffect` `TextureEnabled=true`, no vertex color, pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_bgfx_test(cna_test_bgfx_basiceffect_texture_enabled …)` / `cna_register_backend_test(NAME Bgfx_BasicEffect_TextureEnabled …)`, `cmake/Tests/BgfxTests.cmake:293-296`)
- XNA/FNA relevance: direct — `BasicEffect.TextureEnabled`, the no-lighting/no-vertex-color textured shader
  variant (`PSBasicTx`)
- FNA reference: `HLSL/BasicEffect.fx` (`PSBasicTx`: `SAMPLE_TEXTURE(Texture, pin.TexCoord) * pin.Diffuse`,
  where `pin.Diffuse` for the no-vertex-color `VSBasicTx` path is just `DiffuseColor` unmultiplied by anything
  else)
- Related production code: `src/CNA/Internal/Backends/Bgfx/shaders/vs_textured3d.sc`, `fs_textured3d.sc`

## Purpose

3-check pixel test proving that with `LightingEnabled=false`, `VertexColorEnabled=false` (both real FNA
defaults) and `TextureEnabled=true` (explicitly set), `BasicEffect`'s shader outputs
`TextureColor*DiffuseColor*Alpha` component-wise, with no vertex-color multiply anywhere in this shader
variant. Per the file's own header, it deliberately reuses the same `(200,100,50)` texture color /
`(0.8,0.4,0.6)` `DiffuseColor` pair as the vertex-color-enabled sibling test (Task 365) so the correct product
`(160,40,30)` is the same numeric target, independently re-derived rather than copy-pasted.

## Executive Verdict

**Healthy** — the expected value and both negative checks were independently re-derived and match exactly;
the production shader path was traced and confirmed to match FNA's `PSBasicTx`/`VSBasicTx` exactly (no
vertex-color term present at all in this shader variant, as required). The file shares the batch's
task-tracking-number documentation issue (F1).

## Checklist Results

### API / XNA / FNA parity
`fx.setTextureEnabledProperty(true)` / `setTextureProperty(&tex)` / `setDiffuseColorProperty(kDiffuse)`
(lines 100-102) map directly to FNA's `BasicEffect.TextureEnabled`/`Texture`/`DiffuseColor`. `LightingEnabled`
and `VertexColorEnabled` are correctly left unset (both default `false` per `BasicEffect.hpp` lines 48, 367),
matching this file's own comment about exercising real FNA defaults rather than an arbitrarily-configured
scene.

### Behavioral correctness
Re-derived: `kTexColor=(200,100,50)`, `kDiffuse=(0.8,0.4,0.6)`.
`R=200*0.8=160`; `G=100*0.4=40`; `B=50*0.6=30` → **(160,40,30)**, exact match to `kExpected`, no rounding
ambiguity (all three products are already integers). The two negative checks —
`kDiffuseOnly(204,102,153)` (`0.8*255=204`, `0.4*255=102`, `0.6*255=153`, i.e. DiffuseColor alone scaled to
byte range) and `kTextureOnly(200,100,50)` (the raw, unmultiplied texture color) — are both independently
correct reference points for "the DiffuseColor multiply didn't happen" and "the texture sample didn't happen,"
respectively.

### Logic
Single, non-parameterized scene (`Draw()` lines 92-136 constructs one `BasicEffect`, one `Texture2D`, one
quad) — simplest file in this batch, matching its narrow, single-hypothesis purpose.

### C++ correctness
No issues found.

### Robustness
The two negative checks (lines 129-132) rule out two specific, plausible wrong implementations (dropping the
texture sample; dropping the DiffuseColor multiply) rather than merely asserting "the pixel isn't zero."

### Testing
3 checks (positive product, texture-not-ignored, diffuse-not-ignored) is an appropriately complete, minimal
set for this narrow no-lighting/no-vertex-color textured feature.

### Cross-file consistency
Traced `vs_textured3d.sc` (lines 12-23: `v_color0 = u_diffuseColor;` unconditionally, no vertex-color
attribute even read by this shader variant's `$input`) and `fs_textured3d.sc` (line 10:
`vec4 color = texture2D(s_texColor, v_texcoord0) * v_color0;`) — confirms this shader variant genuinely has no
code path by which a vertex color attribute could influence the output even if one were present in the vertex
buffer, matching FNA's `VSBasicTx`/`PSBasicTx` (no `vin.Color` reference at all, unlike `VSBasicTxVc`). This is
architecturally the correct, minimal shader for this feature combination — not merely "the vertex-color path
with vertex color forced to white," which would be a functionally-equivalent but less direct implementation.

## Detailed Findings

### F1 — Header comment's cull-state "not fixed there or here" claim cites the wrong task number and is stale (shared with 7 sibling files)

- Severity: MEDIUM
- Confidence: HIGH
- Category: documentation-accuracy / stale-comment
- Location/symbol: header comment lines 10-14 (`"tracked as Task 884, not fixed there or here"`)
- Evidence: `git log --oneline --all | grep "Task 884"` shows the real Task 884 is
  `75aefb7b fix(Task 884): EffectParameterCollection/EffectPassCollection dangling-pointer hazard`, unrelated
  to the cull-state issue this comment describes. The actual fix landed as Task 896
  (`b6a00bc6 fix(Task 896): push GraphicsDevice's real default RasterizerState to all 3 backends`), confirmed
  (`git merge-base --is-ancestor`) as an ancestor of the current `HEAD`, and `GraphicsDevice.cpp` line 207
  confirms it is live (pushing FNA's real `CullCounterClockwiseFace` default to all three backends from the
  constructor). This file's last content change is commit `f8c70725` (Jul 6 17:39), predating `b6a00bc6`
  (Jul 7 19:39).
- Why it matters: same reasoning recorded across this batch — the `RasterizerState::CullNone` workaround
  (line 119) remains correct and necessary, but the wrong task-number reference and the "still unaddressed,
  Bgfx-only" framing are both inaccurate as of the current checkout.
- FNA/XNA comparison: N/A (documentation-accuracy).
- Related files: shares the wrong-task-number variant with `bgfx_basiceffect_one_light_test.cpp`,
  `bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp`, and originally
  `bgfx_basiceffect_vertexcolor_disabled_test.cpp`; the corrected-number variant appears in the other 4 files.
- Suggested future action (not implemented by this audit): correct the task-number reference and note the
  actual closing commit.

## Cross-File Observations

- Deliberately shares its texture-color/`DiffuseColor` numeric pair with
  `bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp` (per its own header comment), and both files'
  expected products were independently re-derived by this audit and found mutually consistent.
- Simplest and shortest file in this batch (154 lines total, one un-parameterized scene) — proportionally
  brief report reflects genuinely lower complexity, not reduced scrutiny.

## Missing or Weak Tests

None found — the 3-check set is complete for this narrow feature combination.

## Positive Findings

- The expected value and both negative-check constants were independently re-derived and match exactly with
  no rounding ambiguity.
- Confirmed via direct shader inspection that this shader variant has no code path through which vertex color
  could leak into the output, rather than merely trusting the test's own assertion that it doesn't.

## Final Assessment

A simple, correct, fully-verified test for `BasicEffect`'s texture-only shader path. Its only issue is the
shared, batch-wide stale/incorrect task-number cull-state documentation (F1), not a functional defect.
