# Audit: examples/sdlrenderer_fullscreen_toggle_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_fullscreen_toggle_test.cpp` (123 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 712, fullscreen toggle round-trip
- File type: standalone `Game`-subclass executable (`SdlFullscreenToggleTest`), exit-code PASS/FAIL
- XNA/FNA relevance: `PresentationParameters.IsFullScreen`, `GraphicsDevice.Reset(PresentationParameters)`
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`applyPresentationParametersToWindow()`, lines 1606-1626+, called from `Reset()` at line 406)
- Git corroboration: `2ecf9dbc`/`d9b638b2` `verify(Task 712): fullscreen toggle round-trips through
  PresentationParameters`.

## Purpose

Verifies the XNA-level contract this project can actually guarantee for fullscreen toggling under a headless/Xvfb
test environment: `PresentationParameters.IsFullScreen` round-trips correctly through `Reset()` in both directions
(false→true→false), the toggle call never throws even if the underlying `SDL_SetWindowFullscreen` call itself fails
(non-fatal, per the Task 902 handling), and the device remains fully functional (a real draw+readback still
succeeds) after both toggles.

## Executive Verdict

**Healthy** — the file's own stated scope (data-level round-trip + no-throw + functional-after guarantees, explicitly
NOT a claim that the window actually visually went fullscreen under Xvfb) is honest and precisely matches what
`GraphicsDevice::applyPresentationParametersToWindow()` actually does, confirmed by direct inspection.

## Checklist Results

### API / XNA / FNA parity
`PresentationParameters.IsFullScreen` and `GraphicsDevice.Reset(PresentationParameters)` match FNA's own API surface
(`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/PresentationParameters.cs`,
`GraphicsDevice.cs`). FNA itself doesn't guarantee a headless environment can visually switch fullscreen either
(the underlying platform call can simply fail); this test's scope — verifying the stored XNA-level state and
no-throw/no-crash guarantee rather than actual window-manager-visible fullscreen — is a reasonable, explicitly
documented, testable subset of the real contract.

### Behavioral correctness
Directly confirmed against `GraphicsDevice.cpp` lines 1606-1626:
```cpp
void GraphicsDevice::applyPresentationParametersToWindow()
{
    if (window_ == nullptr) { return; }

    const bool fullScreen = presentationParameters_.getIsFullScreenProperty();
    if (!SDL_SetWindowFullscreen(window_, fullScreen))
    {
        SDL_ClearError();   // non-fatal: Task 902's own documented handling
    }
    ...
}
```
This is called unconditionally from `GraphicsDevice::Reset()` (line 406) AFTER `presentationParameters_ = presentationParameters;`
(line 393) has already stored the new value — meaning `dev.getPresentationParametersProperty().getIsFullScreenProperty()`
correctly reflects the requested value regardless of whether `SDL_SetWindowFullscreen` itself actually succeeds
under Xvfb, exactly matching the test's own two assertions
(`check(dev.getPresentationParametersProperty().getIsFullScreenProperty(), ...)` after toggling on, and the inverse
after toggling off) and its `ResetNoThrow` helper's expectation that `Reset()` itself never throws regardless of the
underlying SDL call's success.

### Logic
`ResetNoThrow` correctly narrows to catching `std::exception` and printing `e.what()` for diagnostic visibility on
failure — a reasonable, minimal wrapper; no logic issues found.

### Robustness
The closing check (`Clear(Color(0, 255, 0, 255))` + `GetBackBufferData` +
`pixel.getRProperty() <= 15 && pixel.getGProperty() >= 240 && pixel.getBProperty() <= 15`) is the tightest of the
"device remains functional" checks across this shard's files audited in this batch — unlike the sibling tests'
weaker `>= 240` only-on-lit-channels pattern (see the `sdlrenderer_double_dispose_test.cpp` report's F1), this one
DOES additionally bound the two unlit channels (`<= 15`), making it a strictly stronger closing assertion than most
of its siblings in this shard.

### Testing
The test's own scope-limiting note (in its header comment) is accurate and non-overclaiming: it explicitly says it
verifies "the XNA-level contract this project can actually guarantee under Xvfb," not that fullscreen visually
occurred — this audit confirms that's exactly what `applyPresentationParametersToWindow()`'s design provides, so the
test isn't quietly under-testing relative to what's achievable in this environment; it is testing the correct,
maximal, honestly-scoped claim.

## Detailed Findings

No HIGH, CRITICAL, or notable MEDIUM/LOW findings — this file's claims all check out precisely against the current
source, and its closing functional check is stricter than most sibling files in this same batch (a positive
finding, not a defect).

## Cross-File Observations

- This file's closing-check pattern (`<= 15` bound on unlit channels, not just `>= 240` on lit ones) is strictly
  better test-authoring than the identical-purpose closing checks in `sdlrenderer_double_dispose_test.cpp`,
  `sdlrenderer_disposed_guards_test.cpp`, `sdlrenderer_drawprimitives_throws_test.cpp`,
  `sdlrenderer_drawuserprimitives_throws_test.cpp`, and `sdlrenderer_drawuserindexedprimitives_throws_test.cpp`
  (all audited in this same batch) — worth citing as the shard's own best-practice example if that shared,
  low-severity pattern is ever revisited across the shard.
- Complements `sdlrenderer_devicereset_events_test.cpp` (also audited in this batch): that file verifies
  `DeviceResetting`/`DeviceReset` event ordering/visibility around a resize-triggered `Reset()`; this file verifies
  a different `PresentationParameters` field (`IsFullScreen`) round-trips through the same `Reset()` mechanism —
  together they give solid coverage of `Reset()`'s general "store new PP, reconfigure backend, don't corrupt state"
  contract from two different angles.

## Missing or Weak Tests

- No check that toggling fullscreen with an IDENTICAL value (e.g. `Reset()` called again with `IsFullScreen` already
  `true`) is also a safe no-op — a minor, low-value gap given XNA doesn't specify special no-op behavior here either.

## Positive Findings

- Header comment is precise and non-overclaiming about what can actually be verified in a headless/Xvfb test
  environment — an honest, well-scoped test rather than one that either skips the topic entirely or falsely claims
  full window-manager-level verification.
- The closing functional check is the tightest (both-bounds) version of this shard's common pattern, a genuinely
  stronger assertion than several sibling files audited in this same batch.
- `ResetNoThrow`'s diagnostic `e.what()` printout on failure is a nice touch for triage if this test ever regresses.

## Final Assessment

A precise, honestly-scoped, and correctly-verified test. No discrepancies found between its claims and the actual
`GraphicsDevice::applyPresentationParametersToWindow()`/`Reset()` implementation; its closing assertion is a
positive example other files in this shard could adopt.
