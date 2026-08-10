// SPDX-License-Identifier: MS-PL
// GDI-058: requested presentation/resource settings must report the storage actually applied.

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 8;
    constexpr int kHeight = 8;

    bool Expect(bool condition, const char* message)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        return condition;
    }

    [[nodiscard]] bool SameColor(const Color& actual, const Color& expected)
    {
        return actual.getRProperty() == expected.getRProperty() &&
               actual.getGProperty() == expected.getGProperty() &&
               actual.getBProperty() == expected.getBProperty() &&
               actual.getAProperty() == expected.getAProperty();
    }

    [[nodiscard]] Color ReadTargetPixel(RenderTarget2D& target)
    {
        Color pixel = Color::Transparent;
        const Rectangle rectangle(0, 0, 1, 1);
        target.GetData(0, &rectangle, &pixel, 0, 1);
        return pixel;
    }

    bool ExpectBackbufferState(const PresentationParameters& applied, int expectedSamples,
                               const char* message)
    {
        return Expect(applied.getBackBufferFormatProperty() == SurfaceFormat::Color &&
                          applied.getDepthStencilFormatProperty() == DepthFormat::None &&
                          applied.getMultiSampleCountProperty() == expectedSamples,
                      message);
    }

    bool ExerciseBackbufferState(GraphicsDevice& device)
    {
        bool ok = true;
        ok &= ExpectBackbufferState(
            device.getPresentationParametersProperty(), 0,
            "construction normalizes unsupported RGB565/depth/2x requests to RGBA8/none/0x");
        ok &= Expect(device.SupportsCapability(CNA::GraphicsCapability::StencilBuffer) &&
                         !device.SupportsCapability(CNA::GraphicsCapability::DepthStencilBuffer),
                     "normalized DepthFormat.None remains distinct from GDI's standalone stencil plane");

        const Color stored(17, 33, 65, 255);
        device.Clear(stored);
        Color readback = Color::Transparent;
        const Rectangle pixelRegion(7, 7, 1, 1);
        device.GetBackBufferData(&pixelRegion, &readback, 0, 1);
        ok &= Expect(SameColor(readback, stored),
                     "normalized backbuffer property agrees with actual RGBA8 readback storage");

        // SetPresentationParameters is intentionally a store-only CNAEXT path. It must therefore
        // keep the already active sample count rather than claim an unapplied new request.
        PresentationParameters storedOnly = device.getPresentationParametersProperty().Clone();
        storedOnly.setBackBufferFormatProperty(SurfaceFormat::Bgra4444);
        storedOnly.setDepthStencilFormatProperty(DepthFormat::Depth16);
        storedOnly.setMultiSampleCountProperty(8);
        device.SetPresentationParameters(storedOnly);
        ok &= ExpectBackbufferState(
            device.getPresentationParametersProperty(), 0,
            "store-only presentation update retains the real active GDI format/depth/MSAA state");

        PresentationParameters reset = device.getPresentationParametersProperty().Clone();
        reset.setBackBufferFormatProperty(SurfaceFormat::ColorSrgbEXT);
        reset.setDepthStencilFormatProperty(DepthFormat::Depth24);
        reset.setMultiSampleCountProperty(4);
        device.Reset(reset);
        ok &= ExpectBackbufferState(
            device.getPresentationParametersProperty(), 4,
            "Reset normalizes unsupported formats and writes back the applied GDI 4x count");

        reset = device.getPresentationParametersProperty().Clone();
        reset.setBackBufferFormatProperty(SurfaceFormat::Rgba1010102);
        reset.setDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        reset.setMultiSampleCountProperty(8);
        device.Reset(reset);
        ok &= ExpectBackbufferState(
            device.getPresentationParametersProperty(), 0,
            "Reset writes back zero when an unsupported GDI 8x request is not allocated");
        return ok;
    }

    bool ExerciseAppliedRenderTargetState(GraphicsDevice& device)
    {
        bool ok = true;
        RenderTarget2D target(device, 4, 4, true, SurfaceFormat::Color,
                              DepthFormat::Depth24Stencil8, 4,
                              RenderTargetUsage::PreserveContents);
        ok &= Expect(target.getFormatProperty() == SurfaceFormat::Color &&
                         target.getDepthStencilFormatProperty() == DepthFormat::None &&
                         target.getMultiSampleCountProperty() == 0 &&
                         target.getRenderTargetUsageProperty() == RenderTargetUsage::PreserveContents &&
                         target.getLevelCountProperty() == 3,
                     "RenderTarget2D properties report RGBA8/depthless/single-sample mip storage");

        const Color mipColor(19, 71, 143, 255);
        device.SetRenderTarget(&target);
        device.Clear(mipColor);
        device.SetRenderTarget(nullptr);
        Color lastMip = Color::Transparent;
        target.GetData(2, nullptr, &lastMip, 0, 1);
        ok &= Expect(SameColor(lastMip, mipColor),
                     "reported three-level target is backed by a readable generated 1x1 mip");

        bool rejectedUnsupportedFormat = false;
        try
        {
            RenderTarget2D unsupported(device, 2, 2, false, SurfaceFormat::Bgr565,
                                       DepthFormat::None, 0,
                                       RenderTargetUsage::DiscardContents);
            (void)unsupported;
        }
        catch (const std::runtime_error&)
        {
            rejectedUnsupportedFormat = true;
        }
        ok &= Expect(rejectedUnsupportedFormat,
                     "RenderTarget2D rejects a non-RGBA8 format instead of reporting false storage");
        return ok;
    }

    bool ExerciseUsage(GraphicsDevice& device, RenderTargetUsage usage, const Color& seeded,
                       const Color& expectedAfterRebind, const char* label)
    {
        RenderTarget2D target(device, 2, 2, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, usage);
        const bool propertyMatches = target.getRenderTargetUsageProperty() == usage;
        device.SetRenderTarget(&target);
        device.Clear(seeded);
        device.SetRenderTarget(nullptr);
        device.SetRenderTarget(&target);
        device.SetRenderTarget(nullptr);
        return Expect(propertyMatches &&
                          SameColor(ReadTargetPixel(target), expectedAfterRebind),
                      label);
    }

    bool ExerciseRenderTargetUsage(GraphicsDevice& device)
    {
        bool ok = true;
        ok &= ExerciseUsage(device, RenderTargetUsage::PreserveContents, Color::Red, Color::Red,
                            "PreserveContents property and storage both retain colour on rebind");
        ok &= ExerciseUsage(device, RenderTargetUsage::PlatformContents, Color::Green, Color::Green,
                            "PlatformContents property and GDI storage both retain colour on rebind");
        ok &= ExerciseUsage(device, RenderTargetUsage::DiscardContents, Color::Blue, Color::Black,
                            "DiscardContents property and storage both clear colour on rebind");
        return ok;
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("CNA GDI applied state", kWidth, kHeight,
                                          SDL_WINDOW_HIDDEN);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int result = 0;
    try
    {
        PresentationParameters requested;
        requested.setBackBufferWidthProperty(kWidth);
        requested.setBackBufferHeightProperty(kHeight);
        requested.setBackBufferFormatProperty(SurfaceFormat::Bgr565);
        requested.setDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        requested.setMultiSampleCountProperty(2);
        requested.setDeviceWindowHandleProperty(
            reinterpret_cast<PresentationParameters::IntPtr>(window));

        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),
                              GraphicsProfile::Reach, requested);
        bool ok = true;
        ok &= ExerciseBackbufferState(device);
        ok &= ExerciseAppliedRenderTargetState(device);
        ok &= ExerciseRenderTargetUsage(device);
        result = ok ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI applied-state test failed: %s\n", error.what());
        result = 1;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
