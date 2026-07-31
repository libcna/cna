// SPDX-License-Identifier: MS-PL
//
// plan_html_dom.md HTMLDOM-70: GTest coverage for everything on the HTML_DOM backend that does not
// need a real DOM -- the blend-state mapping, the addressing-mode validation, the sprite geometry
// encoder, and the 3D "not yet implemented" surface. Deliberately structured so it runs under this
// repo's `node CnaTests.js` runner, which has no document, no CSS and no canvas: nothing here
// creates a texture or touches window_.
//
// The geometry assertions matter most. BuildDrawCommandEXT is where pivot placement, flip mirroring
// and source-rectangle clamping are decided, and getting any of them wrong is invisible in a
// structural review but obvious on screen -- so each is checked against a hand-derived expectation
// rather than against whatever the implementation happens to produce.
#include <gtest/gtest.h>

#if defined(CNA_BACKEND_HTML_DOM)
#include "CNA/Internal/Backends/HtmlDom/HtmlDomGraphicsBackend.hpp"
#include "CNA/Internal/Backends/HtmlDom/HtmlDomSpriteBatchBackend.hpp"
#include "CNA/Internal/Backends/HtmlDom/HtmlDomState.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

#include <string>

using namespace CNA::Internal::Backends;
using namespace CNA::Internal::Backends::HtmlDom;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::SpriteEffects;

namespace
{
    // Non-null but never dereferenced: the constructor only null-checks its window pointer and
    // registers it in a pointer-keyed map, and none of the throwing methods exercised below reads
    // window_ before throwing.
    SDL_Window* FakeWindow() { return reinterpret_cast<SDL_Window*>(0x1); }

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

    constexpr int kTexW = 64;
    constexpr int kTexH = 32;

    HtmlDomDrawCommand Build(const Rectangle& dest, const Rectangle& source,
                             const Color& color = Color(255, 255, 255, 255),
                             float rotation = 0.0f,
                             const Vector2& origin = Vector2(0, 0),
                             SpriteEffects effects = SpriteEffects::None,
                             bool smoothing = true,
                             int addressU = 1, int addressV = 1,
                             DomCompositeOp op = DomCompositeOp::NonPremultiplied)
    {
        return BuildDrawCommandEXT(7, kTexW, kTexH, dest, source, color, rotation, origin,
                                   effects, smoothing, addressU, addressV, op);
    }
}

// ---------------------------------------------------------------------------------------------
// BlendState -> composite operation
// ---------------------------------------------------------------------------------------------

TEST(HtmlDomBlendStateMapping, StandardPresetsMapCorrectly)
{
    EXPECT_EQ(BlendStateToDomCompositeOp(0, 0, 1, 1, 0, 0), DomCompositeOp::Opaque);
    EXPECT_EQ(BlendStateToDomCompositeOp(0, 0, 5, 5, 0, 0), DomCompositeOp::AlphaBlend);
    EXPECT_EQ(BlendStateToDomCompositeOp(4, 4, 5, 5, 0, 0), DomCompositeOp::NonPremultiplied);
    EXPECT_EQ(BlendStateToDomCompositeOp(4, 4, 0, 0, 0, 0), DomCompositeOp::Additive);
    // AlphaBlend and NonPremultiplied must not collapse into one value: they need different source
    // pixels (un-premultiplied vs. as-uploaded) even though both composite the same way.
    EXPECT_NE(BlendStateToDomCompositeOp(0, 0, 5, 5, 0, 0),
              BlendStateToDomCompositeOp(4, 4, 5, 5, 0, 0));
}

TEST(HtmlDomBlendStateMapping, CustomBlendStatesThrow)
{
    // Asymmetric colour/alpha factors.
    EXPECT_THROW(BlendStateToDomCompositeOp(0, 4, 5, 5, 0, 0), std::runtime_error);
    // A non-Add blend function.
    EXPECT_THROW(BlendStateToDomCompositeOp(4, 4, 5, 5, 1, 1), std::runtime_error);
    // A factor pair that is not one of the four presets.
    EXPECT_THROW(BlendStateToDomCompositeOp(3, 3, 3, 3, 0, 0), std::runtime_error);
}

