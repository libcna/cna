# Audit: examples/sdlrenderer_rendertargets_mrt_throws_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_rendertargets_mrt_throws_test.cpp` (141 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — MRT (`SetRenderTargets` with 2+ bindings) rejection test
- Build/CTest registration: `cna_sdl_test(cna_test_sdl_rendertargets_mrt_throws …)` /
  `cna_register_backend_test(NAME SDL_Renderer_RenderTargets_MrtThrows …)`,
  `cmake/Tests/SdlRendererTests.cmake:283-287`. Header traces to Task 709 (confirmed live: `git log` shows
  `d9a772f3 fix(Task 709): SetRenderTargets silently ignored MRT bindings on SDL_Renderer`).
- XNA/FNA relevance: `GraphicsDevice.SetRenderTargets(RenderTargetBinding[])` (`Microsoft::Xna::Framework::
  Graphics`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`SetRenderTargets(const std::vector<RenderTargetBinding>&)`, lines 1881-1937),
  `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp` (`SetRenderTargets`, lines 760-767).

## Purpose

Verifies that `GraphicsDevice::SetRenderTargets` with 2+ bindings throws a clear `std::runtime_error` on
SDL_Renderer (which supports exactly one active render target), rather than the shared
`IGraphicsBackend::SetRenderTargets` default of silently binding only the first target and dropping the rest —
a real, previously-shipped bug per the header comment (confirmed: `SdlGraphicsBackend` did not override
`SetRenderTargets` before Task 709). The test also asserts the device "remains fully usable" after catching the
throw, by performing an ordinary backbuffer draw and pixel readback afterward.

## Executive Verdict

**Needs attention.** The MRT-rejection behavior itself is correctly implemented and correctly tested (checks 1).
However, this audit traced a genuine, currently-live exception-safety bug in the *shared* (not
SDL_Renderer-specific) `GraphicsDevice::SetRenderTargets` that this test's own recovery step inadvertently masks:
`currentRenderTargets_`/`renderTargetBound_` are mutated to reflect the (rejected) 2-target binding **before**
the backend call that throws, so after the exception, `GraphicsDevice`'s own bookkeeping falsely believes 2
render targets are bound even though the SDL renderer's real target was never touched. This test's second check
("device remains fully usable") only passes because the test itself calls `dev.SetRenderTarget(nullptr)`
immediately after catching the exception (line 109) — a corrective step a real caller has no way of knowing is
required, and one the commit's own message ("proving the throw doesn't leave the device in a corrupted
half-bound state") claims isn't necessary. See F1.

## Checklist Results

### API / XNA / FNA parity
`RenderTargetBinding(&rtA)`/`RenderTargetBinding(&rtB)` (line 92) and `GraphicsDevice::SetRenderTargets(const
std::vector<RenderTargetBinding>&)` (line 98) match FNA's `SetRenderTargets(params RenderTargetBinding[])`
shape (modulo the `params[]` → `std::vector` convention already established project-wide). FNA itself supports
true MRT on capable hardware; this test correctly verifies the *rejection* path specific to this 2D-only
backend, not a general XNA behavior.

### Behavioral correctness
Confirmed the actual throw source: `GraphicsDevice::SetRenderTargets` (lines 1881-1937) passes the size check
(`renderTargets.size() > MAX_RENDERTARGET_BINDINGS` where `MAX_RENDERTARGET_BINDINGS=4`, line 1883-1885 — 2 does
not exceed this), the disposed-resource guard loop (lines 1908-1914 — neither `rtA` nor `rtB` is disposed), then
reaches `backend_->SetRenderTargets(backends.data(), static_cast<int>(backends.size()))` (line 1936), which
dispatches to `SdlGraphicsBackend::SetRenderTargets(IRenderTargetBackend* const*, int)`
(`SdlGraphicsBackend.cpp` lines 760-767): `if (count > 1) throw std::runtime_error(...)` — confirmed, this is
exactly the throw the test expects (line 105), and the message content matches the backend's own descriptive
text. Check 1 is genuine and correctly wired end-to-end.

### Logic
See F1 for the state-corruption issue in the code path between the disposed-check loop and the backend call.

### Memory/resource lifetime
`rtA`/`rtB` (line 90-91) are stack locals; `bindings` (line 92) is a local vector of `RenderTargetBinding` value
objects, each holding a pointer into `rtA`/`rtB` — no ownership issue, both remain alive through the whole
`Draw()` body.

### C++ correctness
No unsafe casts. `catch (const std::exception& e)` (line 100) correctly catches `std::runtime_error` (a base
`std::exception` derivative), consistent with every other exception-catching test in this shard.

### Performance
N/A — single-frame test.

### Thread safety
N/A.

