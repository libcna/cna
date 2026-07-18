# Audit: examples/vulkan_skinnedeffect_vertexcolor_test.cpp

## Metadata

- Source file: `examples/vulkan_skinnedeffect_vertexcolor_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — CNB-67, `SkinnedEffect.VertexColorEnabled` pixel test
  (Vulkan-native counterpart; a separately-registered `Vulkan_SkinnedEffect_VertexColor_Reused` test
  target also runs the pre-existing `examples/easygl_skinnedeffect_vertexcolor_test.cpp` file
  unmodified against this backend — see Cross-File Observations).
- File type: hand-rolled `Game`-derived executable (not `PixelTestGame`), CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_skinnedeffect_vertexcolor …)` /
  `cna_register_backend_test(NAME Vulkan_SkinnedEffect_VertexColor …)`,
  `cmake/Tests/VulkanTests.cmake:941-944`).
- XNA/FNA relevance: `VertexColorEnabled` is a real XNA `SkinnedEffect` member (also present on
  `BasicEffect`/`DualTextureEffect`), gating whether the per-vertex `Color` channel multiplies into
  the final lit/textured output.
- Production code exercised: `SkinnedEffect::VertexColorEnabled`/`FillGpuDrawParams()`
  (`p.vertexColorEnabled = VertexColorEnabled;`, `SkinnedEffect.cpp` line 325),
  `shaders/skinned3d_color.frag.glsl` (stride-56 variant, since `VertexColorEnabled` requires the
  `SkinnedColorGpuVertex` layout), `VulkanGraphicsBackend::GetOrCreatePipelineSkinned3D`'s
  stride-56 branch (attribute location 5 = `aColor`, `VulkanGraphicsBackend.cpp` line 5221).

## Purpose

Proves the stride-56 `SkinnedVertex+Color` vertex layout's `aColor` attribute is actually read and
correctly gated by `pc.vertexColorEnabled`, multiplied into the final combined diffuse+specular
output (not diffuse alone) per its own header comment. Uses a straight-on camera
(`eye=(0,0,3)`, quad normal `(0,0,1)`) with `DirectionalLight0.setDirectionProperty(0,0,-1)` so, at
the exact backbuffer centre pixel, `N=L=V=(0,0,1)` and `NdotL0=1` — an analytically exact case. Sets
`SpecularColor=(0,0,0)` and `AmbientLightColor=(0,0,0)` to "zero out every other term, isolating
VertexColorEnabled's own multiply" (its own comment, lines 13-14).

## Executive Verdict

**Needs attention.** The three checks' arithmetic is correct and independently re-verified against
the live shader formula, but this file's own header comment explicitly flags — and then
deliberately routes around, rather than resolves — the exact same Vulkan-specific
ambient/emissive-forwarding gap this audit independently confirmed as a real, active defect while
tracing the shared `SkinnedEffect`/Vulkan skinned-shader code path (see F1). Separately, this file's
own stated goal of proving the multiply happens on the "FINAL combined diffuse+specular output (not
just diffuse alone)" is not actually exercised, since `SpecularColor=(0,0,0)` makes the specular term
always zero (see F2).

## Checklist Results

