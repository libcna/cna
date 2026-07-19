# Audit: include/Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp`
- Audit status: AUDITED (full read, 93 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/EffectTechnique.cs`
- Main related tests: not independently located in this pass

## Purpose
Represents a rendering technique within an effect, containing one or more passes (`Name`, `Passes`,
`Annotations`).

## Executive Verdict
Correct. The `NOXNA`-tagged `getIdInternal()`/monotonic-id design is a well-reasoned, clearly
documented C++ substitute for FNA's opaque native `TechniquePointer` (`IntPtr`) equality check:
since this port has no equivalent native handle to compare, a stable, construction-order-independent
identity token serves the same purpose (`EffectPass::Apply()` uses it to detect "does this pass
belong to the effect's currently-selected technique") without depending on C++ object addresses,
which — unlike FNA's opaque native pointer — could otherwise coincidentally collide after a
technique object is moved/reallocated within its owning collection.

## Checklist Results
- Doxygen coverage: complete, including an explicit `@note` disclosing `getIdInternal()`'s NOXNA
  status and rationale.

## Detailed Findings
None.

## Cross-File Observations
See `EffectPass.hpp.audit.md`/`.cpp.audit.md` for the consuming side of this identity mechanism.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Choosing a stable integer identity token over raw pointer/address comparison is a correct,
forward-looking design choice — it remains correct even if a future refactor changes
`EffectTechniqueCollection`'s storage strategy in a way that would invalidate a raw-address-based
comparison.

## Final Assessment
No findings.
