# Audit: examples/occlusion_query_test.cpp

## Metadata

- Source file: `examples/occlusion_query_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard by file content (no backend-specific include, uses only
  `Game`/`GraphicsDevice`/`OcclusionQuery`), but **the file is actually EasyGL-only in practice** —
  registered exclusively in `cmake/Tests/EasyGLTests.cmake:205-208`
  (`cna_easygl_test(cna_test_occlusion_query examples/occlusion_query_test.cpp)` →
  `EasyGL_OcclusionQuery_Cycle`). Corroborated independently by `cmake/Tests/BgfxTests.cmake:874`'s
  own comment, which explicitly distinguishes its own Bgfx-specific dispose test from *"Task 449's
  own EasyGL-only `occlusion_query_test.cpp`"* — the project's own build system documents this
  file's backend scope in its own words. Unlike most single-backend files in this codebase, it does
  **not** carry an `easygl_` filename prefix, which could otherwise mislead a reader of the
  `examples/` directory listing alone.
- File type: standalone `Game`-subclass executable (all logic runs inside `Initialize()`, not
  `Draw()`), CTest-registered.
- XNA/FNA relevance: direct — `OcclusionQuery.Begin()`/`End()`/`IsComplete`/`PixelCount` (FNA
  `OcclusionQuery.cs`).
- FNA reference: `OcclusionQuery.cs` — confirmed (per this file's own comment, corroborated by
  independent reading of the current CNA implementation below) that FNA's `Begin()`/`End()` are
  pure one-line forwards to `FNA3D_QueryBegin`/`FNA3D_QueryEnd` with **no** C#-level call-sequence
  validation.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/OcclusionQuery.cpp` (full file,
  39 lines), `include/Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp`.

## Purpose

Integration test for `OcclusionQuery`'s full lifecycle: normal `Begin()`/`End()` cycle plus 3
invalid-call-sequence scenarios (Tasks 442-444: `End()` before `Begin()`; double `Begin()`; double
`End()`) and a 50-iteration destroy-while-active stress test (Task 449) checking that
`GetTrackedResourceCount()` returns to baseline afterward and that the device/backend remains
healthy for a subsequent normal query.

## Executive Verdict

**Healthy** — this audit independently read the full production `OcclusionQuery.cpp` and confirmed
every one of this file's claims about CNA's lack of call-sequence validation matches the actual
current implementation exactly (pure pass-through to the backend, no state machine at all), so the
"this is not a bug, it correctly mirrors FNA's own unvalidated shape" framing in the file's header
comment is verified true, not merely asserted. No defects found; one minor observation about a
self-defeating assertion pattern repeated 4 times (F1, LOW).

## Checklist Results

### Purpose
Correctly placed; despite its generic filename this is a single-backend (EasyGL) integration test
by CMake registration — noted above, not a defect, just a scope clarification per this batch's own
instructions.

### API / XNA / FNA parity
`OcclusionQuery(GraphicsDevice&)` constructor, `Begin()`, `End()`, `getIsCompleteProperty()`,
`getPixelCountProperty()` all match the real FNA `OcclusionQuery` surface (property-style C#
getters correctly mapped per this project's `getXProperty()` convention). `GetTypeName()` check
(`"Microsoft.Xna.Framework.Graphics.OcclusionQuery"`) matches the fully-qualified .NET name
requirement from `CLAUDE.md`/`CHECKLIST.md`.

### Behavioral correctness
Independently read `OcclusionQuery.cpp` in full:
```cpp
void OcclusionQuery::Begin() { if (backend_) backend_->Begin(); }
void OcclusionQuery::End()   { if (backend_) backend_->End(); }
```
— confirmed these are pure one-line conditional forwards with **zero** state tracking (no "already
begun" flag, no exception on out-of-sequence calls), exactly matching this test's own claim about
FNA's real `OcclusionQuery.cs` shape. All 3 invalid-sequence checks (Tasks 442/443/444) correctly
assert `!threw` rather than expecting an exception — the right assertion given the verified absence
of any validation to throw from.

### Memory/resource lifetime
`OcclusionQuery::backend_` is a `std::unique_ptr<IOcclusionQueryBackend>`
(`OcclusionQuery.hpp:54`), so destroying an `OcclusionQuery` (via `activeQuery.reset()` in the Task
449 stress loop) deterministically destroys the backend object through its virtual destructor —
this is what actually makes "destroy while active" safe, not merely an assumption. The test's own
comment about EasyGL's `easygl::Query` RAII member unconditionally calling `glDeleteQueries` (safe
per the GL spec even for an active query, since deletion is deferred internally) is consistent with
this ownership chain.

### Robustness
The Task 449 stress loop's use of `device.GetTrackedResourceCount()` returning to baseline after 50
create/Begin/destroy-without-End cycles is a genuinely meaningful check — it would catch a leak in
`GraphicsResource`'s device-side registration/unregistration bookkeeping specifically for the
"disposed while active" path, which a simple "no crash" check would not.

### Testing
See F1 for a minor pattern note; otherwise this file's structure (isolate each invalid sequence in
its own nested scope with its own fresh `OcclusionQuery`) is a clean way to avoid cross-
contaminating state between the 4 sub-scenarios.

