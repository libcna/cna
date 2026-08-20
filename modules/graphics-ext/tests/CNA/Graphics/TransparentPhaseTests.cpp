// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2104, MOD-2105: the pipeline's transparent phase.
//
// Two claims and one promise. The claims: transparent geometry is drawn into the scene target, so
// it is tonemapped and graded with everything else rather than pasted on afterwards; and the phase
// sets depth **testing** on with depth **writing** off, which is the pair that decides whether a
// second transparent surface behind the first is visible at all. The promise is the one an opt-in
// feature has to keep: a pipeline nobody asked for transparency from renders the frame it rendered
// before this existed, pixel for pixel.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/TransparencyMode.hpp"
#include "CNA/Graphics/WeightedBlendedTransparency.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace {

using CNA::Graphics::RenderPipeline;
using CNA::Graphics::RenderPipelineSettings;
using CNA::Graphics::TransparencyMode;
using CNA::Graphics::WeightedBlendedTransparency;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

/// The back buffer is whatever the headless device made; the pipeline is sized to match it, because
/// `RenderPipeline::resize` sizes the pipeline's own targets and not the window. Reading the frame
/// at any other size is the "data array too small" error, not a rendering result.
struct Frame
{
    int Width  = 0;
    int Height = 0;
    std::vector<Color> Pixels;

    [[nodiscard]] const Color& at(const float u, const float v) const
    {
        const int x = std::min(Width - 1, static_cast<int>(u * static_cast<float>(Width)));
        const int y = std::min(Height - 1, static_cast<int>(v * static_cast<float>(Height)));
        return Pixels[static_cast<std::size_t>(y) * Width + x];
    }
};

constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
void main() { gl_Position = vec4(aPos, 1.0); }
)";

/// A quad over the left half of the frame, at a chosen clip depth.
std::array<VertexPositionColor, 6> LeftHalf(const float z)
{
    const auto at = [z](const float x, const float y) {
        return VertexPositionColor(Vector3(x, y, z), Color::White);
    };
    return {at(-1.0f, -1.0f), at(-1.0f, 1.0f), at(0.0f, 1.0f),
            at(-1.0f, -1.0f), at(0.0f, 1.0f),  at(0.0f, -1.0f)};
}

std::unique_ptr<ShaderEffect> MakeFlat(GraphicsDevice& device)
{
    return std::make_unique<ShaderEffect>(device, kVertexSource, R"(#version 300 es
precision highp float;
out vec4 FragColor;
uniform vec4 uColour;
void main() { FragColor = uColour; }
)");
}

Frame BackBuffer(GraphicsDevice& device)
{
    Frame frame;
    frame.Width  = device.getViewportProperty().getWidthProperty();
    frame.Height = device.getViewportProperty().getHeightProperty();
    frame.Pixels.assign(static_cast<std::size_t>(frame.Width) * frame.Height, Color(0, 0, 0, 0));
    device.GetBackBufferData(frame.Pixels.data(), static_cast<int>(frame.Pixels.size()));
    return frame;
}

void DrawQuad(GraphicsDevice& device, ShaderEffect& effect, const float z, const Color& colour,
              const float alpha)
{
    effect.Apply();
    effect.SetUniformVec4("uColour", colour.getRProperty() / 255.0f, colour.getGProperty() / 255.0f,
                          colour.getBProperty() / 255.0f, alpha);
    const auto quad = LeftHalf(z);
    device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
}

TEST(TransparentPhaseTest, TheModeDefaultsToNoneAndRoundTrips)
{
    RenderPipelineSettings settings;
    EXPECT_EQ(settings.getTransparencyMode(), TransparencyMode::None)
        << "a game that never asked for transparency must not get a transparent phase";
    settings.setTransparencyMode(TransparencyMode::Sorted);
    EXPECT_EQ(settings.getTransparencyMode(), TransparencyMode::Sorted);
    settings.setTransparencyMode(TransparencyMode::OrderIndependent);
    EXPECT_EQ(settings.getTransparencyMode(), TransparencyMode::OrderIndependent);
}

TEST(TransparentPhaseTest, ARegisteredDrawIsIgnoredWhileTheModeIsNone)
{
    // The opt-in promise, and it is asserted the strong way: the frame with a transparent draw
    // registered and the mode left alone must be the *same pixels* as the frame without one.
    GraphicsDevice device;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(device);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(device);

    RenderPipeline pipeline(device);
    pipeline.resize(device.getViewportProperty().getWidthProperty(), device.getViewportProperty().getHeightProperty());
    const auto flat = MakeFlat(device);
    if (!flat->IsEffectValid()) GTEST_SKIP() << "this renderer cannot run custom effects";

    const auto render = [&](const bool registerTransparent) {
        if (registerTransparent)
            pipeline.setTransparentScene([&] {
                device.setRasterizerStateProperty(RasterizerState::CullNone);
                device.SetVertexBuffer(nullptr);
                DrawQuad(device, *flat, 0.0f, Color(255, 0, 0, 255), 0.5f);
            });
        else
            pipeline.setTransparentScene(nullptr);
        pipeline.begin(Color(20, 40, 80, 255));
        pipeline.end();
        return BackBuffer(device);
    };

    const Frame without = render(false);
    const Frame with    = render(true);
    ASSERT_EQ(without.Pixels.size(), with.Pixels.size());
    int differing = 0;
    for (std::size_t i = 0; i < without.Pixels.size(); ++i)
        if (without.Pixels[i].getRProperty() != with.Pixels[i].getRProperty()) ++differing;
    EXPECT_EQ(differing, 0) << "a registered draw ran while the mode was None";
}

