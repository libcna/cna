#pragma once

#include "CNA/Internal/Backends/Skia/SkiaResourceCounters.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Backends::Skia
{
    /** Immutable byte layout for one CNA-owned 2D mip level. */
    struct SkiaMipLevel2D final
    {
        int width = 0;
        int height = 0;
        std::size_t rowBytes = 0;
        std::size_t offset = 0;
        std::size_t bytes = 0;

        [[nodiscard]] bool operator==(const SkiaMipLevel2D&) const = default;
    };

    enum class SkiaMipChain2DLayoutError
    {
        None,
        InvalidDimensions,
        InvalidBytesPerTexel,
        SizeOverflow,
        BudgetExceeded,
    };

    /**
     * Describes a level-0-only image or complete floor-halved chain without allocating texels.
     * On failure, levels and storageBytes retain their caller-provided values.
     */
    [[nodiscard]] bool TryBuildSkiaMipChain2DLayout(
        int width, int height, bool mipMap, std::size_t bytesPerTexel,
        std::vector<SkiaMipLevel2D>& levels, std::size_t& storageBytes,
        SkiaMipChain2DLayoutError& error);

    /**
     * Stable, contiguous, zero-initialized CPU storage for a checked 2D mip chain.
     * Descriptors and byte addresses never move during the object's lifetime.
     */
    class SkiaMipChain2D final
    {
    public:
        SkiaMipChain2D(int width, int height, bool mipMap, std::size_t bytesPerTexel,
                       std::shared_ptr<SkiaResourceCounters> resourceCounters = {});
        ~SkiaMipChain2D();

        SkiaMipChain2D(const SkiaMipChain2D&) = delete;
        SkiaMipChain2D& operator=(const SkiaMipChain2D&) = delete;
        SkiaMipChain2D(SkiaMipChain2D&&) = delete;
        SkiaMipChain2D& operator=(SkiaMipChain2D&&) = delete;

        [[nodiscard]] int LevelCount() const noexcept
        {
            return static_cast<int>(levels_.size());
        }
        [[nodiscard]] std::size_t StorageBytes() const noexcept { return storage_.size(); }
        [[nodiscard]] const SkiaMipLevel2D& Level(int level) const;
        [[nodiscard]] std::uint8_t* LevelData(int level);
        [[nodiscard]] const std::uint8_t* LevelData(int level) const;
        [[nodiscard]] std::uint8_t* StorageData() noexcept { return storage_.data(); }
        [[nodiscard]] const std::uint8_t* StorageData() const noexcept { return storage_.data(); }

    private:
        std::vector<SkiaMipLevel2D> levels_;
        std::vector<std::uint8_t> storage_;
        std::shared_ptr<SkiaResourceCounters> resourceCounters_;
        bool resourceRegistered_ = false;
    };

    /**
     * Replaces one RGBA8 level with the deterministic area-box reduction of its direct parent.
     * Odd and NPOT source axes are partitioned completely; every source texel contributes once.
     */
    void GenerateSkiaRgba8MipLevel(SkiaMipChain2D& chain, int level);
} // namespace CNA::Internal::Backends::Skia
