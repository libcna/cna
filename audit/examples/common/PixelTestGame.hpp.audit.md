# Audit: examples/common/PixelTestGame.hpp

## Metadata
- Source file: `examples/common/PixelTestGame.hpp` (293 lines)
- Audit status: AUDITED
- Subsystem: `examples-common` shard
- File type: shared header-only test-infrastructure helper, opt-in for new single-frame pixel-
  readback example tests
- XNA/FNA relevance: wraps `Game`, `GraphicsDevice::GetBackBufferData`, `Texture2D::FromStream`/
  `SaveAsPng`/`GetData` — not itself an XNA type
- Related production code: `Game.hpp`/`.cpp`, `GraphicsDevice.hpp`/`.cpp`, `Texture2D.hpp`/`.cpp`
  (audited this session as part of the `xna-framework-core`/`xna-graphics` shards)

## Purpose
Provides `CNA::Examples::PixelTestGame` (a `Game` subclass with `ExpectPixel`/`CompareGoldenImage`
helpers for single-frame draw-then-check tests) and `RunPixelTest<TGame>()` (a one-line
construct-run-return-exit-code helper), explicitly as new, additive, opt-in infrastructure — no
existing example test file was retrofitted to use it.

## Executive Verdict
Correct, thoughtfully scoped, and honestly documented about its own limits: the top-of-file
comment explicitly explains why ~330 existing hand-rolled example tests were deliberately NOT
migrated to this shared base (a large, high-risk, low-value mechanical refactor), and why
multi-frame state-machine tests should keep hand-rolling their own `Game` subclass rather than
being forced into this single-shot shape.

## Checklist Results
- `ExpectPixel()`'s doc comment (lines 71-81) documents it reads a "single-pixel region" but the
  method itself does not validate that `region` is actually 1x1 before calling
  `GetBackBufferData(&region, &pixel, 0, 1)` — passing a larger region would only fill `pixel` with
  whatever `GetBackBufferData`'s own semantics produce for a count-1 read against a larger region
  (behavior of that mismatch is `GraphicsDevice::GetBackBufferData`'s own contract, not validated
  here). Not flagged as a MEDIUM finding since the doc comment is clear about the intended usage
  and, per the file's own claim, ~330 existing tests already use this exact pattern correctly and
  consistently — a LOW, purely-defensive-programming gap.
- `CompareGoldenImage()`'s `CNA_UPDATE_GOLDEN` environment-variable-gated regeneration path (lines
  159-173) is a reasonable, standard golden-image-testing workflow, clearly documented with its own
  usage instructions in the top-of-file comment.
- `RunPixelTest<TGame>()`'s headless-safe pre-flight check (lines 278-283) correctly probes
  `SDL_InitSubSystem(SDL_INIT_VIDEO)` and returns the documented `kSkipExitCode` (77, matching
  CTest's own `SKIP_RETURN_CODE` convention) rather than letting a real `Game`/`GraphicsDevice`
  construction throw an uncaught `std::runtime_error` on a machine with no GPU/display — a genuine,
  useful hardening documented as a specific fix (Task 470).
- `static_assert(std::is_base_of_v<PixelTestGame, TGame>, ...)` (line 275) gives a clear compile-time
  error for template misuse rather than a confusing instantiation failure.

## Detailed Findings
None beyond the LOW note above (not raised to a full finding given the clear documentation and
established correct-usage track record).

## Cross-File Observations
`ProbeGpuDisplayAvailable()` and `RunPixelTest<TGame>()`'s pre-flight check both use the identical
`SDL_InitSubSystem`/`SDL_QuitSubSystem` probe pattern — correctly kept as two separate call sites
(one for hand-rolled multi-frame tests that don't use `RunPixelTest`, one built into
`RunPixelTest` itself) rather than forcing hand-rolled tests to also inherit from `PixelTestGame`.

## Missing or Weak Tests
Not independently located in this pass; this file is itself test infrastructure, not a test.

## Positive Findings
The top-of-file comment's explicit rationale for NOT retrofitting existing tests, and for keeping
multi-frame tests outside this shared base, reflects careful, low-risk-bias engineering judgment
rather than premature or forced abstraction — directly consistent with this project's own
CLAUDE.md guidance against unnecessary refactoring.

## Final Assessment
No findings.
