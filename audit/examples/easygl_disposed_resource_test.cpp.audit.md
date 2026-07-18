# Audit: examples/easygl_disposed_resource_test.cpp

## Metadata

- Source file: `examples/easygl_disposed_resource_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration test
- File type: C++ example/integration-test executable (`DisposedResourceTest : Game`, `main()`)
- Related production code: `TextureCollection::operator()` (`TextureCollection.cpp`), `Effect::Apply()`
  (`Effect.cpp`, inherited by `BasicEffect`), `GraphicsDevice::SetRenderTarget(RenderTarget2D*)`
  (`GraphicsDevice.cpp`)
- XNA/FNA relevance: exercises `GraphicsDevice.Textures[i]`, `Effect.Apply()` (via `BasicEffect`), and
  `GraphicsDevice.SetRenderTarget()` — all core XNA 4.0 surface, all backed by the `GraphicsResource`
  `IDisposable`/`ObjectDisposedException` contract.
- FNA reference: FNA's `TextureCollection.this[int]` setter, `Effect.Apply()`, and
  `GraphicsDevice.SetRenderTarget(RenderTarget2D)` all call `AssertNotDisposed()` on the incoming resource before
  accepting it — the same three-part contract this file targets.
- Main related tests: this file (Task 210); its own header comment (lines 9-10) explicitly notes VertexBuffer/
  IndexBuffer disposed-guards are deferred to a separate task (Task 212) — delivered as
  `easygl_disposed_buffer_test.cpp`, audited separately in this same batch.
- Registered as `cna_test_easygl_disposed_resource` / `EasyGL_DisposedResource` (`EasyGLTests.cmake:956-959`,
  TIMEOUT 30s).

## Purpose

Verifies three independent `GraphicsResource`-disposal guards that live outside the buffer classes: assigning a
disposed `Texture2D` into `GraphicsDevice.Textures[0]`, calling `Apply()` on a disposed `BasicEffect`, and passing a
disposed `RenderTarget2D` to `GraphicsDevice.SetRenderTarget()` must each throw
`System::ObjectDisposedException` rather than silently accepting a resource whose backend state has already been
torn down.

## Executive Verdict

**Healthy** — all three checks were independently traced to and confirmed against the real guard code in
`TextureCollection.cpp`, `Effect.cpp`, and `GraphicsDevice.cpp`; each matches exactly, including the specific
exception-message argument used (`getNameProperty()` in all three cases, unlike the inconsistency noted for buffer
types in the sibling `easygl_disposed_buffer_test.cpp` report).

## Checklist Results

### API / XNA / FNA parity
`GraphicsDevice.Textures` (here exercised via the CNA property-accessor convention
`getTexturesProperty()(0, &tex)`, i.e. `TextureCollection::operator()`, the C++ stand-in for C#'s indexer setter
`Textures[0] = tex`), `Effect.Apply()`/`BasicEffect` inheriting it, and `GraphicsDevice.SetRenderTarget(RenderTarget2D)`
are all real, correctly-named XNA 4.0 members. The `RenderTarget2D` constructor call (lines 77-81) passes all six
FNA-matching parameters (`width, height, mipMap, format, depthFormat, multiSampleCount, usage`) in FNA's own
declared order.

### Behavioral correctness
Each of the three checks was verified line-by-line against production code, not merely inferred from naming:
- **Texture2D → `Textures[0]`** (lines 60-65): `TextureCollection::operator()(int index, Texture* texture)`
  (`TextureCollection.cpp:25-36`) does `if (index<0||index>=MaxTextures) throw std::out_of_range(...)` first, then
  `if (texture != nullptr && texture->getIsDisposedProperty()) throw
  System::ObjectDisposedException(texture->getNameProperty());` — for index 0 and a disposed, non-null texture this
  is exactly the `ObjectDisposedException` path the test expects, matching `throwsODE`'s catch clause.
- **BasicEffect::Apply()** (lines 68-73): `BasicEffect` does not override `Apply()`; the call resolves to the base
  `Effect::Apply()` (`Effect.cpp:53-59`): `if (isDisposed_) throw System::ObjectDisposedException(getNameProperty());`
  before ever calling `OnApply()` — confirmed this guard runs first, so no partially-applied-effect side effect
  occurs on a disposed instance.
- **RenderTarget2D → `SetRenderTarget()`** (lines 76-85): `GraphicsDevice::SetRenderTarget(RenderTarget2D*)`
  (`GraphicsDevice.cpp:1821-1824`) opens with `if (renderTarget && renderTarget->getIsDisposedProperty()) throw
  System::ObjectDisposedException(renderTarget->getNameProperty());` before any backend interaction (`backend_->
  SetRenderTarget2D(...)`) — confirmed the guard precedes any GPU-state mutation, so a disposed render target
  cannot be partially bound.

All three checks are accurate, evidence-backed reproductions of real guarded behavior.

### Logic
Single `Draw()` override (not `Initialize()`, unlike the sibling disposed-buffer test) guarded by a `done_` flag so
the three checks run exactly once on the first frame, then `Exit()`. Running the checks from `Draw()` rather than
`Initialize()` is deliberate and necessary here specifically for the `RenderTarget2D` check: `GraphicsDevice` needs
at least one completed `Initialize`/backend-setup pass before render-target binding is meaningful, though the file
itself does not comment on why `Draw()` was chosen over `Initialize()` (a difference from the sibling
`easygl_disposed_buffer_test.cpp`, which does use `Initialize()` for conceptually similar checks that don't need a
render target).

### Memory/resource lifetime
`tex`, `fx`, and `rt` are each constructed, disposed, and probed within their own `{ }` block, matching the same
tight-scoping discipline as the sibling buffer-disposal test. No overlap or reuse beyond the deliberate post-dispose
probe.

### C++ correctness
`static_cast<System::IDisposable&>(fx).Dispose()` (line 70) is a slightly unusual way to invoke `Dispose()` compared
to just calling `fx.Dispose()` directly — presumably written this way to make the call visibly go through the
`IDisposable` interface contract rather than any `BasicEffect`-specific overload, though `BasicEffect`/`Effect` does
not appear to declare a conflicting non-virtual `Dispose()` that would make the cast necessary (the two forms should
be equivalent here). Harmless, just a slightly indirect way to express the call — not a defect.

### Robustness
The `out_of_range` guard in `TextureCollection::operator()` is exercised only implicitly (index 0 is always valid) —
this file does not test the index-bounds branch, which is reasonable since that is a distinct concern
(index-validation) from the disposed-resource concern this file is scoped to.

### Testing
As with the sibling buffer-disposal test, only exception *type* is asserted, never message content. Unlike that
sibling file, all three guards checked here consistently use `getNameProperty()` for the exception argument, so
there is no cross-file inconsistency to flag here (contrast with Finding F1 in the
`easygl_disposed_buffer_test.cpp` report).

## Detailed Findings

No HIGH or CRITICAL findings. No MEDIUM findings — all three assertions are accurate and the test construction is
sound. One LOW/INFO observation:

### F1 — `RenderTarget2D` disposed-guard check never exercises the "still bound" branch

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `GraphicsDevice::SetRenderTarget(RenderTarget2D*)` (`GraphicsDevice.cpp:1821-1832`); test lines
  76-85.
- Evidence: `SetRenderTarget2D`'s own disposed-guard is checked unconditionally, before the render-target's
  bound/unbound state is examined at all — so this test's disposed-`rt` case never touches the separate
  `RenderTarget2D::Dispose(bool)` guard that throws `System::InvalidOperationException("Disposing target that is
  still bound")` when disposing a target that is the device's *current* render target
  (`RenderTarget2D.cpp:84-93`, confirmed while cross-checking the sibling `easygl_double_dispose_test.cpp`).
- Why it matters: purely a coverage gap, not a correctness defect in this file — the disposed-target-passed-to-
  `SetRenderTarget` path and the disposing-a-currently-bound-target path are two different production guards, and
  this file (correctly, per its own scope) only tests the first.
- Suggested future action (not implemented by this audit): if a future task wants full render-target lifecycle
  coverage, add a case that binds `rt` via `SetRenderTarget(&rt)` and then calls `rt.Dispose()` while still bound,
  asserting `System::InvalidOperationException` — distinct from anything already covered here or in the
  double-dispose file.

## Cross-File Observations

- This file's own header comment (lines 9-10) accurately documents a real, still-relevant scope boundary ("VB and
  IB are not yet derived from GraphicsResource... disposal guards will be added in that task") — verified this is
  no longer strictly true in the sense that VertexBuffer/IndexBuffer disposal *is* now covered, just by a separate
  file (`easygl_disposed_buffer_test.cpp`, Task 240) rather than folded into this one. The comment is dated/no
  longer 100% precise (VB/IB guards do exist and are tested elsewhere) but not misleading about this file's own
  actual scope.
- Confirms all three disposed-guard call sites checked here run their guard *before* any backend/GPU-state mutation
  — a consistent, correct defensive pattern across `TextureCollection`, `Effect`, and `GraphicsDevice`.

## Missing or Weak Tests

- See F1 (still-bound render target disposal path not covered here — likely belongs in a render-target-lifecycle
  specific file rather than this one).
- No negative/index-out-of-range case for `Textures[i]` in this file (reasonable — out of this file's stated scope).

## Positive Findings

- All three guard call sites checked here run the disposed-check strictly before any GPU/backend side effect,
  preventing partial-mutation-then-throw bugs — verified by reading each function's full body, not just its first
  line.
- Unlike the sibling buffer-disposal test, the exception-message argument convention (`getNameProperty()`) is
  applied consistently across all three guards checked in this file.

## Final Assessment

An accurate, narrowly-scoped disposed-resource regression test whose three checks were each traced to and confirmed
against real guard code with no discrepancies. Its own header comment is slightly stale about the current state of
VB/IB coverage (now handled by a sibling file, not a gap), and one adjacent production guard (still-bound
render-target disposal) is untested here but is a distinct concern from what this file claims to cover.
