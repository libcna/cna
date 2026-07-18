# Audit: examples/easygl_pixeltestgame_smoke_test.cpp

## Metadata

- Source file: `examples/easygl_pixeltestgame_smoke_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — canary/self-test for the shared `PixelTestGame` test-infrastructure
  helper
- File type: C++ example/integration-test executable (`PixelTestGameSmokeTest : CNA::Examples::PixelTestGame`,
  `main()`) — 35 lines total, the shortest file in this batch
- Related production code: `examples/common/PixelTestGame.hpp` (`ExpectPixel`, `RunPixelTest<TGame>`)
- XNA/FNA relevance: N/A directly — `CNA::Examples::PixelTestGame` is `NOXNA` test infrastructure, not an XNA API
  surface; it does exercise real `Microsoft::Xna::Framework::Graphics::GraphicsDevice::Clear`/`GetBackBufferData`
  underneath.
- Main related tests: this file itself (Tasks 461/462); it is the very test that proves `PixelTestGame` — the
  shared base class used by `easygl_pbreffect_golden_test.cpp` and `easygl_pixeltestgame_smoke_test.cpp`'s own
  6+ other sibling golden/pixel tests in this shard — actually works end to end.

## Purpose

A minimal canary: clears the backbuffer to solid green, then checks the centre pixel two ways — an exact match
(`Color(0,255,0,255)`, Task 461) and a deliberately-10-off match that only passes because of an explicit
`tolerance=20` (Task 462) — proving both the exact-match and tolerance-match code paths of `PixelTestGame::
ExpectPixel` are genuinely exercised, not merely present and unused.

## Executive Verdict

**Healthy.** A trivial file by design (it is explicitly a canary/self-test, not a feature test), but it does
exactly what its name and header comment claim: it proves the shared harness underneath ~30+ other tests in this
project's `examples-tests-easygl` shard genuinely works (window/device creation, one `Draw()`, readback, exact and
tolerant comparison, exit code), rather than assuming a header file "must work" because it compiles. No
HIGH/CRITICAL/MEDIUM findings; one small, low-priority coverage observation below.

## Checklist Results

### API / XNA / FNA parity
N/A — `CNA::Examples::PixelTestGame`/`RunPixelTest` are `NOXNA` test infrastructure (confirmed by their placement
under `examples/common/`, not `Microsoft::Xna::Framework`). The underlying `device.Clear(Color(...))` and
`ExpectPixel`'s own `GetBackBufferData` call are real XNA-facing calls, correctly used.

### Behavioral correctness
`ExpectPixel("centre-tolerance", ..., Color(10, 255, 10, 255), tolerance=20)` (lines 26-27) is checked against a
scene that actually clears to `Color(0,255,0,255)` — the R and B channels are deliberately mismatched by exactly
10, which is `≤20`, so this check passes *only* through the tolerance path in `PixelTestGame::ExpectPixel`
(`closeEnough(a,b) = abs(a-b) <= tolerance`, `PixelTestGame.hpp` lines 92-95) — verified this is a genuine exercise
of the tolerance branch, not a coincidental exact match: `abs(0-10)=10 ≤ 20` and `abs(0-10)=10 ≤ 20`, both true and
both non-zero, so the tolerance logic is provably load-bearing for this specific check to pass.

### Logic
Two calls, no branching of its own — `RunTest()` is a straight-line 3-statement body (`Clear`, `ExpectPixel` exact,
`ExpectPixel` tolerant). Nothing to trace beyond what's already covered above.

### Memory/resource lifetime
No manual resource management in this file — entirely delegated to `PixelTestGame`/`Game`'s own lifecycle, which
is correct for a file this size and purpose.

### Testing
This file is itself the self-test for the shared test harness used throughout this shard. Its own two checks
exercise `ExpectPixel`'s exact-match and tolerance-match branches, but it does **not** exercise
`PixelTestGame::CompareGoldenImage()` — the other primary entry point the same header offers (used by
`easygl_pbreffect_golden_test.cpp` and other golden-image tests in this shard) — see Missing or Weak Tests.

## Detailed Findings

No HIGH/CRITICAL/MEDIUM findings — this file is correctly scoped to its stated purpose (a small, fast canary) and
achieves it.

## Cross-File Observations

- This file's correctness matters disproportionately to its own small size: `easygl_pbreffect_golden_test.cpp` and
  every other `PixelTestGame`-derived test in this shard depend on the exact-match and tolerance-match behavior
  this file specifically canaries. A regression in `PixelTestGame::ExpectPixel` itself would most cleanly surface
  here first, before it manifests as a confusing failure in one of the larger feature tests.
- `easygl_pbreffect_golden_test.cpp` (audited in this same batch) is the shard's actual `CompareGoldenImage()`
  consumer/self-test-by-proxy, since this file does not call it — together the two files provide reasonably
  complete coverage of `PixelTestGame`'s two comparison mechanisms, just split across two files rather than one.

## Missing or Weak Tests

- `PixelTestGame::CompareGoldenImage()` (the other public comparison method the same header exposes, including its
  `CNA_UPDATE_GOLDEN` regeneration path and its width/height-mismatch guard) is not exercised by this
  "PixelTestGame smoke test" file at all — a reader could reasonably expect a file named
  `easygl_pixeltestgame_smoke_test.cpp` to canary *all* of `PixelTestGame`'s public surface, not just
  `ExpectPixel`. In practice this gap is filled indirectly by `easygl_basiceffect_golden_test.cpp`/
  `easygl_pbreffect_golden_test.cpp` (both already using `CompareGoldenImage` successfully elsewhere in this
  shard), so the actual risk is low, but it is not this file's own stated job.

## Positive Findings

- Genuinely proves the tolerance path is exercised rather than merely present, by using a deliberately-off value
  (10, not 0) instead of testing tolerance with a value that would also pass an exact match.
- Correctly scoped as a small, fast, single-purpose canary rather than being padded out with unrelated assertions.

## Final Assessment

A correctly-scoped, genuinely load-bearing canary test for the shard's shared `PixelTestGame` harness. Both of its
checks provably exercise the code paths they claim to (confirmed by hand-checking the tolerance arithmetic); its
only gap is not also canarying `CompareGoldenImage()`, which is filled in practice by other golden-image tests
elsewhere in this same shard.
