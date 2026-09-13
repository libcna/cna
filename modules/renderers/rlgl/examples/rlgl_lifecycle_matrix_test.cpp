// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-058: presentation reset versus native loss, event/status order,
// create/bind failure rollback, and the remaining public-resource disposal contracts.

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceStatus.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/ObjectDisposedException.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    namespace Rlgl = CNA::Internal::Renderers::Rlgl;

    void SetEnvironment(const char* const name, const char* const value)
    {
#if defined(_WIN32)
        (void)_putenv_s(name, value != nullptr ? value : "");
#else
        if (value != nullptr)
            (void)setenv(name, value, 1);
        else
            (void)unsetenv(name);
#endif
    }

    struct EventObservation
    {
        std::string name;
        GraphicsDeviceStatus status = GraphicsDeviceStatus::Normal;

        [[nodiscard]] bool operator==(const EventObservation&) const = default;
    };

    template <typename TOperation>
    [[nodiscard]] bool ThrowsDisposed(TOperation&& operation)
    {
        try
        {
            std::forward<TOperation>(operation)();
        }
        catch (const System::ObjectDisposedException&)
        {
            return true;
        }
        catch (...)
        {
        }
        return false;
    }

    class CreationFailureGame final : public Game
    {
    public:
        CreationFailureGame()
            : graphics_(std::make_unique<GraphicsDeviceManager>(this))
        {
            graphics_->setPreferredBackBufferWidthProperty(32);
            graphics_->setPreferredBackBufferHeightProperty(32);
            graphics_->setPreferredPresentationModeProperty(
                PresentationMode::NativeBackBuffer);
            graphics_->setSynchronizeWithVerticalRetraceProperty(false);
        }

    protected:
        void Draw(const GameTime&) override
        {
            Exit();
        }

    private:
        std::unique_ptr<GraphicsDeviceManager> graphics_;
    };

    class LifecycleMatrixGame final : public CNA::Examples::PixelTestGame
    {
    public:
        LifecycleMatrixGame()
            : graphics_(std::make_unique<GraphicsDeviceManager>(this))
        {
            graphics_->setPreferredBackBufferWidthProperty(64);
            graphics_->setPreferredBackBufferHeightProperty(48);
            graphics_->setPreferredPresentationModeProperty(
                PresentationMode::NativeBackBuffer);
            graphics_->setSynchronizeWithVerticalRetraceProperty(false);
        }

    protected:
        void RunTest() override
        {
            auto& device = getGraphicsDeviceProperty();
            auto& renderer = static_cast<Rlgl::RlglRenderer&>(device.GetRenderer());

            device.DeviceLost += [this, &device](System::Object*, const System::EventArgs&)
            {
                events_.push_back({"Lost", device.getGraphicsDeviceStatusProperty()});
            };
            device.DeviceResetting += [this, &device](System::Object*, const System::EventArgs&)
            {
                events_.push_back({"Resetting", device.getGraphicsDeviceStatusProperty()});
            };
            device.DeviceReset += [this, &device](System::Object*, const System::EventArgs&)
            {
                events_.push_back({"Reset", device.getGraphicsDeviceStatusProperty()});
            };

            RenderTarget2D target(device, 8, 8);
            int contentLostEvents = 0;
            target.ContentLost += [&contentLostEvents](System::Object*, const System::EventArgs&)
            {
                ++contentLostEvents;
            };

            const auto initial = renderer.GetContextRecoverySnapshotForTesting();
            const auto& initialParameters = device.getPresentationParametersProperty();
            Check(initial.state == Rlgl::RlglContextRecoveryState::Available &&
                    initial.contextGeneration == 1 &&
                    static_cast<int>(initialParameters.getBackBufferFormatProperty()) ==
                        initial.appliedBackBufferFormat &&
                    static_cast<int>(initialParameters.getDepthStencilFormatProperty()) ==
                        initial.appliedDepthStencilFormat &&
                    initialParameters.getMultiSampleCountProperty() ==
                        initial.appliedMultiSampleCount,
                "initial public presentation parameters report the granted framebuffer");
            std::printf("[INFO] granted framebuffer: color=%d depth=%d samples=%d robust=%s "
                        "native-reset-poll=%s\n",
                        initial.appliedBackBufferFormat,
                        initial.appliedDepthStencilFormat,
                        initial.appliedMultiSampleCount,
                        initial.robustContext ? "yes" : "no",
                        initial.nativeLossPollingAvailable ? "yes" : "no");

            PresentationParameters reset = initialParameters.Clone();
            reset.setBackBufferFormatProperty(SurfaceFormat::Bgr565);
            reset.setDepthStencilFormatProperty(DepthFormat::None);
            reset.setMultiSampleCountProperty(8);
            device.Reset(reset);

            const auto afterFirstReset = renderer.GetContextRecoverySnapshotForTesting();
            const auto& applied = device.getPresentationParametersProperty();
            const std::vector<EventObservation> ordinaryResetEvents{{
                {"Resetting", GraphicsDeviceStatus::Normal},
                {"Reset", GraphicsDeviceStatus::Normal}}};
            Check(events_ == ordinaryResetEvents &&
                    afterFirstReset.contextGeneration == initial.contextGeneration &&
                    afterFirstReset.presentationResets == initial.presentationResets + 1 &&
                    afterFirstReset.detectedNativeLosses == initial.detectedNativeLosses &&
                    static_cast<int>(applied.getBackBufferFormatProperty()) ==
                        initial.appliedBackBufferFormat &&
                    static_cast<int>(applied.getDepthStencilFormatProperty()) ==
                        initial.appliedDepthStencilFormat &&
                    applied.getMultiSampleCountProperty() == initial.appliedMultiSampleCount &&
                    contentLostEvents == 0 && !target.getIsContentLostProperty(),
                "ordinary Reset reports achieved formats without loss or context recreation");

            device.Reset(reset);
            const auto afterSecondReset = renderer.GetContextRecoverySnapshotForTesting();
            Check(events_.size() == 4 &&
                    events_[2] == ordinaryResetEvents[0] &&
                    events_[3] == ordinaryResetEvents[1] &&
                    afterSecondReset.contextGeneration == initial.contextGeneration &&
                    afterSecondReset.presentationResets == initial.presentationResets + 2 &&
                    contentLostEvents == 0,
                "repeated ordinary Reset remains presentation-only and deterministic");

            events_.clear();
            SetEnvironment("CNA_RLGL_DEBUG_FORCE_NATIVE_LOSS", "1");
            bool nativeLossThrew = false;
            try
            {
                renderer.Present();
            }
            catch (const std::exception&)
            {
                nativeLossThrew = true;
            }
            SetEnvironment("CNA_RLGL_DEBUG_FORCE_NATIVE_LOSS", nullptr);
            renderer.DebugSimulateContextLoss();
            const auto lost = renderer.GetContextRecoverySnapshotForTesting();
            Check(nativeLossThrew && !renderer.CanBeginDrawEXT() &&
                    lost.state == Rlgl::RlglContextRecoveryState::Lost &&
                    lost.detectedNativeLosses == initial.detectedNativeLosses + 1 &&
                    lost.unavailableReason.find("native OpenGL context loss") != std::string::npos &&
                    events_ == std::vector<EventObservation>({
                        {"Lost", GraphicsDeviceStatus::Lost}}) &&
                    contentLostEvents == 0,
                "detected native-loss path closes the gate and raises DeviceLost exactly once");

            renderer.DebugRestoreContext();
            const auto restored = renderer.GetContextRecoverySnapshotForTesting();
            const std::vector<EventObservation> nativeCycleEvents{{
                {"Lost", GraphicsDeviceStatus::Lost},
                {"Resetting", GraphicsDeviceStatus::NotReset},
                {"Reset", GraphicsDeviceStatus::Normal}}};
            Check(events_ == nativeCycleEvents && renderer.CanBeginDrawEXT() &&
                    restored.contextGeneration == initial.contextGeneration + 1 &&
                    restored.presentationResets == initial.presentationResets + 2 &&
                    contentLostEvents == 1 && target.getIsContentLostProperty(),
                "native recreation raises Lost/Resetting/Reset and ContentLost in XNA order");

            events_.clear();
            SetEnvironment("CNA_RLGL_DEBUG_FORCE_NATIVE_LOSS", "1");
            try { renderer.Present(); }
            catch (const std::exception&) {}
            SetEnvironment("CNA_RLGL_DEBUG_FORCE_NATIVE_LOSS", nullptr);
            renderer.DebugRestoreContext();
            const auto repeated = renderer.GetContextRecoverySnapshotForTesting();
            Check(events_ == nativeCycleEvents &&
                    repeated.contextGeneration == initial.contextGeneration + 2 &&
                    repeated.detectedNativeLosses == initial.detectedNativeLosses + 2 &&
                    contentLostEvents == 2,
                "a second detected loss/recreate cycle preserves event and content-loss semantics");

            SpriteBatch spriteBatch(device);
            spriteBatch.Dispose();
            spriteBatch.Dispose();
            Check(spriteBatch.getIsDisposedProperty() &&
                    ThrowsDisposed([&spriteBatch] { spriteBatch.Begin(); }) &&
                    ThrowsDisposed([&spriteBatch] { spriteBatch.End(); }),
                "SpriteBatch repeated disposal is safe and every stateful entry point rejects reuse");

            OcclusionQuery query(device);
            query.Dispose();
            query.Dispose();
            Check(query.getIsDisposedProperty() && !query.HasRenderer() &&
                    ThrowsDisposed([&query] { (void)query.getIsCompleteProperty(); }) &&
                    ThrowsDisposed([&query] { (void)query.getPixelCountProperty(); }) &&
                    ThrowsDisposed([&query] { (void)query.isPixelCountPreciseEXT(); }) &&
                    ThrowsDisposed([&query] { query.Begin(); }) &&
                    ThrowsDisposed([&query] { query.End(); }),
                "OcclusionQuery repeated disposal is safe and every operation rejects reuse");

            target.Dispose();
            target.Dispose();
            Check(target.getIsDisposedProperty(), "recreated target remains safely disposable twice");

            device.Clear(Color(23, 101, 211, 255));
            Color pixel;
            const Rectangle sample(32, 24, 1, 1);
            device.GetBackBufferData(&sample, &pixel, 0, 1);
            Check(pixel == Color(23, 101, 211, 255),
                "drawing and readback remain usable after repeated reset/loss/restore");
        }

    private:
        std::unique_ptr<GraphicsDeviceManager> graphics_;
        std::vector<EventObservation> events_;
    };

    [[nodiscard]] int ValidateCreationFailures()
    {
        constexpr std::array<const char*, 4> stages{{
            "before-context", "after-context", "after-bridge", "after-registry"}};
        int failures = 0;
        for (const char* const stage : stages)
        {
            SetEnvironment("CNA_RLGL_DEBUG_FAIL_CREATE_STAGE", stage);
            bool threwNamedFailure = false;
            try
            {
                CreationFailureGame game;
                game.Run();
            }
            catch (const std::exception& error)
            {
                threwNamedFailure = std::string(error.what()).find(stage) != std::string::npos;
            }
            SetEnvironment("CNA_RLGL_DEBUG_FAIL_CREATE_STAGE", nullptr);
            std::printf("[%s] injected create failure '%s' rolls back completely\n",
                        threwNamedFailure ? "PASS" : "FAIL", stage);
            if (!threwNamedFailure) ++failures;
        }
        return failures;
    }
}

int main()
{
    SetEnvironment("CNA_RLGL_DEBUG_FAIL_CREATE_STAGE", nullptr);
    SetEnvironment("CNA_RLGL_DEBUG_FORCE_NATIVE_LOSS", nullptr);
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    const int creationFailures = ValidateCreationFailures();
    const int lifecycleResult = CNA::Examples::RunPixelTest<LifecycleMatrixGame>();
    return creationFailures == 0 ? lifecycleResult : 1;
}
