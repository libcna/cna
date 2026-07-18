# Audit: examples/easygl_clouds_shader_test.cpp

## Metadata

- Source file: `examples/easygl_clouds_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test, registered as CTest `EasyGL_Clouds_Shader`
  (`cmake/Tests/EasyGLTests.cmake:305-307`, target `cna_test_easygl_clouds_shader`)
- Related production code: `SpriteBatch::Begin`/`Draw`/`End` with a custom `Effect*` parameter
  (`SpriteBatch.cpp`), EasyGL's `SpriteBatch` flush path (`EasyGLGraphicsBackend.cpp:~1020-1160`,
  `uniform_location("projection")`), `SamplerState::PointWrap`, `Texture2D::CreateFromPixels`
- XNA/FNA relevance: ports `NetRumble_4_0/NetRumble/Content/Effects/Clouds.fx`'s
  `PixelShaderFunction` from HLSL to GLSL (Task 947, clears NetRumble's DEFERRED.md shader blocker)
- Main related tests: sibling bloom-trio shaders (Task 946, not in this batch) — together these clear
  every shader NetRumble's `missing.md` lists as blocking.

## Purpose

`EasyGLCloudsShaderTest` proves the EasyGL backend correctly executes a GLSL port of `Clouds.fx`'s
2-sample parallax-cloud pixel shader through the real `SpriteBatch` custom-effect path (not a raw 3D
draw, unlike the sibling CartoonEffect tests in this batch) — verifying both the channel-masking
arithmetic and that the `Position` uniform genuinely reaches and shifts the shader's sampling.

## Executive Verdict

**Healthy.** Independently re-derived the shader's arithmetic by hand from the ported GLSL source for
both checks and got exact agreement with the test's own expected values — this is a rigorously
self-consistent test, not just a plausible-looking one.

## Checklist Results

### API / XNA / FNA parity
`SpriteBatch::Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointWrap, nullptr, nullptr, fx)`
(lines 186-187, 198-199) is the correct XNA `SpriteBatch.Begin` overload for supplying a custom
`Effect` — matches FNA's `SpriteBatch.Begin(SpriteSortMode, BlendState, SamplerState, DepthStencilState,
RasterizerState, Effect, Matrix?)` parameter ordering (positional match: sort mode, blend, sampler,
depth-stencil=null, rasterizer=null, effect).

### Behavioral correctness
Verified `kFragSrc` (lines 96-117) against the FNA reference quoted in the header (lines 8-23):
```hlsl
texCoord.x += Position.x * 0.00025f; texCoord.y += Position.y * 0.00025f; texCoord *= 0.5f;
results = float4(0,0,1,0.25) * tex2D(Sampler, texCoord);
texCoord.x += Position.x * 0.00025f + 0.25f; texCoord.y += Position.y * 0.00025f - 0.15; texCoord *= 0.4f;
results += float4(0,1,0,0.15) * tex2D(Sampler, texCoord);
```
The ported GLSL preserves the one genuinely error-prone detail the header comment (lines 25-30) calls
out: the second block mutates the *already-transformed* `texCoord` left over from the first block, not
a fresh copy of the original `TEXCOORD0` — confirmed line-by-line, `texCoord.x += uPosition.x * 0.00025`
(line 105) operates on the same `vec2 texCoord = TexCoord;` local (line 103) that was already
scaled/offset by lines 105-107, exactly matching HLSL's non-resetting semantics.

Independently re-derived both checks from the ported shader (not just trusted the header comment):
- **Check A** (`Position=(0,0)`, screen-center UV≈`(0.5,0.5)`, texture=2×1 texels
  `[(0,60,180,255), (0,220,20,255)]`):
  - 1st sample: `texCoord=(0.5,0.5)`; `+=0`; `*=0.5 → (0.25,0.25)`; `u=0.25` ∈ `[0,0.5)` → texel0
    `(0,60,180,255)`.
  - 2nd sample: continues from `(0.25,0.25)`; `+= (0.25,-0.15) → (0.5,0.10)`; `*=0.4 → (0.2,0.04)`;
    `u=0.2` ∈ `[0,0.5)` → texel0 again.
  - `result = (0,0,1,0.25)*texel0 + (0,1,0,0.15)*texel0 = (0,0,180,63.75) + (0,60,0,38.25) =
    (0,60,180,102)` — **exactly** matches the test's expected `(0,60,180,102)` (line 212).
- **Check B** (`Position=(4000,0)`):
  - 1st sample: `texCoord=(0.5,0.5)`; `x += 4000*0.00025=1.0 → 1.5`; `*=0.5 → (0.75,0.25)`; `u=0.75` ∈
    `[0.5,1)` → texel1 `(0,220,20,255)`.
  - 2nd sample: continues from `(0.75,0.25)`; `x += 4000*0.00025+0.25=1.25 → 2.0`;
    `y += 4000*0.00025-0.15=0.85 → 1.10`; `*=0.4 → (0.8,0.44)`; `u=0.8` ∈ `[0.5,1)` (mod-1 wrap not
    even needed) → texel1 again.
  - `result = (0,0,20,63.75) + (0,220,0,38.25) = (0,220,20,102)` — **exactly** matches the test's
    expected `(0,220,20,102)` (line 215).

Both derivations match the file's stated expectations exactly (not just approximately), confirming this
test's arithmetic claims are correct, not merely plausible-sounding.

### Logic
The choice of a solid-2-texel (not gradient) texture plus `SamplerState::PointWrap` (documented,
header lines 32-38, as a deliberate simplification swapping the real shader's Linear filter for Point)
is exactly the right technique to make point-sampled texel selection unambiguous and isolate the
channel-masking/`Position`-wiring formula from bilinear-blend arithmetic — a well-reasoned test-design
choice, not an oversight.

### Memory/resource lifetime
`fxBase_`/`cloudTex_` are member `shared_ptr<Effect>`/`Texture2D` (value type), `SpriteBatch sb(device)`
is stack-local within `Draw()` — clean, unambiguous lifetimes throughout.

### C++ correctness
No unusual casts, no raw-pointer ownership. `dynamic_cast<ShaderEffect*>(fxBase_.get())` (line 169) is
checked once (`!fx || !fx->IsEffectValid()`, line 170) before either use in `Draw()` — unlike the
sibling CartoonEffect tests in this batch, there is no redundant second cast here since this file only
calls `fx->Apply()`/`SetUniformVec2` directly in `Draw()`, not through a separate per-call helper.

### Performance / Thread safety
N/A — single-frame test with 2 sequential `SpriteBatch` begin/draw/end passes.

### Architecture
Correct use of the public `SpriteBatch` custom-effect path; no direct backend symbols. Confirmed
cross-file that the EasyGL `SpriteBatch` flush path actually uses the uniform name `"projection"`
(`EasyGLGraphicsBackend.cpp:1139`, `prog->uniform_location("projection")`) matching this test's
`uniform mat4 projection;` declaration (line 87) exactly, and that its vertex-attribute layout
(`vao_.set_attribute_pointer(0, 2, …)` / `(1, 2, …)` / `(2, 4, …)`, lines 1020-1027) matches this
test's declared `layout(location = 0) in vec2 aPos; (1) in vec2 aTexCoord; (2) in vec4 aColor;`
(lines 83-85) attribute-for-attribute — this custom vertex shader is genuinely compatible with the real
`SpriteBatch` vertex stream, not just syntactically valid GLSL.

### Maintainability
Header comment (lines 1-47) is thorough: quotes the FNA source, explains the sequential-mutation
subtlety explicitly (rather than silently "fixing" it), and documents the Point-vs-Linear filter
substitution as a deliberate test-scope decision.

### Portability
No platform-specific code.

### Robustness
Clean `IsEffectValid()` failure path (lines 170-175) before any draw is attempted.

### Testing
Two checks with genuinely different, independently-derived expected values and confirmed real
arithmetic behind them (see above) — strong test. Not tested: negative `Position` values, or a
`Position` that causes point-sampling to land exactly on a texel boundary (the deliberately "comfortably
clear of any boundary" design, header lines 36-38, is itself the reason this isn't tested — intentional
scope, not an oversight).

### Cross-file consistency
Confirmed the ported shader is wire-compatible with the real `SpriteBatch` GL pipeline (uniform name,
attribute layout — see Architecture above), which is a stronger consistency check than most files in
this batch get, since it's exercising `SpriteBatch`'s actual vertex/uniform contract rather than a
private 3D vertex buffer.

## Detailed Findings

No HIGH/MEDIUM/LOW findings — this is one of the more rigorously self-verified files in the batch; the
math checks out exactly rather than approximately.

## Cross-File Observations

- This is the only file in this batch that routes a custom `ShaderEffect` through `SpriteBatch`'s
  actual GL vertex/uniform pipeline rather than a private `VertexBuffer`/`IndexBuffer` pair — worth
  using as the reference example for how a custom sprite shader must declare its attributes/uniforms
  if a future test in this family needs the same integration.

## Missing or Weak Tests

- No negative-`Position` check (both checks use non-negative values); given the formula is purely
  linear (`texCoord.x += Position.x * 0.00025`), a negative value is very unlikely to reveal a distinct
  bug class beyond what Check A/B already establish, but it would be a cheap addition.

## Positive Findings

- Independently re-derivable, exactly-correct expected values for both checks (verified by this audit
  from first principles, not just trusted from the header comment).
- Faithfully preserves the FNA shader's one genuinely subtle detail (sequential, non-resetting
  `texCoord` mutation across the two sample blocks) rather than "cleaning it up."
- Confirmed wire-compatible with the real `SpriteBatch` GL vertex/uniform contract, not just
  syntactically valid GLSL.

## Final Assessment

A rigorously correct, well-documented shader-port test whose arithmetic claims this audit independently
verified to match exactly — the strongest-evidenced file in this batch.
