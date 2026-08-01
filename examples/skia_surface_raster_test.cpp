// SPDX-License-Identifier: MS-PL
// Pixel-level contract for the window-independent SkiaSurface boundary. This is deliberately
// separate from Game/SDL tests so the raster core keeps a deterministic, headless oracle.

#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

using CNA::Internal::Backends::Skia::SkiaSurface;

namespace
{
    int failures = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures;
    }

    bool Equal(const std::uint8_t* actual, const std::array<std::uint8_t, 4>& expected)
    {
        for (std::size_t i = 0; i < expected.size(); ++i)
            if (actual[i] != expected[i]) return false;
        return true;
    }
}

int main()
{
    SkiaSurface surface(3, 2);
    surface.Clear(17.0f / 255.0f, 34.0f / 255.0f, 51.0f / 255.0f, 1.0f);
    const std::vector<std::uint8_t> cleared = surface.SnapshotRgba();
    Check(cleared.size() == 24, "SnapshotRgba returns tightly packed RGBA8 pixels");
    Check(cleared.size() >= 4 && Equal(cleared.data(), {17, 34, 51, 255}),
          "Clear preserves top-left opaque RGBA8 channels");
    Check(cleared.size() >= 24 && Equal(cleared.data() + 20, {17, 34, 51, 255}),
          "Clear preserves bottom-right top-row-first orientation");

    constexpr std::array<std::uint8_t, 12> replacement{
        1, 2, 3, 255,
        4, 5, 6, 128,
        7, 8, 9, 64,
    };
    Check(surface.WritePixels(0, 1, 3, 1, replacement.data(), 12),
          "WritePixels accepts a tightly packed lower row");
    std::array<std::uint8_t, 12> readback{};
    Check(surface.ReadPixels(0, 1, 3, 1, readback.data(), 12),
          "ReadPixels reads the written lower row");
    // Skia's raster surface stores premultiplied RGBA. The alpha-128 and alpha-64 texels make
    // the documented integer unpremultiplication rounding observable: 5 -> 6 at alpha 128 and
    // 7/9 -> 8 at alpha 64. Keep this exact, deliberate conversion stable rather than claiming a
    // byte-for-byte straight-alpha round trip that a premultiplied canvas cannot provide.
    constexpr std::array<std::uint8_t, 12> expectedReadback{
        1, 2, 3, 255,
        4, 6, 6, 128,
        8, 8, 8, 64,
    };
    Check(readback == expectedReadback,
          "ReadPixels applies the documented premultiplied-to-straight-alpha conversion");

    std::array<std::uint8_t, 4> untouched{0xA5, 0xA5, 0xA5, 0xA5};
    Check(!surface.ReadPixels(-1, 0, 1, 1, untouched.data(), 4),
          "ReadPixels rejects an out-of-bounds rectangle");
    Check(untouched == std::array<std::uint8_t, 4>{0xA5, 0xA5, 0xA5, 0xA5},
          "Rejected ReadPixels leaves destination untouched");

    surface.Resize(1, 1);
    Check(surface.Width() == 1 && surface.Height() == 1, "Resize updates surface dimensions");
    Check(surface.SnapshotRgba().size() == 4, "Resize replaces the raster pixel storage");

    std::printf("=== %s ===\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
