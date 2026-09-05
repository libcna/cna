// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu.md WEBGPU-198 -- the render-target format query and the `RenderTarget2D`
// constructor must agree, for every `SurfaceFormat`, on the adapter actually in front of us.
//
// `GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT` is what a game asks before choosing a
// target format; the constructor is what it then calls. A renderer where those two disagree is
// useless in both directions -- a `true` that throws is a promise it cannot keep, and a `false`
// that would have worked hides a capability the caller could have used. This is the same invariant
// `TextureCubeCompressedFormatAgreementTests` asserts for cube storage, applied to renderability,
// and it is renderer-neutral for the same reason: it does not say WHICH formats a renderer must
// support, only that it must answer the question the same way twice.
//
// The table is every enumerator of `SurfaceFormat`, not a curated list. A format nobody thought
// about is exactly where the two answers drift apart.

#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace
{
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    struct NamedFormat { const char* name; SurfaceFormat format; };

    /// Every enumerator, spelled out so a newly added one has to be considered here rather than
    /// silently skipped by a range loop over a count that no longer matches.
    const std::vector<NamedFormat> kAllFormats{
        {"Color", SurfaceFormat::Color},
        {"Bgr565", SurfaceFormat::Bgr565},
        {"Bgra5551", SurfaceFormat::Bgra5551},
        {"Bgra4444", SurfaceFormat::Bgra4444},
        {"Dxt1", SurfaceFormat::Dxt1},
        {"Dxt3", SurfaceFormat::Dxt3},
        {"Dxt5", SurfaceFormat::Dxt5},
        {"NormalizedByte2", SurfaceFormat::NormalizedByte2},
        {"NormalizedByte4", SurfaceFormat::NormalizedByte4},
        {"Rgba1010102", SurfaceFormat::Rgba1010102},
        {"Rg32", SurfaceFormat::Rg32},
        {"Rgba64", SurfaceFormat::Rgba64},
        {"Alpha8", SurfaceFormat::Alpha8},
        {"Single", SurfaceFormat::Single},
        {"Vector2", SurfaceFormat::Vector2},
        {"Vector4", SurfaceFormat::Vector4},
        {"HalfSingle", SurfaceFormat::HalfSingle},
        {"HalfVector2", SurfaceFormat::HalfVector2},
        {"HalfVector4", SurfaceFormat::HalfVector4},
        {"HdrBlendable", SurfaceFormat::HdrBlendable},
        {"ColorBgraEXT", SurfaceFormat::ColorBgraEXT},
        {"ColorSrgbEXT", SurfaceFormat::ColorSrgbEXT},
        {"Dxt5SrgbEXT", SurfaceFormat::Dxt5SrgbEXT},
        {"Bc7EXT", SurfaceFormat::Bc7EXT},
        {"Bc7SrgbEXT", SurfaceFormat::Bc7SrgbEXT},
        {"ByteEXT", SurfaceFormat::ByteEXT},
        {"UShortEXT", SurfaceFormat::UShortEXT},
    };

    [[nodiscard]] std::string RendererName()
    {
        return std::string(CNA::getGraphicsRendererName(
            CNA::GraphicsRendererSelection::GetSelected()));
    }
}

