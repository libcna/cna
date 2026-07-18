# Audit: examples/sdlrenderer_custom_effect_throws_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_custom_effect_throws_test.cpp` (118 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 676 decision test (custom `Effect` on `SpriteBatch::Begin`)
- File type: standalone `Game`-subclass executable (`SdlCustomEffectThrowsTest`), exit-code PASS/FAIL
- XNA/FNA relevance: `SpriteBatch::Begin(sortMode, blendState, samplerState, depthStencilState, rasterizerState, effect)`
  with a custom `Effect` argument — FNA supports this (SM3+ shader replaces the built-in sprite technique);
  SDL_Renderer has no programmable shader stage at all.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`Begin()`/`End()`, lines 81-139),
  `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`SdlSpriteBatchBackend::SetCustomEffect`, lines 83-89).
- Git corroboration: `219eafce`/`cf19656b` `fix(Task 676): SDL_Renderer throws for a custom SpriteBatch effect` —
  header comment's narrative matches the real commit.

## Purpose

Verifies the Task 676 design decision: since SDL_Renderer has no programmable shader stage, a non-null custom
`Effect` passed to `SpriteBatch::Begin()` must throw (rather than silently drawing with the built-in blit and
producing a misrendered frame with no error). Two checks: (1) the overwhelmingly common `nullptr` effect case must
NOT throw, (2) a genuine `ShaderEffect` instance passed as the 6th `Begin()` argument MUST throw.

## Executive Verdict

**Needs attention** — both of the file's own two checks are correct and independently confirmed against production
code. However, tracing the exact code path this test exercises surfaced a real, unrelated production defect in
`SpriteBatch::Begin()`'s exception safety (F1): the object's `begun` flag is set to `true` *before* the very backend
call this test relies on throwing, so this test's own second check leaves the `SpriteBatch` object in a
permanently-inconsistent state that only an undocumented recovery step (`End()`) can clear — and the test itself does
not notice or probe this because it calls `Exit()` immediately afterward without ever attempting to reuse `sb_`.

## Checklist Results

### API / XNA / FNA parity
Confirmed against `SdlGraphicsBackend.cpp` lines 83-89:
```cpp
void SdlSpriteBatchBackend::SetCustomEffect(Effect* effect)
{
    if (effect != nullptr)
        throw std::runtime_error(
            "SDL_Renderer does not support custom SpriteBatch Effects: ...");
}
```
This exactly matches the test's two assertions. `Effect` itself is abstract in this codebase (confirmed no
non-virtual constructor exists that can be instantiated directly) and `ShaderEffect(dev, "", "")` is indeed the
minimal concrete subclass used — its constructor is documented (by this file's own header comment) to safely no-op
when `CreateEffectBackend()` returns `nullptr` (SDL_Renderer's default), so no actual shader-compilation attempt
occurs before `Begin()` is even called; this is consistent with `IGraphicsBackend.hpp`'s default no-op behavior for
unimplemented `CreateEffectBackend()`.

### Behavioral correctness
Both of the test's assertions hold against the real code:
- `sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, nullptr)` — a null `Effect`
  — does not throw, and `sb_->End()` afterward completes cleanly (confirmed: `SpriteBatch::End()` only calls
  `backend_->SetCustomEffect(nullptr)`, which is a documented no-op for `effect == nullptr`).
- `sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, &fx)` — a non-null custom
  `Effect` — does throw `std::runtime_error`, confirmed to originate from `SdlSpriteBatchBackend::SetCustomEffect`
  via `SpriteBatch::Begin()`'s own call chain (`SpriteBatch.cpp` line 114: `backend_->SetCustomEffect(customEffect_)`).

### Logic
See F1 below — the specific sequencing of `SpriteBatch::Begin()`'s field assignments vs. its backend calls is the
root of the finding.

### Robustness / exception safety

**F1 (Detailed Findings)**. This is the most substantive result of auditing this file: exercising the exact scenario
this test is built around (a `Begin()` call that is *designed* to throw) reveals that `SpriteBatch::Begin()` itself
is not exception-safe.

### Testing
The file's own two checks are sound as far as they go, but neither verifies recovery after the induced exception —
see "Missing or Weak Tests" and F1.

### Cross-file consistency
`SpriteBatch::Begin()`'s eager, unconditional state-application design (calling `graphicsDevice_->setBlendStateProperty`,
`backend_->SetCustomEffect`, `SetTransformMatrix`, `SetSamplerFilter`, `SetSamplerAddressMode`, `backend_->Begin()`
all inline, regardless of `sortMode`) is itself a deliberate-looking divergence from FNA's own `SpriteBatch.Begin()`
(`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/SpriteBatch.cs` lines 273-305), which only calls
`PrepRenderState()` — the equivalent state-application step — when `sortMode == SpriteSortMode.Immediate`; for every
other sort mode FNA defers all state application to `FlushBatch()`/`PrepRenderState()`, invoked from `End()` (or from
each `Draw` call in Immediate mode). Because of this, in FNA, `Begin()` cannot itself fail due to a state-application
error for non-Immediate modes — only the `beginCalled` double-`Begin()` guard can throw from `Begin()`. This CNA port
made `Begin()` unconditionally eager, which is what exposes F1: an SDL_Renderer-specific throw (or any future
backend's own `SetCustomEffect`/`SetTransformMatrix`/`SetSamplerFilter`/`Begin()` failure) is now reachable *from
inside* `Begin()` itself for every sort mode, not just Immediate — a path FNA's own design never has to protect.

## Detailed Findings

### F1 — `SpriteBatch::Begin()` sets `begun = true` before backend calls that can throw, leaving the object permanently stuck in "already began" state after any such exception (surfaced by, but not specific to, this test's own scenario)

- Severity: HIGH
- Confidence: HIGH (exact failing sequence traced through the source; not merely a pattern match)
- Category: correctness / exception-safety
- Location/symbol: `Microsoft::Xna::Framework::Graphics::SpriteBatch::Begin(...)`,
  `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` lines 106-123 (`begun = true;` at line 110, followed by
  `backend_->SetCustomEffect(customEffect_)` at line 114 inside the `if (backend_)` block that can throw)
- Evidence:
  ```cpp
  customEffect_    = effect;
  transformMatrix_ = transformMatrix;
  sortMode_        = sortMode;
  spriteQueue_.clear();
  begun            = true;              // <-- set BEFORE any backend call below

  if (backend_)
  {
      backend_->SetCustomEffect(customEffect_);      // <-- THROWS here for this test's scenario
      backend_->SetTransformMatrix(transformMatrix_);
      ...
      backend_->Begin();
  }
  ```
  `begun` is a private `bool` (`include/Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp` line 56) with no public
  accessor, so the only way a caller observes the corruption is indirectly: calling `Begin()` again afterward hits
  `if (begun) throw std::runtime_error("Begin has been called before calling End.");` (`SpriteBatch.cpp` line 89) —
  even though the *previous* `Begin()` call never actually completed successfully.
- Why it matters: this test's own second check (`sb_->Begin(..., &fx)`) is precisely the scenario that triggers this.
  Concretely: a game that (1) tries to pass a custom `Effect` to `SpriteBatch::Begin()`, (2) catches the resulting
  `std::runtime_error` (exactly the "graceful, catchable failure" behavior Task 676's own header comment says is the
  whole point of throwing instead of silently misrendering), and (3) then falls back to a normal
  `sb_->Begin()`/`sb_->Draw()`/`sb_->End()` sequence — will find that fallback `Begin()` call itself throws
  `"Begin has been called before calling End."`, an unrelated and confusing second failure the caller has no way to
  anticipate from the API surface. The only way to actually recover is to call `sb_->End()` first (which happens to
  work because `spriteQueue_` was already cleared before `begun` was set, and `backend_->End()` /
  `backend_->SetCustomEffect(nullptr)` are safe even though `backend_->Begin()` was never reached) — an undocumented,
  non-obvious workaround. This defeats the specific design goal (a caller can catch the exception and fall back
  gracefully) that Task 676's own comment states as the reason silent no-op was rejected. The defect is general to
  `SpriteBatch.cpp` (shared code, not SDL_Renderer-specific) — any backend whose `SetCustomEffect`/
  `SetTransformMatrix`/`SetSamplerFilter`/`SetSamplerAddressMode`/`Begin()` throws for any reason hits the same
  stuck state.
- FNA/XNA comparison: FNA's own `SpriteBatch.Begin()` (`Graphics/SpriteBatch.cs` lines 273-305) only sets
  `beginCalled = true` and assigns fields; it calls `PrepRenderState()` (FNA's equivalent state-application step)
  only when `sortMode == SpriteSortMode.Immediate`. For every other sort mode, `Begin()` in FNA cannot throw for any
  reason other than the double-`Begin()` guard itself — this exact failure mode does not exist in FNA. This CNA port
  made `Begin()` eager for every sort mode, which is what creates the exposure.
- Related files: `include/Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp` (declares the private `begun` field, no
  accessor); every other `examples/sdlrenderer_*` test that calls `SpriteBatch::Begin()` successfully is unaffected
  since none of them trigger a throwing backend call from inside `Begin()`.
- Suggested future action (not implemented by this audit): set `begun = true` only after the backend calls in the
  `if (backend_)` block complete successfully (or wrap them so a thrown exception restores `begun = false` before
  propagating), matching the effective guarantee FNA provides by construction.

## Cross-File Observations

- This finding is a byproduct of the eager-vs-lazy `Begin()` divergence noted in "Cross-file consistency" above —
  worth flagging to whichever future audit covers `SpriteBatch.cpp` itself as a `xna-graphics`-shard file, since the
  root cause lives entirely there, not in this test.
- No other file in this shard currently exercises a throwing path from inside `SpriteBatch::Begin()` (confirmed via
  `grep -rn "Begin has been called before calling End"` across `src/`, `tests/`, `examples/` — the only occurrence is
  the throw site itself), so F1 is a previously-unexercised code path, not a duplicate of an existing finding.

## Missing or Weak Tests

- The file never attempts to reuse `sb_` (or any other `SpriteBatch`) after the induced exception in check 2 before
  calling `Exit()`. Adding a third check — e.g. `sb_->Begin(); sb_->Draw(...); sb_->End();` immediately after the
  custom-effect throw, verifying it does NOT throw `"Begin has been called before calling End."` — would have
  caught F1 directly and is the natural extension of this file's own stated purpose (proving the throw is a clean,
  recoverable failure mode, not just "throws something").

## Positive Findings

- Both of the file's own two assertions are precise, correctly derived from the real `SdlSpriteBatchBackend`
  implementation, and match the documented Task 676 design rationale exactly.
- The header comment accurately and specifically explains why `ShaderEffect(dev, "", "")` is safe to construct
  without a real backend (`CreateEffectBackend()` returning `nullptr`), which this audit independently confirmed
  is consistent with `IGraphicsBackend.hpp`'s established default-no-op convention.

## Final Assessment

The test's own two checks are correct and well-targeted, but this file's specific scenario (an intentionally
throwing `Begin()` call) is exactly the kind of edge case that exposes a real, general exception-safety bug in
`SpriteBatch::Begin()` (F1) — a caller cannot cleanly recover from the very failure mode this test proves is
reachable. The test itself is not "wrong" so much as incomplete: it stops short of the one additional assertion that
would have caught the defect it otherwise sets up perfectly.
