// SPDX-License-Identifier: MS-PL
//
// plan_canvas.md CANVAS-80: structural GTest coverage for everything on the CANVAS renderer that
// doesn't need a real CanvasRenderingContext2D -- ThrowNo3D coverage and the blend-mode->
// globalCompositeOperation pure-function mapping. Runs either under `node CnaTests.js` (Design
// decision 9) or in the native host-contract target. Neither loop has a real browser DOM, so every
// test here deliberately avoids the EM_JS presentation path; browser integration remains covered
// by CANVAS-82's manual checklist.
#include <gtest/gtest.h>

#if defined(CNA_RENDERER_CANVAS) || defined(CNA_CANVAS_HOST_TESTS)
#include "CNA/Internal/Renderers/Canvas/CanvasRenderer.hpp"
#include "CNA/Internal/Renderers/Canvas/CanvasSpriteBatchRenderer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Canvas;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;

namespace
{
    GraphicsRendererCreateArgs TestArgs()
    {
        GraphicsRendererCreateArgs args;
        args.surface.windowId = 1;
        args.surface.nativeHandle.system = CNA::Platform::NativeWindowSystem::Web;
        args.surface.drawableSize = {64, 64};
        args.virtualWidth = 64;
        args.virtualHeight = 64;
        args.presentationMode = CnaPresentationMode::FixedHeightDynamicWidth;
        return args;
    }

    // Minimal stand-ins so DrawColoredPrimitives/DrawIndexedColoredPrimitives (which take
    // references, not pointers) have something valid to bind to -- ThrowNo3D fires immediately,
    // before either argument's contents are ever read.
    struct DummyVertexBuffer final : IVertexBufferRenderer
    {
        void SetData(const void*, int, std::size_t) override {}
        void SetVertexDeclaration(const VertexDeclaration&) override {}
        int GetVertexCount() const override { return 0; }
    };
    struct DummyIndexBuffer final : IIndexBufferRenderer
    {
        void SetData16(const void*, int) override {}
        int GetIndexCount() const override { return 0; }
    };
}

TEST(CanvasBlendStateMapping, StandardPresetsMapCorrectly)
{
    EXPECT_EQ(BlendStateToCompositeOp(0, 0, 1, 1, 0, 0), CanvasCompositeOp::Copy);   // Opaque
    EXPECT_EQ(BlendStateToCompositeOp(0, 0, 5, 5, 0, 0), CanvasCompositeOp::AlphaBlendSourceOver);
    EXPECT_EQ(BlendStateToCompositeOp(4, 4, 5, 5, 0, 0), CanvasCompositeOp::NonPremultipliedSourceOver);
    EXPECT_EQ(BlendStateToCompositeOp(4, 4, 0, 0, 0, 0), CanvasCompositeOp::Lighter); // Additive
    // AlphaBlend and NonPremultiplied must NOT collapse to the same enum value -- they need
    // different per-pixel processing (un-premultiply only for AlphaBlend) even though both end up
    // driving the same ctx.globalCompositeOperation string.
    EXPECT_NE(BlendStateToCompositeOp(0, 0, 5, 5, 0, 0), BlendStateToCompositeOp(4, 4, 5, 5, 0, 0));
}

TEST(CanvasBlendStateMapping, AsymmetricColorAlphaFactorsThrow)
{
    EXPECT_THROW(BlendStateToCompositeOp(0, 4, 5, 5, 0, 0), std::runtime_error);
}

TEST(CanvasBlendStateMapping, NonAddBlendFunctionThrows)
{
    EXPECT_THROW(BlendStateToCompositeOp(0, 0, 5, 5, 1, 0), std::runtime_error);
}

TEST(CanvasBlendStateMapping, ArbitraryCustomBlendFactorsThrow)
{
    EXPECT_THROW(BlendStateToCompositeOp(2, 2, 3, 3, 0, 0), std::runtime_error);
}

TEST(CanvasTintNormalization, AlphaBlendRecoversStraightRgb)
{
    const Color normalized = NormalizeCanvasSpriteTint(
        Color::FromNonPremultiplied(255, 128, 64, 128), true);
    EXPECT_EQ(normalized.getRProperty(), 255);
    EXPECT_EQ(normalized.getGProperty(), 128);
    EXPECT_EQ(normalized.getBProperty(), 64);
    EXPECT_EQ(normalized.getAProperty(), 128);
}

TEST(CanvasTintNormalization, AlphaOnlyFadeStaysWhiteAndZeroAlphaIsCanonical)
{
    for (const int alpha : {255, 192, 128, 64, 1})
    {
        const Color normalized = NormalizeCanvasSpriteTint(
            Color::FromNonPremultiplied(255, 255, 255, alpha), true);
        EXPECT_EQ(normalized.getRProperty(), 255);
        EXPECT_EQ(normalized.getGProperty(), 255);
        EXPECT_EQ(normalized.getBProperty(), 255);
        EXPECT_EQ(normalized.getAProperty(), alpha);
    }

    const Color transparent = NormalizeCanvasSpriteTint(
        Color::FromNonPremultiplied(12, 34, 56, 0), true);
    EXPECT_EQ(transparent.getRProperty(), 255);
    EXPECT_EQ(transparent.getGProperty(), 255);
    EXPECT_EQ(transparent.getBProperty(), 255);
    EXPECT_EQ(transparent.getAProperty(), 0);
}

TEST(CanvasTintNormalization, OtherBlendModesKeepRawChannels)
{
    const Color raw(17, 34, 51, 68);
    const Color unchanged = NormalizeCanvasSpriteTint(raw, false);
    EXPECT_EQ(unchanged.getRProperty(), 17);
    EXPECT_EQ(unchanged.getGProperty(), 34);
    EXPECT_EQ(unchanged.getBProperty(), 51);
    EXPECT_EQ(unchanged.getAProperty(), 68);
}

