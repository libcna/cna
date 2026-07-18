# Audit: examples/easygl_presentation_parameters_test.cpp

## Metadata

- Source file: `examples/easygl_presentation_parameters_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test — `examples-tests-easygl` shard
- File type: C++ example/integration-test executable (Task 182)
- Related production code: `GraphicsDeviceManager::INTERNAL_CreateGraphicsDeviceInformation`
  (`src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp:448-499`), `GraphicsDeviceManager::ApplyChanges`/`CreateDevice`
  /`applyToExistingBackend` (same file), `Microsoft::Xna::Framework::Graphics::PresentationParameters`
- XNA/FNA relevance: `PresentationParameters.BackBufferWidth/Height/DepthStencilFormat/PresentationInterval/
  MultiSampleCount` and `GraphicsDeviceManager.PreferredBackBufferWidth/Height/PreferredDepthStencilFormat/
  SynchronizeWithVerticalRetrace/PreferMultiSampling` are all real XNA 4.0 API surface.

## Purpose

`PresentationParametersTest : Game` constructs a `GraphicsDeviceManager`, sets five preferred values in its own
constructor (`BackBufferWidth=320`, `Height=240`, `DepthStencilFormat=Depth24`, `SynchronizeWithVerticalRetrace=false`,
`PreferMultiSampling=false`), calls `ApplyChanges()` immediately, then in its `Initialize()` override checks that the
live `GraphicsDevice`'s `PresentationParameters` reflects all five requested values. This directly exercises the
"preferred settings propagate to the real device" contract that is the entire reason `GraphicsDeviceManager` exists
in XNA/FNA.

## Executive Verdict

**Mostly healthy** — every one of the five field checks is grounded in a real, traced mapping in
`INTERNAL_CreateGraphicsDeviceInformation`/`applyToExistingBackend`, not a tautological getter/setter check. The one
real defect found (F1) is a wasteful-but-harmless double device-reset at test startup, caused by the test calling
`ApplyChanges()` explicitly from its own constructor — the exact anti-pattern the project's own `GraphicsDeviceManager`
constructor comment says it deliberately avoids for every other `Game`.

## Checklist Results

### API / XNA / FNA parity
All five properties under test (`BackBufferWidth`, `BackBufferHeight`, `DepthStencilFormat`, `PresentationInterval`,
`MultiSampleCount` on `PresentationParameters`; the five matching `GraphicsDeviceManager` preferred-setting
properties) are real XNA 4.0 members with matching names/semantics. PASS.

### Behavioral correctness
Traced `INTERNAL_CreateGraphicsDeviceInformation` (`GraphicsDeviceManager.cpp:448-499`):
- `pp.setBackBufferWidthProperty(preferredBackBufferWidth_)` / height — direct passthrough when
  `!supportsOrientations_` (true on desktop Linux per `platformSupportsOrientations()`, `GraphicsDeviceManager.cpp:23-32`,
  which only returns true for iOS/Android) — matches the test's `kW=320`/`kH=240` check.
- `pp.setDepthStencilFormatProperty(preferredDepthStencilFormat_)` — direct passthrough, matches `Depth24` check.
- `pp.setPresentationIntervalProperty(synchronizeWithVerticalRetrace_ ? PresentInterval::One :
  PresentInterval::Immediate)` — with `synchronizeWithVerticalRetrace_=false` (test explicitly sets this), resolves
  to `Immediate`, matching the test's expectation.
- `if (!preferMultiSampling_) pp.setMultiSampleCountProperty(0);` — with `preferMultiSampling_=false` (test
  explicitly sets this), resolves to `0`, matching the test's expectation.
All five checks are real, non-tautological verifications of production mapping logic. PASS.

### Logic
See **Finding F1** below — a genuine, evidence-traced double-application of device settings at test startup.

### Performance
F1 (see Detailed Findings) — one avoidable extra `GraphicsDevice::Reset()`/backend reconfiguration cycle at process
startup. One-time cost only (not a per-frame/hot-path issue), so rated LOW despite being a real, traceable waste.

### Testing
This file itself is the primary/only direct test of the five-field GDM→PresentationParameters propagation path in
this shard (complementary to `easygl_present_interval_test.cpp`, which covers the `PresentInterval` runtime
round-trip specifically).

## Detailed Findings

### F1 — Test constructor calls `ApplyChanges()` directly, duplicating the reset `Game::DoInitialize()` performs moments later

- Severity: LOW
- Confidence: HIGH (traced constructor ordering and `ApplyChanges()`'s guard condition directly)
- Category: performance / consistency-with-established-pattern
- Location/symbol: `PresentationParametersTest::PresentationParametersTest()` (lines 69-78, specifically the final
  `gdm_->ApplyChanges();` call); compare `GraphicsDeviceManager::GraphicsDeviceManager(Game*)` constructor comment
  (`GraphicsDeviceManager.cpp:72-83`)
- Evidence: `GraphicsDeviceManager(Game*)`'s own constructor explicitly documents *not* calling `ApplyChanges()`
  itself, specifically because "`Game::DoInitialize()` unconditionally calls `CreateDevice()` shortly after this
  constructor returns — so calling `ApplyChanges()` here just did the exact same backend reconfiguration
  (`SetPresentationMode` + `Reset`) twice in a row... causing a visible double-reconfiguration flicker on startup."
  This test's own derived-`Game` constructor calls `gdm_->ApplyChanges()` directly (after setting five preferred
  properties), which — since `graphicsDevice_` is already non-null at that point (the GDM ctor grabs it from
  `game_->getGraphicsDeviceProperty()`) and `prefsChanged_` is `true` (set by the property setters just called) —
  actually executes the full `applyToExistingBackend(gdi)` path (`GraphicsDeviceManager.cpp:206-246`), not an
  early-return no-op. Later, when `game.Run()` is called, `Game::DoInitialize()` calls
  `graphicsDeviceManager_->CreateDevice()` (`Game.cpp:648-651`) on the same GDM instance, which re-derives an
  identical `GraphicsDeviceInformation` from the same (unchanged) preferred-* fields and calls
  `applyToExistingBackend(gdi)` a second time.
- Why it matters: the final observed state is unaffected (both applications produce identical `PresentationParameters`,
  so the test's own checks still pass and still validate real mapping logic) — but the test pays for two full
  `GraphicsDevice::Reset()` cycles (window resize/fullscreen-toggle path, `SetGraphicsProfileEXT`,
  `SetPresentationMode`, backend `Reset`) at startup instead of one, which is exactly the wasted work / visible
  flicker the framework's own constructor comment was written to prevent for every *other* test in this shard (none
  of the other 7 files in this batch call `ApplyChanges()` from their own constructor).
- FNA/XNA comparison: N/A — this is a CNA-internal device-lifecycle efficiency concern, not an XNA API-surface
  question (real FNA constructs its `GraphicsDevice` lazily inside `GraphicsDeviceManager` itself, so the double-init
  hazard doesn't arise the same way there).
- Suggested action (not implemented by this audit): remove the explicit `gdm_->ApplyChanges()` call from the
  constructor and rely on `Game::DoInitialize()`'s automatic `CreateDevice()` call, matching the pattern used by
  every sibling file in this shard (e.g. `easygl_present_interval_test.cpp`, `easygl_render_target_usage_test.cpp`).

## Cross-File Observations

- `easygl_present_interval_test.cpp` exercises the same `INTERNAL_CreateGraphicsDeviceInformation`
  `PresentInterval` mapping line (`GraphicsDeviceManager.cpp:486-488`) from the opposite angle (both boolean states
  of `SynchronizeWithVerticalRetrace`, via `CreateDevice()` rather than an explicit constructor-time
  `ApplyChanges()`) — between the two files the mapping is well covered, but only this file additionally covers
  `BackBufferWidth/Height`, `DepthStencilFormat`, and `MultiSampleCount`.

## Missing or Weak Tests

- No check of the `supportsOrientations_` branch (`GraphicsDeviceManager.cpp:460-480`, the portrait/landscape
  min/max swap used on iOS/Android) — reasonably out of scope for an EasyGL-desktop-only shard, but worth noting as
  an FNA-parity gap nobody in this shard covers.
- No check that a *second* `ApplyChanges()` call with an unchanged preferred-value set is a true no-op (would have
  caught F1 directly via an observable side effect, e.g. a `DeviceReset` event-fire counter).

## Positive Findings

- All five field checks are grounded in directly-traced production mapping code, not assumed from the file's own
  docstring — genuinely verifies GDM→PresentationParameters propagation end to end.
- Correctly chose `SynchronizeWithVerticalRetrace=false`/`PreferMultiSampling=false` (both non-default values) rather
  than leaving GDM's own defaults in place, so the test can actually distinguish "GDM's mapping was applied" from
  "these fields just happened to already be at that value."

## Final Assessment

A genuinely evidence-based propagation test whose five assertions are all correctly traced to real mapping logic in
`GraphicsDeviceManager`. Its only defect is a self-inflicted, harmless-but-wasteful double device-reset at startup
(F1) that goes against an established, explicitly-documented project convention — worth a one-line fix if this file
is touched again, but not a correctness risk today.
