// SPDX-License-Identifier: MS-PL
//
// L47 (IEFFECT-AUDIT-001): a custom ShaderEffect's uniform locations are resolved once per link,
// a recompiled program forgets what the previous one had uploaded, and a live effect survives a
// context loss with the values the game set.

#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <metagl/metagl.hpp>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace
{
    using namespace CNA::Testing::Renderers;  // NOLINT(google-build-using-namespace)
    using namespace Microsoft::Xna::Framework;  // NOLINT(google-build-using-namespace)
    using namespace Microsoft::Xna::Framework::Graphics;  // NOLINT(google-build-using-namespace)
    using CNA::Internal::Renderers::EasyGL::EasyGLEffectRenderer;

    const char* const kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() { gl_Position = projection * vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }
)";

    // Draws uMyTint; uArr and uScale stay declared and live so the program has several uniforms.
    const char* const kTintSource = R"(#version 300 es
precision mediump float;
in vec2 TexCoord;
out vec4 FragColor;
uniform vec4 uMyTint;
uniform float uArr[2];
uniform float uScale;
void main() { FragColor = uMyTint + vec4(uArr[0] + uArr[1] + uScale) * 0.0; }
)";

    // Red where the sampled source on unit 0 is a render target, black where it is uploaded.
    const char* const kFlipSource = R"(#version 300 es
precision mediump float;
out vec4 FragColor;
uniform vec4 uRtFlipV;
void main() { FragColor = vec4(uRtFlipV.x, 0.0, 0.0, 1.0); }
)";

    class EasyGLShaderEffectLifecycleTest : public ::testing::Test
    {
    protected:
        std::unique_ptr<GraphicsDevice> device;
        std::unique_ptr<SpriteBatch> batch;
        std::unique_ptr<Texture2D> white;
        std::unique_ptr<RenderTarget2D> target;

        void SetUp() override
        {
            if (!CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2))
                GTEST_SKIP() << "ShaderEffect GLSL runs only on the EasyGL renderers";
            device = std::make_unique<GraphicsDevice>(
                GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                PresentationParameters::CreateWindowedEXT(64, 64));
            batch = std::make_unique<SpriteBatch>(*device);
            white = std::make_unique<Texture2D>(*device, 1, 1);
            const Color opaque = Color::White;
            white->SetData(&opaque, 1);
            target = std::make_unique<RenderTarget2D>(*device, 8, 8);
        }

        [[nodiscard]] static EasyGLEffectRenderer& RendererOf(const ShaderEffect& effect)
        {
            auto* renderer = dynamic_cast<EasyGLEffectRenderer*>(effect.GetEffectRendererPtr());
            EXPECT_NE(renderer, nullptr);
            return *renderer;
        }

        /// Draws the white texture through @p effect into the 8x8 target and reads one pixel.
        [[nodiscard]] Color DrawThrough(ShaderEffect& effect)
        {
            device->SetRenderTarget(target.get());
            device->Clear(Color::Magenta);
            batch->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                         nullptr, nullptr, nullptr, &effect);
            batch->Draw(*white, Rectangle(0, 0, 8, 8), Color::White);
            batch->End();
            device->SetRenderTarget(nullptr);
            std::vector<Color> pixels(64);
            target->GetData(pixels.data(), static_cast<int>(pixels.size()));
            return pixels[4 * 8 + 4];
        }
    };
}

