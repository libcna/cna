# Audit: examples/easygl_viewport_state_test.cpp

## Metadata

- Source file: `examples/easygl_viewport_state_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/test executable (Google-Test-free, hand-rolled `Game` subclass +
  `printf`-based PASS/FAIL harness), registered as CTest `EasyGL_ViewportState`
  (`cmake/Tests/EasyGLTests.cmake` line ~941), gated on `CNA_GRAPHICS_BACKEND STREQUAL "EASYGL"`
  and `CNA_BUILD_EXAMPLES`/`CNA_BUILD_TESTS`
- Related production code: `Microsoft::Xna::Framework::Graphics::GraphicsDevice::getViewportProperty`/
  `setViewportProperty` (`src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` lines 235-247),
  `CNA::Internal::Backends::EasyGL::EasyGLGraphicsBackend::SetViewport`
  (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` lines 2034-2053),
  `Microsoft::Xna::Framework::Graphics::Viewport` (`include/.../Viewport.hpp`)
- XNA/FNA relevance: exercises real `Microsoft::Xna` public API (`GraphicsDevice.Viewport`,
  `Viewport` ctor/properties, `Clear`, `DrawUserPrimitives`) — judged against
  `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Viewport.cs` and FNA's `GraphicsDevice`
  Clear/Viewport semantics.
- Main related tests: this file is itself a test; sibling unit test
  `tests/Microsoft/Xna/Framework/Graphics/ViewportTests.cpp` covers `Viewport`'s own GPU-agnostic
  methods (`Project`/`Unproject`/`AspectRatio`/`Bounds`/`ToString`) — confirms this file's scope
  (GPU-side wiring/persistence only) is intentional, not an accidental coverage gap.

## Purpose

Verifies "Task 208: GraphicsDevice viewport state" — that `GraphicsDevice::getViewportProperty()`/
`setViewportProperty()` round-trip correctly and, critically, that the stored `Viewport` value is
**not silently clobbered** by other device operations (`Clear()`, `DrawUserPrimitives()`) that have
no business touching it. This directly targets a category of bug the neighboring production code
comments (`GraphicsDevice.cpp` lines 1533-1539, 1555-1558) explicitly describe as previously real:
a naive implementation could reset `viewport_` to full-window size as a side effect of unrelated
calls. Placement under `examples/` with the `easygl_` prefix is correct per `AUDIT_SCOPE.md`'s
sharding rule (backend-named integration executable, gated at CMake-configure time on
`CNA_GRAPHICS_BACKEND=EASYGL` — the file itself contains no EasyGL-specific code, which is expected
and consistent with every other backend's equivalently-named test).

## Executive Verdict

**Healthy.** All seven checks are genuine, targeted assertions against real `Viewport`/
`GraphicsDevice` semantics (not "compiles and doesn't crash" filler), each one traceable to a
specific, previously-fixed defect class documented in the production code it exercises. One real,
if minor, gap: no preflight `ProbeGpuDisplayAvailable()` headless-guard (see F1), and the initial
Draw-timing assumption in check 1 isn't verified by an actual test run in this audit pass (no build
directory was available in this sandbox for this batch — see Testing).

## Checklist Results

### API / XNA / FNA parity
The `Viewport` surface exercised — `Viewport(int,int,int,int)` (MinDepth=0/MaxDepth=1 per ctor,
matches `include/.../Viewport.hpp` line 34 doc and FNA's `Viewport(int,int,int,int)` overload),
`getXProperty`/`getYProperty`/`getWidthProperty`/`getHeightProperty`/`getMinDepthProperty`/
`getMaxDepthProperty`, `setMinDepthProperty`/`setMaxDepthProperty` — matches FNA's
`Viewport.X`/`Y`/`Width`/`Height`/`MinDepth`/`MaxDepth` properties one-for-one (verified against
`FNA/src/Graphics/Viewport.cs`). `GraphicsDevice.getViewportProperty()`/`setViewportProperty()`
correctly map to FNA's `GraphicsDevice.Viewport` get/set property. `device.Clear(Color)` and
`device.Clear(ClearOptions, Color, float, int)` overloads both exist and match FNA's two `Clear`
overloads. `PrimitiveType::TriangleList` and `DrawUserPrimitives(PrimitiveType, T*, int, int)` match
FNA's `GraphicsDevice.DrawUserPrimitives<T>(PrimitiveType, T[], int, int)` shape (vertex data,
vertexOffset, primitiveCount).

