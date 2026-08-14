// plan_runtimerenderer.md RTR-P8-5..P8-8: fallback between renderers that both really work.
//
// The P5 suite proves fallback with a candidate that is not compiled in. That covers the policy but
// not the interesting half: a renderer that IS present, IS linked, and is nonetheless passed over.
// Proving that needs two working renderers in one binary, so this whole file is guarded on
// CNA_MULTI_RENDERER and compiles to nothing in a single-renderer build.
//
// The rejection is driven by CNA_DEBUG_UNAVAILABLE_RENDERERS, the documented debug facility for
// verifying a fallback chain without breaking a driver to do it.

#include <gtest/gtest.h>

#ifdef CNA_MULTI_RENDERER

#include "CNA/GraphicsRendererFallbackRecord.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererRegistry.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "System/InvalidOperationException.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

using CNA::GraphicsRendererFallbackReason;
using CNA::GraphicsRendererSelection;
using CNA::GraphicsRendererType;

namespace
{
    class MultiRendererFallbackTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            GraphicsRendererSelection::ResetForTestingEXT();
            SDL_unsetenv_unsafe("CNA_DEBUG_UNAVAILABLE_RENDERERS");
            SDL_unsetenv_unsafe("CNA_DEBUG_FAIL_RENDERER_INIT");
        }

        void TearDown() override
        {
            SDL_unsetenv_unsafe("CNA_DEBUG_UNAVAILABLE_RENDERERS");
            SDL_unsetenv_unsafe("CNA_DEBUG_FAIL_RENDERER_INIT");
            GraphicsRendererSelection::ResetForTestingEXT();
        }

        /// Marks renderers as failing their availability probe.
        static void ForceUnavailable(const std::vector<GraphicsRendererType>& types)
        {
            std::string value;
            for (const GraphicsRendererType type : types)
            {
                if (!value.empty())
                    value += ',';
                value += CNA::getGraphicsRendererName(type);
            }
            SDL_setenv_unsafe("CNA_DEBUG_UNAVAILABLE_RENDERERS", value.c_str(), 1);
        }

        /// Marks renderers as failing during initialization -- after their window already exists.
        static void ForceInitFailure(const std::vector<GraphicsRendererType>& types)
        {
            std::string value;
            for (const GraphicsRendererType type : types)
            {
                if (!value.empty())
                    value += ',';
                value += CNA::getGraphicsRendererName(type);
            }
            SDL_setenv_unsafe("CNA_DEBUG_FAIL_RENDERER_INIT", value.c_str(), 1);
        }

        /// The first compiled-in renderer whose window kind differs from @p from's.
        [[nodiscard]] static const CNA::Internal::Renderers::GraphicsRendererDescriptor*
        FindDifferentWindowKind(GraphicsRendererType from)
        {
            namespace Renderers = CNA::Internal::Renderers;
            const auto* origin = Renderers::GraphicsRendererRegistry::Find(from);
            if (origin == nullptr)
                return nullptr;
            for (const auto& candidate : Renderers::GraphicsRendererRegistry::All())
            {
                if (candidate.type != from && candidate.windowKind != origin->windowKind)
                    return &candidate;
            }
            return nullptr;
        }

        [[nodiscard]] static std::vector<GraphicsRendererType> Available()
        {
            const auto span = GraphicsRendererSelection::GetAvailable();
            return std::vector<GraphicsRendererType>(span.begin(), span.end());
        }
    };
}

TEST_F(MultiRendererFallbackTest, ThisBuildGenuinelyContainsSeveralRenderers)
{
    // Guards the rest of the file: if this ever reports one renderer, every test below would be
    // vacuously true rather than failing.
    ASSERT_GT(Available().size(), 1u)
        << "CNA_MULTI_RENDERER is defined but only one renderer is compiled in";
}

