// SPDX-License-Identifier: MS-PL
// MTH-PLAT-004: an installed-SDK Emscripten application that must draw and present real frames
// in a browser. scripts/run_browser_tests.mjs accepts it only on its fixed-denominator summary
// with no [FAIL] line, page error or uncaptured WebGPU error.
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include <cstdio>
#include <exception>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
constexpr int FrameCount = 3;
// Renderer identity, one check per frame, Run() returning after Exit(), destruction.
constexpr int Expected = 1 + FrameCount + 2;
int passed = 0;
int total = 0;
bool destroyed = false;

void Check(bool ok, const char* label)
{
    ++total;
    passed += ok;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
    std::fflush(stdout);
}

class InstalledBrowserFrame final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<Texture2D> white_;
    std::unique_ptr<SpriteBatch> batch_;
    int frames_ = 0;

    static Color Read(GraphicsDevice& device, int x, int y)
    {
        Color pixel;
        const Rectangle region(x, y, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto name = device.GetGraphicsRendererName();
        std::printf("[RENDERER] %.*s expected=%s\n", static_cast<int>(name.size()), name.data(),
                    PROBE_EXPECTED_RENDERER);
        Check(name == PROBE_EXPECTED_RENDERER, "installed SDK runs the producer's renderer");
        white_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color texel = Color::White;
        white_->SetData(&texel, 1);
        batch_ = std::make_unique<SpriteBatch>(device);
    }

    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        // Alternate the clear colour so a stale previous frame cannot satisfy the check.
        const Color background = frames_ % 2 ? Color::Blue : Color::Red;
        device.Clear(background);
        batch_->Begin();
        batch_->Draw(*white_, Rectangle(8, 8, 16, 16), Color::Lime);
        batch_->End();
        ++frames_;
        std::printf("[FRAME] %d\n", frames_);
        Check(Read(device, 48, 48) == background && Read(device, 16, 16) == Color::Lime,
              "frame cleared its backbuffer and drew a sprite");
        if (frames_ == FrameCount)
            Exit();
    }

public:
    InstalledBrowserFrame()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }

    ~InstalledBrowserFrame() override { destroyed = true; }

    int DrawnFrames() const { return frames_; }
};
}

int main()
{
    try
    {
        InstalledBrowserFrame game;
        game.Run();
        Check(game.DrawnFrames() == FrameCount, "blocking Run() returned after Exit() on the last frame");
    }
    catch (const std::exception& error)
    {
        std::printf("[FAIL] installed browser application: %s\n", error.what());
    }
    Check(destroyed, "stack-local Game was destroyed after Run() returned");
    // Fixed denominator: an early exit cannot qualify a partial run.
    std::printf("=== %d/%d PASS ===\n", passed, Expected);
    std::fflush(stdout);
    return passed == Expected && total == Expected ? 0 : 1;
}
