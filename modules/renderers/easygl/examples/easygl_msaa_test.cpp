// SPDX-License-Identifier: MS-PL
// Renderer-neutral back-buffer MSAA contract (plans/plan_dx.md DX-219).
//
// A solid full-screen quad cannot distinguish multisampling from single-sample rendering. This
// fixture instead draws a flat white triangle over black and requires partially covered pixels on
// its diagonal edge after the default render surface is resolved into the public back buffer.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BackBufferMsaaTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int failures_ = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        PresentationParameters parameters =
            device.getPresentationParametersProperty().Clone();
        parameters.setMultiSampleCountProperty(4);
        device.Reset(parameters);

        const int applied =
            device.getPresentationParametersProperty().getMultiSampleCountProperty();
        Check(applied == 4,
              "requesting four samples reports four actually applied samples");

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.Clear(Color::Black);

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setTextureEnabledProperty(false);
        effect.VertexColorEnabled = true;
        effect.Apply();

        const VertexPositionColor triangle[3] = {
            {Vector3(-0.85f, -0.75f, 0.5f), Color::White},
            {Vector3( 0.85f, -0.75f, 0.5f), Color::White},
            {Vector3(-0.85f,  0.75f, 0.5f), Color::White},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle, 0, 1);

        const auto& viewport = device.getViewportProperty();
        const int width = viewport.getWidthProperty();
        const int height = viewport.getHeightProperty();
        std::vector<Color> pixels(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        device.GetBackBufferData(
            pixels.data(), 0, static_cast<int>(pixels.size()));

        int blackPixels = 0;
        int whitePixels = 0;
        int intermediatePixels = 0;
        for (const Color& pixel : pixels)
        {
            const int red = pixel.getRProperty();
            if (red == 0) ++blackPixels;
            else if (red == 255) ++whitePixels;
            else ++intermediatePixels;
        }

        Check(blackPixels > 100 && whitePixels > 100,
              "triangle produces deterministic inside and outside control regions");
        Check(intermediatePixels > 0,
              "resolved diagonal edge contains partially covered pixels");
        std::printf("Applied samples=%d, black=%d, white=%d, intermediate=%d\n",
                    applied, blackPixels, whitePixels, intermediatePixels);

        Exit();
    }

public:
    BackBufferMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
    }

    [[nodiscard]] int GetResult() const { return failures_ == 0 ? 0 : 1; }
};

int main()
{
    BackBufferMsaaTest game;
    game.Run();
    return game.GetResult();
}
