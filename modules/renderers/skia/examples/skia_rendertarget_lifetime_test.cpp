// SPDX-License-Identifier: MS-PL
// SKIA-69: public rendering regression for a RenderTarget2D whose C++ destructor runs while the
// target is selected.  Explicit Dispose remains guarded by the shared XNA contract; this covers
// the destructor-only path and proves the next raster work lands on the backbuffer, not freed RAM.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SkiaRenderTargetLifetimeTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> whiteTexture_;
    int failures_ = 0;
    bool finished_ = false;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures_;
    }

protected:
    void Initialize() override
    {
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        whiteTexture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
    }

    void Draw(const GameTime&) override
    {
        if (finished_) return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        {
            RenderTarget2D target(device, 4, 4);
            device.SetRenderTarget(&target);
            device.Clear(Color(220, 30, 40, 255));
        } // The target is gone while the renderer still considers it the active destination.

        device.Clear(Color(20, 70, 180, 255));
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        spriteBatch_->Draw(*whiteTexture_, Rectangle(1, 1, 1, 1), Color::White);
        spriteBatch_->End();
        device.SetRenderTarget(nullptr);

        Check(device.GetRenderTargets().empty(),
              "explicit unbind clears the public target list after a destroyed bound target");
        Color pixels[2]{Color(0, 0, 0, 0), Color(0, 0, 0, 0)};
        const Rectangle topLeft(0, 0, 1, 1);
        device.GetBackBufferData(&topLeft, pixels, 0, 1);
        Check(pixels[0] == Color(20, 70, 180, 255),
              "post-destruction Clear writes the raster backbuffer");
        const Rectangle spritePixel(1, 1, 1, 1);
        device.GetBackBufferData(&spritePixel, pixels, 0, 1);
        Check(pixels[0] == Color::White,
              "post-destruction SpriteBatch draw writes the raster backbuffer");

        RenderTarget2D freshTarget(device, 4, 4);
        device.SetRenderTarget(&freshTarget);
        device.Clear(Color(80, 190, 60, 255));
        device.SetRenderTarget(nullptr);
        Check(device.GetRenderTargets().empty(),
              "a fresh target bind cycle remains usable after target destruction");
        Exit();
    }

public:
    SkiaRenderTargetLifetimeTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(8);
        graphics_->setPreferredBackBufferHeightProperty(8);
    }

    [[nodiscard]] int Result() const { return failures_ == 0 ? 0 : 1; }
};

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SkiaRenderTargetLifetimeTest game;
    game.Run();
    return game.Result();
}
