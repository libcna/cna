// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using CNA::GraphicsCapability;

// This test target only ever builds against a fully 3D-capable backend (EasyGL by default on
// Linux) -- SDL_Renderer/DX3/Canvas each have their own dedicated
// *_graphics_capability_test.cpp example asserting the opposite (nothing supported).

TEST(GraphicsDeviceCapabilityTest, SupportsThreeD)
{
    GraphicsDevice gd;
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::ThreeD));
}

TEST(GraphicsDeviceCapabilityTest, SupportsDepthStencilBuffer)
{
    GraphicsDevice gd;
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::DepthStencilBuffer));
}

TEST(GraphicsDeviceCapabilityTest, SupportsMultipleRenderTargets)
{
    GraphicsDevice gd;
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::MultipleRenderTargets));
}

TEST(GraphicsDeviceCapabilityTest, SupportsOcclusionQuery)
{
    GraphicsDevice gd;
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::OcclusionQuery));
}

TEST(GraphicsDeviceCapabilityTest, SupportsCustomEffects)
{
    GraphicsDevice gd;
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::CustomEffects));
}

// GLES3 (EasyGL's underlying API) has no wireframe fill mode at all -- matches the XNA 4.0
// Graphics API coverage table's own "EasyGL N/A (GLES3)" entry.
TEST(GraphicsDeviceCapabilityTest, DoesNotSupportWireFrame)
{
    GraphicsDevice gd;
    EXPECT_FALSE(gd.SupportsCapability(GraphicsCapability::WireFrame));
}

// MSAA/anisotropic filtering are genuinely device/driver-dependent -- don't assert a specific
// value (would make this test flaky across different CI machines/GPUs), just that querying them
// doesn't throw.
TEST(GraphicsDeviceCapabilityTest, MultiSampleAntiAliasingQueryDoesNotThrow)
{
    GraphicsDevice gd;
    EXPECT_NO_THROW({ (void)gd.SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing); });
}

TEST(GraphicsDeviceCapabilityTest, AnisotropicFilteringQueryDoesNotThrow)
{
    GraphicsDevice gd;
    EXPECT_NO_THROW({ (void)gd.SupportsCapability(GraphicsCapability::AnisotropicFiltering); });
}

// REMED-CONTENT-001: GetMaxTextureDimension() must report a real, positive, sane ceiling -- shared
// content-reading code (Texture2DContentTypeReader) relies on this to reject malformed/adversarial
// XNB dimensions before any backend-specific texture creation is attempted.
TEST(GraphicsDeviceCapabilityTest, GetMaxTextureDimensionReturnsSanePositiveValue)
{
    GraphicsDevice gd;
    const int maxDim = gd.GetMaxTextureDimension();
    EXPECT_GT(maxDim, 0);
    // Comfortably above any legitimate real-world game asset, and comfortably below the
    // adversarial int32 values a corrupt/malicious .xnb could declare.
    EXPECT_GE(maxDim, 2048);
    EXPECT_LT(maxDim, 0x7FFFFFFF);
}
