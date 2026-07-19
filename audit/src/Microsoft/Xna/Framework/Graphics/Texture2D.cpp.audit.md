# Audit: src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp`
- Audit status: AUDITED (full read, 873 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Texture2D.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements every `Texture2D` constructor, `SetData`/`GetData` (both the full-texture and
level+rect overloads), `FromStream` (with an internal DDS/DXT1/3/5 fast path via `DxtUtil`, falling
back to SDL_image for everything else), `SaveAsPng`/`SaveAsJpeg`, and the CNA-only cache/testing
factory helpers.

## Executive Verdict
Substantively correct and, in several places, more careful than a literal FNA port would be (see
Positive Findings) — but contains one confirmed MEDIUM defect (`GetTypeName()`) and shares this
shard's recurring exception-type pattern pervasively across essentially every public entry point.

## Checklist Results

### `GetTypeName()` — CONFIRMED MEDIUM
```cpp
const std::string& Texture2D::GetTypeName() const
{
    static const std::string name = "Texture2D";
    return name;
}
```
(lines 206-210). Every sibling class in this exact file family returns the fully-qualified `.NET`
name as this project's own `CHECKLIST.md`/`CLAUDE.md` require: `Texture3D::GetTypeName()` returns
`"Microsoft.Xna.Framework.Graphics.Texture3D"`, `TextureCube::GetTypeName()` returns
`"Microsoft.Xna.Framework.Graphics.TextureCube"`, `RenderTarget2D::GetTypeName()` returns
`"Microsoft.Xna.Framework.Graphics.RenderTarget2D"`, `RenderTargetCube::GetTypeName()` returns
`"Microsoft.Xna.Framework.Graphics.RenderTargetCube"` (all confirmed via direct grep/read of each
file in this same batch). `Texture2D` — almost certainly the single most commonly instantiated XNA
type in any real game (every sprite, every loaded image) — is the one outlier returning a bare,
non-fully-qualified name. Any code path that reflects on a `Texture2D*`'s reported type name (e.g. a
generic `System::Object`-based diagnostic/serialization/logging helper elsewhere in the codebase)
would silently get the wrong string for this one, extremely common type while getting the right
string for every structurally-identical sibling.

### `SetData`/`GetData` bounds-checking — correct, matches or exceeds FNA's real validation
- `SetData(const Color*, int elementCount)` (lines 221-243): checks `elementCount < total` (pixel
  count), matching the spirit of FNA's generic overload's implicit length checks.
- `SetData(level, rect, data, startIndex, elementCount)` (lines 245-316): validates `data`
  non-null, `startIndex >= 0`, `level >= 0`, rectangle bounds against the mip level's real
  dimensions, and `elementCount` against the requested region's pixel count — matches FNA's own
  `SetData<T>`'s `startIndex < 0`/`data.Length < elementCount+startIndex`/`requiredBytes >
  availableBytes` checks in spirit (FNA's version additionally accounts for block-compressed
  formats via `GetFormatSizeEXT`/`GetBlockSizeSquaredEXT`, moot here since `Texture::ValidateFormat`
  currently only allows `SurfaceFormat::Color`, so the 1-Color-per-pixel simplification is
  consistent with this codebase's current real scope, not a hidden narrowing).
- `GetData` (3 overloads, lines 329-455): bounds-checked equivalently, with an additional,
  well-reasoned `gpuOnlyContent_` fallback path (see Positive Findings) that FNA's own
  implementation has no need for (FNA always reads back from the real GL texture regardless of
  source; CNA's CPU-shadow-first design needs this extra case).
- A genuinely reachable pointer-based API limitation (not a bug): since `data` is a raw `Color*`
  with no length carried alongside it (unlike FNA's `T[] data`, whose `.Length` FNA validates
  against directly), this API has no way to detect a caller passing a `data` buffer smaller than
  `startIndex + elementCount` claims — an inherent consequence of the pointer-based API surface
  choice (see the paired `.hpp` report), not something achievable to check here regardless of
  effort.

## Detailed Findings

### MEDIUM — `GetTypeName()` returns `"Texture2D"` instead of the fully-qualified `.NET` type name
See Checklist Results above for the full analysis and cross-file comparison confirming this is an
isolated miss, not a shard-wide pattern.

### MEDIUM — pervasive raw `std::` exception usage instead of this project's own `System::`
exception hierarchy, across nearly every throw site in this file
Confirmed at (non-exhaustive, representative sample): `SetData` (`std::out_of_range` line 226,
`std::invalid_argument` line 249, `std::out_of_range` lines 251/253/265/267), `GetData`
(`std::invalid_argument` line 332, `std::out_of_range` lines 334/358/363/384/386/410/412/425),
`SaveAsPng`/`SaveAsJpeg` (`std::invalid_argument`/`std::runtime_error` throughout). FNA's real
equivalents throw `ArgumentNullException`/`ArgumentOutOfRangeException`/`ArgumentException` in every
comparable case (confirmed against `Texture2D.cs`'s own `SetData<T>`/`GetData<T>` bodies). This is
the same recurring cross-cutting pattern flagged elsewhere in this audit, here spanning the single
largest and most heavily-used file yet found to exhibit it.

## Cross-File Observations
`FromStream`'s DDS/DXT1/3/5 fast-path decoder (`TryDecodeDds`, lines 462-497) duplicates
`TextureCube.cpp`'s own independent DDS-parsing logic rather than sharing a common `Texture`-level
helper — see `Texture.hpp`'s audit report for the corresponding maintainability note.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
- The `gpuOnlyContent_` flag and its `GetData()` fallback-to-real-backend-readback path (lines
  336-357, 397-424) is a genuinely well-designed mechanism specifically for `RenderTarget2D`
  (constructed via the protected pre-built-backend constructor): it correctly distinguishes "no CPU
  shadow because this is a render target whose content comes from GPU rendering" from "no CPU shadow
  because it was freed due to disabled context recovery" — the former transparently falls back to a
  real backend readback, the latter still throws, matching each scenario's real semantics rather than
  conflating them.
- `SetData(level, rect, ...)`'s `coversFullLevel` guard (lines 274-279) is a genuine, well-reasoned
  correctness fix beyond what a literal port would need: it explicitly prevents a partial-region
  update from silently corrupting already-uploaded GPU pixels outside the requested region when the
  CPU shadow had to be lazily re-zeroed.
- `GetJpegSaveQuality()` (lines 714-723) correctly mirrors FNA's own `FNA_GRAPHICS_JPEG_SAVE_QUALITY`
  environment-variable convention, including the same 100-quality fallback.

## Final Assessment
Two MEDIUM findings: the isolated `GetTypeName()` regression, and the pervasive raw-`std::`-exception
pattern already flagged elsewhere in this audit, here at its widest single-file scope so far.
