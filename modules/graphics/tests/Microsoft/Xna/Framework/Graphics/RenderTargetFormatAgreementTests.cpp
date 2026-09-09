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
#include <utility>
#include "Microsoft/Xna/Framework/Vector4.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace
{
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
    using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture;

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

// plans/plan_webgpu.md WEBGPU-202: every transfer path must be sized from the FORMAT, at sizes that
// do not divide the staging alignment.
//
// A readback stages through a row-aligned buffer -- 256 bytes on WebGPU -- so the row stride and the
// texel stride are two different numbers, and a path that assumed four bytes per texel reads the
// wrong ones for an 8- or 16-byte format. NPOT widths are what make that visible: at 16x16 an
// RGBA32Float row is 256 bytes and needs no padding at all, so the bug hides; at 13x7 it is 208 and
// the padding is real. Each size below is chosen so at least one format's row is unaligned.
//
// Exactness is the point. These are `EXPECT_FLOAT_EQ`, not a tolerance: a render target's readback
// is a byte transfer, and a value that survives it approximately has been through something it
// should not have.
// WEBGPU-202: an MRT set whose attachments differ in FORMAT. WebGPU permits it, but a pipeline's
// `targets[]` must match the bound set exactly -- so a renderer that keyed its pipelines on slot 0
// alone would hand the second set the first set's pipeline and fail validation, or worse, not.
// Two sets are bound in one frame that differ ONLY in slot 1, which is the case a slot-0 key cannot
// tell apart.
TEST(RenderTargetFormatAgreement, AnMrtSetKeyedOnEverySlotsFormat)
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::RasterizerState;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets))
        GTEST_SKIP() << "this renderer binds no multi-target set";
    if (!device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable))
        GTEST_SKIP() << "this renderer has no float render target to mix in";

    const auto bindPair = [&device](SurfaceFormat second) -> std::string {
        try
        {
            RenderTarget2D slot0(device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
            RenderTarget2D slot1(device, 8, 8, false, second, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
            device.SetRenderTargets({RenderTargetBinding(static_cast<Texture*>(&slot0)),
                                     RenderTargetBinding(static_cast<Texture*>(&slot1))});
            device.Clear(Color(32, 64, 96, 255));
            // A DRAW, not just the clear: a clear needs no render pipeline, so a clear-only leg
            // would pass whatever the pipeline key contained. This is what forces a pipeline to be
            // built for THIS attachment layout.
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            BasicEffect effect(device);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.setLightingEnabledProperty(false);
            effect.setTextureEnabledProperty(false);
            effect.setVertexColorEnabledProperty(true);
            effect.Apply();
            const VertexPositionColor tri[3] = {
                {Vector3(-0.8f, -0.8f, 0.0f), Color(255, 32, 16, 255)},
                {Vector3( 0.8f, -0.8f, 0.0f), Color(255, 32, 16, 255)},
                {Vector3(-0.8f,  0.8f, 0.0f), Color(255, 32, 16, 255)},
            };
            device.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            // Force the frame to be recorded and submitted, so a pipeline/pass mismatch surfaces
            // here rather than at some later flush.
            device.Present();
        }
        catch (const std::exception& e)
        {
            try { device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); } catch (...) {}
            return e.what();
        }
        return {};
    };

    // Same slot 0, different slot 1 -- in one frame, so the second binding meets whatever cache the
    // first one populated.
    const std::string allColour = bindPair(SurfaceFormat::Color);
    const std::string mixed = bindPair(SurfaceFormat::HdrBlendable);
    std::cout << "[WEBGPU-202] " << RendererName() << " MRT {Color,Color}: "
              << (allColour.empty() ? std::string("ok") : allColour) << std::endl;
    std::cout << "[WEBGPU-202] " << RendererName() << " MRT {Color,HdrBlendable}: "
              << (mixed.empty() ? std::string("ok") : mixed) << std::endl;

    EXPECT_TRUE(allColour.empty())
        << RendererName() << " cannot bind a uniform two-target set: \"" << allColour << '"';
    // Either outcome is legal -- what is not legal is accepting it and rendering through a pipeline
    // built for a different attachment layout, which is a native validation failure rather than a
    // clean exception.
    if (!mixed.empty())
    {
        EXPECT_NE(std::string::npos, mixed.find("format"))
            << RendererName() << " refused a mixed-format MRT set without saying it was about the "
               "formats: \"" << mixed << '"';
    }
}

TEST(RenderTargetFormatAgreement, AFloatTargetReadsBackExactlyAtNonPowerOfTwoSizes)
{
    using Microsoft::Xna::Framework::Vector4;

    GraphicsDevice device;
    if (!device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4))
        GTEST_SKIP() << "this renderer has no RGBA32F render target to read back";

    // Values that are exact in binary floating point at every width in play, and distinct per
    // channel so a transposed or truncated channel cannot pass.
    const float r = 2.5f, g = 0.25f, b = 17.0f, a = 1.0f;

    for (const auto& [w, h] : std::vector<std::pair<int, int>>{{13, 7}, {17, 3}, {5, 11}, {64, 1}})
    {
        SCOPED_TRACE(std::to_string(w) + "x" + std::to_string(h));
        RenderTarget2D target(device, w, h, false, SurfaceFormat::Vector4, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&target);
        device.Clear(r, g, b, a);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Vector4> pixels(static_cast<std::size_t>(w) * h,
                                    Vector4(-1.0f, -1.0f, -1.0f, -1.0f));
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));

        std::size_t wrong = 0;
        for (const Vector4& texel : pixels)
        {
            if (texel.X != r || texel.Y != g || texel.Z != b || texel.W != a) ++wrong;
        }
        std::cout << "[WEBGPU-202] " << RendererName() << ' ' << w << 'x' << h
                  << ": " << (pixels.size() - wrong) << '/' << pixels.size()
                  << " texels exact, first = (" << pixels.front().X << ',' << pixels.front().Y
                  << ',' << pixels.front().Z << ',' << pixels.front().W << ')' << std::endl;
        EXPECT_EQ(0u, wrong)
            << RendererName() << ": " << wrong << " of " << pixels.size() << " texels of a " << w
            << 'x' << h << " Vector4 target came back wrong -- a transfer sized from a fixed texel "
               "width rather than from the format reads exactly like this";
    }
}
