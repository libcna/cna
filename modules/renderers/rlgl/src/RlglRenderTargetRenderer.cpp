// SPDX-License-Identifier: MS-PL

#include "RlglResources.hpp"

#include "RlglBridge.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Renderers::Rlgl
{
    namespace
    {
        [[nodiscard]] int CalculateMipLevels(int width, int height)
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

        [[nodiscard]] std::vector<std::uint8_t> FlipRows(
            const std::uint8_t* const pixels, const int width, const int height,
            const int bytesPerTexel)
        {
            const std::size_t rowBytes = static_cast<std::size_t>(width) *
                static_cast<std::size_t>(bytesPerTexel);
            std::vector<std::uint8_t> flipped(rowBytes * static_cast<std::size_t>(height));
            for (int row = 0; row < height; ++row)
            {
                std::memcpy(
                    flipped.data() + static_cast<std::size_t>(height - row - 1) * rowBytes,
                    pixels + static_cast<std::size_t>(row) * rowBytes, rowBytes);
            }
            return flipped;
        }

        class RlglRenderTargetRenderer final
            : public IRenderTargetRenderer, public IRlglTextureResource
        {
        public:
            RlglRenderTargetRenderer(
                const int width, const int height, const int depthFormat,
                const bool preserveContents, const bool mipMap,
                const int multiSampleCount, const int surfaceFormat)
                : width_(width)
                , height_(height)
                , depthFormat_(depthFormat)
                , levelCount_(mipMap ? CalculateMipLevels(width, height) : 1)
                , surfaceFormat_(surfaceFormat)
                , preserveContents_(preserveContents)
            {
                if (width_ <= 0 || height_ <= 0)
                    throw std::invalid_argument(
                        "RLGL: RenderTarget2D dimensions must be positive");
                if (depthFormat_ < 0 || depthFormat_ > 3)
                    throw std::invalid_argument("RLGL: invalid DepthFormat ordinal");
                bytesPerTexel_ = Bridge::RenderTargetBytesPerTexel(surfaceFormat_);
                storage_ = Bridge::CreateRenderTarget2D(
                    width_, height_, levelCount_, depthFormat_,
                    multiSampleCount, surfaceFormat_);
            }

            ~RlglRenderTargetRenderer() override
            {
                Bridge::DestroyRenderTarget2D(storage_);
            }

            RlglRenderTargetRenderer(const RlglRenderTargetRenderer&) = delete;
            RlglRenderTargetRenderer& operator=(const RlglRenderTargetRenderer&) = delete;

            [[nodiscard]] int GetWidth() const override { return width_; }
            [[nodiscard]] int GetHeight() const override { return height_; }
            [[nodiscard]] int GetSurfaceFormatEXT() const noexcept override
            {
                return surfaceFormat_;
            }

            void UpdatePixels(const std::uint8_t* const data, const int stride) override
            {
                if (data == nullptr || stride != width_ * bytesPerTexel_)
                    throw std::invalid_argument("RLGL: invalid RenderTarget2D level-zero upload");
                const std::vector<std::uint8_t> flipped =
                    FlipRows(data, width_, height_, bytesPerTexel_);
                Bridge::UpdateTexture2D(
                    storage_.colorTexture, surfaceFormat_,
                    0, width_, height_, flipped.data());
            }

            void UpdatePixelsLevel(
                const int level, const std::uint8_t* const data,
                const int levelWidth, const int levelHeight) override
            {
                ValidateLevel(level, levelWidth, levelHeight);
                if (data == nullptr)
                    throw std::invalid_argument("RLGL: RenderTarget2D upload data is null");
                const std::vector<std::uint8_t> flipped =
                    FlipRows(data, levelWidth, levelHeight, bytesPerTexel_);
                Bridge::UpdateTexture2D(
                    storage_.colorTexture, surfaceFormat_,
                    level, levelWidth, levelHeight, flipped.data());
            }

            [[nodiscard]] bool HasDefinedMipLevel(const int level) const noexcept override
            {
                return level >= 0 && level < levelCount_;
            }

            [[nodiscard]] bool GetData(
                const int level, const int x, const int y,
                const int width, const int height,
                void* const data, const int dataLength) const override
            {
                if (level < 0 || level >= levelCount_)
                    throw std::out_of_range("RLGL: RenderTarget2D mip level is invalid");
                const int levelWidth = MipDimension(width_, level);
                const int levelHeight = MipDimension(height_, level);
                if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
                    x > levelWidth - width || y > levelHeight - height || data == nullptr)
                {
                    throw std::out_of_range(
                        "RLGL: RenderTarget2D readback rectangle is invalid");
                }
                const std::size_t required =
                    static_cast<std::size_t>(width) * height *
                    static_cast<std::size_t>(bytesPerTexel_);
                if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
                    throw std::out_of_range(
                        "RLGL: RenderTarget2D readback destination is too small");
                Bridge::ResolveRenderTarget2D(storage_, width_, height_);
                const unsigned int readFramebuffer = storage_.multiSampleCount > 0
                    ? storage_.resolveFramebuffer : storage_.framebuffer;
                Bridge::ReadRenderTarget2D(
                    readFramebuffer, storage_.colorTexture, level,
                    levelWidth, levelHeight, x, y, width, height,
                    static_cast<std::uint8_t*>(data), surfaceFormat_);
                return true;
            }

            void BindGL(const int unit) const override
            {
                Bridge::BindTexture2D(storage_.colorTexture, unit);
            }

            void BindAsRenderTarget() override
            {
                Bridge::BindFramebuffer(storage_.framebuffer);
            }

            void UnbindAsRenderTarget() override
            {
                Bridge::ResolveRenderTarget2D(storage_, width_, height_);
                if (levelCount_ > 1)
                {
                    Bridge::GenerateRenderTargetMipmaps(
                        storage_.colorTexture, width_, height_, levelCount_);
                }
                Bridge::BindDefaultFramebuffer();
            }

            [[nodiscard]] unsigned int GetColorGLHandle() const override
            {
                return storage_.colorTexture;
            }

            [[nodiscard]] int GetMultiSampleCount() const override
            {
                return storage_.multiSampleCount;
            }

            [[nodiscard]] bool HasRealDepthBuffer(
                const bool depthFormatWasRequested) const override
            {
                return depthFormatWasRequested && depthFormat_ != 0;
            }

            [[nodiscard]] bool HasRealStencilBuffer(
                const bool stencilFormatWasRequested) const override
            {
                return stencilFormatWasRequested && depthFormat_ == 3;
            }

            [[nodiscard]] int DepthBufferBitsEXT() const override
            {
                return depthFormat_ == 1 ? 16 : 24;
            }

            [[nodiscard]] unsigned int NativeTextureId() const noexcept override
            {
                return storage_.colorTexture;
            }

            [[nodiscard]] bool SampledRowsAreBottomUp() const noexcept override
            {
                return true;
            }

            [[nodiscard]] RenderTargetResourceSnapshot Snapshot() const noexcept
            {
                return {
                    storage_.framebuffer,
                    storage_.resolveFramebuffer,
                    storage_.colorTexture,
                    storage_.multisampleColorRenderbuffer,
                    storage_.depthStencilRenderbuffer,
                    width_, height_, depthFormat_, surfaceFormat_, levelCount_,
                    storage_.multiSampleCount, preserveContents_};
            }

        private:
            void ValidateLevel(
                const int level, const int levelWidth, const int levelHeight) const
            {
                if (level < 0 || level >= levelCount_ ||
                    levelWidth != MipDimension(width_, level) ||
                    levelHeight != MipDimension(height_, level))
                {
                    throw std::out_of_range("RLGL: RenderTarget2D mip level is invalid");
                }
            }

            Bridge::RenderTargetStorage storage_{};
            int width_ = 0;
            int height_ = 0;
            int depthFormat_ = 0;
            int surfaceFormat_ = 0;
            int levelCount_ = 1;
            int bytesPerTexel_ = 4;
            bool preserveContents_ = false;
        };
    }

    std::unique_ptr<IRenderTargetRenderer> CreateRenderTargetRenderer(
        const int width, const int height, const int depthFormat,
        const bool preserveContents, const bool mipMap,
        const int multiSampleCount, const int surfaceFormat)
    {
        return std::make_unique<RlglRenderTargetRenderer>(
            width, height, depthFormat, preserveContents,
            mipMap, multiSampleCount, surfaceFormat);
    }

    RenderTargetResourceSnapshot GetRenderTargetResourceSnapshotForTesting(
        const IRenderTargetRenderer& resource)
    {
        const auto* const target = dynamic_cast<const RlglRenderTargetRenderer*>(&resource);
        if (target == nullptr)
            throw std::invalid_argument("RLGL: render target belongs to another renderer");
        return target->Snapshot();
    }
}
