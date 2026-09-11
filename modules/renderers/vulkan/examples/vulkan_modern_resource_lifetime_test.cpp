// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2252: one native lifetime matrix for every Vulkan modern resource.
//
// A  All implemented modern resource families expose live native records.
// B  Buffer compute, storage-image compute, render-target image compute, array transfer and a
//    timer range are accepted into one ordinary presented frame and produce exact results.
// C  A real swapchain resize preserves every resource identity and all paths remain usable.
// D  Destroying buffers, images, render targets and their programs after enqueue but before
//    Present leaves the internal records alive only until command recording consumes them,
//    including an ordinary Texture2D used through MOD-2244's legal storage-image bridge.
// E  Destroying an unsubmitted timer removes its queued events without a global wait.
// F  Explicit GraphicsDevice teardown releases and disconnects every externally retained record;
//    those records can then be destroyed after the VkDevice without dereferencing their owner.
// G  The complete matrix adds no Vulkan validation message.

#include "CNA/GraphicsImageAccess.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

using CNA::Internal::Renderers::IComputeShaderRenderer;
using CNA::Internal::Renderers::IGpuTimerRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanComputeShaderRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanGpuTimerRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanRenderTargetRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanStorageBufferRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanStorageTexture2DRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanTexture2DArrayRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanTextureRenderer;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GameTime;
using Microsoft::Xna::Framework::GraphicsDeviceManager;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    constexpr std::size_t ElementCount = 64;
    constexpr std::size_t BufferBytes = ElementCount * sizeof(float);

    // shaderc -O output for three std430 buffers and local_size_x=64:
    // values[gl_GlobalInvocationID.x] = a[i] + b[i].
    constexpr std::uint32_t VectorAddSpirV[] = {
        0x07230203u, 0x00010000u, 0x000d000bu, 0x0000002cu, 0x00000000u,
        0x00020011u, 0x00000001u, 0x0006000bu, 0x00000001u, 0x4c534c47u,
        0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u,
        0x00000001u, 0x0006000fu, 0x00000005u, 0x00000004u, 0x6e69616du,
        0x00000000u, 0x0000000bu, 0x00060010u, 0x00000004u, 0x00000011u,
        0x00000040u, 0x00000001u, 0x00000001u, 0x00040047u, 0x0000000bu,
        0x0000000bu, 0x0000001cu, 0x00040047u, 0x00000011u, 0x00000006u,
        0x00000004u, 0x00030047u, 0x00000012u, 0x00000003u, 0x00040048u,
        0x00000012u, 0x00000000u, 0x00000019u, 0x00050048u, 0x00000012u,
        0x00000000u, 0x00000023u, 0x00000000u, 0x00030047u, 0x00000014u,
        0x00000019u, 0x00040047u, 0x00000014u, 0x00000021u, 0x00000002u,
        0x00040047u, 0x00000014u, 0x00000022u, 0x00000000u, 0x00040047u,
        0x00000018u, 0x00000006u, 0x00000004u, 0x00030047u, 0x00000019u,
        0x00000003u, 0x00040048u, 0x00000019u, 0x00000000u, 0x00000018u,
        0x00050048u, 0x00000019u, 0x00000000u, 0x00000023u, 0x00000000u,
        0x00030047u, 0x0000001bu, 0x00000018u, 0x00040047u, 0x0000001bu,
        0x00000021u, 0x00000000u, 0x00040047u, 0x0000001bu, 0x00000022u,
        0x00000000u, 0x00040047u, 0x00000020u, 0x00000006u, 0x00000004u,
        0x00030047u, 0x00000021u, 0x00000003u, 0x00040048u, 0x00000021u,
        0x00000000u, 0x00000018u, 0x00050048u, 0x00000021u, 0x00000000u,
        0x00000023u, 0x00000000u, 0x00030047u, 0x00000023u, 0x00000018u,
        0x00040047u, 0x00000023u, 0x00000021u, 0x00000001u, 0x00040047u,
        0x00000023u, 0x00000022u, 0x00000000u, 0x00040047u, 0x0000002bu,
        0x0000000bu, 0x00000019u, 0x00020013u, 0x00000002u, 0x00030021u,
        0x00000003u, 0x00000002u, 0x00040015u, 0x00000006u, 0x00000020u,
        0x00000000u, 0x00040017u, 0x00000009u, 0x00000006u, 0x00000003u,
        0x00040020u, 0x0000000au, 0x00000001u, 0x00000009u, 0x0004003bu,
        0x0000000au, 0x0000000bu, 0x00000001u, 0x0004002bu, 0x00000006u,
        0x0000000cu, 0x00000000u, 0x00040020u, 0x0000000du, 0x00000001u,
        0x00000006u, 0x00030016u, 0x00000010u, 0x00000020u, 0x0003001du,
        0x00000011u, 0x00000010u, 0x0003001eu, 0x00000012u, 0x00000011u,
        0x00040020u, 0x00000013u, 0x00000002u, 0x00000012u, 0x0004003bu,
        0x00000013u, 0x00000014u, 0x00000002u, 0x00040015u, 0x00000015u,
        0x00000020u, 0x00000001u, 0x0004002bu, 0x00000015u, 0x00000016u,
        0x00000000u, 0x0003001du, 0x00000018u, 0x00000010u, 0x0003001eu,
        0x00000019u, 0x00000018u, 0x00040020u, 0x0000001au, 0x00000002u,
        0x00000019u, 0x0004003bu, 0x0000001au, 0x0000001bu, 0x00000002u,
        0x00040020u, 0x0000001du, 0x00000002u, 0x00000010u, 0x0003001du,
        0x00000020u, 0x00000010u, 0x0003001eu, 0x00000021u, 0x00000020u,
        0x00040020u, 0x00000022u, 0x00000002u, 0x00000021u, 0x0004003bu,
        0x00000022u, 0x00000023u, 0x00000002u, 0x0004002bu, 0x00000006u,
        0x00000029u, 0x00000040u, 0x0004002bu, 0x00000006u, 0x0000002au,
        0x00000001u, 0x0006002cu, 0x00000009u, 0x0000002bu, 0x00000029u,
        0x0000002au, 0x0000002au, 0x00050036u, 0x00000002u, 0x00000004u,
        0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x00050041u,
        0x0000000du, 0x0000000eu, 0x0000000bu, 0x0000000cu, 0x0004003du,
        0x00000006u, 0x0000000fu, 0x0000000eu, 0x00060041u, 0x0000001du,
        0x0000001eu, 0x0000001bu, 0x00000016u, 0x0000000fu, 0x0004003du,
        0x00000010u, 0x0000001fu, 0x0000001eu, 0x00060041u, 0x0000001du,
        0x00000025u, 0x00000023u, 0x00000016u, 0x0000000fu, 0x0004003du,
        0x00000010u, 0x00000026u, 0x00000025u, 0x00050081u, 0x00000010u,
        0x00000027u, 0x0000001fu, 0x00000026u, 0x00060041u, 0x0000001du,
        0x00000028u, 0x00000014u, 0x00000016u, 0x0000000fu, 0x0003003eu,
        0x00000028u, 0x00000027u, 0x000100fdu, 0x00010038u,
    };

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

