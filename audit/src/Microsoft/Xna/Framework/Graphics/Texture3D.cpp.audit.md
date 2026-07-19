# Audit: src/Microsoft/Xna/Framework/Graphics/Texture3D.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/Texture3D.cpp`
- Audit status: AUDITED (full read, 193 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Texture3D.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `Texture3D`'s constructor, `SetData`/`GetData` (both the 2/3-arg full-texture
convenience overloads and the 10-arg sub-box overload), and `SetDataPointerEXT`.

## Executive Verdict
Correct box-bounds validation matching FNA's own `SetData<T>`/`GetData<T>` box-validity checks
(`left < 0 || left >= right`, etc.), correct fully-qualified `GetTypeName()`. Shares this shard's
recurring exception-type finding.

## Checklist Results
- `SetData(level, left, top, right, bottom, front, back, data, startIndex, elementCount)` (lines
  118-137): validates `data` non-null, `elementCount > 0`, `startIndex >= 0`, `level >= 0`, box
  validity (`left/top/front` each checked against their paired `right/bottom/back`), and
  `elementCount` against the requested voxel count — matches FNA's own `(left < 0 || left >= right)
  || (top < 0 || top >= bottom) || (front < 0 || front >= back)` check (`Texture3D.cs` lines
  252-257) exactly in shape, with CNA additionally validating `elementCount`/`startIndex`/`level`
  individually (FNA's generic overload gets some of this "for free" from `T[]`'s own length, but
  not all — CNA's explicit checks here are at least as strict).
- `GetData` (10-arg overload, lines 169-192): identical bounds-check shape to `SetData`, plus
  `Texture::ValidateGetDataFormat(format_, 4)` matching FNA's own call to the same-named method.
- `GetTypeName()` (lines 84-88): correctly returns `"Microsoft.Xna.Framework.Graphics.Texture3D"` —
  confirmed NOT sharing `Texture2D`'s isolated `GetTypeName()` regression (see that file's report).

## Detailed Findings

### MEDIUM — raw `std::` exceptions instead of `System::` exception types (shard-wide recurring
pattern)
Every throw site in `SetData`/`SetDataPointerEXT`/`GetData` (lines 122-130, 142-143, 172-183) uses
`std::invalid_argument`/`std::out_of_range` instead of `System::ArgumentNullException`/
`ArgumentException`/`ArgumentOutOfRangeException`. FNA's own equivalents throw
`ArgumentNullException`/`ArgumentException` in the comparable cases (`Texture3D.cs` lines 117-120,
151-154, 241-257). Same recurring cross-cutting pattern as `Texture.cpp`/`Texture2D.cpp` (audited in
this same batch).

## Cross-File Observations
`colorsToRgba`/`rgbaToColors` helpers (lines 93-104, 150-155) mirror `TextureCube.cpp`'s own
identically-named, identically-shaped helpers almost verbatim — a real, minor duplication that could
be a shared free function instead, though not a behavioral defect.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Box-bounds validation is thorough and correctly modeled on FNA's own real validation logic; the
`GraphicsProfile`-ceiling enforcement (`ValidateVolumeSizeForProfileEXT`, D3D9-only, lines 35-51)
correctly uses `System::NotSupportedException` — a positive counter-example within the very same
file that otherwise exhibits the raw-`std::`-exception pattern elsewhere.

## Final Assessment
One MEDIUM finding: raw `std::` exceptions instead of `System::` exception types in `SetData`/
`SetDataPointerEXT`/`GetData`.
