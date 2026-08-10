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

    /** The bytes owned by an image source before Skia evaluates it in premultiplied working space. */
    enum class SkiaSourceStorageAlpha
    {
        CanonicalRgbaBytes,
        PremultipliedSurface,
    };

    /** Observable route from public/source storage to the colour received by a Skia blender. */
    enum class SkiaWorkingSourceRoute
    {
        PreserveDeclaredComponents,
        PremultiplyStraightRgbOnce,
        ReusePremultipliedSurface,
    };

    [[nodiscard]] inline constexpr SkiaWorkingSourceRoute ResolveSkiaWorkingSourceRoute(
        SkiaSourceStorageAlpha storage, SkiaSourceAlphaConvention convention) noexcept
    {
        if (storage == SkiaSourceStorageAlpha::PremultipliedSurface)
            return SkiaWorkingSourceRoute::ReusePremultipliedSurface;
        return convention == SkiaSourceAlphaConvention::Straight
            ? SkiaWorkingSourceRoute::PremultiplyStraightRgbOnce
            : SkiaWorkingSourceRoute::PreserveDeclaredComponents;
    }

    /** Internal common image view for Skia Texture2D and RenderTarget2D resources. */
    class SkiaImageSource
    {
    public:
        virtual ~SkiaImageSource() = default;
        [[nodiscard]] virtual SkiaSourceStorageAlpha StorageAlphaEXT() const noexcept = 0;
        [[nodiscard]] virtual sk_sp<SkImage> SnapshotImage(
            SkiaSourceAlphaConvention alphaConvention) const = 0;
        [[nodiscard]] virtual int MipLevelCountEXT() const noexcept { return 1; }
        [[nodiscard]] virtual sk_sp<SkImage> SnapshotMipLevelEXT(
            int level, SkiaSourceAlphaConvention alphaConvention) const
        {
            return level == 0 ? SnapshotImage(alphaConvention) : nullptr;
        }
    };
} // namespace CNA::Internal::Backends::Skia
