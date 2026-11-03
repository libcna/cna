// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_STUB)
#include "CNA/Internal/Renderers/Stub/StubRenderer.hpp"
#include <gtest/gtest.h>
#include <array>
using namespace CNA::Internal::Renderers::Stub;

TEST(StubRefusalContract, WrongIndexWidthThrowsAndPreservesPreviousCount) {
    // Stub's documented no-storage buffer retains the count, but rejects mixed index widths.
    StubIndexBufferRenderer narrow{7,false}, wide{11,true};
    const std::array<std::uint16_t,3> indices16{0,1,2};
    const std::array<std::uint32_t,4> indices32{0,1,2,3};
    narrow.SetData16(indices16.data(),3); wide.SetData32(indices32.data(),4);
    EXPECT_EQ(narrow.GetIndexCount(),3); EXPECT_EQ(wide.GetIndexCount(),4);
    EXPECT_THROW(narrow.SetData32(indices32.data(),4),std::runtime_error);
    EXPECT_THROW(wide.SetData16(indices16.data(),3),std::runtime_error);
    EXPECT_EQ(narrow.GetIndexCount(),3); EXPECT_EQ(wide.GetIndexCount(),4);
    EXPECT_FALSE(narrow.IsThirtyTwoBit()); EXPECT_TRUE(wide.IsThirtyTwoBit());
}

TEST(StubRefusalContract, TextureReadbackCannotFabricatePixels) {
    // ITextureRenderer GetData refusal contract; STUB has dimensions but no pixel storage.
    StubTextureRenderer texture{3,2};
    std::array<std::uint8_t,24> bytes; bytes.fill(0x5a);
    EXPECT_EQ(texture.GetWidth(),3); EXPECT_EQ(texture.GetHeight(),2);
    EXPECT_FALSE(texture.GetData(0,0,0,3,2,bytes.data(),bytes.size()));
    for(auto byte:bytes) EXPECT_EQ(byte,0x5a);
}
#endif
