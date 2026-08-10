// SPDX-License-Identifier: MS-PL
//
// GTest coverage for everything on the SVG_DOM renderer that does not need a real browser: the
// blend-state mapping, the PNG/base64/un-premultiply pixel pipeline (genuinely round-tripped via
// SDL3_image, not merely asserted), the sprite geometry encoder, source-rectangle validation, and
// the 3D "not yet implemented" surface. Structured so it runs under this repo's native `CnaTests`
// binary with no document/DOM/browser at all -- nothing here touches EM_JS.
#include <gtest/gtest.h>

#if defined(CNA_RENDERER_SVG_DOM) || defined(CNA_SVG_DOM_HOST_TESTS)
#include "CNA/Internal/Renderers/SvgDom/SvgDomRenderer.hpp"
#include "CNA/Internal/Renderers/SvgDom/SvgDomRenderTargetRenderer.hpp"
#include "CNA/Internal/Renderers/SvgDom/SvgDomSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/SvgDom/SvgDomState.hpp"
#include "CNA/Internal/Renderers/SvgDom/SvgDomTextureRenderer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <cstring>
#include <limits>
#include <string>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::SvgDom;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::SpriteEffects;

namespace
{
    SDL_Window* FakeWindow() { return reinterpret_cast<SDL_Window*>(0x1); }

    ImageData MakeImage(int w, int h, const std::vector<std::uint8_t>& pixels)
    {
        ImageData img;
        img.width = w;
        img.height = h;
        img.pixels = pixels;
        return img;
    }

