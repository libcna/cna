// SPDX-License-Identifier: MS-PL
//
// plans/plan_opengl4_modern_graphics.md GL4-0031: lifetime, context ownership, threading and GL
// object accounting of the modern resources.
//
//  * Every GL name a storage buffer, compute program, storage texture or GPU timer created is gone
//    once the object is destroyed (glIs* answers false) -- the leak accounting the brief asks for,
//    asked of the driver itself.
//  * A SpriteBatch draw is issued by End(), so an effect destroyed after End cannot be replayed:
//    OpenGL4 has no counterpart of WebGPU's queued-frame window (webgpu_effect_outlived_by_draw_test,
//    ForgetEffectEXT). The case is kept anyway, so AddressSanitizer runs of it would see a replay.
//  * Two devices each run compute in their own context, and a device destroyed mid-sequence leaves
//    the other's modern resources intact (GL4-0021's rule, applied to the Workstream B objects).
//  * Modern resources created on a loading thread under the context lease are used by the frame
//    thread afterwards, which is the ContentManager contract.

#if defined(CNA_RENDERER_OPENGL4)

#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/OpenGL4/GL4Loader.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Modern.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Renderers;
    using namespace CNA::Internal::Renderers::OpenGL4;
    using Microsoft::Xna::Framework::Color;

    /// Counts the renderer's "[OpenGL4 GL Error]" lines for the lifetime of the object.
    class GlErrorCapture
    {
    public:
        GlErrorCapture()
        {
            CNA::Logger::SetSink([this](CNA::LogLevel, CNA::LogCategory, std::string_view line) {
                if (line.find("[OpenGL4 GL Error]") != std::string_view::npos) ++errors_;
            });
        }
        ~GlErrorCapture() { CNA::Logger::ResetSink(); }
        GlErrorCapture(const GlErrorCapture&) = delete;
        GlErrorCapture& operator=(const GlErrorCapture&) = delete;
        [[nodiscard]] int Count() const { return errors_; }

    private:
        int errors_ = 0;
    };

    constexpr const char* kDoubler = R"GLSL(#version 430 core
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer Values { float values[]; };
void main() { values[gl_GlobalInvocationID.x] *= 2.0; }
)GLSL";

    bool HasCompute(GraphicsDevice& device)
    {
        return device.GetGraphicsRendererType() == CNA::GraphicsRendererType::OpenGL4 &&
               device.GetRenderer().SupportsComputeShadersEXT();
    }

    /// Uploads 0..63, doubles them on the GPU, and reads them back.
    std::vector<float> DoubleOnGpu(IGraphicsRenderer& renderer, IStorageBufferRenderer& buffer,
                                   IComputeShaderRenderer& shader)
    {
        shader.BindStorageBuffer(0, &buffer);
        renderer.DispatchCompute(&shader, 1, 1, 1);
        std::vector<float> values(64);
        buffer.GetData(values.data(), values.size() * sizeof(float));
        return values;
    }

    std::vector<float> Ramp()
    {
        std::vector<float> values(64);
        for (std::size_t i = 0; i < values.size(); ++i) values[i] = static_cast<float>(i);
        return values;
    }
}

