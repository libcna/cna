// SPDX-License-Identifier: MS-PL
// SDLGPU-68: native swapchain acquisition can succeed with a null texture while a
// window is minimized. The command buffer must still be submitted, and the pending frame must be
// retained so restore/retry renders the work rather than silently dropping it.

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>

using CNA::Internal::Renderers::SdlGpu::SdlGpuRenderer;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GameTime;
using Microsoft::Xna::Framework::GraphicsDeviceManager;
using Microsoft::Xna::Framework::Rectangle;

class SdlGpuMinimizedRetryTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passed_ = 0;
    int failed_ = 0;
    bool done_ = false;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        condition ? ++passed_ : ++failed_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto& renderer = dynamic_cast<SdlGpuRenderer&>(device.GetRenderer());
        const Rectangle centre(32, 32, 1, 1);

        device.Clear(Color::Red);
        renderer.ForceNextNullSwapchainTextureForTestEXT();
        bool presentThrew = false;
        try
        {
            device.Present();
        }
        catch (...)
        {
            presentThrew = true;
        }
        Check(!presentThrew,
              "documented minimized-window null swapchain texture is a non-error");

        Color pixel;
        device.GetBackBufferData(&centre, &pixel, 0, 1);
        Check(pixel == Color::Red,
              "pending clear survives the null acquisition and renders on the retry");

        device.Clear(Color::Blue);
        device.GetBackBufferData(&centre, &pixel, 0, 1);
        Check(pixel == Color::Blue,
              "renderer remains live after the null-acquisition retry");

        std::printf("=== %d/%d PASS ===\n", passed_, passed_ + failed_);
        Exit();
    }

public:
    SdlGpuMinimizedRetryTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    [[nodiscard]] int Result() const { return failed_ == 0 ? 0 : 1; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuMinimizedRetryTest game;
    game.Run();
    return game.Result();
}
