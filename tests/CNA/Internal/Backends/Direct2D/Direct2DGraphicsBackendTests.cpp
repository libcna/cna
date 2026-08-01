// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#if defined(CNA_BACKEND_DIRECT2D)
#include "CNA/Internal/Backends/Direct2D/Direct2DGraphicsBackend.hpp"

using CNA::Internal::Backends::Direct2D::BlendStateToDirect2DBlendMode;
using CNA::Internal::Backends::Direct2D::Direct2DBlendMode;

TEST(Direct2DBlendStateMapping, StandardSpriteBatchPresetsMapToNativePrimitiveBlends)
{
    EXPECT_EQ(BlendStateToDirect2DBlendMode(0, 0, 1, 1, 0, 0), Direct2DBlendMode::Copy);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(0, 0, 5, 5, 0, 0), Direct2DBlendMode::SourceOver);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(4, 4, 5, 5, 0, 0), Direct2DBlendMode::SourceOver);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(4, 4, 0, 0, 0, 0), Direct2DBlendMode::Add);
}

TEST(Direct2DBlendStateMapping, UnsupportedFactorOrEquationFailsExplicitly)
{
    EXPECT_THROW(BlendStateToDirect2DBlendMode(0, 4, 5, 5, 0, 0), std::runtime_error);
    EXPECT_THROW(BlendStateToDirect2DBlendMode(2, 2, 3, 3, 0, 0), std::runtime_error);
    EXPECT_THROW(BlendStateToDirect2DBlendMode(0, 0, 5, 5, 1, 1), std::runtime_error);
}
#endif
