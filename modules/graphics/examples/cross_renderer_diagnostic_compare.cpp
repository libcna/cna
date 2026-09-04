// SPDX-License-Identifier: MS-PL
// plans/plan_software.md SOFTWARE-61/84: standalone comparator for cross_renderer_diagnostic_scene's
// dumps. Deliberately has no CNA/SHARP_RUNTIME dependency -- just reads two raw 64x64 RGBA8 files
// and reports the per-channel max/mean absolute difference, exiting 1 if the max exceeds the
// given tolerance.
//
// Usage: cna_diag_compare <fileA> <fileB> [tolerance=40] [WxH=64x64]
//
// plans/plan_webgpu.md WEBGPU-207: the optional 4th argument is the frame size, so the shared
// EasyGL<->WebGPU parity fixtures (modules/graphics/examples/parity/) can dump at whatever
// resolution their scene needs and still be judged by THIS comparator rather than by a second one
// that would drift from it. Omitted, it stays the 64x64 the two original diagnostic scenes use, so
// every existing invocation is unchanged.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
    constexpr int kDefaultSize = 64;

    std::vector<std::uint8_t> ReadFile(const char* path, std::size_t expectedBytes)
    {
        std::FILE* f = std::fopen(path, "rb");
        if (f == nullptr)
        {
            std::fprintf(stderr, "cna_diag_compare: failed to open '%s'\n", path);
            std::exit(2);
        }
        std::vector<std::uint8_t> data(expectedBytes);
        const std::size_t read = std::fread(data.data(), 1, data.size(), f);
        std::fclose(f);
        if (read != expectedBytes)
        {
            std::fprintf(stderr, "cna_diag_compare: '%s' is %zu bytes, expected %zu\n",
                        path, read, expectedBytes);
            std::exit(2);
        }
        return data;
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: cna_diag_compare <fileA> <fileB> [tolerance=40] [WxH=64x64]\n");
        return 2;
    }
    const int tolerance = argc > 3 ? std::atoi(argv[3]) : 40;

    int width = kDefaultSize;
    int height = kDefaultSize;
    if (argc > 4)
    {
        if (std::sscanf(argv[4], "%dx%d", &width, &height) != 2 || width <= 0 || height <= 0)
        {
            std::fprintf(stderr, "cna_diag_compare: '%s' is not a WxH frame size\n", argv[4]);
            return 2;
        }
    }
    const std::size_t expectedBytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;

    const std::vector<std::uint8_t> a = ReadFile(argv[1], expectedBytes);
    const std::vector<std::uint8_t> b = ReadFile(argv[2], expectedBytes);

    int maxDiff = 0;
    long sumDiff = 0;
    int maxDiffX = -1, maxDiffY = -1, maxDiffChannel = -1;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            for (int c = 0; c < 4; ++c)
            {
                const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                         static_cast<std::size_t>(x)) * 4u +
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
    const double meanDiff = static_cast<double>(sumDiff) / static_cast<double>(expectedBytes);

    std::printf("%dx%d: max diff = %d at (%d,%d) channel %d; mean diff = %.3f; tolerance = %d\n",
               width, height, maxDiff, maxDiffX, maxDiffY, maxDiffChannel, meanDiff, tolerance);

    if (maxDiff > tolerance)
    {
        std::printf("FAIL: max diff %d exceeds tolerance %d\n", maxDiff, tolerance);
        return 1;
    }
    std::printf("PASS: max diff %d within tolerance %d\n", maxDiff, tolerance);
    return 0;
}
