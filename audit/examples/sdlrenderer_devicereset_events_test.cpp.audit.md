# Audit: examples/sdlrenderer_devicereset_events_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_devicereset_events_test.cpp` (116 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 715, `DeviceResetting`/`DeviceReset` event ordering/visibility
- File type: standalone `Game`-subclass executable (`SdlDeviceResetEventsTest`), exit-code PASS/FAIL
- XNA/FNA relevance: `GraphicsDevice.DeviceResetting`/`GraphicsDevice.DeviceReset` events, `GraphicsDevice.Reset(PresentationParameters)`
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`Reset(const PresentationParameters&, GraphicsAdapter*)`, lines 389-439; `Reset(const PresentationParameters&)`,
  lines 1763-1766)
- Git corroboration: `18860f98`/`39316eb5` `verify(Task 715): DeviceResetting/DeviceReset fire correctly on resize`.

## Purpose

Verifies that `GraphicsDevice::Reset(pp)` raises `DeviceResetting` and `DeviceReset` exactly once each, in the
correct order (`DeviceResetting` before `DeviceReset`), and that each handler observes the correct
`PresentationParameters` snapshot: `DeviceResetting`'s handler must see the OLD (pre-resize) `BackBufferWidth`,
`DeviceReset`'s handler must see the NEW (post-resize) value — proving the events fire around the actual state
transition rather than both firing before or both firing after.

## Executive Verdict

**Healthy** — every assertion in this file is independently confirmed correct against the real `GraphicsDevice::Reset()`
implementation; the event-ordering and state-visibility claims are exactly right. One minor, low-severity
observation (F1: an inherited boilerplate comment referencing pixel-readback machinery this specific file never
uses).

## Checklist Results

### API / XNA / FNA parity
`GraphicsDevice.DeviceResetting`/`DeviceReset` are modeled via `System::EventHandler<T>` per this project's
established convention (`dev.DeviceResetting += [this](System::Object* sender, const System::EventArgs&) {...}`),
matching FNA's own `event EventHandler<EventArgs> DeviceResetting`/`DeviceReset` declarations
(`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/GraphicsDevice.cs`). The test correctly casts `sender` back
to `GraphicsDevice*` to read `getPresentationParametersProperty()` from inside the handler — a reasonable pattern
given `System::Object*` is the generic sender type.

### Behavioral correctness
Directly confirmed against `GraphicsDevice.cpp` lines 389-439:
```cpp
void GraphicsDevice::Reset(const PresentationParameters& presentationParameters, GraphicsAdapter* adapter)
{
    DeviceResetting.Raise(this, System::EventArgs::Empty);   // <-- BEFORE presentationParameters_ is overwritten

    presentationParameters_ = presentationParameters;
    ...
    UpdateViewportFromWindow();
    DeviceReset.Raise(this, System::EventArgs::Empty);        // <-- AFTER the full reconfiguration
}
```
This exactly matches the test's structure: `resettingSawWidth_` (read inside the `DeviceResetting` handler) is
asserted `== 32` (the constructor's initial `setPreferredBackBufferWidthProperty(32)`), and `resetSawWidth_` (read
inside the `DeviceReset` handler) is asserted `== 64` (the new `newPP.setBackBufferWidthProperty(64)` passed to
`Reset()`). Since `DeviceResetting.Raise` happens on line 391 — before `presentationParameters_ = presentationParameters`
on line 393 — and `DeviceReset.Raise` happens on line 438 — after every field/backend update — both assertions are
correct by direct inspection, not just plausible.

