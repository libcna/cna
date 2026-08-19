// SPDX-License-Identifier: MS-PL
//
// Structural GTest coverage for the NANOVG renderer's pure-function pieces -- the
// BlendState -> NVGcompositeOperation mapping -- which needs no real SDL window/GL context/
// NVGcontext to exercise. Real windowed behavior (Clear/Present/SpriteBatch draw/readback) is
// covered by modules/renderers/nanovg/examples/ instead (this module's own smoke, rotation-oracle
// and blend tests), matching the established split every other renderer with a tests/ directory
// uses (see e.g. modules/renderers/openvg/tests/... 's own header comment).
#include <gtest/gtest.h>

#if defined(CNA_RENDERER_NANOVG) || defined(CNA_RENDERER_PRESENT_NANOVG)
#include "CNA/Internal/Renderers/NanoVg/NanoVgRenderer.hpp"

using namespace CNA::Internal::Renderers::NanoVg;

TEST(NanoVgBlendStateMapping, StandardPresetsMapCorrectly)
{
    // NVGcompositeOperation ordinals: NVG_SOURCE_OVER=0, NVG_LIGHTER=8, NVG_COPY=9.
    EXPECT_EQ(BlendStateToNvgCompositeOperation(0, 0, 1, 1, 0, 0), 9); // Opaque -> NVG_COPY
    EXPECT_EQ(BlendStateToNvgCompositeOperation(4, 4, 5, 5, 0, 0), 0); // NonPremultiplied -> NVG_SOURCE_OVER
    EXPECT_EQ(BlendStateToNvgCompositeOperation(0, 0, 5, 5, 0, 0), 0); // AlphaBlend -> NVG_SOURCE_OVER (documented caveat)
}

TEST(NanoVgBlendStateMapping, AdditiveMapsToRealLighterMode)
{
    // Unlike OPENVG (ShivaVG), NANOVG genuinely implements Additive: NVG_LIGHTER = (GL_ONE,
    // GL_ONE), a real glBlendFuncSeparate -- see nanovg.c's own nvg__compositeOperationState.
    // Real XNA BlendState.Additive: srcBlend=SourceAlpha(4), dstBlend=One(0) (BlendState.cpp).
    EXPECT_EQ(BlendStateToNvgCompositeOperation(4, 4, 0, 0, 0, 0), 8); // Additive -> NVG_LIGHTER
}

TEST(NanoVgBlendStateMapping, AsymmetricColorAlphaFactorsThrow)
{
    EXPECT_THROW(BlendStateToNvgCompositeOperation(0, 4, 1, 1, 0, 0), std::runtime_error);
}

TEST(NanoVgBlendStateMapping, NonAddBlendFunctionThrows)
{
    EXPECT_THROW(BlendStateToNvgCompositeOperation(0, 0, 1, 1, 1, 1), std::runtime_error);
}

TEST(NanoVgBlendStateMapping, ArbitraryCustomBlendStateThrows)
{
    EXPECT_THROW(BlendStateToNvgCompositeOperation(2, 2, 3, 3, 0, 0), std::runtime_error);
}
#endif // CNA_RENDERER_NANOVG / CNA_RENDERER_PRESENT_NANOVG
