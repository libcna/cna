// SPDX-License-Identifier: MS-PL
// REMED-GFX-179: render-target lifetime must not consume bgfx's finite per-frame view-id space.
//
// The historical allocator assigned one persistent view id to every live RenderTarget2D/Cube and
// partitioned only [1,64) for those ids.  The 64th simultaneous target therefore failed during its
// constructor even when none of the targets had ever been bound.  This fixture separates that
// lifetime cardinality from the thing bgfx actually limits: distinct ordered views USED in one
// frame.  It also verifies the state/lifetime cases that make a simple shared-view workaround
// unsafe: A/B/A ordering, per-frame ID reuse, readback's reserved highest view, native-handle
// reuse, disposal/recreation, and normal teardown with many targets still live.

#include "CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Backends::Bgfx::BgfxGraphicsBackend;
using CNA::Internal::Backends::Bgfx::BgfxRenderTargetBackend;

namespace
{
    constexpr int kBackbufferSize = 32;
    constexpr int kTargetSize = 4;

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() &&
               a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() &&
               a.getAProperty() == b.getAProperty();
    }

    std::unique_ptr<RenderTarget2D> MakeTarget(GraphicsDevice& device)
    {
        return std::make_unique<RenderTarget2D>(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    }

    BgfxRenderTargetBackend& NativeTarget(RenderTarget2D& target)
    {
        return *static_cast<BgfxRenderTargetBackend*>(target.GetRenderTargetBackend());
    }
}

