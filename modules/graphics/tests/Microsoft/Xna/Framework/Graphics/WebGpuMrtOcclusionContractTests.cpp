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
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
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

    // WEBGPU-85/86/87: a minimal MRT custom effect -- position-only vertex, fragment writing a
    // DISTINCT constant colour to each of two @location outputs. Proving slot 1 receives blue and
    // not slot 0's red is the whole point of MRT (a mis-wired pass would put slot 0's colour in both).
    const char* const kMrtVertWgsl = R"WGSL(
@vertex fn vs_main(@location(0) position: vec3f) -> @builtin(position) vec4f {
    return vec4f(position, 1.0);
}
)WGSL";
    const char* const kMrtFragWgsl = R"WGSL(
struct FragOut {
    @location(0) t0: vec4f,
    @location(1) t1: vec4f,
};
@fragment fn fs_main() -> FragOut {
    var o: FragOut;
    o.t0 = vec4f(1.0, 0.0, 0.0, 1.0);
    o.t1 = vec4f(0.0, 0.0, 1.0, 1.0);
    return o;
}
)WGSL";

    Color CenterOf(RenderTarget2D& target)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        const Rectangle region(0, 0, kSize, kSize);
        target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels[(static_cast<std::size_t>(kSize) / 2) * kSize + kSize / 2];
    }
}   // namespace

// ---------------------------------------------------------------------------
// WEBGPU-85/86/87: MRT is implemented, so the capability query now answers TRUE (the WEBGPU-134
// false arm that stood while MRT was refused is gone -- SetRenderTargets now accepts 2..4).
// ---------------------------------------------------------------------------
TEST(WebGpuMrtOcclusionContract, MultipleRenderTargetsCapabilityIsTrue)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::MultipleRenderTargets))
        << "WebGPU implements MRT (WEBGPU-85/86/87) but SupportsCapability reports false";
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
// WEBGPU-85/86/87: a two-target bind now SUCCEEDS, and a custom ShaderEffect that writes
// @location(0..1) fans out to both slots -- each receives its OWN content (red into slot 0, blue
// into slot 1), not slot 0's colour in both. The device still renders a single target afterwards.
// ---------------------------------------------------------------------------
TEST(WebGpuMrtOcclusionContract, TwoTargetBindSucceedsAndBothTargetsReceiveOwnContent)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    RenderTarget2D t0 = MakeTarget(gd);
    RenderTarget2D t1 = MakeTarget(gd);

    ShaderEffect fx(gd, kMrtVertWgsl, kMrtFragWgsl);
    ASSERT_TRUE(fx.IsEffectValid())
        << "the MRT ShaderEffect failed to compile: " << fx.GetCompileErrorEXT();

    const VertexPositionColor quad[6] = {
        {Vector3(-1.0f, 1.0f, 0.0f), Color(255, 255, 255, 255)},
        {Vector3(-1.0f, -1.0f, 0.0f), Color(255, 255, 255, 255)},
        {Vector3(1.0f, -1.0f, 0.0f), Color(255, 255, 255, 255)},
        {Vector3(-1.0f, 1.0f, 0.0f), Color(255, 255, 255, 255)},
        {Vector3(1.0f, -1.0f, 0.0f), Color(255, 255, 255, 255)},
        {Vector3(1.0f, 1.0f, 0.0f), Color(255, 255, 255, 255)},
    };
    VertexBuffer vb(gd, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
    vb.SetData(quad, 6);

    RasterizerState rs;
    rs.setCullModeProperty(CullMode::None);
    gd.setRasterizerStateProperty(rs);
    gd.setDepthStencilStateProperty(DepthStencilState::None);
    gd.setBlendStateProperty(BlendState::Opaque);

    // The two-target bind must NOT throw now.
    ASSERT_NO_THROW(gd.SetRenderTargets({RenderTargetBinding(&t0), RenderTargetBinding(&t1)}));
    gd.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
    gd.Clear(Color(0, 0, 0, 255));
    fx.Apply();
    gd.SetVertexBuffer(&vb);
    gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    gd.SetVertexBuffer(nullptr);
    gd.SetRenderTarget(nullptr);  // unbind the whole set + flush it into both targets

    const Color c0 = CenterOf(t0);
    const Color c1 = CenterOf(t1);
    EXPECT_GT(c0.getRProperty(), 200) << "slot 0 did not receive its red @location(0) output";
    EXPECT_LT(c0.getBProperty(), 64) << "slot 0 wrongly holds slot 1's blue";
    EXPECT_GT(c1.getBProperty(), 200) << "slot 1 did not receive its blue @location(1) output";
    EXPECT_LT(c1.getRProperty(), 64)
        << "slot 1 wrongly holds slot 0's red -- the MRT pass fanned one output to every slot";

    // The device is still usable for a single target afterwards.
    EXPECT_GT(RenderIntoSingleTargetAndCount(gd), 0)
        << "the device did not recover to single-target rendering after an MRT bind";
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
