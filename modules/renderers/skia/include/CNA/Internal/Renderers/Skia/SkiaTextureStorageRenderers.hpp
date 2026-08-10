#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaResourceCounters.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaResourcePolicy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::Skia
{
    /** Six-face, per-mip RGBA8 storage. It deliberately provides no shader-sampling binding. */
    class SkiaTextureCubeRenderer final : public ITextureCubeRenderer
    {
    public:
        SkiaTextureCubeRenderer(int size, bool mipMap,
                               std::shared_ptr<SkiaResourceCounters> resourceCounters = {});
        ~SkiaTextureCubeRenderer() override;

        SkiaTextureCubeRenderer(const SkiaTextureCubeRenderer&) = delete;
        SkiaTextureCubeRenderer& operator=(const SkiaTextureCubeRenderer&) = delete;
        SkiaTextureCubeRenderer(SkiaTextureCubeRenderer&&) = delete;
        SkiaTextureCubeRenderer& operator=(SkiaTextureCubeRenderer&&) = delete;

        [[nodiscard]] bool SetData(int face, int level, int x, int y, int width, int height,
                                   const void* data, int dataLength) override;
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int width, int height,
                                   void* data, int dataLength) const override;
        [[nodiscard]] int GetSizeEXT() const noexcept override
        {
            return levels_.empty() ? 0 : levels_.front().dimension;
        }

        NOXNA [[nodiscard]] std::size_t StorageBytesEXT() const noexcept { return storageBytes_; }
        NOXNA [[nodiscard]] int LevelCountEXT() const noexcept
        {
            return static_cast<int>(levels_.size());
        }

    private:
        struct Level final
        {
            int dimension = 0;
            std::array<std::vector<std::uint8_t>, 6> faces;
        };

        std::vector<Level> levels_;
        std::size_t storageBytes_ = 0;
        std::shared_ptr<SkiaResourceCounters> resourceCounters_;
        bool resourceRegistered_ = false;
    };

    /** Per-mip RGBA8 voxel storage. It deliberately provides no shader-sampling binding. */
    class SkiaTexture3DRenderer final : public ITexture3DRenderer
    {
    public:
        SkiaTexture3DRenderer(int width, int height, int depth, bool mipMap,
                             std::shared_ptr<SkiaResourceCounters> resourceCounters = {});
        ~SkiaTexture3DRenderer() override;

        SkiaTexture3DRenderer(const SkiaTexture3DRenderer&) = delete;
        SkiaTexture3DRenderer& operator=(const SkiaTexture3DRenderer&) = delete;
        SkiaTexture3DRenderer(SkiaTexture3DRenderer&&) = delete;
        SkiaTexture3DRenderer& operator=(SkiaTexture3DRenderer&&) = delete;

        [[nodiscard]] bool SetData(int level, int x, int y, int z,
                                   int width, int height, int depth,
                                   const void* data, int dataLength) override;
        [[nodiscard]] bool GetData(int level, int x, int y, int z,
                                   int width, int height, int depth,
                                   void* data, int dataLength) const override;
        void GetDimensionsEXT(int& width, int& height, int& depth) const noexcept override
        {
            if (levels_.empty()) { width = height = depth = 0; return; }
            width = levels_.front().width;
            height = levels_.front().height;
            depth = levels_.front().depth;
        }

        NOXNA [[nodiscard]] std::size_t StorageBytesEXT() const noexcept { return storageBytes_; }
        NOXNA [[nodiscard]] int LevelCountEXT() const noexcept
        {
            return static_cast<int>(levels_.size());
        }

    private:
        struct Level final
        {
            int width = 0;
            int height = 0;
            int depth = 0;
            std::vector<std::uint8_t> voxels;
        };

        std::vector<Level> levels_;
        std::size_t storageBytes_ = 0;
        std::shared_ptr<SkiaResourceCounters> resourceCounters_;
        bool resourceRegistered_ = false;
    };
} // namespace CNA::Internal::Renderers::Skia
