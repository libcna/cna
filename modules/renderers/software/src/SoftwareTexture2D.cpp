// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "SoftwareTextureErrors.hpp"

#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::Software
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

        [[nodiscard]] bool IsDxt(int surfaceFormat) noexcept
        {
            const auto format = static_cast<SurfaceFormat>(surfaceFormat);
            return format == SurfaceFormat::Dxt1 || format == SurfaceFormat::Dxt3 ||
                   format == SurfaceFormat::Dxt5;
        }

        [[nodiscard]] int DxtBlockBytes(int surfaceFormat)
        {
            return static_cast<SurfaceFormat>(surfaceFormat) == SurfaceFormat::Dxt1 ? 8 : 16;
        }

        [[nodiscard]] int BytesPerTexel(int surfaceFormat)
        {
            return Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(
                static_cast<SurfaceFormat>(surfaceFormat));
        }

        [[nodiscard]] std::size_t RawByteCount(int surfaceFormat, int width, int height)
        {
            if (IsDxt(surfaceFormat))
            {
                return static_cast<std::size_t>((width + 3) / 4)
                    * static_cast<std::size_t>((height + 3) / 4)
                    * static_cast<std::size_t>(DxtBlockBytes(surfaceFormat));
            }
            return static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
                * static_cast<std::size_t>(BytesPerTexel(surfaceFormat));
        }

        [[nodiscard]] std::uint16_t Read16(const std::uint8_t* bytes) noexcept
        {
            return static_cast<std::uint16_t>(bytes[0]) |
                   static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8u);
        }

        [[nodiscard]] std::uint32_t Read32(const std::uint8_t* bytes) noexcept
        {
            return static_cast<std::uint32_t>(bytes[0]) |
                   (static_cast<std::uint32_t>(bytes[1]) << 8u) |
                   (static_cast<std::uint32_t>(bytes[2]) << 16u) |
                   (static_cast<std::uint32_t>(bytes[3]) << 24u);
        }

        [[nodiscard]] std::uint64_t Read64(const std::uint8_t* bytes) noexcept
        {
            return static_cast<std::uint64_t>(Read32(bytes)) |
                   (static_cast<std::uint64_t>(Read32(bytes + 4)) << 32u);
        }

        [[nodiscard]] float ReadSingle(const std::uint8_t* bytes) noexcept
        {
            return std::bit_cast<float>(Read32(bytes));
        }

        [[nodiscard]] float DecodeSnorm8(std::uint8_t bits) noexcept
        {
            const int value = bits <= 127u ? static_cast<int>(bits)
                                           : static_cast<int>(bits) - 256;
            return value <= -127 ? -1.0f : static_cast<float>(value) / 127.0f;
        }

        [[nodiscard]] std::uint8_t FloatToByte(float value) noexcept
        {
            if (!(value > 0.0f)) return 0u;
            if (value >= 1.0f) return 255u;
            return static_cast<std::uint8_t>(value * 255.0f + 0.5f);
        }

        void StoreDecodedTexel(std::size_t texel, float r, float g, float b, float a,
                               std::vector<std::uint8_t>& rgba8,
                               std::vector<float>& samples)
        {
            const std::size_t offset = texel * 4u;
            rgba8[offset + 0] = FloatToByte(r);
            rgba8[offset + 1] = FloatToByte(g);
            rgba8[offset + 2] = FloatToByte(b);
            rgba8[offset + 3] = FloatToByte(a);
            samples[offset + 0] = r;
            samples[offset + 1] = g;
            samples[offset + 2] = b;
            samples[offset + 3] = a;
        }

        void DecodePixels(int surfaceFormat, const std::uint8_t* source, std::size_t sourceBytes,
                          int sourceStride, int width, int height,
                          std::vector<std::uint8_t>& destination,
                          std::vector<float>& samples)
        {
            if (IsDxt(surfaceFormat))
            {
                using CNA::Internal::Graphics::DxtUtil;
                const auto format = static_cast<SurfaceFormat>(surfaceFormat);
                destination = format == SurfaceFormat::Dxt1
                    ? DxtUtil::DecompressDxt1(source, sourceBytes, width, height)
                    : (format == SurfaceFormat::Dxt3
                        ? DxtUtil::DecompressDxt3(source, sourceBytes, width, height)
                        : DxtUtil::DecompressDxt5(source, sourceBytes, width, height));
                samples.resize(destination.size());
                for (std::size_t i = 0; i < destination.size(); ++i)
                    samples[i] = destination[i] / 255.0f;
                return;
            }

            destination.resize(
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
            samples.resize(destination.size());

            const auto format = static_cast<SurfaceFormat>(surfaceFormat);
            const int bytesPerTexel = BytesPerTexel(surfaceFormat);
            using Microsoft::Xna::Framework::Graphics::PackedVector::HalfTypeHelper;
            for (int y = 0; y < height; ++y)
            {
                const std::uint8_t* row = source + static_cast<std::size_t>(y) * sourceStride;
                for (int x = 0; x < width; ++x)
                {
                    const std::size_t sourceOffset =
                        static_cast<std::size_t>(x) * static_cast<std::size_t>(bytesPerTexel);
                    const std::uint8_t* texelBytes = row + sourceOffset;
                    const std::size_t texel = static_cast<std::size_t>(y)
                        * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                    float r = 0.0f;
                    float g = 0.0f;
                    float b = 0.0f;
                    float a = 1.0f;
                    switch (format)
                    {
                    case SurfaceFormat::Color:
                        r = texelBytes[0] / 255.0f;
                        g = texelBytes[1] / 255.0f;
                        b = texelBytes[2] / 255.0f;
                        a = texelBytes[3] / 255.0f;
                        break;
                    case SurfaceFormat::Bgr565:
                    {
                        const std::uint16_t packed = Read16(texelBytes);
                        r = static_cast<float>((packed >> 11) & 31u) / 31.0f;
                        g = static_cast<float>((packed >> 5) & 63u) / 63.0f;
                        b = static_cast<float>(packed & 31u) / 31.0f;
                        break;
                    }
                    case SurfaceFormat::Bgra5551:
                    {
                        const std::uint16_t packed = Read16(texelBytes);
                        r = static_cast<float>((packed >> 10) & 31u) / 31.0f;
                        g = static_cast<float>((packed >> 5) & 31u) / 31.0f;
                        b = static_cast<float>(packed & 31u) / 31.0f;
                        a = (packed & 0x8000u) != 0u ? 1.0f : 0.0f;
                        break;
                    }
                    case SurfaceFormat::Bgra4444:
                    {
                        const std::uint16_t packed = Read16(texelBytes);
                        r = static_cast<float>((packed >> 8) & 15u) / 15.0f;
                        g = static_cast<float>((packed >> 4) & 15u) / 15.0f;
                        b = static_cast<float>(packed & 15u) / 15.0f;
                        a = static_cast<float>((packed >> 12) & 15u) / 15.0f;
                        break;
                    }
                    case SurfaceFormat::NormalizedByte2:
                        r = DecodeSnorm8(texelBytes[0]);
                        g = DecodeSnorm8(texelBytes[1]);
                        b = a = 1.0f;
                        break;
                    case SurfaceFormat::NormalizedByte4:
                        r = DecodeSnorm8(texelBytes[0]);
                        g = DecodeSnorm8(texelBytes[1]);
                        b = DecodeSnorm8(texelBytes[2]);
                        a = DecodeSnorm8(texelBytes[3]);
                        break;
                    case SurfaceFormat::Rgba1010102:
                    {
                        const std::uint32_t packed = Read32(texelBytes);
                        r = static_cast<float>(packed & 1023u) / 1023.0f;
                        g = static_cast<float>((packed >> 10) & 1023u) / 1023.0f;
                        b = static_cast<float>((packed >> 20) & 1023u) / 1023.0f;
                        a = static_cast<float>((packed >> 30) & 3u) / 3.0f;
                        break;
                    }
                    case SurfaceFormat::Rg32:
                    {
                        const std::uint32_t packed = Read32(texelBytes);
                        r = static_cast<float>(packed & 65535u) / 65535.0f;
                        g = static_cast<float>(packed >> 16) / 65535.0f;
                        b = a = 1.0f;
                        break;
                    }
                    case SurfaceFormat::Rgba64:
                    {
                        const std::uint64_t packed = Read64(texelBytes);
                        r = static_cast<float>(packed & 65535u) / 65535.0f;
                        g = static_cast<float>((packed >> 16) & 65535u) / 65535.0f;
                        b = static_cast<float>((packed >> 32) & 65535u) / 65535.0f;
                        a = static_cast<float>((packed >> 48) & 65535u) / 65535.0f;
                        break;
                    }
                    case SurfaceFormat::Alpha8:
                        a = texelBytes[0] / 255.0f;
                        break;
                    case SurfaceFormat::Single:
                        r = ReadSingle(texelBytes);
                        g = b = a = 1.0f;
                        break;
                    case SurfaceFormat::Vector2:
                        r = ReadSingle(texelBytes);
                        g = ReadSingle(texelBytes + 4);
                        b = a = 1.0f;
                        break;
                    case SurfaceFormat::Vector4:
                        r = ReadSingle(texelBytes);
                        g = ReadSingle(texelBytes + 4);
                        b = ReadSingle(texelBytes + 8);
                        a = ReadSingle(texelBytes + 12);
                        break;
                    case SurfaceFormat::HalfSingle:
                        r = HalfTypeHelper::Convert(Read16(texelBytes));
                        g = b = a = 1.0f;
                        break;
                    case SurfaceFormat::HalfVector2:
                        r = HalfTypeHelper::Convert(Read16(texelBytes));
                        g = HalfTypeHelper::Convert(Read16(texelBytes + 2));
                        b = a = 1.0f;
                        break;
                    case SurfaceFormat::HalfVector4:
                    case SurfaceFormat::HdrBlendable:
                        r = HalfTypeHelper::Convert(Read16(texelBytes));
                        g = HalfTypeHelper::Convert(Read16(texelBytes + 2));
                        b = HalfTypeHelper::Convert(Read16(texelBytes + 4));
                        a = HalfTypeHelper::Convert(Read16(texelBytes + 6));
                        break;
                    default:
                        throw std::runtime_error(
                            "SoftwareTextureRenderer: unsupported SurfaceFormat decode");
                    }
                    StoreDecodedTexel(texel, r, g, b, a, destination, samples);
                }
            }
        }
    }

    // GDI-076: GDI's own 16,384-per-axis ceiling made a single square RGBA8 level as large as
    // 1 GiB before this planner existed -- neither this constructor, GdiRenderer's
    // CreateTexture forward, nor the direct-ImageData boundary rejected it, and a caller-supplied
    // pixel buffer smaller than width*height*4 was copied verbatim rather than validated, an
    // out-of-bounds read waiting to happen the first time the rasterizer sampled past it.
    SoftwareTextureRenderer::SoftwareTextureRenderer(const ImageData& data)
        : width_(data.width), height_(data.height)
        , surfaceFormat_(data.surfaceFormat)
        , declaredLevels_(data.mipLevels > 0 ? data.mipLevels : 1)
    {
        const SoftwareTextureAllocationRequest request{width_, height_, declaredLevels_};
        const SoftwareTextureAllocationLayout layout = PlanSoftwareTextureAllocation(request);
        if (!layout.IsValid())
            ThrowInvalidTextureLayout(request, layout.error);
        const std::size_t rawBaseBytes = RawByteCount(surfaceFormat_, width_, height_);
        if (data.pixels.size() < rawBaseBytes)
            ThrowTexturePixelDataMismatch(width_, height_, rawBaseBytes,
                                         data.pixels.size());

        try
        {
            rawPixels_.assign(data.pixels.begin(),
                              data.pixels.begin() + static_cast<std::ptrdiff_t>(rawBaseBytes));
            DecodePixels(surfaceFormat_, rawPixels_.data(), rawPixels_.size(),
                         IsDxt(surfaceFormat_) ? 0 : width_ * BytesPerTexel(surfaceFormat_),
                         width_, height_, pixels_, samplePixels_);
        }
        catch (const std::bad_alloc&)
        {
            ThrowTextureAllocationFailure(request, layout);
        }
        catch (const std::length_error&)
        {
            ThrowTextureAllocationFailure(request, layout);
        }
    }

    SoftwareTextureRenderer::SoftwareTextureRenderer(int width, int height)
        : width_(width), height_(height)
    {
        const SoftwareTextureAllocationRequest request{width_, height_, declaredLevels_};
        const SoftwareTextureAllocationLayout layout = PlanSoftwareTextureAllocation(request);
        if (!layout.IsValid())
            ThrowInvalidTextureLayout(request, layout.error);

        try
        {
            pixels_.assign(layout.baseColorBytes, 0u);
            rawPixels_ = pixels_;
            samplePixels_.assign(layout.baseColorBytes, 0.0f);
        }
        catch (const std::bad_alloc&)
        {
            ThrowTextureAllocationFailure(request, layout);
        }
        catch (const std::length_error&)
        {
            ThrowTextureAllocationFailure(request, layout);
        }
    }

    void SoftwareTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (rgba == nullptr)
            throw std::runtime_error("SoftwareTextureRenderer::UpdatePixels: rgba must not be null");
        if (IsDxt(surfaceFormat_))
        {
            throw std::runtime_error(
                "SoftwareTextureRenderer::UpdatePixels: compressed blocks require "
                "UpdatePixelsLevel");
        }
        const std::size_t rowBytes = static_cast<std::size_t>(width_)
            * static_cast<std::size_t>(BytesPerTexel(surfaceFormat_));
        // REMED-GFX-229: a positive pitch smaller than one complete RGBA8 row makes the
        // row-by-row copy overlap the prior row and read past the caller's final row. Validate it
        // before resizing or changing the authoritative texture bytes. Zero/negative retains the
        // existing renderer contract of selecting a tightly packed upload.
        if (stride > 0 && static_cast<std::size_t>(stride) < rowBytes)
            throw System::ArgumentOutOfRangeException(
                "stride", std::to_string(stride),
                "A positive texture upload stride must be at least " +
                    std::to_string(rowBytes) + " bytes (width * format bytes per texel).");
        const std::size_t effectiveStride = stride > 0 ? static_cast<std::size_t>(stride) : rowBytes;
        try
        {
            rawPixels_.resize(rowBytes * static_cast<std::size_t>(height_));
            for (int y = 0; y < height_; ++y)
            {
                std::copy(rgba + static_cast<std::size_t>(y) * effectiveStride,
                          rgba + static_cast<std::size_t>(y) * effectiveStride + rowBytes,
                          rawPixels_.begin() + static_cast<std::ptrdiff_t>(y)
                              * static_cast<std::ptrdiff_t>(rowBytes));
            }
            DecodePixels(surfaceFormat_, rawPixels_.data(), rawPixels_.size(),
                         static_cast<int>(rowBytes), width_, height_, pixels_, samplePixels_);
        }
        catch (const std::bad_alloc&)
        {
            ThrowTextureAllocationFailure(
                {width_, height_, declaredLevels_},
                PlanSoftwareTextureAllocation({width_, height_, declaredLevels_}));
        }
        catch (const std::length_error&)
        {
            ThrowTextureAllocationFailure(
                {width_, height_, declaredLevels_},
                PlanSoftwareTextureAllocation({width_, height_, declaredLevels_}));
        }
    }

    // REMED-GFX-175: mip levels above 0 used to be dropped on the floor here, which is why NO
    // TextureFilter ordinal could mip-filter on this renderer -- not merely ordinals 0 and 1. The
    // level a caller supplies is now STORED, exactly as given: nothing is downsampled, nothing is
    // generated, and a level the caller never writes is never invented. SOFTWARE-141 also routes
    // compressed level 0 through this method because the public compressed-transfer contract is
    // expressed in mip-level/block terms. `storedLevels_` counts only the levels held CONTIGUOUSLY
    // from 0, so a chain written out of order, or abandoned half way, bounds the sampler at the last
    // level that really exists instead of exposing a gap.
    void SoftwareTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba,
                                                    int levelW, int levelH)
    {
        if (level < 0 || rgba == nullptr) return;
        if (level >= declaredLevels_) return;
        if (levelW <= 0 || levelH <= 0) return;
        if (level == 0 && (levelW != width_ || levelH != height_)) return;

        // GDI-076: this level's own bytes still participate in the same per-resource budget as
        // level 0 -- a caller-supplied levelW/levelH is trusted for shape (there is no length
        // parameter here to check against), but not for size.
        const SoftwareTextureAllocationRequest levelRequest{levelW, levelH, 1};
        const SoftwareTextureAllocationLayout levelLayout =
            PlanSoftwareTextureAllocation(levelRequest);
        if (!levelLayout.IsValid())
            ThrowInvalidTextureLayout(levelRequest, levelLayout.error);

        if (level == 0)
        {
            try
            {
                const std::size_t rawBytes = RawByteCount(surfaceFormat_, levelW, levelH);
                rawPixels_.assign(rgba, rgba + rawBytes);
                DecodePixels(surfaceFormat_, rawPixels_.data(), rawPixels_.size(),
                             IsDxt(surfaceFormat_) ? 0 : levelW * BytesPerTexel(surfaceFormat_),
                             levelW, levelH, pixels_, samplePixels_);
            }
            catch (const std::bad_alloc&)
            {
                ThrowTextureAllocationFailure(levelRequest, levelLayout);
            }
            catch (const std::length_error&)
            {
                ThrowTextureAllocationFailure(levelRequest, levelLayout);
            }
            return;
        }

        if (static_cast<int>(mipLevels_.size()) < level)
            mipLevels_.resize(static_cast<std::size_t>(level));

        MipLevel& dst = mipLevels_[static_cast<std::size_t>(level - 1)];
        dst.width = levelW;
        dst.height = levelH;
        try
        {
            const std::size_t rawBytes = RawByteCount(surfaceFormat_, levelW, levelH);
            dst.rawPixels.assign(rgba, rgba + rawBytes);
            DecodePixels(surfaceFormat_, dst.rawPixels.data(), dst.rawPixels.size(),
                         IsDxt(surfaceFormat_) ? 0 : levelW * BytesPerTexel(surfaceFormat_),
                         levelW, levelH, dst.pixels, dst.samplePixels);
        }
        catch (const std::bad_alloc&)
        {
            ThrowTextureAllocationFailure(levelRequest, levelLayout);
        }
        catch (const std::length_error&)
        {
            ThrowTextureAllocationFailure(levelRequest, levelLayout);
        }

        // Recount from level 1 upward: a level only counts once every level below it is present.
        int contiguous = 1;
        for (std::size_t i = 0; i < mipLevels_.size(); ++i)
        {
            if (mipLevels_[i].pixels.empty()) break;
            ++contiguous;
        }
        storedLevels_ = contiguous;
    }

    bool SoftwareTextureRenderer::HasDefinedMipLevel(int level) const noexcept
    {
        if (level == 0) return !rawPixels_.empty();
        return level > 0 && level <= static_cast<int>(mipLevels_.size())
            && !mipLevels_[static_cast<std::size_t>(level - 1)].rawPixels.empty();
    }

    bool SoftwareTextureRenderer::GetData(int level, int x, int y, int w, int h,
                                          void* data, int dataLength) const
    {
        if (data == nullptr || level < 0 || level >= declaredLevels_ ||
            x < 0 || y < 0 || w <= 0 || h <= 0)
            return false;

        const int levelWidth = level == 0
            ? width_ : (level <= static_cast<int>(mipLevels_.size())
                ? mipLevels_[static_cast<std::size_t>(level - 1)].width : 0);
        const int levelHeight = level == 0
            ? height_ : (level <= static_cast<int>(mipLevels_.size())
                ? mipLevels_[static_cast<std::size_t>(level - 1)].height : 0);
        if (levelWidth <= 0 || levelHeight <= 0 ||
            w > levelWidth || h > levelHeight ||
            x > levelWidth - w || y > levelHeight - h)
            return false;

        const std::vector<std::uint8_t>& source = level == 0
            ? rawPixels_ : mipLevels_[static_cast<std::size_t>(level - 1)].rawPixels;
        if (IsDxt(surfaceFormat_))
        {
            const bool touchesRightEdge = x + w == levelWidth;
            const bool touchesBottomEdge = y + h == levelHeight;
            if ((x % 4) != 0 || (y % 4) != 0 ||
                ((w % 4) != 0 && !touchesRightEdge) ||
                ((h % 4) != 0 && !touchesBottomEdge))
                return false;

            const std::size_t blockBytes =
                static_cast<std::size_t>(DxtBlockBytes(surfaceFormat_));
            const std::size_t fullBlockColumns =
                static_cast<std::size_t>((levelWidth + 3) / 4);
            const std::size_t fullBlockRows =
                static_cast<std::size_t>((levelHeight + 3) / 4);
            const std::size_t blockX = static_cast<std::size_t>(x / 4);
            const std::size_t blockY = static_cast<std::size_t>(y / 4);
            const std::size_t blockColumns = static_cast<std::size_t>((w + 3) / 4);
            const std::size_t blockRows = static_cast<std::size_t>((h + 3) / 4);
            const std::size_t required = blockColumns * blockRows * blockBytes;
            const std::size_t fullRequired = fullBlockColumns * fullBlockRows * blockBytes;
            if (source.size() < fullRequired || dataLength < 0 ||
                static_cast<std::size_t>(dataLength) < required)
                return false;

            auto* destination = static_cast<std::uint8_t*>(data);
            const std::size_t sourceRowBytes = fullBlockColumns * blockBytes;
            const std::size_t copyBytes = blockColumns * blockBytes;
            for (std::size_t row = 0; row < blockRows; ++row)
            {
                const std::size_t sourceOffset =
                    (blockY + row) * sourceRowBytes + blockX * blockBytes;
                std::memcpy(destination + row * copyBytes,
                            source.data() + sourceOffset, copyBytes);
            }
            return true;
        }

        const std::size_t bytesPerTexel =
            static_cast<std::size_t>(BytesPerTexel(surfaceFormat_));
        const std::size_t required = static_cast<std::size_t>(w)
            * static_cast<std::size_t>(h) * bytesPerTexel;
        const std::size_t fullRequired = static_cast<std::size_t>(levelWidth)
            * static_cast<std::size_t>(levelHeight) * bytesPerTexel;
        if (source.size() < fullRequired || dataLength < 0 ||
            static_cast<std::size_t>(dataLength) < required)
            return false;

        auto* destination = static_cast<std::uint8_t*>(data);
        const std::size_t copyBytes = static_cast<std::size_t>(w) * bytesPerTexel;
        for (int row = 0; row < h; ++row)
        {
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(levelWidth)
                 + static_cast<std::size_t>(x)) * bytesPerTexel;
            std::memcpy(destination + static_cast<std::size_t>(row) * copyBytes,
                        source.data() + sourceOffset, copyBytes);
        }
        return true;
    }

    int SoftwareTextureRenderer::ColorWidth(int level) const
    {
        if (level <= 0 || level > static_cast<int>(mipLevels_.size())) return width_;
        return mipLevels_[static_cast<std::size_t>(level - 1)].width;
    }

    int SoftwareTextureRenderer::ColorHeight(int level) const
    {
        if (level <= 0 || level > static_cast<int>(mipLevels_.size())) return height_;
        return mipLevels_[static_cast<std::size_t>(level - 1)].height;
    }

    const std::vector<std::uint8_t>& SoftwareTextureRenderer::ColorPixels(int level) const
    {
        if (level <= 0 || level > static_cast<int>(mipLevels_.size())) return pixels_;
        return mipLevels_[static_cast<std::size_t>(level - 1)].pixels;
    }

    void SoftwareTextureRenderer::FetchColorTexel(int level, int x, int y,
                                                   float& r, float& g, float& b, float& a) const
    {
        const int levelWidth = ColorWidth(level);
        const std::vector<float>& samples =
            level <= 0 || level > static_cast<int>(mipLevels_.size())
                ? samplePixels_
                : mipLevels_[static_cast<std::size_t>(level - 1)].samplePixels;
        const std::size_t offset =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(levelWidth) +
             static_cast<std::size_t>(x)) * 4u;
        r = samples[offset + 0];
        g = samples[offset + 1];
        b = samples[offset + 2];
        a = samples[offset + 3];
    }
}