### API / XNA / FNA parity
`fx.VertexColorEnabled = vertexColorEnabled;` (line 104) — a public field, not a getter/setter pair
(consistent with `SkinnedEffect.hpp`'s own declaration style for this member, matching the sibling
`easygl_skinnedeffect_vertexcolor_test.cpp`'s identical usage).

### Behavioral correctness
Traced `shaders/skinned3d_color.frag.glsl`: `litRGB = (pc.ambientColor + lightSum) *
pc.diffuseColor.rgb`; `vc = (pc.vertexColorEnabled > 0.5) ? vColor : vec4(1,1,1,1)`; `outColor =
vec4(litRGB*tex.rgb, diffuseColor.a*tex.a*vc.a); outColor.rgb += specularRGB*outColor.a;
outColor.rgb *= vc.rgb;`. With `AmbientLightColor=Zero`, `SpecularColor=Zero`,
`DirectionalLight0.Direction=(0,0,-1)`, `DiffuseColor=(0.8,0.6,0.4)`, white 1×1 texture,
`NdotL0=1`, `light0Diffuse=(0.5,0.5,0.5)`:
- `litRGB = (0 + 0.5) * (0.8,0.6,0.4) = (0.4,0.3,0.2)`.
- (a) `VertexColorEnabled=false`: `vc=(1,1,1,1)`, `outColor.rgb = litRGB*tex.rgb = (0.4,0.3,0.2)` →
  `×255=(102,76.5,51)≈(102,77,51)` — matches the file's own asserted `Color(102,77,51,255)` exactly.
- (b) `VertexColorEnabled=true`, `vc=(200,100,50,255)/255`: `outColor.rgb = (0.4,0.3,0.2) *
  (200,100,50)/255 = (0.313725,0.117647,0.039216)` → `×255=(80,30,10)` — matches the file's own
  asserted `Color(80,30,10,255)` exactly.
- (c) `VertexColorEnabled=true`, `vc=(0,0,0,255)`: `outColor.rgb *= (0,0,0)` → `(0,0,0)` regardless
  of `litRGB` — matches `Color(0,0,0,255)` exactly.
All three derivations are exact (not tolerance-dependent), and correctly demonstrate the multiply
happens (gated by `vertexColorEnabled`) — but since `SpecularColor=(0,0,0)` throughout, the
`+= specularRGB*outColor.a` step contributes `0` in every case, so this scene cannot actually
distinguish "vertex color multiplies the final diffuse+specular sum" (what the shader does, and what
the header comment claims is being tested) from "vertex color multiplies diffuse only, with an
unmultiplied specular term added afterward" (a plausible regression shape the shader's own header
comment says it deliberately guards against: "applied after the specular add so
VertexColorEnabled=true with a black vertex color genuinely zeroes the pixel... a specular highlight
added afterward would otherwise leak through unmodulated"). See F2.

### Robustness
Retry-until-nonblack loop (lines 132-143), consistent with this shard's `specular`/
`preferperpixellighting`/`multilight` siblings.

### Testing
Three checks correctly isolate: vertex-color-disabled baseline, vertex-color-enabled multiply with a
distinctive color, and vertex-color-enabled zeroing — a reasonable minimal set for the "is the
multiply gated and applied" question, but not for the "is the multiply applied at the right pipeline
stage relative to specular" question its own production-code comment (and, by extension, this test's
own header comment) frames as the actual point of the check. See F2.

## Detailed Findings

### F1 — This file's own header comment identifies, then deliberately routes around, the exact real ambient-forwarding gap this audit independently confirmed

- Severity: HIGH
- Confidence: HIGH (this file's own comment states the concern; this audit independently traced the
  full code path and confirmed it is a real, currently-active defect, not a hypothetical)
