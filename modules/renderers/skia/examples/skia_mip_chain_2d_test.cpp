// SPDX-License-Identifier: MS-PL
// SKIA-125: checked, stable, CNA-owned 2D mip-chain layout and storage.

#include "CNA/Internal/Renderers/Skia/SkiaMipChain2D.hpp"

#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace CNA::Internal::Renderers::Skia;

namespace
{
    int failures = 0;

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass) ++failures;
    }

    bool SameLevel(const SkiaMipLevel2D& level, int width, int height,
                   std::size_t rowBytes, std::size_t offset, std::size_t bytes)
    {
        return level.width == width && level.height == height
            && level.rowBytes == rowBytes && level.offset == offset && level.bytes == bytes;
    }
}

int main()
{
    auto counters = std::make_shared<SkiaResourceCounters>();
    {
        SkiaMipChain2D chain(7, 5, true, 4u, counters);
        Check(chain.LevelCount() == 3
                  && SameLevel(chain.Level(0), 7, 5, 28u, 0u, 140u)
                  && SameLevel(chain.Level(1), 3, 2, 12u, 140u, 24u)
                  && SameLevel(chain.Level(2), 1, 1, 4u, 164u, 4u)
                  && chain.StorageBytes() == 168u,
              "odd NPOT dimensions floor-halve through stable offsets to 1x1");
        Check(std::all_of(chain.StorageData(), chain.StorageData() + chain.StorageBytes(),
                          [](std::uint8_t value) { return value == 0u; }),
              "every allocated level is deterministically zero initialized");

        std::uint8_t* const base = chain.StorageData();
        chain.LevelData(1)[0] = 0xA5u;
        Check(chain.LevelData(0) == base
                  && chain.LevelData(1) == base + 140u
                  && chain.LevelData(2) == base + 164u
                  && chain.LevelData(1)[0] == 0xA5u
                  && chain.LevelData(0)[0] == 0u
                  && chain.LevelData(2)[0] == 0u,
              "one contiguous allocation keeps level addresses stable and levels isolated");

        const SkiaResourceStats live = counters->GetStats();
        Check(live.mipChains2D == 1u && live.mipChain2DStorageBytes == 168u,
              "live accounting publishes the exact complete-chain allocation once");
    }
    Check(counters->GetStats().mipChains2D == 0u
              && counters->GetStats().mipChain2DStorageBytes == 0u,
          "RAII destruction returns 2D mip-chain counters to baseline");

    SkiaMipChain2D vertical(1, 9, true, 1u);
    SkiaMipChain2D horizontal(9, 1, true, 1u);
    Check(vertical.LevelCount() == 4
              && vertical.Level(0).height == 9 && vertical.Level(1).height == 4
              && vertical.Level(2).height == 2 && vertical.Level(3).height == 1
              && horizontal.LevelCount() == 4
              && horizontal.Level(0).width == 9 && horizontal.Level(1).width == 4
              && horizontal.Level(2).width == 2 && horizontal.Level(3).width == 1,
          "one-dimensional odd chains retain the unit axis and terminate exactly at 1x1");

    SkiaMipChain2D levelZeroOnly(7, 5, false, 4u);
    Check(levelZeroOnly.LevelCount() == 1 && levelZeroOnly.StorageBytes() == 140u,
          "mipMap=false allocates only the checked level-zero layout");

    std::vector<SkiaMipLevel2D> layout{{99, 99, 99u, 99u, 99u}};
    std::size_t describedBytes = 0xBEEFu;
    SkiaMipChain2DLayoutError layoutError = SkiaMipChain2DLayoutError::None;
    Check(TryBuildSkiaMipChain2DLayout(16384, 16384, false, 1u,
                                       layout, describedBytes, layoutError)
              && layoutError == SkiaMipChain2DLayoutError::None
              && layout.size() == 1u
              && describedBytes == 256u * 1024u * 1024u,
          "layout accepts the exact 256-MiB boundary without allocating texels");

    const auto preservedLayout = layout;
    const std::size_t preservedBytes = describedBytes;
    Check(!TryBuildSkiaMipChain2DLayout(16384, 16384, true, 1u,
                                        layout, describedBytes, layoutError)
              && layoutError == SkiaMipChain2DLayoutError::BudgetExceeded
              && layout == preservedLayout && describedBytes == preservedBytes,
          "a descendant crossing the byte ceiling rejects without publishing partial layout");

    Check(!TryBuildSkiaMipChain2DLayout(2, 2, true,
                                        std::numeric_limits<std::size_t>::max(),
                                        layout, describedBytes, layoutError)
              && layoutError == SkiaMipChain2DLayoutError::SizeOverflow
              && layout == preservedLayout && describedBytes == preservedBytes,
          "row-byte overflow rejects without changing caller layout or accounting");
    Check(!TryBuildSkiaMipChain2DLayout(0, 2, true, 4u,
                                        layout, describedBytes, layoutError)
              && layoutError == SkiaMipChain2DLayoutError::InvalidDimensions
              && !TryBuildSkiaMipChain2DLayout(2, 2, true, 0u,
                                               layout, describedBytes, layoutError)
              && layoutError == SkiaMipChain2DLayoutError::InvalidBytesPerTexel,
          "invalid axes and zero texel width have distinct deterministic dispositions");

    const SkiaResourceStats beforeFailure = counters->GetStats();
    bool budgetRejected = false;
    try
    {
        SkiaMipChain2D overBudget(16384, 16384, true, 1u, counters);
        (void)overBudget;
    }
    catch (const System::NotSupportedException&)
    {
        budgetRejected = true;
    }
    Check(budgetRejected
              && counters->GetStats().mipChains2D == beforeFailure.mipChains2D
              && counters->GetStats().mipChain2DStorageBytes
                    == beforeFailure.mipChain2DStorageBytes,
          "failed construction leaves live object and byte counters unchanged");

    bool lowRejected = false;
    bool highRejected = false;
    try { SkiaMipChain2D invalid(0, 1, true, 4u); }
    catch (const std::invalid_argument&) { lowRejected = true; }
    try { SkiaMipChain2D invalid(16385, 1, true, 4u); }
    catch (const std::invalid_argument&) { highRejected = true; }
    Check(lowRejected && highRejected,
          "constructor rejects dimensions outside the shared 1..16384 axis policy");

    bool levelRejected = false;
    try { (void)levelZeroOnly.Level(1); }
    catch (const std::out_of_range&) { levelRejected = true; }
    Check(levelRejected, "out-of-range level access fails instead of aliasing another level");

    return failures == 0 ? 0 : 1;
}
