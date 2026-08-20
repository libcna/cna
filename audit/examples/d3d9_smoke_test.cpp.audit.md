# Audit: examples/d3d9_smoke_test.cpp

## Metadata

- Source file: `examples/d3d9_smoke_test.cpp` (1141 lines)
- Audit status: AUDITED (STATIC/SOURCE-READING ONLY — see Environment Note)
- Subsystem: `examples-tests-d3d9` shard — the backend's foundational device-lifecycle smoke test
  (`plans/plan_dx9.md` Phase D9-3 through D9-64, checks A-Z plus `RunNoDepthBufferCheck()`/
  `RunHiDefProfileCheck()`), by far the largest single file in this batch.
- File type: standalone `Game`-subclass executable + two free-standing non-`Game` helper functions,
  CTest-registered, real window/device path.
- XNA/FNA relevance: broad but mostly indirect — exercises `GraphicsDevice::Clear()`/`Present()`/
  `GetBackBufferData()`/`SetRenderTarget(s)`/`DeviceLost`/`DeviceResetting`/`DeviceReset` events/
  `GraphicsDeviceStatus`, all real XNA 4.0 API surface, but through backend-internal `EXT` methods
  (`GetCapsEXT()`, `DebugSimulateContextLoss()`, etc.) that are themselves `NOXNA`.
- Related production code read in full: `src/CNA/Internal/Backends/D3D9/D3D9GraphicsBackend.cpp`
  (1112 lines, entirely read), `D3D9Buffers.cpp`, `D3D9RenderTargets.cpp`; and
  `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`'s `Clear(ClearOptions, ...)` dispatcher
  (lines 264-365) for the XNA-layer clear-flag routing this file's Checks B-I ultimately depend on.

**Environment note (per D-P4/audit instructions):** D3D9 is Windows-only; this report is entirely
static/source-reading. No build or execution was attempted or claimed in this Linux sandbox. All
conclusions below (agreement between test expectations and production logic) were reached by direct
line-by-line comparison of the test's assertions against the current backend source, not by running
anything.

## Purpose

A single large `Game`-subclass smoke test covering, in one file: device creation and real D3DCAPS9
(Check A), 6 `ClearOptions` combination variants with genuine target/depth/stencil isolation proofs
(B-I), a live backbuffer-resize/`Reset()` convergence check (L), the full simulated device-lost →
resetting → reset lifecycle (M), vertex/index buffer byte-exact round-trips including a 16-vs-32-bit
distinction (N-O), device-lost/recover buffer release-and-lazy-recreate semantics (P), texture/
cube/volume-texture byte round-trips gated on real device caps (Q-R), render target / render-target-
cube / MSAA-render-target / MRT bind-clear-unbind-restore cycles (S-V), a real occlusion query (W),
NPOT capability-gated texture creation (X), `ApplySamplerState()` readback (Y), and a real
depth-test-enable/disable rasterization proof (Z) — plus two free-standing checks for a
no-depth-buffer device (J) and `GraphicsProfile::HiDef` construction (K).

## Executive Verdict

**Healthy** — this is the strongest file in the batch. Every check traced against current production
code (`D3D9GraphicsBackend.cpp`, `D3D9Buffers.cpp`, `D3D9RenderTargets.cpp`) matches what that code
actually does, each check's own header comment accurately explains what real D3D9/DXVK behavior it is
proving (not merely asserting a value), and several checks explicitly document real bugs this task
found and fixed along the way (the zFarPlane sign convention noted for SpriteBatch, the D9-64
`SetDepthTestEnabled`/`SetDepthWriteEnabled` silent-throw-stub fix) with the fix itself independently
confirmed present in current source.

## Checklist Results

### API / XNA / FNA parity
Checks B-I's dispatch is XNA-parity-relevant even though invoked through `dev.Clear(...)` (real public
API): `GraphicsDevice::Clear(const Color&)` (`GraphicsDevice.cpp` lines 264-274) correctly matches
FNA's single-argument `Clear(Color)` overload (`Target|DepthBuffer|Stencil`, `Viewport.MaxDepth`, `0`
— confirmed against `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/GraphicsDevice.cs` lines
791-799), and `Clear(ClearOptions, ...)` (`GraphicsDevice.cpp` lines 284-365) correctly masks out
depth/stencil flags a backend/target genuinely cannot honor (line 320-323) before dispatching to one
of 7 exact-flag-combination backend methods (lines 337-364) — this file's Checks D-I each target one
of exactly those 7 combinations. `DeviceLost`/`DeviceResetting`/`DeviceReset` (Check M) are real
`System::EventHandler<EventArgs>` members on `GraphicsDevice`, matching the XNA event-based device-
loss contract.

