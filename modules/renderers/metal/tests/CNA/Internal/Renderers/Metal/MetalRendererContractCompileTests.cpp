// SPDX-License-Identifier: MS-PL

#include <CNA/Internal/Renderers/Metal/MetalRenderer.hpp>

#include <gtest/gtest.h>

#include <type_traits>

using CNA::Internal::Renderers::Metal::MetalRenderer;

static_assert(!std::is_abstract_v<MetalRenderer>,
              "MetalRenderer must implement the complete current IGraphicsRenderer contract");

TEST(MetalRendererContract, ImplementsEveryCurrentPureVirtualMethod)
{
    EXPECT_FALSE(std::is_abstract_v<MetalRenderer>);
}
