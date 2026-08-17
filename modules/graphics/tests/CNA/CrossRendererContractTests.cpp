// plan_runtimerenderer.md RTR-P9-23: cross-renderer contracts checked from ONE binary.
//
// Until a build could hold several renderers, "do these two renderers agree about X?" could only be
// answered by building twice and comparing artifacts out of band -- which is why this project's
// cross-renderer comparisons live in shell scripts and oracle corpora rather than in the test suite.
// A multi-renderer build can ask it directly, in-process, against live devices.
//
// These tests deliberately assert PROPERTIES EVERY RENDERER MUST HOLD rather than comparing two
// renderers' answers to each other. Two renderers legitimately differ (SOFTWARE rasterizes, STUB
// renders nothing); what they may not do is disagree about the framework contract itself.

#include <gtest/gtest.h>

#ifdef CNA_MULTI_RENDERER

#include "CNA/GraphicsCapability.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using CNA::GraphicsRendererSelection;
using CNA::GraphicsRendererType;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

namespace
{
    class CrossRendererContractTest : public ::testing::Test
    {
    protected:
        void SetUp() override { GraphicsRendererSelection::ResetForTestingEXT(); }
        void TearDown() override { GraphicsRendererSelection::ResetForTestingEXT(); }

        [[nodiscard]] static std::vector<GraphicsRendererType> Available()
        {
            const auto span = GraphicsRendererSelection::GetAvailable();
            return std::vector<GraphicsRendererType>(span.begin(), span.end());
        }

        /// Runs @p body against a live device on every compiled-in renderer that can actually
        /// create one in this environment.
        ///
        /// Being compiled in and being USABLE HERE are different things, and conflating them was a
        /// real defect in the first version of this fixture: OPENGLES1 needs an ES 1.1-capable Mesa
        /// (Debian builds Mesa with -Dgles1=disabled, see scripts/opengles1-test-env.sh), and LLGL
        /// and DILIGENT need SDL's x11 video driver, so on a stock Wayland session each of them
        /// throws from its constructor. Treating that as a contract violation made this suite fail
        /// for reasons that have nothing to do with the contract it exists to check.
        ///
        /// A renderer that cannot be constructed here is skipped and NAMED, so a run always says
        /// which renderers it actually covered rather than quietly covering fewer.
        template <typename Body>
        static void ForEachRenderer(Body&& body)
        {
            std::vector<std::string> covered;
            std::vector<std::string> unavailable;

            for (const GraphicsRendererType type : Available())
            {
                GraphicsRendererSelection::ResetForTestingEXT();
                GraphicsRendererSelection::SetPreferred(type);

                std::unique_ptr<GraphicsDevice> device;
                try
                {
                    device = std::make_unique<GraphicsDevice>();
                }
                catch (const std::exception& e)
                {
                    unavailable.emplace_back(std::string(CNA::getGraphicsRendererName(type)) +
                                             " (" + e.what() + ")");
                    continue;
                }

                covered.emplace_back(CNA::getGraphicsRendererName(type));
                SCOPED_TRACE(std::string("renderer: ") +
                             std::string(CNA::getGraphicsRendererName(type)));
                body(*device, type);
            }

            // A pass that covered nothing is not a pass.
            ASSERT_FALSE(covered.empty())
                << "no compiled-in renderer could create a device in this environment";

            if (!unavailable.empty())
            {
                std::string message;
                for (const std::string& entry : unavailable)
                    message += "\n    - " + entry;
                GTEST_LOG_(INFO) << "covered " << covered.size() << " renderer(s); skipped "
                                 << unavailable.size() << " that cannot run here:" << message;
            }
        }
    };
}

TEST_F(CrossRendererContractTest, EveryRendererReportsTheIdentityItWasSelectedAs)
{
    // The contract the whole feature rests on: whatever a renderer does, it must not lie about
    // which renderer it is. A mismatch here would make every other cross-renderer result unreadable.
    ForEachRenderer([](GraphicsDevice& device, GraphicsRendererType type) {
        EXPECT_EQ(device.GetGraphicsRendererType(), type);
        EXPECT_EQ(device.GetGraphicsRendererName(), CNA::getGraphicsRendererName(type));
        EXPECT_EQ(GraphicsRendererSelection::GetActive(), type);
    });
}

TEST_F(CrossRendererContractTest, CapabilityAnswersAreStableWithinARendererAndSurviveAReset)
{
    // A capability query must be a property of the renderer, not of when it happens to be asked.
    // Renderers legitimately give DIFFERENT answers from each other; none may give two.
    constexpr CNA::GraphicsCapability probed[] = {
        CNA::GraphicsCapability::CustomEffects,
        CNA::GraphicsCapability::OcclusionQuery,
        CNA::GraphicsCapability::WireFrame,
    };

    ForEachRenderer([&](GraphicsDevice& device, GraphicsRendererType) {
        for (const CNA::GraphicsCapability capability : probed)
        {
            const bool first = device.SupportsCapability(capability);
            EXPECT_EQ(device.SupportsCapability(capability), first)
                << "capability answer changed between two identical queries";

            device.RecreateRendererForMultiSampleCount(1);
            EXPECT_EQ(device.SupportsCapability(capability), first)
                << "capability answer changed after the renderer was rebuilt";
        }
    });
}

TEST_F(CrossRendererContractTest, ClearIsAcceptedByEveryRenderer)
{
    // The narrowest thing every CNA renderer must do, including the ones that render nothing.
    ForEachRenderer([](GraphicsDevice& device, GraphicsRendererType) {
        EXPECT_NO_THROW(device.Clear(Microsoft::Xna::Framework::Color::CornflowerBlue));
        EXPECT_NO_THROW(device.Present());
    });
}

TEST_F(CrossRendererContractTest, ViewportIsNeverDegenerateOnAnyRenderer)
{
    // A renderer with no window still owes a usable logical viewport -- HEADLESS/SOFTWARE/STUB
    // included, since SpriteBatch's coordinate space is derived from it.
    ForEachRenderer([](GraphicsDevice& device, GraphicsRendererType) {
        const auto viewport = device.getViewportProperty();
        EXPECT_GT(viewport.getWidthProperty(), 0);
        EXPECT_GT(viewport.getHeightProperty(), 0);
    });
}

TEST_F(CrossRendererContractTest, ATextureCanBeCreatedAndReadBackOnEveryRenderer)
{
    // Texture2D round-trip is the smallest resource contract shared by every renderer. Running it
    // across renderers in ONE process is what this build mode makes possible; previously it needed
    // one build per renderer and an out-of-band comparison.
    ForEachRenderer([](GraphicsDevice& device, GraphicsRendererType) {
        Microsoft::Xna::Framework::Graphics::Texture2D texture(device, 4, 4);
        EXPECT_EQ(texture.getWidthProperty(), 4);
        EXPECT_EQ(texture.getHeightProperty(), 4);

        const std::vector<Microsoft::Xna::Framework::Color> pixels(
            16, Microsoft::Xna::Framework::Color::Red);
        EXPECT_NO_THROW(texture.SetData(pixels.data(), static_cast<int>(pixels.size())));
    });
}

TEST_F(CrossRendererContractTest, SelectingEveryRendererInTurnLeavesNoFallbackResidue)
{
    // Each selection must be a clean start: a renderer chosen after another must not inherit the
    // previous one's fallback history, which would make GetFallbackHistory() meaningless as a
    // diagnostic.
    ForEachRenderer([](GraphicsDevice&, GraphicsRendererType) {
        EXPECT_TRUE(GraphicsRendererSelection::GetFallbackHistory().empty());
    });
}

#endif  // CNA_MULTI_RENDERER