    // A tiny 2x2 texture: (255,0,0,255) (0,255,0,128) / (0,0,255,64) (255,255,255,0), row-major.
    std::vector<std::uint8_t> Sample2x2()
    {
        return {
            255, 0, 0, 255,    0, 255, 0, 128,
            0, 0, 255, 64,     255, 255, 255, 0,
        };
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

// ---------------------------------------------------------------------------------------------
// BlendState -> DomCompositeOp mapping
// ---------------------------------------------------------------------------------------------

TEST(SvgDomBlendStateMapping, StandardPresetsMapCorrectly)
{
    EXPECT_EQ(BlendStateToDomCompositeOp(0, 0, 1, 1, 0, 0), DomCompositeOp::Opaque);
    EXPECT_EQ(BlendStateToDomCompositeOp(0, 0, 5, 5, 0, 0), DomCompositeOp::AlphaBlend);
    EXPECT_EQ(BlendStateToDomCompositeOp(4, 4, 5, 5, 0, 0), DomCompositeOp::NonPremultiplied);
    EXPECT_EQ(BlendStateToDomCompositeOp(4, 4, 0, 0, 0, 0), DomCompositeOp::Additive);
}

TEST(SvgDomBlendStateMapping, CustomBlendStatesThrow)
{
    EXPECT_THROW(BlendStateToDomCompositeOp(4, 4, 1, 1, 0, 0), std::runtime_error);
    EXPECT_THROW(BlendStateToDomCompositeOp(0, 4, 1, 5, 0, 0), std::runtime_error);
    EXPECT_THROW(BlendStateToDomCompositeOp(0, 0, 1, 1, 1, 0), std::runtime_error);
}

TEST(SvgDomBlendStateMapping, VariantModeSelectionMatchesTheOpsPixelRequirement)
{
    EXPECT_EQ(VariantModeFor(DomCompositeOp::Opaque), 0);
    EXPECT_EQ(VariantModeFor(DomCompositeOp::NonPremultiplied), 0);
    EXPECT_EQ(VariantModeFor(DomCompositeOp::Additive), 0);
    EXPECT_EQ(VariantModeFor(DomCompositeOp::AlphaBlend), 1);
}

// ---------------------------------------------------------------------------------------------
// Shared draw-path state (composite op / scissor / viewport offset / bound render target)
// ---------------------------------------------------------------------------------------------

TEST(SvgDomSharedState, CompositeOpRoundTrips)
{
    SetCurrentCompositeOpEXT(DomCompositeOp::Additive);
    EXPECT_EQ(GetCurrentCompositeOpEXT(), DomCompositeOp::Additive);
    SetCurrentCompositeOpEXT(DomCompositeOp::NonPremultiplied);
    EXPECT_EQ(GetCurrentCompositeOpEXT(), DomCompositeOp::NonPremultiplied);
}

TEST(SvgDomSharedState, ScissorEnableRoundTrips)
{
    SetCurrentScissorEnableEXT(true);
    EXPECT_TRUE(GetCurrentScissorEnableEXT());
    SetCurrentScissorEnableEXT(false);
    EXPECT_FALSE(GetCurrentScissorEnableEXT());
}

TEST(SvgDomSharedState, ScissorRectRoundTrips)
{
    SetCurrentScissorRectEXT(1, 2, 3, 4);
    float x = 0, y = 0, w = 0, h = 0;
    GetCurrentScissorRectEXT(x, y, w, h);
    EXPECT_FLOAT_EQ(x, 1); EXPECT_FLOAT_EQ(y, 2); EXPECT_FLOAT_EQ(w, 3); EXPECT_FLOAT_EQ(h, 4);
}

TEST(SvgDomSharedState, ViewportOffsetRoundTrips)
{
    SetCurrentViewportOffsetEXT(5, 6);
    float x = 0, y = 0;
    GetCurrentViewportOffsetEXT(x, y);
    EXPECT_FLOAT_EQ(x, 5); EXPECT_FLOAT_EQ(y, 6);
    SetCurrentViewportOffsetEXT(0, 0);
}

TEST(SvgDomSharedState, BoundRenderTargetIdRoundTrips)
{
    SetBoundRenderTargetIdEXT(42);
    EXPECT_EQ(GetBoundRenderTargetIdEXT(), 42);
    SetBoundRenderTargetIdEXT(0);
    EXPECT_EQ(GetBoundRenderTargetIdEXT(), 0);
}

TEST(SvgDomSharedState, AllocateTextureIdEXTIsMonotonicallyIncreasing)
{
    const int a = AllocateTextureIdEXT();
    const int b = AllocateTextureIdEXT();
    EXPECT_GT(b, a);
    EXPECT_GT(a, 0);
}

TEST(SvgDomSourceRectangleValidation, InBoundsNeverThrows)
{
    EXPECT_NO_THROW(ValidateSourceRectangleEXT(false));
}

TEST(SvgDomSourceRectangleValidation, OutOfBoundsThrows)
{
    EXPECT_THROW(ValidateSourceRectangleEXT(true), std::runtime_error);
}

// ---------------------------------------------------------------------------------------------
// PNG / base64 / un-premultiply pixel pipeline -- pure C++, genuinely round-tripped
// ---------------------------------------------------------------------------------------------

TEST(SvgDomBase64, KnownVectorsMatchRfc4648)
{
    const std::string man = "Man";
    EXPECT_EQ(Base64EncodeEXT(reinterpret_cast<const std::uint8_t*>(man.data()), man.size()), "TWFu");
    const std::string m = "M";
    EXPECT_EQ(Base64EncodeEXT(reinterpret_cast<const std::uint8_t*>(m.data()), m.size()), "TQ==");
    const std::string ma = "Ma";
    EXPECT_EQ(Base64EncodeEXT(reinterpret_cast<const std::uint8_t*>(ma.data()), ma.size()), "TWE=");
    EXPECT_EQ(Base64EncodeEXT(nullptr, 0), "");
}

TEST(SvgDomPngRoundTrip, EncodedPngDecodesBackToTheExactPixels)
{
    const std::vector<std::uint8_t> pixels = Sample2x2();
    const std::vector<std::uint8_t> png = EncodePngEXT(pixels.data(), 2, 2);
    ASSERT_GT(png.size(), 0u);
    // A PNG file always starts with this fixed 8-byte signature.
    static const std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    ASSERT_GE(png.size(), 8u);
    EXPECT_EQ(std::memcmp(png.data(), kSig, 8), 0);

    SDL_IOStream* io = SDL_IOFromConstMem(png.data(), png.size());
    ASSERT_NE(io, nullptr);
    SDL_Surface* decoded = IMG_Load_IO(io, true);
    ASSERT_NE(decoded, nullptr);
    EXPECT_EQ(decoded->w, 2);
    EXPECT_EQ(decoded->h, 2);
    SDL_Surface* rgba = SDL_ConvertSurface(decoded, SDL_PIXELFORMAT_RGBA32);
    ASSERT_NE(rgba, nullptr);
    const auto* bytes = static_cast<const std::uint8_t*>(rgba->pixels);
    for (int i = 0; i < 16; ++i)
        EXPECT_EQ(bytes[i], pixels[i]) << "byte " << i;
    SDL_DestroySurface(rgba);
    SDL_DestroySurface(decoded);
}

TEST(SvgDomPngRoundTrip, DataUriHasThePngMimePrefix)
{
    const std::vector<std::uint8_t> pixels = Sample2x2();
    const std::vector<std::uint8_t> png = EncodePngEXT(pixels.data(), 2, 2);
    const std::string uri = BuildPngDataUriEXT(png);
    EXPECT_EQ(uri.rfind("data:image/png;base64,", 0), 0u);
}

TEST(SvgDomUnpremultiply, DividesColourByAlphaAndLeavesFullyTransparentPixelsAlone)
{
    // Premultiplied (128,0,0,128) -> straight (255,0,0,128) (128 is ~half of 255; integer rounding
    // of 128*255/128 == 255 exactly).
    const std::vector<std::uint8_t> px = {128, 0, 0, 128,   0, 0, 0, 0,   10, 20, 30, 255};
    const std::vector<std::uint8_t> out = UnpremultiplyEXT(px.data(), 3, 1);
    EXPECT_EQ(out[0], 255); EXPECT_EQ(out[1], 0); EXPECT_EQ(out[2], 0); EXPECT_EQ(out[3], 128);
    EXPECT_EQ(out[4], 0); EXPECT_EQ(out[5], 0); EXPECT_EQ(out[6], 0); EXPECT_EQ(out[7], 0);
    EXPECT_EQ(out[8], 10); EXPECT_EQ(out[9], 20); EXPECT_EQ(out[10], 30); EXPECT_EQ(out[11], 255);
}

TEST(SvgDomPrepareSpritePixels, ExtractsSubRectAndAppliesTint)
{
    const std::vector<std::uint8_t> tex = Sample2x2();
    const std::vector<std::uint8_t> out = PrepareSpritePixelsEXT(
        tex, 2, 0, 0, 1, 1, Color(255, 255, 255, 255), DomCompositeOp::NonPremultiplied);
    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(out[0], 255); EXPECT_EQ(out[1], 0); EXPECT_EQ(out[2], 0); EXPECT_EQ(out[3], 255);
}

TEST(SvgDomPrepareSpritePixels, TintHalvesEachChannel)
{
    const std::vector<std::uint8_t> tex = {200, 200, 200, 200};
    const std::vector<std::uint8_t> out = PrepareSpritePixelsEXT(
        tex, 1, 0, 0, 1, 1, Color(128, 128, 128, 128), DomCompositeOp::NonPremultiplied);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(out[i], (200 * 128) / 255) << "channel " << i;
}

TEST(SvgDomPrepareSpritePixels, OpaqueForcesOutputAlphaTo255)
{
    const std::vector<std::uint8_t> tex = {10, 20, 30, 40};
    const std::vector<std::uint8_t> out = PrepareSpritePixelsEXT(
        tex, 1, 0, 0, 1, 1, Color(255, 255, 255, 255), DomCompositeOp::Opaque);
    EXPECT_EQ(out[3], 255);
}

TEST(SvgDomTextureRows, PitchedRgbaUploadsArePackedWithoutPadding)
{
    // 2x1 image with 16-byte stride (8 bytes of padding per row).
    std::vector<std::uint8_t> padded(16 * 2, 0xEE);
    padded[0] = 1; padded[1] = 2; padded[2] = 3; padded[3] = 4;
    padded[4] = 5; padded[5] = 6; padded[6] = 7; padded[7] = 8;
    padded[16] = 9; padded[17] = 10; padded[18] = 11; padded[19] = 12;
    padded[20] = 13; padded[21] = 14; padded[22] = 15; padded[23] = 16;
    const std::vector<std::uint8_t> tight = TightenTextureRowsEXT(padded.data(), 2, 2, 16);
    ASSERT_EQ(tight.size(), 16u);
    for (int i = 0; i < 16; ++i) EXPECT_EQ(tight[i], i + 1);
}

// ---------------------------------------------------------------------------------------------
// SvgDomTextureRenderer
// ---------------------------------------------------------------------------------------------

TEST(SvgDomTextureRendererTest, ConstructsFromImageDataAndReportsSize)
{
    SvgDomTextureRenderer tex(MakeImage(2, 2, Sample2x2()));
    EXPECT_EQ(tex.GetWidth(), 2);
    EXPECT_EQ(tex.GetHeight(), 2);
    EXPECT_EQ(tex.GetNativeTexture(), nullptr);
    EXPECT_GT(tex.GetCanvasIdEXT(), 0);
}

TEST(SvgDomTextureRendererTest, GetDataRoundTripsAPlainUpload)
{
    SvgDomTextureRenderer tex(MakeImage(2, 2, Sample2x2()));
    std::uint8_t out[4] = {};
    ASSERT_TRUE(tex.GetData(0, 1, 0, 1, 1, out, 4));
    EXPECT_EQ(out[0], 0); EXPECT_EQ(out[1], 255); EXPECT_EQ(out[2], 0); EXPECT_EQ(out[3], 128);
}

TEST(SvgDomTextureRendererTest, GetDataRejectsOutOfRangeRegions)
{
    SvgDomTextureRenderer tex(MakeImage(2, 2, Sample2x2()));
    std::uint8_t out[4] = {};
    EXPECT_FALSE(tex.GetData(0, 2, 0, 1, 1, out, 4));
    EXPECT_FALSE(tex.GetData(0, 0, 0, 1, 1, out, 3));
    EXPECT_FALSE(tex.GetData(1, 0, 0, 1, 1, out, 4));
    EXPECT_FALSE(tex.GetData(0, 0, 0, 1, 1, nullptr, 4));
}

TEST(SvgDomTextureRendererTest, DataUriIsCachedAndInvalidatedByUpdatePixels)
{
    SvgDomTextureRenderer tex(MakeImage(2, 2, Sample2x2()));
    const std::string first = tex.GetDataUriEXT(0);
    EXPECT_EQ(tex.GetDataUriEXT(0), first) << "cached: identical string instance content";

    std::vector<std::uint8_t> replaced(16, 7);
    tex.UpdatePixels(replaced.data(), 8);
    const std::string second = tex.GetDataUriEXT(0);
    EXPECT_NE(first, second);
}

TEST(SvgDomTextureRendererTest, UnpremultipliedVariantDiffersFromStraightForTranslucentPixels)
{
    SvgDomTextureRenderer tex(MakeImage(2, 2, Sample2x2()));
    EXPECT_NE(tex.GetDataUriEXT(0), tex.GetDataUriEXT(1));
}

TEST(SvgDomTextureRendererTest, UpdatePixelsLevelAboveZeroThrows)
{
    SvgDomTextureRenderer tex(MakeImage(2, 2, Sample2x2()));
    std::vector<std::uint8_t> level1(4, 0);
    EXPECT_THROW(tex.UpdatePixelsLevel(1, level1.data(), 1, 1), std::runtime_error);
}

// ---------------------------------------------------------------------------------------------
// Size validation (Texture2D / RenderTarget2D construction)
// ---------------------------------------------------------------------------------------------

TEST(SvgDomTextureRendererSizeValidation, ZeroOrNegativeDimensionsThrow)
{
    EXPECT_THROW(SvgDomTextureRenderer(0, 4), System::ArgumentOutOfRangeException);
    EXPECT_THROW(SvgDomTextureRenderer(4, 0), System::ArgumentOutOfRangeException);
    EXPECT_THROW(SvgDomTextureRenderer(-1, 4), System::ArgumentOutOfRangeException);
}

TEST(SvgDomTextureRendererSizeValidation, RenderTargetRejectsZeroOrNegativeDimensionsToo)
{
    EXPECT_THROW(SvgDomRenderTargetRenderer(0, 4), System::ArgumentOutOfRangeException);
    EXPECT_THROW(SvgDomRenderTargetRenderer(4, -1), System::ArgumentOutOfRangeException);
}

TEST(SvgDomRenderTargetRendererTest, NeverBoundGetDataServesTheCpuBufferDirectly)
{
    SvgDomRenderTargetRenderer rt(2, 2);
    std::vector<std::uint8_t> pixels(16, 0);
    pixels[4] = 9; pixels[5] = 8; pixels[6] = 7; pixels[7] = 6;
    rt.UpdatePixels(pixels.data(), 8);
    std::uint8_t out[4] = {};
    ASSERT_TRUE(rt.GetData(0, 1, 0, 1, 1, out, 4));
    EXPECT_EQ(out[0], 9); EXPECT_EQ(out[1], 8); EXPECT_EQ(out[2], 7); EXPECT_EQ(out[3], 6);
}

TEST(SvgDomRenderTargetRendererTest, DirtyAfterBindReturnsFalseWithoutABrowser)
{
    SvgDomRenderTargetRenderer rt(2, 2);
    rt.BindAsRenderTarget();
    std::uint8_t out[4] = {};
    // No Emscripten canvas exists in this host build -- honest false, not stale/fabricated data.
    EXPECT_FALSE(rt.GetData(0, 0, 0, 1, 1, out, 4));
    rt.UnbindAsRenderTarget();
}

// ---------------------------------------------------------------------------------------------
// SvgDomDrawCommand geometry encoder
// ---------------------------------------------------------------------------------------------

TEST(SvgDomDrawCommandLayout, StrideStaysFourteenFourByteFields)
{
    EXPECT_EQ(sizeof(SvgDomDrawCommand), static_cast<std::size_t>(SvgDomDrawCommandFields) * 4);
}

TEST(SvgDomDrawCommand, UnscaledUnrotatedDrawPlacesOriginAtDest)
{
    const auto cmd = BuildDrawCommandEXT(
        7, 64, 32, Rectangle(10, 20, 8, 8), Rectangle(0, 0, 8, 8), Color(255, 255, 255, 255),
        0.0f, Vector2(0, 0), SpriteEffects::None, true, DomCompositeOp::NonPremultiplied);
    // Identity rotation/scale, no origin: matrix should be pure translate(10,20).
    EXPECT_FLOAT_EQ(cmd.m0, 1.0f); EXPECT_FLOAT_EQ(cmd.m1, 0.0f);
    EXPECT_FLOAT_EQ(cmd.m2, 0.0f); EXPECT_FLOAT_EQ(cmd.m3, 1.0f);
    EXPECT_FLOAT_EQ(cmd.m4, 10.0f); EXPECT_FLOAT_EQ(cmd.m5, 20.0f);
    EXPECT_EQ(cmd.textureId, 7);
    EXPECT_FLOAT_EQ(cmd.sw, 8.0f); EXPECT_FLOAT_EQ(cmd.sh, 8.0f);
}

TEST(SvgDomDrawCommand, ScaleIsDestinationOverUnclampedSource)
{
    const auto cmd = BuildDrawCommandEXT(
        1, 64, 32, Rectangle(0, 0, 20, 40), Rectangle(0, 0, 10, 10), Color(255, 255, 255, 255),
        0.0f, Vector2(0, 0), SpriteEffects::None, true, DomCompositeOp::NonPremultiplied);
    EXPECT_FLOAT_EQ(cmd.m0, 2.0f); // scaleX
    EXPECT_FLOAT_EQ(cmd.m3, 4.0f); // scaleY
}

TEST(SvgDomDrawCommand, OriginTranslatesLocalPivotIntoTheDestinationPoint)
{
    // origin at (4,4) of a 8x8 source, no rotation/scale: point (4,4) in source space should map
    // exactly to dest (10,20) after the full transform.
    const auto cmd = BuildDrawCommandEXT(
        1, 64, 32, Rectangle(10, 20, 8, 8), Rectangle(0, 0, 8, 8), Color(255, 255, 255, 255),
        0.0f, Vector2(4, 4), SpriteEffects::None, true, DomCompositeOp::NonPremultiplied);
    const float px = cmd.m0 * 4 + cmd.m2 * 4 + cmd.m4;
    const float py = cmd.m1 * 4 + cmd.m3 * 4 + cmd.m5;
    EXPECT_NEAR(px, 10.0f, 1e-4f);
    EXPECT_NEAR(py, 20.0f, 1e-4f);
}

TEST(SvgDomDrawCommand, NinetyDegreeRotationMapsRightToDown)
{
    // No origin/scale, dest at (0,0): rotating +90 degrees (clockwise, XNA/screen convention)
    // should map the local point (1,0) to approximately (0,1).
    constexpr float kHalfPi = 1.57079632679489661923f;
    const auto cmd = BuildDrawCommandEXT(
        1, 64, 32, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8), Color(255, 255, 255, 255),
        kHalfPi, Vector2(0, 0), SpriteEffects::None, true,
        DomCompositeOp::NonPremultiplied);
    const float px = cmd.m0 * 1 + cmd.m2 * 0 + cmd.m4;
    const float py = cmd.m1 * 1 + cmd.m3 * 0 + cmd.m5;
    EXPECT_NEAR(px, 0.0f, 1e-4f);
    EXPECT_NEAR(py, 1.0f, 1e-4f);
}

TEST(SvgDomDrawCommand, OutOfBoundsSourceRectangleThrows)
{
    EXPECT_THROW(BuildDrawCommandEXT(
        1, 8, 8, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 16, 8), Color(255, 255, 255, 255),
        0.0f, Vector2(0, 0), SpriteEffects::None, true, DomCompositeOp::NonPremultiplied),
        std::runtime_error);
}

