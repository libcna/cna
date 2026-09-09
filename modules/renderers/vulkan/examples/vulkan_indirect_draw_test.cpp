// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2245: native Vulkan indirect drawing.
//
// A  Public/renderer capability agrees with enabled drawIndirectFirstInstance.
// B  A CPU command at a non-zero argument offset preserves VertexBufferBinding + FirstVertex.
// C  The indexed command preserves binding offset + FirstIndex + BaseVertex independently.
// D  BaseInstance selects one retained per-instance record even after public buffer disposal.
// E  A compute shader writes the indexed command and the draw consumes it without CPU readback.
// F  A native SPIR-V effect reflects and binds a sparse vertex-stage storage-buffer slot.
// G  Compute-written data drive fresh left, right, and culled frames through early disposal.
// H  The compute-to-graphics path adds no one-time submission and records both dependencies.
// I  SpriteBatch -> compute -> copy -> XNA 3D -> indirect -> present remains one ordered submit.
// J  Deferred compute/copy add no one-time submission or queue-wide wait.
// K  No Vulkan validation message is added by the complete exercise.

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/IndirectDrawArguments.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using CNA::GraphicsCapability;
using CNA::IndirectDrawArguments;
using CNA::IndirectDrawIndexedArguments;
using CNA::Graphics::ComputeShader;
using CNA::Graphics::StorageBuffer;
using CNA::Graphics::StorageBufferCpuAccess;
using CNA::Graphics::StorageBufferDescriptor;
using CNA::Graphics::StorageBufferUsage;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GameTime;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::GraphicsDeviceManager;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 16;
    const Color kClear(0, 255, 0, 255);

    struct Vertex
    {
        float x, y, z;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(Vertex) == 16);

    struct InstanceMatrix
    {
        float m[16];
    };
    static_assert(sizeof(InstanceMatrix) == 64);

    InstanceMatrix Translate(const float x)
    {
        InstanceMatrix result{};
        result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0f;
        result.m[12] = x;
        return result;
    }

    VertexDeclaration VertexLayout()
    {
        return VertexDeclaration(16, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
    }

    std::array<Vertex, 3> CoveringTriangle()
    {
        return {Vertex{-1.0f, -1.0f, 0.0f, 255, 0, 0, 255},
                Vertex{-1.0f,  3.0f, 0.0f, 255, 0, 0, 255},
                Vertex{ 3.0f, -1.0f, 0.0f, 255, 0, 0, 255}};
    }

    int CountRed(RenderTarget2D& target)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kSize * kSize));
        target.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        int count = 0;
        for (const Color& pixel : pixels)
            if (pixel.getRProperty() > 200 && pixel.getGProperty() < 50) ++count;
        return count;
    }

    std::array<Color, 2> ReadHorizontalProbes(RenderTarget2D& target)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kSize * kSize));
        target.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        return {pixels[static_cast<std::size_t>(kSize * (kSize / 2) + 4)],
                pixels[static_cast<std::size_t>(kSize * (kSize / 2) + 12)]};
    }

    // libshaderc -O output for a one-invocation SPIR-V compute program which writes:
    // { IndexCount=6, InstanceCount=1, FirstIndex=0, BaseVertex=0, BaseInstance=1 }.
    constexpr std::uint32_t kWriteIndexedCommandSpirV[] = {
        0x07230203u, 0x00010000u, 0x000d000bu, 0x0000001cu, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
        0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
        0x0005000fu, 0x00000005u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00060010u, 0x00000004u, 0x00000011u,
        0x00000001u, 0x00000001u, 0x00000001u, 0x00030047u, 0x00000008u, 0x00000003u, 0x00050048u, 0x00000008u,
        0x00000000u, 0x00000023u, 0x00000000u, 0x00050048u, 0x00000008u, 0x00000001u, 0x00000023u, 0x00000004u,
        0x00050048u, 0x00000008u, 0x00000002u, 0x00000023u, 0x00000008u, 0x00050048u, 0x00000008u, 0x00000003u,
        0x00000023u, 0x0000000cu, 0x00050048u, 0x00000008u, 0x00000004u, 0x00000023u, 0x00000010u, 0x00040047u,
        0x0000000au, 0x00000021u, 0x00000000u, 0x00040047u, 0x0000000au, 0x00000022u, 0x00000000u, 0x00040047u,
        0x0000001bu, 0x0000000bu, 0x00000019u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u,
        0x00040015u, 0x00000006u, 0x00000020u, 0x00000000u, 0x00040015u, 0x00000007u, 0x00000020u, 0x00000001u,
        0x0007001eu, 0x00000008u, 0x00000006u, 0x00000006u, 0x00000006u, 0x00000007u, 0x00000006u, 0x00040020u,
        0x00000009u, 0x00000002u, 0x00000008u, 0x0004003bu, 0x00000009u, 0x0000000au, 0x00000002u, 0x0004002bu,
        0x00000007u, 0x0000000bu, 0x00000000u, 0x0004002bu, 0x00000006u, 0x0000000cu, 0x00000006u, 0x00040020u,
        0x0000000du, 0x00000002u, 0x00000006u, 0x0004002bu, 0x00000007u, 0x0000000fu, 0x00000001u, 0x0004002bu,
        0x00000006u, 0x00000010u, 0x00000001u, 0x0004002bu, 0x00000007u, 0x00000012u, 0x00000002u, 0x0004002bu,
        0x00000006u, 0x00000013u, 0x00000000u, 0x0004002bu, 0x00000007u, 0x00000015u, 0x00000003u, 0x00040020u,
        0x00000016u, 0x00000002u, 0x00000007u, 0x0004002bu, 0x00000007u, 0x00000018u, 0x00000004u, 0x00040017u,
        0x0000001au, 0x00000006u, 0x00000003u, 0x0006002cu, 0x0000001au, 0x0000001bu, 0x00000010u, 0x00000010u,
        0x00000010u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u,
        0x00050041u, 0x0000000du, 0x0000000eu, 0x0000000au, 0x0000000bu, 0x0003003eu, 0x0000000eu, 0x0000000cu,
        0x00050041u, 0x0000000du, 0x00000011u, 0x0000000au, 0x0000000fu, 0x0003003eu, 0x00000011u, 0x00000010u,
        0x00050041u, 0x0000000du, 0x00000014u, 0x0000000au, 0x00000012u, 0x0003003eu, 0x00000014u, 0x00000013u,
        0x00050041u, 0x00000016u, 0x00000017u, 0x0000000au, 0x00000015u, 0x0003003eu, 0x00000017u, 0x0000000bu,
        0x00050041u, 0x0000000du, 0x00000019u, 0x0000000au, 0x00000018u, 0x0003003eu, 0x00000019u, 0x00000010u,
        0x000100fdu, 0x00010038u,
    };

    std::string CommandProgram()
    {
        return std::string(
            reinterpret_cast<const char*>(kWriteIndexedCommandSpirV),
            sizeof(kWriteIndexedCommandSpirV));
    }

    // Offline libshaderc output. Compute writes one instance transform plus its indexed indirect
    // command; the vertex shader reads that transform from descriptor set 2, binding 6. The
    // compute module is intentionally unoptimized so its push-constant member names survive for
    // the renderer's named-scalar reflection.
    constexpr std::uint32_t kDrawStorageComputeSpirV[] = {
        0x07230203u, 0x00010000u, 0x000d000bu, 0x0000003bu, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
        0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
        0x0005000fu, 0x00000005u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00060010u, 0x00000004u, 0x00000011u,
        0x00000001u, 0x00000001u, 0x00000001u, 0x00030003u, 0x00000002u, 0x000001c2u, 0x000a0004u, 0x475f4c47u,
        0x4c474f4fu, 0x70635f45u, 0x74735f70u, 0x5f656c79u, 0x656e696cu, 0x7269645fu, 0x69746365u, 0x00006576u,
        0x00080004u, 0x475f4c47u, 0x4c474f4fu, 0x6e695f45u, 0x64756c63u, 0x69645f65u, 0x74636572u, 0x00657669u,
        0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00050005u, 0x0000000au, 0x6e617274u, 0x726f6673u,
        0x0000006du, 0x00040005u, 0x00000014u, 0x61726150u, 0x0000736du, 0x00040006u, 0x00000014u, 0x00000000u,
        0x00005875u, 0x00050006u, 0x00000014u, 0x00000001u, 0x756f4375u, 0x0000746eu, 0x00040005u, 0x00000016u,
        0x61726170u, 0x0000736du, 0x00060005u, 0x00000020u, 0x69736956u, 0x42656c62u, 0x6b636f6cu, 0x00000000u,
        0x00050006u, 0x00000020u, 0x00000000u, 0x6c726f77u, 0x00000064u, 0x00040005u, 0x00000022u, 0x69736976u,
        0x00656c62u, 0x00060005u, 0x00000027u, 0x6d6d6f43u, 0x42646e61u, 0x6b636f6cu, 0x00000000u, 0x00050006u,
        0x00000027u, 0x00000000u, 0x6d6d6f63u, 0x00646e61u, 0x00060005u, 0x00000029u, 0x7074756fu, 0x6f437475u,
        0x6e616d6du, 0x00000064u, 0x00030047u, 0x00000014u, 0x00000002u, 0x00050048u, 0x00000014u, 0x00000000u,
        0x00000023u, 0x00000000u, 0x00050048u, 0x00000014u, 0x00000001u, 0x00000023u, 0x00000004u, 0x00040047u,
        0x0000001fu, 0x00000006u, 0x00000040u, 0x00030047u, 0x00000020u, 0x00000003u, 0x00040048u, 0x00000020u,
        0x00000000u, 0x00000005u, 0x00050048u, 0x00000020u, 0x00000000u, 0x00000007u, 0x00000010u, 0x00040048u,
        0x00000020u, 0x00000000u, 0x00000019u, 0x00050048u, 0x00000020u, 0x00000000u, 0x00000023u, 0x00000000u,
        0x00030047u, 0x00000022u, 0x00000019u, 0x00040047u, 0x00000022u, 0x00000021u, 0x00000000u, 0x00040047u,
        0x00000022u, 0x00000022u, 0x00000000u, 0x00040047u, 0x00000026u, 0x00000006u, 0x00000004u, 0x00030047u,
        0x00000027u, 0x00000003u, 0x00040048u, 0x00000027u, 0x00000000u, 0x00000019u, 0x00050048u, 0x00000027u,
        0x00000000u, 0x00000023u, 0x00000000u, 0x00030047u, 0x00000029u, 0x00000019u, 0x00040047u, 0x00000029u,
        0x00000021u, 0x00000001u, 0x00040047u, 0x00000029u, 0x00000022u, 0x00000000u, 0x00040047u, 0x0000003au,
        0x0000000bu, 0x00000019u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00030016u,
        0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u, 0x00000004u, 0x00040018u, 0x00000008u,
        0x00000007u, 0x00000004u, 0x00040020u, 0x00000009u, 0x00000007u, 0x00000008u, 0x0004002bu, 0x00000006u,
        0x0000000bu, 0x3f800000u, 0x0004002bu, 0x00000006u, 0x0000000cu, 0x00000000u, 0x0007002cu, 0x00000007u,
        0x0000000du, 0x0000000bu, 0x0000000cu, 0x0000000cu, 0x0000000cu, 0x0007002cu, 0x00000007u, 0x0000000eu,
        0x0000000cu, 0x0000000bu, 0x0000000cu, 0x0000000cu, 0x0007002cu, 0x00000007u, 0x0000000fu, 0x0000000cu,
        0x0000000cu, 0x0000000bu, 0x0000000cu, 0x0007002cu, 0x00000007u, 0x00000010u, 0x0000000cu, 0x0000000cu,
        0x0000000cu, 0x0000000bu, 0x0007002cu, 0x00000008u, 0x00000011u, 0x0000000du, 0x0000000eu, 0x0000000fu,
        0x00000010u, 0x00040015u, 0x00000012u, 0x00000020u, 0x00000001u, 0x0004002bu, 0x00000012u, 0x00000013u,
        0x00000003u, 0x0004001eu, 0x00000014u, 0x00000006u, 0x00000012u, 0x00040020u, 0x00000015u, 0x00000009u,
        0x00000014u, 0x0004003bu, 0x00000015u, 0x00000016u, 0x00000009u, 0x0004002bu, 0x00000012u, 0x00000017u,
        0x00000000u, 0x00040020u, 0x00000018u, 0x00000009u, 0x00000006u, 0x00040015u, 0x0000001bu, 0x00000020u,
        0x00000000u, 0x0004002bu, 0x0000001bu, 0x0000001cu, 0x00000000u, 0x00040020u, 0x0000001du, 0x00000007u,
        0x00000006u, 0x0003001du, 0x0000001fu, 0x00000008u, 0x0003001eu, 0x00000020u, 0x0000001fu, 0x00040020u,
        0x00000021u, 0x00000002u, 0x00000020u, 0x0004003bu, 0x00000021u, 0x00000022u, 0x00000002u, 0x00040020u,
        0x00000024u, 0x00000002u, 0x00000008u, 0x0003001du, 0x00000026u, 0x0000001bu, 0x0003001eu, 0x00000027u,
        0x00000026u, 0x00040020u, 0x00000028u, 0x00000002u, 0x00000027u, 0x0004003bu, 0x00000028u, 0x00000029u,
        0x00000002u, 0x0004002bu, 0x0000001bu, 0x0000002au, 0x00000006u, 0x00040020u, 0x0000002bu, 0x00000002u,
        0x0000001bu, 0x0004002bu, 0x00000012u, 0x0000002du, 0x00000001u, 0x00040020u, 0x0000002eu, 0x00000009u,
        0x00000012u, 0x0004002bu, 0x00000012u, 0x00000033u, 0x00000002u, 0x0004002bu, 0x00000012u, 0x00000036u,
        0x00000004u, 0x00040017u, 0x00000038u, 0x0000001bu, 0x00000003u, 0x0004002bu, 0x0000001bu, 0x00000039u,
        0x00000001u, 0x0006002cu, 0x00000038u, 0x0000003au, 0x00000039u, 0x00000039u, 0x00000039u, 0x00050036u,
        0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x0004003bu, 0x00000009u,
        0x0000000au, 0x00000007u, 0x0003003eu, 0x0000000au, 0x00000011u, 0x00050041u, 0x00000018u, 0x00000019u,
        0x00000016u, 0x00000017u, 0x0004003du, 0x00000006u, 0x0000001au, 0x00000019u, 0x00060041u, 0x0000001du,
        0x0000001eu, 0x0000000au, 0x00000013u, 0x0000001cu, 0x0003003eu, 0x0000001eu, 0x0000001au, 0x0004003du,
        0x00000008u, 0x00000023u, 0x0000000au, 0x00060041u, 0x00000024u, 0x00000025u, 0x00000022u, 0x00000017u,
        0x00000017u, 0x0003003eu, 0x00000025u, 0x00000023u, 0x00060041u, 0x0000002bu, 0x0000002cu, 0x00000029u,
        0x00000017u, 0x00000017u, 0x0003003eu, 0x0000002cu, 0x0000002au, 0x00050041u, 0x0000002eu, 0x0000002fu,
        0x00000016u, 0x0000002du, 0x0004003du, 0x00000012u, 0x00000030u, 0x0000002fu, 0x0004007cu, 0x0000001bu,
        0x00000031u, 0x00000030u, 0x00060041u, 0x0000002bu, 0x00000032u, 0x00000029u, 0x00000017u, 0x0000002du,
        0x0003003eu, 0x00000032u, 0x00000031u, 0x00060041u, 0x0000002bu, 0x00000034u, 0x00000029u, 0x00000017u,
        0x00000033u, 0x0003003eu, 0x00000034u, 0x0000001cu, 0x00060041u, 0x0000002bu, 0x00000035u, 0x00000029u,
        0x00000017u, 0x00000013u, 0x0003003eu, 0x00000035u, 0x0000001cu, 0x00060041u, 0x0000002bu, 0x00000037u,
        0x00000029u, 0x00000017u, 0x00000036u, 0x0003003eu, 0x00000037u, 0x0000001cu, 0x000100fdu, 0x00010038u,
    };
    constexpr std::uint32_t kDrawStorageVertexSpirV[] = {
        0x07230203u, 0x00010000u, 0x000d000bu, 0x0000002bu, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
        0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
        0x000a000fu, 0x00000000u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x0000000du, 0x00000016u, 0x0000001du,
        0x00000027u, 0x00000029u, 0x00030047u, 0x0000000bu, 0x00000002u, 0x00050048u, 0x0000000bu, 0x00000000u,
        0x0000000bu, 0x00000000u, 0x00050048u, 0x0000000bu, 0x00000001u, 0x0000000bu, 0x00000001u, 0x00050048u,
        0x0000000bu, 0x00000002u, 0x0000000bu, 0x00000003u, 0x00050048u, 0x0000000bu, 0x00000003u, 0x0000000bu,
        0x00000004u, 0x00040047u, 0x00000011u, 0x00000006u, 0x00000040u, 0x00030047u, 0x00000012u, 0x00000003u,
        0x00040048u, 0x00000012u, 0x00000000u, 0x00000005u, 0x00050048u, 0x00000012u, 0x00000000u, 0x00000007u,
        0x00000010u, 0x00040048u, 0x00000012u, 0x00000000u, 0x00000018u, 0x00050048u, 0x00000012u, 0x00000000u,
        0x00000023u, 0x00000000u, 0x00030047u, 0x00000014u, 0x00000018u, 0x00040047u, 0x00000014u, 0x00000021u,
        0x00000006u, 0x00040047u, 0x00000014u, 0x00000022u, 0x00000002u, 0x00040047u, 0x00000016u, 0x0000000bu,
        0x0000002bu, 0x00040047u, 0x0000001du, 0x0000001eu, 0x00000000u, 0x00040047u, 0x00000027u, 0x0000001eu,
        0x00000000u, 0x00040047u, 0x00000029u, 0x0000001eu, 0x00000001u, 0x00020013u, 0x00000002u, 0x00030021u,
        0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u,
        0x00000004u, 0x00040015u, 0x00000008u, 0x00000020u, 0x00000000u, 0x0004002bu, 0x00000008u, 0x00000009u,
        0x00000001u, 0x0004001cu, 0x0000000au, 0x00000006u, 0x00000009u, 0x0006001eu, 0x0000000bu, 0x00000007u,
        0x00000006u, 0x0000000au, 0x0000000au, 0x00040020u, 0x0000000cu, 0x00000003u, 0x0000000bu, 0x0004003bu,
        0x0000000cu, 0x0000000du, 0x00000003u, 0x00040015u, 0x0000000eu, 0x00000020u, 0x00000001u, 0x0004002bu,
        0x0000000eu, 0x0000000fu, 0x00000000u, 0x00040018u, 0x00000010u, 0x00000007u, 0x00000004u, 0x0003001du,
        0x00000011u, 0x00000010u, 0x0003001eu, 0x00000012u, 0x00000011u, 0x00040020u, 0x00000013u, 0x00000002u,
        0x00000012u, 0x0004003bu, 0x00000013u, 0x00000014u, 0x00000002u, 0x00040020u, 0x00000015u, 0x00000001u,
        0x0000000eu, 0x0004003bu, 0x00000015u, 0x00000016u, 0x00000001u, 0x00040020u, 0x00000018u, 0x00000002u,
        0x00000010u, 0x00040017u, 0x0000001bu, 0x00000006u, 0x00000003u, 0x00040020u, 0x0000001cu, 0x00000001u,
        0x0000001bu, 0x0004003bu, 0x0000001cu, 0x0000001du, 0x00000001u, 0x0004002bu, 0x00000006u, 0x0000001fu,
        0x3f800000u, 0x00040020u, 0x00000025u, 0x00000003u, 0x00000007u, 0x0004003bu, 0x00000025u, 0x00000027u,
        0x00000003u, 0x00040020u, 0x00000028u, 0x00000001u, 0x00000007u, 0x0004003bu, 0x00000028u, 0x00000029u,
        0x00000001u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u,
        0x0004003du, 0x0000000eu, 0x00000017u, 0x00000016u, 0x00060041u, 0x00000018u, 0x00000019u, 0x00000014u,
        0x0000000fu, 0x00000017u, 0x0004003du, 0x00000010u, 0x0000001au, 0x00000019u, 0x0004003du, 0x0000001bu,
        0x0000001eu, 0x0000001du, 0x00050051u, 0x00000006u, 0x00000020u, 0x0000001eu, 0x00000000u, 0x00050051u,
        0x00000006u, 0x00000021u, 0x0000001eu, 0x00000001u, 0x00050051u, 0x00000006u, 0x00000022u, 0x0000001eu,
        0x00000002u, 0x00070050u, 0x00000007u, 0x00000023u, 0x00000020u, 0x00000021u, 0x00000022u, 0x0000001fu,
        0x00050091u, 0x00000007u, 0x00000024u, 0x0000001au, 0x00000023u, 0x00050041u, 0x00000025u, 0x00000026u,
        0x0000000du, 0x0000000fu, 0x0003003eu, 0x00000026u, 0x00000024u, 0x0004003du, 0x00000007u, 0x0000002au,
        0x00000029u, 0x0003003eu, 0x00000027u, 0x0000002au, 0x000100fdu, 0x00010038u,
    };
    constexpr std::uint32_t kDrawStorageFragmentSpirV[] = {
        0x07230203u, 0x00010000u, 0x000d000bu, 0x00000022u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
        0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
        0x0007000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00000009u, 0x00000021u, 0x00030010u,
        0x00000004u, 0x00000007u, 0x00030003u, 0x00000002u, 0x000001c2u, 0x000a0004u, 0x475f4c47u, 0x4c474f4fu,
        0x70635f45u, 0x74735f70u, 0x5f656c79u, 0x656e696cu, 0x7269645fu, 0x69746365u, 0x00006576u, 0x00080004u,
        0x475f4c47u, 0x4c474f4fu, 0x6e695f45u, 0x64756c63u, 0x69645f65u, 0x74636572u, 0x00657669u, 0x00040005u,
        0x00000004u, 0x6e69616du, 0x00000000u, 0x00050005u, 0x00000009u, 0x4374756fu, 0x726f6c6fu, 0x00000000u,
        0x00060005u, 0x0000000cu, 0x69736956u, 0x42656c62u, 0x6b636f6cu, 0x00000000u, 0x00050006u, 0x0000000cu,
        0x00000000u, 0x6c726f77u, 0x00000064u, 0x00040005u, 0x0000000eu, 0x69736976u, 0x00656c62u, 0x00040005u,
        0x00000021u, 0x6f436e69u, 0x00726f6cu, 0x00040047u, 0x00000009u, 0x0000001eu, 0x00000000u, 0x00040047u,
        0x0000000bu, 0x00000006u, 0x00000040u, 0x00030047u, 0x0000000cu, 0x00000003u, 0x00040048u, 0x0000000cu,
        0x00000000u, 0x00000005u, 0x00050048u, 0x0000000cu, 0x00000000u, 0x00000007u, 0x00000010u, 0x00040048u,
        0x0000000cu, 0x00000000u, 0x00000018u, 0x00050048u, 0x0000000cu, 0x00000000u, 0x00000023u, 0x00000000u,
        0x00030047u, 0x0000000eu, 0x00000018u, 0x00040047u, 0x0000000eu, 0x00000021u, 0x00000006u, 0x00040047u,
        0x0000000eu, 0x00000022u, 0x00000002u, 0x00040047u, 0x00000021u, 0x0000001eu, 0x00000000u, 0x00020013u,
        0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u,
        0x00000007u, 0x00000006u, 0x00000004u, 0x00040020u, 0x00000008u, 0x00000003u, 0x00000007u, 0x0004003bu,
        0x00000008u, 0x00000009u, 0x00000003u, 0x00040018u, 0x0000000au, 0x00000007u, 0x00000004u, 0x0003001du,
        0x0000000bu, 0x0000000au, 0x0003001eu, 0x0000000cu, 0x0000000bu, 0x00040020u, 0x0000000du, 0x00000002u,
        0x0000000cu, 0x0004003bu, 0x0000000du, 0x0000000eu, 0x00000002u, 0x00040015u, 0x0000000fu, 0x00000020u,
        0x00000001u, 0x0004002bu, 0x0000000fu, 0x00000010u, 0x00000000u, 0x0004002bu, 0x0000000fu, 0x00000011u,
        0x00000003u, 0x00040015u, 0x00000012u, 0x00000020u, 0x00000000u, 0x0004002bu, 0x00000012u, 0x00000013u,
        0x00000000u, 0x00040020u, 0x00000014u, 0x00000002u, 0x00000006u, 0x0004002bu, 0x00000006u, 0x00000017u,
        0x00000000u, 0x00020014u, 0x00000018u, 0x0004002bu, 0x00000006u, 0x0000001au, 0x3f800000u, 0x0007002cu,
        0x00000007u, 0x0000001bu, 0x0000001au, 0x00000017u, 0x00000017u, 0x0000001au, 0x0007002cu, 0x00000007u,
        0x0000001cu, 0x00000017u, 0x00000017u, 0x0000001au, 0x0000001au, 0x00040017u, 0x0000001du, 0x00000018u,
        0x00000004u, 0x00040020u, 0x00000020u, 0x00000001u, 0x00000007u, 0x0004003bu, 0x00000020u, 0x00000021u,
        0x00000001u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u,
        0x00080041u, 0x00000014u, 0x00000015u, 0x0000000eu, 0x00000010u, 0x00000010u, 0x00000011u, 0x00000013u,
        0x0004003du, 0x00000006u, 0x00000016u, 0x00000015u, 0x000500b8u, 0x00000018u, 0x00000019u, 0x00000016u,
        0x00000017u, 0x00070050u, 0x0000001du, 0x0000001eu, 0x00000019u, 0x00000019u, 0x00000019u, 0x00000019u,
        0x000600a9u, 0x00000007u, 0x0000001fu, 0x0000001eu, 0x0000001bu, 0x0000001cu, 0x0003003eu, 0x00000009u,
        0x0000001fu, 0x000100fdu, 0x00010038u,
    };

    template<std::size_t N>
    std::string SpirVProgram(const std::uint32_t (&words)[N])
    {
        return std::string(reinterpret_cast<const char*>(words), sizeof(words));
    }

    std::string DrawStorageVertexWithoutReadonly()
    {
        std::string result = SpirVProgram(kDrawStorageVertexSpirV);
        bool changed = false;
        for (std::size_t cursor = 5; cursor < result.size() / sizeof(std::uint32_t);)
        {
            std::uint32_t instruction = 0;
            std::memcpy(&instruction, result.data() + cursor * sizeof(std::uint32_t),
                        sizeof(instruction));
            const std::uint16_t wordCount = static_cast<std::uint16_t>(instruction >> 16u);
            const std::uint16_t opcode = static_cast<std::uint16_t>(instruction & 0xffffu);
            if (wordCount == 0 || cursor + wordCount > result.size() / sizeof(std::uint32_t))
                return {};
            const std::size_t decorationWord =
                opcode == 71 && wordCount >= 3 ? cursor + 2 :
                (opcode == 72 && wordCount >= 4 ? cursor + 3 : 0);
            if (decorationWord != 0)
            {
                std::uint32_t decoration = 0;
                std::memcpy(&decoration,
                            result.data() + decorationWord * sizeof(std::uint32_t),
                            sizeof(decoration));
                if (decoration == 24)
                {
                    constexpr std::uint32_t NonReadable = 25;
                    std::memcpy(result.data() + decorationWord * sizeof(std::uint32_t),
                                &NonReadable, sizeof(NonReadable));
                    changed = true;
                }
            }
            cursor += wordCount;
        }
        return changed ? result : std::string{};
    }
}

