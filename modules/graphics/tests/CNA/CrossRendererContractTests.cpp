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
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "CNA/Unsupported3DGraphicsCallBehavior.hpp"
#include "System/NotSupportedException.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorMatrixEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "CNA/RendererCapabilityProfile.hpp"

#include <memory>
#include <type_traits>
#include <array>
#include <optional>
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
// renderer that draws it must produce red. Two renderers, both since retired, did not when this
// test was written:
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

// CNA-DEMOS-ISSUE-104: on a renderer without volume storage, Texture3D is refused under the
// default Throw policy, and under WarnAndStub it is the promised usable null object -- uploads are
// accepted and reads return zeros -- rather than a wrapper that refuses its first SetData.
TEST_F(CrossRendererContractTest, WarnAndStubMakesTexture3DAUsableNullObjectWithoutVolumeSupport)
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture3D;
    int judged = 0;
    ForEachRenderer([&](GraphicsDevice& device, GraphicsRendererType)
    {
        // Reach refuses volumes on every renderer; under HiDef only the renderer can.
        device.SetGraphicsProfileEXT(Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
        if (device.SupportsCapability(CNA::GraphicsCapability::Texture3D)) { return; }
        ++judged;
        EXPECT_THROW(Texture3D(device, 1, 1, 1, false, SurfaceFormat::Color),
                     System::NotSupportedException);

        device.SetUnsupported3DGraphicsCallBehavior(
            CNA::Unsupported3DGraphicsCallBehavior::WarnAndStub);
        try
        {
            Texture3D first(device, 1, 1, 1, false, SurfaceFormat::Color);
            Texture3D second(device, 1, 1, 1, false, SurfaceFormat::Color);
            const Color red = Color::Red;
            first.SetData(&red, 1);
            Color back = Color::White;
            first.GetData(&back, 1);
            EXPECT_EQ(Color(0, 0, 0, 0), back);
        }
        catch (const std::exception& e)
        {
            ADD_FAILURE() << "WarnAndStub Texture3D was not a usable null object: " << e.what();
        }
        device.SetUnsupported3DGraphicsCallBehavior(
            CNA::Unsupported3DGraphicsCallBehavior::Throw);
    });
    if (judged == 0) { GTEST_SKIP() << "every renderer in this build stores volume textures"; }
}

// CNA-DEMOS-ISSUE-007: a ColorMatrixEffect is applied by the renderers that implement the fixed
// CPU transform and refused by SpriteBatch::Begin everywhere else, instead of being ignored.
TEST_F(CrossRendererContractTest, SpriteBatchAppliesOrRefusesAColorMatrixEffect)
{
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::ColorMatrixEffect;
    using Microsoft::Xna::Framework::Graphics::SpriteBatch;
    using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
    int applied = 0;
    int refused = 0;
    ForEachRenderer([&](GraphicsDevice& device, GraphicsRendererType)
    {
        ColorMatrixEffect effect(device);
        SpriteBatch batch(device);
        if (device.GetRenderer().SupportsColorMatrixSpriteEffectEXT())
        {
            EXPECT_NO_THROW(batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, nullptr,
                                        nullptr, nullptr, &effect));
            EXPECT_NO_THROW(batch.End());
            ++applied;
            return;
        }
        EXPECT_THROW(batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, nullptr, nullptr,
                                 nullptr, &effect),
                     System::NotSupportedException);
        // The refused Begin published nothing: an ordinary batch still begins.
        EXPECT_NO_THROW(batch.Begin());
        EXPECT_NO_THROW(batch.End());
        ++refused;
    });
    EXPECT_GT(applied + refused, 0);
}

