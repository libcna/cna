// SPDX-License-Identifier: MS-PL

// ============================================================================
// WEBGPU-134 / WEBGPU-135 -- WebGPU's capability contract for the two entries it reports false for
// but the shared IGraphicsRenderer default would report true.
//
// WEBGPU-134 (MultipleRenderTargets): SetRenderTargets() refuses any count > 1 (the MRT
// infrastructure, WEBGPU-85/86/87, is not built), so SupportsCapability(MultipleRenderTargets) must
// report false -- the same defect class WEBGPU-115 ruled on for WireFrame: a renderer must not claim
// a capability it silently refuses. This file measures: the query answers false; a single-target
// bind still renders unchanged; a two-target bind throws a catchable System::NotSupportedException
// that points at the capability query; and the next single-target draw still renders.
//
// WEBGPU-84 (OcclusionQuery): occlusion queries are now IMPLEMENTED. CreateOcclusionQuery() returns
// a real WebGPUOcclusionQueryRenderer that records a BeginOcclusionQuery/EndOcclusionQuery pair
// around its tagged draws and resolves an exact sample count, so SupportsCapability(OcclusionQuery)
// now reports true. (This superseded WEBGPU-135's temporary false arm, which stood only while the
// feature was a no-op.) The exact-count behaviour is proven by the WebGPU_OcclusionQuery pixel test;
// the check below asserts the capability is truthful and the query object is real.
// ============================================================================

// Compiled only into a build that holds the WebGPU renderer's own headers; each test then checks at
// runtime that WebGPU is the ACTIVE renderer (mirrors WebGpuWireFrameContractTests.cpp).
#if defined(CNA_RENDERER_WEBGPU) || defined(CNA_RENDERER_PRESENT_WEBGPU)

#include <array>
#include <cstdint>
#include <exception>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "System/NotSupportedException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

namespace
{
    using CNA::GraphicsCapability;
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    constexpr int kSize = 32;

    RenderTarget2D MakeTarget(GraphicsDevice& device)
    {
        return RenderTarget2D(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    }

    // Renders the shared unlit vertex-colour triangle into a single render target and returns the
    // number of non-black pixels -- proof that the single-target path drew something. The state and
    // readback shape mirror the shared WireFrame oracle's own proven single-target route (cull None,
    // full scissor, opaque, region GetData); the point of this test is the render-target bind path,
    // not a winding or blend convention.
    int RenderIntoSingleTargetAndCount(GraphicsDevice& device)
    {
        RenderTarget2D target = MakeTarget(device);

        const VertexPositionColor verts[3] = {
            {Vector3(0.0f, 0.9f, 0.5f), Color(255, 0, 0, 255)},
            {Vector3(-0.9f, -0.9f, 0.5f), Color(0, 255, 0, 255)},
            {Vector3(0.9f, -0.9f, 0.5f), Color(0, 0, 255, 255)},
        };
        VertexBuffer vb(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                        BufferUsage::None);
        vb.SetData(verts, 3);

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);

        BasicEffect fx(device);
        fx.VertexColorEnabled = true;
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.setFogEnabledProperty(false);

        device.SetRenderTarget(&target);
        device.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
        device.Clear(Color(0, 0, 0, 255));
        fx.Apply();
        device.SetVertexBuffer(&vb);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(nullptr);

        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        const Rectangle region(0, 0, kSize, kSize);
        target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));
        int nonBlack = 0;
        for (const Color& c : pixels)
            if (c.getRProperty() || c.getGProperty() || c.getBProperty())
                ++nonBlack;
        return nonBlack;
    }
}   // namespace

// ---------------------------------------------------------------------------
// WEBGPU-134.1: the capability query answers false, asserted by the renderer, not inherited.
// ---------------------------------------------------------------------------
TEST(WebGpuMrtOcclusionContract, MultipleRenderTargetsCapabilityIsFalse)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    EXPECT_FALSE(gd.SupportsCapability(GraphicsCapability::MultipleRenderTargets))
        << "WebGPU claims MRT support while SetRenderTargets refuses count > 1 -- the WEBGPU-134 "
           "override is gone and a game is told a frame it will then get an exception for";
}

// ---------------------------------------------------------------------------
// WEBGPU-134.2: a single-target bind still renders exactly as before the capability answer changed.
// ---------------------------------------------------------------------------
TEST(WebGpuMrtOcclusionContract, SingleTargetBindStillRenders)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    EXPECT_GT(RenderIntoSingleTargetAndCount(gd), 0)
        << "binding one render target and drawing produced an empty frame";
}

// ---------------------------------------------------------------------------
// WEBGPU-134.3: a two-target bind throws a catchable NotSupportedException that points at the
// capability query -- and the device is still usable for a single target afterwards.
// ---------------------------------------------------------------------------
TEST(WebGpuMrtOcclusionContract, TwoTargetBindThrowsNotSupportedAndDeviceRecovers)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    RenderTarget2D t0 = MakeTarget(gd);
    RenderTarget2D t1 = MakeTarget(gd);

    try
    {
        gd.SetRenderTargets({RenderTargetBinding(&t0), RenderTargetBinding(&t1)});
        FAIL() << "a two-target bind was accepted -- WebGPU MRT is not implemented (WEBGPU-85/86/87)";
    }
    catch (const System::NotSupportedException& e)
    {
        const std::string msg = e.what();
        EXPECT_NE(std::string::npos, msg.find("WebGPU"))
            << "the refusal does not name the renderer: \"" << msg << '"';
        EXPECT_NE(std::string::npos, msg.find("SupportsCapability"))
            << "the refusal does not point at the capability query: \"" << msg << '"';
    }

    // The refusal held no state: a single-target bind on the same device still renders.
    EXPECT_GT(RenderIntoSingleTargetAndCount(gd), 0)
        << "the device did not recover to single-target rendering after a refused MRT bind";
}

// ---------------------------------------------------------------------------
// WEBGPU-84: the occlusion-query capability now answers TRUE, and CreateOcclusionQuery() returns a
// real query (not the base nullptr). The WEBGPU-135 arm that reported false while the feature was a
// no-op is gone. The exact-sample-count behaviour (0 behind an occluder, a full target in front) is
// proven by the WebGPU_OcclusionQuery pixel test; this contract check asserts the capability is
// truthful and the query object is real.
// ---------------------------------------------------------------------------
TEST(WebGpuMrtOcclusionContract, OcclusionQueryCapabilityIsTrue)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::OcclusionQuery))
        << "WebGPU implements occlusion queries (WEBGPU-84) but SupportsCapability reports false";
    // The query object must be real: constructing and running Begin()/End() must not throw, and a
    // fresh query reports not-complete with a zero count before any frame resolves it.
    OcclusionQuery query(gd);
    EXPECT_NO_THROW({ query.Begin(); query.End(); });
    EXPECT_EQ(0, query.getPixelCountProperty());
}

#endif  // CNA_RENDERER_WEBGPU
