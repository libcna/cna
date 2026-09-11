// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2253: long-running bounded-allocation and retirement gate.

#include "CNA/GraphicsImageAccess.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

using CNA::Internal::Renderers::IComputeShaderRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanComputeShaderRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanStorageTexture2DRenderer;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GameTime;
using Microsoft::Xna::Framework::GraphicsDeviceManager;

namespace
{
    constexpr int StressFrames = 2048;
    constexpr int ResizePeriod = 64;
    constexpr int ExpectedResizeRequests = StressFrames / ResizePeriod;
    // ApplyChanges adopts the changed native surface and then the changed virtual resolution.
    constexpr int ExpectedSwapchainRecreates = ExpectedResizeRequests * 2;
    constexpr int DrainFrames = 4;

    // One invocation writes rgba8 (0.25, 0.5, 0.75, 1.0) to image binding zero.
    constexpr std::uint32_t StorageImageWriteSpirV[] = {
        0x07230203, 0x00010000, 0x00000000, 0x00000014, 0x00000000,
        0x00020011, 0x00000001, 0x0003000e, 0x00000000, 0x00000001,
        0x0005000f, 0x00000005, 0x0000000a, 0x6e69616d, 0x00000000,
        0x00060010, 0x0000000a, 0x00000011, 0x00000001, 0x00000001, 0x00000001,
        0x00030047, 0x00000009, 0x00000019,
        0x00040047, 0x00000009, 0x00000021, 0x00000000,
        0x00040047, 0x00000009, 0x00000022, 0x00000000,
        0x00020013, 0x00000001, 0x00030021, 0x00000002, 0x00000001,
        0x00030016, 0x00000003, 0x00000020,
        0x00040017, 0x00000004, 0x00000003, 0x00000004,
        0x00040015, 0x00000005, 0x00000020, 0x00000001,
        0x00040017, 0x00000006, 0x00000005, 0x00000002,
        0x00090019, 0x00000007, 0x00000003, 0x00000001, 0x00000000,
                    0x00000000, 0x00000000, 0x00000002, 0x00000004,
        0x00040020, 0x00000008, 0x00000000, 0x00000007,
        0x0004003b, 0x00000008, 0x00000009, 0x00000000,
        0x0004002b, 0x00000003, 0x0000000b, 0x3e800000,
        0x0004002b, 0x00000003, 0x0000000c, 0x3f000000,
        0x0004002b, 0x00000003, 0x0000000d, 0x3f400000,
        0x0004002b, 0x00000003, 0x0000000e, 0x3f800000,
        0x0007002c, 0x00000004, 0x0000000f, 0x0000000b, 0x0000000c,
                    0x0000000d, 0x0000000e,
        0x0004002b, 0x00000005, 0x00000010, 0x00000000,
        0x0005002c, 0x00000006, 0x00000011, 0x00000010, 0x00000010,
        0x00050036, 0x00000001, 0x0000000a, 0x00000000, 0x00000002,
        0x000200f8, 0x00000012,
        0x0004003d, 0x00000007, 0x00000013, 0x00000009,
        0x00040063, 0x00000013, 0x00000011, 0x0000000f,
        0x000100fd, 0x00010038,
    };

    std::string Bytes(const std::uint32_t* words, const std::size_t byteCount)
    {
        return {reinterpret_cast<const char*>(words), byteCount};
    }

    template<typename Native, typename Base>
    std::shared_ptr<Native> Share(std::unique_ptr<Base> value)
    {
        std::shared_ptr<Base> base(std::move(value));
        return std::dynamic_pointer_cast<Native>(base);
    }
}

class VulkanModernAllocatorStressTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    VulkanRenderer* renderer_ = nullptr;
    std::unique_ptr<IComputeShaderRenderer> program_;
    VulkanComputeShaderRenderer* nativeProgram_ = nullptr;
    std::shared_ptr<VulkanStorageTexture2DRenderer> output_;
    std::size_t descriptorLiveBaseline_ = 0;
    std::uint64_t descriptorCreatedBaseline_ = 0;
    std::size_t pipelineLiveBaseline_ = 0;
    std::uint64_t pipelineCreatedBaseline_ = 0;
    std::uint64_t timerCreatedBaseline_ = 0;
    std::uint64_t deviceWaitBaseline_ = 0;
    std::uint64_t recreateBaseline_ = 0;
    std::size_t maxStagingLive_ = 0;
    std::size_t maxRetiredBuckets_ = 0;
    std::size_t maxRetiredHandles_ = 0;
    std::size_t maxTimerLive_ = 0;
    bool routineModernSubmissionStayedDeferred_ = true;
    int frame_ = 0;
    int drain_ = 0;
    int passed_ = 0;
    int failed_ = 0;

    void Check(const bool condition, const std::string& label, const std::string& detail = {})
    {
        std::printf("[%s] %s%s%s\n", condition ? "PASS" : "FAIL", label.c_str(),
                    detail.empty() ? "" : ": ", detail.c_str());
        if (condition) ++passed_; else ++failed_;
    }

    void Finish()
    {
        std::array<std::uint8_t, 4> bytes{};
        const bool readback = output_->GetData(0, 0, 0, 1, 1, bytes.data(), bytes.size());
        const std::uint64_t recreates =
            renderer_->GetSwapchainRecreateCountEXT() - recreateBaseline_;
        const std::uint64_t deviceWaits =
            renderer_->GetDeviceWaitIdleCountEXT() - deviceWaitBaseline_;
        const std::uint64_t timersCreated =
            renderer_->GetGpuTimerQueryPoolCreateCountEXT() - timerCreatedBaseline_;

        Check(frame_ == StressFrames && recreates == ExpectedSwapchainRecreates,
              "A 2048 presented frames include 32 real resize requests",
              "frames=" + std::to_string(frame_) + " resize=" + std::to_string(recreates));
        Check(routineModernSubmissionStayedDeferred_ && deviceWaits == recreates,
              "B routine compute/upload/allocation performs no queue/device-wide stall",
              "device waits=" + std::to_string(deviceWaits));
        Check(renderer_->GetLiveComputeDescriptorSetCountEXT() == descriptorLiveBaseline_ &&
                  renderer_->GetComputeDescriptorSetAllocationCountEXT() ==
                      descriptorCreatedBaseline_ &&
                  renderer_->GetLiveComputePipelineCountEXT() == pipelineLiveBaseline_ &&
                  renderer_->GetComputePipelineCreationCountEXT() == pipelineCreatedBaseline_,
              "C descriptor and compute-pipeline allocation counts stay constant");
        Check(renderer_->GetModernStagingAllocationCountEXT() == StressFrames &&
                  maxStagingLive_ <= 4 &&
                  renderer_->GetLiveModernStagingAllocationCountEXT() == 0,
              "D 2048 immutable staging allocations stay in a four-frame window and drain",
              "max live=" + std::to_string(maxStagingLive_));
        Check((!renderer_->SupportsGpuTimerEXT() || timersCreated == StressFrames) &&
                  maxTimerLive_ <= 1 && renderer_->GetLiveGpuTimerCountEXT() == 0,
              "E timer query pools retire without live-count growth",
              "created=" + std::to_string(timersCreated) +
                  " max live=" + std::to_string(maxTimerLive_));
        Check(maxRetiredBuckets_ <= 20 && maxRetiredHandles_ <= 40 &&
                  renderer_->GetPendingRetiredResourceBucketCountEXT() == 0 &&
                  renderer_->GetPendingRetiredNativeHandleCountEXT() == 0,
              "F retirement buckets and native handles remain bounded and fully drain",
              "max=" + std::to_string(maxRetiredBuckets_) + "/" +
                  std::to_string(maxRetiredHandles_));
        Check(renderer_->GetLiveStorageTexture2DCountEXT() == 1 &&
                  renderer_->GetLiveStorageBufferCountEXT() == 0,
              "G temporary modern records leave only the one intentional output alive");
        Check(readback && bytes == std::array<std::uint8_t, 4>{64, 128, 191, 255} &&
                  renderer_->GetValidationMessagesEXT().empty(),
              "H final compute result is exact and the stress run is validation-clean");
        std::printf("=== MOD-2253 result: %d/%d PASS ===\n", passed_, passed_ + failed_);
        Exit();
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        renderer_ = dynamic_cast<VulkanRenderer*>(&device.GetRenderer());
        Check(renderer_ != nullptr && VulkanRenderer::IsValidationActiveEXT(),
              "A0 Vulkan renderer and validation layer are active");
        if (renderer_ == nullptr)
        {
            Exit();
            return;
        }

        output_ = Share<VulkanStorageTexture2DRenderer>(
            renderer_->CreateStorageTexture2DEXT(1, 1, 1, 0, UINT32_C(0x32)));
        program_ = renderer_->CreateComputeShader(
            Bytes(StorageImageWriteSpirV, sizeof(StorageImageWriteSpirV)));
        nativeProgram_ = dynamic_cast<VulkanComputeShaderRenderer*>(program_.get());
        if (output_ == nullptr || nativeProgram_ == nullptr || !nativeProgram_->IsValid() ||
            !nativeProgram_->BindStorageTexture2DEXT(
                0, output_, static_cast<int>(CNA::GraphicsImageAccess::WriteOnly)))
            throw std::runtime_error("MOD-2253 stress resources were not created");

        descriptorLiveBaseline_ = renderer_->GetLiveComputeDescriptorSetCountEXT();
        descriptorCreatedBaseline_ = renderer_->GetComputeDescriptorSetAllocationCountEXT();
        pipelineLiveBaseline_ = renderer_->GetLiveComputePipelineCountEXT();
        pipelineCreatedBaseline_ = renderer_->GetComputePipelineCreationCountEXT();
        timerCreatedBaseline_ = renderer_->GetGpuTimerQueryPoolCreateCountEXT();
        deviceWaitBaseline_ = renderer_->GetDeviceWaitIdleCountEXT();
        recreateBaseline_ = renderer_->GetSwapchainRecreateCountEXT();
    }

    void Draw(const GameTime&) override
    {
        if (renderer_ == nullptr) return;
        auto& device = getGraphicsDeviceProperty();
        if (frame_ >= StressFrames)
        {
            device.Clear(Color::Black);
            if (++drain_ >= DrainFrames) Finish();
            return;
        }

        const std::uint64_t oneTimeBefore = renderer_->GetOneTimeCommandCountEXT();
        const std::uint64_t waitsBefore = renderer_->GetDeviceWaitIdleCountEXT();

        nativeProgram_->Bind();
        nativeProgram_->DispatchEXT(1, 1, 1);

        auto upload = Share<VulkanStorageTexture2DRenderer>(
            renderer_->CreateStorageTexture2DEXT(1, 1, 1, 0, UINT32_C(0x22)));
        const std::array<std::uint8_t, 4> zero{};
        if (upload == nullptr ||
            !upload->SetData(0, 0, 0, 1, 1, zero.data(), zero.size()))
            throw std::runtime_error("MOD-2253 stress upload was refused");
        upload.reset();

        auto transientBuffer = renderer_->CreateStorageBuffer(sizeof(std::uint32_t));
        if (transientBuffer == nullptr)
            throw std::runtime_error("MOD-2253 stress buffer was refused");
        transientBuffer.reset();

        auto timer = renderer_->CreateGpuTimerEXT();
        if (timer != nullptr)
        {
            maxTimerLive_ = std::max(maxTimerLive_, renderer_->GetLiveGpuTimerCountEXT());
            timer->Begin();
            timer->End();
            timer.reset();
        }

        device.Clear((frame_ & 1) == 0 ? Color::Black : Color::CornflowerBlue);
        const bool resize = (frame_ + 1) % ResizePeriod == 0;
        if (resize)
        {
            const bool wide = ((frame_ + 1) / ResizePeriod & 1) != 0;
            manager_->setPreferredBackBufferWidthProperty(wide ? 96 : 64);
            manager_->setPreferredBackBufferHeightProperty(wide ? 80 : 64);
            manager_->ApplyChanges();
        }
        else
        {
            routineModernSubmissionStayedDeferred_ =
                routineModernSubmissionStayedDeferred_ &&
                renderer_->GetOneTimeCommandCountEXT() == oneTimeBefore &&
                renderer_->GetDeviceWaitIdleCountEXT() == waitsBefore;
        }

        maxStagingLive_ = std::max(
            maxStagingLive_, renderer_->GetLiveModernStagingAllocationCountEXT());
        maxRetiredBuckets_ = std::max(
            maxRetiredBuckets_, renderer_->GetPendingRetiredResourceBucketCountEXT());
        maxRetiredHandles_ = std::max(
            maxRetiredHandles_, renderer_->GetPendingRetiredNativeHandleCountEXT());
        ++frame_;
    }

public:
    VulkanModernAllocatorStressTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
        manager_->setSynchronizeWithVerticalRetraceProperty(false);
        setIsFixedTimeStepProperty(false);
    }

    [[nodiscard]] int Result() const noexcept { return failed_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanModernAllocatorStressTest game;
    game.Run();
    return game.Result();
}
