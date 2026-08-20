# Audit: src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (1984 lines)
- Audit status: AUDITED (full structural read + targeted deep read of Clear/Present/Reset/Dispose/
  createBackend/event-raising/exception sites; Draw* overload bodies spot-checked, not each
  individually line-by-line given this file's size)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct, central XNA type; FNA reference: `src/Graphics/GraphicsDevice.cs`
  (1820 lines)
- Main related tests: not independently located in this pass

## Purpose
Implements the entire `GraphicsDevice` surface: construction/backend creation, state properties,
Clear/Present/Reset/Dispose, render-target/vertex/index-buffer binding, and every Draw*/
DrawUser*/DrawUserIndexed* overload.

## Executive Verdict
The single most heavily-used class in the whole framework, and mostly a faithful, well-documented
port with two confirmed, real defects: a HIGH-severity internal inconsistency in exception-type
usage (this exact file uses the project's own `System::` exception types correctly in 13 places,
but raw `std::runtime_error`/`std::invalid_argument` in ~27 others for materially identical kinds
of validation), and a MEDIUM-severity event-ordering inversion in `Dispose()` relative to FNA's
real `Dispose(bool)`.

## Checklist Results

### Device-lifecycle event wiring (specifically requested cross-check)
**Confirmed: `GraphicsDevice` itself correctly raises every device-lifecycle event at the right
point.** `DeviceResetting` fires at the very start of `Reset(...)` (line 391), before any parameter
is applied; `DeviceReset` fires at the very end (line 438), after the backend has been fully
reconfigured (virtual resolution, MSAA, swap interval, presentation format) — this ordering matches
real XNA's documented Resetting-before/Reset-after contract. `Disposing` fires in `Dispose()` (line
457). `DeviceLost`/`DeviceResetting`/`DeviceReset` are ALSO wired to a real backend-reported
device-lifecycle callback (`deviceEventCallback` in `createBackend()`, lines 1461-1475), gated by
`plans/plan_dx9.md D9-34`'s own honest disclosure that only the D3D9 backend actually calls this callback
today — every other backend leaves `deviceStatus_` at `Normal` permanently, matching this project's
policy of not faking a capability no backend can really report.

**This refines the previously-documented HIGH finding from the `xna-framework-core` shard
(`GraphicsDeviceManager` never subscribes to `GraphicsDevice`'s own `DeviceResetting`/`DeviceReset`/
`Disposing` events): the gap is entirely on the `GraphicsDeviceManager` subscriber side, not here.**
`GraphicsDevice` itself has no gap in when or whether it raises these events — a caller that DOES
subscribe would receive them correctly and at the correct points.

### HIGH — pervasive, internally-inconsistent use of raw `std::` exceptions instead of this
project's own `System::` exception types
This file `#include`s and correctly uses `System::ArgumentOutOfRangeException` (lines 294, 1970),
`System::InvalidOperationException` (lines 375, 717, 777, 830, 1012), `System::ObjectDisposedException`
(lines 517, 524, 1824, 1864, 1914), and `System::NotSupportedException` (line 1897) — 13 call
sites total, all correct and idiomatic for this codebase. **The same file simultaneously throws raw
`std::runtime_error`/`std::invalid_argument` at ~27 other sites** for materially identical kinds of
argument/state validation:
- Every one of the ~20 "no vertex buffer is bound" / "no index buffer is bound" / "no effect has
  been applied" guards across `DrawPrimitives`/`DrawIndexedPrimitives`/every `DrawUserPrimitives`/
  `DrawUserIndexedPrimitives` overload throws `std::runtime_error` with a hand-written message
  (lines 570, 573, 607, 610, 613, 651, 655, 659, 703, 763, 874, 900, 925, 952, 980, 1023, 1053,
  1082, 1113, 1145, 1175, 1204, 1235, 1268, 1294) — this is exactly the same *kind* of "invalid
  call sequence" condition `System::InvalidOperationException` is already used for four lines away
  in the same overload family (e.g. line 717's "Unrecognized primitive type!").
- `GetBackBufferData`'s null/size validation (lines 1781, 1799) throws `std::invalid_argument`/
  `std::runtime_error` where `System::ArgumentNullException`/`System::ArgumentException` would
  match this project's convention (and FNA's own real `GetBackBufferData` throws
  `ArgumentException("Output buffer size incorrect")` for the size case — line 1089 of
  `GraphicsDevice.cs` — so the raw `std::runtime_error` here is also a divergence from the specific
  real FNA exception type, not just this project's convention).
- `SetRenderTargets`'s render-target-count validation (line 1884) throws `std::invalid_argument`.
- The internal `makeSdlError()` helper (line 56-59) and the "backend is not available" guard (line
  1317) both use `std::runtime_error`.

**Why this matters**: `GraphicsDevice` is reachable from essentially every real game's per-frame
code path — a caller catching this project's own `System::Exception` hierarchy (the documented,
established idiom elsewhere in this codebase) to handle a "no effect applied" mistake gracefully
would not catch it here, since `std::runtime_error`/`std::invalid_argument` do not derive from
`System::Exception`. This is the largest single-file instance of this audit's recurring
"raw `std::` exception instead of the project's own exception type" cross-cutting pattern found so
far — larger than the `xna-gamerservices` shard's `PropertyDictionary` (9 methods), by a wide margin
(~27 sites), and in the single most central, most frequently-exercised class in the entire
framework.

