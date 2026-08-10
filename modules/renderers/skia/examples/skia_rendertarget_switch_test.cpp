// SPDX-License-Identifier: MS-PL
// Public SKIA-62 target-switch contract: A -> B -> backbuffer keeps target pixels independent.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);

    [[nodiscard]] bool Same(Color left, Color right)
    {
        return left.getRProperty() == right.getRProperty()
            && left.getGProperty() == right.getGProperty()
            && left.getBProperty() == right.getBProperty();
    }
}

class SkiaRenderTargetSwitchTest final : public Game
{
protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D targetA(device, 4, 4, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::PreserveContents);
        RenderTarget2D targetB(device, 4, 4, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&targetA);
        device.Clear(kRed);
        device.SetRenderTarget(&targetB);
        device.Clear(kBlue);
        device.SetRenderTarget(nullptr);

        device.Clear(Color(0, 0, 0, 255));
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
        spriteBatch_->Draw(targetA, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->Draw(targetB, Rectangle(8, 0, 8, 8), Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->End();

        CheckPixel(4, 4, kRed, "target A stays Red after target B was bound and rendered");
        CheckPixel(12, 4, kBlue, "target B remains independently Blue after restoring the backbuffer");
        Exit();
    }

public:
    SkiaRenderTargetSwitchTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(16);
        graphics_->setPreferredBackBufferHeightProperty(8);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

private:
    void CheckPixel(int x, int y, Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle sample(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&sample, &actual, 0, 1);
        const bool pass = Same(actual, expected);
        std::printf("[%s] %s: got=(%d,%d,%d)\n", pass ? "PASS" : "FAIL", label,
                    actual.getRProperty(), actual.getGProperty(), actual.getBProperty());
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaRenderTargetSwitchTest game;
    game.Run();
    return game.Result();
}
