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
using CNA::Platform::WindowId;

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