class VulkanModernResourceLifetimeTest final : public Game
{
    struct Handles
    {
        std::array<VkBuffer, 3> buffers{};
        VkImage storageImage = VK_NULL_HANDLE;
        VkImageView storageView = VK_NULL_HANDLE;
        VkImage arrayImage = VK_NULL_HANDLE;
        VkImageView arrayView = VK_NULL_HANDLE;
        VkPipeline bufferPipeline = VK_NULL_HANDLE;
        VkDescriptorPool bufferPool = VK_NULL_HANDLE;
        VkPipeline imagePipeline = VK_NULL_HANDLE;
        VkDescriptorPool imagePool = VK_NULL_HANDLE;
        VkQueryPool timerPool = VK_NULL_HANDLE;
        VkImage bridgeImage = VK_NULL_HANDLE;
        VkImageView bridgeView = VK_NULL_HANDLE;

        [[nodiscard]] bool operator==(const Handles&) const = default;
    };

    std::unique_ptr<GraphicsDeviceManager> manager_;
    VulkanRenderer* renderer_ = nullptr;
    std::shared_ptr<VulkanStorageBufferRenderer> inputA_;
    std::shared_ptr<VulkanStorageBufferRenderer> inputB_;
    std::shared_ptr<VulkanStorageBufferRenderer> output_;
    std::shared_ptr<VulkanStorageTexture2DRenderer> storage_;
    std::shared_ptr<VulkanTexture2DArrayRenderer> array_;
    std::unique_ptr<IComputeShaderRenderer> bufferProgram_;
    std::unique_ptr<IComputeShaderRenderer> imageProgram_;
    VulkanComputeShaderRenderer* bufferNative_ = nullptr;
    VulkanComputeShaderRenderer* imageNative_ = nullptr;
    std::unique_ptr<IGpuTimerRenderer> timer_;
    VulkanGpuTimerRenderer* timerNative_ = nullptr;
    std::unique_ptr<RenderTarget2D> bridgePublic_;
    std::shared_ptr<VulkanRenderTargetRenderer> bridge_;
    Handles handles_{};
    std::size_t validationBefore_ = 0;
    int passed_ = 0;
    int failed_ = 0;
    bool done_ = false;

