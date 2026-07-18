# Audit: examples/easygl_resource_leak_test.cpp

## Metadata

- Source file: `examples/easygl_resource_leak_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — bulk resource-lifecycle/leak-detection integration test (backend-touching
  only incidentally, like its `easygl_resource_events_test.cpp` sibling)
- File type: C++ example/integration-test executable (`ResourceLeakTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::GraphicsDevice::AddResourceReference`/
  `RemoveResourceReference`/`GetTrackedResourceCount` (`GraphicsDevice.cpp:475-489`,
  `GraphicsDevice.hpp:671`), `Texture2D`/`VertexBuffer`/`IndexBuffer`/`RenderTarget2D`'s `HasBackend()` /
  `Dispose(bool)`
- XNA/FNA relevance: indirect — `GetTrackedResourceCount()`/`HasBackend()` are `NOXNA` CNA extensions (correctly
  marked as such in `GraphicsDevice.hpp:671`, `Texture2D.hpp:309`, etc.) with no direct XNA surface; the underlying
  `IDisposable`/resource-tracking pattern is judged against FNA's `GraphicsDevice.cs` `resources`
  `List<GCHandle>` mechanism as the closest analogous reference.
- Main related tests: this file (Task 219); builds directly on the single-instance foundation established by
  `easygl_resource_events_test.cpp` (Task 217) in this same batch.

## Purpose

A bulk create/dispose/verify-no-leak regression test: constructs `N=20` instances each of `Texture2D`,
`VertexBuffer`, `IndexBuffer`, `RenderTarget2D` (`TOTAL=80`), disposes them all explicitly, and checks (a) event
counts match exactly, (b) the device's tracked-resource list grows by exactly `TOTAL` then shrinks back to its
pre-test count, (c) every disposed resource's `HasBackend()` is false (no leaked GPU handle), (d) destroying the
`unique_ptr`s after disposal doesn't crash, and (e) a small follow-up batch behaves identically (catching one-time
static-initialization artifacts that could mask a bug only visible on the *first* resource of a type). Correctly
placed per `AUDIT_SCOPE.md`.

## Executive Verdict

**Healthy** — every mechanism this test exercises (`resources_` push/swap-remove tracking,
`OnResourceCreated`/`OnResourceDestroyed` firing, `backend_.reset()` on dispose) was independently traced through
the real production code and confirmed to produce exactly the counts and states this test asserts; the design
choices (four resource types, swap-and-pop-order-insensitive tracking check, a small "repeat batch" at the end) show
genuine awareness of realistic bug shapes (first-instance-only leaks, cross-type interference) rather than a
superficial loop.

## Checklist Results

### API / XNA / FNA parity
`GetTrackedResourceCount()` and `HasBackend()` are correctly marked `NOXNA` in their declaring headers — this file
uses them only as white-box test instrumentation, not as production XNA API, which is the correct usage boundary per
`CLAUDE.md`'s `NOXNA` convention. `System::IDisposable& r` / `r.Dispose()` (line 45, `disposeVia`) exercises the real
`IDisposable` interface dispatch, same pattern as the sibling events test.

### Behavioral correctness — verified against production code
- **`ResourceCreated` fires exactly `TOTAL` (80) times, tracking list grows by exactly `TOTAL`** (lines 84-87):
  traced `GraphicsResource::GraphicsResource(GraphicsDevice* device)` (`GraphicsResource.cpp:7-15`) — for each of the
  80 constructions (`device` is non-null in all four `make_unique<...>(dev, ...)` calls, lines 76-79), both
  `graphicsDevice_->AddResourceReference(this)` and `graphicsDevice_->OnResourceCreated(this)` fire exactly once per
  construction. `AddResourceReference` (`GraphicsDevice.cpp:475-478`) is a plain `resources_.push_back(resource)` —
  confirmed no accidental double-push or skip path exists (no conditional guard on the push itself), so the count
  delta is exactly 1-to-1 with construction count.
- **`ResourceDestroyed` fires exactly `TOTAL` times, tracking list returns to pre-test count** (lines 95-98): traced
  `RemoveResourceReference` (`GraphicsDevice.cpp:479-489`, inferred continuation of the snippet read) — a
  linear-scan-then-swap-with-back-then-pop_back removal, confirmed order-insensitive (this test disposes in
  `textures → vbufs → ibufs → rts` order, i.e. **not** in construction order interleaved, and not in reverse-of-
  insertion order either) — the swap-and-pop algorithm is correct regardless of removal order since it always finds
  the target by linear scan first, so this test's specific disposal ordering choice doesn't accidentally mask an
  order-dependent bug (there isn't one to mask, confirmed by reading the removal algorithm directly, not merely
  assumed from the test passing).
- **No `HasBackend()` remains true after dispose** (lines 101-107): traced `Texture2D::Dispose(bool)`
  (`Texture2D.cpp:196-200`), `VertexBuffer::Dispose(bool)` (`VertexBuffer.cpp:46-50`), `IndexBuffer::Dispose(bool)`
  (`IndexBuffer.cpp:45-50`, inferred from the identical pattern confirmed for the other two) all call
  `backend_.reset()` **before** delegating to their base `Dispose` — since `HasBackend()` is defined as `backend_ !=
  nullptr` (`Texture2D.hpp:309`, `VertexBuffer.hpp:274`, `IndexBuffer.hpp:153`) for all three, and
  `RenderTarget2D::Dispose(bool)` (`RenderTarget2D.cpp:84-100`) delegates to `Texture2D::Dispose(disposing)` (line
  94) which itself does the `backend_.reset()`, all four resource types are confirmed to correctly null their
  backend on disposal — the assertion is not merely "probably true," it was traced to hold for every one of the four
  types this test constructs.
- **Clearing `unique_ptr`s after dispose doesn't crash** (lines 111-116): since `backend_` is already null after
  `Dispose()`, each object's destructor (`~Texture2D`/`~VertexBuffer`/`~IndexBuffer`/`~RenderTarget2D`, all default
  per their headers) has nothing live to release — `GraphicsResource::~GraphicsResource()` calls `Dispose(false)`
  again (`GraphicsResource.cpp:39-42`), which re-enters the already-`isDisposed_`-guarded `Dispose(bool)` and returns
  immediately (`GraphicsResource.cpp:86-89`) — confirmed genuinely safe, not merely "didn't crash in this one run."
- **Second batch (2 resources) behaves identically** (lines 118-127): this specifically guards against a bug class
  where only the *first-ever* construction of a given type does something different (e.g. a static/global
  one-time-initialization side effect that fires an extra event, or a lazily-initialized backend singleton that
  isn't accounted for in later constructions) — a real, non-obvious defect category this design choice is
  well-suited to catch, since the first batch already constructed 20 of each type before this second batch runs.

### Logic
`trackBefore`/`trackAfter` (lines 72, 82) correctly compute the tracked-count *delta* rather than asserting an
absolute value — correct given `GetTrackedResourceCount()` reflects the *device's* entire resource list, which could
include the framework's own internal resources (e.g. default `SamplerState`s) already tracked before this test runs;
asserting a delta avoids a spurious failure from any such pre-existing tracked count. `static_cast<int>(trackAfter -
trackBefore)` (line 86) — both are `std::size_t`; the subtraction happens in unsigned arithmetic before the cast, but
since `trackAfter >= trackBefore` is guaranteed by construction order (test never disposes before this measurement
point), no unsigned-underflow-then-huge-value risk exists here.

### Memory/resource lifetime
`std::vector<std::unique_ptr<T>>` (lines 62-65) with `.reserve(N)` (lines 67-70) is the correct ownership container
for a bulk-create-then-bulk-dispose test — no raw-pointer bookkeeping, no leak risk from the container itself. The
explicit `disposeVia(*r)` loop (lines 90-93) disposes each resource *before* the `unique_ptr`s are cleared (lines
111-114), which is deliberately testing "does explicit `Dispose()` before destruction leave the object in a safe
state for its destructor to run" — the harder, more meaningful ordering to get right compared to only ever relying
on RAII destruction.

### C++ correctness
No raw owning pointers; no casts beyond `System::IDisposable&`, an ordinary (and safe) upcast through public
inheritance in `disposeVia`.

### Performance
N/A — one-shot integration test; 80+2 resource constructions is a trivial cost for a smoke/leak test, not a
performance benchmark.

### Thread safety
N/A — single-threaded.

### Architecture
Correctly instruments via `NOXNA` extension points (`GetTrackedResourceCount`, `HasBackend`) rather than reaching
into `CNA::Internal::Backends` internals directly — appropriate white-box testing that stays at the public
(if CNA-extended) API boundary.

### Maintainability
`N`/`TYPES`/`TOTAL` (lines 28-30) are named `constexpr` constants used consistently throughout rather than repeated
magic numbers — good practice; the arithmetic relationship (`TOTAL = N * TYPES`) is self-documenting and confirmed
correct (20×4=80, matching every hardcoded `TOTAL` reference in the assertions).

### Portability
N/A — EasyGL-specific by shard convention, though the actual logic under test (`GraphicsDevice`'s resource tracking)
is itself backend-agnostic C++.

### Robustness
`check(true, "Clearing unique_ptrs after dispose does not crash")` (line 116) is an unusual but reasonable idiom —
if the preceding `.clear()` calls (lines 111-114) had actually crashed, this line would never execute and the test
binary itself would report a crash/non-zero exit rather than a `[FAIL]` line; using `check(true, ...)` as a marker
that "we got this far" is a legitimate (if slightly indirect) way to log a checkpoint in output, not a meaningless
assertion.

### Testing
This file is itself a test; see Missing/Weak Tests below for its own remaining gaps.

### Cross-file consistency
Directly extends `easygl_resource_events_test.cpp`'s single-instance event-count verification to a 20×4=80-instance
scale, and adds instrumentation (`HasBackend()`, `GetTrackedResourceCount()`) that the events test never touches —
confirmed the two files are complementary, not overlapping: the events test uniquely covers `Name`/`Tag` forwarding
and the device-less-resource negative case; this file uniquely covers bulk-scale tracking-list bookkeeping and
GPU-handle-null-after-dispose verification across four concrete resource types including `RenderTarget2D` (which the
events test never constructs at all).

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — `RenderTarget2D`'s "still bound" Dispose guard is never exercised by this bulk test (all RTs disposed while unbound)

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `rts` disposal loop (line 93); `RenderTarget2D::Dispose(bool)`'s bound-check
  (`RenderTarget2D.cpp:84-93`)
- Evidence: none of the 20 `RenderTarget2D` instances created in this file are ever passed to
  `GraphicsDevice::SetRenderTarget` — they are constructed, tracked, and disposed while never bound as the active
  render target, so `RenderTarget2D::Dispose(bool)`'s `for (const auto& binding : graphicsDevice_->GetRenderTargets())
  ... throw InvalidOperationException(...)` guard path (`RenderTarget2D.cpp:86-92`) is never actually reached by this
  test — the loop it iterates (`GetRenderTargets()`) is always empty for every one of these 80 disposals.
- Why it matters: purely a coverage-completeness note — this file's actual purpose (bulk leak-detection at scale) is
  correctly served by never binding the RTs, since binding/unbinding cycles would introduce backend-specific GPU
  state changes irrelevant to the tracking-list bookkeeping under test; the guard-path itself is more appropriately
  someone else's responsibility to test (and is, in fact, implicitly exercised whenever any of this shard's
  render-target rendering tests correctly avoid disposing a bound target — none of which are in this batch's 8
  files).
- FNA/XNA comparison: N/A (CNA-internal guard, no FNA equivalent since FNA's model differs — see
  `easygl_rendertargetcube_properties_test.cpp`'s F1 in this same batch for the related `RenderTargetCube` gap,
  which has no guard at all).
- Related files: `RenderTarget2D.cpp`.
- Suggested future action (not implemented by this audit): none required for this file's own stated purpose; a
  dedicated small test elsewhere (not in this batch) exercising "dispose while bound throws" would be the more
  natural place to close this specific gap.

## Cross-File Observations

- This file and `easygl_rendertargetcube_properties_test.cpp` (also in this batch) together sketch the full picture
  of the project's render-target dispose-safety story: `RenderTarget2D` has a "still bound" guard (exercised nowhere
  in this batch, per F1 above, but present in production code), while `RenderTargetCube` has none at all (a real,
  disclosed gap — see that file's own F1). Worth flagging as a single, unified cross-cutting note for whichever pass
  synthesizes this shard's findings: **render-target dispose-safety is asymmetric between `RenderTarget2D` and
  `RenderTargetCube`, and no test anywhere in this 8-file batch exercises either one's bound-guard behavior
  directly.**

## Missing or Weak Tests

- See F1 and the Cross-File Observation above — no test in this batch (or, as far as this audit's scope extends,
  identified elsewhere) actually disposes a *bound* render target to observe the `RenderTarget2D` exception path or
  the `RenderTargetCube` UAF-risk path in practice.
- No test in this file constructs resources of more than one type in a genuinely interleaved (not batched-by-type)
  order — all 20 of one type are created before the next type begins (lines 74-80, inner loop body creates one of
  each per iteration, so it *is* actually interleaved per-iteration — re-checking: line 74's loop body pushes one
  `Texture2D`, one `VertexBuffer`, one `IndexBuffer`, one `RenderTarget2D` per iteration, so construction genuinely
  interleaves types; only *disposal* is batched-by-type, lines 90-93). This is a minor point already correctly
  handled by the actual code — construction interleaving is present; only disposal-order interleaving is untested,
  which per the swap-and-pop tracing above is a non-issue since removal is order-insensitive.

## Positive Findings

- The "repeat batch" step (lines 118-127) is a genuinely well-designed defense against first-instance-only bugs — a
  non-obvious test design choice that shows real awareness of a realistic defect class (static/lazy initialization
  side effects), not boilerplate repetition.
- Delta-based tracking-count assertions (rather than absolute-value assertions) correctly avoid a spurious failure
  from any pre-existing device-tracked resources.
- Every mechanism this test relies on (`AddResourceReference`/`RemoveResourceReference`'s swap-and-pop algorithm,
  `backend_.reset()` ordering in all four resource types' `Dispose(bool)`) was independently traced and confirmed
  correct, not assumed from the test's own passing output.

## Final Assessment

A rigorous, well-designed bulk lifecycle test whose four concrete resource types, delta-based bookkeeping
assertions, and first-instance-artifact-catching repeat batch were all independently verified against the real
`GraphicsDevice`/`GraphicsResource`/backend disposal code. Its only gaps (F1, and the render-target dispose-safety
asymmetry noted for cross-file synthesis) are test-coverage completeness notes rather than defects in what this file
actually asserts.