TEST(OpenGL4ModernLifetime, EveryModernResourceReleasesItsGlNames)
{
    GraphicsDevice device;
    if (!HasCompute(device)) GTEST_SKIP() << "this run has no OpenGL4 compute";
    ASSERT_NE(GL4::gl4_glIsBuffer, nullptr);
    ASSERT_NE(GL4::gl4_glIsQuery, nullptr);
    auto& renderer = device.GetRenderer();

    std::vector<GLuint> buffers, programs, textures, queries;
    for (int round = 0; round < 3; ++round)
    {
        auto buffer = renderer.CreateStorageBuffer(256);
        auto shader = renderer.CreateComputeShader(kDoubler);
        auto image = renderer.CreateStorageTexture2DEXT(8, 8, 2, 0, UINT32_C(0x3F));
        auto timer = renderer.CreateGpuTimerEXT();
        ASSERT_NE(buffer, nullptr);
        ASSERT_NE(shader, nullptr);
        ASSERT_NE(image, nullptr);
        ASSERT_NE(timer, nullptr);

        // Used, so every name is a real object and not merely a reserved one.
        timer->Begin();
        std::vector<float> ramp = Ramp();
        buffer->SetData(ramp.data(), 256);
        shader->BindStorageBuffer(0, buffer.get());
        renderer.DispatchCompute(shader.get(), 1, 1, 1);
        timer->End();

        const auto& nativeBuffer = dynamic_cast<const OpenGL4StorageBufferRenderer&>(*buffer);
        const auto& nativeShader = dynamic_cast<const OpenGL4ComputeShaderRenderer&>(*shader);
        const auto& nativeImage = dynamic_cast<const OpenGL4StorageTexture2DRenderer&>(*image);
        const auto& nativeTimer = dynamic_cast<const OpenGL4GpuTimerRenderer&>(*timer);
        buffers.push_back(nativeBuffer.GLHandle());
        programs.push_back(nativeShader.GLProgram());
        textures.push_back(nativeImage.GLHandle());
        queries.push_back(nativeTimer.GLQuery(0));
        queries.push_back(nativeTimer.GLQuery(1));
        EXPECT_EQ(GL_TRUE, GL4::gl4_glIsBuffer(buffers.back()));
        EXPECT_EQ(GL_TRUE, GL4::gl4_glIsProgram(programs.back()));
        EXPECT_EQ(GL_TRUE, glIsTexture(textures.back()));
        EXPECT_EQ(GL_TRUE, GL4::gl4_glIsQuery(queries.back()));
    }   // everything destroyed here

    for (const GLuint name : buffers) EXPECT_EQ(GL_FALSE, GL4::gl4_glIsBuffer(name)) << name;
    for (const GLuint name : programs) EXPECT_EQ(GL_FALSE, GL4::gl4_glIsProgram(name)) << name;
    for (const GLuint name : textures) EXPECT_EQ(GL_FALSE, glIsTexture(name)) << name;
    for (const GLuint name : queries) EXPECT_EQ(GL_FALSE, GL4::gl4_glIsQuery(name)) << name;
}

TEST(OpenGL4ModernLifetime, AnEffectDestroyedAfterEndLeavesNothingToReplay)
{
    GlErrorCapture capture;
    GraphicsDevice device;
    if (device.GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";

    Texture2D white(device, 1, 1);
    const Color whiteTexel = Color::White;
    white.SetData(&whiteTexel, 1);
    RenderTarget2D target(device, 4, 4);
    SpriteBatch batch(device);

    auto effect = std::make_unique<ShaderEffect>(device, R"GLSL(#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform mat4 projection;
void main() { gl_Position = projection * vec4(aPos, 1.0); }
)GLSL",
                                                 R"GLSL(#version 410 core
out vec4 FragColor;
void main() { FragColor = vec4(1.0, 0.0, 0.0, 1.0); }
)GLSL");
    ASSERT_TRUE(effect->IsEffectValid());

    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    batch.Begin(SpriteSortMode::Deferred, nullptr, nullptr, nullptr, nullptr, effect.get());
    batch.Draw(white, Microsoft::Xna::Framework::Rectangle(0, 0, 4, 4), Color::White);
    batch.End();
    effect.reset();   // the draw above was issued by End(); nothing still names the effect

    // An ordinary batch afterwards draws through the stock program, not a stale one.
    batch.Begin();
    batch.Draw(white, Microsoft::Xna::Framework::Rectangle(0, 0, 2, 4), Color::Lime);
    batch.End();
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    std::array<Color, 16> pixels{};
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
        {
            const Color expected = x < 2 ? Color::Lime : Color::Red;
            EXPECT_EQ(expected.getPackedValueProperty(),
                      pixels[static_cast<std::size_t>(y * 4 + x)].getPackedValueProperty())
                << x << "," << y;
        }
    EXPECT_EQ(0, capture.Count());
}

