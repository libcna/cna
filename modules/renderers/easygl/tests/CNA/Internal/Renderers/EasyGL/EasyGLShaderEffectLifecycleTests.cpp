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

    // Draws the explicitly set tint.
    const char* const kTintSource = R"(#version 300 es
precision mediump float;
in vec2 TexCoord;
out vec4 FragColor;
uniform vec4 uMyTint;
void main() { FragColor = uMyTint; }
)";

    // Both elements contribute to the pixel; losing the second element changes yellow to red.
    const char* const kArraySource = R"(#version 300 es
precision highp float;
out vec4 FragColor;
uniform float uValues[2];
void main() { FragColor = vec4(uValues[0], uValues[1], 0.0, 1.0); }
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

TEST_F(EasyGLShaderEffectLifecycleTest, ActiveArrayLookupIsCachedAndValuesReachThePixel)
{
    ShaderEffect effect(*device, kVertexSource, kArraySource);
    ASSERT_TRUE(effect.IsEffectValid());
    EasyGLEffectRenderer& renderer = RendererOf(effect);
    const int location = renderer.ArrayUniformLocation("uValues");
    ASSERT_GE(location, 0);
    EXPECT_EQ(renderer.UniformLocation("uValues[0]"), location);
    const std::uint64_t queries = renderer.GetUniformLocationQueryCountEXT();
    const float values[] = {1.0f, 1.0f};
    for (int repeat = 0; repeat < 5; ++repeat)
    {
        EXPECT_EQ(renderer.ArrayUniformLocation("uValues"), location);
        effect.SetUniformFloatArray("uValues", values, 2);
    }
    EXPECT_EQ(renderer.GetUniformLocationQueryCountEXT(), queries);
    EXPECT_EQ(DrawThrough(effect), Color::Yellow);
}

TEST_F(EasyGLShaderEffectLifecycleTest, ShorterFloatArrayWritePreservesSuffixAfterRecovery)
{
    ShaderEffect effect(*device, kVertexSource, kArraySource);
    ASSERT_TRUE(effect.IsEffectValid());
    const float full[] = {0.0f, 1.0f};
    const float prefix[] = {1.0f};
    effect.SetUniformFloatArray("uValues", full, 2);
    effect.SetUniformFloatArray("uValues", prefix, 1);
    ASSERT_EQ(DrawThrough(effect), Color::Yellow);
    device->GetRenderer().DebugSimulateContextLoss();
    EXPECT_TRUE(effect.IsEffectValid());
    EXPECT_EQ(DrawThrough(effect), Color::Yellow);
}

TEST_F(EasyGLShaderEffectLifecycleTest, ShorterVec2ArrayWritePreservesSuffixAfterRecovery)
{
    const char* fragment = R"(#version 300 es
precision highp float;
out vec4 FragColor;
uniform vec2 uValues[2];
void main() { FragColor = vec4(uValues[0].x, uValues[1].y, 0.0, 1.0); }
)";
    ShaderEffect effect(*device, kVertexSource, fragment);
    ASSERT_TRUE(effect.IsEffectValid());
    const float full[] = {0.0f, 0.0f, 0.0f, 1.0f};
    const float prefix[] = {1.0f, 0.0f};
    effect.SetUniformVec2Array("uValues", full, 2);
    effect.SetUniformVec2Array("uValues", prefix, 1);
    ASSERT_EQ(DrawThrough(effect), Color::Yellow);
    device->GetRenderer().DebugSimulateContextLoss();
    EXPECT_EQ(DrawThrough(effect), Color::Yellow);
}