### Cross-file consistency
Consistent with `graphicsdevice_default_state_occlusion_test.cpp`'s and other files' shared use of
`GetTrackedResourceCount()` as a NOXNA leak-detection convention
(`GraphicsDevice.hpp:671`: `NOXNA [[nodiscard]] std::size_t GetTrackedResourceCount() const { return
resources_.size(); }` — confirmed present and correctly tagged).

## Detailed Findings

### F1 — Several "does not crash" checks are unconditional no-op assertions that cannot themselves fail

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage / weak-assertion pattern
- Location/symbol: `check(true, "getIsCompleteProperty() does not crash")` and its 3 repetitions
  (lines 88, 106, 127, 149), each immediately preceded by `bool complete = q.getIsCompleteProperty();
  (void)complete;`.
- Evidence: `check(bool cond, …)` only records a failure when `cond` is false; `check(true, …)` can
  never fail by construction. The actual "does it crash" property being described is really tested
  by *reaching* that line at all (a real crash would abort the process before `check()` runs, not
  cause `check()` to receive `false`) — so these 4 lines document intent in the console/log output
  but add no assertion power beyond what unconditional control-flow already provides.
- Why it matters: low impact — this is an intentional, easy-to-read idiom (also seen used similarly
  elsewhere in this codebase) rather than a mistake, and a genuine crash would still fail the test
  via process abort/non-zero-without-the-final-print rather than a silent pass. Flagged only because
  a future refactor might mistake these for meaningful behavioral assertions when they are actually
  just narration.
- FNA/XNA comparison: N/A — test-authoring style observation only.
- Related files: none.
- Suggested action (not implemented by this audit): none required; if tightened, these could assert
  something about the *value* returned (e.g. that it's a plain `bool`, already guaranteed by the
  type system) or simply be left as documentation-via-`check(true, …)`, which is a defensible choice
  for "this call must not crash" style coverage.

## Cross-File Observations

- This file's header comment's own framing of Task 441 ("FNA's real `OcclusionQuery.cs` has ZERO
  C#-level validation of Begin/End call sequence… correcting this task's own stale Notes-column
  framing") is a good example of a test file explicitly documenting and correcting a prior
  mis-framed task expectation rather than silently working around it — this audit's independent
  read of the current CNA `OcclusionQuery.cpp` confirms the corrected framing (no validation to
  match) is accurate for the current implementation.
- Distinct sibling coverage exists per-backend for the same lifecycle concerns (e.g.
  `cmake/Tests/BgfxTests.cmake`'s `Bgfx_OcclusionQuery_DisposeActive`,
  `Bgfx_OcclusionQuery_PixelCount`; `cmake/Tests/VulkanTests.cmake`'s
  `Vulkan_OcclusionQuery_PixelCount`; `cmake/Tests/SdlRendererTests.cmake`'s
  `SDL_Renderer_OcclusionQueryThrows` for the backend that doesn't support 3D queries at all) — this
  file is correctly scoped to be the EasyGL-specific member of that family rather than a duplicate.

## Missing or Weak Tests

- See F1 (minor, not a real gap).
- This file does not test `getPixelCountProperty()`'s actual numeric behavior against a known
  visible/occluded scene (e.g. drawing a quad and checking `PixelCount > 0` vs. drawing nothing and
  checking `PixelCount == 0`) — only that it returns a value `>= 0` in various call-sequence states.
  That deeper behavioral check does exist elsewhere in the project for other backends (e.g. Bgfx's
  `Bgfx_OcclusionQuery_PixelCount`, `bgfx_occlusion_query_test.cpp`'s comment referencing "visible/
  occluded pixel counts"), so this is not a total gap in the project, but this specific EasyGL file
  does not carry an equivalent visible-vs-occluded pixel-count assertion of its own.

## Positive Findings

- Every claim in this file's extensive header comment about FNA's real (lack of) validation
  behavior was independently verified against the current CNA production source, not merely taken
  on faith — and it holds up.
- The Task 449 stress test's use of `GetTrackedResourceCount()` returning to baseline is a
  meaningfully strong check for a "dispose while active" scenario, not just a "did it crash" check.
- Clean isolation of each invalid-sequence scenario into its own nested scope with its own
  `OcclusionQuery` instance avoids state leakage between checks.

## Final Assessment

A well-evidenced, currently-accurate integration test whose extensive self-documentation about
FNA's real (lack of) Begin/End validation was independently confirmed against the current
production `OcclusionQuery.cpp`. The only note is a cosmetic, intentional weak-assertion idiom
(F1) with no practical impact on test power.
