// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-007: end-to-end vertical-slice evidence for the standalone-rlgl
// renderer. Each of two sequential Game instances creates a CNA platform window and GraphicsDevice,
// loads/initializes rlgl in CNA's OpenGL 3.3 context, performs selective clears and exact
// backbuffer readback, explicitly swaps once, exercises resize bookkeeping, then destroys the
// device cleanly. The second cycle proves the rlgl global-lifecycle guard is released on shutdown.
//
// Exit code 0 = all checks pass, 1 = at least one fails, 77 = no usable display/GL context.

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "common/PixelTestGame.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kInitialWidth = 96;
    constexpr int kInitialHeight = 64;
    constexpr int kResizedWidth = 80;
    constexpr int kResizedHeight = 56;
}

class RlglSmokeTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglSmokeTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kInitialWidth);
        graphics_->setPreferredBackBufferHeightProperty(kInitialHeight);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<CNA::Internal::Renderers::Rlgl::RlglRenderer&>(
            device.GetRenderer());

        int logicalWidth = 0;
        int logicalHeight = 0;
        renderer.GetViewportSize(logicalWidth, logicalHeight);
        Check(logicalWidth == kInitialWidth && logicalHeight == kInitialHeight,
              "GraphicsDevice exposes the requested native-backbuffer logical size");
        Check(!renderer.SupportsCapability(CNA::GraphicsCapability::ThreeD),
              "unfinished 3D resources are reported unsupported, not inferred from OpenGL");
        Check(renderer.GetSwapIntervalEXT() == 0,
              "requested immediate presentation interval reached the renderer");

        const Color firstClear(17, 91, 203, 255);
        device.Clear(firstClear);
        ExpectPixel(
            "color clear is visible before Present",
            Rectangle(kInitialWidth / 2, kInitialHeight / 2, 1, 1), firstClear);

        const Color selectiveClear(193, 37, 71, 255);
        device.Clear(
            ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
            selectiveClear, 0.25f, 9);
        ExpectPixel(
            "combined color/depth/stencil clear preserves the color result",
            Rectangle(kInitialWidth / 3, kInitialHeight / 3, 1, 1), selectiveClear);

        device.Present();
        Check(true, "explicit GraphicsDevice.Present completed through CNA's GL service");

        graphics_->setPreferredBackBufferWidthProperty(kResizedWidth);
        graphics_->setPreferredBackBufferHeightProperty(kResizedHeight);
        graphics_->ApplyChanges();

        SDL_Window* window = reinterpret_cast<SDL_Window*>(
            getWindowProperty().getHandleProperty());
        const bool synchronized = window != nullptr && SDL_SyncWindow(window);
        if (synchronized)
            renderer.OnSurfaceChanged(CNA::Examples::SdlTestSurface(window));
        Check(synchronized, "platform window resize synchronized");

        renderer.GetViewportSize(logicalWidth, logicalHeight);
        Check(logicalWidth == kResizedWidth && logicalHeight == kResizedHeight,
              "logical viewport follows GraphicsDeviceManager resize");

        int viewportX = 0;
        int viewportY = 0;
        int viewportWidth = 0;
        int viewportHeight = 0;
        renderer.GetDefaultViewportRect(
            viewportX, viewportY, viewportWidth, viewportHeight);
        Check(viewportX == 0 && viewportY == 0 && viewportWidth > 0 && viewportHeight > 0,
              "resized native presentation rectangle covers a non-empty drawable");

        if (synchronized)
        {
            const Color afterResize(43, 181, 79, 255);
            device.Clear(afterResize);
            ExpectPixel(
                "clear/readback remains correct after resize bookkeeping",
                Rectangle(kResizedWidth / 2, kResizedHeight / 2, 1, 1), afterResize);
        }
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    for (int cycle = 1; cycle <= 2; ++cycle)
    {
        std::printf("=== RLGL lifecycle cycle %d/2 ===\n", cycle);
        RlglSmokeTest game;
        game.Run();
        if (game.getResultProperty() != 0) return 1;
    }

    std::printf("[PASS] two sequential RLGL GraphicsDevice lifecycles completed\n");
    return 0;
}
