// SPDX-License-Identifier: MS-PL
// ISSUE-113: distinct queries have independent XNA Begin/End intervals.
#include <array>
#include <chrono>
#include <memory>
#include <gtest/gtest.h>
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class OcclusionOverlapTest : public testing::Test
{
protected:
    std::unique_ptr<GraphicsDevice> device;
    std::unique_ptr<BasicEffect> effect;
    int unitCount = 0;
    bool precise = false;
    void Draw(int count)
    {
        const std::array<VertexPositionColor, 3> vertices = {
            VertexPositionColor(Vector3(-0.5f, -0.5f, 0.5f), Color::White),
            VertexPositionColor(Vector3(0, 0.5f, 0.5f), Color::White),
            VertexPositionColor(Vector3(0.5f, -0.5f, 0.5f), Color::White)};
        effect->Apply();
        for (int i = 0; i < count; ++i)
            device->DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 1);
    }
    int Result(OcclusionQuery& query)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        device->Present();
        while (!query.getIsCompleteProperty())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                ADD_FAILURE() << "Query did not complete after its interval ended";
                return -1;
            }
            device->Present();
        }
        return query.getPixelCountProperty();
    }
    int Expected(int triangles) const { return triangles == 0 ? 0 : precise ? unitCount * triangles : 1; }
    void SetUp() override
    {
        PresentationParameters pp = PresentationParameters::CreateWindowedEXT(800, 480);
        pp.setBackBufferWidthProperty(64);
        pp.setBackBufferHeightProperty(64);
        pp.setDepthStencilFormatProperty(DepthFormat::None);
        pp.setPresentationIntervalProperty(PresentInterval::Immediate);
        device = std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, pp);
        RecordProperty("renderer", std::string(device->GetGraphicsRendererName()));
        if (!device->SupportsCapability(CNA::GraphicsCapability::OcclusionQuery))
            GTEST_SKIP() << "The renderer explicitly lacks occlusion queries";
        effect = std::make_unique<BasicEffect>(*device);
        effect->setVertexColorEnabledProperty(true);
        device->setRasterizerStateProperty(RasterizerState::CullNone);
        device->setDepthStencilStateProperty(DepthStencilState::None);
        device->setBlendStateProperty(BlendState::Opaque);
        device->Clear(Color::Black);
        OcclusionQuery control(*device);
        control.Begin(); Draw(1); control.End();
        unitCount = Result(control);
        precise = control.isPixelCountPreciseEXT();
        ASSERT_GT(unitCount, 0);
    }
};

TEST_F(OcclusionOverlapTest, NestedIntervalsAccumulateIndependently)
{
    OcclusionQuery owner(*device), contender(*device);
    owner.Begin(); Draw(1);
    ASSERT_NO_THROW(contender.Begin()); Draw(2);
    contender.End(); Draw(4); owner.End();
    EXPECT_EQ(Result(owner), Expected(7));
    EXPECT_EQ(Result(contender), Expected(2));
}

TEST_F(OcclusionOverlapTest, CrossedIntervalsAccumulateIndependently)
{
    OcclusionQuery owner(*device), contender(*device);
    owner.Begin(); Draw(1);
    ASSERT_NO_THROW(contender.Begin()); Draw(2);
    owner.End(); Draw(4); contender.End();
    EXPECT_EQ(Result(owner), Expected(3));
    EXPECT_EQ(Result(contender), Expected(6));
}

TEST_F(OcclusionOverlapTest, DisposingInnerDoesNotEndOuter)
{
    OcclusionQuery owner(*device), contender(*device);
    owner.Begin(); Draw(1); contender.Begin(); Draw(2);
    contender.Dispose(); Draw(4); owner.End();
    EXPECT_EQ(Result(owner), Expected(7));
}

TEST_F(OcclusionOverlapTest, DisposingOuterDoesNotEndInner)
{
    OcclusionQuery owner(*device), contender(*device);
    owner.Begin(); Draw(1); contender.Begin(); Draw(2);
    owner.Dispose(); Draw(4); contender.End();
    EXPECT_EQ(Result(contender), Expected(6));
}

TEST_F(OcclusionOverlapTest, EmptyInnerDoesNotStealOuterSamples)
{
    OcclusionQuery owner(*device), contender(*device);
    owner.Begin(); Draw(1); contender.Begin(); contender.End(); Draw(1); owner.End();
    EXPECT_EQ(Result(owner), Expected(2));
    EXPECT_EQ(Result(contender), 0);
}

TEST_F(OcclusionOverlapTest, InnerReceivesSamplesAfterEmptyOuterEnds)
{
    OcclusionQuery owner(*device), contender(*device);
    owner.Begin(); contender.Begin(); owner.End(); Draw(1); contender.End();
    EXPECT_EQ(Result(owner), 0);
    EXPECT_EQ(Result(contender), Expected(1));
}

TEST_F(OcclusionOverlapTest, ManyUnstartedQueriesLeaveNativeCapacityForActiveIntervals)
{
    std::array<std::unique_ptr<OcclusionQuery>, 40> queries;
    for (auto& query : queries) query = std::make_unique<OcclusionQuery>(*device);
    queries.front()->Begin(); Draw(1); queries.back()->Begin(); Draw(2);
    queries.back()->End(); Draw(4); queries.front()->End();
    EXPECT_EQ(Result(*queries.front()), Expected(7));
    EXPECT_EQ(Result(*queries.back()), Expected(2));
}

TEST_F(OcclusionOverlapTest, ManyNestedQueriesPreserveEveryIndependentResult)
{
    std::array<std::unique_ptr<OcclusionQuery>, 40> queries;
    for (auto& query : queries)
    {
        query = std::make_unique<OcclusionQuery>(*device);
        query->Begin(); Draw(1);
    }
    for (auto i = queries.rbegin(); i != queries.rend(); ++i)
    {
        Draw(1); (*i)->End();
    }
    for (std::size_t i = 0; i < queries.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(Result(*queries[i]), Expected(static_cast<int>(2 * (queries.size() - i))));
    }
}
}