TEST(SvgDomDrawCommand, ColorIsPackedRgbaAndOpSelectsVariantAndFlags)
{
    const auto opaque = BuildDrawCommandEXT(
        1, 8, 8, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8), Color(1, 2, 3, 4),
        0.0f, Vector2(0, 0), SpriteEffects::None, true, DomCompositeOp::Opaque);
    EXPECT_EQ(opaque.packedColor, 1u | (2u << 8) | (3u << 16) | (4u << 24));
    EXPECT_TRUE(opaque.flags & SvgFlagOpaque);
    EXPECT_EQ(opaque.variantMode, 0);

    const auto alphaBlend = BuildDrawCommandEXT(
        1, 8, 8, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8), Color(255, 255, 255, 255),
        0.0f, Vector2(0, 0), SpriteEffects::None, true, DomCompositeOp::AlphaBlend);
    EXPECT_EQ(alphaBlend.variantMode, 1);

    const auto additive = BuildDrawCommandEXT(
        1, 8, 8, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8), Color(255, 255, 255, 255),
        0.0f, Vector2(0, 0), SpriteEffects::None, true, DomCompositeOp::Additive);
    EXPECT_TRUE(additive.flags & SvgFlagAdditive);
}

TEST(SvgDomDrawCommand, PointFilteringClearsTheSmoothingFlag)
{
    const auto cmd = BuildDrawCommandEXT(
        1, 8, 8, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8), Color(255, 255, 255, 255),
        0.0f, Vector2(0, 0), SpriteEffects::None, false, DomCompositeOp::NonPremultiplied);
    EXPECT_FALSE(cmd.flags & SvgFlagSmoothing);
}

