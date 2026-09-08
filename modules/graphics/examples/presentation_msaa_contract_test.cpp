// SPDX-License-Identifier: MS-PL
// SOFTWARE-127: renderer-neutral applied backbuffer-MSAA reset and storage contract.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    int expectedSamples = 0;

    bool Near(int actual, int expected, int tolerance = 3)
    {
        const int difference = actual > expected ? actual - expected : expected - actual;
        return difference <= tolerance;
    }
}

class PresentationMsaaContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
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

    void DrawFullScreen(GraphicsDevice& device, const Color& color)
    {
        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setLightingEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();

        const VertexPositionColor vertices[6] = {
            {Vector3(-1.0f,  1.0f, 0.0f), color},
            {Vector3(-1.0f, -1.0f, 0.0f), color},
            {Vector3( 1.0f, -1.0f, 0.0f), color},
            {Vector3(-1.0f,  1.0f, 0.0f), color},
            {Vector3( 1.0f, -1.0f, 0.0f), color},
            {Vector3( 1.0f,  1.0f, 0.0f), color},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    Color ReadCenter(GraphicsDevice& device)
    {
        const auto& viewport = device.getViewportProperty();
        const Rectangle region(
            viewport.getWidthProperty() / 2, viewport.getHeightProperty() / 2, 1, 1);
        Color pixel;
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = device.GetRenderer();
        const auto& parameters = device.getPresentationParametersProperty();

        Check(parameters.getMultiSampleCountProperty() == expectedSamples,
              "GraphicsDevice stores the renderer-applied sample count");
        Check(renderer.GetMultiSampleCount() == expectedSamples,
              "renderer reports the same applied backbuffer sample count");

        RasterizerState rasterizer;
        rasterizer.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rasterizer);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        BlendState firstSampleOnly;
        firstSampleOnly.setMultiSampleMaskProperty(1);
        device.setBlendStateProperty(firstSampleOnly);
        device.Clear(Color::Black);
        DrawFullScreen(device, Color::Red);
        const Color enabledPixel = ReadCenter(device);
        const int expectedRed = expectedSamples == 4 ? 63 : 255;
        Check(Near(enabledPixel.getRProperty(), expectedRed) &&
                  enabledPixel.getGProperty() == 0 && enabledPixel.getBProperty() == 0,
              "MultiSampleMask observes the actual backbuffer sample storage");

        graphics_->setPreferMultiSamplingProperty(false);
        graphics_->ApplyChanges();
        Check(device.getPresentationParametersProperty().getMultiSampleCountProperty() == 0 &&
                  renderer.GetMultiSampleCount() == 0,
              "disabling PreferMultiSampling releases backbuffer sample storage");

        device.setBlendStateProperty(BlendState::Opaque);
        device.Clear(Color::Black);
        DrawFullScreen(device, Color::Red);
        const Color disabledPixel = ReadCenter(device);
        Check(disabledPixel.getRProperty() == 255 && disabledPixel.getGProperty() == 0 &&
                  disabledPixel.getBProperty() == 0,
              "single-sample backbuffer draws normally after sample storage is released");

        graphics_->setPreferMultiSamplingProperty(true);
        graphics_->ApplyChanges();
        Check(device.getPresentationParametersProperty().getMultiSampleCountProperty()
                  == expectedSamples &&
                  renderer.GetMultiSampleCount() == expectedSamples,
              "re-enabling PreferMultiSampling restores backbuffer sample storage");

        device.setBlendStateProperty(firstSampleOnly);
        device.Clear(Color::Black);
        DrawFullScreen(device, Color::Red);
        const Color reenabledPixel = ReadCenter(device);
        Check(Near(reenabledPixel.getRProperty(), expectedRed) &&
                  reenabledPixel.getGProperty() == 0 && reenabledPixel.getBProperty() == 0,
              "re-enabled backbuffer exposes independent sample-mask output again");

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    PresentationMsaaContractTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(64);
        graphics_->setPreferMultiSamplingProperty(true);
    }

    int Result() const { return result_; }
};

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: presentation_msaa_contract_test <expected-samples>\n");
        return 2;
    }
    expectedSamples = std::atoi(argv[1]);
    PresentationMsaaContractTest game;
    game.Run();
    return game.Result();
}