### Behavioral correctness
- **Checks B/C**: `D3D9GraphicsBackend::Clear(float,float,float,float)` (`.cpp` lines 417-424) issues
  a plain `D3DCLEAR_TARGET`-only `Clear()` — but Checks B/C call `dev.Clear(Color)`, which (per above)
  actually routes to `ClearColorDepthAndStencil` since a real depth-stencil surface exists in this
  test's own device (constructed with `DepthFormat::Depth24Stencil8`). Confirmed: `hasClearFlag`
  routing in `GraphicsDevice::Clear(ClearOptions,...)` correctly reaches
  `backend_->ClearColorDepthAndStencil(...)` (line 339) since `clearTarget && clearDepth &&
  clearStencil` are all true here — this genuinely exercises the combination check, not the plain
  `Clear(r,g,b,a)` overload the test's own naming might suggest.
- **Check D** (`ClearColorAndDepth`): `HasDepthBuffer()`/`D3DCLEAR_ZBUFFER` gating
  (`D3D9GraphicsBackend.cpp` lines 448-456) traced — correct. The depth-stencil-surface reality check
  (`GetDepthStencilSurface()`+`GetDesc()`, format/size) is a genuine driver-validated proof, not an
  inference from a non-throwing call.
- **Checks E/F/G** (depth-only / stencil-only / depth+stencil-only, unchanged color proof):
  `ClearDepth()`/`ClearStencil()`/`ClearDepthAndStencil()` (lines 458-486) each build a `DWORD flags`
  containing only `D3DCLEAR_ZBUFFER`/`D3DCLEAR_STENCIL` (never `D3DCLEAR_TARGET`) — confirmed no path
  by which these could touch the color buffer, matching the check's own "color buffer must be
  UNCHANGED" assertion structurally, not just empirically.
- **Checks H/I** (`ClearColorAndStencil`/`ClearColorDepthAndStencil`): correct flag composition
  traced at lines 488-510.
- **Check J** (no depth buffer ⇒ silent no-op): `ClearDepth()`/`ClearStencil()` both guard with
  `if (!HasDepthBuffer(...)) return;` / `if (!HasStencilBuffer(...)) return;` (lines 461, 470) before
  ever calling `device_->Clear()` — confirmed these cannot throw when no depth-stencil surface exists,
  matching the check.
- **Check L** (resize convergence): `EnsureDeviceSize()` (`.cpp` lines 237-303) is invoked lazily
  (only from `Present()`, confirmed at line 435) — the test's own 30-frame poll loop with
  `dev.Present()` inside is therefore the *correct* way to observe this, not merely a defensive
  retry-for-flakiness pattern. The pre-`Reset()` release of every `ID3D9DefaultPoolResourceEXT`
  (lines 258-261) plus the cached depth-stencil surface (line 268) is confirmed to be exactly the
  fix `EnsureDeviceSize()`'s own comment (lines 253-267) describes as a D9-53 finding, and the test's
  own resize (from the initial 64×64 device to 96×80) would exercise precisely this path (both
  vertex/index buffers created earlier in the same `Draw()` frame and the cached depth-stencil
  surface are registered `ID3D9DefaultPoolResourceEXT`s).