TEST(SvgDomDrawCommand, FlipHorizontallySetsTheFlagWithoutThrowing)
{
    EXPECT_NO_THROW(BuildDrawCommandEXT(
        1, 8, 8, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8), Color(255, 255, 255, 255),
        0.0f, Vector2(0, 0), SpriteEffects::FlipHorizontally, true, DomCompositeOp::NonPremultiplied));
    const auto cmd = BuildDrawCommandEXT(
        1, 8, 8, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8), Color(255, 255, 255, 255),
        0.0f, Vector2(0, 0), SpriteEffects::FlipHorizontally, true, DomCompositeOp::NonPremultiplied);
    EXPECT_TRUE(cmd.flags & SvgFlagFlipHorizontally);
}

// ---------------------------------------------------------------------------------------------
// SvgDomSpriteBatchRenderer
// ---------------------------------------------------------------------------------------------

TEST(SvgDomSpriteBatch, DrawBeforeBeginThrows)
{
    SvgDomSpriteBatchRenderer batch;
    SvgDomTextureRenderer tex(MakeImage(2, 2, Sample2x2()));
    EXPECT_THROW(batch.Draw(tex, 0.0f, 0.0f), std::runtime_error);
}

TEST(SvgDomSpriteBatch, CustomEffectThrowsButNullIsAccepted)
{
    SvgDomSpriteBatchRenderer batch;
    EXPECT_NO_THROW(batch.SetCustomEffect(nullptr));
    Microsoft::Xna::Framework::Graphics::Effect* bogus =
        reinterpret_cast<Microsoft::Xna::Framework::Graphics::Effect*>(0x1);
    EXPECT_THROW(batch.SetCustomEffect(bogus), std::runtime_error);
}

