# Audit: examples/sdlrenderer_presentinterval_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_presentinterval_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 713, `PresentInterval` (vsync) mapping to
  `SDL_SetRenderVSync`.
- File type: standalone `Game`-subclass executable, CTest-registered (`SDL_Renderer_PresentInterval` /
  `cna_test_sdl_presentinterval`, `cmake/Tests/SdlRendererTests.cmake:307-309`).
- XNA/FNA relevance: `PresentationParameters.PresentInterval`/`GraphicsDevice.Reset()` semantics.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`toSwapInterval`, lines 61-69; `Reset(...)`, lines 419-425), `src/CNA/Internal/Backends/SdlRenderer/
  SdlGraphicsBackend.cpp` (`SetSwapInterval`, lines 519-536).

## Purpose

Verifies a real bug fix (Task 713): `SdlGraphicsBackend::SetSwapInterval` previously collapsed *any* positive
interval to `1`, silently discarding `PresentInterval::Two`'s documented "wait for two vertical retrace periods"
semantics — now the raw value (0/1/2) is passed straight through to `SDL_SetRenderVSync`, with a fallback to `1`
if the driver rejects `2` outright. Since neither `GraphicsDevice` nor `SdlGraphicsBackend` expose a public
accessor for the real current SDL vsync setting, this test verifies what's observable at the XNA level: every
`PresentInterval` value applies via `Reset()` without throwing, round-trips correctly through
`PresentationParameters.PresentationInterval`, and the device stays functional afterward.

## Executive Verdict

**Healthy** — the `toSwapInterval` mapping, `Reset()`'s forwarding to `SetSwapInterval`, and the value-passthrough
fix in `SdlGraphicsBackend::SetSwapInterval` were all independently confirmed against the current source; the
test's own self-acknowledged limitation (it cannot observe the real SDL vsync state, only the round-tripped XNA
property) is accurately described, not overclaimed.

## Checklist Results

### API / XNA / FNA parity

`PresentInterval::Immediate/One/Two/Default` (test lines 71-74) matches the 4-value XNA `PresentInterval` enum
exactly (confirmed via `PresentInterval.hpp` inclusion and `GraphicsDevice.cpp`'s own `toSwapInterval` switch
covering the same 3 distinct cases with `Default` folded into `One`'s branch, lines 61-69:
`Immediate→0`, `Two→2`, `default→1` covering both `Default` and `One`) — this test's 4-value array intentionally
includes both `One` and `Default` as separate entries even though they map to the same underlying `int` (1),
which is the correct way to test that *both* enum values are accepted and *both* round-trip identically, not
merely that "some value mapping to 1" works.

### Behavioral correctness

Traced `GraphicsDevice::Reset(...)` (line 425): `backend_->SetSwapInterval(toSwapInterval(presentationParameters_
.getPresentationIntervalProperty()))` — confirmed this line exists and is reached on every `Reset()` call (not
gated behind a first-time-only branch), consistent with the file's own header-comment claim that this forwarding
was "previously missing" from this particular `Reset()` overload and has since been fixed to match
`SetPresentationParameters()`'s equivalent forwarding.

