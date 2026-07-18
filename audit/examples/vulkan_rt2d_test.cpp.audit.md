# Audit: examples/vulkan_rt2d_test.cpp

## Metadata

- Source file: `examples/vulkan_rt2d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `RenderTarget2D` full render/sample cycle, Vulkan
  backend
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_rt2d …)` / `cna_register_backend_test(NAME
  Vulkan_RenderTarget2D_FullCycle …)`, `cmake/Tests/VulkanTests.cmake:363-367`).
- XNA/FNA relevance: direct — `RenderTarget2D`, `GraphicsDevice.SetRenderTarget`,
  `BasicEffect.VertexColorEnabled`, `SpriteBatch` sampling a render target as a texture.
- FNA reference: `Graphics/Effect/StockEffects/BasicEffect.cs` (`VertexColorEnabled` field,
  default `false`), `Graphics/GraphicsDevice.cs` (`SetRenderTarget(RenderTarget2D)`).
- Related production code: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`RecordCommandBuffer()` two-phase deferred recording, lines 6224-6920;
  `pc[31] = p.vertexColorEnabled` at lines 3591/3633), `include/Microsoft/Xna/Framework/Graphics/
  BasicEffect.hpp` (`VertexColorEnabled` default, line 48).

## Purpose

The simplest RT round-trip in this shard: Phase 1 draws a full-screen red
`VertexPositionColor` quad into a `RenderTarget2D` sized to the viewport via
`DrawUserPrimitives`+`BasicEffect` (`VertexColorEnabled=true`); Phase 2 unbinds the RT and blits it
to the backbuffer as a texture via `SpriteBatch`; `GetBackBufferData` then asserts the centre
pixel is red. It is the shard's "does the basic RT pipeline work at all" smoke test, with two
inline comments citing specific historical bugs (Task 361/364) that this exact scenario would
have caught.

## Executive Verdict

**Healthy** — the test's own logic is correct and its two inline bug citations were independently
verified against the current source and `git log`, both accurate.

## Checklist Results

### API / XNA / FNA parity
`fx.VertexColorEnabled = true;` (public field assignment, line 81) matches CNA's established
convention of leaving `BasicEffect::VertexColorEnabled` as a plain public field (not a
getter/setter pair) — confirmed against `BasicEffect.hpp:48`, where it is declared as
`bool VertexColorEnabled = false;`, i.e. a direct field, consistent with this test's usage and the
comment "VertexColorEnabled defaults to false (Task 361)".

### Behavioral correctness
Re-verified both cited historical claims against the actual current source rather than trusting
the comment at face value:
- *"VertexColorEnabled defaults to false (Task 361)"* — confirmed: `BasicEffect.hpp:48` reads
  `bool VertexColorEnabled = false;`; `git log --oneline --all | grep "Task 361"` shows commit
  `4e0466cf`/`c079089d` *"fix(Task 361): audit BasicEffect properties/defaults against FNA, fix 2
  default-value bugs"*.
- *"previously masked by a since-fixed Vulkan bug — Task 364 — where the colored3D pipeline
  ignored this flag and always used vertex color"* — confirmed: commit `736d3b95`/`54aee7a2`
  *"fix(Task 364): honor VertexColorEnabled in BasicEffect's no-texture shader path on all 3
  backends"*, and the current `VulkanGraphicsBackend.cpp` genuinely threads
  `p.vertexColorEnabled` through to the shader (e.g. `pc[31] = p.vertexColorEnabled ? 1.f : 0.f;`
  at lines 3591/3633) — i.e. this is not a stale claim, the fix is real and present.

### Logic
Both phases share a single global clear-colour scalar (`clearR_`/`G_`/`B_`/`A_`), and the file's own
comment ("Global clear colour: used by both passes in RecordCommandBuffer") correctly identifies
this — verified directly against `VulkanGraphicsBackend::Clear()` (only one scalar tuple, no
per-RT storage). Because `Clear(Color(0,255,0,255))` is called once, *before*
`SetRenderTarget(rt_.get())`, the RT never enters `clearedRTs_` via the Task-875 mechanism; it is
still correctly included in `usedRTs` because the subsequent `DrawUserPrimitives` call tags
`draw.rt` (verified against `RecordCommandBuffer()`'s `usedRTs` construction, lines 6702-6707).
Since the quad is genuinely full-screen NDC coverage, no RT pixel is left un-drawn, so the shared
green clear-colour scalar never actually shows through in the sampled result — a fragile-looking
but, on inspection, harmless coupling for this specific full-screen-quad test.

### C++ correctness
`device.setRasterizerStateProperty(RasterizerState::CullNone);` with the accompanying "Task 896
finding" comment (winding is CCW/back-facing under CNA's real default `RasterizerState`) — the
same Task 896 fix (`b6a00bc6`, verified in the previous file's audit in this batch) applies here
identically; the comment is accurate and current, not stale.

### Testing
Single centre-pixel PASS/FAIL assertion (`R>=200, G<=50, B<=50`), appropriately tight for a
solid-colour full-screen scene. No boundary/edge-pixel check (e.g. a corner outside the RT quad),
but the quad is genuinely full-screen so there is no non-trivial boundary to probe in this
specific scenario.

### Cross-file consistency
Structurally near-identical to `vulkan_rtcube_test.cpp` and
`vulkan_rendertargetcube_sample_test.cpp` in this same shard (Phase-1-RT / Phase-2-backbuffer /
`GetBackBufferData` pattern) — this file is the `RenderTarget2D` baseline the other two extend to
`RenderTargetCube`.

## Detailed Findings

None — no CRITICAL/HIGH/MEDIUM findings identified. The file is short, its comments were
independently re-verified rather than trusted, and both claims checked out against current source
and git history.

## Missing or Weak Tests

- No assertion on a pixel *outside* the drawn quad (there isn't one, since the quad is
  full-screen) — this means the test cannot distinguish "the RT's own image was sampled" from "the
  backbuffer's own most-recent Clear() colour leaked through," since both would need to differ for
  such a check to be meaningful. In this exact scenario, that ambiguity doesn't undermine the
  test's own claim (full-screen red is still a red flag if RT sampling were broken, since the RT
  would then show whatever garbage/undefined data instead), but a slightly stronger design (e.g.
  RT smaller than the viewport, with a background-colour assertion at a point outside the RT's
  own footprint) would make the pass/fail causality unambiguous. This is the same technique
  `vulkan_rt_roundtrip_test.cpp` and `vulkan_rtcube_test.cpp` in this same shard already use.

## Positive Findings

- Both inline bug citations (Task 361, Task 364) were independently confirmed accurate against
  current source and `git log` — no stale-comment issue here, unlike the sibling file audited
  immediately before this one in the same batch.
- Clearly documents the shared-clear-colour-scalar coupling between the two `RecordCommandBuffer`
  phases, which is a genuinely easy architectural detail to miss when reasoning about this
  backend's deferred two-phase recording model.

## Final Assessment

A small, correct, well-documented smoke test. Its only mild weakness is exercising a scenario
(full-screen quad) too simple to fully disambiguate "RT sampling is genuinely correct" from "the
shared clear-colour scalar happened not to matter here" — worth strengthening only if a future
regression in that area proves hard to isolate with the existing suite.
