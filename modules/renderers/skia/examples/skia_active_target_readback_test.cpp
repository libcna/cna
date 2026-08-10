// SPDX-License-Identifier: MS-PL
// SKIA-68: GetBackBufferData follows Skia's active canvas while a RenderTarget2D is bound, then
// returns to the preserved default backbuffer after unbind. Both destinations use top-left pixels.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBlue(0, 0, 255, 255);
    const Color kRed(255, 0, 0, 255);

    [[nodiscard]] bool Same(Color actual, Color expected)
    {
        return actual.getRProperty() == expected.getRProperty()
            && actual.getGProperty() == expected.getGProperty()
            && actual.getBProperty() == expected.getBProperty()
            && actual.getAProperty() == expected.getAProperty();
    }
}

class SkiaActiveTargetReadbackTest final : public Game
{
public:
    SkiaActiveTargetReadbackTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(8);
        graphics_->setPreferredBackBufferHeightProperty(6);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, 3, 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        const Rectangle firstPixel(0, 0, 1, 1);

        device.Clear(kRed);
        device.SetRenderTarget(&target);
        device.Clear(kBlue);
        CheckBackbuffer(device, firstPixel, kBlue,
                        "GetBackBufferData reads the active render target while it is bound");

        Color targetPixel(0, 0, 0, 0);
        target.GetData(0, &firstPixel, &targetPixel, 0, 1);
        Check(Same(targetPixel, kBlue), "RenderTarget2D GetData agrees with active-target readback");

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        CheckBackbuffer(device, firstPixel, kRed,
                        "GetBackBufferData returns the preserved default backbuffer after unbind");
        Exit();
    }

private:
    void CheckBackbuffer(GraphicsDevice& device, const Rectangle& region, Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        device.GetBackBufferData(&region, &actual, 0, 1);
        Check(Same(actual, expected), label);
    }

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaActiveTargetReadbackTest game;
    game.Run();
    return game.Result();
}