TEST_F(EasyGLShaderEffectLifecycleTest, ShorterVec3ArrayWritePreservesSuffixAfterRecovery)
{
    const char* fragment = R"(#version 300 es
precision highp float;
out vec4 FragColor;
uniform vec3 uValues[2];
void main() { FragColor = vec4(uValues[0].x, uValues[1].z, 0.0, 1.0); }
)";
    ShaderEffect effect(*device, kVertexSource, fragment);
    ASSERT_TRUE(effect.IsEffectValid());
    const float full[] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float prefix[] = {1.0f, 0.0f, 0.0f};
    effect.SetUniformVec3Array("uValues", full, 2);
    effect.SetUniformVec3Array("uValues", prefix, 1);
    ASSERT_EQ(DrawThrough(effect), Color::Yellow);
    device->GetRenderer().DebugSimulateContextLoss();
    EXPECT_EQ(DrawThrough(effect), Color::Yellow);
}

TEST_F(EasyGLShaderEffectLifecycleTest, ShorterMatrixArrayWritePreservesSuffixAfterRecovery)
{
    const char* fragment = R"(#version 300 es
precision highp float;
out vec4 FragColor;
uniform mat4 uValues[2];
void main() { FragColor = vec4(uValues[0][0][0], uValues[1][1][1], 0.0, 1.0); }
)";
    ShaderEffect effect(*device, kVertexSource, fragment);
    ASSERT_TRUE(effect.IsEffectValid());
    float full[32] = {};
    full[16 + 5] = 1.0f;
    float prefix[16] = {};
    prefix[0] = 1.0f;
    effect.SetUniformMat4Array("uValues", full, 2);
    effect.SetUniformMat4Array("uValues", prefix, 1);
    ASSERT_EQ(DrawThrough(effect), Color::Yellow);
    device->GetRenderer().DebugSimulateContextLoss();
    EXPECT_EQ(DrawThrough(effect), Color::Yellow);
}

TEST_F(EasyGLShaderEffectLifecycleTest, ZeroCountArrayWritesPreserveValuesAfterRecovery)
{
    const char* fragment = R"(#version 300 es
precision highp float;
out vec4 FragColor;
uniform float uFloats[2];
uniform vec2 uVec2s[2];
uniform vec3 uVec3s[2];
uniform mat4 uMatrices[2];
void main() { FragColor = vec4(uFloats[1], uVec2s[1].y, uVec3s[1].z, uMatrices[1][0][0]); }
)";
    ShaderEffect effect(*device, kVertexSource, fragment);
    ASSERT_TRUE(effect.IsEffectValid());
    const float floats[] = {0.0f, 1.0f};
    const float vec2s[] = {0.0f, 0.0f, 0.0f, 1.0f};
    const float vec3s[] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    float matrices[32] = {};
    matrices[16] = 1.0f;
    effect.SetUniformFloatArray("uFloats", floats, 2);
    effect.SetUniformVec2Array("uVec2s", vec2s, 2);
    effect.SetUniformVec3Array("uVec3s", vec3s, 2);
    effect.SetUniformMat4Array("uMatrices", matrices, 2);
    effect.SetUniformFloatArray("uFloats", floats, 0);
    effect.SetUniformVec2Array("uVec2s", vec2s, 0);
    effect.SetUniformVec3Array("uVec3s", vec3s, 0);
    effect.SetUniformMat4Array("uMatrices", matrices, 0);
    ASSERT_EQ(DrawThrough(effect), Color::White);
    device->GetRenderer().DebugSimulateContextLoss();
    EXPECT_EQ(DrawThrough(effect), Color::White);
}

TEST_F(EasyGLShaderEffectLifecycleTest, ArrayAliasWritesRecoverInBothOrders)
{
    ShaderEffect effect(*device, kVertexSource, kArraySource);
    ASSERT_TRUE(effect.IsEffectValid());
    const float first[] = {0.0f, 1.0f};
    effect.SetUniformFloatArray("uValues", first, 2);
    effect.SetUniformFloat("uValues[0]", 1.0f);
    ASSERT_EQ(DrawThrough(effect), Color::Yellow);
    device->GetRenderer().DebugSimulateContextLoss();
    EXPECT_EQ(DrawThrough(effect), Color::Yellow);

    // Keep the same two names but reverse which writes last. Fixed map iteration cannot
    // reproduce both histories. Recovery checks stay nonfatal so both orders are witnessed.
    const float second[] = {1.0f, 1.0f};
    effect.SetUniformFloat("uValues[0]", 0.0f);
    effect.SetUniformFloatArray("uValues", second, 2);
    ASSERT_EQ(DrawThrough(effect), Color::Yellow);
    device->GetRenderer().DebugSimulateContextLoss();
    EXPECT_EQ(DrawThrough(effect), Color::Yellow);
}

