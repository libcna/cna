// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;
using Microsoft::Xna::Framework::Graphics::TextureCollection;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;

// =============================================================================
// TextureCollection — index bounds
// =============================================================================

TEST(TextureCollectionValidationTest, SetNullTexture_DoesNotThrow)
{
    TextureCollection col;
    EXPECT_NO_THROW(col(0, nullptr));
}

TEST(TextureCollectionValidationTest, GetNullSlot_ReturnsNull)
{
    TextureCollection col;
    EXPECT_EQ(col[0], nullptr);
}

TEST(TextureCollectionValidationTest, NegativeIndex_ThrowsOutOfRange)
{
    TextureCollection col;
    EXPECT_THROW(col(-1, nullptr), std::out_of_range);
}

TEST(TextureCollectionValidationTest, IndexAtMax_ThrowsOutOfRange)
{
    TextureCollection col;
    EXPECT_THROW(col(TextureCollection::MaxTextures, nullptr), std::out_of_range);
}

TEST(TextureCollectionValidationTest, IndexAtLastSlot_DoesNotThrow)
{
    TextureCollection col;
    EXPECT_NO_THROW(col(TextureCollection::MaxTextures - 1, nullptr));
}

// =============================================================================
// TextureCollection — disposed texture rejected
// =============================================================================

TEST(TextureCollectionValidationTest, DisposedTexture_ThrowsObjectDisposedException)
{
    TextureCollection col;
    Texture2D tex; // default constructor — no device, no GPU resource
    tex.Dispose();
    EXPECT_THROW(col(0, &tex), System::ObjectDisposedException);
}

TEST(TextureCollectionValidationTest, DisposedTexture_CatchableAsInvalidOperationException)
{
    TextureCollection col;
    Texture2D tex;
    tex.Dispose();
    EXPECT_THROW(col(0, &tex), System::InvalidOperationException);
}

TEST(TextureCollectionValidationTest, LiveTexture_DoesNotThrowForDisposedCheck)
{
    TextureCollection col;
    Texture2D tex; // not disposed
    EXPECT_NO_THROW(col(0, &tex));
}

// =============================================================================
// TextureCollection — active render targets cannot simultaneously be sampled
// =============================================================================

TEST(TextureCollectionValidationTest, ActiveRenderTargetCannotBindToPixelTextureSlot)
{
    GraphicsDevice gd;
    RenderTarget2D target(gd, 4, 4);
    gd.SetRenderTarget(&target);

    EXPECT_THROW(
        gd.getTexturesProperty()(0, &target),
        System::InvalidOperationException);
    EXPECT_EQ(gd.getTexturesProperty()[0], nullptr);
}

TEST(TextureCollectionValidationTest, ActiveRenderTargetCannotBindToVertexTextureSlot)
{
    GraphicsDevice gd;
    RenderTarget2D target(gd, 4, 4);
    gd.SetRenderTarget(&target);

    EXPECT_THROW(
        gd.getVertexTexturesProperty()(0, &target),
        System::InvalidOperationException);
    EXPECT_EQ(gd.getVertexTexturesProperty()[0], nullptr);
}

TEST(TextureCollectionValidationTest, RenderTargetCanBindForSamplingAfterUnbind)
{
    GraphicsDevice gd;
    RenderTarget2D target(gd, 4, 4);
    gd.SetRenderTarget(&target);
    gd.SetRenderTarget(nullptr);

    EXPECT_NO_THROW(gd.getTexturesProperty()(0, &target));
    EXPECT_EQ(gd.getTexturesProperty()[0], static_cast<Texture2D*>(&target));
}

// =============================================================================
// GraphicsDevice.SetRenderTargets — MAX_RENDERTARGET_BINDINGS=4 cap (Task 881)
//
// Matches FNA's real behavior: GraphicsDevice.MAX_RENDERTARGET_BINDINGS=4, and
// SetRenderTargets's Array.Copy into the fixed-size renderTargetBindings array throws when
// given more than 4 targets.
//
// Real RenderTarget2D instances are used throughout so the cap tests reach only the fixed-size
// binding limit. The explicit RenderTargetBinding constructors reject null, while a default
// binding remains representable and is covered separately by the deterministic null-binding test.
// =============================================================================

TEST(GraphicsDeviceValidationTest, SetRenderTargets_FiveTargets_Throws)
{
    GraphicsDevice gd;
    std::vector<std::unique_ptr<RenderTarget2D>> targets;
    std::vector<RenderTargetBinding> bindings;
    for (int i = 0; i < 5; ++i)
    {
        targets.push_back(std::make_unique<RenderTarget2D>(gd, 4, 4));
        bindings.emplace_back(targets.back().get());
    }
    EXPECT_THROW(gd.SetRenderTargets(bindings), std::invalid_argument);
}

