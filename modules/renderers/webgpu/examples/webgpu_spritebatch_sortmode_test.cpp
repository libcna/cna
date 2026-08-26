// SPDX-License-Identifier: MS-PL
// WEBGPU-63: verify every SpriteBatch sort mode's ordering reaches the WebGPU submission path.
// The sort itself is shared SpriteBatch behaviour; this test proves the WebGPU renderer draws the
// resulting sprites in the order the sort produced, by overlapping two solid sprites (red at
// layerDepth 0.2, blue at 0.8) in the same rectangle and reading which colour ends up on top.
//
// Each depth-sorted mode is set up so the sorted result is the OPPOSITE of the submission order,
// so the assertion can only pass if the sort actually took effect through to the WebGPU draw:
//
// Check A -- BackToFront, submitted [red@0.2, blue@0.8]: low depth wins -> RED on top (opposite of
//   submission order, which would put blue on top).
// Check B -- FrontToBack, submitted [blue@0.8, red@0.2]: high depth wins -> BLUE on top (opposite
//   of submission order).
// Check C -- Deferred, submitted [red, blue]: depth ignored, last submitted wins -> BLUE.
// Check D -- Deferred, submitted [blue, red]: last submitted wins -> RED (confirms Deferred is
//   order-based, not depth-based).
// Check E -- Immediate, submitted [red, blue]: draws in submission order -> BLUE.
// Check F -- Texture (single texture), submitted [red, blue]: depth ignored, order preserved within
//   the one texture group -> BLUE.
//
// All rendering is into a RenderTarget2D the same size as the backbuffer (opaque sprites, no alpha
// or wrap sampling), so this is independent of the WEBGPU-141 backbuffer-readback issue.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 32;

    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    bool isRed(const Color& c)  { return c.getRProperty() > 200 && c.getGProperty() < 60 && c.getBProperty() < 60; }
    bool isBlue(const Color& c) { return c.getBProperty() > 200 && c.getRProperty() < 60 && c.getGProperty() < 60; }
}

class WebGpuSpriteSortModeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    SpriteBatch* sb_ = nullptr;
    Texture2D white_;
    int frame_ = 0;

    // Draws two overlapping solid sprites in submission order under `mode`, returns the centre
    // colour. Each spec is (tint, layerDepth).
    Color topColour(GraphicsDevice& dev, SpriteSortMode mode,
                    std::pair<Color, float> a, std::pair<Color, float> b)
    {
        RenderTarget2D target(dev, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        const Rectangle rect(4, 4, kSize - 8, kSize - 8);   // both sprites share this rect
        const Rectangle src(0, 0, 1, 1);
        const Vector2 origin(0.0f, 0.0f);

        dev.SetRenderTarget(&target);
        dev.Clear(Color::Black);
        sb_->Begin(mode, BlendState::Opaque);
        sb_->Draw(white_, rect, src, a.first, 0.0f, origin, SpriteEffects::None, a.second);
        sb_->Draw(white_, rect, src, b.first, 0.0f, origin, SpriteEffects::None, b.second);
        sb_->End();
        dev.SetRenderTarget(nullptr);

        Color px(0, 0, 0, 0);
        const Rectangle center(kSize / 2, kSize / 2, 1, 1);
        target.GetData(0, &center, &px, 0, 1);
        return px;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;
        auto& dev = getGraphicsDeviceProperty();

        const auto red = std::make_pair(Color(255, 0, 0, 255), 0.2f);
        const auto blue = std::make_pair(Color(0, 0, 255, 255), 0.8f);

        check(isRed(topColour(dev, SpriteSortMode::BackToFront, red, blue)),
              "Check A: BackToFront sorts low-depth to the top (red), overriding submission order");
        check(isBlue(topColour(dev, SpriteSortMode::FrontToBack, blue, red)),
              "Check B: FrontToBack sorts high-depth to the top (blue), overriding submission order");
        check(isBlue(topColour(dev, SpriteSortMode::Deferred, red, blue)),
              "Check C: Deferred keeps submission order (blue drawn last is on top)");
        check(isRed(topColour(dev, SpriteSortMode::Deferred, blue, red)),
              "Check D: Deferred is order-based, not depth-based (red drawn last is on top)");
        check(isBlue(topColour(dev, SpriteSortMode::Immediate, red, blue)),
              "Check E: Immediate draws in submission order (blue last is on top)");
        check(isBlue(topColour(dev, SpriteSortMode::Texture, red, blue)),
              "Check F: Texture mode (one texture group) preserves submission order (blue on top)");

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    WebGpuSpriteSortModeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    void LoadContent() override
    {
        sb_ = new SpriteBatch(getGraphicsDeviceProperty());
        white_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                             std::vector<std::uint8_t>{255, 255, 255, 255});
    }
};

int main()
{
    WebGpuSpriteSortModeTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
