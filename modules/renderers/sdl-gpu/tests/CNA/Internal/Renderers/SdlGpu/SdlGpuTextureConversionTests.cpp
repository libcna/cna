// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_SDLGPU) || defined(CNA_RENDERER_PRESENT_SDLGPU)
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuStateMapping.hpp"
#include <gtest/gtest.h>

using namespace CNA::Internal::Renderers::SdlGpu::Detail;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

TEST(SdlGpuTextureConversion, NarrowAndNonPowerOfTwoMipCounts)
{
    // Halve each dimension with floor, clamp each axis to one; include level zero.
    for (const auto [width,height,levels] : {std::array{1,1,1}, {1,8,4}, {8,1,4},
         {7,3,3}, {3,9,4}, {16,2,5}, {1,257,9}}) {
        EXPECT_EQ(CalculateMipLevels(width,height),levels) << width << 'x' << height;
    }
}

TEST(SdlGpuTextureConversion, PackedFormatsExpandLiteralBitPatterns)
{
    // D3D packed layouts: 565=rrrrrggggggbbbbb, 5551=arrrrrgggggbbbbb,
    // 4444=aaaarrrrggggbbbb (one nibble per A/R/G/B). Expand n bits by replication.
    const std::uint8_t bgr[]{0x00,0xf8, 0xe0,0x07, 0x1f,0x00, 0x10,0x84};
    EXPECT_EQ(ConvertTextureLevelForFallback(SurfaceFormat::Bgr565,bgr,sizeof bgr,2,2),
        (std::vector<std::uint8_t>{255,0,0,255, 0,255,0,255, 0,0,255,255, 132,130,132,255}));
    const std::uint8_t bgra1[]{0x00,0xfc, 0x1f,0x00};
    EXPECT_EQ(ConvertTextureLevelForFallback(SurfaceFormat::Bgra5551,bgra1,sizeof bgra1,2,1),
        (std::vector<std::uint8_t>{255,0,0,255, 0,0,255,0}));
    const std::uint8_t bgra4[]{0x23,0xa1, 0xf0,0x0f};
    EXPECT_EQ(ConvertTextureLevelForFallback(SurfaceFormat::Bgra4444,bgra4,sizeof bgra4,1,2),
        (std::vector<std::uint8_t>{17,34,51,170, 255,255,0,0}));
}

TEST(SdlGpuTextureConversion, MissingChannelsHaveXnaFillValues)
{
    // D3D9 sampling: absent channels are one (SNORM +1 = 0x7f); Alpha8 is (0,0,0,A).
    const std::uint8_t snorm[]{0x80,0x7f,0xff,0x00};
    EXPECT_EQ(ConvertTextureLevelForFallback(SurfaceFormat::NormalizedByte2,snorm,4,2,1),
        (std::vector<std::uint8_t>{0x80,0x7f,0x7f,0x7f,0xff,0x00,0x7f,0x7f}));
    const std::uint8_t alpha[]{0,127,255};
    EXPECT_EQ(ConvertTextureLevelForFallback(SurfaceFormat::Alpha8,alpha,3,3,1),
        (std::vector<std::uint8_t>{0,0,0,0,0,0,0,127,0,0,0,255}));
    // IEEE-754 little-endian 2.0f/(-0.5f), followed by literal 1.0f in missing channels.
    const std::uint8_t single[]{0,0,0,0x40, 0,0,0,0xbf};
    EXPECT_EQ(ConvertTextureLevelForFallback(SurfaceFormat::Single,single,8,2,1),
        (std::vector<std::uint8_t>{0,0,0,0x40,0,0,0x80,0x3f,0,0,0x80,0x3f,0,0,0x80,0x3f,
                                  0,0,0,0xbf,0,0,0x80,0x3f,0,0,0x80,0x3f,0,0,0x80,0x3f}));
    EXPECT_EQ(ConvertTextureLevelForFallback(SurfaceFormat::Vector2,single,8,1,1),
        (std::vector<std::uint8_t>{0,0,0,0x40,0,0,0,0xbf,0,0,0x80,0x3f,0,0,0x80,0x3f}));
    // binary16: 2=0x4000, -0.5=0xb800, 1=0x3c00. UNORM16 one=0xffff.
    const std::uint8_t half[]{0,0x40,0,0xb8};
    EXPECT_EQ(ConvertTextureLevelForFallback(SurfaceFormat::HalfSingle,half,4,2,1),
        (std::vector<std::uint8_t>{0,0x40,0,0x3c,0,0x3c,0,0x3c,0,0xb8,0,0x3c,0,0x3c,0,0x3c}));
    EXPECT_EQ(ConvertTextureLevelForFallback(SurfaceFormat::HalfVector2,half,4,1,1),
        (std::vector<std::uint8_t>{0,0x40,0,0xb8,0,0x3c,0,0x3c}));
    EXPECT_EQ(ConvertTextureLevelForFallback(SurfaceFormat::Rg32,half,4,1,1),
        (std::vector<std::uint8_t>{0,0x40,0,0xb8,0xff,0xff,0xff,0xff}));
}

TEST(SdlGpuTextureConversion, DxtSubBlockLevelsDecodeExactTexels)
{
    // BC1: endpoint0=red 565, endpoint1=0, all indices 0. BC2 alpha nibbles all f;
    // BC3 alpha endpoint0=255 and all alpha indices 0. One block covers all 1..4 texels/axis.
    const std::uint8_t bc1[]{0,0xf8,0,0,0,0,0,0};
    const std::uint8_t bc2[]{255,255,255,255,255,255,255,255,0,0xf8,0,0,0,0,0,0};
    const std::uint8_t bc3[]{255,0,0,0,0,0,0,0,0,0xf8,0,0,0,0,0,0};
    for (int width : {1,2,3,4}) for (int height : {1,2,3,4}) {
        std::vector<std::uint8_t> expected;
        for (int i=0;i<width*height;++i) expected.insert(expected.end(),{255,0,0,255});
        EXPECT_EQ(ConvertTextureLevelForFallback(SurfaceFormat::Dxt1,bc1,8,width,height),expected);
        EXPECT_EQ(ConvertTextureLevelForFallback(SurfaceFormat::Dxt3,bc2,16,width,height),expected);
        EXPECT_EQ(ConvertTextureLevelForFallback(SurfaceFormat::Dxt5,bc3,16,width,height),expected);
    }
}

TEST(SdlGpuTextureConversion, NullAndUnsupportedConversionAreRefused)
{
    EXPECT_THROW((void)ConvertTextureLevelForFallback(SurfaceFormat::Bgr565,nullptr,2,1,1),std::invalid_argument);
    const std::uint8_t bytes[4]{};
    EXPECT_THROW((void)ConvertTextureLevelForFallback(static_cast<SurfaceFormat>(-1),bytes,4,1,1),std::invalid_argument);
}
#endif
