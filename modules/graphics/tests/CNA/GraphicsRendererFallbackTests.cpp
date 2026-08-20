// plans/plan_runtimerenderer.md RTR-P5-18/19/20: the fallback chain's behaviour.
//
// A single-renderer build can still exercise real substitution, and these tests do: naming a
// renderer this build does NOT contain as the preference, with the built one as the chain, makes
// resolution genuinely reject the first candidate and genuinely fall through to the second. That
// covers the NotCompiledIn path, the history, and GetActive() diverging from GetSelected() without
// needing a multi-renderer build.
//
// What a single-renderer build cannot exercise is a candidate whose isAvailable() probe fails or
// whose construction throws while ANOTHER real renderer is present to take over -- that needs two
// working renderers in one binary, and lands with the first multi-renderer set (RTR-P8-5/P8-6).

#include <gtest/gtest.h>

#include "CNA/GraphicsRendererFallbackRecord.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererRegistry.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "System/InvalidOperationException.hpp"

#include <algorithm>
#include <string>
#include <vector>

using CNA::GraphicsRendererFallbackReason;
using CNA::GraphicsRendererSelection;
using CNA::GraphicsRendererType;

namespace
{
    class GraphicsRendererFallbackTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            GraphicsRendererSelection::ResetForTestingEXT();
            namespace Renderers = CNA::Internal::Renderers;
            std::vector<GraphicsRendererType> available;
            for (const auto& descriptor : Renderers::GraphicsRendererRegistry::All())
                available.push_back(descriptor.type);
            CNA::GraphicsRendererSelectionAccessEXT::PublishAvailable(
                available, Renderers::GraphicsRendererRegistry::Default().type);
        }

        void TearDown() override { GraphicsRendererSelection::ResetForTestingEXT(); }

        [[nodiscard]] static GraphicsRendererType Built()
        {
            return CNA::Internal::Renderers::GraphicsRendererRegistry::Default().type;
        }

        [[nodiscard]] static GraphicsRendererType Absent()
        {
            for (int ordinal = 0;
                 ordinal <= static_cast<int>(GraphicsRendererType::PortableGL); ++ordinal)
            {
                const auto candidate = static_cast<GraphicsRendererType>(ordinal);
                if (!GraphicsRendererSelection::IsAvailable(candidate))
                    return candidate;
            }
            ADD_FAILURE() << "this test needs an identity that is not compiled in";
            return GraphicsRendererType::Stub;
        }
    };
}

TEST_F(GraphicsRendererFallbackTest, WithoutFallbackTheChainIsExactlyOneAttempt)
{
    // Design decision 6, stated as a property: with fallback off there is nothing to fall back to,
    // so any failure is the game's error and propagates unchanged.
    EXPECT_EQ(CNA::GraphicsRendererSelectionAccessEXT::GetAttemptOrder().size(), 1u);
    EXPECT_FALSE(GraphicsRendererSelection::IsFallbackEnabled());
}

TEST_F(GraphicsRendererFallbackTest, HistoryIsEmptyWhenThePreferredRendererWorksFirstTime)
{
    // The overwhelmingly common case, and the only possible one while fallback stays disabled.
    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
    EXPECT_TRUE(GraphicsRendererSelection::GetFallbackHistory().empty());
    EXPECT_EQ(GraphicsRendererSelection::GetActive(), GraphicsRendererSelection::GetSelected());
}

TEST_F(GraphicsRendererFallbackTest, RealSubstitutionRecordsTheRejectionAndUsesTheNextCandidate)
{
    // A genuine end-to-end fallback: the preferred renderer is not in this build, the chain names
    // one that is, and resolution has to notice and move on.
    GraphicsRendererSelection::SetFallbackChain(std::vector<GraphicsRendererType>{Built()});
    GraphicsRendererSelection::SetPreferred(Absent());
    ASSERT_EQ(GraphicsRendererSelection::GetSelected(), Absent());

    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;

    // GetActive() reports what was really created, and it differs from what was asked for.
    EXPECT_EQ(GraphicsRendererSelection::GetActive(), Built());
    EXPECT_NE(GraphicsRendererSelection::GetActive(), GraphicsRendererSelection::GetSelected());

    const auto history = GraphicsRendererSelection::GetFallbackHistory();
    ASSERT_EQ(history.size(), 1u);
    EXPECT_EQ(history.front().type, Absent());
    EXPECT_EQ(history.front().reason, GraphicsRendererFallbackReason::NotCompiledIn);
    EXPECT_FALSE(history.front().message.empty());
}

TEST_F(GraphicsRendererFallbackTest, SubstitutionStillLatchesTheSelection)
{
    GraphicsRendererSelection::SetFallbackChain(std::vector<GraphicsRendererType>{Built()});
    GraphicsRendererSelection::SetPreferred(Absent());

    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;

    EXPECT_TRUE(GraphicsRendererSelection::IsLatched());
    EXPECT_THROW(GraphicsRendererSelection::SetPreferred(Built()),
                 System::InvalidOperationException);
}