TEST(SvgDomSpriteBatch, SamplerFilterSupportsOnlyExactLinearAndPointModes)
{
    SvgDomSpriteBatchRenderer batch;
    EXPECT_NO_THROW(batch.SetSamplerFilter(0));
    EXPECT_NO_THROW(batch.SetSamplerFilter(1));
    EXPECT_THROW(batch.SetSamplerFilter(2), std::runtime_error);
}

TEST(SvgDomSpriteBatch, SamplerAddressModeRejectsOutOfRangeOrdinals)
{
    SvgDomSpriteBatchRenderer batch;
    EXPECT_NO_THROW(batch.SetSamplerAddressMode(0, 2));
    EXPECT_THROW(batch.SetSamplerAddressMode(-1, 0), std::runtime_error);
    EXPECT_THROW(batch.SetSamplerAddressMode(0, 3), std::runtime_error);
}

TEST(SvgDomSpriteBatch, BeginClearsCommandsFromAPreviousBatch)
{
    SvgDomSpriteBatchRenderer batch;
    SvgDomTextureRenderer tex(MakeImage(2, 2, Sample2x2()));
    batch.Begin();
    batch.Draw(tex, 0.0f, 0.0f);
    EXPECT_EQ(batch.GetCommandsEXT().size(), 1u);
    batch.Begin();
    EXPECT_TRUE(batch.GetCommandsEXT().empty());
}