TEST_F(EasyGLShaderEffectLifecycleTest, ArrayElementOverlapRecoversInWriteOrder)
{
    ShaderEffect effect(*device, kVertexSource, kArraySource);
    ASSERT_TRUE(effect.IsEffectValid());
    const float first[] = {1.0f, 0.0f};
    effect.SetUniformFloatArray("uValues", first, 2);
    effect.SetUniformFloat("uValues[1]", 1.0f);
    ASSERT_EQ(DrawThrough(effect), Color::Yellow);
    device->GetRenderer().DebugSimulateContextLoss();
    EXPECT_EQ(DrawThrough(effect), Color::Yellow);

    const float second[] = {1.0f, 1.0f};
    effect.SetUniformFloat("uValues[1]", 0.0f);
    effect.SetUniformFloatArray("uValues", second, 2);
    ASSERT_EQ(DrawThrough(effect), Color::Yellow);
    device->GetRenderer().DebugSimulateContextLoss();
    EXPECT_EQ(DrawThrough(effect), Color::Yellow);
}

TEST_F(EasyGLShaderEffectLifecycleTest, FailedShaderRecompilePreservesProgramAndRecovery)
{
    ShaderEffect effect(*device, kVertexSource, kArraySource);
    ASSERT_TRUE(effect.IsEffectValid());
    const float values[] = {1.0f, 1.0f};
    effect.SetUniformFloatArray("uValues", values, 2);
    ASSERT_EQ(DrawThrough(effect), Color::Yellow);
    EasyGLEffectRenderer& renderer = RendererOf(effect);
    const std::uint64_t queries = renderer.GetUniformLocationQueryCountEXT();

    const char* invalid = "#version 300 es\nthis is not a shader";
    ASSERT_FALSE(renderer.CompileProgram(kVertexSource, invalid));
    EXPECT_FALSE(renderer.GetCompileError().empty());
    ASSERT_TRUE(renderer.IsValid());
    EXPECT_EQ(DrawThrough(effect), Color::Yellow);
    EXPECT_EQ(renderer.GetUniformLocationQueryCountEXT(), queries)
        << "failed compilation must leave the old program's cached locations intact";
    device->GetRenderer().DebugSimulateContextLoss();
    ASSERT_TRUE(renderer.IsValid());
    EXPECT_EQ(DrawThrough(effect), Color::Yellow);
}

TEST_F(EasyGLShaderEffectLifecycleTest, FailedLinkPreservesProgramAndRecovery)
{
    ShaderEffect effect(*device, kVertexSource, kArraySource);
    ASSERT_TRUE(effect.IsEffectValid());
    const float values[] = {1.0f, 1.0f};
    effect.SetUniformFloatArray("uValues", values, 2);
    ASSERT_EQ(DrawThrough(effect), Color::Yellow);
    EasyGLEffectRenderer& renderer = RendererOf(effect);
    const std::uint64_t queries = renderer.GetUniformLocationQueryCountEXT();

    // kVertexSource exports vec2 TexCoord; both shaders compile, but these interfaces cannot link.
    const char* mismatched = R"(#version 300 es
precision highp float;
in vec3 TexCoord;
out vec4 FragColor;
void main() { FragColor = vec4(TexCoord, 1.0); }
)";
    ASSERT_FALSE(renderer.CompileProgram(kVertexSource, mismatched));
    EXPECT_EQ(renderer.GetCompileError().find("Link:"), 0u);
    ASSERT_TRUE(renderer.IsValid());
    EXPECT_EQ(DrawThrough(effect), Color::Yellow);
    EXPECT_EQ(renderer.GetUniformLocationQueryCountEXT(), queries);
    device->GetRenderer().DebugSimulateContextLoss();
    ASSERT_TRUE(renderer.IsValid());
    EXPECT_EQ(DrawThrough(effect), Color::Yellow);
}

