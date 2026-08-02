// SPDX-License-Identifier: MS-PL
// SKIA-65 / SKIA-131: RenderTarget2D uploads replace the addressed stable mip surface.

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
#include <stdexcept>
#include <vector>

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

class SkiaRenderTargetSetDataTest final : public Game
{
public:
    SkiaRenderTargetSetDataTest()
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
        std::vector<Color> initial(6, kBlue);
        target.SetData(initial.data(), static_cast<int>(initial.size()));
        const Rectangle column(1, 0, 1, 2);
        const Color replacement[] { kRed, kRed };
        target.SetData(0, &column, replacement, 0, 2);

        device.SetRenderTarget(&target);
        CheckActivePixel(device, 0, 0, kBlue, "full level-0 SetData reaches active Skia target");
        CheckActivePixel(device, 1, 0, kRed, "partial level-0 SetData replaces first requested pixel");
        CheckActivePixel(device, 1, 1, kRed, "partial level-0 SetData replaces second requested pixel");
        CheckActivePixel(device, 2, 1, kBlue, "partial update preserves untouched target pixels");
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> readback(6, Color(0, 0, 0, 0));
        target.GetData(readback.data(), 0, static_cast<int>(readback.size()));
        Check(Same(readback[0], kBlue) && Same(readback[1], kRed) && Same(readback[4], kRed)
                  && Same(readback[5], kBlue),
              "public RenderTarget2D GetData round-trips uploaded level-0 pixels");

        bool invalidLevelRejected = false;
        try
        {
            target.SetData(1, nullptr, replacement, 0, 1);
        }
        catch (const std::out_of_range&)
        {
            invalidLevelRejected = true;
        }
        Check(invalidLevelRejected, "level 1 upload is rejected for a non-mip raster target");

        RenderTarget2D mipTarget(device, 4, 4, true, SurfaceFormat::Color, DepthFormat::None);
        const Color mipPixels[] { kRed, kBlue, kBlue, kRed };
        mipTarget.SetData(1, nullptr, mipPixels, 0, 4);
        Color mipReadback[4] { kBlue, kBlue, kBlue, kBlue };
        mipTarget.GetData(1, nullptr, mipReadback, 0, 4);
        Check(mipTarget.getLevelCountProperty() == 3
                  && Same(mipReadback[0], kRed) && Same(mipReadback[1], kBlue)
                  && Same(mipReadback[2], kBlue) && Same(mipReadback[3], kRed),
              "mipmapped raster RenderTarget2D stores and reads level 1 exactly");
        Exit();
    }

private:
    void CheckActivePixel(GraphicsDevice& device, int x, int y, Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle pixel(x, y, 1, 1);
        device.GetBackBufferData(&pixel, &actual, 0, 1);
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
    SkiaRenderTargetSetDataTest game;
    game.Run();
    return game.Result();
}
