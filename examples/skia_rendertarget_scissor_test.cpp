// SPDX-License-Identifier: MS-PL
// Pixel contract for SKIA-42: ScissorRectangle clips SpriteBatch on an off-screen SkSurface.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kTargetWidth = 24;
    constexpr int kTargetHeight = 20;
    const Rectangle kScissor(6, 4, 10, 8);
    const Color kBlack(0, 0, 0, 255);
    const Color kRed(255, 0, 0, 255);

    [[nodiscard]] bool Same(Color left, Color right)
    {
        return left.getRProperty() == right.getRProperty()
            && left.getGProperty() == right.getGProperty()
            && left.getBProperty() == right.getBProperty()
            && left.getAProperty() == right.getAProperty();
    }
}

class SkiaRenderTargetScissorTest final : public Game
{
protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        texel_ = std::make_unique<Texture2D>(device, 1, 1);
        texel_->SetData(&kRed, 1);
        target_ = std::make_unique<RenderTarget2D>(device, kTargetWidth, kTargetHeight, false,
                                                   SurfaceFormat::Color, DepthFormat::None, 0,
                                                   RenderTargetUsage::PreserveContents);
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        RenderIntoTarget(true);
        CheckPixel(10, 8, kRed, "enabled scissor renders inside its off-screen target rectangle");
        CheckPixel(3, 8, kBlack, "enabled scissor clips left side in target coordinates");
        CheckPixel(19, 8, kBlack, "enabled scissor clips right side in target coordinates");
        CheckPixel(10, 2, kBlack, "enabled scissor clips top side in target coordinates");
        CheckPixel(10, 14, kBlack, "enabled scissor clips bottom side in target coordinates");

        // Binding the target resets the public scissor rectangle; write it again with the test
        // disabled to prove that the stored rectangle itself does not impose a clip.
        RenderIntoTarget(false);
        CheckPixel(3, 8, kRed, "disabled scissor permits the same target pixel outside the rectangle");
        CheckPixel(19, 8, kRed, "disabled scissor permits the opposite target exterior pixel");
        Exit();
    }

public:
    SkiaRenderTargetScissorTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(40);
        graphics_->setPreferredBackBufferHeightProperty(32);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

private:
    void RenderIntoTarget(bool scissorEnabled)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(target_.get());
        device.Clear(kBlack);
        device.setScissorRectangleProperty(kScissor);
        RasterizerState rasterizer = RasterizerState::CullNone;
        rasterizer.setScissorTestEnableProperty(scissorEnabled);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, &rasterizer);
        spriteBatch_->Draw(*texel_, Rectangle(0, 0, kTargetWidth, kTargetHeight),
                          Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    void CheckPixel(int x, int y, Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        target_->GetData(0, &region, &actual, 0, 1);
        const bool pass = Same(actual, expected);
        std::printf("[%s] %s: got=(%d,%d,%d)\n", pass ? "PASS" : "FAIL", label,
                    actual.getRProperty(), actual.getGProperty(), actual.getBProperty());
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texel_;
    std::unique_ptr<RenderTarget2D> target_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaRenderTargetScissorTest game;
    game.Run();
    return game.Result();
}