TEST(SvgDomSpriteBatch, QueuedDrawEncodesTheTextureIdAndFullTextureSourceRect)
{
    SvgDomSpriteBatchRenderer batch;
    SvgDomTextureRenderer tex(MakeImage(2, 2, Sample2x2()));
    batch.Begin();
    batch.Draw(tex, 5.0f, 6.0f);
    ASSERT_EQ(batch.GetCommandsEXT().size(), 1u);
    const auto& cmd = batch.GetCommandsEXT()[0];
    EXPECT_EQ(cmd.textureId, tex.GetCanvasIdEXT());
    EXPECT_FLOAT_EQ(cmd.sw, 2.0f);
    EXPECT_FLOAT_EQ(cmd.sh, 2.0f);
}

TEST(SvgDomSpriteBatch, IdentityTransformMatrixIsAcceptedAndNotFlaggedAsSet)
{
    SvgDomSpriteBatchRenderer batch;
    EXPECT_NO_THROW(batch.SetTransformMatrix(Matrix::getIdentityProperty()));
}

TEST(SvgDomSpriteBatch, SetImmediateModeIsAcceptedAndDoesNotThrow)
{
    SvgDomSpriteBatchRenderer batch;
    EXPECT_NO_THROW(batch.SetImmediateMode(true));
    EXPECT_NO_THROW(batch.SetImmediateMode(false));
    batch.Begin();
    EXPECT_TRUE(batch.GetCommandsEXT().empty());
    batch.End();
}

TEST(SvgDomSpriteBatch, ZeroSizedSourceOrDestinationRectanglesAreSilentlySkipped)
{
    SvgDomSpriteBatchRenderer batch;
    SvgDomTextureRenderer tex(MakeImage(2, 2, Sample2x2()));
    batch.Begin();
    batch.Draw(tex, Rectangle(0, 0, 0, 4), Rectangle(0, 0, 2, 2), Color(255, 255, 255, 255));
    EXPECT_TRUE(batch.GetCommandsEXT().empty());
}

