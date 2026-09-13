// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-056: native context invalidation, retryable recreation, event order,
// and deterministic renderer-owned state.

#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglResourceLifetime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceStatus.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "RlglBridge.hpp"
#include "RlglResources.hpp"
#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    namespace Rlgl = CNA::Internal::Renderers::Rlgl;
    namespace Bridge = CNA::Internal::Renderers::Rlgl::Bridge;
    using CNA::Internal::Graphics::PositionColorStream;

    void SetFailureStage(const char* const stage)
    {
#if defined(_WIN32)
        (void)_putenv_s("CNA_RLGL_DEBUG_FAIL_RECREATE_STAGE", stage != nullptr ? stage : "");
#else
        if (stage != nullptr)
            (void)setenv("CNA_RLGL_DEBUG_FAIL_RECREATE_STAGE", stage, 1);
        else
            (void)unsetenv("CNA_RLGL_DEBUG_FAIL_RECREATE_STAGE");
#endif
    }

    [[nodiscard]] bool SameState(
        const Bridge::PipelineSnapshot& left,
        const Bridge::PipelineSnapshot& right)
    {
        return left.blendEnabled == right.blendEnabled &&
            left.colorSourceBlend == right.colorSourceBlend &&
            left.colorDestinationBlend == right.colorDestinationBlend &&
            left.alphaSourceBlend == right.alphaSourceBlend &&
            left.alphaDestinationBlend == right.alphaDestinationBlend &&
            left.colorBlendFunction == right.colorBlendFunction &&
            left.alphaBlendFunction == right.alphaBlendFunction &&
            left.colorWriteMasks == right.colorWriteMasks &&
            left.sampleMaskEnabled == right.sampleMaskEnabled &&
            left.sampleMask == right.sampleMask &&
            left.blendFactor == right.blendFactor &&
            left.depthTestEnabled == right.depthTestEnabled &&
            left.depthWriteEnabled == right.depthWriteEnabled &&
            left.depthFunction == right.depthFunction &&
            left.cullEnabled == right.cullEnabled &&
            (!left.cullEnabled || left.cullFace == right.cullFace) &&
            left.viewport == right.viewport && left.depthRange == right.depthRange &&
            left.scissorEnabled == right.scissorEnabled &&
            left.scissorBox == right.scissorBox &&
            left.polygonMode == right.polygonMode &&
            left.polygonOffsetFillEnabled == right.polygonOffsetFillEnabled &&
            left.polygonOffsetFactor == right.polygonOffsetFactor &&
            left.polygonOffsetUnits == right.polygonOffsetUnits;
    }

    [[nodiscard]] bool SameSampler(
        const Bridge::SamplerSnapshot& left,
        const Bridge::SamplerSnapshot& right)
    {
        return left.minFilter == right.minFilter &&
            left.magFilter == right.magFilter &&
            left.wrapS == right.wrapS && left.wrapT == right.wrapT &&
            left.wrapR == right.wrapR && left.compareMode == right.compareMode &&
            left.minLod == right.minLod && left.maxLod == right.maxLod &&
            left.lodBias == right.lodBias && left.anisotropy == right.anisotropy;
    }

    class ContextRecreateTest final : public CNA::Examples::PixelTestGame
    {
    public:
        ContextRecreateTest()
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
            const auto lifetime = renderer.GetResourceLifetimeForTesting();

            device.DeviceLost += [this](System::Object*, const System::EventArgs&)
            {
                events_.emplace_back("Lost");
            };
            device.DeviceResetting += [this](System::Object*, const System::EventArgs&)
            {
                events_.emplace_back("Resetting");
            };
            device.DeviceReset += [this](System::Object*, const System::EventArgs&)
            {
                events_.emplace_back("Reset");
            };

            CNA::Internal::Renderers::BlendWriteState writeState;
            writeState.colorWriteChannels[0] = 15;
            writeState.colorWriteChannels[1] = 7;
            writeState.colorWriteChannels[2] = 3;
            writeState.colorWriteChannels[3] = 1;
            writeState.multiSampleMask = 0x5A5A5A5Au;
            renderer.ApplyBlendState(4, 4, 5, 5, 0, 0, writeState);
            renderer.SetBlendFactor(0.25f, 0.5f, 0.75f, 1.0f);
            renderer.SetBlendEnabled(false);
            renderer.ApplyDepthStencilState(
                true, false, 6, false, 0, 0, 0, 0,
                0x7F, 0x3F, 11, false, 0, 0, 0, 0);
            renderer.SetReferenceStencil(37);
            renderer.ApplyRasterizerState(0, 1, true, 0.0f, 0.0f);
            renderer.SetViewport(3, 4, 40, 30, 0.2f, 0.8f);
            renderer.SetScissorRect(0, 0, 64, 48);
            renderer.ApplySamplerState(2, 1, 2, 1, 8);
            renderer.ApplySamplerMipState(2, 3, -0.5f);
            renderer.ApplySamplerAddressW(2, 2);
            auto spriteBatch = renderer.CreateSpriteBatch();

            const std::array<PositionColorStream, 3> triangle{{
                {-0.5f, -0.5f, 0.0f, 255, 0, 0, 255},
                {0.5f, -0.5f, 0.0f, 0, 255, 0, 255},
                {0.0f, 0.5f, 0.0f, 0, 0, 255, 255}}};
            auto vertexBuffer = renderer.CreateVertexBuffer(3);
            vertexBuffer->SetVertexDeclaration(
                VertexPositionColor::getVertexDeclarationStatic());
            vertexBuffer->SetData(
                triangle.data(), static_cast<int>(triangle.size()),
                sizeof(PositionColorStream));
            const Matrix identity = Matrix::getIdentityProperty();
            renderer.DrawColoredPrimitives(
                *vertexBuffer, identity, identity, identity,
                PrimitiveType::TriangleList, 1);

            const Bridge::PipelineSnapshot originalPipeline =
                Bridge::GetPipelineSnapshotForTesting();
            const auto originalContext = renderer.GetContextRecoverySnapshotForTesting();
            const Bridge::SamplerSnapshot originalSampler =
                Bridge::GetSamplerSnapshotForTesting(originalContext.samplerIds[2]);
            Check(renderer.CanBeginDrawEXT() &&
                    originalContext.state == Rlgl::RlglContextRecoveryState::Available &&
                    originalContext.contextGeneration == 1 &&
                    originalContext.rlglInitialized &&
                    originalContext.realizedSamplers == 1 &&
                    originalContext.liveSamplers == 1 &&
                    originalContext.primitivePipelineRealized &&
                    originalContext.primitivePipelineLive
#if defined(CNA_RLGL_COMPILED_EFFECTS)
                    && originalContext.mojoShaderContextRealized &&
                    originalContext.mojoShaderContextLive
#endif
                    ,
                "initial context exposes all realized renderer-owned state");

            renderer.DebugSimulateContextLoss();
            const auto lostContext = renderer.GetContextRecoverySnapshotForTesting();
            const auto lostLifetime = lifetime->GetSnapshotForTesting();
            const auto lostBuffer = Rlgl::GetBufferResourceSnapshotForTesting(*vertexBuffer);
            Check(!renderer.CanBeginDrawEXT() &&
                    lostContext.state == Rlgl::RlglContextRecoveryState::Lost &&
                    !lostContext.rlglInitialized && lostContext.liveSamplers == 0 &&
                    !lostContext.primitivePipelineLive && lostBuffer.id == 0 &&
                    lostLifetime.contextLossInvalidations == 1 &&
                    device.getGraphicsDeviceStatusProperty() == GraphicsDeviceStatus::Lost &&
                    events_ == std::vector<std::string>({"Lost"}),
                "loss closes drawing, invalidates native identities, and raises DeviceLost");

            renderer.DebugSimulateContextLoss();
            Check(events_.size() == 1 &&
                    lifetime->GetSnapshotForTesting().contextLossInvalidations == 1,
                "duplicate loss notification is idempotent");

            constexpr std::array<const char*, 4> failureStages{{
                "before-context", "after-context", "after-bridge", "after-state"}};
            for (const char* const stage : failureStages)
            {
                SetFailureStage(stage);
                bool threw = false;
                try
                {
                    renderer.DebugRestoreContext();
                }
                catch (const std::exception&)
                {
                    threw = true;
                }
                SetFailureStage(nullptr);
                const auto failed = renderer.GetContextRecoverySnapshotForTesting();
                Check(threw && !renderer.CanBeginDrawEXT() &&
                        failed.state == Rlgl::RlglContextRecoveryState::Unavailable &&
                        !failed.rlglInitialized && failed.contextGeneration == 1 &&
                        failed.unavailableReason.find(stage) != std::string::npos &&
                        device.getGraphicsDeviceStatusProperty() ==
                            GraphicsDeviceStatus::NotReset &&
                        events_.back() == "Resetting",
                    (std::string("injected '") + stage +
                        "' failure leaves one named unavailable state").c_str());
            }

            renderer.DebugRestoreContext();
            const auto restored = renderer.GetContextRecoverySnapshotForTesting();
            const Bridge::PipelineSnapshot restoredPipeline =
                Bridge::GetPipelineSnapshotForTesting();
            const Bridge::SamplerSnapshot restoredSampler =
                Bridge::GetSamplerSnapshotForTesting(restored.samplerIds[2]);
            Check(renderer.CanBeginDrawEXT() &&
                    restored.state == Rlgl::RlglContextRecoveryState::Available &&
                    restored.contextGeneration == 2 && restored.rlglInitialized &&
                    restored.liveSamplers == 1 && restored.primitivePipelineLive &&
#if defined(CNA_RLGL_COMPILED_EFFECTS)
                    restored.mojoShaderContextLive &&
#endif
                    restored.unavailableReason.empty() &&
                    device.getGraphicsDeviceStatusProperty() == GraphicsDeviceStatus::Normal,
                "retry creates one complete context and raises DeviceReset");
            const bool pipelineMatches = SameState(originalPipeline, restoredPipeline);
            const bool samplerMatches = SameSampler(originalSampler, restoredSampler);
            Check(pipelineMatches && samplerMatches,
                "renderer-owned pipeline, sampler, viewport, and scissor state is deterministic");

            const std::size_t eventsBeforeSecondCycle = events_.size();
            renderer.DebugSimulateContextLoss();
            renderer.DebugRestoreContext();
            const auto repeated = renderer.GetContextRecoverySnapshotForTesting();
            Check(repeated.contextGeneration == 3 && renderer.CanBeginDrawEXT() &&
                    lifetime->GetSnapshotForTesting().contextLossInvalidations == 2 &&
                    events_.size() == eventsBeforeSecondCycle + 3 &&
                    events_[eventsBeforeSecondCycle] == "Lost" &&
                    events_[eventsBeforeSecondCycle + 1] == "Resetting" &&
                    events_[eventsBeforeSecondCycle + 2] == "Reset",
                "a second complete loss/recreate cycle preserves exact event order");

            device.Clear(Color(19, 83, 197, 255));
            Color pixel;
            const Rectangle sample(32, 24, 1, 1);
            device.GetBackBufferData(&sample, &pixel, 0, 1);
            Check(pixel == Color(19, 83, 197, 255),
                "recreated rlgl core clears and reads the replacement back buffer");
        }

    private:
        std::unique_ptr<GraphicsDeviceManager> graphics_;
        std::vector<std::string> events_;
    };
}

int main()
{
    SetFailureStage(nullptr);
    return CNA::Examples::RunPixelTest<ContextRecreateTest>();
}
