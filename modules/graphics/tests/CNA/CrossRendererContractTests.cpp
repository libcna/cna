// plans/plan_runtimerenderer.md RTR-P9-23: cross-renderer contracts checked from ONE binary.
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
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include <cstdio>
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"

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

// plans/plan_gltf.md GLTF-475. A renderer picks its shader program from the vertex stride; the effect the
// caller applied says what that program should COMPUTE. Where those two are conflated, the renderer
// draws a wrong picture and reports success -- and no static inventory in this repository can see
// it, because every declaration involved is correct.
//
// The input below is the cheapest instance of that conflation: a plain XNA
// VertexPositionNormalTangentTexture buffer (stride 48, the same layout glTF emits for a tangented
// primitive) drawn with a BasicEffect whose DiffuseColor is RED, lighting off, no texture. Every
// renderer that draws it must produce red. Two did not when this test was written:
//
//   * OPENGL4 produced rgba(0,0,255) -- the colour fallback dropped GpuDrawParams and its program
//     painted attribute location 1, which on this record is the NORMAL (0,0,1), as the surface;
//   * DILIGENT produced rgba(0,0,0) -- it selected a metallic-roughness program from the stride
//     alone, for a draw that never came from PbrEffect.
//
// Refusing is the other acceptable answer and several renderers give it: a renderer without a
// program for this layout says so by name instead of substituting a different one. What no renderer
// may do is the third state -- accept the draw and answer with a colour that is neither the
// effect's nor a refusal.
TEST_F(CrossRendererContractTest, NoRendererPaintsAVertexAttributeInsteadOfTheEffectsDiffuseColour)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    int judged = 0;
    ForEachRenderer([&judged](GraphicsDevice& device, GraphicsRendererType type) {
        if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) { return; }
        const std::string name{CNA::getGraphicsRendererName(type)};
        SCOPED_TRACE("stride-48 BasicEffect on " + name);

        RenderTarget2D target(device, 8, 8);
        VertexBuffer vb(device, VertexPositionNormalTangentTexture::getVertexDeclarationStatic(),
                        3, BufferUsage::None);
        // A NORMAL of (0,0,1) and a TANGENT of (1,0,0,1): both are unit vectors along an axis, so a
        // renderer that paints either one instead of the diffuse colour answers a saturated blue or
        // red-with-full-alpha rather than something ambiguous. The triangle covers the whole target.
        const VertexPositionNormalTangentTexture verts[3] = {
            {Vector3(-1, -1, 0), Vector3(0, 0, 1), Vector4(1, 0, 0, 1), Vector2(0, 0)},
            {Vector3(3, -1, 0), Vector3(0, 0, 1), Vector4(1, 0, 0, 1), Vector2(1, 0)},
            {Vector3(-1, 3, 0), Vector3(0, 0, 1), Vector4(1, 0, 0, 1), Vector2(0, 1)},
        };
        vb.SetData(verts, 3);

        BasicEffect fx(device);
        fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        fx.setLightingEnabledProperty(false);

        bool drew = false;
        std::string refusal;
        try
        {
            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            // Without this the default rasterizer state culls the triangle and every renderer
            // agrees on the cleared colour, which would make this test pass while measuring nothing.
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.SetVertexBuffer(&vb);
            for (auto& pass : fx.getCurrentTechniqueProperty()->getPassesProperty()) { pass.Apply(); }
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            device.SetRenderTarget(nullptr);
            drew = true;
        }
        catch (const std::exception& e)
        {
            refusal = e.what();
            device.SetRenderTarget(nullptr);
        }

        if (!drew)
        {
            // The other permitted state. A refusal has to SAY something -- an empty or generic
            // message is how a limitation becomes indistinguishable from a crash.
            EXPECT_GT(refusal.size(), 20u)
                << name << " refused the draw without a usable diagnostic: \"" << refusal << "\"";
            return;
        }

        std::vector<Color> pixels(64, Color::Black);
        try
        {
            target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        }
        catch (const std::exception&)
        {
            // A renderer with no colour readback (HEADLESS) cannot be judged on pixels here. Its
            // draw-boundary behaviour is covered by RendererStrideConformance instead.
            return;
        }

        ++judged;
        const Color centre = pixels[8 * 4 + 4];
        EXPECT_EQ(centre, Color(255, 0, 0, 255))
            << name << " drew rgba(" << int(centre.getRProperty()) << ","
            << int(centre.getGProperty()) << "," << int(centre.getBProperty()) << ","
            << int(centre.getAProperty()) << ") for a BasicEffect whose DiffuseColor is opaque red. "
            << "A renderer may refuse this layout, but not answer it with a different colour.";
    });

    EXPECT_GT(judged, 0)
        << "no renderer in this build both drew the stride-48 record and could read the result "
           "back, so this test measured nothing";
}

#endif  // CNA_MULTI_RENDERER