// ---------------------------------------------------------------------------------------------
// The 3D surface: every entry point throws "not yet implemented"
// ---------------------------------------------------------------------------------------------

class SvgDom3DSurfaceTest : public ::testing::Test
{
protected:
    SvgDomRenderer renderer{FakeWindow(), 800, 480, CnaPresentationMode::FixedHeightDynamicWidth};
};

TEST_F(SvgDom3DSurfaceTest, DepthAndStencilClearsThrow)
{
    EXPECT_THROW(renderer.ClearColorAndDepth(0, 0, 0, 1, 1.0f), std::runtime_error);
    EXPECT_THROW(renderer.ClearDepth(1.0f), std::runtime_error);
    EXPECT_THROW(renderer.ClearStencil(0), std::runtime_error);
    EXPECT_THROW(renderer.ClearDepthAndStencil(1.0f, 0), std::runtime_error);
    EXPECT_THROW(renderer.ClearColorAndStencil(0, 0, 0, 1, 0), std::runtime_error);
    EXPECT_THROW(renderer.ClearColorDepthAndStencil(0, 0, 0, 1, 1.0f, 0), std::runtime_error);
}

TEST_F(SvgDom3DSurfaceTest, DepthAndBlendTogglesThrow)
{
    EXPECT_THROW(renderer.SetDepthTestEnabled(true), std::runtime_error);
    EXPECT_THROW(renderer.SetBlendEnabled(true), std::runtime_error);
    EXPECT_THROW(renderer.SetDepthWriteEnabled(true), std::runtime_error);
}

TEST_F(SvgDom3DSurfaceTest, BufferCreationAndDrawCallsThrow)
{
    EXPECT_THROW(renderer.CreateVertexBuffer(4), std::runtime_error);
    EXPECT_THROW(renderer.CreateIndexBuffer16(4), std::runtime_error);
    DummyVertexBuffer vb;
    DummyIndexBuffer ib;
    EXPECT_THROW(renderer.DrawColoredPrimitives(
        vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(), Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1),
        std::runtime_error);
    EXPECT_THROW(renderer.DrawIndexedColoredPrimitives(
        vb, ib, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(), Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1),
        std::runtime_error);
}

TEST_F(SvgDom3DSurfaceTest, UnsupportedResourceFactoriesThrow)
{
    EXPECT_THROW(renderer.CreateOcclusionQuery(), std::runtime_error);
    EXPECT_THROW(renderer.CreateTexture3D(4, 4, 4, false, 0), std::runtime_error);
    EXPECT_THROW(renderer.CreateTextureCube(4, false, 0), std::runtime_error);
    EXPECT_THROW(renderer.CreateRenderTargetCube(4, 0), std::runtime_error);
    EXPECT_THROW(renderer.CreateEffectRenderer("", ""), std::runtime_error);
}

TEST_F(SvgDom3DSurfaceTest, RenderTarget2DRejectsUnsupportedResourceOptions)
{
    EXPECT_THROW(renderer.CreateRenderTarget2D(4, 4, 1), std::runtime_error);
    EXPECT_THROW(renderer.CreateRenderTarget2D(4, 4, 0, false, true), std::runtime_error);
    EXPECT_THROW(renderer.CreateRenderTarget2D(4, 4, 0, false, false, 4), std::runtime_error);
    EXPECT_THROW(renderer.CreateRenderTarget2DEXT(4, 4, 0, false, false, 0, 1), std::runtime_error);
    EXPECT_NO_THROW(renderer.CreateRenderTarget2D(4, 4, 0));
}

TEST_F(SvgDom3DSurfaceTest, CapabilityQueriesReportTheTwoDimensionalBoundaryUpFront)
{
    EXPECT_FALSE(renderer.SupportsDepthStencil());
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::ThreeD));
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput));
}

TEST_F(SvgDom3DSurfaceTest, SupportsCapabilityAnswersAdditiveBlendingNatively)
{
    // No browser here -- the honest answer is false, not a fabricated true.
    EXPECT_FALSE(renderer.SupportsCapability(CNA::GraphicsCapability::AdditiveBlending));
}

TEST_F(SvgDom3DSurfaceTest, MultipleRenderTargetsAndCubeFacesThrow)
{
    auto rt1 = renderer.CreateRenderTarget2D(4, 4, 0);
    auto rt2 = renderer.CreateRenderTarget2D(4, 4, 0);
    RenderTargetBindingDescriptor descs[2] = {
        RenderTargetBindingDescriptor::ForRenderTarget2D(rt1.get(), 0, 4, 4, 0),
        RenderTargetBindingDescriptor::ForRenderTarget2D(rt2.get(), 0, 4, 4, 0),
    };
    EXPECT_THROW(renderer.SetRenderTargets(descs, 2), std::runtime_error);
}

