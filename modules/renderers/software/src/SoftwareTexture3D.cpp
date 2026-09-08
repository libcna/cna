// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace CNA::Internal::Renderers::Software
{
    namespace
    {
        int CalculateVolumeMipLevels(int width, int height)
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

        bool RequiredBytes(int width, int height, int depth, std::size_t& result)
        {
            if (width <= 0 || height <= 0 || depth <= 0) return false;
            const std::size_t w = static_cast<std::size_t>(width);
            const std::size_t h = static_cast<std::size_t>(height);
            const std::size_t d = static_cast<std::size_t>(depth);
            const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
            if (w > maximum / h || w * h > maximum / d || w * h * d > maximum / 4u)
                return false;
            result = w * h * d * 4u;
            return true;
        }
    }

    SoftwareTexture3DRenderer::SoftwareTexture3DRenderer(
        int width, int height, int depth, bool mipMap)
        : width_(width), height_(height), depth_(depth)
        , levelCount_(mipMap ? CalculateVolumeMipLevels(width, height) : 1)
    {
        if (width <= 0 || height <= 0 || depth <= 0)
            throw std::invalid_argument("SoftwareTexture3DRenderer: dimensions must be positive");

        levels_.resize(static_cast<std::size_t>(levelCount_));
        for (int level = 0; level < levelCount_; ++level)
        {
            std::size_t bytes = 0;
            if (!RequiredBytes(LevelWidth(level), LevelHeight(level), LevelDepth(level), bytes))
                throw std::length_error("SoftwareTexture3DRenderer: volume byte size overflows");
            levels_[static_cast<std::size_t>(level)].assign(bytes, 0u);
        }
    }

    int SoftwareTexture3DRenderer::LevelWidth(int level) const
    {
        return std::max(1, width_ >> level);
    }

    int SoftwareTexture3DRenderer::LevelHeight(int level) const
    {
        return std::max(1, height_ >> level);
    }

    int SoftwareTexture3DRenderer::LevelDepth(int level) const
    {
        return std::max(1, depth_ >> level);
    }

    bool SoftwareTexture3DRenderer::SetData(
        int level, int x, int y, int z, int w, int h, int depth,
        const void* data, int dataLength)
    {
        if (data == nullptr || level < 0 || level >= levelCount_ || dataLength < 0)
            return false;
        const int levelW = LevelWidth(level);
        const int levelH = LevelHeight(level);
        const int levelD = LevelDepth(level);
        if (w <= 0 || h <= 0 || depth <= 0 || x < 0 || y < 0 || z < 0 ||
            x > levelW - w || y > levelH - h || z > levelD - depth)
            return false;

        std::size_t required = 0;
        if (!RequiredBytes(w, h, depth, required) ||
            static_cast<std::size_t>(dataLength) < required)
            return false;

        const auto* source = static_cast<const std::uint8_t*>(data);
        std::vector<std::uint8_t>& destination = levels_[static_cast<std::size_t>(level)];
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        const std::size_t sourceSliceBytes = rowBytes * static_cast<std::size_t>(h);
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < h; ++row)
            {
                const std::size_t sourceOffset =
                    static_cast<std::size_t>(slice) * sourceSliceBytes +
                    static_cast<std::size_t>(row) * rowBytes;
                const std::size_t destinationOffset =
                    ((static_cast<std::size_t>(z + slice) * static_cast<std::size_t>(levelH) +
                      static_cast<std::size_t>(y + row)) * static_cast<std::size_t>(levelW) +
                     static_cast<std::size_t>(x)) * 4u;
                std::copy_n(source + sourceOffset, rowBytes,
                            destination.begin() + static_cast<std::ptrdiff_t>(destinationOffset));
            }
        }
        return true;
    }

    bool SoftwareTexture3DRenderer::GetData(
        int level, int x, int y, int z, int w, int h, int depth,
        void* data, int dataLength) const
    {
        if (data == nullptr || level < 0 || level >= levelCount_ || dataLength < 0)
            return false;
        const int levelW = LevelWidth(level);
        const int levelH = LevelHeight(level);
        const int levelD = LevelDepth(level);
        if (w <= 0 || h <= 0 || depth <= 0 || x < 0 || y < 0 || z < 0 ||
            x > levelW - w || y > levelH - h || z > levelD - depth)
            return false;

        std::size_t required = 0;
        if (!RequiredBytes(w, h, depth, required) ||
            static_cast<std::size_t>(dataLength) < required)
            return false;

        const std::vector<std::uint8_t>& source = levels_[static_cast<std::size_t>(level)];
        auto* destination = static_cast<std::uint8_t*>(data);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        const std::size_t destinationSliceBytes = rowBytes * static_cast<std::size_t>(h);
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < h; ++row)
            {
                const std::size_t sourceOffset =
                    ((static_cast<std::size_t>(z + slice) * static_cast<std::size_t>(levelH) +
                      static_cast<std::size_t>(y + row)) * static_cast<std::size_t>(levelW) +
                     static_cast<std::size_t>(x)) * 4u;
                const std::size_t destinationOffset =
                    static_cast<std::size_t>(slice) * destinationSliceBytes +
                    static_cast<std::size_t>(row) * rowBytes;
                std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(sourceOffset), rowBytes,
                            destination + destinationOffset);
            }
        }
        return true;
    }

    void SoftwareTexture3DRenderer::GetDimensionsEXT(
        int& width, int& height, int& depth) const noexcept
    {
        width = width_;
        height = height_;
        depth = depth_;
    }
}
