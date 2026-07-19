# Audit: tests/Microsoft/Xna/Framework/Content/CnjStockEffectTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Content/CnjStockEffectTests.cpp` (309 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-content` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `.cnj` JSON support for the 5 stock effects (`BasicEffect`/
  `AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`), dispatched from
  the shared `EffectTypeReader` (NOXNA content pipeline extension; field lists are a JSON port of
  `StockEffectContentTypeReaders.cpp`'s already-FNA-verified per-effect field order)
- Main related tests: N/A (this IS a test file)

## Purpose
Tests all-fields loading and engine-default-matching for `BasicEffect`, plus field coverage for
the other 4 stock effects, unrecognized-type rejection (including a specific error-message-content
regression), `sourceFile` rejection, and a regression check that the pre-existing custom-GLSL
`Effect` shape still works after the reader started dispatching across 6 type names.

## Executive Verdict
Correct, thorough, and contains a genuinely valuable regression test with a documented real-bug
provenance. `UnrecognizedTypeWithSourceFileReportsTypeProblem` (lines 252-272) is explicitly
labeled "Found by adversarial review, 2026-07-17" and asserts on the *exact exception message
content* (`message.find("NotARealEffectType")` present, `message.find("sourceFile")` absent) —
proving the type-mismatch error is reported, not the coincidentally-also-true but less useful
`sourceFile`-rejection error, when both conditions are present simultaneously. This is a real,
previously-found error-message-priority bug, now regression-tested precisely.

## Checklist Results
- `LoadsBasicEffectWithAllFields`/`BasicEffectDefaultsMatchEngineDefaults`: the latter compares
  against a genuinely freshly-constructed `BasicEffect` rather than hardcoded literal defaults —
  a more robust test design that automatically tracks the real engine defaults if they ever change,
  rather than needing manual updates.
- `CustomGlslEffectStillWorks` (lines 289-309) is an explicit, well-labeled regression test for the
  reader's pre-existing behavior after this task's larger refactor (dispatch across 6 type names
  instead of one fixed validation) — correctly verifies backward compatibility wasn't broken.

## Detailed Findings
None.

## Cross-File Observations
`CustomGlslEffectStillWorks` is nearly identical to `CnjEffectTests.cpp`'s own
`LoadsRealCnjFixture` (same shard) — reasonable duplication given this file's specific purpose is
proving the *stock-effect* dispatch refactor didn't regress the pre-existing custom shape, not
testing custom-GLSL loading as its primary subject.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The adversarial-review-found error-message-priority regression test and the
compare-against-a-freshly-constructed-instance default-value test design are both genuinely
mature testing practices.

## Final Assessment
No findings.
