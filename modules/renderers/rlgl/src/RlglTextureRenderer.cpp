// SPDX-License-Identifier: MS-PL

#include "RlglResources.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"

#include "RlglBridge.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace CNA::Internal::Renderers::Rlgl
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        using Microsoft::Xna::Framework::Graphics::Texture;

        [[nodiscard]] int MaximumMipLevels(int width, int height)
        {
            int levels = 1;
            while (width > 1 || height > 1)
            {
                width = std::max(1, width / 2);
                height = std::max(1, height / 2);
                ++levels;
            }
            return levels;
        }

        [[nodiscard]] int MipDimension(const int base, const int level)
        {
            return std::max(1, base >> level);
        }

        [[nodiscard]] bool IsImplementedFormat(const SurfaceFormat format)
        {
            switch (format)
            {
            case SurfaceFormat::Color:
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::NormalizedByte4:
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
            case SurfaceFormat::Rgba1010102:
            case SurfaceFormat::Rg32:
            case SurfaceFormat::Rgba64:
            case SurfaceFormat::Alpha8:
            case SurfaceFormat::Single:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::Vector4:
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                return true;
            default:
                return false;
            }
        }

        class RlglTextureRenderer final
            : public ITextureRenderer,
              public IRlglTextureResource,
              public IRlglNativeResource
        {
        public:
            RlglTextureRenderer(
                const CNA::Internal::Graphics::ImageData& data,
                std::shared_ptr<RlglResourceLifetime> lifetime)
                : width_(data.width)
                , height_(data.height)
                , mipLevels_(data.mipLevels > 0 ? data.mipLevels : 1)
                , surfaceFormat_(data.surfaceFormat)
                , definedLevels_(static_cast<std::size_t>(mipLevels_), false)
                , lifetime_(std::move(lifetime))
            {
                if (width_ <= 0 || height_ <= 0)
                    throw std::invalid_argument("RLGL: Texture2D dimensions must be positive");
                if (mipLevels_ > MaximumMipLevels(width_, height_))
                    throw std::invalid_argument("RLGL: Texture2D declares too many mip levels");
                const SurfaceFormat format = static_cast<SurfaceFormat>(surfaceFormat_);
                if (!IsImplementedFormat(format))
                {
                    throw std::runtime_error(
                        "RLGL: SurfaceFormat is not implemented (plans/plan_rlgl.md RLGL-024)");
                }

                compressed_ = format == SurfaceFormat::Dxt1 ||
                    format == SurfaceFormat::Dxt3 || format == SurfaceFormat::Dxt5;
                nativeCompressed_ = compressed_ &&
                    Bridge::SupportsDxtTexture2D(surfaceFormat_);
                const int formatBytes = Texture::GetFormatSizeEXT(format);
                if (compressed_) blockBytes_ = formatBytes;
                else bytesPerTexel_ = formatBytes;
                const std::size_t expectedBytes = compressed_
                    ? static_cast<std::size_t>((width_ + 3) / 4) *
                        static_cast<std::size_t>((height_ + 3) / 4) * blockBytes_
                    : static_cast<std::size_t>(width_) * height_ * bytesPerTexel_;
                if (data.pixels.size() != expectedBytes)
                {
                    throw std::invalid_argument(
                        "RLGL: Texture2D level zero has an invalid byte count");
                }

                id_ = Bridge::CreateTexture2D(
                    surfaceFormat_, width_, height_, mipLevels_, data.pixels.data());
                bool registered = false;
                try
                {
                    recoveryRegistered_ = lifetime_->Register(*this);
                    registered = true;
                    if (recoveryRegistered_ && !(compressed_ && !nativeCompressed_))
                    {
                        recoveryLevels_.resize(static_cast<std::size_t>(mipLevels_));
                        recoveryLevels_[0] = data.pixels;
                    }
                    if (compressed_ && !nativeCompressed_)
                    {
                        fallbackCompressedLevels_.resize(
                            static_cast<std::size_t>(mipLevels_));
                        fallbackCompressedLevels_[0] = data.pixels;
                    }
                    definedLevels_[0] = true;
                }
                catch (...)
                {
                    if (registered) lifetime_->Dispose(*this);
                    else ReleaseNativeResource();
                    throw;
                }
            }

            ~RlglTextureRenderer() override
            {
                lifetime_->Dispose(*this);
            }

            RlglTextureRenderer(const RlglTextureRenderer&) = delete;
            RlglTextureRenderer& operator=(const RlglTextureRenderer&) = delete;

            [[nodiscard]] int GetWidth() const override { return width_; }
            [[nodiscard]] int GetHeight() const override { return height_; }
            [[nodiscard]] int GetSurfaceFormatEXT() const noexcept override
            {
                return surfaceFormat_;
            }

            void UpdatePixels(const std::uint8_t* data, const int stride) override
            {
                if (compressed_)
                    throw std::invalid_argument(
                        "RLGL: compressed Texture2D requires block transfer");
                if (data == nullptr || stride != width_ * bytesPerTexel_)
                    throw std::invalid_argument("RLGL: invalid level-zero texture update");
                const std::size_t byteCount =
                    static_cast<std::size_t>(width_) * height_ * bytesPerTexel_;
                const bool sharedBytesAreCurrent = sharedLevelZero_ &&
                    sharedLevelZero_->size() == byteCount &&
                    sharedLevelZero_->data() == data;
                std::vector<std::uint8_t> recoveryCopy;
                if (recoveryRegistered_ && !sharedBytesAreCurrent)
                {
                    recoveryCopy.assign(data, data + byteCount);
                }
                Bridge::UpdateTexture2D(
                    id_, surfaceFormat_, 0, width_, height_, data);
                if (recoveryRegistered_ && !sharedBytesAreCurrent)
                {
                    if (sharedLevelZero_) *sharedLevelZero_ = std::move(recoveryCopy);
                    else recoveryLevels_[0] = std::move(recoveryCopy);
                }
                definedLevels_[0] = true;
            }

            void UpdatePixelsLevel(
                const int level, const std::uint8_t* data,
                const int levelWidth, const int levelHeight) override
            {
                ValidateLevel(level, levelWidth, levelHeight);
                if (data == nullptr)
                    throw std::invalid_argument("RLGL: Texture2D update data must not be null");
                const std::size_t byteCount = compressed_
                    ? static_cast<std::size_t>((levelWidth + 3) / 4) *
                        static_cast<std::size_t>((levelHeight + 3) / 4) * blockBytes_
                    : static_cast<std::size_t>(levelWidth) * levelHeight * bytesPerTexel_;
                const bool sharedBytesAreCurrent = level == 0 && sharedLevelZero_ &&
                    sharedLevelZero_->size() == byteCount &&
                    sharedLevelZero_->data() == data;
                std::vector<std::uint8_t> recoveryCopy;
                std::vector<std::uint8_t> fallbackCopy;
                if (recoveryRegistered_ && !(compressed_ && !nativeCompressed_) &&
                    !sharedBytesAreCurrent)
                {
                    recoveryCopy.assign(data, data + byteCount);
                }
                if (compressed_ && !nativeCompressed_)
                    fallbackCopy.assign(data, data + byteCount);
                Bridge::UpdateTexture2D(
                    id_, surfaceFormat_, level, levelWidth, levelHeight, data);
                if (recoveryRegistered_ && !(compressed_ && !nativeCompressed_) &&
                    !sharedBytesAreCurrent)
                {
                    if (level == 0 && sharedLevelZero_)
                        *sharedLevelZero_ = std::move(recoveryCopy);
                    else
                        recoveryLevels_[static_cast<std::size_t>(level)] =
                            std::move(recoveryCopy);
                }
                if (compressed_ && !nativeCompressed_)
                    fallbackCompressedLevels_[static_cast<std::size_t>(level)] =
                        std::move(fallbackCopy);
                definedLevels_[static_cast<std::size_t>(level)] = true;
            }

            [[nodiscard]] bool HasDefinedMipLevel(const int level) const noexcept override
            {
                return level >= 0 && level < mipLevels_
                    && definedLevels_[static_cast<std::size_t>(level)];
            }

            void ShareCpuPixels(std::shared_ptr<std::vector<std::uint8_t>> pixels) override
            {
                if (!recoveryRegistered_ || compressed_ || !pixels) return;
                const std::size_t expected =
                    static_cast<std::size_t>(width_) * height_ * bytesPerTexel_;
                if (pixels->size() != expected) return;
                sharedLevelZero_ = std::move(pixels);
                std::vector<std::uint8_t>().swap(recoveryLevels_[0]);
            }

            [[nodiscard]] bool GetData(
                const int level, const int x, const int y,
                const int width, const int height,
                void* data, const int dataLength) const override
            {
                if (level < 0 || level >= mipLevels_)
                    throw std::out_of_range("RLGL: Texture2D mip level is invalid");
                const int levelWidth = MipDimension(width_, level);
                const int levelHeight = MipDimension(height_, level);
                if (!HasDefinedMipLevel(level)) return false;
                if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
                    x > levelWidth - width || y > levelHeight - height || data == nullptr)
                {
                    throw std::out_of_range("RLGL: Texture2D readback rectangle is invalid");
                }
                if (compressed_ &&
                    ((x % 4) != 0 || (y % 4) != 0 ||
                     ((width % 4) != 0 && x + width != levelWidth) ||
                     ((height % 4) != 0 && y + height != levelHeight)))
                {
                    throw std::out_of_range(
                        "RLGL: compressed Texture2D readback rectangle is not block-aligned");
                }
                const std::size_t required = compressed_
                    ? static_cast<std::size_t>((width + 3) / 4) *
                        static_cast<std::size_t>((height + 3) / 4) * blockBytes_
                    : static_cast<std::size_t>(width) * height * bytesPerTexel_;
                if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
                    throw std::out_of_range("RLGL: Texture2D readback destination is too small");

                if (compressed_ && !nativeCompressed_)
                {
                    const auto& levelBytes =
                        fallbackCompressedLevels_[static_cast<std::size_t>(level)];
                    const int levelBlockColumns = (levelWidth + 3) / 4;
                    const int rectangleBlockColumns = (width + 3) / 4;
                    const int rectangleBlockRows = (height + 3) / 4;
                    const std::size_t sourceRowBytes =
                        static_cast<std::size_t>(levelBlockColumns) * blockBytes_;
                    const std::size_t destinationRowBytes =
                        static_cast<std::size_t>(rectangleBlockColumns) * blockBytes_;
                    auto* destination = static_cast<std::uint8_t*>(data);
                    for (int row = 0; row < rectangleBlockRows; ++row)
                    {
                        const std::uint8_t* source = levelBytes.data()
                            + static_cast<std::size_t>(y / 4 + row) * sourceRowBytes
                            + static_cast<std::size_t>(x / 4) * blockBytes_;
                        std::memcpy(
                            destination + static_cast<std::size_t>(row) * destinationRowBytes,
                            source, destinationRowBytes);
                    }
                }
                else
                {
                    Bridge::ReadTexture2D(
                        id_, surfaceFormat_, level, levelWidth, levelHeight,
                        x, y, width, height, static_cast<std::uint8_t*>(data));
                }
                return true;
            }

            void BindGL(const int unit) const override
            {
                Bridge::BindTexture2D(id_, unit);
            }

            [[nodiscard]] unsigned int NativeTextureId() const noexcept override
            {
                return id_;
            }

            [[nodiscard]] bool SampledRowsAreBottomUp() const noexcept override
            {
                return false;
            }

            [[nodiscard]] Texture2DResourceSnapshot Snapshot() const
            {
                Texture2DResourceSnapshot snapshot;
                snapshot.texture = id_;
                snapshot.width = width_;
                snapshot.height = height_;
                snapshot.levelCount = mipLevels_;
                snapshot.surfaceFormat = surfaceFormat_;
                snapshot.nativeCompressed = nativeCompressed_;
                snapshot.recoveryRegistered = recoveryRegistered_;
                if (recoveryRegistered_)
                {
                    snapshot.definedLevels = definedLevels_;
                    snapshot.recoveryLevels = compressed_ && !nativeCompressed_
                        ? fallbackCompressedLevels_ : recoveryLevels_;
                    if (sharedLevelZero_)
                        snapshot.recoveryLevels[0] = *sharedLevelZero_;
                }
                return snapshot;
            }

        private:
            void ReleaseNativeResource() noexcept override
            {
                Bridge::DestroyTexture2D(id_);
                id_ = 0;
            }

            void InvalidateNativeResource() noexcept override
            {
                id_ = 0;
            }

            [[nodiscard]] RlglResourceRecoveryInfo GetRecoveryInfo() const noexcept override
            {
                RlglResourceRecoveryInfo info;
                info.definedTextureSubresources = static_cast<std::size_t>(
                    std::count(definedLevels_.begin(), definedLevels_.end(), true));
                if (compressed_ && !nativeCompressed_)
                {
                    for (const auto& level : fallbackCompressedLevels_)
                        info.retainedCpuBytes += level.size();
                }
                else
                {
                    for (std::size_t level = 0; level < recoveryLevels_.size(); ++level)
                    {
                        info.retainedCpuBytes += level == 0 && sharedLevelZero_
                            ? sharedLevelZero_->size() : recoveryLevels_[level].size();
                    }
                }
                return info;
            }

            void ValidateLevel(
                const int level, const int levelWidth, const int levelHeight) const
            {
                if (level < 0 || level >= mipLevels_ ||
                    levelWidth != MipDimension(width_, level) ||
                    levelHeight != MipDimension(height_, level))
                {
                    throw std::out_of_range("RLGL: Texture2D mip level is invalid");
                }
            }

            unsigned int id_ = 0;
            int width_ = 0;
            int height_ = 0;
            int mipLevels_ = 1;
            int surfaceFormat_ = 0;
            int bytesPerTexel_ = 4;
            int blockBytes_ = 0;
            bool compressed_ = false;
            bool nativeCompressed_ = false;
            bool recoveryRegistered_ = false;
            std::vector<bool> definedLevels_;
            std::vector<std::vector<std::uint8_t>> fallbackCompressedLevels_;
            std::vector<std::vector<std::uint8_t>> recoveryLevels_;
            std::shared_ptr<std::vector<std::uint8_t>> sharedLevelZero_;
            std::shared_ptr<RlglResourceLifetime> lifetime_;
        };
    }

    std::unique_ptr<ITextureRenderer> CreateTextureRenderer(
        const CNA::Internal::Graphics::ImageData& data,
        const std::shared_ptr<RlglResourceLifetime>& lifetime)
    {
        return std::make_unique<RlglTextureRenderer>(data, lifetime);
    }

    unsigned int GetNativeTextureId(const ITextureRenderer& resource)
    {
        const auto* const texture = dynamic_cast<const IRlglTextureResource*>(&resource);
        if (texture == nullptr)
            throw std::invalid_argument("RLGL: texture resource belongs to another renderer");
        return texture->NativeTextureId();
    }

    bool SampledRowsAreBottomUp(const ITextureRenderer& resource)
    {
        const auto* const texture = dynamic_cast<const IRlglTextureResource*>(&resource);
        if (texture == nullptr)
            throw std::invalid_argument("RLGL: texture resource belongs to another renderer");
        return texture->SampledRowsAreBottomUp();
    }

    Texture2DResourceSnapshot GetTexture2DResourceSnapshotForTesting(
        const ITextureRenderer& resource)
    {
        const auto* const texture = dynamic_cast<const RlglTextureRenderer*>(&resource);
        if (texture == nullptr)
            throw std::invalid_argument("RLGL: texture belongs to another renderer");
        return texture->Snapshot();
    }
}
