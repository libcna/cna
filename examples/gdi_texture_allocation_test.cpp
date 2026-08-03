// SPDX-License-Identifier: MS-PL
// GDI-076: CPU Texture2D allocation now shares GDI-067's framebuffer allocation discipline --
// a checked layout/byte-budget contract, pixel-data/dimension agreement, and translated
// bad_alloc/length_error failures, enforced at the GDI/Software CPU texture boundary.

#include "CNA/Internal/Backends/Gdi/GdiGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Software/SoftwareGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Software/SoftwareTextureAllocation.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <exception>
#include <memory>
#include <string>

using namespace CNA::Internal::Backends;
using namespace CNA::Internal::Backends::Gdi;
using namespace CNA::Internal::Backends::Software;
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

    bool ExerciseLiveTextureCreation(GdiGraphicsBackend& backend)
    {
        bool ok = true;

        ImageData tiny;
        tiny.width = 2;
        tiny.height = 2;
        tiny.pixels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
        std::unique_ptr<ITextureBackend> texture = backend.CreateTexture(tiny);
        auto* softwareTexture = dynamic_cast<SoftwareTextureBackend*>(texture.get());
        ok &= Expect(softwareTexture != nullptr && softwareTexture->ColorPixels().size() == 16u,
                     "an ordinary small texture creates real checked CPU storage");

        // An oversized caller-supplied buffer must be truncated to exactly width*height*4, not
        // retained verbatim -- avoids paying for and keeping bytes the resource never reports.
        ImageData oversizedBuffer;
        oversizedBuffer.width = 2;
        oversizedBuffer.height = 2;
        oversizedBuffer.pixels.assign(64, 7);
        std::unique_ptr<ITextureBackend> truncated = backend.CreateTexture(oversizedBuffer);
        auto* softwareTruncated = dynamic_cast<SoftwareTextureBackend*>(truncated.get());
        ok &= Expect(softwareTruncated != nullptr &&
                         softwareTruncated->ColorPixels().size() == 16u,
                     "a larger-than-required caller buffer is truncated to exactly its own level");

        ImageData undersizedBuffer;
        undersizedBuffer.width = 4;
        undersizedBuffer.height = 4;
        undersizedBuffer.pixels.assign(8, 0); // Needs 64 bytes; supplies far fewer.
        ok &= ExpectThrows<System::ArgumentException>(
            [&] { (void)backend.CreateTexture(undersizedBuffer); },
            "requires at least",
            "an undersized caller buffer is rejected before it can be sampled out of bounds");

        ok &= ExpectThrows<System::ArgumentOutOfRangeException>(
            [&] {
                ImageData huge;
                huge.width = 11586;
                huge.height = 11586;
                huge.pixels.assign(4, 0); // Rejected on layout before this length is even checked.
                (void)backend.CreateTexture(huge);
            },
            "texture byte budget exceeded",
            "a texture request above the documented byte budget fails at the GDI/Software boundary");

        ok &= ExpectThrows<System::ArgumentOutOfRangeException>(
            [&] {
                ImageData invalidDims;
                invalidDims.width = 0;
                invalidDims.height = 4;
                invalidDims.pixels.assign(16, 0);
                (void)backend.CreateTexture(invalidDims);
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
        GdiGraphicsBackend backend(window, 8, 6, CnaPresentationMode::Stretch);
        ok &= ExerciseLiveTextureCreation(backend);
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
