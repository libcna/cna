// plan_runtimerenderer.md RTR-P4-13: the selection API's contract.
//
// These live in the graphics module's test tree, not core's, because a meaningful test needs the
// compiled-in renderer set to have been published -- and that is the graphics module's side of the
// handshake. The suite runs against whatever single renderer this build selected, so it must never
// hardcode one.

#include <gtest/gtest.h>

#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererRegistry.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"

#include <algorithm>
#include <string>
#include <vector>

using CNA::GraphicsRendererSelection;
using CNA::GraphicsRendererType;

namespace
{
    /// Publishing happens lazily, the first time GraphicsDevice resolves a descriptor. A test that
    /// only ever asks the selection layer would otherwise see an unpublished, empty world.
    void EnsurePublished()
    {
        namespace Renderers = CNA::Internal::Renderers;
        std::vector<GraphicsRendererType> available;
        for (const auto& descriptor : Renderers::GraphicsRendererRegistry::All())
            available.push_back(descriptor.type);
        CNA::GraphicsRendererSelectionAccessEXT::PublishAvailable(
            available, Renderers::GraphicsRendererRegistry::Default().type);
    }

    class GraphicsRendererSelectionTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            GraphicsRendererSelection::ResetForTestingEXT();
            EnsurePublished();
        }

        void TearDown() override
        {
            GraphicsRendererSelection::ResetForTestingEXT();
        }

        /// This build's own renderer -- the suite is renderer-agnostic on purpose.
        [[nodiscard]] static GraphicsRendererType Built()
        {
            return CNA::Internal::Renderers::GraphicsRendererRegistry::Default().type;
        }

        /// An identity that is definitely NOT compiled into this build.
        [[nodiscard]] static GraphicsRendererType Absent()
        {
            for (int ordinal = 0;
                 ordinal <= static_cast<int>(GraphicsRendererType::PortableGL); ++ordinal)
            {
                const auto candidate = static_cast<GraphicsRendererType>(ordinal);
                if (!GraphicsRendererSelection::IsAvailable(candidate))
                    return candidate;
            }
            ADD_FAILURE() << "every renderer identity is compiled in; this test needs an absent one";
            return GraphicsRendererType::Stub;
        }
    };
}

// ---------------------------------------------------------------------------
// Availability and defaults
// ---------------------------------------------------------------------------

TEST_F(GraphicsRendererSelectionTest, AvailableSetMatchesTheCompiledInRegistry)
{
    const auto available = GraphicsRendererSelection::GetAvailable();
    EXPECT_EQ(available.size(), CNA::Internal::Renderers::GraphicsRendererRegistry::Count());
    EXPECT_FALSE(available.empty());
    EXPECT_TRUE(GraphicsRendererSelection::IsAvailable(Built()));
}

TEST_F(GraphicsRendererSelectionTest, DefaultSelectionIsThisBuildsRenderer)
{
    // The whole point of design decision 1: a build that never calls this API behaves exactly as it
    // did before the API existed.
    EXPECT_EQ(GraphicsRendererSelection::GetSelected(), Built());
}

TEST_F(GraphicsRendererSelectionTest, QueryingSelectionDoesNotLatchIt)
{
    // Asking what is selected must never be the thing that freezes the selection -- otherwise a
    // diagnostic log line early in startup would silently forbid a later, legitimate choice.
    (void)GraphicsRendererSelection::GetSelected();
    (void)GraphicsRendererSelection::GetAvailable();
    EXPECT_FALSE(GraphicsRendererSelection::IsLatched());
    EXPECT_NO_THROW(GraphicsRendererSelection::SetPreferred(Built()));
}

// ---------------------------------------------------------------------------
// SetPreferred
// ---------------------------------------------------------------------------

TEST_F(GraphicsRendererSelectionTest, SetPreferredToTheCompiledInRendererIsAccepted)
{
    GraphicsRendererSelection::SetPreferred(Built());
    EXPECT_EQ(GraphicsRendererSelection::GetSelected(), Built());
}

