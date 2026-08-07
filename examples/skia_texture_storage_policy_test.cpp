// SPDX-License-Identifier: MS-PL
// SKIA-80/SKIA-82: direct proof of bounded, checked CPU cube/volume storage.

#include "CNA/Internal/Backends/Skia/SkiaTextureStorageBackends.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <vector>

using namespace CNA::Internal::Backends::Skia;

namespace
{
    int failures = 0;

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass) ++failures;
    }

    template<typename Callable>
    bool Throws(Callable&& callable)
    {
        try
        {
            callable();
        }
        catch (const std::exception&)
        {
            return true;
        }
        return false;
    }
}

int main()
{
    Check(kSkiaCpuTextureStorageLimitBytes == 256u * 1024u * 1024u,
          "CPU storage has an explicit 256 MiB per-resource ceiling");
    Check(kSkiaCpuTextureMaximumAxis == 16384,
          "CPU storage axis ceiling matches GraphicsDevice texture policy");

    auto counters = std::make_shared<SkiaResourceCounters>();
    {
        SkiaTextureCubeBackend cube(8, true, counters);
        SkiaTexture3DBackend volume(4, 3, 5, true, counters);
        Check(cube.LevelCountEXT() == 4 && cube.StorageBytesEXT() == 2040,
              "8x8 cube allocates six isolated zeroed faces across four exact mip levels");
        Check(volume.LevelCountEXT() == 3 && volume.StorageBytesEXT() == 260,
              "4x3x5 volume follows the public width/height-driven three-level mip contract");

        const SkiaResourceStats live = counters->GetStats();
        Check(live.cpuTextureCubeBackends == 1 && live.cpuTexture3DBackends == 1
                  && live.cpuTextureStorageBytes == 2300,
              "debug accounting reports exact live cube and volume storage bytes");

        std::array<std::uint8_t, 4> zeroProbe{0xCD, 0xCD, 0xCD, 0xCD};
        Check(cube.GetData(5, 3, 0, 0, 1, 1, zeroProbe.data(), 4)
                  && std::all_of(zeroProbe.begin(), zeroProbe.end(),
                                 [](std::uint8_t value) { return value == 0; }),
              "new cube faces and mip levels are deterministically zero initialized");

        std::array<std::uint8_t, 16> cubeSource{};
        for (std::size_t i = 0; i < cubeSource.size(); ++i)
            cubeSource[i] = static_cast<std::uint8_t>(i + 1);
        Check(cube.SetData(2, 1, 1, 2, 2, 2, cubeSource.data(), 16),
              "cube accepts one tightly packed partial face rectangle");
        std::array<std::uint8_t, 16> cubeRead{};
        Check(cube.GetData(2, 1, 1, 2, 2, 2, cubeRead.data(), 16)
                  && cubeRead == cubeSource,
              "cube partial rectangle reads back byte-exact and top-row-first");
        Check(!cube.SetData(2, 1, 1, 2, 2, 2, cubeSource.data(), 15),
              "cube rejects a short upload before changing storage");
        cubeRead.fill(0);
        Check(cube.GetData(2, 1, 1, 2, 2, 2, cubeRead.data(), 16)
                  && cubeRead == cubeSource,
              "rejected cube upload leaves the previous region unchanged");
        std::array<std::uint8_t, 4> untouched{0xA5, 0xA5, 0xA5, 0xA5};
        Check(!cube.GetData(6, 0, 0, 0, 1, 1, untouched.data(), 4)
                  && std::all_of(untouched.begin(), untouched.end(),
                                 [](std::uint8_t value) { return value == 0xA5; }),
              "invalid cube face read rejects without touching destination memory");

        std::array<std::uint8_t, 32> volumeSource{};
        for (std::size_t i = 0; i < volumeSource.size(); ++i)
            volumeSource[i] = static_cast<std::uint8_t>(0x40u + i);
        Check(volume.SetData(0, 1, 1, 2, 2, 2, 2, volumeSource.data(), 32),
              "volume accepts one tightly packed partial box");
        std::array<std::uint8_t, 32> volumeRead{};
        Check(volume.GetData(0, 1, 1, 2, 2, 2, 2, volumeRead.data(), 32)
                  && volumeRead == volumeSource,
              "volume box reads back byte-exact, row-major and front-to-back");
        Check(!volume.SetData(0, 1, 1, 2, 2, 2, 2, volumeSource.data(), 31),
              "volume rejects a short upload before changing storage");
        volumeRead.fill(0);
        Check(volume.GetData(0, 1, 1, 2, 2, 2, 2, volumeRead.data(), 32)
                  && volumeRead == volumeSource,
              "rejected volume upload leaves the previous box unchanged");
        Check(!volume.GetData(0, 3, 0, 0, 2, 1, 1, volumeRead.data(), 8),
              "volume rejects an overflowing box without integer addition overflow");
    }
    const SkiaResourceStats released = counters->GetStats();
    Check(released.cpuTextureCubeBackends == 0 && released.cpuTexture3DBackends == 0
              && released.cpuTextureStorageBytes == 0,
          "cube and volume destruction return CPU storage counters to zero");

    Check(Throws([] { SkiaTextureCubeBackend invalid(0, false); }),
          "zero-sized cube rejects before allocation");
    Check(Throws([] { SkiaTexture3DBackend invalid(1, -1, 1, false); }),
          "negative volume dimension rejects before allocation");
    Check(Throws([] { SkiaTextureCubeBackend oversized(4096, false); }),
          "cube exceeding 256 MiB rejects before allocating any face");
    Check(Throws([] { SkiaTexture3DBackend oversized(512, 512, 512, false); }),
          "volume exceeding 256 MiB rejects before allocating voxel storage");
    Check(Throws([] { SkiaTexture3DBackend oversized(16385, 1, 1, false); }),
          "axis exceeding 16384 rejects independently from byte budget");

    return failures == 0 ? 0 : 1;
}
