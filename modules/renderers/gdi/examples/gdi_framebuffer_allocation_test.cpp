// SPDX-License-Identifier: MS-PL
// GDI-067: optional attachment storage and overflow-/budget-safe framebuffer allocation.

#include "CNA/Internal/Renderers/Gdi/GdiRenderer.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareFramebufferAllocation.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <string>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Gdi;
using namespace CNA::Internal::Renderers::Software;

namespace
{
    bool Expect(bool condition, const char* message)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        return condition;
    }

    template<typename Callback>
    bool ExpectArgumentOutOfRange(Callback&& callback, const char* requiredText,
                                  const char* message)
    {
        try
        {
            callback();
        }
        catch (const System::ArgumentOutOfRangeException& error)
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
        const SoftwareFramebufferAllocationLayout single =
            PlanSoftwareFramebufferAllocation({800, 600, false, true, 0});
        ok &= Expect(single.IsValid() && single.pixelCount == 480000u &&
                         single.colorBytes == 1920000u && single.depthBytes == 0u &&
                         single.stencilBytes == 480000u && single.multiSampleBytes == 0u &&
                         single.totalBytes == 2400000u,
                     "single-sample GDI layout commits exactly five bytes per pixel");

        const SoftwareFramebufferAllocationLayout multisampled =
            PlanSoftwareFramebufferAllocation({800, 600, false, true, 4});
        ok &= Expect(multisampled.IsValid() && multisampled.depthBytes == 0u &&
                         multisampled.multiSampleBytes == 7680000u &&
                         multisampled.totalBytes == 10080000u,
                     "4x GDI layout commits exactly twenty-one bytes per pixel");

        const SoftwareFramebufferAllocationLayout fullSoftware =
            PlanSoftwareFramebufferAllocation({800, 600, true, true, 4});
        ok &= Expect(fullSoftware.IsValid() && fullSoftware.depthBytes == 1920000u &&
                         fullSoftware.totalBytes == 12000000u,
                     "full Software layout still accounts for depth, stencil and 4x color");

        const SoftwareFramebufferAllocationLayout mipmapped =
            PlanSoftwareFramebufferAllocation({800, 600, false, true, 0, true});
        ok &= Expect(mipmapped.IsValid() && mipmapped.mipBytes == 639756u &&
                         mipmapped.totalBytes == 3039756u,
                     "mipmapped GDI target budget includes every generated lower color level");

        ok &= Expect(PlanSoftwareFramebufferAllocation({0, 1, false, true, 0}).error ==
                         SoftwareFramebufferAllocationError::NonPositiveDimension &&
                         PlanSoftwareFramebufferAllocation({1, -1, false, true, 0}).error ==
                         SoftwareFramebufferAllocationError::NonPositiveDimension,
                     "layout rejects zero and negative dimensions before unsigned conversion");
        ok &= Expect(PlanSoftwareFramebufferAllocation(
                         {SoftwareFramebufferMaxDimension + 1, 1, false, true, 0}).error ==
                         SoftwareFramebufferAllocationError::DimensionLimitExceeded,
                     "layout enforces the documented single-axis dimension ceiling");
        ok &= Expect(PlanSoftwareFramebufferAllocation({1, 1, false, true, 2}).error ==
                         SoftwareFramebufferAllocationError::UnsupportedSampleCount,
                     "layout accepts only the implemented 0x and 4x sample counts");
        ok &= Expect(PlanSoftwareFramebufferAllocation({11000, 11000, false, true, 0}).error ==
                         SoftwareFramebufferAllocationError::ByteBudgetExceeded,
                     "layout rejects a multiplication-safe surface above the 512 MiB budget");
        ok &= Expect(PlanSoftwareFramebufferAllocation({10000, 10000, false, true, 0}).IsValid() &&
                         PlanSoftwareFramebufferAllocation(
                             {10000, 10000, false, true, 0, true}).error ==
                             SoftwareFramebufferAllocationError::ByteBudgetExceeded,
                     "mip storage participates in the same per-resource byte budget");

        const SoftwareFramebufferAllocationError wideError =
            PlanSoftwareFramebufferAllocation(
                {SoftwareFramebufferMaxDimension, SoftwareFramebufferMaxDimension,
                 false, true, 4}).error;
        ok &= Expect(wideError == SoftwareFramebufferAllocationError::ArithmeticOverflow ||
                         wideError == SoftwareFramebufferAllocationError::ByteBudgetExceeded,
                     "maximum 4x layout fails safely on both 32-bit and 64-bit size_t");
        ok &= Expect(SoftwareFramebufferMaxBytes <=
                         std::numeric_limits<std::uint32_t>::max() &&
                         multisampled.colorBytes <= SoftwareFramebufferMaxBytes,
                     "every accepted GDI color plane fits the Win32 DWORD DIB byte field");
        return ok;
    }

    bool ExerciseLiveStorage(SDL_Window* window)
    {
        bool ok = true;
        GdiRenderer renderer(CNA::Examples::SdlTestRendererArgs(
            window, nullptr, nullptr, 8, 6, CnaPresentationMode::Stretch));

        GdiFramebufferStorageTelemetry storage = renderer.DebugGetBackbufferStorage();
        ok &= Expect(storage.colorBytes == 8u * 6u * 4u && storage.depthBytes == 0u &&
                         storage.stencilBytes == 8u * 6u && storage.multiSampleBytes == 0u &&
                         storage.TotalBytes() == 8u * 6u * 5u,
                     "live GDI backbuffer allocates RGBA8 plus stencil and no depth");

        ok &= Expect(renderer.ApplyMultiSampleCount(4) == 4,
                     "live GDI backbuffer enables its supported 4x storage");
        storage = renderer.DebugGetBackbufferStorage();
        ok &= Expect(storage.depthBytes == 0u &&
                         storage.multiSampleBytes == 8u * 6u * 16u &&
                         storage.TotalBytes() == 8u * 6u * 21u,
                     "live 4x GDI backbuffer matches the twenty-one-byte layout");
        ok &= Expect(renderer.ApplyMultiSampleCount(2) == 0 &&
                         renderer.DebugGetBackbufferStorage().multiSampleBytes == 0u,
                     "unsupported MSAA disables and releases optional sample storage");

        std::unique_ptr<IRenderTargetRenderer> target =
            renderer.CreateRenderTarget2D(7, 5, 0, true, false, 0);
        auto* softwareTarget = dynamic_cast<SoftwareRenderTargetRenderer*>(target.get());
        ok &= Expect(softwareTarget != nullptr &&
                         softwareTarget->Framebuffer().color.size() == 7u * 5u * 4u &&
                         softwareTarget->Framebuffer().depthBuffer.empty() &&
                         softwareTarget->Framebuffer().stencilBuffer.size() == 7u * 5u &&
                         softwareTarget->Framebuffer().multiSampleColor.empty(),
                     "GDI render target follows its RGBA8/stencil-only/single-sample contract");

        ok &= ExpectArgumentOutOfRange(
            [&] { (void)renderer.CreateRenderTarget2D(0, 5, 0, true, false, 0); },
            "dimensions must be positive",
            "zero-sized render target fails clearly before allocation");
        ok &= ExpectArgumentOutOfRange(
            [&] { (void)renderer.CreateRenderTarget2D(11000, 11000, 0, true, false, 0); },
            "framebuffer byte budget exceeded",
            "oversized render target fails at the documented byte budget");
        ok &= ExpectArgumentOutOfRange(
            [&] {
                GdiRenderer invalid(CNA::Examples::SdlTestRendererArgs(
                    window, nullptr, nullptr, 0, 6, CnaPresentationMode::Stretch));
            },
            "must be positive",
            "GDI constructor rejects a non-positive backbuffer dimension");

        renderer.Clear(0.25f, 0.5f, 0.75f, 1.0f);
        std::array<std::uint8_t, 4> pixelBefore{};
        renderer.ReadBackbuffer(0, 0, 1, 1, pixelBefore.data());
        ok &= ExpectArgumentOutOfRange(
            [&] { renderer.SetVirtualResolution(11000, 11000); },
            "framebuffer byte budget exceeded",
            "oversized virtual resize fails before attempting allocation");
        int widthAfterFailure = 0;
        int heightAfterFailure = 0;
        renderer.GetViewportSize(widthAfterFailure, heightAfterFailure);
        std::array<std::uint8_t, 4> pixelAfterFailure{};
        renderer.ReadBackbuffer(0, 0, 1, 1, pixelAfterFailure.data());
        ok &= Expect(widthAfterFailure == 8 && heightAfterFailure == 6 &&
                         pixelAfterFailure == pixelBefore,
                     "rejected resize preserves prior dimensions and color storage transactionally");
        ok &= ExpectArgumentOutOfRange(
            [&] { renderer.SetVirtualResolution(-1, 6); },
            "must be positive",
            "negative virtual dimension fails before mutating requested state");
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
        "CNA GDI framebuffer allocation", 8, 6, SDL_WINDOW_HIDDEN);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    try
    {
        ok &= ExerciseLiveStorage(window);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI framebuffer allocation test failed: %s\n", error.what());
        ok = false;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 1;
}
