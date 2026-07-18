# Audit: examples/easygl_alphatest_vertexcolor_diffuse_test.cpp

## Metadata

- Source file: `examples/easygl_alphatest_vertexcolor_diffuse_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `AlphaTestEffect` vertex-color × diffuse-color × EasyGL
  backend pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_alphatest_vertexcolor_diffuse …)` /
  `cna_register_backend_test(NAME EasyGL_AlphaTest_VertexColorDiffuse …)`,
  `cmake/Tests/EasyGLTests.cmake:1126-1128`).
- XNA/FNA relevance: direct — exercises `AlphaTestEffect.VertexColorEnabled` interacting with the
  alpha-test `clip()`.
- FNA reference:
  `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/HLSL/AlphaTestEffect.fx`
  (`VSAlphaTestVcNoFog`'s `vout.Diffuse *= vin.Color`, `PSAlphaTestLtGtNoFog`'s
  `color = SAMPLE_TEXTURE(...) * pin.Diffuse` feeding the `clip()`).
- Related production code: `AlphaTestEffect.cpp::FillGpuDrawParams()` (diffuse-color forwarding,
  lines 324-327), `EasyGLGraphicsBackend.cpp::EnsureColoredTextured3DProgram()` (stride-24 shader,
  lines 2702-2764 — the exact shader this stride-24 `VertexPositionColorTexture` test drives).

## Purpose

Confirms that on EasyGL, `AlphaTestEffect.VertexColorEnabled` doesn't just tint RGB — the alpha
test itself gates on the *combined* `TextureAlpha × VertexAlpha × EffectAlpha`, not material alpha
alone, matching FNA's HLSL where the `clip()` runs against the already-vertex-color-multiplied
`color.a`. Two `CompareFunction::Greater` cases with different `ReferenceAlpha` are chosen
specifically so "combined alpha used" and "diffuse-alpha-alone" hypotheses diverge (header comment
lines 38-45).

## Executive Verdict