// CNA-DEMOS-ISSUE-066: GraphicsDevice::SetBlendEnabled installs one equation on every renderer --
// BlendState::NonPremultiplied's when enabled, Opaque's overwrite when disabled -- and the next
// BlendState assignment, even of the current object, replaces it again.
TEST_F(CrossRendererContractTest, SetBlendEnabledInstallsNonPremultipliedBlendingEverywhere)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    int judged = 0;
    ForEachRenderer([&judged](GraphicsDevice& device, GraphicsRendererType type) {
        if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) { return; }
        const std::string name{CNA::getGraphicsRendererName(type)};
        RenderTarget2D target(device, 8, 8);
        BasicEffect fx(device);
        fx.setVertexColorEnabledProperty(true);
        const Color halfRed(255, 0, 0, 128);
        const VertexPositionColor verts[3] = {{Vector3(-1, -1, 0), halfRed},
                                              {Vector3(3, -1, 0), halfRed},
                                              {Vector3(-1, 3, 0), halfRed}};
        // Draws the covering triangle `draws` times over black after `configure`, and returns the
        // centre texel, or nothing where the renderer cannot read a target back.
        const auto run = [&](auto&& configure, int draws) -> std::optional<Color> {
            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            device.setBlendStateProperty(BlendState::Opaque);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            configure();
            for (int i = 0; i < draws; ++i)
            {
                for (auto& pass : fx.getCurrentTechniqueProperty()->getPassesProperty()) { pass.Apply(); }
                device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 1);
            }
            device.SetRenderTarget(nullptr);
            std::vector<Color> pixels(64);
            try { target.GetData(pixels.data(), static_cast<int>(pixels.size())); }
            catch (const std::exception&) { return std::nullopt; }
            return pixels[8 * 4 + 4];
        };

        const std::optional<Color> blended = run([&] { device.SetBlendEnabled(true); }, 2);
        if (!blended) { return; }
        ++judged;
        SCOPED_TRACE(name);
        // Two passes of SourceAlpha/InverseSourceAlpha over opaque black: 1 - 0.5^2 of the source
        // red, and alpha 0.5 * 0.5 + 0.5 * (0.5 * 0.5 + 0.5 * 1) = 0.625.
        EXPECT_NEAR(blended->getRProperty(), 191, 3);
        EXPECT_NEAR(blended->getAProperty(), 159, 3);
        EXPECT_EQ(device.getBlendStateProperty().getColorDestinationBlendProperty(), Blend::Zero)
            << "the BlendState property is the caller's object and does not change";

        const std::optional<Color> reassigned = run([&] {
            device.SetBlendEnabled(true);
            device.setBlendStateProperty(BlendState::Opaque);
        }, 2);
        ASSERT_TRUE(reassigned.has_value());
        EXPECT_EQ(*reassigned, halfRed) << "assigning the current Opaque state did not replace it";

        const std::optional<Color> disabled = run([&] {
            device.SetBlendEnabled(true);
            device.SetBlendEnabled(false);
        }, 2);
        ASSERT_TRUE(disabled.has_value());
        EXPECT_EQ(*disabled, halfRed) << "SetBlendEnabled(false) did not restore overwrite";
    });
    EXPECT_GT(judged, 0) << "no renderer in this build could draw and read the result back";
}

