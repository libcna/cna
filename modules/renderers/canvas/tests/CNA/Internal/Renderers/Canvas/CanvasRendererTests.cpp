// SPDX-License-Identifier: MS-PL
//
// plan_canvas.md CANVAS-80: structural GTest coverage for everything on the CANVAS renderer that
// doesn't need a real CanvasRenderingContext2D -- ThrowNo3D coverage and the blend-mode->
// globalCompositeOperation pure-function mapping. Runs under `node CnaTests.js` (Design decision
// 9): this dev loop has no real browser DOM, and SDL_Init(SDL_INIT_VIDEO) itself throws under
// Emscripten/node (confirmed empirically in Phase C1) -- so every test here deliberately avoids
// touching window_ (GetViewportSize/TransformWindowToLogical/... need a real SDL window and are
// left to CANVAS-82's manual browser checklist instead).
#include <gtest/gtest.h>

#if defined(CNA_RENDERER_CANVAS)
#include "CNA/Internal/Renderers/Canvas/CanvasRenderer.hpp"
#include "CNA/Internal/Renderers/Canvas/CanvasSpriteBatchRenderer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Canvas;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;

namespace
{
    // Non-null but never-dereferenced: CanvasRenderer's constructor only null-checks its
    // window pointer and registers it in a pointer-keyed map (IGraphicsRenderer::RegisterForWindow)
    // -- it never calls a real SDL API on it, and none of the ThrowNo3D-only methods exercised
    // below touch window_ either.
    SDL_Window* FakeWindow() { return reinterpret_cast<SDL_Window*>(0x1); }

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

TEST(CanvasRendererThrowNo3D, ClearVariantsThrow)
{
    CanvasRenderer renderer(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
    EXPECT_THROW(renderer.ClearColorAndDepth(0, 0, 0, 1, 1.0f), std::runtime_error);
    EXPECT_THROW(renderer.ClearDepth(1.0f), std::runtime_error);
    EXPECT_THROW(renderer.ClearStencil(0), std::runtime_error);
    EXPECT_THROW(renderer.ClearDepthAndStencil(1.0f, 0), std::runtime_error);
    EXPECT_THROW(renderer.ClearColorAndStencil(0, 0, 0, 1, 0), std::runtime_error);
    EXPECT_THROW(renderer.ClearColorDepthAndStencil(0, 0, 0, 1, 1.0f, 0), std::runtime_error);
}

TEST(CanvasRendererThrowNo3D, DepthAndBlendStateSettersThrow)
{
    CanvasRenderer renderer(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
    EXPECT_THROW(renderer.SetDepthTestEnabled(true), std::runtime_error);
    EXPECT_THROW(renderer.SetBlendEnabled(true), std::runtime_error);
    EXPECT_THROW(renderer.SetDepthWriteEnabled(true), std::runtime_error);
    EXPECT_FALSE(renderer.SupportsDepthStencil());
}

TEST(CanvasRendererThrowNo3D, VertexAndIndexBufferCreationThrows)
{
    CanvasRenderer renderer(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
    EXPECT_THROW(renderer.CreateVertexBuffer(3), std::runtime_error);
    EXPECT_THROW(renderer.CreateIndexBuffer16(3), std::runtime_error);
    // CANVAS-62 / GLTF-163: no Canvas-local override; the shared default now rejects 32-bit
    // buffers directly rather than delegating to a 16-bit factory.
    EXPECT_THROW(renderer.CreateIndexBuffer32(3), std::runtime_error);
}

TEST(CanvasRendererThrowNo3D, DrawCallsThrow)
{
    CanvasRenderer renderer(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
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
    CanvasRenderer renderer(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
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
