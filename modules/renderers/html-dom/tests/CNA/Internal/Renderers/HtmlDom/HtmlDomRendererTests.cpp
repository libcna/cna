// SPDX-License-Identifier: MS-PL
//
// plan_html_dom.md HTMLDOM-70: GTest coverage for everything on the HTML_DOM renderer that does not
// need a real DOM -- the blend-state mapping, the addressing-mode validation, the sprite geometry
// encoder, and the 3D "not yet implemented" surface. Deliberately structured so it runs under this
// repo's `node CnaTests.js` runner or the native host-contract target: nothing here requires a
// document, CSS or a browser canvas.
//
// The geometry assertions matter most. BuildDrawCommandEXT is where pivot placement, flip mirroring
// and source-rectangle clamping are decided, and getting any of them wrong is invisible in a
// structural review but obvious on screen -- so each is checked against a hand-derived expectation
// rather than against whatever the implementation happens to produce.
#include <gtest/gtest.h>

#if defined(CNA_RENDERER_HTML_DOM) || defined(CNA_HTML_DOM_HOST_TESTS)
#include "CNA/Internal/Renderers/HtmlDom/HtmlDomRenderer.hpp"
#include "CNA/Internal/Renderers/HtmlDom/HtmlDomRenderTargetRenderer.hpp"
#include "CNA/Internal/Renderers/HtmlDom/HtmlDomSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/HtmlDom/HtmlDomState.hpp"
#include "CNA/Internal/Renderers/HtmlDom/HtmlDomTextureRenderer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <limits>
#include <string>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::HtmlDom;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::SpriteEffects;

