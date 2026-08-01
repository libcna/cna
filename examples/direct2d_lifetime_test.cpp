// SPDX-License-Identifier: MS-PL
// D2D-27: exercise Direct2D's frame-scoped resources across the boundaries that release them.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class Direct2DLifetimeTest final : public Game
{
public:
    Direct2DLifetimeTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(32);
        manager_->setPreferredBackBufferHeightProperty(24);
        manager_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const { return result_; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sprites_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<uint8_t>{255, 255, 255, 255}));
        target_ = std::make_unique<RenderTarget2D>(device, 16, 16, true,
                                                   SurfaceFormat::Color, DepthFormat::None);
    }

    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        SamplerState pointWrap;
        pointWrap.setFilterProperty(TextureFilter::Point);
        pointWrap.setAddressUProperty(TextureAddressMode::Wrap);
        pointWrap.setAddressVProperty(TextureAddressMode::Mirror);

        // Fill an off-screen target, then sample it through ImageBrush with wrapping and flip.
        // This allocates frame-scoped Direct2D image brushes, which must be released by every
        // EndDraw path below rather than being retained across frames.
        device.SetRenderTarget(target_.get());
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointWrap, nullptr, nullptr);
        for (int y = 0; y < 16; ++y)
        {
            for (int x = 0; x < 16; ++x)
            {
                const Color color = ((x + y + frame_) & 1) == 0
                    ? Color(255, 0, 0, 255) : Color(0, 255, 0, 255);
                sprites_->Draw(*white_, Rectangle(x, y, 1, 1), Rectangle(0, 0, 1, 1), color);
            }
        }
        sprites_->End();

        if ((frame_ & 1) == 0)
        {
            // Forces EndDraw and target switching while image resources are live.
            Color mipPixel(0, 0, 0, 0);
            const Rectangle texel(0, 0, 1, 1);
            target_->GetData(1, &texel, &mipPixel, 0, 1);
        }

        device.SetRenderTarget(nullptr);
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointWrap, nullptr, nullptr);
        for (int index = 0; index < 128; ++index)
        {
            const int x = (index * 3) % 32;
            const int y = (index * 5) % 24;
            sprites_->Draw(*target_, Rectangle(x, y, 4, 4), Rectangle(-1, -1, 4, 4), Color::White,
                           0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
        }
        sprites_->End();

        // Readback is another EndDraw boundary. Keep a minimal observable result so a silent
        // target/clip failure cannot turn this into a mere no-crash test.
        Color pixel(0, 0, 0, 0);
        const Rectangle readbackTexel(0, 0, 1, 1);
        device.GetBackBufferData(&readbackTexel, &pixel, 0, 1);
        if (pixel.getAProperty() != 255)
        {
            std::printf("[FAIL] Direct2D lifetime smoke readback alpha=%d\n", pixel.getAProperty());
            result_ = 1;
            Exit();
            return;
        }

        if (frame_ == 2)
        {
            device.GetBackend().DebugSimulateContextLoss();
        }
        else if (frame_ == 4)
        {
            manager_->setPreferredBackBufferWidthProperty(40);
            manager_->setPreferredBackBufferHeightProperty(24);
            manager_->ApplyChanges();
        }

        ++frame_;
        if (frame_ == 8)
        {
            std::printf("[PASS] Direct2D lifetime smoke\n");
            result_ = 0;
            Exit();
        }
    }

private:
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<SpriteBatch> sprites_;
    std::unique_ptr<Texture2D> white_;
    std::unique_ptr<RenderTarget2D> target_;
    int frame_ = 0;
    int result_ = 1;
};

int main()
{
    Direct2DLifetimeTest test;
    test.Run();
    return test.Result();
}
