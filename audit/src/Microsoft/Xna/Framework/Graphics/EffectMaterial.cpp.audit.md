# Audit: src/Microsoft/Xna/Framework/Graphics/EffectMaterial.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/EffectMaterial.cpp`
- Audit status: AUDITED (full read, 27 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/EffectMaterial.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements the cloning constructor, `GetTypeName()`, `Clone()`, and the empty `OnApply()` override.

## Executive Verdict
Correct. `GetTypeName()` returns the correct fully-qualified name
(`"Microsoft.Xna.Framework.Graphics.EffectMaterial"`). `OnApply()` is an empty override — matches
FNA's real `EffectMaterial`, which also has no `OnApply()` override at all (inherits `Effect`'s own
empty virtual base implementation) — functionally identical either way.

## Checklist Results
No issues found.

## Detailed Findings
None. See the paired `.hpp` report for the disclosed `Clone()`-type-preservation divergence from
FNA's literal (arguably buggy) behavior.

## Cross-File Observations
See `EffectMaterial.hpp.audit.md`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Minimal, correct, matches FNA's real minimal implementation.

## Final Assessment
No findings.
