// SPDX-License-Identifier: MS-PL
// REMED-GFX-016: public EasyGL multiple-render-target contract regression.
//
// The historical test used BasicEffect, whose fragment shader has only output location 0, and
// expected attachment 1's old blue contents to survive an MRT bind. RenderTargetUsage defaults
// to DiscardContents, however, so GraphicsDevice correctly cleared the complete active target set
// to black before that single-output draw. That test therefore could not distinguish "attachment
// 1 is not active" from the documented single-output-shader behavior.
//
// This replacement uses one public ShaderEffect fragment invocation with explicitly distinct
// outputs at locations 0..3. It covers ordered one-through-four-target rendering, independent
// ColorWriteChannels0..3, first-target depth ownership, true MRT MSAA + resolve, target-set
// transitions/replacement, RenderTargetUsage, direct readback, immediate producer-to-consumer
// sampling after unbind, deterministic compatibility rejection, and the native GL error queue.
//
// Exit code 0 = every check passed, 1 = one or more checks failed.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kTargetSize = 32;
    const Color kClear(10, 20, 30, 40);
    const Color kBaseA(200, 100, 50, 220);
    const Color kBaseB(40, 180, 220, 160);
    const Color kBaseC(230, 60, 140, 200);

    const char* kMrtVertexShader = R"GLSL(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
void main()
{
    gl_Position = Projection * View * World * vec4(aPosition, 1.0);
}
)GLSL";

    const char* kMrtFragmentShader = R"GLSL(#version 300 es
precision highp float;
layout(location = 0) out vec4 outTarget0;
layout(location = 1) out vec4 outTarget1;
layout(location = 2) out vec4 outTarget2;
layout(location = 3) out vec4 outTarget3;
uniform vec4 uBase;
void main()
{
    outTarget0 = uBase;
    outTarget1 = uBase.gbra;
    outTarget2 = uBase.brga;
    outTarget3 = vec4(uBase.r, uBase.b, uBase.g, uBase.a);
}
)GLSL";

    // gl_SampleMask is an ESSL 3.20 core output. Selecting exactly sample 0 makes a full-screen
    // draw resolve to 1/N of its source on a real N-sample attachment, but remain fully opaque
    // if a backend silently substituted a single-sample texture.
    const char* kMsaaVertexShader = R"GLSL(#version 320 es
precision highp float;
layout(location = 0) in vec3 aPosition;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
void main()
{
    gl_Position = Projection * View * World * vec4(aPosition, 1.0);
}
)GLSL";

    const char* kMsaaFragmentShader = R"GLSL(#version 320 es
precision highp float;
layout(location = 0) out vec4 outTarget0;
layout(location = 1) out vec4 outTarget1;
void main()
{
    gl_SampleMask[0] = 1;
    outTarget0 = vec4(1.0, 0.0, 0.0, 1.0);
    outTarget1 = vec4(0.0, 1.0, 0.0, 1.0);
}
)GLSL";

    Color OutputFor(const Color& base, int slot)
    {
        switch (slot)
        {
        case 0:
            return base;
        case 1:
            return Color(base.getGProperty(), base.getBProperty(),
                         base.getRProperty(), base.getAProperty());
        case 2:
            return Color(base.getBProperty(), base.getRProperty(),
                         base.getGProperty(), base.getAProperty());
        default:
            return Color(base.getRProperty(), base.getBProperty(),
                         base.getGProperty(), base.getAProperty());
        }
    }

    bool Near(int got, int expected, int tolerance = 4)
    {
        return std::abs(got - expected) <= tolerance;
    }

    bool Matches(const Color& got, const Color& expected, int tolerance = 4)
    {
        return Near(got.getRProperty(), expected.getRProperty(), tolerance)
            && Near(got.getGProperty(), expected.getGProperty(), tolerance)
            && Near(got.getBProperty(), expected.getBProperty(), tolerance)
            && Near(got.getAProperty(), expected.getAProperty(), tolerance);
    }

    std::string Describe(const Color& color)
    {
        return "(" + std::to_string(color.getRProperty()) + ","
            + std::to_string(color.getGProperty()) + ","
            + std::to_string(color.getBProperty()) + ","
            + std::to_string(color.getAProperty()) + ")";
    }
}

class EasyGlMrtTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<ShaderEffect> mrtEffect_;
    std::unique_ptr<ShaderEffect> msaaEffect_;
    int pass_ = 0;
    int total_ = 0;
    bool done_ = false;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (ok) ++pass_;
    }

    std::unique_ptr<RenderTarget2D> MakeTarget(
        DepthFormat depth = DepthFormat::None,
        int samples = 0,
        RenderTargetUsage usage = RenderTargetUsage::PreserveContents,
        int width = kTargetSize,
        int height = kTargetSize)
    {
        return std::make_unique<RenderTarget2D>(
            getGraphicsDeviceProperty(), width, height, false, SurfaceFormat::Color,
            depth, samples, usage);
    }

    static std::vector<RenderTargetBinding> Bindings(
        const std::vector<RenderTarget2D*>& targets)
    {
        std::vector<RenderTargetBinding> result;
        result.reserve(targets.size());
        for (RenderTarget2D* target : targets)
            result.emplace_back(target);
        return result;
    }

    Color ReadCenter(RenderTarget2D& target)
    {
        const int width = target.getWidthProperty();
        const int height = target.getHeightProperty();
        std::vector<Color> pixels(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
            Color(0, 0, 0, 0));
        target.GetData(0, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels[static_cast<std::size_t>(height / 2) * width + width / 2];
    }

    Color SampleCenter(RenderTarget2D& target)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();
        device.Clear(Color::Black);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect effect(device);
        effect.setTextureEnabledProperty(true);
        effect.setTextureProperty(&target);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();

        const VertexPositionTexture quad[6] = {
            {Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f)},
            {Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f)},
            {Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f)},
            {Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f)},
            {Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f)},
            {Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f)},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        const Rectangle center(
            viewport.getWidthProperty() / 2, viewport.getHeightProperty() / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&center, &pixel, 0, 1);
        return pixel;
    }

    void PrepareDraw(ShaderEffect& effect, const BlendState& blend,
                     const DepthStencilState& depth)
    {
        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(blend);
        device.setDepthStencilStateProperty(depth);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();
    }

    void DrawFullScreen(ShaderEffect& effect, const Color& base, float z)
    {
        auto& device = getGraphicsDeviceProperty();
        effect.Apply();
        effect.SetUniformVec4(
            "uBase",
            base.getRProperty() / 255.0f,
            base.getGProperty() / 255.0f,
            base.getBProperty() / 255.0f,
            base.getAProperty() / 255.0f);
        const Color unused = Color::White;
        const VertexPositionColor quad[6] = {
            {Vector3(-1.0f,  1.0f, z), unused},
            {Vector3(-1.0f, -1.0f, z), unused},
            {Vector3( 1.0f, -1.0f, z), unused},
            {Vector3(-1.0f,  1.0f, z), unused},
            {Vector3( 1.0f, -1.0f, z), unused},
            {Vector3( 1.0f,  1.0f, z), unused},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
    }

    void Render(const std::vector<RenderTarget2D*>& targets, const Color& base,
                const BlendState& blend = BlendState::Opaque)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTargets(Bindings(targets));
        device.Clear(kClear);
        PrepareDraw(*mrtEffect_, blend, DepthStencilState::None);
        DrawFullScreen(*mrtEffect_, base, 0.0f);
        device.SetRenderTargets({});
    }

    void CheckSlots(const std::vector<RenderTarget2D*>& targets, const Color& base,
                    const std::string& label)
    {
        bool ok = true;
        std::string actual;
        for (std::size_t i = 0; i < targets.size(); ++i)
        {
            const Color got = ReadCenter(*targets[i]);
            ok = ok && Matches(got, OutputFor(base, static_cast<int>(i)));
            actual += (i == 0 ? "" : " ") + std::to_string(i) + "=" + Describe(got);
        }
        Check(ok, label + ": " + actual);
    }

    void TestOneThroughFourAndOrdering()
    {
        for (int count = 1; count <= 4; ++count)
        {
            std::vector<std::unique_ptr<RenderTarget2D>> owned;
            std::vector<RenderTarget2D*> targets;
            for (int i = 0; i < count; ++i)
            {
                owned.push_back(MakeTarget());
                targets.push_back(owned.back().get());
            }
            Render(targets, kBaseA);
            CheckSlots(targets, kBaseA, std::to_string(count) + "-target ordered output");
        }

        auto first = MakeTarget();
        auto second = MakeTarget();
        Render({second.get(), first.get()}, kBaseB);
        const Color firstGot = ReadCenter(*first);
        const Color secondGot = ReadCenter(*second);
        Check(Matches(secondGot, OutputFor(kBaseB, 0))
                  && Matches(firstGot, OutputFor(kBaseB, 1)),
              "ordered binding maps output 0->first slot and output 1->second slot: first="
                  + Describe(firstGot) + " second=" + Describe(secondGot));
    }

    void TestDistinctOutputsAndImmediateConsumption()
    {
        auto target0 = MakeTarget();
        auto target1 = MakeTarget();
        Render({target0.get(), target1.get()}, kBaseA);

        const Color direct0 = ReadCenter(*target0);
        const Color direct1 = ReadCenter(*target1);
        Check(Matches(direct0, OutputFor(kBaseA, 0))
                  && Matches(direct1, OutputFor(kBaseA, 1))
                  && !Matches(direct0, direct1),
              "two-location ShaderEffect writes distinct values in one draw: rt0="
                  + Describe(direct0) + " rt1=" + Describe(direct1));

        const Color sampled0 = SampleCenter(*target0);
        const Color sampled1 = SampleCenter(*target1);
        Check(Matches(sampled0, OutputFor(kBaseA, 0))
                  && Matches(sampled1, OutputFor(kBaseA, 1)),
              "both MRT producers are immediately sampleable after unbind: rt0="
                  + Describe(sampled0) + " rt1=" + Describe(sampled1));
    }

    void TestColorWriteChannels()
    {
        std::array<std::unique_ptr<RenderTarget2D>, 4> owned = {
            MakeTarget(), MakeTarget(), MakeTarget(), MakeTarget()
        };
        std::vector<RenderTarget2D*> targets = {
            owned[0].get(), owned[1].get(), owned[2].get(), owned[3].get()
        };

        BlendState masks = BlendState::Opaque;
        masks.setColorWriteChannelsProperty(ColorWriteChannels::Red);
        masks.setColorWriteChannels1Property(ColorWriteChannels::Green);
        masks.setColorWriteChannels2Property(ColorWriteChannels::Blue);
        masks.setColorWriteChannels3Property(
            ColorWriteChannels::Red | ColorWriteChannels::Blue);
        Render(targets, kBaseA, masks);

        const std::array<Color, 4> expected = {
            Color(kBaseA.getRProperty(), kClear.getGProperty(), kClear.getBProperty(), kClear.getAProperty()),
            Color(kClear.getRProperty(), kBaseA.getBProperty(), kClear.getBProperty(), kClear.getAProperty()),
            Color(kClear.getRProperty(), kClear.getGProperty(), kBaseA.getGProperty(), kClear.getAProperty()),
            Color(kBaseA.getRProperty(), kClear.getGProperty(), kBaseA.getGProperty(), kClear.getAProperty())
        };
        bool ok = true;
        std::string actual;
        for (int i = 0; i < 4; ++i)
        {
            const Color got = ReadCenter(*targets[i]);
            ok = ok && Matches(got, expected[i]);
            actual += (i == 0 ? "" : " ") + std::to_string(i) + "=" + Describe(got);
        }
        Check(ok, "ColorWriteChannels0..3 stay aligned with MRT slots: " + actual);
        getGraphicsDeviceProperty().setBlendStateProperty(BlendState::Opaque);
    }

    void TestDepthOwnership()
    {
        auto depth0 = MakeTarget(DepthFormat::Depth24);
        auto color1 = MakeTarget();
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTargets(Bindings({depth0.get(), color1.get()}));
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kClear, 1.0f, 0);
        PrepareDraw(*mrtEffect_, BlendState::Opaque, DepthStencilState::Default);
        DrawFullScreen(*mrtEffect_, kBaseA, -0.5f);
        DrawFullScreen(*mrtEffect_, kBaseB, 0.5f);
        device.SetRenderTargets({});

        const Color depthGot0 = ReadCenter(*depth0);
        const Color depthGot1 = ReadCenter(*color1);
        Check(Matches(depthGot0, OutputFor(kBaseA, 0))
                  && Matches(depthGot1, OutputFor(kBaseA, 1)),
              "first target owns the shared MRT depth attachment: rt0="
                  + Describe(depthGot0) + " rt1=" + Describe(depthGot1));

        auto noDepth0 = MakeTarget();
        auto unusedDepth1 = MakeTarget(DepthFormat::Depth24);
        device.SetRenderTargets(Bindings({noDepth0.get(), unusedDepth1.get()}));
        device.Clear(kClear);
        PrepareDraw(*mrtEffect_, BlendState::Opaque, DepthStencilState::Default);
        DrawFullScreen(*mrtEffect_, kBaseA, -0.5f);
        DrawFullScreen(*mrtEffect_, kBaseB, 0.5f);
        device.SetRenderTargets({});
        const Color noDepthGot0 = ReadCenter(*noDepth0);
        const Color noDepthGot1 = ReadCenter(*unusedDepth1);
        Check(Matches(noDepthGot0, OutputFor(kBaseB, 0))
                  && Matches(noDepthGot1, OutputFor(kBaseB, 1)),
              "depthless first target does not borrow a later slot's depth buffer: rt0="
                  + Describe(noDepthGot0) + " rt1=" + Describe(noDepthGot1));
    }

    void TestTransitionsAndReplacement()
    {
        auto single = MakeTarget();
        auto a0 = MakeTarget();
        auto a1 = MakeTarget();
        auto b0 = MakeTarget();
        auto b1 = MakeTarget();

        Render({single.get()}, kBaseA);
        Render({a0.get(), a1.get()}, kBaseB);
        Render({single.get()}, kBaseC);
        Check(Matches(ReadCenter(*single), OutputFor(kBaseC, 0))
                  && Matches(ReadCenter(*a0), OutputFor(kBaseB, 0))
                  && Matches(ReadCenter(*a1), OutputFor(kBaseB, 1)),
              "one target -> MRT -> one target preserves every destination");

        Render({a0.get(), a1.get()}, kBaseA);
        Render({b0.get(), b1.get()}, kBaseB);
        Render({a0.get(), a1.get()}, kBaseC);
        Check(Matches(ReadCenter(*a0), OutputFor(kBaseC, 0))
                  && Matches(ReadCenter(*a1), OutputFor(kBaseC, 1))
                  && Matches(ReadCenter(*b0), OutputFor(kBaseB, 0))
                  && Matches(ReadCenter(*b1), OutputFor(kBaseB, 1)),
              "MRT set A -> set B -> set A has no ordered-set framebuffer alias");

        auto stable = MakeTarget();
        auto replaced = MakeTarget();
        Render({stable.get(), replaced.get()}, kBaseA);
        replaced.reset();
        auto replacement = MakeTarget();
        Render({stable.get(), replacement.get()}, kBaseB);
        Check(Matches(ReadCenter(*stable), OutputFor(kBaseB, 0))
                  && Matches(ReadCenter(*replacement), OutputFor(kBaseB, 1)),
              "target-object replacement reattaches the new object without stale framebuffer reuse");
    }

    void TestUsageAndCompatibility()
    {
        auto discard0 = MakeTarget(
            DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        auto discard1 = MakeTarget(
            DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(discard0.get());
        device.Clear(Color::Red);
        device.SetRenderTarget(discard1.get());
        device.Clear(Color::Blue);
        device.SetRenderTargets({});
        device.SetRenderTargets(Bindings({discard0.get(), discard1.get()}));
        device.SetRenderTargets({});
        const Color discardGot0 = ReadCenter(*discard0);
        const Color discardGot1 = ReadCenter(*discard1);
        Check(Matches(discardGot0, Color(0, 0, 0, 255))
                  && Matches(discardGot1, Color(0, 0, 0, 255)),
              "DiscardContents clears every active MRT attachment (old false-positive corrected): rt0="
                  + Describe(discardGot0) + " rt1=" + Describe(discardGot1));

        auto preserve0 = MakeTarget();
        auto preserve1 = MakeTarget();
        Render({preserve0.get(), preserve1.get()}, kBaseA);
        device.SetRenderTargets(Bindings({preserve0.get(), preserve1.get()}));
        device.SetRenderTargets({});
        Check(Matches(ReadCenter(*preserve0), OutputFor(kBaseA, 0))
                  && Matches(ReadCenter(*preserve1), OutputFor(kBaseA, 1)),
              "PreserveContents survives an MRT rebind without an implicit clear");

        auto small = MakeTarget();
        auto wide = MakeTarget(
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents,
            kTargetSize + 1, kTargetSize);
        bool dimensionsThrew = false;
        try
        {
            device.SetRenderTargets(Bindings({small.get(), wide.get()}));
        }
        catch (const std::runtime_error&)
        {
            dimensionsThrew = true;
        }
        Check(dimensionsThrew && device.GetRenderTargets().empty(),
              "incompatible MRT dimensions reject deterministically without changing active state");

        std::vector<std::unique_ptr<RenderTarget2D>> fiveOwned;
        std::vector<RenderTarget2D*> five;
        for (int i = 0; i < 5; ++i)
        {
            fiveOwned.push_back(MakeTarget());
            five.push_back(fiveOwned.back().get());
        }
        bool countThrew = false;
        try
        {
            device.SetRenderTargets(Bindings(five));
        }
        catch (const std::invalid_argument&)
        {
            countThrew = true;
        }
        Check(countThrew && device.GetRenderTargets().empty(),
              "CNA's public four-target ceiling rejects five targets before backend mutation");
    }

    void TestMsaa()
    {
        auto msaa0 = MakeTarget(DepthFormat::None, 4);
        auto msaa1 = MakeTarget(DepthFormat::None, 4);
        const int samples = msaa0->getMultiSampleCountProperty();
        if (samples <= 1 || !msaaEffect_ || !msaaEffect_->IsEffectValid())
        {
            std::printf("[NOTE] true MRT MSAA discriminator unavailable: applied samples=%d, "
                        "ESSL 3.20 effect=%s\n",
                        samples, msaaEffect_ && msaaEffect_->IsEffectValid() ? "valid" : "invalid");
        }
        else
        {
            auto& device = getGraphicsDeviceProperty();
            device.SetRenderTargets(Bindings({msaa0.get(), msaa1.get()}));
            device.Clear(Color(0, 0, 0, 0));
            PrepareDraw(*msaaEffect_, BlendState::Opaque, DepthStencilState::None);
            DrawFullScreen(*msaaEffect_, Color::White, 0.0f);
            device.SetRenderTargets({});

            const Color got0 = ReadCenter(*msaa0);
            const Color got1 = ReadCenter(*msaa1);
            const int oneSample = static_cast<int>(std::lround(255.0 / samples));
            const Color expected0(oneSample, 0, 0, oneSample);
            const Color expected1(0, oneSample, 0, oneSample);
            Check(Matches(got0, expected0, 8) && Matches(got1, expected1, 8),
                  "MRT uses real " + std::to_string(samples)
                      + "x attachments and resolves every slot: rt0=" + Describe(got0)
                      + " rt1=" + Describe(got1));
        }

        auto plain = MakeTarget();
        bool samplesThrew = false;
        try
        {
            getGraphicsDeviceProperty().SetRenderTargets(
                Bindings({msaa0.get(), plain.get()}));
        }
        catch (const std::runtime_error&)
        {
            samplesThrew = true;
        }
        Check(samplesThrew && getGraphicsDeviceProperty().GetRenderTargets().empty(),
              "mixed applied sample counts reject deterministically without changing active state");
    }

    void CheckNativeGlErrors()
    {
        using GetError = unsigned int (*)();
        const auto getError = reinterpret_cast<GetError>(SDL_GL_GetProcAddress("glGetError"));
        std::vector<unsigned int> errors;
        if (getError)
        {
            for (int i = 0; i < 64; ++i)
            {
                const unsigned int error = getError();
                if (error == 0) break;
                errors.push_back(error);
            }
        }
        for (unsigned int error : errors)
            std::printf("[GL-ERROR] 0x%04X\n", error);
        Check(getError != nullptr && errors.empty(),
              "complete native GL error drain reports no EasyGL MRT errors");
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        mrtEffect_ = std::make_unique<ShaderEffect>(
            device, kMrtVertexShader, kMrtFragmentShader);
        msaaEffect_ = std::make_unique<ShaderEffect>(
            device, kMsaaVertexShader, kMsaaFragmentShader);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        if (!mrtEffect_ || !mrtEffect_->IsEffectValid())
        {
            Check(false, "four-output ESSL 3.00 MRT effect compiles");
        }
        else
        {
            TestDistinctOutputsAndImmediateConsumption();
            TestOneThroughFourAndOrdering();
            TestColorWriteChannels();
            TestDepthOwnership();
            TestTransitionsAndReplacement();
            TestUsageAndCompatibility();
            TestMsaa();
        }
        CheckNativeGlErrors();

        std::printf("=== %d/%d PASS ===\n", pass_, total_);
        result_ = pass_ == total_ ? 0 : 1;
        Exit();
    }

public:
    EasyGlMrtTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(96);
        graphics_->setPreferredBackBufferHeightProperty(72);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGlMrtTest game;
    game.Run();
    return game.getResult();
}
