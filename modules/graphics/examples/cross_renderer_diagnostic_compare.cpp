// SPDX-License-Identifier: MS-PL
// plans/plan_software.md SOFTWARE-61/84: standalone comparator for cross_renderer_diagnostic_scene's
// dumps. Deliberately has no CNA/SHARP_RUNTIME dependency -- just reads two raw 64x64 RGBA8 files
// and reports the per-channel max/mean absolute difference, exiting 1 if the max exceeds the
// given tolerance.
//
// Usage: cna_diag_compare <fileA> <fileB> [tolerance=40]

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
    constexpr int kSize = 64;
    constexpr std::size_t kExpectedBytes = static_cast<std::size_t>(kSize) * kSize * 4u;

    std::vector<std::uint8_t> ReadFile(const char* path)
    {
        std::FILE* f = std::fopen(path, "rb");
        if (f == nullptr)
        {
            std::fprintf(stderr, "cna_diag_compare: failed to open '%s'\n", path);
            std::exit(2);
        }
        std::vector<std::uint8_t> data(kExpectedBytes);
        const std::size_t read = std::fread(data.data(), 1, data.size(), f);
        std::fclose(f);
        if (read != kExpectedBytes)
        {
            std::fprintf(stderr, "cna_diag_compare: '%s' is %zu bytes, expected %zu\n",
                        path, read, kExpectedBytes);
            std::exit(2);
        }
        return data;
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: cna_diag_compare <fileA> <fileB> [tolerance=40]\n");
        return 2;
    }
    const int tolerance = argc > 3 ? std::atoi(argv[3]) : 40;

    const std::vector<std::uint8_t> a = ReadFile(argv[1]);
    const std::vector<std::uint8_t> b = ReadFile(argv[2]);

    int maxDiff = 0;
    long sumDiff = 0;
    int maxDiffX = -1, maxDiffY = -1, maxDiffChannel = -1;
    for (int y = 0; y < kSize; ++y)
    {
        for (int x = 0; x < kSize; ++x)
        {
            for (int c = 0; c < 4; ++c)
            {
                const std::size_t idx = (static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x)) * 4u +
                                        static_cast<std::size_t>(c);
                const int diff = std::abs(static_cast<int>(a[idx]) - static_cast<int>(b[idx]));
                sumDiff += diff;
                if (diff > maxDiff)
                {
                    maxDiff = diff;
                    maxDiffX = x; maxDiffY = y; maxDiffChannel = c;
                }
            }
        }
    }
    const double meanDiff = static_cast<double>(sumDiff) / static_cast<double>(kExpectedBytes);

    std::printf("max diff = %d at (%d,%d) channel %d; mean diff = %.3f; tolerance = %d\n",
               maxDiff, maxDiffX, maxDiffY, maxDiffChannel, meanDiff, tolerance);

    if (maxDiff > tolerance)
    {
        std::printf("FAIL: max diff %d exceeds tolerance %d\n", maxDiff, tolerance);
        return 1;
    }
    std::printf("PASS: max diff %d within tolerance %d\n", maxDiff, tolerance);
    return 0;
}
