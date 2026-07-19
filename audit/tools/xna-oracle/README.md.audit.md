# Audit: tools/xna-oracle/README.md

## Metadata
- Source file: `tools/xna-oracle/README.md` (533 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation
- XNA/FNA relevance: documents the project's own FNA/XNA-comparison oracle tooling in full —
  scene format spec, both-sides build instructions, diff instructions, and an exhaustive
  scene-by-scene status/history log
- Main related tests: N/A (documentation); describes the workflow around
  `Oracle.cs`/`CnaOracleRender.cpp`/`scripts/xna-diff.py`

## Purpose
Documents the scene text-format spec (full key table), how to build/run the real-XNA side under
Wine, how to build/run the CNA side, how to diff the two PNGs, and an exhaustive log of every
scene's status, hand-derived expected values, and every real bug the corpus has found historically.

## Executive Verdict
One of the most thorough, honest, and self-critical README files encountered in this audit. It
documents not just "how to use this tool" but a full, dated history of real bugs the tool has
caught — in CNA's own D3D9 backend, in the scene corpus's own documentation, and in a CTest's own
color-packing — with clear before/after descriptions and, in the most significant case, explicit
mutation-testing verification (reverting a fix and confirming the relevant CTest then fails).
This transparency is itself strong evidence of the tool's trustworthiness: a fabricated or
uncritical account would not include entries admitting the corpus's own documentation was wrong
twice (`envmap_quad.scene`, `skinned_quad.scene`).

## Checklist Results
- Scene format key table (cross-checked in this pass against all 39 actual `.scene` files via a
  `sort -u` extraction of every key used) matches exactly — no undocumented or stray keys found in
  any scene file.
- Build instructions for the XNA side explicitly warn that the real `csc.exe` targets an old
  C# language level with no C# 6+ features, citing a real, concrete incident
  (`Oracle.cs` briefly shipped with an expression-bodied `ParseBool` and the real compiler rejected
  it) — a specific, credible detail rather than generic Wine-compatibility boilerplate.
- Diff instructions correctly document `scripts/xna-diff.py`'s dependency on Pillow and its
  `--tolerance` flag, and claim the script itself was mutation-tested (a 1-pixel-mutated copy of a
  passing PNG correctly reported as `FAIL: 1/65536 pixels differ ... max per-channel delta=1` at
  the default `--tolerance=0`, and correctly passes at `--tolerance=1`) — this specific script is
  outside this shard's own file list (`scripts/`, not `tools/xna-oracle/`) and could not be
  independently re-verified in this pass; see Cross-File Observations.
- The `SpriteSortMode.Texture` gap is documented honestly as a real, permanent limitation (FNA's
  own `TextureComparer` sorts by `Object.GetHashCode()`, an implementation-defined identity hash
  with no reproducible ordering — no deterministic scene can exist for it) rather than silently
  omitted or falsely claimed covered.
- Final corpus-completeness claims (all 5 Stock Effects, all 8 `AlphaFunction` values, all 3
  `WeightsPerVertex` values, all 4 `PrimitiveType` values, "all pixel-perfect") were independently
  re-derived and cross-checked against the actual 39 scene files in this pass (see each scene's own
  audit report) and found accurate.

## Detailed Findings
None. (The historical bugs this file documents — the dangling-pointer bug, the two scene
documentation errors, the PNG-alpha=0 encoder quirk, the D3D9 sprite Z-clipping bug, the CTest
color-packing bug, and the D3D9 half-pixel-offset mutation-testing finding — are all already fixed,
per both this file's own account and confirmation in the corresponding source files audited
alongside this one. None are open defects.)

## Cross-File Observations
- `scripts/xna-diff.py` is described in detail here (tolerance semantics, mutation-test claim) but
  is not part of this shard's 42-file manifest and was not independently reviewed in this pass —
  the single biggest completeness gap in verifying this shard's overall trustworthiness claim, since
  the actual pixel-comparison logic is as foundational as the two renderers themselves. Recommend
  confirming (with the parent coordinator) whether any other shard's manifest covers `scripts/*.py`.
- Consistent, verified-accurate cross-references to FNA source line numbers/behavior throughout
  (e.g. `AlphaTestEffect.cs`'s `threshold = 0.5f/255f`, `SkinnedEffect.fx`'s weighted-sum `Skin()`
  formula) — matches what was independently confirmed while reading the individual scene files.

## Missing or Weak Tests
N/A — documentation file.

## Positive Findings
The willingness to document two of the corpus's own past documentation errors (`envmap_quad.scene`,
`skinned_quad.scene` each silently exercising a different code path than their own header comment
claimed, both due to relying on an unstated XNA default) is a strong positive signal: it shows the
tool's authors were actively hunting for and correcting subtle self-inconsistencies rather than
accepting "looks plausible" as good enough.

## Final Assessment
No findings beyond a scope-boundary caveat (the pixel-diff script itself is out of this shard's
file list and could not be independently verified here). The documentation is accurate, thorough,
and consistent with the actual code and scene corpus.