// CNA-DEMOS-ISSUE-006: RendererFeature::SamplerLevelOfDetailBias tells the truth on every
// renderer. A 16x16 texture whose mip levels are red, green, blue, white and yellow is drawn 1:1
// (level 0) with a +1.25 bias: a renderer that applies the bias shows level 1's green, one that
// does not shows level 0's red, and the reported support must say which.
TEST_F(CrossRendererContractTest, SamplerLevelOfDetailBiasSupportMatchesWhatSpriteBatchSamples)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    int judged = 0;
    ForEachRenderer([&judged](GraphicsDevice& device, GraphicsRendererType type) {
        const std::string name{CNA::getGraphicsRendererName(type)};
        SCOPED_TRACE(name);
        Texture2D texture(device, 16, 16, true, SurfaceFormat::Color);
        const Color levels[] = {Color(255, 0, 0, 255), Color(0, 255, 0, 255),
                                Color(0, 0, 255, 255), Color(255, 255, 255, 255),
                                Color(255, 255, 0, 255)};
        for (int level = 0; level < texture.getLevelCountProperty() && level < 5; ++level)
        {
            const int size = 16 >> level;
            std::vector<Color> texels(static_cast<std::size_t>(size * size), levels[level]);
            texture.SetData(level, nullptr, texels.data(), 0, size * size);
        }
        SamplerState biased;
        biased.setFilterProperty(TextureFilter::Point);
        biased.setAddressUProperty(TextureAddressMode::Clamp);
        biased.setAddressVProperty(TextureAddressMode::Clamp);
        biased.setMipMapLevelOfDetailBiasProperty(1.25f);

        RenderTarget2D target(device, 16, 16);
        SpriteBatch batch(device);
        try
        {
            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque, &biased, nullptr, nullptr,
                        nullptr);
            batch.Draw(texture, Rectangle(0, 0, 16, 16), Color::White);
            batch.End();
            device.SetRenderTarget(nullptr);
        }
        catch (const std::exception&)
        {
            // No render-target route (STUB): nothing to sample, so nothing to judge.
            EXPECT_FALSE(device.SupportsRendererFeatureEXT(
                CNA::RendererFeature::SamplerLevelOfDetailBias)) << name;
            return;
        }
        std::vector<Color> pixels(256);
        try { target.GetData(pixels.data(), static_cast<int>(pixels.size())); }
        catch (const std::exception&) { return; }  // no colour readback (HEADLESS)
        const Color centre = pixels[8 * 16 + 8];
        if (centre == Color::Black) { return; }  // draws nothing at all (STUB)
        ++judged;
        const bool applied = centre == levels[1];
        EXPECT_TRUE(applied || centre == levels[0])
            << name << " sampled rgba(" << int(centre.getRProperty()) << ","
            << int(centre.getGProperty()) << "," << int(centre.getBProperty())
            << "), neither level 0 nor the biased level 1";
        EXPECT_EQ(device.SupportsRendererFeatureEXT(CNA::RendererFeature::SamplerLevelOfDetailBias),
                  applied)
            << name << (applied ? " applies the bias but does not report it"
                                : " reports the bias but sampled level 0");
    });
    EXPECT_GT(judged, 0) << "no renderer in this build drew and read back the mip probe";
}

namespace
{
    /// The PBR layouts the renderers draw PbrEffect and SkinnedPbrEffect from: position, normal,
    /// tangent and UV (stride 48), plus four weights and four indices (stride 68).
    struct PbrProbeVertex
    {
        float position[3];
        float normal[3];
        float tangent[4];
        float uv[2];
    };
    struct SkinnedPbrProbeVertex
    {
        PbrProbeVertex base;
        float weights[4];
        std::uint8_t indices[4];
    };
    static_assert(sizeof(PbrProbeVertex) == 48 && sizeof(SkinnedPbrProbeVertex) == 68);

    template <typename Vertex>
    std::array<Vertex, 6> PbrProbeQuad()
    {
        const float corners[6][4] = {{-1, -1, 0, 1}, {1, -1, 1, 1}, {1, 1, 1, 0},
                                     {-1, -1, 0, 1}, {1, 1, 1, 0}, {-1, 1, 0, 0}};
        std::array<Vertex, 6> quad{};
        for (std::size_t i = 0; i < quad.size(); ++i)
        {
            PbrProbeVertex v{{corners[i][0], corners[i][1], 0.0f}, {0.0f, 0.0f, 1.0f},
                             {1.0f, 0.0f, 0.0f, 1.0f}, {corners[i][2], corners[i][3]}};
            if constexpr (std::is_same_v<Vertex, PbrProbeVertex>) { quad[i] = v; }
            else { quad[i] = {v, {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}}; }
        }
        return quad;
    }

    Microsoft::Xna::Framework::Graphics::VertexDeclaration PbrProbeDeclaration(bool skinned)
    {
        using namespace Microsoft::Xna::Framework::Graphics;
        std::vector<VertexElement> elements{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
            VertexElement(40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0)};
        if (skinned)
        {
            elements.emplace_back(48, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0);
            elements.emplace_back(64, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0);
        }
        return VertexDeclaration(elements);
    }