**Healthy.** Independently recomputed the exact shader arithmetic from
`EnsureColoredTextured3DProgram()`'s live fragment source (`FragColor=texture(uTexture,vUV)*vc*
uDiffuseColor`, then alpha-test against `FragColor.a`, line 2747-2750) and confirmed both this
test's expected RGB `(96,32,32)` and its two-case discrimination between combined-alpha (`160/255`,
passes `reference=100`, fails `reference=180`) and diffuse-alone alpha (`204/255`, would
incorrectly pass both) are numerically exact — the test is a genuine, non-trivial regression check
for a real formula-ordering detail, not a "doesn't crash" smoke test.

## Checklist Results

### API / XNA / FNA parity
`setVertexColorEnabledProperty(true)` (line 125) matches
`include/.../AlphaTestEffect.hpp:199-206`; combined with `setAlphaFunctionProperty(CompareFunction::
Greater)`/`setReferenceAlphaProperty(...)` (lines 128-129) to construct the two discriminating
cases. `VertexPositionColorTexture` (stride 24) is the correct vertex type to trigger EasyGL's
`EnsureColoredTextured3DProgram()` per-stride dispatch (confirmed at
`EasyGLGraphicsBackend.cpp:3963`, `case 24: EnsureColoredTextured3DProgram(); return
prog_col_textured_;`).

### Behavioral correctness
Full re-derivation, using the live shader (`FragColor=texture(uTexture,vUV)*vc*uDiffuseColor`,
`vc` gated by `uVertexColorEnabled`, line 2747-2748) and
`FillGpuDrawParams()`'s diffuse formula (`diffuseColor_.X * alpha_`, W-component `= alpha_`):
- `TextureColor=white=(1,1,1,1)`.
- `VertexColor=(200,100,50,200)/255=(0.7843,0.3922,0.1961,0.7843)`.
- `DiffuseColor=(0.6,0.4,0.8)×0.8=(0.48,0.32,0.64)`, alpha component `=0.8`.
- `FragColor.rgb = 1 × VertexColor.rgb × Diffuse.rgb`:
  `R=0.7843×0.48=0.3765→96` (`×255=96.0`), `G=0.3922×0.32=0.1255→32`, `B=0.1961×0.64=0.1255→32`.
  Matches `kExpectedRgb=(96,32,32)` (line 78) exactly.
- `FragColor.a = VertexColor.a(0.7843) × Diffuse.a(0.8) = 0.6274 → byte 160` — matches the header
  comment's own derivation ("combined alpha 160/255", line 43-44) exactly.
- Case A, `reference=100`: `alphaTest.X = (100/255+0.5/255)=0.3941`. Shader:
  `(FragColor.a<X)?Z:W` with `Z=-1,W=1` for `Greater` → `0.6274<0.3941` is false → returns `W=1` →
  not discarded → drawn. Matches expected "passes" (line 165-169).
- Case B, `reference=180`: `alphaTest.X=(180/255+0.5/255)=0.7078`. `0.6274<0.7078` is true →
  returns `Z=-1` → discarded. Matches expected "discarded" (line 174-177).
- Cross-check against the *wrong* hypothesis (diffuse-alone alpha `=0.8000→204/255`, i.e. what the
  result would be if the alpha test read `Diffuse.a` alone instead of the combined
  `Vertex.a×Diffuse.a`): under `Greater`'s branch (`(a<x)?z:w`, `z=-1,w=1`, so "drawn" iff `a>=x`),
  at `x=0.7078` (`reference=180`) the diffuse-alone value `0.8000>=0.7078` → would be drawn, while
  the correct combined-alpha value `0.6274<0.7078` → discarded. The two hypotheses genuinely
  disagree at this reference value exactly as the header comment (lines 43-44) claims, confirming
  the test's discriminating design is sound.

### Logic
`RasterizerState::CullNone` per draw (line 139, "Task 896 finding"), consistent with the shard.
20-iteration blank-frame retry loop present (lines 133-145) — consistent with the other
retry-protected siblings.

### C++ correctness
`matches()`/`closeTo()` (lines 103-110) use `±8` tolerance on R/G/B only, appropriately ignoring
alpha (matching this project's established convention per `PixelTestGame::ExpectPixel`'s own
documented R/G/B-only comparison policy) — correct, since the two expected outcomes here
(`(96,32,32)` drawn vs. `(0,0,0)` discarded-to-black-clear) are far enough apart that `±8` cannot
cause a false match.

### Testing
Two cases, each independently meaningful (one proves the RGB formula, the other proves the alpha
test reads combined rather than diffuse-alone alpha) — a compact, well-targeted 2-assertion test
that is not redundant with its siblings (each of the 5 files in this shard isolates a genuinely
different `AlphaTestEffect` behavior).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — the arithmetic and discriminating logic both check out exactly
against the live shader source.

### F1 — Formula and discrimination logic independently re-verified (documentation-only)

- Severity: INFO
- Confidence: HIGH
- Category: correctness (verification note)
- Location/symbol: `kExpectedRgb` (line 78), `EnsureColoredTextured3DProgram()` fragment shader
  (`EasyGLGraphicsBackend.cpp` lines 2746-2751)
- Evidence / why it matters: see Behavioral correctness above — recorded so the (mildly convoluted)
  reference-value discrimination argument doesn't need re-deriving on a future pass.

## Cross-File Observations

- Directly complements `easygl_alphatest_null_texture_test.cpp` (same stride-24 shader family,
  different feature) and `easygl_alphatest_fog_test.cpp` (same effect class, different property) —
  the three together give EasyGL's `AlphaTestEffect` reasonably thorough per-feature pixel coverage,
  each isolating one property rather than testing all of them at once (which would leave formula
  bugs harder to localize).
- The header comment's claim that Vulkan/Bgfx's `AlphaTestEffect` pipeline ignores vertex color
  entirely by default (lines 23-36, "Task 887") is consistent with `docs/alphatesteffect-support.md`
  section 4's own write-up of the same finding — cross-referenced, not contradicted.

## Missing or Weak Tests

None specific to this file.

## Positive Findings

- The choice of `reference=100` vs `reference=180` to straddle the gap between the correct combined
  alpha (`160/255`) and the incorrect diffuse-alone alpha (`204/255`) is precise, deliberate test
  design — verified in this audit to genuinely discriminate the two hypotheses, not just
  coincidentally produce different booleans.
- Uses a plain white texture specifically to isolate the vertex-color × diffuse-color interaction
  from any texture-color contribution (line 38-39) — good experimental-design discipline.

## Final Assessment

A precise, correctly-verified test of a genuinely subtle formula-ordering detail (alpha test reads
post-vertex-color-multiply alpha). All arithmetic independently re-derived from the live shader
source and found exact.
