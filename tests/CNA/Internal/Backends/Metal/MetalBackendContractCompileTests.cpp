// SPDX-License-Identifier: MS-PL

#include <CNA/Internal/Backends/Metal/MetalGraphicsBackend.hpp>

#include <gtest/gtest.h>

#include <type_traits>

using CNA::Internal::Backends::Metal::MetalGraphicsBackend;

static_assert(!std::is_abstract_v<MetalGraphicsBackend>,
              "MetalGraphicsBackend must implement the complete current IGraphicsBackend contract");

TEST(MetalBackendContract, ImplementsEveryCurrentPureVirtualMethod)
{
    EXPECT_FALSE(std::is_abstract_v<MetalGraphicsBackend>);
}
