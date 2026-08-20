# Audit: include/CNA/Internal/Xnb/StockEffectContentTypeReaders.hpp

## Metadata
- Source file: `include/CNA/Internal/Xnb/StockEffectContentTypeReaders.hpp`
- Audit status: AUDITED (full read, 115 lines)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ header
- XNA/FNA relevance: matches FNA's `BasicEffectReader`/`AlphaTestEffectReader`/`DualTextureEffectReader`/
  `EnvironmentMapEffectReader`/`SkinnedEffectReader`
- Main related tests: not independently located in this pass

## Purpose
Declares the 5 stock-effect `.xnb` readers, each targeting `std::shared_ptr<Effect>` (the common base, not
the concrete effect type) for a specifically-reasoned C++ dispatch constraint.

## Executive Verdict
Healthy -- a genuinely well-reasoned architectural note explaining a real C++-vs-C# divergence (why the
common base type is required here, unlike FNA's own RTTI-based `ReadSharedResource<Effect>()`).

## Checklist Results

### `std::shared_ptr<Effect>` erasure: correctly reasoned, not arbitrary
The file-level comment precisely identifies *why* every stock-effect reader must erase to the same
`shared_ptr<Effect>` base rather than its own concrete type: `ReadSharedResource<std::shared_ptr<Effect>>()`
(used by a `ModelMeshPart`'s effect reference) needs `std::any_cast<T>` to succeed regardless of which of
the 5 concrete effect types a given file actually used, and `std::any_cast` (unlike C#'s RTTI-based cast)
requires an exact type match -- correctly identified as a genuine C++-specific constraint FNA's own C#
implementation doesn't share.

### `SetOwnedTexture()`/`SetOwnedTexture2()`/`SetOwnedEnvironmentMap()`: correctly scoped NOXNA additions
Clearly documented as letting a standalone content-loaded effect keep its own texture reference alive
(matching XNA's real GC-tracked `Effect.Texture`), since a standalone effect has no external owner the way
a `Model`'s shared texture pool does -- a sensible, explicitly-scoped NOXNA extension.

## Detailed Findings
None in this header -- see the paired `.cpp`.

## Cross-File Observations
Each `Read()` correctly relies on `ContentReader::ReadExternalReference<T>()` for its texture field(s),
consistent with `plans/plan_xnb.md XNB-35`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
A rare, genuinely insightful architectural note precisely identifying a real C++-vs-C#-RTTI divergence and
its correct resolution.

## Final Assessment
No issues found.