TEST(CanvasRendererThrowNo3D, ClearVariantsThrow)
{
    CanvasRenderer renderer(TestArgs());
    EXPECT_THROW(renderer.ClearColorAndDepth(0, 0, 0, 1, 1.0f), std::runtime_error);
    EXPECT_THROW(renderer.ClearDepth(1.0f), std::runtime_error);
    EXPECT_THROW(renderer.ClearStencil(0), std::runtime_error);
    EXPECT_THROW(renderer.ClearDepthAndStencil(1.0f, 0), std::runtime_error);
    EXPECT_THROW(renderer.ClearColorAndStencil(0, 0, 0, 1, 0), std::runtime_error);
    EXPECT_THROW(renderer.ClearColorDepthAndStencil(0, 0, 0, 1, 1.0f, 0), std::runtime_error);
}

TEST(CanvasRendererThrowNo3D, DepthAndBlendStateSettersThrow)
{
    CanvasRenderer renderer(TestArgs());
    EXPECT_THROW(renderer.SetDepthTestEnabled(true), std::runtime_error);
    EXPECT_THROW(renderer.SetBlendEnabled(true), std::runtime_error);
    EXPECT_THROW(renderer.SetDepthWriteEnabled(true), std::runtime_error);
    EXPECT_FALSE(renderer.SupportsDepthStencil());
}

TEST(CanvasRendererThrowNo3D, VertexAndIndexBufferCreationThrows)
{
    CanvasRenderer renderer(TestArgs());
    EXPECT_THROW(renderer.CreateVertexBuffer(3), std::runtime_error);
    EXPECT_THROW(renderer.CreateIndexBuffer16(3), std::runtime_error);
    // CANVAS-62: CreateIndexBuffer32 has no Canvas-local override -- IGraphicsRenderer's own shared
    // default delegates to CreateIndexBuffer16(), which already throws.
    EXPECT_THROW(renderer.CreateIndexBuffer32(3), std::runtime_error);
}

TEST(CanvasRendererThrowNo3D, DrawCallsThrow)
{
    CanvasRenderer renderer(TestArgs());
    DummyVertexBuffer vb;
    DummyIndexBuffer ib;
    const Matrix identity = Matrix::getIdentityProperty();
    EXPECT_THROW(
        renderer.DrawColoredPrimitives(vb, identity, identity, identity, PrimitiveType::TriangleList, 1),
        std::runtime_error);
    EXPECT_THROW(
        renderer.DrawIndexedColoredPrimitives(vb, ib, identity, identity, identity, PrimitiveType::TriangleList, 1),
        std::runtime_error);
    // CANVAS-63: DrawPrimitivesEx/DrawIndexedPrimitivesEx have no Canvas-local override -- the
    // shared IGraphicsRenderer default falls back to the (already-throwing) colored-primitives
    // methods above. DrawInstancedPrimitivesEx's shared default throws unconditionally regardless
    // of renderer.
}

TEST(CanvasRendererThrowNo3D, SharedDefaultsReturnNullptr)
{
    CanvasRenderer renderer(TestArgs());
    // CANVAS-64/66/67: no Canvas-local override for any of these -- IGraphicsRenderer's own shared
    // default (return nullptr) is already the intended behavior (CreateOcclusionQuery's nullptr is
    // Design decision 11, a deliberate choice that happens to coincide with the shared default).
    EXPECT_EQ(renderer.CreateOcclusionQuery(), nullptr);
    EXPECT_EQ(renderer.CreateTexture3D(4, 4, 4, false, 0), nullptr);
    EXPECT_EQ(renderer.CreateTextureCube(4, false, 0), nullptr);
    EXPECT_EQ(renderer.CreateRenderTargetCube(4, 0), nullptr);
    EXPECT_EQ(renderer.CreateEffectRenderer("", ""), nullptr);
}

TEST(CanvasSpriteBatchRendererTest, NullCustomEffectDoesNotThrow)
{
    CanvasSpriteBatchRenderer batch;
    EXPECT_NO_THROW(batch.SetCustomEffect(nullptr));
}

TEST(CanvasAddressModeValidation, ClampOrInBoundsNeverThrows)
{
    // addressU=addressV=1 (Clamp): never throws regardless of exceedsBounds/tinted/unpremultiply.
    EXPECT_NO_THROW(ValidateAddressModeCombination(1, 1, true, true, true));
    // In-bounds: never throws regardless of address mode.
    EXPECT_NO_THROW(ValidateAddressModeCombination(0, 2, false, true, true));
}

TEST(CanvasAddressModeValidation, MixedAxisModesThrowOnlyWhenExceedingBounds)
{
    EXPECT_NO_THROW(ValidateAddressModeCombination(0, 2, false, false, false));
    EXPECT_THROW(ValidateAddressModeCombination(0, 2, true, false, false), std::runtime_error);
}

TEST(CanvasAddressModeValidation, TintedDrawThrowsWhenExceedingBoundsWithWrap)
{
    EXPECT_NO_THROW(ValidateAddressModeCombination(0, 0, true, false, false));
    EXPECT_THROW(ValidateAddressModeCombination(0, 0, true, true, false), std::runtime_error);
}

TEST(CanvasAddressModeValidation, UnpremultipliedDrawThrowsWhenExceedingBoundsWithMirror)
{
    EXPECT_NO_THROW(ValidateAddressModeCombination(2, 2, true, false, false));
    EXPECT_THROW(ValidateAddressModeCombination(2, 2, true, false, true), std::runtime_error);
}
#endif
