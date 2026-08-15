# Texture2D::FromStream — Supported Encoded Formats

> Verified for Task 262 (Phase 32). Covers `Texture2D::FromStream` (both overloads).

---

## How format detection works

`Texture2D::FromStream` first checks for a DDS header (`"DDS "` magic + valid `DDS_HEADER`)
via an internal parser (`TryDecodeDds` in `Texture2D.cpp`) that recognizes the
`DXT1`/`DXT3`/`DXT5` FourCC codes and decodes each declared mip level itself via `DxtUtil`.
Once DDS magic is present, malformed headers, unsupported FourCCs, incomplete chains and
truncated per-level payloads raise a DDS-specific exception; they no longer fall through to an
unrelated image decoder. Non-DDS streams use
`CNA::Internal::Graphics::ImageLoader::LoadFromMemory`, backed by the repository's pinned
`stb_image` 2.30 decoder. It sniffs the container from bytes rather than trusting a file extension,
and always returns packed RGBA8 to the rest of graphics.

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
| DDS (DXT1/DXT3/DXT5) | CNA-internal `TryDecodeDds` + `DxtUtil` (bypasses `ImageLoader`) | ✅ | `Skia_Texture2D_ContentMips` verifies all three codecs across an exact four-level 8×8 chain, plus single-level and malformed/truncated cases (SKIA-130). Decoded storage is canonical RGBA8 `SurfaceFormat::Color`. |

DDS and XNB Texture2D content may declare either level zero only or the complete floor-halved
chain through 1×1. A partial prefix is rejected: allocating CNA/XNA's complete mip resource and
generating the absent suffix would silently invent asset content. The resize/crop `FromStream`
overload intentionally transforms level zero and returns a single-level output texture.

## Decoder-supported but not verified by CNA tests

GIF, TGA, PSD, HDR, PIC and PNM are enabled in the vendored decoder but are not covered by CNA's
round-trip suite. AVIF, TIFF and WebP are not supported by this deliberately dependency-free
backend. FNA/XNA conventionally documents PNG/JPG/BMP/GIF/TGA/DDS; callers that require an
unverified format should add a fixture before treating it as a compatibility guarantee.

## `FromStream(GraphicsDevice&, Stream&, int width, int height, bool zoom)`

Added in Task 262 (previously missing — see `AUDIT.md` "Texture2D detailed audit"). Mirrors
FNA3D's native `FNA3D_Image_Load` resize/crop logic:

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
