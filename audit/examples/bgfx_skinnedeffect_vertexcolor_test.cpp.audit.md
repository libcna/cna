# Audit: examples/bgfx_skinnedeffect_vertexcolor_test.cpp

## Metadata

- Source file: `examples/bgfx_skinnedeffect_vertexcolor_test.cpp` (228 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SkinnedEffect.VertexColorEnabled` (`NOXNA`) pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_skinnedeffect_vertexcolor …)` /
  `cna_register_backend_test(NAME Bgfx_SkinnedEffect_VertexColor …)`, `cmake/Tests/BgfxTests.cmake:904-908`).
- XNA/FNA relevance: indirect — `VertexColorEnabled` on `SkinnedEffect` is explicitly `NOXNA` (real
  XNA's `SkinnedEffect` has no such property; see `SkinnedEffect.hpp` lines 349-356) added for
  glTF-imported skinned meshes with a `COLOR_0` attribute (CNB-66/67). The stride-56 vertex layout
  and shader gating it exercises is CNA-specific, but the underlying lighting/skinning math it also
  incidentally exercises (via both the vertex-lit and pixel-lit shader pairs) is XNA-facing.
- Related production code: `SkinnedEffect.hpp` lines 349-366 (`VertexColorEnabled` field,
  `FillGpuDrawParams` line 325 `p.vertexColorEnabled = VertexColorEnabled`),
  `BgfxGraphicsBackend.cpp`'s `MakeBgfxLayout(stride==56)` (lines 2058-2069),
  `fs_skinned3d.sc`/`fs_skinned3d_vertexlit.sc` (`u_vertexColorEnabled3D` gate, lines 55-65 / 17-25
  respectively).

## Purpose

CNB-67's pixel test proving the stride-56 skinned+Color vertex layout's `a_color0` attribute is
genuinely read and gated by `u_vertexColorEnabled3D` in **both** shader variants this backend can
dispatch to for `SkinnedEffect` — the per-vertex-lit pair (`vs_skinned3d_vertexlit.sc`/
`fs_skinned3d_vertexlit.sc`, real XNA's own `PreferPerPixelLighting=false` default) and the
per-pixel-lit pair (`vs_skinned3d.sc`/`fs_skinned3d.sc`, `PreferPerPixelLighting=true`). Four quads
sharing identical geometry/lighting/texture but differing only in `VertexColorEnabled`/
`PreferPerPixelLighting`: (A) vertex-lit, `VertexColorEnabled=false` → black per-vertex color must
be ignored, red-dominant lit/textured result; (B) vertex-lit, `VertexColorEnabled=true` → black
per-vertex color must zero the result to `(0,0,0)`; (C)/(D) repeat A/B under
`PreferPerPixelLighting=true`, exercising the *other* shader pair's own `a_color0`/
`u_vertexColorEnabled3D` wiring independently.

## Executive Verdict

**Healthy** — the core design insight (a pure-black per-vertex color must zero the output
regardless of what the lit/textured color would otherwise be, since `litRGB*texColor.rgb*vc.rgb`
with `vc.rgb=(0,0,0)` is `(0,0,0)` unconditionally) is a genuinely lighting-independent,
unambiguous check — it does not require reproducing `EnableDefaultLighting()`'s three-light Phong
math by hand to know the expected answer, which is a robust test-design choice this audit confirms
is mathematically sound by inspecting both fragment shaders' actual multiply order.

## Checklist Results

### API / XNA / FNA parity
N/A for `VertexColorEnabled` itself (explicitly `NOXNA`, correctly documented as such in
`SkinnedEffect.hpp`'s Doxygen comment). The surrounding `SetBoneTransforms`/
`setWeightsPerVertexProperty`/`EnableDefaultLighting`/`setPreferPerPixelLightingProperty` calls are
all real XNA API and map correctly.

### Behavioral correctness
Verified the "must zero to black" logic against both actual fragment shaders:
- `fs_skinned3d_vertexlit.sc` (vertex-lit, quads A/B): `gl_FragColor = tex * v_color0 *
  vec4(v_litRGB,1.0); ...; gl_FragColor.rgb *= vc.rgb;` (lines 20-25) — `vc.rgb=(0,0,0)` when
  `VertexColorEnabled=true` and the vertex color is black, applied via a final `*=` **after** the
  specular add (`gl_FragColor.rgb += v_specularRGB * gl_FragColor.a;` at line 22, then the `vc.rgb`
  multiply at line 25) — correctly zeroing the *combined* diffuse+specular output, not just
  diffuse, matching the test's own comment ("black vertex color zeroes the result ... lighting
  independent by construction").
- `fs_skinned3d.sc` (pixel-lit, quads C/D): identical ordering (`gl_FragColor.rgb += specularRGB *
  gl_FragColor.a;` at line 60, then `gl_FragColor.rgb *= vc.rgb;` at line 65) — the same guarantee
  holds for the pixel-lit shader pair independently, which is exactly what quads C/D exist to prove
  (that this gating isn't accidentally only wired in one of the two shader pairs).
- Quad A/C's `VertexColorEnabled=false` check (`R > G && R > 50`) is cross-validated against this
  batch's own `bgfx_skinnedeffect_identity_bones_test.cpp`, which independently established the
  same "identity-bone, red-texture, lit" scenario produces a red-dominant result — this audit
  confirms both files' scenes are close enough in setup (identity bone, red 1×1 texture,
  `EnableDefaultLighting()`) that the cross-reference is legitimate, not coincidental.

### Logic
`renderAndRead()` follows the same safe per-checkpoint full `Clear`+`Apply`+`Draw`+
`GetBackBufferData` pattern (with a 20-iteration blank-frame retry) used by the
identity-bones/translation-bone/twobone-blend siblings in this batch — correctly avoids the
multi-read-per-frame pitfall that this audit found in
`bgfx_skinnedeffect_weightspervertex_test.cpp` (see that file's own report).

### C++ correctness
`SkinnedColorGpuVertex` (56 bytes, `static_assert`-verified) field order — `Position(12) +
Normal(12) + TexCoord0(8) + Weight(16) + Indices(4) + Color0(4)` — matches `MakeBgfxLayout`'s
`stride==56` case exactly (`BgfxGraphicsBackend.cpp` lines 2058-2069), confirmed field-by-field by
this audit, including that `Color0` is appended *after* `Indices` (not interleaved), matching the
comment's own claim ("Color appended after BlendIndices").

### Robustness
Uses a single Identity bone (`SetBoneTransforms(std::vector<Matrix>{
Matrix::getIdentityProperty() })`) with all vertex indices `= 0` — safe under the dormant
`EffectParameter`-truncation behavior documented in this batch's other reports (`p.boneCount=1`,
index `0` is within range).

### Testing
Genuinely tests both shader-dispatch branches this backend has for `SkinnedEffect`
(`vs_skinned3d_vertexlit`/`vs_skinned3d`), not just one — a meaningfully more thorough test than a
single-shader-pair check would be, and correctly exercises the fact that CNB-67's `VertexColorEnabled`
gating was wired independently into both shader pairs (a place a real implementation could easily
have fixed one and forgotten the other).

## Detailed Findings

None at MEDIUM/HIGH/CRITICAL severity.

## Cross-File Observations

- The `matches(..., 10)` tolerance used for the "must be black" checks (B/D) is generous relative
  to the near-zero expected values, which is appropriate here (black should render as very close to
  `(0,0,0)` regardless of GPU rounding, and a `±10` band on an 8-bit channel is tight enough to
  catch a "vertex color not applied at all" regression, which would instead show the full
  red-dominant lit color, wildly outside this tolerance).
- Shares the same dormant `EffectParameter::SetValue(std::vector<Matrix>)`-truncation behavior
  documented in detail in `bgfx_skinnedeffect_translation_bone_test.cpp.audit.md`'s Robustness
  section; confirmed harmless here for the same reason (single bone, index 0 only).
- Startvertex offsets (`0`, `6`, `12`, `18` for quads A/B/C/D respectively, each 2 triangles = 6
  vertices) were independently checked against the `appendQuad()` call order (A, B, C, D pushed in
  that sequence into one shared 24-vertex buffer) and are correct.

## Missing or Weak Tests

None specific to this file — it already covers the property's two boolean states across both
shader-dispatch branches, which is the complete state space for this specific `NOXNA` property.

## Positive Findings

- The "black vertex color must zero the result regardless of lighting" design is a deliberately
  lighting-math-independent check — a genuinely robust test-authoring pattern that avoids needing
  to hand-derive `EnableDefaultLighting()`'s three-light Phong output, and this audit confirmed by
  reading both fragment shaders that the claimed zeroing guarantee actually holds given the actual
  multiply ordering (vertex-color multiply applied last, after the specular add).
- Deliberately tests both of this backend's two `SkinnedEffect` shader-dispatch branches
  independently (quads A/B vs. C/D) rather than assuming shared gating logic — this is exactly the
  kind of test that would have caught a real one-shader-pair-only wiring bug.

## Final Assessment

A thorough, well-designed, mathematically-sound test with no defects found. Exercises both
shader-dispatch paths for a NOXNA extension property with an appropriately lighting-independent
verification strategy.
