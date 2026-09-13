// SPDX-License-Identifier: MS-PL

#include "RlglResources.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

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

        [[nodiscard]] int MaximumMipLevels(int size)
        {
            int levels = 1;
            while (size > 1)
            {
                size = std::max(1, size / 2);
                ++levels;
            }
            return levels;
        }

        [[nodiscard]] int MipSize(const int base, const int level)
        {
            return std::max(1, base >> level);
        }

        [[nodiscard]] bool IsDxt(const SurfaceFormat format)
        {
            return format == SurfaceFormat::Dxt1 ||
                format == SurfaceFormat::Dxt3 || format == SurfaceFormat::Dxt5;
        }

        class RlglTextureCubeRenderer final
            : public ITextureCubeRenderer,
              public IRlglTextureResource,
              public IRlglNativeResource
        {
        public:
            RlglTextureCubeRenderer(
                const int size, const bool mipMap, const int surfaceFormat,
                std::shared_ptr<RlglResourceLifetime> lifetime)
                : size_(size)
                , levelCount_(mipMap ? MaximumMipLevels(size) : 1)
                , surfaceFormat_(surfaceFormat)
                , lifetime_(std::move(lifetime))
            {
                if (size_ <= 0)
                    throw std::invalid_argument("RLGL: TextureCube size must be positive");
                const SurfaceFormat format = static_cast<SurfaceFormat>(surfaceFormat_);
                compressed_ = IsDxt(format);
                if (format != SurfaceFormat::Color && !compressed_)
                {
                    throw std::invalid_argument(
                        "RLGL: TextureCube format has no exact public transfer path");
                }
                if (compressed_)
                {
                    blockBytes_ = format == SurfaceFormat::Dxt1 ? 8 : 16;
                    nativeCompressed_ = Bridge::SupportsDxtTextureCube(surfaceFormat_);
                    id_ = Bridge::CreateTextureCubeDxt(
                        surfaceFormat_, size_, levelCount_, nativeCompressed_);
                }
                else
                {
                    id_ = Bridge::CreateTextureCubeColor(size_, levelCount_);
                }
                bool registered = false;
                try
                {
                    recoveryRegistered_ = lifetime_->Register(*this);
                    registered = true;
                    if (recoveryRegistered_)
                    {
                        const std::size_t count =
                            static_cast<std::size_t>(6 * levelCount_);
                        definedSubresources_.assign(count, false);
                        recoverySubresources_.resize(count);
                    }
                }
                catch (...)
                {
                    if (registered) lifetime_->Dispose(*this);
                    else ReleaseNativeResource();
                    throw;
                }
            }

            ~RlglTextureCubeRenderer() override
            {
                lifetime_->Dispose(*this);
            }

            RlglTextureCubeRenderer(const RlglTextureCubeRenderer&) = delete;
            RlglTextureCubeRenderer& operator=(const RlglTextureCubeRenderer&) = delete;

            [[nodiscard]] bool SetData(
                const int face, const int level, const int x, const int y,
                const int width, const int height,
                const void* data, const int dataLength) override
            {
                if (compressed_) return false;
                const int levelSize =
                    ValidateRegion(face, level, x, y, width, height, data);
                ValidateByteCount(width, height, 4, dataLength);
                std::vector<std::uint8_t> recoveryCopy;
                if (recoveryRegistered_)
                {
                    recoveryCopy = PrepareColorShadow(
                        face, level, levelSize, x, y, width, height,
                        static_cast<const std::uint8_t*>(data));
                }
                Bridge::UpdateTextureCubeColor(
                    id_, face, level, x, y, width, height,
                    static_cast<const std::uint8_t*>(data));
                CommitRecoveryShadow(face, level, std::move(recoveryCopy));
                return true;
            }

            [[nodiscard]] bool SetCompressedDataEXT(
                const int face, const int level, const int x, const int y,
                const int width, const int height,
                const void* data, const int dataLength) override
            {
                if (!compressed_) return false;
                const int levelSize = ValidateRegion(
                    face, level, x, y, width, height, data);
                if ((x % 4) != 0 || (y % 4) != 0 ||
                    ((width % 4) != 0 && x + width != levelSize) ||
                    ((height % 4) != 0 && y + height != levelSize))
                {
                    throw std::out_of_range(
                        "RLGL: compressed TextureCube rectangle is not block-aligned");
                }
                const std::size_t required =
                    static_cast<std::size_t>((width + 3) / 4) *
                    static_cast<std::size_t>((height + 3) / 4) * blockBytes_;
                if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
                {
                    throw std::out_of_range(
                        "RLGL: compressed TextureCube transfer buffer is too small");
                }
                std::vector<std::uint8_t> recoveryCopy;
                if (recoveryRegistered_)
                {
                    recoveryCopy = PrepareCompressedShadow(
                        face, level, levelSize, x, y, width, height,
                        static_cast<const std::uint8_t*>(data));
                }
                Bridge::UpdateTextureCubeDxt(
                    id_, surfaceFormat_, nativeCompressed_, face, level,
                    x, y, width, height, static_cast<const std::uint8_t*>(data), required);
                CommitRecoveryShadow(face, level, std::move(recoveryCopy));
                return true;
            }

            [[nodiscard]] bool GetData(
                const int face, const int level, const int x, const int y,
                const int width, const int height,
                void* data, const int dataLength) const override
            {
                const int levelSize = ValidateRegion(
                    face, level, x, y, width, height, data);
                ValidateByteCount(width, height, 4, dataLength);
                Bridge::ReadTextureCubeColor(
                    id_, face, level, levelSize, x, y, width, height,
                    static_cast<std::uint8_t*>(data));
                return true;
            }

            void BindGL(const int unit) const override
            {
                Bridge::BindTextureCube(id_, unit);
            }

            [[nodiscard]] int GetSizeEXT() const noexcept override { return size_; }

            [[nodiscard]] int GetSurfaceFormatEXT() const noexcept override
            {
                return surfaceFormat_;
            }

            [[nodiscard]] unsigned int NativeTextureId() const noexcept override
            {
                return id_;
            }

            [[nodiscard]] bool SampledRowsAreBottomUp() const noexcept override
            {
                return false;
            }

            [[nodiscard]] TextureCubeResourceSnapshot Snapshot() const
            {
                return {
                    id_, size_, levelCount_, surfaceFormat_, nativeCompressed_,
                    recoveryRegistered_, definedSubresources_, recoverySubresources_};
            }

        private:
            void ReleaseNativeResource() noexcept override
            {
                Bridge::DestroyTextureCube(id_);
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
                    std::count(
                        definedSubresources_.begin(), definedSubresources_.end(), true));
                for (const auto& subresource : recoverySubresources_)
                    info.retainedCpuBytes += subresource.size();
                return info;
            }

            [[nodiscard]] std::size_t SubresourceIndex(
                const int face, const int level) const noexcept
            {
                return static_cast<std::size_t>(face * levelCount_ + level);
            }

            [[nodiscard]] std::vector<std::uint8_t> PrepareColorShadow(
                const int face, const int level, const int levelSize,
                const int x, const int y, const int width, const int height,
                const std::uint8_t* const data) const
            {
                const std::size_t index = SubresourceIndex(face, level);
                std::vector<std::uint8_t> result = recoverySubresources_[index];
                if (result.empty())
                {
                    result.assign(
                        static_cast<std::size_t>(levelSize) * levelSize * 4u, 0u);
                }
                const std::size_t destinationRowBytes =
                    static_cast<std::size_t>(levelSize) * 4u;
                const std::size_t sourceRowBytes = static_cast<std::size_t>(width) * 4u;
                for (int row = 0; row < height; ++row)
                {
                    std::memcpy(
                        result.data() + static_cast<std::size_t>(y + row) *
                            destinationRowBytes + static_cast<std::size_t>(x) * 4u,
                        data + static_cast<std::size_t>(row) * sourceRowBytes,
                        sourceRowBytes);
                }
                return result;
            }

            [[nodiscard]] std::vector<std::uint8_t> PrepareCompressedShadow(
                const int face, const int level, const int levelSize,
                const int x, const int y, const int width, const int height,
                const std::uint8_t* const data) const
            {
                const std::size_t index = SubresourceIndex(face, level);
                const int levelBlockColumns = (levelSize + 3) / 4;
                const int levelBlockRows = (levelSize + 3) / 4;
                const int rectangleBlockColumns = (width + 3) / 4;
                const int rectangleBlockRows = (height + 3) / 4;
                std::vector<std::uint8_t> result = recoverySubresources_[index];
                if (result.empty())
                {
                    result.assign(
                        static_cast<std::size_t>(levelBlockColumns) * levelBlockRows *
                            static_cast<std::size_t>(blockBytes_),
                        0u);
                }
                const std::size_t destinationRowBytes =
                    static_cast<std::size_t>(levelBlockColumns) * blockBytes_;
                const std::size_t sourceRowBytes =
                    static_cast<std::size_t>(rectangleBlockColumns) * blockBytes_;
                for (int row = 0; row < rectangleBlockRows; ++row)
                {
                    std::memcpy(
                        result.data() + static_cast<std::size_t>(y / 4 + row) *
                            destinationRowBytes +
                            static_cast<std::size_t>(x / 4) * blockBytes_,
                        data + static_cast<std::size_t>(row) * sourceRowBytes,
                        sourceRowBytes);
                }
                return result;
            }

            void CommitRecoveryShadow(
                const int face, const int level, std::vector<std::uint8_t> bytes)
            {
                if (!recoveryRegistered_) return;
                const std::size_t index = SubresourceIndex(face, level);
                recoverySubresources_[index] = std::move(bytes);
                definedSubresources_[index] = true;
            }

            [[nodiscard]] int ValidateRegion(
                const int face, const int level, const int x, const int y,
                const int width, const int height,
                const void* data) const
            {
                if (face < 0 || face >= 6)
                    throw std::out_of_range("RLGL: TextureCube face is invalid");
                if (level < 0 || level >= levelCount_)
                    throw std::out_of_range("RLGL: TextureCube mip level is invalid");
                const int levelSize = MipSize(size_, level);
                if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
                    x > levelSize - width || y > levelSize - height || data == nullptr)
                {
                    throw std::out_of_range("RLGL: TextureCube transfer rectangle is invalid");
                }
                return levelSize;
            }

            static void ValidateByteCount(
                const int width, const int height, const int bytesPerTexel,
                const int dataLength)
            {
                const std::size_t required = static_cast<std::size_t>(width) *
                    static_cast<std::size_t>(height) * bytesPerTexel;
                if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
                    throw std::out_of_range("RLGL: TextureCube transfer buffer is too small");
            }

            unsigned int id_ = 0;
            int size_ = 0;
            int levelCount_ = 1;
            int surfaceFormat_ = 0;
            int blockBytes_ = 0;
            bool compressed_ = false;
            bool nativeCompressed_ = false;
            bool recoveryRegistered_ = false;
            std::vector<bool> definedSubresources_;
            std::vector<std::vector<std::uint8_t>> recoverySubresources_;
            std::shared_ptr<RlglResourceLifetime> lifetime_;
        };
    }

    std::unique_ptr<ITextureCubeRenderer> CreateTextureCubeRenderer(
        const int size, const bool mipMap, const int surfaceFormat,
        const std::shared_ptr<RlglResourceLifetime>& lifetime)
    {
        return std::make_unique<RlglTextureCubeRenderer>(
            size, mipMap, surfaceFormat, lifetime);
    }

    TextureCubeResourceSnapshot GetTextureCubeResourceSnapshotForTesting(
        const ITextureCubeRenderer& resource)
    {
        const auto* const texture = dynamic_cast<const RlglTextureCubeRenderer*>(&resource);
        if (texture == nullptr)
            throw std::invalid_argument("RLGL: cube texture belongs to another renderer");
        return texture->Snapshot();
    }
}
