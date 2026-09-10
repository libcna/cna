# Texture2D stream decoding — Supported Encoded Formats

> Covers classic `Texture2D::FromStream` and the explicit `DDSFromStreamEXT` extension.

---

## How format detection works

Classic `Texture2D::FromStream` uses
`CNA::Internal::Graphics::ImageLoader::LoadFromMemory`, backed by the repository's pinned
`stb_image` 2.30 decoder. It sniffs the container from bytes rather than trusting a file extension,
and always returns packed RGBA8 to the rest of graphics. `SOFTWARE-306` removed CNA's former silent
DDS auto-detection from this classic method: an isolated Microsoft XNA 4.0 oracle rejects a valid
DXT1 DDS with `InvalidOperationException` in both overloads, and FNA likewise keeps DDS outside its
ordinary image path.

The explicit `CNAEXT Texture2D::DDSFromStreamEXT` entry points check for a DDS header (`"DDS "`
magic plus a valid `DDS_HEADER`) through `TryDecodeDds`. They recognize DXT1/DXT3/DXT5 and decode
each declared mip level through `DxtUtil`, or retain native blocks when the renderer advertises that
path. Once DDS magic is present, malformed headers, unsupported FourCCs, incomplete chains and
truncated per-level payloads raise DDS-specific exceptions rather than falling through to the
ordinary image decoder.

This backend is compiled into `cna_graphics_core`; it has no host codec packages and neither SDL3
nor SDL3_image participates in image loading. The selected decoder understands PNG, JPEG, BMP,
GIF, TGA, PSD, HDR, PIC and PNM. The table below distinguishes formats CNA actually verifies from
formats merely supported by the vendored decoder.

## Verified via round-trip unit tests (`Texture2DFromStreamFormatTest`, `Texture2DTests.cpp`)

| Format | Decode path | Verified | Notes |
|---|---|---|---|
| PNG | Vendored `stb_image` | ✅ | Round-tripped via `Texture2D::SaveAsPng` → `FromStream`; lossless, exact pixel match. |
| JPEG | Vendored `stb_image` | ✅ | Round-tripped via `Texture2D::SaveAsJpeg` → `FromStream`; lossy, verified within tolerance. |
| BMP | Vendored `stb_image` | ✅ | Verified via a hand-built minimal 24bpp uncompressed BMP; exact pixel match. |
| DDS (DXT1/DXT3/DXT5) | `DDSFromStreamEXT` → CNA-internal `TryDecodeDds` + `DxtUtil` | ✅ | The explicit extension retains compressed blocks where supported and otherwise produces canonical RGBA8 `SurfaceFormat::Color`; its resize overload always returns Color. Classic `FromStream` deliberately rejects the same payload. |

DDS and XNB Texture2D content may declare either level zero only or the complete floor-halved
chain through 1×1. A partial prefix is rejected: allocating CNA/XNA's complete mip resource and
generating the absent suffix would silently invent asset content. The resize/crop
`DDSFromStreamEXT` overload intentionally transforms level zero and returns a single-level output
texture.

## Decoder-supported but not verified by CNA tests

GIF, TGA, PSD, HDR, PIC and PNM are enabled in the vendored decoder but are not covered by CNA's
round-trip suite. AVIF, TIFF and WebP are not supported by this deliberately dependency-free
backend. Callers that require an
unverified format should add a fixture before treating it as a compatibility guarantee.

## Resize/crop overloads

Classic `FromStream(GraphicsDevice&, Stream&, int width, int height, bool zoom)` and CNA's explicit
DDS counterpart share the established resize/crop logic:

- **`zoom = false`** (fit): the decoded image is scaled down so it fits inside a
  `width x height` box while preserving aspect ratio. The scale factor is chosen from
  whichever dimension is larger in the source image (width if landscape/square, height if
  portrait) — this is a simplified heuristic that assumes the target box is square-ish, and
  does **not** compute a generic `min(width/w, height/h)` bounding-box fit. The resulting
  texture's actual dimensions may be smaller than `width`/`height` in one axis.
- **`zoom = true`** (cover): the image is scaled up (by whichever dimension needs less
  scaling) and centre-cropped, so the resulting texture is always exactly `width x height`.

Implemented behind `ImageLoader::ResizeRgba` with pixel-centre bilinear sampling and crop-edge
clamping. Verified via `Texture2DFromStreamResizeTest` for both `zoom` values against an 8x4
landscape source, including a patterned centre-crop regression, and by `ImageLoaderTests` with an
exact four-texel interpolation assertion.

Microsoft XNA 4.0 first requires `stream.CanSeek`. A false result throws `ArgumentException` naming
`stream` before `Length`, `Position`, `Read` or `Seek`, and before resize-dimension validation
(`SOFTWARE-305`). Once that precondition passes, the resize overload validates requested `width`
and then `height` before it reads encoded data. Zero and negative values throw
`ArgumentOutOfRangeException` naming the corresponding parameter; `width` wins when both are
invalid. For either public overload, empty or otherwise undecodable non-DDS image data throws
`InvalidOperationException`. `SOFTWARE-304` pins those types and the remaining validation order on
both Software and EasyGL. DDS-specific parser diagnostics belong only to `DDSFromStreamEXT`; the
classic method reports DDS as the same `InvalidOperationException` used for other undecodable data.
