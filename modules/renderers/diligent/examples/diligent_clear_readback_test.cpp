// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-16: real-GPU pixel proof for the Diligent renderer's 2D path. Every
// check goes through the public XNA API (GraphicsDevice::Clear, SpriteBatch, Texture2D,
// GetBackBufferData) and asserts on pixels actually read back from the device, not on "the call
// didn't throw".
//
// Check A -- Clear(Red) is observable through GetBackBufferData().
// Check B -- a second Clear(Blue) replaces it, so no stale frame is being served.
// Check C -- a SpriteBatch quad covering the left half is visible where it was drawn...
// Check D -- ...and the background outside it is untouched (partial coverage, not a full overwrite).
// Check E -- an alpha=0 sprite leaves the destination unmodified.
// Check F -- a sourceRectangle selecting the right texel of a 2x1 red|blue texture samples blue,
//   proving source-rectangle cropping rather than whole-texture sampling.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device (CTest treats that as a skip
// through the registered skip regex, since a headless machine has no GPU to prove anything on).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
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
    constexpr int kChecks = 6;

    bool ColorNear(Color a, Color b, int tolerance = 8)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tolerance &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tolerance &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tolerance;
    }

    Color ReadPixel(GraphicsDevice& device, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }
}

class DiligentClearReadbackTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    SpriteBatch* spriteBatch_ = nullptr;
    Texture2D greenTexture_;
    Texture2D redBlueTexture_;
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
        greenTexture_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                    std::vector<std::uint8_t>{0, 255, 0, 255});
        redBlueTexture_ = Texture2D::CreateFromPixels(
            getGraphicsDeviceProperty(), 2, 1,
            std::vector<std::uint8_t>{255, 0, 0, 255, 0, 0, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        device.Clear(Color::Red);
        Check(ColorNear(ReadPixel(device, kSize / 2, kSize / 2), Color::Red),
              "Clear(Red) is observable through GetBackBufferData()");

        device.Clear(Color::Blue);
        Check(ColorNear(ReadPixel(device, kSize / 2, kSize / 2), Color::Blue),
              "Clear(Blue) replaces the previous frame, no stale content served");

        device.Clear(Color::Black);
        spriteBatch_->Begin();
        spriteBatch_->Draw(greenTexture_, Rectangle(0, 0, kSize / 2, kSize), Color::White);
        spriteBatch_->End();
        Check(ColorNear(ReadPixel(device, kSize / 4, kSize / 2), Color::Lime),
              "SpriteBatch quad is visible inside its destination rectangle");
        Check(ColorNear(ReadPixel(device, kSize - kSize / 4, kSize / 2), Color::Black),
              "background outside the destination rectangle stays black");

        device.Clear(Color::White);
        spriteBatch_->Begin();
        spriteBatch_->Draw(greenTexture_, Rectangle(0, 0, kSize, kSize), Color(255, 255, 255, 0));
        spriteBatch_->End();
        Check(ColorNear(ReadPixel(device, kSize / 2, kSize / 2), Color::White),
              "alpha=0 sprite leaves the destination unmodified");

        device.Clear(Color::Black);
        spriteBatch_->Begin();
        spriteBatch_->Draw(redBlueTexture_, Rectangle(0, 0, kSize, kSize), Rectangle(1, 0, 1, 1),
                           Color::White);
        spriteBatch_->End();
        Check(ColorNear(ReadPixel(device, kSize / 2, kSize / 2), Color::Blue),
              "sourceRectangle selecting the right texel samples blue, not red");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    DiligentClearReadbackTest()
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
        DiligentClearReadbackTest game;
        game.Run();
        return game.GetResult();
    }
    catch (const std::exception& error)
    {
        // Only a genuine "there is no device here" failure is a skip. Any other exception is a
        // real defect and must fail: an over-broad catch turned a renderer bug into a green skip
        // once already while this test was being written.
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