TEST_F(GraphicsRendererSelectionTest, SetPreferredAcceptsTheCanonicalNameCaseInsensitively)
{
    const std::string canonical(CNA::getGraphicsRendererName(Built()));

    GraphicsRendererSelection::SetPreferred(canonical);
    EXPECT_EQ(GraphicsRendererSelection::GetSelected(), Built());

    std::string lowered = canonical;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    GraphicsRendererSelection::ResetForTestingEXT();
    EnsurePublished();
    GraphicsRendererSelection::SetPreferred(lowered);
    EXPECT_EQ(GraphicsRendererSelection::GetSelected(), Built());
}

TEST_F(GraphicsRendererSelectionTest, SetPreferredRejectsAnUnknownName)
{
    // Not a renderer identity at all -- an argument error, distinct from "real identity, not in
    // this build" below.
    EXPECT_THROW(GraphicsRendererSelection::SetPreferred("NOT_A_RENDERER"),
                 System::ArgumentException);
    EXPECT_FALSE(GraphicsRendererSelection::IsLatched());
}

TEST_F(GraphicsRendererSelectionTest, SetPreferredRejectsARendererThatIsNotCompiledIn)
{
    // Design decision 6: hard failure, never a silent downgrade to whatever this build does have.
    EXPECT_THROW(GraphicsRendererSelection::SetPreferred(Absent()),
                 System::InvalidOperationException);
    EXPECT_EQ(GraphicsRendererSelection::GetSelected(), Built());
}

