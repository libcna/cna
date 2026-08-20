// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: adapts examples/easygl_spritebatch_sourcerect_test.cpp's own scene verbatim --
// verifies SpriteBatch::Draw's sourceRectangle parameter correctly crops which region of the
// texture is sampled, rather than always sampling (and stretching) the whole texture.
//
// A 20x20 texture split into a 2x2 grid of 10x10 solid-color cells (Red/Blue/Magenta/Yellow),
// drawn with sourceRectangle=(10,0,10,10) -- the top-right (Blue) cell only -- stretched into a
// 50x50 destinationRectangle. If cropping works correctly, the ENTIRE drawn sprite must be
// uniformly Blue; sampling near either corner would show the WRONG cell colour if
// sourceRectangle were ignored (two independent, differently-colored tells for the same bug).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed     (255,   0,   0, 255);
    const Color kBlue    (  0,   0, 255, 255);
    const Color kMagenta (255,   0, 255, 255);
    const Color kYellow  (255, 255,   0, 255);
    const Color kGreen   (  0, 255,   0, 255);
}

class OpenGL2SpriteBatchSourceRectTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> tex_;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        tex_ = std::make_unique<Texture2D>(dev, 20, 20);
        std::vector<Color> pixels(20 * 20, kRed);
        for (int y = 0; y < 20; ++y)
        {
            for (int x = 0; x < 20; ++x)
            {
                const bool right  = x >= 10;
                const bool bottom = y >= 10;
                Color c = kRed;
                if (!bottom && right)  c = kBlue;
                if (bottom  && !right) c = kMagenta;
                if (bottom  && right)  c = kYellow;
                pixels[y * 20 + x] = c;
            }
        }
        tex_->SetData(pixels.data(), static_cast<int>(pixels.size()));
    }

    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);

        dev.Clear(kGreen);
        dev.setBlendStateProperty(BlendState::Opaque);

        sb.Begin(SpriteSortMode::Deferred,
                 BlendState::Opaque,
                 const_cast<SamplerState*>(&SamplerState::PointClamp),
                 nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

        sb.Draw(*tex_,
                Rectangle(100, 100, 50, 50),  // destinationRectangle
                Rectangle(10, 0, 10, 10),      // sourceRectangle: top-right cell only
                Color::White);

        sb.End();

        ExpectPixel("near-dest-topleft-cropped", Rectangle(105, 105, 1, 1), kBlue, /*tolerance=*/60);
        ExpectPixel("near-dest-bottomright-cropped", Rectangle(145, 145, 1, 1), kBlue, /*tolerance=*/60);
        ExpectPixel("outside-sprite", Rectangle(200, 50, 1, 1), kGreen, /*tolerance=*/60);
    }

public:
    OpenGL2SpriteBatchSourceRectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(300);
        gdm_->setPreferredBackBufferHeightProperty(200);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2SpriteBatchSourceRectTest>();
}
