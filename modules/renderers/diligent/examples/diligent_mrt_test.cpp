// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-24: real-device proof that binding several render targets at once on
// the Diligent renderer genuinely reaches all of them, through the public XNA API only.
//
// Check A -- with two targets bound, Clear() reaches slot 0...
// Check B -- ...and slot 1 as well. XNA clears every bound target, and a renderer that only ever
//   binds slot 0 passes A and fails B.
// Check C -- a stock (single-output) draw while both are bound writes slot 0...
// Check D -- ...and leaves slot 1 exactly as the clear left it. That is CNA's real contract, not a
//   gap in the binding: every built-in shader in this codebase declares one SV_TARGET, so slots
//   1..3 receive clears but no fragments until a multi-output custom ShaderEffect exists
//   (DILIGENT-42). D is what would catch a renderer that wrongly broadcast slot 0's fragments to
//   every bound target.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kTargetSize = 32;
    constexpr int kChecks = 4;

    bool ColorNear(Color a, Color b, int tolerance = 12)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tolerance &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tolerance &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tolerance;
    }

    Color ReadTargetPixel(RenderTarget2D& target, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        target.GetData(0, &region, &pixel, 0, 1);
        return pixel;
    }
}

class DiligentMrtTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    SpriteBatch* spriteBatch_ = nullptr;
    Texture2D whiteTexture_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++passCount_;
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = new SpriteBatch(getGraphicsDeviceProperty());
        whiteTexture_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                    std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);

        RenderTarget2D slot0(device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
                             DepthFormat::Depth24Stencil8);
        RenderTarget2D slot1(device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
                             DepthFormat::None);

        device.SetRenderTargets({RenderTargetBinding(&slot0), RenderTargetBinding(&slot1)});
        device.Clear(Color::Blue);
        device.SetRenderTargets({});

        Check(ColorNear(ReadTargetPixel(slot0, kTargetSize / 2, kTargetSize / 2), Color::Blue),
              "Clear() with two targets bound reaches slot 0");
        Check(ColorNear(ReadTargetPixel(slot1, kTargetSize / 2, kTargetSize / 2), Color::Blue),
              "...and reaches slot 1 as well");

        device.SetRenderTargets({RenderTargetBinding(&slot0), RenderTargetBinding(&slot1)});
        device.Clear(Color::Black);
        spriteBatch_->Begin();
        spriteBatch_->Draw(whiteTexture_, Rectangle(0, 0, kTargetSize, kTargetSize), Color::White);
        spriteBatch_->End();
        device.SetRenderTargets({});

        Check(ColorNear(ReadTargetPixel(slot0, kTargetSize / 2, kTargetSize / 2), Color::White),
              "a stock single-output draw writes slot 0");
        const Color slot1Pixel = ReadTargetPixel(slot1, kTargetSize / 2, kTargetSize / 2);
        std::printf("    (slot 1 after a single-output draw -> R=%d G=%d B=%d)\n",
                    slot1Pixel.getRProperty(), slot1Pixel.getGProperty(), slot1Pixel.getBProperty());
        Check(ColorNear(slot1Pixel, Color::Black),
              "...and leaves slot 1 as the clear left it, rather than broadcasting to every slot");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    DiligentMrtTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(kSize);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentMrtTest game;
        game.Run();
        return game.GetResult();
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        const bool noDevice = message.find("no device type could be created") != std::string::npos ||
                              message.find("unsupported SDL video driver") != std::string::npos ||
                              message.find("SDL_Init") != std::string::npos ||
                              message.find("live SDL window") != std::string::npos;
        if (noDevice)
        {
            std::printf("[SKIP] CNA Diligent smoke: no usable device (%s)\n", error.what());
            return 77;
        }
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        return 1;
    }
}