- Category: test-coverage gap on a confirmed-active production defect
- Location/symbol: header comment lines 12-14: *"SpecularColor=(0,0,0) and AmbientLightColor=(0,0,0)
  (the latter sidesteps a separate, pre-existing question of exactly how SkinnedEffect's ambient
  term reaches this backend's skinned3d shaders -- out of this task's scope; only
  DirectionalLight0's own diffuse contribution is exercised here)"*.
- Evidence: this audit independently traced `SkinnedEffect::FillGpuDrawParams()`
  (`SkinnedEffect.cpp` lines 320-404) and confirmed it never populates
  `GpuDrawParams::ambientColor` (only `p.emissiveColor`, folding ambient+emissive+diffuse together
  per its own convention), while `VulkanGraphicsBackend::FillExtPushConst()` forwards
  `p.ambientColor` (never set, defaults to `{0,0,0}`) into the skinned shader's `pc.ambientColor`,
  and the `needsSkinned` UBO-fill block (`VulkanGraphicsBackend.cpp` lines 7466-7494) never forwards
  `params.emissiveColor` at all for the skinned draw path. The full analysis, including the
  cross-backend comparison against EasyGL (which correctly forwards this value through its own
  `uEmissiveColor` uniform), is recorded in
  `vulkan_skinnedeffect_preferperpixellighting_test.cpp.audit.md`'s F1. This file's own author was
  evidently already aware this exact code path was suspect and chose to route around it (setting
  ambient to `0`, where the bug is a no-op) rather than confirm or fix it — this audit's independent
  trace confirms the suspicion was well-founded.
- Why it matters: this leaves the entire shard (all 8 files in this batch) without a single test
  that would catch a regression — or, in this case, confirm the currently-real defect — in
  `SkinnedEffect`'s ambient/emissive forwarding on the Vulkan backend.
- FNA/XNA comparison: N/A directly (this is a test-coverage decision, not an XNA behavior question);
  see the cross-referenced report for the full FNA-parity analysis of the underlying defect.
- Suggested future action (not implemented by this audit): once the underlying defect (cross-
  referenced report's F1) is fixed, extend this file (or add a sibling) with a non-zero,
  exact-value `AmbientLightColor`/`EmissiveColor` case to lock in the fix.

### F2 — The claimed "multiply applies to the final diffuse+specular output, not diffuse alone" is not actually exercised, since `SpecularColor=(0,0,0)` throughout

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-coverage gap
- Location/symbol: header comment lines 6-8 (*"multiplied into the FINAL combined diffuse+specular
  output (not just diffuse alone -- see skinned3d_color.frag.glsl's own header comment for why the
  multiply's position matters)"*) vs. `renderWith()`'s `fx.setSpecularColorProperty(Vector3::Zero)`
  (line 102, held at zero in every one of the three checks).
- Evidence: `shaders/skinned3d_color.frag.glsl`'s own header comment (lines 4-8) explains the
  deliberate ordering (`outColor.rgb += specularRGB*outColor.a;` before `outColor.rgb *= vc.rgb;`)
  exists specifically so a black vertex color also zeroes a would-be specular highlight. With
  `SpecularColor` fixed at `(0,0,0)` for all three checks in this file, `specularRGB` is
  identically zero in every case regardless of where in the pipeline the vertex-color multiply
  happens — a hypothetical regression that swapped the order (multiplying `vc.rgb` into diffuse
  only, then adding an *unmultiplied* specular afterward) would produce byte-identical output to the
  correct implementation for every case this file checks, since there is no specular term to leak.
- Why it matters: the specific ordering bug this shader's own comment says it is guarding against
  (specular leaking through unmodulated by a black vertex color) is exactly the scenario this file
  does not construct — a non-zero `SpecularColor` combined with a black vertex color would be needed
  to actually distinguish the two orderings, and no check in this file does that.
- FNA/XNA comparison: N/A (test-authoring gap).
- Suggested future action (not implemented by this audit): add a fourth check with a non-zero
  `SpecularColor` (and a light/eye/normal configuration producing a non-trivial specular value) and a
  black vertex color, asserting the specular contribution is also zeroed — this is the check that
  would actually validate the ordering claim in the shader's own header comment.

## Cross-File Observations

- `cmake/Tests/VulkanTests.cmake` registers **two** distinct `SkinnedEffect.VertexColorEnabled`
  tests against the Vulkan backend: `Vulkan_SkinnedEffect_VertexColor_Reused` (lines 926-929, running
  the unmodified EasyGL-authored `examples/easygl_skinnedeffect_vertexcolor_test.cpp`, which uses
  `EnableDefaultLighting()` and a golden-image comparison) and `Vulkan_SkinnedEffect_VertexColor`
  (lines 941-944, this file, a Vulkan-native hand-derived-value test). Both genuinely exercise the
  Vulkan `skinned3d_color` shader pipeline, giving this specific property double coverage on this
  backend (one coarse/golden-image, one exact-value/analytical) — a reasonable, not redundant,
  belt-and-suspenders arrangement.
- This is the only file in the batch whose own header comment explicitly names the
  ambient-forwarding question this audit independently confirmed as F1 in the
  `preferperpixellighting` report — a valuable corroborating signal that the defect was a real,
  known-suspect area rather than something only this audit's own code trace surfaced.
- Same Identity-`World` convention as the rest of this shard, so cannot expose the missing
  world-space normal-transform defect (F2 in the `preferperpixellighting` report) either — though
  this file's own `SpecularColor=0`/no-lighting-direction-off-axis design would not exercise that
  defect even with a non-Identity `World`, since it never varies the normal's contribution to a
  directional quantity beyond the trivial `N·L=1` straight-on case.

## Missing or Weak Tests

- See F1 (no exact-value ambient/emissive case anywhere in this shard).
- See F2 (no check isolates the vertex-color-multiply-vs-specular-ordering claim the shader's own
  comment makes).

## Positive Findings

- Three checks' arithmetic is exact (not tolerance-dependent) and independently re-verified against
  the live shader formula — genuinely confirms the core `VertexColorEnabled` gate and multiply.
- Unusually transparent self-documentation: the header comment's explicit acknowledgment of the
  ambient-forwarding "pre-existing question" is exactly the kind of honest test-authoring note this
  audit values, and made F1 straightforward to corroborate rather than requiring independent
  discovery from scratch.
- Correctly reuses the vertex-attribute stride-56 layout convention established elsewhere in this
  codebase (`SkinnedColorGpuVertex`, byte-identical to the EasyGL sibling's own struct).

## Final Assessment

A well-executed, exactly-verified test for `VertexColorEnabled`'s core gate/multiply behavior, whose
own header comment candidly flags a real, still-unresolved Vulkan-specific defect (F1) in the
surrounding `SkinnedEffect` lighting pipeline that this audit independently confirmed, and whose
stated "final combined diffuse+specular" claim is not actually exercised by any of its three checks
(F2).