TEST(TransparentPhaseTest, TheSortedPhaseReachesTheFrame)
{
    GraphicsDevice device;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(device);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(device);

    RenderPipeline pipeline(device);
    pipeline.resize(device.getViewportProperty().getWidthProperty(), device.getViewportProperty().getHeightProperty());
    const auto flat = MakeFlat(device);
    if (!flat->IsEffectValid()) GTEST_SKIP() << "this renderer cannot run custom effects";

    pipeline.getSettings().setTransparencyMode(TransparencyMode::Sorted);
    pipeline.setTransparentScene([&] {
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.SetVertexBuffer(nullptr);
        DrawQuad(device, *flat, 0.0f, Color(255, 0, 0, 255), 0.5f);
    });
    pipeline.begin(Color(0, 0, 255, 255));
    pipeline.end();

    const Frame frame = BackBuffer(device);
    EXPECT_GT(frame.at(0.25f, 0.5f).getRProperty(), 64)
        << "the transparent quad did not reach the frame";
    EXPECT_GT(frame.at(0.25f, 0.5f).getBProperty(), 32)
        << "it covered the background instead of blending";
    EXPECT_LT(frame.at(0.75f, 0.5f).getRProperty(), 32)
        << "it covered the half it was not drawn on";
}

TEST(TransparentPhaseTest, DepthIsTestedAndNotWritten)
{
    // MOD-2105, and the reason it is its own row: writing depth from a transparent surface hides
    // every transparent surface behind it, which looks like missing geometry rather than like a
    // state mistake. Two quads at different depths, the near one drawn first -- if depth were
    // written, the far one would be rejected and the frame would show only the near colour.
    GraphicsDevice device;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(device);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(device);

    RenderPipeline pipeline(device);
    pipeline.resize(device.getViewportProperty().getWidthProperty(), device.getViewportProperty().getHeightProperty());
    const auto flat = MakeFlat(device);
    if (!flat->IsEffectValid()) GTEST_SKIP() << "this renderer cannot run custom effects";

    pipeline.getSettings().setTransparencyMode(TransparencyMode::Sorted);
    pipeline.setTransparentScene([&] {
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.SetVertexBuffer(nullptr);
        DrawQuad(device, *flat, -0.5f, Color(255, 0, 0, 255), 0.5f);   // nearer, drawn first
        DrawQuad(device, *flat, 0.5f, Color(0, 255, 0, 255), 0.5f);    // further, drawn second
    });
    pipeline.begin(Color::Black);
    pipeline.end();

    const Frame frame = BackBuffer(device);
    EXPECT_GT(frame.at(0.25f, 0.5f).getGProperty(), 16)
        << "the further transparent surface is missing, so the nearer one wrote depth";
    EXPECT_GT(frame.at(0.25f, 0.5f).getRProperty(), 16)
        << "the nearer transparent surface is missing";
}

TEST(TransparentPhaseTest, TheOrderIndependentPathKeepsTheOpaqueFrame)
{
    // The trap this path sets for itself: it unbinds the scene target to accumulate into its own
    // and binds it again to resolve, and a DiscardContents target throws the whole opaque frame
    // away on that second bind. The right half of the frame has no transparency on it at all, so
    // it is exactly the opaque background -- and that is what this reads.
    GraphicsDevice device;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(device);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(device);

    RenderPipeline pipeline(device);
    pipeline.resize(device.getViewportProperty().getWidthProperty(), device.getViewportProperty().getHeightProperty());
    WeightedBlendedTransparency probe(device, 4, 4);
    if (!probe.isSupported()) GTEST_SKIP() << probe.getUnsupportedReason();

    std::string source = "#version 300 es\nprecision highp float;\n";
    source += WeightedBlendedTransparency::getAccumulationGlsl();
    source += R"(
uniform vec4 uColour;
void main() { cnaOitEmit(uColour.rgb, uColour.a, 10.0); }
)";
    ShaderEffect emitter(device, kVertexSource, source);
    ASSERT_TRUE(emitter.IsEffectValid());

    pipeline.getSettings().setTransparencyMode(TransparencyMode::OrderIndependent);
    pipeline.setTransparentScene([&] {
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.SetVertexBuffer(nullptr);
        emitter.Apply();
        emitter.SetUniformVec4("uColour", 1.0f, 0.0f, 0.0f, 0.5f);
        emitter.SetUniformFloat("uCnaOitFarPlane", 100.0f);
        const auto quad = LeftHalf(0.0f);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
    });
    pipeline.begin(Color(0, 0, 200, 255));
    pipeline.end();

    EXPECT_TRUE(pipeline.getTransparencyFallbackReasonEXT().empty())
        << pipeline.getTransparencyFallbackReasonEXT();

    const Frame frame = BackBuffer(device);
    EXPECT_GT(frame.at(0.75f, 0.5f).getBProperty(), 128)
        << "the opaque frame was discarded when the resolve bound the scene target again";
    EXPECT_GT(frame.at(0.25f, 0.5f).getRProperty(), 32)
        << "the transparent surface did not resolve";
}

} // namespace

#endif // CNA_CNAEXT