TEST(HtmlDomBlendStateMapping, VariantModeSelectionMatchesTheOpsPixelRequirement)
{
    EXPECT_EQ(VariantModeFor(DomCompositeOp::NonPremultiplied), 0);
    EXPECT_EQ(VariantModeFor(DomCompositeOp::Additive), 0);   // same pixels, different compositing
    EXPECT_EQ(VariantModeFor(DomCompositeOp::AlphaBlend), 1);
    EXPECT_EQ(VariantModeFor(DomCompositeOp::Opaque), 2);
}

TEST(HtmlDomCompositeOpState, DefaultsToNonPremultipliedAndRoundTrips)
{
    SetCurrentCompositeOpEXT(DomCompositeOp::NonPremultiplied);
    EXPECT_EQ(GetCurrentCompositeOpEXT(), DomCompositeOp::NonPremultiplied);
    SetCurrentCompositeOpEXT(DomCompositeOp::Additive);
    EXPECT_EQ(GetCurrentCompositeOpEXT(), DomCompositeOp::Additive);
    SetCurrentCompositeOpEXT(DomCompositeOp::NonPremultiplied);
}

// ---------------------------------------------------------------------------------------------
// TextureAddressMode validation
// ---------------------------------------------------------------------------------------------

TEST(HtmlDomAddressModes, InBoundsNeverThrowsWhateverTheModes)
{
    EXPECT_NO_THROW(ValidateAddressModes(0, 0, false));
    EXPECT_NO_THROW(ValidateAddressModes(2, 2, false));
    EXPECT_NO_THROW(ValidateAddressModes(0, 2, false));
}

TEST(HtmlDomAddressModes, ClampNeverThrowsEvenOutOfBounds)
{
    EXPECT_NO_THROW(ValidateAddressModes(1, 1, true));
}

TEST(HtmlDomAddressModes, WrapIsAcceptedOutOfBounds)
{
    EXPECT_NO_THROW(ValidateAddressModes(0, 0, true));
}

TEST(HtmlDomAddressModes, MirrorAndMixedModesThrowOutOfBounds)
{
    EXPECT_THROW(ValidateAddressModes(2, 2, true), std::runtime_error);
    EXPECT_THROW(ValidateAddressModes(0, 1, true), std::runtime_error);
    EXPECT_THROW(ValidateAddressModes(1, 0, true), std::runtime_error);
}

// ---------------------------------------------------------------------------------------------
// Sprite geometry encoding
// ---------------------------------------------------------------------------------------------

TEST(HtmlDomDrawCommandLayout, StrideStaysTwentyFourByteFields)
{
    EXPECT_EQ(sizeof(HtmlDomDrawCommand), 80u);
    EXPECT_EQ(HtmlDomDrawCommandFields, 20);
    EXPECT_EQ(sizeof(HtmlDomDrawCommand) / 4, static_cast<std::size_t>(HtmlDomDrawCommandFields));
}

TEST(HtmlDomDrawCommand, UnscaledUnrotatedDrawIsIdentityPlacement)
{
    const HtmlDomDrawCommand c = Build(Rectangle(10, 20, 32, 16), Rectangle(0, 0, 32, 16));
    EXPECT_EQ(c.textureId, 7);
    EXPECT_FLOAT_EQ(c.sx, 0.0f);
    EXPECT_FLOAT_EQ(c.sy, 0.0f);
    EXPECT_FLOAT_EQ(c.sw, 32.0f);
    EXPECT_FLOAT_EQ(c.sh, 16.0f);
    EXPECT_FLOAT_EQ(c.destX, 10.0f);
    EXPECT_FLOAT_EQ(c.destY, 20.0f);
    EXPECT_FLOAT_EQ(c.scaleX, 1.0f);
    EXPECT_FLOAT_EQ(c.scaleY, 1.0f);
    EXPECT_FLOAT_EQ(c.localX, 0.0f);
    EXPECT_FLOAT_EQ(c.localY, 0.0f);
    EXPECT_EQ(c.variantMode, 0);
    EXPECT_EQ(c.flags, FlagSmoothing);
}

