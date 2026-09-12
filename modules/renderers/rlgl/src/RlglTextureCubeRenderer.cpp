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
                if (static_cast<SurfaceFormat>(surfaceFormat_) != SurfaceFormat::Color)
                {
                    throw std::invalid_argument(
                        "RLGL: plain TextureCube baseline supports only SurfaceFormat.Color");
                }
                id_ = Bridge::CreateTextureCubeColor(size_, levelCount_);
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
                const int levelSize = ValidateRegion(
                    face, level, x, y, width, height, data, dataLength);
                (void)levelSize;
                Bridge::UpdateTextureCubeColor(
                    id_, face, level, x, y, width, height,
                    static_cast<const std::uint8_t*>(data));
                return true;
            }

            [[nodiscard]] bool GetData(
                const int face, const int level, const int x, const int y,
                const int width, const int height,
                void* data, const int dataLength) const override
            {
                const int levelSize = ValidateRegion(
                    face, level, x, y, width, height, data, dataLength);
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
                return {id_, size_, levelCount_, surfaceFormat_};
            }

        private:
            [[nodiscard]] int ValidateRegion(
                const int face, const int level, const int x, const int y,
                const int width, const int height,
                const void* data, const int dataLength) const
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
                const std::size_t required =
                    static_cast<std::size_t>(width) * height * 4u;
                if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
                    throw std::out_of_range("RLGL: TextureCube transfer buffer is too small");
                return levelSize;
            }

            unsigned int id_ = 0;
            int size_ = 0;
            int levelCount_ = 1;
            int surfaceFormat_ = 0;
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
