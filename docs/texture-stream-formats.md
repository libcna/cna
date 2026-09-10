# Texture2D stream decoding — Supported Encoded Formats

> Covers classic `Texture2D::FromStream` and the explicit `DDSFromStreamEXT` extension.

---

## How format detection works

Classic `Texture2D::FromStream` uses
`CNA::Internal::Graphics::ImageLoader::LoadFromMemory`, backed by the repository's pinned
`stb_image` 2.30 decoder. Before invoking that broader internal decoder, the classic public method
requires a PNG, JPEG or GIF byte signature. This is a measured Microsoft XNA 4.0 boundary rather
than a limitation of stb: the oracle accepts valid examples of those three containers and rejects
valid BMP3, TGA, QOI, PSD, HDR and PPM with `InvalidOperationException` (`SOFTWARE-307`). FNA enables
TGA and QOI as additional ordinary-path formats, but that lower-authority extension is not part of
the Microsoft XNA contract. The decoded result is always packed RGBA8.

`SOFTWARE-306` separately removed CNA's former silent DDS auto-detection from the classic method:
Microsoft XNA rejects a valid DXT1 DDS with `InvalidOperationException` in both overloads, and FNA
likewise keeps DDS outside its ordinary image path.

The explicit `CNAEXT Texture2D::DDSFromStreamEXT` entry points check for a DDS header (`"DDS "`
magic plus a valid `DDS_HEADER`) through `TryDecodeDds`. They recognize DXT1/DXT3/DXT5 and decode
each declared mip level through `DxtUtil`, or retain native blocks when the renderer advertises that
path. Once DDS magic is present, malformed headers, unsupported FourCCs, incomplete chains and
truncated per-level payloads raise DDS-specific exceptions rather than falling through to the
ordinary image decoder.

This backend is compiled into `cna_graphics_core`; it has no host codec packages and neither SDL3
nor SDL3_image participates in image loading. The internal decoder still understands PNG, JPEG,
BMP, GIF, TGA, QOI, PSD, HDR, PIC and PNM for renderer-neutral internal consumers. Public
`Texture2D::FromStream` applies the narrower classic allowlist above.

## Verified via round-trip unit tests (`Texture2DFromStreamFormatTest`, `Texture2DTests.cpp`)

| Format | Decode path | Verified | Notes |
|---|---|---|---|
| PNG | Vendored `stb_image` | ✅ | Round-tripped via `Texture2D::SaveAsPng` → `FromStream`; lossless, exact pixel match. |
| JPEG | Vendored `stb_image` | ✅ | Round-tripped via `Texture2D::SaveAsJpeg` → `FromStream`; lossy, verified within tolerance. |
| GIF | Vendored `stb_image` | ✅ | A valid 2×2 GIF is accepted by both the Microsoft oracle and the shared Software/EasyGL test. |
| DDS (DXT1/DXT3/DXT5) | `DDSFromStreamEXT` → CNA-internal `TryDecodeDds` + `DxtUtil` | ✅ | The explicit extension retains compressed blocks where supported and otherwise produces canonical RGBA8 `SurfaceFormat::Color`; its resize overload always returns Color. Classic `FromStream` deliberately rejects the same payload. |

DDS and XNB Texture2D content may declare either level zero only or the complete floor-halved
chain through 1×1. A partial prefix is rejected: allocating CNA/XNA's complete mip resource and
generating the absent suffix would silently invent asset content. The resize/crop
`DDSFromStreamEXT` overload intentionally transforms level zero and returns a single-level output
texture.

## Internal decoder formats outside classic `FromStream`

The shared conformance matrix supplies valid BMP, TGA, QOI, PSD, HDR and PPM payloads, proves that
`ImageLoader` can decode each one, then proves that both classic `FromStream` overloads reject each
one like Microsoft XNA. PIC remains decoder-enabled but is not independently fixture-tested. AVIF,
TIFF and WebP are not supported by this deliberately dependency-free backend. A CNA-owned internal
or explicitly extended API may use the broader decoder; the classic XNA method may not silently do
so.

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
