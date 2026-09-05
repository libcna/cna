// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/BitmapContent.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <utility>

#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/PixelBitmapContent.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    namespace
    {
        struct BitmapTypeEntry
        {
            std::string dotNetName;
            std::function<std::shared_ptr<BitmapContent>(SharpRuntime::intcs, SharpRuntime::intcs)> factory;
        };

        struct BitmapTypeRegistry
        {
            std::mutex mutex;
            std::map<System::Type, BitmapTypeEntry, decltype([](const System::Type& a, const System::Type& b)
                                                              { return a.GetHashCode() < b.GetHashCode(); })> entries;
        };

        BitmapTypeRegistry& Registry()
        {
            static BitmapTypeRegistry registry;
            return registry;
        }

        bool RegionInside(const Rectangle& region, const BitmapContent& bitmap)
        {
            return region.X >= 0 && region.Y >= 0 && region.Width >= 0 && region.Height >= 0 &&
                   region.X + region.Width <= bitmap.getWidthProperty() &&
                   region.Y + region.Height <= bitmap.getHeightProperty();
        }
    }

    BitmapContent::BitmapContent() = default;

    BitmapContent::BitmapContent(SharpRuntime::intcs width, SharpRuntime::intcs height)
    {
        setWidthProperty(width);
        setHeightProperty(height);
    }

    SharpRuntime::intcs BitmapContent::getHeightProperty() const noexcept { return height_; }

    SharpRuntime::intcs BitmapContent::getWidthProperty() const noexcept { return width_; }

    void BitmapContent::setHeightProperty(SharpRuntime::intcs value)
    {
        if (value <= 0)
        {
            throw System::ArgumentOutOfRangeException("height", "Bitmap size must be greater than zero.");
        }
        height_ = value;
    }

    void BitmapContent::setWidthProperty(SharpRuntime::intcs value)
    {
        if (value <= 0)
        {
            throw System::ArgumentOutOfRangeException("width", "Bitmap size must be greater than zero.");
        }
        width_ = value;
    }

    std::string BitmapContent::ToString() const
    {
        return TypeDisplayName() + ", " + std::to_string(width_) + "x" + std::to_string(height_);
    }

    void BitmapContent::Copy(const std::shared_ptr<BitmapContent>& sourceBitmap,
                             const std::shared_ptr<BitmapContent>& destinationBitmap)
    {
        if (sourceBitmap == nullptr)
        {
            throw System::ArgumentNullException("sourceBitmap");
        }
        if (destinationBitmap == nullptr)
        {
            throw System::ArgumentNullException("destinationBitmap");
        }
        Copy(sourceBitmap, Rectangle(0, 0, sourceBitmap->getWidthProperty(), sourceBitmap->getHeightProperty()),
             destinationBitmap,
             Rectangle(0, 0, destinationBitmap->getWidthProperty(), destinationBitmap->getHeightProperty()));
    }

    void BitmapContent::Copy(const std::shared_ptr<BitmapContent>& sourceBitmap, Rectangle sourceRegion,
                             const std::shared_ptr<BitmapContent>& destinationBitmap, Rectangle destinationRegion)
    {
        ValidateCopyArguments(sourceBitmap, sourceRegion, destinationBitmap, destinationRegion);
        if (sourceRegion.Width == 0 || sourceRegion.Height == 0 || destinationRegion.Width == 0 ||
            destinationRegion.Height == 0)
        {
            return;
        }
        // Copying a bitmap onto itself reads the source region before any of it is overwritten.
        if (sourceBitmap.get() == destinationBitmap.get())
        {
            auto snapshot = std::make_shared<PixelBitmapContent<Vector4>>(sourceRegion.Width, sourceRegion.Height);
            Copy(sourceBitmap, sourceRegion, snapshot, Rectangle(0, 0, sourceRegion.Width, sourceRegion.Height));
            Copy(snapshot, Rectangle(0, 0, sourceRegion.Width, sourceRegion.Height), destinationBitmap, destinationRegion);
            return;
        }
        if (destinationBitmap->TryCopyFrom(sourceBitmap, sourceRegion, destinationRegion))
        {
            return;
        }
        if (sourceBitmap->TryCopyTo(destinationBitmap, sourceRegion, destinationRegion))
        {
            return;
        }
        // Neither side knows the other: every bitmap can convert to and from Vector4 pixels.
        const std::shared_ptr<BitmapContent> intermediate =
            std::make_shared<PixelBitmapContent<Vector4>>(sourceRegion.Width, sourceRegion.Height);
        const Rectangle whole(0, 0, sourceRegion.Width, sourceRegion.Height);
        if (!intermediate->TryCopyFrom(sourceBitmap, sourceRegion, whole) &&
            !sourceBitmap->TryCopyTo(intermediate, sourceRegion, whole))
        {
            throw System::InvalidOperationException("Cannot copy from a " + sourceBitmap->TypeDisplayName() +
                                                    ": the bitmap type supports no conversion.");
        }
        if (!destinationBitmap->TryCopyFrom(intermediate, whole, destinationRegion) &&
            !intermediate->TryCopyTo(destinationBitmap, whole, destinationRegion))
        {
            throw System::InvalidOperationException("Cannot copy into a " + destinationBitmap->TypeDisplayName() +
                                                    ": the bitmap type supports no conversion.");
        }
    }

    void BitmapContent::ValidateCopyArguments(const std::shared_ptr<BitmapContent>& sourceBitmap, Rectangle sourceRegion,
                                              const std::shared_ptr<BitmapContent>& destinationBitmap,
                                              Rectangle destinationRegion)
    {
        if (sourceBitmap == nullptr)
        {
            throw System::ArgumentNullException("sourceBitmap");
        }
        if (destinationBitmap == nullptr)
        {
            throw System::ArgumentNullException("destinationBitmap");
        }
        if (!RegionInside(sourceRegion, *sourceBitmap))
        {
            throw System::ArgumentOutOfRangeException("sourceRegion");
        }
        if (!RegionInside(destinationRegion, *destinationBitmap))
        {
            throw System::ArgumentOutOfRangeException("destinationRegion");
        }
    }

    void BitmapContent::RegisterBitmapType(System::Type bitmapType, std::string dotNetName, BitmapFactory factory)
    {
        BitmapTypeRegistry& registry = Registry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        registry.entries[bitmapType] = BitmapTypeEntry{std::move(dotNetName), std::move(factory)};
    }

    std::shared_ptr<BitmapContent> BitmapContent::CreateBitmap(System::Type bitmapType, SharpRuntime::intcs width,
                                                               SharpRuntime::intcs height)
    {
        BitmapTypeRegistry& registry = Registry();
        BitmapFactory factory;
        {
            std::lock_guard<std::mutex> lock(registry.mutex);
            const auto found = registry.entries.find(bitmapType);
            if (found == registry.entries.end())
            {
                throw System::ArgumentException("ConvertBitmapType cannot convert to " + bitmapType.getName() +
                                                ". The target type must be derived from BitmapContent.");
            }
            factory = found->second.factory;
        }
        return factory(width, height);
    }

    std::string BitmapContent::BitmapTypeName(System::Type bitmapType)
    {
        BitmapTypeRegistry& registry = Registry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        const auto found = registry.entries.find(bitmapType);
        return found == registry.entries.end() ? bitmapType.getName() : found->second.dotNetName;
    }

    // -----------------------------------------------------------------------------------------
    // PixelBitmapContentBase: conversion and resampling between pixel bitmaps
    // -----------------------------------------------------------------------------------------
    void PixelBitmapContentBase::ConvertRegion(const PixelBitmapContentBase& source, Rectangle sourceRegion,
                                               PixelBitmapContentBase& destination, Rectangle destinationRegion)
    {
        const SharpRuntime::intcs sw = sourceRegion.Width;
        const SharpRuntime::intcs sh = sourceRegion.Height;
        const SharpRuntime::intcs dw = destinationRegion.Width;
        const SharpRuntime::intcs dh = destinationRegion.Height;
        if (sw == dw && sh == dh)
        {
            for (SharpRuntime::intcs y = 0; y < sh; ++y)
            {
                for (SharpRuntime::intcs x = 0; x < sw; ++x)
                {
                    destination.SetPixelVector4(destinationRegion.X + x, destinationRegion.Y + y,
                                                source.GetPixelVector4(sourceRegion.X + x, sourceRegion.Y + y));
                }
            }
            return;
        }
        // Enlarging samples bilinearly at pixel centres, clamped at the edges; reducing averages the
        // source footprint of each destination pixel (a box filter). Both axes are handled at once.
        const auto sample = [&](SharpRuntime::intcs x, SharpRuntime::intcs y)
        {
            x = std::clamp<SharpRuntime::intcs>(x, 0, sw - 1);
            y = std::clamp<SharpRuntime::intcs>(y, 0, sh - 1);
            return source.GetPixelVector4(sourceRegion.X + x, sourceRegion.Y + y);
        };
        const bool enlargeX = dw >= sw;
        const bool enlargeY = dh >= sh;
        for (SharpRuntime::intcs dy = 0; dy < dh; ++dy)
        {
            for (SharpRuntime::intcs dx = 0; dx < dw; ++dx)
            {
                Vector4 accumulated(0, 0, 0, 0);
                float weight = 0;
                // Footprint of this destination pixel in source pixels.
                const double x0 = static_cast<double>(dx) * sw / dw;
                const double x1 = static_cast<double>(dx + 1) * sw / dw;
                const double y0 = static_cast<double>(dy) * sh / dh;
                const double y1 = static_cast<double>(dy + 1) * sh / dh;
                if (enlargeX && enlargeY)
                {
                    const double cx = (dx + 0.5) * sw / dw - 0.5;
                    const double cy = (dy + 0.5) * sh / dh - 0.5;
                    const SharpRuntime::intcs ix = static_cast<SharpRuntime::intcs>(std::floor(cx));
                    const SharpRuntime::intcs iy = static_cast<SharpRuntime::intcs>(std::floor(cy));
                    const float fx = static_cast<float>(cx - ix);
                    const float fy = static_cast<float>(cy - iy);
                    const Vector4 a = sample(ix, iy);
                    const Vector4 b = sample(ix + 1, iy);
                    const Vector4 c = sample(ix, iy + 1);
                    const Vector4 d = sample(ix + 1, iy + 1);
                    accumulated = a * ((1 - fx) * (1 - fy)) + b * (fx * (1 - fy)) + c * ((1 - fx) * fy) + d * (fx * fy);
                    weight = 1;
                }
                else
                {
                    for (SharpRuntime::intcs sy = static_cast<SharpRuntime::intcs>(std::floor(y0)); sy < std::ceil(y1); ++sy)
                    {
                        const float wy = static_cast<float>(std::min<double>(y1, sy + 1) - std::max<double>(y0, sy));
                        if (wy <= 0)
                        {
                            continue;
                        }
                        for (SharpRuntime::intcs sx = static_cast<SharpRuntime::intcs>(std::floor(x0)); sx < std::ceil(x1); ++sx)
                        {
                            const float wx = static_cast<float>(std::min<double>(x1, sx + 1) - std::max<double>(x0, sx));
                            if (wx <= 0)
                            {
                                continue;
                            }
                            accumulated = accumulated + sample(sx, sy) * (wx * wy);
                            weight += wx * wy;
                        }
                    }
                }
                destination.SetPixelVector4(destinationRegion.X + dx, destinationRegion.Y + dy,
                                            weight > 0 ? accumulated * (1.0f / weight) : accumulated);
            }
        }
    }
}
