#include <array>

#include <gtest/gtest.h>

#include "CNA/Internal/Backends/Glide/GlideCapability.hpp"

using CNA::GraphicsCapability;
using CNA::Internal::Backends::Glide::SupportsGlideCapability;

TEST(GlideCapabilityTest, ReportsImplementedThreeDPath)
{
    EXPECT_TRUE(SupportsGlideCapability(GraphicsCapability::ThreeD));
    EXPECT_TRUE(SupportsGlideCapability(GraphicsCapability::AdditiveBlending));
}

TEST(GlideCapabilityTest, RejectsEveryUnimplementedCurrentCapability)
{
    constexpr std::array unsupported{
        GraphicsCapability::DepthStencilBuffer,
        GraphicsCapability::MultiSampleAntiAliasing,
        GraphicsCapability::MultipleRenderTargets,
        GraphicsCapability::AnisotropicFiltering,
        GraphicsCapability::WireFrame,
        GraphicsCapability::OcclusionQuery,
        GraphicsCapability::CustomEffects,
        GraphicsCapability::Texture3D,
        GraphicsCapability::MultiStreamVertexInput,
        GraphicsCapability::Instancing,
        GraphicsCapability::StencilBuffer,
    };
    for (const GraphicsCapability capability : unsupported)
    {
        EXPECT_FALSE(SupportsGlideCapability(capability));
    }
}
