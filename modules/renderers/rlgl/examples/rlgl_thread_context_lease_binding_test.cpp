// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-053: exact platform binding, nesting, exclusion, and thread-handover
// evidence for the standalone-rlgl context lease. No native window-toolkit API crosses this test.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformGlContext.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "common/PixelTestGame.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::RendererThreadContextLeaseRelease;

class RlglThreadContextLeaseBindingTest final : public Game
{
public:
    RlglThreadContextLeaseBindingTest()
        : graphics_(std::make_unique<GraphicsDeviceManager>(this))
    {
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(48);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    [[nodiscard]] int Result() const noexcept { return result_; }

protected:
    void Update(GameTime&) override
    {
        if (!contractRan_)
        {
            contractRan_ = true;
            try
            {
                RunBindingContract();
            }
            catch (const std::exception& error)
            {
                std::fprintf(stderr, "[FAIL] context-lease contract threw: %s\n", error.what());
                result_ = 1;
                Exit();
            }
            return;
        }

        if (drawObserved_ && !drawReported_)
        {
            drawReported_ = true;
            Check(true, "ordinary frame draw and present remain usable after thread handover");
            Exit();
        }
    }

    void Draw(const GameTime&) override
    {
        getGraphicsDeviceProperty().Clear(Color(31, 79, 149, 255));
        drawObserved_ = true;
    }

private:
    void Check(const bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) result_ = 1;
    }

    void RunBindingContract()
    {
        auto& renderer = getGraphicsDeviceProperty().GetRenderer();
        CNA::Platform::IPlatformGlContext* const glContext =
            GetPlatformEXT().GetGlContext();
        if (glContext == nullptr)
            throw std::runtime_error("RLGL test platform did not expose its GL service");

        CNA::Platform::GlContextBinding rendererBinding;
        {
            auto lease = renderer.AcquireThreadContextLeaseEXT();
            rendererBinding = glContext->GetCurrentBinding();
            Check(lease != nullptr && rendererBinding.context != nullptr,
                "lease binds the CNA-owned RLGL context");
        }

        glContext->MakeCurrent(rendererBinding.window, rendererBinding.context);
        {
            auto lease = renderer.AcquireThreadContextLeaseEXT(
                RendererThreadContextLeaseRelease::ReleaseRendererBinding);
            Check(glContext->GetCurrentBinding().context == rendererBinding.context,
                "frame-style outer lease owns the renderer context");
        }
        Check(glContext->GetCurrentBinding().context == nullptr,
            "frame-style lease releases its own previous renderer binding");

        CNA::Platform::GlContextDescription otherDescription;
        otherDescription.majorVersion = 3;
        otherDescription.minorVersion = 3;
        otherDescription.profile = CNA::Platform::GlProfile::Core;
        otherDescription.depthBits = 24;
        otherDescription.stencilBits = 8;
        otherDescription.doubleBuffer = true;
        CNA::Platform::GlContextHandle const otherContext =
            glContext->CreateContext(rendererBinding.window, otherDescription);
        try
        {
            glContext->MakeCurrent(rendererBinding.window, otherContext);
            {
                auto lease = renderer.AcquireThreadContextLeaseEXT(
                    RendererThreadContextLeaseRelease::ReleaseRendererBinding);
                Check(glContext->GetCurrentBinding().context == rendererBinding.context,
                    "lease temporarily replaces an unrelated caller binding");
            }
            Check(glContext->GetCurrentBinding().context == otherContext,
                "lease restores an unrelated binding even in release mode");
            glContext->MakeCurrent(rendererBinding.window, nullptr);
            glContext->DestroyContext(otherContext);
        }
        catch (...)
        {
            glContext->MakeCurrent(rendererBinding.window, nullptr);
            glContext->DestroyContext(otherContext);
            throw;
        }

        glContext->MakeCurrent(rendererBinding.window, rendererBinding.context);
        {
            auto outer = renderer.AcquireThreadContextLeaseEXT(
                RendererThreadContextLeaseRelease::ReleaseRendererBinding);
            {
                auto inner = renderer.AcquireThreadContextLeaseEXT(
                    RendererThreadContextLeaseRelease::RestorePreviousBinding);
                Check(glContext->GetCurrentBinding().context == rendererBinding.context,
                    "nested lease retains the one renderer binding");
            }
            Check(glContext->GetCurrentBinding().context == rendererBinding.context,
                "inner release does not unwind an outer lease");
        }
        Check(glContext->GetCurrentBinding().context == nullptr,
            "outer release policy controls the complete nested operation");

        glContext->MakeCurrent(rendererBinding.window, rendererBinding.context);
        {
            auto outer = renderer.AcquireThreadContextLeaseEXT(
                RendererThreadContextLeaseRelease::RestorePreviousBinding);
            auto inner = renderer.AcquireThreadContextLeaseEXT(
                RendererThreadContextLeaseRelease::ReleaseRendererBinding);
            inner.reset();
            outer.reset();
        }
        Check(glContext->GetCurrentBinding().context == rendererBinding.context,
            "an inner release request cannot override the outer restore policy");
        glContext->MakeCurrent(rendererBinding.window, nullptr);

        std::atomic<bool> workerStarted{false};
        std::atomic<bool> workerAcquired{false};
        bool workerHadContext = false;
        bool workerReleasedContext = false;
        std::exception_ptr workerError;
        auto mainLease = renderer.AcquireThreadContextLeaseEXT();
        std::thread worker([&]() {
            try
            {
                workerStarted.store(true, std::memory_order_release);
                {
                    auto lease = renderer.AcquireThreadContextLeaseEXT();
                    workerAcquired.store(true, std::memory_order_release);
                    workerHadContext =
                        glContext->GetCurrentBinding().context == rendererBinding.context;
                    renderer.Clear(0.25f, 0.5f, 0.75f, 1.0f);
                }
                workerReleasedContext = glContext->GetCurrentBinding().context == nullptr;
            }
            catch (...)
            {
                workerError = std::current_exception();
            }
        });

        while (!workerStarted.load(std::memory_order_acquire))
            std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        Check(!workerAcquired.load(std::memory_order_acquire),
            "a second thread cannot enter while the first lease is alive");
        mainLease.reset();
        worker.join();
        if (workerError != nullptr) std::rethrow_exception(workerError);
        Check(workerAcquired.load(std::memory_order_acquire) && workerHadContext,
            "the CNA context moves to the waiting worker after release");
        Check(workerReleasedContext,
            "the worker restores its previous unbound state");
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    bool contractRan_ = false;
    bool drawObserved_ = false;
    bool drawReported_ = false;
    int result_ = 0;
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    RlglThreadContextLeaseBindingTest game;
    game.Run();
    return game.Result();
}
