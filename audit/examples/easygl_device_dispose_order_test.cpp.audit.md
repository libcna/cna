# Audit: examples/easygl_device_dispose_order_test.cpp

## Metadata

- Source file: `examples/easygl_device_dispose_order_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard), Task 218
- File type: standalone `Game` subclass test exercising `GraphicsDevice`/`GraphicsResource` lifecycle
- Related production code: `GraphicsDevice::Dispose()` (`GraphicsDevice.cpp:441-461`),
  `GraphicsResource::Dispose(bool)` (`GraphicsResource.cpp:84-100`), `AddResourceReference`/
  `RemoveResourceReference` (`GraphicsDevice.cpp:475-490`)
- FNA reference: general `IDisposable`/`GraphicsResource` disposal pattern (`Graphics/GraphicsResource.cs`,
  `Graphics/GraphicsDevice.cs`'s own resource-tracking `AddResourceReference`/`RemoveResourceReference`)
- Build registration: `cmake/Tests/EasyGLTests.cmake:986-987`

## Purpose

Exercises `GraphicsDevice`'s ownership/disposal contract for tracked `GraphicsResource`-derived objects
(`VertexBuffer`, `IndexBuffer`, `Texture2D`): that device disposal disposes all tracked resources before tearing
down the backend (no use-after-free), that double-`Dispose()` is a safe no-op at both the resource and device level,
and (per its own header comment) that resources explicitly disposed before device disposal are removed from the
tracking list rather than being disposed twice.

## Executive Verdict

**Needs attention** — not because the underlying `GraphicsDevice`/`GraphicsResource` disposal logic is wrong (it
was independently verified correct by reading `GraphicsDevice.cpp:441-461` and `GraphicsResource.cpp:84-100`), but
because this test file's own header comment claims a specific behavior is "Verified" (item 4) that its test body
explicitly admits, in its own inline comment, it does **not** actually exercise.

## Checklist Results

### Purpose
PASS — correctly scoped, placed under `examples/`, Task 218.

### API / XNA / FNA parity
N/A — exercises CNA-internal resource-tracking/disposal mechanics, not an XNA-namespace-visible API surface beyond
the standard `Dispose()`/`IsDisposed` pattern every `GraphicsResource` subclass already exposes.

### Behavioral correctness
**Partially verified — see Finding F1.** Items 1, 2, 3, and 5 from the file's own "Verified" list (header comment,
lines 4-11) are genuinely exercised:
- Item 1/2 (resources tracked, disposed before backend teardown): section 1 (lines 50-70) heap-allocates a
  `VertexBuffer`/`IndexBuffer`/`Texture2D`, confirms each `HasBackend()` before `dev.Dispose()`, then confirms all
  three are `getIsDisposedProperty()==true` and `!HasBackend()` afterward. This is a real, meaningful check: it
  proves the device's own `Dispose()` (which does `std::vector<GraphicsResource*> toDispose =
  std::move(resources_); ... for (res : toDispose) res->Dispose();` per `GraphicsDevice.cpp:451-455`) actually
  reaches resources the test never explicitly registered with — i.e., that `AddResourceReference` was correctly
  invoked by each resource's own constructor via `GraphicsResource`'s base constructor
  (`GraphicsResource.cpp:7-15`), which was independently confirmed by reading that file.
- Item 3 (double-dispose no-op): section 2 (lines 72-81) calls `Dispose()` again on the same three (now-disposed)
  resources and confirms no exception — matches `GraphicsResource::Dispose(bool)`'s own `if (isDisposed_) return;`
  guard (`GraphicsResource.cpp:86-89`), independently confirmed.
- **Item 4 is NOT actually tested.** The file's own header comment (line 9-10) claims: "Resources disposed
  explicitly before device disposal are removed from the tracking list so device disposal does not try to dispose
  them again." But section 3's body (lines 87-98) contains this self-admission in its own inline comment:
  "Since dev is now disposed, we can only check reference tracking via a resource NOT bound to the disposed
  device… Just verify: after device.Dispose(), double-dispose of device is safe." The actual assertion executed is
  `check(noThrow2, "Second device.Dispose() is a safe no-op")` — a *device*-level double-dispose check, which is a
  legitimate check in its own right, but is a different claim than item 4 (resource-level early-dispose +
  tracking-list removal). No resource is created, explicitly disposed, and then checked against a subsequent
  `device.Dispose()` anywhere in this file.
- Item 5 ("After device disposal the resource list is empty") is also never directly asserted — there is no public
  accessor exposed by `GraphicsDevice` to query `resources_.size()`, so this is inferred only indirectly (via "no
  double-dispose crash on a second `dev.Dispose()`"), not directly verified. The underlying implementation confirms
  it's true (`resources_.clear()` at `GraphicsDevice.cpp:452`), but this test file does not itself prove it.

### Logic
PASS for what is actually tested — `disposeVia(System::IDisposable&)` (line 40) correctly dispatches through the
`IDisposable` interface rather than the concrete type, matching the item being tested (that base-class
`Dispose()` dispatch is safe post-disposal).

### Memory/resource lifetime
PASS for what is tested — `new`/`delete` pairing for `vb`/`ib`/`tex` is correct (heap-allocated specifically so they
outlive the block scope until `dev.Dispose()` runs, then explicitly `delete`d at lines 83-85 after the
already-disposed-no-op check). No leak or double-free in the code as written.

### Testing
**Finding F1** applies here directly — see Detailed Findings.

## Detailed Findings

### F1 — Header comment claims resource-tracking-removal behavior (item 4) that the test body does not verify

- Severity: MEDIUM
- Confidence: HIGH (both the header's claim and the body's own admission were read directly)
- Category: testing / documentation accuracy
- Location: header comment `examples/easygl_device_dispose_order_test.cpp:4-11` (item 4) vs. test body
  `examples/easygl_device_dispose_order_test.cpp:87-98` (section 3)
- Evidence: the header lists 5 "Verified" behaviors; section 3's own in-code comment concedes it cannot actually
  test item 4 as originally scoped and substitutes a different (still valid, but distinct) check — "Second
  device.Dispose() is a safe no-op" — without updating the header's claim list to match what was actually
  delivered.
- Why it matters: per this project's own `CLAUDE.md` ("Do not mark an API as 'complete' in AUDIT.md unless its
  tests are also complete"), a test file's own claimed coverage should be trustworthy without having to read the
  implementation to discover the gap. A future maintainer skimming only the header comment (a completely reasonable
  thing to do) would believe the "explicit-dispose-before-device-dispose removes it from tracking" path is covered
  when it is not — e.g. a future regression that makes `GraphicsDevice::Dispose()` iterate a stale/uncleared list
  and double-`Dispose()` an already-explicitly-disposed resource would not be caught by this file, despite this
  file's own header claiming that exact scenario is verified.
- FNA/XNA comparison: N/A — this is a CNA-internal resource-tracking mechanism with no direct FNA API surface.
- Related files: `GraphicsDevice.cpp:441-490` (the actual tracking/disposal logic, confirmed correct by direct
  reading, i.e. this is a test-coverage gap, not a production-code defect).
- Suggested action (not implemented by this audit): add a genuine case — construct a resource on the same `dev`,
  call its own `Dispose()` explicitly *before* `dev.Dispose()`, then call `dev.Dispose()` and confirm (a) no
  exception, and (b) ideally some observable signal that the already-disposed resource wasn't touched a second time
  (e.g. a `Disposing` event-fire counter on that specific resource, confirming it fires exactly once total, not
  twice). Update the header's "Verified" list to match whatever is actually delivered.

## Cross-File Observations

- `GraphicsResource`'s copy constructor/assignment operator (`GraphicsResource.cpp:17-37`) reset `isDisposed_` to
  `false` and do not copy `graphicsDevice_`'s registration (i.e. a copied `GraphicsResource` is not itself
  re-registered via `AddResourceReference`) — outside this file's own test scope (none of `VertexBuffer`/
  `IndexBuffer`/`Texture2D` are copied here), but worth flagging for whichever shard directly audits
  `GraphicsResource.cpp`/its concrete subclasses, since a copied resource silently omitted from the device's
  tracking list could itself be an instance of the same "resource not tracked → not disposed with device" concern
  this file is trying to guard against, just via a different code path (copy rather than explicit-early-dispose).

## Missing or Weak Tests

- See F1 — item 4 as originally scoped is untested.
- No test of disposing the device with a resource still holding an *active* GPU binding (e.g. a bound
  `VertexBuffer` currently set via `SetVertexBuffer`) to confirm the device correctly unbinds/dereferences it rather
  than leaving a dangling current-binding pointer after disposal — an edge case not exercised anywhere in this file.

## Positive Findings

- Section 1's design (heap-allocate three different resource kinds, dispose the *device*, confirm all three
  transition to disposed with their backend freed) is a real, meaningful test of the tracked-resource fan-out
  disposal contract, independently confirmed correct against `GraphicsDevice::Dispose()`'s actual implementation.
- The `disposeVia(System::IDisposable&)` helper correctly exercises the polymorphic `Dispose()` path rather than
  only the concrete type's own method, which is the more rigorous check.

## Final Assessment

The device-to-resource disposal ordering this file *does* test is real and correctly verified against production
code. However, the file overclaims: its own header lists a specific tracking-removal behavior (item 4) as
"Verified" that its body's own comment admits it could not actually exercise and silently substitutes a different,
narrower check for. This is a genuine, evidence-based test-integrity finding (F1, MEDIUM) — the kind of gap the
anti-boilerplate audit rule is specifically meant to surface.