    void Check(const bool condition, const std::string& label, const std::string& detail = {})
    {
        std::printf("[%s] %s%s%s\n", condition ? "PASS" : "FAIL", label.c_str(),
                    detail.empty() ? "" : ": ", detail.c_str());
        std::fflush(stdout);
        if (condition) ++passed_; else ++failed_;
    }

    [[nodiscard]] Handles CaptureHandles() const
    {
        Handles result;
        result.buffers = {inputA_->GetBufferEXT(), inputB_->GetBufferEXT(),
                          output_->GetBufferEXT()};
        result.storageImage = storage_->GetVkImageEXT();
        result.storageView = storage_->GetStorageImageViewEXT();
        result.arrayImage = array_->GetVkImageEXT();
        result.arrayView = array_->GetVkArrayImageView();
        result.bufferPipeline = bufferNative_->GetVkPipelineEXT();
        result.bufferPool = bufferNative_->GetVkDescriptorPoolEXT();
        result.imagePipeline = imageNative_->GetVkPipelineEXT();
        result.imagePool = imageNative_->GetVkDescriptorPoolEXT();
        result.timerPool = timerNative_ != nullptr
            ? timerNative_->GetVkQueryPoolEXT() : VK_NULL_HANDLE;
        result.bridgeImage = bridge_->GetVkImageEXT();
        result.bridgeView = bridge_->GetStorageImageViewEXT();
        return result;
    }