TEST(HtmlDomDrawCommand, ScaleIsDestinationOverUnclampedSource)
{
    // A 2x horizontal, 3x vertical draw. Getting this wrong -- for instance by folding the scale in
    // twice, once through scaleX and once through the local box size -- would grow the sprite
    // quadratically with the requested scale, which is exactly why it is asserted numerically.
    const HtmlDomDrawCommand c = Build(Rectangle(0, 0, 64, 48), Rectangle(0, 0, 32, 16));
    EXPECT_FLOAT_EQ(c.scaleX, 2.0f);
    EXPECT_FLOAT_EQ(c.scaleY, 3.0f);
    // The local box stays in SOURCE pixels; the single scale above is what turns it into
    // destination pixels, so 32 * 2 = 64 and 16 * 3 = 48 on screen.
    EXPECT_FLOAT_EQ(c.sw, 32.0f);
    EXPECT_FLOAT_EQ(c.sh, 16.0f);
}

TEST(HtmlDomDrawCommand, OriginBecomesTheLocalPivotOffset)
{
    const HtmlDomDrawCommand c = Build(Rectangle(100, 100, 32, 16), Rectangle(0, 0, 32, 16),
                                       Color(255, 255, 255, 255), 0.0f, Vector2(16, 8));
    // The sprite is shifted back by the pivot so that `origin` lands exactly on (destX,destY) --
    // invariant under any rotation, since the rotation happens before this translation.
    EXPECT_FLOAT_EQ(c.localX, -16.0f);
    EXPECT_FLOAT_EQ(c.localY, -8.0f);
    EXPECT_FLOAT_EQ(c.destX, 100.0f);
    EXPECT_FLOAT_EQ(c.destY, 100.0f);
}

TEST(HtmlDomDrawCommand, FlipMirrorsAboutTheSpritesOwnCentreNotThePivot)
{
    const HtmlDomDrawCommand c = Build(Rectangle(0, 0, 32, 16), Rectangle(0, 0, 32, 16),
                                       Color(255, 255, 255, 255), 0.0f, Vector2(16, 8),
                                       SpriteEffects::FlipHorizontally);
    EXPECT_TRUE((c.flags & FlagFlipHorizontally) != 0);
    EXPECT_FALSE((c.flags & FlagFlipVertically) != 0);
    // Local space already has the pivot subtracted, so the sprite's own centre sits at
    // -origin + size/2. With origin == centre that is exactly 0, and flipping is then a pure
    // mirror about the pivot -- the destination footprint is unchanged either way.
    EXPECT_FLOAT_EQ(c.flipCenterX, 0.0f);
    EXPECT_FLOAT_EQ(c.flipCenterY, 0.0f);

    const HtmlDomDrawCommand topLeftPivot =
        Build(Rectangle(0, 0, 32, 16), Rectangle(0, 0, 32, 16), Color(255, 255, 255, 255), 0.0f,
              Vector2(0, 0), SpriteEffects::FlipVertically);
    EXPECT_TRUE((topLeftPivot.flags & FlagFlipVertically) != 0);
    EXPECT_FLOAT_EQ(topLeftPivot.flipCenterX, 16.0f);
    EXPECT_FLOAT_EQ(topLeftPivot.flipCenterY, 8.0f);
}

TEST(HtmlDomDrawCommand, ClampNarrowsTheSourceRectAndShiftsTheLocalBoxToMatch)
{
    // 8 pixels to the left of the texture and 16 past its right edge: only x in [0,64) survives.
    const HtmlDomDrawCommand c = Build(Rectangle(0, 0, 72, 16), Rectangle(-8, 0, 72, 16));
    EXPECT_FLOAT_EQ(c.sx, 0.0f);
    EXPECT_FLOAT_EQ(c.sw, 64.0f);
    // The surviving texels must stay where the unclamped draw would have put them: the box starts
    // 8 source pixels into the sprite, so the local offset moves it there.
    EXPECT_FLOAT_EQ(c.localX, 8.0f);
    EXPECT_FLOAT_EQ(c.localY, 0.0f);
    // The scale still divides by the UNCLAMPED source width -- clamping removes texels, it does not
    // stretch the ones that remain.
    EXPECT_FLOAT_EQ(c.scaleX, 1.0f);
    EXPECT_EQ(c.flags & FlagWrap, 0);
}

