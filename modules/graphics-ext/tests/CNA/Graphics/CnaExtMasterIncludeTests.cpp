// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Graphics/CNAEXT.hpp"
#ifdef CNA_CNAEXT
TEST(CnaExtMasterIncludeTest, RetainedTypesAreComplete) {
    EXPECT_GT(sizeof(CNA::Graphics::AsciiPostProcessEffect), 0u);
    EXPECT_GT(sizeof(CNA::Graphics::CRTEffect), 0u);
    EXPECT_GT(sizeof(CNA::Graphics::DepthEffect), 0u);
    EXPECT_GT(sizeof(CNA::Graphics::DebugDraw), 0u);
    EXPECT_GT(sizeof(CNA::Graphics::ShaderCodeEXT), 0u);
    EXPECT_GT(sizeof(CNA::Graphics::ShaderPackageEXT), 0u);
    EXPECT_EQ(static_cast<int>(CNA::Graphics::AsciiQuantizeMode::BlackWhite), 0);
    EXPECT_EQ(static_cast<int>(CNA::Graphics::CRTMaskType::None), 0);
    EXPECT_EQ(static_cast<int>(CNA::Graphics::DepthEffectMode::Color16Bit), 0);
    EXPECT_EQ(static_cast<int>(CNA::Graphics::DitherMode::None), 0);
}
#endif // CNA_CNAEXT