### Architecture
See F1 — the underlying issue is an exception-safety violation in shared `GraphicsDevice` code (state mutated
before an operation that can fail), not a backend-specific defect; it happens to be *observable* only through
this SDL_Renderer-specific throw path today, but the code shape means it could equally surface on any other
backend under a different failure condition inside `backend_->SetRenderTargets(...)` (e.g. a hardware
render-target-count limit on a different backend).

### Maintainability
141 lines; proportionate; the header comment's own "process finding" paragraph (about a stale known-failure
baseline count) is a useful, honest piece of process history, independently corroborated by `git log` (commit
`d9a772f3`'s message narrates the identical baseline-correction story).

### Portability
N/A — SDL_Renderer-specific, CMake-gated.

### Robustness
See F1 — the test's "device remains fully usable" claim is only true because the test performs its own recovery
call; the framework itself does not guarantee this.

### Testing
The MRT-rejection behavior (check 1) is correctly, genuinely tested. The "device remains usable" claim (check
2) does **not** actually test the framework's own exception-safety guarantee, because the test's own explicit
`dev.SetRenderTarget(nullptr)` call (line 109) — inserted between the catch block and the "usable" assertions —
resets the exact state (`currentRenderTargets_`, `renderTargetBound_`) that F1 shows gets corrupted by the throw.
Removing that one line would very likely change check 2's own outcome (see F1's traced consequence via
`GraphicsDevice::Present()`).

### Cross-file consistency
The commit introducing this file (`d9a772f3`) explicitly states: *"GraphicsDevice remains fully usable afterward
(a normal backbuffer draw succeeds), proving the throw doesn't leave the device in a corrupted half-bound
state."* This audit's trace of the actual code shows this claim is not fully accurate as stated — the device
*is* left with corrupted bookkeeping (though not a corrupted *rendering* state, since the backend's real bound
target never actually changed) — and the test only demonstrates "usable" because of its own extra recovery
call, not because `SetRenderTargets` itself preserves a consistent state on failure.

## Detailed Findings

### F1 — `GraphicsDevice::SetRenderTargets` mutates `currentRenderTargets_`/`renderTargetBound_` to reflect the *requested* (not actually applied) bindings before calling into the backend, so a backend-thrown rejection (e.g. this test's MRT case) leaves the device's own bookkeeping falsely believing N targets are bound; a caller that does not immediately call `SetRenderTarget(nullptr)` (unlike this test) would then get an unrelated, confusing `InvalidOperationException` from the next `Present()` call

