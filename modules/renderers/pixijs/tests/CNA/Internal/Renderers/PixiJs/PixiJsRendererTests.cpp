// SPDX-License-Identifier: MS-PL
//
// plan_pixijs.md PIXIJS-80: structural GTest coverage for everything on the PIXIJS renderer that
// doesn't need a real PIXI.Application -- ThrowNo3D coverage and the blend-state ->
// PixiJsBlendMode pure-function mapping. Compiled either under the selected PIXIJS renderer or in
// the opt-in native host-contract target (CNA_PIXIJS_HOST_TESTS, modules/renderers/CMakeLists.txt),
// exactly like the CANVAS/HTML_DOM/SVG_DOM suites this one is modelled on: the EM_JS bodies are
// excluded on a native host, so the platform-neutral half of the renderer is testable without a
// browser.
#include <gtest/gtest.h>

#if defined(CNA_RENDERER_PIXIJS) || defined(CNA_RENDERER_PRESENT_PIXIJS) \
    || defined(CNA_PIXIJS_HOST_TESTS)
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
    // plan_platform.md PLAT-58/PLAT-61: the renderer receives a platform-neutral surface snapshot,
    // never a windowing-library handle. Same fixture shape as CanvasRendererTests.cpp's TestArgs().
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

// plan_pixijs.md PIXIJS-52: any non-preset Blend/BlendFunction combination now gets a real, generic
// Custom mapping instead of a throw -- verified in a real browser (cna_test_pixijs_smoke frame 12,
// 22/22) via a custom PixiJS blend-mode table registration, not just this pure-function mapping.
TEST(PixiJsBlendStateMapping, AsymmetricColorAlphaFactorsMapToCustom)
{
    EXPECT_EQ(BlendStateToPixiJsBlendMode(0, 4, 5, 5, 0, 0), PixiJsBlendMode::Custom);
}

TEST(PixiJsBlendStateMapping, NonAddBlendFunctionMapsToCustom)
{
    EXPECT_EQ(BlendStateToPixiJsBlendMode(0, 0, 5, 5, 1, 0), PixiJsBlendMode::Custom);
}

TEST(PixiJsBlendStateMapping, ArbitraryCustomBlendFactorsMapToCustom)
{
    EXPECT_EQ(BlendStateToPixiJsBlendMode(2, 2, 3, 3, 0, 0), PixiJsBlendMode::Custom);
}

// plan_pixijs.md PIXIJS-52: XnaBlendToGlFactor/XnaBlendFunctionToGlEquation are the pure-function
// halves of the generic mapping -- every enumerator covered, matching real WebGL GL enum values
// confirmed live against a real WebGL context (see PixiJsRenderer.cpp's own doc comments).
TEST(PixiJsBlendStateMapping, XnaBlendToGlFactorCoversEveryEnumerator)
{
    EXPECT_EQ(XnaBlendToGlFactor(0), 1);      // One -> ONE
    EXPECT_EQ(XnaBlendToGlFactor(1), 0);      // Zero -> ZERO
    EXPECT_EQ(XnaBlendToGlFactor(2), 768);    // SourceColor -> SRC_COLOR
    EXPECT_EQ(XnaBlendToGlFactor(3), 769);    // InverseSourceColor -> ONE_MINUS_SRC_COLOR
    EXPECT_EQ(XnaBlendToGlFactor(4), 770);    // SourceAlpha -> SRC_ALPHA
    EXPECT_EQ(XnaBlendToGlFactor(5), 771);    // InverseSourceAlpha -> ONE_MINUS_SRC_ALPHA
    EXPECT_EQ(XnaBlendToGlFactor(6), 774);    // DestinationColor -> DST_COLOR
    EXPECT_EQ(XnaBlendToGlFactor(7), 775);    // InverseDestinationColor -> ONE_MINUS_DST_COLOR
    EXPECT_EQ(XnaBlendToGlFactor(8), 772);    // DestinationAlpha -> DST_ALPHA
    EXPECT_EQ(XnaBlendToGlFactor(9), 773);    // InverseDestinationAlpha -> ONE_MINUS_DST_ALPHA
    EXPECT_EQ(XnaBlendToGlFactor(10), 32769); // BlendFactor -> CONSTANT_COLOR
    EXPECT_EQ(XnaBlendToGlFactor(11), 32770); // InverseBlendFactor -> ONE_MINUS_CONSTANT_COLOR
    EXPECT_EQ(XnaBlendToGlFactor(12), 776);   // SourceAlphaSaturation -> SRC_ALPHA_SATURATE
    EXPECT_THROW(XnaBlendToGlFactor(13), std::runtime_error);
}