### Behavioral correctness
- **Check 1** (lines 60-67): initial viewport equals full backbuffer size at (0,0). This is a
  real, non-trivial assertion — it fails if `GraphicsDevice`'s constructor/window-setup path (via
  `UpdateViewportFromWindow()`, `GraphicsDevice.cpp` lines 1513-1561) never runs before the first
  `Draw()`, or if it computes the wrong size. Traced: `UpdateViewportFromWindow()` sets
  `viewport_` fields directly to `(0,0,width,height,minDepth=0,maxDepth=1)` from
  `backend_->GetViewportSize()`/`SDL_GetWindowSize` fallback — consistent with what the test
  expects, assuming this runs before `Draw()` (plausible given `GraphicsDeviceManager` typically
  calls a `Reset`/`ApplyChanges` path before the first frame, but not independently re-verified by
  executing the binary in this pass).
- **Check 2** (lines 69-75): `Viewport custom(50,30,320,240)` + explicit `MinDepth=0.1`/`MaxDepth=0.9`
  round-trips through `set`/`get`. Directly exercises `GraphicsDevice::setViewportProperty` (line
  240-247), which stores into `viewport_` *and* forwards to `backend_->SetViewport(...)` — the test
  only observes the C++-side stored copy via `getViewportProperty()`, not the GPU-side `glViewport`
  call itself; that GPU-side effect is what `easygl_viewport_subregion_test.cpp` verifies via pixel
  readback (correct division of labor between the two files, not a gap in this one).
- **Checks 3-4** (lines 77-94): `Clear(Color)`, `Clear(ClearOptions|.., Color, depth, stencil)`, and
  `DrawUserPrimitives(...)` each individually re-checked against `custom` afterward. This is the
  file's core value proposition and is faithfully implemented: the production comment at
  `GraphicsDevice.cpp` lines 1555-1558 explicitly states `UpdateViewportFromWindow()` mutates
  `viewport_` fields directly rather than via `setViewportProperty()` specifically to avoid
  reintroducing this exact regression, and `EasyGLGraphicsBackend::Clear()`
  (`EasyGLGraphicsBackend.cpp` lines 1557-1566) has a matching "Task 880" comment confirming the
  historical bug this check guards against was real and backend-side.
  `try { ... } catch (...) {}` around `DrawUserPrimitives` (line 91-92) is a deliberate, reasonable
  choice given the draw uses an all-zero-initialized, degenerate `VertexPositionColor tri[3]{}` —
  the test's purpose is viewport-persistence, not asserting the draw itself succeeds, so swallowing
  any GPU-path exception here is correct scoping, not sloppy error handling.
- **Checks 5-6** (lines 96-107): second `setViewportProperty` overwrites the stored value
  (`vpEq(..., second)` true, `vpEq(..., custom)` false — both directions checked, a real
  affirmative+negative pair rather than only the positive case), then restoring `initial` proves
  `setViewportProperty` isn't a one-way/latching operation.
