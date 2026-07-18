# Audit: examples/sdlrenderer_drawuserprimitives_throws_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_drawuserprimitives_throws_test.cpp` (155 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 721, exact exception type+message for all 5
  `DrawUserPrimitives` overloads
- File type: standalone `Game`-subclass executable (`SdlDrawUserPrimitivesThrowsTest`), pass/fail counter, exit-code
- XNA/FNA relevance: `GraphicsDevice.DrawUserPrimitives<T>` typed overloads (VertexPositionColor,
  VertexPositionColorTexture, VertexPositionTexture, VertexPositionNormalTexture) and the raw-pointer +
  `VertexDeclaration` overload.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`DrawUserPrimitives` overloads, lines 868-994), `src/Microsoft/Xna/Framework/Graphics/Effect.cpp`
  (`Apply()`, lines 53-59), `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`CreateVertexBuffer`, lines 795-799).
- Git corroboration: `b40484bf`/`a14d07af` `test(Task 721): verify DrawUserPrimitives overloads throw exact
  exception on SDL_Renderer`.

## Purpose

Verifies all 5 `DrawUserPrimitives` overloads (4 typed vertex types + 1 raw-pointer/`VertexDeclaration` overload)
throw the correct exception TYPE and MESSAGE in both of their two reachable failure states on SDL_Renderer: (1) no
`Effect` applied yet → shared "no effect has been applied" error (a backend-agnostic check), and (2) with a real
`Effect` applied → `backend_->CreateVertexBuffer(n)` throws SDL_Renderer's own 3D-unsupported message, since
`DrawUserPrimitives` (unlike `DrawPrimitives`) builds its own transient vertex buffer from caller data on every call
rather than requiring a pre-existing bound one.

## Executive Verdict

**Healthy** — all 10 distinct throw assertions (5 overloads × 2 states) were independently confirmed against the
real `GraphicsDevice.cpp` source, and the claim that `BasicEffect::Apply()` itself does not throw on SDL_Renderer was
traced through `Effect::Apply()`'s actual implementation, not just assumed.

## Checklist Results

### API / XNA / FNA parity
`GraphicsDevice.DrawUserPrimitives<T>(PrimitiveType, T[], int, int)` matches FNA's own generic overload family
(`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/GraphicsDevice.cs`); this C++ port necessarily expands the
single C# generic method into 4 explicitly-typed overloads plus 1 raw+`VertexDeclaration` overload (matching FNA's
own second, non-generic `DrawUserPrimitives(PrimitiveType, IVertexType[], int, int)`-shaped overload), which is the
established, correct C++ mapping strategy for this codebase (no unnecessary template complexity introduced into the
XNA-facing surface).

### Behavioral correctness
Directly confirmed against `GraphicsDevice.cpp`:
- All 5 overloads (lines 869-994) begin with
  `if (!currentEffect_) throw std::runtime_error("GraphicsDevice::DrawUserPrimitives: no effect has been applied.");`
  — an exact match to the test's `kNoEffect` constant, confirmed identical across all 5 (`VertexPositionColor` line
  874, `VertexPositionTexture` line 900, `VertexPositionColorTexture` line 925, `VertexPositionNormalTexture` line
  952, raw+`VertexDeclaration` line 980).
- After `currentEffect_` is set (via `BasicEffect::Apply()` → `Effect::Apply()`'s `device_->SetCurrentEffect(this)`,
  confirmed at `Effect.cpp` lines 53-59), all 5 overloads proceed to pack vertex data into a scratch buffer, then
  call `backend_->CreateVertexBuffer(n)` (confirmed at `GraphicsDevice.cpp` lines 885, 910, 937, 963, 987 — one call
  site per overload, all reached before any `DrawPrimitivesEx` call) — which on SDL_Renderer unconditionally throws
  `"SDL_Renderer does not support 3D: CreateVertexBuffer"` (`SdlGraphicsBackend.cpp` lines 795-799). This exactly
  matches the test's `kNo3D` constant and its claim that "each overload proceeds to `backend_->CreateVertexBuffer(n)`,
  which DOES throw."
- `effect.Apply()` not throwing on SDL_Renderer: confirmed at `Effect::Apply()` (`Effect.cpp` lines 53-59) — the only
  possible throw is `if (isDisposed_) throw System::ObjectDisposedException(...)`, which cannot fire for a
  freshly-constructed `BasicEffect`; `OnApply()` (the effect's own virtual hook) and
  `device_->SetCurrentEffect(this)` are both backend-agnostic (no backend call occurs inside `Effect::Apply()`
  itself), so the test's assertion `!effectApplyThrew` is correctly expected to hold on every backend, not just
  SDL_Renderer.

### Logic
The static test-data arrays (`vpc`, `vpct`, `vpt`, `vpnt`) are declared `static const` at file scope inside `Draw()`
— a minor style note (function-local `static` re-initializes only once across repeated `Draw()` calls, but this file
only runs `Draw()` once via its own `done_` guard, so no repeated-construction concern applies here).

### C++ correctness
`VertexPositionTexture vpt[3]{};`/`VertexPositionNormalTexture vpnt[3]{};` value-initialize all fields to zero —
acceptable since these overloads only need to reach `CreateVertexBuffer` (which throws before ever touching the
actual vertex contents), so the all-zero data is never meaningfully consumed; this is a correct minimal-effort test
data choice, not an oversight.

### Robustness
The closing check (`Clear`+`GetBackBufferData`) proves the device survived 10 consecutive throws (5 overloads × 2
states) without corrupted state — meaningful given each of the 5 "effect applied" checks allocates and then
abandons a scratch buffer (`AcquireUserVertexScratch`) before the throw; confirmed this scratch buffer is a
persistent member (`userVertexScratch_`, reused/grown via `.resize()`, never leaked per-call) so repeated throws
don't leak or corrupt device-level state.

### Testing
Comprehensive — 5 overloads × 2 reachable states = 10 checks, matching the file's own stated scope exactly. No
overload is skipped.

## Detailed Findings

No HIGH or CRITICAL findings. Shares the shard-wide LOW-severity "no upper-bound check on the unlit R channel"
pattern already documented once in `sdlrenderer_double_dispose_test.cpp`'s report (line 131:
`pixel.getGProperty() >= 240 && pixel.getBProperty() >= 240` for a cyan clear, no R upper bound) — not re-detailed
here.

## Cross-File Observations

- Direct sibling of `sdlrenderer_drawuserindexedprimitives_throws_test.cpp` (audited in this same batch) — that
  file follows an identical two-state (no-effect / effect-applied-then-CreateVertexBuffer-throws) structure, just
  extended to 10 overloads (4 types × 2 index widths + 1 raw × 2 index widths) instead of this file's 5. Both
  correctly identify `backend_->CreateVertexBuffer` as firing before any index-buffer-specific code path.
- Complements `sdlrenderer_drawprimitives_throws_test.cpp` (also audited in this batch): that file covers the
  bound-vertex-buffer draw entry points (`DrawPrimitives`/`DrawIndexedPrimitives`/`DrawInstancedPrimitives`, which
  can never have a buffer bound on this backend at all); this file covers the user-primitive entry points (which
  build their own transient buffer per call, reaching a different, later throw point). Together the three files in
  this batch give complete coverage of this backend's 3D-draw-entry-point-throws contract.

## Missing or Weak Tests

None beyond the shard-wide minor pattern already noted elsewhere.

## Positive Findings

- All 10 throw assertions (5 overloads × 2 states) were independently traced to the exact corresponding source lines
  in `GraphicsDevice.cpp`, confirming both the exact message text and the claimed call-order (effect check before
  vertex-buffer creation).
- Correctly and non-trivially confirms `Effect::Apply()`/`BasicEffect::Apply()` doesn't throw on this backend by
  tracing the actual (backend-agnostic) implementation, not just observing the test pass.

## Final Assessment

A precise, fully-verified test covering all 5 `DrawUserPrimitives` overloads across both of their reachable failure
states with exact-message assertions. No discrepancies found between the test's claims and the actual production
code.
