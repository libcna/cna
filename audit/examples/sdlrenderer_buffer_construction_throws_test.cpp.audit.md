# Audit: examples/sdlrenderer_buffer_construction_throws_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_buffer_construction_throws_test.cpp` (154 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 723: exhaustive re-verification that all
  `VertexBuffer`/`IndexBuffer`/`Dynamic*` construction overloads throw correctly on `SDL_Renderer`.
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_sdl_buffer_construction_throws` /
  `SDL_Renderer_BufferConstructionThrows`, `cmake/Tests/SdlRendererTests.cmake` lines 379-381).
- XNA/FNA relevance: direct — `VertexBuffer`/`IndexBuffer`/`DynamicVertexBuffer`/`DynamicIndexBuffer` constructor
  overloads, `IndexElementSize`, `BufferUsage`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/VertexBuffer.cpp`,
  `src/Microsoft/Xna/Framework/Graphics/IndexBuffer.cpp`,
  `include/Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp`,
  `include/Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp`,
  `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` (`CreateIndexBuffer32`'s default delegation),
  `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp` (`CreateVertexBuffer`/`CreateIndexBuffer16`,
  lines 795-808), `src/Microsoft/Xna/Framework/Graphics/GraphicsResource.cpp` (exception-path resource-list
  cleanup).

## Purpose

`SdlBufferConstructionThrowsTest::ThrowsExactRuntimeError` (lines 55-71) is a template helper that invokes a
lambda and asserts it throws `std::runtime_error` with an *exact* string match against an expected message (not
just "throws something"). The `Draw()` override (lines 74-135) exercises 10 distinct construction scenarios: both
`VertexBuffer` public constructor overloads, `DynamicVertexBuffer`, both `IndexBuffer` index widths, both
`DynamicIndexBuffer` widths, and 3 explicit boundary cases (`VertexBuffer(dev, 0)`, `VertexBuffer(dev, -1)`,
`IndexBuffer(dev, -1)`) checking the backend-unsupported throw fires even for degenerate counts, before any
argument validation could run. A final check re-clears the device and re-reads a pixel to confirm the device
itself is not left in a broken state after 10 consecutive constructor-throw exceptions.

## Executive Verdict

**Healthy** — every one of this file's 10 exact-message assertions was independently verified against the actual
current production code (both the backend's throw sites and each buffer class's C++ constructor-delegation chain)
and found correct. This is one of the more thorough, well-reasoned test files in this batch: its central technical
claim (the backend throw happens in the member-initializer list, before any argument validation in the constructor
body could run) was independently traced through both `VertexBuffer.cpp` and `IndexBuffer.cpp` and confirmed
true.

## Checklist Results

### API / XNA / FNA parity

N/A directly for this file's own logic (it deliberately targets an *intentionally unsupported* 3D surface on a
2D-only backend, not an XNA behavior to reproduce) — but the *shape* of the API surface exercised (both
`VertexBuffer` constructors, `IndexElementSize::SixteenBits`/`ThirtyTwoBits`, `BufferUsage::None`,
`DynamicVertexBuffer`/`DynamicIndexBuffer`'s own constructor signatures) was independently cross-checked against
each class's actual public constructor set and found to be a complete, not partial, enumeration of the
overloads that exist.

### Behavioral correctness

Independently re-traced the exact production code path for every one of the 10 `check()` calls:

- **`VertexBuffer(dev, 4)`** (line 86): delegates to `VertexBuffer(device, VertexDeclaration{}, 4,
  BufferUsage::None, false)` (`VertexBuffer.cpp` lines 15-18), whose member-initializer list constructs
  `backend_(device.GetBackend().CreateVertexBuffer(vertexCount))` (line 33) — `SdlGraphicsBackend::CreateVertexBuffer`
  (lines 795-799) calls `ThrowNo3D("CreateVertexBuffer")` unconditionally, producing exactly
  `"SDL_Renderer does not support 3D: CreateVertexBuffer"` — matches `kNoVb` (line 82) exactly.
- **`VertexBuffer(dev, decl, 4, BufferUsage::None)`** (line 88): same underlying delegating constructor, same
  message — confirmed.
- **`DynamicVertexBuffer(dev, decl, 4, BufferUsage::None)`** (line 92): `DynamicVertexBuffer`'s own constructor
  (`DynamicVertexBuffer.hpp` lines 22-28) calls `VertexBuffer(device, vertexDeclaration, vertexCount, bufferUsage,
  true)` — the trailing `dynamic=true` argument is received by `VertexBuffer`'s 5-argument constructor as an
  explicitly-named-but-unused parameter (`bool /*dynamic*/`, `VertexBuffer.cpp` line 32) — confirmed the `dynamic`
  flag has genuinely zero effect on which backend method is called or what throws, matching the file's own claim
  exactly.
- **`IndexBuffer(dev, 4)`** (line 97, 16-bit default): delegates to `IndexBuffer(device,
  IndexElementSize::SixteenBits, 4, BufferUsage::None, false)` (`IndexBuffer.cpp` lines 13-15), whose
  member-initializer conditionally calls `CreateIndexBuffer32`/`CreateIndexBuffer16` based on
  `indexElementSize == ThirtyTwoBits` (lines 30-32) — for `SixteenBits` this calls `CreateIndexBuffer16`, which
  `SdlGraphicsBackend` overrides (lines 801-804) to `ThrowNo3D("CreateIndexBuffer16")`, producing
  `"SDL_Renderer does not support 3D: CreateIndexBuffer16"` — matches `kNoIb` (line 83) exactly.
- **`IndexBuffer(dev, SixteenBits, ...)`** (line 100): same path, confirmed.
- **`IndexBuffer(dev, ThirtyTwoBits, ...)`** (line 102-103): calls `device.GetBackend().CreateIndexBuffer32(4)` —
  `SdlGraphicsBackend` does **not** override `CreateIndexBuffer32`, so `IGraphicsBackend`'s own base-class default
  (`include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` lines 750-755: `return
  CreateIndexBuffer16(index_capacity);`) is used, which reaches the exact same `SdlGraphicsBackend::CreateIndexBuffer16`
  override and therefore the exact same `"...CreateIndexBuffer16"` message — confirmed the test's own claim that
  a 32-bit `IndexBuffer` throws the 16-bit method's message, not a distinct 32-bit-specific one, is accurate, not
  a stale/invented detail.
- **`DynamicIndexBuffer(SixteenBits/ThirtyTwoBits, ...)`** (lines 107-112): confirmed directly —
  `DynamicIndexBuffer`'s constructor (`DynamicIndexBuffer.hpp` lines 23-29) forwards to `IndexBuffer(device,
  indexElementSize, indexCount, bufferUsage, true)`, reaching the exact same `IndexBuffer` 5-argument constructor
  and the same ignored `bool /*dynamic*/` parameter as the non-dynamic case — consistent with the file's own claim.
- **Degenerate counts** (`VertexBuffer(dev, 0)`, `VertexBuffer(dev, -1)`, `IndexBuffer(dev, -1)`, lines 117-122):
  since `backend_`'s initializer runs *before* the constructor body (where any `vertexCount`/`indexCount`
  range-validation would have to live, and no such validation exists anywhere in the constructor body for either
  class per the source read above), the backend throw always fires first regardless of the count's value — a
  correct, verified claim, not merely an assumption.

### Memory/resource lifetime

An important, non-obvious C++ exception-safety question this test's own final check (line 130) partially, but not
fully, addresses: when a derived constructor's member-initializer list throws *after* a base class subobject has
already been fully constructed, the C++ standard guarantees that already-constructed base/member subobjects are
destroyed in reverse order during stack unwinding, even though the derived object itself never finishes
constructing. Here, `VertexBuffer`/`IndexBuffer`'s base `GraphicsResource(&device)` constructor (which calls
`graphicsDevice_->AddResourceReference(this)`, registering the not-yet-fully-constructed object into the
device's own `resources_` vector) completes successfully *before* the `backend_` member's initializer throws.
This audit independently traced `~GraphicsResource()` (`GraphicsResource.cpp` line 41: calls `Dispose(false)`)
and confirmed `Dispose(bool)` (lines 84-100) unconditionally calls `graphicsDevice_->RemoveResourceReference(this)`
(line 97) when not already disposed — meaning each of the 10 failed constructions in this test correctly
unregisters itself from `GraphicsDevice::resources_` during unwind, leaving no dangling pointer. This is a
genuine, verified positive: a less careful implementation could easily have left 10 dangling resource-reference
pointers in the device's internal list after this test's own runs. See "Missing or Weak Tests" below, however —
the test itself does not explicitly assert this invariant even though the ability to (`GraphicsDevice::
GetTrackedResourceCount()`, a `NOXNA` accessor) already exists.

### C++ correctness

`ThrowsExactRuntimeError`'s catch structure (lines 62-70: catches `const std::runtime_error&` first, then falls
back to `const std::exception&` returning `false`) correctly distinguishes "threw the right exception type but
possibly wrong message" (returns false, a comparison failure) from "threw some other unrelated exception type
entirely" (also returns false) — both failure modes are indistinguishable from the test's own `check()` output,
which is a minor observability gap (a failing check doesn't tell you *which* of these two happened) but not a
correctness defect.

### Robustness

The final check (lines 124-131) — clearing to cyan, then reading back a centre pixel and asserting
`G>=240 && B>=240` — is a real, if coarse, verification that the device did not enter a broken/half-initialized
state after 10 consecutive constructor-throw exceptions. `PresentationMode::NativeBackBuffer` correctly set
(line 143), consistent with this batch's Task 915 rationale.

### Testing

This file is itself the test; see Behavioral correctness and Memory/resource lifetime above.

## Detailed Findings

None at HIGH/CRITICAL severity. No MEDIUM findings. One LOW-severity test-coverage observation (see "Missing or
Weak Tests").

## Cross-File Observations

- Complements `sdlrenderer_clearoptions_audit_test.cpp`'s general "does this 2D-only backend fail cleanly and
  leave the device usable" theme, applied here to buffer construction instead of `Clear()`.
- The `CreateIndexBuffer32` default-delegation behavior this test relies on (`IGraphicsBackend.hpp` lines 750-755)
  is a shared-interface convenience for every 2D-only or 16-bit-only backend, not SDL_Renderer-specific — worth
  keeping in mind if a future backend needs a genuinely distinct 32-bit-unsupported message.

## Missing or Weak Tests

- The test's own final "device remains usable" check (lines 124-131) verifies `Clear()`/`GetBackBufferData()`
  continue to work, but does not verify the more specific, more relevant invariant this audit independently
  traced as true: that none of the 10 failed constructions left a dangling entry in
  `GraphicsDevice::resources_`. `GraphicsDevice::GetTrackedResourceCount()` (a `NOXNA` accessor, declared at
  `include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp` line 671) already exists and would let this test
  assert `dev.GetTrackedResourceCount()` returns to its pre-test baseline after all 10 throws — a cheap, valuable
  addition that would turn this audit's independently-confirmed-correct-but-unasserted invariant into an actual
  regression guard.

## Positive Findings

- Every one of the 10 exact-message assertions was independently traced through the real constructor-delegation
  chains in `VertexBuffer.cpp`/`IndexBuffer.cpp` and confirmed correct, including the more subtle claims (32-bit
  `IndexBuffer` throwing the 16-bit message via default delegation; the `dynamic` flag being a fully-ignored
  parameter for both `Dynamic*` types).
- The "throw happens before any argument validation, because it's in the member-initializer list" claim is a
  genuinely correct and non-obvious C++ observation that this audit independently verified by reading both
  classes' actual constructor bodies, not merely a plausible-sounding assertion.
- Exception-path resource-list cleanup (an easy-to-get-wrong C++ exception-safety detail) was independently
  confirmed correct for this exact scenario, even though the test itself does not explicitly assert it.

## Final Assessment

A rigorous, well-reasoned test whose every factual claim about production-code behavior was independently
verified and found accurate. The one improvement opportunity (asserting `GetTrackedResourceCount()` stability)
would strengthen an already-solid file rather than fix a defect.
