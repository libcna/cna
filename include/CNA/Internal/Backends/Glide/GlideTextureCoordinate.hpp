#pragma once

#include <algorithm>
#include <stdexcept>

namespace CNA::Internal::Backends::Glide
{
    /**
     * Glide's native window-coordinate S/T unit system (GR_PARAM_STn, Glide 3.0 Reference):
     * s/q and t/q are stored in the range [0..256] for one full repeat of the texture's *long*
     * padded dimension; the short dimension's repeat range is reduced in proportion to the
     * texture's aspect ratio. 256 is a fixed scale of the fixed-point representation, independent
     * of the texture's actual pixel size or the runtime's reported GR_MAX_TEXTURE_SIZE.
     */
    inline constexpr float kGlideTextureCoordinateFullRepeat = 256.0f;

    /**
     * Returns the scale factor that converts a texel offset within one physical tile (including
     * its gutter padding) into Glide's native S/T window-coordinate units.
     *
     * One shared scale applies identically to both axes: `256 / largeDimension`, where
     * `largeDimension` is the tile's larger padded (power-of-two) side. Because the short axis
     * has fewer texels than the long axis, multiplying its texel offsets by this same scale
     * naturally yields a proportionally smaller native range for its own full repeat -- exactly
     * the aspect-ratio-limited short-axis behaviour Glide documents, without needing separate
     * per-axis formulas or branching on which axis is longer.
     */
    [[nodiscard]] inline float GlideNativeTextureCoordinateScale(int paddedWidth, int paddedHeight)
    {
        if (paddedWidth <= 0 || paddedHeight <= 0)
        {
            throw std::runtime_error(
                "GLIDE native texture coordinate scale requires a positive padded tile dimension");
        }
        const int largeDimension = std::max(paddedWidth, paddedHeight);
        return kGlideTextureCoordinateFullRepeat / static_cast<float>(largeDimension);
    }
} // namespace CNA::Internal::Backends::Glide
