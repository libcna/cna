# Audit: examples/sdlgpu_skinnedeffect_vertexcolor_test.cpp

## Metadata

- Source file: `examples/sdlgpu_skinnedeffect_vertexcolor_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — `SkinnedEffect.VertexColorEnabled` proof for the
  stride-56 skinned+Color vertex layout on the SDL_GPU backend
- File type: standalone `Game`-subclass executable, CTest-registered
  (`SdlGpu_SkinnedEffectVertexColor`, `cmake/Tests/SdlGpuTests.cmake:92-94`, `TIMEOUT 60 LABELS
  "SdlGpu"`)
- XNA/FNA relevance: direct — `SkinnedEffect.VertexColorEnabled` (bare public field, see Cross-File
  Observations), `IEffectLights`/`EnableDefaultLighting()`.
- FNA reference: `Graphics/Effect/SkinnedEffect.cs` (`VertexColorEnabled` property), the stock
  `SkinnedEffect.fx`'s vertex-color gating in its pixel shader.
- Related production code: `src/CNA/Internal/Backends/SdlGpu/shaders/skinned_colored3d.vert.glsl`,
  `skinned_colored3d.frag.glsl`, `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
  (`GetOrCreatePipelineSkinned3D`'s `hasVertexColor` branch, lines 2627-2682).

## Purpose

Two-check pixel test proving the stride-56 skinned+Color vertex layout's per-vertex `Color`
attribute (location 5) is (A) correctly *gated off* by `VertexColorEnabled=false` (the lit/textured
result is unaffected by a black per-vertex color) and (B) correctly *applied* when
`VertexColorEnabled=true`, multiplying a pure-black per-vertex color into the **final** combined
diffuse+specular+texture result (zeroing it exactly), not just the diffuse term. The file's own
header comment explains the choice of pure black specifically so the expected value (`(0,0,0)`) is
lighting-formula-independent — a deliberately robust oracle design, mirroring
`easygl_skinnedeffect_vertexcolor_test.cpp`'s established rationale. Correct placement for a
backend `SkinnedEffect` feature test.

## Executive Verdict

**Healthy** for what this test actually verifies — both checks are independently confirmed correct
against the real `skinned_colored3d.frag.glsl` fragment shader, and the "multiply after specular,
not folded into the diffuse-only tint" ordering this file's header calls out as the specific bug
class it guards against was verified by direct inspection to be correctly implemented. As with
`sdlgpu_skinned_test.cpp`, this file's `World=Identity` scene cannot detect (and does not claim to
detect) the same cross-cutting World-normal-matrix omission already confirmed in this backend's
skinned shaders (see F1, carried forward from that file's audit).

## Checklist Results

### API / XNA / FNA parity
`fx.VertexColorEnabled = false;` / `= true;` (lines 152, 160) — `VertexColorEnabled` is used here as
a bare public field assignment, not a `setVertexColorEnabledProperty()` call. This is **not** a
defect introduced by this test file: `AUDIT_CROSS_CUTTING_FINDINGS.md` already documents
`BasicEffect::VertexColorEnabled` as a bare public field lacking the project's own
`getX/setXProperty()` convention (`CLAUDE.md`'s explicit rule), confirmed via two independent
backend test batches. This audit additionally confirms `SkinnedEffect::VertexColorEnabled` shares
the identical bare-field API shape (used the same way in `sdlgpu_skinned_test.cpp`'s sibling files
in this shard is not applicable there, but the shape is consistent with `BasicEffect`'s own). This
is a genuine, if minor, `CLAUDE.md`-convention violation, but it is **pre-existing on the shared
production `SkinnedEffect` class**, not something this test file could or should work around — a
test correctly uses whatever the real public API surface is.

### Behavioral correctness
- Vertex layout: `SkinnedColorGpuVertex` (lines 53-61, `static_assert(... == 56, ...)`) —
  pos(12)+normal(12)+uv(8)+weights(16)+indices(4)+color(4) = 56 bytes, matches
  `GetOrCreatePipelineSkinned3D`'s `hasVertexColor==true` branch (`vbDesc.pitch = 56`, 6 attributes
  including `attrs[5]` at `UBYTE4_NORM`, offset 52 — confirmed by direct inspection of
  `SdlGpuGraphicsBackend.cpp` lines 2643-2652).
- Quad A (`VertexColorEnabled=false`, lines 148-155): `skinned_colored3d.frag.glsl`'s final line
  `vec4 vertexColor = (pc.vertexColorEnabled > 0.5) ? fragVertexColor : vec4(1.0); color *=
  vertexColor;` (lines 78-79) — with `vertexColorEnabled` false, `vertexColor` is forced to
  `vec4(1.0)` regardless of the per-vertex data, so the pure-black `Color(0,0,0,255)` this test's
  quad geometry carries (`BuildQuad(verts, 0,0,0,255)`, line 125) is correctly ignored, and the
  qualitative "red-dominant, lit" check (`aOk`, line 154) is an appropriate assertion for this
  "control" side of the test (the file's own header explicitly frames it this way, and this audit
  agrees that a byte-exact Blinn-Phong-across-3-lights derivation would be impractical and
  unnecessary here, matching the established EasyGL precedent it cites).
- Quad B (`VertexColorEnabled=true`, lines 157-163): with `vertexColorEnabled` true,
  `vertexColor = fragVertexColor = (0,0,0,1.0)` (normalized from the raw `(0,0,0,255)` bytes via the
  `UBYTE4_NORM` vertex format), and `color *= vertexColor` zeroes `color.rgb` **unconditionally**,
  regardless of what `color.rgb` held before this line — i.e. regardless of the specific
  diffuse+specular+texture+ambient sum computed above it (lines 51-76 of the fragment shader). This
  is exactly the rigorous, lighting-formula-independent proof the header claims: `bOk` (line 162)
  correctly asserts `r==g==b==0` with **exact** equality (not a tolerance-based `Matches()`), which
  is appropriate since `x * 0.0 == 0.0` exactly in IEEE-754 float arithmetic for any finite,
  non-NaN, non-infinite `x` — there is no floating-point-rounding risk in this specific assertion.
- The header's own claim about *why* this shader's multiply-order matters — *"the multiply happens
  AFTER the specular term is added (not folded into fragTint, which only feeds the diffuse term)"*
  — was independently verified: `skinned_colored3d.frag.glsl` line 71 computes `vec3 lit = lightSum
  * fragTint.rgb + emissiveColor;` (diffuse+ambient, from `fragTint`, i.e. `pc.diffuseColor`, a
  *different* value from the per-vertex `fragVertexColor`), line 73 adds `specularRGB` on top
  (`color.rgb += specularRGB * color.a;`), and only *then*, at lines 78-79 (outside and after the
  `if (lightingEnabled)` block entirely), is `fragVertexColor` multiplied in. A hypothetical buggy
  variant that instead multiplied `fragVertexColor` into `fragTint` before the lighting/specular
  computation would leave the specular contribution unscaled by vertex color — this test's
  pure-black-vertex-color design would still catch that specific bug class too (since `color` would
  still be non-zero from the unscaled specular term alone), confirming the test's discriminating
  power extends to the exact regression class its header names.

### Logic
Quad A and Quad B intentionally share the *same* vertex buffer (`vbNoColor_`, built once in
`LoadContent()`, reused for both draws at lines 153/161) — the only variable between the two checks
is `fx.VertexColorEnabled`, which is exactly the property under test; this isolates the property
correctly (a bug that always honors or always ignores `VertexColorEnabled` is guaranteed to fail at
least one of the two checks, per the header's own stated design goal).

### C++ correctness
`RenderAndSampleCenter` (lines 93-109) correctly re-clears the render target and reapplies the
effect (`fx.Apply()`, line 101) between the two draws (called twice, lines 153 and 161, both against
the same `fx` instance with only `VertexColorEnabled` mutated in between) — this is a valid reuse of
one `SkinnedEffect` object across two draws with different property values, correctly re-triggering
`FillGpuDrawParams()`/re-queuing at each `Apply()` call.

### Robustness
No `try`/`catch` around either check (consistent with `sdlgpu_skinned_test.cpp`'s own lack of one,
a shard-wide pattern already noted in that file's report, not new here).

### Testing
2/2 checks are precise, non-redundant, and exercise real differentiated production code (not
metadata/compile-only checks) — a strong, focused test appropriate to its narrow scope.

## Detailed Findings

### F1 — `skinned_colored3d.vert.glsl` shares the same World-normal-matrix omission already confirmed in `skinned3d.vert.glsl` (this file's sibling shader) and in EasyGL/WebGPU/Vulkan — masked identically by this test's `World=Identity` scene

- Severity: MEDIUM (confirmed-present; not observable through this specific test)
- Confidence: HIGH (read the shader source directly)
- Category: correctness / FNA-parity / cross-cutting (carried forward from
  `sdlgpu_skinned_test.cpp.audit.md`'s F1 — see that report for the full cross-backend evidence
  trail)
- Location/symbol: `skinned_colored3d.vert.glsl` line 66: `fragNormal = normalize(mat3(skinMat) *
  inNormal);` — byte-for-byte identical omission pattern to `skinned3d.vert.glsl` line 75, with an
  identical self-disclosing comment (lines 63-65: *"Matches skinned3d.vert.glsl exactly: the normal
  is transformed by the skin matrix alone, with no additional World normal-matrix contribution
  (established SkinnedEffect simplification shared by every backend, unchanged here)."*)
- Evidence: this is the same defect as `sdlgpu_skinned_test.cpp`'s F1, confirmed in a second,
  distinct shader file within the same backend (the stride-56 vertex-color variant, not just the
  stride-52 plain variant) — meaning the omission is consistently applied across *both* of this
  backend's `SkinnedEffect` vertex-shader variants, not an inconsistency between them. This test's
  own scene uses `World=Identity` (line 140), so `WorldInverseTranspose == Identity` and the
  omission is unobservable here, identically to the sibling file.
- Why it matters: identical rationale to `sdlgpu_skinned_test.cpp`'s F1 — any real skinned+
  vertex-colored model with a non-identity `World` (the common case) would render with
  incorrectly-oriented normals/lighting on this backend.
- FNA/XNA comparison: same as `sdlgpu_skinned_test.cpp`'s F1 — a genuine parity gap relative to real
  XNA/FNA's `Skin()`-then-world-space-normal-transform behavior.
- Related files: `skinned3d.vert.glsl` (see `sdlgpu_skinned_test.cpp.audit.md`),
  `pbr_skinned3d.vert.glsl` (a related but distinct variant — see
  `sdlgpu_skinnedpbreffect_test.cpp.audit.md`'s F1, which *does* apply a World normal transform but
  uses the raw World matrix rather than its inverse-transpose).
- Suggested future action (not implemented by this audit): same as the sibling file — a
  non-identity-`World` companion test would convert this from a masked-but-latent defect into an
  observable regression test.

## Cross-File Observations

- This file and `sdlgpu_skinned_test.cpp` together give this backend's `SkinnedEffect` two
  independently-confirmed instances of the same normal-matrix omission (stride-52 and stride-56
  shader variants both share it) — internally *consistent* (no divergence between the two shader
  variants), which at least rules out the variant risk of "the color-carrying shader was patched
  differently from its sibling and now the two disagree."
- The bare-`VertexColorEnabled`-field API-shape observation (Checklist section above) reinforces
  `AUDIT_CROSS_CUTTING_FINDINGS.md`'s existing note that this is a `BasicEffect`-specific lapse
  worth checking against every effect class with a `VertexColorEnabled` property — this audit adds
  `SkinnedEffect` as a second confirmed instance of the same bare-field pattern, worth folding into
  that cross-cutting entry.
- Git history: no dedicated closing commit title matching this specific file was found separately
  from the combined `a72bc60b`/`fa3babc0` ("port PbrEffect/SkinnedPbrEffect BRDF and skinned vertex
  color from EasyGL") commit, which also introduced `sdlgpu_skinnedpbreffect_test.cpp` — consistent
  with the file's own header statement that it "mirrors easygl_skinnedeffect_vertexcolor_test.cpp's
  own established rationale," i.e. this was a deliberate, reasoned port rather than an
  independently-authored test.

## Missing or Weak Tests

- See F1 — no non-identity-`World` variant exists.
- No test in this file exercises `WeightsPerVertex` values other than 1 (only a single identity
  bone is used, line 143) in combination with vertex color — a 2-bone-blend + vertex-color
  interaction is untested (low risk, since the two features are additive/orthogonal in the shader,
  but unverified).

## Positive Findings

- The pure-black-vertex-color oracle design is genuinely rigorous: it produces a lighting-
  formula-independent, exact (not tolerance-based) expected value, and — as independently verified
  above — its discriminating power correctly extends to the *specific* "multiply happens too early,
  before specular" bug class the header names, not just a generic "is VertexColorEnabled respected"
  check.
- Reusing one vertex buffer and one `SkinnedEffect` instance across both checks, varying only the
  property under test, is a clean, minimal-variable test design.
- The shader-level comment discipline (both `skinned3d.vert.glsl` and `skinned_colored3d.vert.glsl`
  explicitly cross-reference each other and disclose the shared simplification) is good, if the
  simplification itself is eventually revisited — better than a silent, undocumented omission.

## Final Assessment

Both checks are precise and independently confirmed correct against the real
`skinned_colored3d.frag.glsl` shader, including the specific "multiply-after-specular" ordering
correctness the header calls out. F1 is not a defect in this test — it is confirmation that this
backend's stride-56 skinned+vertex-color shader shares the identical, already cross-backend-
confirmed World-normal-matrix omission found in its stride-52 sibling, invisible to this file's
`World=Identity` scene by the same construction.
