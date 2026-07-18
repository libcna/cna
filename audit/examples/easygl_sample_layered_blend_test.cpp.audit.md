# Audit: examples/easygl_sample_layered_blend_test.cpp

## Metadata

- Source file: `examples/easygl_sample_layered_blend_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `SpriteBatch` multi-layer alpha-compositing sample/test
- File type: C++ example/integration-test executable (`LayeredBlendSample : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::SpriteBatch::Begin`
  (`SpriteBatch.cpp:48-124`), `CNA::Internal::Backends::EasyGL::EasyGLSpriteBatchBackend::Begin`/`FlushBatch`
  (`EasyGLGraphicsBackend.cpp:1034-1049`, `1082-1171`), `GraphicsDevice::ApplyBlendState`
  (`EasyGLGraphicsBackend.cpp:1904-1922`), `BlendState::Opaque`/`NonPremultiplied` (`BlendState.cpp:7-9`)
- XNA/FNA relevance: `SpriteBatch.Begin(SpriteSortMode, BlendState)`, `BlendState.NonPremultiplied` straight-alpha
  compositing — judged against FNA's `SpriteBatch.cs::Begin()` (always applies the passed `BlendState`) and the
  documented straight-alpha blend factor pair `(SourceAlpha, InverseSourceAlpha)`.
- Main related tests: this file (Task 498, sample 4/4); this file's own header cites Task 956 as the fix this file
  is now written to correctly exercise (and, per its comment, previously only passed *despite* the bug being live).

## Purpose

A classic XNA 2D layering demo: an opaque solid-Blue background sprite drawn with `BlendState::Opaque`, then a
semi-transparent White overlay drawn with `BlendState::NonPremultiplied` on top, whose alpha is driven by
`Update()`-based frame-counter state (a two-stage fade: alpha=64 for frames <2, alpha=192 for frames ≥2), proving
the full `Update()`-state → `SpriteBatch` layering → real alpha compositing → EasyGL draw pipeline. Placement
matches `examples-tests-easygl`.

## Executive Verdict

**Healthy.** Independently re-derived the expected composited pixel values from the actual `BlendState::Opaque`/
`NonPremultiplied` blend-factor definitions and confirmed both the test's own expected constants and the
underlying `EasyGLSpriteBatchBackend::Begin()` fix (Task 956, referenced in this file's own header) are genuinely
present in the current backend source, not just claimed by a stale comment.

## Checklist Results

### API / XNA / FNA parity
`sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque)` / `Begin(SpriteSortMode::Deferred, BlendState::
NonPremultiplied)` (lines 122, 126) use the real 2-argument `SpriteBatch::Begin` overload
(`SpriteBatch.cpp:54-58`) — confirmed this overload forwards to the full `Begin()` which unconditionally calls
`graphicsDevice_->setBlendStateProperty(blendState)` (`SpriteBatch.cpp:94`), i.e. the passed `BlendState` is always
actually applied to the device, not silently dropped.

### Behavioral correctness — independent formula re-derivation
Traced `BlendState::Opaque` / `NonPremultiplied` (`BlendState.cpp:7-9`): `Opaque = {ColorSrc=One, ColorDst=Zero,
AlphaSrc=Zero → in this file's ctor: (One, One, Zero, Zero)}`; confirmed `EasyGLGraphicsBackend::ApplyBlendState`
(lines 1904-1922) detects the `(One,One,Zero,Zero)` pattern and disables blending entirely (`blendEnabled = false`)
— so the background draw is unconditionally opaque, matching the header's framing. `NonPremultiplied = (SourceAlpha,
SourceAlpha, InverseSourceAlpha, InverseSourceAlpha)` enables standard straight-alpha blending.

Re-derived the actual composited color by hand from the real `SpriteBatch` tint-multiply + GL blend-factor pipeline
(not merely trusting the test's own comment): overlay is a 1x1 White texture (`(1,1,1,1)`) drawn with tint
`Color(255,255,255,alphaByte)`, i.e. `tint.rgb=(1,1,1)`, `tint.a=alpha/255`. `SpriteBatch` multiplies texel×tint
per-channel, giving `fg.rgb=(1,1,1)`, `fg.a=alpha/255`. Under `(SrcAlpha, InvSrcAlpha)` blending:
`result.rgb = fg.rgb*fg.a + bg.rgb*(1-fg.a)`. For the R/G channels (`bg.r=bg.g=0`, Blue background):
`result = 1*(alpha/255) + 0 = alpha/255` → scaled back to a byte, exactly `alpha` — matching the test's expected
`~64`/`~192` (lines 135-136, 144-145) *exactly*, not merely approximately. For the B channel (`bg.b=1`, `fg.b=1`):
`result = 1*a + 1*(1-a) = 1` → stays 255 regardless of alpha — exactly matching the header's own stated reasoning
(lines 19-22) and the `finalPx.getBProperty() >= 240` check (line 147). This is a fully closed-form, independently
verified derivation, not a coincidental pass.

Confirmed the Task 956 fix this file's own header describes (lines 113-121) is genuinely present in
`EasyGLSpriteBatchBackend::Begin()` (`EasyGLGraphicsBackend.cpp:1034-1049`): the function body is now empty except
for `begun = true` and an explanatory comment; it no longer hardcodes `set_blend_enabled(true)` +
`SrcAlpha/OneMinusSrcAlpha` unconditionally. This means `BlendState::Opaque`/`NonPremultiplied` passed to
`SpriteBatch::Begin()` genuinely reach the GPU via `GraphicsDevice::setBlendStateProperty` →
`EasyGLGraphicsBackend::ApplyBlendState`, exactly as this test's comment claims should now happen. This is a real,
currently-live fix, not a stale/reverted comment.

### Logic
`Update()` only increments `frame_` (line 100-104); `Draw()` derives `alpha` from `frame_` each call
(`kFadeUpAtFrame=2`, line 111) — correctly re-evaluated every frame rather than cached once. The frame-1 capture
(`capturedFrame1_`, lines 131-138) and frame-4 completion (`done_`, lines 140-151) guards mirror the same
one-shot-capture idiom used by `easygl_sample_dualtexture_swap_test.cpp`.

### Memory/resource lifetime
`bg_`/`overlay_` (`std::unique_ptr<Texture2D>`) constructed once in `Initialize()`, reused across all 4 frames — no
per-frame allocation.

### Performance
N/A — 1x1 textures, 4-frame run.

### Robustness
No malformed-input path exercised.

### Testing
This file is itself a test; see Missing or Weak Tests.

## Detailed Findings

No HIGH/CRITICAL/MEDIUM findings — the compositing math and the underlying fix this test guards were both
independently confirmed correct and currently live in the codebase.

### F1 — `closeTo()` tolerance (±6) is generous relative to the derived-exact expected values

- Severity: LOW
- Confidence: MEDIUM
- Category: test-coverage
- Location/symbol: `closeTo()` (line 55); its uses at lines 135-136, 144-146, 149
- Evidence: this audit's own hand-derivation above shows the expected R/G values (`alpha` exactly, i.e. 64/192) are
  not approximations but exact closed-form results given the actual blend-factor math (no floating-point rounding
  step beyond the standard 8-bit blend hardware's own quantization). A `±6` tolerance is loose enough that it would
  not catch a blend-factor mix-up that happened to land within ~2% of the correct value (e.g. an
  off-by-one-step gamma/sRGB-decode error in the blend hardware path, which typically shifts mid-tones by more than
  a few counts and likely would be caught, but a smaller systematic bias might not be).
- Why it matters: minor — a tighter tolerance (e.g. ±2-3) would still comfortably absorb real 8-bit blend-hardware
  quantization noise while narrowing the window for a subtly-wrong blend-factor pairing to slip through undetected.
- FNA/XNA comparison: N/A.
- Suggested future action: consider tightening the tolerance if this file is revisited, though not urgent given the
  formula's exactness leaves little room for a *plausible* wrong-but-close result in the first place.

## Cross-File Observations

- Shares the `Update()`-state / captured-frame-1 / genuinely-changed-by-frame-4 three-assertion idiom with
  `easygl_sample_dualtexture_swap_test.cpp` — a consistent, deliberate pattern across this batch's four "Task 498"
  samples.
- This is the one file in the current batch whose header comment describes a specific historical bug (Task 956)
  and whose fix was independently confirmed still present and correctly exercised by the test — a genuine, verified
  regression guard, not merely a description of intent.

## Missing or Weak Tests

- No case exercises `BlendState::AlphaBlend` (premultiplied) with a premultiplied-alpha source texture for
  comparison against the `NonPremultiplied` path this file tests — the two straight-vs-premultiplied alpha paths
  are easy to confuse and only one is covered here (reasonably, since that's this file's stated scope).
- No case verifies that a 3D draw issued *after* this `SpriteBatch` sequence, without explicitly reassigning
  `BlendState`, doesn't inherit `SpriteBatch`'s last-used blend state as "leftover raw GL state" — the exact class
  of bug the Task 956 comment (lines 116-121, referencing the pre-fix EasyGL behavior) describes as also having
  been present; this file's single-`SpriteBatch`-sequence, `Exit()`-immediately structure doesn't exercise that
  post-`SpriteBatch` 3D-draw scenario.

## Positive Findings

- Independently re-derived every expected pixel value in this file from the real `BlendState`/`SpriteBatch`/GL
  blend-factor pipeline and found them to be exact, not approximate — a rigorously correct test.
- Confirmed the specific historical bug this file's header describes (Task 956) is both accurately described and
  genuinely fixed in the current `EasyGLSpriteBatchBackend::Begin()` source.

## Final Assessment

A rigorously-derived, currently-accurate regression test for a real, previously-shipped blend-state bug (Task 956);
the only note worth carrying forward is that its pass/fail tolerance (F1) is looser than the underlying math
strictly requires.
