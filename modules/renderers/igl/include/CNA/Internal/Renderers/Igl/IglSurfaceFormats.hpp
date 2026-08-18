// SPDX-License-Identifier: MS-PL
#pragma once

#include <igl/TextureFormat.h>

namespace CNA::Internal::Renderers::Igl
{
    /**
     * @file
     * @brief This renderer's surface-format boundary, and the byte arithmetic every transfer needs.
     *
     * Two separate questions live here, and conflating them is what the previous single mapping
     * function got wrong.
     *
     * **Which formats IGL can genuinely store.** `igl::TextureFormat` is not a superset of XNA's
     * `SurfaceFormat`, and the difference is not always a missing name -- several IGL formats have
     * a familiar name and a different texel layout, and a few differ between IGL's own two
     * backends. A mapping that answered "RGBA8 for everything else" therefore did not fail
     * loudly for an unrepresentable format; it silently created a texture of a different size and
     * channel order from the one the caller asked for. Every entry below is decided from the
     * layout XNA's own packed-vector types define (`Microsoft::Xna::Framework::Graphics::
     * PackedVector`) against what IGL v1.1.1 maps the candidate format to on BOTH backends
     * (`igl/opengl/Texture.cpp`'s `toFormatDescGL`, `igl/vulkan/Common.cpp`'s
     * `textureFormatToVkFormat`); a format the two backends disagree about is refused, because
     * this renderer picks its backend at run time and cannot offer a texel layout that depends on
     * which one was chosen.
     *
     * **How many bytes a transfer moves.** `ITextureRenderer`'s transfer methods are named for
     * RGBA8 because most CNA textures are, but the format an `ImageData` (or a
     * `CreateRenderTarget2DEXT` call) names is a raw `SurfaceFormat` ordinal, and its texel may be
     * 1, 2, 4, 8 or 16 bytes -- or a 4x4 compressed block. `width * 4` is right for exactly one
     * of those cases. The counts below delegate to the shared `Texture::GetFormatSizeEXT` /
     * `Texture::GetBlockSizeSquaredEXT` metadata rather than restating a format table, so a format
     * added to CNA is sized correctly here without any change.
     */

    /**
     * @brief Whether this renderer can store @p surfaceFormat with XNA's own texel semantics.
     *
     * "Can store" is deliberately strict: the IGL format must have the same texel size, the same
     * channel order and the same normalized/integer/float interpretation as the XNA format, on
     * both of this renderer's backends.
     *
     * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
     * @return True when @ref ToIglSurfaceFormat returns a real format for it.
     */
    [[nodiscard]] bool IsSupportedSurfaceFormat(int surfaceFormat) noexcept;

    /**
     * @brief Maps an XNA `SurfaceFormat` ordinal to the IGL texture format that matches it exactly.
     *
     * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
     * @return The matching IGL format, or `igl::TextureFormat::Invalid` when there is none.
     */
    [[nodiscard]] igl::TextureFormat ToIglSurfaceFormatOrInvalid(int surfaceFormat) noexcept;

    /**
     * @brief Maps an XNA `SurfaceFormat` ordinal to an IGL texture format, refusing by name.
     *
     * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
     * @return The matching IGL format.
     * @throws std::runtime_error When IGL has no format with this one's exact semantics. The
     *         message names the format and why, rather than substituting a different layout.
     */
    [[nodiscard]] igl::TextureFormat ToIglSurfaceFormat(int surfaceFormat);

    /**
     * @brief Returns a stable name for @p surfaceFormat, for diagnostics.
     *
     * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
     * @return The XNA name ("Rgba64", "Color", ...), or "<unknown>" for an ordinal that names no
     *         `SurfaceFormat`.
     */
    [[nodiscard]] const char* GetSurfaceFormatName(int surfaceFormat) noexcept;

    /**
     * @brief Whether @p surfaceFormat transfers 4x4 compressed blocks rather than linear texels.
     *
     * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
     * @return True for the block-compressed formats (Dxt1/Dxt3/Dxt5/Dxt5Srgb/Bc7/Bc7Srgb).
     */
    [[nodiscard]] bool IsBlockCompressedFormat(int surfaceFormat) noexcept;

    /**
     * @brief Bytes one texel, or one 4x4 block, occupies in @p surfaceFormat.
     *
     * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
     * @return 1 for Alpha8, 2 for HalfSingle, 4 for Color, 8 for Rgba64, 16 for Vector4, and so
     *         on; 0 for an ordinal that names no `SurfaceFormat`.
     */
    [[nodiscard]] int FormatUnitByteCount(int surfaceFormat) noexcept;

    /**
     * @brief Bytes one row of a @p width-wide region occupies -- the transfer's row pitch.
     *
     * For a compressed format this is one row of BLOCKS, i.e. `ceil(width / 4)` blocks.
     *
     * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
     * @param width         Region width in texels.
     * @return The row pitch in bytes; 0 for a non-positive width or an unknown ordinal.
     */
    [[nodiscard]] int FormatRowByteCount(int surfaceFormat, int width) noexcept;

    /**
     * @brief Reverses the row order of a tightly packed region in place.
     *
     * IGL's Vulkan `copyBytesColorAttachment` passes `flipImageVertical = true` to
     * `VulkanStagingDevice::getImageData2D` unconditionally, reversing the rows of whatever
     * rectangle it copied; its OpenGL counterpart is a plain `glReadPixels` and reverses nothing.
     * A caller that wants one convention from both backends has to undo exactly one of them, and
     * this is that undo. A single-row region is unaffected either way, which is precisely why the
     * discrepancy stayed invisible to every per-pixel test.
     *
     * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
     * @param width         Region width in texels.
     * @param height        Region height in texels.
     * @param data          Start of the region; @p height rows of `FormatRowByteCount` bytes.
     */
    void FlipRowsInPlace(int surfaceFormat, int width, int height, void* data) noexcept;

    /**
     * @brief Bytes a @p width x @p height region occupies in @p surfaceFormat.
     *
     * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
     * @param width         Region width in texels.
     * @param height        Region height in texels.
     * @return `ceil(w/4) * ceil(h/4) * blockBytes` for a compressed format, `w * h * texelBytes`
     *         otherwise; 0 for a non-positive extent or an unknown ordinal.
     */
    [[nodiscard]] int FormatRegionByteCount(int surfaceFormat, int width, int height) noexcept;

    /**
     * @brief Bytes a @p width x @p height x @p depth box occupies in @p surfaceFormat.
     *
     * @param surfaceFormat Raw XNA `SurfaceFormat` ordinal.
     * @param width         Box width in texels.
     * @param height        Box height in texels.
     * @param depth         Box depth in texels.
     * @return The byte count; 0 for a non-positive extent or an unknown ordinal.
     */
    [[nodiscard]] int FormatBoxByteCount(int surfaceFormat, int width, int height,
                                         int depth) noexcept;
}
