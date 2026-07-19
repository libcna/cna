# Audit: src/Microsoft/Xna/Framework/Graphics/Texture.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/Texture.cpp`
- Audit status: AUDITED (full read, 137 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Texture.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements the static `SurfaceFormat` size/alignment/validation helpers and `Texture`'s protected
constructor/`Dispose(bool)`.

## Executive Verdict
The size/alignment table logic is verified correct and complete, matching FNA's own switch
statements case-for-case. One MEDIUM, shard-wide-recurring finding: every throw site in this file
uses a raw `std::` exception type instead of this project's own `System::` exception hierarchy,
diverging from FNA's real exception type in every case.

## Checklist Results
- `GetBlockSizeSquaredEXT` (lines 15-51): all 22 cases match FNA's own switch (16 for block-compressed
  formats, 1 for everything else).
- `GetFormatSizeEXT` (lines 53-94): all byte sizes (1/2/4/8/16, keyed by format) match FNA's own
  switch exactly.
- `GetPixelStoreAlignment` (lines 96-99): `std::min(8, GetFormatSizeEXT(format))`, matching FNA's
  `Math.Min(8, GetFormatSizeEXT(format))` and its own cited OpenGL 2.1 §3.6.1 rationale.
- `ValidateGetDataFormat` (lines 101-107): `GetFormatSizeEXT(format) % elementSizeInBytes != 0` throw
  condition matches FNA's own check exactly.
- `Dispose(bool)` (lines 128-136): correctly unbinds from both `Textures` and `VertexTextures`
  sampler collections before delegating to `GraphicsResource::Dispose`, matching FNA's
  `GraphicsDevice.Textures.RemoveDisposedTexture(this)` / `VertexTextures.RemoveDisposedTexture(this)`
  calls in `Texture.Dispose(bool)`.

## Detailed Findings

### MEDIUM — every throw in this file uses a raw `std::` exception instead of this project's own
`System::` exception type, diverging from FNA's real exception type in every case
- `GetBlockSizeSquaredEXT`'s default case (line 49) throws `std::out_of_range`; FNA's equivalent
  default case throws `ArgumentException("Should be a value defined in SurfaceFormat", "Format")`
  (`Texture.cs` line 137) — the correct project-convention type would be `System::ArgumentException`,
  not `std::out_of_range` (which models an out-of-bounds index/iterator, not an invalid enum value —
  a semantic mismatch on top of the raw-`std::` issue).
- `GetFormatSizeEXT`'s default case (line 92): same issue, same FNA equivalent (`ArgumentException`
  at `Texture.cs` line 180).
- `ValidateGetDataFormat` (lines 103-106) throws `std::invalid_argument`; FNA's equivalent throws
  `ArgumentException` (`Texture.cs` lines 200-205) — again, `System::ArgumentException` is the
  correct project-convention type.
- `ValidateFormat` (lines 109-116, `NOXNA`) throws `std::runtime_error` — since this method has no
  FNA equivalent to compare against, `System::NotSupportedException` (already used elsewhere in
  this same shard for "format/size not yet supported" cases, e.g. `Texture2D.cpp`'s
  `ValidateTextureSizeForProfileEXT`) would be the more consistent choice than a raw `std::`
  exception, though this one is lower priority since it's a CNA-only helper with no FNA contract to
  match.

This is the same recurring cross-cutting pattern already flagged multiple times elsewhere in this
audit (`ContentLoadException`, `PropertyDictionary`, etc.) — here affecting the base class every
concrete texture type (`Texture2D`, `Texture3D`, `TextureCube`) calls into for format validation, so
the practical reach is wide even though this specific file is short.

## Cross-File Observations
`Texture2D`/`Texture3D`/`TextureCube` (all audited in this same batch) call
`Texture::ValidateGetDataFormat`/`ValidateFormat` directly and share this same exception-type issue
in their own call sites too (see their respective reports).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The size-table logic itself (the actual numeric content, as opposed to the exception types thrown
on invalid input) is a verified-correct, complete port of FNA's own tables.

## Final Assessment
One MEDIUM finding: raw `std::` exceptions instead of `System::ArgumentException`/
`NotSupportedException`, present at every throw site in this file.
