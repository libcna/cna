#pragma once

// plans/plan_dx.md Phase DIRECTX3 (DX-11-fmt): XNA SurfaceFormat/DepthFormat -> DXGI_FORMAT mapping, shared
// between D3D11 and D3D12 (design decision 4). Cross-checked against the equivalent XNA/FNA/D3D
// format correspondences used by every other renderer's own format table.

#include <dxgiformat.h>

namespace CNA::Internal::Renderers::D3DCommon
{
    /// Maps an XNA SurfaceFormat ordinal (Microsoft::Xna::Framework::Graphics::SurfaceFormat cast
    /// to int, to avoid coupling this shared header to the XNA namespace -- mirrors
    /// CreateTexture3D/CreateTextureCube's own `surfaceFormat` int convention in IGraphicsRenderer)
    /// to the corresponding DXGI_FORMAT. Returns DXGI_FORMAT_UNKNOWN for an unrecognized ordinal.
    DXGI_FORMAT SurfaceFormatToDxgi(int surfaceFormat);

    /**
     * @brief Returns whether an ordinal is an uncompressed XNA 4.0 surface format.
     *
     * Block-compressed formats are deliberately excluded because their transfer layout is handled
     * separately. CNA extension formats are also excluded from the DirectX parity scope.
     *
     * @param surfaceFormat SurfaceFormat ordinal.
     * @return True for an uncompressed format from the XNA 4.0 enum range.
     */
    bool IsXnaUncompressedSurfaceFormat(int surfaceFormat) noexcept;

    /**
     * @brief Returns whether an ordinal is a block-compressed XNA 4.0 surface format.
     *
     * @param surfaceFormat SurfaceFormat ordinal.
     * @return True for Dxt1, Dxt3, or Dxt5; CNA extension formats are excluded.
     */
    bool IsXnaBlockCompressedSurfaceFormat(int surfaceFormat) noexcept;

    /**
     * @brief Returns whether XNA permits an ordinal as a render-target surface format.
     *
     * This is the renderer-neutral XNA/FNA format set. A DirectX renderer must additionally query
     * its device for texture, render-target, and shader-sampling support before accepting one.
     *
     * @param surfaceFormat SurfaceFormat ordinal.
     * @return True for an XNA 4.0 render-target format; compressed and CNA extension formats are
     *         excluded.
     */
    bool IsXnaRenderTargetSurfaceFormat(int surfaceFormat) noexcept;

    /**
     * @brief Returns the byte size of one texel for an uncompressed XNA 4.0 surface format.
     *
     * @param surfaceFormat SurfaceFormat ordinal.
     * @return Bytes per texel, or zero for compressed, extension, or unknown formats.
     */
    int SurfaceFormatBytesPerTexel(int surfaceFormat) noexcept;

    /**
     * @brief Returns the byte size of one 4x4 block for an XNA compressed format.
     *
     * @param surfaceFormat SurfaceFormat ordinal.
     * @return Eight for Dxt1, sixteen for Dxt3/Dxt5, or zero otherwise.
     */
    int SurfaceFormatBytesPerBlock(int surfaceFormat) noexcept;

    /**
     * @brief Returns a stable diagnostic name for a SurfaceFormat ordinal.
     *
     * @param surfaceFormat SurfaceFormat ordinal.
     * @return Enum member name, or `Unknown` for an invalid ordinal.
     */
    const char* SurfaceFormatName(int surfaceFormat) noexcept;

    /**
     * @brief Writes Direct3D 9's channel expansion for sampling a texture of this format.
     *
     * WINCLOSE-0022. XNA runs on Direct3D 9, which hands a shader (R, 1, 1, 1) for a one-channel
     * format and (R, G, 1, 1) for a two-channel one; Direct3D 10+ hands (R, 0, 0, 1) and
     * (R, G, 0, 1). A shader restores XNA's result as `sample * mask + fill`. Single and HalfSingle
     * are one channel; Vector2, HalfVector2, NormalizedByte2 and Rg32 two (EasyGL's table,
     * SAMPLE-038). Every other format -- Alpha8 included, which both APIs expand as (0, 0, 0, A) --
     * takes the identity pair.
     *
     * @param surfaceFormat SurfaceFormat ordinal of the sampled texture.
     * @param mask Receives four multipliers.
     * @param fill Receives four addends.
     */
    void D3D9ChannelExpansion(int surfaceFormat, float mask[4], float fill[4]) noexcept;

    /// Maps an XNA DepthFormat ordinal (None=0, Depth16=1, Depth24=2, Depth24Stencil8=3) to the
    /// corresponding DXGI_FORMAT. D3D11 has no pure 24-bit-depth-only format, so Depth24 maps to
    /// the same DXGI_FORMAT_D24_UNORM_S8_UINT as Depth24Stencil8 -- matches this project's own
    /// Vulkan renderer, which falls back to the same combined format when a depth-only format is
    /// requested (see VulkanRenderer.cpp's own depth-format candidate list). Returns
    /// DXGI_FORMAT_UNKNOWN for None (no depth buffer requested) or an unrecognized ordinal.
    DXGI_FORMAT DepthFormatToDxgi(int depthFormat);
}