### MEDIUM — `Dispose()`'s resource-teardown/event order is inverted relative to FNA's real
`Dispose(bool disposing)`
CNA's `Dispose()` (lines 441-460): disposes every tracked `GraphicsResource` FIRST (lines 448-455),
**then** raises `Disposing` (line 457). Real FNA's `GraphicsDevice.Dispose(bool disposing)`
(`GraphicsDevice.cs` lines 500-536) does the opposite: raises `Disposing` FIRST ("We're about to
dispose, notify the application" — line 508's own comment), **then** disposes every tracked
resource (lines 514-535). A `Disposing` event handler in real XNA/FNA can therefore still safely
inspect a still-valid graphics resource at the moment it fires; the equivalent handler in this port
can never do so, since every resource this device owns has already been disposed by the time
`Disposing` fires. This is a genuine, confirmed behavioral divergence, not merely a documentation
gap — a real handler written against documented XNA `Disposing` semantics (e.g. one that logs a
resource's current state, or defers some cleanup expecting resources are still alive) would observe
different, already-torn-down state in this port.

**Failure scenario**: a game subscribes `graphicsDevice.Disposing += (s, e) => { /* inspect or use
some resource still expected to be alive per XNA's documented ordering */ };` and calls
`graphicsDevice.Dispose()` — in FNA the resource is still valid at that point; in this port it has
already been disposed.

## Cross-File Observations
`Clear(const Color&)`'s own comment (Task 928) and `Clear(ClearOptions, ...)`'s own comment (about
masking `DepthBuffer`/`Stencil` out when the active target has no real depth-stencil buffer, and
Task 871's stencil-clear fix) both cite specific, real, previously-fixed defects with clear
before/after reasoning — consistent with this shard's general pattern of well-tracked incremental
fixes.

## Missing or Weak Tests
Not independently located in this pass; given the scale of the raw-exception-type finding, a test
asserting `catch (const System::Exception&)` actually catches a "no effect applied"/"no vertex
buffer bound" `Draw*` failure would have caught this immediately.

## Positive Findings
- Device-lifecycle event raising (`DeviceResetting`/`DeviceReset`/`DeviceLost`/`Disposing`) is
  fully and correctly implemented, resolving the ambiguity left open by the prior
  `xna-framework-core` shard's `GraphicsDeviceManager` finding.
- `Reset()`'s own comments correctly identify and fix two previously-missing forwarding paths
  (`PresentationInterval` never reaching `SetSwapInterval`; back-buffer/depth-stencil
  format/fullscreen never reaching the backend) — both real, disclosed, tracked fixes
  (`plans/plan_dx9.md D9-30`/`D9-33`).
- Where `System::` exceptions ARE used (13 sites), they are used correctly and idiomatically.

## Final Assessment
Two real findings: a HIGH-severity, large-scale internal inconsistency in exception-type usage
(the single largest instance of this audit's recurring pattern), and a MEDIUM-severity `Dispose()`
event-ordering inversion relative to FNA. Device-lifecycle event raising itself is confirmed
correct, resolving the open question from the sibling `xna-framework-core` shard's
`GraphicsDeviceManager` finding in `GraphicsDevice`'s favor.