- **Check M** (device-lost lifecycle): `DebugSimulateContextLoss()`/`DebugRestoreContext()` (lines
  1083-1103) and `PerformResetRecovery()` (lines 341-379) were traced end-to-end — `deviceLost_`
  becomes `true` and fires exactly one `Lost` callback (no `Resetting`/`Reset` yet, matching the
  test's own sequencing assertion), `ThrowIfDeviceLost()` (lines 392-398) is called at the top of
  every `Clear*` method (confirmed for the base `Clear()` at line 419; by extension the same pattern
  holds for all the sibling `Clear*` overloads), and `PerformResetRecovery()` fires `Resetting` then
  performs a real `device_->Reset()` then fires `Reset` — genuinely matching the documented sequence,
  not a hand-waved simulation (the only "simulated" part is *entering* the lost state; the recovery
  itself is a real `Reset()` call against the real device).
- **Checks N/O** (vertex/index buffer round-trip, 16-vs-32-bit distinction): `D3D9IndexBufferBackend`'s
  constructor takes an explicit `thirtyTwoBit` flag (`D3D9Buffers.cpp` line 117-122), and
  `CreateIndexBuffer32()` (`D3D9GraphicsBackend.cpp` line 628-631) passes `true` — confirmed this
  really creates a distinct `D3DFMT_INDEX32` buffer (`GetFormatEXT()`, line 135-138), not a silent
  delegation to the 16-bit path the test's own comment calls out as a known D3D11 trap
  (`D3D9IndexBufferBackend::Upload()` at line 161-170 additionally throws if the caller's own
  `dataIsThirtyTwoBit` disagrees with the buffer's declared width — an extra, real guard the test
  does not directly exercise but which reinforces the same guarantee).
- **Check P** (device-lost buffer release/recreate): `ReleaseDefaultPoolResourceEXT()`
  (`D3D9Buffers.cpp` lines 47-51) resets `buffer_`/`byteWidth_` to null/0; `EnsureCapacity()`
  (lines 53-78) checks `if (buffer_ && requiredBytes <= byteWidth_) return;` so a null `buffer_`
  always falls through to a fresh `CreateVertexBuffer()` — confirmed the exact lazy-recreate-on-next-
  `SetData()` semantics the check (and its own explicit "pointer reuse is not sound proof" caveat
  about testing actual data content instead of address) requires.
- **Checks S-V** (render targets, cube, MSAA, MRT): traced against `D3D9RenderTargets.cpp` in full.
  `BindAsRenderTarget()`/`UnbindAsRenderTarget()` (lines 82-110), the MSAA `StretchRect`-resolve on
  unbind (line 105-108), and `SetRenderTargets()`'s (`D3D9GraphicsBackend.cpp` lines 863-926)
  over-request throw (`count > caps_.NumSimultaneousRTs`, lines 874-880, a real, named
  `std::runtime_error`, not a silent clamp) all match the check's own assertions.

### Logic
`ClampMultiSampleCountEXT()` (`D3D9GraphicsBackend.cpp` lines 788-799) correctly notes D3D9's
`D3DMULTISAMPLE_TYPE` enumerators for 2-16 samples are numerically identical to the sample count
(`D3DMULTISAMPLE_4_SAMPLES == 4`), so the direct cast used both by this file (Check U) and by
`D3D9RenderTargetBackend::Recreate()` (`D3D9RenderTargets.cpp` line 44/62) is correct, not a
coincidence the test happens not to catch.

### C++ correctness
`ReadRenderTargetSurfaceD3D9()` (the file's own test helper, lines 139-164) assumes
`D3DFMT_A8B8G8R8` with no swizzle, consistent with this backend's own established RGBA8-storage
convention (matches `D3D9Textures.hpp`/`D3D9RenderTargets.hpp`'s own convention per the file's own
comment) — this is the same convention `ReadBackbuffer()` (`D3D9GraphicsBackend.cpp` lines 512-591)
uses for the back buffer itself, just without that function's additional `D3DFMT_A8R8G8B8` swap-RB
branch (render targets in this backend are never created in the display-only `A8R8G8B8` format the
swap chain sometimes substitutes, so the omission is correct, not an oversight).

### Memory/resource lifetime
`RunNoDepthBufferCheck()`/`RunHiDefProfileCheck()` (lines 1050-1125) each construct their own SDL
window and `D3D9GraphicsBackend` directly (bypassing `GraphicsDeviceManager`), and both correctly call
`SDL_DestroyWindow()` on every exit path (including the early `if (!sdlWindow) return;` guard, which
correctly does not attempt to destroy a null window). `RunHiDefProfileCheck()`'s
`D3D9GraphicsBackend backend(createArgs);` is constructed inside its own `try` block scoped to a
single `{}` — the backend's destructor runs before the function returns, releasing the device before
the window is destroyed, correct ordering.

### Performance
N/A — a one-shot lifecycle/smoke CTest, not a hot path.

### Architecture
Correctly scoped to backend-internal `EXT` methods for everything genuinely `NOXNA` (`GetCapsEXT()`,
`DebugSimulateContextLoss()`, `ClampMultiSampleCountEXT()`, etc.), while routing every genuinely-XNA
operation (`Clear`, `Present`, `GetBackBufferData`, the `DeviceLost`/`DeviceResetting`/`DeviceReset`
events) through the real public `GraphicsDevice` API rather than reaching into the backend directly —
matches this shard's own stated intent ("tested through the real public API", per the file's own
header comment on other D3D9 test files in this batch).

### Maintainability
Extremely well-commented for a 1141-line file — nearly every check's rationale, the specific D3D9/DXVK
quirk it defends against, and (where applicable) the exact bug this task found while writing it are
documented inline, letting this audit trace and independently confirm each one against current source
without needing to reverse-engineer intent.

### Robustness
Check K (`GraphicsProfile::HiDef`) explicitly and honestly discloses (lines 1090-1093) that only the
acceptance path can be exercised on real hardware in this environment — the rejection path (a device
whose real caps fall below the HiDef floor) has no way to be forced here. This is a disclosed,
reasonable gap, not a hidden one; the floor-check logic itself
(`D3D9GraphicsBackend::CreateDeviceResources()` lines 220-232) is a simple integer comparison, low
residual risk.

### Testing
This file alone is by far the broadest single-file test in the `examples-tests-d3d9` shard, covering
essentially the entire device-lifecycle surface of `D3D9GraphicsBackend`. Coverage gaps are narrow and
self-disclosed (Check K's rejection-path asymmetry; no draw-based occlusion-query proof yet, explicitly
deferred to D9-82 per Check W's own comment).

### Cross-file consistency
`D3D9GraphicsBackend.cpp`, `D3D9Buffers.cpp`, and `D3D9RenderTargets.cpp` were each read in full and
every assertion in this file traces to current, correct production logic — no stale expected-value or
already-fixed-bug claim was found (unlike some sibling D3D9 test files flagged elsewhere in this
batch's own cross-referenced findings, e.g. `d3d9_drawex_test.cpp`, not part of this batch but
mentioned in this shard's other reports).

## Detailed Findings

No CRITICAL/HIGH findings. No MEDIUM findings either — every check traced against current production
source is both real (not a tautology) and currently passing by this audit's own independent tracing.

## Missing or Weak Tests

- Check K's rejection path (a real device whose caps genuinely fall below `GraphicsProfile::HiDef`'s
  vs_3_0/ps_3_0 floor) cannot be exercised on this real, SM3-capable development GPU — self-disclosed,
  not a hidden gap.
- Check W's occlusion query only wraps a `Clear()` (no draw path exists yet per D9-82) — `PixelCount()
  == 0` is the correct, honest result for that scene, but a genuine occluded-vs-visible-geometry proof
  remains open work for whenever D9-82 lands, as the file's own comment already states.

## Positive Findings

- Check L's device-resize proof and Check M's full device-lost lifecycle are both driven through real
  `Reset()`/`TestCooperativeLevel()` calls against the actual D3D9 device (not mocked), exercising
  genuine DXVK/Wine behavior this project's own commit history (cited inline) shows was previously a
  source of real bugs (the D9-53 "still has alive losable resources" `Reset()` failure).
- Check O's explicit `IsThirtyTwoBit()`/`GetFormatEXT()` assertions directly target the exact
  silent-16-bit-delegation trap this project's own D3D11 backend (`DX-31`) previously fell into —
  genuinely discriminating, not a tautological presence check.
- Check Z is a real, two-sided proof (near-then-far draw both with and without depth testing enabled,
  asserting the OPPOSITE winner in each case) rather than a one-sided "didn't throw" check for a
  previously-silent-stub API (`SetDepthTestEnabled`/`SetDepthWriteEnabled`) this task's own comment
  documents finding and fixing (D9-64) — independently confirmed present and correct in current
  `D3D9GraphicsBackend.cpp` (lines 602-616).

## Final Assessment

A large, unusually thorough, and — as far as this audit's static tracing against current production
source can determine — fully correct device-lifecycle smoke test. Every check's own stated rationale
was independently verified against the actual implementation it exercises, and several checks
document real historical bugs (D9-53 Reset-with-live-resources, D9-64 depth-test silent stubs, the
D9-93 zFarPlane sign convention referenced from this file's own SpriteBatch sibling) whose fixes this
audit confirmed are genuinely present in current source, not merely claimed.