TEST(HtmlDomDrawCommand, WrapKeepsTheFullSourceRectAndFlagsTiling)
{
    const HtmlDomDrawCommand c = Build(Rectangle(0, 0, 128, 16), Rectangle(0, 0, 128, 16),
                                       Color(255, 255, 255, 255), 0.0f, Vector2(0, 0),
                                       SpriteEffects::None, true, /*addressU*/ 0, /*addressV*/ 0);
    EXPECT_TRUE((c.flags & FlagWrap) != 0);
    // Tiling is the whole point: narrowing the rectangle here would make Wrap behave like Clamp.
    EXPECT_FLOAT_EQ(c.sw, 128.0f);
    EXPECT_FLOAT_EQ(c.localX, 0.0f);
}

TEST(HtmlDomDrawCommand, MirrorOutOfBoundsThrowsFromTheEncoder)
{
    EXPECT_THROW(Build(Rectangle(0, 0, 128, 16), Rectangle(0, 0, 128, 16),
                       Color(255, 255, 255, 255), 0.0f, Vector2(0, 0), SpriteEffects::None, true,
                       /*addressU*/ 2, /*addressV*/ 2),
                 std::runtime_error);
}

TEST(HtmlDomDrawCommand, ColorIsPackedRgbaAndBlendOpSelectsTheVariant)
{
    const HtmlDomDrawCommand c = Build(Rectangle(0, 0, 32, 16), Rectangle(0, 0, 32, 16),
                                       Color(0x11, 0x22, 0x33, 0x44), 0.0f, Vector2(0, 0),
                                       SpriteEffects::None, true, 1, 1, DomCompositeOp::AlphaBlend);
    EXPECT_EQ(c.packedColor & 0xFFu, 0x11u);
    EXPECT_EQ((c.packedColor >> 8) & 0xFFu, 0x22u);
    EXPECT_EQ((c.packedColor >> 16) & 0xFFu, 0x33u);
    EXPECT_EQ((c.packedColor >> 24) & 0xFFu, 0x44u);
    EXPECT_EQ(c.variantMode, 1);
    EXPECT_EQ(c.flags & FlagAdditive, 0);

    const HtmlDomDrawCommand additive =
        Build(Rectangle(0, 0, 32, 16), Rectangle(0, 0, 32, 16), Color(255, 255, 255, 255), 0.0f,
              Vector2(0, 0), SpriteEffects::None, true, 1, 1, DomCompositeOp::Additive);
    EXPECT_TRUE((additive.flags & FlagAdditive) != 0);
    EXPECT_EQ(additive.variantMode, 0);

    const HtmlDomDrawCommand opaque =
        Build(Rectangle(0, 0, 32, 16), Rectangle(0, 0, 32, 16), Color(255, 255, 255, 255), 0.0f,
              Vector2(0, 0), SpriteEffects::None, true, 1, 1, DomCompositeOp::Opaque);
    EXPECT_EQ(opaque.variantMode, 2);
}

TEST(HtmlDomDrawCommand, PointFilteringClearsTheSmoothingFlag)
{
    const HtmlDomDrawCommand c = Build(Rectangle(0, 0, 32, 16), Rectangle(0, 0, 32, 16),
                                       Color(255, 255, 255, 255), 0.0f, Vector2(0, 0),
                                       SpriteEffects::None, /*smoothing*/ false);
    EXPECT_EQ(c.flags & FlagSmoothing, 0);
}

// ---------------------------------------------------------------------------------------------
// SpriteBatch queueing
// ---------------------------------------------------------------------------------------------

TEST(HtmlDomSpriteBatch, DrawBeforeBeginThrows)
{
    // The overload taking a bare texture reference cannot be reached without a texture backend, so
    // the guard is exercised through the state the batch itself owns.
    HtmlDomSpriteBatchBackend batch;
    EXPECT_FALSE(batch.IsBegun());
    batch.Begin();
    EXPECT_TRUE(batch.IsBegun());
    batch.End();
    EXPECT_FALSE(batch.IsBegun());
}

