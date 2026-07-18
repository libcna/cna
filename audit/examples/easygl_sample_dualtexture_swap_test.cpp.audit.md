# Audit: examples/easygl_sample_dualtexture_swap_test.cpp

## Metadata

- Source file: `examples/easygl_sample_dualtexture_swap_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `DualTextureEffect` reactive-material sample/test
- File type: C++ example/integration-test executable (`DualTextureSwapSample : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::DualTextureEffect::FillGpuDrawParams`
  (`DualTextureEffect.cpp:248-275`), `CNA::Internal::Backends::EasyGL::EasyGLGraphicsBackend::
  EnsureDualTextured3DProgram` (`EasyGLGraphicsBackend.cpp:3009-3070`, the actual bound GLSL)
- XNA/FNA relevance: `DualTextureEffect.Texture`/`.Texture2`, judged against
  `FNA/src/Graphics/Effect/StockEffects/HLSL/DualTextureEffect.fx::PSDualTexture()`.
- Main related tests: this file (Task 498, sample 2/4); Task 191 (referenced in the file's own header) covers the
  multiply-blend math itself elsewhere — this file deliberately isolates the "did the swap reach the shader"
  question instead.

## Purpose

A "reactive material" demo: a static full-screen `DualTextureEffect` quad whose `Texture2` reference is swapped
between a 1x1 Red and a 1x1 Green `Texture2D` based on `Update()`-driven frame counter state (frame 1 = Red phase,
frame 4 = Green phase after swapping at `kSwapAtFrame=2`), proving genuine per-frame `Update()` state reaches the
render rather than being baked in once at `Initialize()`. Placement matches `examples-tests-easygl`.

## Executive Verdict

**Healthy** — the swap mechanism and its three assertions are correctly targeted and pass a real
`Update()`-driven-state-versus-static-scene distinction; one documentation inaccuracy (F1) was found in the file's
own header, though it does not affect the test's actual correctness given the fully-saturated colors used.

## Checklist Results

### API / XNA / FNA parity
`setTextureProperty`/`setTexture2Property` (lines 96-97, 105) are real `DualTextureEffect` members, confirmed
against `DualTextureEffect.hpp`/`.cpp` (`getTextureProperty`/`getTexture2Property` pairs at lines 154-168 of the
`.cpp`) — correct usage, no CNA-only extension misused here.

### Behavioral correctness
Traced the actual bound GLSL fragment shader (`EnsureDualTextured3DProgram`, `EasyGLGraphicsBackend.cpp:3038-3056`):
```
vec4 base=texture(uTexture,vUV);
base.rgb*=2.0;
FragColor=base*texture(uTexture2,vUV)*uDiffuseColor;
```
This `base.rgb *= 2.0` doubling is confirmed to exactly match FNA's own `PSDualTexture` HLSL
(`DualTextureEffect.fx:100`: `color.rgb *= 2;`) — a faithful port of XNA's classic dual-texture "lightmap" doubling
technique, not a CNA-introduced deviation. See F1 for the one consequence of this that the test file's own header
comment gets subtly wrong (harmlessly, given the actual color values used).

`DualTextureEffect::FillGpuDrawParams` (`DualTextureEffect.cpp:248-275`) confirms `p.texture0 = &texture_->
GetBackend()` (bound as `uTexture`) and `p.texture1 = &texture2_->GetBackend()` (bound as `uTexture2`, unit 1,
`EasyGLGraphicsBackend.cpp:4167-4178`) — `Texture`/`Texture2` map to the shader's `uTexture`/`uTexture2` exactly as
the test's setup (`white_` as `Texture`, `red_`/`green_` as `Texture2`) assumes.

`Update()`'s swap logic (line 100-106): `effect_->setTexture2Property(frame_ < kSwapAtFrame ? red_.get() :
green_.get())` — with `kSwapAtFrame=2`, frame 1 keeps Red, frames 2-4 use Green; the test only asserts at frame 1
(Red phase) and frame 4 (Green phase, well past the swap), correctly avoiding an assertion exactly at the swap
boundary frame itself.

### Logic
`Draw()`'s frame-1 capture guard (`if (frame_ == 1 && !capturedFrame1Set_)`, line 126) and frame-4 completion guard
(`if (frame_ < kFrameCount || done_) return; done_ = true;`, line 134-135) correctly fire exactly once each across
the 4-frame run; `capturedFrame1Set_` prevents a hypothetical re-entry at `frame_==1` (not otherwise possible given
`Update()`'s monotonic `++frame_`, but a harmless defensive guard).

### Memory/resource lifetime
`white_`/`red_`/`green_` are `std::unique_ptr<Texture2D>` owned by the `Game` subclass, outliving `effect_`'s raw
`Texture2D*` references (`setTextureProperty`/`setTexture2Property` store non-owning pointers) — correct ownership
direction, no dangling-pointer risk within this test's lifetime.

### C++ correctness
`makeSolid()` (lines 72-77) constructs a `Texture2D(dev, 1, 1)` then `SetData(&c, 1)` — standard, correct 1x1
solid-color texture construction pattern used consistently across this shard.

### Performance
N/A — 4-frame, 1x1-texture microbenchmark-irrelevant test.

### Testing
This file is itself a test; see Missing or Weak Tests.

## Detailed Findings

No HIGH/CRITICAL/MEDIUM findings.

### F1 — Header comment's "white * C = C" claim is imprecise given the shader's real `*2` doubling, though harmless for this test's saturated colors

- Severity: LOW
- Confidence: HIGH
- Category: documentation / test-design
- Location/symbol: file header, lines 12-14 ("Texture is left as a constant white 1x1 for this sample, so the
  rendered color is exactly whichever color Texture2 currently holds (white * C = C)"); actual shader,
  `EasyGLGraphicsBackend.cpp:3050-3052`
- Evidence: the real formula is `FragColor = (white.rgb * 2.0) * Texture2.rgb * diffuseColor`, i.e. `2*C` (clamped
  to `[0,1]` on write to the 8-bit backbuffer), not literally `white * C = C`. For the fully-saturated Red `(1,0,0)`
  and Green `(0,1,0)` values this test actually uses, `2*1=2` clamps back down to `1`, so the observable result is
  identical to the claimed `C` — the test's pass/fail behavior is unaffected — but the stated reasoning is not
  algebraically accurate for a general (non-saturated) `Texture2` color, and a reader relying on the comment to
  reason about a future variant of this test with a mid-range color would be misled.
- Why it matters: purely a documentation-accuracy issue; the test itself remains correct because it only ever uses
  binary-saturated colors.
- FNA/XNA comparison: the `*2` doubling itself is a faithful, verified port of FNA's `PSDualTexture` HLSL — the
  inaccuracy is in this test file's own explanatory comment, not in the production shader.
- Suggested future action: amend the comment to note the doubling is present but saturates away for these specific
  fully-saturated colors, if this file is touched again.

## Cross-File Observations

- Same swap-proof pattern (`capturedFrame1_` vs. final-frame color, explicit "genuinely changed" cross-check at
  lines 140-143) recurs in `easygl_sample_layered_blend_test.cpp` — a deliberate, consistent idiom across this
  batch's four "Task 498" samples for proving `Update()`-driven state (not a static bake) reached the render.
- `easygl_sampler_state_effect_test.cpp` also uses `DualTextureEffect` with a white second texture, but never makes
  the "white * C = C" claim this file does — F1 is specific to this file's own header wording.

## Missing or Weak Tests

- No case exercises the swap happening on the very first (`frame_==1`) or very last (`frame_==kFrameCount`)
  `Update()` call, which the file's own header (line 18) explicitly calls out as intentionally avoided ("the swap
  happens partway through the run, not on the very first or very last frame") — a reasonable, self-documented scope
  limit, not an oversight, but a boundary-frame variant is absent from this shard.
- No case exercises `Texture2` being un-set (`nullptr`) mid-run, which `DualTextureEffect`'s `FillGpuDrawParams`
  (line 258: `if (texture2_) p.texture1 = ...`) implies falls back to the backend's default white texture
  (`EasyGLGraphicsBackend.cpp:4170-4176`) — untested here.

## Positive Findings

- Independently confirmed the shader's `Texture`/`Texture2` → `uTexture`/`uTexture2` unit mapping and the `*2`
  doubling both against `DualTextureEffect.cpp` and against FNA's own `DualTextureEffect.fx` — the doubling is a
  faithful port, not a divergence.
- The three-assertion structure (frame-1 value, frame-4 value, genuinely-different cross-check) is a robust pattern
  that would actually catch a "value baked once at `Initialize()`" regression, not just a "does it render at all"
  smoke test.

## Final Assessment

A correctly-targeted `Update()`-state-reaches-the-shader regression test whose only issue is a minor, harmless
documentation imprecision (F1) about the exact multiply formula involved.
