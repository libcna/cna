// SPDX-License-Identifier: MS-PL

#include "RlglResources.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include "RlglBridge.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

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

        class RlglTextureCubeRenderer final : public ITextureCubeRenderer
        {
        public:
            RlglTextureCubeRenderer(
                const int size, const bool mipMap, const int surfaceFormat)
                : size_(size)
                , levelCount_(mipMap ? MaximumMipLevels(size) : 1)
                , surfaceFormat_(surfaceFormat)
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
            }

            ~RlglTextureCubeRenderer() override
            {
                Bridge::DestroyTextureCube(id_);
                id_ = 0;
            }

            RlglTextureCubeRenderer(const RlglTextureCubeRenderer&) = delete;
            RlglTextureCubeRenderer& operator=(const RlglTextureCubeRenderer&) = delete;

            [[nodiscard]] bool SetData(
                const int face, const int level, const int x, const int y,
                const int width, const int height,
                const void* data, const int dataLength) override
            {
                if (compressed_) return false;
                (void)ValidateRegion(face, level, x, y, width, height, data);
                ValidateByteCount(width, height, 4, dataLength);
                Bridge::UpdateTextureCubeColor(
                    id_, face, level, x, y, width, height,
                    static_cast<const std::uint8_t*>(data));
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
                Bridge::UpdateTextureCubeDxt(
                    id_, surfaceFormat_, nativeCompressed_, face, level,
                    x, y, width, height, static_cast<const std::uint8_t*>(data), required);
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

            [[nodiscard]] TextureCubeResourceSnapshot Snapshot() const noexcept
            {
                return {id_, size_, levelCount_, surfaceFormat_, nativeCompressed_};
            }

        private:
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
        };
    }

    std::unique_ptr<ITextureCubeRenderer> CreateTextureCubeRenderer(
        const int size, const bool mipMap, const int surfaceFormat)
    {
        return std::make_unique<RlglTextureCubeRenderer>(size, mipMap, surfaceFormat);
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
