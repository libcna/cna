// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/BitmapContent.hpp"

#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/DxtBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/PixelBitmapContent.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/PixelBitmapContent.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/IndexOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    namespace
    {
        /** @brief The source pixels one destination pixel reads along one axis, and their weights. */
        struct ResampleTap
        {
            SharpRuntime::intcs first = 0;
            std::vector<double> weights;
        };

        /**
         * @brief XNA's resize weights along one axis: @p source pixels resampled to @p destination.
         *
         * Destination pixel i covers [i*s/d, (i+1)*s/d] of the source, and its value is the mean
         * over that interval of the linear interpolation between source pixel centres (j + 1/2),
         * held constant outside the first and last centre. PixelBitmapContentBase::ConvertRegion
         * names the measurements this reproduces.
         */
        std::vector<ResampleTap> ResampleTaps(SharpRuntime::intcs source, SharpRuntime::intcs destination)
        {
            std::vector<ResampleTap> taps(static_cast<std::size_t>(std::max<SharpRuntime::intcs>(destination, 0)));
            if (source <= 0)
            {
                return taps;
            }
            const double s = source;
            for (SharpRuntime::intcs i = 0; i < destination; ++i)
            {
                const double a = static_cast<double>(i) * s / destination;
                const double b = static_cast<double>(i + 1) * s / destination;
                const SharpRuntime::intcs low = std::clamp<SharpRuntime::intcs>(
                    static_cast<SharpRuntime::intcs>(std::floor(a - 0.5)), 0, source - 1);
                const SharpRuntime::intcs high = std::clamp<SharpRuntime::intcs>(
                    static_cast<SharpRuntime::intcs>(std::ceil(b - 0.5)), 0, source - 1);
                ResampleTap& tap = taps[static_cast<std::size_t>(i)];
                tap.first = low;
                tap.weights.assign(static_cast<std::size_t>(high - low + 1), 0.0);
                // [a, b] is cut at every pixel centre inside it; between two centres the
                // interpolation is linear, so each piece integrates exactly.
                std::vector<double> cuts{a};
                for (SharpRuntime::intcs j = low; j <= high; ++j)
                {
                    const double centre = j + 0.5;
                    if (centre > a && centre < b)
                    {
                        cuts.push_back(centre);
                    }
                }
                cuts.push_back(b);
                const auto add = [&tap, low](SharpRuntime::intcs index, double amount)
                { tap.weights[static_cast<std::size_t>(index - low)] += amount; };
                for (std::size_t k = 0; k + 1 < cuts.size(); ++k)
                {
                    const double lo = cuts[k];
                    const double hi = cuts[k + 1];
                    const double middle = (lo + hi) / 2;
                    if (middle <= 0.5)
                    {
                        add(0, hi - lo);
                        continue;
                    }
                    if (middle >= s - 0.5)
                    {
                        add(source - 1, hi - lo);
                        continue;
                    }
                    const auto left = static_cast<SharpRuntime::intcs>(std::floor(middle - 0.5));
                    const double t0 = lo - (left + 0.5);
                    const double t1 = hi - (left + 0.5);
                    const double rising = (t1 * t1 - t0 * t0) / 2;
                    add(left, (t1 - t0) - rising);
                    add(left + 1, rising);
                }
                const double width = b - a;
                for (double& weight : tap.weights)
                {
                    weight /= width;
                }
            }
            return taps;
        }

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

        /// Int32 addition that wraps like C#'s unchecked arithmetic instead of overflowing.
        SharpRuntime::intcs WrappedSum(SharpRuntime::intcs a, SharpRuntime::intcs b) noexcept
        {
            return static_cast<SharpRuntime::intcs>(static_cast<std::uint32_t>(a) +
                                                    static_cast<std::uint32_t>(b));
        }

        // XNA compares Rectangle.Right/Bottom, which wrap past Int32: X=Int32.MaxValue, Width=1
        // gives Right=Int32.MinValue and passes. Genuine XNA was measured to accept such regions
        // and fail later in the operation (CNA-FIX-D05, owner's choice 2026-11-02:
        // tests/reference/xna40/bitmap-region-overflow); the copy paths reproduce those failures.
        bool RegionInside(const Rectangle& region, const BitmapContent& bitmap)
        {
            return region.X >= 0 && region.Y >= 0 && region.Width >= 0 && region.Height >= 0 &&
                   WrappedSum(region.X, region.Width) <= bitmap.getWidthProperty() &&
                   WrappedSum(region.Y, region.Height) <= bitmap.getHeightProperty();
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
        const bool resizes = sourceRegion.Width != destinationRegion.Width ||
                             sourceRegion.Height != destinationRegion.Height;
        if (resizes && (sourceRegion.Width == 0 || sourceRegion.Height == 0))
        {
            // XNA resizes through a bitmap of the source region's size, whose constructor refuses
            // an empty dimension (measured: ArgumentOutOfRangeException "height", CNA-FIX-D05).
            (void) PixelBitmapContent<Vector4>(sourceRegion.Width, sourceRegion.Height);
        }
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

    namespace
    {
        /**
         * @brief Registers every bitmap type the pipeline ships, once.
         *
         * A type registers itself when one is first constructed, which is enough for the copy
         * protocol but not for `ConvertBitmapType`: converting to a type no instance of which
         * exists yet -- the compressed formats a texture processor asks for -- would find an empty
         * registry. XNA reflects the type instead and needs no such list.
         */
        void RegisterBuiltInBitmapTypes()
        {
            static const bool once = []
            {
                namespace Packed = Microsoft::Xna::Framework::Graphics::PackedVector;
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Color>>(PixelBitmapContent<Color>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Vector4>>(PixelBitmapContent<Vector4>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Vector3>>(PixelBitmapContent<Vector3>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Vector2>>(PixelBitmapContent<Vector2>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<float>>(PixelBitmapContent<float>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::Alpha8>>(PixelBitmapContent<Packed::Alpha8>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::Bgr565>>(PixelBitmapContent<Packed::Bgr565>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::Bgra4444>>(PixelBitmapContent<Packed::Bgra4444>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::Bgra5551>>(PixelBitmapContent<Packed::Bgra5551>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::Byte4>>(PixelBitmapContent<Packed::Byte4>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::HalfSingle>>(PixelBitmapContent<Packed::HalfSingle>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::HalfVector2>>(PixelBitmapContent<Packed::HalfVector2>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::HalfVector4>>(PixelBitmapContent<Packed::HalfVector4>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::NormalizedByte2>>(PixelBitmapContent<Packed::NormalizedByte2>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::NormalizedByte4>>(PixelBitmapContent<Packed::NormalizedByte4>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::NormalizedShort2>>(PixelBitmapContent<Packed::NormalizedShort2>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::NormalizedShort4>>(PixelBitmapContent<Packed::NormalizedShort4>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::Rg32>>(PixelBitmapContent<Packed::Rg32>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::Rgba1010102>>(PixelBitmapContent<Packed::Rgba1010102>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::Rgba64>>(PixelBitmapContent<Packed::Rgba64>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::Short2>>(PixelBitmapContent<Packed::Short2>::XnaTypeName);
                BitmapContent::RegisterBitmapType<PixelBitmapContent<Packed::Short4>>(PixelBitmapContent<Packed::Short4>::XnaTypeName);
                BitmapContent::RegisterBitmapType<Dxt1BitmapContent>(std::string(Dxt1BitmapContent::XnaTypeName));
                BitmapContent::RegisterBitmapType<Dxt3BitmapContent>(std::string(Dxt3BitmapContent::XnaTypeName));
                BitmapContent::RegisterBitmapType<Dxt5BitmapContent>(std::string(Dxt5BitmapContent::XnaTypeName));
                return true;
            }();
            (void)once;
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
        RegisterBuiltInBitmapTypes();
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
        RegisterBuiltInBitmapTypes();
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
        // Regions validated with wrapped edges (CNA-FIX-D05) can still lie outside either bitmap.
        // XNA's conversion fails on a row with ArgumentOutOfRangeException("y") and on a column
        // with IndexOutOfRangeException; its resize writes the same way into the destination but
        // reads unvalidated source memory, which CNA reads as transparent black.
        const auto rowOutside = [](const PixelBitmapContentBase& bitmap, SharpRuntime::intcs regionY,
                                   SharpRuntime::intcs y)
        { return static_cast<std::int64_t>(regionY) + y >= bitmap.getHeightProperty(); };
        const auto columnsOutside = [](const PixelBitmapContentBase& bitmap, SharpRuntime::intcs regionX,
                                       SharpRuntime::intcs width)
        { return static_cast<std::int64_t>(regionX) + width > bitmap.getWidthProperty(); };
        if (sw == dw && sh == dh)
        {
            for (SharpRuntime::intcs y = 0; y < sh; ++y)
            {
                if (rowOutside(source, sourceRegion.Y, y) || rowOutside(destination, destinationRegion.Y, y))
                {
                    throw System::ArgumentOutOfRangeException("y");
                }
                if (columnsOutside(source, sourceRegion.X, sw) ||
                    columnsOutside(destination, destinationRegion.X, sw))
                {
                    throw System::IndexOutOfRangeException(
                        "Index was outside the bounds of the array.");
                }
                for (SharpRuntime::intcs x = 0; x < sw; ++x)
                {
                    destination.SetPixelVector4(destinationRegion.X + x, destinationRegion.Y + y,
                                                source.GetPixelVector4(sourceRegion.X + x, sourceRegion.Y + y));
                }
            }
            return;
        }
        // XNA's resize kernel, measured (TQ-034, genuine BitmapContent.Copy over Vector4 pixels, so
        // no rounding or dither hides it: 3x1->4x1, 1x3->1x4, 5x1->8x1, 6x1->8x1, 3x3->4x4,
        // 3x3->4x3, 3x3->3x4, 3x2->4x1, 2x1->3x1, 2x1->4x1, 4x4->2x4, 4x1->1x1, 8x1->2x1 and the
        // committed resize/4x4_to_2x2_vector4, every value to the float). It is separable, and along
        // each axis a destination pixel is the *mean over its own footprint* of the source's linear
        // interpolation between pixel centres, held at the end values outside the first and last
        // centre. That one rule is bilinear in the interior of an enlargement, the {1,7,7,1}/16 taps
        // of a halving and an area average for an odd reduction; it also filters an axis whose
        // size does not change while the other one does (3x3->4x3 blurs its rows). Point sampling
        // at the pixel centre, which this replaced, answered 0 where XNA answers 1/24 at the edge
        // of a 3->4 enlargement and a box where XNA answers {1,7,7,1}/16.
        const std::vector<ResampleTap> tapsX = ResampleTaps(sw, dw);
        const std::vector<ResampleTap> tapsY = ResampleTaps(sh, dh);
        for (SharpRuntime::intcs y = 0; y < dh; ++y)
        {
            if (rowOutside(destination, destinationRegion.Y, y))
            {
                throw System::ArgumentOutOfRangeException("y");
            }
        }
        if (columnsOutside(destination, destinationRegion.X, dw))
        {
            throw System::IndexOutOfRangeException("Index was outside the bounds of the array.");
        }
        const auto sample = [&](SharpRuntime::intcs x, SharpRuntime::intcs y)
        {
            x = std::clamp<SharpRuntime::intcs>(x, 0, sw - 1);
            y = std::clamp<SharpRuntime::intcs>(y, 0, sh - 1);
            const std::int64_t sourceX = static_cast<std::int64_t>(sourceRegion.X) + x;
            const std::int64_t sourceY = static_cast<std::int64_t>(sourceRegion.Y) + y;
            if (sourceX >= source.getWidthProperty() || sourceY >= source.getHeightProperty())
            {
                return Vector4(0, 0, 0, 0);
            }
            return source.GetPixelVector4(static_cast<SharpRuntime::intcs>(sourceX),
                                          static_cast<SharpRuntime::intcs>(sourceY));
        };
        for (SharpRuntime::intcs dy = 0; dy < dh; ++dy)
        {
            const ResampleTap& rowTaps = tapsY[static_cast<std::size_t>(dy)];
            for (SharpRuntime::intcs dx = 0; dx < dw; ++dx)
            {
                const ResampleTap& columnTaps = tapsX[static_cast<std::size_t>(dx)];
                Vector4 accumulated(0, 0, 0, 0);
                for (std::size_t j = 0; j < rowTaps.weights.size(); ++j)
                {
                    const double wy = rowTaps.weights[j];
                    if (wy == 0) { continue; }
                    for (std::size_t i = 0; i < columnTaps.weights.size(); ++i)
                    {
                        const double wx = columnTaps.weights[i];
                        if (wx == 0) { continue; }
                        accumulated = accumulated +
                                      sample(columnTaps.first + static_cast<SharpRuntime::intcs>(i),
                                             rowTaps.first + static_cast<SharpRuntime::intcs>(j)) *
                                          static_cast<float>(wx * wy);
                    }
                }
                destination.SetPixelVector4(destinationRegion.X + dx, destinationRegion.Y + dy, accumulated);
            }
        }
    }
}