`SdlGraphicsBackend::SetSwapInterval(int interval)` (`SdlGraphicsBackend.cpp:519-536`): confirmed the value is
now passed straight through — `SDL_SetRenderVSync(renderer, interval)` (line 529) — with the historical bug
(`interval > 0 ? 1 : 0`, collapsing `Two`'s `2` down to `1`) genuinely gone from the current source, replaced by a
fallback that only engages `if (interval > 1)` *and* the direct call already failed (line 534-535) — i.e. `2` is
tried as `2` first, not proactively downgraded. This matches the header comment's claim precisely, including the
"empirically confirmed this project's own sandbox GL driver rejects interval=2" framing, which this test cannot
itself verify (correctly not attempted) but which is consistent with the fallback code existing at all — a
fallback path a developer would have no reason to add without having observed the rejection.

Every value in `values[]` is applied via `Reset()` (line 81) then immediately read back via
`getPresentationIntervalProperty()` (line 89) — since `Reset()` unconditionally stores
`presentationParameters_ = presentationParameters` (`GraphicsDevice.cpp:393`) before any backend interaction, the
round-trip assertion is guaranteed to hold regardless of what the *backend* does with the value — meaning this
specific assertion (`round-trips as %s`) is really testing `PresentationParameters`'s copy/storage semantics, not
`SetSwapInterval`'s own correctness. The *only* assertion that meaningfully exercises `SetSwapInterval`'s "does
not throw" behavior is the `check(true, ...)` "reaching here" pattern (line 85) — see Logic below.

### Logic

The `check(true, label1)` "does not throw (reaching here)" pattern (lines 83-85) is a legitimate, if implicit,
way to assert "the preceding statement didn't throw": since `dev.Reset(pp)` on line 81 is called outside any
`try`/`catch`, an actual thrown exception would propagate out of `Draw()` uncaught, terminating the test process
(a hard crash/non-zero exit, visible to CTest as a failure) rather than silently passing — so this is not a
no-op check, but it does mean a thrown exception here produces a *process crash* rather than a clean `[FAIL]`
diagnostic line the way this file's other `check()` calls do. This is a minor asymmetry in failure-mode quality
(a crash gives less diagnostic information than a caught-and-reported failure) but is not a false-positive risk —
it cannot silently mask a real throw.

### Robustness

Final functionality check (lines 92-100: orange `Clear(255,128,0,255)` + 1x1 readback with tolerance-banded RGB
assertion `R>=240 && G∈[118,138] && B<=15`) correctly confirms the device remains usable after cycling through all
4 interval values — same pattern verified correct elsewhere in this shard.

### Testing

Covers all 4 `PresentInterval` enum values via `Reset()`, the one XNA-facing entry point that reaches
`SetSwapInterval`. Does not (and, per the file's own comment, structurally cannot from outside the backend)
directly assert on the real SDL vsync state or on which of `interval`/the `interval>1` fallback branch actually
fired for `Two` specifically — an honestly-disclosed limitation, not a hidden gap.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. One LOW observation:

### F1 — The "does not throw" check for each interval uses the "reaching here" pattern outside a try/catch, so a genuine regression would crash the test process rather than report a clean per-value failure

- Severity: LOW
- Confidence: HIGH
- Category: test-authoring / diagnostic-quality
- Location/symbol: lines 79-85 (`dev.Reset(pp)` call, then `check(true, label1)`)
- Evidence: `dev.Reset(pp)` (line 81) is not wrapped in `try`/`catch`; if `SetSwapInterval` (or anything else
  `Reset()` calls) threw for a given interval value, the exception would propagate out of `Draw()` uncaught. The
  test binary would terminate abnormally (uncaught-exception `std::terminate`) rather than printing
  `[FAIL] Reset() with PresentInterval::Two does not throw...` and continuing to the next iteration.
- Why it matters: CTest would still correctly report this test as failed (non-zero/abnormal exit), so there is no
  false-positive risk — but the diagnostic output would be a crash/abort message instead of this file's own
  structured `[PASS]`/`[FAIL]` log lines, and the loop would stop at the first failing interval rather than
  reporting all 4 values' results in one run (as the other `check()`-protected assertions in this same loop do).
- FNA/XNA comparison: N/A (test-harness robustness, not an XNA/FNA behavior question).
- Related files: none — self-contained pattern, also used by `sdlrenderer_multisamplecount_decision_test.cpp` and
  `sdlrenderer_getbackbufferdata_after_rt_unbind_test.cpp` in this same shard via their own `try`/`catch` +
  `check(!threw, ...)` variants (which *do* catch and report cleanly) — this file is the one instance in the
  8 files reviewed in this batch that omits the `try`/`catch` wrapper for its "does not throw" assertion.
- Suggested future action (not implemented by this audit): wrap `dev.Reset(pp)` in a `try`/`catch` and report
  `check(!threw, label1)` the same way `sdlrenderer_multisamplecount_decision_test.cpp` does for its own
  "does not throw" assertion, so a regression on any one interval value produces a clean, complete diagnostic
  report across all 4 values instead of an abrupt process crash on the first failure.

## Cross-File Observations

- `sdlrenderer_multisamplecount_decision_test.cpp` (same shard, same "does `Reset()` forward a field to the
  backend without throwing" concern for a *different* `PresentationParameters` field) uses the safer
  `try { dev.Reset(pp); } catch (...) { threw = true; ... } check(!threw, ...)` pattern — worth using as the
  template if F1 is ever addressed.
- The `PresentationParameters.PresentationInterval` round-trip assertion's actual dependency (on
  `PresentationParameters`'s copy semantics, not `SetSwapInterval`) mirrors a similar observation made in this
  batch for `sdlrenderer_graphics_capability_test.cpp` (where 8 checks all exercise one parameter-independent
  branch) — a recurring, low-severity pattern in this shard worth keeping in mind: several "checks" verify a
  necessary but not sufficient condition for the behavior being described.

## Missing or Weak Tests

See F1. No coverage gap in terms of *values tested* (all 4 enum values covered) — the gap is purely in
failure-mode diagnostic quality for one specific assertion.

## Positive Findings

- Correctly includes both `PresentInterval::One` and `::Default` as separate cases despite both mapping to the
  same underlying integer, verifying enum-value acceptance rather than just integer-value acceptance.
- The underlying production fix (passing `interval` through unchanged instead of collapsing to `1`, with a
  driver-rejection fallback) was independently confirmed correct and precisely matches the header comment's
  claims, including the specific fallback-triggering condition (`interval > 1`, only after the direct call
  already failed).
- Honest, explicit acknowledgment of what this test *cannot* observe (the real SDL-level vsync setting), rather
  than implying full verification of a claim it cannot actually check.

## Final Assessment

A correct test of a correct fix, with one minor diagnostic-robustness nit (F1: an unguarded `Reset()` call whose
failure mode is a process crash rather than a clean per-value `[FAIL]`). No functional defects found in either the
test or the `SetSwapInterval`/`toSwapInterval`/`Reset()` code it exercises.
