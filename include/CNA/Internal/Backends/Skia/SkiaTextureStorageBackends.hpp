#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourceCounters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Backends::Skia
{
    /** Maximum CPU memory committed by one emulated cube or volume texture. */
    inline constexpr std::size_t kSkiaCpuTextureStorageLimitBytes = 256u * 1024u * 1024u;

    /** Maximum accepted edge/axis, matching GraphicsDevice's existing texture ceiling. */
    inline constexpr int kSkiaCpuTextureMaximumAxis = 16384;

    /** Six-face, per-mip RGBA8 storage. It deliberately provides no shader-sampling binding. */
    class SkiaTextureCubeBackend final : public ITextureCubeBackend
    {
    public:
        SkiaTextureCubeBackend(int size, bool mipMap,
                               std::shared_ptr<SkiaResourceCounters> resourceCounters = {});
        ~SkiaTextureCubeBackend() override;

        SkiaTextureCubeBackend(const SkiaTextureCubeBackend&) = delete;
        SkiaTextureCubeBackend& operator=(const SkiaTextureCubeBackend&) = delete;
        SkiaTextureCubeBackend(SkiaTextureCubeBackend&&) = delete;
        SkiaTextureCubeBackend& operator=(SkiaTextureCubeBackend&&) = delete;

        [[nodiscard]] bool SetData(int face, int level, int x, int y, int width, int height,
                                   const void* data, int dataLength) override;
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int width, int height,
                                   void* data, int dataLength) const override;

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
    class SkiaTexture3DBackend final : public ITexture3DBackend
    {
    public:
        SkiaTexture3DBackend(int width, int height, int depth, bool mipMap,
                             std::shared_ptr<SkiaResourceCounters> resourceCounters = {});
        ~SkiaTexture3DBackend() override;

        SkiaTexture3DBackend(const SkiaTexture3DBackend&) = delete;
        SkiaTexture3DBackend& operator=(const SkiaTexture3DBackend&) = delete;
        SkiaTexture3DBackend(SkiaTexture3DBackend&&) = delete;
        SkiaTexture3DBackend& operator=(SkiaTexture3DBackend&&) = delete;

        [[nodiscard]] bool SetData(int level, int x, int y, int z,
                                   int width, int height, int depth,
                                   const void* data, int dataLength) override;
        [[nodiscard]] bool GetData(int level, int x, int y, int z,
                                   int width, int height, int depth,
                                   void* data, int dataLength) const override;

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
} // namespace CNA::Internal::Backends::Skia