    /// Draws a full-target quad textured with a left-red, right-blue image through @p Effect and
    /// reports whether the two halves came out red and blue -- whether the effect sampled its
    /// texture rather than drawing a uniform fallback. Empty without colour readback.
    template <typename Effect, typename Vertex, typename Configure>
    std::optional<bool> DrawsItsTexture(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                                        bool skinned, Configure&& configure)
    {
        using namespace Microsoft::Xna::Framework;
        using namespace Microsoft::Xna::Framework::Graphics;
        Texture2D texture(device, 2, 1, false, SurfaceFormat::Color);
        const Color halves[] = {Color(255, 0, 0, 255), Color(0, 0, 255, 255)};
        texture.SetData(halves, 2);
        Effect effect(device);
        effect.EnableDefaultLighting();
        effect.setTextureProperty(&texture);
        effect.setMetallicFactorProperty(0.0f);
        effect.setRoughnessFactorProperty(1.0f);
        configure(effect);

        const std::array<Vertex, 6> quad = PbrProbeQuad<Vertex>();
        const VertexDeclaration declaration = PbrProbeDeclaration(skinned);
        RenderTarget2D target(device, 16, 8);
        VertexBuffer vertices(device, declaration, 6, BufferUsage::None);
        vertices.SetData(quad.data(), 6);
        // The left (red) half's centre texel, drawn with the effect's current lights.
        const auto draw = [&]() -> std::optional<std::pair<Color, Color>> {
            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setBlendStateProperty(BlendState::Opaque);
            device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
            device.SetVertexBuffer(&vertices);
            for (auto& pass : effect.getCurrentTechniqueProperty()->getPassesProperty()) { pass.Apply(); }
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            device.SetRenderTarget(nullptr);
            std::vector<Color> pixels(128);
            try { target.GetData(pixels.data(), static_cast<int>(pixels.size())); }
            catch (const std::exception&) { return std::nullopt; }
            return std::pair{pixels[4 * 16 + 3], pixels[4 * 16 + 12]};
        };
        const auto lit = draw();
        if (!lit.has_value()) { return std::nullopt; }
        const auto [left, right] = *lit;
        const bool textured = left.getRProperty() > left.getBProperty() + 40 &&
                              right.getBProperty() > right.getRProperty() + 40;
        // With every light off and no ambient, a lit material goes (nearly) black; an unlit
        // fallback does not change.
        effect.getDirectionalLight0Property().setEnabledProperty(false);
        effect.getDirectionalLight1Property().setEnabledProperty(false);
        effect.getDirectionalLight2Property().setEnabledProperty(false);
        effect.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        const auto dark = draw();
        const bool lighted = dark.has_value() &&
                             dark->first.getRProperty() + 20 < left.getRProperty();
        return textured && lighted;
    }
}

// CNA-DEMOS-ISSUE-006: PbrEffectShading and SkinnedPbrEffectShading tell the truth on every
// renderer: a renderer that reports them samples the material's texture, and one that does not
// draws the uniform fallback (or refuses the draw).
TEST_F(CrossRendererContractTest, PbrShadingSupportMatchesWhetherTheEffectSamplesItsTexture)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    int judged = 0;
    ForEachRenderer([&judged](GraphicsDevice& device, GraphicsRendererType type) {
        if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) { return; }
        const std::string name{CNA::getGraphicsRendererName(type)};
        SCOPED_TRACE(name);
        const auto judge = [&](CNA::RendererFeature feature, const char* effectName, auto&& draw) {
            std::optional<bool> shaded;
            try { shaded = draw(); }
            catch (const std::exception& e)
            {
                EXPECT_FALSE(device.SupportsRendererFeatureEXT(feature))
                    << name << " reports " << effectName << " shading but refused it: " << e.what();
                return;
            }
            if (!shaded.has_value()) { return; }  // no colour readback (HEADLESS)
            ++judged;
            EXPECT_EQ(device.SupportsRendererFeatureEXT(feature), *shaded)
                << name << (*shaded ? " shades " : " reports shading but drew the fallback for ")
                << effectName << (*shaded ? " but does not report it" : "");
        };
        judge(CNA::RendererFeature::PbrEffectShading, "PbrEffect", [&] {
            return DrawsItsTexture<PbrEffect, PbrProbeVertex>(device, false, [](PbrEffect&) {});
        });
        judge(CNA::RendererFeature::SkinnedPbrEffectShading, "SkinnedPbrEffect", [&] {
            return DrawsItsTexture<SkinnedPbrEffect, SkinnedPbrProbeVertex>(
                device, true, [](SkinnedPbrEffect& effect) {
                    effect.SetBoneTransforms({Matrix::getIdentityProperty()});
                });
        });
    });
    EXPECT_GT(judged, 0) << "no renderer in this build drew and read back the PBR probe";
}

