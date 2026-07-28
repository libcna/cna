// SPDX-License-Identifier: MS-PL
// REMED-GFX-072 (Phase 9): within a single frame/target, each SpriteBatch Begin/End must use the
// GraphicsDevice.Viewport that was active for THAT batch -- not merely the last viewport set.
//
// This is the per-batch requirement for the deferred/per-draw/immediate backends (Vulkan captures
// the viewport per BatchSnapshot at End(); SdlGpu captures it per QueuedDrawRef at enqueue; EasyGL
// flushes per End() and reads the live viewport). A fix that threads only the final viewport into
// the sprite clip-space bake would place the first batch wrong.
//
// (Bgfx accumulates into one view, so two different viewports in one frame there is a separate
// per-view last-wins limitation and this test is not registered for it. WebGPU used to be in the
// same sentence: it accumulated all backbuffer sprites into one render pass whose viewport was
// resolved live at flush time. REMED-GFX-116 made the viewport per-draw state on that backend, so
// this file IS registered for WebGPU now.)
//
// Scene: backbuffer 96x72, Clear Black, two independent Begin/Draw/End cycles, read once via
// GetBackBufferData (top-left consistent):
//   Batch 1: Viewport A=(10,8,30,20), RED  sprite viewport-local Rectangle(2,2,10,8)
//            -> absolute footprint x in [12,22), y in [10,18) (10x8, minX 12, minY 10).
//   Batch 2: Viewport B=(50,40,30,25), BLUE sprite viewport-local Rectangle(4,3,12,9)
//            -> absolute footprint x in [54,66), y in [43,52) (12x9, minX 54, minY 43).
// Red/Blue are full-intensity (0/255) so sRGB cannot masquerade as a placement error.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBBW = 96;
    constexpr int kBBH = 72;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool IsRed(const Color& c)  { return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60; }
    bool IsBlue(const Color& c) { return c.getBProperty() >= 200 && c.getRProperty() <= 60 && c.getGProperty() <= 60; }

    struct BBox
    {
        int minX = std::numeric_limits<int>::max();
        int minY = std::numeric_limits<int>::max();
        int maxX = std::numeric_limits<int>::min();
        int maxY = std::numeric_limits<int>::min();
        int count = 0;
        bool empty() const { return count == 0; }
        int width()  const { return empty() ? 0 : (maxX - minX + 1); }
        int height() const { return empty() ? 0 : (maxY - minY + 1); }
        std::string str() const
        {
            if (empty()) return "<empty>";
            return "x[" + std::to_string(minX) + "," + std::to_string(maxX) + "] y["
                 + std::to_string(minY) + "," + std::to_string(maxY) + "] n=" + std::to_string(count);
        }
    };
}

class SpriteBatchViewportSwitchTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void DrawBatch(GraphicsDevice& dev, const Viewport& vp, const Rectangle& local, const Color& tint)
    {
        dev.setViewportProperty(vp);
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(whiteTex_, local, Rectangle(0, 0, 1, 1), tint);
        sb.End();
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1,
                        std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        dev.setViewportProperty(Viewport(0, 0, kBBW, kBBH));
        dev.Clear(Color::Black);
        DrawBatch(dev, Viewport(10, 8, 30, 20), Rectangle(2, 2, 10, 8), Color::Red);   // batch 1, vp A
        DrawBatch(dev, Viewport(50, 40, 30, 25), Rectangle(4, 3, 12, 9), Color::Blue); // batch 2, vp B
        dev.setViewportProperty(Viewport(0, 0, kBBW, kBBH));

        std::vector<Color> pix(static_cast<std::size_t>(kBBW) * kBBH, Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, kBBW, kBBH);
        dev.GetBackBufferData(&whole, pix.data(), 0, static_cast<int>(pix.size()));

        BBox red, blue;
        int redOutsideA = 0, blueOutsideB = 0;
        for (int y = 0; y < kBBH; ++y)
            for (int x = 0; x < kBBW; ++x)
            {
                const Color& c = pix[static_cast<std::size_t>(y) * kBBW + x];
                if (IsRed(c))
                {
                    red.minX = std::min(red.minX, x); red.maxX = std::max(red.maxX, x);
                    red.minY = std::min(red.minY, y); red.maxY = std::max(red.maxY, y); ++red.count;
                    if (x < 10 || x >= 40 || y < 8 || y >= 28) ++redOutsideA;
                }
                else if (IsBlue(c))
                {
                    blue.minX = std::min(blue.minX, x); blue.maxX = std::max(blue.maxX, x);
                    blue.minY = std::min(blue.minY, y); blue.maxY = std::max(blue.maxY, y); ++blue.count;
                    if (x < 50 || x >= 80 || y < 40 || y >= 65) ++blueOutsideB;
                }
            }

        check(CloseTo(red.width(), 10, 1) && CloseTo(red.height(), 8, 1),
              "Check 1: batch-1 red footprint is 10x8 (viewport A relative, not squished): " + red.str());
        check(CloseTo(red.minX, 12, 1) && CloseTo(red.minY, 10, 1),
              "Check 2: batch-1 red at (12,10) (=A.X+2,A.Y+2) -- uses viewport A, not the final "
              "viewport B: " + red.str());
        check(redOutsideA == 0,
              "Check 3: no red pixels outside viewport A (10,8,30,20): outside=" + std::to_string(redOutsideA));
        check(CloseTo(blue.width(), 12, 1) && CloseTo(blue.height(), 9, 1),
              "Check 4: batch-2 blue footprint is 12x9 (viewport B relative): " + blue.str());
        check(CloseTo(blue.minX, 54, 1) && CloseTo(blue.minY, 43, 1),
              "Check 5: batch-2 blue at (54,43) (=B.X+4,B.Y+3) -- uses viewport B: " + blue.str());
        check(blueOutsideB == 0,
              "Check 6: no blue pixels outside viewport B (50,40,30,25): outside=" + std::to_string(blueOutsideB));

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    SpriteBatchViewportSwitchTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }
};

int main()
{
    SpriteBatchViewportSwitchTest game;
    game.Run();
    return game.getResult();
}
