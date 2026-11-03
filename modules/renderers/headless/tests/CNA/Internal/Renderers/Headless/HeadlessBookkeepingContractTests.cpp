// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_HEADLESS)
#include "CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp"
#include <gtest/gtest.h>
#include <array>
using namespace CNA::Internal::Renderers::Headless;

TEST(HeadlessBookkeepingContract, CountersAndResourceLifetimesRemainIndependent) {
    // docs/headless-renderer.md: cumulative counts survive frame boundaries and destruction;
    // AliveResources is a current-lifetime registry, not a creation counter.
    HeadlessRenderer renderer{4,3};
    renderer.SetMode(HeadlessMode::Validation);
    EXPECT_TRUE(renderer.AliveResources().empty());
    {
        auto vertex=renderer.CreateVertexBuffer(3);
        auto index=renderer.CreateIndexBuffer16(3);
        ASSERT_NE(vertex,nullptr); ASSERT_NE(index,nullptr);
        EXPECT_EQ(renderer.AliveResources().size(),2u);
        EXPECT_THROW(renderer.AssertNoLeaks(),HeadlessValidationException);
        EXPECT_EQ(renderer.GetStatistics().vertexBuffersCreated,1u);
        EXPECT_EQ(renderer.GetStatistics().indexBuffersCreated,1u);
        renderer.Clear(1,0,0,1); renderer.Clear(0,1,0,1); renderer.Present();
        renderer.Clear(0,0,1,1); renderer.Present();
        EXPECT_EQ(renderer.GetStatistics().clearCount,3u);
        EXPECT_EQ(renderer.GetStatistics().presentCount,2u);
        vertex.reset();
        EXPECT_EQ(renderer.AliveResources().size(),1u);
    }
    EXPECT_TRUE(renderer.AliveResources().empty());
    EXPECT_EQ(renderer.GetStatistics().vertexBuffersCreated,1u);
    EXPECT_EQ(renderer.GetStatistics().indexBuffersCreated,1u);
}

TEST(HeadlessBookkeepingContract, RenderTargetReadbackRefusesWithoutTouchingDestination) {
    // docs/headless-renderer.md: no rasterization means no fabricated colour readback.
    HeadlessRenderer renderer{4,3};
    auto target=renderer.CreateRenderTarget2D(2,2,0,false,false,0);
    ASSERT_NE(target,nullptr);
    renderer.SetRenderTarget2D(target.get()); renderer.Clear(1,0,0,1);
    std::array<std::uint8_t,18> result; result.fill(0xa5);
    EXPECT_FALSE(target->GetData(0,0,0,2,2,result.data()+1,16));
    for(auto byte:result) EXPECT_EQ(byte,0xa5);
    renderer.SetRenderTarget2D(nullptr);
}

TEST(HeadlessBookkeepingContract, TraceDiffDetectsContentAndPrefixButIgnoresCallIndex) {
    // CompareTraceLogs' documented contract compares frame/method/args, not redundant callIndex.
    const std::vector<HeadlessTraceEntry> baseline{{1,0,"Clear","red"},{2,1,"Present",""}};
    auto changed=baseline;
    changed[0].callIndex=99;
    EXPECT_TRUE(CompareTraceLogs(baseline,changed).identical);
    for(int field=0;field<3;++field) {
        changed=baseline;
        if(field==0) changed[1].frameIndex=2;
        if(field==1) changed[1].method="Clear";
        if(field==2) changed[1].argsSummary="blue";
        const auto diff=CompareTraceLogs(baseline,changed);
        EXPECT_FALSE(diff.identical); EXPECT_EQ(diff.firstDivergingIndex,1u);
    }
    changed=baseline; changed.pop_back();
    const auto prefix=CompareTraceLogs(baseline,changed);
    EXPECT_FALSE(prefix.identical); EXPECT_EQ(prefix.firstDivergingIndex,1u);
}
#endif
