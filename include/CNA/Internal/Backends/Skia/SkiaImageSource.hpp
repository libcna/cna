#pragma once

#include "include/core/SkImage.h"

namespace CNA::Internal::Backends::Skia
{
    /**
     * XNA has two source-alpha conventions.  A Texture2D stores its original RGBA bytes, while
     * the active BlendState determines whether those bytes are already premultiplied.
     */
    enum class SkiaSourceAlphaConvention
    {
        Premultiplied,
        Straight,
    };

    /** Internal common image view for Skia Texture2D and RenderTarget2D resources. */
    class SkiaImageSource
    {
    public:
        virtual ~SkiaImageSource() = default;
        [[nodiscard]] virtual sk_sp<SkImage> SnapshotImage(
            SkiaSourceAlphaConvention alphaConvention) const = 0;
    };
} // namespace CNA::Internal::Backends::Skia