- **Check 7** (lines 109-117): `MinDepth`/`MaxDepth` specifically (0.25/0.75, values distinct from
  both the ctor defaults 0/1 and check 2's 0.1/0.9) survive independently of the other four fields —
  guards against a plausible copy-paste bug where only X/Y/Width/Height are copied in
  `setViewportProperty`/`getViewportProperty` and the depth range fields are dropped. Traced against
  `GraphicsDevice::setViewportProperty` (`viewport_ = value;`, a full struct copy) — correctly not
  reproducible today, so this is a regression guard rather than a currently-failing assertion.

### Logic
`vpEq()` (lines 29-37) is a genuine, non-trivial field-by-field comparator, not a rubber-stamp —
it correctly uses an epsilon (`1e-5f`) for the two float fields (`MinDepth`/`MaxDepth`) and exact
equality for the four int fields, appropriate given `Viewport` (like FNA's) has no `operator==`/
`Equals()` override of its own (confirmed: neither `Viewport.hpp` nor FNA's `Viewport.cs` defines
one — plain-struct value equality isn't part of the XNA contract here, so the test correctly
supplies its own rather than relying on a nonexistent API).

### C++ correctness
`VertexPositionColor tri[3]{}` value-initializes all three vertices to zero position/zero color —
well-formed, no UB; `BasicEffect fx(device); fx.Apply();` before the draw satisfies
`GraphicsDevice::DrawUserPrimitives`'s "no effect has been applied" guard (`GraphicsDevice.cpp` line
874), so the `catch (...)` is very unlikely to ever actually fire — it exists purely as a defensive
belt for this specific test's non-goal (draw success) rather than masking a real expected failure.
No lifetime issues: `gdm_` is a `unique_ptr` member constructed before `Run()`, matches every other
example in this shard.

### Memory/resource lifetime
Trivial/correct — single `GraphicsDeviceManager` owned by `unique_ptr`, standard `Game::Run()`
lifecycle, `Exit()` called exactly once after `done_` latches. No leaks, no double-teardown risk.

### Performance
N/A for a single-frame diagnostic test — not a hot path.

### Thread safety
N/A — single-threaded `Game` loop, main-thread only.

### Architecture
Correctly exercises the public `Microsoft::Xna::Framework` surface only, no direct backend/EasyGL
symbol references anywhere in the file — the EasyGL-specificity is entirely a build-configuration
concern (CMake gate), which is the intended architecture per `AUDIT_SCOPE.md`'s example-sharding
rule and matches every other backend-prefixed test file in this shard.

### Maintainability
Hand-rolled `Game` subclass + `check()`/`printf` harness duplicates the shape that
`examples/common/PixelTestGame.hpp` (Task 461) now provides — but this is **not** a defect: that
header's own top-of-file comment explicitly states retrofitting existing tests is out of scope, and
this file's checks aren't pixel-readback comparisons at all (they're getter/setter/state-persistence
assertions), which falls outside `PixelTestGame::ExpectPixel()`'s pixel-comparison-only design
anyway. Clearly commented, well-organized into seven numbered sections matching the file's own
top-of-file checklist comment (lines 4-12) — comment and implementation stay in lockstep, which is
good maintainability practice.

### Portability
`SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}` environment is set by the CMake test registration
itself (`cna_register_backend_test`), so the CI/CTest execution path is portable within this
project's Linux/X11 sandbox convention; no platform-conditional code in the file itself.

### Robustness
See F1 below — no headless-preflight guard, unlike ~22 other example files in the tree that use
`CNA::Examples::ProbeGpuDisplayAvailable()`.

### Testing
This *is* the test. Coverage of `GraphicsDevice.Viewport` get/set/persistence is genuinely
comprehensive for what it claims to cover (7 distinct scenarios, all real assertions per above); it
correctly leaves `Viewport`'s own non-GPU methods (`Project`/`Unproject`/`Bounds`/`TitleSafeArea`/
`AspectRatio`/`ToString`) to `tests/Microsoft/Xna/Framework/Graphics/ViewportTests.cpp`, and leaves
the actual GPU-side `glViewport` sub-region clipping effect to
`examples/easygl_viewport_subregion_test.cpp` — a well-divided three-way split with no gaps and no
redundant overlap found between the three.

### Cross-file consistency
Consistent with `GraphicsDevice.cpp`'s own in-code narrative of the Task 880 viewport-persistence
fix and with `EasyGLGraphicsBackend.cpp`'s matching comments — the test's checks map 1:1 onto the
exact defect classes those comments describe as previously real, which is strong evidence this test
was written *to* those fixes rather than independently/coincidentally.

## Detailed Findings

### F1 — No headless-display preflight guard before constructing the real `Game`

- Severity: LOW
- Confidence: HIGH
- Category: robustness
- Location/symbol: `main()` (lines 132-137) — constructs `ViewportStateTest` and calls `Run()`
  directly, with no `CNA::Examples::ProbeGpuDisplayAvailable()` check beforehand.
- Evidence: `examples/common/PixelTestGame.hpp`'s `RunPixelTest<TGame>()` (lines 272-292) documents
  that constructing a real `Game`/`GraphicsDevice` "throws a `std::runtime_error` out of
  `SDL_InitSubSystem(SDL_INIT_VIDEO)` if no GPU/display is available at all" and provides
  `ProbeGpuDisplayAvailable()` specifically so hand-rolled, non-`PixelTestGame`-shaped tests can
  skip cleanly (`kSkipExitCode` = 77) instead of crashing with an uncaught exception. This file does
  not call it.
