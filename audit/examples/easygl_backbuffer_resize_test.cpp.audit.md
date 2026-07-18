# Audit: examples/easygl_backbuffer_resize_test.cpp

## Metadata

- Source file: `examples/easygl_backbuffer_resize_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration test
- File type: C++ example/integration-test executable (`BackbufferResizeTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::GraphicsDeviceManager` (`GraphicsDeviceManager.cpp`/`.hpp`),
  `Microsoft::Xna::Framework::Graphics::GraphicsDevice::SetPresentationParameters`/`Reset`/`UpdateViewportFromWindow`
  (`GraphicsDevice.cpp`)
- XNA/FNA relevance: exercises `GraphicsDeviceManager.PreferredBackBufferWidth/Height` + `ApplyChanges()` and
  `GraphicsDevice.PresentationParameters`/`SetPresentationParameters()` — all real XNA 4.0 surface.
- FNA reference: no direct FNA analogue for the two-path distinction being tested (FNA's own back-buffer resize path
  is FNA3D-backed and does not have CNA's virtual-resolution/viewport layering), but the *properties* under test
  (`PreferredBackBufferWidth/Height`, `ApplyChanges`, `PresentationParameters`, `SetPresentationParameters`) are all
  real XNA members.
- Main related tests: this file itself (`Task 227`); no other example duplicates this specific GDM-vs-direct
  distinction.

## Purpose

Verifies that changing the back buffer size updates the right internal state along two different, deliberately
distinguished code paths in `GraphicsDeviceManager`/`GraphicsDevice`: (A) the standard XNA `GraphicsDeviceManager`
path (`setPreferredBackBufferWidth/HeightProperty()` + `ApplyChanges()`), which is expected to propagate all the way
to the backend's virtual resolution and the `Viewport`; and (B) `GraphicsDevice::SetPresentationParameters(pp)`
called directly, which per the file's own header comment (lines 9-11) is expected to update only the stored
`PresentationParameters`, forwarding just the swap interval to the backend. Placement under `examples/` as a
backend-named (`easygl_...`) integration-test executable matches the project's own `AUDIT_SCOPE.md` classification
for this population.

## Executive Verdict

**Mostly healthy** — the test's own claims about the two code paths are verified accurate against
`GraphicsDeviceManager.cpp`/`GraphicsDevice.cpp` (see Detailed Findings), but the test only *partially* proves its
own stated thesis: it never actually reads back the `Viewport`/backend state after the Direct path to confirm it
was *not* changed, despite that being exactly the property distinguishing path B from path A that the file exists to
demonstrate (Finding F1).

## Checklist Results

### API / XNA / FNA parity
The properties exercised (`PreferredBackBufferWidth`, `PreferredBackBufferHeight`, `ApplyChanges()`,
`PresentationParameters`, `SetPresentationParameters()`) are named and used correctly per XNA 4.0 conventions
(`getXProperty`/`setXProperty` CNA mapping). `PresentationParameters::Clone()` (line 80) matches FNA's
`PresentationParameters.Clone()` (a deep-copy factory), used correctly here to snapshot current PP before mutating
the clone and passing it to `SetPresentationParameters`.

### Behavioral correctness
Verified directly against production code:
- **GDM default 800×480**: `GraphicsDeviceManager.hpp` defines `DefaultBackBufferWidth = 800`,
  `DefaultBackBufferHeight = 480` (used to seed `preferredBackBufferWidth_`/`preferredBackBufferHeight_` in the
  constructor) — matches the test's own `checkDim(..., 800, "GDM default width")` / `480` assertions (lines 53-54).
- **GDM path propagates to virtual resolution + viewport**: `GraphicsDeviceManager::ApplyChanges()` →
  `applyToExistingBackend()` → `GraphicsDevice::Reset()` sets `virtualWidth_`/`virtualHeight_` from the PP
  (`GraphicsDevice.cpp:399-400`), calls `backend_->SetVirtualResolution()` (line 410), and ends with
  `UpdateViewportFromWindow()` (line 437) — this exactly matches the test's assertion that after
  `gdm_->ApplyChanges()` both the PP and the `Viewport` are updated (lines 61-66, 73-76).
- **`FixedHeightDynamicWidth` semantics**: `GraphicsDeviceManager`'s constructor defaults
  `preferredPresentationMode_(PresentationMode::FixedHeightDynamicWidth)` (`GraphicsDeviceManager.cpp:55`) — the
  test's comment "viewport HEIGHT = virtualHeight; WIDTH adapts to the physical window's aspect ratio" (lines 63-64)
  is a correct description of this mode given `UpdateViewportFromWindow()` derives both dimensions from
  `backend_->GetViewportSize()` (`GraphicsDevice.cpp:1513-1521`), which is where the backend's aspect-locking logic
  lives.
- **Direct path forwards only swap interval**: `GraphicsDevice::SetPresentationParameters()` (`GraphicsDevice.cpp:
  1339-1344`) is exactly two statements — `presentationParameters_ = pp;` and
  `backend_->SetSwapInterval(toSwapInterval(pp.getPresentationIntervalProperty()))` — no call to
  `SetVirtualResolution`, no call to `UpdateViewportFromWindow`. This precisely matches the test's own header claim
  (lines 9-11) and the code comment at line 79 ("Only the PP is updated; virtual resolution is not forwarded to
  backend").

All of the above are correctly-described, accurately-verified behavior — not guesswork by the test author.

### Logic
Single linear `Initialize()` override drives all checks (GDM default → resize to 640×360 → resize to 1280×720 →
direct-PP 320×240 → restore to 800×480), then `Draw()` prints the pass/fail tally and exits on the very first frame.
No branching/loop logic to verify beyond the straight-line sequence.

### Memory/resource lifetime
`gdm_` is a `std::unique_ptr<GraphicsDeviceManager>` constructed in the test's own constructor before `Game::Run()`
— ownership is clean, `BackbufferResizeTest` outlives it for the whole run, no dangling-pointer risk.

### C++ correctness
`checkDim`'s `snprintf` into a 256-byte stack buffer (lines 38-43) is safely bounded (`sizeof(buf)`), no overflow
risk for the short labels actually passed. No casts, no UB observed.

### Performance
N/A — single-frame test, no hot path.

### Architecture
Correctly uses the public `Microsoft::Xna::Framework`/`Graphics` XNA-facing API surface only; no direct backend
(`CNA::Internal::Backends`) coupling, appropriate for an examples-level black-box test.

### Robustness
No input validation needed (self-contained test, no external input). No exception-safety concern — no code path
here can throw given valid GDM/device state.

### Testing
This is itself a test. See Finding F1 for the coverage gap in what it actually asserts vs. what it documents.

## Detailed Findings

### F1 — The "Direct path leaves viewport/virtual-resolution unchanged" claim is documented but never asserted

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `BackbufferResizeTest::Initialize()`, Direct-path block, lines 78-91
- Evidence: The file's own top-of-file comment (lines 9-11) states the Direct path's defining property is: "PP
  dimensions updated — virtual resolution / viewport NOT changed (only swap interval is forwarded)." The test body
  for this block (lines 80-86) calls `checkDim` twice, both against `getPresentationParametersProperty()` (PP width
  320, height 240) — it never reads `dev.getViewportProperty()` (as the GDM-path blocks above it do, lines 65-66,
  75-76) to confirm the viewport dimensions are still the pre-Direct-path values (1280×720, the last GDM resize)
  rather than having been silently updated to 320×240.
- Why it matters: this is precisely the behavioral distinction the whole file exists to demonstrate (per its own
  task-227 header comment's two-path framing). If a future refactor accidentally made
  `SetPresentationParameters()` call `UpdateViewportFromWindow()` or `SetVirtualResolution()` (a very plausible
  future "fix" someone might add thinking it's an oversight), this test would keep passing — the PP-only assertions
  it currently has would still hold — while silently no longer testing the one property (`viewport untouched`) that
  makes path B interesting/different from path A.
- FNA/XNA comparison: N/A (CNA-specific virtual-resolution/viewport layering, no FNA equivalent).
- Related files: `Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (`SetPresentationParameters`,
  lines 1339-1344).
