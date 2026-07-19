# Audit: include/Microsoft/Xna/Framework/Graphics/Texture.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/Texture.hpp`
- Audit status: AUDITED (full read, 86 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Texture.cs`
- Main related tests: not independently located in this pass

## Purpose
Abstract base class for `Texture2D`/`Texture3D`/`TextureCube`, holding `Format`/`LevelCount` and the
static `SurfaceFormat` size/alignment/validation helpers FNA also exposes at this level.

## Executive Verdict
Mostly correct, matching FNA's static format-size table logic exactly, but shares this shard's
recurring exception-type issue (see the paired `.cpp` report for specifics) and omits FNA's DDS
parsing/mip-level-count helpers (a reasonable, disclosed scope reduction, not a silent gap — see
Cross-File Observations).

## Checklist Results
- `GetBlockSizeSquaredEXT`/`GetFormatSizeEXT`/`GetPixelStoreAlignment`/`ValidateGetDataFormat` are
  all real FNA members (`Texture.cs`'s `#region Static SurfaceFormat Size Methods`), correctly
  ported as `static`. `ValidateFormat` is correctly `NOXNA`-tagged as a CNA-only addition.
- Visibility: FNA's `GetPixelStoreAlignment`/`ValidateGetDataFormat` are `internal` — ported here as
  fully `public static`, a pragmatic C++ accommodation (no `internal` equivalent) consistent with
  this shard's other enum/interface files, not separately flagged again here.

## Detailed Findings
See the paired `.cpp` report for the concrete exception-type finding (MEDIUM) — this header's own
declarations carry no `@throws` documentation for any of the four static validators, so the
implementation-level issue isn't visible from the header alone.

## Cross-File Observations
FNA's `Texture.cs` additionally declares `CalculateMipLevels`, `CalculateDDSLevelSize`, and
`ParseDDS` (a hand-rolled DDS header parser reused by `Texture2D`/`TextureCube`'s own
`DDSFromStreamEXT`). This header has none of these three — `Texture2D.cpp` and `TextureCube.cpp`
(both audited in this same batch) instead each implement their own local, independent DDS-parsing
logic (`TryDecodeDds`/inline DDS constants respectively) rather than sharing one base-class
implementation. This is a real, disclosed simplification (CNA supports only DXT1/3/5 cube/2D DDS via
`DxtUtil`, not FNA's full format/DX10-header range) rather than a silent omission — but the
duplicated-not-shared parsing logic between `Texture2D`/`TextureCube` is worth a maintainability
note: any future DDS-format-support fix would need to be applied in two places instead of one shared
`Texture` helper the way FNA structures it.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The static `SurfaceFormat` size/alignment tables (`GetBlockSizeSquaredEXT`/`GetFormatSizeEXT`) are
verified byte-for-byte identical to FNA's own switch statements (confirmed by direct comparison of
every case).

## Final Assessment
No new findings in the header beyond the `.cpp`-level exception-type issue (see that report) and
the disclosed DDS-parsing-not-shared observation above.