TEST(GraphicsDeviceValidationTest, SetRenderTargets_FourTargets_DoesNotThrow)
{
    GraphicsDevice gd;
    std::vector<std::unique_ptr<RenderTarget2D>> targets;
    std::vector<RenderTargetBinding> bindings;
    for (int i = 0; i < 4; ++i)
    {
        targets.push_back(std::make_unique<RenderTarget2D>(gd, 4, 4));
        bindings.emplace_back(targets.back().get());
    }
#if defined(CNA_BACKEND_SDL_RENDERER) || defined(CNA_BACKEND_ASCII) || defined(CNA_BACKEND_DX3) || defined(CNA_BACKEND_DX1) || defined(CNA_BACKEND_DX2) || defined(CNA_BACKEND_DX30) || defined(CNA_BACKEND_DX5) || defined(CNA_BACKEND_DX6) || defined(CNA_BACKEND_DX7)
    // Task 709 (SDL_Renderer) / DX3-27 (DirectDraw, plan_dx3.md) / DX1-27 (real DirectDraw v1,
    // plan_dx1.md) / DX2-84 (same DirectDraw v1 2D layer, plan_dx2.md) / plan_dx30.md (same 2D
    // layer, now DirectDraw v2) / plan_dx5.md (same 2D layer, now DirectDraw v4): all six support
    // exactly one active render target at a time -- unlike the other, real-MRT-capable backends,
    // binding more than one target here must throw clearly rather than silently rendering to only
    // the first. 4 is still within the MAX_RENDERTARGET_BINDINGS cap this test's name/history
    // (Task 881) refers to, so the throw here comes entirely from the backend's own single-target
    // limitation, not the cap check. ASCII (plan_ascii.md) forwards SetRenderTargets straight to
    // the same real SdlGraphicsBackend instance it wraps, so it inherits this exact throw too.
    EXPECT_THROW(gd.SetRenderTargets(bindings), std::runtime_error);
#else
    EXPECT_NO_THROW(gd.SetRenderTargets(bindings));
#endif
}

TEST(GraphicsDeviceValidationTest, SetRenderTargets_OneTarget_DoesNotThrow)
{
    GraphicsDevice gd;
    RenderTarget2D rt(gd, 4, 4);
    std::vector<RenderTargetBinding> bindings{ RenderTargetBinding(&rt) };
    EXPECT_NO_THROW(gd.SetRenderTargets(bindings));
}

TEST(GraphicsDeviceValidationTest, SetRenderTargets_Empty_DoesNotThrow)
{
    GraphicsDevice gd;
    EXPECT_NO_THROW(gd.SetRenderTargets({}));
}

TEST(GraphicsDeviceValidationTest, SetRenderTargets_DefaultNullBindingThrows)
{
    GraphicsDevice gd;
    EXPECT_THROW(
        gd.SetRenderTargets({RenderTargetBinding()}),
        std::invalid_argument);
}

// =============================================================================
// GraphicsDevice.SetVertexBuffers — public binding validation and empty unbind
// =============================================================================

TEST(GraphicsDeviceValidationTest, SetVertexBuffers_DefaultNullBindingThrows)
{
    GraphicsDevice gd;
    EXPECT_THROW(
        gd.SetVertexBuffers({VertexBufferBinding()}),
        System::ArgumentNullException);
}

TEST(GraphicsDeviceValidationTest, SetVertexBuffers_EmptyClearsSingularBinding)
{
    GraphicsDevice gd;
    VertexBuffer vertexBuffer(gd, 3);
    gd.SetVertexBuffer(&vertexBuffer);
    gd.SetVertexBuffers({});

    try
    {
        gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        FAIL() << "DrawPrimitives unexpectedly accepted an empty vertex-buffer state";
    }
    catch (const std::runtime_error& ex)
    {
        EXPECT_NE(
            std::string(ex.what()).find("no vertex buffer"),
            std::string::npos);
    }
}

// Regression test for a real reported crash (cna-template/missing.md): the single-argument
// Clear(const Color&) overload matches FNA's own semantics by requesting
// Target|DepthBuffer|Stencil together, which used to forward unconditionally to
// ClearColorDepthAndStencil() -- a hard throw on SDL_Renderer, since that backend is entirely
// 2D-only and never has a depth/stencil buffer at all. GraphicsDevice::Clear(ClearOptions, ...)
// now masks DepthBuffer/Stencil out of the request when IGraphicsBackend::SupportsDepthStencil()
// reports false, degrading to a color-only clear instead of crashing (matching FNA's own
// dsFormat == DepthFormat.None masking behavior in GraphicsDevice.Clear(ClearOptions, ...)).
TEST(GraphicsDeviceValidationTest, Clear_SingleArgumentColorOverload_DoesNotThrow)
{
    GraphicsDevice gd;
    EXPECT_NO_THROW(gd.Clear(Color::CornflowerBlue));
}