- Suggested future action (not implemented by this audit): add
  `checkDim(dev.getViewportProperty().getWidthProperty(), <last-GDM-width>, "Viewport unchanged after direct PP set")`
  (and the height equivalent) immediately after the Direct-path `SetPresentationParameters()` call, before the
  restore-to-defaults block.

## Cross-File Observations

- This file and `easygl_basiceffect_*_test.cpp` siblings in this batch all construct their `GraphicsDeviceManager`
  in the test class's own constructor and set preferred back-buffer dimensions there (or, for this file, rely on
  GDM's own 800×480 default) — consistent with the pattern documented in `GraphicsDeviceManager.cpp`'s constructor
  comment (lines 72-84) that `ApplyChanges()` must not be called from the constructor itself.
- Confirms (via reading `GraphicsDeviceManager.cpp:72-84`) that CNA's `Game` always pre-owns its `GraphicsDevice`
  unlike real FNA — a documented, intentional architectural deviation, not a defect, and consistent with what this
  test implicitly relies on (`gdm_->ApplyChanges()` reconfiguring an already-existing device rather than creating
  one from scratch).

## Missing or Weak Tests

- See F1 — the Direct path's "viewport NOT changed" claim needs its own explicit assertion.
- No test in this file (or, from this batch, anywhere else) exercises a *third* resize path relevant to production
  code: `GraphicsDevice::RecreateBackendForMultiSampleCount()` (`GraphicsDevice.cpp:1346-1352`), which also calls
  `UpdateViewportFromWindow()` after recreating the backend — out of scope for this specific file's Task 227 remit,
  but worth flagging for whichever shard covers MSAA/backend-recreation tests.

## Positive Findings

- Every dimension/behavior claim actually checked in this file was independently verified against the real
  `GraphicsDeviceManager.cpp`/`GraphicsDevice.cpp` production code during this audit and found accurate — this is a
  well-grounded, non-boilerplate integration test for the paths it does cover.
- The "restore to 800×480 via GDM" cleanup step (lines 88-91) is a considerate touch that keeps the game loop's
  final state sane for whatever runs after `Draw()`'s `Exit()`, even though it isn't itself asserted.

## Final Assessment

A genuine, accurate integration test for the GDM-vs-direct back-buffer resize distinction it documents, with one
concrete, fixable coverage gap: it proves the *positive* claims (GDM path does propagate) but never proves the
*negative* claim (direct path does not propagate to viewport) that is the entire point of contrasting the two paths.
