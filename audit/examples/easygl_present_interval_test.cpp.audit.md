# Audit: examples/easygl_present_interval_test.cpp

## Metadata

- Source file: `examples/easygl_present_interval_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test — `examples-tests-easygl` shard
- File type: C++ example/integration-test executable (Task 223)
- Related production code: `Microsoft::Xna::Framework::GraphicsDeviceManager` (`src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp`),
  `Microsoft::Xna::Framework::Graphics::GraphicsDevice::SetPresentationParameters`/`Reset` (`src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`),
  `EasyGLGraphicsBackend::SetSwapInterval` (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:1589-1593`)
- XNA/FNA relevance: `PresentInterval`/`PresentationParameters.PresentationInterval` are real XNA 4.0 API surface; no
  FNA `GraphicsDeviceManager.cs`-equivalent behavior differs materially from what's exercised here.
- Main related tests: this file itself is the only test exercising the `PresentInterval` round-trip path end to end.

## Purpose

`PresentIntervalTest : Game` is run twice from `main()` — once with `SynchronizeWithVerticalRetrace=true`, once
`false` — and for each run checks (1) that `GraphicsDeviceManager`'s sync-flag correctly seeds the device's initial
`PresentationParameters.PresentationInterval`, and (2) that `GraphicsDevice::SetPresentationParameters()` accepts
all four `PresentInterval` values (`Immediate`, `One`, `Two`, `Default`) at runtime without throwing, and that each
one round-trips through the getter unchanged. Placement (`examples/`, EasyGL-backend-flavored filename) is correct
per `AUDIT_SCOPE.md`'s example-sharding rules.

## Executive Verdict

**Mostly healthy** — the two behaviors it actually exercises (GDM→PresentInterval seeding, and
`SetPresentationParameters` round-trip → `backend_->SetSwapInterval`) are both real and verified correct against
the production code. However, the file's own header comment inside `Initialize()` misdescribes the actual
`GraphicsDeviceManager` control flow (Finding F1) — a documentation-only defect, not a test-logic bug, but exactly
the kind of "confidently wrong rationale" this audit is meant to catch.

## Checklist Results

### API / XNA / FNA parity
`PresentInterval` (`Default`/`One`/`Two`/`Immediate`) and `PresentationParameters.PresentationInterval` are real
XNA/FNA members; `GraphicsDeviceManager.SynchronizeWithVerticalRetrace` is real XNA/FNA API. PASS.

### Behavioral correctness
Traced the full call chain: `GraphicsDeviceManager::INTERNAL_CreateGraphicsDeviceInformation`
(`GraphicsDeviceManager.cpp:486-488`) sets `pp.setPresentationIntervalProperty(synchronizeWithVerticalRetrace_ ?
PresentInterval::One : PresentInterval::Immediate)` — exactly matching the test's `expected` computation at
`Draw()`. `GraphicsDevice::SetPresentationParameters` (`GraphicsDevice.cpp:1339-1344`) does
`presentationParameters_ = pp; backend_->SetSwapInterval(toSwapInterval(pp.getPresentationIntervalProperty()));` —
a real field assignment (not a translated/lossy copy), so `setAndCheck()`'s round-trip check
(`dev.getPresentationParametersProperty().getPresentationIntervalProperty() == pi`) is a genuine, non-trivial
verification, not a tautology — it depends on `SetPresentationParameters` actually replacing the stored parameters
wholesale (confirmed) rather than, say, silently clamping `Two`/`Default` to `One`. PASS.

### Logic
See **Finding F1**: the `Initialize()` override's comment ("GDM::ApplyChanges() was already called in GDM ctor")
does not match the actual constructor behavior.

