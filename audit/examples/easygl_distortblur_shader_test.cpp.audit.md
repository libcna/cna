# Audit: examples/easygl_distortblur_shader_test.cpp

## Metadata

- Source file: `examples/easygl_distortblur_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend shader-port pixel-readback test
- File type: C++ example/integration-test executable (`EasyGLDistortBlurTest : Game`, `main()`)
- Related production code: `ShaderEffect`/`Effect` (`ShaderEffect.cpp`), `SpriteBatch` (`SpriteBatch.cpp`),
  `RenderTarget2D` (`RenderTarget2D.cpp`), `EasyGLEffectBackend::SetUniformFloatArray`/`SetUniformVec2Array`
  (`EasyGLGraphicsBackend.cpp:333-343`)
- XNA/FNA relevance: ports the `distortionBlur=true` branch of `DistortionSample_4_0`'s `Distort.fx`
  (`Distort_PixelShader`), reusing the 15-tap Gaussian blur math from `BloomSample_4_0`'s `BloomComponent.cs`
  (`ComputeGaussian`/`SetBlurEffectParameters`) — both actual XNA Game Studio sample sources, confirmed present on
  disk.
- FNA reference: N/A directly (sample content), but the effect-parameter shapes (`float2[]`/`float[]` array
  uniforms) are real XNA `EffectParameter.SetValue(Vector2[])`/`SetValue(float[])` surface.
- Main related tests: sibling to `easygl_distort_shader_test.cpp` (same `Distort.fx`, `distortionBlur=false`
  branch, audited separately in this batch) — this file's own header comment correctly states it "combines two
  capabilities this task's own sibling tests already proved independently."
- Registered as `cna_test_easygl_distortblur_shader` / `EasyGL_DistortBlur_Shader` (`EasyGLTests.cmake:390-395`,
  TIMEOUT 30s).

## Purpose

Proves the GLSL port of `Distort.fx`'s `DistortBlur` technique (`distortionBlur=true` branch: sentinel-passthrough
still short-circuits, but the non-sentinel branch runs a 15-tap weighted Gaussian blur sum instead of a single
displaced sample) is correct, and specifically that it composes correctly with the already-separately-proven
sentinel/displacement decode logic and the already-separately-proven Gaussian blur math — not just that each piece
works in isolation.

## Executive Verdict

**Healthy**, with one real but minor documentation-accuracy defect (F1: two cited source line numbers in this
file's own header comment do not point at the claims they're cited for). The ported math (`ComputeGaussian`,
`ComputeBlurParameters`) was independently recomputed against the actual `BloomComponent.cs` source and matches
exactly; the GLSL fragment shader is a faithful port of the actual `Distort_PixelShader`'s `distortionBlur=true`
branch.

## Checklist Results

### API / XNA / FNA parity
N/A directly (`ShaderEffect`/`SetUniformFloatArray`/`SetUniformVec2Array` are `NOXNA` extensions). `RenderTarget2D`
(default `Texture2D(dev, kSize, kSize)` two-arg constructor at line 210, and the two-arg `RenderTarget2D(device,
kSize, kSize)` constructor at line 222) are real XNA-shaped constructors used correctly for an intermediate render
target that is bound, drawn into, read back, then unbound (`device.SetRenderTarget(nullptr)`, line 258) each call to
`DrawOnce()` — correct cleanup between the two draws in this file (sentinel map, then blur map).

### Behavioral correctness
Cross-checked against the real sample sources on disk:
- **`Distort_PixelShader`'s `distortionBlur=true` branch** (`Distort.fx` lines 40-48): the ported GLSL `for (int i=0;
  i<15;i++) finalColor += texture(uSceneTexture, TexCoord.xy+displacement+uSampleOffsets[i]) *
  uSampleWeights[i];` (lines 112-115) matches the original's loop body exactly, including reusing the
  already-decoded `displacement` (post `-= .5+ZeroOffset`) as a shared base offset added to each of the 15 taps —
  not recomputed or dropped per-tap.
- **`ComputeGaussian`** (test lines 123-127) reproduces `BloomComponent.cs:306-312`'s
  `(1/sqrt(2*pi*theta))*exp(-(n*n)/(2*theta*theta))` exactly, parameterized as `(n, theta)` instead of capturing
  `theta` from a `Settings` singleton (the correct, necessary adaptation for a standalone C++ test with no
  settings object).
- **`ComputeBlurParameters`** (test lines 130-157) reproduces `BloomComponent.cs`'s `SetBlurEffectParameters`
  (lines 239-297) statement-for-statement: `weights[0]=ComputeGaussian(0,theta)`, the `sampleCount/2` loop building
  symmetric weight/offset pairs with the `i*2+1.5` bilinear-tap-doubling trick, and the final normalize-by-
  `totalWeights` pass — all present and in the same order.
- **Draw A (sentinel map) exact-equality checks**: `a.onLine.getRProperty()==255` / `a.adjacent.getRProperty()==0`
  (lines 278-279) — correctly strict (not tolerance-banded) because the sentinel branch performs zero blending,
  passing the pre-blur source through pixel-for-pixel; any blur leakage into the sentinel path would flip these
  from exact values.
- **Draw B (real, zero-net-displacement blur map) tolerance checks**: `b.onLine` in `(20,250)` (partially blurred,
  not fully washed out, line 280), `b.adjacent > 0` (energy spread into a previously-pure-black neighbor, line 281),
  `b.far == (0,0,0)` at 24 texels away, correctly beyond the 15-tap filter's `13.5`-texel max reach
  (`(15/2-1)*2+1.5 = 13.5`, independently recomputed and matching the file's own comment) — this specific check
  (far pixel *must* stay exactly black) is the strongest discriminator in the file: a blur-radius or tap-count bug
  that over-reached would flip this one from `==0` to nonzero, while a blur that didn't run at all would instead
  fail the `bOnLineOk`/`bAdjacentOk` checks. The two failure modes are cleanly separated across three checks.
- **128/255 decodes to exactly zero displacement**: `128/255 - 0.5 - 0.5/255 = 0.501960... - 0.5 - 0.001960... =
  0.0` exactly (re-verified by hand) — a deliberately chosen value that is simultaneously non-zero (so the sentinel
  branch is *not* taken, proving real branch selection rather than a hardcoded shortcut) and decodes to zero net
  offset (isolating the blur-tap math from any position-shift complexity). A well-constructed test value.

### Logic
`DrawOnce(Texture2D&)` (lines 206-260) is a clean, reusable helper: builds a fresh 64×64 source texture with a
single bright vertical line, computes blur parameters once (identical for both calls — only the `distortionMap`
argument differs between Draw A and Draw B), renders into an intermediate `RenderTarget2D`, reads back three probe
pixels, unbinds the render target, and returns a `Sample` struct. `Draw()` calls this twice and compares five
derived booleans — correct, no duplicated/diverging logic between the two draws that could hide a bug in one path.

### Memory/resource lifetime
`sourceTex`/`destRt` are local to `DrawOnce()` and rebuilt fresh on each of the two calls — no state leakage between
Draw A and Draw B (each gets an independently-constructed 255-on-black source line). `device.SetRenderTarget
(nullptr)` at the end of `DrawOnce()` (line 258) correctly restores the backbuffer as the active target before
`GetBackBufferData` would be meaningful for any caller after this returns (though this file's own `GetBackBufferData`
calls happen *before* the unbind, at lines 254-256, reading from the render target while it's still bound — correct
per `GetBackBufferData`'s general contract of reading whatever the *currently bound* target is).

### C++ correctness
`float flatOffsets[kSampleCount*2]` (line 227) flattening `offsets[kSampleCount][2]` into a flat array before
calling `SetUniformVec2Array` matches `EasyGLEffectBackend::SetUniformVec2Array`'s own expected flat-`float*`-with-
implicit-stride-2 contract (confirmed at `EasyGLGraphicsBackend.cpp:339-343`: `std::span<const float>(values,
count*2)`, `2` = components-per-element) — correct, not an off-by-one/stride mismatch.

### Performance
N/A for a single-shot correctness test — two draws of a 64×64 target with a 15-tap shader is negligible.

### Robustness
Same `!fx || !fx->IsEffectValid()` guard pattern as the sibling `Distort` test (line 268), correctly checked once
before either `DrawOnce()` call rather than duplicated per-draw.

### Testing
Thorough for what it targets: sentinel-passthrough-with-blur-context (a case the plain `Distort` test's sibling
sentinel check doesn't cover, since that file has no blur loop at all) and a genuine blur-applied case with three
spatially distinct probe points chosen specifically to bound the filter's reach. Does not test an *asymmetric*
`dx,dy` blur direction (only horizontal, `dx=1/kSize, dy=0`) or a `distortionBlur=true` case where the displacement
is simultaneously non-zero in *position* (only the zero-net-displacement `128/255` value is used) — reasonable scope
limitation given the position-shift behavior is already proven by the sibling `Distort` test and the blur math by
the (referenced but not re-audited in this batch) `GaussianBlur.fx` test.

## Detailed Findings

No HIGH, CRITICAL, or MEDIUM findings.

### F1 — Header comment cites the wrong `Game.cs` line numbers for the "3 distorters, 2 set DistortionBlur=true" claim

- Severity: LOW
- Confidence: HIGH
- Category: documentation-accuracy
- Location/symbol: file header comment, line 6: `"both of the sample's 3 distorters that set
  DistortionBlur=true (Game.cs:85,101)"`.
