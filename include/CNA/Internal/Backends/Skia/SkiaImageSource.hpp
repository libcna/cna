#pragma once

#include "include/core/SkImage.h"

namespace CNA::Internal::Backends::Skia
{
    /** Internal common image view for Skia Texture2D and RenderTarget2D resources. */
    class SkiaImageSource
    {
    public:
        virtual ~SkiaImageSource() = default;
        [[nodiscard]] virtual sk_sp<SkImage> SnapshotImage() const = 0;
    };
} // namespace CNA::Internal::Backends::Skia
