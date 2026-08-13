// SPDX-License-Identifier: MS-PL
//
// PLAT-61: the common renderer registry is keyed by the platform contract's WindowId. These
// tests intentionally use no SDL window: a native pointer entering this API again would make this
// translation unit fail to compile and would reintroduce the coupling this task removes.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <gtest/gtest.h>

#include <cstddef>

namespace {

using CNA::Internal::Renderers::IGraphicsRenderer;
using CNA::Internal::Renderers::RendererSurfaceInfo;
using CNA::Platform::NativeWindowSystem;
using CNA::Platform::WindowId;

TEST(RendererSurfaceInfoTests, DefaultsDescribeAWindowlessUnscaledSurface)
{
    const RendererSurfaceInfo surface;

    EXPECT_EQ(surface.windowId, 0u);
    EXPECT_EQ(surface.nativeHandle.system, NativeWindowSystem::Unknown);
    EXPECT_EQ(surface.nativeHandle.display, nullptr);
    EXPECT_EQ(surface.nativeHandle.window, nullptr);
    EXPECT_EQ(surface.nativeHandle.surface, nullptr);
    EXPECT_EQ(surface.nativeHandle.windowId, 0u);
    EXPECT_EQ(surface.drawableSize.width, 0);
    EXPECT_EQ(surface.drawableSize.height, 0);
    EXPECT_FLOAT_EQ(surface.displayScale, 1.0f);
}

TEST(RendererSurfaceInfoTests, CarriesACompletePlatformSnapshotByValue)
{
    RendererSurfaceInfo surface;
    surface.windowId = 0x5800u;
    surface.nativeHandle.system = NativeWindowSystem::Headless;
    surface.drawableSize = {1600, 960};
    surface.displayScale = 2.0f;

    const RendererSurfaceInfo copy = surface;

    EXPECT_EQ(copy.windowId, 0x5800u);
    EXPECT_EQ(copy.nativeHandle.system, NativeWindowSystem::Headless);
    EXPECT_EQ(copy.drawableSize.width, 1600);
    EXPECT_EQ(copy.drawableSize.height, 960);
    EXPECT_FLOAT_EQ(copy.displayScale, 2.0f);
}

class RendererWindowRegistryTest : public ::testing::Test
{
protected:
    static constexpr WindowId FirstWindow = 0x7fff0001u;
    static constexpr WindowId SecondWindow = 0x7fff0002u;

    void TearDown() override
    {
        IGraphicsRenderer::UnregisterForWindow(FirstWindow);
        IGraphicsRenderer::UnregisterForWindow(SecondWindow);
    }

    // The registry stores non-owning pointers and never dereferences them. Distinct aligned
    // addresses are sufficient identity tokens here without requiring a 100-method renderer fake.
    alignas(IGraphicsRenderer) std::byte firstRendererStorage_[sizeof(void*)]{};
    alignas(IGraphicsRenderer) std::byte secondRendererStorage_[sizeof(void*)]{};

    [[nodiscard]] IGraphicsRenderer* FirstRenderer()
    {
        return reinterpret_cast<IGraphicsRenderer*>(firstRendererStorage_);
    }

    [[nodiscard]] IGraphicsRenderer* SecondRenderer()
    {
        return reinterpret_cast<IGraphicsRenderer*>(secondRendererStorage_);
    }
};

TEST_F(RendererWindowRegistryTest, DistinctWindowIdsKeepDistinctRenderers)
{
    IGraphicsRenderer::RegisterForWindow(FirstWindow, FirstRenderer());
    IGraphicsRenderer::RegisterForWindow(SecondWindow, SecondRenderer());

    EXPECT_EQ(IGraphicsRenderer::GetForWindow(FirstWindow), FirstRenderer());
    EXPECT_EQ(IGraphicsRenderer::GetForWindow(SecondWindow), SecondRenderer());
}

TEST_F(RendererWindowRegistryTest, UnregisteringOneWindowPreservesTheOther)
{
    IGraphicsRenderer::RegisterForWindow(FirstWindow, FirstRenderer());
    IGraphicsRenderer::RegisterForWindow(SecondWindow, SecondRenderer());

    IGraphicsRenderer::UnregisterForWindow(FirstWindow);

    EXPECT_EQ(IGraphicsRenderer::GetForWindow(FirstWindow), nullptr);
    EXPECT_EQ(IGraphicsRenderer::GetForWindow(SecondWindow), SecondRenderer());
}

TEST_F(RendererWindowRegistryTest, RegisteringAgainReplacesOnlyThatWindow)
{
    IGraphicsRenderer::RegisterForWindow(FirstWindow, FirstRenderer());
    IGraphicsRenderer::RegisterForWindow(FirstWindow, SecondRenderer());

    EXPECT_EQ(IGraphicsRenderer::GetForWindow(FirstWindow), SecondRenderer());
}

} // namespace