TEST_F(MultiRendererFallbackTest, AnUnavailablePreferredRendererIsSubstituted)
{
    // RTR-P8-5: the first real fallback between two renderers that both work.
    const auto available = Available();
    const GraphicsRendererType preferred = available[0];
    const GraphicsRendererType next = available[1];

    GraphicsRendererSelection::SetFallbackChain(std::vector<GraphicsRendererType>{next});
    GraphicsRendererSelection::SetPreferred(preferred);
    ForceUnavailable({preferred});

    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;

    EXPECT_EQ(GraphicsRendererSelection::GetSelected(), preferred);
    EXPECT_EQ(GraphicsRendererSelection::GetActive(), next);
    EXPECT_EQ(device.GetGraphicsRendererType(), next)
        << "the device must report the renderer it actually got, not the one that was requested";

    const auto history = GraphicsRendererSelection::GetFallbackHistory();
    ASSERT_EQ(history.size(), 1u);
    EXPECT_EQ(history.front().type, preferred);
    EXPECT_EQ(history.front().reason, GraphicsRendererFallbackReason::ProbeUnavailable);
}

TEST_F(MultiRendererFallbackTest, FallbackWalksThePreferenceOrderRatherThanJumpingToTheEnd)
{
    // With three renderers and the first two unavailable, the third must be reached BY WALKING --
    // and both rejections must be recorded, in order. A chain that silently jumped to the last
    // working entry would pass a weaker test and hide which candidates were actually considered.
    const auto available = Available();
    if (available.size() < 3)
        GTEST_SKIP() << "needs at least three compiled-in renderers";

    GraphicsRendererSelection::SetFallbackChain(
        std::vector<GraphicsRendererType>{available[1], available[2]});
    GraphicsRendererSelection::SetPreferred(available[0]);
    ForceUnavailable({available[0], available[1]});

    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;

    EXPECT_EQ(GraphicsRendererSelection::GetActive(), available[2]);

    const auto history = GraphicsRendererSelection::GetFallbackHistory();
    ASSERT_EQ(history.size(), 2u);
    EXPECT_EQ(history[0].type, available[0]);
    EXPECT_EQ(history[1].type, available[1]);
}

TEST_F(MultiRendererFallbackTest, WithoutAChainAnUnavailableRendererIsAHardFailure)
{
    // Design decision 6, proven against a renderer that is present and working: being unavailable
    // is an error, and CNA does not quietly reach for one of the others it happens to have.
    const auto available = Available();
    GraphicsRendererSelection::SetPreferred(available[0]);
    ForceUnavailable({available[0]});

    EXPECT_THROW({ Microsoft::Xna::Framework::Graphics::GraphicsDevice device; },
                 System::InvalidOperationException);
    EXPECT_FALSE(GraphicsRendererSelection::IsLatched());
}

TEST_F(MultiRendererFallbackTest, EveryRendererUnavailableExhaustsTheChainAndThrows)
{
    // RTR-P8-7.
    const auto available = Available();
    GraphicsRendererSelection::EnableAutomaticFallback(true);
    ForceUnavailable(available);

    try
    {
        Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
        FAIL() << "expected construction to throw once every candidate was rejected";
    }
    catch (const System::InvalidOperationException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find("no graphics renderer could be created"), std::string::npos);
        // Every rejected candidate has to appear, not merely the last one.
        for (const GraphicsRendererType type : available)
        {
            EXPECT_NE(message.find(CNA::getGraphicsRendererName(type)), std::string::npos)
                << CNA::getGraphicsRendererName(type) << " missing from the failure message";
        }
    }

    EXPECT_EQ(GraphicsRendererSelection::GetFallbackHistory().size(), available.size());
}

TEST_F(MultiRendererFallbackTest, AutomaticFallbackReachesAWorkingRendererWithoutAnExplicitChain)
{
    const auto available = Available();
    GraphicsRendererSelection::EnableAutomaticFallback(true);
    GraphicsRendererSelection::SetPreferred(available[0]);
    ForceUnavailable({available[0]});

    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;

    EXPECT_NE(GraphicsRendererSelection::GetActive(), available[0]);
    EXPECT_TRUE(GraphicsRendererSelection::IsAvailable(GraphicsRendererSelection::GetActive()));
}

