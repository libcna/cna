# Audit: examples/easygl_skinnedeffect_vertexcolor_test.cpp

## Metadata

- Source file: `examples/easygl_skinnedeffect_vertexcolor_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `SkinnedEffect.VertexColorEnabled` (NOXNA) golden-image test
- File type: `PixelTestGame`-derived executable (`common/PixelTestGame.hpp`), CTest-registered
  (`cna_easygl_test(cna_test_easygl_skinnedeffect_vertexcolor …)` /
  `cna_register_backend_test(NAME EasyGL_SkinnedEffect_VertexColor …)`,
  `cmake/Tests/EasyGLTests.cmake:134-137`).
- XNA/FNA relevance: **NOXNA extension**. Real XNA's `SkinnedEffect` has no `VertexColorEnabled`
  property (unlike `BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`, which do) — confirmed by
  `SkinnedEffect.hpp`'s own doc comment (lines 350-355) and independently by inspecting
  `SkinnedEffect.cs` in the FNA reference tree, which has no such member. Correctly declared
  `NOXNA bool VertexColorEnabled = false;` (`SkinnedEffect.hpp` line 356) as a public field, not a
  `getX`/`setX` pair — an intentional, documented deviation from this project's usual C# "property"
  convention, matching `BasicEffect::VertexColorEnabled`'s own shape per the header comment.
- Golden fixtures: `examples/golden/easygl_skinnedeffect_vertexcolor_test_a.png` and `_b.png`
  (confirmed present on disk).
- Production code exercised: stride-56 `SkinnedColorGpuVertex` layout vs.
  `EasyGLVertexBufferBackend::ApplyLayout`'s stride==56 case (`EasyGLGraphicsBackend.cpp` lines
  2303-2324), `EnsureSkinnedVertexLitProgram()`'s `uVertexColorEnabled` gate (lines 3539-3548, the
  vertex-lit family, since this test uses `EnableDefaultLighting()` + the real XNA
  `PreferPerPixelLighting=false` default).

## Purpose

CNB-67's golden-image test proving the stride-56 skinned+`Color` vertex layout's `aColor` attribute
(location 5) is actually consumed by the shader and gated by the `uVertexColorEnabled` uniform, rather
than merely existing in the vertex buffer layout unused. Draws two quads with an identical scene setup
(Identity bone, red 1×1 texture, default lighting) but opposite `VertexColorEnabled` values and an
identical pure-black per-vertex color: quad A (`VertexColorEnabled=false`) should render the normal
lit/textured result (black vertex color ignored); quad B (`VertexColorEnabled=true`) should render pure
black (`litRGB*texColor.rgb*vc.rgb` with `vc.rgb=(0,0,0)` zeroes regardless of the lighting math) — a
genuinely lighting-independent way to prove the gate works, avoiding the need to hand-derive an exact
lit color.

## Executive Verdict

**Healthy.** The "use black to make the check lighting-independent" design is a sound, low-maintenance
technique (verified correct against the actual shader multiply order), and the test correctly
cross-references a sibling file's own live-observed value for its "disabled" baseline rather than
re-deriving fresh Phong arithmetic. One MEDIUM finding regarding a load-bearing but under-labeled
cross-file dependency (F1).

## Checklist Results

### API / XNA / FNA parity
`VertexColorEnabled` correctly `NOXNA` (see Metadata) — direct field access (`fx.VertexColorEnabled =
false;`, lines 101/105) matches how the header declares it (a plain public `bool`, not
`getX`/`setXProperty`), and matches `BasicEffect`'s own precedent per the header comment, so this is
internally consistent with the project's established pattern for this one specific NOXNA member, not
an inconsistent one-off.

### Behavioral correctness
`SkinnedColorGpuVertex` (lines 37-46, `static_assert(sizeof==56)`) matches `ApplyLayout`'s stride-56
case exactly: the stride-52 skinning layout (position/normal/uv/weights/indices at offsets 0/12/24/32/48)
with a `ubyte4` normalized color appended at offset 52 (location 5) — verified field-by-field against
`EasyGLGraphicsBackend.cpp` lines 2312-2323, including the comment's own claim that "location 5 (aColor)
is simply left unbound for stride-52 draws" (i.e. this single shader pair correctly serves both the
stride-52 and stride-56 layouts, a design this test's use of stride-56 specifically confirms works).

Verified the shader's actual gating and multiply order (`EnsureSkinnedVertexLitProgram()`'s fragment
stage, lines 3542-3548): `vc = (uVertexColorEnabled>0.5) ? vColor : (1,1,1,1)`;
`FragColor = vec4(vLitRGB*texColor.rgb, uDiffuseColor.a*texColor.a*vc.a)`; `FragColor.rgb +=
vSpecularRGB*FragColor.a`; `FragColor.rgb *= vc.rgb` — i.e. vertex color multiplies the **combined**
diffuse+specular sum, applied *after* the specular add, not just the diffuse term. This directly
confirms the test's own reasoning (header comment lines 9-14): with `vc.rgb=(0,0,0)`, the final
`FragColor.rgb *= vc.rgb` step zeroes the output regardless of what `vLitRGB`/`vSpecularRGB`/`texColor`
evaluated to, making the check genuinely lighting-independent as claimed, not merely "probably always
true because these specific inputs make it black anyway." A shader bug that modulated only the diffuse
term (not specular) by vertex color would still be caught here IF specular were nonzero for this scene
— though this scene's material has no explicit `SpecularColor` set (`EnableDefaultLighting()`'s own
default, not overridden), so this specific test likely does not actually exercise a nonzero specular
term for quad B; the "modulates the whole combined output" claim is verified from the shader source
directly, not solely inferred from this test's own pixel result.

Quad A's expected value, `Color(174, 0, 0, 255)` with `tolerance=40` (line 117), is explicitly
cross-referenced (comment lines 113-115) against "the identical identity-bone/red-texture/lit scenario"
in `easygl_skinnedeffect_golden_test.cpp` (a sibling file **not** in this batch) rather than derived
independently in this file — this audit could not verify that sibling file's own value in this pass
(out of shard scope for this batch); see F1.

### Testing
Combines a fast, tight-tolerance `ExpectPixel` check (`tolerance=10` for quad B's black result, a much
tighter bound than quad A's `40`, appropriate since "exactly zero" is a much easier target to hit exactly
than an arbitrary lit value) with a `CompareGoldenImage` region check for both quads — a reasonable
belt-and-suspenders combination consistent with this project's established `PixelTestGame` pattern.

## Detailed Findings

No CRITICAL/HIGH findings in this file's own logic.

### F1 — Quad A's expected value is inherited from a sibling test file not audited in this pass, without restating the underlying derivation

- Severity: MEDIUM
- Confidence: MEDIUM
- Category: test-coverage / maintainability
- Location/symbol: `Color(174, 0, 0, 255)` (line 117), comment (lines 113-115) citing
  `easygl_skinnedeffect_golden_test.cpp`'s "own live-observed value"
- Evidence: unlike `easygl_skinnedeffect_specular_test.cpp`/`_preferperpixellighting_test.cpp` (this
  same shard), which each restate an intermediate analytical derivation (e.g. "specular(TL)=0.5798...")
  even when citing a sibling file's scene, this file's quote A value is described only as a
  "live-observed value" from another file — i.e. whatever that other test's actual measured pixel
  happened to be, not an independently-checkable formula. `easygl_skinnedeffect_golden_test.cpp` is not
  part of this audit batch, so this specific number's correctness could not be independently verified in
  this pass.
- Why it matters: if the cited sibling file's own expected value were ever wrong (or its scene setup
  quietly drifted — e.g. a future edit to `EnableDefaultLighting()`'s own default light/material values),
  this file would silently inherit the same error with no independent check of its own, and a `tolerance
  ±40` window is wide enough that a real regression could still slip through undetected by either file.
  This is exactly the "chain of trust" pattern flagged as a lower-severity observation in this shard's
  specular test, but elevated to MEDIUM here specifically because, unlike that case, no intermediate
  arithmetic is shown at all — there is nothing in this file (or reasonably nearby) to check the number
  against besides trusting the other file's own comment.
- FNA/XNA comparison: N/A — `VertexColorEnabled` is NOXNA, so there is no FNA reference value to
  cross-check against independently either.
- Suggested future action (not implemented by this audit): when `easygl_skinnedeffect_golden_test.cpp`
  is itself audited (a different shard/batch), verify its own quoted derivation for this exact scene and
  cross-reference the result back into this file's audit; consider tightening this file's own comment to
  restate the underlying diffuse/ambient/texture arithmetic the way the specular tests do, rather than
  only citing "the other file said so."

## Cross-File Observations

- This is the third file in this shard (after `_specular_`/`_preferperpixellighting_`) to rely on a
  sibling file's own previously-observed value as its oracle rather than deriving independently — a
  recurring pattern across this test family worth tracking centrally (e.g. in
  `AUDIT_CROSS_CUTTING_FINDINGS.md`) rather than re-flagging identically in every file, since a single
  wrong root value could silently propagate through several dependent files.
- Same Identity-`World` convention as the rest of this shard (lines 80-82); the missing-world-space-
  normal production finding documented in this shard's `preferperpixellighting`/`specular` reports
  applies to the same `EnsureSkinnedVertexLitProgram()` shader this file dispatches to, but — as with
  the position-only tests — is not meaningfully exercised here since `World` is Identity throughout.
- Correctly uses `CNA::Examples::PixelTestGame`/`RunPixelTest<T>()` rather than hand-rolling a `Game`
  subclass, unlike most of this shard's other files (which predate `PixelTestGame`, per that header's
  own comment about only new files opting in) — no defect, just a note that this shard mixes both
  conventions, consistent with the project's own stated migration policy (`PixelTestGame.hpp` lines
  1-12).

## Missing or Weak Tests

- See F1 (quad A's expected value not independently re-derivable from this file alone).
- Does not test `VertexColorEnabled` combined with a non-white (non-black) per-vertex color that
  neither fully zeroes nor fully passes through the result (e.g. a mid-gray or tinted color) — the
  current design (pure black) only proves the gate exists and multiplies, not that arbitrary color
  values are correctly forwarded from the vertex buffer.
- Does not test the stride-52 (no color attribute) + `VertexColorEnabled=true` combination, which per
  `ApplyLayout`'s own comment "location 5... is simply left unbound for stride-52 draws" — worth
  confirming this doesn't read uninitialized/garbage vertex-attribute memory into `vColor` when the
  buffer genuinely has no color data bound at that location (a plausible GPU-driver-dependent
  undefined-behavior risk the current test cannot detect, since it always uses the stride-56 layout).

## Positive Findings

- The "use pure black to make the check lighting-independent" design is a genuinely good testing
  technique — verified against the actual shader multiply order that it does what it claims, rather
  than merely assuming it.
- Correctly distinguishes tolerance requirements per check (`±40` for an arbitrary lit color vs. `±10`
  for an exact-zero target) rather than using one blanket tolerance for both.

## Final Assessment

A soundly-designed golden test for a correctly-implemented NOXNA extension; its main weakness is an
unusually thin chain of trust for quad A's expected value (F1, MEDIUM) relative to this shard's stronger
precedent of showing intermediate arithmetic.