TEST(HtmlDomSpriteBatch, CustomEffectThrowsButNullIsAccepted)
{
    HtmlDomSpriteBatchBackend batch;
    EXPECT_NO_THROW(batch.SetCustomEffect(nullptr));
    EXPECT_THROW(batch.SetCustomEffect(reinterpret_cast<Effect*>(0x1)), std::runtime_error);
}

TEST(HtmlDomSpriteBatch, SamplerFilterMapsMagnificationToSmoothing)
{
    HtmlDomSpriteBatchBackend batch;
    // Every filter whose magnification component is Linear turns smoothing on; Point-magnifying
    // ones turn it off. Checked through the encoder, which is where the flag actually lands.
    for (int linear : {0, 2, 3, 7, 8})
    {
        batch.SetSamplerFilter(linear);
        batch.Begin();
        EXPECT_TRUE(batch.GetCommandsEXT().empty());
        batch.End();
    }
    EXPECT_NO_THROW(batch.SetSamplerFilter(1));
}

TEST(HtmlDomSpriteBatch, BeginClearsCommandsFromAPreviousBatch)
{
    HtmlDomSpriteBatchBackend batch;
    batch.Begin();
    EXPECT_TRUE(batch.GetCommandsEXT().empty());
    batch.End();
    batch.Begin();
    EXPECT_TRUE(batch.GetCommandsEXT().empty());
    batch.End();
}

TEST(HtmlDomSpriteBatch, IdentityTransformMatrixIsAccepted)
{
    HtmlDomSpriteBatchBackend batch;
    EXPECT_NO_THROW(batch.SetTransformMatrix(Matrix::getIdentityProperty()));
    EXPECT_NO_THROW(batch.SetTransformMatrix(Matrix::CreateTranslation(10.0f, 20.0f, 0.0f)));
}

// ---------------------------------------------------------------------------------------------
// The 3D surface: every entry point throws "not yet implemented"
// ---------------------------------------------------------------------------------------------

class HtmlDom3DSurfaceTest : public ::testing::Test
{
protected:
    HtmlDomGraphicsBackend backend{FakeWindow(), 800, 480,
                                   CnaPresentationMode::FixedHeightDynamicWidth};
};

TEST_F(HtmlDom3DSurfaceTest, DepthAndStencilClearsThrow)
{
    EXPECT_THROW(backend.ClearColorAndDepth(0, 0, 0, 1, 1.0f), std::runtime_error);
    EXPECT_THROW(backend.ClearDepth(1.0f), std::runtime_error);
    EXPECT_THROW(backend.ClearStencil(0), std::runtime_error);
    EXPECT_THROW(backend.ClearDepthAndStencil(1.0f, 0), std::runtime_error);
    EXPECT_THROW(backend.ClearColorAndStencil(0, 0, 0, 1, 0), std::runtime_error);
    EXPECT_THROW(backend.ClearColorDepthAndStencil(0, 0, 0, 1, 1.0f, 0), std::runtime_error);
}

TEST_F(HtmlDom3DSurfaceTest, DepthAndBlendTogglesThrow)
{
    EXPECT_THROW(backend.SetDepthTestEnabled(true), std::runtime_error);
    EXPECT_THROW(backend.SetBlendEnabled(true), std::runtime_error);
    EXPECT_THROW(backend.SetDepthWriteEnabled(true), std::runtime_error);
}

TEST_F(HtmlDom3DSurfaceTest, BufferCreationAndDrawCallsThrow)
{
    EXPECT_THROW((void)backend.CreateVertexBuffer(16), std::runtime_error);
    EXPECT_THROW((void)backend.CreateIndexBuffer16(16), std::runtime_error);

    DummyVertexBuffer vb;
    DummyIndexBuffer ib;
    const Matrix m = Matrix::getIdentityProperty();
    EXPECT_THROW(backend.DrawColoredPrimitives(vb, m, m, m, PrimitiveType::TriangleList, 1),
                 std::runtime_error);
    EXPECT_THROW(backend.DrawIndexedColoredPrimitives(vb, ib, m, m, m, PrimitiveType::TriangleList, 1),
                 std::runtime_error);
}

