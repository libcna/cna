// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2241: public Vulkan ComputeShader + StorageBuffer conformance.
//
// A  The selected queue, public capability, and all reported compute limits agree.
// B  Non-SPIR-V input is rejected with a useful compile error rather than reaching the driver.
// C  StorageBuffer's public CPU upload/readback round-trip preserves every element.
// D  A three-SSBO SPIR-V vector add dispatch produces every expected result.
// E  Binding/feature boundaries are explicit: out-of-range slots and pre-MOD-2242 scalar
//    metadata fail by name, while image binding remains reported unsupported until MOD-2244.
// F  The complete exercise produces no new Vulkan validation warnings or errors.

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using CNA::Graphics::ComputeShader;
using CNA::Graphics::StorageBufferT;
using CNA::GraphicsCapability;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GameTime;
using Microsoft::Xna::Framework::GraphicsDeviceManager;

namespace
{
    constexpr std::size_t kElementCount = 256;

    // shaderc -O output for:
    //   #version 450
    //   layout(local_size_x=64) in;
    //   layout(std430,set=0,binding=0) readonly buffer A { float a[]; };
    //   layout(std430,set=0,binding=1) readonly buffer B { float b[]; };
    //   layout(std430,set=0,binding=2) writeonly buffer C { float values[]; };
    //   void main(){ uint i=gl_GlobalInvocationID.x; values[i]=a[i]+b[i]; }
    constexpr uint32_t kVectorAddSpirV[] = {
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

    std::string VectorAddProgram()
    {
        return std::string(
            reinterpret_cast<const char*>(kVectorAddSpirV), sizeof(kVectorAddSpirV));
    }
}

class VulkanComputeStorageBufferTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void check(const bool condition, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", condition ? "PASS" : "FAIL",
                    label.c_str(), detail.c_str());
        std::fflush(stdout);
        if (condition) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto* renderer = dynamic_cast<VulkanRenderer*>(&device.GetRenderer());
        check(renderer != nullptr, "A native Vulkan renderer is active",
              renderer != nullptr ? "dynamic_cast succeeded" : "wrong renderer identity");
        if (renderer == nullptr) {
            Exit();
            return;
        }

        const bool queueCanCompute =
            (renderer->GetGraphicsQueueFlagsEXT() & VK_QUEUE_COMPUTE_BIT) != 0;
        const bool limitsPublished =
            device.GetMaxComputeWorkGroupCountEXT(0) > 0 &&
            device.GetMaxComputeWorkGroupCountEXT(1) > 0 &&
            device.GetMaxComputeWorkGroupCountEXT(2) > 0 &&
            device.GetMaxComputeWorkGroupSizeEXT(0) >= 64 &&
            device.GetMaxComputeWorkGroupInvocationsEXT() >= 64;
        check(queueCanCompute && limitsPublished &&
                  device.SupportsCapability(GraphicsCapability::ComputeShaders) &&
                  renderer->SupportsComputeShadersEXT(),
              "B native queue, public capability, and limits agree",
              "queueCompute=" + std::to_string(queueCanCompute) +
                  " maxGroupsX=" +
                  std::to_string(device.GetMaxComputeWorkGroupCountEXT(0)) +
                  " maxLocalX=" +
                  std::to_string(device.GetMaxComputeWorkGroupSizeEXT(0)) +
                  " maxInvocations=" +
                  std::to_string(device.GetMaxComputeWorkGroupInvocationsEXT()));
        if (!queueCanCompute || !limitsPublished ||
            !device.SupportsCapability(GraphicsCapability::ComputeShaders)) {
            std::printf("SKIP: selected Vulkan device cannot execute the 64-wide compute oracle\n");
            std::fflush(stdout);
            std::exit(77);
        }

        bool invalidRejected = false;
        std::string invalidMessage;
        try {
            ComputeShader invalid(device, "this is not SPIR-V");
        } catch (const std::runtime_error& error) {
            invalidMessage = error.what();
            invalidRejected = invalidMessage.find("SPIR-V") != std::string::npos;
        }
        check(invalidRejected, "C invalid shader bytes are refused before dispatch",
              invalidMessage.empty() ? "no exception" : invalidMessage);

