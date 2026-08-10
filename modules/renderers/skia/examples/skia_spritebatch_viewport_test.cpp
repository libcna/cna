// SPDX-License-Identifier: MS-PL
// Pixel contract for SKIA-41: SpriteBatch coordinates are local to a top-left Viewport.

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
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
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

class SkiaSpriteBatchViewportTest final : public Game
{
protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        texel_ = std::make_unique<Texture2D>(device, 1, 1);
        texel_->SetData(&kRed, 1);
        target_ = std::make_unique<RenderTarget2D>(device, 27, 19, false, SurfaceFormat::Color,
                                                   DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();

        // Backbuffer: the local (0,0)-(16,12) sprite must occupy only the offset viewport.
        device.Clear(kBlack);
        device.setViewportProperty(Viewport(9, 6, 16, 12));
        DrawViewportFill(16, 12);
        CheckBackbuffer(10, 7, kRed, "backbuffer viewport top-left uses a top-left positive Y offset");
        CheckBackbuffer(23, 16, kRed, "backbuffer viewport interior is filled from local coordinates");
        CheckBackbuffer(8, 7, kBlack, "backbuffer viewport clips the left exterior");
        CheckBackbuffer(10, 5, kBlack, "backbuffer viewport clips the top exterior");
        CheckBackbuffer(25, 7, kBlack, "backbuffer viewport clips the right exterior");
        CheckBackbuffer(10, 19, kBlack, "backbuffer viewport clips the bottom exterior");

        // Render target: binding resets viewport state; set a new asymmetric viewport and verify
        // exactly the same top-left coordinate convention via target readback after unbinding.
        device.SetRenderTarget(target_.get());
        device.Clear(kBlack);
        device.setViewportProperty(Viewport(5, 3, 11, 8));
        DrawViewportFill(11, 8);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        CheckTarget(6, 4, kRed, "render target viewport top-left matches backbuffer orientation");
        CheckTarget(14, 9, kRed, "render target viewport local SpriteBatch coordinates fill its interior");
        CheckTarget(4, 4, kBlack, "render target viewport clips the left exterior");
        CheckTarget(6, 2, kBlack, "render target viewport clips the top exterior");
        CheckTarget(16, 4, kBlack, "render target viewport clips the right exterior");
        CheckTarget(6, 11, kBlack, "render target viewport clips the bottom exterior");
        Exit();
    }

public:
    SkiaSpriteBatchViewportTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(40);
        graphics_->setPreferredBackBufferHeightProperty(28);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

private:
    void DrawViewportFill(int width, int height)
    {
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr);
        spriteBatch_->Draw(*texel_, Rectangle(0, 0, width, height), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
    }

    void CheckBackbuffer(int x, int y, Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&region, &actual, 0, 1);
        Check(Same(actual, expected), actual, label);
    }

    void CheckTarget(int x, int y, Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        target_->GetData(0, &region, &actual, 0, 1);
        Check(Same(actual, expected), actual, label);
    }

    void Check(bool pass, Color actual, const char* label)
    {
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
    SkiaSpriteBatchViewportTest game;
    game.Run();
    return game.Result();
}