- Evidence: cross-checked against the actual `DistortionSample_4_0/Distortion/Game.cs` on disk. The claim itself is
  correct (`distorters[0].DistortionBlur = true;` and `distorters[1].DistortionBlur = true;` do exist, with
  `distorters[2].DistortionBlur = false;`), but the cited line numbers are wrong: the `DistortionBlur = true;`
  assignments are actually at **lines 86 and 95**, not 85 and 101. Line 85 is `distorters[0].DistortionScale =
  0.0003f;` (the line immediately *before* the cited `DistortionBlur` assignment), and line 101 is
  `distorters[2].Technique =` (continuing onto line 102 as `DisplacementMapped`) — i.e. line 101 actually belongs to
  the *third* distorter, the one whose `DistortionBlur` is `false`, not one of the two `true` cases the comment is
  citing evidence for.
- Why it matters: purely a citation-accuracy issue in a code comment, not a functional defect — the underlying
  factual claim (2 of 3 distorters use `DistortionBlur=true`) is itself correct, just imprecisely sourced. A future
  reader following the citation to double-check the claim would land on the wrong lines.
- FNA/XNA comparison: N/A (citation of external sample source, not an FNA/XNA API question).
- Related files: `DistortionSample_4_0/Distortion/Game.cs` (external sample, not part of this repository).
- Suggested future action (not implemented by this audit): correct the citation to `Game.cs:86,95` if this file's
  header comment is ever revised for other reasons.

