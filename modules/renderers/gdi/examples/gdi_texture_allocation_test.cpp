// SPDX-License-Identifier: MS-PL
// GDI-076: CPU Texture2D allocation now shares GDI-067's framebuffer allocation discipline --
// a checked layout/byte-budget contract, pixel-data/dimension agreement, and translated
// bad_alloc/length_error failures, enforced at the GDI/Software CPU texture boundary.

#include "CNA/Internal/Renderers/Gdi/GdiRenderer.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareTextureAllocation.hpp"
#include "common/SdlTestGraphicsServices.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Gdi;
using namespace CNA::Internal::Renderers::Software;
using namespace CNA::Internal::Graphics;

namespace
{
    bool Expect(bool condition, const char* message)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        return condition;
    }

    template<typename ExceptionT, typename Callback>
    bool ExpectThrows(Callback&& callback, const char* requiredText, const char* message)
    {
        try
        {
            callback();
        }
        catch (const ExceptionT& error)
        {
            return Expect(std::string(error.what()).find(requiredText) != std::string::npos,
                          message);
        }
        catch (...)
        {
            return Expect(false, message);
        }
        return Expect(false, message);
    }

    bool ExercisePureLayouts()
    {
        bool ok = true;

        const SoftwareTextureAllocationLayout single =
            PlanSoftwareTextureAllocation({800, 600, 1});
        ok &= Expect(single.IsValid() && single.pixelCount == 480000u &&
                         single.baseColorBytes == 1920000u && single.mipBytes == 0u &&
                         single.totalBytes == 1920000u,
                     "single-level layout commits exactly four bytes per pixel");

        const SoftwareTextureAllocationLayout mipmapped =
            PlanSoftwareTextureAllocation({4, 4, 3});
        ok &= Expect(mipmapped.IsValid() && mipmapped.baseColorBytes == 64u &&
                         mipmapped.mipBytes == 20u && mipmapped.totalBytes == 84u,
                     "declared mip chain accounts for every generated lower level's bytes");

        ok &= Expect(PlanSoftwareTextureAllocation({0, 1, 1}).error ==
                         SoftwareTextureAllocationError::NonPositiveDimension &&
                         PlanSoftwareTextureAllocation({1, -1, 1}).error ==
                         SoftwareTextureAllocationError::NonPositiveDimension,
                     "layout rejects zero and negative dimensions before unsigned conversion");
        ok &= Expect(PlanSoftwareTextureAllocation(
                         {SoftwareFramebufferMaxDimension + 1, 1, 1}).error ==
                         SoftwareTextureAllocationError::DimensionLimitExceeded,
                     "layout enforces the documented single-axis dimension ceiling");
        ok &= Expect(PlanSoftwareTextureAllocation({11586, 11586, 1}).error ==
                         SoftwareTextureAllocationError::ByteBudgetExceeded,
                     "layout rejects a multiplication-safe single level above the 512 MiB budget");
        ok &= Expect(PlanSoftwareTextureAllocation({11500, 11500, 1}).IsValid() &&
                         PlanSoftwareTextureAllocation({11500, 11500, 2}).error ==
                             SoftwareTextureAllocationError::ByteBudgetExceeded,
                     "declared mip storage participates in the same per-resource byte budget");
        return ok;
    }

    bool ExerciseLiveTextureCreation(GdiRenderer& renderer)
    {
        bool ok = true;

        ImageData tiny;
        tiny.width = 2;
        tiny.height = 2;
        tiny.pixels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
        std::unique_ptr<ITextureRenderer> texture = renderer.CreateTexture(tiny);
        auto* softwareTexture = dynamic_cast<SoftwareTextureRenderer*>(texture.get());
        ok &= Expect(softwareTexture != nullptr && softwareTexture->ColorPixels().size() == 16u,
                     "an ordinary small texture creates real checked CPU storage");

        // REMED-GFX-229: use an odd width and deliberately asymmetric channels so a tight-row
        // assumption, padding copy, channel swap, or row overlap cannot accidentally pass.
        ImageData stridedData;
        stridedData.width = 3;
        stridedData.height = 2;
        stridedData.pixels.assign(3u * 2u * 4u, 0u);
        std::unique_ptr<ITextureRenderer> strided = renderer.CreateTexture(stridedData);
        auto* softwareStrided = dynamic_cast<SoftwareTextureRenderer*>(strided.get());
        const std::array<std::uint8_t, 32> paddedUpload{
            1, 17, 33, 49,  65, 81, 97, 113,  129, 145, 161, 177,
            0xDE, 0xAD, 0xBE, 0xEF,
            2, 19, 37, 53,  71, 89, 107, 127,  149, 167, 191, 211,
            0xCA, 0xFE, 0xBA, 0xBE,
        };
        const std::vector<std::uint8_t> expectedRows{
            1, 17, 33, 49,  65, 81, 97, 113,  129, 145, 161, 177,
            2, 19, 37, 53,  71, 89, 107, 127,  149, 167, 191, 211,
        };
        strided->UpdatePixels(paddedUpload.data(), 16);
        ok &= Expect(softwareStrided != nullptr &&
                         softwareStrided->ColorPixels() == expectedRows,
                     "odd-width texture upload consumes each padded RGBA row without padding");

        ok &= ExpectThrows<System::ArgumentOutOfRangeException>(
            [&] { strided->UpdatePixels(paddedUpload.data(), 11); },
            "at least 12",
            "a positive texture stride shorter than width*4 is rejected before copying");
        ok &= Expect(softwareStrided != nullptr &&
                         softwareStrided->ColorPixels() == expectedRows,
                     "a rejected short texture stride leaves the prior pixels unchanged");

        // An oversized caller-supplied buffer must be truncated to exactly width*height*4, not
        // retained verbatim -- avoids paying for and keeping bytes the resource never reports.
        ImageData oversizedBuffer;
        oversizedBuffer.width = 2;
        oversizedBuffer.height = 2;
        oversizedBuffer.pixels.assign(64, 7);
        std::unique_ptr<ITextureRenderer> truncated = renderer.CreateTexture(oversizedBuffer);
        auto* softwareTruncated = dynamic_cast<SoftwareTextureRenderer*>(truncated.get());
        ok &= Expect(softwareTruncated != nullptr &&
                         softwareTruncated->ColorPixels().size() == 16u,
                     "a larger-than-required caller buffer is truncated to exactly its own level");

        ImageData undersizedBuffer;
        undersizedBuffer.width = 4;
        undersizedBuffer.height = 4;
        undersizedBuffer.pixels.assign(8, 0); // Needs 64 bytes; supplies far fewer.
        ok &= ExpectThrows<System::ArgumentException>(
            [&] { (void)renderer.CreateTexture(undersizedBuffer); },
            "requires at least",
            "an undersized caller buffer is rejected before it can be sampled out of bounds");

        ok &= ExpectThrows<System::ArgumentOutOfRangeException>(
            [&] {
                ImageData huge;
                huge.width = 11586;
                huge.height = 11586;
                huge.pixels.assign(4, 0); // Rejected on layout before this length is even checked.
                (void)renderer.CreateTexture(huge);
            },
            "texture byte budget exceeded",
            "a texture request above the documented byte budget fails at the GDI/Software boundary");

        ok &= ExpectThrows<System::ArgumentOutOfRangeException>(
            [&] {
                ImageData invalidDims;
                invalidDims.width = 0;
                invalidDims.height = 4;
                invalidDims.pixels.assign(16, 0);
                (void)renderer.CreateTexture(invalidDims);
            },
            "dimensions must be positive",
            "a non-positive dimension is rejected before allocation");

        return ok;
    }
}

int main()
{
    bool ok = ExercisePureLayouts();

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "CNA GDI texture allocation", 8, 6, SDL_WINDOW_HIDDEN);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    try
    {
        GdiRenderer renderer(CNA::Examples::SdlTestRendererArgs(
            window, nullptr, nullptr, 8, 6, CnaPresentationMode::Stretch));
        ok &= ExerciseLiveTextureCreation(renderer);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI texture allocation test failed: %s\n", error.what());
        ok = false;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 1;
}
