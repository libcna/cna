// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2229/MOD-2241/MOD-2242: public Vulkan compute/storage conformance.
//
// A  The selected queue, public capability, and all reported compute limits agree.
// B  Non-SPIR-V input is rejected with a useful compile error rather than reaching the driver.
// D  Stripped scalar-name metadata fails before native allocation.
// E  StorageBuffer's public CPU upload/readback round-trip preserves every element.
// F  A three-SSBO SPIR-V vector add dispatch produces every expected result.
// G  Unknown/mistyped names, undeclared/missing slots, and images fail explicitly.
// H  Reflected sparse SSBO slots and named int32/float32 push constants drive real output.
// I  Repeated dispatches reuse one descriptor set/layout and destruction reclaims both.
// J  Descriptor and pipeline-layout destruction returns live counts to baseline.
// K  Descriptor usage becomes exact VkBufferUsage and CPU-none memory is never mapped.
// L  Range upload/copy/readback crosses a GPU-only buffer byte-exactly.
// M  A GPU-only storage destination is written by compute and copied to CPU-readable staging.
// N  Invalid access/range/overlap requests fail before native mutation.
// O  Two copies from one source prove read/read barrier elision without losing either result.
// P  The complete exercise produces no new Vulkan validation warnings or errors.

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using CNA::Graphics::ComputeShader;
using CNA::Graphics::StorageBuffer;
using CNA::Graphics::StorageBufferCpuAccess;
using CNA::Graphics::StorageBufferDescriptor;
using CNA::Graphics::StorageBufferT;
using CNA::Graphics::StorageBufferUsage;
using CNA::GraphicsCapability;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
using CNA::Internal::Renderers::Vulkan::VulkanStorageBufferRenderer;
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

    // Unoptimized shaderc output keeps OpMemberName records, which are the Vulkan-side metadata
    // required by ComputeShader::setUniform(name, value). Its output buffer intentionally uses
    // sparse binding 7, proving that the descriptor layout follows SPIR-V rather than a fixed
    // four-slot public fiction:
    //   layout(push_constant) uniform Params { int uCount; float uScale; } params;
    //   values[i] = (a[i] + b[i]) * params.uScale; // only while i < params.uCount
    constexpr uint32_t kScaledVectorAddSpirV[] = {
        0x07230203u, 0x00010000u, 0x000d000bu, 0x0000003eu, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
        0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
        0x0006000fu, 0x00000005u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x0000000bu, 0x00060010u, 0x00000004u,
        0x00000011u, 0x00000040u, 0x00000001u, 0x00000001u, 0x00030003u, 0x00000002u, 0x000001c2u, 0x000a0004u,
        0x475f4c47u, 0x4c474f4fu, 0x70635f45u, 0x74735f70u, 0x5f656c79u, 0x656e696cu, 0x7269645fu, 0x69746365u,
        0x00006576u, 0x00080004u, 0x475f4c47u, 0x4c474f4fu, 0x6e695f45u, 0x64756c63u, 0x69645f65u, 0x74636572u,
        0x00657669u, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00030005u, 0x00000008u, 0x00000069u,
        0x00080005u, 0x0000000bu, 0x475f6c67u, 0x61626f6cu, 0x766e496cu, 0x7461636fu, 0x496e6f69u, 0x00000044u,
        0x00040005u, 0x00000013u, 0x61726150u, 0x0000736du, 0x00050006u, 0x00000013u, 0x00000000u, 0x756f4375u,
        0x0000746eu, 0x00050006u, 0x00000013u, 0x00000001u, 0x61635375u, 0x0000656cu, 0x00040005u, 0x00000015u,
        0x61726170u, 0x0000736du, 0x00030005u, 0x00000021u, 0x00000043u, 0x00050006u, 0x00000021u, 0x00000000u,
        0x756c6176u, 0x00007365u, 0x00030005u, 0x00000023u, 0x00000000u, 0x00030005u, 0x00000026u, 0x00000041u,
        0x00040006u, 0x00000026u, 0x00000000u, 0x00000061u, 0x00030005u, 0x00000028u, 0x00000000u, 0x00030005u,
        0x0000002eu, 0x00000042u, 0x00040006u, 0x0000002eu, 0x00000000u, 0x00000062u, 0x00030005u, 0x00000030u,
        0x00000000u, 0x00040047u, 0x0000000bu, 0x0000000bu, 0x0000001cu, 0x00030047u, 0x00000013u, 0x00000002u,
        0x00050048u, 0x00000013u, 0x00000000u, 0x00000023u, 0x00000000u, 0x00050048u, 0x00000013u, 0x00000001u,
        0x00000023u, 0x00000004u, 0x00040047u, 0x00000020u, 0x00000006u, 0x00000004u, 0x00030047u, 0x00000021u,
        0x00000003u, 0x00040048u, 0x00000021u, 0x00000000u, 0x00000019u, 0x00050048u, 0x00000021u, 0x00000000u,
        0x00000023u, 0x00000000u, 0x00030047u, 0x00000023u, 0x00000019u, 0x00040047u, 0x00000023u, 0x00000021u,
        0x00000007u, 0x00040047u, 0x00000023u, 0x00000022u, 0x00000000u, 0x00040047u, 0x00000025u, 0x00000006u,
        0x00000004u, 0x00030047u, 0x00000026u, 0x00000003u, 0x00040048u, 0x00000026u, 0x00000000u, 0x00000018u,
        0x00050048u, 0x00000026u, 0x00000000u, 0x00000023u, 0x00000000u, 0x00030047u, 0x00000028u, 0x00000018u,
        0x00040047u, 0x00000028u, 0x00000021u, 0x00000000u, 0x00040047u, 0x00000028u, 0x00000022u, 0x00000000u,
        0x00040047u, 0x0000002du, 0x00000006u, 0x00000004u, 0x00030047u, 0x0000002eu, 0x00000003u, 0x00040048u,
        0x0000002eu, 0x00000000u, 0x00000018u, 0x00050048u, 0x0000002eu, 0x00000000u, 0x00000023u, 0x00000000u,
        0x00030047u, 0x00000030u, 0x00000018u, 0x00040047u, 0x00000030u, 0x00000021u, 0x00000001u, 0x00040047u,
        0x00000030u, 0x00000022u, 0x00000000u, 0x00040047u, 0x0000003du, 0x0000000bu, 0x00000019u, 0x00020013u,
        0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00040015u, 0x00000006u, 0x00000020u, 0x00000000u,
        0x00040020u, 0x00000007u, 0x00000007u, 0x00000006u, 0x00040017u, 0x00000009u, 0x00000006u, 0x00000003u,
        0x00040020u, 0x0000000au, 0x00000001u, 0x00000009u, 0x0004003bu, 0x0000000au, 0x0000000bu, 0x00000001u,
        0x0004002bu, 0x00000006u, 0x0000000cu, 0x00000000u, 0x00040020u, 0x0000000du, 0x00000001u, 0x00000006u,
        0x00040015u, 0x00000011u, 0x00000020u, 0x00000001u, 0x00030016u, 0x00000012u, 0x00000020u, 0x0004001eu,
        0x00000013u, 0x00000011u, 0x00000012u, 0x00040020u, 0x00000014u, 0x00000009u, 0x00000013u, 0x0004003bu,
        0x00000014u, 0x00000015u, 0x00000009u, 0x0004002bu, 0x00000011u, 0x00000016u, 0x00000000u, 0x00040020u,
        0x00000017u, 0x00000009u, 0x00000011u, 0x00020014u, 0x0000001bu, 0x0003001du, 0x00000020u, 0x00000012u,
        0x0003001eu, 0x00000021u, 0x00000020u, 0x00040020u, 0x00000022u, 0x00000002u, 0x00000021u, 0x0004003bu,
        0x00000022u, 0x00000023u, 0x00000002u, 0x0003001du, 0x00000025u, 0x00000012u, 0x0003001eu, 0x00000026u,
        0x00000025u, 0x00040020u, 0x00000027u, 0x00000002u, 0x00000026u, 0x0004003bu, 0x00000027u, 0x00000028u,
        0x00000002u, 0x00040020u, 0x0000002au, 0x00000002u, 0x00000012u, 0x0003001du, 0x0000002du, 0x00000012u,
        0x0003001eu, 0x0000002eu, 0x0000002du, 0x00040020u, 0x0000002fu, 0x00000002u, 0x0000002eu, 0x0004003bu,
        0x0000002fu, 0x00000030u, 0x00000002u, 0x0004002bu, 0x00000011u, 0x00000035u, 0x00000001u, 0x00040020u,
        0x00000036u, 0x00000009u, 0x00000012u, 0x0004002bu, 0x00000006u, 0x0000003bu, 0x00000040u, 0x0004002bu,
        0x00000006u, 0x0000003cu, 0x00000001u, 0x0006002cu, 0x00000009u, 0x0000003du, 0x0000003bu, 0x0000003cu,
        0x0000003cu, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u,
        0x0004003bu, 0x00000007u, 0x00000008u, 0x00000007u, 0x00050041u, 0x0000000du, 0x0000000eu, 0x0000000bu,
        0x0000000cu, 0x0004003du, 0x00000006u, 0x0000000fu, 0x0000000eu, 0x0003003eu, 0x00000008u, 0x0000000fu,
        0x0004003du, 0x00000006u, 0x00000010u, 0x00000008u, 0x00050041u, 0x00000017u, 0x00000018u, 0x00000015u,
        0x00000016u, 0x0004003du, 0x00000011u, 0x00000019u, 0x00000018u, 0x0004007cu, 0x00000006u, 0x0000001au,
        0x00000019u, 0x000500aeu, 0x0000001bu, 0x0000001cu, 0x00000010u, 0x0000001au, 0x000300f7u, 0x0000001eu,
        0x00000000u, 0x000400fau, 0x0000001cu, 0x0000001du, 0x0000001eu, 0x000200f8u, 0x0000001du, 0x000100fdu,
        0x000200f8u, 0x0000001eu, 0x0004003du, 0x00000006u, 0x00000024u, 0x00000008u, 0x0004003du, 0x00000006u,
        0x00000029u, 0x00000008u, 0x00060041u, 0x0000002au, 0x0000002bu, 0x00000028u, 0x00000016u, 0x00000029u,
        0x0004003du, 0x00000012u, 0x0000002cu, 0x0000002bu, 0x0004003du, 0x00000006u, 0x00000031u, 0x00000008u,
        0x00060041u, 0x0000002au, 0x00000032u, 0x00000030u, 0x00000016u, 0x00000031u, 0x0004003du, 0x00000012u,
        0x00000033u, 0x00000032u, 0x00050081u, 0x00000012u, 0x00000034u, 0x0000002cu, 0x00000033u, 0x00050041u,
        0x00000036u, 0x00000037u, 0x00000015u, 0x00000035u, 0x0004003du, 0x00000012u, 0x00000038u, 0x00000037u,
        0x00050085u, 0x00000012u, 0x00000039u, 0x00000034u, 0x00000038u, 0x00060041u, 0x0000002au, 0x0000003au,
        0x00000023u, 0x00000016u, 0x00000024u, 0x0003003eu, 0x0000003au, 0x00000039u, 0x000100fdu, 0x00010038u
    };

    std::string VectorAddProgram()
    {
        return std::string(
            reinterpret_cast<const char*>(kVectorAddSpirV), sizeof(kVectorAddSpirV));
    }

    std::string ScaledVectorAddProgram()
    {
        return std::string(
            reinterpret_cast<const char*>(kScaledVectorAddSpirV),
            sizeof(kScaledVectorAddSpirV));
    }

    std::string ScaledVectorWithoutFirstMemberNameProgram()
    {
        std::vector<uint32_t> words(
            std::begin(kScaledVectorAddSpirV), std::end(kScaledVectorAddSpirV));
        for (std::size_t cursor = 5; cursor < words.size();) {
            const uint16_t wordCount = static_cast<uint16_t>(words[cursor] >> 16u);
            const uint16_t opcode = static_cast<uint16_t>(words[cursor] & 0xffffu);
            if (opcode == 6) {
                // Turn one OpMemberName into an ignored OpName-shaped record. The native parser
                // must refuse the now-incomplete name metadata before driver object creation.
                words[cursor] = (words[cursor] & 0xffff0000u) | 5u;
                break;
            }
            cursor += wordCount;
        }
        return std::string(
            reinterpret_cast<const char*>(words.data()),
            words.size() * sizeof(uint32_t));
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

        bool missingNameRejected = false;
        std::string missingNameMessage;
        try {
            ComputeShader missingName(device, ScaledVectorWithoutFirstMemberNameProgram());
        } catch (const std::runtime_error& error) {
            missingNameMessage = error.what();
            missingNameRejected = missingNameMessage.find("OpMemberName") != std::string::npos;
        }
        check(missingNameRejected,
              "D stripped scalar-name metadata is refused before native object creation",
              missingNameMessage.empty() ? "no exception" : missingNameMessage);

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
              "E public storage-buffer upload/readback preserves every input element",
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
              "F SPIR-V dispatch computes A+B for every output element",
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
        bool unknownScalarRejected = false;
        try {
            vectorAdd.setUniform("uCount", static_cast<int>(kElementCount));
        } catch (const std::out_of_range&) {
            unknownScalarRejected = true;
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
        check(slotRejected && unknownScalarRejected && missingBindingRejected &&
                  !vectorAdd.isImageBindingSupported(),
              "G undeclared, unknown, missing, and image bindings fail explicitly",
              "slot4=" + std::string(slotRejected ? "refused" : "accepted") +
                  " unknownScalar=" +
                  std::string(unknownScalarRejected ? "refused" : "accepted") +
                  " missing2=" +
                  std::string(missingBindingRejected ? "refused" : "accepted") +
                  " imageSupport=" +
                  std::string(vectorAdd.isImageBindingSupported() ? "true" : "false"));

        constexpr std::size_t kActiveCount = 173;
        const std::size_t baselineLiveSets =
            renderer->GetLiveComputeDescriptorSetCountEXT();
        const std::size_t baselineLiveLayouts =
            renderer->GetLiveComputePipelineLayoutCountEXT();
        const uint64_t baselineSetAllocations =
            renderer->GetComputeDescriptorSetAllocationCountEXT();
        const uint64_t baselineLayoutCreations =
            renderer->GetComputePipelineLayoutCreationCountEXT();
        bool allocatedExactlyOnce = false;
        bool countersStayedBounded = false;
        bool scalarTypesRejected = false;
        std::size_t scaledMismatch = kElementCount;
        {
            ComputeShader scaled(device, ScaledVectorAddProgram());
            allocatedExactlyOnce =
                renderer->GetLiveComputeDescriptorSetCountEXT() == baselineLiveSets + 1 &&
                renderer->GetLiveComputePipelineLayoutCountEXT() == baselineLiveLayouts + 1 &&
                renderer->GetComputeDescriptorSetAllocationCountEXT() ==
                    baselineSetAllocations + 1 &&
                renderer->GetComputePipelineLayoutCreationCountEXT() ==
                    baselineLayoutCreations + 1;

            scaled.bindStorageBuffer(0, inputA.getBuffer());
            scaled.bindStorageBuffer(1, inputB.getBuffer());
            scaled.bindStorageBuffer(7, output.getBuffer());
            scaled.setUniform("uCount", static_cast<int>(kActiveCount));
            output.setData(sentinel);
            for (int iteration = 0; iteration < 32; ++iteration) {
                scaled.setUniform("uScale", 0.25f + static_cast<float>(iteration) * 0.01f);
                scaled.dispatch(static_cast<int>(kElementCount / 64));
            }
            scaled.setUniform("uScale", 0.5f);
            scaled.dispatch(static_cast<int>(kElementCount / 64));

            const std::vector<float> scaledActual = output.getData();
            for (std::size_t i = 0; i < kElementCount; ++i) {
                const float wanted = i < kActiveCount ? expected[i] * 0.5f : sentinel[i];
                if (std::abs(scaledActual[i] - wanted) > 0.0001f) {
                    scaledMismatch = i;
                    break;
                }
            }

            bool intAsFloatRejected = false;
            bool floatAsIntRejected = false;
            try {
                scaled.setUniform("uCount", 1.0f);
            } catch (const std::invalid_argument&) {
                intAsFloatRejected = true;
            }
            try {
                scaled.setUniform("uScale", 1);
            } catch (const std::invalid_argument&) {
                floatAsIntRejected = true;
            }
            scalarTypesRejected = intAsFloatRejected && floatAsIntRejected;
            countersStayedBounded =
                renderer->GetLiveComputeDescriptorSetCountEXT() == baselineLiveSets + 1 &&
                renderer->GetLiveComputePipelineLayoutCountEXT() == baselineLiveLayouts + 1 &&
                renderer->GetComputeDescriptorSetAllocationCountEXT() ==
                    baselineSetAllocations + 1 &&
                renderer->GetComputePipelineLayoutCreationCountEXT() ==
                    baselineLayoutCreations + 1;
        }

        check(scaledMismatch == kElementCount,
              "H sparse SSBO and named scalar metadata drive exact bounded output",
              scaledMismatch == kElementCount
                  ? std::to_string(kActiveCount) + " scaled, " +
                        std::to_string(kElementCount - kActiveCount) + " untouched"
                  : "first mismatch at " + std::to_string(scaledMismatch));
        check(allocatedExactlyOnce && countersStayedBounded && scalarTypesRejected,
              "I repeated dispatches reuse one typed descriptor/layout snapshot",
              "iterations=33 setAllocations=" +
                  std::to_string(renderer->GetComputeDescriptorSetAllocationCountEXT() -
                                 baselineSetAllocations) +
                  " layoutCreations=" +
                  std::to_string(renderer->GetComputePipelineLayoutCreationCountEXT() -
                                 baselineLayoutCreations) +
                  " typeMismatch=" +
                  std::string(scalarTypesRejected ? "refused" : "accepted"));
        check(renderer->GetLiveComputeDescriptorSetCountEXT() == baselineLiveSets &&
                  renderer->GetLiveComputePipelineLayoutCountEXT() == baselineLiveLayouts,
              "J compute descriptor and pipeline-layout ownership is reclaimed",
              "liveSets=" +
                  std::to_string(renderer->GetLiveComputeDescriptorSetCountEXT()) +
                  " liveLayouts=" +
                  std::to_string(renderer->GetLiveComputePipelineLayoutCountEXT()));

        constexpr StorageBufferUsage fullUsage =
            StorageBufferUsage::Storage |
            StorageBufferUsage::TransferSource |
            StorageBufferUsage::TransferDestination |
            StorageBufferUsage::IndirectArguments |
            StorageBufferUsage::Vertex |
            StorageBufferUsage::Index;
        constexpr VkBufferUsageFlags fullNativeUsage =
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        const std::size_t vectorBytes = kElementCount * sizeof(float);
        StorageBuffer gpuOnly(
            device, StorageBufferDescriptor(
                        vectorBytes, fullUsage, StorageBufferCpuAccess::None));
        auto* gpuNative = dynamic_cast<VulkanStorageBufferRenderer*>(gpuOnly.getRendererEXT());
        check(gpuNative != nullptr &&
                  gpuOnly.getDescriptor().getByteSize() == vectorBytes &&
                  gpuOnly.getDescriptor().getUsage() == fullUsage &&
                  gpuOnly.getDescriptor().getCpuAccess() == StorageBufferCpuAccess::None &&
                  gpuNative->GetVkBufferUsageEXT() == fullNativeUsage &&
                  gpuNative->GetVkMemoryPropertiesEXT() == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT &&
                  !gpuNative->IsMappedEXT(),
              "K immutable usage maps to exact Vulkan flags and GPU-only memory",
              gpuNative == nullptr
                  ? "renderer record has the wrong type"
                  : "vkUsage=" + std::to_string(gpuNative->GetVkBufferUsageEXT()) +
                        " memory=" +
                        std::to_string(gpuNative->GetVkMemoryPropertiesEXT()) +
                        " mapped=" + (gpuNative->IsMappedEXT() ? "true" : "false"));

        StorageBuffer upload(
            device, StorageBufferDescriptor(
                        64, StorageBufferUsage::TransferSource,
                        StorageBufferCpuAccess::Write));
        StorageBuffer readback(
            device, StorageBufferDescriptor(
                        vectorBytes, StorageBufferUsage::TransferDestination,
                        StorageBufferCpuAccess::Read));
        const std::array<std::uint8_t, 17> bytes = {
            3, 17, 29, 41, 53, 67, 79, 83, 97, 101, 113, 127, 131, 149, 157, 173, 181};
        upload.setBytes(7, bytes.data(), bytes.size());
        upload.copyTo(gpuOnly, 7, 31, bytes.size());
        gpuOnly.copyTo(readback, 31, 19, bytes.size());
        std::array<std::uint8_t, 17> copied{};
        readback.getBytes(19, copied.data(), copied.size());
        auto* uploadNative = dynamic_cast<VulkanStorageBufferRenderer*>(upload.getRendererEXT());
        auto* readbackNative =
            dynamic_cast<VulkanStorageBufferRenderer*>(readback.getRendererEXT());
        check(copied == bytes && uploadNative != nullptr && readbackNative != nullptr &&
                  uploadNative->IsMappedEXT() && readbackNative->IsMappedEXT() &&
                  uploadNative->GetVkBufferUsageEXT() == VK_BUFFER_USAGE_TRANSFER_SRC_BIT &&
                  readbackNative->GetVkBufferUsageEXT() == VK_BUFFER_USAGE_TRANSFER_DST_BIT,
              "L ranged upload/copy/readback crosses GPU-only storage exactly",
              std::to_string(copied.size()) + "/" + std::to_string(bytes.size()) +
                  " exact; sourceUsage=" +
                  std::to_string(uploadNative != nullptr
                                     ? uploadNative->GetVkBufferUsageEXT() : 0) +
                  " destinationUsage=" +
                  std::to_string(readbackNative != nullptr
                                     ? readbackNative->GetVkBufferUsageEXT() : 0));

        ComputeShader gpuOnlyVectorAdd(device, VectorAddProgram());
        gpuOnlyVectorAdd.bindStorageBuffer(0, inputA.getBuffer());
        gpuOnlyVectorAdd.bindStorageBuffer(1, inputB.getBuffer());
        gpuOnlyVectorAdd.bindStorageBuffer(2, gpuOnly);
        gpuOnlyVectorAdd.dispatch(static_cast<int>(kElementCount / 64));
        gpuOnly.copyTo(readback, 0, 0, vectorBytes);
        std::vector<float> gpuOnlyActual(kElementCount);
        readback.getBytes(gpuOnlyActual.data(), vectorBytes);
        check(gpuOnlyActual == expected,
              "M compute writes GPU-only storage before targeted readback copy",
              std::to_string(kElementCount) + " elements");

        bool gpuOnlyUploadRefused = false;
        bool gpuOnlyReadbackRefused = false;
        bool overflowRangeRefused = false;
        bool missingCopyUsageRefused = false;
        bool overlappingCopyRefused = false;
        try { gpuOnly.setBytes(bytes.data(), bytes.size()); }
        catch (const System::NotSupportedException&) { gpuOnlyUploadRefused = true; }
        try { gpuOnly.getBytes(copied.data(), copied.size()); }
        catch (const System::NotSupportedException&) { gpuOnlyReadbackRefused = true; }
        try {
            upload.setBytes(
                std::numeric_limits<std::size_t>::max(), bytes.data(), bytes.size());
        } catch (const std::invalid_argument&) { overflowRangeRefused = true; }
        try { readback.copyTo(upload, 0, 0, 1); }
        catch (const System::NotSupportedException&) { missingCopyUsageRefused = true; }
        StorageBuffer overlap(
            device, StorageBufferDescriptor(
                        32, StorageBufferUsage::TransferSource |
                                StorageBufferUsage::TransferDestination,
                        StorageBufferCpuAccess::Read | StorageBufferCpuAccess::Write));
        try { overlap.copyTo(overlap, 0, 4, 8); }
        catch (const std::invalid_argument&) { overlappingCopyRefused = true; }
        check(gpuOnlyUploadRefused && gpuOnlyReadbackRefused && overflowRangeRefused &&
                  missingCopyUsageRefused && overlappingCopyRefused,
              "N access intent and overflow-safe ranges refuse invalid operations",
              "cpuWrite=" + std::string(gpuOnlyUploadRefused ? "refused" : "accepted") +
                  " cpuRead=" +
                  std::string(gpuOnlyReadbackRefused ? "refused" : "accepted") +
                  " overflow=" +
                  std::string(overflowRangeRefused ? "refused" : "accepted") +
                  " usage=" +
                  std::string(missingCopyUsageRefused ? "refused" : "accepted") +
                  " overlap=" +
                  std::string(overlappingCopyRefused ? "refused" : "accepted"));

        StorageBuffer fanoutSource(
            device, StorageBufferDescriptor(
                        bytes.size(), StorageBufferUsage::TransferSource,
                        StorageBufferCpuAccess::Write));
        StorageBuffer fanoutA(
            device, StorageBufferDescriptor(
                        bytes.size(), StorageBufferUsage::TransferDestination,
                        StorageBufferCpuAccess::Read));
        StorageBuffer fanoutB(
            device, StorageBufferDescriptor(
                        bytes.size(), StorageBufferUsage::TransferDestination,
                        StorageBufferCpuAccess::Read));
        fanoutSource.setBytes(bytes.data(), bytes.size());
        const std::uint64_t barriersBeforeFanout =
            renderer->GetLogicalResourceBarrierCountEXT();
        const std::uint64_t elisionsBeforeFanout =
            renderer->GetLogicalResourceBarrierElisionCountEXT();
        fanoutSource.copyTo(fanoutA, 0, 0, bytes.size());
        fanoutSource.copyTo(fanoutB, 0, 0, bytes.size());
        std::array<std::uint8_t, 17> fanoutBytesA{};
        std::array<std::uint8_t, 17> fanoutBytesB{};
        fanoutB.getBytes(fanoutBytesB.data(), fanoutBytesB.size());
        fanoutA.getBytes(fanoutBytesA.data(), fanoutBytesA.size());
        const std::uint64_t fanoutBarriers =
            renderer->GetLogicalResourceBarrierCountEXT() - barriersBeforeFanout;
        const std::uint64_t fanoutElisions =
            renderer->GetLogicalResourceBarrierElisionCountEXT() - elisionsBeforeFanout;
        check(fanoutBytesA == bytes && fanoutBytesB == bytes &&
                  fanoutBarriers == 3 && fanoutElisions == 1,
              "O compatible repeated transfer reads elide their Vulkan barrier",
              "barriers=" + std::to_string(fanoutBarriers) +
                  " elisions=" + std::to_string(fanoutElisions) +
                  " outputs=" +
                  std::string(fanoutBytesA == bytes && fanoutBytesB == bytes
                                  ? "exact" : "mismatch"));

        const std::size_t validationAfter = renderer->GetValidationMessagesEXT().size();
        check(VulkanRenderer::IsValidationActiveEXT() &&
                  validationAfter == validationBefore,
              "P compute/storage operations add no Vulkan validation messages",
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