TEST(RenderTargetFormatAgreement, TheQueryAndTheConstructorAgreeForEveryFormat)
{
    GraphicsDevice device;

    int supported = 0;
    int refused = 0;
    for (const NamedFormat& candidate : kAllFormats)
    {
        SCOPED_TRACE(candidate.name);
        const bool claims = device.SupportsSurfaceFormatAsRenderTargetEXT(candidate.format);

        std::string refusal;
        try
        {
            RenderTarget2D target(device, 4, 4, false, candidate.format, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
            // A target that constructs must also report back the format it was asked for. A
            // renderer that quietly substituted Color would otherwise pass the agreement check
            // while handing the caller a different resource than it requested -- which is exactly
            // what MOD-115 refused to let RenderTarget2D do.
            EXPECT_EQ(candidate.format, target.getFormatProperty())
                << RendererName() << " built a " << candidate.name
                << " render target that reports a different format";
        }
        catch (const std::exception& e)
        {
            refusal = e.what();
        }

        if (claims) ++supported; else ++refused;
        std::cout << "[WEBGPU-198] " << RendererName() << ' ' << candidate.name << ": query="
                  << (claims ? "supported" : "unsupported") << ", construction="
                  << (refusal.empty() ? std::string("accepted") : '"' + refusal + '"') << std::endl;

        // THE INVARIANT, both directions.
        if (claims)
        {
            EXPECT_TRUE(refusal.empty())
                << RendererName() << " reports " << candidate.name
                << " renderable and then refuses to build it: \"" << refusal << '"';
        }
        else
        {
            EXPECT_FALSE(refusal.empty())
                << RendererName() << " reports " << candidate.name
                << " NOT renderable and then builds it anyway -- a capability the caller was told "
                   "it did not have";
        }
    }

    // Not an assertion about which formats: an assertion that the answer is not degenerate. A
    // renderer that answered the same way for all 27 would satisfy the agreement above vacuously.
    std::cout << "[WEBGPU-198] " << RendererName() << ": " << supported << " renderable, "
              << refused << " refused, of " << kAllFormats.size() << std::endl;
    EXPECT_GT(supported, 0) << RendererName() << " reports no renderable format at all";
    EXPECT_GT(refused, 0)
        << RendererName() << " reports EVERY SurfaceFormat renderable, including the "
           "block-compressed ones, which no renderer can genuinely do";
}

// A format that constructs must also BIND and clear. Construction allocating a texture the renderer
// then cannot make a render pass out of would be the same broken promise one step later -- and it is
// the step `SupportsSurfaceFormatAsRenderTargetEXT` is actually about.
TEST(RenderTargetFormatAgreement, EveryRenderableFormatCanBeBoundAndCleared)
{
    using Microsoft::Xna::Framework::Color;
    GraphicsDevice device;

    int bound = 0;
    for (const NamedFormat& candidate : kAllFormats)
    {
        if (!device.SupportsSurfaceFormatAsRenderTargetEXT(candidate.format)) continue;
        SCOPED_TRACE(candidate.name);

        std::string failure;
        try
        {
            RenderTarget2D target(device, 8, 8, false, candidate.format, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
            device.SetRenderTarget(&target);
            device.Clear(Color(64, 128, 192, 255));
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }
        catch (const std::exception& e)
        {
            failure = e.what();
            try { device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); } catch (...) {}
        }
        ++bound;
        std::cout << "[WEBGPU-198] " << RendererName() << ' ' << candidate.name
                  << ": bind+clear " << (failure.empty() ? std::string("ok") : '"' + failure + '"')
                  << std::endl;
        EXPECT_TRUE(failure.empty())
            << RendererName() << " reports " << candidate.name
            << " renderable and builds it, but cannot bind and clear it: \"" << failure << '"';
    }
    EXPECT_GT(bound, 0) << RendererName() << " has no renderable format to bind";
}

// And a format that binds must take a real 3D draw. Clearing exercises only the pass; a draw is what
// forces a render PIPELINE to be built against the target's own colour format, which on WebGPU is
// exactly the dimension `WEBGPU-197` added to the pipeline key. A renderer that keyed its pipelines
// on the swap chain would raise a native format-mismatch here, or silently render nothing.
TEST(RenderTargetFormatAgreement, EveryRenderableFormatTakesAStock3DDraw)
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::RasterizerState;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "this renderer rasterizes no 3D triangles";

    const VertexPositionColor quad[6] = {
        {Vector3(-0.8f,  0.8f, 0.0f), Color(255, 64, 32, 255)},
        {Vector3(-0.8f, -0.8f, 0.0f), Color(255, 64, 32, 255)},
        {Vector3( 0.8f, -0.8f, 0.0f), Color(255, 64, 32, 255)},
        {Vector3(-0.8f,  0.8f, 0.0f), Color(255, 64, 32, 255)},
        {Vector3( 0.8f, -0.8f, 0.0f), Color(255, 64, 32, 255)},
        {Vector3( 0.8f,  0.8f, 0.0f), Color(255, 64, 32, 255)},
    };

    int drawn = 0;
    for (const NamedFormat& candidate : kAllFormats)
    {
        if (!device.SupportsSurfaceFormatAsRenderTargetEXT(candidate.format)) continue;
        SCOPED_TRACE(candidate.name);

        std::string failure;
        try
        {
            RenderTarget2D target(device, 8, 8, false, candidate.format, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
            device.SetRenderTarget(&target);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.Clear(Color(0, 0, 0, 255));
            BasicEffect effect(device);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.setLightingEnabledProperty(false);
            effect.setTextureEnabledProperty(false);
            effect.setVertexColorEnabledProperty(true);
            effect.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }
        catch (const std::exception& e)
        {
            failure = e.what();
            try { device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); } catch (...) {}
        }
        ++drawn;
        std::cout << "[WEBGPU-198] " << RendererName() << ' ' << candidate.name << ": stock 3D draw "
                  << (failure.empty() ? std::string("ok") : '"' + failure + '"') << std::endl;
        EXPECT_TRUE(failure.empty())
            << RendererName() << " cannot draw into a " << candidate.name
            << " render target it reports renderable: \"" << failure << '"';
    }
    EXPECT_GT(drawn, 0) << RendererName() << " has no renderable format to draw into";
}

