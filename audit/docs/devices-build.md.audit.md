# Audit: docs/devices-build.md

## Metadata
- Source file: `docs/devices-build.md` (693 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown reproducible-build/test commands reference
- XNA/FNA relevance: `Microsoft::Devices` build/test/sanitizer/CI reproduction recipes

## Purpose
Every build/test/sanitizer/CI command actually run for `Microsoft::Devices` work, from a fresh clone
through Android cross-compile/APK/emulator, iOS (blocked), sanitizer presets, and CI — each with a
stated claim that it was actually executed in this repository, not copy-pasted unverified.

## Executive Verdict
An exceptionally disciplined reproducibility document. Its own stated rule — "Where a command's
success is asserted, it was verified in this repository, on this branch, this session" — is followed
throughout, including several instances of catching and fixing this document's *own* prior
inaccuracy (the exact-suite-name test-filter correction after finding the old substring filter both
silently dropped a suite and matched 2 false-positive cross-namespace tests; the TSan run finding a
real, new, previously-undiscovered race in a test fixture itself, not production code).

## Checklist Results
- The exact-suite-name filter correction (finding the old substring filter both dropped
  `CalibrationEventArgsTests` and falsely matched `GamePadTest`/`SdlInputBridgeTouchGestureTest`) is a
  concrete, well-evidenced bug-in-the-documentation-itself finding — a good example of documentation
  quality assurance applied to the doc's own commands, not just to the code it describes.
  the "if this number drifts again, re-run the ground-truth grep... rather than trusting either number
  at face value" instruction shows awareness that even this document's own numbers can go stale.
- The TSan section's "if a future TSan run reports anything other than this one `TimeSpan.cpp:55`
  finding, treat it as a real, new bug" instruction, followed immediately by an account of exactly that
  happening (a second, real, new `SensorBaseTests.cpp` fixture race found and fixed) is a strong,
  self-consistent demonstration that this warning is taken seriously, not just written once and
  ignored.
- Section 8's CI description ("This CI job has not yet actually executed on GitHub Actions as of this
  writing") is consistent with `docs/devices-android.md`'s own corrected CI claim ("What remains
  genuinely unconfirmed is whether that workflow has actually run green on a real GitHub-hosted
  runner") — no drift between the two documents.

## Detailed Findings
None found against this document's own claims — every cross-checkable assertion (suite counts, sibling
repository requirements, sanitizer results) is stated with a specific verification method, and no
internal or cross-file contradiction was found.

## Cross-File Observations
- Directly corroborates `docs/devices-android.md`'s CI status claim (both agree the workflow exists
  but has not yet run green on a real runner).
- Section 7's "Decided against: a native Android vibration backend" is word-for-word consistent with
  `docs/devices-android.md`'s own "Vibration: no native bridge exists, and none is needed" section —
  the same evidence (SDL3's own Android haptic source reading) cited identically in both places.
- Section 5.1's iOS `StartLeftRight()` planning note explicitly reuses "the identical blend weighting"
  from `docs/devices-android.md`'s Android finding for consistency — a good example of one document
  building on another's established finding rather than re-deriving it independently (and potentially
  inconsistently).

## Missing or Weak Tests
N/A — this document is itself the record of test/build reproduction; no gap identified in its own
account beyond what it already discloses (e.g. `googletest` not configured for Android NDK, so
`CnaTests` was never cross-compiled there — a compile-only Android verification, honestly stated as
such).

## Positive Findings
The "Concurrency tests in this suite are stress tests, not single-shot checks" section, backed by
three concrete historical incidents (a heap-corruption bug needing 40+ loop iterations to surface, a
cross-class SDL-mutex fix needing a new multi-class stress test, a dispatch use-after-free confirmed
real only by deliberately reverting the fix and observing 5/5 segfaults) is an exceptionally strong,
evidence-backed testing-methodology lesson — one of the best in this entire docs corpus.

## Final Assessment
No findings. An exceptionally well-verified, appropriately self-correcting reproducibility reference.