- Why it matters: on a machine/CI runner with no X server and no Xvfb, this test would abort with an
  uncaught exception (reported as a hard CTest failure/crash) rather than a clean `SKIPPED` result —
  cosmetic for this project's own documented/registered CI environment (which always sets
  `DISPLAY=${CNA_TEST_DISPLAY}` per the CMake registration), but a real gap for any ad-hoc local
  `./cna_test_easygl_viewport_state` invocation without Xvfb running.
- FNA/XNA comparison: N/A (CNA-internal test infrastructure, not an XNA API concern).
- Related files: `examples/common/PixelTestGame.hpp` (`ProbeGpuDisplayAvailable`, lines 253-263).
- Scope note: this is a **systemic pattern, not unique to this file** — only ~22 of ~797 files under
  `examples/` use this preflight (confirmed via `grep -rl ProbeGpuDisplayAvailable examples/*.cpp`),
  including immediate siblings like `easygl_scissor_test.cpp`. Recording it here per-file as
  instructed, but it is not evidence of a defect specific to this test's authorship — it predates
  Task 470's introduction of the helper and was never backfilled, matching the same
  no-retrofit policy `PixelTestGame.hpp` documents for its own pixel-comparison helpers.

## Cross-File Observations

- The three-way test split (this file: get/set persistence; `easygl_viewport_subregion_test.cpp`:
  GPU-side sub-region clipping; `ViewportTests.cpp`: `Viewport`'s own GPU-agnostic math methods) is
  a clean, deliberate division of responsibility with no detected gap or redundant duplication.
- Asymmetric backend coverage: only EasyGL has a "basic viewport state persists across Clear/Draw"
  test (`cmake/Tests/EasyGLTests.cmake` line 941); Vulkan and Bgfx only got the Task 880 sub-region
  test, not an equivalent of this file's Task 208 checks (confirmed via
  `find examples -iname '*viewport*'` — no `vulkan_viewport_state_test.cpp` or
  `bgfx_viewport_state_test.cpp` exist). Worth flagging for the `examples-tests-vulkan`/
  `examples-tests-bgfx` shard audits as a coverage-parity gap, not a defect in this file itself.

## Missing or Weak Tests

- No test in this file (or found elsewhere in this shard) verifies that `setViewportProperty()`
  actually calls `backend_->SetViewport(...)` with the exact values passed (i.e., a mock/spy on the
  backend boundary) — the C++-side persistence is well covered, but the GPU-call-argument-forwarding
  contract is only indirectly verified via the subregion test's pixel readback, which conflates
  "backend was called correctly" with "backend rendered correctly." Low priority given the subregion
  test already provides strong end-to-end evidence.
- No negative/edge-case check for degenerate `Viewport` values (zero or negative width/height) —
  reasonable to omit here since `EasyGLGraphicsBackend::SetViewport`'s own `if (w <= 0 || h <= 0)
  return;` guard (line 2037) is a backend-implementation-detail edge case better suited to a
  backend-focused test, not this GraphicsDevice-level state test.

## Positive Findings

- Every one of the seven checks is traceable to a specific, real, previously-fixed defect class in
  the production code it exercises (verified via matching "Task 880"/"Task 208" comments in
  `GraphicsDevice.cpp` and `EasyGLGraphicsBackend.cpp`) — this is exactly the kind of regression
  test that earns its keep, not boilerplate.
- Both a positive and negative assertion pair at check 5 (`vpEq(..., second)` true AND
  `!vpEq(..., custom)` false) — a more rigorous pattern than many single-direction equality checks
  seen elsewhere in this codebase.
- Clean division of test responsibility across three files (this one, the subregion test, and the
  unit-test file) with no detected overlap or gap.

## Final Assessment

A well-targeted, genuinely evidence-based regression test that correctly narrows its scope to
GraphicsDevice-level Viewport-state persistence and leaves GPU-side effect and pure-math coverage to
its sibling files. The only real (if minor and non-unique-to-this-file) gap is the missing headless
preflight guard (F1); the initial-viewport-timing assumption in check 1 is plausible from static
reading but was not independently confirmed by executing the binary in this audit pass.
