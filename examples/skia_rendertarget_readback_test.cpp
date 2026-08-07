// SPDX-License-Identifier: MS-PL
// Public SKIA-64 readback contract for a rendered target: full rows and a partial rectangle.

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
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBlue(0, 0, 255, 255);
    const Color kRed(255, 0, 0, 255);

    [[nodiscard]] bool Same(Color left, Color right)
    {
        return left.getRProperty() == right.getRProperty()
            && left.getGProperty() == right.getGProperty()
            && left.getBProperty() == right.getBProperty()
            && left.getAProperty() == right.getAProperty();
    }
}

class SkiaRenderTargetReadbackTest final : public Game
{
protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color white = Color::White;
        white_->SetData(&white, 1);
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, 4, 3, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&target);
        device.Clear(kBlue);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
        spriteBatch_->Draw(*white_, Rectangle(1, 1, 2, 1), Rectangle(0, 0, 1, 1), kRed);
        spriteBatch_->End();
        device.SetRenderTarget(nullptr);

        std::vector<Color> full(12, Color(0, 0, 0, 0));
        target.GetData(full.data(), 0, static_cast<int>(full.size()));
        Check(Same(full[0], kBlue) && Same(full[4], kBlue) && Same(full[5], kRed)
                  && Same(full[6], kRed) && Same(full[7], kBlue) && Same(full[11], kBlue),
              "full RenderTarget2D GetData is top-row-first and preserves rendered pixels");

        const Rectangle marker(1, 1, 2, 1);
        std::vector<Color> partial(2, Color(0, 0, 0, 0));
        target.GetData(0, &marker, partial.data(), 0, static_cast<int>(partial.size()));
        Check(Same(partial[0], kRed) && Same(partial[1], kRed),
              "partial RenderTarget2D GetData reads only the requested rectangle in row order");
        Exit();
    }

public:
    SkiaRenderTargetReadbackTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(8);
        graphics_->setPreferredBackBufferHeightProperty(8);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

private:
    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> white_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaRenderTargetReadbackTest game;
    game.Run();
    return game.Result();
}