    void CreateLongLived(GraphicsDevice& device)
    {
        inputA_ = Share<VulkanStorageBufferRenderer>(renderer_->CreateStorageBuffer(BufferBytes));
        inputB_ = Share<VulkanStorageBufferRenderer>(renderer_->CreateStorageBuffer(BufferBytes));
        output_ = Share<VulkanStorageBufferRenderer>(renderer_->CreateStorageBuffer(BufferBytes));
        storage_ = Share<VulkanStorageTexture2DRenderer>(
            renderer_->CreateStorageTexture2DEXT(1, 1, 1, 0, UINT32_C(0x32)));
        array_ = Share<VulkanTexture2DArrayRenderer>(
            renderer_->CreateTexture2DArrayEXT(2, 2, 2, 1, 0, UINT32_C(0x0D)));
        bufferProgram_ = renderer_->CreateComputeShader(Bytes(VectorAddSpirV,
                                                               sizeof(VectorAddSpirV)));
        imageProgram_ = renderer_->CreateComputeShader(Bytes(StorageImageWriteSpirV,
                                                              sizeof(StorageImageWriteSpirV)));
        bufferNative_ = dynamic_cast<VulkanComputeShaderRenderer*>(bufferProgram_.get());
        imageNative_ = dynamic_cast<VulkanComputeShaderRenderer*>(imageProgram_.get());
        timer_ = renderer_->CreateGpuTimerEXT();
        timerNative_ = dynamic_cast<VulkanGpuTimerRenderer*>(timer_.get());

        bridgePublic_ = std::make_unique<RenderTarget2D>(
            device, 1, 1, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::DiscardContents);
        bridge_ = std::dynamic_pointer_cast<VulkanRenderTargetRenderer>(
            bridgePublic_->GetRenderer().shared_from_this());
        if (inputA_ == nullptr || inputB_ == nullptr || output_ == nullptr ||
            storage_ == nullptr || array_ == nullptr || bufferNative_ == nullptr ||
            imageNative_ == nullptr || bridge_ == nullptr || !bufferNative_->IsValid() ||
            !imageNative_->IsValid() || !bridge_->IsStorageImageCapableEXT())
        {
            throw std::runtime_error("one MOD-2252 native resource was not created");
        }
        handles_ = CaptureHandles();
        const bool handlesLive = handles_.buffers[0] != VK_NULL_HANDLE &&
            handles_.buffers[1] != VK_NULL_HANDLE && handles_.buffers[2] != VK_NULL_HANDLE &&
            handles_.storageImage != VK_NULL_HANDLE && handles_.storageView != VK_NULL_HANDLE &&
            handles_.arrayImage != VK_NULL_HANDLE && handles_.arrayView != VK_NULL_HANDLE &&
            handles_.bufferPipeline != VK_NULL_HANDLE && handles_.bufferPool != VK_NULL_HANDLE &&
            handles_.imagePipeline != VK_NULL_HANDLE && handles_.imagePool != VK_NULL_HANDLE &&
            handles_.bridgeImage != VK_NULL_HANDLE && handles_.bridgeView != VK_NULL_HANDLE &&
            (!renderer_->SupportsGpuTimerEXT() || handles_.timerPool != VK_NULL_HANDLE);
        Check(handlesLive, "A every implemented modern resource family owns a live native record");
    }

    void RunRound(GraphicsDevice& device, const int round)
    {
        std::array<float, ElementCount> a{};
        std::array<float, ElementCount> b{};
        std::array<float, ElementCount> out{};
        for (std::size_t i = 0; i < ElementCount; ++i)
        {
            a[i] = static_cast<float>(i + round);
            b[i] = static_cast<float>(100 + round * 10) - static_cast<float>(i) * 0.25f;
        }
        inputA_->SetData(a.data(), BufferBytes);
        inputB_->SetData(b.data(), BufferBytes);
        output_->SetData(out.data(), BufferBytes);
        bufferNative_->Bind();
        bufferNative_->BindStorageBuffer(0, inputA_.get());
        bufferNative_->BindStorageBuffer(1, inputB_.get());
        bufferNative_->BindStorageBuffer(2, output_.get());
        bufferNative_->DispatchEXT(1, 1, 1);

        const std::array<std::uint8_t, 4> zero{};
        if (!storage_->SetData(0, 0, 0, 1, 1, zero.data(), zero.size()))
            throw std::runtime_error("the storage-image upload was refused");
        imageNative_->Bind();
        if (!imageNative_->BindStorageTexture2DEXT(
                0, storage_, static_cast<int>(CNA::GraphicsImageAccess::WriteOnly)))
            throw std::runtime_error("the storage-image binding was refused");
        imageNative_->DispatchEXT(1, 1, 1);
        imageNative_->Bind();
        imageNative_->BindImageTexture(
            0, bridge_.get(), static_cast<int>(CNA::GraphicsImageAccess::WriteOnly));
        imageNative_->DispatchEXT(1, 1, 1);

        std::array<std::uint8_t, 16> layer{};
        for (std::size_t i = 0; i < layer.size(); i += 4)
        {
            layer[i] = static_cast<std::uint8_t>(20 + round);
            layer[i + 1] = static_cast<std::uint8_t>(80 + round);
            layer[i + 2] = static_cast<std::uint8_t>(140 + round);
            layer[i + 3] = 255;
        }
        if (!array_->SetData(1, 0, 0, 0, 2, 2, layer.data(), layer.size()))
            throw std::runtime_error("the array upload was refused");

        if (timer_ != nullptr) timer_->Begin();
        device.Clear(round == 0 ? Color::Blue : Color::Green);
        if (timer_ != nullptr) timer_->End();
        const std::size_t pendingBeforePresent = renderer_->GetPendingModernCommandCountEXT();
        device.Present();

        output_->GetData(out.data(), BufferBytes);
        bool bufferExact = true;
        for (std::size_t i = 0; i < ElementCount; ++i)
            bufferExact = bufferExact && out[i] == a[i] + b[i];
        std::array<std::uint8_t, 4> storageBytes{};
        if (!storage_->GetData(
                0, 0, 0, 1, 1, storageBytes.data(), storageBytes.size()))
            throw std::runtime_error("the storage-image readback was refused");
        std::array<std::uint8_t, 16> arrayBytes{};
        if (!array_->GetData(1, 0, 0, 0, 2, 2, arrayBytes.data(), arrayBytes.size()))
            throw std::runtime_error("the array readback was refused");
        Check(bufferExact && storageBytes == std::array<std::uint8_t, 4>{64, 128, 191, 255} &&
                  arrayBytes == layer && pendingBeforePresent == 4 &&
                  renderer_->GetPendingModernCommandCountEXT() == 0,
              round == 0
                  ? "B one presented frame consumes every modern command with exact results"
                  : "C all modern paths remain exact after the real swapchain resize",
              "pending=" + std::to_string(pendingBeforePresent));
    }