TEST_F(EasyGLShaderEffectLifecycleTest, EachUniformNameIsQueriedOncePerLink)
{
    ShaderEffect effect(*device, kVertexSource, kTintSource);
    ASSERT_TRUE(effect.IsEffectValid());
    EasyGLEffectRenderer& renderer = RendererOf(effect);

    effect.SetUniformVec4("uMyTint", 0.0f, 0.0f, 1.0f, 1.0f);
    const std::uint64_t afterFirst = renderer.GetUniformLocationQueryCountEXT();
    for (int repeat = 0; repeat < 5; ++repeat)
        effect.SetUniformVec4("uMyTint", 0.0f, 0.0f, 1.0f, 1.0f);
    EXPECT_EQ(renderer.GetUniformLocationQueryCountEXT(), afterFirst)
        << "a present uniform must be looked up once";

    effect.SetUniformFloat("uNotDeclared", 1.0f);
    const std::uint64_t afterMiss = renderer.GetUniformLocationQueryCountEXT();
    EXPECT_EQ(afterMiss, afterFirst + 1u);
    for (int repeat = 0; repeat < 5; ++repeat)
        effect.SetUniformFloat("uNotDeclared", 1.0f);
    EXPECT_EQ(renderer.GetUniformLocationQueryCountEXT(), afterMiss)
        << "an absent uniform must be remembered as absent";

    const float values[2] = {0.25f, 0.5f};
    effect.SetUniformFloatArray("uMissingArray", values, 2);
    const std::uint64_t afterArrayMiss = renderer.GetUniformLocationQueryCountEXT();
    EXPECT_EQ(afterArrayMiss, afterMiss + 2u) << "a missing array tries its bare and [0] spellings";
    for (int repeat = 0; repeat < 5; ++repeat)
        effect.SetUniformFloatArray("uMissingArray", values, 2);
    EXPECT_EQ(renderer.GetUniformLocationQueryCountEXT(), afterArrayMiss);

    // SpriteBatch writes projection and the channel pair into the custom program every flush;
    // those three names resolve on the first flush and never again.
    EXPECT_EQ(DrawThrough(effect), Color::Blue);
    const std::uint64_t afterFlush = renderer.GetUniformLocationQueryCountEXT();
    EXPECT_EQ(DrawThrough(effect), Color::Blue);
    EXPECT_EQ(DrawThrough(effect), Color::Blue);
    EXPECT_EQ(renderer.GetUniformLocationQueryCountEXT(), afterFlush);
}

TEST_F(EasyGLShaderEffectLifecycleTest, RecompilingForgetsWhatThePreviousProgramHadUploaded)
{
    ShaderEffect effect(*device, kVertexSource, kFlipSource);
    ASSERT_TRUE(effect.IsEffectValid());

    // A rendered source sets uRtFlipV.x, and the flag is recorded as uploaded.
    RenderTarget2D source(*device, 8, 8);
    device->SetRenderTarget(&source);
    device->Clear(Color::White);
    device->SetRenderTarget(nullptr);
    effect.SetTexture(0, source);
    EXPECT_EQ(DrawThrough(effect), Color::Red);

    // The same flag value against a freshly linked program: had the "already uploaded" state
    // survived, the new program would keep GL's initial zero and draw black.
    EasyGLEffectRenderer& renderer = RendererOf(effect);
    ASSERT_TRUE(renderer.CompileProgram(kVertexSource, kFlipSource));
    effect.SetTexture(0, source);
    EXPECT_EQ(DrawThrough(effect), Color::Red);
}

TEST_F(EasyGLShaderEffectLifecycleTest, ACustomEffectSurvivesAContextLossWithItsUniforms)
{
    ShaderEffect effect(*device, kVertexSource, kTintSource);
    ASSERT_TRUE(effect.IsEffectValid());
    effect.SetUniformVec4("uMyTint", 0.0f, 0.0f, 1.0f, 1.0f);
    const float values[2] = {0.25f, 0.5f};
    effect.SetUniformFloatArray("uArr", values, 2);
    ASSERT_EQ(DrawThrough(effect), Color::Blue);

    device->GetRenderer().DebugSimulateContextLoss();

    // Nothing is set again: XNA keeps an effect's parameter values across a device reset, so the
    // recovered program must draw with the value the game set before the loss.
    EXPECT_TRUE(effect.IsEffectValid());
    EXPECT_EQ(DrawThrough(effect), Color::Blue);

    // And it keeps working as an ordinary effect afterwards.
    effect.SetUniformVec4("uMyTint", 0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_EQ(DrawThrough(effect), Color::Lime);
}