TEST_F(MultiRendererFallbackTest, EachCompiledInRendererCanBeSelectedAndReallyBacksTheDevice)
{
    // RTR-P8-2: the point of the whole feature -- one binary, every compiled-in renderer reachable,
    // and the device genuinely backed by the one that was asked for.
    for (const GraphicsRendererType type : Available())
    {
        GraphicsRendererSelection::ResetForTestingEXT();
        GraphicsRendererSelection::SetPreferred(type);

        Microsoft::Xna::Framework::Graphics::GraphicsDevice device;

        EXPECT_EQ(GraphicsRendererSelection::GetActive(), type);
        EXPECT_EQ(device.GetGraphicsRendererType(), type);
        EXPECT_EQ(device.GetGraphicsRendererName(), CNA::getGraphicsRendererName(type));
        EXPECT_TRUE(GraphicsRendererSelection::GetFallbackHistory().empty());
    }
}

TEST_F(MultiRendererFallbackTest, SelectionByNameReachesTheSameRenderers)
{
    for (const GraphicsRendererType type : Available())
    {
        GraphicsRendererSelection::ResetForTestingEXT();
        GraphicsRendererSelection::SetPreferred(CNA::getGraphicsRendererName(type));

        Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
        EXPECT_EQ(device.GetGraphicsRendererType(), type);
    }
}

TEST_F(MultiRendererFallbackTest, ReconstructionKeepsTheSubstitutedRendererNotThePreferredOne)
{
    // RTR-P5-17 against real renderers: a reconstruction must not re-resolve, or it would start
    // again from a preference that has already been rejected once.
    const auto available = Available();
    GraphicsRendererSelection::SetFallbackChain(std::vector<GraphicsRendererType>{available[1]});
    GraphicsRendererSelection::SetPreferred(available[0]);
    ForceUnavailable({available[0]});

    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
    ASSERT_EQ(device.GetGraphicsRendererType(), available[1]);

    EXPECT_NO_THROW(device.RecreateRendererForMultiSampleCount(1));
    EXPECT_EQ(device.GetGraphicsRendererType(), available[1]);
    EXPECT_EQ(GraphicsRendererSelection::GetFallbackHistory().size(), 1u);
}

TEST_F(MultiRendererFallbackTest, InitializationFailureAfterTheWindowExistsFallsThrough)
{
    // RTR-P8-6. Materially different from a failed probe: by the time construction throws, the
    // window has already been created for the candidate that failed.
    const auto available = Available();
    GraphicsRendererSelection::SetFallbackChain(std::vector<GraphicsRendererType>{available[1]});
    GraphicsRendererSelection::SetPreferred(available[0]);
    ForceInitFailure({available[0]});

    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;

    EXPECT_EQ(device.GetGraphicsRendererType(), available[1]);

    const auto history = GraphicsRendererSelection::GetFallbackHistory();
    ASSERT_EQ(history.size(), 1u);
    EXPECT_EQ(history.front().type, available[0]);
    EXPECT_EQ(history.front().reason, GraphicsRendererFallbackReason::InitializationFailed);
    // The renderer's own message has to survive into the record, or a real driver failure would be
    // reduced to "it did not work".
    EXPECT_NE(history.front().message.find("CNA_DEBUG_FAIL_RENDERER_INIT"), std::string::npos);
}

TEST_F(MultiRendererFallbackTest, FallingBackAcrossWindowKindsRecreatesTheWindow)
{
    // RTR-P5-12 / design decision 8, the case that needs BOTH a real multi build and a failure that
    // happens after a window exists: SDL refuses a window carrying two graphics-API flags, so
    // crossing from one window kind to another means destroying and recreating it.
    const auto available = Available();
    const auto* crossing = FindDifferentWindowKind(available[0]);
    if (crossing == nullptr)
        GTEST_SKIP() << "this build has no two renderers with different window kinds";

    GraphicsRendererSelection::SetFallbackChain(
        std::vector<GraphicsRendererType>{crossing->type});
    GraphicsRendererSelection::SetPreferred(available[0]);
    ForceInitFailure({available[0]});

    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;

    EXPECT_EQ(device.GetGraphicsRendererType(), crossing->type);
    // The device has to be genuinely usable afterwards -- a recreated window that nothing draws
    // into would satisfy a weaker assertion.
    EXPECT_NO_THROW(device.Clear(Microsoft::Xna::Framework::Color::CornflowerBlue));
}

#endif  // CNA_MULTI_RENDERER
