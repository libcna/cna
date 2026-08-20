# Audit: examples/d3d9_spritebatch_customeffect_test.cpp

## Metadata

- Source file: `examples/d3d9_spritebatch_customeffect_test.cpp` (189 lines)
- Audit status: AUDITED (STATIC/SOURCE-READING ONLY — see Environment Note)
- Subsystem: `examples-tests-d3d9` shard — `SpriteBatch::Begin(effect)` custom-shader wiring
  (`plans/plan_dx9.md` D9-11/D9-112), `Game`-subclass, CTest-registered, real device/window path.
- XNA/FNA relevance: direct — `SpriteBatch.Begin(SpriteSortMode, BlendState, SamplerState,
  DepthStencilState, RasterizerState, Effect)` is real XNA 4.0 API; the mechanics of a custom
  `Effect` replacing the stock `SpriteEffect` shaders for a batch is a genuine, documented XNA
  feature this test targets.
- Related production code read in full: `src/CNA/Internal/Backends/D3D9/D3D9SpriteBatch.cpp`,
  `src/CNA/Internal/Backends/D3D9/D3D9EffectBackend.cpp`,
  `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`Begin()`/`End()`, lines 54-139),
  `src/Microsoft/Xna/Framework/Graphics/ShaderEffect.cpp`,
  `src/Microsoft/Xna/Framework/Graphics/Effect.cpp` (`Apply()`, lines 53-59); cross-checked against
  `src/CNA/Internal/Backends/D3D11/D3D11SpriteBatch.cpp` for the "mirrors D3D11" claim in this file's
  own header comment.

**Environment note (per D-P4/audit instructions):** D3D9 is Windows-only; this report is entirely
static/source-reading. No build or execution was attempted or claimed in this Linux sandbox.

## Purpose

A 4-check test proving `SpriteBatch::Begin(..., Effect*)` genuinely replaces the stock `SpriteEffect`
vertex/pixel shaders for a whole batch with a runtime-compiled custom HLSL pair (a deliberate RGB
color inversion): (A) the custom shader compiles under real SM2 HLSL; (B) sprites drawn through it
show the inverted color, not the stock one; (C) the custom vertex shader's own `vpSize`-driven NDC
transform genuinely positions the sprite (background outside the destination rectangle stays
untouched); (D) a subsequent batch with no effect restores the stock (non-inverting) pipeline.

## Executive Verdict

**Needs attention** — Checks A-C are solid, discriminating, and independently confirmed against
current production code. Check D, however, does not actually exercise the transition it claims to
prove: it constructs a brand-new `SpriteBatch`/backend object rather than reusing the one from
Checks B/C, so it cannot distinguish "the custom-to-stock transition genuinely works" from "a
never-touched fresh object simply defaults to the stock path" — the latter would pass identically even
if the transition logic itself were completely broken. See F1.

## Checklist Results

### API / XNA / FNA parity
`SpriteBatch::Begin(SpriteSortMode, BlendState, SamplerState*, DepthStencilState*, RasterizerState*,
&invertEffect)` (line 121) uses the real 6-argument overload; `sb.Begin()` (line 151) uses the
real 0-argument default overload. Both map correctly to FNA's `SpriteBatch.Begin()` overload set.

### Behavioral correctness
- **Check A**: `ShaderEffect(dev, kVertexShaderSrc, kPixelShaderSrc)` →
  `D3D9GraphicsBackend::CreateEffectBackend()` (`.cpp` lines 941-949) constructs a
  `D3D9EffectBackend` and calls `CompileProgram()` when both sources are non-empty — confirmed
  `IsEffectValid()` reflects the real `D3DCompile()` result (`D3D9EffectBackend.cpp` lines 26-76),
  not a hardcoded `true`.
- **Check B**: traced `D3D9SpriteBatchBackend::FlushBatch()` (`D3D9SpriteBatch.cpp` lines 189-256).
  When `customEffect_` is set and its backend `IsValid()`, the custom path (lines 217-222) uploads
  `vpSize` and calls `customEffect_->Apply()` then `customBackend->Bind()` — **not** the stock
  `EnsureShadersEXT()`/`UploadVertexShaderConstantEXT(..., "MatrixTransform", ...)` path (lines
  223-236). Confirmed the custom pixel shader's own math
  (`float4(1.0 - texColor.rgb, 1.0)`) is what actually executes for a red source texel — the check's
  expected `(0,255,255,255)` (RGB-inverted red, alpha forced to 255 by the shader) is the correct,
  discriminating value (the stock pipeline would leave red unmodified).
- **Check C**: the custom vertex shader's NDC math (`ndc = (Position.xy/vpSize)*2-1; Position =
  (ndc.x, -ndc.y, 0, 1)`) is a standalone, from-scratch transform (not reusing
  `BuildMatrixTransformEXT()`), so this check's "outside-the-rectangle stays Clear()-color" assertion
  is a real, non-trivial proof that `vpSize` genuinely carries the CURRENT viewport size (uploaded
  fresh every `FlushBatch()`, `D3D9SpriteBatch.cpp` line 219) rather than a stale or degenerate value.
- **Check D**: see F1 — the assertion itself (inside color reverts to unmodified red) is correct and
  passes, but does not prove what the test's own comment (lines 21-24) claims.

### Logic
`D3D9SpriteBatchBackend::SetCustomEffect()` (`D3D9SpriteBatch.cpp` lines 86-96): `if (customEffect_ !=
effect) { FlushBatch(); customEffect_ = effect; }` — flushes any pending batch built for the OLD
effect before switching, correctly preventing a batch from being retroactively redrawn with a
different shader than it was queued under. This is the mechanism F1 concerns: it is real and
correctly written, but Check D's own object never exercises the `!=` branch actually being true with a
non-null old value (see F1).

### C++ correctness
`D3D9EffectBackend::UploadEXT()` (`D3D9EffectBackend.cpp` lines 93-121) pads scalar/vec2/vec3 uploads
to a local `float padded[16]` before calling `SetVertexShaderConstantF`/`SetPixelShaderConstantF` —
correctly bounded (`clamped = std::min(componentCount, 16)`), no over-read of the caller's own
2-or-3-element array (`SetUniformVec2`'s `float v[2]`, line 141).

### Memory/resource lifetime
`ShaderEffect`'s `effectBackend_` is a `unique_ptr` owned by the `Effect`/`ShaderEffect` object itself
(not the `SpriteBatch`), and `D3D9SpriteBatchBackend::customEffect_` is a raw, non-owning `Effect*`
(matches D3D11's identical convention) — the test's own `invertEffect` local outlives every
`SpriteBatch` instance that references it (constructed before both `SpriteBatch sb` blocks, destroyed
after `main()`'s own scope ends), so no dangling-pointer risk in this file as written.

### Performance
`FlushBatch()`'s custom-effect branch calls `customEffect_->Apply()` (which — per `Effect::Apply()`,
`Effect.cpp` lines 53-59 — invokes the virtual `OnApply()`, and `ShaderEffect::OnApply()`,
`ShaderEffect.cpp` lines 89-95, calls `effectBackend_->Bind()`) **and then immediately calls
`customBackend->Bind()` again** (`D3D9SpriteBatch.cpp` line 221) — a redundant second
`SetVertexShader`/`SetPixelShader` pair on every flush while a custom effect is active. Confirmed this
exact double-bind pattern is also present, identically, in `D3D11SpriteBatchBackend`'s own
`SetCustomEffect()` path (`D3D11SpriteBatch.cpp` lines 155-157: `customEffect_->Apply();
customBackend->Bind();`) — a pre-existing, harmless (idempotent) cross-backend duplication, not a
D3D9-specific regression. LOW severity, theoretical-only performance cost (two redundant device calls
per flush).

### Architecture
Correctly layered: `D3D9SpriteBatchBackend` reaches `Effect`/`IEffectBackend` only through the public
`GetEffectBackendPtr()`/`Apply()` surface, matching `D3D11SpriteBatchBackend`'s own established
pattern for the identical mechanism (both explicitly documented as "NOXNA, CNA convention" in their
own header comments).

### Maintainability
The file's own header comment (lines 1-26) is detailed and accurate against current production code;
the vertex/pixel shader source strings (lines 62-88) are inlined with a comment correctly noting they
match `Macros.fxh`'s own real SM2/SM3 macro-expansion syntax.

### Robustness
N/A beyond Check A's own compile-failure path (which would make `IsEffectValid()` false and the check
fail loudly rather than silently proceeding with a null/garbage shader) — no additional
malformed-input path is relevant to this file's scope.

### Testing
See F1 for the specific gap. Otherwise this file's 3 solid checks (A-C) give meaningful coverage of
the custom-effect compile/draw/position axes.

### Cross-file consistency
`D3D9SpriteBatch.cpp`'s custom-effect path is confirmed to mirror `D3D11SpriteBatch.cpp`'s own
identical mechanism (including sharing the same minor double-`Bind()` redundancy, F-adjacent
observation above) — consistent with this file's own header comment's explicit "mirrors D3D11's own
DX-71 Check AA exactly" claim.

## Detailed Findings

### F1 — Check D constructs a brand-new `SpriteBatch`/backend rather than reusing the one from Checks B/C, so it cannot actually distinguish "the custom-effect-to-stock transition works" from "a fresh object was never given a custom effect in the first place"

- Severity: MEDIUM
- Confidence: HIGH (directly traced both the test's own scoping and the production default-member
  values that make the untested case indistinguishable from the tested one)
- Category: test-coverage / correctness-of-test
- Location/symbol: Check D's own block, lines 147-163 (`SpriteBatch sb(dev); sb.Begin(); ...`) versus
  Checks B/C's separate, earlier-scoped `SpriteBatch sb(dev)` at line 120 (destructed at the closing
  brace of that block, line 145, before Check D's block begins).
- Evidence: `SpriteBatch::SpriteBatch(GraphicsDevice&)` (`SpriteBatch.cpp` lines 26-30) constructs a
  brand-new `ISpriteBatchBackend` via `graphicsDevice.GetBackend().CreateSpriteBatch()` on every
  call — i.e. Check D's `sb` is backed by a **new** `D3D9SpriteBatchBackend` instance whose own
  `customEffect_` member (`D3D9SpriteBatch.hpp`) defaults to `nullptr` from construction, never
  having been set to anything else. `D3D9SpriteBatchBackend::SetCustomEffect()`'s actual
  transition-handling logic (`if (customEffect_ != effect) { FlushBatch(); customEffect_ = effect; }`,
  lines 91-95) is therefore invoked in Check D with `customEffect_` already `nullptr` and `effect`
  also `nullptr` — the `!=` comparison is `false`, so the flush-and-swap branch is never even
  entered. The stock (non-inverting) pipeline in Check D's draw is used simply because this fresh
  object's `customEffect_` was always `nullptr`, not because any "restore" logic fired. A
  hypothetical regression that broke the transition (e.g. `SetCustomEffect()` failing to actually
  clear a previously-bound custom shader's `vs_`/`ps_` state on the SAME backend object across
  multiple `Begin()`/`End()` cycles — the realistic XNA usage pattern, since most real games construct
  one `SpriteBatch` and reuse it every frame) would not be caught by this check as written, because
  the check never puts a single backend instance through the actual custom→stock transition at all.
- Why it matters: the test's own header comment (lines 21-24) explicitly claims Check D is "proving
  `SetCustomEffect(nullptr)` genuinely restores the stock SpriteEffect path rather than leaving the
  custom shader stuck bound" — a claim about a *stateful transition* on a persistent object, which is
  a meaningfully stronger property than "a never-used object defaults correctly," and the current
  check only demonstrates the latter.
- FNA/XNA comparison: N/A (test-authoring issue; the underlying production `SetCustomEffect()` logic
  was independently read and appears structurally correct — this finding is about what the test
  proves, not a confirmed production bug).
- Related files: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`Begin()`/`End()`, which
  *does* unconditionally call `backend_->SetCustomEffect(nullptr)` on every `End()`, line 135, and
  `backend_->SetCustomEffect(customEffect_)` on every `Begin()`, line 114, regardless of the previous
  batch — meaning the actual restore mechanism IS exercised at the shared XNA-layer `SpriteBatch`
  class level by ordinary use, just not provably so by this specific test file with a fresh
  per-block object).
- Suggested future action (not implemented by this audit): reuse the SAME `SpriteBatch sb` instance
  (or explicitly construct the D3D9 backend once and drive two `Begin()`/`Draw()`/`End()` cycles
  against it — first with `&invertEffect`, then with none) so Check D genuinely forces
  `D3D9SpriteBatchBackend::customEffect_` through a non-null→null transition on one persistent object,
  which is the actual property the check's own comment claims to prove.

## Missing or Weak Tests

- See F1.
- No check exercises a THIRD effect switch (custom effect A → custom effect B, both non-null) to
  confirm `FlushBatch()`-on-change fires for an effect-to-effect transition, not just an effect-to-
  none or none-to-effect one.

## Positive Findings

- Checks A-C are precise, correctly targeted, and independently confirmed against current production
  code (`D3D9EffectBackend`/`D3D9SpriteBatchBackend`) — Check C in particular is a genuinely
  discriminating geometric-placement proof, not just a color-presence check.
- The custom vertex/pixel shader source strings are real, compilable SM2 HLSL matching this backend's
  own established macro conventions, not a simplified stand-in.

## Final Assessment

Three of the four checks in this file are strong and correctly verify real, current production
behavior. The fourth (Check D) asserts a value that is genuinely correct, but for a materially weaker
reason than its own header comment claims — it verifies a fresh object's default state rather than a
genuine custom-to-stock transition on a persistent one, and so would not catch a real regression in
the transition logic it is nominally there to protect.
