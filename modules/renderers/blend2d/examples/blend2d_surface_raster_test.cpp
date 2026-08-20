// SPDX-License-Identifier: MS-PL
// plans/plan_blend2d.md: window-independent raster test for Blend2DSurface -- exercises the Blend2D
// BLImage/BLContext wrapper and the straight<->premultiplied pixel-conversion helpers directly,
// with no SDL window/video subsystem involved at all (mirrors the SKIA renderer's window-
// independent Skia_Surface_Raster test category, docs/skia-renderer.md).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "CNA/Internal/Renderers/Blend2D/Blend2DSurface.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace CNA::Internal::Renderers::Blend2D;

namespace
{
    int passCount = 0;
    int totalChecks = 0;

    void check(bool ok, const char* label)
    {
        ++totalChecks;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }
}

int main()
{
    {
        Blend2DSurface surface(8, 8);
        check(surface.Width() == 8 && surface.Height() == 8, "Blend2DSurface reports the constructed size");

        std::vector<std::uint8_t> pixels(8 * 8 * 4);
        check(surface.ReadPixelsRgba(0, 0, 8, 8, pixels.data()),
              "ReadPixelsRgba succeeds for the full surface");
        bool transparentBlack = true;
        for (std::size_t i = 0; i < pixels.size(); ++i)
            if (pixels[i] != 0) transparentBlack = false;
        check(transparentBlack, "A freshly constructed surface starts fully transparent black");
    }

    {
        Blend2DSurface surface(4, 4);
        surface.Clear(1.0f, 0.0f, 0.0f, 1.0f);
        std::vector<std::uint8_t> pixels(4 * 4 * 4);
        check(surface.ReadPixelsRgba(0, 0, 4, 4, pixels.data()), "ReadPixelsRgba succeeds after Clear()");
        bool allRed = true;
        for (int i = 0; i < 4 * 4; ++i)
        {
            if (pixels[i * 4 + 0] != 255 || pixels[i * 4 + 1] != 0 || pixels[i * 4 + 2] != 0 ||
                pixels[i * 4 + 3] != 255)
            {
                allRed = false;
            }
        }
        check(allRed, "Clear(1,0,0,1) produces exact opaque red bytes on readback");

        // A half-alpha clear must round-trip exactly through the premultiply/unpremultiply pair
        // for a colour whose premultiplied component divides evenly by its alpha.
        surface.Clear(1.0f, 1.0f, 1.0f, 0.5f);
        check(surface.ReadPixelsRgba(0, 0, 4, 4, pixels.data()), "ReadPixelsRgba succeeds after a semi-transparent Clear()");
        const std::uint8_t expectedAlpha = pixels[3];
        check(expectedAlpha > 120 && expectedAlpha < 135, "A 0.5 alpha Clear() round-trips to approximately 127/128");
        check(pixels[0] > 250 && pixels[1] > 250 && pixels[2] > 250,
              "A semi-transparent white Clear() keeps full-brightness straight RGB after unpremultiply");
    }

    {
        Blend2DSurface surface(4, 4);
        surface.Resize(2, 2);
        check(surface.Width() == 2 && surface.Height() == 2, "Resize() replaces the surface dimensions");
        std::vector<std::uint8_t> pixels(2 * 2 * 4);
        check(surface.ReadPixelsRgba(0, 0, 2, 2, pixels.data()), "ReadPixelsRgba succeeds after Resize()");
        bool transparentAfterResize = true;
        for (std::size_t i = 0; i < pixels.size(); ++i)
            if (pixels[i] != 0) transparentAfterResize = false;
        check(transparentAfterResize, "A resized surface starts fully transparent black again");
    }

    {
        Blend2DSurface surface(4, 4);
        std::vector<std::uint8_t> pixels(4 * 4 * 4);
        check(!surface.ReadPixelsRgba(2, 2, 4, 4, pixels.data()),
              "ReadPixelsRgba rejects a region that extends past the surface bounds");
        check(!surface.ReadPixelsRgba(-1, 0, 2, 2, pixels.data()),
              "ReadPixelsRgba rejects a negative origin");
    }

    std::printf("=== %d/%d PASS ===\n", passCount, totalChecks);
    return passCount == totalChecks ? 0 : 1;
}
