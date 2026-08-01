// SPDX-License-Identifier: MS-PL
// SKIA-27: pin the public raster mip policy after the Skia API audit.
//
// Skia's checked-in mip builder lives under src/core and is not a public raster-image upload API.
// CNA deliberately does not bind that private/cache-owned implementation: every mipMap=true
// Texture2D or RenderTarget2D request must instead fail as NotSupportedException before callers
// can submit any pixel data. A subsequent level-0 upload/draw proves this refusal does not poison
// the usable 2D path.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);

    [[nodiscard]] bool same(Color left, Color right)
    {
        return left.getRProperty() == right.getRProperty()
            && left.getGProperty() == right.getGProperty()
            && left.getBProperty() == right.getBProperty()
            && left.getAProperty() == right.getAProperty();
    }

    template <typename F>
    [[nodiscard]] bool throwsNotSupported(F&& operation)
    {
        try
        {
            operation();
        }
        catch (const System::NotSupportedException&)
        {
            return true;
        }
        catch (...)
        {
            return false;
        }
        return false;
    }
}

class SkiaMipmapPolicyTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    bool finished_ = false;
    int failures_ = 0;

    void check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures_;
    }

    void checkPixel(int x, Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle pixel(x, 1, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&pixel, &actual, 0, 1);
        check(same(actual, expected), label);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        check(throwsNotSupported([&] {
                  Texture2D rejected(device, 4, 4, true, SurfaceFormat::Color);
                  (void)rejected;
              }),
              "Texture2D mipMap=true rejects with NotSupportedException before SetData is possible");
        check(throwsNotSupported([&] {
                  RenderTarget2D rejected(device, 4, 4, true, SurfaceFormat::Color,
                                          DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
                  (void)rejected;
              }),
              "RenderTarget2D mipMap=true rejects with NotSupportedException");

        Texture2D levelZero(device, 2, 1, false, SurfaceFormat::Color);
        const std::vector<Color> pixels = { kRed, kBlue };
        levelZero.SetData(pixels.data(), static_cast<int>(pixels.size()));
        device.Clear(Color(0, 0, 0, 255));
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
        spriteBatch_->Draw(levelZero, Rectangle(0, 0, 4, 2), Rectangle(0, 0, 2, 1), Color::White);
        spriteBatch_->End();
        checkPixel(1, kRed, "level-0 upload remains usable after rejected mip requests (red half)");
        checkPixel(3, kBlue, "level-0 upload remains usable after rejected mip requests (blue half)");
        Exit();
    }

public:
    SkiaMipmapPolicyTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(4);
        graphics_->setPreferredBackBufferHeightProperty(2);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }
};

int main()
{
    SkiaMipmapPolicyTest game;
    game.Run();
    return game.Result();
}
