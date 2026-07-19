# Audit: docs/fna-reference-harness.md

## Metadata
- Source file: `docs/fna-reference-harness.md` (134 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (tooling how-to + status)
- XNA/FNA relevance: documents the `tools/fna-reference` (C#)/`tools/cna-reference` (C++)/
  `scripts/compare-fna-reference.py` cross-implementation comparison harness
- Related audit: `tools-fna-reference` shard (7 files, fully audited this session, confirmed
  trustworthy), `scripts/xna-diff.py.audit.md` (a sibling but distinct harness for pixel comparison,
  not this text/JSON one)

## Purpose
Explains why and how to regenerate FNA-vs-CNA reference JSON dumps for enums, state presets,
`PackedVector`, and `Viewport.Project`/`Unproject` — genuine independent confirmation against a
*running* real FNA.dll, not just a second reading of FNA's source.

## Executive Verdict
Accurate and consistent with the `tools-fna-reference` shard's own audit this session (which
confirmed the C# generator/C++ dump/comparison-script chain is fundamentally trustworthy). This
document's own account of "what Task 479's first real run found" (3 harness bugs fixed before
finding anything real, then one genuine divergence — `IndexElementSize`'s numeric values,
`SixteenBits=0`/`ThirtyTwoBits=1` in FNA vs `16`/`32` in CNA at the time) matches
`docs/graphics-compatibility-report.md`'s own citation of the identical bug (Task 921, fixed
2026-07-09) — cross-checked and consistent between the two documents (both audited in this batch).

## Checklist Results
- The "why some categories are still missing" section (`BasicEffect`, `SpriteFont`, reference
  screenshots — all deferred, needing a real `GraphicsDevice`/native `FNA3D.so`/compiled `.xnb`
  content) gives specific, technically plausible blockers rather than vague "not done yet" —
  consistent with the general engineering discipline observed elsewhere in this project's tooling
  documentation.
- The one-directional comparison design (every FNA key must exist and match on the CNA side;
  CNA-only NOXNA extension keys like `PrimitiveType.PointListEXT` are not flagged) is a sound,
  correctly-reasoned design choice for this specific comparison's purpose.
- Explicitly states this harness is "not registered as a ctest" and is "a manually-invoked developer
  verification workflow, not an automated regression gate" — an honest scope statement, not
  overclaiming automation that doesn't exist.

## Detailed Findings
None.

## Cross-File Observations
- Consistent with `docs/graphics-compatibility-report.md`'s citation of the same `IndexElementSize`
  bug and its Task 921 fix date (both audited in this batch).
- Distinct from, and should not be confused with, `scripts/xna-diff.py` (audited earlier this
  session as part of `tools-xna-oracle`'s cross-cutting scope) — that script does real-time pixel
  comparison of rendered PNGs via `tools/xna-oracle`; this harness instead does JSON-value
  comparison of non-rendering API surfaces (enums, state presets, `PackedVector`, `Viewport` math)
  via `tools/fna-reference`/`tools/cna-reference`. The two are complementary, non-overlapping
  verification mechanisms, both real and both already audited as trustworthy this session.

## Missing or Weak Tests
The document itself notes this harness's outputs are not wired into CI/ctest — a real, disclosed
limitation (manual-only) rather than a hidden gap.

## Positive Findings
The explicit "why this exists" framing (a second hand-derivation of an FNA formula can repeat the
same reading mistake as the first; running the real compiled FNA.dll avoids that specific failure
mode) is a clear, well-reasoned justification for the harness's existence, and its account of the
first real run's own bug-hunting (harness bugs found and fixed before any real divergence was
found) is honest, specific, and consistent with other similarly-rigorous accounts elsewhere in this
project's documentation.

## Final Assessment
No findings. An accurate document, cross-consistent with sibling docs and this session's own
`tools-fna-reference` shard audit.
