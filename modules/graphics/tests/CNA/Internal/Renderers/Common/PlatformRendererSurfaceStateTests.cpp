// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Common/PlatformRendererSurfaceState.hpp"

#include <gtest/gtest.h>

namespace
{
    using CNA::Internal::Renderers::PlatformRendererSurfaceState;
    using CNA::Internal::Renderers::RendererSurfaceInfo;
    using CNA::Platform::NativeWindowSystem;
    using CNA::Platform::PlatformException;

    RendererSurfaceInfo MakeX11Surface()
    {
        RendererSurfaceInfo surface;
        surface.windowId = 17;
        surface.nativeHandle.system = NativeWindowSystem::X11;
        surface.nativeHandle.display = reinterpret_cast<void*>(0x1234);
        surface.nativeHandle.windowId = 0x5678;
        surface.drawableSize = {800, 600};
        surface.displayScale = 2.0f;
        return surface;
    }

    TEST(PlatformRendererSurfaceStateTests, RequiresStableIdAndUsableNativeHandle)
    {
        RendererSurfaceInfo surface = MakeX11Surface();
        surface.windowId = 0;
        EXPECT_THROW((PlatformRendererSurfaceState(surface, "TEST")), PlatformException);

        surface = MakeX11Surface();
        surface.nativeHandle = {};
        EXPECT_THROW((PlatformRendererSurfaceState(surface, "TEST")), PlatformException);
    }

    TEST(PlatformRendererSurfaceStateTests, RefreshesSizeAndDensityWithoutRetargeting)
    {
        RendererSurfaceInfo surface = MakeX11Surface();
        PlatformRendererSurfaceState state(surface, "TEST");
        EXPECT_EQ(state.GetWindowId(), 17u);
        EXPECT_EQ(state.GetDrawableSize().width, 800);
        EXPECT_FLOAT_EQ(state.WindowToDrawable(10.0f), 20.0f);

        surface.drawableSize = {1200, 900};
        surface.displayScale = 1.5f;
        state.Update(surface);
        EXPECT_EQ(state.GetDrawableSize().height, 900);
        EXPECT_FLOAT_EQ(state.WindowToDrawable(10.0f), 15.0f);
        EXPECT_FLOAT_EQ(state.DrawableToWindow(15.0f), 10.0f);
    }

    TEST(PlatformRendererSurfaceStateTests, RejectsStableIdentityOrNativeHandleReplacement)
    {
        RendererSurfaceInfo surface = MakeX11Surface();
        PlatformRendererSurfaceState state(surface, "TEST");

        RendererSurfaceInfo changed = surface;
        changed.windowId = 18;
        EXPECT_THROW(state.Update(changed), PlatformException);

        changed = surface;
        changed.nativeHandle.windowId = 0x9999;
        EXPECT_THROW(state.Update(changed), PlatformException);
    }

    TEST(PlatformRendererSurfaceStateTests, InvalidDensityFallsBackToOne)
    {
        RendererSurfaceInfo surface = MakeX11Surface();
        surface.displayScale = 0.0f;
        PlatformRendererSurfaceState state(surface, "TEST");
        EXPECT_FLOAT_EQ(state.GetDisplayScale(), 1.0f);
        EXPECT_FLOAT_EQ(state.WindowToDrawable(7.0f), 7.0f);
    }
}
