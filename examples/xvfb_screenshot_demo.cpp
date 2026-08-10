// SPDX-License-Identifier: MS-PL
// Ad hoc diagnostic (deliberately untracked and repository-level, not owned by any module):
// draws the same SpriteBatch rotate-around-origin scene as
// modules/renderers/easygl/examples/easygl_spritebatch_rotation_golden_test.cpp
// (Task 465/417) -- a 100x100 texture (top-left 20x20 = Red marker, rest = Blue) drawn rotated
// 90 degrees around its own bottom-right corner -- and saves the resulting backbuffer to a real
// PNG file on disk, proving a real screenshot can be produced under Xvfb. 2D-only renderers can't
// run the SOFTWARE-renderer screenshot demo's raw-VertexBuffer 3D scene (SDL_Renderer/CANVAS/ASCII
// all throw on CreateVertexBuffer), so this demo uses SpriteBatch instead.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

// Shared example-support header, owned by the graphics module's examples directory and
// reached through CNA_GRAPHICS_EXAMPLES_DIR (the registering module's include path).
#include "common/ScreenshotEXT.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kClear(0, 255, 0, 255);
}

class XvfbScreenshotDemo : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);

        Texture2D tex(dev, 100, 100);
        std::vector<Color> pixels(100 * 100, kBlue);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 20; ++x)
                pixels[y * 100 + x] = kRed;
        tex.SetData(pixels.data(), static_cast<int>(pixels.size()));

        dev.Clear(kClear);
        dev.setBlendStateProperty(BlendState::Opaque);

        sb.Begin(SpriteSortMode::Deferred,
                 BlendState::Opaque,
                 const_cast<SamplerState*>(&SamplerState::PointClamp),
                 nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

        sb.Draw(tex,
                Rectangle(200, 150, 100, 100),
                Rectangle(0, 0, 100, 100),
                Color::White,
                MathHelper::PiOver2,
                Vector2(100.0f, 100.0f),
                SpriteEffects::None,
                0.0f);

        sb.End();

        const char* outPath = std::getenv("CNA_SCREENSHOT_OUT");
        SaveBackBufferScreenshotEXT(dev, outPath ? outPath : "/tmp/cna_xvfb_screenshot.png");
        std::printf("[DONE] screenshot written\n");

        Exit();
    }

public:
    XvfbScreenshotDemo()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->setPreferredBackBufferHeightProperty(300);
    }
};

int main()
{
    XvfbScreenshotDemo game;
    game.Run();
    return 0;
}
