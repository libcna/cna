# Audit: examples/easygl_shader_effect_test.cpp

## Metadata

- Source file: `examples/easygl_shader_effect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `ShaderEffect` (GLSL custom-shader) + `SpriteBatch` integration proof
  (Task 132)
- File type: raw `Game`-derived executable (not `PixelTestGame`-based), manual `printf`/exit-code PASS/FAIL
- XNA/FNA relevance: `ShaderEffect` itself is a `NOXNA` CNA extension (no FNA equivalent — FNA/XNA has no
  GLSL-source-based effect type), but it is exercised through the genuinely XNA-facing
  `SpriteBatch::Begin(SpriteSortMode, BlendState, SamplerState*, DepthStencilState*, RasterizerState*, Effect*)`
  overload, which is checked below against FNA's parameter shape.
- Related production files: `include/Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp` /
  `src/…/ShaderEffect.cpp`, `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EasyGLSpriteBatchBackend::FlushBatch()`, `EasyGLEffectBackend::BindTexture()`).

## Purpose

Proves that a `ShaderEffect` constructed directly from GLSL source strings (not via the `.cnj`/`ContentManager`
pipeline used by every other file in this batch) can be passed to `SpriteBatch::Begin()`'s custom-effect overload
and genuinely replace the built-in sprite shader for one draw. The fragment shader
(`FragColor = vec4(texture(texture1, TexCoord).r, 0.0, 0.0, t.a)`) collapses a white 1×1 texture to pure red;
the test clears to green and checks that the sprite's centre reads red while an uncleared corner still reads
green.

## Executive Verdict

**Healthy.** The single assertion genuinely discriminates a working custom-shader path from the "no shader
applied" case (default sprite output would leave G≈255, failing the `G<=50` check), and every non-default piece
of API surface used (`SpriteBatch::Begin`'s 6-argument overload, `ShaderEffect`'s direct-construction
constructor, `texture1`'s implicit unit-0 default) was cross-checked against the actual `EasyGLSpriteBatchBackend`/
`EasyGLEffectBackend` implementation and confirmed correct, not merely assumed from the comment.

## Checklist Results

### API / XNA / FNA parity
`sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr, nullptr, &fx)` matches
`SpriteBatch::Begin(SpriteSortMode, BlendState, SamplerState*, DepthStencilState*, RasterizerState*, Effect*)`
(`SpriteBatch.hpp` lines 132-137) parameter-for-parameter, which itself mirrors FNA's
`SpriteBatch.Begin(sortMode, blendState, samplerState, depthStencilState, rasterizerState, effect)` overload —
correct XNA-facing call shape. `ShaderEffect` itself (the type being exercised) is correctly `NOXNA`-marked in its
own header since it has no FNA counterpart.

### Behavioral correctness
Verified end-to-end against `EasyGLSpriteBatchBackend::FlushBatch()` (`EasyGLGraphicsBackend.cpp` lines 1082-1145):
when a custom effect is set, `FlushBatch()` binds the *same* compiled program `ShaderEffect` owns
(`customEffect_->GetEffectBackendPtr()`, not a re-parsed copy — Task 1077's fix, comment at line 1087-1091), sets
the `projection` uniform by name (matches this file's own `uniform mat4 projection;` declaration at line 55
exactly), then calls `current_texture_->BindGL()` with no explicit `glActiveTexture()` call — meaning the sprite
texture lands on whatever unit is currently active. Every one of `EasyGLEffectBackend::BindTexture`/
`BindTextureCube`/`BindTexture3D` (lines 345-379) restores `Texture0` as their last step, so the currently-active
unit is always `GL_TEXTURE0` by the time `FlushBatch()` runs — the file's own claim ("`texture1` defaults to
texture unit 0, no explicit uniform-integer set needed") is correct both because GLSL's own spec zero-initializes
unset sampler uniforms and because this call path never disturbs unit 0.

### Logic
`fx.IsEffectValid()` is checked before drawing (lines 115-120) and short-circuits to `Exit()`/`result_=1` on
compile failure — correct fail-fast, not a silent fallback to the built-in shader.

### Memory/resource lifetime
`ShaderEffect fx(device, kVertSrc, kFragSrc)` (line 113) is a Draw()-local stack object; its destructor runs at the
end of `Draw()`'s scope, after `Exit()` is called but before the device is torn down (`Exit()` only requests
shutdown, it does not synchronously destroy the device) — no use-after-free risk. `tex_` is a `Texture2D` value
member, default-constructed then move-assigned via `Texture2D::CreateFromPixels()` (verified this static factory
default-constructs a local `Texture2D`, populates it, and returns by value — `Texture2D.cpp` lines 811-828), the
same pattern used throughout this shard.

### C++ correctness
No raw owning pointers, no casts, no signed/unsigned traps. `getResult()` is a plain `const` accessor, consistent
with every sibling file's `main()` exit-code convention.

### Performance
N/A — single-frame test, not a hot-path concern.

### Robustness
No guard against `getGraphicsDeviceProperty().backend_` being null before constructing `SpriteBatch`/`ShaderEffect`
— acceptable for a test executable (a null backend at this point would already be a fatal `Game::Initialize()`
failure elsewhere, not something this file needs to defend against independently).

### Testing
This file's own single check (centre=red, corner=green) is genuinely discriminating: the default/no-shader
outcome (plain white sprite over green) would fail the `G<=50` centre check, so the assertion cannot pass by
accident if the custom-shader path silently no-ops. See Missing or Weak Tests below for one real gap.

### Cross-file consistency
Consistent with `ShaderEffect.cpp`'s `OnApply()` (binds `effectBackend_` if valid) and `FillGpuDrawParams()`
(irrelevant here since this is a 2D `SpriteBatch` draw, not a `DrawIndexedPrimitives` call — `GpuDrawParams` is
only populated for the 3D draw path exercised by the other 7 files in this batch).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Only R and G channels are asserted at the centre pixel; B and full alpha precision are unchecked

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `EasyGLShaderEffectTest::Draw()`, lines 142-143
- Evidence: `centOk = (centPx.getRProperty() >= 200 && centPx.getGProperty() <= 50)` — the fragment shader also
  writes `FragColor.b = 0.0` and `FragColor.a = t.a`, but the test never asserts `centPx.getBProperty() <= <n>`
  or a bound on alpha.
- Why it matters: a shader mutation that leaves the red channel and the green threshold intact but corrupts the
  blue channel (e.g. echoing `t.g` instead of a constant `0.0`) would not be caught by this test, even though the
  file's own header comment describes the fragment shader as producing exactly `(t.r, 0, 0, t.a)`.
- FNA/XNA comparison: N/A (custom test assertion, not an XNA behavior).
- Suggested future action (not implemented by this audit): add a `centPx.getBProperty() <= 50` check to make the
  assertion match the shader's actual documented output shape.

## Cross-File Observations

- This is the only file in this batch that constructs `ShaderEffect` directly from source strings rather than
  through the `.cnj`/`ContentManager::Load<std::shared_ptr<Effect>>` pipeline the other 7 files use (Task 947+
  convention) — both construction paths are real, supported `ShaderEffect` entry points (`ShaderEffect.hpp`'s
  public constructor vs. `ContentManager`'s type reader), so this is a legitimate second code path being
  exercised, not an inconsistency to flag.
- Unlike every other file in this batch, this test does not create an explicit `GraphicsDeviceManager` — it relies
  on `Game`'s own default device setup. Since every dimension used (`W`, `H`, quad rectangle, pixel regions) is
  read back dynamically from `device.getViewportProperty()` rather than hardcoded, this has no correctness impact
  either way.

## Missing or Weak Tests

- See F1 (blue-channel/alpha not asserted).
- No coverage in this file (or found elsewhere in a quick check of this batch) for `ShaderEffect`'s direct-source
  constructor failing to compile *and* still being passed to `SpriteBatch::Begin()` — the `IsEffectValid()` guard
  here correctly avoids that case for this specific test, but there is no dedicated test proving
  `SpriteBatch::Begin()` itself tolerates (or rejects) an invalid custom effect gracefully.

## Positive Findings

- The single check is self-discriminating without needing a "decoy" mutation-detection trick (unlike several
  sibling files in this batch that needed one for cube/volume textures) — the default built-in sprite shader's
  output is white, not red, so a regression that silently drops back to the built-in shader fails the green-channel
  threshold on its own.
- `texture1`'s implicit unit-0 binding assumption was independently verified against the real
  `EasyGLEffectBackend::BindTexture()`/`FlushBatch()` implementation rather than taken on faith from the comment.

## Final Assessment

A small, correctly-scoped proof of the direct-construction `ShaderEffect` + `SpriteBatch` custom-effect path,
whose single assertion is genuinely discriminating; the only gap is an incomplete channel assertion (F1, LOW) that
does not affect the test's actual pass/fail correctness today.
