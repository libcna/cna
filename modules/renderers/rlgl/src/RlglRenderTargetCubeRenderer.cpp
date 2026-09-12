// SPDX-License-Identifier: MS-PL

#include "RlglResources.hpp"

#include "RlglBridge.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace CNA::Internal::Renderers::Rlgl
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

        [[nodiscard]] int CalculateMipLevels(int size)
        {
            int levels = 1;
            while (size > 1)
            {
                size = std::max(1, size / 2);
                ++levels;
            }
            return levels;
        }

        [[nodiscard]] int MipDimension(const int base, const int level)
        {
            return std::max(1, base >> level);
        }

        class RlglRenderTargetCubeRenderer final
            : public IRenderTargetCubeRenderer, public IRlglTextureResource
        {
        public:
            RlglRenderTargetCubeRenderer(
                const int size, const int depthFormat, const bool preserveContents,
                const bool mipMap, const int multiSampleCount, const int surfaceFormat)
                : size_(size)
                , depthFormat_(depthFormat)
                , surfaceFormat_(surfaceFormat)
                , levelCount_(mipMap ? CalculateMipLevels(size) : 1)
                , preserveContents_(preserveContents)
            {
                if (size_ <= 0)
                    throw std::invalid_argument(
                        "RLGL: RenderTargetCube size must be positive");
                if (depthFormat_ < 0 || depthFormat_ > 3)
                    throw std::invalid_argument("RLGL: invalid DepthFormat ordinal");
                (void)Bridge::RenderTargetBytesPerTexel(surfaceFormat_);
                storage_ = Bridge::CreateRenderTargetCube(
                    size_, levelCount_, depthFormat_, multiSampleCount, surfaceFormat_);
            }

            ~RlglRenderTargetCubeRenderer() override
            {
                Bridge::DestroyRenderTargetCube(storage_);
            }

            RlglRenderTargetCubeRenderer(const RlglRenderTargetCubeRenderer&) = delete;
            RlglRenderTargetCubeRenderer& operator=(
                const RlglRenderTargetCubeRenderer&) = delete;

            [[nodiscard]] int GetSize() const override { return size_; }

            void BindAsRenderTargetFace(const int face) override
            {
                ValidateFace(face);
                activeFace_ = face;
                Bridge::BindRenderTargetCubeFace(storage_, face);
            }

            void UnbindAsRenderTarget() override
            {
                FinalizeFace(activeFace_);
                Bridge::BindDefaultFramebuffer();
            }

            [[nodiscard]] unsigned int GetGLHandle() const override
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

            void BindGL(const int unit) const override
            {
                Bridge::BindTextureCube(storage_.colorTexture, unit);
            }

            [[nodiscard]] bool SetData(
                const int face, const int level, const int x, const int y,
                const int width, const int height,
                const void* const data, const int dataLength) override
            {
                if (static_cast<SurfaceFormat>(surfaceFormat_) != SurfaceFormat::Color)
                    return false;
                try
                {
                    (void)ValidateRegion(face, level, x, y, width, height, data);
                    ValidateByteCount(width, height, 4, dataLength);
                }
                catch (const std::out_of_range&)
                {
                    return false;
                }
                Bridge::UpdateTextureCubeColor(
                    storage_.colorTexture, face, level, x, y, width, height,
                    static_cast<const std::uint8_t*>(data));
                return true;
            }

            [[nodiscard]] bool GetData(
                const int face, const int level, const int x, const int y,
                const int width, const int height,
                void* const data, const int dataLength) const override
            {
                if (static_cast<SurfaceFormat>(surfaceFormat_) != SurfaceFormat::Color)
                    return false;
                int levelSize = 0;
                try
                {
                    levelSize = ValidateRegion(
                        face, level, x, y, width, height, data);
                    ValidateByteCount(width, height, 4, dataLength);
                }
                catch (const std::out_of_range&)
                {
                    return false;
                }
                Bridge::ResolveRenderTargetCubeFace(storage_, face, size_);
                if (level > 0 && levelCount_ > 1)
                {
                    Bridge::GenerateRenderTargetCubeMipmaps(
                        storage_.colorTexture, size_, levelCount_);
                }
                Bridge::ReadRenderTargetCube(
                    storage_.colorTexture, face, level, levelSize,
                    x, y, width, height,
                    static_cast<std::uint8_t*>(data), surfaceFormat_);
                return true;
            }

            [[nodiscard]] unsigned int NativeTextureId() const noexcept override
            {
                return storage_.colorTexture;
            }

            [[nodiscard]] bool SampledRowsAreBottomUp() const noexcept override
            {
                return true;
            }

            void FinalizeFace(const int face)
            {
                ValidateFace(face);
                Bridge::ResolveRenderTargetCubeFace(storage_, face, size_);
                if (levelCount_ > 1)
                {
                    Bridge::GenerateRenderTargetCubeMipmaps(
                        storage_.colorTexture, size_, levelCount_);
                }
            }

            [[nodiscard]] RenderTargetCubeResourceSnapshot Snapshot() const noexcept
            {
                return {
                    storage_.framebuffer,
                    storage_.resolveFramebuffer,
                    storage_.colorTexture,
                    storage_.multisampleColorRenderbuffers,
                    storage_.depthStencilRenderbuffer,
                    size_, depthFormat_, surfaceFormat_, levelCount_,
                    storage_.multiSampleCount, activeFace_, preserveContents_};
            }

        private:
            static void ValidateFace(const int face)
            {
                if (face < 0 || face >= 6)
                    throw std::out_of_range("RLGL: RenderTargetCube face is invalid");
            }

            [[nodiscard]] int ValidateRegion(
                const int face, const int level, const int x, const int y,
                const int width, const int height, const void* const data) const
            {
                ValidateFace(face);
                if (level < 0 || level >= levelCount_)
                    throw std::out_of_range(
                        "RLGL: RenderTargetCube mip level is invalid");
                const int levelSize = MipDimension(size_, level);
                if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
                    x > levelSize - width || y > levelSize - height || data == nullptr)
                {
                    throw std::out_of_range(
                        "RLGL: RenderTargetCube transfer rectangle is invalid");
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
                {
                    throw std::out_of_range(
                        "RLGL: RenderTargetCube transfer buffer is too small");
                }
            }

            Bridge::RenderTargetCubeStorage storage_{};
            int size_ = 0;
            int depthFormat_ = 0;
            int surfaceFormat_ = 0;
            int levelCount_ = 1;
            int activeFace_ = 0;
            bool preserveContents_ = false;
        };
    }

    std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCubeRenderer(
        const int size, const int depthFormat, const bool preserveContents,
        const bool mipMap, const int multiSampleCount, const int surfaceFormat)
    {
        return std::make_unique<RlglRenderTargetCubeRenderer>(
            size, depthFormat, preserveContents,
            mipMap, multiSampleCount, surfaceFormat);
    }

    RenderTargetCubeResourceSnapshot GetRenderTargetCubeResourceSnapshotForTesting(
        const IRenderTargetCubeRenderer& resource)
    {
        const auto* const target =
            dynamic_cast<const RlglRenderTargetCubeRenderer*>(&resource);
        if (target == nullptr)
            throw std::invalid_argument(
                "RLGL: cube render target belongs to another renderer");
        return target->Snapshot();
    }

    void FinalizeRenderTargetCubeFace(
        IRenderTargetCubeRenderer& resource, const int face)
    {
        auto* const target = dynamic_cast<RlglRenderTargetCubeRenderer*>(&resource);
        if (target == nullptr)
            throw std::invalid_argument(
                "RLGL: cube render target belongs to another renderer");
        target->FinalizeFace(face);
    }
}
