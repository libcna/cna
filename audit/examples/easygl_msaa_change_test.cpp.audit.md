# Audit: examples/easygl_msaa_change_test.cpp

## Metadata

- Source file: `examples/easygl_msaa_change_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `GraphicsDeviceManager`/`PresentationParameters`
  `MultiSampleCount` write-back test (no pixel readback; state-only assertions)
- File type: `Game`-derived executable, CTest-registered as `cna_test_easygl_msaa_change` /
  `EasyGL_MsaaChange` (`cmake/Tests/EasyGLTests.cmake:1027-1029`)
- XNA/FNA relevance: direct — `GraphicsDeviceManager.PreferMultiSampling`,
  `PresentationParameters.MultiSampleCount`, `GraphicsDevice.Reset()`
- Production sources cross-checked: `src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp`
  (`ApplyChanges`, `applyToExistingBackend`, `PrepareDeviceSettings`),
  `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (`Reset`),
  `include/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.hpp`
  (`GetMultiSampleCount`/`ApplyMultiSampleCount` comments)

## Purpose

Verifies that `PresentationParameters.MultiSampleCount`, as observed through
`GraphicsDevice.getPresentationParametersProperty()`, honestly reflects EasyGL's *actual* MSAA
capability rather than blindly echoing whatever was requested: since EasyGL cannot reconfigure MSAA
on an already-constructed backend, toggling `GraphicsDeviceManager.PreferMultiSampling` via
`ApplyChanges()` on a live device must leave the reported `MultiSampleCount` at `0`, not silently
report the requested (but never-applied) value — while `GraphicsDevice.SetPresentationParameters()`
called directly (bypassing `Reset()`'s write-back) still stores whatever arbitrary value is set,
unvalidated.

## Executive Verdict

**Healthy.** Every one of this file's non-pixel, state-based assertions was independently traced
through `GraphicsDeviceManager.cpp` and `GraphicsDevice.cpp` and confirmed to follow exactly from the
real code path it names ("Task 902... `GraphicsDevice::Reset()`... `ApplyMultiSampleCount()`
write-back") — this is a rare case of a test's own extensive header commentary being fully,
concretely verifiable rather than aspirational.

## Checklist Results

### API / XNA / FNA parity
`GraphicsDeviceManager.PreferMultiSampling`/`ApplyChanges()`/`PresentationParameters.MultiSampleCount`
match FNA's real property/method names. FNA queries `FNA3D_GetMaxMultiSampleCount` and clamps
requested MSAA to whatever the driver actually supports, writing the *applied* value back into
`PresentationParameters` after `FNA3D_ResetBackbuffer` — CNA's own comment
(`GraphicsDeviceManager.cpp:496`: "FNA queries FNA3D_GetMaxMultiSampleCount and caps at 8; CNA uses 8
directly (no FNA3D)") documents this as an intentional simplification (no real FNA3D layer to query),
not an oversight, and the resulting cap-then-honestly-report-back behavior this test checks is the
same *shape* of behavior FNA exhibits, even though the concrete cap value (a hardcoded `8`) is a CNA
simplification rather than a real driver query.

### Behavioral correctness
Confirmed `GraphicsDeviceManager::PrepareDeviceSettings` (`GraphicsDeviceManager.cpp:490-498`):
`if (!preferMultiSampling_) pp.setMultiSampleCountProperty(0);` — so
`gdm_->setPreferMultiSamplingProperty(false); gdm_->ApplyChanges();` (test lines 95-98) genuinely
drives `MultiSampleCount` to `0` through the real preference path, not just the test's own
expectation.

Confirmed `applyToExistingBackend` (`GraphicsDeviceManager.cpp:551`) calls
`graphicsDevice_->Reset(pp, *gdi.getAdapterProperty())` (line 582), and
`GraphicsDevice::Reset(const PresentationParameters&, GraphicsAdapter*)`
(`GraphicsDevice.cpp:389+`) calls `backend_->ApplyMultiSampleCount(...)` (line 415) and stores the
**returned, backend-reported** value back into the presentation parameters — not the originally
requested one. Cross-checked EasyGL's own declared behavior
(`EasyGLGraphicsBackend.hpp:549-552`: "ApplyMultiSampleCount() uses IGraphicsBackend's default
(echoes back the current, already-applied value, ignoring the request). GetMultiSampleCount()
reports that real value" — and `GetMultiSampleCount()` is literally
`sampleCount_ > 1 ? sampleCount_ : 0`, where `sampleCount_` was fixed at construction time and is
never mutated by any runtime call) — so a device first constructed with `preferMultiSampling=false`
(hence `sampleCount_ = 1` internally) can never later report a non-zero `MultiSampleCount` through
this path, exactly matching the test's assertion at line 89-90 (expects `0`, not the requested `8`
default from `PrepareDeviceSettings`'s `preferMultiSampling_==true` branch, line 494-497).

The direct-path assertions (`directSet`, lines 66-73, 101-105) call
`GraphicsDevice::SetPresentationParameters(pp)` — a distinct method from `Reset()`, which (per the
test's own accurate comment) "bypasses Reset()'s ApplyMultiSampleCount() write-back entirely" and
simply stores whatever `PresentationParameters` object it is given; round-tripping `0/1/2/4/8`
unvalidated is consistent with a raw property-store method that performs no backend renegotiation.

### Logic
`checkCount` (lines 59-64) formats a descriptive failure message with `snprintf` into a fixed
256-byte buffer before delegating to `check()` — safe given the short, compile-time-bounded format
string and integer arguments; no truncation risk in practice.

### Robustness
The test's own header comment (lines 31-35) explicitly and correctly scopes out what it does *not*
test: real MSAA rendering quality (pixel-level anti-aliasing) and the "preferMultiSampling set
*before* first construction" scenario (deferred to `vulkan_msaa_test.cpp` for a backend that
supports runtime MSAA reconfiguration) — this is exactly the kind of explicit, honest scope-limiting
that the anti-boilerplate audit standard wants to see, rather than a title implying broader coverage
than the file actually delivers.

### Testing
9 assertions across `Initialize()` only (no separate `Draw()`-phase checks beyond the final PASS/FAIL
tally) — appropriate, since none of this file's checks require an actual rendered frame; state can be
asserted the moment the device exists.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Direct-`SetPresentationParameters` round-trip test does not itself validate against a real backend re-query

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `directSet` (lines 66-73), calls at lines 101-105
- Evidence: `directSet` only re-reads `dev.getPresentationParametersProperty().getMultiSampleCountProperty()`
  immediately after `SetPresentationParameters(pp)` — it does not also call
  `dev.getGraphicsBackend()`-equivalent to confirm the backend's own `GetMultiSampleCount()` was left
  unchanged by this bypass path (i.e., that the stored `PresentationParameters` value and the
  backend's real capability can now legitimately disagree, which is the whole point being
  demonstrated, but is never explicitly cross-checked in the same block).
- Why it matters: purely a missed opportunity to make the "these two can now disagree" property an
  explicit assertion rather than an implicit read of the test's own prose; no functional bug.

## Cross-File Observations

- This file and `easygl_msaa_test.cpp` (also in this batch) both exercise EasyGL's MSAA path, but
  from opposite ends: this file checks that `PresentationParameters.MultiSampleCount` is *honestly
  reported* (state-only, no rendering); `easygl_msaa_test.cpp` renders and reads back a pixel to
  confirm the MSAA-resolve pipeline itself works. See that file's own audit report for a discrepancy
  found between its claimed "4×" sample count and the actual `preferMultiSampling=true` default of
  `8` established in `GraphicsDeviceManager.cpp:494-497` (the same code path independently confirmed
  correct here).

## Missing or Weak Tests

- See F1 — a direct backend-level re-query alongside the `PresentationParameters` re-query in
  `directSet` would make the "these can diverge" property explicit rather than implicit.

## Positive Findings

- Exceptionally well-corroborated header commentary: every claim in the file's own 36-line preamble
  about `Reset()`/`ApplyMultiSampleCount()`/write-back behavior was independently traced through
  `GraphicsDeviceManager.cpp`, `GraphicsDevice.cpp`, and `EasyGLGraphicsBackend.hpp` and found
  accurate down to the specific line-level mechanics.
- Explicitly and correctly scopes out what it does not test (lines 31-35), rather than overclaiming.

## Final Assessment

An accurate, well-documented state-transition test of a genuinely subtle write-back mechanism
(`GraphicsDeviceManager.ApplyChanges()` → `GraphicsDevice.Reset()` →
`IGraphicsBackend.ApplyMultiSampleCount()`); every assertion traces correctly to real production
code, and the file's scope-limiting commentary is honest rather than aspirational.