class VulkanIndirectDrawTest final : public Game
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
        if (renderer == nullptr) {
            std::printf("SKIP: native Vulkan renderer is not active\n");
            std::exit(77);
        }
        const std::size_t validationBefore = renderer->GetValidationMessagesEXT().size();
        const bool nativeFeature =
            renderer->GetEnabledDeviceFeaturesEXT().drawIndirectFirstInstance == VK_TRUE;
        check(nativeFeature && renderer->SupportsIndirectDrawEXT() &&
                  device.SupportsCapability(GraphicsCapability::IndirectDraw),
              "A capability follows the enabled complete indirect contract",
              std::string("native=") + (nativeFeature ? "true" : "false"));
        if (!nativeFeature) {
            std::printf("SKIP: selected Vulkan device lacks drawIndirectFirstInstance\n");
            std::exit(77);
        }

        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.VertexColorEnabled = true;
        const VertexDeclaration vertexLayout = VertexLayout();

        {
            std::array<Vertex, 7> data{};
            const auto triangle = CoveringTriangle();
            std::copy(triangle.begin(), triangle.end(), data.begin() + 4);
            VertexBuffer vertices(device, vertexLayout, static_cast<int>(data.size()), BufferUsage::None);
            vertices.SetDataRaw(data.data(), static_cast<int>(data.size()), sizeof(Vertex));
            std::array<IndirectDrawArguments, 2> commands{};
            commands[1] = {3, 1, 3, 0};
            StorageBuffer arguments(
                device, StorageBufferDescriptor(
                            sizeof(commands), StorageBufferUsage::IndirectArguments,
                            StorageBufferCpuAccess::Write));
            arguments.setBytes(commands.data(), sizeof(commands));
            RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            device.SetRenderTarget(&target);
            device.Clear(kClear);
            effect.Apply();
            device.SetVertexBuffers({VertexBufferBinding(&vertices, 1, 0)});
            device.DrawPrimitivesIndirectEXT(
                PrimitiveType::TriangleList, *arguments.getRendererEXT(),
                sizeof(IndirectDrawArguments));
            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const int red = CountRed(target);
            const bool exactRole =
                arguments.getDescriptor().getUsage() ==
                    StorageBufferUsage::IndirectArguments &&
                arguments.getDescriptor().getCpuAccess() == StorageBufferCpuAccess::Write;
            check(red == kSize * kSize && exactRole,
                  "B CPU non-indexed command needs indirect usage, not compute/storage",
                  std::to_string(red) + "/" + std::to_string(kSize * kSize) +
                      " red pixels, exactRole=" + (exactRole ? "true" : "false"));
        }

        {
            std::array<Vertex, 6> data{};
            const auto triangle = CoveringTriangle();
            std::copy(triangle.begin(), triangle.end(), data.begin() + 3);
            VertexBuffer vertices(device, vertexLayout, static_cast<int>(data.size()), BufferUsage::None);
            vertices.SetDataRaw(data.data(), static_cast<int>(data.size()), sizeof(Vertex));
            const std::array<std::uint16_t, 6> indices{0, 0, 0, 0, 1, 2};
            IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
            indexBuffer.SetData(indices.data(), static_cast<int>(indices.size()));
            std::array<IndirectDrawIndexedArguments, 2> commands{};
            commands[1] = {3, 1, 3, 2, 0};
            StorageBuffer arguments(
                device, StorageBufferDescriptor(
                            sizeof(commands), StorageBufferUsage::IndirectArguments,
                            StorageBufferCpuAccess::Write));
            arguments.setBytes(commands.data(), sizeof(commands));
            RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            device.SetRenderTarget(&target);
            device.Clear(kClear);
            effect.Apply();
            device.SetVertexBuffers({VertexBufferBinding(&vertices, 1, 0)});
            device.SetIndexBuffer(&indexBuffer);
            device.DrawIndexedPrimitivesIndirectEXT(
                PrimitiveType::TriangleList, *arguments.getRendererEXT(),
                sizeof(IndirectDrawIndexedArguments));
            device.SetIndexBuffer(nullptr);
            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const int red = CountRed(target);
            check(red == kSize * kSize,
                  "C indexed command preserves FirstIndex, BaseVertex, and binding offset",
                  std::to_string(red) + "/" + std::to_string(kSize * kSize) + " red pixels");
        }

        VertexBuffer mesh(device, vertexLayout, 4, BufferUsage::None);
        const Vertex quad[4] = {
            {-0.18f,  0.18f, 0.0f, 255, 0, 0, 255},
            {-0.18f, -0.18f, 0.0f, 255, 0, 0, 255},
            { 0.18f, -0.18f, 0.0f, 255, 0, 0, 255},
            { 0.18f,  0.18f, 0.0f, 255, 0, 0, 255},
        };
        mesh.SetDataRaw(quad, 4, sizeof(Vertex));
        IndexBuffer quadIndices(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        const std::uint16_t quadOrder[6] = {0, 1, 2, 0, 2, 3};
        quadIndices.SetData(quadOrder, 6);
        const VertexDeclaration instanceLayout(64, {
            VertexElement(0, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 1),
            VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 2),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 3),
            VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 4),
        });
        VertexBuffer instances(device, instanceLayout, 2, BufferUsage::None);
        const InstanceMatrix instanceData[2] = {Translate(-0.5f), Translate(0.5f)};
        instances.SetDataRaw(instanceData, 2, sizeof(InstanceMatrix));

        const auto drawInstanced = [&](StorageBuffer& arguments) {
            RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            device.SetRenderTarget(&target);
            device.Clear(kClear);
            effect.Apply();
            device.SetVertexBuffers({VertexBufferBinding(&mesh, 0, 0),
                                     VertexBufferBinding(&instances, 0, 1)});
            device.SetIndexBuffer(&quadIndices);
            device.DrawIndexedPrimitivesIndirectEXT(
                PrimitiveType::TriangleList, *arguments.getRendererEXT(), 0);
            device.SetIndexBuffer(nullptr);
            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            return ReadHorizontalProbes(target);
        };

        {
            StorageBuffer arguments(
                device, StorageBufferDescriptor(
                            sizeof(IndirectDrawIndexedArguments),
                            StorageBufferUsage::IndirectArguments,
                            StorageBufferCpuAccess::Write));
            const IndirectDrawIndexedArguments command{6, 1, 0, 0, 1};
            arguments.setBytes(&command, sizeof(command));
            RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            device.SetRenderTarget(&target);
            device.Clear(kClear);
            effect.Apply();
            device.SetVertexBuffers({VertexBufferBinding(&mesh, 0, 0),
                                     VertexBufferBinding(&instances, 0, 1)});
            device.SetIndexBuffer(&quadIndices);
            device.DrawIndexedPrimitivesIndirectEXT(
                PrimitiveType::TriangleList, *arguments.getRendererEXT(), 0);
            arguments.Dispose();
            device.SetIndexBuffer(nullptr);
            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const auto probes = ReadHorizontalProbes(target);
            const bool selected = probes[0] == kClear && probes[1].getRProperty() > 200;
            check(selected,
                  "D BaseInstance selects record one and survives public buffer disposal",
                  "leftR=" + std::to_string(probes[0].getRProperty()) +
                      " rightR=" + std::to_string(probes[1].getRProperty()));
        }

        {
            constexpr StorageBufferUsage usage =
                StorageBufferUsage::Storage | StorageBufferUsage::IndirectArguments;
            StorageBuffer arguments(
                device, StorageBufferDescriptor(
                            sizeof(IndirectDrawIndexedArguments), usage,
                            StorageBufferCpuAccess::Write));
            const IndirectDrawIndexedArguments zero{};
            arguments.setBytes(&zero, sizeof(zero));
            ComputeShader writer(device, CommandProgram());
            writer.bindStorageBuffer(0, arguments);
            writer.dispatch(1);
            const auto probes = drawInstanced(arguments);
            const bool selected = probes[0] == kClear && probes[1].getRProperty() > 200;
            check(selected,
                  "E compute-generated command drives count and first instance without readback",
                      "leftR=" + std::to_string(probes[0].getRProperty()) +
                          " rightR=" + std::to_string(probes[1].getRProperty()));
        }

        {
            StorageBuffer visible(
                device, StorageBufferDescriptor(
                            sizeof(InstanceMatrix), StorageBufferUsage::Storage,
                            StorageBufferCpuAccess::None));
            constexpr StorageBufferUsage commandUsage =
                StorageBufferUsage::Storage | StorageBufferUsage::IndirectArguments;
            StorageBuffer command(
                device, StorageBufferDescriptor(
                            sizeof(IndirectDrawIndexedArguments), commandUsage,
                            StorageBufferCpuAccess::None));
            ComputeShader producer(device, SpirVProgram(kDrawStorageComputeSpirV));
            producer.bindStorageBuffer(0, visible);
            producer.bindStorageBuffer(1, command);
            ShaderEffect drawEffect(
                device, SpirVProgram(kDrawStorageVertexSpirV),
                SpirVProgram(kDrawStorageFragmentSpirV));
            ShaderEffect writableEffect(
                device, DrawStorageVertexWithoutReadonly(),
                SpirVProgram(kDrawStorageFragmentSpirV));
            const bool writableRejected =
                !writableEffect.IsEffectValid() &&
                writableEffect.GetCompileErrorEXT().find("readonly") != std::string::npos;

            check(renderer->GetMaxVertexShaderStorageBlocksEXT() > 0 &&
                      drawEffect.IsEffectValid() && writableRejected,
                  "F native SPIR-V reflects readonly vertex/fragment storage binding 6",
                  "maxVertexSSBO=" +
                      std::to_string(renderer->GetMaxVertexShaderStorageBlocksEXT()) +
                      " effect=" +
                      std::string(drawEffect.IsEffectValid()
                                      ? "valid" : drawEffect.GetCompileErrorEXT()) +
                      " writable=" + (writableRejected ? "refused" : "accepted"));

            const auto drawProduced = [&](const float x, const int count, bool& queuedDeferred,
                                          const bool disposeBuffers) {
                RenderTarget2D target(
                    device, kSize, kSize, false, SurfaceFormat::Color,
                    DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
                const std::uint64_t oneTimeBeforeQueue =
                    renderer->GetOneTimeCommandCountEXT();
                producer.setUniform("uX", x);
                producer.setUniform("uCount", count);
                producer.dispatch(1);

                device.SetRenderTarget(&target);
                device.Clear(kClear);
                drawEffect.Apply();
                renderer->BindStorageBufferForDrawEXT(6, *visible.getRendererEXT());
                device.SetVertexBuffer(&mesh);
                device.SetIndexBuffer(&quadIndices);
                device.DrawIndexedPrimitivesIndirectEXT(
                    PrimitiveType::TriangleList, *command.getRendererEXT(), 0);
                device.SetIndexBuffer(nullptr);
                device.SetVertexBuffer(nullptr);
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                if (disposeBuffers) {
                    visible.Dispose();
                    command.Dispose();
                }
                queuedDeferred =
                    renderer->GetOneTimeCommandCountEXT() == oneTimeBeforeQueue;
                return ReadHorizontalProbes(target);
            };

            device.setDepthStencilStateProperty(DepthStencilState::None);
            bool warmupDeferred = false;
            const auto warmup = drawProduced(0.0f, 0, warmupDeferred, false);
            (void) warmup;
            const std::uint64_t barriersBefore =
                renderer->GetLogicalResourceBarrierCountEXT();
            bool leftDeferred = false;
            bool rightDeferred = false;
            bool culledDeferred = false;
            const auto left = drawProduced(-0.5f, 1, leftDeferred, false);
            const auto culled = drawProduced(0.0f, 0, culledDeferred, false);
            const auto right = drawProduced(0.5f, 1, rightDeferred, true);
            const auto isRed = [](const Color& color) {
                return color.getRProperty() > 200 && color.getGProperty() < 50;
            };
            const auto isBlue = [](const Color& color) {
                return color.getBProperty() > 200 && color.getGProperty() < 50;
            };
            const bool framesFresh =
                isRed(left[0]) && left[1] == kClear &&
                right[0] == kClear && isBlue(right[1]) &&
                culled[0] == kClear && culled[1] == kClear;
            check(framesFresh,
                  "G compute output drives fresh left, right, and culled draws through disposal",
                  "leftR=(" + std::to_string(left[0].getRProperty()) + "," +
                      std::to_string(left[1].getRProperty()) + ") rightB=(" +
                      std::to_string(right[0].getBProperty()) + "," +
                      std::to_string(right[1].getBProperty()) + ") culledR=(" +
                      std::to_string(culled[0].getRProperty()) + "," +
                      std::to_string(culled[1].getRProperty()) + ")");
            const std::uint64_t barriers =
                renderer->GetLogicalResourceBarrierCountEXT() - barriersBefore;
            // Each measured iteration records four buffer hazards (two compute writes followed by
            // vertex/indirect reads) and two requested readback image transitions. MOD-2253 made
            // the latter use this same logical tracker, so the complete exact count is 3 * 6.
            check(leftDeferred && rightDeferred && culledDeferred && barriers == 18,
                  "H compute-to-graphics stays deferred with exact buffer/readback hazards",
                  "queued=" + std::string(leftDeferred ? "1" : "0") +
                      std::string(rightDeferred ? "1" : "0") +
                      std::string(culledDeferred ? "1" : "0") +
                      " barriers=" + std::to_string(barriers));
        }

        {
            Texture2D blue(device, 1, 1);
            const Color bluePixel(0, 0, 255, 255);
            blue.SetData(&bluePixel, 1);
            SpriteBatch sprites(device);

            constexpr StorageBufferUsage sourceUsage =
                StorageBufferUsage::Storage | StorageBufferUsage::TransferSource;
            constexpr StorageBufferUsage destinationUsage =
                StorageBufferUsage::TransferDestination | StorageBufferUsage::IndirectArguments;
            StorageBuffer produced(
                device, StorageBufferDescriptor(
                            sizeof(IndirectDrawIndexedArguments), sourceUsage,
                            StorageBufferCpuAccess::None));
            StorageBuffer consumed(
                device, StorageBufferDescriptor(
                            sizeof(IndirectDrawIndexedArguments), destinationUsage,
                            StorageBufferCpuAccess::Read | StorageBufferCpuAccess::Write));
            const IndirectDrawIndexedArguments zero{};
            consumed.setBytes(&zero, sizeof(zero));
            const Vertex redTriangle[3] = {
                {-1.0f, -1.0f, 0.0f, 255, 0, 0, 255},
                {-1.0f,  3.0f, 0.0f, 255, 0, 0, 255},
                { 3.0f, -1.0f, 0.0f, 255, 0, 0, 255},
            };
            const Vertex greenQuad[6] = {
                {-1.0f, -1.0f, 0.0f, 0, 255, 0, 255},
                {-1.0f,  3.0f, 0.0f, 0, 255, 0, 255},
                { 3.0f, -1.0f, 0.0f, 0, 255, 0, 255},
                {-1.0f, -1.0f, 0.0f, 0, 255, 0, 255},
                {-1.0f,  3.0f, 0.0f, 0, 255, 0, 255},
                { 3.0f, -1.0f, 0.0f, 0, 255, 0, 255},
            };
            VertexBuffer greenVertices(device, vertexLayout, 6, BufferUsage::None);
            greenVertices.SetDataRaw(greenQuad, 6, sizeof(Vertex));

            device.Clear(Color(0, 0, 0, 255));
            device.setBlendStateProperty(BlendState::Opaque);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            sprites.Begin(
                SpriteSortMode::Deferred, BlendState::Opaque,
                const_cast<SamplerState*>(&SamplerState::PointClamp),
                nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
            sprites.Draw(
                blue, Rectangle(0, 0, 64, 64), Rectangle(0, 0, 1, 1), Color::White);
            sprites.End();

            const std::uint64_t oneTimeBefore = renderer->GetOneTimeCommandCountEXT();
            const std::uint64_t submitsBefore = renderer->GetFrameSubmitCountEXT();
            {
                ComputeShader writer(device, CommandProgram());
                writer.bindStorageBuffer(0, produced);
                writer.dispatch(1);
            }
            produced.copyTo(consumed, 0, 0, sizeof(IndirectDrawArguments));
            effect.Apply();
            device.DrawUserPrimitives(
                PrimitiveType::TriangleList, redTriangle, 0, 1, vertexLayout);
            device.SetVertexBuffer(&greenVertices);
            device.DrawPrimitivesIndirectEXT(
                PrimitiveType::TriangleList, *consumed.getRendererEXT(), 0);
            device.SetVertexBuffer(nullptr);

            Color center(0, 0, 0, 0);
            const Rectangle centerRect(32, 32, 1, 1);
            device.GetBackBufferData(&centerRect, &center, 0, 1);
            IndirectDrawArguments observed{};
            consumed.getBytes(&observed, sizeof(observed));
            const bool finalGreen = center.getGProperty() > 200 &&
                                    center.getRProperty() < 50 && center.getBProperty() < 50;
            check(finalGreen,
                  "I mixed SpriteBatch, compute, copy, XNA 3D, indirect, present order",
                  "center=(" + std::to_string(center.getRProperty()) + "," +
                      std::to_string(center.getGProperty()) + "," +
                      std::to_string(center.getBProperty()) + ") command=(" +
                      std::to_string(observed.VertexCount) + "," +
                      std::to_string(observed.InstanceCount) + "," +
                      std::to_string(observed.FirstVertex) + "," +
                      std::to_string(observed.BaseInstance) + ")");
            check(renderer->GetOneTimeCommandCountEXT() == oneTimeBefore &&
                      renderer->GetFrameSubmitCountEXT() == submitsBefore + 1,
                  "J routine compute and copy share the frame submission",
                  "oneTime=" + std::to_string(oneTimeBefore) + "->" +
                      std::to_string(renderer->GetOneTimeCommandCountEXT()) +
                      " frameSubmits=" + std::to_string(submitsBefore) + "->" +
                      std::to_string(renderer->GetFrameSubmitCountEXT()));
        }

        const std::size_t validationAfter = renderer->GetValidationMessagesEXT().size();
        check(validationAfter == validationBefore,
              "K indirect and mixed-order work add no Vulkan validation messages",
              std::to_string(validationBefore) + " -> " +
                  std::to_string(validationAfter));

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanIndirectDrawTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }

    int Result() const noexcept { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanIndirectDrawTest game;
    game.Run();
    return game.Result();
}
