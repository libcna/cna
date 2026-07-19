# Audit: tests/Microsoft/Xna/Framework/Content/CnjModelSharedAnimationClipTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Content/CnjModelSharedAnimationClipTests.cpp` (238 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-content` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Model`'s `"animations"` field referencing either a raw
  `.clip.bin` blob or a shareable standalone `.cnj` `AnimationClip` asset (NOXNA content pipeline
  extension)
- Main related tests: N/A (this IS a test file)

## Purpose
Tests that two different `Model`s can share one `.cnj`-based `AnimationClip` asset (loaded/cached
once through the normal `ContentManager` path), that the original raw `.clip.bin` shape still works
unchanged, and that a missing referenced clip throws.

## Executive Verdict
Correct, thorough. `TwoModelsShareOneCnjAnimationClip` genuinely proves sharing by asserting both
models' `SkinningData::AnimationClips["Walk"]` entries have matching, correctly-loaded content
(duration, track count, key count) — not merely that neither model crashed.
`RawClipBinStillWorksUnchanged`'s own top comment correctly scopes what this file covers vs. what's
"unaffected by this change" (the binary-blob shape, covered elsewhere) — an accurate, non-redundant
division of test responsibility.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Complements `CnjAnimationClipTests.cpp` (audited separately, standalone `AnimationClip` loading) by
covering the `Model`-side consumption of the same asset type.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`TwoModelsShareOneCnjAnimationClip` is a genuinely meaningful sharing/caching proof, not a
superficial "both loaded fine" check.

## Final Assessment
No findings.
