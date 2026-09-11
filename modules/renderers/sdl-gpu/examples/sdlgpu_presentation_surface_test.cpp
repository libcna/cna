// SPDX-License-Identifier: MS-PL
// SDLGPU-68: deterministic renderer-level presentation surface, HiDPI transform, zero-size and
// present-interval checks. A synthetic platform snapshot avoids asynchronous window-resize timing;
// the separate EasyGL real-window fixture covers delivery through the public event path.

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#include "common/PixelTestGame.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace
{
    using CNA::Internal::Renderers::CnaPresentationMode;
    using CNA::Internal::Renderers::RendererSurfaceInfo;
    using CNA::Internal::Renderers::SdlGpu::SdlGpuRenderer;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    int passed = 0;
    int failed = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        condition ? ++passed : ++failed;
    }

    bool Near(float left, float right)
    {
        return std::fabs(left - right) < 0.0001f;
    }
}

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    GraphicsDevice device;
    auto& renderer = dynamic_cast<SdlGpuRenderer&>(device.GetRenderer());
    auto* window = reinterpret_cast<SDL_Window*>(
        device.getPresentationParametersProperty().getDeviceWindowHandleProperty());
    if (window == nullptr)
    {
        std::printf("[FAIL] active renderer did not create a platform window\n");
        return 1;
    }

    RendererSurfaceInfo surface{};
    surface.windowId = SDL_GetWindowID(window);
    surface.drawableSize = {800, 480};
    surface.displayScale = 2.0f;
    renderer.OnSurfaceChanged(surface);
    renderer.SetVirtualResolution(400, 400);
    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));

    int x = 0, y = 0, width = 0, height = 0;
    renderer.GetDefaultViewportRect(x, y, width, height);
    Check(x == 160 && y == 0 && width == 480 && height == 480,
          "Letterbox returns the centred physical drawable rectangle");

    int logicalWidth = 0, logicalHeight = 0;
    renderer.GetViewportSize(logicalWidth, logicalHeight);
    Check(logicalWidth == 400 && logicalHeight == 400,
          "logical viewport dimensions stay distinct from the physical rectangle");

    float logicalX = 0.0f, logicalY = 0.0f;
    Check(renderer.TransformWindowToLogical(200.0f, 120.0f, logicalX, logicalY) &&
              Near(logicalX, 200.0f) && Near(logicalY, 200.0f),
          "window-to-logical transform accounts for the display scale");
    Check(!renderer.TransformWindowToLogical(40.0f, 120.0f, logicalX, logicalY),
          "window-to-logical transform rejects a point inside the letterbox bar");

    float windowX = 0.0f, windowY = 0.0f;
    Check(renderer.TransformLogicalToWindow(100.0f, 300.0f, windowX, windowY) &&
              Near(windowX, 140.0f) && Near(windowY, 180.0f),
          "logical-to-window transform accounts for the display scale");

    surface.drawableSize = {0, 0};
    surface.displayScale = std::nanf("");
    renderer.OnSurfaceChanged(surface);
    renderer.GetDefaultViewportRect(x, y, width, height);
    Check(width == 0 && height == 0,
          "zero-sized/minimized surface produces a zero physical rectangle");
    Check(!renderer.TransformWindowToLogical(0.0f, 0.0f, logicalX, logicalY) &&
              !renderer.TransformLogicalToWindow(0.0f, 0.0f, windowX, windowY),
          "zero-sized/minimized surface transforms fail safely");

    bool rejectedWindowChange = false;
    surface.windowId += 1;
    try
    {
        renderer.OnSurfaceChanged(surface);
    }
    catch (const std::invalid_argument&)
    {
        rejectedWindowChange = true;
    }
    Check(rejectedWindowChange, "surface updates cannot change the renderer's window identity");

    renderer.SetSwapInterval(2);
    Check(renderer.GetSwapIntervalEXT() == 2,
          "requested PresentInterval::Two remains observable as the requested interval");
    Check(renderer.GetAppliedSwapIntervalEXT() == 1,
          "half-rate request truthfully records the available one-vblank fallback");

    renderer.SetSwapInterval(1);
    Check(renderer.GetSwapIntervalEXT() == 1 && renderer.GetAppliedSwapIntervalEXT() == 1,
          "one-vblank request is both recorded and applied");

    renderer.SetSwapInterval(0);
    Check(renderer.GetSwapIntervalEXT() == 0,
          "immediate request remains observable even when the driver must fall back");
    Check(renderer.GetAppliedSwapIntervalEXT() == 0 || renderer.GetAppliedSwapIntervalEXT() == 1,
          "immediate request records either native immediate or synchronized fallback");
    renderer.SetSwapInterval(1);

    std::printf("=== %d/%d PASS ===\n", passed, passed + failed);
    return failed == 0 ? 0 : 1;
}
