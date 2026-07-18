# Audit: examples/vulkan_occlusionquery_pixelcount_test.cpp

## Metadata

- Source file: `examples/vulkan_occlusionquery_pixelcount_test.cpp` (269 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `OcclusionQuery.PixelCount()`/`IsComplete()` real
  per-draw-call correlation test.
- File type: standalone `Game`-subclass executable (`class VulkanOcclusionQueryPixelCountTest`),
  CTest-registered.
- XNA/FNA relevance: direct — `Microsoft::Xna::Framework::Graphics::OcclusionQuery` (`Begin()`,
  `End()`, `getIsCompleteProperty()`/`IsComplete`, `getPixelCountProperty()`/`PixelCount`).
- FNA reference: `src/Graphics/OcclusionQuery.cs` — `IsComplete`→`FNA3D_QueryComplete`,
  `PixelCount`→`FNA3D_QueryPixelCount`, `Begin()`/`End()` wrap `FNA3D_QueryBegin`/`QueryEnd`. The
  CNA property-convention names (`getIsCompleteProperty`, `getPixelCountProperty`) map 1:1 onto
  FNA's `IsComplete`/`PixelCount`.
- Related production code: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` —
  `VulkanOcclusionQueryBackend` (construction/`Begin`/`End`/`IsComplete`/`PixelCount`, lines
  ~8503–8572), `VulkanGraphicsBackend::RecordCommandBuffer()`'s query-pool reset block (lines
  ~6231–6251) and `draw3DFor`'s contiguous-run `vkCmdBeginQuery`/`vkCmdEndQuery` wrapping (lines
  ~6362–6403), `PushPending3DDraw()` (line 7252), `CreateOcclusionQuery()` (line 8003).
- Task references: Task 447/854 (`git log`: `dd6ed767 fix(Task 447/854): implement real
  per-draw-call OcclusionQuery correlation on Vulkan`, corroborating the header comment's
  narrative that this backend was previously "entirely-stubbed").

## Purpose

Three-scenario, 6-check pixel/property test proving the Vulkan `OcclusionQuery` backend does real
per-draw-call GPU occlusion correlation rather than a stub:
- **Scenario A** (`RunScenario(dev, occluded=false)`): a single quad wrapped in `Begin()`/`End()`,
  nothing occluding it — expects `PixelCount() > 0` and the rendered pixel to be the quad's own
  colour (Red).
- **Scenario B** (`RunScenario(dev, occluded=true)`): an opaque nearer quad (Blue, z=0.1, drawn
  *without* a query attached) drawn first, then the target quad (Red, z=0.9) wrapped in
  `Begin()`/`End()` — expects the real depth test to reject every target fragment
  (`PixelCount()==0`) and the visible colour to be the occluder's own (Blue), proving the target
  is genuinely hidden, not merely "a query returning 0 coincidentally."
- **Scenario C** (`RunMultiDrawScenario`): two non-overlapping half-quads (left/right) drawn
  between a *single* `Begin()`/`End()` pair — a discriminating check that a correct implementation
  must *sum* both draws' contributions (`4096` = `64×64`), not just count the last one
  (`~2048`), which would silently pass a naive "one draw span, keep only the last query result"
  bug.

## Executive Verdict

**Healthy** — all three scenarios are traced against the real Vulkan implementation and the math
and Vulkan synchronization sequencing both check out; this is a strong, genuinely discriminating
test, not a "compiles and doesn't crash" placeholder.

## Checklist Results

### API / XNA / FNA parity — PASS
`OcclusionQuery(dev)`, `query->Begin()`, `query->End()`, `query->getIsCompleteProperty()`,
`query->getPixelCountProperty()` (lines 100, 120–137) map exactly onto FNA's
`OcclusionQuery(graphicsDevice)`/`Begin()`/`End()`/`IsComplete`/`PixelCount` (`OcclusionQuery.cs`).
No XNA-facing member is exercised that doesn't exist in FNA's surface.

### Behavioral correctness — PASS
Re-traced the actual command sequence recorded by `RecordCommandBuffer()`'s `draw3DFor` lambda for
Scenario B: occluder (z=0.1, no query tag) is recorded first, writing depth 0.1 and colour Blue.
The tagged target draw (z=0.9) is next; `DepthStencilState` (lines 112–115,
`DepthBufferEnable=true`, `DepthBufferWriteEnable=true`) plus `DepthStencilState::Default`'s
`LessEqual` function (`include/.../DepthStencilState.hpp`) means `0.9 <= 0.1` is false, so every
target fragment is depth-rejected — exactly what `vkCmdBeginQuery`/`vkCmdEndQuery` (opened/closed
around this one draw per the `openQuery != draw.occlusionQuery` transition logic, lines 6392–6403)
would report as a 0-sample occlusion query. Traced `IsComplete()`/`PixelCount()`
(`vkGetQueryPoolResults`, lines 8556–8570) — `pixelCount_` is only written once `VK_SUCCESS` is
observed, matching this test's poll loop (`while` + `kMaxPollFrames=30`) rather than assuming
first-frame availability.

For Scenario C, confirmed `draw3DFor`'s contiguous-run tracking (`openQuery` stays equal to
`draw.occlusionQuery` across both half-quad draws since both carry the *same* tagged query and no
untagged draw interrupts the run) results in exactly one `vkCmdBeginQuery`/`vkCmdEndQuery` pair
spanning both draws — the sum-not-last-only behaviour the test's own comment (lines 239-241)
explicitly calls out as the differentiator from a hypothetical "keep only the last draw's tag"
bug.

### Logic — PASS
The polling loop's "continue"/"break" control flow (lines 106–145) is a single C++ `while` inside
one call to `Draw()`, not multiple real game frames — verified this is safe by tracing
`GraphicsDevice::GetBackBufferData` → `VulkanGraphicsBackend::ReadBackbuffer` (line 6982), which
only flushes (`SubmitFrame(true)`) when `hasNewWork` (`!pending3D_.empty() ||
!activeBatches_.empty()`) is true, and `RecordCommandBuffer()` unconditionally clears
`pending3D_`/`activeBatches_` at the end of every flush (lines 6809–6810). Because `Clear()`
itself is stateful, not an immediate GPU command (`VulkanGraphicsBackend::Clear()`, line 6214,
just stores `clearR_/G_/B_/A_`), re-calling `dev.Clear(kBlack)` at the top of every loop iteration
before the first `GetBackBufferData()` call is harmless — only the last clear colour before the
actual flush matters. Each `RunScenario`/`RunMultiDrawScenario` call therefore starts from an
empty `pending3D_` and correctly self-flushes on its own polling iterations.

### C++ correctness — PASS
`ScenarioResult` is a small aggregate returned by value (no dangling references); `query` is a
`unique_ptr<OcclusionQuery>` scoped to each `RunScenario`/`RunMultiDrawScenario` call, destroyed
(and its Vulkan query pool destroyed via `~VulkanOcclusionQueryBackend()`) at the end of each —
no lifetime issue across the three scenarios sharing one `GraphicsDevice`.

### Robustness — PASS
`colourMatch()`'s `tol=40` (line 53) is generous enough to tolerate ordinary blend/AA rounding
without being so loose it would mask a genuinely wrong colour (Red vs Blue vs Black are far enough
apart in RGB space that a 40-tolerance box around one can't overlap another). The
`kMaxPollFrames=30` cap prevents an infinite loop if the query pool never signals completion,
converting that failure mode into an ordinary `complete=false` test FAIL rather than a hang.

### Testing — PASS
All four scalar assertions (`visibleOk`, `visibleCountOk`, `occludedOk`, `occludedCountOk`) plus
the two multi-draw assertions are independently meaningful — none is redundant with another (e.g.
`visibleCountOk` tests the property API's numeric correctness while `visibleOk` tests the
independent visual/depth-test correctness of the same scenario).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. Two minor observations below (LOW/INFO), not defects.

### F1 — `RunScenario`'s "frame" numbering is a misnomer relative to real game frames

- Severity: LOW
- Confidence: HIGH
- Category: maintainability
- Location/symbol: `RunScenario()`/`RunMultiDrawScenario()`, the `int frame` counter and
  `if (frame == 0)` branches (lines 101, 118, 150, 170)
- Evidence: as traced under "Logic" above, all iterations of the `while` loop happen inside a
  single call to `Game::Draw()`; `frame` is really an internal polling-iteration counter, not an
  XNA game-loop frame number, even though `dev.Clear()` is called every iteration in a way that
  reads like a per-frame re-render.
- Why it matters: purely a readability/naming concern for future maintainers of this test family
  — the actual behaviour is correct (verified above), but a reader unfamiliar with the Vulkan
  backend's deferred-recording model could mistake this for "N real Present() cycles" when it is
  actually "N pending-draw accumulations flushed together, then N-1 further single-flush polls."
- Suggested action (not implemented by this audit): rename `frame` to `iter`/`pollIter` in a
  future pass, or add a one-line comment noting this is not one Present()-cycle per iteration.

## Cross-File Observations

- This file's `DrawQuad`/`DrawHalfQuad` helpers both explicitly set
  `RasterizerState::CullNone` (lines 68, 83) — consistent with the Task 896 finding documented in
  sibling files in this shard (`vulkan_render_target_usage_test.cpp`,
  `vulkan_rendertarget_depthformat_fidelity_test.cpp`): this test family's standard NDC quad
  winding is back-facing under CNA's real default `RasterizerState`, and every file in the family
  correctly works around it rather than silently relying on a permissive default.
- `BasicEffect`'s `World`/`View`/`Projection` are left at their CNA-side default
  (`Matrix::getIdentityProperty()`, `include/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp`
  lines 41/43/45) rather than explicitly set in this file — a deliberate, project-wide convenience
  default (documented elsewhere in this codebase's test family) that lets these hand-authored NDC
  quads render directly without an extra `setViewProperty`/`setProjectionProperty` call; not a
  defect specific to this file.

## Missing or Weak Tests

None found for the specific `PixelCount()`/`IsComplete()` correlation surface this file targets.
A residual, out-of-scope gap noted for completeness: no Vulkan test in this shard exercises
`OcclusionQuery::Dispose()`/destruction while a query is still `Begin()`-active (i.e., `End()`
never called) — a plausible XNA usage-error path FNA's own `OcclusionQuery.Dispose()` handles via
`FNA3D_AddDisposeQuery`. Not a defect in this file (it is out of this file's own stated scope), but
worth flagging for a future lifecycle-focused OcclusionQuery test.

## Positive Findings

- The multi-draw-span scenario (Scenario C) is a genuinely well-designed differential test: its
  expected value (`4096`) is specifically chosen so a "only the last draw counts" bug would
  produce a distinguishable wrong answer (`~2048`), not just an ambiguous near-miss.
- The occluded-quad scenario doesn't only check `PixelCount()==0` — it independently verifies the
  *visual* result (occluder's own colour visible) via `colourMatch`, so a hypothetical backend bug
  that reported `PixelCount()==0` for the wrong reason (e.g. a broken query pool always reading 0)
  would still be caught by the colour assertion diverging from expectations in a different way if
  the depth test itself were broken.
- The header comment's own claim that the Vulkan `OcclusionQueryBackend::Begin()/End()` was
  "previously entirely-stubbed" is corroborated by `git log` (`dd6ed767 fix(Task 447/854):
  implement real per-draw-call OcclusionQuery correlation on Vulkan`), not an unverifiable
  assertion.

## Final Assessment

A well-constructed, three-scenario differential test whose numeric expectations were independently
re-derived against the actual Vulkan command-recording and depth-test logic in this audit, not
just taken on faith from the file's own comments. No correctness issues found.
