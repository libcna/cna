// SPDX-License-Identifier: MS-PL
// cna-killer KF-6: the back buffer was the window's surface, and on Wayland (Mesa's EGL) that
// surface can lag the size CNA reports. A loading thread that takes the context between two
// frames binds the window's surface, which makes EGL fetch the window's next buffer at the size
// the window has at that moment; a resize the game thread then processes before its next frame
// misses that frame, so Clear covered only the old size and GetBackBufferData read zeros past
// it. The back buffer is now an offscreen framebuffer of the size CNA reports, and the window's
// surface only receives it at Present.
//
// Each cycle resizes the back buffer from Update and starts a worker that creates a texture; the
// worker runs between that frame and the next. Every frame clears the back buffer and reads back
// its far corner. On X11 the race does not exist and this passes either way.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>
#include <thread>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class WorkerResizeBackBufferTest final : public Game
{
public:
    WorkerResizeBackBufferTest()
        : graphics_(std::make_unique<GraphicsDeviceManager>(this))
    {
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setPreferredBackBufferWidthProperty(320);
        graphics_->setPreferredBackBufferHeightProperty(240);
    }

    ~WorkerResizeBackBufferTest() override
    {
        if (worker_.joinable())
            worker_.join();
    }

    [[nodiscard]] int Result() const noexcept { return result_; }

protected:
    void Update(GameTime& gameTime) override
    {
        Game::Update(gameTime);
        ++frames_;
        if (worker_.joinable() && workerDone_.load(std::memory_order_acquire))
        {
            worker_.join();
            idleFrames_ = 0;
        }
        if (!worker_.joinable() && ++idleFrames_ >= 3 && cycles_ < kCycles)
        {
            ++cycles_;
            static constexpr std::array<int, 4> widths{480, 320, 560, 400};
            static constexpr std::array<int, 4> heights{360, 240, 200, 300};
            graphics_->setPreferredBackBufferWidthProperty(widths[cycles_ % widths.size()]);
            graphics_->setPreferredBackBufferHeightProperty(heights[cycles_ % heights.size()]);
            graphics_->ApplyChanges();

            GraphicsDevice* device = &getGraphicsDeviceProperty();
            workerDone_.store(false, std::memory_order_release);
            worker_ = std::thread([this, device] {
                try
                {
                    Texture2D texture(*device, 4, 4);
                    std::array<Color, 16> pixels{};
                    pixels.fill(Color::Red);
                    texture.SetData(pixels.data(), 16);
                }
                catch (...)
                {
                    workerFailure_ = std::current_exception();
                }
                workerDone_.store(true, std::memory_order_release);
            });
        }
        if (cycles_ == kCycles && !worker_.joinable() && idleFrames_ >= 3)
            Finish();
        if (frames_ > 2000)
        {
            std::printf("[FAIL] the cycles did not finish\n");
            ++failures_;
            Finish();
        }
    }

    void Draw(const GameTime& gameTime) override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& pp = device.getPresentationParametersProperty();
        const int width = pp.getBackBufferWidthProperty();
        const int height = pp.getBackBufferHeightProperty();
        const Color colour(40 + frames_ * 7 % 200, 90, 160, 255);
        device.Clear(colour);
        Game::Draw(gameTime);

        const Rectangle corner(width - 1, height - 1, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&corner, &pixel, 0, 1);
        if (pixel != colour)
        {
            if (failures_ < 5)
            {
                std::printf("[FAIL] frame %d, cycle %d: the %dx%d back buffer's corner reads (%d,%d,%d,%d) after "
                            "a Clear to (%d,%d,%d)\n",
                            frames_, cycles_, width, height, pixel.getRProperty(), pixel.getGProperty(),
                            pixel.getBProperty(), pixel.getAProperty(), colour.getRProperty(),
                            colour.getGProperty(), colour.getBProperty());
            }
            ++failures_;
        }
    }

private:
    static constexpr int kCycles = 16;

    void Finish()
    {
        if (workerFailure_)
        {
            try
            {
                std::rethrow_exception(workerFailure_);
            }
            catch (const std::exception& error)
            {
                std::printf("[FAIL] the worker thread threw: %s\n", error.what());
            }
            ++failures_;
        }
        if (failures_ == 0)
            std::printf("[PASS] %d resizes with a worker creating a texture between frames; every frame's "
                        "back buffer cleared completely\n", cycles_);
        else
            std::printf("[FAIL] %d frames read a back buffer that was not cleared completely\n", failures_);
        result_ = failures_ == 0 ? 0 : 1;
        Exit();
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::thread worker_;
    std::atomic<bool> workerDone_{false};
    std::exception_ptr workerFailure_;
    int frames_ = 0;
    int idleFrames_ = 0;
    int cycles_ = 0;
    int failures_ = 0;
    int result_ = 1;
};

int main()
{
    try
    {
        WorkerResizeBackBufferTest game;
        game.Run();
        return game.Result();
    }
    catch (const std::exception& error)
    {
        std::printf("[FAIL] the game ended with an exception: %s\n", error.what());
        return 1;
    }
}
