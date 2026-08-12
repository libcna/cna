// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Internal::Renderers::Fna3d
{
    /**
     * @file
     * @brief Byte arithmetic for a texture transfer, for every XNA surface format.
     *
     * `ITextureRenderer`'s transfer methods are named for RGBA8 because that is what the great
     * majority of CNA textures are, but the format an `ImageData` (or a `CreateTexture3D` /
     * `CreateTextureCube` / `CreateRenderTarget2DEXT` call) actually names is a raw
     * `SurfaceFormat` ordinal, and a block-compressed one moves **raw compressed blocks** through
     * those very same calls -- SKIA-140/141 established that convention. A renderer that assumes
     * `width * height * 4` therefore reads past the end of a compressed upload and writes past the
     * end of a compressed readback.
     *
     * Reachability, stated exactly: the shared `Texture::ValidateFormat` currently admits only
     * `SurfaceFormat::Color` for every renderer except Skia, so no *public* `Texture2D` under
     * FNA3D is compressed today. The renderer contract itself carries no such restriction -- an
     * `ImageData` with a compressed `surfaceFormat` reaches `CreateTexture` directly -- so this is
     * the layer where the arithmetic has to be right, and where the tests measure it.
     *
     * These helpers replace the RGBA8 assumption with the format's real layout. They delegate to
     * the shared `Texture::GetFormatSizeEXT` / `Texture::GetBlockSizeSquaredEXT` rather than
     * duplicating a format table, so a new format added to CNA is sized correctly here without
     * any change.
     */

    /**
     * @brief Whether @p surfaceFormat transfers 4x4 compressed blocks rather than linear texels.
     * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
     * @return True for the block-compressed formats (Dxt1/Dxt3/Dxt5/Dxt5Srgb/Bc7/Bc7Srgb).
     */
    [[nodiscard]] bool IsBlockCompressedFormat(int surfaceFormat);

    /**
     * @brief Bytes one texel, or one 4x4 block, occupies in @p surfaceFormat.
     * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
     * @return 8 for Dxt1, 16 for Dxt3/Dxt5/Bc7, 4 for Color, and so on.
     */
    [[nodiscard]] int FormatUnitByteCount(int surfaceFormat);

    /**
     * @brief Bytes one row of a @p width-wide region occupies -- the transfer's row pitch.
     *
     * For a compressed format this is one row of BLOCKS, i.e. `ceil(width / 4)` blocks, which is
     * what a caller staging compressed data lays out.
     *
     * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
     * @param width         Region width in texels.
     * @return The row pitch in bytes; 0 for a non-positive width.
     */
    [[nodiscard]] int FormatRowByteCount(int surfaceFormat, int width);

    /**
     * @brief Bytes a @p width x @p height region occupies in @p surfaceFormat.
     *
     * This is the exact value `FNA3D_SetTextureData2D`/`FNA3D_GetTextureData2D` must be given:
     * `ceil(w/4) * ceil(h/4) * blockBytes` for a compressed format, `w * h * texelBytes`
     * otherwise.
     *
     * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
     * @param width         Region width in texels.
     * @param height        Region height in texels.
     * @return The byte count; 0 for a non-positive extent.
     */
    [[nodiscard]] int FormatRegionByteCount(int surfaceFormat, int width, int height);

    /**
     * @brief Checks a 2D texture transfer before its coordinates reach FNA3D.
     *
     * The subtraction form avoids signed-overflow in `x + width` / `y + height`, which matters
     * because every FNA3D transfer receives signed 32-bit coordinates.
     */
    [[nodiscard]] bool IsValidTextureRegion2D(int levelWidth, int levelHeight, int x, int y,
                                              int width, int height) noexcept;

    /**
     * @brief 3D counterpart of IsValidTextureRegion2D.
     */
    [[nodiscard]] bool IsValidTextureRegion3D(int levelWidth, int levelHeight, int levelDepth,
                                              int x, int y, int z, int width, int height,
                                              int depth) noexcept;
}