### Robustness
`setAndCheck()` correctly asserts only "does not crash and round-trips" for `Two`/`Default`/`Immediate` — it does
not (and per the file's own header comment, deliberately cannot) verify actual VSync timing headlessly. This is an
honest, appropriately-scoped claim.

### Testing
This is itself a test file; see Missing/Weak Tests below for gaps in its own coverage.

### Maintainability
Small, focused, single-purpose file (105 lines). No dead code, no magic numbers beyond the two well-named
`PresentInterval` constants under test.

## Detailed Findings

### F1 — `Initialize()`'s comment misattributes the initial `ApplyChanges()` call to the GDM constructor

- Severity: LOW
- Confidence: HIGH (traced both constructors and `Game::DoInitialize()` directly)
- Category: maintainability / correctness-of-documentation
- Location/symbol: `PresentIntervalTest::Initialize()`, lines 49-56
- Evidence: The comment reads "GDM::ApplyChanges() was already called in GDM ctor (with default sync=true)... 
  Explicitly call ApplyChanges() so the new interval takes effect before Draw()." But
  `GraphicsDeviceManager::GraphicsDeviceManager(Game*)` (`GraphicsDeviceManager.cpp:59-84`) explicitly does **not**
  call `ApplyChanges()` — its own comment states "Deliberately NOT calling ApplyChanges() here... Game::DoInitialize()
  unconditionally calls CreateDevice() shortly after this constructor returns." Tracing further,
  `Game::DoInitialize()` (`Game.cpp:644-662`) calls `graphicsDeviceManager_->CreateDevice()` *before* invoking the
  virtual `Initialize()` override, and `CreateDevice()` itself ends with `prefsChanged_ = false`
  (`GraphicsDeviceManager.cpp:311`). Since the test's own ctor calls
  `gdm_->setSynchronizeWithVerticalRetraceProperty(syncVRetrace_)` *before* `Run()`/`DoInitialize()` ever executes,
  `CreateDevice()` already applies the correct, final `syncVRetrace_` value — meaning by the time the test's
  `Initialize()` override runs `gdm_->ApplyChanges()`, `prefsChanged_` is already `false` and `graphicsDevice_` is
  already non-null, so `ApplyChanges()`'s own early-return guard (`GraphicsDeviceManager.cpp:214-217`,
  `!prefsChanged_ && !useResizedBackBuffer_`) makes this call a genuine no-op.
- Why it matters: the check that follows ("Initial PresentInterval matches SynchronizeWithVerticalRetrace setting")
  does pass, and does exercise real production logic — but it does so via `CreateDevice()`, not via the
  `ApplyChanges()` call the comment credits, and not because of anything that happened "in the GDM ctor" (which
  explicitly does the opposite). A future maintainer reading only this comment could reasonably conclude
  `ApplyChanges()` needs to be called here for correctness, when in fact removing that line would not change the
  test's outcome at all — the misleading rationale could lead to an incorrect "fix" being applied to a *different*
  file under the mistaken belief that GDM's constructor calls `ApplyChanges()`.
- FNA/XNA comparison: N/A (comment-only issue).
- Suggested action (not implemented by this audit): correct the comment to say the initial mapping is applied via
  `Game::DoInitialize()`'s `CreateDevice()` call (which runs before this `Initialize()` override), and that the
  `gdm_->ApplyChanges()` call here is currently a no-op / vestigial.

## Cross-File Observations

- This file and `easygl_presentation_parameters_test.cpp` both rely on the same
  `INTERNAL_CreateGraphicsDeviceInformation` mapping table; between the two files, `BackBufferWidth/Height`,
  `DepthStencilFormat`, `PresentInterval`, and `MultiSampleCount` are all cross-verified in this shard.
- `PresentInterval::Two`'s runtime round-trip (`setAndCheck(dev, PresentInterval::Two, "Two")`) maps to
  `SDL_GL_SetSwapInterval(2)` (`toSwapInterval` in `GraphicsDevice.cpp:61-69`, `EasyGLGraphicsBackend::SetSwapInterval`);
  this is a real GL/GLX extension request ("swap every 2 vblanks") that this test only confirms is *accepted*
  (doesn't crash and getter round-trips), not that the display actually halves its update rate — consistent with the
  file's own stated headless limitation.

## Missing or Weak Tests

- No check that `PresentInterval::Default` is treated identically to `One` end-to-end (i.e., that
  `toSwapInterval(Default)` also resolves to swap-interval `1`) — the round-trip check only proves the *stored*
  enum value survives, not that `Default` and `One` produce the same backend call. `toSwapInterval`'s `default:`
  case does map both to `1` (`GraphicsDevice.cpp:67`), but this test does not assert that behavioral equivalence
  directly (e.g. via a way to observe the last swap-interval value passed to the backend).
- No negative-input case (e.g. an out-of-range `PresentInterval` cast) — unlike the sibling
  `easygl_primitivetype_validation_test.cpp`, which does test its enum's `default:` throw path.

## Positive Findings

- Real, non-trivial verification of the `SynchronizeWithVerticalRetrace` → `PresentInterval` mapping across both
  boolean states (`true`/`false`), not just one — this is exactly the "contrast" testing style the audit values
  (a test using only `true` could not distinguish "correctly follows the flag" from "hardcoded to One").
- `setAndCheck()`'s round-trip assertion is grounded in the real `SetPresentationParameters` implementation
  (verified: a full parameter-object replacement, not a partial/lossy update).

## Final Assessment

A small, genuinely evidence-based test whose core assertions are correct and verified against the real
`GraphicsDeviceManager`/`GraphicsDevice` control flow. Its only defect is a stale/incorrect code comment describing
*why* the test works, which the audit is flagging as a documentation-accuracy issue (F1) rather than a functional
one — the test's assertions are unaffected and still exercise genuine production behavior.
