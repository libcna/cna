# Audit: include/Microsoft/Devices/VibrateController.hpp

## Metadata
- Source file: `include/Microsoft/Devices/VibrateController.hpp` (229 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace (WP7-only API, never implemented in desktop FNA); the 5-second max-duration contract is independently corroborable as a real, documented WP7 API constraint (archived MSDN `ff403287(v=vs.105)`)
- Main related tests: not independently located in this pass

## Purpose
Declares `VibrateController`, the WP7-shaped singleton controlling the device's vibration motor, plus CNA-specific extensions (`Start(duration, intensity)`, `StartLeftRight`, `getIsSupportedProperty`, `getDeviceNameProperty`).

## Executive Verdict
Correct. Matches the real WP7 API shape precisely: `VibrateController` is not static, reached via a single shared instance (`getDefaultProperty()`), with instance methods — confirmed against the file's own accurate description of the real API. Every `NOXNA` extension is clearly tagged and independently justified (SDL3 rumble-strength access, dual-motor support, capability/name probing) rather than silently blended into the strict XNA surface.

## Checklist Results
- `Start(duration)` correctly documents the real WP7 `[0, 5 seconds]` duration contract with a citation to the specific archived MSDN page, re-verified per its own comment (Task VIB-006) — and correctly documents `System::ArgumentOutOfRangeException` as the thrown type (matching this project's established exception-type convention, not a raw `std::` exception).
- Explicitly excludes a connected gamepad's own rumble motor from device selection (documented in the class comment) so this class never competes with `GamePad::SetVibration()` for the same physical actuator — a genuinely thoughtful API-boundary design decision.
- `intensity 0.0f` is explicitly documented as NOT an implicit `Stop()` — it still uploads and plays a zero-strength effect for the full duration — with the doc comment citing a specific re-confirmation against SDL3's own actual header/implementation (not just its documentation) that `0` is inside `SDL_PlayHapticRumble()`'s own valid range and that this composes correctly with repeated/overlapping calls. A real, load-bearing behavioral distinction, correctly disclosed rather than silently assumed.
- `SetBackendForTesting()`'s doc comment correctly notes `VibrateController` has no persistent "started" state to protect (every call is independently fire-and-forget), unlike `Compass`/`Motion`'s equivalent testing hook — an accurate, non-copy-pasted distinction between two superficially similar patterns.

## Detailed Findings
None.

## Cross-File Observations
See `src/Microsoft/Devices/VibrateController.cpp.audit.md` for confirmation that `backendMutex_` is genuinely held across every backend call (not released before dereferencing, unlike the confirmed real `FileDialog`/`MessageBox` mutex-scoping bug this project's own `AUDIT_CROSS_CUTTING_FINDINGS.md` documents in an adjacent area) — this class does NOT share that bug.

## Missing or Weak Tests
Not independently located in this pass; the doc comment's citation of a specific test name (`VibrateControllerTests.StartWithIntensityZeroForwardsAsAnActiveZeroStrengthStartNotAnImplicitStop`) suggests a real, exercised test suite covering this exact edge case.

## Positive Findings
The gamepad-rumble-exclusion design and the intensity-zero-is-not-Stop() distinction are both genuine, well-reasoned API design decisions with primary-source verification (SDL3's actual implementation, not just its header comments) behind them.

## Final Assessment
No findings.