        std::vector<float> a(kElementCount);
        std::vector<float> b(kElementCount);
        std::vector<float> expected(kElementCount);
        std::vector<float> sentinel(kElementCount, -777.0f);
        for (std::size_t i = 0; i < kElementCount; ++i) {
            a[i] = static_cast<float>(i) * 0.5f;
            b[i] = 1024.0f - static_cast<float>(i) * 0.25f;
            expected[i] = a[i] + b[i];
        }

        StorageBufferT<float> inputA(device, kElementCount);
        StorageBufferT<float> inputB(device, kElementCount);
        StorageBufferT<float> output(device, kElementCount);
        inputA.setData(a);
        inputB.setData(b);
        output.setData(sentinel);
        check(inputA.getData() == a && inputB.getData() == b && output.getData() == sentinel,
              "D public storage-buffer upload/readback preserves every input element",
              std::to_string(kElementCount) + " elements in each of three buffers");

        const std::size_t validationBefore = renderer->GetValidationMessagesEXT().size();
        ComputeShader vectorAdd(device, VectorAddProgram());
        vectorAdd.bindStorageBuffer(0, inputA.getBuffer());
        vectorAdd.bindStorageBuffer(1, inputB.getBuffer());
        vectorAdd.bindStorageBuffer(2, output.getBuffer());
        vectorAdd.dispatch(static_cast<int>(kElementCount / 64));
        const std::vector<float> actual = output.getData();
        std::size_t mismatch = kElementCount;
        for (std::size_t i = 0; i < kElementCount; ++i) {
            if (actual[i] != expected[i]) {
                mismatch = i;
                break;
            }
        }
        check(mismatch == kElementCount,
              "E SPIR-V dispatch computes A+B for every output element",
              mismatch == kElementCount
                  ? std::to_string(kElementCount) + "/" +
                        std::to_string(kElementCount) + " exact"
                  : "first mismatch at " + std::to_string(mismatch) +
                        ": actual=" + std::to_string(actual[mismatch]) +
                        " expected=" + std::to_string(expected[mismatch]));

        bool slotRejected = false;
        try {
            vectorAdd.bindStorageBuffer(4, inputA.getBuffer());
        } catch (const std::out_of_range&) {
            slotRejected = true;
        }
        bool scalarRejected = false;
        std::string scalarMessage;
        try {
            vectorAdd.setUniform("uCount", static_cast<int>(kElementCount));
        } catch (const std::runtime_error& error) {
            scalarMessage = error.what();
            scalarRejected = scalarMessage.find("MOD-2242") != std::string::npos;
        }
        bool missingBindingRejected = false;
        try {
            ComputeShader incomplete(device, VectorAddProgram());
            incomplete.bindStorageBuffer(0, inputA.getBuffer());
            incomplete.bindStorageBuffer(1, inputB.getBuffer());
            incomplete.dispatch(static_cast<int>(kElementCount / 64));
        } catch (const std::runtime_error& error) {
            missingBindingRejected =
                std::string(error.what()).find("binding 2") != std::string::npos;
        }
        check(slotRejected && scalarRejected && missingBindingRejected &&
                  !vectorAdd.isImageBindingSupported(),
              "F unimplemented or out-of-range bindings fail explicitly",
              "slot4=" + std::string(slotRejected ? "refused" : "accepted") +
                  " scalar=" + std::string(scalarRejected ? "refused" : "accepted") +
                  " missing2=" +
                  std::string(missingBindingRejected ? "refused" : "accepted") +
                  " imageSupport=" +
                  std::string(vectorAdd.isImageBindingSupported() ? "true" : "false") +
                  (scalarMessage.empty() ? "" : " message=" + scalarMessage));

        const std::size_t validationAfter = renderer->GetValidationMessagesEXT().size();
        check(VulkanRenderer::IsValidationActiveEXT() &&
                  validationAfter == validationBefore,
              "G compute/storage operations add no Vulkan validation messages",
              "layer=" +
                  std::string(VulkanRenderer::IsValidationActiveEXT() ? "active" : "inactive") +
                  " before=" + std::to_string(validationBefore) +
                  " after=" + std::to_string(validationAfter));

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanComputeStorageBufferTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }

    int Result() const noexcept { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanComputeStorageBufferTest game;
    game.Run();
    return game.Result();
}
