// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#if defined(CNA_RENDERER_LLGL)
#include "CNA/Internal/Renderers/Llgl/LlglPlatformSurface.hpp"

#include <stdexcept>
#include <string>

namespace
{
    using CNA::Internal::Renderers::Llgl::LlglPlatformSurface;
    using CNA::Internal::Renderers::RendererSurfaceInfo;
    using CNA::Platform::NativeWindowSystem;

    RendererSurfaceInfo MakeX11Surface()
    {
        RendererSurfaceInfo surface;
        surface.windowId = 9;
        surface.nativeHandle.system = NativeWindowSystem::X11;
        surface.nativeHandle.display = reinterpret_cast<void*>(0x1234);
        surface.nativeHandle.windowId = 0x5678;
        surface.drawableSize = {640, 360};
        surface.displayScale = 2.0f;
        return surface;
    }

    TEST(LlglPlatformSurfaceTests, NonX11NativeWindowIsRejectedClearly)
    {
        RendererSurfaceInfo surface = MakeX11Surface();
        surface.nativeHandle.system = NativeWindowSystem::Wayland;
        surface.nativeHandle.surface = reinterpret_cast<void*>(0x4321);
        surface.nativeHandle.windowId = 0;

        try
        {
            LlglPlatformSurface adapter(surface, false);
            FAIL() << "Wayland window was accepted by an X11-only LLGL build";
        }
        catch (const std::runtime_error& error)
        {
            EXPECT_NE(std::string(error.what()).find("X11"), std::string::npos);
            EXPECT_NE(std::string(error.what()).find("Wayland"), std::string::npos);
        }
    }

    TEST(LlglPlatformSurfaceTests, UsesSnapshotSizeAndPlatformOwnedFullscreenState)
    {
        RendererSurfaceInfo surface = MakeX11Surface();
        LlglPlatformSurface adapter(surface, true);
        EXPECT_EQ(adapter.GetContentSize().width, 640u);
        EXPECT_FLOAT_EQ(adapter.WindowToDrawable(10.0f), 20.0f);

        LLGL::Extent2D requested{800, 600};
        bool fullscreen = false;
        EXPECT_FALSE(adapter.AdaptForVideoMode(&requested, &fullscreen));
        EXPECT_EQ(requested.width, 640u);
        EXPECT_EQ(requested.height, 360u);
        EXPECT_TRUE(fullscreen);

        surface.drawableSize = {1280, 720};
        surface.displayScale = 1.5f;
        adapter.Update(surface);
        EXPECT_EQ(adapter.GetContentSize().height, 720u);
        EXPECT_FLOAT_EQ(adapter.DrawableToWindow(15.0f), 10.0f);
    }
}
#endif
