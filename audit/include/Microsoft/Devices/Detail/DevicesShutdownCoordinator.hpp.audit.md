# Audit: include/Microsoft/Devices/Detail/DevicesShutdownCoordinator.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Detail/DevicesShutdownCoordinator.hpp` (118 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header (single atomic-flag coordinator class)
- XNA/FNA relevance: CNA-internal plumbing; FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
Process-wide coordinator letting an application signal, before its own `SDL_Quit()` call, that `Microsoft::Devices` objects (specifically `VibrateController`'s function-local-static singleton) should skip their own native SDL teardown calls, since `SDL_Quit()` already reclaims those resources.

## Executive Verdict
Correct, and the doc comment is a model example of distinguishing a *reasoned-but-unverified* risk from an *empirically-verified* one, for two genuinely different failure modes: `SDL_CloseHaptic()` against an already-`SDL_Quit()`-closed device is reasoned (from reading SDL's own source, `CHECK_HAPTIC_MAGIC` dereferencing already-freed memory) to be a genuine heap-use-after-free, but explicitly disclosed as **not empirically reproduced under ASan** in this environment (no physical haptic device is ever opened here); by contrast, `SDL_QuitSubSystem(SDL_INIT_HAPTIC)` after `SDL_Quit()` was **both** reasoned safe (refcount-gated no-op) **and** empirically verified clean under ASan via a real test harness (`tools/devices/shutdown_ordering_harness.cpp`), run both with and without this coordinator's guard active. This precision — not overclaiming "verified" for the one case that genuinely wasn't, while still fixing it defensively — is exactly the right level of honesty for a report like this to trust.

## Checklist Results
- `Shutdown()`/`IsShutdown()`/`ResetForTesting()` are correctly implemented via a function-local `static std::atomic<bool>` with `memory_order_release`/`memory_order_acquire` — appropriate, standard pairing for a cross-thread visibility flag with no other state that needs to be ordered relative to it.
- `Shutdown()` is documented and correctly implemented as idempotent and safe to call even if no `Microsoft::Devices` object was ever constructed (it's just a flag flip, with no dependency on any other object's lifetime).
- The doc comment also separately investigates and correctly rules out `SDL_Log()` (used by `NativeDiagnosticSink`) as needing this same guard, with a specific, source-grounded reason (`SDL_LockMutex(NULL)` is a documented-safe no-op) — a thorough sweep of every SDL-touching call site in this area, not just the one the class was originally built for.

## Detailed Findings
None.

## Cross-File Observations
Directly consumed by `SdlHapticVibrateBackend`'s destructor (audited separately, confirmed correctly checking `IsShutdown()` before its native calls).

## Missing or Weak Tests
Not independently located in this pass; the doc comment's citation of `tools/devices/shutdown_ordering_harness.cpp` and a specific ASan-clean empirical result (both with and without the guard active) is strong, real evidence this design was genuinely validated, not just reasoned about.

## Positive Findings
The precise distinction between "reasoned but not empirically reproduced" and "reasoned and empirically verified" for two superficially similar SDL calls is exactly the kind of calibrated confidence this audit values — it would be easy (and wrong) to claim both are equally verified.

## Final Assessment
No findings.