The single-argument `Reset(const PresentationParameters&)` overload the test actually calls
(`dev.Reset(newPP)`, `GraphicsDevice.cpp` lines 1763-1766) forwards unconditionally to the two-argument overload
above with `adapter_` (the device's existing adapter), so no different code path is exercised.

### Logic
`resettingFiredFirst_` is set inside the `DeviceResetting` handler only `if (resetCount_ == 0)` — this is a correct,
minimal way to detect ordering without needing a monotonic counter/timestamp: since both handlers run synchronously
within the single `Reset()` call, `resetCount_` can only be nonzero inside the `DeviceResetting` handler if
`DeviceReset` had already fired first (impossible given the real ordering, so this correctly stays `false` unless a
regression reversed the order).

### Memory/resource lifetime
The lambda handlers close over `this` (the `Game` subclass) by reference-capture-of-pointer semantics
(`[this]`) and are registered once in `Initialize()`, never re-registered — no double-subscription risk within this
single-`Reset()`-call test.

### C++ correctness
`static_cast<GraphicsDevice*>(sender)` inside each handler is safe here since `Reset()` always raises the events with
`this` (a `GraphicsDevice*`) as the sender — confirmed at both raise sites (`GraphicsDevice.cpp` lines 391, 438).

### Robustness
`check(resettingCount_ == 0 && resetCount_ == 0, ...)` before calling `Reset()` at all is a good baseline — it rules
out spurious firing during `Game`/`GraphicsDeviceManager` construction or `Initialize()`, which would otherwise
silently inflate the later "fires exactly once" counts into false passes.

### Testing
This file exercises only the manual/explicit `GraphicsDevice::Reset(pp)` path (shared, backend-agnostic code). It
does not exercise the separate backend-autonomous device-event path
(`GraphicsDevice::createBackend()`'s `deviceEventCallback` lambda, `GraphicsDevice.cpp` lines 1461-1478, which maps a
backend-detected `BackendDeviceEvent::Resetting`/`Reset` to the same two events) — that path is a different trigger
mechanism (a backend calling back into `GraphicsDevice` on its own, e.g. after detecting an OS-level device-lost
condition) not reachable through SDL_Renderer today, so its absence here is a scope boundary, not a defect.

## Detailed Findings

### F1 — Header comment cites `PresentationMode::NativeBackBuffer`/`SDL_RenderReadPixels` rationale that does not apply to this specific file (no pixel readback occurs anywhere in it)

- Severity: LOW
- Confidence: HIGH (directly confirmed by reading the full file — no `GetBackBufferData`/`ReadBackbuffer` call
  exists anywhere in it)
- Category: maintainability / stale documentation
- Location/symbol: header comment lines 14-16:
  > "Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates in physical
  > output coordinates, while this backend's default presentation mode (FixedHeightDynamicWidth) does not map
  > logical pixels 1:1 to physical ones."
  and constructor line 105: `gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);`
- Evidence: this file's only assertions read `PresentationParameters.BackBufferWidth` via
  `getPresentationParametersProperty().getBackBufferWidthProperty()` — a pure data-level property read, never a
  pixel-level `GetBackBufferData`/`ReadBackbuffer` call. Tracing `GraphicsDevice::Reset()`
  (`GraphicsDevice.cpp` lines 389-439) confirms `presentationParameters_.getBackBufferWidthProperty()` reflects
  exactly the value passed into `Reset()` (`virtualWidth_ = presentationParameters_.getBackBufferWidthProperty();`
  at line 399, with no later read-back/recompute of width the way `MultiSampleCount` explicitly gets read back from
  the backend at line 417) — so `PresentationMode` has no bearing on what this test actually asserts.
- Why it matters: this is boilerplate evidently copied from a sibling test in the same shard that DOES do pixel
  readback (e.g. `sdlrenderer_disposed_guards_test.cpp`, `sdlrenderer_fullscreen_toggle_test.cpp`, both of which
  legitimately need `NativeBackBuffer` for their `GetBackBufferData` calls per the real Task 915 finding). Here it's
  harmless (the mode is still validly set, just unnecessary for what this file actually checks), but it slightly
  misleads a reader into thinking this file does pixel-level verification when it only does data-level
  `PresentationParameters` verification — exactly the kind of stale/copy-pasted claim this audit was asked to
  independently re-verify rather than trust.
- FNA/XNA comparison: N/A (project-internal test-authoring/documentation observation).
- Related files: none outside this file — purely a local comment-accuracy note.
- Suggested future action (not implemented by this audit): trim the header comment to state only what this file
  actually verifies, or note explicitly that `NativeBackBuffer` is set defensively/for shard consistency even though
  unused by this particular test's assertions.

## Cross-File Observations

- The `deviceEventCallback` path in `GraphicsDevice::createBackend()` (`GraphicsDevice.cpp` lines 1461-1478) raises
  the same two events from a backend-autonomous trigger; SDL_Renderer's `SdlGraphicsBackend` (per its own audited
  report) does not currently invoke this callback, so this file's manual-`Reset()`-only coverage is the correct and
  only coverage possible on this backend today.

## Missing or Weak Tests

- No coverage of `DeviceLost` in this file (reasonable — SDL_Renderer has no real device-loss concept to simulate),
  and no coverage of calling `Reset()` a second time with unchanged parameters to confirm the events still fire
  (XNA does not suppress `Reset()` events for a no-op-sized reset) — a minor addition, not a defect in what exists.

## Positive Findings

- Every core assertion (fire-once, ordering, old/new `PresentationParameters` visibility) is independently confirmed
  correct by direct line-level inspection of `GraphicsDevice::Reset()`, not just self-consistent with the test's own
  expectations.
- The `resettingFiredFirst_`/`resetCount_ == 0` ordering check is a clean, minimal technique that doesn't require
  timestamps or a shared ordering log.

## Final Assessment

A correct, well-targeted test whose every assertion traces cleanly to the real `GraphicsDevice::Reset()`
implementation. The only issue found (F1) is a harmless, low-severity documentation staleness, not a functional
defect.