TEST(PixiJsBlendStateMapping, XnaBlendFunctionToGlEquationCoversEveryEnumerator)
{
    EXPECT_EQ(XnaBlendFunctionToGlEquation(0), 32774); // Add -> FUNC_ADD
    EXPECT_EQ(XnaBlendFunctionToGlEquation(1), 32778); // Subtract -> FUNC_SUBTRACT
    EXPECT_EQ(XnaBlendFunctionToGlEquation(2), 32779); // ReverseSubtract -> FUNC_REVERSE_SUBTRACT
    EXPECT_EQ(XnaBlendFunctionToGlEquation(3), 32776); // Max -> MAX
    EXPECT_EQ(XnaBlendFunctionToGlEquation(4), 32775); // Min -> MIN
    EXPECT_THROW(XnaBlendFunctionToGlEquation(5), std::runtime_error);
}

TEST(PixiJsRendererThrowNo3D, ClearVariantsThrow)
{
    PixiJsRenderer renderer(TestArgs());
    EXPECT_THROW(renderer.ClearColorAndDepth(0, 0, 0, 1, 1.0f), std::runtime_error);
    EXPECT_THROW(renderer.ClearDepth(1.0f), std::runtime_error);
    EXPECT_THROW(renderer.ClearStencil(0), std::runtime_error);
    EXPECT_THROW(renderer.ClearDepthAndStencil(1.0f, 0), std::runtime_error);
    EXPECT_THROW(renderer.ClearColorAndStencil(0, 0, 0, 1, 0), std::runtime_error);
    EXPECT_THROW(renderer.ClearColorDepthAndStencil(0, 0, 0, 1, 1.0f, 0), std::runtime_error);
}

TEST(PixiJsRendererThrowNo3D, DepthAndBlendStateSettersThrow)
{
    PixiJsRenderer renderer(TestArgs());
    EXPECT_THROW(renderer.SetDepthTestEnabled(true), std::runtime_error);
    EXPECT_THROW(renderer.SetBlendEnabled(true), std::runtime_error);
    EXPECT_THROW(renderer.SetDepthWriteEnabled(true), std::runtime_error);
    EXPECT_FALSE(renderer.SupportsDepthStencil());
}

TEST(PixiJsRendererThrowNo3D, VertexAndIndexBufferCreationThrows)
{
    PixiJsRenderer renderer(TestArgs());
    EXPECT_THROW(renderer.CreateVertexBuffer(3), std::runtime_error);
    EXPECT_THROW(renderer.CreateIndexBuffer16(3), std::runtime_error);
    EXPECT_THROW(renderer.CreateIndexBuffer32(3), std::runtime_error);
}

TEST(PixiJsRendererThrowNo3D, DrawCallsThrow)
{
    PixiJsRenderer renderer(TestArgs());
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
    PixiJsRenderer renderer(TestArgs());
    EXPECT_EQ(renderer.CreateOcclusionQuery(), nullptr);
    EXPECT_EQ(renderer.CreateTexture3D(4, 4, 4, false, 0), nullptr);
    EXPECT_EQ(renderer.CreateTextureCube(4, false, 0), nullptr);
    EXPECT_EQ(renderer.CreateRenderTargetCube(4, 0), nullptr);
    EXPECT_EQ(renderer.CreateEffectRenderer("", ""), nullptr);
}