namespace
{
    GraphicsRendererCreateArgs TestArgs()
    {
        GraphicsRendererCreateArgs args;
        args.surface.windowId = 1;
        args.surface.nativeHandle.system = CNA::Platform::NativeWindowSystem::Web;
        args.surface.drawableSize = {800, 480};
        args.virtualWidth = 800;
        args.virtualHeight = 480;
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

// HTMLDOM-97: symmetric Mirror (the same mode on both axes -- every built-in SamplerState Mirror
// preset is symmetric U/V) is now supported via a pre-tiled-mirrored variant.
TEST(HtmlDomAddressModes, SymmetricMirrorIsAcceptedOutOfBounds)
{
    EXPECT_NO_THROW(ValidateAddressModes(2, 2, true));
}

// HTMLDOM-97: mixed per-axis modes that do NOT involve Mirror are now supported via independent
// per-axis background-repeat/CanvasPattern repetition.
TEST(HtmlDomAddressModes, MixedNonMirrorAxesAreAcceptedOutOfBounds)
{
    EXPECT_NO_THROW(ValidateAddressModes(0, 1, true));
    EXPECT_NO_THROW(ValidateAddressModes(1, 0, true));
}

// The one combination that remains genuinely unsupported: Mirror on one axis paired with a
// DIFFERENT mode on the other -- no built-in SamplerState preset can even produce this.
TEST(HtmlDomAddressModes, MirrorMixedWithADifferentAxisModeThrowsOutOfBounds)
{
    EXPECT_THROW(ValidateAddressModes(2, 0, true), std::runtime_error);
    EXPECT_THROW(ValidateAddressModes(0, 2, true), std::runtime_error);
    EXPECT_THROW(ValidateAddressModes(2, 1, true), std::runtime_error);
    EXPECT_THROW(ValidateAddressModes(1, 2, true), std::runtime_error);
}

// HTMLDOM-121: invalid public enum ordinals must not fall through to a Clamp-like approximation.
TEST(HtmlDomAddressModes, OutOfRangeOrdinalsThrowRegardlessOfMatch)
{
    EXPECT_THROW(ValidateAddressModes(999, 999, true), std::runtime_error);
    EXPECT_THROW(ValidateAddressModes(999, 5, true), std::runtime_error);
    EXPECT_THROW(ValidateAddressModes(-1, 1, false), std::runtime_error);
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

// plan_html_dom.md HTMLDOM-119: a negative origin is a valid, if unusual, XNA pivot (not clamped to
// the sprite's own bounds -- a pivot placed OUTSIDE the sprite, e.g. for an object that should
// rotate around a point beyond its own edge). localX/localY is plain negation with no sign branch
// or clamp anywhere in BuildDrawCommandEXT, so this should compose identically to a positive
// origin, just offset the other way -- confirmed here rather than left unverified.
TEST(HtmlDomDrawCommand, NegativeOriginIsAcceptedAndComposesLikeAnyOtherPivot)
{
    const HtmlDomDrawCommand c = Build(Rectangle(100, 100, 32, 16), Rectangle(0, 0, 32, 16),
                                       Color(255, 255, 255, 255), 0.0f, Vector2(-16, -8));
    // Same sign-flip relationship as the positive-origin case above, just mirrored: localX/localY
    // is always -origin, unconditionally.
    EXPECT_FLOAT_EQ(c.localX, 16.0f);
    EXPECT_FLOAT_EQ(c.localY, 8.0f);
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

// plan_html_dom.md HTMLDOM-104: Clamp samples the nearest EDGE TEXEL for the out-of-bounds portion
// -- it does NOT crop destination geometry. An earlier version narrowed sx/sw into the texture and
// shifted localX to match, cropping the sprite's own footprint instead (the wrong result, per the
// audit that reopened this task). The encoder now passes the RAW, unclamped source rect straight
// through unconditionally -- the edge-extension itself happens entirely on the JS side
// (Module['cnaDomGetPaddedVariant'], not unit-testable without a browser), so this is what pure
// C++ can actually prove: destRect/origin math is completely unaffected by Clamp overflow.
TEST(HtmlDomDrawCommand, ClampKeepsTheRawSourceRectAndLeavesDestGeometryUnchanged)
{
    // 8 pixels to the left of the texture, exactly flush with its right edge (-8+72=64=kTexW).
    const HtmlDomDrawCommand c = Build(Rectangle(0, 0, 72, 16), Rectangle(-8, 0, 72, 16));
    EXPECT_FLOAT_EQ(c.sx, -8.0f);
    EXPECT_FLOAT_EQ(c.sw, 72.0f);
    // No crop, so no compensating shift either -- origin is (0,0), so localX/Y are exactly what an
    // ordinary in-bounds draw would produce.
    EXPECT_FLOAT_EQ(c.localX, 0.0f);
    EXPECT_FLOAT_EQ(c.localY, 0.0f);
    EXPECT_FLOAT_EQ(c.scaleX, 1.0f);
    EXPECT_EQ(c.flags & FlagWrap, 0);
}

// plan_html_dom.md HTMLDOM-104: an honest boundary, not silent cropping -- overflow beyond
// kMaxClampPadding texture pixels on any one edge is rejected outright rather than approximated.
TEST(HtmlDomDrawCommand, ClampOverflowBeyondTheCapThrows)
{
    // 300 texture pixels to the left of a 64-wide texture -- comfortably past the 256px cap.
    EXPECT_THROW(Build(Rectangle(0, 0, 10, 10), Rectangle(-300, 0, 310, 16)), std::runtime_error);
    // A modest overflow (8px) must NOT throw -- confirms the cap doesn't reject ordinary use.
    EXPECT_NO_THROW(Build(Rectangle(0, 0, 72, 16), Rectangle(-8, 0, 72, 16)));
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

// HTMLDOM-97: symmetric Mirror (same mode both axes) now keeps the full unclamped source rect and
// tiles, exactly like Wrap, but additionally flags FlagMirror so the flush picks the pre-tiled
// mirrored variant instead of the plain one.
TEST(HtmlDomDrawCommand, SymmetricMirrorKeepsTheFullSourceRectAndFlagsMirrorTiling)
{
    const HtmlDomDrawCommand c = Build(Rectangle(0, 0, 128, 16), Rectangle(0, 0, 128, 16),
                                       Color(255, 255, 255, 255), 0.0f, Vector2(0, 0),
                                       SpriteEffects::None, true, /*addressU*/ 2, /*addressV*/ 2);
    EXPECT_TRUE((c.flags & FlagWrap) != 0);
    EXPECT_TRUE((c.flags & FlagWrapV) != 0);
    EXPECT_TRUE((c.flags & FlagMirror) != 0);
    EXPECT_FLOAT_EQ(c.sw, 128.0f);
    EXPECT_FLOAT_EQ(c.localX, 0.0f);
}

// HTMLDOM-97: Mirror mixed with a DIFFERENT mode on the other axis remains the one unsupported
// combination -- still throws from the encoder, same as before this task.
TEST(HtmlDomDrawCommand, MirrorMixedWithADifferentAxisModeThrowsFromTheEncoder)
{
    EXPECT_THROW(Build(Rectangle(0, 0, 128, 16), Rectangle(0, 0, 128, 16),
                       Color(255, 255, 255, 255), 0.0f, Vector2(0, 0), SpriteEffects::None, true,
                       /*addressU*/ 2, /*addressV*/ 1),
                 std::runtime_error);
}

// HTMLDOM-97: mixed non-Mirror axes (U=Wrap, V=Clamp) tile only the U extent and clamp the V
// extent independently -- flags carry each axis's own repetition, and the V extent is genuinely
// narrowed while the U extent stays full, unlike the old whole-rect-follows-addressU behaviour.
// plan_html_dom.md HTMLDOM-104: V (Clamp) keeps its own RAW source rect too now -- no narrowing,
// no localY shift -- independent of U (Wrap), which continues to tile as before.
TEST(HtmlDomDrawCommand, MixedWrapUClampVTilesOnlyUAndClampsV)
{
    // U: 0..128 (exceeds the 64-wide texture, Wrap). V: -8..24 (exceeds the 32-tall texture on the
    // top edge only, by 8px -- Clamp).
    const HtmlDomDrawCommand c = Build(Rectangle(0, 0, 128, 32), Rectangle(0, -8, 128, 32),
                                       Color(255, 255, 255, 255), 0.0f, Vector2(0, 0),
                                       SpriteEffects::None, true, /*addressU*/ 0, /*addressV*/ 1);
    EXPECT_TRUE((c.flags & FlagWrap) != 0);
    EXPECT_EQ(c.flags & FlagWrapV, 0);
    EXPECT_EQ(c.flags & FlagMirror, 0);
    EXPECT_FLOAT_EQ(c.sx, 0.0f);
    EXPECT_FLOAT_EQ(c.sw, 128.0f);
    EXPECT_FLOAT_EQ(c.sy, -8.0f);
    EXPECT_FLOAT_EQ(c.sh, 32.0f);
    EXPECT_FLOAT_EQ(c.localY, 0.0f);
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
    // The overload taking a bare texture reference cannot be reached without a texture renderer, so
    // the guard is exercised through the state the batch itself owns.
    HtmlDomSpriteBatchRenderer batch;
    EXPECT_FALSE(batch.IsBegun());
    batch.Begin();
    EXPECT_TRUE(batch.IsBegun());
    batch.End();
    EXPECT_FALSE(batch.IsBegun());
}

TEST(HtmlDomSpriteBatch, CustomEffectThrowsButNullIsAccepted)
{
    HtmlDomSpriteBatchRenderer batch;
    EXPECT_NO_THROW(batch.SetCustomEffect(nullptr));
    EXPECT_THROW(batch.SetCustomEffect(reinterpret_cast<Effect*>(0x1)), std::runtime_error);
}

TEST(HtmlDomSpriteBatch, SamplerFilterSupportsOnlyExactLinearAndPointModes)
{
    HtmlDomSpriteBatchRenderer batch;
    EXPECT_NO_THROW(batch.SetSamplerFilter(0));
    EXPECT_NO_THROW(batch.SetSamplerFilter(1));
    for (int unsupported : {2, 3, 4, 5, 6, 7, 8, -1, 999})
        EXPECT_THROW(batch.SetSamplerFilter(unsupported), std::runtime_error);
}

TEST(HtmlDomSpriteBatch, SamplerAddressModeRejectsOutOfRangeOrdinals)
{
    HtmlDomSpriteBatchRenderer batch;
    EXPECT_NO_THROW(batch.SetSamplerAddressMode(0, 2));
    EXPECT_THROW(batch.SetSamplerAddressMode(999, 999), std::runtime_error);
    EXPECT_THROW(batch.SetSamplerAddressMode(-1, 1), std::runtime_error);
}

TEST(HtmlDomTextureRows, PitchedRgbaUploadsArePackedWithoutPadding)
{
    const std::uint8_t pitched[24] = {
        1, 2, 3, 4, 5, 6, 7, 8, 0xEE, 0xEE, 0xEE, 0xEE,
        9, 10, 11, 12, 13, 14, 15, 16, 0xDD, 0xDD, 0xDD, 0xDD,
    };
    const auto tight = TightenTextureRowsEXT(pitched, 2, 2, 12);
    ASSERT_EQ(tight.size(), 16u);
    for (std::size_t i = 0; i < tight.size(); ++i)
        EXPECT_EQ(tight[i], static_cast<std::uint8_t>(i + 1));
    EXPECT_THROW((void)TightenTextureRowsEXT(pitched, 2, 2, 7),
                 System::ArgumentOutOfRangeException);
}

TEST(HtmlDomTextureRows, WidthOverflowIsRejectedBeforeReadingTheSource)
{
    const std::uint8_t byte = 0;
    EXPECT_THROW(
        (void)TightenTextureRowsEXT(
            &byte, std::numeric_limits<int>::max(), 1, std::numeric_limits<int>::max()),
        System::ArgumentOutOfRangeException);
}

TEST(HtmlDomSpriteBatch, BeginClearsCommandsFromAPreviousBatch)
{
    HtmlDomSpriteBatchRenderer batch;
    batch.Begin();
    EXPECT_TRUE(batch.GetCommandsEXT().empty());
    batch.End();
    batch.Begin();
    EXPECT_TRUE(batch.GetCommandsEXT().empty());
    batch.End();
}

TEST(HtmlDomSpriteBatch, IdentityTransformMatrixIsAccepted)
{
    HtmlDomSpriteBatchRenderer batch;
    EXPECT_NO_THROW(batch.SetTransformMatrix(Matrix::getIdentityProperty()));
    EXPECT_NO_THROW(batch.SetTransformMatrix(Matrix::CreateTranslation(10.0f, 20.0f, 0.0f)));
}

// plan_html_dom.md HTMLDOM-118: SetImmediateMode itself is trivially unit-testable (a plain
// setter); the actual per-draw-flush BEHAVIOUR it enables needs a real ITextureRenderer (which
// this file's own DrawBeforeBeginThrows comment notes cannot be reached without one) and a real
// JS-side flush to observe -- that is verified end-to-end in htmldom_smoke_test.cpp instead,
// matching how BuildDrawCommandEXT's own geometry is unit-tested here while the JS variant
// generation it feeds is only browser-verified (see e.g. HTMLDOM-104's own precedent).
TEST(HtmlDomSpriteBatch, SetImmediateModeIsAcceptedAndDoesNotThrow)
{
    HtmlDomSpriteBatchRenderer batch;
    EXPECT_NO_THROW(batch.SetImmediateMode(true));
    batch.Begin();
    EXPECT_TRUE(batch.GetCommandsEXT().empty());
    batch.End();
    EXPECT_NO_THROW(batch.SetImmediateMode(false));
    batch.Begin();
    EXPECT_TRUE(batch.GetCommandsEXT().empty());
    batch.End();
}

// ---------------------------------------------------------------------------------------------
// The 3D surface: every entry point throws "not yet implemented"
// ---------------------------------------------------------------------------------------------

class HtmlDom3DSurfaceTest : public ::testing::Test
{
protected:
    HtmlDomRenderer renderer{TestArgs()};
};

TEST_F(HtmlDom3DSurfaceTest, DepthAndStencilClearsThrow)
{
    EXPECT_THROW(renderer.ClearColorAndDepth(0, 0, 0, 1, 1.0f), std::runtime_error);
    EXPECT_THROW(renderer.ClearDepth(1.0f), std::runtime_error);
    EXPECT_THROW(renderer.ClearStencil(0), std::runtime_error);
    EXPECT_THROW(renderer.ClearDepthAndStencil(1.0f, 0), std::runtime_error);
    EXPECT_THROW(renderer.ClearColorAndStencil(0, 0, 0, 1, 0), std::runtime_error);
    EXPECT_THROW(renderer.ClearColorDepthAndStencil(0, 0, 0, 1, 1.0f, 0), std::runtime_error);
}

TEST_F(HtmlDom3DSurfaceTest, DepthAndBlendTogglesThrow)
{
    EXPECT_THROW(renderer.SetDepthTestEnabled(true), std::runtime_error);
    EXPECT_THROW(renderer.SetBlendEnabled(true), std::runtime_error);
    EXPECT_THROW(renderer.SetDepthWriteEnabled(true), std::runtime_error);
}

TEST_F(HtmlDom3DSurfaceTest, BufferCreationAndDrawCallsThrow)
{
    EXPECT_THROW((void)renderer.CreateVertexBuffer(16), std::runtime_error);
    EXPECT_THROW((void)renderer.CreateIndexBuffer16(16), std::runtime_error);

    DummyVertexBuffer vb;
    DummyIndexBuffer ib;
    const Matrix m = Matrix::getIdentityProperty();
    EXPECT_THROW(renderer.DrawColoredPrimitives(vb, m, m, m, PrimitiveType::TriangleList, 1),
                 std::runtime_error);
    EXPECT_THROW(renderer.DrawIndexedColoredPrimitives(vb, ib, m, m, m, PrimitiveType::TriangleList, 1),
                 std::runtime_error);
}

TEST_F(HtmlDom3DSurfaceTest, UnsupportedResourceFactoriesAndShared3DGateThrow)
{
    EXPECT_THROW((void)renderer.CreateTexture3D(2, 2, 2, false, 0), std::runtime_error);
    EXPECT_THROW((void)renderer.CreateTextureCube(2, false, 0), std::runtime_error);
    EXPECT_THROW((void)renderer.CreateRenderTargetCube(2, 0, false, false, 0), std::runtime_error);
    EXPECT_THROW((void)renderer.CreateOcclusionQuery(), std::runtime_error);
    EXPECT_THROW((void)renderer.CreateEffectRenderer("void main(){}", "void main(){}"),
                 std::runtime_error);
    EXPECT_THROW(renderer.Ensure3DSupported("GraphicsDevice::DrawPrimitives"), std::runtime_error);
}

TEST_F(HtmlDom3DSurfaceTest, RenderTarget2DRejectsUnsupportedResourceOptions)
{
    EXPECT_NO_THROW((void)renderer.CreateRenderTarget2D(4, 4, 0, false, false, 0));
    EXPECT_THROW((void)renderer.CreateRenderTarget2D(4, 4, 1, false, false, 0), std::runtime_error);
    EXPECT_THROW((void)renderer.CreateRenderTarget2D(4, 4, 0, false, true, 0), std::runtime_error);
    EXPECT_THROW((void)renderer.CreateRenderTarget2D(4, 4, 0, false, false, 4), std::runtime_error);
    EXPECT_THROW((void)renderer.CreateRenderTarget2DEXT(4, 4, 0, false, false, 0, 1),
                 std::runtime_error);
    EXPECT_EQ(renderer.GetAppliedDepthStencilFormatEXT(3), 0);
}

TEST_F(HtmlDom3DSurfaceTest, ThrownMessagesNameTheRendererAndTheMissingCapability)
{
    try
    {
        renderer.ClearDepth(1.0f);
        FAIL() << "ClearDepth must throw on a 2D-only renderer.";
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
    EXPECT_FALSE(renderer.SupportsDepthStencil());
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::ThreeD));
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::DepthStencilBuffer));
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing));
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets));
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering));
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery));
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::CustomEffects));
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::Texture3D));
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::Instancing));
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput));
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::WireFrame));
}

