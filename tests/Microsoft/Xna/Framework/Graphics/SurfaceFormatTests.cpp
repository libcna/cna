// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

// XNA 4.0 ordinal values (0–19) match FNA source.

TEST(SurfaceFormatTest, ColorIsZero)      { EXPECT_EQ(static_cast<int>(SurfaceFormat::Color),           0); }
TEST(SurfaceFormatTest, Bgr565IsOne)      { EXPECT_EQ(static_cast<int>(SurfaceFormat::Bgr565),          1); }
TEST(SurfaceFormatTest, Bgra5551IsTwo)    { EXPECT_EQ(static_cast<int>(SurfaceFormat::Bgra5551),        2); }
TEST(SurfaceFormatTest, Bgra4444IsThree)  { EXPECT_EQ(static_cast<int>(SurfaceFormat::Bgra4444),        3); }
TEST(SurfaceFormatTest, Dxt1IsFour)       { EXPECT_EQ(static_cast<int>(SurfaceFormat::Dxt1),            4); }
TEST(SurfaceFormatTest, Dxt3IsFive)       { EXPECT_EQ(static_cast<int>(SurfaceFormat::Dxt3),            5); }
TEST(SurfaceFormatTest, Dxt5IsSix)        { EXPECT_EQ(static_cast<int>(SurfaceFormat::Dxt5),            6); }
TEST(SurfaceFormatTest, NormByte2Is7)     { EXPECT_EQ(static_cast<int>(SurfaceFormat::NormalizedByte2), 7); }
TEST(SurfaceFormatTest, NormByte4Is8)     { EXPECT_EQ(static_cast<int>(SurfaceFormat::NormalizedByte4), 8); }
TEST(SurfaceFormatTest, Rgba1010102Is9)   { EXPECT_EQ(static_cast<int>(SurfaceFormat::Rgba1010102),     9); }
TEST(SurfaceFormatTest, Rg32Is10)         { EXPECT_EQ(static_cast<int>(SurfaceFormat::Rg32),            10); }
TEST(SurfaceFormatTest, Rgba64Is11)       { EXPECT_EQ(static_cast<int>(SurfaceFormat::Rgba64),          11); }
TEST(SurfaceFormatTest, Alpha8Is12)       { EXPECT_EQ(static_cast<int>(SurfaceFormat::Alpha8),          12); }
TEST(SurfaceFormatTest, SingleIs13)       { EXPECT_EQ(static_cast<int>(SurfaceFormat::Single),          13); }
TEST(SurfaceFormatTest, Vector2Is14)      { EXPECT_EQ(static_cast<int>(SurfaceFormat::Vector2),         14); }
TEST(SurfaceFormatTest, Vector4Is15)      { EXPECT_EQ(static_cast<int>(SurfaceFormat::Vector4),         15); }
TEST(SurfaceFormatTest, HalfSingleIs16)   { EXPECT_EQ(static_cast<int>(SurfaceFormat::HalfSingle),      16); }
TEST(SurfaceFormatTest, HalfVector2Is17)  { EXPECT_EQ(static_cast<int>(SurfaceFormat::HalfVector2),     17); }
TEST(SurfaceFormatTest, HalfVector4Is18)  { EXPECT_EQ(static_cast<int>(SurfaceFormat::HalfVector4),     18); }
TEST(SurfaceFormatTest, HdrBlendableIs19) { EXPECT_EQ(static_cast<int>(SurfaceFormat::HdrBlendable),    19); }
