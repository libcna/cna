# Audit: examples/easygl_distorters_displacementmapped_shader_test.cpp

## Metadata

- Source file: `examples/easygl_distorters_displacementmapped_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend shader-port pixel-readback test
- File type: C++ example/integration-test executable (`EasyGLDistortersDisplacementMappedTest : Game`, `main()`)
- Related production code: `ShaderEffect` (`ShaderEffect.cpp`), `GraphicsDevice::DrawIndexedPrimitives`
  (`GraphicsDevice.cpp`), `VertexBuffer`/`IndexBuffer` (`VertexBuffer.cpp`/`IndexBuffer.cpp`),
  `Matrix::ToColumnMajor` (`Matrix.cpp:1234-1249`)
- XNA/FNA relevance: ports `DistortionSample_4_0`'s `Distorters.fx`'s `DisplacementMapped` technique (used by the
  sample's "Window" distorter, `Game.cs:97-103`) — confirmed against the actual sample source on disk.
- FNA reference: N/A directly (sample content), but this is CNA's first genuinely-custom-3D (non-`SpriteBatch`)
  `ShaderEffect` shader in this task's rollout, exercising `VertexPositionTexture` (a real XNA vertex type) and
  `GraphicsDevice.DrawIndexedPrimitives` (real XNA 4.0 API).
- Main related tests: sibling to `easygl_distorters_heathaze_shader_test.cpp` and
  `easygl_distorters_pullin_shader_test.cpp` (both audited in this same batch) — together these three cover all
  three of `Distorters.fx`'s actually-used techniques (`ZeroDisplacement` is unused by the real sample and
  correctly not ported, per this file's own header comment and confirmed against the sample's `Game.cs`, which
  never references `ZeroDisplacement`).
- Registered as `cna_test_easygl_distorters_displacementmapped_shader` /
  `EasyGL_Distorters_DisplacementMapped_Shader` (`EasyGLTests.cmake:371-375`, TIMEOUT 30s).

## Purpose

Proves the GLSL port of `Distorters.fx`'s `DisplacementMapped` technique is correct: a `DisplacementMap` texture's
RG channels pass straight through to the output color, but the blue channel is deliberately dropped (forced to 0)
and the alpha channel passes through — verifying this specific channel-swizzle behavior via a live GPU pixel
readback of a solid-color quad with a genuinely non-zero blue source value.

## Executive Verdict

**Healthy** — the ported vertex/fragment shaders were checked against the actual `Distorters.fx` source on disk and
match exactly (including the file's own stated rationale for combining `World*View*Projection` into a single
pre-uploaded uniform, which was independently re-derived and confirmed mathematically sound), and the single check's
arithmetic was recomputed and matches the file's own comment.

## Checklist Results

### API / XNA / FNA parity
`VertexPositionTexture` (stride 20 — `Vector3 Position` + `Vector2 TexCoord`) is a real XNA vertex type, confirmed
via the test's own vertex array construction matching that layout exactly (`{ Vector3, Vector2 }` pairs, line
141-146). `IndexBuffer`/`VertexBuffer` two-arg constructors (`VertexBuffer(device,4)`, `IndexBuffer(device,6)`,
lines 149-152) are `NOXNA` convenience constructors (confirmed via `VertexBuffer.hpp`'s own Doxygen, "Uses a default
(empty) VertexDeclaration") that work here specifically because `SetData(const VertexPositionTexture*, count)` is
one of the explicitly-supported known vertex-type overloads (confirmed `VertexPositionTexture.hpp` is among
`VertexBuffer.hpp`'s own explicit includes for this purpose). `GraphicsDevice::DrawIndexedPrimitives(PrimitiveType::
TriangleList, 0, 0, 4, 0, 2)` (line 187) is called with the correct XNA argument order and a primitive **count** of
2 (two triangles from 6 indices), not a vertex count — correctly avoiding the vertex-count-vs-primitive-count
mistake this project's own audit memory has flagged as a recurring error class in sibling test files.

### Behavioral correctness
Cross-checked against `Distorters.fx` (lines 21-58 of the actual sample source):
- HLSL `TransformAndTexture_VertexShader`: `output.Position = mul(input.Position, WorldViewProjection);
  output.TexCoord = input.TexCoord;` → ported GLSL (lines 80-90): `gl_Position = WorldViewProjection *
  vec4(aPosition,1.0); TexCoord = aTexCoord;` — correct, and the file's header comment (lines 23-29) correctly
  explains *why* a single pre-combined `WorldViewProjection` uniform (computed as `World*View*Projection` on the
  C++ side and uploaded via `Matrix::ToColumnMajor`, matching every other port in this rollout's established
  row-vector→GLSL-column-vector convention) is mathematically equivalent to HLSL's `mul(input.Position,
  WorldViewProjection)` row-vector convention — independently re-derived and confirmed: `ToColumnMajor` writes the
  XNA matrix's rows sequentially into the output array, which GLSL then reads as columns — i.e. it uploads the
  *transpose* of the XNA matrix, and `M_gl * v = transpose(M_xna) * v = (v * M_xna)^T`, exactly reproducing the
  row-vector HLSL semantics as a column-vector GLSL multiply. Confirmed via direct inspection of
  `Matrix::ToColumnMajor` (`Matrix.cpp:1234-1249`).
- HLSL `Textured_PixelShader`: `float4 color = tex2D(DisplacementMapSampler, texCoord); return float4(color.rg, 0,
  color.a);` → ported GLSL (lines 96-102): `vec4 color = texture(uDisplacementMap, TexCoord); FragColor =
  vec4(color.rg, 0.0, color.a);` — exact structural match, including the blue-channel drop.
- **Check A arithmetic re-verified**: `DisplacementMap` texel = `(153,102,204,255)` → normalized `(0.6, 0.4, 0.8,
  1.0)` → output = `(0.6, 0.4, 0.0, 1.0)` → back to 8-bit ≈ `(153, 102, 0, 255)`. Test's tolerance bands
  (`R∈[147,159]`, `G∈[96,108]`, `B==0` exactly, lines 193-195) correctly allow ±6 rounding slack on R/G (reasonable
  for float↔8-bit round-trip through a render target) while requiring the blue channel be *exactly* 0 — the right
  asymmetry, since exact-zero is the actual claim under test (a shader that "mostly zeroed" blue via some rounding
  quirk rather than a hard `0.0` literal should still fail this).
- World=View=Projection=Identity is a deliberate simplification (stated in the header comment, lines 34-35) that
  correctly isolates the channel-swizzle behavior from any camera/transform correctness question — appropriate
  scope for what this file claims to test.

### Logic
Single `Draw()` pass (guarded by `done_`), one quad, one index buffer, one solid-color 1×1 texture — no loops or
branches beyond the standard `!fx || !fx->IsEffectValid()` guard shared with every sibling shader test in this
batch.

### Memory/resource lifetime
`vb_`/`ib_` are `unique_ptr<VertexBuffer>`/`unique_ptr<IndexBuffer>` member fields constructed once in
`Initialize()` and used once in `Draw()` — no lifetime concerns for a single-shot test.

### C++ correctness
`float wvpCM[16]; Matrix::getIdentityProperty().ToColumnMajor(wvpCM);` (lines 179-180) — correctly sized stack
buffer for a 4×4 matrix, no overflow risk.

### Robustness
Same `!fx || !fx->IsEffectValid()` fail-fast guard pattern as every sibling file in this batch.

### Testing
Covers exactly the one behavior the file claims to test (blue-channel drop) and does so with a texture value chosen
specifically to make blue non-trivial (204, not 0) — a shader that accidentally passed blue through instead of
dropping it would produce `B≈204`, clearly failing the `B==0` check. Does not test the vertex-shader's
`WorldViewProjection` transform under a non-identity matrix (correctly deferred — that's the sibling `PullIn`/
`HeatHaze` tests' concern, which do vary `World`), and does not test the alpha-channel passthrough numerically
(source alpha is 255, a trivially-preserved value that wouldn't discriminate a bug) — a minor, low-priority gap.

## Detailed Findings

No HIGH, CRITICAL, or MEDIUM findings.

### F1 — Alpha-channel passthrough is asserted only implicitly, not numerically checked

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: fragment shader `vec4(color.rg, 0.0, color.a)` (line 100); test check (lines 193-198) only reads
  `c.getRProperty()`/`getGProperty()`/`getBProperty()`, never `getAProperty()`.
- Evidence: the source `DisplacementMap` texel's alpha is `255` (line 138: `{153, 102, 204, 255}`), a value that
  would also result from many *incorrect* implementations (e.g. one that always outputs alpha=255 regardless of
  input) — so even if the alpha channel were checked, this specific source value wouldn't strongly discriminate a
  real bug in the `color.a` passthrough.
- Why it matters: purely a coverage completeness note — the RGB checks already thoroughly prove the file's stated
  thesis (blue-channel drop). A future regression that broke *only* alpha passthrough (unlikely given how simple
  the shader is, but possible in a larger refactor) would not be caught here.
- Suggested future action (not implemented by this audit): if this file is revisited, use a non-255 source alpha
  (e.g. 128) and assert `c.getAProperty()` is close to that value, for a strictly stronger regression net — not
  required given the shader's simplicity.

## Cross-File Observations

- This is the first genuinely-3D (non-`SpriteBatch`) `ShaderEffect` test in the batch audited so far, and correctly
  uses `GraphicsDevice::SetVertexBuffer`/`setIndicesProperty`/`DrawIndexedPrimitives` directly rather than routing
  through `SpriteBatch` — an appropriate API choice given `Distorters.fx`'s techniques operate on real 3D models
  (`Dude`/`Cylinder`/`Window`) in the original sample, not sprites.
- The `DrawIndexedPrimitives(..., 0, 0, 4, 0, 2)` call correctly passes primitive count (2 triangles), consistent
  with this project's own established convention flagged as a common footgun in other test files — good adherence
  here.
- Shares the `WorldViewProjection`-as-single-combined-uniform pattern and its accompanying mathematical
  justification with the sibling `PullIn` test (which additionally needs a `WorldView`-only uniform) — consistent
  cross-file convention, correctly adapted per-shader's actual declared uniform set.

## Missing or Weak Tests

- See F1 (alpha passthrough not numerically distinguished from a constant-255 default).
- No test of the vertex shader's transform correctness under a non-identity `WorldViewProjection` in *this* file —
  reasonable, since that's covered by this task's other tests using the same combined-uniform convention.

## Positive Findings

- The blue-channel-drop check uses a deliberately non-trivial (204) source value specifically to prove the drop is
  real, not incidental — good test design, explicitly called out and verified in the file's own header comment.
- The mathematical justification for the pre-combined `WorldViewProjection` uniform approach (stated in the header
  comment) was independently re-derived during this audit and confirmed correct, not merely trusted.

## Final Assessment

An accurate, correctly-scoped shader-port test whose single behavioral claim (RGB passthrough with hard blue-channel
zeroing) was verified against the real sample source and independently re-derived arithmetic, with only a minor,
low-priority alpha-channel coverage gap.
