# Audit: src/Microsoft/Xna/Framework/Graphics/TextureCube.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/TextureCube.cpp`
- Audit status: AUDITED (full read, 349 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/TextureCube.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `TextureCube`'s constructors, per-face `SetData`/`GetData`, and `DDSFromStreamEXT` (a
from-scratch DDS/DXT1/3/5 cube-map decoder using `DxtUtil`).

## Executive Verdict
Correct and confirms the "cube-face mip regeneration" question raised for this batch: `SetData`
(lines 144-170) validates `face` via `IsValidCubeMapFace` and passes it straight through to
`backend_->SetData(static_cast<int>(face), level, ...)` — the XNA-facing call is always scoped to
exactly one face, never triggering a whole-cube operation itself. `GetTypeName()` correctly returns
the fully-qualified name, unlike the sibling `Texture2D`'s isolated regression.

## Checklist Results
- `IsValidCubeMapFace` (lines 137-142): a real, `CubeMapFace`-range validation with no direct FNA
  equivalent (FNA's C# enum-typed parameter has no runtime range check by default either) — a
  reasonable CNA hardening against an invalid `static_cast`-constructed `CubeMapFace`, not a defect.
- `SetData`/`GetData` (6-arg and 10-arg overloads): bounds-checking shape (rectangle validity against
  the mip level's real dimensions, `elementCount` against the requested pixel count) matches
  `Texture2D`'s own equivalent logic, itself matching FNA's real validation in spirit.
- `DDSFromStreamEXT` (lines 251-348): a from-scratch DDS header parser (magic number, header size,
  `DDSD_HEIGHT|DDSD_WIDTH` flags, pixel-format size, `DDSCAPS_TEXTURE`/`DDSCAPS2_CUBEMAP`,
  DXT1/3/5 FourCC codes) — cross-checked field-by-field against FNA's real `Texture.ParseDDS`
  (`Texture.cs` lines 267-510) and confirmed to check the same fields at the same byte offsets,
  correctly rejecting non-cube DDS files (`System::FormatException`, matching FNA's own
  `FormatException("This file does not contain cube data!")`) and non-DXT1/3/5 formats
  (`System::NotSupportedException`, matching FNA's `NotSupportedException` for unsupported FourCC
  codes). Explicitly and honestly documents its own scope reduction versus FNA (DXT1/3/5-compressed
  only, always decompressed to `SurfaceFormat::Color` rather than uploaded as a real compressed GPU
  format — cited against `NEXT.md`'s documented "SurfaceFormat support is Color-only for real GPU
  formats" limitation).

## Detailed Findings

### MEDIUM — raw `std::`/mixed exception types instead of consistently using `System::` (shard-wide
recurring pattern)
`SetData`/`GetData` (lines 148-156, 190-196) use `std::out_of_range`/`std::invalid_argument` for
their own parameter validation, while `DDSFromStreamEXT` (lines 258, 267-303, 326) correctly and
consistently uses `System::NotSupportedException`/`System::FormatException` for its own errors — an
internally inconsistent mix within the same file. FNA's real `SetData<T>`/`GetData<T>` throw
`ArgumentNullException`/`ArgumentException` for the comparable parameter-validation cases (matching
`TextureCube.cs` lines 142-145, 263-273).

## Cross-File Observations
`colorsToRgba`/`rgbaToColors` (lines 105-123) duplicate `Texture3D.cpp`'s own identically-named,
identically-shaped helpers — see that file's report for the same maintainability note. The DDS
constants/`ReadU32LE`/`CalculateDDSLevelSize` local helpers (lines 216-249) also duplicate logic
present in `Texture2D.cpp`'s own `TryDecodeDds` — see `Texture.hpp`'s audit report for the
maintainability observation about this not being centralized in the shared `Texture` base class the
way FNA structures it.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
- `DDSFromStreamEXT`'s field-by-field DDS header validation is thorough and correctly matches FNA's
  real `ParseDDS` at the byte-offset level — a genuinely careful from-scratch reimplementation, not
  a superficial approximation.
- Confirms this class's own contribution to the previously-found cube-mip-regeneration backend
  defect is nil — the XNA-facing API is correctly, unambiguously per-face throughout.

## Final Assessment
One MEDIUM finding: inconsistent exception types within the same file (raw `std::` in `SetData`/
`GetData`, correct `System::` types in `DDSFromStreamEXT`).
