// SPDX-License-Identifier: MS-PL
// Renderer-neutral runtime back-buffer MSAA contract (plans/plan_dx.md DX-219).
//
// The fixture alternates single-sample and four-sample rendering through GraphicsDevice::Reset,
// then compares the resolved diagonal edge. It also proves that an unreasonable request is
// reported as a real device-clamped value rather than echoed back unchanged.

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

class MsaaChangeTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int failures_ = 0;
    bool done_ = false;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures_;
    }

    int ApplyCount(GraphicsDevice& device, int requested)
    {
        PresentationParameters parameters =
            device.getPresentationParametersProperty().Clone();
        parameters.setMultiSampleCountProperty(requested);
        device.Reset(parameters);
        return device.getPresentationParametersProperty().getMultiSampleCountProperty();
    }

    int RenderDiagonal(GraphicsDevice& device)
    {
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
        Check(blackPixels > 50 && whitePixels > 50,
              "each reset leg preserves inside/outside control pixels");
        return intermediatePixels;
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
        Check(device.getPresentationParametersProperty().getMultiSampleCountProperty() == 0,
              "default device starts single-sampled");

        const int initialSingleIntermediate = RenderDiagonal(device);
        Check(initialSingleIntermediate == 0,
              "single-sample baseline has no partially covered pixels");

        const int firstFourCount = ApplyCount(device, 4);
        const int firstFourIntermediate = RenderDiagonal(device);
        Check(firstFourCount == 4,
              "Reset from zero to four reports four applied samples");
        Check(firstFourIntermediate > 0,
              "Reset from zero to four enables multisample edge coverage");

        const int disabledCount = ApplyCount(device, 0);
        const int disabledIntermediate = RenderDiagonal(device);
        Check(disabledCount == 0,
              "Reset from four to zero reports multisampling disabled");
        Check(disabledIntermediate == 0,
              "Reset from four to zero removes multisample edge coverage");

        const int unreasonableCount = ApplyCount(device, 1024);
        Check(unreasonableCount >= 0 && unreasonableCount < 1024 &&
                  unreasonableCount != 1,
              "unsupported request is reported as a legal device-clamped count");

        const int secondFourCount = ApplyCount(device, 4);
        const int secondFourIntermediate = RenderDiagonal(device);
        Check(secondFourCount == 4 && secondFourIntermediate > 0,
              "four-sample behavior survives disable and rejected-request legs without leakage");

        std::printf(
            "Intermediate pixels: initial=%d, first4x=%d, disabled=%d, second4x=%d; clamp=%d\n",
            initialSingleIntermediate, firstFourIntermediate, disabledIntermediate,
            secondFourIntermediate, unreasonableCount);
        Exit();
    }

public:
    MsaaChangeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(160);
        gdm_->setPreferredBackBufferHeightProperty(120);
    }

    [[nodiscard]] int GetResult() const { return failures_ == 0 ? 0 : 1; }
};

int main()
{
    MsaaChangeTest game;
    game.Run();
    return game.GetResult();
}