// UB-009: sample real GPU mip contents; Texture2D::GetData can read its CPU shadow.
TEST_F(EasyGLShaderEffectLifecycleTest, AuthoredMipContentsSurviveRepeatedContextRecovery)
{
    white = std::make_unique<Texture2D>(*device, 8, 8, true, SurfaceFormat::Color);
    const Color colours[] = {Color::Red, Color::Green, Color::Blue, Color::Yellow};
    for (int level = 0; level < 4; ++level)
    {
        const int side = 8 >> level;
        const std::vector<Color> pixels(side * side, colours[level]);
        white->SetData(level, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
    }
    ShaderEffect effect(*device, kVertexSource, R"(#version 300 es
precision highp float;
uniform sampler2D uProbeTexture;
uniform float uProbeLevel;
out vec4 FragColor;
void main() { FragColor = textureLod(uProbeTexture, vec2(0.5), uProbeLevel); }
)");
    ASSERT_TRUE(effect.IsEffectValid());
    effect.SetUniformInt("uProbeTexture", 0);
    for (int recovery = 0; recovery < 3; ++recovery)
    {
        if (recovery != 0) device->GetRenderer().DebugSimulateContextLoss();
        for (int level = 0; level < 4; ++level)
        {
            effect.SetUniformFloat("uProbeLevel", static_cast<float>(level));
            EXPECT_EQ(DrawThrough(effect), colours[level]) << "recovery=" << recovery << " mip=" << level;
        }
        // A partial update must retain the rest of the authored level for the next recovery.
        device->getTexturesProperty()(0, nullptr);
        const Rectangle corner(0, 0, 1, 1);
        const Color changed = Color::Magenta;
        white->SetData(1, &corner, &changed, 0, 1);
    }
}

TEST_F(EasyGLShaderEffectLifecycleTest, CompressedAuthoredMipContentsSurviveContextRecovery)
{
    if (!device->GetRenderer().IsCompressedTransferFormatEXT(static_cast<int>(SurfaceFormat::Dxt1)))
        GTEST_SKIP() << "DXT1 is unavailable";
    white = std::make_unique<Texture2D>(*device, 8, 8, true, SurfaceFormat::Dxt1);
    const std::uint16_t packed[] = {0xf800, 0x07e0, 0x001f, 0xffe0};
    const Color colours[] = {Color::Red, Color(0, 255, 0), Color::Blue, Color::Yellow};
    for (int level = 0; level < 4; ++level)
    {
        const int side = 8 >> level;
        const int blocks = ((side + 3) / 4) * ((side + 3) / 4);
        std::vector<std::uint8_t> bytes(blocks * 8, 0);
        for (int block = 0; block < blocks; ++block)
        {
            bytes[block * 8] = static_cast<std::uint8_t>(packed[level]);
            bytes[block * 8 + 1] = static_cast<std::uint8_t>(packed[level] >> 8);
        }
        white->SetData(level, nullptr, bytes.data(), 0, static_cast<int>(bytes.size()));
    }
    ShaderEffect effect(*device, kVertexSource, R"(#version 300 es
precision highp float;
uniform sampler2D uProbeTexture;
uniform float uProbeLevel;
out vec4 FragColor;
void main() { FragColor = textureLod(uProbeTexture, vec2(0.5), uProbeLevel); }
)");
    ASSERT_TRUE(effect.IsEffectValid());
    effect.SetUniformInt("uProbeTexture", 0);
    for (int recovery = 0; recovery < 3; ++recovery)
    {
        if (recovery != 0) device->GetRenderer().DebugSimulateContextLoss();
        for (int level = 0; level < 4; ++level)
        {
            effect.SetUniformFloat("uProbeLevel", static_cast<float>(level));
            EXPECT_EQ(DrawThrough(effect), colours[level]) << "recovery=" << recovery << " mip=" << level;
        }
    }
}
