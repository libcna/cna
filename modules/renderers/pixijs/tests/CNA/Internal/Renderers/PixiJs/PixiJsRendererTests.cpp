// SPDX-License-Identifier: MS-PL
//
// plans/plan_pixijs.md PIXIJS-80: structural GTest coverage for everything on the PIXIJS renderer that
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
#include "System/NotSupportedException.hpp"

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::PixiJs;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;

namespace
{
    // plans/plan_platform.md PLAT-58/PLAT-61: the renderer receives a platform-neutral surface snapshot,
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

// plans/plan_pixijs.md PIXIJS-52: any non-preset Blend/BlendFunction combination gets a real, generic
// Custom classification instead of a throw. PIXIJS-87 then renders EVERY state -- preset or not --
// from its literal factors, which the browser suite verifies with real pixels.
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

// plans/plan_pixijs.md PIXIJS-52: XnaBlendToGlFactor/XnaBlendFunctionToGlEquation are the pure-function
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

// PIXIJS-90: the sampler mappings are pure functions with a real domain, so an out-of-range
// enumerator is an error rather than a silent fallback to Clamp/Point -- which used to mean a
// caller could get a sampler state it never asked for and never learn about it.
TEST(PixiJsSamplerMapping, AddressModesMapToRealWebGLWrapConstants)
{
    EXPECT_EQ(TextureAddressModeToPixiWrapMode(0), 10497); // Wrap   -> gl.REPEAT
    EXPECT_EQ(TextureAddressModeToPixiWrapMode(1), 33071); // Clamp  -> gl.CLAMP_TO_EDGE
    EXPECT_EQ(TextureAddressModeToPixiWrapMode(2), 33648); // Mirror -> gl.MIRRORED_REPEAT
    EXPECT_THROW(TextureAddressModeToPixiWrapMode(3), std::runtime_error);
    EXPECT_THROW(TextureAddressModeToPixiWrapMode(-1), std::runtime_error);
}

TEST(PixiJsSamplerMapping, TextureFilterMagnificationGroupingCoversEveryEnumerator)
{
    EXPECT_TRUE(TextureFilterIsLinear(0));  // Linear
    EXPECT_FALSE(TextureFilterIsLinear(1)); // Point
    EXPECT_TRUE(TextureFilterIsLinear(2));  // Anisotropic
    EXPECT_TRUE(TextureFilterIsLinear(3));  // LinearMipPoint
    EXPECT_FALSE(TextureFilterIsLinear(4)); // PointMipLinear
    EXPECT_FALSE(TextureFilterIsLinear(5)); // MinLinearMagPointMipLinear
    EXPECT_FALSE(TextureFilterIsLinear(6)); // MinLinearMagPointMipPoint
    EXPECT_TRUE(TextureFilterIsLinear(7));  // MinPointMagLinearMipLinear
    EXPECT_TRUE(TextureFilterIsLinear(8));  // MinPointMagLinearMipPoint
    EXPECT_THROW(TextureFilterIsLinear(9), std::runtime_error);
}

// PIXIJS-89: BlendState.MultiSampleMask. Every mask that leaves coverage sample 0 enabled behaves
// exactly like the all-ones default on this renderer's single-sample targets, so it is accepted;
// one that disables sample 0 asks for something with no single-sample equivalent and is rejected
// rather than silently ignored.
TEST(PixiJsBlendWriteState, AcceptsEveryMultiSampleMaskThatKeepsSampleZero)
{
    PixiJsRenderer renderer(TestArgs());
    BlendWriteState writeState;
    EXPECT_NO_THROW(renderer.ApplyBlendState(0, 0, 5, 5, 0, 0, writeState));
    writeState.multiSampleMask = 0x00000001u;
    EXPECT_NO_THROW(renderer.ApplyBlendState(0, 0, 5, 5, 0, 0, writeState));
    writeState.multiSampleMask = 0x0000000Fu;
    EXPECT_NO_THROW(renderer.ApplyBlendState(0, 0, 5, 5, 0, 0, writeState));
}

TEST(PixiJsBlendWriteState, RejectsAMultiSampleMaskThatDisablesSampleZero)
{
    PixiJsRenderer renderer(TestArgs());
    BlendWriteState writeState;
    writeState.multiSampleMask = 0xFFFFFFFEu;
    EXPECT_THROW(renderer.ApplyBlendState(0, 0, 5, 5, 0, 0, writeState), System::NotSupportedException);
    writeState.multiSampleMask = 0u;
    EXPECT_THROW(renderer.ApplyBlendState(0, 0, 5, 5, 0, 0, writeState), System::NotSupportedException);
}

// PIXIJS-89: a colour write mask is honoured, not rejected -- it reaches gl.colorMask, which the
// browser suite verifies with real pixels. Slots 1..3 describe MRT outputs this renderer never
// binds, so a non-default value there is inapplicable rather than an error; SetRenderTargets is
// where an MRT request is actually refused (below).
TEST(PixiJsBlendWriteState, AcceptsEveryColorWriteChannelCombination)
{
    PixiJsRenderer renderer(TestArgs());
    BlendWriteState writeState;
    for (int channels = 0; channels <= 15; ++channels)
    {
        writeState.colorWriteChannels[0] = channels;
        EXPECT_NO_THROW(renderer.ApplyBlendState(0, 0, 5, 5, 0, 0, writeState)) << "channels=" << channels;
    }
    writeState.colorWriteChannels[0] = 15;
    writeState.colorWriteChannels[1] = 1;
    writeState.colorWriteChannels[2] = 2;
    writeState.colorWriteChannels[3] = 4;
    EXPECT_NO_THROW(renderer.ApplyBlendState(0, 0, 5, 5, 0, 0, writeState));
}

TEST(PixiJsRendererThrowNo3D, MultipleRenderTargetsAreRejected)
{
    PixiJsRenderer renderer(TestArgs());
    const RenderTargetBindingDescriptor bindings[2] = {
        RenderTargetBindingDescriptor::ForRenderTarget2D(nullptr, 0, 4, 4, 0),
        RenderTargetBindingDescriptor::ForRenderTarget2D(nullptr, 0, 4, 4, 0),
    };
    EXPECT_THROW(renderer.SetRenderTargets(bindings, 2), std::runtime_error);
}

TEST(PixiJsSpriteBatchRendererTest, NullCustomEffectDoesNotThrow)
{
    PixiJsSpriteBatchRenderer batch;
    EXPECT_NO_THROW(batch.SetCustomEffect(nullptr));
}

// PIXIJS-90: a PIXI.BaseTexture carries ONE wrapMode. A mixed per-axis request used to be stored
// and then half-applied (AddressU won silently); it is now refused, so a caller cannot be handed a
// sampler state it did not ask for.
TEST(PixiJsSpriteBatchRendererTest, MatchingAddressModesAreAccepted)
{
    PixiJsSpriteBatchRenderer batch;
    EXPECT_NO_THROW(batch.SetSamplerAddressMode(0, 0)); // Wrap/Wrap
    EXPECT_NO_THROW(batch.SetSamplerAddressMode(1, 1)); // Clamp/Clamp
    EXPECT_NO_THROW(batch.SetSamplerAddressMode(2, 2)); // Mirror/Mirror
}

TEST(PixiJsSpriteBatchRendererTest, MixedAddressModesAreRejected)
{
    PixiJsSpriteBatchRenderer batch;
    EXPECT_THROW(batch.SetSamplerAddressMode(0, 1), System::NotSupportedException);
    EXPECT_THROW(batch.SetSamplerAddressMode(1, 2), System::NotSupportedException);
}

TEST(PixiJsSpriteBatchRendererTest, InvalidSamplerEnumeratorsAreRejected)
{
    PixiJsSpriteBatchRenderer batch;
    EXPECT_THROW(batch.SetSamplerAddressMode(7, 7), std::runtime_error);
    EXPECT_THROW(batch.SetSamplerFilter(42), std::runtime_error);
}

// plans/plan_pixijs.md PIXIJS-45: SetTransformMatrix no longer throws for a non-identity matrix -- it
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

// --- platform-neutral creation contract (plans/plan_platform.md PLAT-58/PLAT-61) -----------------------
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

// PIXIJS-93: a caller building a resize snapshot needs the identity the renderer was created with;
// inventing one is exactly what OnSurfaceChanged refuses above.
TEST(PixiJsRendererSurfaceContract, ExposesTheSurfaceItIsDriving)
{
    GraphicsRendererCreateArgs args = TestArgs();
    args.surface.drawableSize = {800, 400};
    PixiJsRenderer renderer(args);

    EXPECT_EQ(renderer.GetSurfaceInfo().windowId, args.surface.windowId);
    EXPECT_EQ(renderer.GetSurfaceInfo().drawableSize.width, 800);
    EXPECT_EQ(renderer.GetSurfaceInfo().drawableSize.height, 400);

    RendererSurfaceInfo resized = renderer.GetSurfaceInfo();
    resized.drawableSize = {96, 48};
    ASSERT_NO_THROW(renderer.OnSurfaceChanged(resized));
    EXPECT_EQ(renderer.GetSurfaceInfo().drawableSize.width, 96);
    EXPECT_EQ(renderer.GetSurfaceInfo().drawableSize.height, 48);
}
#endif
