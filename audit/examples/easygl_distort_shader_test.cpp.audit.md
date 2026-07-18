# Audit: examples/easygl_distort_shader_test.cpp

## Metadata

- Source file: `examples/easygl_distort_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend shader-port pixel-readback test
- File type: C++ example/integration-test executable (`EasyGLDistortTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::ShaderEffect`/`Effect`
  (`ShaderEffect.cpp`/`.hpp`, `Effect.cpp`), `SpriteBatch` (`SpriteBatch.cpp`),
  `CNA::Internal::Backends::EasyGL::EasyGLEffectBackend` (`EasyGLGraphicsBackend.cpp`)
- XNA/FNA relevance: ports a real XNA Game Studio sample shader (`DistortionSample_4_0`'s `Distort.fx`), not a
  file that ships with FNA core itself — verified against the actual sample source at
  `DistortionSample_4_0/Distortion/Content/Distort.fx` (found on disk under `XNAGameStudio/Samples/`).
- FNA reference: N/A directly (sample content, not FNA-core), but the sample targets XNA 4.0's `Effect`/HLSL model,
  which is exactly the surface CNA's `ShaderEffect` (a documented `NOXNA` extension over stock `Effect`) exists to
  serve.
- Main related tests: sibling to `easygl_distortblur_shader_test.cpp` (audited separately in this batch), which
  ports the same file's `DistortBlur` technique (the `distortionBlur=true` branch this file's `Distort` technique
  does not exercise).
- Registered as `cna_test_easygl_distort_shader` / `EasyGL_Distort_Shader` (`EasyGLTests.cmake:365-369`, TIMEOUT
  30s).

## Purpose

Proves the GLSL port of `Distort.fx`'s `Distort` technique (the `distortionBlur=false` branch of
`Distort_PixelShader`) is behaviorally correct: a `DistortionMap` texel of exactly `(0,0)` must act as a sentinel
meaning "pass the `SceneTexture` through unmodified," while any other texel value must be decoded into a genuine UV
displacement (`- (0.5 + ZeroOffset)`) and used to offset the `SceneTexture` sample — not just present in the shader
source, but demonstrably taking effect at render time via a live GPU pixel readback.

## Executive Verdict

**Healthy** — the ported GLSL was checked line-for-line against the actual sample HLSL source (confirmed present on
disk), the test's own worked-through arithmetic for both checks was independently recomputed and found correct, and
the test genuinely discriminates "displacement applied" from "displacement ignored" rather than merely "shader
compiles and something is drawn" (see Behavioral correctness).

## Checklist Results

### API / XNA / FNA parity
N/A directly — `ShaderEffect`/`SetUniformInt`/`SetTexture` are `NOXNA` CNA extensions (confirmed via
`ShaderEffect.hpp`'s `NOXNA` markers on every method used here), by design not part of the XNA `Effect` API surface,
used here purely as the plumbing to get a custom GLSL pixel shader onto the GPU. `SpriteBatch::Begin`/`Draw`/`End`
and `Texture2D::CreateFromPixels`/`GraphicsDevice::GetBackBufferData` are real XNA-4.0-shaped members used correctly
(the `Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr, fx)` six-argument overload
matches FNA's `SpriteBatch.Begin(SpriteSortMode, BlendState, SamplerState, DepthStencilState, RasterizerState,
Effect)`).

### Behavioral correctness
Cross-checked the actual FNA-Game-Studio sample source (`Distort.fx`, lines 19-57) against the ported GLSL
(lines 91-109 of this file):
- HLSL: `displacement -= .5 + ZeroOffset;` where `ZeroOffset = 0.5f/255.0f` → GLSL: `displacement -= vec2(0.5 +
  kZeroOffset);` where `kZeroOffset = 0.5/255.0` — exact match, including the documented reason for the
  `ZeroOffset` epsilon (8-bit channels have no exact representation of 0.5).
- HLSL sentinel branch (`if ((displacement.x==0)&&(displacement.y==0)) finalColor = tex2D(SceneTexture,TexCoord);`)
  ported verbatim as an `if`/`else` on the same condition — the `distortionBlur=false` branch (`finalColor =
  tex2D(SceneTexture, TexCoord.xy + displacement)`) is what's actually ported here (the `distortionBlur=true` loop
  is correctly *not* included in this technique's GLSL, deferred to the sibling `DistortBlur` file).
- **Check A arithmetic re-verified**: `DistortionMap` texel0 = `(0,0,0,255)` (the file's own comment states
  `(0,0,*,255)`) → `displacement.rg = (0,0)` exactly → sentinel branch taken → `SceneTexture` sampled unmodified at
  `TexCoord≈0.25` (screen `x=W/4`) → texel0 of a 2-wide `SceneTexture` (`{255,0,0,255}, {0,255,0,255}`) → Red.
  Matches `isRed(a)` check (`R>=200 && G<=30 && B<=30`).
- **Check B arithmetic re-verified**: `DistortionMap` texel1 = `(25,25,0,255)` → decoded R channel = `25/255 -
  0.5 - 0.5/255 = 0.098039... - 0.5 - 0.001961... ≈ -0.4039` (matches the file's own comment's `-0.404`) → sampled
  `SceneTexture` UV = `0.75 + (-0.404) ≈ 0.346` → for a 2-texel-wide texture with texel boundaries at `{0, 0.5, 1.0}`,
  `0.346` falls in texel0's `[0, 0.5)` span → Red, even though the *undisplaced* UV `0.75` would have read texel1
  (Green). This is a genuine discriminator: a shader that dropped, mis-signed, or mis-scaled the displacement would
  read texel1 (Green) at Check B instead, and the test would correctly report FAIL.
- `SamplerState::PointClamp` (line 180) is applied to texture-unit 0 (the `SceneTexture` slot, matching the
  original's `register(s0)`) via `sb.Begin(...)`'s sampler-state argument, eliminating any bilinear-blend ambiguity
  at the non-texel-center sample position (`0.346`) that Check B depends on.

### Logic
`Initialize()` writes three small files (`.vert.glsl`/`.frag.glsl`/`.cnj`) to a per-instance temp directory (keyed by
`this` pointer, avoiding collisions between concurrent test-binary runs) and loads them through the real
`ContentManager`/`.cnj` pipeline rather than constructing a `ShaderEffect` directly — exercising the actual
content-loading path a game would use, not a shortcut. `Draw()` is guarded by `done_` so the check sequence
(render → two 1×1 `GetBackBufferData` readbacks → pass/fail) runs exactly once.

### Memory/resource lifetime
`fxBase_` is a `shared_ptr<Effect>` returned by `ContentManager::Load<>` — ownership matches the `Load<T>` API's own
contract (shared, since content can be cached/reused across callers). `sceneTex_`/`distortionMap_` are plain
`Texture2D` value members (not pointers), consistent with `Texture2D`'s value-type-with-GraphicsResource-guts
pattern used throughout this test population.

### C++ correctness
`dynamic_cast<ShaderEffect*>(fxBase_.get())` (line 168) is checked for null before use (`if (!fx || ...)`), avoiding
a null-deref if `.cnj` loading ever returned a different concrete `Effect` subtype. `reinterpret_cast<std::uintptr_t>
(this)` (line 128) used only to build a unique temp-directory name — a legitimate, non-UB use of the pointer's bit
pattern for a filesystem-path string, not for any aliasing/dereference purpose.

### Robustness
If `.cnj` load or GLSL compile fails, the test explicitly detects this (`!fx || !fx->IsEffectValid()`) and reports a
clean `[FAIL]` with a descriptive message rather than crashing on a null effect pointer later — correct
fail-safe design for a test whose exit code is script-consumed.

### Testing
This file thoroughly covers only the `distortionBlur=false` branch's *sentinel* and *displacement-applied* cases; it
does not attempt to test an intermediate/boundary displacement magnitude or a negative-displacement value (only one
sign of the R-channel decode is exercised, by construction of the chosen texel value `25`). This is a reasonable,
proportionate scope for a "does the port work at all" proof rather than an exhaustive numeric-precision test, and
the two cases chosen (zero vs. a value large enough to cross into the neighboring texel) are the two that most
directly discriminate a broken port from a working one.

## Detailed Findings

No HIGH, CRITICAL, or MEDIUM findings. No LOW findings beyond the note below.

### F1 — `uSceneTexture` sampler uniform relies on GLSL's implicit-zero default rather than being set explicitly

- Severity: INFO
- Confidence: HIGH
- Category: maintainability
- Location/symbol: fragment shader `uniform sampler2D uSceneTexture;` (line 95); test never calls
  `fx->SetUniformInt("uSceneTexture", 0)`, only `fx->SetUniformInt("uDistortionMap", 1)` (line 184).
- Evidence: `EasyGLEffectBackend::BindTexture` (`EasyGLGraphicsBackend.cpp:345-353`) binds a texture object to a GL
  texture *unit* via `glActiveTexture`/`BindGL()` but never itself calls `glUniform1i` to point a sampler uniform at
  that unit — that binding only happens when `SetUniformInt` is called explicitly. `uSceneTexture` is never bound
  this way in this test; it works only because (a) GLSL sampler uniforms default to unit 0 when never explicitly
  set after program link, and (b) `SpriteBatch`'s own draw call independently binds the sprite's primary texture
  (`sceneTex_`) to unit 0 as part of its normal operation — the same convention the original HLSL encodes explicitly
  via `register(s0)`.
- Why it matters: correct today (verified both preconditions hold), and actually a faithful reproduction of the
  original shader's own register-slot convention (`SceneTexture : register(s0)`), not an accidental omission — but
  it is an *implicit* correctness dependency (GLSL default-uniform-value semantics + SpriteBatch's own texture-unit-0
  convention) that isn't stated anywhere in this file's comments, unlike the `uDistortionMap` binding which is
  explicit.
- FNA/XNA comparison: N/A (GLSL-side implementation detail; the original HLSL's `register(s0)`/`register(s1)`
  declarations are the XNA-side analogue, and are exactly what this implicit behavior reproduces).
- Suggested future action (not implemented by this audit): consider adding an explicit
  `fx->SetUniformInt("uSceneTexture", 0);` call for symmetry/self-documentation, if this file is touched again —
  purely a clarity improvement, not a correctness fix.

## Cross-File Observations

- Uses the same `.cnj`-file-based content-loading pattern, temp-directory-per-instance naming scheme, and
  `dynamic_cast<ShaderEffect*>`/`IsEffectValid()` guard idiom as every other `easygl_*_shader_test.cpp` file in this
  batch — a consistent, well-established test-authoring convention across the Task 947 shader-port rollout.
- The `SamplerState::PointClamp` usage here specifically to disambiguate a non-texel-center sample point is a good
  practice not every sibling test in this batch needs (some, like the HeatHaze/PullIn tests, sample exact pixel
  centers where filtering mode is irrelevant) — worth noting this file actually needed the extra care and applied
  it correctly.

## Missing or Weak Tests

- No case exercises a negative-signed displacement (only positive R/G channel values relative to the `0.5+
  ZeroOffset` midpoint are used) — a shader that flipped the subtraction's sign would still be caught by Check B
  (displacement would then land even further from texel0, likely still landing in texel0's region or wrapping via
  `Clamp` addressing, so the specific failure mode isn't guaranteed to flip the test's pass/fail outcome in every
  possible sign-error scenario). Low priority given the test already discriminates the most likely regression
  (displacement not applied at all).

## Positive Findings

- Genuinely traces to and matches the real, on-disk XNA Game Studio sample source rather than a paraphrase —
  confirmed via direct file comparison.
- The choice of `PointClamp` sampling and a displacement magnitude large enough to cross a full texel boundary are
  both deliberate, well-reasoned choices that make Check B a real discriminator, not a coincidental pass.

## Final Assessment

An accurate, well-verified shader-port proof test: the GLSL is a faithful line-for-line port of the actual sample
HLSL, and both pixel-readback checks were independently recomputed and confirmed to genuinely distinguish "the
displacement decode/application logic works" from "it doesn't" rather than merely confirming the shader compiles
and something renders.
