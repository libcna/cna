// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/EasyGL/GlPresentRegion.hpp"
#include <climits>

namespace CNA::Internal::Renderers::EasyGL
{
    TEST(GlPresentRegion, SourceAndDestinationUseTheirOwnFramebufferHeights)
    {
        GpuPresentMappingEXT m;
        m.source = {24,18,24,18}; m.destination = {16,8,48,36};
        const auto blit = BuildGlPresentBlitEXT(m,64,48,80);
        ASSERT_TRUE(blit.has_value());
        // Source [24,48)x[18,36) -> GL y=[48-36,48-18)=[12,30).
        // Destination [16,64)x[8,44) -> GL y=[80-44,80-8)=[36,72).
        EXPECT_EQ(blit->source,(std::array<int,4>{24,12,48,30}));
        EXPECT_EQ(blit->destination,(std::array<int,4>{16,36,64,72}));
    }
    TEST(GlPresentRegion, DestinationClippingDoesNotReplaceOriginalEndpoints)
    {
        GpuPresentMappingEXT m;
        m.source = {0,0,64,48}; m.destination = {-32,-24,64,48}; m.clip = {0,0,32,24};
        const auto blit = BuildGlPresentBlitEXT(m,64,48,48);
        ASSERT_TRUE(blit.has_value());
        // Original affine map dx=sx-32,dy=sy-24. The GPU clips writes, not the input map.
        EXPECT_EQ(blit->source,(std::array<int,4>{0,0,64,48}));
        EXPECT_EQ(blit->destination,(std::array<int,4>{-32,24,32,72}));
    }
    TEST(GlPresentRegion, FractionalClampedAndHugeMappingsUseThePixelFallback)
    {
        GpuPresentMappingEXT m;
        m.source = {0.25f,0,16,12}; m.destination = {0,0,32,24};
        EXPECT_FALSE(BuildGlPresentBlitEXT(m,64,48,48)); // 1/4 cannot be an integer GL endpoint.
        m.source = {-8,0,64,48};
        EXPECT_FALSE(BuildGlPresentBlitEXT(m,64,48,48)); // Overscan must clamp, not drop pixels.
        m.source = {0,0,64,48}; m.destination = {INT_MAX-2,0,8,8};
        EXPECT_FALSE(BuildGlPresentBlitEXT(m,64,48,48)); // right=INT_MAX+6, computed in int64.
    }
    TEST(GlPresentRegion, ClippedPixelFallbackKeepsTheMapAndTheParentStride)
    {
        const std::vector<std::uint8_t> source{
            255,0,0,255, 0,255,0,255, 17,18,19,20,
            0,0,255,255, 255,255,255,255, 21,22,23,24};
        GpuPresentMappingEXT m;
        m.destination = {-2,1,4,4}; m.clip = {0,1,2,4};
        const auto patch = ClipGlPresentPixelsEXT({source.data(),2,2,12},m);
        // Scale2 exposes only the right source column: 2 rows G, then 2 rows W.
        EXPECT_EQ(patch.destination.x,0); EXPECT_EQ(patch.destination.y,1);
        EXPECT_EQ(patch.pixels,(std::vector<std::uint8_t>{
            0,255,0,255, 0,255,0,255, 0,255,0,255, 0,255,0,255,
            255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255}));
        EXPECT_EQ(source[8],17); EXPECT_EQ(source[20],21); // borrowed parent padding is untouched.
    }
    TEST(GlPresentRegion, NonintegerScaleSamplesVisiblePixelCenters)
    {
        const std::uint8_t source[]{255,0,0,255, 0,255,0,255, 0,0,255,255};
        GpuPresentMappingEXT m;
        m.destination = {-1,0,5,1}; m.clip = {0,0,4,1};
        const auto patch = ClipGlPresentPixelsEXT({source,3,1,0},m);
        // floor((x+1.5)*3/5) at x=0,1,2,3 is 0,1,2,2 -> R,G,B,B.
        EXPECT_EQ(patch.pixels,(std::vector<std::uint8_t>{
            255,0,0,255, 0,255,0,255, 0,0,255,255, 0,0,255,255}));
    }
}
