// SPDX-License-Identifier: MS-PL
#if __has_include(<mojoshader.h>)
#include "CNA/Internal/Renderers/MojoShader/SpirvPipelineAdaptation.hpp"
#include <gtest/gtest.h>
#include <vector>
using namespace CNA::Internal::Renderers::MojoShaderEffect;
TEST(MojoShaderAdaptationBoundary, MalformedInstructionStreamsRefuseWithoutRewritingInput) {
    // SPIR-V binary grammar: five-word header; high 16 bits encode instruction word count.
    // Zero count and an instruction extending past the buffer are invalid; no rewrite is safe.
    for(const std::vector<std::uint32_t> words:{
        std::vector<std::uint32_t>{0x07230203,0x00010000,0,8,0,0},
        std::vector<std::uint32_t>{0x07230203,0x00010000,0,8,0,(4u<<16)|71u,1}}) {
        for(const auto& result:{MirrorFrontFacing(words.data(),words.size()),
                               DropUnattachedColorOutputs(words.data(),words.size(),1),
                               OffsetClipPosition(words.data(),words.size(),.25f,-.25f)}) {
            EXPECT_EQ(result.error,"malformed SPIR-V instruction stream");
            EXPECT_FALSE(result.changed); EXPECT_EQ(result.words,words);
        }
    }
}
TEST(MojoShaderAdaptationBoundary, AModuleWithoutRelevantInterfacesRemainsByteIdentical) {
    // Header-only module has no front-facing loads or colour outputs for these transforms.
    // This is a walker/no-op contract, not a claim that an empty module is a valid shader.
    const std::vector<std::uint32_t> words{0x07230203,0x00010000,0,1,0};
    for(const auto& result:{MirrorFrontFacing(words.data(),words.size()),
                           DropUnattachedColorOutputs(words.data(),words.size(),1),
                           OffsetClipPosition(words.data(),words.size(),0,0)}) {
        EXPECT_TRUE(result.error.empty()); EXPECT_FALSE(result.changed); EXPECT_EQ(result.words,words);
    }
}
#endif
