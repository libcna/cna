# Audit: examples/sdlrenderer_resource_leak_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_resource_leak_test.cpp` (146 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `Texture2D`/`RenderTarget2D` create/dispose leak-check
- Build/CTest registration: `cna_sdl_test(cna_test_sdl_resource_leak …)` /
  `cna_register_backend_test(NAME SDL_Renderer_ResourceLeak …)`, `cmake/Tests/SdlRendererTests.cmake:344-348`.
  Header traces to Task 719, explicitly said to mirror Task 219's EasyGL leak-check (confirmed live: `git log`
  shows `f443ee83 test(Task 719): leak-check Texture2D/RenderTarget2D create/dispose on SDL_Renderer`).
- XNA/FNA relevance: `GraphicsDevice`'s NOXNA resource-tracking extensions
  (`ResourceCreated`/`ResourceDestroyed` events, `GetTrackedResourceCount`), `IDisposable` pattern on
  `Texture2D`/`RenderTarget2D`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsResource.cpp` (constructor/
  `Dispose(bool)`, the shared registration/unregistration logic), `src/Microsoft/Xna/Framework/Graphics/
  GraphicsDevice.cpp` (`AddResourceReference`/`RemoveResourceReference`/`OnResourceCreated`/`OnResourceDestroyed`,
  lines 463-489), `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp` (`Dispose(bool)`, line 196-199),
  `include/Microsoft/Xna/Framework/Graphics/Texture2D.hpp` (`HasBackend()`, line 309).

## Purpose

Creates and disposes 80 resources (40 `Texture2D` + 40 `RenderTarget2D`) across several phases and checks that:
`ResourceCreated`/`ResourceDestroyed` fire exactly once per resource; `GraphicsDevice`'s internal tracking list
(`GetTrackedResourceCount`) grows and shrinks by exactly the expected amount; no `SDL_Texture` handle is left
dangling (`HasBackend()` is `false` for every disposed resource); a small second "repeat" batch catches any
one-time initialization artifact; and the device remains fully drawable after all the churn.

## Executive Verdict

**Healthy.** Every mechanism this test relies on was independently traced to and confirmed against the actual
`GraphicsResource`/`GraphicsDevice`/`Texture2D` code: registration happens exactly once per resource (a
`RenderTarget2D` — which IS-A `Texture2D` IS-A `Texture` IS-A `GraphicsResource` — has only one base
`GraphicsResource` subobject, so it fires exactly one `ResourceCreated`, not two), disposal is idempotent
(`isDisposed_` guard prevents double-counting on scope-exit destruction after an explicit `Dispose()`), and
`HasBackend()` genuinely reflects whether the backend `shared_ptr` was reset. No discrepancy found between what
this test asserts and what the code actually does.

## Checklist Results

### API / XNA / FNA parity
N/A for most of this file — `ResourceCreated`/`ResourceDestroyed`/`GetTrackedResourceCount`/`HasBackend()` are
all `NOXNA` extensions (confirmed: `GetTrackedResourceCount` is declared `NOXNA` in
`GraphicsDevice.hpp` line 671; `HasBackend()` is declared `NOXNA` in `Texture2D.hpp` line 309), correctly not
part of the XNA-facing surface. `System::IDisposable::Dispose()` (used via `disposeVia`, line 50) is the
project's standard IDisposable mapping, matching `Texture2D`/`RenderTarget2D`'s real
`Microsoft.Xna.Framework.Graphics.Texture2D`/`RenderTarget2D`'s IDisposable inheritance in FNA.

### Behavioral correctness
Traced the full event-firing chain: `GraphicsResource::GraphicsResource(GraphicsDevice*)` (lines 7-15) calls
`graphicsDevice_->AddResourceReference(this)` then `graphicsDevice_->OnResourceCreated(this)` — both fire
exactly once per constructed object. Since `RenderTarget2D`'s inheritance chain
(`RenderTarget2D : Texture2D : Texture : GraphicsResource`) has exactly one `GraphicsResource` base subobject
per instance, constructing one `RenderTarget2D` fires `ResourceCreated` exactly once, not once per class in the
hierarchy — confirmed this matches the test's expectation that `createCount == TOTAL` (80) after creating 40 of
each type (line 80-81), not 120 or some other multiple.
`GraphicsResource::Dispose(bool)` (lines 84-100) is guarded by `if (isDisposed_) return;` at the very top —
confirmed this makes `disposeVia(*r)` (an explicit `Dispose()` call) followed by the object's own destructor at
scope-exit (which calls `Dispose(false)` again per line 41) **safe**, since the second call is a no-op after the
flag is set — correctly avoiding a double `ResourceDestroyed` fire or a double
`RemoveResourceReference` call that could otherwise corrupt `GraphicsDevice`'s tracking vector.

### Logic
The "quick repeat" phase (lines 102-111) deliberately re-zeroes `createCount`/`destroyCount` and constructs a
*fresh* `Texture2D`/`RenderTarget2D` pair inside a nested scope, disposes them explicitly, then checks exactly 2
created/2 destroyed — a good, minimal check for "did the first 80-resource batch leave some static/
one-time-init state that would make a *second*, independent batch behave differently" (e.g. a static counter
not reset, or a cached backend object reused incorrectly).

### Memory/resource lifetime
`Texture2D::Dispose(bool disposing)` (Texture2D.cpp lines 196-199) does `backend_.reset();` unconditionally
(safe even if already null) then delegates to `Texture::Dispose(disposing)`, which eventually reaches
`GraphicsResource::Dispose(bool)`'s idempotency guard — confirmed `HasBackend()`
(`return backend_ != nullptr`) correctly reflects the post-disposal state used by the leak-check at lines 93-96.
`RenderTarget2D::Dispose(bool)` (RenderTarget2D.cpp lines 84-100) additionally guards against disposing a
render target that is still bound (throws `InvalidOperationException` if so, lines 86-93) and explicitly clears
its own cached raw `rtBackend_` pointer (line 99, with an inline comment citing the exact Task 717 UAF this
fixed) — none of the 80 resources in this test are ever bound as the active render target, so this guard is
never triggered here, correctly.

### C++ correctness
`static void disposeVia(System::IDisposable& r) { r.Dispose(); }` (line 50) is a clean, minimal helper —
correctly takes by reference (no slicing risk) and dispatches through the virtual `IDisposable::Dispose()`
interface rather than a concrete type's method, exercising the polymorphic path a real generic-resource-cleanup
caller would use.

### Performance
N=40 per type (80 total) is a reasonable, fast leak-check scale; no performance concern for a one-shot test.

### Thread safety
N/A — single-threaded `Game::Draw` callback.

### Architecture
Correctly scoped per its own header comment: explicitly *not* re-testing `VertexBuffer`/`IndexBuffer` leak
behavior (already covered by the shared, backend-agnostic Task 219 EasyGL test), only the two SDL_Renderer-
specific resource types (`Texture2D`/`RenderTarget2D`) that actually have backend-owned `SDL_Texture` handles.

### Maintainability
146 lines; the `check()` helper and `N`/`TYPES`/`TOTAL` constants (lines 33-35) keep the numeric expectations
self-documenting and in one place rather than repeated magic numbers.

### Portability
N/A — SDL_Renderer-specific, CMake-gated.

### Robustness
The event-handler lambdas (lines 62-63) capture `createCount`/`destroyCount` by reference and are correctly
cleared via `dev.ResourceCreated.Clear()`/`dev.ResourceDestroyed.Clear()` (lines 113-114) before the final
functional check — preventing a stale lambda capturing now-out-of-scope local variables from firing during the
subsequent `Clear()`/`GetBackBufferData()` calls (which, on this backend, do not themselves create/destroy
`GraphicsResource`s, but clearing the handlers first is still the correct defensive practice, matching
`System::EventHandler<T>::Clear()`'s confirmed existence in `sharp-runtime`'s `EventHandler.hpp` line 203).

### Testing
This file is a genuine, mechanism-level leak-check, not a placeholder — it verifies event-firing counts, an
internal tracking-list size delta, and the actual backend-handle-null-after-dispose condition, rather than only
"no crash after 80 creates/disposes."

### Cross-file consistency
Confirmed consistent with `GraphicsResource.cpp`'s copy-constructor/copy-assignment (lines 17-37), which reset
`isDisposed_` to `false` on copy — not exercised by this test (no `Texture2D`/`RenderTarget2D` copies are made
here, correctly, since both are non-copyable per their own deleted copy constructors), so no conflict with this
test's expectations.

## Detailed Findings

None. No CRITICAL/HIGH/MEDIUM/LOW findings in this file.

## Cross-File Observations

- Explicitly designed as the SDL_Renderer-specific complement to Task 219's EasyGL leak-check — the two tests
  share the same overall shape (construct N, check tracked count, dispose, check count returns to baseline, check
  no backend handle survives) but are scoped to different resource types per backend capability, a sensible
  division of labor rather than duplicated coverage.
- The `RenderTargetsMrtThrowsTest` file in this same batch (`sdlrenderer_rendertargets_mrt_throws_test.cpp`)
  found a real state-corruption bug (F1 in that file's report) in a *different* piece of `GraphicsDevice`'s
  bookkeeping (`currentRenderTargets_`/`renderTargetBound_`, not the `resources_` tracking list this file
  exercises) — this file's own tracking-list bookkeeping (`AddResourceReference`/`RemoveResourceReference`) was
  independently checked in this pass and found to have no equivalent issue: both are called unconditionally
  inside the *already-idempotent* `GraphicsResource` constructor/`Dispose(bool)` path, not split across a
  can-fail backend call the way `SetRenderTargets`' bookkeeping is.

## Missing or Weak Tests

None identified as missing for this file's stated scope. A theoretical addition (not required) would be a
leak-check that disposes resources in a different order than creation (e.g. reverse or interleaved), to
stress-test `RemoveResourceReference`'s linear-scan-and-swap-with-back() removal (`GraphicsDevice.cpp` lines
482-489) more thoroughly than this test's current create-all-then-dispose-all-in-order pattern does — low
priority since the removal algorithm itself is order-independent by construction (a linear search, not an
index-based assumption).

## Positive Findings

- Every mechanism this test depends on (event firing count, tracking-list delta, backend-handle nullness,
  disposal idempotency) was independently traced to and confirmed against the real implementation, with no
  discrepancy found.
- The "quick repeat" second batch is a genuinely useful check for one-time-initialization artifacts that a
  single large batch could miss.
- Correctly scoped to avoid duplicating the existing, backend-agnostic `VertexBuffer`/`IndexBuffer` leak
  coverage from Task 219.

## Final Assessment

A thorough, accurate leak-check test. No defects found in the test or in the resource-tracking/disposal code
paths it exercises.
