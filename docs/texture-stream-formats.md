# Texture2D::FromStream — Supported Encoded Formats

> Verified for Task 262 (Phase 32). Covers `Texture2D::FromStream` (both overloads).

---

## How format detection works

`Texture2D::FromStream` first checks for a DDS header (`"DDS "` magic + valid `DDS_HEADER`)
via an internal minimal parser (`TryDecodeDds` in `Texture2D.cpp`) that only recognizes the
`DXT1`/`DXT3`/`DXT5` FourCC codes and decodes them itself via `DxtUtil`. Any other DDS FourCC,
or any non-DDS stream, falls through to `CNA::Internal::Graphics::ImageLoader::LoadFromMemory`,
which calls SDL3_image's `IMG_Load_IO` — a format-sniffing loader that auto-detects the
container format from the byte stream, not from a file extension.

The set of formats `IMG_Load_IO` can actually decode depends on which codecs the linked
SDL3_image build was compiled with (see `Requires.private` in `sdl3-image.pc`).

## Verified via round-trip unit tests (`Texture2DFromStreamFormatTest`, `Texture2DTests.cpp`)

| Format | Decode path | Verified | Notes |
|---|---|---|---|
| PNG | SDL3_image (`IMG_Load_IO`) | ✅ | Round-tripped via `Texture2D::SaveAsPng` → `FromStream`; lossless, exact pixel match. |
| JPEG | SDL3_image (`IMG_Load_IO`) | ✅ | Round-tripped via `Texture2D::SaveAsJpeg` → `FromStream`; lossy, verified within tolerance. |
| BMP | SDL3_image (`IMG_Load_IO`, native SDL3 BMP support) | ✅ | Verified via a hand-built minimal 24bpp uncompressed BMP; exact pixel match. |
| DDS (DXT1/DXT3/DXT5) | CNA-internal `TryDecodeDds` + `DxtUtil` (bypasses SDL3_image entirely) | ✅ | Pre-existing; see `examples/dxt1_texture_test.cpp` (Task 125). |

## Not verified (present in the SDL3_image build's private deps, but untested here)

The installed SDL3_image (`sdl3-image.pc`) links `libavif`, `libpng`, `libtiff-4`, `libwebp`,
so AVIF/TIFF/WebP are likely decodable via `IMG_Load_IO` as well, but this was out of scope
for Task 262 (FNA/XNA only documents PNG/JPG/BMP/GIF/TGA/DDS as the conventionally-used formats)
and has not been round-trip tested.

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

Implemented in `Texture2D::FromStream` (5-arg overload) using `SDL_CreateSurfaceFrom` +
`SDL_BlitSurfaceScaled`, matching FNA3D's `SDL_BlitSurfaceScaled`-based implementation.
Verified via `Texture2DFromStreamResizeTest` (`Texture2DTests.cpp`) for both `zoom` values
against an 8x4 landscape source.
