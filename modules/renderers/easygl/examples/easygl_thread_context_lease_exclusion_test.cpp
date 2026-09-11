// SPDX-License-Identifier: MS-PL

// SAMPLE-067: the renderer thread-context lease must be MUTUAL EXCLUSION between a frame and a
// background content load, on every platform.
//
// The companion test easygl_background_content_context_test proves the other half of the
// contract -- that the context itself survives the handover. Neither of them noticed that the
// Emscripten arm of EasyGLRenderer::AcquireThreadContextLeaseEXT returned null, dropping the
// exclusion entirely on the web: with -sOFFSCREEN_FRAMEBUFFER=1 every GL call is proxied to one
// shared browser-thread context, so a loading thread's bind/upload pair could be split by the
// frame's own binds and a texture came out empty (a different subset of background-loaded
// textures rendered black in 2 of 5 Firefox and 1 of 5 Chrome runs of CatapultWars).
//
// The lease code is now shared by both platforms -- only the binding handover is native-only --
// so this native test exercises the same statements the browser depends on. It asserts what the
// bug actually violated: the lease is never null, two threads never hold it at once, and a
// worker never runs inside a frame's Draw.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int WorkerCount = 2;
    constexpr int IterationsPerWorker = 120;
}

class ThreadContextLeaseExclusionTest final : public Game
{
public:
    ThreadContextLeaseExclusionTest()
        : graphics_(std::make_unique<GraphicsDeviceManager>(this))
    {
    }

    ~ThreadContextLeaseExclusionTest() override
    {
        JoinWorkers();
    }

    [[nodiscard]] int Result() const noexcept { return result_; }

protected:
    void Update(GameTime&) override
    {
        ++updateCount_;

        // Start after one complete frame, so the workers contend with a real frame lease rather
        // than with device creation.
        if (updateCount_ == 2 && workers_.empty())
        {
            for (int i = 0; i < WorkerCount; ++i)
            {
                workers_.emplace_back([this]() { WorkerBody(); });
            }
        }

        if (workersFinished_.load(std::memory_order_acquire) == WorkerCount && !workersJoined_)
        {
            JoinWorkers();
            workersJoined_ = true;
            Report();
            Exit();
            return;
        }

        if (updateCount_ > 900)
        {
            std::fprintf(stderr, "[FAIL] lease exclusion workers did not finish in time\n");
            result_ = 1;
            Exit();
        }
    }

    void Draw(const GameTime&) override
    {
        // GraphicsDeviceManager::BeginDraw holds the frame lease across this call, so any worker
        // observed inside its own lease while this flag is set proves the exclusion is gone.
        inFrame_.store(true, std::memory_order_release);
        getGraphicsDeviceProperty().Clear(Color::CornflowerBlue);
        ++drawCount_;
        inFrame_.store(false, std::memory_order_release);
    }

private:
    void WorkerBody()
    {
        auto& renderer = getGraphicsDeviceProperty().GetRenderer();
        for (int i = 0; i < IterationsPerWorker; ++i)
        {
            auto lease = renderer.AcquireThreadContextLeaseEXT();
            if (lease == nullptr)
            {
                nullLease_.store(true, std::memory_order_release);
                break;
            }

            if (inFrame_.load(std::memory_order_acquire))
            {
                overlappedFrame_.store(true, std::memory_order_release);
            }
            if (inside_.fetch_add(1, std::memory_order_acq_rel) != 0)
            {
                overlappedWorker_.store(true, std::memory_order_release);
            }

            std::this_thread::sleep_for(std::chrono::microseconds(200));

            inside_.fetch_sub(1, std::memory_order_acq_rel);
            ++acquisitions_;
        }
        workersFinished_.fetch_add(1, std::memory_order_acq_rel);
    }

    void JoinWorkers()
    {
        for (auto& worker : workers_)
        {
            if (worker.joinable()) worker.join();
        }
    }

    void Report()
    {
        if (nullLease_.load(std::memory_order_acquire))
        {
            std::fprintf(stderr,
                "[FAIL] AcquireThreadContextLeaseEXT returned null; a background load and the "
                "frame are not serialized\n");
            result_ = 1;
            return;
        }
        if (overlappedWorker_.load(std::memory_order_acquire))
        {
            std::fprintf(stderr, "[FAIL] two threads held the renderer context lease at once\n");
            result_ = 1;
            return;
        }
        if (overlappedFrame_.load(std::memory_order_acquire))
        {
            std::fprintf(stderr, "[FAIL] a lease was held while a frame was drawing\n");
            result_ = 1;
            return;
        }
        if (acquisitions_.load(std::memory_order_acquire) != WorkerCount * IterationsPerWorker)
        {
            std::fprintf(stderr, "[FAIL] only %d of %d lease acquisitions completed\n",
                         acquisitions_.load(std::memory_order_acquire),
                         WorkerCount * IterationsPerWorker);
            result_ = 1;
            return;
        }
        if (drawCount_ == 0)
        {
            std::fprintf(stderr, "[FAIL] no frame was drawn while the workers contended\n");
            result_ = 1;
            return;
        }

        std::printf(
            "[PASS] %d renderer context leases across %d threads excluded each other and %d "
            "frames\n",
            acquisitions_.load(std::memory_order_acquire), WorkerCount, drawCount_);
        result_ = 0;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::vector<std::thread> workers_;
    std::atomic<int> workersFinished_{0};
    std::atomic<int> inside_{0};
    std::atomic<int> acquisitions_{0};
    std::atomic<bool> inFrame_{false};
    std::atomic<bool> nullLease_{false};
    std::atomic<bool> overlappedWorker_{false};
    std::atomic<bool> overlappedFrame_{false};
    bool workersJoined_ = false;
    int updateCount_ = 0;
    int drawCount_ = 0;
    int result_ = 1;
};

int main()
{
    ThreadContextLeaseExclusionTest game;
    game.Run();
    return game.Result();
}
