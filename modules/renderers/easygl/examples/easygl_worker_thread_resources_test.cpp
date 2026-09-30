// SPDX-License-Identifier: MS-PL
// cna-killer KF-1: XNA 4.0 lets a game create resources and set their data on any thread -- the
// loading-screen pattern. Only ContentReader leased EasyGL's context, so a Texture2D or VertexBuffer
// constructed on a worker made the context current there while the game thread still held it
// ("GlContext::MakeCurrent failed: BadAccess"), and the game thread's context was unusable after;
// two workers at once also corrupted the heap (KF-1a). Resource construction and data transfer now
// lease the context, so a worker waits for the game thread to be between frames.
//
// Two workers each create a texture (filled and read back), a vertex buffer and a render target
// while the game keeps running frames; the game thread then renders into both targets and samples
// both textures.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    struct Load
    {
        Color colour;
        std::thread thread;
        std::atomic<bool> done{false};
        std::exception_ptr failure;
        bool readBackMatched = false;
        std::unique_ptr<Texture2D> texture;
        std::unique_ptr<VertexBuffer> vertices;
        std::unique_ptr<RenderTarget2D> target;
    };
}

class WorkerThreadResourcesTest final : public Game
{
public:
    WorkerThreadResourcesTest()
        : graphics_(std::make_unique<GraphicsDeviceManager>(this))
    {
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        loads_[0].colour = Color(200, 40, 40, 255);
        loads_[1].colour = Color(40, 40, 200, 255);
    }

    ~WorkerThreadResourcesTest() override
    {
        for (Load& load : loads_)
        {
            if (load.thread.joinable())
                load.thread.join();
        }
    }

    [[nodiscard]] int Result() const noexcept { return result_; }

protected:
    void Update(GameTime& gameTime) override
    {
        Game::Update(gameTime);
        ++updates_;
        if (updates_ == 2)
        {
            GraphicsDevice* device = &getGraphicsDeviceProperty();
            for (Load& load : loads_)
            {
                Load* job = &load;
                load.thread = std::thread([job, device] {
                    try
                    {
                        std::vector<Color> pixels(32 * 32, job->colour);
                        job->texture = std::make_unique<Texture2D>(*device, 32, 32);
                        job->texture->SetData(pixels.data(), static_cast<int>(pixels.size()));
                        std::vector<Color> readBack(pixels.size());
                        job->texture->GetData(readBack.data(), static_cast<int>(readBack.size()));
                        job->readBackMatched = readBack == pixels;
                        const std::array<VertexPositionColor, 3> triangle{
                            VertexPositionColor(Vector3(-1, 1, 0), job->colour),
                            VertexPositionColor(Vector3(1, 1, 0), job->colour),
                            VertexPositionColor(Vector3(-1, -1, 0), job->colour)};
                        job->vertices = std::make_unique<VertexBuffer>(
                            *device, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
                        job->vertices->SetData(triangle.data(), 3);
                        job->target = std::make_unique<RenderTarget2D>(*device, 16, 16);
                    }
                    catch (...)
                    {
                        job->failure = std::current_exception();
                    }
                    job->done.store(true, std::memory_order_release);
                });
            }
        }
        if (updates_ > 600)
        {
            std::printf("[FAIL] the worker threads did not finish while the game kept running frames\n");
            result_ = 1;
            Exit();
        }
    }

    void Draw(const GameTime& gameTime) override
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color::Black);
        Game::Draw(gameTime);
        if (updates_ < 2 || !loads_[0].done.load() || !loads_[1].done.load())
            return;

        bool pass = true;
        SpriteBatch batch(device);
        for (Load& load : loads_)
        {
            load.thread.join();
            if (load.failure)
            {
                try
                {
                    std::rethrow_exception(load.failure);
                }
                catch (const std::exception& error)
                {
                    std::printf("[FAIL] a worker thread threw: %s\n", error.what());
                }
                pass = false;
                continue;
            }
            if (!load.readBackMatched)
            {
                std::printf("[FAIL] a texture filled on a worker thread read back different data\n");
                pass = false;
            }
            // The game thread renders into the worker's render target, sampling the worker's texture.
            device.SetRenderTarget(load.target.get());
            device.Clear(Color::White);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            batch.Draw(*load.texture, Rectangle(0, 0, 16, 16), Color::White);
            batch.End();
            device.SetRenderTarget(nullptr);
            std::vector<Color> pixels(256);
            load.target->GetData(pixels.data(), 256);
            if (pixels[0] != load.colour || pixels[255] != load.colour)
            {
                std::printf("[FAIL] a worker-created texture drawn into a worker-created target reads (%d,%d,%d)\n",
                            pixels[0].getRProperty(), pixels[0].getGProperty(), pixels[0].getBProperty());
                pass = false;
            }
        }
        if (pass)
            std::printf("[PASS] two worker threads created textures, buffers and targets the game thread used\n");
        result_ = pass ? 0 : 1;
        Exit();
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::array<Load, 2> loads_;
    int updates_ = 0;
    int result_ = 1;
};

int main()
{
    try
    {
        WorkerThreadResourcesTest game;
        game.Run();
        return game.Result();
    }
    catch (const std::exception& error)
    {
        std::printf("[FAIL] the game ended with an exception: %s\n", error.what());
        return 1;
    }
}
