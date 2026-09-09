// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2245: native Vulkan indirect drawing.
//
// A  Public/renderer capability agrees with enabled drawIndirectFirstInstance.
// B  A CPU command at a non-zero argument offset preserves VertexBufferBinding + FirstVertex.
// C  The indexed command preserves binding offset + FirstIndex + BaseVertex independently.
// D  BaseInstance selects one retained per-instance record even after public buffer disposal.
// E  A compute shader writes the indexed command and the draw consumes it without CPU readback.
// F  No Vulkan validation message is added by the complete exercise.

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
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
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
            StorageBuffer arguments(device, sizeof(commands));
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
            check(red == kSize * kSize,
                  "B CPU non-indexed command preserves both byte and vertex offsets",
                  std::to_string(red) + "/" + std::to_string(kSize * kSize) + " red pixels");
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
            StorageBuffer arguments(device, sizeof(commands));
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
            StorageBuffer arguments(device, sizeof(IndirectDrawIndexedArguments));
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

        const std::size_t validationAfter = renderer->GetValidationMessagesEXT().size();
        check(validationAfter == validationBefore,
              "F indirect draws add no Vulkan validation messages",
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
