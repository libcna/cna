// SPDX-License-Identifier: MS-PL
//
// Structural GTest coverage for the NANOVG renderer's pure-function pieces -- the
// BlendState -> NVGblendFactor mapping -- which needs no real SDL window/GL context/NVGcontext to
// exercise. Real windowed behavior (Clear/Present/SpriteBatch draw/readback, and the composited
// pixel each mapping below actually produces) is covered by modules/renderers/nanovg/examples/
// instead, matching the established split every other renderer with a tests/ directory uses (see
// e.g. modules/renderers/openvg/tests/... 's own header comment).
#include <gtest/gtest.h>

#if defined(CNA_RENDERER_NANOVG) || defined(CNA_RENDERER_PRESENT_NANOVG)
#include "CNA/Internal/Renderers/NanoVg/NanoVgRenderer.hpp"

using namespace CNA::Internal::Renderers::NanoVg;

namespace
{
    // NVGblendFactor is a bit-flag enum (nanovg.h), not an ordinal sequence. Spelled out here
    // rather than included, because this test target links CNA without NanoVG's own headers.
    constexpr int kNvgZero = 1 << 0;
    constexpr int kNvgOne = 1 << 1;
    constexpr int kNvgSrcColor = 1 << 2;
    constexpr int kNvgDstColor = 1 << 4;
    constexpr int kNvgSrcAlpha = 1 << 6;
    constexpr int kNvgOneMinusSrcAlpha = 1 << 7;
    constexpr int kNvgSrcAlphaSaturate = 1 << 10;

    // Raw XNA Blend ordinals (Blend.hpp) and BlendFunction ordinals (BlendFunction.hpp).
    constexpr int kOne = 0, kZero = 1, kSrcColor = 2, kSrcAlpha = 4, kInvSrcAlpha = 5,
                  kDstColor = 6, kBlendFactor = 10, kInvBlendFactor = 11, kSrcAlphaSaturation = 12;
    constexpr int kAdd = 0, kSubtract = 1, kMin = 4;
}

// The four built-in presets, transcribed from modules/graphics/src/Xna/BlendState.cpp. AlphaBlend
// and NonPremultiplied differ ONLY in their source factor, and that difference must survive: the
// first consumes already-premultiplied source RGB, the second multiplies straight RGB by the
// source alpha at the blend stage. A mapping that collapsed them onto one NanoVG composite
// operation -- which is what this renderer used to do -- fails here.
TEST(NanoVgBlendStateMapping, AlphaBlendAndNonPremultipliedStayDistinct)
{
    const NanoVgBlendFunc alphaBlend =
        BlendStateToNvgBlendFunc(kOne, kOne, kInvSrcAlpha, kInvSrcAlpha, kAdd, kAdd);
    EXPECT_EQ(alphaBlend.srcRGB, kNvgOne);
    EXPECT_EQ(alphaBlend.dstRGB, kNvgOneMinusSrcAlpha);
    EXPECT_EQ(alphaBlend.srcAlpha, kNvgOne);
    EXPECT_EQ(alphaBlend.dstAlpha, kNvgOneMinusSrcAlpha);

    const NanoVgBlendFunc nonPremultiplied =
        BlendStateToNvgBlendFunc(kSrcAlpha, kSrcAlpha, kInvSrcAlpha, kInvSrcAlpha, kAdd, kAdd);
    EXPECT_EQ(nonPremultiplied.srcRGB, kNvgSrcAlpha);
    EXPECT_EQ(nonPremultiplied.dstRGB, kNvgOneMinusSrcAlpha);
    EXPECT_EQ(nonPremultiplied.srcAlpha, kNvgSrcAlpha);
    EXPECT_EQ(nonPremultiplied.dstAlpha, kNvgOneMinusSrcAlpha);

    EXPECT_NE(alphaBlend.srcRGB, nonPremultiplied.srcRGB);
}

TEST(NanoVgBlendStateMapping, OpaqueIsPlainSourceReplacement)
{
    const NanoVgBlendFunc opaque = BlendStateToNvgBlendFunc(kOne, kOne, kZero, kZero, kAdd, kAdd);
    EXPECT_EQ(opaque.srcRGB, kNvgOne);
    EXPECT_EQ(opaque.dstRGB, kNvgZero);
    EXPECT_EQ(opaque.srcAlpha, kNvgOne);
    EXPECT_EQ(opaque.dstAlpha, kNvgZero);
}