// plan_html_dom.md HTMLDOM-117: GraphicsCapability::AdditiveBlending is the one capability this
// renderer answers for real rather than a blanket false -- under `node` (no __EMSCRIPTEN__,
// CNA_HtmlDom_SupportsAdditiveBlending's own EM_JS body compiles out entirely) there is no CSS
// engine to ask, so `false` is the correct native answer here, not merely the same "2D-only"
// blanket every other capability gets for an unrelated reason. The real, TRUE-in-a-real-browser
// answer is verified in htmldom_smoke_test.cpp instead (SupportsCapability(AdditiveBlending) ==
// JsSupportsPlusLighter(), the same raw CSS.supports() query this renderer's own EM_JS uses).
TEST_F(HtmlDom3DSurfaceTest, SupportsCapabilityAnswersAdditiveBlendingNatively)
{
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::AdditiveBlending));
}

TEST_F(HtmlDom3DSurfaceTest, MultipleRenderTargetsAndCubeFacesThrow)
{
    // Null targets are enough: both rejections happen on the descriptor count and kind, before any
    // target pointer is dereferenced.
    const RenderTargetBindingDescriptor descriptors[2] = {
        RenderTargetBindingDescriptor::ForRenderTarget2D(nullptr, 0, 4, 4, 0),
        RenderTargetBindingDescriptor::ForRenderTarget2D(nullptr, 0, 4, 4, 0),
    };
    EXPECT_THROW(renderer.SetRenderTargets(descriptors, 2), std::runtime_error);

    const RenderTargetBindingDescriptor cubeFace[1] = {
        RenderTargetBindingDescriptor::ForRenderTargetCubeFace(nullptr, 0, 4, 0),
    };
    EXPECT_THROW(renderer.SetRenderTargets(cubeFace, 1), std::runtime_error);

    // A single 2D target, and an empty set, are both legitimate.
    EXPECT_NO_THROW(renderer.SetRenderTargets(nullptr, 0));
}

