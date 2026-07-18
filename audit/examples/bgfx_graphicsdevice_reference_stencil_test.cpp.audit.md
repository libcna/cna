# Audit: examples/bgfx_graphicsdevice_reference_stencil_test.cpp

## Metadata

- Source file: `examples/bgfx_graphicsdevice_reference_stencil_test.cpp` (190 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `GraphicsDevice.ReferenceStencil` reaching draw calls
  when set standalone (not via a full `DepthStencilState` re-application), Bgfx backend, Task 764
  (Bgfx-specific adaptation of `easygl_graphicsdevice_reference_stencil_test.cpp`, Task 319).
- CTest registration: `cna_bgfx_test(cna_test_bgfx_graphicsdevice_reference_stencil …)` /
  `cna_register_backend_test(NAME Bgfx_GraphicsDevice_ReferenceStencil …)`
  (`cmake/Tests/BgfxTests.cmake:634-636`).
- XNA/FNA relevance: direct — `GraphicsDevice.ReferenceStencil` (a genuinely independent device
  property, distinct from `DepthStencilState.ReferenceStencil`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`setReferenceStencilProperty()` forwarding to `backend_->SetReferenceStencil(value)`, lines
  1750-1756; `setDepthStencilStateProperty()` syncing `GraphicsDevice.ReferenceStencil` from the
  assigned state, lines ~1701-1710), `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`SetReferenceStencil()`/`RebuildStencilState()`, lines 1723-1771).

## Purpose

Two-check, per-check-separately-read pixel test proving `GraphicsDevice::setReferenceStencilProperty()`
called *standalone* (without re-applying a whole new `DepthStencilState`) genuinely reaches the
Bgfx draw call, rather than being silently ignored in favor of whatever `ReferenceStencil` value was
last baked into a `DepthStencilState`. Both checks stamp the screen to stencil `0x05`, then apply
the *same* `DepthStencilState` with `ReferenceStencil=0x05` baked in (matching the stamp). Check A
(baseline) draws with no further change → stencil `Equal` test passes → green. Check B additionally
calls `setReferenceStencilProperty(0x99)` standalone before the same test draw → if the override
genuinely reaches the draw, the effective reference becomes `0x99 != 0x05` → the stencil test fails
→ background color, not green.

## Executive Verdict

**Healthy** — this audit independently traced the full call path
(`GraphicsDevice::setReferenceStencilProperty()` → `IGraphicsBackend::SetReferenceStencil()` →
`BgfxGraphicsBackend::RebuildStencilState()`) and confirmed the standalone override genuinely
changes the GPU-facing stencil state without requiring a `DepthStencilState` re-application, exactly
as this test's two checks assume.

## Checklist Results

### Behavioral correctness — production code trace
`GraphicsDevice::setReferenceStencilProperty(int value)` calls `backend_->SetReferenceStencil(value)`
(`GraphicsDevice.cpp:1751-1756`). On Bgfx, `SetReferenceStencil(int value)` sets
`referenceStencilCached_ = value` and calls `RebuildStencilState()` (`BgfxGraphicsBackend.cpp:
1767-1771`), which rebuilds `stencilFront_`/`stencilBack_` from the *cached* function/pass/fail/mask
state plus the newly-updated `referenceStencilCached_` (lines 1723-1765) — i.e. exactly the
"rebuild without requiring a full `ApplyDepthStencilState` re-application" behavior the header
comment (lines 3-4, 13-15) and this test's own Check B assume. `ApplyDepthStencilState()` itself
(lines 1674-1721) caches `referenceStencil` alongside every other stencil parameter and also calls
`RebuildStencilState()` — so `MakeTestState()`'s baked-in `ReferenceStencil=0x05`
(`ds.setReferenceStencilProperty(0x05)`, line 91) is genuinely live until `SetReferenceStencil(0x99)`
(line 129, gated by `overrideReferenceStandalone`) explicitly overwrites just the reference value
afterward — precisely the sequence Check B needs to be a real, not accidental, test.
`GraphicsDevice::setDepthStencilStateProperty()` additionally syncs `GraphicsDevice.ReferenceStencil`
from whichever `DepthStencilState` was just assigned (`GraphicsDevice.cpp:~1701-1710`, "FNA applies
a DepthStencilState's own ReferenceStencil atomically as part of the whole state … keep
GraphicsDevice.ReferenceStencil in sync") — meaning Check A's baseline correctly starts from
`ReferenceStencil=0x05` via `MakeTestState()`'s own assignment (line 127), and Check B's subsequent
standalone `setReferenceStencilProperty(0x99)` (line 129) is a genuine, subsequent, independent
override on top of that, not a redundant no-op.

### Logic
Each check calls `RunCheck()` independently (lines 155-166) rather than reading two regions of one
shared frame — the header comment (lines 6-10) explains this is a deliberate restructuring from the
original EasyGL/Vulkan two-region-single-frame test, because "Bgfx's own `GetBackBufferData` only
reliably reflects the FIRST read per rendered frame (Task 406 finding)." This audit independently
traced `ReadBackbuffer()` (`BgfxGraphicsBackend.cpp:303-325`) and confirmed it internally calls
`bgfx::frame()` up to 3 times per invocation to force its screenshot callback — meaning a second
`GetBackBufferData()` call within the same nominal `Draw()` would already be reading after the bgfx
frame counter has advanced past the point where the second check's own stamp/test draws were
queued, corroborating the claim rather than merely trusting it (consistent with this batch's
`clear_stencil_test.cpp` report reaching the same independent conclusion).

### Robustness
`RunCheck()`'s own retry loop (lines 112-137, up to 20 iterations, breaking on first non-black
readback) is the standard anti-flakiness idiom used throughout this shard.

## Detailed Findings

None specific to this file. This audit's independent trace of the full property-to-GPU-state path
found it to be correctly wired end-to-end for exactly the scenario this test exercises.

## Cross-File Observations

- Shares `DrawQuad()`'s `RasterizerState::CullNone` workaround and the Task 896 cull-mode finding
  (comment lines 68-69) with this batch's `bgfx_graphicsdevice_clear_stencil_test.cpp` — both
  independently re-verified against `RasterizerState.cpp:11`'s `CullCounterClockwiseFace` default
  and `BgfxGraphicsBackend.cpp:1781-1782`'s `BGFX_STATE_CULL_CCW` mapping in that file's own report.
- Unlike the sibling `clear_stencil_test.cpp` (which uses a lazily-constructed, never-destroyed
  `static BasicEffect*`), this file constructs a fresh `BasicEffect fx(dev)` per `RunCheck()`
  iteration (line 117) — a more conventional RAII-friendly pattern; both are harmless in a
  short-lived test binary, but this file's approach is the cleaner of the two.
- This file's F1-equivalent-scope finding — whether `GraphicsDevice::Clear()`'s selective-buffer
  semantics are genuinely honored on Bgfx — is out of this file's scope (it never calls the
  multi-`ClearOptions` overload, only `Clear(ClearOptions::Target|ClearOptions::DepthBuffer,
  kBackground, 1.0f, 0)`, line 114, which is a fixed 2-of-3 combination unaffected by the
  Stencil-alone-clear gap documented in the sibling `clear_stencil_test.cpp` report's F1).

## Missing or Weak Tests

None identified beyond the general observation (shared with the sibling `clear_stencil_test.cpp`)
that this file's own scene keeps `Clear()`'s color/depth arguments constant (`kBackground`, `1.0f`)
across every call, so it would not itself surface the sibling file's F1 finding even if it were
relevant here (it is not, since this file never issues a stencil-only clear).

## Positive Findings

- The restructuring from a single-frame two-region read (the original EasyGL/Vulkan pattern) to two
  independently-read `RunCheck()` passes is well-justified and independently confirmed necessary by
  this audit's own trace of `ReadBackbuffer()`'s internal frame-advancing behavior.
- Check B's design (same baked-in `DepthStencilState`, only the standalone reference overridden
  afterward) is the correct minimal way to isolate "does `GraphicsDevice.ReferenceStencil` reach the
  draw independently of `DepthStencilState`" from "does `DepthStencilState.ReferenceStencil` reach
  the draw at all" (the latter already covered by Check A's baseline).

## Final Assessment

A well-designed, correctly-targeted test whose full property-to-GPU-state path this audit
independently traced and confirmed sound end-to-end; no defects found in either this file or the
specific production code path it exercises.