TEST(OpenGL4ModernLifetime, TwoDevicesComputeInTheirOwnContexts)
{
    GlErrorCapture capture;
    auto first = std::make_unique<GraphicsDevice>();
    if (!HasCompute(*first)) GTEST_SKIP() << "this run has no OpenGL4 compute";
    GraphicsDevice second;

    auto& firstRenderer = first->GetRenderer();
    auto& secondRenderer = second.GetRenderer();
    auto firstBuffer = firstRenderer.CreateStorageBuffer(256);
    auto firstShader = firstRenderer.CreateComputeShader(kDoubler);
    auto secondBuffer = secondRenderer.CreateStorageBuffer(256);
    auto secondShader = secondRenderer.CreateComputeShader(kDoubler);
    const std::vector<float> ramp = Ramp();
    firstBuffer->SetData(ramp.data(), 256);
    secondBuffer->SetData(ramp.data(), 256);

    // Interleaved: each call arrives while the other device's context is current.
    const std::vector<float> firstOnce = DoubleOnGpu(firstRenderer, *firstBuffer, *firstShader);
    const std::vector<float> secondOnce = DoubleOnGpu(secondRenderer, *secondBuffer, *secondShader);
    const std::vector<float> firstTwice = DoubleOnGpu(firstRenderer, *firstBuffer, *firstShader);
    for (std::size_t i = 0; i < ramp.size(); ++i)
    {
        EXPECT_EQ(ramp[i] * 2.0f, firstOnce[i]) << i;
        EXPECT_EQ(ramp[i] * 2.0f, secondOnce[i]) << i;
        EXPECT_EQ(ramp[i] * 4.0f, firstTwice[i]) << i;
    }

    // The first device goes, taking its context; its resources then issue no GL, and the second
    // device's are untouched.
    first.reset();
    EXPECT_NO_THROW(firstShader.reset());
    EXPECT_NO_THROW(firstBuffer.reset());
    const std::vector<float> secondTwice = DoubleOnGpu(secondRenderer, *secondBuffer, *secondShader);
    for (std::size_t i = 0; i < ramp.size(); ++i)
        EXPECT_EQ(ramp[i] * 4.0f, secondTwice[i]) << i;
    EXPECT_EQ(0, capture.Count());
}

TEST(OpenGL4ModernLifetime, ResourcesCreatedOnALoadingThreadServeTheFrameThread)
{
    GlErrorCapture capture;
    GraphicsDevice device;
    if (!HasCompute(device)) GTEST_SKIP() << "this run has no OpenGL4 compute";
    auto& renderer = device.GetRenderer();

    // The frame thread gives up its binding, as a Game does between frames.
    renderer.AcquireThreadContextLeaseEXT(RendererThreadContextLeaseRelease::ReleaseRendererBinding)
        .reset();

    std::unique_ptr<IStorageBufferRenderer> buffer;
    std::unique_ptr<IComputeShaderRenderer> shader;
    std::thread loader([&] {
        auto lease = renderer.AcquireThreadContextLeaseEXT(
            RendererThreadContextLeaseRelease::ReleaseRendererBinding);
        buffer = renderer.CreateStorageBuffer(256);
        shader = renderer.CreateComputeShader(kDoubler);
        const std::vector<float> ramp = Ramp();
        buffer->SetData(ramp.data(), 256);
    });
    loader.join();
    ASSERT_NE(buffer, nullptr);
    ASSERT_NE(shader, nullptr);
    ASSERT_TRUE(shader->IsValid()) << shader->GetCompileError();

    const std::vector<float> doubled = DoubleOnGpu(renderer, *buffer, *shader);
    const std::vector<float> ramp = Ramp();
    for (std::size_t i = 0; i < ramp.size(); ++i) EXPECT_EQ(ramp[i] * 2.0f, doubled[i]) << i;
    EXPECT_EQ(0, capture.Count());
}

#endif // CNA_RENDERER_OPENGL4
