// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2254: modern-resource recovery across every recoverable swapchain
// result handled by Vulkan.
//
// A  Validation and the long-lived compute/image resources are live.
// B  Acquire OUT_OF_DATE recreates the swapchain without submitting or discarding queued modern
//    work, and leaves every resource handle unchanged.
// C  The next frame consumes that retained upload/dispatch and produces the exact image.
// D  Present SUBOPTIMAL recreates after consuming the frame and preserves its exact result and
//    resource identities.
// E  Present OUT_OF_DATE does the same.
// F  The three injected results cause exactly three recreations/device waits and only the three
//    successful frame submissions.
// G  Descriptor, pipeline and live-resource counts stay constant through every recreation.
// H  The complete matrix emits no Vulkan validation message.

#include "CNA/GraphicsImageAccess.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

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

class VulkanModernRecoveryTest final : public Game
{
    struct Handles
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

        [[nodiscard]] bool operator==(const Handles&) const = default;
    };

    std::unique_ptr<GraphicsDeviceManager> manager_;
    VulkanRenderer* renderer_ = nullptr;
    std::shared_ptr<VulkanStorageTexture2DRenderer> output_;
    std::unique_ptr<IComputeShaderRenderer> program_;
    VulkanComputeShaderRenderer* nativeProgram_ = nullptr;
    Handles handles_{};
    std::uint64_t recreateBefore_ = 0;
    std::uint64_t deviceWaitBefore_ = 0;
    std::uint64_t frameSubmitBefore_ = 0;
    std::size_t descriptorLiveBefore_ = 0;
    std::uint64_t descriptorCreatedBefore_ = 0;
    std::size_t pipelineLiveBefore_ = 0;
    std::uint64_t pipelineCreatedBefore_ = 0;
    std::size_t validationBefore_ = 0;
    int frame_ = 0;
    int passed_ = 0;
    int failed_ = 0;

    void Check(const bool condition, const std::string& label, const std::string& detail = {})
    {
        std::printf("[%s] %s%s%s\n", condition ? "PASS" : "FAIL", label.c_str(),
                    detail.empty() ? "" : ": ", detail.c_str());
        std::fflush(stdout);
        if (condition) ++passed_; else ++failed_;
    }

    [[nodiscard]] Handles CaptureHandles() const noexcept
    {
        return {output_->GetVkImageEXT(), output_->GetStorageImageViewEXT(),
                nativeProgram_->GetVkPipelineEXT(), nativeProgram_->GetVkDescriptorPoolEXT()};
    }

    void QueueExactWrite()
    {
        constexpr std::array<std::uint8_t, 4> zero{};
        if (!output_->SetData(0, 0, 0, 1, 1, zero.data(), zero.size()))
            throw std::runtime_error("MOD-2254 storage-image upload was refused");
        nativeProgram_->Bind();
        nativeProgram_->DispatchEXT(1, 1, 1);
    }

    [[nodiscard]] bool ReadIsExact()
    {
        std::array<std::uint8_t, 4> bytes{};
        return output_->GetData(0, 0, 0, 1, 1, bytes.data(), bytes.size()) &&
               bytes == std::array<std::uint8_t, 4>{64, 128, 191, 255};
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        renderer_ = dynamic_cast<VulkanRenderer*>(&device.GetRenderer());
        if (renderer_ == nullptr) throw std::runtime_error("MOD-2254 requires Vulkan");

        VulkanRenderer::SetSwapchainOutOfDateForTestEXT(0);
        VulkanRenderer::SetSwapchainPresentOutOfDateForTestEXT(0);
        VulkanRenderer::SetSwapchainPresentSuboptimalForTestEXT(0);
        output_ = Share<VulkanStorageTexture2DRenderer>(
            renderer_->CreateStorageTexture2DEXT(1, 1, 1, 0, UINT32_C(0x32)));
        program_ = renderer_->CreateComputeShader(
            Bytes(StorageImageWriteSpirV, sizeof(StorageImageWriteSpirV)));
        nativeProgram_ = dynamic_cast<VulkanComputeShaderRenderer*>(program_.get());
        if (output_ == nullptr || nativeProgram_ == nullptr || !nativeProgram_->IsValid() ||
            !nativeProgram_->BindStorageTexture2DEXT(
                0, output_, static_cast<int>(CNA::GraphicsImageAccess::WriteOnly)))
            throw std::runtime_error("MOD-2254 recovery resources were not created");

        handles_ = CaptureHandles();
        descriptorLiveBefore_ = renderer_->GetLiveComputeDescriptorSetCountEXT();
        descriptorCreatedBefore_ = renderer_->GetComputeDescriptorSetAllocationCountEXT();
        pipelineLiveBefore_ = renderer_->GetLiveComputePipelineCountEXT();
        pipelineCreatedBefore_ = renderer_->GetComputePipelineCreationCountEXT();
        validationBefore_ = renderer_->GetValidationMessagesEXT().size();
        Check(VulkanRenderer::IsValidationActiveEXT() && validationBefore_ == 0 &&
                  handles_.image != VK_NULL_HANDLE && handles_.view != VK_NULL_HANDLE &&
                  handles_.pipeline != VK_NULL_HANDLE && handles_.descriptorPool != VK_NULL_HANDLE,
              "A validation and long-lived modern resources are active");
    }

    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        switch (frame_)
        {
        case 0:
            device.Clear(Color::Black);
            break;
        case 1:
            recreateBefore_ = renderer_->GetSwapchainRecreateCountEXT();
            deviceWaitBefore_ = renderer_->GetDeviceWaitIdleCountEXT();
            frameSubmitBefore_ = renderer_->GetFrameSubmitCountEXT();
            QueueExactWrite();
            device.Clear(Color::CornflowerBlue);
            VulkanRenderer::SetSwapchainOutOfDateForTestEXT(1);
            break;
        case 2:
            Check(renderer_->GetSwapchainRecreateCountEXT() == recreateBefore_ + 1 &&
                      renderer_->GetFrameSubmitCountEXT() == frameSubmitBefore_ &&
                      renderer_->GetPendingModernCommandCountEXT() == 2 &&
                      CaptureHandles() == handles_,
                  "B acquire OUT_OF_DATE retains queued modern work and every native handle",
                  "pending=" + std::to_string(renderer_->GetPendingModernCommandCountEXT()));
            device.Clear(Color::Black);
            break;
        case 3:
            Check(renderer_->GetPendingModernCommandCountEXT() == 0 && ReadIsExact(),
                  "C the next frame consumes the retained upload/dispatch with exact output");
            QueueExactWrite();
            device.Clear(Color::CornflowerBlue);
            VulkanRenderer::SetSwapchainPresentSuboptimalForTestEXT(1);
            break;
        case 4:
            Check(renderer_->GetSwapchainRecreateCountEXT() == recreateBefore_ + 2 &&
                      renderer_->GetPendingModernCommandCountEXT() == 0 && ReadIsExact() &&
                      CaptureHandles() == handles_,
                  "D present SUBOPTIMAL recreates after exact modern work and preserves handles");
            QueueExactWrite();
            device.Clear(Color::Black);
            VulkanRenderer::SetSwapchainPresentOutOfDateForTestEXT(1);
            break;
        case 5:
            Check(renderer_->GetSwapchainRecreateCountEXT() == recreateBefore_ + 3 &&
                      renderer_->GetPendingModernCommandCountEXT() == 0 && ReadIsExact() &&
                      CaptureHandles() == handles_,
                  "E present OUT_OF_DATE recreates after exact modern work and preserves handles");
            Check(renderer_->GetDeviceWaitIdleCountEXT() == deviceWaitBefore_ + 3 &&
                      renderer_->GetFrameSubmitCountEXT() == frameSubmitBefore_ + 3,
                  "F three recovery results cause three recreations and three successful submits",
                  "waits=" + std::to_string(
                      renderer_->GetDeviceWaitIdleCountEXT() - deviceWaitBefore_) +
                      " submits=" + std::to_string(
                          renderer_->GetFrameSubmitCountEXT() - frameSubmitBefore_));
            Check(renderer_->GetLiveComputeDescriptorSetCountEXT() == descriptorLiveBefore_ &&
                      renderer_->GetComputeDescriptorSetAllocationCountEXT() ==
                          descriptorCreatedBefore_ &&
                      renderer_->GetLiveComputePipelineCountEXT() == pipelineLiveBefore_ &&
                      renderer_->GetComputePipelineCreationCountEXT() == pipelineCreatedBefore_ &&
                      renderer_->GetLiveStorageTexture2DCountEXT() == 1 &&
                      renderer_->GetLiveComputeShaderCountEXT() == 1,
                  "G descriptors, pipelines and modern live-resource counts stay constant");
            Check(renderer_->GetValidationMessagesEXT().size() == validationBefore_,
                  "H every recovery path is validation-clean");
            std::printf("=== MOD-2254 recovery result: %d/%d PASS ===\n",
                        passed_, passed_ + failed_);
            std::fflush(stdout);
            Exit();
            break;
        default:
            Exit();
            break;
        }
        ++frame_;
    }

public:
    VulkanModernRecoveryTest()
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
    VulkanModernRecoveryTest game;
    game.Run();
    return game.Result();
}
