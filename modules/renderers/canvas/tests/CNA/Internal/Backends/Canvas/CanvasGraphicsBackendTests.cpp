// SPDX-License-Identifier: MS-PL
//
// plan_canvas.md CANVAS-80: structural GTest coverage for everything on the CANVAS backend that
// doesn't need a real CanvasRenderingContext2D -- ThrowNo3D coverage and the blend-mode->
// globalCompositeOperation pure-function mapping. Runs under `node CnaTests.js` (Design decision
// 9): this dev loop has no real browser DOM, and SDL_Init(SDL_INIT_VIDEO) itself throws under
// Emscripten/node (confirmed empirically in Phase C1) -- so every test here deliberately avoids
// touching window_ (GetViewportSize/TransformWindowToLogical/... need a real SDL window and are
// left to CANVAS-82's manual browser checklist instead).
#include <gtest/gtest.h>

#if defined(CNA_BACKEND_CANVAS)
#include "CNA/Internal/Backends/Canvas/CanvasGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Canvas/CanvasSpriteBatchBackend.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

using namespace CNA::Internal::Backends;
using namespace CNA::Internal::Backends::Canvas;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;

namespace
{
    // Non-null but never-dereferenced: CanvasGraphicsBackend's constructor only null-checks its
    // window pointer and registers it in a pointer-keyed map (IGraphicsBackend::RegisterForWindow)
    // -- it never calls a real SDL API on it, and none of the ThrowNo3D-only methods exercised
    // below touch window_ either.
    SDL_Window* FakeWindow() { return reinterpret_cast<SDL_Window*>(0x1); }

    // Minimal stand-ins so DrawColoredPrimitives/DrawIndexedColoredPrimitives (which take
    // references, not pointers) have something valid to bind to -- ThrowNo3D fires immediately,
    // before either argument's contents are ever read.
    struct DummyVertexBuffer final : IVertexBufferBackend
    {
        void SetData(const void*, int, std::size_t) override {}
        void SetVertexDeclaration(const VertexDeclaration&) override {}
        int GetVertexCount() const override { return 0; }
    };
    struct DummyIndexBuffer final : IIndexBufferBackend
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

TEST(CanvasGraphicsBackendThrowNo3D, ClearVariantsThrow)
{
    CanvasGraphicsBackend backend(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
    EXPECT_THROW(backend.ClearColorAndDepth(0, 0, 0, 1, 1.0f), std::runtime_error);
    EXPECT_THROW(backend.ClearDepth(1.0f), std::runtime_error);
    EXPECT_THROW(backend.ClearStencil(0), std::runtime_error);
    EXPECT_THROW(backend.ClearDepthAndStencil(1.0f, 0), std::runtime_error);
    EXPECT_THROW(backend.ClearColorAndStencil(0, 0, 0, 1, 0), std::runtime_error);
    EXPECT_THROW(backend.ClearColorDepthAndStencil(0, 0, 0, 1, 1.0f, 0), std::runtime_error);
}

TEST(CanvasGraphicsBackendThrowNo3D, DepthAndBlendStateSettersThrow)
{
    CanvasGraphicsBackend backend(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
    EXPECT_THROW(backend.SetDepthTestEnabled(true), std::runtime_error);
    EXPECT_THROW(backend.SetBlendEnabled(true), std::runtime_error);
    EXPECT_THROW(backend.SetDepthWriteEnabled(true), std::runtime_error);
    EXPECT_FALSE(backend.SupportsDepthStencil());
}

TEST(CanvasGraphicsBackendThrowNo3D, VertexAndIndexBufferCreationThrows)
{
    CanvasGraphicsBackend backend(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
    EXPECT_THROW(backend.CreateVertexBuffer(3), std::runtime_error);
    EXPECT_THROW(backend.CreateIndexBuffer16(3), std::runtime_error);
    // CANVAS-62: CreateIndexBuffer32 has no Canvas-local override -- IGraphicsBackend's own shared
    // default delegates to CreateIndexBuffer16(), which already throws.
    EXPECT_THROW(backend.CreateIndexBuffer32(3), std::runtime_error);
}

TEST(CanvasGraphicsBackendThrowNo3D, DrawCallsThrow)
{
    CanvasGraphicsBackend backend(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
    DummyVertexBuffer vb;
    DummyIndexBuffer ib;
    const Matrix identity = Matrix::getIdentityProperty();
    EXPECT_THROW(
        backend.DrawColoredPrimitives(vb, identity, identity, identity, PrimitiveType::TriangleList, 1),
        std::runtime_error);
    EXPECT_THROW(
        backend.DrawIndexedColoredPrimitives(vb, ib, identity, identity, identity, PrimitiveType::TriangleList, 1),
        std::runtime_error);
    // CANVAS-63: DrawPrimitivesEx/DrawIndexedPrimitivesEx have no Canvas-local override -- the
    // shared IGraphicsBackend default falls back to the (already-throwing) colored-primitives
    // methods above. DrawInstancedPrimitivesEx's shared default throws unconditionally regardless
    // of backend.
}

TEST(CanvasGraphicsBackendThrowNo3D, SharedDefaultsReturnNullptr)
{
    CanvasGraphicsBackend backend(FakeWindow(), 64, 64, CnaPresentationMode::FixedHeightDynamicWidth);
    // CANVAS-64/66/67: no Canvas-local override for any of these -- IGraphicsBackend's own shared
    // default (return nullptr) is already the intended behavior (CreateOcclusionQuery's nullptr is
    // Design decision 11, a deliberate choice that happens to coincide with the shared default).
    EXPECT_EQ(backend.CreateOcclusionQuery(), nullptr);
    EXPECT_EQ(backend.CreateTexture3D(4, 4, 4, false, 0), nullptr);
    EXPECT_EQ(backend.CreateTextureCube(4, false, 0), nullptr);
    EXPECT_EQ(backend.CreateRenderTargetCube(4, 0), nullptr);
    EXPECT_EQ(backend.CreateEffectBackend("", ""), nullptr);
}

TEST(CanvasSpriteBatchBackendTest, NullCustomEffectDoesNotThrow)
{
    CanvasSpriteBatchBackend batch;
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