    void ExerciseEarlyDestruction(GraphicsDevice& device)
    {
        const std::size_t buffersBefore = renderer_->GetLiveStorageBufferCountEXT();
        const std::size_t programsBefore = renderer_->GetLiveComputeShaderCountEXT();
        auto a = Share<VulkanStorageBufferRenderer>(renderer_->CreateStorageBuffer(BufferBytes));
        auto b = Share<VulkanStorageBufferRenderer>(renderer_->CreateStorageBuffer(BufferBytes));
        auto out = Share<VulkanStorageBufferRenderer>(renderer_->CreateStorageBuffer(BufferBytes));
        std::array<float, ElementCount> zeros{};
        a->SetData(zeros.data(), BufferBytes);
        b->SetData(zeros.data(), BufferBytes);
        out->SetData(zeros.data(), BufferBytes);
        auto program = renderer_->CreateComputeShader(Bytes(VectorAddSpirV, sizeof(VectorAddSpirV)));
        auto* native = dynamic_cast<VulkanComputeShaderRenderer*>(program.get());
        native->Bind();
        native->BindStorageBuffer(0, a.get());
        native->BindStorageBuffer(1, b.get());
        native->BindStorageBuffer(2, out.get());
        native->DispatchEXT(1, 1, 1);
        program.reset();
        a.reset();
        b.reset();
        out.reset();
        const bool buffersRetained =
            renderer_->GetLiveComputeShaderCountEXT() == programsBefore &&
            renderer_->GetLiveStorageBufferCountEXT() == buffersBefore + 3;
        device.Present();
        const bool buffersConsumed =
            renderer_->GetLiveStorageBufferCountEXT() == buffersBefore;

        const std::size_t imagesBefore = renderer_->GetLiveStorageTexture2DCountEXT();
        auto image = Share<VulkanStorageTexture2DRenderer>(
            renderer_->CreateStorageTexture2DEXT(1, 1, 1, 0, UINT32_C(0x02)));
        auto imageProgram = renderer_->CreateComputeShader(
            Bytes(StorageImageWriteSpirV, sizeof(StorageImageWriteSpirV)));
        auto* imageShader = dynamic_cast<VulkanComputeShaderRenderer*>(imageProgram.get());
        imageShader->Bind();
        if (!imageShader->BindStorageTexture2DEXT(
                0, image, static_cast<int>(CNA::GraphicsImageAccess::WriteOnly)))
            throw std::runtime_error("the ephemeral storage-image binding was refused");
        imageShader->DispatchEXT(1, 1, 1);
        imageProgram.reset();
        image.reset();
        const bool imageRetained =
            renderer_->GetLiveStorageTexture2DCountEXT() == imagesBefore + 1;
        device.Present();
        const bool imageConsumed =
            renderer_->GetLiveStorageTexture2DCountEXT() == imagesBefore;

        const std::size_t targetsBefore = renderer_->GetLiveRenderTargetCountEXT();
        auto target = std::make_unique<RenderTarget2D>(
            device, 1, 1, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::DiscardContents);
        imageNative_->Bind();
        imageNative_->BindImageTexture(
            0, &target->GetRenderer(), static_cast<int>(CNA::GraphicsImageAccess::WriteOnly));
        imageNative_->DispatchEXT(1, 1, 1);
        target->Dispose();
        target.reset();
        imageNative_->BindImageTexture(
            0, bridge_.get(), static_cast<int>(CNA::GraphicsImageAccess::WriteOnly));
        const bool targetRetained =
            renderer_->GetLiveRenderTargetCountEXT() == targetsBefore + 1;
        device.Present();
        const bool targetConsumed =
            renderer_->GetLiveRenderTargetCountEXT() == targetsBefore;

        auto ordinaryPublic = std::make_unique<Texture2D>(
            device, 1, 1, false, SurfaceFormat::Color);
        const std::array<std::uint8_t, 4> ordinaryBytes{1, 2, 3, 255};
        ordinaryPublic->SetDataRGBA(ordinaryBytes.data(), 4);
        auto ordinary = std::dynamic_pointer_cast<VulkanTextureRenderer>(
            ordinaryPublic->GetRenderer().shared_from_this());
        if (ordinary == nullptr || !ordinary->IsStorageImageCapableEXT())
            throw std::runtime_error("the ordinary texture storage-image bridge was unavailable");
        std::weak_ptr<VulkanTextureRenderer> ordinaryWeak = ordinary;
        imageNative_->Bind();
        imageNative_->BindImageTexture(
            0, ordinary.get(), static_cast<int>(CNA::GraphicsImageAccess::WriteOnly));
        imageNative_->DispatchEXT(1, 1, 1);
        imageNative_->BindImageTexture(
            0, bridge_.get(), static_cast<int>(CNA::GraphicsImageAccess::WriteOnly));
        ordinaryPublic.reset();
        ordinary.reset();
        const bool ordinaryRetained = !ordinaryWeak.expired();
        device.Present();
        const bool ordinaryConsumed = ordinaryWeak.expired();

        Check(buffersRetained && buffersConsumed && imageRetained && imageConsumed &&
                  targetRetained && targetConsumed && ordinaryRetained && ordinaryConsumed,
              "D pre-present destruction retains records exactly through command recording",
              "buffers=" + std::to_string(buffersRetained) + "/" +
                  std::to_string(buffersConsumed) + " images=" +
                  std::to_string(imageRetained) + "/" + std::to_string(imageConsumed) +
                  " targets=" + std::to_string(targetRetained) + "/" +
                  std::to_string(targetConsumed) + " texture=" +
                  std::to_string(ordinaryRetained) + "/" +
                  std::to_string(ordinaryConsumed));

        const std::size_t timersBefore = renderer_->GetLiveGpuTimerCountEXT();
        auto abandonedTimer = renderer_->CreateGpuTimerEXT();
        if (abandonedTimer != nullptr)
        {
            abandonedTimer->Begin();
            device.Clear(Color::Black);
            abandonedTimer->End();
            abandonedTimer.reset();
        }
        const bool timerReleased = renderer_->GetLiveGpuTimerCountEXT() == timersBefore;
        device.Present();
        Check(timerReleased, "E an abandoned unsubmitted timer purges its events and retires once");
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        try
        {
            auto& device = getGraphicsDeviceProperty();
            renderer_ = dynamic_cast<VulkanRenderer*>(&device.GetRenderer());
            Check(renderer_ != nullptr, "A0 the live renderer is Vulkan");
            if (renderer_ == nullptr)
            {
                Exit();
                return;
            }
            validationBefore_ = renderer_->GetValidationMessagesEXT().size();
            CreateLongLived(device);
            RunRound(device, 0);

            const std::uint64_t recreatesBefore = renderer_->GetSwapchainRecreateCountEXT();
            manager_->setPreferredBackBufferWidthProperty(96);
            manager_->setPreferredBackBufferHeightProperty(80);
            manager_->ApplyChanges();
            const Handles afterResize = CaptureHandles();
            Check(afterResize == handles_ &&
                      renderer_->GetSwapchainRecreateCountEXT() > recreatesBefore,
                  "C resize recreates only the swapchain and preserves every modern handle",
                  std::to_string(recreatesBefore) + " -> " +
                      std::to_string(renderer_->GetSwapchainRecreateCountEXT()));
            RunRound(device, 1);
            ExerciseEarlyDestruction(device);
            Check(renderer_->GetValidationMessagesEXT().size() == validationBefore_,
                  "G submit, resize and early destruction add no validation message",
                  std::to_string(validationBefore_) + " -> " +
                      std::to_string(renderer_->GetValidationMessagesEXT().size()));
        }
        catch (const std::exception& exception)
        {
            Check(false, "MOD-2252 lifetime matrix completed without exception", exception.what());
        }
        Exit();
    }

public:
    VulkanModernResourceLifetimeTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
        manager_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    void TearDownAndVerify()
    {
        getGraphicsDeviceProperty().Dispose();
        const auto bufferReleased = [](const auto& buffer) {
            return buffer != nullptr && !buffer->HasOwnerEXT() &&
                   buffer->GetBufferEXT() == VK_NULL_HANDLE && !buffer->IsMappedEXT();
        };
        const bool released = bufferReleased(inputA_) && bufferReleased(inputB_) &&
            bufferReleased(output_) && storage_ != nullptr && !storage_->HasOwnerEXT() &&
            storage_->GetVkImageEXT() == VK_NULL_HANDLE &&
            storage_->GetStorageImageViewEXT() == VK_NULL_HANDLE && array_ != nullptr &&
            !array_->HasOwnerEXT() && array_->GetVkImageEXT() == VK_NULL_HANDLE &&
            array_->GetVkArrayImageView() == VK_NULL_HANDLE && bufferNative_ != nullptr &&
            !bufferNative_->HasOwnerEXT() && !bufferNative_->IsValid() &&
            bufferNative_->GetVkDescriptorPoolEXT() == VK_NULL_HANDLE && imageNative_ != nullptr &&
            !imageNative_->HasOwnerEXT() && !imageNative_->IsValid() &&
            imageNative_->GetVkDescriptorPoolEXT() == VK_NULL_HANDLE && bridge_ != nullptr &&
            !bridge_->HasOwnerEXT() && bridge_->GetVkImageEXT() == VK_NULL_HANDLE &&
            bridge_->GetStorageImageViewEXT() == VK_NULL_HANDLE &&
            (timerNative_ == nullptr ||
             (!timerNative_->HasOwnerEXT() &&
              timerNative_->GetVkQueryPoolEXT() == VK_NULL_HANDLE));
        Check(released,
              "F device teardown releases and disconnects every surviving modern record");

        timer_.reset();
        bufferProgram_.reset();
        imageProgram_.reset();
        bridgePublic_.reset();
        bridge_.reset();
        inputA_.reset();
        inputB_.reset();
        output_.reset();
        storage_.reset();
        array_.reset();
        Check(true, "F disconnected records are safely destructible after their VkDevice");
    }

    void RecordTeardownFailure(const std::string& detail)
    {
        Check(false, "F explicit device teardown completed", detail);
    }

    void PrintSummary() const
    {
        std::printf("=== MOD-2252 result: %d/%d PASS ===\n",
                    passed_, passed_ + failed_);
    }

    [[nodiscard]] int Result() const noexcept { return failed_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanModernResourceLifetimeTest game;
    game.Run();
    try
    {
        game.TearDownAndVerify();
        game.Dispose();
    }
    catch (const std::exception& exception)
    {
        game.RecordTeardownFailure(exception.what());
    }
    game.PrintSummary();
    return game.Result();
}