## Cross-File Observations

- This file explicitly reuses (per its own header comment) math already independently proven by a separate,
  earlier task's `GaussianBlur.fx` test rather than re-deriving/re-verifying it from scratch — a good instance of
  the audit's own cross-file-consistency concern being handled well by the code's authors: composing two
  independently-tested pieces and testing only the *composition*, not re-testing each piece.
- Shares the exact `dynamic_cast<ShaderEffect*>`/`.cnj`-content-loading/`IsEffectValid()` idiom with every other
  `easygl_*_shader_test.cpp` file in this batch.

## Missing or Weak Tests

- No test of a `dx,dy` diagonal or vertical blur direction (only horizontal) — low priority since the blur-direction
  math itself is the sibling `GaussianBlur.fx` test's responsibility, not this file's.
- No test of the sentinel branch when there *is* real off-line content nearby to blur (Draw A's source texture is
  identical to Draw B's, so the "no blur ran" claim rests entirely on the pre/post pixel values being bit-identical,
  which is a valid and sufficient proof, just worth noting there's no *additional* scenario varying the source
  image itself).

## Positive Findings

- The `128/255` displacement-map test value is a well-chosen discriminator: simultaneously proves branch selection
  (non-zero raw texel avoids the sentinel path) and isolates the blur math from position-shift complexity (decodes
  to exactly zero net offset) — a sign of genuine engineering care in test design, not a boilerplate copy-paste.
- The three-probe-point strategy (on-line / adjacent / far) cleanly separates "blur didn't run" from "blur ran but
  over-reached" into independently-checkable, independently-diagnosable booleans.

## Final Assessment

A carefully constructed composition test whose core math (Gaussian weight/offset computation) was independently
re-derived and confirmed to exactly match the real `BloomComponent.cs` source, and whose GLSL port of the
`distortionBlur=true` branch matches the real `Distort.fx` source line-for-line. The one real defect found (F1) is a
citation-accuracy slip in a comment, not a functional or test-correctness issue.
