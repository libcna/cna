# Audit: examples/sdlgpu_shadereffect_test.cpp

## Metadata

- Source file: `examples/sdlgpu_shadereffect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — custom `ShaderEffect` (runtime GLSL→SPIR-V) proof for
  the SDL_GPU backend
- File type: standalone `Game`-subclass executable, CTest-registered (`SdlGpu_ShaderEffect`,
  `cmake/Tests/SdlGpuTests.cmake:96-99`, `TIMEOUT 60 LABELS "SdlGpu"`)
- XNA/FNA relevance: indirect — `ShaderEffect` is a CNA-only (`NOXNA`) extension, not part of XNA
  4.0; the surrounding `SpriteBatch.Begin(effect)`/`Draw()`/`End()` call sequence it is verified
  through is direct XNA API (`SpriteBatch.Begin` custom-effect overload).
- FNA reference: N/A for `ShaderEffect` itself (no such type in FNA); `Graphics/SpriteBatch.cs`
  for the `Begin(SpriteSortMode, BlendState, SamplerState, DepthStencilState, RasterizerState,
  Effect)` overload this test drives the custom shader through.
- Related production code: `include/Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp` (correctly
  `NOXNA`-tagged, lines 25, 35, 43 etc.), `src/CNA/Internal/Backends/SdlGpu/
  SdlGpuGraphicsBackend.cpp` (`SdlGpuEffectBackend::CompileProgram`/`SetUniformVec4`/
  `GetOrCreatePipeline`, lines 4827-5020; `CompileGlslToSpirv`/libshaderc `extern "C"` bindings,
  lines 424-450+).

## Purpose

Three-check pixel test proving a *real* runtime GLSL→SPIR-V compile (via libshaderc, since SDL_gpu
only accepts precompiled bytecode) end-to-end through the public XNA-facing `SpriteBatch` API: (A)
`IsEffectValid()` is true post-compile, (B) a custom fragment shader's own `uColor`-multiply
uniform produces an exact tinted pixel, (C) the same draw with no custom effect reads back plain
white, discriminating "the custom shader's own uniform is genuinely read" from "the custom
pipeline/uniform is ignored and the stock sprite shader renders regardless." Correct placement;
`ShaderEffect` is properly a `NOXNA`-marked CNA extension, not miscategorized as XNA API.

## Executive Verdict

**Healthy.** All 3 checks were independently traced to real backend code (the runtime shaderc
compile path, the fixed-layout uniform push-constant convention, and the `SpriteBatch`
custom-effect dispatch) and confirmed to exercise what they claim. One notable, accurately-labeled
architectural constraint (uniform names are ignored; a fixed byte-offset convention is used
instead) is consistent with every sibling `EffectBackend` in this codebase and does not affect this
specific test's correctness, but is worth flagging as a real API-surface gap for `ShaderEffect`
users in general (see F1).

## Checklist Results

### API / XNA / FNA parity
`ShaderEffect` (constructor, `IsEffectValid()`, `SetUniformVec4()`) is correctly `NOXNA`-tagged in
the header (`ShaderEffect.hpp` line 25 `NOXNA class ShaderEffect`, and every public method) — not
misplaced into the XNA-facing surface. `SpriteBatch::Begin(SpriteSortMode::Immediate, BlendState::
Opaque, nullptr, nullptr, nullptr, customEffect)` (line 114) matches FNA's real 6-argument
`Begin` overload's parameter order (`sortMode, blendState, samplerState, depthStencilState,
rasterizerState, effect`) exactly, with the 3 state parameters correctly defaulted to `nullptr`
(meaning "use the built-in default," per FNA's own `Begin` semantics) since this test only cares
about the custom effect parameter.

### Behavioral correctness
- Check A (line 145): `effect_->IsEffectValid()` reads `SdlGpuEffectBackend::valid_`, which is only
  set `true` at the very end of `CompileProgram()` (line 4890) after both
  `CompileGlslToSpirv(vertSrc,...)` and `CompileGlslToSpirv(fragSrc,...)` succeed *and*
  `SDL_CreateGPUShader` succeeds for both stages (lines 4852-4888) — a genuine multi-stage success
  gate, not a constructor-always-true stub. `CreateEffectBackend` (lines 5013-5020) calls
  `CompileProgram` unconditionally in the `ShaderEffect` constructor (via `LoadContent()`'s
  `std::make_unique<ShaderEffect>(dev, kVertSrc, kFragSrc)`, line 134), so `IsEffectValid()` at
  Check A time genuinely reflects a completed real compile, not an async/deferred one.
- Check B (lines 147-156): the custom fragment shader (`kFragSrc`) multiplies a sampled white
  texture by `pc.color`, a value this test sets via `SetUniformVec4("color", 0.2f, 0.6f, 1.0f,
  1.0f)`. Traced `SetUniformVec4` (line 4982) to `pushConst_[20..23] = x,y,z,w` — and confirmed via
  the class-level layout comment (lines 4974-4976, "Fixed 128-byte layout... [80..95]=vec4 color")
  that byte offset 80 = float index 20, exactly matching the custom fragment shader's own declared
  `PC` struct layout in this test file (`vpSize_pad`(16B) + `matrix`(64B) + `color`(16B at byte 80)
  + `slot0_pad`), so the value this test sets is read by the exact uniform the shader multiplies
  by. `wantTint=(51,153,255,255)` (0.2,0.6,1.0,1.0 × 255, rounded) is arithmetically correct for
  `white(1,1,1,1) * tint`, and the `±1` tolerance (lines 150-152) is appropriately tight for a
  byte-exact, no-blend multiply.
- Check C (lines 158-161): the same draw with `customEffect=nullptr` reads back plain white
  (255,255,255) with **exact** equality (`==`, not `Matches()`/tolerance) — this is the load-bearing
  discriminator the header comment describes: if the custom pipeline/uniform were silently ignored
  and every draw fell through to the stock sprite shader regardless of the `Effect*` argument,
  Check B's tinted result could never have appeared in the first place (the stock shader has no
  tint uniform), so Check C's real purpose is confirming the *opposite* failure mode doesn't hold —
  that Check B's tint came from the custom shader genuinely being bound, not from some other stray
  state leaking a tint onto the stock shader's output. Both checks together correctly close the
  discrimination loop.

### Logic
`RenderCenterPixel` (lines 108-124) is called twice with different `customEffect` values but is
otherwise identical, correctly isolating "which effect is bound" as the only variable between
Check B and Check C's two renders.

### C++ correctness
`SdlGpuEffectBackend::CompileProgram` correctly releases *all* previously-created pipelines and
both shader stages before attempting the new compile (lines 4844-4850) — avoids leaking GPU
shader/pipeline handles if `CompileProgram` is ever called more than once on the same
`ShaderEffect` instance (not exercised by this specific test, which compiles once, but correct
defensive design for the general API). Partial-failure cleanup within `CompileProgram` itself is
also correct: if the fragment-stage `SDL_CreateGPUShader` fails after the vertex stage already
succeeded, the vertex shader is explicitly released before returning `false` (lines 4882-4887) —
no dangling `SDL_GPUShader*` leak on the failure path.

### Robustness
`Check A` runs before `Check B`, so if `CompileProgram` silently failed (`valid_` stays `false`),
Check A would already report `FAIL` and the test would still proceed into Check B/C, whose
`RenderCenterPixel` calls a null/never-bound pipeline path — traced `GetOrCreatePipeline` (line
4898-4899): `if (!valid_) return nullptr;`, and the actual sprite-draw issue code (not shown in this
excerpt but implied by the null-pipeline-tolerant early return) would need to itself tolerate a null
pipeline gracefully rather than dereferencing it. This audit did not trace the null-pipeline
draw-issue path exhaustively — flagged as a LOW-confidence gap, not a confirmed defect (see F2).

### Testing
3/3 checks, each independently meaningful and non-redundant; appropriately scoped for a
feature-proof test rather than a general `ShaderEffect` API surface test (uniform setter overloads
other than `SetUniformVec4` — `SetUniformMat4`, `SetUniformVec3`, `SetUniformFloat`, `SetUniformInt`,
array variants — are declared in the header but not exercised by this specific file; likely covered
by `tests/Microsoft/Xna/Framework/Graphics/ShaderEffectTests.cpp`, out of this batch's scope).

## Detailed Findings

### F1 — `SetUniform*` methods ignore the `name` parameter entirely; every value writes to a fixed byte offset regardless of what name is passed

- Severity: LOW (consistent, documented, cross-backend convention — not specific to this file or a
  regression; flagged for API-surface awareness, not as a live bug in this test)
- Confidence: HIGH (read `SetUniformVec4`/`SetUniformVec3`/`SetUniformVec2`/`SetUniformFloat`/
  `SetUniformInt`/`SetUniformMat4`, all take `const char* /*name*/` explicitly marked unused, lines
  4977-5005)
- Category: architecture / API-design
- Location/symbol: `SdlGpuEffectBackend::SetUniformVec4` etc. (`SdlGpuGraphicsBackend.cpp` lines
  4974-5005)
- Evidence: the class-level comment states explicitly: *"`name` is deliberately ignored, matching
  every sibling `EffectBackend`'s own convention."* Confirmed this is not a stale/local shortcut:
  the fixed 128-byte push-constant layout (`vpSize`[0-15]/`matrix`[16-79]/`color`[80-95]/
  `slot0`[96-99]) is a single hardcoded shape shared by every custom `ShaderEffect` on this backend
  — calling `SetUniformVec4("literally_anything", ...)` writes to the same `pushConst_[20..23]`
  slot regardless of the string passed.
- Why it matters: this test happens to pass a `name` ("color") that coincidentally matches the
  custom shader's own field name, but the production API would behave identically if the test had
  called `SetUniformVec4("banana", ...)` instead — the name string is purely documentation for the
  caller, not a real binding key. A future `ShaderEffect` user writing a custom shader with *two*
  `vec4` uniforms (e.g. `tint` and `outlineColor`) would have no way to address the second one
  through this API on this backend — both would alias the same `pushConst_[20..23]` slot (or
  whichever fixed slot each named setter targets), silently overwriting each other. This is a real
  API-surface limitation of `ShaderEffect` as currently implemented (cross-backend, not specific to
  SdlGpu), not something this particular test's own 3 checks would ever surface, since it uses only
  one uniform.
- FNA/XNA comparison: N/A — `ShaderEffect` is a `NOXNA` CNA-only extension with no FNA equivalent to
  compare against; this is purely an internal API-design observation.
- Suggested future action (not implemented by this audit): document this fixed-slot-per-call-site
  limitation prominently in `ShaderEffect.hpp`'s own Doxygen (a future reader relying on `name` as
  a real binding key would otherwise be surprised), or extend the backend to a genuine
  name→offset reflection table if multi-uniform custom effects become a real use case.

### F2 — Unverified: whether a `!valid_` (failed-compile) `ShaderEffect` bound via `SpriteBatch.Begin(effect)` is guaranteed not to crash on draw

- Severity: LOW
- Confidence: LOW (not traced to a concrete crash; flagged as an unverified robustness question,
  not a confirmed defect)
- Category: robustness
- Location/symbol: `GetOrCreatePipeline` (`SdlGpuGraphicsBackend.cpp` lines 4894-4899,
  `if (!valid_) return nullptr;`)
- Evidence: `GetOrCreatePipeline` correctly returns `nullptr` for an invalid effect, but this audit
  did not trace the sprite-draw-issue call site that consumes this return value (outside this
  file's own scope) to confirm it null-checks before binding the pipeline to a render pass. This
  specific test never exercises the failed-compile path (`CompileProgram` always succeeds for its
  fixed, valid GLSL sources), so this file provides no evidence either way.
- Why it matters: if a real `ShaderEffect` client supplied genuinely invalid GLSL (a realistic use
  case for a `NOXNA` runtime-shader-compile feature), a subsequent `Draw()` with that effect bound
  could plausibly crash rather than degrade gracefully — but this is speculative pending tracing
  the actual sprite-issue code, not a confirmed finding.
- Suggested future action (not implemented by this audit): add a dedicated negative test (e.g.
  `sdlgpu_shadereffect_invalid_test.cpp`) that deliberately supplies malformed GLSL and asserts
  either a clean exception at `CompileProgram` time or a well-defined no-crash fallback at draw
  time — closing this open question definitively.

## Cross-File Observations

- `SpriteBatch.Begin`'s custom-effect overload and the null-effect default path are exercised
  together in the same file via Check B/C — a good pattern also worth checking for consistency
  against other backends' own `*_shadereffect_test.cpp` or `*_customeffect_test.cpp` equivalents
  (out of this batch's scope) for whether they share F1's fixed-slot limitation.
- No fog/skinned-normal-matrix/EnvironmentMapEffect cross-cutting item applies to this file — custom
  `ShaderEffect` shaders are entirely user-authored GLSL with no relationship to this project's own
  stock 3D shader family.
- Git history (`57632343`/`8130152a`, "close SDLGPU-42/43 -- SDL_GPU custom ShaderEffect") is this
  file's sole authoring commit; the header's "no libshaderc-dev package available... hand-declared
  extern "C" prototypes" claim (lines 424-430 of the production source) is corroborated by the same
  commit and remains architecturally consistent with `CompileGlslToSpirv`'s current implementation.

## Missing or Weak Tests

- See F1 — no test in this file (or, as far as this audit traced, elsewhere in this shard) exercises
  a `ShaderEffect` with more than one distinct named uniform of the same GLSL type, which would be
  the concrete scenario that surfaces F1's aliasing risk.
- See F2 — no negative/malformed-GLSL test exists for this backend's `ShaderEffect` compile path.

## Positive Findings

- The 3-check design is a genuinely tight, well-reasoned discriminator: Check B alone could pass
  "by accident" if some unrelated state (e.g. a stray vertex-color tint) happened to produce the
  same RGB value, but Check C's exact-white assertion on the *same* draw shape with the effect
  removed closes that gap convincingly.
- `CompileProgram`'s resource-cleanup discipline (releasing prior pipelines/shaders before a new
  compile attempt, and releasing the vertex shader if the fragment shader fails to compile) is
  correct and was independently verified by reading the actual release calls, not assumed from the
  test passing.
- `ShaderEffect` is correctly and consistently `NOXNA`-tagged throughout its header — no XNA-facing
  API-surface leakage found.

## Final Assessment

A precise, well-designed 3-check test whose claims were independently confirmed against the real
runtime-shaderc-compile and fixed-uniform-layout backend code. F1 (name-ignoring uniform API) is an
architectural characteristic shared across every backend's `ShaderEffect`, not a defect in this
file; F2 (failed-compile draw robustness) is an open, unverified question worth a follow-up
negative test but is not demonstrated to be broken.
