# Audit: include/Microsoft/Xna/Framework/Graphics/Texture2D.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/Texture2D.hpp`
- Audit status: AUDITED (full read, 339 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Texture2D.cs`
- Main related tests: not independently located in this pass

## Purpose
The most heavily-used concrete texture type: 2D textures with `SetData`/`GetData`, `FromStream`
loading, `SaveAsPng`/`SaveAsJpeg`, plus a large NOXNA extension surface for CNA's own asset pipeline
(weak-reference caching, cache reconstruction, test-only factory helpers).

## Executive Verdict
Overall correct and reasonably faithful to FNA's public `SetData`/`GetData`/`FromStream`/
`SaveAsPng`/`SaveAsJpeg` contract, adapted from FNA's generic `T[]` array API to a concrete
`Color*`+`elementCount` pointer API (a defensible, disclosed-by-necessity simplification given C++
has no direct equivalent to C#'s `where T : struct` generic constraint the way FNA's API relies on).
One MEDIUM defect found: `GetTypeName()` (see paired `.cpp` report) returns just `"Texture2D"`
instead of the fully-qualified `.NET` name every sibling texture/render-target type in this same
shard correctly returns.

## Checklist Results
- Doxygen coverage: complete on every public member.
- `NOXNA` tagging: correctly applied to every CNA-only addition (`Texture2D(assetName)`,
  `SetDataRGBA`, `GetBackend`/`GetBackendWeak`/`GetCpuPixelsWeak`, `CreateFromPixels`,
  `CreateCpuOnlyForTests`, `CreateWithBackendForTests`, `ReconstructFromCache`, `HasBackend`).
- Reduced API surface vs. FNA: FNA's `SetData<T>`/`GetData<T>` are generic over any `struct` (used
  in practice for `Color`, `Vector2`, custom vertex-like pixel types, etc. via
  `MarshalHelper.SizeOf<T>()`); CNA's version is hardcoded to `Color*` only. This is a real,
  disclosed API-surface reduction (not silently narrower — the header consistently documents every
  overload in terms of `Color`), consistent with this codebase's broader "Color-only" `SurfaceFormat`
  scope limitation (`Texture::ValidateFormat` only accepts `SurfaceFormat::Color` today) — not scored
  as an independent defect since a fully generic C++ template equivalent would be a much larger
  design undertaking out of scope for a single-file finding.

## Detailed Findings
See the paired `.cpp` report for the confirmed MEDIUM `GetTypeName()` defect and the recurring
exception-type finding.

## Cross-File Observations
`SetData(int level, const Rectangle* rect, ...)`'s doc comment (lines 101-105) correctly and
proactively documents a real, non-obvious throw condition (partial update after the CPU-side pixel
shadow was freed via disabled context recovery) — confirmed consistent with the `.cpp`'s actual
`coversFullLevel` guard logic.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The protected pre-built-backend constructor (used exclusively by `RenderTarget2D`) and its
`gpuOnlyContent_` flag are a well-reasoned mechanism for `GetData()`'s dual CPU-shadow/GPU-readback
fallback behavior — confirmed consistent with `RenderTarget2D`'s own construction path (audited
separately).

## Final Assessment
One MEDIUM finding (via the paired `.cpp`): `GetTypeName()` returns a bare class name instead of the
fully-qualified `.NET` type name.
