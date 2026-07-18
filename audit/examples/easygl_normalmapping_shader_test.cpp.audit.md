# Audit: examples/easygl_normalmapping_shader_test.cpp

## Metadata

- Source file: `examples/easygl_normalmapping_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — custom-vertex-layout `ShaderEffect`/`.cnj` GLSL
  shader-conversion proof (tangent-space normal mapping)
- File type: `Game`-derived executable, CTest-registered as `cna_test_easygl_normalmapping_shader` /
  `EasyGL_NormalMapping_Shader` (`cmake/Tests/EasyGLTests.cmake:446-448`)
- XNA/FNA relevance: direct, via the FNA sample corpus (not FNA runtime source) —
  `NormalMappingSample_4_0/NormalMappingEffect/Content/NormalMapping.fx`; also exercises
  `Microsoft::Xna::Framework::Content::ContentManager`'s `.cnj`-based `Effect` loading path and
  `VertexDeclaration`/`VertexElement` custom-layout support (Task 1080)
- Production sources cross-checked: `src/Microsoft/Xna/Framework/Content/ContentManager.cpp`
  (`.cnj` `"Effect"` type handling, ~lines 707-757), `src/Microsoft/Xna/Framework/Graphics/
  VertexBuffer.cpp` (`SetDataRaw`, lines 380-390)

## Purpose

Proves CNA's HLSL→GLSL hand-conversion of the `NormalMapping.fx` sample effect (tangent-space normal
mapping with Phong specular) renders numerically correct output, using a genuinely custom
56-byte vertex layout (`Position+TexCoord+Normal+Binormal+Tangent`) loaded through the real `.cnj`
content pipeline rather than a built-in vertex format.

## Executive Verdict

**Healthy — and unusually rigorous.** This audit independently re-derived both of the file's two
expected pixel values (Check A and Check B) from first principles using the file's own stated
formula, camera, and light parameters, without reading the file's derivation first, and both
recomputations landed on byte values matching the file's claimed expected output
(`(145,80,48,140)` and `(113,59,32,108)`) to within rounding — the math the test asserts against is
independently confirmed correct, not merely self-consistent.

## Checklist Results

### API / XNA / FNA parity
The HLSL source quoted in the header comment (lines 9-37) matches the well-known
`NormalMappingEffect` sample shader. The GLSL port (`kVertSrc`/`kFragSrc`, lines 169-239) is a
line-by-line translation: `mul(input.position, World)` → `World * vec4(aPosition, 1.0)`;
`mul(-View._m30_m31_m32, transpose(View))` → `transpose(mat3(View)) * (-View[3].xyz)` (the file's own
comment (lines 49-58) derives why this is algebraically equivalent given this project's established
row/column convention, and independently verifies it reproduces `eyePosition=(0,0,3)` for this test's
specific rotation-free camera); `mul(v, tangentToWorld)` (a row-vector times a HLSL `float3x3` built
from three `float3` rows) → the equivalent weighted sum
`x*vTangentWorld + y*vBinormalWorld + z*vNormalWorld` (mathematically identical to the row-vector
product, confirmed by direct substitution below). Custom vertex layout matches `VS_INPUT`'s field
order exactly (`Position, TexCoord, Normal, Binormal, Tangent`, offsets 0/12/20/32/44, stride 56).

### Behavioral correctness
**Independently re-derived Check A** (NormalMap texel `(0,0,255,255)`, World=Identity so
`tangentToWorld` rows are exactly Tangent=(1,0,0)/Binormal=(0,1,0)/Normal=(0,0,1) unchanged):
`normalFromMap = 0·T + 0·B + 1·N = (0,0,1)` (already unit length). `lightDir = normalize((0,0,5)-(0,0,0)) =
(0,0,1)`. `nDotL = 1`. `viewDir`: eye=(0,0,3), quad centre=(0,0,0) → `viewDir=normalize((0,0,0)-(0,0,3))=
(0,0,-1)`. `reflect(lightDir,N) = lightDir - 2·dot(N,lightDir)·N = (0,0,1)-2(0,0,1)=(0,0,-1)`.
`rDotV = dot((0,0,-1),(0,0,-1)) = 1`. `pow(1,1)=1`.
`diffuse = (0.6,0.6,0.6,0.5)·1`; `+ambient(0.05,0.05,0.05,0) = (0.65,0.65,0.65,0.5)`;
`×texture(200,100,50,255)/255=(0.7843,0.3922,0.1961,1) → (0.5098,0.2549,0.1275,0.5)`;
`specular = 0.1·(0.6,0.6,0.6,0.5)·1 = (0.06,0.06,0.06,0.05)`;
**final = (0.5698,0.3149,0.1875,0.55) → byte (145,80,48,140)** — matches the file's stated expected
value exactly.

**Independently re-derived Check B** (NormalMap texel `(128,128,255,255)` → floats
`(0.502,0.502,1.0)`): `normalFromMap` before normalize `= 0.502·(1,0,0)+0.502·(0,1,0)+1·(0,0,1) =
(0.502,0.502,1.0)`, magnitude `√(0.502²+0.502²+1²)=√1.504≈1.2265`, normalized `≈(0.4093,0.4093,0.8154)`.
`lightDir=(0,0,1)` (same as A) so `nDotL=N.z≈0.8154`. `reflect((0,0,1),N)=(0,0,1)-2·0.8154·N≈
(-0.6677,-0.6677,-0.3301)`. `viewDir=(0,0,-1)` so `rDotV = -reflect.z ≈ 0.3301`. `pow(0.3301,1)=0.3301`.
`diffuse=(0.6,0.6,0.6,0.5)·0.8154≈(0.4892,0.4892,0.4892,0.4077)`; `+ambient≈(0.5392,…,0.4077)`;
`×texture(0.7843,0.3922,0.1961,1)≈(0.4230,0.2115,0.1057,0.4077)`;
`specular=0.1·(0.6,0.6,0.6,0.5)·0.3301≈(0.0198,0.0198,0.0198,0.0165)`;
**final≈(0.4428,0.2313,0.1255,0.4242) → byte (113,59,32,108)** — again matches the file's stated
expected value exactly.

Both re-derivations were computed by this audit from the file's own stated formula/camera/light
values, not copied from the file's comment — the match confirms the test's `±6`-byte tolerance
assertions (lines 366-370) are checking genuinely correct expected values, not a plausible-looking
but wrong number that happens to also appear in a stale comment.

### Logic
The file documents (lines 106-112) that it applied **two deliberate mutations** to its own shader
source and reconfirmed both checks fail distinctly: (1) swapping the Binormal/Normal roles in the
GLSL weighted-sum port — both checks fail with different wrong-basis colours, proving the weighted
sum genuinely reads the correct vertex attribute per slot; (2) dropping `AmbientLightColor` from the
sum — both checks' R channel shifts by the exact predicted `0.05 × diffuseTexture.r ≈ 10/255`,
outside the `±6` tolerance. This is a rare, concrete demonstration of discriminating power actually
being exercised and reported, not merely asserted.

### Memory/resource lifetime
Uses a temp-directory-per-`this`-pointer scheme (`root = temp_directory_path() /
("cna_normalmapping_test_" + to_string(uintptr_t(this)))`, lines 260-263) to avoid collisions between
concurrently-running instances of this test binary, then writes `.vert.glsl`/`.frag.glsl`/`.cnj` files
into it and points `ContentManager` at that directory — a reasonable, self-contained pattern (no
dependency on a checked-in fixture asset for this file's specific shader).

### C++ correctness
`NormalMappingVertex` is `#pragma pack(push, 1)`-packed with a `static_assert(sizeof(...) == 56)`
(lines 157-167) — correctly guards against any compiler padding silently breaking the layout that
must match the `VertexDeclaration`'s hand-specified byte offsets.

