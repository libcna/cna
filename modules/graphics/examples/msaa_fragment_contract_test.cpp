// SPDX-License-Identifier: MS-PL
// SOFTWARE-110/SOFTWARE-160: renderer-neutral 4x coverage, mask, depth, stencil and
// RasterizerState.MultiSampleAntiAlias contract.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kTargetSize = 8;
    const Color kBlack(0, 0, 0, 255);
    const Color kFullGreen(0, 255, 0, 255);

#if defined(CNA_RENDERER_EASYGL) && !defined(CNA_GL_PROFILE_OPENGL33)
    constexpr bool kCanDisableMultisampleRasterization = false;
#else
    constexpr bool kCanDisableMultisampleRasterization = true;
#endif

    std::string Text(const Color& color)
    {
        return "(" + std::to_string(static_cast<int>(color.getRProperty())) + "," +
               std::to_string(static_cast<int>(color.getGProperty())) + "," +
               std::to_string(static_cast<int>(color.getBProperty())) + "," +
               std::to_string(static_cast<int>(color.getAProperty())) + ")";
    }

    bool NearByte(int actual, int expected, int tolerance = 3)
    {
        const int difference = actual > expected ? actual - expected : expected - actual;
        return difference <= tolerance;
    }

    BlendState SampleMask(unsigned int mask)
    {
        BlendState state;
        state.setMultiSampleMaskProperty(static_cast<int>(mask));
        return state;
    }
}

class MsaaFragmentContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<RenderTarget2D> target_;
    std::unique_ptr<BasicEffect> effect_;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        ++total_;
        if (condition)
            ++passed_;
    }

    void DrawQuad(GraphicsDevice& device, const Color& color, float depth)
    {
        const VertexPositionColor vertices[6] = {
            {Vector3(-1.0f,  1.0f, depth), color},
            {Vector3(-1.0f, -1.0f, depth), color},
            {Vector3( 1.0f, -1.0f, depth), color},
            {Vector3(-1.0f,  1.0f, depth), color},
            {Vector3( 1.0f, -1.0f, depth), color},
            {Vector3( 1.0f,  1.0f, depth), color},
        };
        effect_->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    void DrawHalfTriangle(GraphicsDevice& device, const Color& color, float depth)
    {
        const VertexPositionColor vertices[3] = {
            {Vector3(-1.0f,  1.0f, depth), color},
            {Vector3(-1.0f, -1.0f, depth), color},
            {Vector3( 1.0f,  1.0f, depth), color},
        };
        effect_->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 1);
    }

    Color FinishAndRead(GraphicsDevice& device, int x = kTargetSize / 2,
                        int y = kTargetSize / 2)
    {
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color pixel;
        const Rectangle center(x, y, 1, 1);
        target_->GetData(0, &center, &pixel, 0, 1);
        return pixel;
    }

    std::array<Color, kTargetSize * kTargetSize> FinishAndReadAll(GraphicsDevice& device)
    {
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::array<Color, kTargetSize * kTargetSize> pixels{};
        target_->GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels;
    }

    void Begin(GraphicsDevice& device, float depth = 1.0f, int stencil = 0)
    {
        device.SetRenderTarget(target_.get());
        device.setViewportProperty(Viewport(0, 0, kTargetSize, kTargetSize));
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     kBlack, depth, stencil);
    }

    void CheckColorSampleMasks(GraphicsDevice& device)
    {
        for (int sample = 0; sample < 4; ++sample)
        {
            Begin(device);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(SampleMask(1u << sample));
            DrawQuad(device, Color::Red, 0.5f);
            const Color result = FinishAndRead(device);
            Check(NearByte(result.getRProperty(), 63) && result.getGProperty() == 0 &&
                      result.getBProperty() == 0 && result.getAProperty() == 255,
                  "MultiSampleMask bit " + std::to_string(sample) +
                      " writes exactly one quarter of a full-coverage pixel: " + Text(result));
        }
    }

    void CheckIndependentDepthSamples(GraphicsDevice& device)
    {
        Begin(device);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setBlendStateProperty(SampleMask(0x1u));
        DrawQuad(device, kFullGreen, 0.2f);
        device.setBlendStateProperty(SampleMask(0xFu));
        DrawQuad(device, Color::Red, 0.8f);
        const Color result = FinishAndRead(device);
        Check(NearByte(result.getRProperty(), 191) && NearByte(result.getGProperty(), 63) &&
                  result.getBProperty() == 0 && result.getAProperty() == 255,
              "a near write to sample 0 does not depth-reject farther writes to samples 1..3: " +
                  Text(result));
    }

    void CheckDepthAtCoveredSamples(GraphicsDevice& device)
    {
        Begin(device);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setBlendStateProperty(SampleMask(0xFu));
        DrawHalfTriangle(device, kFullGreen, 0.2f);
        DrawQuad(device, Color::Red, 0.8f);
        const auto pixels = FinishAndReadAll(device);
        bool mixedNearFar = false;
        Color witness;
        for (int y = 1; y < kTargetSize - 1 && !mixedNearFar; ++y)
        {
            for (int x = 1; x < kTargetSize - 1; ++x)
            {
                const Color& pixel = pixels[static_cast<std::size_t>(y * kTargetSize + x)];
                if (pixel.getRProperty() > 8 && pixel.getGProperty() > 8)
                {
                    mixedNearFar = true;
                    witness = pixel;
                    break;
                }
            }
        }
        Check(mixedNearFar,
              "an edge pixel retains near-covered samples and accepts the farther quad in its "
              "uncovered samples: " + Text(witness));
    }

    void CheckIndependentStencilSamples(GraphicsDevice& device)
    {
        Begin(device);
        DepthStencilState stamp;
        stamp.setDepthBufferEnableProperty(false);
        stamp.setDepthBufferWriteEnableProperty(false);
        stamp.setStencilEnableProperty(true);
        stamp.setStencilFunctionProperty(CompareFunction::Always);
        stamp.setStencilPassProperty(StencilOperation::Replace);
        stamp.setReferenceStencilProperty(1);
        device.setDepthStencilStateProperty(stamp);
        device.setBlendStateProperty(SampleMask(0x1u));
        DrawQuad(device, kBlack, 0.5f);

        DepthStencilState select;
        select.setDepthBufferEnableProperty(false);
        select.setDepthBufferWriteEnableProperty(false);
        select.setStencilEnableProperty(true);
        select.setStencilFunctionProperty(CompareFunction::Equal);
        select.setReferenceStencilProperty(0);
        device.setDepthStencilStateProperty(select);
        device.setBlendStateProperty(SampleMask(0xFu));
        DrawQuad(device, Color::Blue, 0.5f);
        const Color result = FinishAndRead(device);
        Check(result.getRProperty() == 0 && result.getGProperty() == 0 &&
                  NearByte(result.getBProperty(), 191) && result.getAProperty() == 255,
              "stencil written in sample 0 does not reject samples 1..3: " + Text(result));
    }

    void CheckMultiSampleAntiAliasToggle(GraphicsDevice& device)
    {
        const auto render = [&](bool enabled) {
            Begin(device);
            RasterizerState rasterizer;
            rasterizer.setCullModeProperty(CullMode::None);
            rasterizer.setMultiSampleAntiAliasProperty(enabled);
            device.setRasterizerStateProperty(rasterizer);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            DrawHalfTriangle(device, kFullGreen, 0.5f);
            return FinishAndReadAll(device);
        };

        const auto independentlySampled = render(true);
        int partialWithMsaa = 0;
        for (const Color& pixel : independentlySampled)
            if (pixel.getGProperty() > 0 && pixel.getGProperty() < 255)
                ++partialWithMsaa;
        Check(partialWithMsaa > 0,
              "MultiSampleAntiAlias=true independently covers boundary samples (partial pixels=" +
                  std::to_string(partialWithMsaa) + ")");

        if constexpr (kCanDisableMultisampleRasterization)
        {
            const auto replicatedCenter = render(false);
            int partialWithoutMsaa = 0;
            for (const Color& pixel : replicatedCenter)
                if (pixel.getGProperty() > 0 && pixel.getGProperty() < 255)
                    ++partialWithoutMsaa;
            Check(partialWithoutMsaa == 0,
                  "MultiSampleAntiAlias=false evaluates one pixel center and replicates its result "
                  "to every sample (partial pixels=" + std::to_string(partialWithoutMsaa) + ")");
            Check(independentlySampled != replicatedCenter,
                  "MultiSampleAntiAlias true and false produce observably different boundary coverage");
            Check(render(true) == independentlySampled,
                  "MultiSampleAntiAlias true/false/true transition restores independent coverage");
        }
        else
        {
            std::printf("[NOTE] MultiSampleAntiAlias=false is unrepresentable on this OpenGL ES "
                        "profile; desktop GL and Software execute the toggle checks\n");
        }
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        target_ = std::make_unique<RenderTarget2D>(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::Depth24Stencil8, 4, RenderTargetUsage::PreserveContents);
        Check(target_->getMultiSampleCountProperty() == 4,
              "requested 4x RenderTarget2D reports four applied samples");

        RasterizerState rasterizer;
        rasterizer.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rasterizer);
        effect_ = std::make_unique<BasicEffect>(device);
        effect_->VertexColorEnabled = true;
        effect_->setLightingEnabledProperty(false);
        effect_->setWorldProperty(Matrix::getIdentityProperty());
        effect_->setViewProperty(Matrix::getIdentityProperty());
        effect_->setProjectionProperty(Matrix::getIdentityProperty());

        CheckColorSampleMasks(device);
        CheckIndependentDepthSamples(device);
        CheckDepthAtCoveredSamples(device);
        CheckIndependentStencilSamples(device);
        CheckMultiSampleAntiAliasToggle(device);

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    MsaaFragmentContractTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(64);
    }

    int Result() const { return result_; }
};

int main()
{
    MsaaFragmentContractTest game;
    game.Run();
    return game.Result();
}