// CNA-DEMOS DC-R2: RendererFeature::MultiSampleMask tells the truth on every renderer. A red quad
// covering a 4x multisampled target is drawn under a mask keeping every sample, none, and one: a
// Supported renderer resolves the last to a quarter of the red, a Restricted one refuses it while
// honouring the other two, and an Unsupported one ignores every mask.
TEST_F(CrossRendererContractTest, MultiSampleMaskSupportMatchesWhatTheResolvedTargetShows)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    int judged = 0;
    ForEachRenderer([&judged](GraphicsDevice& device, GraphicsRendererType type) {
        if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD) ||
            !device.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing))
        {
            return;
        }
        const std::string name{CNA::getGraphicsRendererName(type)};
        SCOPED_TRACE(name);
        device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
        RenderTarget2D target(device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 4,
                              RenderTargetUsage::DiscardContents);
        if (target.getMultiSampleCountProperty() < 4) { return; }
        BasicEffect effect(device);
        effect.setVertexColorEnabledProperty(true);
        const Color red(255, 0, 0, 255);
        const VertexPositionColor quad[6] = {
            {Vector3(-1, -1, 0), red}, {Vector3(1, -1, 0), red}, {Vector3(1, 1, 0), red},
            {Vector3(-1, -1, 0), red}, {Vector3(1, 1, 0), red}, {Vector3(-1, 1, 0), red}};
        const auto draw = [&](int mask) -> std::optional<Color> {
            BlendState masked;
            masked.setMultiSampleMaskProperty(mask);
            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            device.setBlendStateProperty(masked);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            try
            {
                for (auto& pass : effect.getCurrentTechniqueProperty()->getPassesProperty()) { pass.Apply(); }
                device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            }
            catch (...)
            {
                device.SetRenderTarget(nullptr);
                device.setBlendStateProperty(BlendState::Opaque);
                throw;
            }
            device.SetRenderTarget(nullptr);
            device.setBlendStateProperty(BlendState::Opaque);
            std::vector<Color> pixels(64);
            try { target.GetData(pixels.data(), static_cast<int>(pixels.size())); }
            catch (const std::exception&) { return std::nullopt; }
            return pixels[4 * 8 + 4];
        };

        const std::optional<Color> all = draw(-1);
        if (!all.has_value()) { return; }  // no colour readback
        ++judged;
        EXPECT_EQ(*all, red) << "a mask keeping every sample";
        const CNA::RendererFeatureSupport support =
            device.GetRendererFeatureSupportEXT(CNA::RendererFeature::MultiSampleMask);
        const std::optional<Color> none = draw(0);
        ASSERT_TRUE(none.has_value());
        if (support == CNA::RendererFeatureSupport::Unsupported)
        {
            EXPECT_EQ(*none, red) << name << " reports no mask support but honoured mask 0";
            return;
        }
        EXPECT_EQ(*none, Color(0, 0, 0, 255)) << name << " ignored a mask keeping no sample";
        if (support == CNA::RendererFeatureSupport::Restricted)
        {
            EXPECT_THROW((void)draw(0x1), System::NotSupportedException)
                << name << " reports a restricted mask but did not refuse a partial one";
            return;
        }
        const std::optional<Color> one = draw(0x1);
        ASSERT_TRUE(one.has_value());
        EXPECT_NEAR(one->getRProperty(), 64, 24) << name << " resolved one of four samples";
        EXPECT_EQ(support, CNA::RendererFeatureSupport::Supported);
    });
    EXPECT_GT(judged, 0) << "no renderer in this build drew and read back the mask probe";
}

#endif  // CNA_MULTI_RENDERER