TEST_F(GraphicsRendererFallbackTest, ExhaustedChainThrowsAndNamesEveryAttempt)
{
    // Nothing in the chain is compiled in, so there is no candidate left. Design decision 7: this
    // is an error, and the message has to explain what was tried -- otherwise the game author sees
    // only "no renderer" with no way to act on it.
    const std::vector<GraphicsRendererType> impossible{Absent()};
    GraphicsRendererSelection::SetFallbackChain(impossible);
    GraphicsRendererSelection::SetPreferred(Absent());

    try
    {
        Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
        FAIL() << "expected construction to throw when no candidate can be created";
    }
    catch (const System::InvalidOperationException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find(CNA::getGraphicsRendererName(Absent())), std::string::npos);
        EXPECT_NE(message.find("no graphics renderer could be created"), std::string::npos);
    }

    // Every attempt is recorded, not only the last.
    const auto history = GraphicsRendererSelection::GetFallbackHistory();
    EXPECT_FALSE(history.empty());
    for (const auto& record : history)
    {
        EXPECT_EQ(record.reason, GraphicsRendererFallbackReason::NotCompiledIn);
    }
}

TEST_F(GraphicsRendererFallbackTest, AFailedResolutionDoesNotLatch)
{
    // Nothing was created, so the choice must stay open -- a game catching the error and trying a
    // different configuration is exactly the situation this needs to support.
    GraphicsRendererSelection::SetFallbackChain(std::vector<GraphicsRendererType>{Absent()});
    GraphicsRendererSelection::SetPreferred(Absent());

    EXPECT_THROW({ Microsoft::Xna::Framework::Graphics::GraphicsDevice device; },
                 System::InvalidOperationException);

    EXPECT_FALSE(GraphicsRendererSelection::IsLatched());
    EXPECT_THROW((void)GraphicsRendererSelection::GetActive(),
                 System::InvalidOperationException);
    EXPECT_NO_THROW(GraphicsRendererSelection::SetPreferred(Built()));
}

TEST_F(GraphicsRendererFallbackTest, RecoveringAfterAFailedResolutionActuallyWorks)
{
    GraphicsRendererSelection::SetFallbackChain(std::vector<GraphicsRendererType>{Absent()});
    GraphicsRendererSelection::SetPreferred(Absent());
    EXPECT_ANY_THROW({ Microsoft::Xna::Framework::Graphics::GraphicsDevice failed; });

    GraphicsRendererSelection::SetPreferred(Built());
    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
    EXPECT_EQ(GraphicsRendererSelection::GetActive(), Built());
}

TEST_F(GraphicsRendererFallbackTest, ReconstructionAfterSubstitutionKeepsTheSubstitutedRenderer)
{
    // RTR-P5-17: a reconstruction must rebuild the renderer this device RESOLVED to, never
    // re-resolve. Re-resolving would start again from a preference that already failed once.
    GraphicsRendererSelection::SetFallbackChain(std::vector<GraphicsRendererType>{Built()});
    GraphicsRendererSelection::SetPreferred(Absent());

    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
    ASSERT_EQ(GraphicsRendererSelection::GetActive(), Built());

    const std::size_t historyBefore = GraphicsRendererSelection::GetFallbackHistory().size();
    EXPECT_NO_THROW(device.RecreateRendererForMultiSampleCount(1));

    EXPECT_EQ(GraphicsRendererSelection::GetActive(), Built());
    // Reconstruction is not a fresh resolution, so it must not append to the history either.
    EXPECT_EQ(GraphicsRendererSelection::GetFallbackHistory().size(), historyBefore);
}

TEST_F(GraphicsRendererFallbackTest, ChainEntriesAreTriedInOrderAndNotDuplicated)
{
    const std::vector<GraphicsRendererType> chain{Absent(), Built(), Built()};
    GraphicsRendererSelection::SetFallbackChain(chain);
    GraphicsRendererSelection::SetPreferred(Absent());

    const auto order = CNA::GraphicsRendererSelectionAccessEXT::GetAttemptOrder();
    ASSERT_GE(order.size(), 2u);
    EXPECT_EQ(order.front(), Absent());
    EXPECT_EQ(std::count(order.begin(), order.end(), Built()), 1);
    EXPECT_EQ(std::count(order.begin(), order.end(), Absent()), 1);
}

TEST_F(GraphicsRendererFallbackTest, AutomaticFallbackResolvesToThisBuildsRenderer)
{
    GraphicsRendererSelection::EnableAutomaticFallback(true);
    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
    EXPECT_EQ(GraphicsRendererSelection::GetActive(), Built());
}