class BgfxGfx179ViewCapacityTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<RenderTarget2D> targetA_;
    std::unique_ptr<RenderTarget2D> targetB_;
    std::unique_ptr<RenderTarget2D> targetC_;
    std::vector<std::unique_ptr<RenderTarget2D>> teardownTargets_;
    int step_ = 0;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;
    bgfx::ViewId firstFrameFirstView_ = bgfx::kInvalidHandle;
    std::set<uint16_t> framebufferIds_;
    std::set<uint16_t> textureIds_;
    int sequentialCreated_ = 0;
    bool framebufferReuse_ = false;
    bool textureReuse_ = false;
    bool sequentialNativeValid_ = true;
    bool disposedBackendCleared_ = true;

    void Check(bool condition, const std::string& label)
    {
        ++total_;
        if (condition) ++passed_;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
    }

    BgfxGraphicsBackend& Backend()
    {
        return static_cast<BgfxGraphicsBackend&>(getGraphicsDeviceProperty().GetBackend());
    }

    void RecordNativeIds(RenderTarget2D& target)
    {
        const auto& native = NativeTarget(target);
        sequentialNativeValid_ = sequentialNativeValid_ &&
                                 bgfx::isValid(native.fbo) && bgfx::isValid(native.colorTex);
        framebufferReuse_ = framebufferReuse_ ||
                            !framebufferIds_.insert(native.fbo.idx).second;
        textureReuse_ = textureReuse_ || !textureIds_.insert(native.colorTex.idx).second;
    }

    void CheckSimultaneousCount(GraphicsDevice& device, int count)
    {
        std::vector<std::unique_ptr<RenderTarget2D>> targets;
        targets.reserve(static_cast<std::size_t>(count));
        std::string failure;
        try
        {
            for (int i = 0; i < count; ++i)
            {
                targets.push_back(MakeTarget(device));
                RecordNativeIds(*targets.back());
            }
        }
        catch (const std::exception& ex)
        {
            failure = ex.what();
        }

        Check(static_cast<int>(targets.size()) == count && failure.empty(),
              std::to_string(count) + " simultaneously live, never-bound targets construct" +
                  (failure.empty() ? std::string() : " (stopped at " +
                      std::to_string(targets.size()) + ": " + failure + ")"));
    }

    void RunSequentialCreationBatch(GraphicsDevice& device)
    {
        // Twenty-four stays below bgfx's 128-entry native framebuffer pool even beside the 96-live
        // boundary wave.  Eleven Draw ticks make 264 sequential cycles while Present between
        // batches gives native reclamation its actual frame-latency opportunity.
        constexpr int kPerFrame = 24;
        for (int i = 0; i < kPerFrame; ++i)
        {
            auto target = MakeTarget(device);
            RecordNativeIds(*target);
            target->Dispose();
            disposedBackendCleared_ = disposedBackendCleared_ &&
                                      target->GetRenderTargetBackend() == nullptr;
            ++sequentialCreated_;
        }
    }

    void CheckStrictPublicViewOrder(const std::vector<bgfx::ViewId>& order,
                                    std::size_t expectedCount,
                                    const std::string& label)
    {
        bool valid = order.size() == expectedCount;
        for (std::size_t i = 0; valid && i < order.size(); ++i)
        {
            valid = order[i] < CNA::Internal::Backends::Bgfx::Detail::kBackbufferFlushViewId;
            if (i > 0) valid = valid && order[i - 1] < order[i];
        }
        Check(valid, label);
    }

    void CheckTargetColor(RenderTarget2D& target, const Color& expected,
                          const std::string& label)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kTargetSize) * kTargetSize,
                                  Color(1, 2, 3, 4));
        target.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        Check(std::all_of(pixels.begin(), pixels.end(), [&](const Color& pixel) {
                  return Same(pixel, expected);
              }), label);
    }

    void RunOldLimitFrame(GraphicsDevice& device)
    {
        // The exact old last-supported count.
        CheckSimultaneousCount(device, 63);
        RunSequentialCreationBatch(device);
    }

    void RunFirstFailureAndOrderingFrame(GraphicsDevice& device)
    {
        // The old allocator failed on target index 63: the 64th simultaneous target.
        CheckSimultaneousCount(device, 64);
        RunSequentialCreationBatch(device);

        targetA_ = MakeTarget(device);
        targetB_ = MakeTarget(device);

        // Three observable operations in one unadvanced frame.  A shared or recycled same-frame
        // view redirects an earlier clear because bgfx resolves framebuffer state once per view.
        device.SetRenderTarget(targetA_.get());
        device.Clear(Color(220, 20, 30, 255));
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.SetRenderTarget(targetB_.get());
        device.Clear(Color(20, 40, 220, 255));
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.SetRenderTarget(targetA_.get());
        device.Clear(Color(20, 210, 60, 255));
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const auto& order = Backend().frameViewOrder_;
        CheckStrictPublicViewOrder(order, 3,
                                   "target A/B/A bind-unbind operations own three collision-free "
                                   "views in public order");
        if (!order.empty()) firstFrameFirstView_ = order.front();
    }

    void RunReadbackAndFrameReuse(GraphicsDevice& device)
    {
        // Present ended the previous Draw tick.  A target used in this new frame must reclaim the
        // first per-frame ID instead of growing a process-lifetime cursor.
        targetC_ = MakeTarget(device);
        device.SetRenderTarget(targetC_.get());
        device.Clear(Color(230, 180, 20, 255));
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const auto& order = Backend().frameViewOrder_;
        Check(order.size() == 1 && order.front() == firstFrameFirstView_,
              "the first ordered view ID is reused after Present starts a new frame");

        CheckTargetColor(*targetA_, Color(20, 210, 60, 255),
                         "A/B/A leaves target A with its final A contents");
        CheckTargetColor(*targetB_, Color(20, 40, 220, 255),
                         "A/B/A leaves target B with its own contents");
        CheckTargetColor(*targetC_, Color(230, 180, 20, 255),
                         "the next-frame target survives direct readback");

        CheckSimultaneousCount(device, 65);
        RunSequentialCreationBatch(device);
    }

    void RunReservedReadbackInteraction(GraphicsDevice& device)
    {
        CheckSimultaneousCount(device, 66);
        RunSequentialCreationBatch(device);

        const Color backbufferColor(70, 30, 190, 255);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.Clear(backbufferColor);
        CheckStrictPublicViewOrder(Backend().frameViewOrder_, 1,
                                   "a fresh backbuffer frame uses one non-reserved public view");

        Color pixel(0, 0, 0, 0);
        const Rectangle sample(kBackbufferSize / 2, kBackbufferSize / 2, 1, 1);
        device.GetBackBufferData(&sample, &pixel, 0, 1);
        Check(Same(pixel, backbufferColor),
              "reserved highest-view backbuffer readback returns the current frame exactly");

        // GetBackBufferData advanced the frame and recycled only public per-frame IDs.  The
        // readback reservation at 255 must remain excluded while the first target ID is reusable.
        device.SetRenderTarget(targetA_.get());
        device.Clear(Color(15, 100, 235, 255));
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const auto& order = Backend().frameViewOrder_;
        Check(order.size() == 1 && order.front() == firstFrameFirstView_ &&
                  order.front() != CNA::Internal::Backends::Bgfx::Detail::kBackbufferFlushViewId,
              "readback view 255 never collides with the reclaimed first target view ID");
        CheckTargetColor(*targetA_, Color(15, 100, 235, 255),
                         "target rendering remains exact after reserved-view readback");
    }

    void RunBeyondBoundaryFrame(GraphicsDevice& device)
    {
        CheckSimultaneousCount(device, 96);
        RunSequentialCreationBatch(device);
    }

    void RunReclamationFrame(GraphicsDevice& device)
    {
        RunSequentialCreationBatch(device);
    }

    void RunTeardownFrame(GraphicsDevice& device)
    {
        Check(sequentialCreated_ == 264 && sequentialNativeValid_ && disposedBackendCleared_,
              "264 sequential create/dispose cycles across frames keep native objects valid and "
              "release wrappers");
        Check(framebufferReuse_ && textureReuse_,
              "native framebuffer and texture handle IDs are reclaimed and reused after disposal");

        // Keep these alive until the Game's derived members are destroyed.  Clean process exit
        // then covers target destruction followed by normal bgfx device teardown.
        constexpr int kLiveAtTeardown = 96;
        teardownTargets_.reserve(kLiveAtTeardown);
        for (int i = 0; i < kLiveAtTeardown; ++i)
            teardownTargets_.push_back(MakeTarget(device));
        Check(static_cast<int>(teardownTargets_.size()) == kLiveAtTeardown,
              std::to_string(kLiveAtTeardown) +
                  " never-bound targets remain live through normal device teardown");

        std::printf("=== REMED-GFX-179: %d/%d checks passed ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

protected:
    void Draw(const GameTime&) override
    {
        GraphicsDevice& device = getGraphicsDeviceProperty();
        switch (step_++)
        {
        case 0: RunOldLimitFrame(device); break;
        case 1: RunFirstFailureAndOrderingFrame(device); break;
        case 2: RunReadbackAndFrameReuse(device); break;
        case 3: RunReservedReadbackInteraction(device); break;
        case 4: RunBeyondBoundaryFrame(device); break;
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10: RunReclamationFrame(device); break;
        default: RunTeardownFrame(device); break;
        }
    }

public:
    BgfxGfx179ViewCapacityTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(kBackbufferSize);
        manager_->setPreferredBackBufferHeightProperty(kBackbufferSize);
    }

    int Result() const { return result_; }
};

int main()
{
    BgfxGfx179ViewCapacityTest game;
    game.Run();
    return game.Result();
}
