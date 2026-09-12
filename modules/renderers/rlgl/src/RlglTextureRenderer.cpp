// SPDX-License-Identifier: MS-PL

#include "RlglResources.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include "RlglBridge.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Renderers::Rlgl
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

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

        class RlglTextureRenderer final : public ITextureRenderer
        {
        public:
            explicit RlglTextureRenderer(const CNA::Internal::Graphics::ImageData& data)
                : width_(data.width)
                , height_(data.height)
                , mipLevels_(data.mipLevels > 0 ? data.mipLevels : 1)
                , surfaceFormat_(data.surfaceFormat)
                , definedLevels_(static_cast<std::size_t>(mipLevels_), false)
            {
                if (width_ <= 0 || height_ <= 0)
                    throw std::invalid_argument("RLGL: Texture2D dimensions must be positive");
                if (mipLevels_ > MaximumMipLevels(width_, height_))
                    throw std::invalid_argument("RLGL: Texture2D declares too many mip levels");
                if (static_cast<SurfaceFormat>(surfaceFormat_) != SurfaceFormat::Color)
                {
                    throw std::runtime_error(
                        "RLGL: only SurfaceFormat.Color is implemented (plans/plan_rlgl.md "
                        "RLGL-024)");
                }

                const std::size_t expectedBytes =
                    static_cast<std::size_t>(width_) * height_ * 4u;
                if (data.pixels.size() != expectedBytes)
                {
                    throw std::invalid_argument(
                        "RLGL: RGBA8 Texture2D level zero has an invalid byte count");
                }

                id_ = Bridge::CreateTexture2DRgba8(
                    width_, height_, mipLevels_, data.pixels.data());
                definedLevels_[0] = true;
            }

            ~RlglTextureRenderer() override
            {
                Bridge::DestroyTexture2D(id_);
                id_ = 0;
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
                if (data == nullptr || stride != width_ * 4)
                    throw std::invalid_argument("RLGL: invalid RGBA8 level-zero update");
                Bridge::UpdateTexture2DRgba8(id_, 0, width_, height_, data);
                definedLevels_[0] = true;
            }

            void UpdatePixelsLevel(
                const int level, const std::uint8_t* data,
                const int levelWidth, const int levelHeight) override
            {
                ValidateLevel(level, levelWidth, levelHeight);
                if (data == nullptr)
                    throw std::invalid_argument("RLGL: Texture2D update data must not be null");
                Bridge::UpdateTexture2DRgba8(id_, level, levelWidth, levelHeight, data);
                definedLevels_[static_cast<std::size_t>(level)] = true;
            }

            [[nodiscard]] bool HasDefinedMipLevel(const int level) const noexcept override
            {
                return level >= 0 && level < mipLevels_
                    && definedLevels_[static_cast<std::size_t>(level)];
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
                const std::size_t required =
                    static_cast<std::size_t>(width) * height * 4u;
                if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
                    throw std::out_of_range("RLGL: Texture2D readback destination is too small");

                Bridge::ReadTexture2DRgba8(
                    id_, level, levelWidth, levelHeight,
                    x, y, width, height, static_cast<std::uint8_t*>(data));
                return true;
            }

            void BindGL(const int unit) const override
            {
                Bridge::BindTexture2D(id_, unit);
            }

        private:
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
            std::vector<bool> definedLevels_;
        };
    }

    std::unique_ptr<ITextureRenderer> CreateTextureRenderer(
        const CNA::Internal::Graphics::ImageData& data)
    {
        return std::make_unique<RlglTextureRenderer>(data);
    }
}
