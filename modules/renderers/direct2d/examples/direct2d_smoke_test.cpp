// SPDX-License-Identifier: MS-PL
// A real Direct2D presentation/readback/sprite proof. This intentionally uses NativeBackBuffer so
// its pixel coordinates are exact and does not depend on a letterbox/overscan scaling policy.
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/Direct2D/Direct2DRenderer.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using XnaRectangle = Microsoft::Xna::Framework::Rectangle;

namespace
{
    bool Matches(const Color& actual, const Color& expected, int tolerance = 4)
    {
        return std::abs(static_cast<int>(actual.getRProperty()) - expected.getRProperty()) <= tolerance &&
               std::abs(static_cast<int>(actual.getGProperty()) - expected.getGProperty()) <= tolerance &&
               std::abs(static_cast<int>(actual.getBProperty()) - expected.getBProperty()) <= tolerance &&
               std::abs(static_cast<int>(actual.getAProperty()) - expected.getAProperty()) <= tolerance;
    }
}

class Direct2DSmokeTest final : public Game
{
public:
    Direct2DSmokeTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(32);
        manager_->setPreferredBackBufferHeightProperty(32);
        manager_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const { return result_; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sprites_ = std::make_unique<SpriteBatch>(device);
        texture_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(device, 2, 2, std::vector<uint8_t>{
                255, 0, 0, 255,    0, 255, 0, 255,
                0, 0, 255, 255,    255, 255, 255, 255,
            }));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<CNA::Internal::Renderers::Direct2D::Direct2DRenderer&>(device.GetRenderer());
        auto* window = reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty());
        bool passed = window != nullptr && SDL_GetRenderer(window) == nullptr;

        device.Clear(Color(12, 34, 56, 255));
        Color clearPixel(0, 0, 0, 0);
        const XnaRectangle point(1, 1, 1, 1);
        device.GetBackBufferData(&point, &clearPixel, 0, 1);
        passed = passed && Matches(clearPixel, Color(12, 34, 56, 255));

        sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                        const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr);
        sprites_->Draw(*texture_, XnaRectangle(8, 8, 8, 8), XnaRectangle(0, 0, 2, 2), Color::White);
        sprites_->End();
        Color spritePixel(0, 0, 0, 0);
        const XnaRectangle spritePoint(9, 9, 1, 1);
        device.GetBackBufferData(&spritePoint, &spritePixel, 0, 1);
        passed = passed && Matches(spritePixel, Color(255, 0, 0, 255));

        std::printf("[%s] Direct2D HWND target, D3D11 staging readback, and point SpriteBatch draw\n",
                    passed ? "PASS" : "FAIL");
        result_ = passed ? 0 : 1;
        Exit();
    }

private:
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<SpriteBatch> sprites_;
    std::unique_ptr<Texture2D> texture_;
    bool done_ = false;
    int result_ = 1;
};

int main()
{
    Direct2DSmokeTest test;
    test.Run();
    return test.Result();
}
