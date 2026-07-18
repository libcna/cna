# Audit: examples/easygl_shader_effect_spritebatch_uniform_test.cpp

## Metadata

- Source file: `examples/easygl_shader_effect_spritebatch_uniform_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `ShaderEffect` + `SpriteBatch` custom-uniform regression
  test
- File type: C++ example/integration-test executable (`EasyGLShaderEffectSpriteBatchUniformTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::ShaderEffect` (`ShaderEffect.cpp`, all of it —
  `OnApply`, `SetUniformVec4`, `GetEffectBackendPtr`), `CNA::Internal::Backends::EasyGL::
  EasyGLSpriteBatchBackend::FlushBatch` (`EasyGLGraphicsBackend.cpp:1082-1171`)
- XNA/FNA relevance: `ShaderEffect`/`SpriteBatch::Begin(..., Effect*)` are CNA (`NOXNA`) extensions to the XNA 4.0
  custom-effect-in-SpriteBatch pattern — no direct FNA equivalent to diff against; judged on internal consistency
  and the specific regression it documents instead.
- Main related tests: this file (Task 1077), which the file's own header states was found while validating Task
  946 (`BloomSample`)'s shader-conversion proof.

## Purpose

Regression test for a specific, previously-shipped bug: `ShaderEffect::SetUniformXxx()` calls only affect whichever
GL program is *currently bound*, but `EasyGLSpriteBatchBackend::FlushBatch()` used to compile and bind its own
*separate* GL program from the effect's raw GLSL source text instead of using the effect's own already-compiled
program — meaning any uniform set via `ShaderEffect::SetUniformXxx()` was silently discarded at the real draw. This
test sets a `uMyTint` uniform to blue via `SetUniformVec4()` and confirms a `SpriteBatch`-drawn quad using that
effect actually reads back blue, not the GLSL-default-initialized black that results when the uniform never
reaches the bound program. Placement matches `examples-tests-easygl`.

## Executive Verdict

**Healthy.** The specific historical bug this file documents (custom program vs. shared effect program) was
independently confirmed to be genuinely fixed in the current `EasyGLSpriteBatchBackend::FlushBatch()` source, and
the test's own uniform-set-before-first-`Apply()`-vs-second-`Apply()`-at-flush-time ordering was traced through GL
uniform-persistence semantics and found sound.

## Checklist Results

### API / XNA / FNA parity
N/A as a strict FNA-parity question (`ShaderEffect` is an explicit `NOXNA` CNA extension per its own type name,
`"CNA.ShaderEffect"`, `ShaderEffect.cpp:115`) — correctly not presented as an XNA-namespace API by the production
code, and this test correctly exercises it only through the public CNA extension surface
(`SetUniformVec4`, `Apply`, `IsEffectValid`) rather than reaching into backend internals.

### Behavioral correctness
Traced the exact fix this file's header describes (lines 10-24): confirmed
`EasyGLSpriteBatchBackend::FlushBatch()` (`EasyGLGraphicsBackend.cpp:1082-1171`) now does:
```cpp
::easygl::Program* prog = &program_;
if (customEffect_) {
    auto* backend = dynamic_cast<EasyGLEffectBackend*>(customEffect_->GetEffectBackendPtr());
    if (backend && backend->IsValid()) prog = &backend->GetProgram();
    customEffect_->Apply();
}
prog->use();
```
— i.e. the real draw now binds the *same* compiled `Program` object the `ShaderEffect` itself owns
(`ShaderEffect::GetEffectBackendPtr()` → `effectBackend_.get()`, `ShaderEffect.cpp:103-106`), not a second
independently-compiled copy. This confirms the regression this test guards against is genuinely fixed in the
current source, not merely described by a stale comment.

Traced the test's own uniform-set ordering (lines 101-102: `fx.Apply(); fx.SetUniformVec4("uMyTint", 0,0,1,1);`)
against `ShaderEffect::OnApply()` (`ShaderEffect.cpp:95-101`, which calls `effectBackend_->Bind()` →
`EasyGLEffectBackend::Bind()` → `program_.use()`, i.e. `glUseProgram`) and `SetUniformVec4`
(`ShaderEffect.cpp:38-41` → `EasyGLEffectBackend::SetUniformVec4`, `EasyGLGraphicsBackend.cpp:321-324`, a plain
`program_.set_uniform(loc, x,y,z,w)` call): since `fx.Apply()` binds `fx`'s program via `glUseProgram` immediately
before `SetUniformVec4` issues what is (per the non-DSA-looking call shape) an implicit-current-program
`glUniform4f`-style call, the uniform is set on the correct, currently-bound program — no other `glUseProgram` call
occurs between these two lines in this test. The later re-`Apply()` inside `FlushBatch()` (`customEffect_->
Apply()`, matching the fixed code above) only re-binds the same program via `glUseProgram` again; GL uniform values
persist across `glUseProgram` calls (they are program-object state, not context state), so the previously-set blue
`uMyTint` value survives this second bind untouched — correctly reasoned, no reset-to-default race.

### Logic
`kFragSrc`'s own comment (lines 56-57) states the test's fallback-detection logic precisely: an unset/orphaned
`uMyTint` GLSL uniform default-initializes to `vec4(0)` (black), which is exactly what would be observed under the
pre-fix bug — making "center pixel is blue" vs. "center pixel is black" a clean, unambiguous pass/fail signal for
whether the uniform reached the real draw, without needing a numeric tolerance comparison against a partial value.

### Memory/resource lifetime
`ShaderEffect fx(device, kVertSrc, kFragSrc)` (line 92) is a local, stack-allocated `Effect` passed to
`sb_->Begin(..., &fx)` (line 108) and used only within the same `Draw()` call before going out of scope after
`sb_->End()` — `SpriteBatch::End()` (confirmed via `SpriteBatch.cpp:126-139`) calls `backend_->SetCustomEffect
(nullptr)`, clearing the backend's stored `customEffect_` pointer before `fx` is destroyed, so there is no
dangling-pointer window after this `Draw()` call returns.

### C++ correctness
`IsEffectValid()` is checked (line 93) before proceeding, with an explicit early-`Exit()` on compile failure
(lines 94-98) — correct defensive handling of a GLSL compile error rather than proceeding to use an invalid
program.

### Performance
N/A — single-frame, 1x1-texture test.

### Robustness
The GLSL compile-failure path is explicitly handled (see C++ correctness above) rather than left to crash or
silently proceed with an invalid program — good defensive test design, though see Missing or Weak Tests for the
untested failure-path itself.

### Testing
This file is itself a test; see Missing or Weak Tests.

## Detailed Findings

No HIGH/CRITICAL/MEDIUM findings — the specific regression this file documents was independently confirmed fixed
in the current source, and the test's own internal uniform-set-then-reapply ordering was traced through GL
uniform-persistence semantics and found correct.

## Cross-File Observations

- This is the only file in the current batch that exercises the `ShaderEffect`/custom-`Effect`-in-`SpriteBatch`
  path (`SpriteBatch::Begin(sortMode, blendState, nullptr, nullptr, nullptr, &fx)`) — none of this batch's other
  seven files pass a custom `Effect*` to `SpriteBatch::Begin`.
- Directly corroborates a specific historical bug/fix pairing (Task 1077) the same way
  `easygl_sample_layered_blend_test.cpp` corroborates Task 956 — both files' header comments describe a defect
  whose fix was independently confirmed still present in the current `EasyGLSpriteBatchBackend` source during this
  audit.

## Missing or Weak Tests

- Only `SetUniformVec4` is exercised; `ShaderEffect` also exposes `SetUniformMat4`/`SetUniformVec3`/
  `SetUniformVec2`/`SetUniformFloat`/`SetUniformInt`/`SetUniformFloatArray`/`SetUniformVec2Array`/`SetTexture`
  (`ShaderEffect.hpp`, confirmed via `ShaderEffect.cpp:33-86`), none of which are proven to reach a real
  `SpriteBatch`-driven draw by any test found in this batch — the Task 1077 fix (binding the shared program) should
  make all of these equally correct, but only the `vec4` case has a regression test.
- No case exercises the texture-changes-mid-batch flush path (`FlushBatch()` is also called when `current_texture_
  != &texture`, `EasyGLGraphicsBackend.cpp:1201-1202`) together with a custom effect — this test draws only once,
  so `FlushBatch()`'s custom-effect branch is only exercised via the `End()`-triggered flush, not the
  texture-change-triggered one.
- The GLSL-compile-failure early-`Exit()` path (lines 94-98) itself has no dedicated test proving `IsEffectValid()`
  correctly returns `false` for genuinely malformed GLSL — this file only reaches that branch if the *valid*
  `kVertSrc`/`kFragSrc` somehow fail to compile, which is not the intended test scenario.

## Positive Findings

- Directly, verifiably corroborates that a specific real historical rendering bug (Task 1077: uniforms silently
  orphaned on a never-bound duplicate program) is fixed in the current source — a genuine regression guard, not
  a speculative test.
- The black-vs-blue pass/fail signal design (relying on GLSL's own zero-initialization of an unset uniform) is an
  elegant way to make "did the uniform reach the draw" unambiguous without needing partial-value tolerance
  reasoning.

## Final Assessment

A well-targeted, verifiably-still-valid regression test for a specific historical `ShaderEffect`/`SpriteBatch`
uniform-orphaning bug (Task 1077); its main limitation is scope — only one of `ShaderEffect`'s eight uniform-setter
overloads is exercised through the `SpriteBatch` custom-effect path.
