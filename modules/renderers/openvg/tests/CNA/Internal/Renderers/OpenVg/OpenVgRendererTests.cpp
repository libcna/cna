// SPDX-License-Identifier: MS-PL
//
// Structural GTest coverage for the OPENVG renderer's pure-function pieces -- the
// BlendState -> VGBlendMode mapping -- which needs no real SDL window/GL context/ShivaVG context
// to exercise. Real windowed behavior (Clear/Present/SpriteBatch draw/readback) is covered by
// modules/renderers/openvg/examples/ instead (this module's own smoke and rotation-oracle tests),
// matching the established split every other renderer with a tests/ directory uses (see e.g.
// modules/renderers/canvas/tests/... 's own header comment).
#include <gtest/gtest.h>

// plan_runtimerenderer.md RTR-P9-9: PRESENT_, not the identity macro. This suite is
// device-free policy coverage for its own renderer, so it is worth compiling and running
// whenever that renderer is COMPILED IN -- in a multi-renderer build it need not be the
// selected one. Only the default renderer's CNA_RENDERER_OPENVG is defined project-wide.
#if defined(CNA_RENDERER_OPENVG) || defined(CNA_RENDERER_PRESENT_OPENVG)
#include "CNA/Internal/Renderers/OpenVg/OpenVgRenderer.hpp"

using namespace CNA::Internal::Renderers::OpenVg;

TEST(OpenVgBlendStateMapping, StandardPresetsMapCorrectly)
{
    // VG_BLEND_SRC = 0x2000, VG_BLEND_SRC_OVER = 0x2001 (openvg.h VGBlendMode).
    EXPECT_EQ(BlendStateToVgBlendMode(0, 0, 1, 1, 0, 0), 0x2000); // Opaque -> VG_BLEND_SRC
    EXPECT_EQ(BlendStateToVgBlendMode(4, 4, 5, 5, 0, 0), 0x2001); // NonPremultiplied -> VG_BLEND_SRC_OVER
    EXPECT_EQ(BlendStateToVgBlendMode(0, 0, 5, 5, 0, 0), 0x2001); // AlphaBlend -> VG_BLEND_SRC_OVER (documented caveat)
}

TEST(OpenVgBlendStateMapping, AdditiveThrows)
{
    // ShivaVG declares VG_BLEND_ADDITIVE but never implements it (falls through to normal alpha
    // blending) -- accepting it here would silently mis-render, so it must throw rather than
    // report success. Additive: srcBlend=SourceAlpha(4), dstBlend=One(0).
    EXPECT_THROW(BlendStateToVgBlendMode(4, 4, 0, 0, 0, 0), std::runtime_error);
}

TEST(OpenVgBlendStateMapping, AsymmetricColorAlphaFactorsThrow)
{
    EXPECT_THROW(BlendStateToVgBlendMode(0, 4, 1, 1, 0, 0), std::runtime_error);
}

TEST(OpenVgBlendStateMapping, NonAddBlendFunctionThrows)
{
    EXPECT_THROW(BlendStateToVgBlendMode(0, 0, 1, 1, 1, 1), std::runtime_error);
}

TEST(OpenVgBlendStateMapping, ArbitraryCustomBlendStateThrows)
{
    EXPECT_THROW(BlendStateToVgBlendMode(2, 2, 3, 3, 0, 0), std::runtime_error);
}
#endif // CNA_RENDERER_OPENVG / CNA_RENDERER_PRESENT_OPENVG