// The pixel half. The legs above prove a float target binds and takes a draw without a native
// format mismatch, which is a real measurement -- WebGPU rejects a pipeline/pass format mismatch
// outright rather than rendering wrongly -- but it does not show that anything LANDED in the
// target. This does, without needing a typed readback: the float target is cleared to a known
// colour, sampled onto an ordinary Color target, and that is read back.
//
// Restricted to the 16-bit float family and Color on purpose. Sampling a 32-bit float texture with
// a linear filter needs a capability (`float32-filterable` in WebGPU, `GL_ARB_texture_float`'s
// filtering rules elsewhere) that a renderer may legitimately not have, so those formats are
// reported rather than asserted -- a skip with a reason, not a silent gap.
TEST(RenderTargetFormatAgreement, ASixteenBitFloatTargetsContentIsSamplableBack)
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Graphics::SpriteBatch;

    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "this renderer rasterizes no 3D triangles";

    // Red is the one channel every one of these formats carries: R16Float has only red, RG16Float
    // red and green, RGBA16Float all four. Asserting on red alone is what lets one leg cover the
    // whole family without pretending a single-channel target holds a blue.
    const Color kClearColor(255, 0, 0, 255);
    const std::vector<NamedFormat> kSixteenBitFloat{
        {"HalfSingle", SurfaceFormat::HalfSingle},
        {"HalfVector2", SurfaceFormat::HalfVector2},
        {"HalfVector4", SurfaceFormat::HalfVector4},
        {"HdrBlendable", SurfaceFormat::HdrBlendable},
    };

    int measured = 0;
    for (const NamedFormat& candidate : kSixteenBitFloat)
    {
        if (!device.SupportsSurfaceFormatAsRenderTargetEXT(candidate.format))
        {
            std::cout << "[WEBGPU-198] " << RendererName() << ' ' << candidate.name
                      << ": not renderable here, nothing to sample" << std::endl;
            continue;
        }
        SCOPED_TRACE(candidate.name);

        RenderTarget2D floatTarget(device, 8, 8, false, candidate.format, DepthFormat::None, 0,
                                   RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&floatTarget);
        device.Clear(kClearColor);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        RenderTarget2D readable(device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&readable);
        device.Clear(Color(0, 0, 0, 255));
        {
            SpriteBatch batch(device);
            batch.Begin();
            batch.Draw(floatTarget, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8), Color::White);
            batch.End();
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color sampled(0, 0, 0, 0);
        const Rectangle centre(4, 4, 1, 1);
        readable.GetData(0, &centre, &sampled, 0, 1);
        ++measured;
        std::cout << "[WEBGPU-198] " << RendererName() << ' ' << candidate.name
                  << ": cleared red, sampled back as (" << int(sampled.getRProperty()) << ','
                  << int(sampled.getGProperty()) << ',' << int(sampled.getBProperty()) << ')'
                  << std::endl;
        EXPECT_GT(static_cast<int>(sampled.getRProperty()), 200)
            << RendererName() << ": a " << candidate.name
            << " target cleared to red sampled back without its red -- the clear did not reach the "
               "target, or sampling it does not read what was written";
    }
    EXPECT_GT(measured, 0)
        << RendererName() << " reports no 16-bit float render target at all, so this measures "
           "nothing -- if that is genuinely true here the test needs its own declared boundary";
}
