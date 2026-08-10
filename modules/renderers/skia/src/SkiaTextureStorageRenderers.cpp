#include "CNA/Internal/Renderers/Skia/SkiaTextureStorageRenderers.hpp"

#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Internal::Renderers::Skia
{
    namespace
    {
        [[nodiscard]] std::size_t RgbaBytes2D(int width, int height)
        {
            std::size_t bytes = 0;
            if (!CheckedTexelBytes2D(static_cast<std::size_t>(width),
                                     static_cast<std::size_t>(height), 4u, bytes))
            {
                throw System::NotSupportedException(
                    "Skia CPU texture storage size overflows the host address space.");
            }
            return bytes;
        }

        [[nodiscard]] std::size_t RgbaBytes3D(int width, int height, int depth)
        {
            std::size_t bytes = 0;
            if (!CheckedTexelBytes3D(static_cast<std::size_t>(width),
                                     static_cast<std::size_t>(height),
                                     static_cast<std::size_t>(depth), 4u, bytes))
            {
                throw System::NotSupportedException(
                    "Skia CPU volume texture storage size overflows the host address space.");
            }
            return bytes;
        }

        void ValidateAxis(int value, const char* resource, const char* axis)
        {
            if (value <= 0)
            {
                throw std::invalid_argument(
                    std::string("Skia ") + resource + " " + axis + " must be positive.");
            }
            if (value > kSkiaCpuTextureMaximumAxis)
            {
                throw System::NotSupportedException(
                    std::string("Skia ") + resource + " " + axis + " exceeds the "
                    + std::to_string(kSkiaCpuTextureMaximumAxis) + " texel axis limit.");
            }
        }

        void AddToBudget(std::size_t bytes, std::size_t& total, const char* resource)
        {
            std::size_t next = 0;
            if (!CheckedSkiaResourceAccumulate(total, bytes, next))
            {
                throw System::NotSupportedException(
                    std::string("Skia ") + resource + " exceeds the 256 MiB per-resource CPU "
                    "storage limit.");
            }
            total = next;
        }

        [[nodiscard]] bool ValidRegion2D(int extentWidth, int extentHeight,
                                         int x, int y, int width, int height) noexcept
        {
            return width > 0 && height > 0 && x >= 0 && y >= 0
                && width <= extentWidth && height <= extentHeight
                && x <= extentWidth - width && y <= extentHeight - height;
        }

        [[nodiscard]] bool ValidRegion3D(int extentWidth, int extentHeight, int extentDepth,
                                         int x, int y, int z,
                                         int width, int height, int depth) noexcept
        {
            return depth > 0 && z >= 0 && depth <= extentDepth && z <= extentDepth - depth
                && ValidRegion2D(extentWidth, extentHeight, x, y, width, height);
        }

        [[nodiscard]] bool HasTransferBytes(int dataLength, std::size_t required) noexcept
        {
            return dataLength >= 0 && static_cast<std::size_t>(dataLength) >= required;
        }
    }

    SkiaTextureCubeRenderer::SkiaTextureCubeRenderer(
        int size, bool mipMap, std::shared_ptr<SkiaResourceCounters> resourceCounters)
        : resourceCounters_(std::move(resourceCounters))
    {
        ValidateAxis(size, "TextureCube", "face size");

        int dimension = size;
        std::vector<std::pair<int, std::size_t>> layout;
        do
        {
            const std::size_t faceBytes = RgbaBytes2D(dimension, dimension);
            std::size_t levelBytes = 0;
            if (!CheckedSizeMultiply(faceBytes, 6u, levelBytes))
            {
                throw System::NotSupportedException(
                    "Skia TextureCube storage size overflows the host address space.");
            }
            AddToBudget(levelBytes, storageBytes_, "TextureCube");
            layout.emplace_back(dimension, faceBytes);
            dimension = std::max(1, dimension / 2);
        }
        while (mipMap && layout.back().first > 1);

        levels_.reserve(layout.size());
        for (const auto& [levelDimension, faceBytes] : layout)
        {
            Level level;
            level.dimension = levelDimension;
            for (auto& face : level.faces)
                face.assign(faceBytes, 0u);
            levels_.push_back(std::move(level));
        }

        if (resourceCounters_)
        {
            resourceCounters_->AddCpuTextureCube(storageBytes_);
            resourceRegistered_ = true;
        }
    }

    SkiaTextureCubeRenderer::~SkiaTextureCubeRenderer()
    {
        if (resourceRegistered_)
            resourceCounters_->RemoveCpuTextureCube(storageBytes_);
    }

    bool SkiaTextureCubeRenderer::SetData(int face, int level, int x, int y,
                                         int width, int height,
                                         const void* data, int dataLength)
    {
        if (!data || face < 0 || face >= 6 || level < 0
            || level >= static_cast<int>(levels_.size()))
        {
            return false;
        }
        Level& targetLevel = levels_[static_cast<std::size_t>(level)];
        if (!ValidRegion2D(targetLevel.dimension, targetLevel.dimension,
                           x, y, width, height))
        {
            return false;
        }
        const std::size_t rowBytes = RgbaBytes2D(width, 1);
        const std::size_t requiredBytes = RgbaBytes2D(width, height);
        if (!HasTransferBytes(dataLength, requiredBytes))
            return false;

        const auto* source = static_cast<const std::uint8_t*>(data);
        auto& target = targetLevel.faces[static_cast<std::size_t>(face)];
        for (int row = 0; row < height; ++row)
        {
            const std::size_t targetOffset =
                (static_cast<std::size_t>(y + row) * targetLevel.dimension + x) * 4u;
            std::memcpy(target.data() + targetOffset,
                        source + static_cast<std::size_t>(row) * rowBytes, rowBytes);
        }
        return true;
    }

    bool SkiaTextureCubeRenderer::GetData(int face, int level, int x, int y,
                                         int width, int height,
                                         void* data, int dataLength) const
    {
        if (!data || face < 0 || face >= 6 || level < 0
            || level >= static_cast<int>(levels_.size()))
        {
            return false;
        }
        const Level& sourceLevel = levels_[static_cast<std::size_t>(level)];
        if (!ValidRegion2D(sourceLevel.dimension, sourceLevel.dimension,
                           x, y, width, height))
        {
            return false;
        }
        const std::size_t rowBytes = RgbaBytes2D(width, 1);
        const std::size_t requiredBytes = RgbaBytes2D(width, height);
        if (!HasTransferBytes(dataLength, requiredBytes))
            return false;

        auto* target = static_cast<std::uint8_t*>(data);
        const auto& source = sourceLevel.faces[static_cast<std::size_t>(face)];
        for (int row = 0; row < height; ++row)
        {
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(y + row) * sourceLevel.dimension + x) * 4u;
            std::memcpy(target + static_cast<std::size_t>(row) * rowBytes,
                        source.data() + sourceOffset, rowBytes);
        }
        return true;
    }

    SkiaTexture3DRenderer::SkiaTexture3DRenderer(
        int width, int height, int depth, bool mipMap,
        std::shared_ptr<SkiaResourceCounters> resourceCounters)
        : resourceCounters_(std::move(resourceCounters))
    {
        ValidateAxis(width, "Texture3D", "width");
        ValidateAxis(height, "Texture3D", "height");
        ValidateAxis(depth, "Texture3D", "depth");

        int levelWidth = width;
        int levelHeight = height;
        int levelDepth = depth;
        struct LevelLayout final
        {
            int width;
            int height;
            int depth;
            std::size_t bytes;
        };
        std::vector<LevelLayout> layout;
        do
        {
            const std::size_t levelBytes = RgbaBytes3D(levelWidth, levelHeight, levelDepth);
            AddToBudget(levelBytes, storageBytes_, "Texture3D");
            layout.push_back({levelWidth, levelHeight, levelDepth, levelBytes});
            levelWidth = std::max(1, levelWidth / 2);
            levelHeight = std::max(1, levelHeight / 2);
            levelDepth = std::max(1, levelDepth / 2);
        }
        while (mipMap && (layout.back().width > 1 || layout.back().height > 1));

        levels_.reserve(layout.size());
        for (const LevelLayout& levelLayout : layout)
        {
            Level level;
            level.width = levelLayout.width;
            level.height = levelLayout.height;
            level.depth = levelLayout.depth;
            level.voxels.assign(levelLayout.bytes, 0u);
            levels_.push_back(std::move(level));
        }

        if (resourceCounters_)
        {
            resourceCounters_->AddCpuTexture3D(storageBytes_);
            resourceRegistered_ = true;
        }
    }

    SkiaTexture3DRenderer::~SkiaTexture3DRenderer()
    {
        if (resourceRegistered_)
            resourceCounters_->RemoveCpuTexture3D(storageBytes_);
    }

    bool SkiaTexture3DRenderer::SetData(int level, int x, int y, int z,
                                       int width, int height, int depth,
                                       const void* data, int dataLength)
    {
        if (!data || level < 0 || level >= static_cast<int>(levels_.size()))
            return false;
        Level& targetLevel = levels_[static_cast<std::size_t>(level)];
        if (!ValidRegion3D(targetLevel.width, targetLevel.height, targetLevel.depth,
                           x, y, z, width, height, depth))
            return false;
        const std::size_t rowBytes = RgbaBytes2D(width, 1);
        const std::size_t sliceBytes = RgbaBytes2D(width, height);
        const std::size_t requiredBytes = RgbaBytes3D(width, height, depth);
        if (!HasTransferBytes(dataLength, requiredBytes))
            return false;

        const auto* source = static_cast<const std::uint8_t*>(data);
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < height; ++row)
            {
                const std::size_t targetOffset =
                    ((static_cast<std::size_t>(z + slice) * targetLevel.height + y + row)
                     * targetLevel.width + x) * 4u;
                const std::size_t sourceOffset =
                    static_cast<std::size_t>(slice) * sliceBytes
                    + static_cast<std::size_t>(row) * rowBytes;
                std::memcpy(targetLevel.voxels.data() + targetOffset,
                            source + sourceOffset, rowBytes);
            }
        }
        return true;
    }

    bool SkiaTexture3DRenderer::GetData(int level, int x, int y, int z,
                                       int width, int height, int depth,
                                       void* data, int dataLength) const
    {
        if (!data || level < 0 || level >= static_cast<int>(levels_.size()))
            return false;
        const Level& sourceLevel = levels_[static_cast<std::size_t>(level)];
        if (!ValidRegion3D(sourceLevel.width, sourceLevel.height, sourceLevel.depth,
                           x, y, z, width, height, depth))
            return false;
        const std::size_t rowBytes = RgbaBytes2D(width, 1);
        const std::size_t sliceBytes = RgbaBytes2D(width, height);
        const std::size_t requiredBytes = RgbaBytes3D(width, height, depth);
        if (!HasTransferBytes(dataLength, requiredBytes))
            return false;

        auto* target = static_cast<std::uint8_t*>(data);
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < height; ++row)
            {
                const std::size_t sourceOffset =
                    ((static_cast<std::size_t>(z + slice) * sourceLevel.height + y + row)
                     * sourceLevel.width + x) * 4u;
                const std::size_t targetOffset =
                    static_cast<std::size_t>(slice) * sliceBytes
                    + static_cast<std::size_t>(row) * rowBytes;
                std::memcpy(target + targetOffset,
                            sourceLevel.voxels.data() + sourceOffset, rowBytes);
            }
        }
        return true;
    }
} // namespace CNA::Internal::Renderers::Skia