TEST(NanoVgBlendStateMapping, AdditiveKeepsItsSourceAlphaFactor)
{
    // Real XNA BlendState.Additive is (SourceAlpha, One), not (One, One): the source is attenuated
    // by its own alpha before being added.
    const NanoVgBlendFunc additive =
        BlendStateToNvgBlendFunc(kSrcAlpha, kSrcAlpha, kOne, kOne, kAdd, kAdd);
    EXPECT_EQ(additive.srcRGB, kNvgSrcAlpha);
    EXPECT_EQ(additive.dstRGB, kNvgOne);
    EXPECT_EQ(additive.srcAlpha, kNvgSrcAlpha);
    EXPECT_EQ(additive.dstAlpha, kNvgOne);
}

TEST(NanoVgBlendStateMapping, ColorAndAlphaFactorsAreIndependent)
{
    // Asymmetric colour/alpha factors are exactly what glBlendFuncSeparate exists for; the mapping
    // must carry each channel's own pair through untouched rather than requiring them to match.
    const NanoVgBlendFunc blend =
        BlendStateToNvgBlendFunc(kSrcAlpha, kZero, kOne, kOne, kAdd, kAdd);
    EXPECT_EQ(blend.srcRGB, kNvgSrcAlpha);
    EXPECT_EQ(blend.dstRGB, kNvgOne);
    EXPECT_EQ(blend.srcAlpha, kNvgZero);
    EXPECT_EQ(blend.dstAlpha, kNvgOne);
}

TEST(NanoVgBlendStateMapping, ColorFactorsAreRepresentable)
{
    const NanoVgBlendFunc blend =
        BlendStateToNvgBlendFunc(kDstColor, kOne, kSrcColor, kZero, kAdd, kAdd);
    EXPECT_EQ(blend.srcRGB, kNvgDstColor);
    EXPECT_EQ(blend.dstRGB, kNvgSrcColor);
}

TEST(NanoVgBlendStateMapping, SourceAlphaSaturationIsASourceFactorOnly)
{
    const NanoVgBlendFunc blend =
        BlendStateToNvgBlendFunc(kSrcAlphaSaturation, kOne, kZero, kZero, kAdd, kAdd);
    EXPECT_EQ(blend.srcRGB, kNvgSrcAlphaSaturate);

    // GL accepts GL_SRC_ALPHA_SATURATE as a destination factor only from OpenGL 4.4 onwards, and
    // this renderer requests a 2.1 context.
    EXPECT_THROW(BlendStateToNvgBlendFunc(kOne, kOne, kSrcAlphaSaturation, kZero, kAdd, kAdd),
                 std::runtime_error);
}

TEST(NanoVgBlendStateMapping, ConstantBlendFactorIsRejected)
{
    // NVGblendFactor has no GL_CONSTANT_COLOR counterpart, so GraphicsDevice.BlendFactor can never
    // reach the blend stage.
    EXPECT_THROW(BlendStateToNvgBlendFunc(kBlendFactor, kOne, kZero, kZero, kAdd, kAdd),
                 std::runtime_error);
    EXPECT_THROW(BlendStateToNvgBlendFunc(kOne, kOne, kInvBlendFactor, kZero, kAdd, kAdd),
                 std::runtime_error);
}

TEST(NanoVgBlendStateMapping, NonAddBlendFunctionIsRejected)
{
    // NanoVG's GL2 backend never calls glBlendEquation, so the equation is permanently GL_FUNC_ADD.
    EXPECT_THROW(BlendStateToNvgBlendFunc(kOne, kOne, kZero, kZero, kSubtract, kSubtract),
                 std::runtime_error);
    EXPECT_THROW(BlendStateToNvgBlendFunc(kOne, kOne, kZero, kZero, kAdd, kMin),
                 std::runtime_error);
}

TEST(NanoVgBlendStateMapping, OutOfRangeBlendOrdinalIsRejected)
{
    EXPECT_THROW(BlendStateToNvgBlendFunc(99, kOne, kZero, kZero, kAdd, kAdd), std::runtime_error);
    EXPECT_THROW(BlendStateToNvgBlendFunc(kOne, kOne, -1, kZero, kAdd, kAdd), std::runtime_error);
}
#endif // CNA_RENDERER_NANOVG / CNA_RENDERER_PRESENT_NANOVG