### Robustness
`Draw()` explicitly checks `fx->IsEffectValid()` before proceeding and fails loudly with a printed
message if the `.cnj` load or GLSL compile failed (lines 355-361) rather than silently rendering
garbage and letting the pixel checks fail with a confusing, disconnected message.

### Testing
Only 2 checks (`Check A`/`Check B`), but each is independently load-bearing (different tangent-space
normal, exercising different code paths of the same formula) and both were shown, via the mutation
testing described above, to actually discriminate a broken TBN transform or a dropped lighting term —
a materially stronger evidentiary basis than most 2-assertion tests in this shard.

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — The `transpose()` term in the eye-position extraction is not independently exercised

- Severity: LOW
- Confidence: HIGH (the file itself discloses this)
- Category: test-coverage
- Location/symbol: `vViewDirection` computation (`kVertSrc`, line 192:
  `transpose(mat3(View)) * (-View[3].xyz)`); file header comment lines 58-63 ("Scope note")
- Evidence: the test's camera (`eye=(0,0,3), target=origin, up=(0,1,0)`) produces a rotation-free
  `View` whose 3×3 part is its own transpose, so `transpose(mat3(View))` is mathematically a no-op for
  this specific test and would not detect a bug that dropped the `transpose()` call entirely (or
  applied it to the wrong operand).
- Why it matters: a regression that broke the transpose specifically (e.g. for a future camera with
  real rotation) would not be caught by this file; the file's own header already discloses this
  limitation rather than silently omitting it, which is the correct way to handle a known coverage
  gap the author chose not to close in this task.
- Suggested future action (not implemented by this audit): a companion check with a rotated camera
  (e.g. `eye` off-axis) would close this gap, but is explicitly out of this file's stated scope.

## Cross-File Observations

- Demonstrates the `.cnj` `"Effect"` content-loading path (`ContentManager.cpp`'s `envelope.type ==
  "Effect"` branch, ~line 757) with a genuinely custom (non-built-in) vertex stride — a useful
  cross-check that `VertexBuffer::SetDataRaw` (`VertexBuffer.cpp:380-390`, Task 1080) correctly
  forwards an arbitrary `VertexDeclaration`'s elements to the backend rather than assuming one of the
  5 built-in vertex formats.

## Missing or Weak Tests

- See F1 — no rotated-camera variant to exercise the `transpose()` term for real, though explicitly
  and honestly disclosed as out of scope by the file itself rather than silently absent.

## Positive Findings

- The file's own header commentary is unusually rigorous: it documents two intentionally-preserved
  "quirky" HLSL behaviors (no `*2-1` normal-map unpacking; the `transpose`-based eye-position
  extraction identity) with an explanation of *why* each is faithful to the original shader rather
  than a bug, and both were independently re-verified by this audit as mathematically accurate.
- Concrete mutation testing (two deliberate, reverted regressions) with reported before/after
  behavior is a strong, verifiable claim of discriminating power — this audit did not need to take
  that claim on faith, since the underlying math was independently re-derived and matches.
- Explicit `IsEffectValid()` guard before drawing, giving an unambiguous failure signal for pipeline
  issues (missing shader compile) distinct from a lighting-math regression.

## Final Assessment

One of the more rigorously self-verified test files in this shard: both of its expected pixel values
were independently re-derived by this audit from the stated formula and match exactly, and its own
disclosed mutation testing demonstrates genuine discriminating power rather than a coincidentally
passing assertion.