TEST(PixiJsRendererCapability, ReportsAdditiveBlendingOnlyIn2DOnlyV1Scope)
{
    PixiJsRenderer renderer(TestArgs());
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

// plan_pixijs.md PIXIJS-45: SetTransformMatrix no longer throws for a non-identity matrix -- it
// applies the transform for real (verified in a real browser, cna_test_pixijs_smoke frame 10). This
// test only checks the pure C++ setter doesn't throw; the real transform math is EM_JS/browser-only
// and isn't exercised by this native-buildable test.
TEST(PixiJsSpriteBatchRendererTest, NonIdentityTransformDoesNotThrow)
{
    PixiJsSpriteBatchRenderer batch;
    Matrix m = Matrix::getIdentityProperty();
    m.M11 = 2.0f;
    EXPECT_NO_THROW(batch.SetTransformMatrix(m));
    EXPECT_NO_THROW(batch.SetTransformMatrix(Matrix::getIdentityProperty()));
}

// --- platform-neutral creation contract (plan_platform.md PLAT-58/PLAT-61) -----------------------
//
// PIXIJS was authored before the platform abstraction landed and took a raw windowing-library
// window pointer plus two native-handle accessors on its public interface. These four cases pin
// the migrated contract so the coupling cannot come back unnoticed: the renderer is driven
// entirely by the surface snapshot GraphicsDevice hands it, and it never asks a native toolkit
// anything.

TEST(PixiJsRendererSurfaceContract, RefusesConstructionWithoutAPlatformWindow)
{
    GraphicsRendererCreateArgs args = TestArgs();
    args.surface.windowId = 0;
    EXPECT_THROW(PixiJsRenderer{args}, std::runtime_error);
}

TEST(PixiJsRendererSurfaceContract, ViewportComesFromTheSurfaceSnapshot)
{
    GraphicsRendererCreateArgs args = TestArgs();
    args.surface.drawableSize = {800, 400};
    args.virtualWidth = 0;
    args.virtualHeight = 240;
    PixiJsRenderer renderer(args);

    int width = 0;
    int height = 0;
    renderer.GetViewportSize(width, height);
    // FixedHeightDynamicWidth: the height is the requested 240 and the width follows the surface's
    // 2:1 aspect ratio, with no windowing-library query anywhere in the path.
    EXPECT_EQ(height, 240);
    EXPECT_EQ(width, 480);
}

TEST(PixiJsRendererSurfaceContract, DisplayScaleSeparatesLogicalFromPhysicalPixels)
{
    GraphicsRendererCreateArgs args = TestArgs();
    args.surface.drawableSize = {1280, 720};
    args.surface.displayScale = 2.0f;
    args.virtualWidth = 0;
    args.virtualHeight = 0;
    PixiJsRenderer renderer(args);

    int width = 0;
    int height = 0;
    renderer.GetViewportSize(width, height);
    EXPECT_EQ(width, 640);
    EXPECT_EQ(height, 360);
}

TEST(PixiJsRendererSurfaceContract, OnSurfaceChangedAdoptsResizesAndRejectsAForeignWindow)
{
    PixiJsRenderer renderer(TestArgs());

    RendererSurfaceInfo resized = TestArgs().surface;
    resized.drawableSize = {320, 160};
    ASSERT_NO_THROW(renderer.OnSurfaceChanged(resized));

    int width = 0;
    int height = 0;
    renderer.GetViewportSize(width, height);
    EXPECT_EQ(height, 64);
    EXPECT_EQ(width, 128);

    RendererSurfaceInfo foreign = resized;
    foreign.windowId = 2;
    EXPECT_THROW(renderer.OnSurfaceChanged(foreign), std::runtime_error);
}
#endif