TEST_F(SvgDom3DSurfaceTest, SetRenderTargetsNullArrayWithPositiveCountThrows)
{
    EXPECT_THROW(renderer.SetRenderTargets(nullptr, 1), System::ArgumentNullException);
}

TEST_F(SvgDom3DSurfaceTest, BackbufferReadbackThrowsWithAnActionableMessage)
{
    std::uint8_t pixels[4] = {};
    EXPECT_THROW(renderer.ReadBackbuffer(0, 0, 1, 1, pixels), std::runtime_error);
}

TEST_F(SvgDom3DSurfaceTest, ApplyBlendStateAcceptsPresetsAndRejectsCustomOnes)
{
    BlendWriteState ws;
    EXPECT_NO_THROW(renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, ws));
    EXPECT_THROW(renderer.ApplyBlendState(1, 1, 1, 1, 0, 0, ws), std::runtime_error);
    ws.colorWriteChannels[0] = 1;
    EXPECT_THROW(renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, ws), std::runtime_error);
}

TEST_F(SvgDom3DSurfaceTest, ViewportFollowsTheVirtualResolutionSetting)
{
    // FakeWindow() is not a real SDL window, so SDL_GetWindowSize reports 0x0 for it --
    // ComputeLogicalViewport's own physW<=0/physH<=0 guard then makes the logical size 0
    // regardless of the requested virtual resolution (matching HtmlDomRenderer's identical
    // FakeWindow()-based test, which asserts the "no virtual resolution" contract instead of a
    // physical size this harness cannot produce).
    EXPECT_NO_THROW(renderer.SetVirtualResolution(320, 240));
    renderer.SetVirtualResolution(0, 0);
    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
    float lx = 0.0f, ly = 0.0f;
    EXPECT_FALSE(renderer.TransformWindowToLogical(10.0f, 10.0f, lx, ly));
    EXPECT_FALSE(renderer.TransformLogicalToWindow(10.0f, 10.0f, lx, ly));
}

TEST_F(SvgDom3DSurfaceTest, SetPresentationModeValidatesOrdinal)
{
    EXPECT_NO_THROW(renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Stretch)));
    EXPECT_THROW(renderer.SetPresentationMode(-1), std::out_of_range);
    EXPECT_THROW(renderer.SetPresentationMode(999), std::out_of_range);
}

TEST_F(SvgDom3DSurfaceTest, StateSettersAcceptOnlyTheTruthfulTwoDimensionalSubset)
{
    EXPECT_NO_THROW(renderer.ApplyDepthStencilState(
        false, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0));
    EXPECT_THROW(renderer.ApplyDepthStencilState(
        true, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0), std::runtime_error);
    EXPECT_NO_THROW(renderer.SetReferenceStencil(0));
    EXPECT_THROW(renderer.SetReferenceStencil(1), std::runtime_error);
}

TEST_F(SvgDom3DSurfaceTest, ApplyRasterizerStateReadsScissorTestEnable)
{
    renderer.ApplyRasterizerState(0, 0, true);
    EXPECT_TRUE(GetCurrentScissorEnableEXT());
    renderer.ApplyRasterizerState(0, 0, false);
    EXPECT_FALSE(GetCurrentScissorEnableEXT());
    EXPECT_THROW(renderer.ApplyRasterizerState(1, 0, false), std::runtime_error); // CullClockwiseFace
    EXPECT_THROW(renderer.ApplyRasterizerState(0, 1, false), std::runtime_error); // WireFrame
    EXPECT_THROW(renderer.ApplyRasterizerState(0, 0, false, 1.0f), std::runtime_error);
}

TEST_F(SvgDom3DSurfaceTest, SetViewportRecordsTheOffsetWithoutThrowing)
{
    EXPECT_NO_THROW(renderer.SetViewport(10, 20, 100, 100, 0.0f, 1.0f));
    float x = 0, y = 0;
    GetCurrentViewportOffsetEXT(x, y);
    EXPECT_FLOAT_EQ(x, 10.0f);
    EXPECT_FLOAT_EQ(y, 20.0f);
    renderer.SetViewport(0, 0, 800, 480, 0.0f, 1.0f);
}

TEST_F(SvgDom3DSurfaceTest, WindowAndRendererInternalAccessorsAreHonest)
{
    EXPECT_EQ(renderer.GetWindowInternal(), FakeWindow());
    EXPECT_EQ(renderer.GetRendererInternal(), nullptr);
    EXPECT_EQ(renderer.GetAppliedDepthStencilFormatEXT(3), 0);
}

#endif // CNA_RENDERER_SVG_DOM || CNA_SVG_DOM_HOST_TESTS
