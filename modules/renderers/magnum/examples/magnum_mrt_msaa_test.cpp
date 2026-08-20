// SPDX-License-Identifier: MS-PL
// plans/plan_magnum.md MAGNUM-56: multi-target MSAA for the MAGNUM renderer.
//
// "Is this attachment genuinely multisampled?" cannot be answered by looking at a resolved image:
// a single-sampled attachment resolves to the same colour a multisampled one does whenever every
// sample agrees. The discriminator is `gl_SampleMask[0] = 1`, which writes exactly ONE sample per
// pixel -- so after the resolve a multisampled attachment reads back at 255/samples, while a
// single-sampled one reads back at full 255 because its one sample IS the pixel.
//
// That shader needs GLSL 4.00, which is also what this exercises on the effect side: a
// ShaderEffect's declared #version has to survive to the compiler.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kRequestedSamples = 4;

    const char* kVertexShader = R"GLSL(#version 400 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
uniform mat4 uWVP;
void main()
{
    gl_Position = uWVP * vec4(aPosition, 1.0);
}
)GLSL";

    // One sample per pixel, and two distinct outputs so each slot's own resolve is measured
    // separately rather than inferred from the first.
    const char* kFragmentShader = R"GLSL(#version 400 core
layout(location = 0) out vec4 outTarget0;
layout(location = 1) out vec4 outTarget1;
void main()
{
    gl_SampleMask[0] = 1;
    outTarget0 = vec4(1.0, 0.0, 0.0, 1.0);
    outTarget1 = vec4(0.0, 1.0, 0.0, 1.0);
}
)GLSL";
}

class MagnumMrtMsaaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> deviceManager_;
    std::unique_ptr<ShaderEffect> effect_;
    bool done_ = false;
    int result_ = 0;

    void Pass(const char* label, const char* detail)
    {
        std::printf("[PASS] %s: %s\n", label, detail);
    }

    void Fail(const char* label, const char* detail)
    {
        std::printf("[FAIL] %s: %s\n", label, detail);
        result_ = 1;
    }

    std::unique_ptr<RenderTarget2D> MakeTarget(int samples)
    {
        return std::make_unique<RenderTarget2D>(
            getGraphicsDeviceProperty(), kSize, kSize, false, SurfaceFormat::Color,
            DepthFormat::None, samples, RenderTargetUsage::DiscardContents);
    }

    Color ReadCentre(RenderTarget2D& target)
    {
        std::array<Color, 1> pixel = {Color(0, 0, 0, 0)};
        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        target.GetData(0, &centre, pixel.data(), 0, 1);
        return pixel[0];
    }

    void DrawFullScreen()
    {
        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        const std::array<float, 16> identity = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        effect_->SetUniformMat4("uWVP", identity.data());
        effect_->Apply();

        const Color white(255, 255, 255, 255);
        const std::array<VertexPositionColor, 6> quad = {
            VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), white),
            VertexPositionColor(Vector3(-1.0f,  1.0f, 0.0f), white),
            VertexPositionColor(Vector3( 1.0f,  1.0f, 0.0f), white),
            VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), white),
            VertexPositionColor(Vector3( 1.0f,  1.0f, 0.0f), white),
            VertexPositionColor(Vector3( 1.0f, -1.0f, 0.0f), white),
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        effect_ = std::make_unique<ShaderEffect>(getGraphicsDeviceProperty(),
                                                 kVertexShader, kFragmentShader);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        if (!effect_->IsEffectValid())
        {
            Fail("a 4.00 ShaderEffect compiles",
                 "the effect is invalid -- the declared #version did not reach the compiler");
            Exit();
            return;
        }
        Pass("a 4.00 ShaderEffect compiles", "gl_SampleMask is available to the fragment stage");

        auto slot0 = MakeTarget(kRequestedSamples);
        auto slot1 = MakeTarget(kRequestedSamples);
        const int samples = slot0->getMultiSampleCountProperty();
        if (samples <= 1)
        {
            std::printf("[NOTE] this driver applied %d samples, so the discriminator cannot run\n",
                        samples);
            Exit();
            return;
        }

        // One sample of every pixel, resolved across `samples` of them.
        const int oneSample = static_cast<int>(std::lround(255.0 / samples));

        std::vector<RenderTargetBinding> bindings;
        bindings.emplace_back(slot0.get());
        bindings.emplace_back(slot1.get());
        device.SetRenderTargets(bindings);
        device.Clear(Color(0, 0, 0, 0));
        DrawFullScreen();
        device.SetRenderTargets({});

        const Color got0 = ReadCentre(*slot0);
        const Color got1 = ReadCentre(*slot1);
        char detail[192];

        const bool slot0Multisampled = std::abs(got0.getRProperty() - oneSample) <= 8
                                    && got0.getGProperty() <= 8;
        std::snprintf(detail, sizeof(detail),
                      "slot 0 read back (%d,%d,%d), one of %d samples is %d",
                      got0.getRProperty(), got0.getGProperty(), got0.getBProperty(),
                      samples, oneSample);
        if (slot0Multisampled)
            Pass("slot 0 keeps its multisample storage inside a set", detail);
        else
            Fail("slot 0 keeps its multisample storage inside a set", detail);

        const bool slot1Multisampled = std::abs(got1.getGProperty() - oneSample) <= 8
                                    && got1.getRProperty() <= 8;
        std::snprintf(detail, sizeof(detail),
                      "slot 1 read back (%d,%d,%d), one of %d samples is %d",
                      got1.getRProperty(), got1.getGProperty(), got1.getBProperty(),
                      samples, oneSample);
        if (slot1Multisampled)
            Pass("slot 1 resolves independently of slot 0", detail);
        else
            Fail("slot 1 resolves independently of slot 0", detail);

        // The same draw into a single-sampled set must read back saturated, which is what makes
        // the two cases above a measurement rather than a coincidence.
        auto plain0 = MakeTarget(0);
        auto plain1 = MakeTarget(0);
        std::vector<RenderTargetBinding> plainBindings;
        plainBindings.emplace_back(plain0.get());
        plainBindings.emplace_back(plain1.get());
        device.SetRenderTargets(plainBindings);
        device.Clear(Color(0, 0, 0, 0));
        DrawFullScreen();
        device.SetRenderTargets({});

        const Color plainGot = ReadCentre(*plain0);
        std::snprintf(detail, sizeof(detail), "single-sampled slot 0 read back (%d,%d,%d)",
                      plainGot.getRProperty(), plainGot.getGProperty(), plainGot.getBProperty());
        if (plainGot.getRProperty() >= 247)
            Pass("a single-sampled set still reads back saturated", detail);
        else
            Fail("a single-sampled set still reads back saturated", detail);

        Exit();
    }

public:
    MagnumMrtMsaaTest()
    {
        deviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        deviceManager_->setPreferredBackBufferWidthProperty(kSize);
        deviceManager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    MagnumMrtMsaaTest game;
    game.Run();
    return game.GetResult();
}
