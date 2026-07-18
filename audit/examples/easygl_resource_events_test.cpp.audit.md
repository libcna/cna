# Audit: examples/easygl_resource_events_test.cpp

## Metadata

- Source file: `examples/easygl_resource_events_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `GraphicsDevice.ResourceCreated`/`ResourceDestroyed` event integration
  test (backend-touching only incidentally — the events themselves are backend-agnostic `GraphicsResource`/
  `GraphicsDevice` logic)
- File type: C++ example/integration-test executable (`ResourceEventsTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::GraphicsDevice::OnResourceCreated`/
  `OnResourceDestroyed` (`GraphicsDevice.cpp:463-472`), `Microsoft::Xna::Framework::Graphics::GraphicsResource`
  constructor/`Dispose(bool)` (`GraphicsResource.cpp`), `ResourceCreatedEventArgs`/`ResourceDestroyedEventArgs`
- XNA/FNA relevance: `GraphicsDevice.ResourceCreated`/`ResourceDestroyed` events and their `EventArgs` shapes
  (`Resource` / `Name`+`Tag`) are real XNA 4.0 members; judged against
  `FNA/src/Graphics/GraphicsDevice.cs` and `ResourceCreatedEventArgs.cs`/`ResourceDestroyedEventArgs.cs`.
- Main related tests: this file (Task 217); its "no leaked handles" concern is extended at scale by the sibling file
  `easygl_resource_leak_test.cpp` (Task 219) in this same batch.

## Purpose

Verifies `GraphicsDevice.ResourceCreated`/`ResourceDestroyed` fire correctly: once per construction/first-Dispose,
never on a redundant second `Dispose()`, never for a device-less resource (`BlendState` with no `GraphicsDevice`),
and that `ResourceDestroyedEventArgs` correctly carries the `Name`/`Tag` set on the disposed resource. Correctly
placed per `AUDIT_SCOPE.md`.

## Executive Verdict

**Healthy** — every assertion in this file was independently traced through the real event-firing chain
(`GraphicsResource` ctor/`Dispose` → `GraphicsDevice::OnResourceCreated`/`OnResourceDestroyed` →
`EventHandler<T>::Raise`) and confirmed correct; this file also incidentally reveals a genuine, interesting FNA
behavioral divergence worth recording as a cross-cutting observation (see below) rather than a defect.

## Checklist Results

### API / XNA / FNA parity
Confirmed against `FNA/src/Graphics/GraphicsDevice.cs`: `public event EventHandler<ResourceCreatedEventArgs>
ResourceCreated;` / `public event EventHandler<ResourceDestroyedEventArgs> ResourceDestroyed;` (lines 359-360) and
`internal void OnResourceCreated(object resource)` / `OnResourceDestroyed(string name, object tag)` (lines 364-377)
match CNA's `GraphicsDevice::OnResourceCreated(System::Object*)` / `OnResourceDestroyed(const std::string&,
System::Object*)` (`GraphicsDevice.hpp:636,645`) both in name and parameter shape (`resource` vs. `name`+`tag`,
correctly asymmetric between the two events exactly as FNA declares them). `ResourceCreatedEventArgs::
getResourceProperty()` / `ResourceDestroyedEventArgs::getNameProperty()`/`getTagProperty()` correctly mirror FNA's
`ResourceCreatedEventArgs.Resource` / `ResourceDestroyedEventArgs.Name`/`.Tag` C# properties via the project's
established `getXProperty()` convention. `dev.ResourceCreated += [&](...) {...}` (line 63) matches the project's
documented `System::EventHandler<T>` subscription idiom (`operator+=`) per `CLAUDE.md`.

**Notable FNA-divergence, recorded for context (not a defect):** grepping the entirety of
`/rv/data/library/github.com/FNA-XNA/FNA/src/` shows `OnResourceCreated`/`OnResourceDestroyed` are declared in FNA's
`GraphicsDevice.cs` but are **never actually invoked from anywhere else in FNA's own source** — i.e., real FNA never
fires these events at all despite exposing them as public API surface (they exist purely for XNA-signature
compatibility). CNA's implementation, by contrast, genuinely wires `GraphicsResource`'s constructor/`Dispose` to call
them (`GraphicsResource.cpp:7-15,84-100`), making CNA's behavior here **more complete than FNA's own reference
implementation**, not merely equivalent to it. Per the project's own established audit guidance ("FNA is NOT
authoritative for API surface" — original XNA did fire these events for GPU-resource-leak-tracking tooling), this
is judged a deliberate, correct improvement rather than a divergence to flag as wrong; it is called out here only so
a future auditor doesn't mistake "this doesn't match FNA's dead-code stub" for a bug.

### Behavioral correctness — verified against production code
- **VB ctor fires `ResourceCreated` once, no destroy before Dispose, double-Dispose fires destroy once** (lines
  67-76): traced `GraphicsResource::GraphicsResource(GraphicsDevice* device)` (`GraphicsResource.cpp:7-15`) — calls
  `OnResourceCreated(this)` exactly once, only `if (graphicsDevice_)`. `GraphicsResource::Dispose(bool)`
  (`GraphicsResource.cpp:84-100`) begins with `if (isDisposed_) return;` (line 86-89) **before** calling
  `OnResourceDestroyed` — this is the exact guard that makes the "second Dispose does NOT re-fire" assertion (line
  75) correct; independently confirmed `VertexBuffer::Dispose(bool)` (`VertexBuffer.cpp:46-50`) calls
  `backend_.reset()` unconditionally then `GraphicsResource::Dispose(disposing)` — the `backend_.reset()` on an
  already-null `unique_ptr` on the second call is a harmless no-op, so no double-free risk from this ordering.
- **Name/Tag forwarded correctly** (lines 82-104): traced `IndexBuffer::Dispose` → `GraphicsResource::Dispose(bool)`
  → `graphicsDevice_->OnResourceDestroyed(name_, tag_)` (`GraphicsResource.cpp:96`) — `name_`/`tag_` are the same
  members set via `setNameProperty("MyIB")`/`setTagProperty(&tagObj)` (lines 95-96) since they're read at the moment
  of disposal, before any reset — confirmed `ResourceDestroyedEventArgs(name, tag)` (line 472,
  `GraphicsDevice.cpp:469-473`) constructs directly from these forwarded values with no aliasing/copy issue.
- **3 resource types (`VertexBuffer`/`IndexBuffer`/`Texture2D`) all fire both events** (lines 107-129): all three
  derive from `GraphicsResource` and rely on the same base-class ctor/`Dispose` mechanism — confirmed
  `Texture2D::Dispose(bool)` (`Texture2D.cpp:196-200`) follows the identical `backend_.reset(); Texture::Dispose
  (disposing);` → `GraphicsResource::Dispose(disposing)` chain.
- **`BlendState` without device does NOT fire `ResourceCreated`** (lines 132-144): confirmed `BlendState::BlendState()`
  (`BlendState.cpp:11-25`) never passes a device to the `GraphicsResource` base, so `GraphicsResource(GraphicsDevice*
  device = nullptr)`'s default (`GraphicsResource.hpp:65`) applies — `graphicsDevice_` is `nullptr`, and the ctor's
  `if (graphicsDevice_) { ... }` guard (`GraphicsResource.cpp:10-14`) is false, so `OnResourceCreated` is genuinely
  never called. This is the correct, verified behavior, not an assumption.

### Logic
Each of the 4 sub-tests is scoped in its own `{ }` block with explicit `dev.ResourceCreated.Clear()`/
`dev.ResourceDestroyed.Clear()` calls at the end (lines 78-79, 103, 127-128, 143) — correctly prevents a later
sub-test's lambda captures (which capture local counters by reference, e.g. `[&]`) from firing after their enclosing
scope has ended, which would otherwise be a dangling-reference call into a destroyed stack variable. This is a real,
correctly-applied safety measure, not incidental cleanup.

### Memory/resource lifetime
`disposeVia(System::IDisposable& r)` (line 49) is a small, correctly-typed helper that calls `Dispose()` through the
interface reference — exercises the polymorphic `IDisposable::Dispose()` path rather than a concrete-type-specific
one, which is the more rigorous thing to test (confirms the interface dispatch itself works, not just the concrete
implementation). `TagObj tagObj` (line 92) is stack-local, outliving the `IndexBuffer ib` scope that references it
via `setTagProperty(&tagObj)` — correct, no dangling-pointer risk since `tagObj`'s scope strictly encloses `ib`'s.

### C++ correctness
Lambda captures are all `[&]` (by reference) — correct given each lambda's lifetime is strictly bounded by its
enclosing `{ }` block and the explicit `.Clear()` calls at each block's end prevent any use-after-scope-exit call.

### Performance
N/A — one-shot integration test with a handful of resource constructions.

### Thread safety
N/A — single-threaded.

### Architecture
Exercises the public XNA `GraphicsDevice`/`GraphicsResource` event surface entirely correctly; `TagObj : public
System::Object` (lines 27-34) is a minimal, correctly-structured test double overriding `GetTypeName()` per the
project's `System::Object`-subclass convention (`CLAUDE.md`'s `NOXNA GetTypeName()` requirement) — appropriately
scoped to this test file only, not polluting production code.

### Maintainability
Concise (~165 lines), each sub-test has a clear inline comment (lines 59, 82, 106, 131) naming what it verifies —
matches the file's own top-of-file numbered summary (lines 4-9) exactly, with no drift between the stated intent and
the actual assertions.

### Portability
N/A.

### Robustness
`check()` (lines 43-47) consistent PASS/FAIL printing; final `%d/%d PASS` summary (line 146) matches shard
convention.

### Cross-file consistency
This file's "no leaked handles at N=1 scale, 4 resource types" foundation is directly extended at N=20×4=80 scale by
`easygl_resource_leak_test.cpp` — the two files form a deliberate small-scale/large-scale pair rather than
duplicating each other's coverage (confirmed: this file never checks `HasBackend()`/`GetTrackedResourceCount()`,
which is the leak test's unique contribution; this file's unique contribution is the `Name`/`Tag` forwarding and the
device-less-resource negative case, neither of which the leak test repeats).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — `BlendState bs;` sub-test only proves absence-of-device suppresses the event, not that a `BlendState` constructed *with* a device fires it correctly

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: sub-test 4 (lines 132-144)
- Evidence: `BlendState`'s only exercised constructor here is the no-device default (`BlendState()`,
  `BlendState.cpp:11-25`); `BlendState` also has a `BlendState(const std::string&, Blend, Blend, Blend, Blend)`
  overload (`BlendState.cpp:27-35`) used internally for the static presets (`Additive`, `AlphaBlend`, etc.), which
  also never takes a device — so no code path in this file (or, per inspection, in `BlendState.cpp` itself) actually
  constructs a device-bound `BlendState` to confirm the *positive* case (`ResourceCreated` fires for a `BlendState`
  that *does* have a device) — only the *negative* case is proven.
- Why it matters: low impact since `BlendState`'s two constructors both happen to never take a device in this
  codebase's actual design (device-bound state objects like `BlendState` are apparently always constructed
  device-less by design, matching real XNA's actual `BlendState`/`RasterizerState`/`SamplerState`/
  `DepthStencilState` semantics, which are plain value-holding config objects not tied to a specific device instance)
  — so this may not be a meaningful gap at all, but the test's own framing ("Resources constructed without a device
  … do NOT fire the events") implies a device-bound case exists to contrast against, which this file never actually
  demonstrates.
- FNA/XNA comparison: FNA's `BlendState`/similar state objects are indeed device-less value objects in the real API
  (no constructor overload takes a `GraphicsDevice`), so there may be no valid contrasting positive case to test at
  all for this specific class — lowering the practical severity of this gap further.
- Related files: `BlendState.hpp`/`.cpp` (own audit shard).
- Suggested future action (not implemented by this audit): none required; noted for completeness only.

## Cross-File Observations

- The FNA-divergence noted under API/XNA/FNA parity above (FNA declares but never fires `ResourceCreated`/
  `ResourceDestroyed`; CNA genuinely fires them) is a useful fact for any future FNA-parity audit of
  `GraphicsDevice.cpp`/`GraphicsResource.cpp` directly — worth carrying forward as a documented, deliberate CNA
  enhancement rather than re-discovering it as an apparent "extra behavior not in FNA" concern.

## Missing or Weak Tests

- See F1.
- No test confirms unsubscribing (`-=`, if `EventHandler<T>` supports it) correctly stops further event delivery —
  only `.Clear()` (removing all subscribers at once) is exercised.

## Positive Findings

- Every assertion in this file was independently traced through the real firing chain and confirmed accurate,
  including the subtle "second Dispose does not re-fire" guard-ordering detail.
- The explicit `.Clear()` calls between sub-tests correctly prevent a real dangling-reference bug that a less careful
  test author could easily have introduced (a `[&]` lambda outliving its captured stack variables).
- Uncovered and correctly contextualized a genuine, interesting FNA-vs-CNA behavioral difference (FNA's dead-code
  event stubs vs. CNA's genuinely wired implementation) rather than silently reproducing FNA's incompleteness.

## Final Assessment

A precise, well-verified test of real event-firing semantics with correct defensive scoping; its only gap (F1) is
minor and possibly moot given `BlendState`'s actual XNA-shaped API never offers a device-bound constructor to
contrast against.