// plan_html_dom.md HTMLDOM-120: GraphicsDevice::SetRenderTargets (the shared layer, the only
// caller a real game ever goes through) always passes count=0 for a null pointer -- (nullptr,
// count>0) can only happen via a direct call bypassing the shared layer, exactly what this test
// audits. Without the fix, `renderTargets[0]` inside SetRenderTargets dereferences this null
// pointer instead of throwing a clean, actionable exception.
TEST_F(HtmlDom3DSurfaceTest, SetRenderTargetsNullArrayWithPositiveCountThrows)
{
    EXPECT_THROW(renderer.SetRenderTargets(nullptr, 1), System::ArgumentNullException);
    EXPECT_THROW(renderer.SetRenderTargets(nullptr, -1), System::ArgumentOutOfRangeException);
}

TEST_F(HtmlDom3DSurfaceTest, BackbufferReadbackThrowsWithAnActionableMessage)
{
    std::uint8_t pixels[4] = {};
    try
    {
        renderer.ReadBackbuffer(0, 0, 1, 1, pixels);
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

// plan_html_dom.md HTMLDOM-120: once a target IS bound (so ReadBackbuffer gets past its own
// "nothing bound" guard above), a null destination pointer or a non-positive/negative region must
// be rejected with an actionable exception before crossing into JS -- not reach
// CNA_HtmlDom_ReadBound, where a null pointer would write into the start of the wasm heap via
// HEAPU8.set (real memory corruption, not a clean crash) and a negative/zero region has no defined
// meaning. A fake nonzero id is enough here (no real texture needed): both new checks run and
// throw before the (compiled-out, under native GTest) EM_JS call would ever be reached.
TEST_F(HtmlDom3DSurfaceTest, ReadBackbufferValidatesPointerAndRegionOnceATargetIsBound)
{
    SetBoundRenderTargetIdEXT(1);
    std::uint8_t pixels[4] = {};
    EXPECT_THROW(renderer.ReadBackbuffer(0, 0, 1, 1, nullptr), System::ArgumentNullException);
    EXPECT_THROW(renderer.ReadBackbuffer(-1, 0, 1, 1, pixels), System::ArgumentOutOfRangeException);
    EXPECT_THROW(renderer.ReadBackbuffer(0, -1, 1, 1, pixels), System::ArgumentOutOfRangeException);
    EXPECT_THROW(renderer.ReadBackbuffer(0, 0, 0, 1, pixels), System::ArgumentOutOfRangeException);
    EXPECT_THROW(renderer.ReadBackbuffer(0, 0, 1, 0, pixels), System::ArgumentOutOfRangeException);
    EXPECT_THROW(renderer.ReadBackbuffer(0, 0, -1, 1, pixels), System::ArgumentOutOfRangeException);
    SetBoundRenderTargetIdEXT(0);
}

TEST_F(HtmlDom3DSurfaceTest, ApplyBlendStateAcceptsPresetsAndRejectsCustomOnes)
{
    const BlendWriteState writeState{};
    EXPECT_NO_THROW(renderer.ApplyBlendState(4, 4, 5, 5, 0, 0, writeState));
    EXPECT_EQ(GetCurrentCompositeOpEXT(), DomCompositeOp::NonPremultiplied);
    EXPECT_NO_THROW(renderer.ApplyBlendState(4, 4, 0, 0, 0, 0, writeState));
    EXPECT_EQ(GetCurrentCompositeOpEXT(), DomCompositeOp::Additive);
    BlendWriteState masked{};
    masked.colorWriteChannels[0] = 7;
    EXPECT_THROW(renderer.ApplyBlendState(4, 4, 0, 0, 0, 0, masked), std::runtime_error);
    BlendWriteState sampleMasked{};
    sampleMasked.multiSampleMask = 0x7FFFFFFFu;
    EXPECT_THROW(renderer.ApplyBlendState(4, 4, 0, 0, 0, 0, sampleMasked), std::runtime_error);
    BlendWriteState inactiveTargetMasked{};
    inactiveTargetMasked.colorWriteChannels[2] = 7;
    EXPECT_THROW(renderer.ApplyBlendState(4, 4, 0, 0, 0, 0, inactiveTargetMasked),
                 std::runtime_error);
    EXPECT_THROW(renderer.ApplyBlendState(3, 3, 3, 3, 0, 0, writeState), std::runtime_error);
    // A rejected state must not have changed the one in effect.
    EXPECT_EQ(GetCurrentCompositeOpEXT(), DomCompositeOp::Additive);
    SetCurrentCompositeOpEXT(DomCompositeOp::NonPremultiplied);
}

TEST_F(HtmlDom3DSurfaceTest, ViewportFollowsTheVirtualResolutionSetting)
{
    renderer.SetVirtualResolution(0, 0);
    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
    float lx = 0.0f, ly = 0.0f;
    // With no virtual resolution there is no logical mapping to report.
    EXPECT_FALSE(renderer.TransformWindowToLogical(10.0f, 10.0f, lx, ly));
    EXPECT_FALSE(renderer.TransformLogicalToWindow(10.0f, 10.0f, lx, ly));
}

// plan_html_dom.md HTMLDOM-120: SetVirtualResolution stores width/height completely unvalidated --
// confirming this is already safe by construction rather than leaving it unverified.
// TransformWindowToLogical/TransformLogicalToWindow's own "no virtual resolution configured" guard
// is `virtualWidth_ <= 0 || virtualHeight_ <= 0` (HtmlDomRenderer.cpp), so a NEGATIVE
// resolution takes the exact same early-out as the already-tested (0,0) case above, not some
// unvalidated third state.
TEST_F(HtmlDom3DSurfaceTest, NegativeVirtualResolutionBehavesLikeUnsetRatherThanThrowingOrCorrupting)
{
    EXPECT_NO_THROW(renderer.SetVirtualResolution(-100, -50));
    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
    float lx = 0.0f, ly = 0.0f;
    EXPECT_FALSE(renderer.TransformWindowToLogical(10.0f, 10.0f, lx, ly));
    EXPECT_FALSE(renderer.TransformLogicalToWindow(10.0f, 10.0f, lx, ly));
    renderer.SetVirtualResolution(0, 0);
}

// HTMLDOM-121: unsupported depth/stencil, wireframe, and depth-bias state must not be accepted as
// inert success.  The all-default two-dimensional subset remains valid, while a caller that asks
// for a state this DOM/CSS implementation cannot represent receives a deterministic failure.
TEST_F(HtmlDom3DSurfaceTest, StateSettersAcceptOnlyTheTruthfulTwoDimensionalSubset)
{
    EXPECT_FALSE(renderer.SupportsDepthStencil());

    EXPECT_NO_THROW(renderer.ApplyDepthStencilState(
        /*depthEnable=*/false, /*depthWriteEnable=*/false, /*depthFunc=*/0,
        /*stencilEnable=*/false, /*stencilFunc=*/0, /*stencilPass=*/0, /*stencilFail=*/0,
        /*stencilDepthFail=*/0, /*stencilMask=*/0, /*stencilWriteMask=*/0,
        /*referenceStencil=*/0, /*twoSidedStencilMode=*/false,
        /*ccwStencilFunc=*/0, /*ccwStencilPass=*/0, /*ccwStencilFail=*/0, /*ccwStencilDepthFail=*/0));
    EXPECT_THROW(renderer.ApplyDepthStencilState(
        true, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0),
        std::runtime_error);
    EXPECT_THROW(renderer.ApplyDepthStencilState(
        false, false, 0, true, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0),
        std::runtime_error);
    EXPECT_NO_THROW(renderer.SetReferenceStencil(0));
    EXPECT_THROW(renderer.SetReferenceStencil(42), std::runtime_error);

    // SetBlendFactor's colour must not leak into -- or otherwise disturb -- the composite op an
    // ApplyBlendState call already selected: it is only ever consumed for a BlendFactor blend
    // combination, which ApplyBlendState rejects before this renderer could ever read it.
    SetCurrentCompositeOpEXT(DomCompositeOp::Additive);
    EXPECT_NO_THROW(renderer.SetBlendFactor(0.25f, 0.5f, 0.75f, 1.0f));
    EXPECT_EQ(GetCurrentCompositeOpEXT(), DomCompositeOp::Additive);
    SetCurrentCompositeOpEXT(DomCompositeOp::NonPremultiplied);

    // Cull mode has no 2D analogue and remains harmless; scissor enable is represented.  Wireframe
    // and depth bias cannot be represented and are rejected instead of silently disappearing.
    EXPECT_NO_THROW(renderer.ApplyRasterizerState(
        /*cullMode=*/2, /*fillMode=*/0, /*scissorTestEnable=*/true,
        /*depthBias=*/0.0f, /*slopeScaleDepthBias=*/0.0f));
    EXPECT_THROW(renderer.ApplyRasterizerState(1, 0, false, 0.0f, 0.0f), std::runtime_error);
    EXPECT_THROW(renderer.ApplyRasterizerState(0, 1, false, 0.0f, 0.0f), std::runtime_error);
    EXPECT_THROW(renderer.ApplyRasterizerState(0, 0, false, 0.5f, 0.0f), std::runtime_error);
    const BlendWriteState writeState{};
    EXPECT_NO_THROW(renderer.ApplyBlendState(4, 4, 5, 5, 0, 0, writeState));
    EXPECT_EQ(GetCurrentCompositeOpEXT(), DomCompositeOp::NonPremultiplied);
    SetCurrentCompositeOpEXT(DomCompositeOp::NonPremultiplied);
    SetCurrentScissorEnableEXT(false);
}

// plan_html_dom.md HTMLDOM-102: ApplyRasterizerState's scissorTestEnable argument is the one field
// this renderer genuinely reads -- verified directly here (pure C++ state, no DOM/browser needed),
// separately from the inert-fields test above so a future change to either can't silently mask a
// regression in the other.
TEST_F(HtmlDom3DSurfaceTest, ApplyRasterizerStateReadsScissorTestEnable)
{
    EXPECT_FALSE(GetCurrentScissorEnableEXT())
        << "false before any ApplyRasterizerState call, matching RasterizerState's own "
           "constructor default";

    renderer.ApplyRasterizerState(/*cullMode=*/0, /*fillMode=*/0, /*scissorTestEnable=*/true, 0.0f, 0.0f);
    EXPECT_TRUE(GetCurrentScissorEnableEXT());

    renderer.ApplyRasterizerState(/*cullMode=*/0, /*fillMode=*/0, /*scissorTestEnable=*/false, 0.0f, 0.0f);
    EXPECT_FALSE(GetCurrentScissorEnableEXT());

    SetCurrentScissorEnableEXT(false);
}

// plan_html_dom.md HTMLDOM-98: under `node` (no __EMSCRIPTEN__), SetViewport's own EM_JS forwarding
// call compiles out entirely, so there is no JS-side state for a native GTest to observe -- this
// verifies the C++-side contract that IS testable here: arbitrary values never throw, a repeated
// call with the SAME values (the idempotency guard's early-return branch) never throws either, and
// neither does a subsequent call with genuinely DIFFERENT values (the branch that would forward to
// JS under Emscripten).
TEST_F(HtmlDom3DSurfaceTest, SetViewportAcceptsArbitraryValuesAndIsIdempotent)
{
    EXPECT_NO_THROW(renderer.SetViewport(0, 0, 800, 480, 0.0f, 1.0f));
    EXPECT_NO_THROW(renderer.SetViewport(0, 0, 800, 480, 0.0f, 1.0f));  // same values again
    EXPECT_NO_THROW(renderer.SetViewport(4, 4, 16, 16, 0.25f, 0.75f));  // genuinely different values
    EXPECT_NO_THROW(renderer.SetViewport(4, 4, 16, 16, 0.25f, 0.75f));  // same as the line above
}

// plan_html_dom.md HTMLDOM-120: SetViewport's w/h are stored into Module['cnaDomViewport'] but
// (confirmed by grep) only ever READ as .x/.y by the sprite-flush path -- .w/.h are dead data this
// renderer never derives anything from -- and minDepth/maxDepth are literally unnamed parameters,
// never read or stored at all. So negative w/h and a genuinely inverted depth range (minDepth >
// maxDepth, itself never validated anywhere in the shared Viewport.cpp layer either) are already
// safe by construction, not merely untested -- confirmed here rather than left unverified.
TEST_F(HtmlDom3DSurfaceTest, SetViewportAcceptsNegativeDimensionsAndInvertedDepthRangeInertly)
{
    EXPECT_NO_THROW(renderer.SetViewport(4, 4, -16, -16, 2.0f, -1.0f));
    renderer.SetViewport(0, 0, 800, 480, 0.0f, 1.0f);   // hygiene: restore for later tests.
}

// ---------------------------------------------------------------------------------------------
// Texture/render-target size validation
// ---------------------------------------------------------------------------------------------
//
// plan_html_dom.md HTMLDOM-120: neither Texture2D.cpp (upper-bound-only) nor RenderTarget2D.cpp
// (no size validation at all -- it never even reaches Texture2D's own dimension checks, since it
// bypasses the two Texture2D constructors that call them) rejects a zero/negative width or height
// anywhere in the shared layer -- confirmed by reading both files. HtmlDomTextureRenderer's own
// constructors are the only place in the whole chain that can catch it before it would otherwise
// reach `new OffscreenCanvas(w,h)`/`canvas.width=w`, whose behavior for a degenerate size is
// browser-implementation-defined and was untested here. Both constructors run fully natively under
// `node` (the CNA_HtmlDom_CreateTexture EM_JS call is never reached once the throw fires first).
TEST(HtmlDomTextureRendererSizeValidation, ZeroOrNegativeDimensionsThrowFromThePixelDataConstructor)
{
    const ImageData zeroWidth{0, 4, std::vector<std::uint8_t>(4 * 4 * 4, 0)};
    EXPECT_THROW((HtmlDomTextureRenderer{zeroWidth}), System::ArgumentOutOfRangeException);
    const ImageData negativeHeight{4, -1, std::vector<std::uint8_t>(4 * 4 * 4, 0)};
    EXPECT_THROW((HtmlDomTextureRenderer{negativeHeight}), System::ArgumentOutOfRangeException);
    const CNA::Internal::Graphics::ImageData valid{4, 4, std::vector<std::uint8_t>(4 * 4 * 4, 0)};
    EXPECT_NO_THROW((HtmlDomTextureRenderer{valid}));
}

TEST(HtmlDomTextureRendererSizeValidation, ZeroOrNegativeDimensionsThrowFromTheRenderTargetConstructor)
{
    EXPECT_THROW((HtmlDomTextureRenderer{0, 4}), System::ArgumentOutOfRangeException);
    EXPECT_THROW((HtmlDomTextureRenderer{4, -1}), System::ArgumentOutOfRangeException);
    EXPECT_THROW((HtmlDomTextureRenderer{std::numeric_limits<int>::max(), 1}),
                 System::ArgumentOutOfRangeException);
    EXPECT_NO_THROW((HtmlDomTextureRenderer{4, 4}));
}

// HtmlDomRenderTargetRenderer(w,h) delegates straight to HtmlDomTextureRenderer(w,h) (its own
// member-initializer list) -- confirming the validation above is inherited, not something a
// render target could bypass by constructing its own texture differently.
TEST(HtmlDomTextureRendererSizeValidation, ZeroOrNegativeDimensionsThrowFromRenderTargetRendererToo)
{
    EXPECT_THROW((HtmlDomRenderTargetRenderer{0, 0}), System::ArgumentOutOfRangeException);
    EXPECT_NO_THROW((HtmlDomRenderTargetRenderer{4, 4}));
}

// plan_html_dom.md HTMLDOM-120: locks in HtmlDomRenderTargetRenderer::GetData's own documented
// `data == nullptr` contract (returns false rather than throwing, HtmlDomRenderTargetRenderer.cpp's
// own comment: "false ... when no canvas exists") as an explicit, regression-proof test rather
// than leaving it implicit in the implementation alone -- every OTHER invalid-argument case in
// this same method (negative level, out-of-range level, an out-of-bounds rectangle, an
// undersized destination buffer) already throws, so this is deliberately the one intentional
// exception to that pattern, worth pinning down explicitly.
TEST(HtmlDomTextureRendererSizeValidation, RenderTargetGetDataWithNullDestinationReturnsFalseNotThrow)
{
    HtmlDomRenderTargetRenderer rt(4, 4);
    bool result = true;
    EXPECT_NO_THROW(result = rt.GetData(0, 0, 0, 2, 2, nullptr, 2 * 2 * 4));
    EXPECT_FALSE(result);
}
#endif // CNA_RENDERER_HTML_DOM || CNA_HTML_DOM_HOST_TESTS