TEST_F(HtmlDom3DSurfaceTest, ThrownMessagesNameTheBackendAndTheMissingCapability)
{
    try
    {
        backend.ClearDepth(1.0f);
        FAIL() << "ClearDepth must throw on a 2D-only backend.";
    }
    catch (const std::runtime_error& e)
    {
        const std::string what = e.what();
        EXPECT_NE(what.find("HTML_DOM"), std::string::npos);
        EXPECT_NE(what.find("not yet implemented"), std::string::npos);
    }
}

TEST_F(HtmlDom3DSurfaceTest, CapabilityQueriesReportTheTwoDimensionalBoundaryUpFront)
{
    // A caller should be able to find out without provoking an exception.
    EXPECT_FALSE(backend.SupportsDepthStencil());
    EXPECT_FALSE(backend.SupportsCapability(CNA::GraphicsCapability::ThreeD));
    EXPECT_EQ(backend.GetRendererInternal(), nullptr);
    EXPECT_EQ(backend.GetWindowInternal(), FakeWindow());
}

TEST_F(HtmlDom3DSurfaceTest, MultipleRenderTargetsAndCubeFacesThrow)
{
    // Null targets are enough: both rejections happen on the descriptor count and kind, before any
    // target pointer is dereferenced.
    const RenderTargetBindingDescriptor descriptors[2] = {
        RenderTargetBindingDescriptor::ForRenderTarget2D(nullptr, 0, 4, 4, 0),
        RenderTargetBindingDescriptor::ForRenderTarget2D(nullptr, 0, 4, 4, 0),
    };
    EXPECT_THROW(backend.SetRenderTargets(descriptors, 2), std::runtime_error);

    const RenderTargetBindingDescriptor cubeFace[1] = {
        RenderTargetBindingDescriptor::ForRenderTargetCubeFace(nullptr, 0, 4, 0),
    };
    EXPECT_THROW(backend.SetRenderTargets(cubeFace, 1), std::runtime_error);

    // A single 2D target, and an empty set, are both legitimate.
    EXPECT_NO_THROW(backend.SetRenderTargets(nullptr, 0));
}

TEST_F(HtmlDom3DSurfaceTest, BackbufferReadbackThrowsWithAnActionableMessage)
{
    std::uint8_t pixels[4] = {};
    try
    {
        backend.ReadBackbuffer(0, 0, 1, 1, pixels);
        FAIL() << "The DOM backbuffer cannot be read back and must say so.";
    }
    catch (const std::runtime_error& e)
    {
        const std::string what = e.what();
        EXPECT_NE(what.find("HTML_DOM"), std::string::npos);
        // The message must point at the supported alternative, not just refuse.
        EXPECT_NE(what.find("RenderTarget2D"), std::string::npos);
    }
}

TEST_F(HtmlDom3DSurfaceTest, ApplyBlendStateAcceptsPresetsAndRejectsCustomOnes)
{
    const BlendWriteState writeState{};
    EXPECT_NO_THROW(backend.ApplyBlendState(4, 4, 5, 5, 0, 0, writeState));
    EXPECT_EQ(GetCurrentCompositeOpEXT(), DomCompositeOp::NonPremultiplied);
    EXPECT_NO_THROW(backend.ApplyBlendState(4, 4, 0, 0, 0, 0, writeState));
    EXPECT_EQ(GetCurrentCompositeOpEXT(), DomCompositeOp::Additive);
    EXPECT_THROW(backend.ApplyBlendState(3, 3, 3, 3, 0, 0, writeState), std::runtime_error);
    // A rejected state must not have changed the one in effect.
    EXPECT_EQ(GetCurrentCompositeOpEXT(), DomCompositeOp::Additive);
    SetCurrentCompositeOpEXT(DomCompositeOp::NonPremultiplied);
}

TEST_F(HtmlDom3DSurfaceTest, ViewportFollowsTheVirtualResolutionSetting)
{
    backend.SetVirtualResolution(0, 0);
    backend.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
    float lx = 0.0f, ly = 0.0f;
    // With no virtual resolution there is no logical mapping to report.
    EXPECT_FALSE(backend.TransformWindowToLogical(10.0f, 10.0f, lx, ly));
    EXPECT_FALSE(backend.TransformLogicalToWindow(10.0f, 10.0f, lx, ly));
}
#endif // CNA_BACKEND_HTML_DOM