- Severity: HIGH
- Confidence: HIGH (fully traced: exact line numbers, exact call sequence, exact consequence via `Present()`'s
  own guard, and the test's own masking action identified precisely)
- Category: correctness / exception-safety / architecture
- Location/symbol: `GraphicsDevice::SetRenderTargets(const std::vector<RenderTargetBinding>&)`
  (`GraphicsDevice.cpp` lines 1881-1937, specifically lines 1917-1918 vs. line 1936);
  `GraphicsDevice::Present()` (lines 372-380)
- Evidence: the function body executes, in order:
  ```
  currentRenderTargets_ = renderTargets;       // line 1917 -- MUTATES state to the 2-target request
  renderTargetBound_ = !renderTargets.empty(); // line 1918 -- sets true
  ...
  backend_->SetRenderTargets(backends.data(), static_cast<int>(backends.size())); // line 1936 -- THROWS here
  auto* first = ...; ResetViewportAndScissorForRenderTarget(...); // lines 1938-1940 -- never reached
  ```
  `SdlGraphicsBackend::SetRenderTargets` (line 761-763) throws **before** calling `SetRenderTarget2D` internally,
  so the *actual* SDL render target is never touched (it remains whatever it was — in this test, the default
  backbuffer, since `SetRenderTarget2D` was never invoked). But by the time the exception propagates out of
  `GraphicsDevice::SetRenderTargets`, `currentRenderTargets_` already holds the two (never-actually-bound)
  `RenderTargetBinding`s, and `renderTargetBound_` is `true`. `GraphicsDevice::Present()` (line 374) checks
  exactly this flag: `if (renderTargetBound_) throw System::InvalidOperationException("Cannot present while
  render targets are bound");`. `Game::EndDraw()` (`Game.cpp` lines 492-501) calls
  `getGraphicsDeviceProperty().Present()` immediately after `Draw()` returns in the normal game loop. This
  test's own `Draw()` (line 109) calls `dev.SetRenderTarget(nullptr);` right after the `catch` block —
  `SetRenderTarget(RenderTarget2D*)`'s implementation unconditionally does `currentRenderTargets_.clear();` and
  `renderTargetBound_ = (renderTarget != nullptr)` (i.e. `false` for `nullptr`), which is exactly what repairs
  the corruption this finding describes — **but that repair is the test calling a method it has no
  documented obligation to call**, not a guarantee `SetRenderTargets` itself provides.
- Why it matters: a caller that catches the "SDL_Renderer doesn't support MRT" exception (a very plausible
  pattern — e.g. cross-backend code that tries binding N targets and falls back to single-target rendering on
  failure) and does **not** happen to also call `SetRenderTarget(nullptr)` before its next `Present()` would get
  an unrelated `InvalidOperationException("Cannot present while render targets are bound")` — a confusing,
  misleading error with no render targets actually bound on the real backend, purely because of leftover,
  never-rolled-back bookkeeping from the earlier failed call. This directly contradicts the commit message's own
  claim that this test "prov[es] the throw doesn't leave the device in a corrupted half-bound state" — the
  *backend* state is fine (nothing was ever rebound), but `GraphicsDevice`'s own internal state is not, and this
  test's structure (recovery call placed before the "usable" assertions) does not actually exercise or prove
  that distinction.
- FNA/XNA comparison: N/A directly — this is a CNA-internal state-management bug in a codepath (multi-target
  binding rejection) that has no real FNA equivalent on desktop-class hardware (FNA's own backends generally do
  support the requested target count or throw for a difference reason). The general principle this violates —
  don't mutate observable state before an operation that can still fail — is a standard C++/exception-safety
  expectation, not an XNA-specific one.
- Related files: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (`SetRenderTarget(RenderTarget2D*)`,
  lines 1821-1859, for comparison — that singular overload's own early disposed-check happens before backend
  dispatch too, but its backend call, `SetRenderTarget2D`, is not currently known to throw on any backend for a
  count-related reason, so it doesn't currently exhibit the same observable bug); `src/Microsoft/Xna/Framework/
  Game.cpp` (`EndDraw()`, lines 492-501, confirming `Present()` runs immediately after `Draw()` in the normal
  loop).
- Suggested future action (not implemented by this audit): move the `currentRenderTargets_ = renderTargets;`/
  `renderTargetBound_ = ...` assignment (and the empty-vector early-return path) to **after**
  `backend_->SetRenderTargets(...)` succeeds, so a thrown rejection leaves `GraphicsDevice`'s bookkeeping
  unchanged (matching whatever the backend's real, unmodified state actually is); alternatively, wrap the
  backend call in a try/catch that restores the pre-call `currentRenderTargets_`/`renderTargetBound_` values
  before rethrowing. Either fix should be paired with a *new* assertion in this test (or a new test) that checks
  `dev.GetRenderTargets().empty()` (or an equivalent bound-state query) is unaffected immediately after catching
  the exception, **without** first calling `SetRenderTarget(nullptr)` — the current test cannot detect a
  regression or a fix in this area because its own recovery call papers over the exact state this finding is
  about.

## Cross-File Observations

- The commit that introduced this file (`d9a772f3`) also fixed a *different*, real bug in the same task
  (`GraphicsDeviceValidationTest.SetRenderTargets_FourTargets_DoesNotThrow` needing a `CNA_BACKEND_SDL_RENDERER`
  conditional) — that fix is unrelated to F1 and was not re-verified in this pass, but is worth noting as
  evidence this was an actively-maintained, carefully-tested area of code at the time, making F1's survival
  through that same review more notable (the state-mutation-before-possible-throw pattern is a subtle enough
  shape that a manual test-writer naturally reaches for "call `SetRenderTarget(nullptr)` to clean up" without it
  registering as "this is the fix for a framework bug," rather than "this is the test's own housekeeping").

## Missing or Weak Tests

See F1's suggested future action: this file should gain (or be extended with) a check that queries
`GraphicsDevice`'s post-exception render-target-bound state **before** any corrective `SetRenderTarget(nullptr)`
call, to actually prove (or disprove) the "no corrupted half-bound state" claim its own header comment and
originating commit message make.

## Positive Findings

- The core MRT-rejection behavior (check 1) is correctly implemented in the backend and correctly, genuinely
  tested by this file.
- The header comment's "process finding" about a stale known-failure baseline count is detailed, plausible, and
  independently corroborated by the corresponding commit message in `git log`.
- The test's *intent* (prove the device stays usable after the throw) is the right thing to want to test — the
  gap found here is in what the test actually manages to prove given its own structure, not in its goal.

## Final Assessment

The MRT-throw behavior itself is solid. This audit found a genuine, previously-unflagged exception-safety defect
in the shared `GraphicsDevice::SetRenderTargets` code (state mutated before a call that can fail), which this
specific test's own recovery step inadvertently conceals rather than exercises — meaning the test's "device
remains fully usable" claim, and the originating commit's identical claim, are not actually proven by the
test as written.
