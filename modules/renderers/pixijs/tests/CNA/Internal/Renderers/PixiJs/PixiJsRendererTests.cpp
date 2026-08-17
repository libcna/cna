// SPDX-License-Identifier: MS-PL
//
// plan_pixijs.md PIXIJS-80: structural GTest coverage for everything on the PIXIJS renderer that
// doesn't need a real PIXI.Application -- ThrowNo3D coverage and the blend-state ->
// PixiJsBlendMode pure-function mapping. plan_pixijs.md's own status block: this has never
// actually been run (no Emscripten toolchain in the session that wrote it) -- written so a future
// session with a real emsdk has something to run immediately.
#include <gtest/gtest.h>

#if defined(CNA_RENDERER_PIXIJS)
#include "CNA/Internal/Renderers/PixiJs/PixiJsRenderer.hpp"
#include "CNA/Internal/Renderers/PixiJs/PixiJsSpriteBatchRenderer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::PixiJs;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;

namespace
{
    // Non-null but never-dereferenced, same rationale as CanvasRendererTests.cpp's own FakeWindow():
    // PixiJsRenderer's constructor only null-checks its window pointer and registers it in a
    // pointer-keyed map -- it never calls a real SDL API on it, and none of the ThrowNo3D-only
    // methods exercised below touch window_ either.
    SDL_Window* FakeWindow() { return reinterpret_cast<SDL_Window*>(0x1); }

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

TEST(PixiJsBlendStateMapping, StandardPresetsMapCorrectly)
{
    EXPECT_EQ(BlendStateToPixiJsBlendMode(0, 0, 1, 1, 0, 0), PixiJsBlendMode::Opaque);
    EXPECT_EQ(BlendStateToPixiJsBlendMode(0, 0, 5, 5, 0, 0), PixiJsBlendMode::AlphaBlend);
    EXPECT_EQ(BlendStateToPixiJsBlendMode(4, 4, 5, 5, 0, 0), PixiJsBlendMode::NonPremultiplied);
    EXPECT_EQ(BlendStateToPixiJsBlendMode(4, 4, 0, 0, 0, 0), PixiJsBlendMode::Additive);
}

TEST(PixiJsBlendStateMapping, AsymmetricColorAlphaFactorsThrow)
{
    EXPECT_THROW(BlendStateToPixiJsBlendMode(0, 4, 5, 5, 0, 0), std::runtime_error);
}

TEST(PixiJsBlendStateMapping, NonAddBlendFunctionThrows)
{
    EXPECT_THROW(BlendStateToPixiJsBlendMode(0, 0, 5, 5, 1, 0), std::runtime_error);
}

TEST(PixiJsBlendStateMapping, ArbitraryCustomBlendFactorsThrow)
{
    EXPECT_THROW(BlendStateToPixiJsBlendMode(2, 2, 3, 3, 0, 0), std::runtime_error);
}

TEST(PixiJsRendererThrowNo3D, ClearVariantsThrow)
{
    PixiJsRenderer renderer(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
    EXPECT_THROW(renderer.ClearColorAndDepth(0, 0, 0, 1, 1.0f), std::runtime_error);
    EXPECT_THROW(renderer.ClearDepth(1.0f), std::runtime_error);
    EXPECT_THROW(renderer.ClearStencil(0), std::runtime_error);
    EXPECT_THROW(renderer.ClearDepthAndStencil(1.0f, 0), std::runtime_error);
    EXPECT_THROW(renderer.ClearColorAndStencil(0, 0, 0, 1, 0), std::runtime_error);
    EXPECT_THROW(renderer.ClearColorDepthAndStencil(0, 0, 0, 1, 1.0f, 0), std::runtime_error);
}

TEST(PixiJsRendererThrowNo3D, DepthAndBlendStateSettersThrow)
{
    PixiJsRenderer renderer(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
    EXPECT_THROW(renderer.SetDepthTestEnabled(true), std::runtime_error);
    EXPECT_THROW(renderer.SetBlendEnabled(true), std::runtime_error);
    EXPECT_THROW(renderer.SetDepthWriteEnabled(true), std::runtime_error);
    EXPECT_FALSE(renderer.SupportsDepthStencil());
}

TEST(PixiJsRendererThrowNo3D, VertexAndIndexBufferCreationThrows)
{
    PixiJsRenderer renderer(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
    EXPECT_THROW(renderer.CreateVertexBuffer(3), std::runtime_error);
    EXPECT_THROW(renderer.CreateIndexBuffer16(3), std::runtime_error);
    EXPECT_THROW(renderer.CreateIndexBuffer32(3), std::runtime_error);
}

TEST(PixiJsRendererThrowNo3D, DrawCallsThrow)
{
    PixiJsRenderer renderer(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
    DummyVertexBuffer vb;
    DummyIndexBuffer ib;
    const Matrix identity = Matrix::getIdentityProperty();
    EXPECT_THROW(
        renderer.DrawColoredPrimitives(vb, identity, identity, identity, PrimitiveType::TriangleList, 1),
        std::runtime_error);
    EXPECT_THROW(
        renderer.DrawIndexedColoredPrimitives(vb, ib, identity, identity, identity, PrimitiveType::TriangleList, 1),
        std::runtime_error);
}

TEST(PixiJsRendererThrowNo3D, SharedDefaultsReturnNullptr)
{
    PixiJsRenderer renderer(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
    EXPECT_EQ(renderer.CreateOcclusionQuery(), nullptr);
    EXPECT_EQ(renderer.CreateTexture3D(4, 4, 4, false, 0), nullptr);
    EXPECT_EQ(renderer.CreateTextureCube(4, false, 0), nullptr);
    EXPECT_EQ(renderer.CreateRenderTargetCube(4, 0), nullptr);
    EXPECT_EQ(renderer.CreateEffectRenderer("", ""), nullptr);
}

TEST(PixiJsRendererCapability, ReportsAdditiveBlendingOnlyIn2DOnlyV1Scope)
{
    PixiJsRenderer renderer(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
    EXPECT_TRUE(renderer.SupportsCapability(CNA::GraphicsCapability::AdditiveBlending));
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::ThreeD));
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets));
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::CustomEffects));
}

TEST(PixiJsSpriteBatchRendererTest, NullCustomEffectDoesNotThrow)
{
    PixiJsSpriteBatchRenderer batch;
    EXPECT_NO_THROW(batch.SetCustomEffect(nullptr));
}

TEST(PixiJsSpriteBatchRendererTest, NonIdentityTransformThrows)
{
    PixiJsSpriteBatchRenderer batch;
    Matrix m = Matrix::getIdentityProperty();
    m.M11 = 2.0f;
    EXPECT_THROW(batch.SetTransformMatrix(m), std::runtime_error);
    EXPECT_NO_THROW(batch.SetTransformMatrix(Matrix::getIdentityProperty()));
}
#endif