TEST_F(GraphicsRendererSelectionTest, RejectionMessageNamesWhatIsActuallyAvailable)
{
    // A "renderer not available" error is useless without saying what is. Pinned because it is the
    // first thing a user hits when they try this API on a single-renderer build.
    try
    {
        GraphicsRendererSelection::SetPreferred(Absent());
        FAIL() << "expected InvalidOperationException";
    }
    catch (const System::InvalidOperationException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find(CNA::getGraphicsRendererName(Absent())), std::string::npos);
        EXPECT_NE(message.find(CNA::getGraphicsRendererName(Built())), std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// The latch (design decision 5)
// ---------------------------------------------------------------------------

TEST_F(GraphicsRendererSelectionTest, ConstructingAGraphicsDeviceLatchesTheSelection)
{
    ASSERT_FALSE(GraphicsRendererSelection::IsLatched());
    {
        Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
        EXPECT_TRUE(GraphicsRendererSelection::IsLatched());
        EXPECT_EQ(GraphicsRendererSelection::GetActive(), Built());
    }
    // Destroying the device does not re-open the choice: other subsystems have already acted on it.
    EXPECT_TRUE(GraphicsRendererSelection::IsLatched());
}

TEST_F(GraphicsRendererSelectionTest, SetPreferredAfterTheLatchThrows)
{
    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
    ASSERT_TRUE(GraphicsRendererSelection::IsLatched());

    // Even re-selecting the SAME renderer throws: the contract is "the choice is closed", not
    // "the choice may not differ", which keeps the rule simple enough to rely on.
    EXPECT_THROW(GraphicsRendererSelection::SetPreferred(Built()),
                 System::InvalidOperationException);
    EXPECT_THROW(GraphicsRendererSelection::SetFallbackChain(std::vector<GraphicsRendererType>{}),
                 System::InvalidOperationException);
    EXPECT_THROW(GraphicsRendererSelection::EnableAutomaticFallback(true),
                 System::InvalidOperationException);
}

TEST_F(GraphicsRendererSelectionTest, GetActiveBeforeAnythingIsCreatedThrows)
{
    // There is no honest answer yet, and returning the *selected* renderer would quietly claim
    // something was created when nothing was.
    EXPECT_THROW((void)GraphicsRendererSelection::GetActive(),
                 System::InvalidOperationException);
}

TEST_F(GraphicsRendererSelectionTest, RendererReconstructionSurvivesTheLatch)
{
    // Design decision 5's other half: latching forbids changing the SELECTION, not creating a
    // renderer again. GraphicsDeviceManager's multisample path rebuilds the renderer on a live
    // device and must keep working.
    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
    ASSERT_TRUE(GraphicsRendererSelection::IsLatched());

    EXPECT_NO_THROW(device.RecreateRendererForMultiSampleCount(1));
    EXPECT_EQ(GraphicsRendererSelection::GetActive(), Built());
}

TEST_F(GraphicsRendererSelectionTest, SecondDeviceKeepsTheSameSelection)
{
    {
        Microsoft::Xna::Framework::Graphics::GraphicsDevice first;
        ASSERT_TRUE(GraphicsRendererSelection::IsLatched());
    }
    Microsoft::Xna::Framework::Graphics::GraphicsDevice second;
    EXPECT_EQ(GraphicsRendererSelection::GetActive(), Built());
}

// ---------------------------------------------------------------------------
// Fallback configuration (behaviour of the chain itself is exercised in P5/P8)
// ---------------------------------------------------------------------------

TEST_F(GraphicsRendererSelectionTest, FallbackIsDisabledByDefault)
{
    // Design decision 6. This is the single most important default in the whole feature.
    EXPECT_FALSE(GraphicsRendererSelection::IsFallbackEnabled());
    EXPECT_TRUE(GraphicsRendererSelection::GetFallbackHistory().empty());
}

TEST_F(GraphicsRendererSelectionTest, ConfiguringAChainEnablesFallback)
{
    const std::vector<GraphicsRendererType> chain{GraphicsRendererType::Software,
                                                   GraphicsRendererType::Stub};
    GraphicsRendererSelection::SetFallbackChain(chain);
    EXPECT_TRUE(GraphicsRendererSelection::IsFallbackEnabled());
}

TEST_F(GraphicsRendererSelectionTest, AChainMayNameRenderersThisBuildDoesNotHave)
{
    // One chain should serve several build configurations; entries that are not compiled in are
    // skipped and recorded at resolution time rather than rejected here.
    const std::vector<GraphicsRendererType> chain{Absent(), Built()};
    EXPECT_NO_THROW(GraphicsRendererSelection::SetFallbackChain(chain));
}

TEST_F(GraphicsRendererSelectionTest, WithFallbackEnabledAnAbsentPreferenceIsNoLongerAHardError)
{
    // The escape hatch design decision 6 describes: asking for a renderer this build lacks is an
    // error ONLY while there is no configured alternative.
    GraphicsRendererSelection::SetFallbackChain(std::vector<GraphicsRendererType>{Built()});
    EXPECT_NO_THROW(GraphicsRendererSelection::SetPreferred(Absent()));
    EXPECT_EQ(GraphicsRendererSelection::GetSelected(), Absent());
}

TEST_F(GraphicsRendererSelectionTest, AutomaticFallbackCoversEveryCompiledInRenderer)
{
    GraphicsRendererSelection::EnableAutomaticFallback(true);
    EXPECT_TRUE(GraphicsRendererSelection::IsFallbackEnabled());

    const auto order = CNA::GraphicsRendererSelectionAccessEXT::GetAttemptOrder();
    EXPECT_GE(order.size(), 1u);
    EXPECT_EQ(order.front(), GraphicsRendererSelection::GetSelected());

    for (const GraphicsRendererType available : GraphicsRendererSelection::GetAvailable())
    {
        EXPECT_NE(std::find(order.begin(), order.end(), available), order.end())
            << "automatic fallback skipped " << CNA::getGraphicsRendererName(available);
    }
}

TEST_F(GraphicsRendererSelectionTest, DisablingAutomaticFallbackRestoresTheSingleAttempt)
{
    GraphicsRendererSelection::EnableAutomaticFallback(true);
    GraphicsRendererSelection::EnableAutomaticFallback(false);

    EXPECT_FALSE(GraphicsRendererSelection::IsFallbackEnabled());
    EXPECT_EQ(CNA::GraphicsRendererSelectionAccessEXT::GetAttemptOrder().size(), 1u);
}

TEST_F(GraphicsRendererSelectionTest, AttemptOrderStartsWithThePreferredRenderer)
{
    GraphicsRendererSelection::SetFallbackChain(std::vector<GraphicsRendererType>{Built()});
    GraphicsRendererSelection::SetPreferred(Built());

    const auto order = CNA::GraphicsRendererSelectionAccessEXT::GetAttemptOrder();
    ASSERT_FALSE(order.empty());
    EXPECT_EQ(order.front(), Built());
    // The preferred renderer must not appear twice just because the chain also names it.
    EXPECT_EQ(std::count(order.begin(), order.end(), Built()), 1);
}
