// SPDX-License-Identifier: MS-PL
// REMED-GFX-104/105/106/108/109/112: deferred indexed-draw correctness, native alignment,
// native index width, triangle-strip pipeline compatibility, buffer versions, and renderer parity.

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

// Lets CNA_RENDERER_IS name identities bare, matching the guards it replaced.
using namespace CNA::Testing::Renderers;

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

// plans/plan_runtimerenderer.md RTR-P9-9: these three blocks need their renderer's own headers and
// types, so they stay COMPILE-time -- no runtime predicate makes a type exist. The condition
// widens from the DEFAULT renderer's macro to "compiled into this build", so a multi-renderer
// build that holds one of these without selecting it still compiles its checks. Every test that
// uses them is gated at runtime on the renderer actually being ACTIVE.
#if defined(CNA_RENDERER_BGFX) || defined(CNA_RENDERER_PRESENT_BGFX)
#define CNA_TEST_BGFX_AVAILABLE 1
#endif
#if defined(CNA_RENDERER_WEBGPU) || defined(CNA_RENDERER_PRESENT_WEBGPU)
#define CNA_TEST_WEBGPU_AVAILABLE 1
#endif
#if defined(CNA_RENDERER_VULKAN) || defined(CNA_RENDERER_PRESENT_VULKAN)
#define CNA_TEST_VULKAN_AVAILABLE 1
#endif

#ifdef CNA_TEST_BGFX_AVAILABLE
#include "CNA/Internal/Renderers/Bgfx/BgfxRenderer.hpp"
#endif
#ifdef CNA_TEST_WEBGPU_AVAILABLE
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#endif
#ifdef CNA_TEST_VULKAN_AVAILABLE
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#endif

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DynamicIndexBuffer;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    VertexDeclaration PositionColorDeclaration()
    {
        return VertexDeclaration(
            16,
            {
                VertexElement(
                    0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(
                    12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            });
    }

    std::array<VertexPositionColor, 3> TriangleAt(float centerX, const Color& color)
    {
        return {
            VertexPositionColor(Vector3(centerX - 0.24f, -0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX + 0.24f, -0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX, 0.45f, 0.5f), color),
        };
    }

    std::array<VertexPositionColor, 3> CenterTriangle(const Color& color)
    {
        return TriangleAt(0.0f, color);
    }

    std::array<VertexPositionColor, 3> StripTriangleAt(
        float centerX,
        const Color& color)
    {
        return {
            VertexPositionColor(Vector3(centerX - 0.18f,  0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX - 0.18f, -0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX + 0.18f,  0.00f, 0.5f), color),
        };
    }

    std::array<VertexPositionColor, 4> StripQuadAt(
        float centerX,
        const Color& color)
    {
        // REMED-GFX-183: clockwise-as-displayed is XNA's front face (GFX-160). A strip must
        // therefore begin TL,TR,BL,BR: fixed-function parity turns its second primitive into
        // BL,TR,BR with the same winding. The former TL,BL,TR,BR order was the reversed,
        // counter-clockwise source and made the GFX-183 culling test assert the opposite oracle.
        return {
            VertexPositionColor(Vector3(centerX - 0.18f,  0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX + 0.18f,  0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX - 0.18f, -0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX + 0.18f, -0.45f, 0.5f), color),
        };
    }

    std::array<VertexPositionColor, 5> FiveVertexStripAt(
        float centerX,
        const Color& color)
    {
        return {
            VertexPositionColor(Vector3(centerX - 0.18f,  0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX - 0.18f, -0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX,          0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX,         -0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX + 0.18f,  0.45f, 0.5f), color),
        };
    }

    template<std::size_t N>
    void AppendVertices(
        std::vector<VertexPositionColor>& destination,
        const std::array<VertexPositionColor, N>& source)
    {
        destination.insert(destination.end(), source.begin(), source.end());
    }

    bool ColorNear(const Color& actual, const Color& expected, int tolerance = 16)
    {
        return std::abs(actual.getRProperty() - expected.getRProperty()) <= tolerance &&
               std::abs(actual.getGProperty() - expected.getGProperty()) <= tolerance &&
               std::abs(actual.getBProperty() - expected.getBProperty()) <= tolerance;
    }

    Color ReadCenter(GraphicsDevice& device)
    {
        const auto& viewport = device.getViewportProperty();
        const Rectangle region(
            viewport.getWidthProperty() / 2,
            viewport.getHeightProperty() / 2,
            1,
            1);
        Color pixel = Color::Transparent;
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    Color ReadAtNdc(GraphicsDevice& device, float x)
    {
        const auto& viewport = device.getViewportProperty();
        const int pixelX = static_cast<int>(
            (x * 0.5f + 0.5f) * static_cast<float>(viewport.getWidthProperty() - 1));
        const Rectangle region(
            pixelX,
            viewport.getHeightProperty() / 2,
            1,
            1);
        Color pixel = Color::Transparent;
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    struct BackbufferSnapshot
    {
        int width = 0;
        int height = 0;
        std::vector<Color> pixels;

        [[nodiscard]] Color AtNdc(float x, float y = 0.0f) const
        {
            const int pixelX = std::clamp(
                static_cast<int>(
                    (x * 0.5f + 0.5f) * static_cast<float>(width - 1)),
                0,
                width - 1);
            const int pixelY = std::clamp(
                static_cast<int>(
                    ((-y) * 0.5f + 0.5f) * static_cast<float>(height - 1)),
                0,
                height - 1);
            return pixels[
                static_cast<std::size_t>(pixelY) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(pixelX)];
        }
    };

    BackbufferSnapshot ReadBackbufferOnce(GraphicsDevice& device)
    {
        const auto& viewport = device.getViewportProperty();
        BackbufferSnapshot snapshot;
        snapshot.width = viewport.getWidthProperty();
        snapshot.height = viewport.getHeightProperty();
        snapshot.pixels.assign(
            static_cast<std::size_t>(snapshot.width) *
                static_cast<std::size_t>(snapshot.height),
            Color::Transparent);
        const Rectangle region(0, 0, snapshot.width, snapshot.height);
        device.GetBackBufferData(
            &region,
            snapshot.pixels.data(),
            0,
            static_cast<int>(snapshot.pixels.size()));
        return snapshot;
    }

    void SampleRenderTargetToBackbuffer(
        GraphicsDevice& device,
        RenderTarget2D& target)
    {
        const std::array<Microsoft::Xna::Framework::Graphics::VertexPositionTexture, 6>
            targetQuad{
                Microsoft::Xna::Framework::Graphics::VertexPositionTexture(
                    Vector3(-1.0f, 1.0f, 0.0f),
                    Microsoft::Xna::Framework::Vector2(0.0f, 0.0f)),
                Microsoft::Xna::Framework::Graphics::VertexPositionTexture(
                    Vector3(-1.0f, -1.0f, 0.0f),
                    Microsoft::Xna::Framework::Vector2(0.0f, 1.0f)),
                Microsoft::Xna::Framework::Graphics::VertexPositionTexture(
                    Vector3(1.0f, -1.0f, 0.0f),
                    Microsoft::Xna::Framework::Vector2(1.0f, 1.0f)),
                Microsoft::Xna::Framework::Graphics::VertexPositionTexture(
                    Vector3(-1.0f, 1.0f, 0.0f),
                    Microsoft::Xna::Framework::Vector2(0.0f, 0.0f)),
                Microsoft::Xna::Framework::Graphics::VertexPositionTexture(
                    Vector3(1.0f, -1.0f, 0.0f),
                    Microsoft::Xna::Framework::Vector2(1.0f, 1.0f)),
                Microsoft::Xna::Framework::Graphics::VertexPositionTexture(
                    Vector3(1.0f, 1.0f, 0.0f),
                    Microsoft::Xna::Framework::Vector2(1.0f, 0.0f)),
            };
        BasicEffect sampleEffect(device);
        sampleEffect.setTextureEnabledProperty(true);
        sampleEffect.setTextureProperty(&target);
        device.Clear(Color::Black);
        sampleEffect.Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, targetQuad.data(), 0, 2);
    }

    void ExpectExactColor(
        const Color& actual,
        const Color& expected,
        const char* label)
    {
        EXPECT_EQ(expected.getRProperty(), actual.getRProperty()) << label;
        EXPECT_EQ(expected.getGProperty(), actual.getGProperty()) << label;
        EXPECT_EQ(expected.getBProperty(), actual.getBProperty()) << label;
        EXPECT_EQ(expected.getAProperty(), actual.getAProperty()) << label;
    }

    void ExpectExactColorNear(
        const BackbufferSnapshot& snapshot,
        float x,
        float y,
        const Color& expected,
        const char* label)
    {
        const int centerX = std::clamp(
            static_cast<int>(
                (x * 0.5f + 0.5f) * static_cast<float>(snapshot.width - 1)),
            0,
            snapshot.width - 1);
        const int centerY = std::clamp(
            static_cast<int>(
                ((-y) * 0.5f + 0.5f) * static_cast<float>(snapshot.height - 1)),
            0,
            snapshot.height - 1);
        bool found = false;
        for (int offsetY = -2; offsetY <= 2 && !found; ++offsetY)
        {
            const int pixelY = std::clamp(
                centerY + offsetY, 0, snapshot.height - 1);
            for (int offsetX = -2; offsetX <= 2; ++offsetX)
            {
                const int pixelX = std::clamp(
                    centerX + offsetX, 0, snapshot.width - 1);
                const Color& actual = snapshot.pixels[
                    static_cast<std::size_t>(pixelY) *
                        static_cast<std::size_t>(snapshot.width) +
                    static_cast<std::size_t>(pixelX)];
                if (actual == expected)
                {
                    found = true;
                    break;
                }
            }
        }
        EXPECT_TRUE(found) << label << " (no exact RGBA pixel in the 5x5 raster probe)";
    }

    class IndexedDrawDeferredTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        // GTEST_SKIP() only unwinds the function it is called from; called from an ordinary
        // member function like RequireIndexedRendering() below it cannot skip the test body that
        // invokes it. SetUp() is where GoogleTest itself checks for a skip, so the capability gate
        // has to run here too -- RequireIndexedRendering() keeps its own copy for the state-setup
        // calls that follow it, which only run once SetUp() has already let the test proceed.
        void SetUp() override
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support indexed rendering";
        }

        void RequireIndexedRendering()
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support indexed rendering";
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
        }

        void ApplyVertexColorEffect(BasicEffect& effect)
        {
            effect.VertexColorEnabled = true;
            effect.Apply();
        }
    };

#ifdef CNA_TEST_BGFX_AVAILABLE
    CNA::Internal::Renderers::Bgfx::BgfxIndexBufferRenderer* GetBgfxIndexRenderer(
        IndexBuffer& buffer)
    {
        return dynamic_cast<
            CNA::Internal::Renderers::Bgfx::BgfxIndexBufferRenderer*>(
                &buffer.GetRenderer());
    }

    void ExpectExactBgfxIndexFlags(IndexBuffer& buffer, bool thirtyTwoBit)
    {
        auto* native = GetBgfxIndexRenderer(buffer);
        ASSERT_NE(nullptr, native);
        const std::uint16_t expected =
            BGFX_BUFFER_ALLOW_RESIZE |
            (thirtyTwoBit ? BGFX_BUFFER_INDEX32 : BGFX_BUFFER_NONE);
        EXPECT_EQ(thirtyTwoBit, native->IsThirtyTwoBit());
        EXPECT_EQ(expected, native->GetNativeCreationFlagsEXT());
        EXPECT_EQ(
            thirtyTwoBit,
            0u != (native->GetNativeCreationFlagsEXT() & BGFX_BUFFER_INDEX32));
    }
#endif

#ifdef CNA_TEST_WEBGPU_AVAILABLE
    struct WebGpuErrorScopeState
    {
        bool completed = false;
        WGPUPopErrorScopeStatus status = WGPUPopErrorScopeStatus_Error;
        WGPUErrorType type = WGPUErrorType_Unknown;
        std::string message;
    };

    void OnWebGpuErrorScope(WGPUPopErrorScopeStatus status,
                            WGPUErrorType type,
                            WGPUStringView message,
                            void* userdata1,
                            void*)
    {
        auto& state = *static_cast<WebGpuErrorScopeState*>(userdata1);
        state.status = status;
        state.type = type;
        if (message.data != nullptr)
        {
            if (message.length == WGPU_STRLEN)
                state.message = message.data;
            else
                state.message.assign(message.data, message.length);
        }
        state.completed = true;
    }

    void PopAndExpectClean(
        CNA::Internal::Renderers::WebGPU::WebGPURenderer& renderer)
    {
        WebGpuErrorScopeState state;
        WGPUPopErrorScopeCallbackInfo callback{};
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.callback = OnWebGpuErrorScope;
        callback.userdata1 = &state;
        wgpuDevicePopErrorScope(renderer.Device(), callback);
        for (int attempt = 0; attempt < 10000 && !state.completed; ++attempt)
            wgpuInstanceProcessEvents(renderer.Instance());

        ASSERT_TRUE(state.completed) << "wgpu-native did not complete the error scope";
        ASSERT_EQ(WGPUPopErrorScopeStatus_Success, state.status)
            << "wgpu-native error-scope status=" << static_cast<int>(state.status)
            << " type=" << static_cast<int>(state.type)
            << "\ncomplete message:\n" << state.message;
        ASSERT_EQ(WGPUErrorType_NoError, state.type)
            << "wgpu-native error-scope status=" << static_cast<int>(state.status)
            << " type=" << static_cast<int>(state.type)
            << "\ncomplete message:\n" << state.message;
        ASSERT_TRUE(state.message.empty())
            << "wgpu-native returned a message for a clean scope:\n" << state.message;
    }

    template<typename T>
    void ExpectExactWebGpuIndexShadow(IndexBuffer& buffer,
                                      const std::vector<T>& indices)
    {
        auto* renderer =
            dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPUIndexBufferRenderer*>(
                &buffer.GetRenderer());
        ASSERT_NE(nullptr, renderer);
        ASSERT_EQ(
            indices.size() * sizeof(T),
            renderer->ShadowData().size());
        EXPECT_EQ(0, std::memcmp(
            indices.data(), renderer->ShadowData().data(),
            renderer->ShadowData().size()));
        EXPECT_EQ(static_cast<int>(indices.size()), renderer->GetIndexCount());
    }
#endif

#ifdef CNA_TEST_VULKAN_AVAILABLE
    void AssertNoNewVulkanValidationMessages(
        const CNA::Internal::Renderers::Vulkan::VulkanRenderer& renderer,
        std::size_t firstMessage)
    {
        // REMED-GFX-144: an empty message list means "no validation problem" ONLY if the layer is
        // really there. When VK_LAYER_KHRONOS_validation is missing, CreateInstance() clears
        // sEnableValidation, no debug messenger is installed and this vector stays empty forever --
        // so every assertion below would pass vacuously. Fail loudly on that instead.
        //
        // This helper deliberately keeps STANDARD validation only. The Khronos synchronization
        // checks are a separate opt-in (VulkanRenderer::SetSyncValidationEnabledEXT) whose
        // hazards belong to whichever pass records them; REMED-GFX-144's acquire hazard was
        // invisible here for exactly that reason, and modules/renderers/vulkan/examples/vulkan_swapchain_sync_test.cpp is
        // the file that enforces it.
        ASSERT_TRUE(CNA::Internal::Renderers::Vulkan::VulkanRenderer::IsValidationActiveEXT())
            << "Vulkan validation layer is not active, so a zero message count proves nothing "
               "(REMED-GFX-112 made these messages fatal; REMED-GFX-144 made their absence "
               "meaningful)";
        const auto& messages = renderer.GetValidationMessagesEXT();
        std::string completeMessages;
        for (std::size_t i = firstMessage; i < messages.size(); ++i)
        {
            completeMessages += "\n--- Vulkan validation message ---\n";
            completeMessages += messages[i];
        }
        ASSERT_EQ(firstMessage, messages.size())
            << "Vulkan validation warning/error made fatal by REMED-GFX-112:"
            << completeMessages;
    }
#endif
}

// REMED-GFX-110 adds CNA_RENDERER_SOFTWARE: the CPU raster paths owe the same public addressing
// contract as the GPU renderers -- startIndex as an index-element offset, baseVertex added exactly
// once, primitiveCount limiting the consumed range, and hints that never change addressing.
TEST_F(IndexedDrawDeferredTest, PersistentDrawHonorsNonzeroStartIndex)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, WebGPU, Vulkan, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, DirectX9, DirectX11, Software);
    RequireIndexedRendering();

    const auto center = TriangleAt(0.0f, Color::Lime);
    const auto left = TriangleAt(-0.65f, Color::Red);
    const auto right = TriangleAt(0.65f, Color::Blue);
    const std::array<VertexPositionColor, 9> vertices{
        center[0], center[1], center[2],
        left[0], left[1], left[2],
        right[0], right[1], right[2],
    };
    // The prefix and suffix are valid, visible decoys. Only the middle range may be consumed.
    const std::array<std::uint16_t, 9> indices{
        0, 1, 2, 3, 4, 5, 6, 7, 8,
    };
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 9, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 9, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 9);
    indexBuffer.SetData(indices.data(), 9);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        0,
        3,
        3,
        3,
        1);

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.65f), Color::Red, "selected nonzero startIndex");
    ExpectExactColor(pixels.AtNdc(0.0f), Color::Black, "unselected index prefix");
    ExpectExactColor(pixels.AtNdc(0.65f), Color::Black, "unselected index suffix");
}

TEST_F(IndexedDrawDeferredTest, PersistentDrawHonorsPositiveBaseVertexWithSixteenBitIndices)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, WebGPU, Vulkan, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, DirectX9, DirectX11, Software);
    RequireIndexedRendering();

    const auto red = CenterTriangle(Color::Red);
    const auto blue = CenterTriangle(Color::Blue);
    const std::array<VertexPositionColor, 6> vertices{
        red[0], red[1], red[2], blue[0], blue[1], blue[2],
    };
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 3);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        3,
        0,
        3,
        0,
        1);

    ExpectExactColor(
        ReadCenter(device), Color::Blue,
        "positive baseVertex with 16-bit indices");
}

TEST_F(IndexedDrawDeferredTest, PersistentDynamicDrawCombinesStartBaseCountAndHints)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, WebGPU, Vulkan, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, DirectX9, DirectX11, Software);
    RequireIndexedRendering();

    const auto ignoredWithoutBase = TriangleAt(-0.75f, Color::Lime);
    const auto prefixWithBase = TriangleAt(-0.25f, Color::Yellow);
    const auto selected = TriangleAt(0.25f, Color::Red);
    const auto suffixWithBase = TriangleAt(0.75f, Color::Blue);
    std::vector<VertexPositionColor> vertices;
    AppendVertices(vertices, ignoredWithoutBase);
    AppendVertices(vertices, prefixWithBase);
    AppendVertices(vertices, selected);
    AppendVertices(vertices, suffixWithBase);
    const std::array<std::uint16_t, 9> indices{
        0, 1, 2,
        3, 4, 5,
        6, 7, 8,
    };
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(vertices.size()), BufferUsage::None);
    DynamicIndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 9, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
    indexBuffer.SetData(indices.data(), 0, 9, SetDataOptions::Discard);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        3,
        3,
        3,
        3,
        1);

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(
        pixels.AtNdc(0.25f), Color::Red,
        "combined startIndex/baseVertex selected range");
    ExpectExactColor(
        pixels.AtNdc(-0.75f), Color::Black,
        "combined range ignored unbased prefix");
    ExpectExactColor(
        pixels.AtNdc(-0.25f), Color::Black,
        "combined range ignored based prefix");
    ExpectExactColor(
        pixels.AtNdc(0.75f), Color::Black,
        "primitiveCount ignored based suffix");
}

TEST_F(IndexedDrawDeferredTest, PersistentDrawTreatsVertexRangesAsHints)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, WebGPU, Vulkan, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, DirectX9, DirectX11, Software);
    RequireIndexedRendering();

    const auto decoy = CenterTriangle(Color::Red);
    const auto selected = CenterTriangle(Color::Blue);
    const std::array<VertexPositionColor, 6> vertices{
        decoy[0], decoy[1], decoy[2],
        selected[0], selected[1], selected[2],
    };
    const std::array<std::uint16_t, 3> indices{3, 4, 5};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 3);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(-0.65f, 0.0f, 0.0f));
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 3, 3, 0, 1);

    effect.setWorldProperty(Microsoft::Xna::Framework::Matrix::getIdentityProperty());
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 6, 0, 1);

    // The native vertex binding cannot begin at minVertexIndex without rebasing every decoded
    // index. Preserve correct addressing even when the caller supplies an overly narrow hint.
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(0.65f, 0.0f, 0.0f));
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 1, 0, 1);

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.65f), Color::Blue, "exact vertex range hint");
    ExpectExactColor(pixels.AtNdc(0.0f), Color::Blue, "loose vertex range hint");
    ExpectExactColor(
        pixels.AtNdc(0.65f), Color::Blue,
        "narrow hint does not replace decoded-index addressing");
}

TEST_F(IndexedDrawDeferredTest, PersistentDrawHonorsThirtyTwoBitIndexElements)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, WebGPU, Vulkan, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, DirectX9, DirectX11, Software);
    RequireIndexedRendering();

    const auto red = CenterTriangle(Color::Red);
    const auto blue = CenterTriangle(Color::Blue);
    const std::array<VertexPositionColor, 6> vertices{
        red[0], red[1], red[2], blue[0], blue[1], blue[2],
    };
    const std::array<std::uint32_t, 3> indices{3, 4, 5};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 3);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        0,
        3,
        3,
        0,
        1);

    ExpectExactColor(
        ReadCenter(device), Color::Blue,
        "32-bit index element width");
}

TEST_F(IndexedDrawDeferredTest, PublicStaticThirtyTwoBitIndicesAbove65535RenderExactGeometry)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, WebGPU, Vulkan, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, DirectX9, DirectX11, SdlGpu, Software);
    RequireIndexedRendering();

    constexpr std::uint32_t highVertex = 65536u;
    const auto falselyDecoded = CenterTriangle(Color::Lime);
    const auto genuinelyThirtyTwoBit = CenterTriangle(Color::Blue);
    std::vector<VertexPositionColor> vertices(
        static_cast<std::size_t>(highVertex) + 3u,
        VertexPositionColor(Vector3(4.0f, 4.0f, 0.5f), Color::Black));
    std::copy(falselyDecoded.begin(), falselyDecoded.end(), vertices.begin());
    std::copy(
        genuinelyThirtyTwoBit.begin(),
        genuinelyThirtyTwoBit.end(),
        vertices.begin() + highVertex);

    // On a falsely 16-bit native handle, the first three little-endian words are 0, 1, 2,
    // selecting the visible lime decoy. Genuine 32-bit decoding selects the high blue triangle.
    const std::array<std::uint32_t, 3> indices{
        highVertex,
        highVertex + 2u,
        highVertex + 1u,
    };
    VertexBuffer vertexBuffer(
        device,
        PositionColorDeclaration(),
        static_cast<int>(vertices.size()),
        BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
    indexBuffer.SetData(indices.data(), 3);

    EXPECT_EQ(
        IndexElementSize::ThirtyTwoBits,
        indexBuffer.getIndexElementSizeProperty());
    EXPECT_EQ(3, indexBuffer.getIndexCountProperty());
    std::array<std::uint32_t, 3> shadow{};
    indexBuffer.GetData(shadow.data(), 3);
    EXPECT_EQ(indices, shadow);

#ifdef CNA_TEST_BGFX_AVAILABLE
    auto* native =
        dynamic_cast<CNA::Internal::Renderers::Bgfx::BgfxIndexBufferRenderer*>(
            &indexBuffer.GetRenderer());
    ASSERT_NE(nullptr, native);
    static_assert(std::is_same_v<
                  decltype(native->handle),
                  bgfx::DynamicIndexBufferHandle>);
    ASSERT_TRUE(bgfx::isValid(native->handle));
    ExpectExactBgfxIndexFlags(indexBuffer, true);
    const std::uint16_t creationFlags = native->GetNativeCreationFlagsEXT();
    EXPECT_NE(0u, creationFlags & BGFX_BUFFER_INDEX32);
    ASSERT_EQ(indices.size() * sizeof(std::uint32_t), native->cpuData.size());
    EXPECT_EQ(
        0,
        std::memcmp(indices.data(), native->cpuData.data(), native->cpuData.size()));
    std::cout
        << "REMED-GFX-108 public static: logical format=ThirtyTwoBits"
        << ", logical count=3, logical bytes=" << sizeof(indices)
        << ", native handle=DynamicIndexBufferHandle"
        << ", native creation flags=0x" << std::hex << creationFlags << std::dec
        << ", uploaded bytes=" << native->cpuData.size()
        << ", draw=TriangleList startIndex=0 baseVertex=0 primitiveCount=1"
        << "\n";
#endif

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        0,
        static_cast<int>(highVertex),
        3,
        0,
        1);

    // plans/plan_runtimerenderer.md RTR-P9-5: SDL_GPU has no backbuffer readback, so it is the one
    // renderer with nothing to compare here.
    if (!CNA_RENDERER_IS(SdlGpu))
    {
        ExpectExactColor(
            ReadCenter(device),
            Color::Blue,
            "public static Uint32 values above 65535");
    }
}

#ifdef CNA_TEST_BGFX_AVAILABLE
TEST_F(IndexedDrawDeferredTest, PublicBufferKindsUseExactFixedBgfxIndexFlags)
{
    RequireIndexedRendering();

    IndexBuffer static16(
        device, IndexElementSize::SixteenBits, 1, BufferUsage::None);
    DynamicIndexBuffer dynamic16(
        device, IndexElementSize::SixteenBits, 1, BufferUsage::None);
    IndexBuffer static32(
        device, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None);
    DynamicIndexBuffer dynamic32(
        device, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None);

    ExpectExactBgfxIndexFlags(static16, false);
    ExpectExactBgfxIndexFlags(dynamic16, false);
    ExpectExactBgfxIndexFlags(static32, true);
    ExpectExactBgfxIndexFlags(dynamic32, true);

    EXPECT_EQ(IndexElementSize::SixteenBits, static16.getIndexElementSizeProperty());
    EXPECT_EQ(IndexElementSize::SixteenBits, dynamic16.getIndexElementSizeProperty());
    EXPECT_EQ(IndexElementSize::ThirtyTwoBits, static32.getIndexElementSizeProperty());
    EXPECT_EQ(IndexElementSize::ThirtyTwoBits, dynamic32.getIndexElementSizeProperty());
    EXPECT_EQ(1, static16.getIndexCountProperty());
    EXPECT_EQ(1, dynamic16.getIndexCountProperty());
    EXPECT_EQ(1, static32.getIndexCountProperty());
    EXPECT_EQ(1, dynamic32.getIndexCountProperty());

    const std::array<std::uint16_t, 1> source16{65535u};
    const std::array<std::uint32_t, 1> source32{65536u};
    static16.SetData(source16.data(), 1);
    dynamic16.SetData(source16.data(), 0, 1, SetDataOptions::Discard);
    static32.SetData(source32.data(), 1);
    dynamic32.SetData(source32.data(), 0, 1, SetDataOptions::Discard);

    auto* static16Native = GetBgfxIndexRenderer(static16);
    auto* dynamic16Native = GetBgfxIndexRenderer(dynamic16);
    auto* static32Native = GetBgfxIndexRenderer(static32);
    auto* dynamic32Native = GetBgfxIndexRenderer(dynamic32);
    ASSERT_NE(nullptr, static16Native);
    ASSERT_NE(nullptr, dynamic16Native);
    ASSERT_NE(nullptr, static32Native);
    ASSERT_NE(nullptr, dynamic32Native);
    static_assert(std::is_same_v<
                  decltype(static16Native->handle),
                  bgfx::DynamicIndexBufferHandle>);

    const std::uint16_t static16Handle = static16Native->handle.idx;
    const std::uint16_t dynamic16Handle = dynamic16Native->handle.idx;
    const std::uint16_t static32Handle = static32Native->handle.idx;
    const std::uint16_t dynamic32Handle = dynamic32Native->handle.idx;
    ASSERT_EQ(sizeof(source16), static16Native->cpuData.size());
    ASSERT_EQ(sizeof(source16), dynamic16Native->cpuData.size());
    ASSERT_EQ(sizeof(source32), static32Native->cpuData.size());
    ASSERT_EQ(sizeof(source32), dynamic32Native->cpuData.size());
    EXPECT_EQ(0, std::memcmp(
        source16.data(), static16Native->cpuData.data(), sizeof(source16)));
    EXPECT_EQ(0, std::memcmp(
        source16.data(), dynamic16Native->cpuData.data(), sizeof(source16)));
    EXPECT_EQ(0, std::memcmp(
        source32.data(), static32Native->cpuData.data(), sizeof(source32)));
    EXPECT_EQ(0, std::memcmp(
        source32.data(), dynamic32Native->cpuData.data(), sizeof(source32)));

    // GFX-054's public empty-upload contract returns before renderer dispatch. It therefore
    // preserves the one-index shadow, handle identity, element width, and exact native flags.
    static16.SetData(static_cast<const std::uint16_t*>(nullptr), 0);
    dynamic16.SetData(
        static_cast<const std::uint16_t*>(nullptr),
        0,
        0,
        SetDataOptions::NoOverwrite);
    static32.SetData(static_cast<const std::uint32_t*>(nullptr), 0);
    dynamic32.SetData(
        static_cast<const std::uint32_t*>(nullptr),
        0,
        0,
        SetDataOptions::NoOverwrite);
    EXPECT_EQ(static16Handle, static16Native->handle.idx);
    EXPECT_EQ(dynamic16Handle, dynamic16Native->handle.idx);
    EXPECT_EQ(static32Handle, static32Native->handle.idx);
    EXPECT_EQ(dynamic32Handle, dynamic32Native->handle.idx);
    ExpectExactBgfxIndexFlags(static16, false);
    ExpectExactBgfxIndexFlags(dynamic16, false);
    ExpectExactBgfxIndexFlags(static32, true);
    ExpectExactBgfxIndexFlags(dynamic32, true);

    std::array<std::uint16_t, 1> shadow16{};
    std::array<std::uint32_t, 1> shadow32{};
    static16.GetData(shadow16.data(), 1);
    EXPECT_EQ(source16, shadow16);
    dynamic16.GetData(shadow16.data(), 1);
    EXPECT_EQ(source16, shadow16);
    static32.GetData(shadow32.data(), 1);
    EXPECT_EQ(source32, shadow32);
    dynamic32.GetData(shadow32.data(), 1);
    EXPECT_EQ(source32, shadow32);

    // Renderer misuse is rejected instead of truncating 32-bit values or widening 16-bit data.
    EXPECT_THROW(static16Native->SetData32(source32.data(), 1), std::runtime_error);
    EXPECT_THROW(static32Native->SetData16(source16.data(), 1), std::runtime_error);

    std::cout
        << "REMED-GFX-108 bgfx flags: static16=0x" << std::hex
        << static16Native->GetNativeCreationFlagsEXT()
        << ", dynamic16=0x" << dynamic16Native->GetNativeCreationFlagsEXT()
        << ", static32=0x" << static32Native->GetNativeCreationFlagsEXT()
        << ", dynamic32=0x" << dynamic32Native->GetNativeCreationFlagsEXT()
        << std::dec
        << "; native handle type=DynamicIndexBufferHandle; one-index bytes=2/4\n";
}
#endif

#ifdef CNA_TEST_BGFX_AVAILABLE
TEST_F(IndexedDrawDeferredTest, PublicThirtyTwoBitDrawHonorsCompleteRangeBaseCountAndHints)
{
    RequireIndexedRendering();

    constexpr std::uint32_t highVertex = 65536u;
    std::vector<VertexPositionColor> vertices(
        static_cast<std::size_t>(highVertex) + 12u,
        VertexPositionColor(Vector3(4.0f, 4.0f, 0.5f), Color::Black));
    const auto unbasedPrefix = TriangleAt(-0.75f, Color::Lime);
    const auto basedPrefix = TriangleAt(-0.25f, Color::Yellow);
    const auto selected = TriangleAt(0.25f, Color::Red);
    const auto basedSuffix = TriangleAt(0.75f, Color::Blue);
    std::copy(
        unbasedPrefix.begin(),
        unbasedPrefix.end(),
        vertices.begin() + highVertex);
    std::copy(
        basedPrefix.begin(),
        basedPrefix.end(),
        vertices.begin() + highVertex + 3u);
    std::copy(
        selected.begin(),
        selected.end(),
        vertices.begin() + highVertex + 6u);
    std::copy(
        basedSuffix.begin(),
        basedSuffix.end(),
        vertices.begin() + highVertex + 9u);

    const std::array<std::uint32_t, 9> rangedIndices{
        highVertex, highVertex + 1u, highVertex + 2u,
        highVertex + 3u, highVertex + 4u, highVertex + 5u,
        highVertex + 6u, highVertex + 7u, highVertex + 8u,
    };
    const std::array<std::uint32_t, 3> hintIndices{
        highVertex + 9u,
        highVertex + 10u,
        highVertex + 11u,
    };
    VertexBuffer vertexBuffer(
        device,
        PositionColorDeclaration(),
        static_cast<int>(vertices.size()),
        BufferUsage::None);
    DynamicIndexBuffer rangedBuffer(
        device, IndexElementSize::ThirtyTwoBits, 9, BufferUsage::None);
    IndexBuffer hintBuffer(
        device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
    rangedBuffer.SetData(
        rangedIndices.data(), 0, 9, SetDataOptions::Discard);
    hintBuffer.SetData(hintIndices.data(), 3);

#ifdef CNA_TEST_BGFX_AVAILABLE
    ExpectExactBgfxIndexFlags(rangedBuffer, true);
    ExpectExactBgfxIndexFlags(hintBuffer, true);
#endif

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    // Nonzero startIndex, positive baseVertex, and exact primitiveCount select only the
    // middle logical 32-bit triangle. All decoded values remain above 65535.
    device.SetIndexBuffer(&rangedBuffer);
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(0.0f, 0.5f, 0.0f));
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        3,
        static_cast<int>(highVertex + 3u),
        3,
        3,
        1);

    // Exact, loose, and deliberately narrow hints must preserve identical high-index
    // addressing. These calls also exercise startIndex/baseVertex zero.
    device.SetIndexBuffer(&hintBuffer);
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(-1.5f, -0.5f, 0.0f));
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        0,
        static_cast<int>(highVertex + 9u),
        3,
        0,
        1);
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(-0.75f, -0.5f, 0.0f));
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        0,
        0,
        static_cast<int>(vertices.size()),
        0,
        1);
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(0.0f, -0.5f, 0.0f));
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        0,
        0,
        1,
        0,
        1);

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(
        pixels.AtNdc(0.25f, 0.5f),
        Color::Red,
        "public Uint32 combined range selected");
    ExpectExactColor(
        pixels.AtNdc(-0.75f, 0.5f),
        Color::Black,
        "public Uint32 unbased prefix excluded");
    ExpectExactColor(
        pixels.AtNdc(-0.25f, 0.5f),
        Color::Black,
        "public Uint32 based prefix excluded");
    ExpectExactColor(
        pixels.AtNdc(0.75f, 0.5f),
        Color::Black,
        "public Uint32 primitiveCount suffix excluded");
    ExpectExactColor(
        pixels.AtNdc(-0.75f, -0.5f),
        Color::Blue,
        "public Uint32 exact hint");
    ExpectExactColor(
        pixels.AtNdc(0.0f, -0.5f),
        Color::Blue,
        "public Uint32 loose hint");
    ExpectExactColor(
        pixels.AtNdc(0.75f, -0.5f),
        Color::Blue,
        "public Uint32 narrow hint");
}
#endif

TEST_F(IndexedDrawDeferredTest, BasicIndexedTriangleStripSupportsBothIndexWidths)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: asked of the ACTIVE renderer. GTEST_SKIP() returns from
    // the test body it is written in, so what used to be the `#else` arm is simply what follows.
    if (CNA_RENDERER_IS(Software))
        GTEST_SKIP() << "Software v1 intentionally supports indexed TriangleList only";

    if (!device.SupportsCapability(GraphicsCapability::ThreeD))
        GTEST_SKIP() << "Renderer explicitly does not support indexed triangle strips";
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);

#ifdef CNA_TEST_VULKAN_AVAILABLE
    auto* vulkanRenderer =
        dynamic_cast<CNA::Internal::Renderers::Vulkan::VulkanRenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, vulkanRenderer);
    const std::size_t validationMessageStart =
        vulkanRenderer->GetValidationMessagesEXT().size();
#endif

    std::vector<VertexPositionColor> vertices;
    AppendVertices(vertices, StripQuadAt(-0.5f, Color::Red));
    AppendVertices(vertices, StripQuadAt(0.5f, Color::Blue));
    const std::array<std::uint16_t, 4> indices16{0, 1, 2, 3};
    const std::array<std::uint32_t, 5> indices32{0, 4, 5, 6, 7};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 8, BufferUsage::None);
    IndexBuffer buffer16(
        device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    IndexBuffer buffer32(
        device, IndexElementSize::ThirtyTwoBits, 5, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 8);
    buffer16.SetData(indices16.data(), 4);
    buffer32.SetData(indices32.data(), 5);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&buffer16);
    EXPECT_NO_THROW(device.DrawIndexedPrimitives(
        PrimitiveType::TriangleStrip, 0, 0, 4, 0, 2));
    device.SetIndexBuffer(&buffer32);
    EXPECT_NO_THROW(device.DrawIndexedPrimitives(
        PrimitiveType::TriangleStrip, 0, 4, 4, 1, 2));

    // plans/plan_runtimerenderer.md RTR-P9-5: the renderers with an exact-pixel backbuffer oracle.
    if (CNA_RENDERER_IS(Bgfx, WebGPU, Vulkan, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2,
                        DirectX9, DirectX11))
    {
        const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
        ExpectExactColor(pixels.AtNdc(-0.5f), Color::Red, "basic Uint16 strip");
        ExpectExactColor(pixels.AtNdc(0.5f), Color::Blue, "basic Uint32 strip");
    }
#ifdef CNA_TEST_VULKAN_AVAILABLE
    AssertNoNewVulkanValidationMessages(*vulkanRenderer, validationMessageStart);
#endif
}

TEST_F(IndexedDrawDeferredTest, DeferredAtoBtoACapturesDataCountsAndLifetimes)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, WebGPU, Vulkan, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, DirectX9, DirectX11, Software);
    RequireIndexedRendering();

    const auto left = TriangleAt(-0.65f, Color::Red);
    const auto center = TriangleAt(0.0f, Color::Lime);
    const auto right = TriangleAt(0.65f, Color::Blue);
    std::array<VertexPositionColor, 9> vertices{
        left[0], left[1], left[2],
        center[0], center[1], center[2],
        right[0], right[1], right[2],
    };
    const std::array<std::uint16_t, 3> leftIndices{0, 1, 2};
    const std::array<std::uint16_t, 3> degenerateIndices{6, 6, 6};
    const std::array<std::uint16_t, 3> rightIndices{6, 7, 8};
    const std::array<std::uint16_t, 9> centerTriangles{
        3, 4, 5, 3, 4, 5, 3, 4, 5};

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 9, BufferUsage::None);
    IndexBuffer staticA(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    DynamicIndexBuffer dynamicB(
        device, IndexElementSize::SixteenBits, 9, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 9);
    staticA.SetData(leftIndices.data(), 3);
    dynamicB.SetData(
        centerTriangles.data(), 0, 9, SetDataOptions::Discard);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    // Queue A, mutate its renderer shadow, queue B, restore A with different data, then queue A
    // again. Each deferred command must retain its own bytes, logical count, and buffer identity.
    device.SetIndexBuffer(&staticA);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 9, 0, 1);
    staticA.SetData(degenerateIndices.data(), 3);

    device.SetIndexBuffer(&dynamicB);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 3, 3, 0, 3);

    staticA.SetData(rightIndices.data(), 3);
    device.SetIndexBuffer(&staticA);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 6, 3, 0, 1);

    // Deferred snapshots must remain valid after public objects and caller storage change.
    for (auto& vertex : vertices)
        vertex = VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black);
    device.SetIndexBuffer(nullptr);
    device.SetVertexBuffer(nullptr);
    staticA.Dispose();
    dynamicB.Dispose();
    vertexBuffer.Dispose();

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.65f), Color::Red, "deferred A first range");
    ExpectExactColor(pixels.AtNdc(0.0f), Color::Lime, "deferred B count/range");
    ExpectExactColor(pixels.AtNdc(0.65f), Color::Blue, "deferred A second range");
    ExpectExactColor(pixels.AtNdc(-0.98f), Color::Black, "deferred range background");
}

TEST_F(IndexedDrawDeferredTest, DeferredStaticVertexAtoBtoAPreservesEveryQueuedVersion)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, WebGPU, Vulkan, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, DirectX9, DirectX11, Software);
    RequireIndexedRendering();

    auto sourceA = CenterTriangle(Color::Red);
    auto sourceB = CenterTriangle(Color::Lime);
    auto overwrittenBeforeFirstDraw = CenterTriangle(Color::Blue);
    VertexBuffer buffer(
        device, PositionColorDeclaration(), 3, BufferUsage::None);

    // Two uploads before any draw reuse one writable native version. Once a draw observes A,
    // later SetData calls must leave that version immutable until frame execution.
    buffer.SetData(overwrittenBeforeFirstDraw.data(), 3);
    {
        const auto destroyedAfterSetData = sourceA;
        buffer.SetData(destroyedAfterSetData.data(), 3);
    }

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    device.Clear(Color::Black);
    device.SetVertexBuffer(&buffer);

    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(-0.68f, 0.0f, 0.0f));
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    // Multiple draws between updates intentionally share A's one immutable native version.
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

    buffer.SetData(sourceB.data(), 3);
    effect.setWorldProperty(Microsoft::Xna::Framework::Matrix::getIdentityProperty());
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

    buffer.SetData(sourceA.data(), 3);
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(0.68f, 0.0f, 0.0f));
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

    // SetData owns its source immediately, and bgfx's queued retirement keeps all native
    // versions alive even after the public object is disposed before frame execution.
    sourceA.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    sourceB.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    overwrittenBeforeFirstDraw.fill(
        VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    device.SetVertexBuffer(nullptr);
    buffer.Dispose();

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.68f), Color::Red, "static vertex A");
    ExpectExactColor(pixels.AtNdc(0.0f), Color::Lime, "static vertex B");
    ExpectExactColor(pixels.AtNdc(0.68f), Color::Red, "static vertex A restore");
}

TEST_F(IndexedDrawDeferredTest, DeferredDynamicVertexAtoBtoAPreservesEveryQueuedVersion)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    // D3D9/D3D11 are excluded here: this deferred-queue contract was never measured on
    // them, and an unmeasured renderer must not be asserted either way.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, WebGPU, Vulkan, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, Software);
    RequireIndexedRendering();

    auto sourceA = CenterTriangle(Color::Blue);
    auto sourceB = CenterTriangle(Color::Yellow);
    DynamicVertexBuffer buffer(
        device, PositionColorDeclaration(), 3, BufferUsage::None);
    buffer.SetData(sourceA.data(), 0, 3, SetDataOptions::Discard);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    device.Clear(Color::Black);
    device.SetVertexBuffer(&buffer);

    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(-0.68f, 0.0f, 0.0f));
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    buffer.SetData(sourceB.data(), 0, 3, SetDataOptions::NoOverwrite);

    effect.setWorldProperty(Microsoft::Xna::Framework::Matrix::getIdentityProperty());
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    buffer.SetData(sourceA.data(), 0, 3, SetDataOptions::Discard);

    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(0.68f, 0.0f, 0.0f));
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

    sourceA.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    sourceB.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    device.SetVertexBuffer(nullptr);
    buffer.Dispose();

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.68f), Color::Blue, "dynamic vertex A");
    ExpectExactColor(pixels.AtNdc(0.0f), Color::Yellow, "dynamic vertex B");
    ExpectExactColor(pixels.AtNdc(0.68f), Color::Blue, "dynamic vertex A restore");
}

TEST_F(IndexedDrawDeferredTest, DeferredDistinctIdenticalVertexBuffersRemainIndependent)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, WebGPU, Vulkan, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, DirectX9, DirectX11, Software);
    RequireIndexedRendering();

    const auto identical = CenterTriangle(Color::Blue);
    auto changed = CenterTriangle(Color::Red);
    VertexBuffer first(
        device, PositionColorDeclaration(), 3, BufferUsage::None);
    VertexBuffer second(
        device, PositionColorDeclaration(), 3, BufferUsage::None);
    first.SetData(identical.data(), 3);
    second.SetData(identical.data(), 3);
    first.SetData(changed.data(), 3);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    device.Clear(Color::Black);

    device.SetVertexBuffer(&first);
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(-0.55f, 0.0f, 0.0f));
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

    device.SetVertexBuffer(&second);
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(0.55f, 0.0f, 0.0f));
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

    changed.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    device.SetVertexBuffer(nullptr);
    first.Dispose();
    second.Dispose();

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.55f), Color::Red, "updated first buffer");
    ExpectExactColor(pixels.AtNdc(0.55f), Color::Blue, "independent identical buffer");
}

TEST_F(IndexedDrawDeferredTest, DeferredDynamicIndexAtoBtoAPreservesEveryQueuedVersion)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    // D3D9/D3D11 are excluded here: this deferred-queue contract was never measured on
    // them, and an unmeasured renderer must not be asserted either way.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, WebGPU, Vulkan, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, Software);
    RequireIndexedRendering();

    const auto red = CenterTriangle(Color::Red);
    const auto lime = CenterTriangle(Color::Lime);
    const std::array<VertexPositionColor, 6> vertices{
        red[0], red[1], red[2], lime[0], lime[1], lime[2],
    };
    auto sourceA = std::array<std::uint16_t, 3>{0, 1, 2};
    auto sourceB = std::array<std::uint16_t, 3>{3, 4, 5};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 6, BufferUsage::None);
    DynamicIndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(sourceA.data(), 0, 3, SetDataOptions::Discard);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(-0.68f, 0.0f, 0.0f));
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 6, 0, 1);
    indexBuffer.SetData(sourceB.data(), 0, 3, SetDataOptions::NoOverwrite);

    effect.setWorldProperty(Microsoft::Xna::Framework::Matrix::getIdentityProperty());
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 6, 0, 1);
    indexBuffer.SetData(sourceA.data(), 0, 3, SetDataOptions::Discard);

    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(0.68f, 0.0f, 0.0f));
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 6, 0, 1);

    sourceA.fill(5);
    sourceB.fill(0);
    device.SetIndexBuffer(nullptr);
    device.SetVertexBuffer(nullptr);
    indexBuffer.Dispose();
    vertexBuffer.Dispose();

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.68f), Color::Red, "dynamic index A");
    ExpectExactColor(pixels.AtNdc(0.0f), Color::Lime, "dynamic index B");
    ExpectExactColor(pixels.AtNdc(0.68f), Color::Red, "dynamic index A restore");
}


#ifdef CNA_TEST_BGFX_AVAILABLE
TEST_F(IndexedDrawDeferredTest, BgfxIndexedAtoBtoACapturesEveryRangeAndBufferVersion)
{
    RequireIndexedRendering();

    const auto left = TriangleAt(-0.65f, Color::Red);
    const auto center = TriangleAt(0.0f, Color::Lime);
    const auto right = TriangleAt(0.65f, Color::Blue);
    std::array<VertexPositionColor, 9> vertices{
        left[0], left[1], left[2],
        center[0], center[1], center[2],
        right[0], right[1], right[2],
    };
    auto sourceAFirst = std::array<std::uint16_t, 9>{
        6, 6, 6,
        0, 1, 2,
        6, 6, 6,
    };
    auto sourceASecond = std::array<std::uint16_t, 9>{
        3, 4, 5,
        0, 0, 0,
        0, 0, 0,
    };
    auto sourceB = std::array<std::uint16_t, 9>{
        8, 8,
        3, 4, 5,
        3, 4, 5,
        8,
    };

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 9, BufferUsage::None);
    IndexBuffer staticA(
        device, IndexElementSize::SixteenBits, 9, BufferUsage::None);
    DynamicIndexBuffer dynamicB(
        device, IndexElementSize::SixteenBits, 9, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 9);
    staticA.SetData(sourceAFirst.data(), 9);
    dynamicB.SetData(sourceB.data(), 0, 9, SetDataOptions::Discard);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    device.SetIndexBuffer(&staticA);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 3, 3, 1);

    staticA.SetData(sourceASecond.data(), 9);
    device.SetIndexBuffer(&dynamicB);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 3, 3, 2, 2);

    device.SetIndexBuffer(&staticA);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 3, 3, 3, 0, 1);

    sourceAFirst.fill(8);
    sourceASecond.fill(0);
    sourceB.fill(0);
    vertices.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    device.SetIndexBuffer(nullptr);
    device.SetVertexBuffer(nullptr);
    staticA.Dispose();
    dynamicB.Dispose();
    vertexBuffer.Dispose();

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.65f), Color::Red, "indexed A first range/version");
    ExpectExactColor(pixels.AtNdc(0.0f), Color::Lime, "indexed B start/count/buffer");
    ExpectExactColor(pixels.AtNdc(0.65f), Color::Blue, "indexed A base/range/new version");
    ExpectExactColor(pixels.AtNdc(-0.98f), Color::Black, "indexed A-to-B-to-A background");
}

TEST_F(IndexedDrawDeferredTest, BgfxPublicThirtyTwoBitBuffersPreserveAtoBtoAVersions)
{
    RequireIndexedRendering();

    constexpr std::uint32_t highVertex = 65536u;
    const auto sourceTriangleA = CenterTriangle(Color::Red);
    const auto sourceTriangleB = CenterTriangle(Color::Lime);
    std::vector<VertexPositionColor> vertices(
        static_cast<std::size_t>(highVertex) + 6u,
        VertexPositionColor(Vector3(4.0f, 4.0f, 0.5f), Color::Black));
    std::copy(
        sourceTriangleA.begin(),
        sourceTriangleA.end(),
        vertices.begin() + highVertex);
    std::copy(
        sourceTriangleB.begin(),
        sourceTriangleB.end(),
        vertices.begin() + highVertex + 3u);
    auto indicesA = std::array<std::uint32_t, 3>{
        highVertex, highVertex + 2u, highVertex + 1u};
    auto indicesB = std::array<std::uint32_t, 3>{
        highVertex + 3u, highVertex + 5u, highVertex + 4u};

    VertexBuffer vertexBuffer(
        device,
        PositionColorDeclaration(),
        static_cast<int>(vertices.size()),
        BufferUsage::None);
    IndexBuffer staticBuffer(
        device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
    DynamicIndexBuffer dynamicBuffer(
        device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), static_cast<int>(vertices.size()));

    // Repeated ordinary updates before a draw retain one writable native allocation.
    staticBuffer.SetData(indicesB.data(), 3);
    auto* staticNative = GetBgfxIndexRenderer(staticBuffer);
    ASSERT_NE(nullptr, staticNative);
    const std::uint16_t staticWritableHandle = staticNative->handle.idx;
    staticBuffer.SetData(indicesA.data(), 3);
    EXPECT_EQ(staticWritableHandle, staticNative->handle.idx);

    dynamicBuffer.SetData(indicesB.data(), 0, 3, SetDataOptions::NoOverwrite);
    auto* dynamicNative = GetBgfxIndexRenderer(dynamicBuffer);
    ASSERT_NE(nullptr, dynamicNative);
    const std::uint16_t dynamicWritableHandle = dynamicNative->handle.idx;
    dynamicBuffer.SetData(indicesA.data(), 0, 3, SetDataOptions::Discard);
    EXPECT_EQ(dynamicWritableHandle, dynamicNative->handle.idx);
    EXPECT_NE(staticNative->handle.idx, dynamicNative->handle.idx);
    ExpectExactBgfxIndexFlags(staticBuffer, true);
    ExpectExactBgfxIndexFlags(dynamicBuffer, true);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    device.SetIndexBuffer(&staticBuffer);
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(-0.65f, 0.5f, 0.0f));
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        0,
        static_cast<int>(highVertex),
        6,
        0,
        1);
    const std::uint16_t staticVersionA = staticNative->handle.idx;

    staticBuffer.SetData(indicesB.data(), 3);
    const std::uint16_t staticVersionB = staticNative->handle.idx;
    EXPECT_NE(staticVersionA, staticVersionB);
    ExpectExactBgfxIndexFlags(staticBuffer, true);
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(0.0f, 0.5f, 0.0f));
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        0,
        static_cast<int>(highVertex),
        6,
        0,
        1);

    staticBuffer.SetData(indicesA.data(), 3);
    const std::uint16_t staticVersionA2 = staticNative->handle.idx;
    EXPECT_NE(staticVersionA, staticVersionA2);
    EXPECT_NE(staticVersionB, staticVersionA2);
    ExpectExactBgfxIndexFlags(staticBuffer, true);
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(0.65f, 0.5f, 0.0f));
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        0,
        static_cast<int>(highVertex),
        6,
        0,
        1);

    // Updating the equal-content static object must not mutate or replace the independent
    // dynamic object, and neither buffer identity participates in pipeline compatibility.
    EXPECT_EQ(dynamicWritableHandle, dynamicNative->handle.idx);
    device.SetIndexBuffer(&dynamicBuffer);
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(-0.65f, -0.5f, 0.0f));
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        0,
        static_cast<int>(highVertex),
        6,
        0,
        1);
    const std::uint16_t dynamicVersionA = dynamicNative->handle.idx;

    dynamicBuffer.SetData(indicesB.data(), 0, 3, SetDataOptions::NoOverwrite);
    const std::uint16_t dynamicVersionB = dynamicNative->handle.idx;
    EXPECT_NE(dynamicVersionA, dynamicVersionB);
    ExpectExactBgfxIndexFlags(dynamicBuffer, true);
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(0.0f, -0.5f, 0.0f));
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        0,
        static_cast<int>(highVertex),
        6,
        0,
        1);

    dynamicBuffer.SetData(indicesA.data(), 0, 3, SetDataOptions::Discard);
    const std::uint16_t dynamicVersionA2 = dynamicNative->handle.idx;
    EXPECT_NE(dynamicVersionA, dynamicVersionA2);
    EXPECT_NE(dynamicVersionB, dynamicVersionA2);
    ExpectExactBgfxIndexFlags(dynamicBuffer, true);
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(0.65f, -0.5f, 0.0f));
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        0,
        static_cast<int>(highVertex),
        6,
        0,
        1);

    std::array<std::uint32_t, 3> shadow{};
    staticBuffer.GetData(shadow.data(), 3);
    EXPECT_EQ(indicesA, shadow);
    dynamicBuffer.GetData(shadow.data(), 3);
    EXPECT_EQ(indicesA, shadow);
    ASSERT_EQ(sizeof(indicesA), staticNative->cpuData.size());
    ASSERT_EQ(sizeof(indicesA), dynamicNative->cpuData.size());
    EXPECT_EQ(
        0,
        std::memcmp(
            indicesA.data(), staticNative->cpuData.data(), staticNative->cpuData.size()));
    EXPECT_EQ(
        0,
        std::memcmp(
            indicesA.data(), dynamicNative->cpuData.data(), dynamicNative->cpuData.size()));

    // Queued draws own every native version after public buffers and caller arrays cease to live.
    indicesA.fill(0);
    indicesB.fill(0);
    std::fill(
        vertices.begin(),
        vertices.end(),
        VertexPositionColor(Vector3(4.0f, 4.0f, 0.5f), Color::Black));
    device.SetIndexBuffer(nullptr);
    device.SetVertexBuffer(nullptr);
    staticBuffer.Dispose();
    dynamicBuffer.Dispose();
    vertexBuffer.Dispose();

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.65f, 0.5f), Color::Red, "static Uint32 A");
    ExpectExactColor(pixels.AtNdc(0.0f, 0.5f), Color::Lime, "static Uint32 B");
    ExpectExactColor(pixels.AtNdc(0.65f, 0.5f), Color::Red, "static Uint32 A restore");
    ExpectExactColor(pixels.AtNdc(-0.65f, -0.5f), Color::Red, "dynamic Uint32 A");
    ExpectExactColor(pixels.AtNdc(0.0f, -0.5f), Color::Lime, "dynamic Uint32 B");
    ExpectExactColor(pixels.AtNdc(0.65f, -0.5f), Color::Red, "dynamic Uint32 A restore");
}

TEST_F(IndexedDrawDeferredTest, BgfxThirtyTwoBitRendererAtoBtoARendersExactPixels)
{
    RequireIndexedRendering();

    struct PackedPositionColor
    {
        float x;
        float y;
        float z;
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        std::uint8_t a;
    };
    static_assert(sizeof(PackedPositionColor) == 16);

    const auto red = CenterTriangle(Color::Red);
    const auto lime = CenterTriangle(Color::Lime);
    std::array<PackedPositionColor, 6> vertices{};
    const auto pack = [](const VertexPositionColor& source)
    {
        return PackedPositionColor{
            source.Position.X,
            source.Position.Y,
            source.Position.Z,
            source.Color.getRProperty(),
            source.Color.getGProperty(),
            source.Color.getBProperty(),
            source.Color.getAProperty(),
        };
    };
    for (std::size_t i = 0; i < red.size(); ++i)
    {
        vertices[i] = pack(red[i]);
        vertices[i + 3] = pack(lime[i]);
    }

    auto sourceA = std::array<std::uint32_t, 3>{0, 1, 2};
    auto sourceB = std::array<std::uint32_t, 3>{3, 4, 5};
    CNA::Internal::Renderers::Bgfx::BgfxVertexBufferRenderer vertexBuffer(6);
    CNA::Internal::Renderers::Bgfx::BgfxIndexBufferRenderer indexBuffer(3, true);
    vertexBuffer.SetData(vertices.data(), 6, sizeof(PackedPositionColor));
    indexBuffer.SetData32(sourceA.data(), 3);

    auto* renderer =
        dynamic_cast<CNA::Internal::Renderers::Bgfx::BgfxRenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, renderer);
    ASSERT_TRUE(indexBuffer.IsThirtyTwoBit());
    EXPECT_EQ(
        BGFX_BUFFER_INDEX32 | BGFX_BUFFER_ALLOW_RESIZE,
        indexBuffer.GetNativeCreationFlagsEXT());

    const auto identity = Microsoft::Xna::Framework::Matrix::getIdentityProperty();
    device.Clear(Color::Black);
    const std::uint16_t versionA = indexBuffer.handle.idx;
    renderer->DrawIndexedColoredPrimitives(
        vertexBuffer,
        indexBuffer,
        Microsoft::Xna::Framework::Matrix::CreateTranslation(-0.68f, 0.0f, 0.0f),
        identity,
        identity,
        PrimitiveType::TriangleList,
        1);

    indexBuffer.SetData32(sourceB.data(), 3);
    const std::uint16_t versionB = indexBuffer.handle.idx;
    EXPECT_NE(versionA, versionB);
    EXPECT_TRUE(indexBuffer.IsThirtyTwoBit());
    EXPECT_EQ(
        BGFX_BUFFER_INDEX32 | BGFX_BUFFER_ALLOW_RESIZE,
        indexBuffer.GetNativeCreationFlagsEXT());
    renderer->DrawIndexedColoredPrimitives(
        vertexBuffer,
        indexBuffer,
        identity,
        identity,
        identity,
        PrimitiveType::TriangleList,
        1);

    indexBuffer.SetData32(sourceA.data(), 3);
    const std::uint16_t versionA2 = indexBuffer.handle.idx;
    EXPECT_NE(versionB, versionA2);
    EXPECT_NE(versionA, versionA2);
    EXPECT_TRUE(indexBuffer.IsThirtyTwoBit());
    EXPECT_EQ(
        BGFX_BUFFER_INDEX32 | BGFX_BUFFER_ALLOW_RESIZE,
        indexBuffer.GetNativeCreationFlagsEXT());
    renderer->DrawIndexedColoredPrimitives(
        vertexBuffer,
        indexBuffer,
        Microsoft::Xna::Framework::Matrix::CreateTranslation(0.68f, 0.0f, 0.0f),
        identity,
        identity,
        PrimitiveType::TriangleList,
        1);

    sourceA.fill(5);
    sourceB.fill(0);
    vertices.fill({});

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.68f), Color::Red, "native Uint32 A");
    ExpectExactColor(pixels.AtNdc(0.0f), Color::Lime, "native Uint32 B");
    ExpectExactColor(pixels.AtNdc(0.68f), Color::Red, "native Uint32 A restore");
}

TEST_F(IndexedDrawDeferredTest, BgfxThirtyTwoBitRendererHonorsRangeBaseAndCount)
{
    RequireIndexedRendering();

    struct PackedPositionColor
    {
        float x;
        float y;
        float z;
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        std::uint8_t a;
    };
    static_assert(sizeof(PackedPositionColor) == 16);

    std::vector<VertexPositionColor> sourceVertices;
    AppendVertices(sourceVertices, TriangleAt(-0.75f, Color::Lime));
    AppendVertices(sourceVertices, TriangleAt(-0.25f, Color::Yellow));
    AppendVertices(sourceVertices, TriangleAt(0.25f, Color::Red));
    AppendVertices(sourceVertices, TriangleAt(0.75f, Color::Blue));
    std::array<PackedPositionColor, 12> vertices{};
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        vertices[i] = {
            sourceVertices[i].Position.X,
            sourceVertices[i].Position.Y,
            sourceVertices[i].Position.Z,
            sourceVertices[i].Color.getRProperty(),
            sourceVertices[i].Color.getGProperty(),
            sourceVertices[i].Color.getBProperty(),
            sourceVertices[i].Color.getAProperty(),
        };
    }
    const std::array<std::uint32_t, 9> indices{
        0, 1, 2,
        3, 4, 5,
        6, 7, 8,
    };

    CNA::Internal::Renderers::Bgfx::BgfxVertexBufferRenderer vertexBuffer(12);
    CNA::Internal::Renderers::Bgfx::BgfxIndexBufferRenderer indexBuffer(9, true);
    vertexBuffer.SetData(vertices.data(), 12, sizeof(PackedPositionColor));
    indexBuffer.SetData32(indices.data(), 9);
    ASSERT_TRUE(indexBuffer.IsThirtyTwoBit());
    EXPECT_EQ(
        BGFX_BUFFER_INDEX32 | BGFX_BUFFER_ALLOW_RESIZE,
        indexBuffer.GetNativeCreationFlagsEXT());

    auto* renderer =
        dynamic_cast<CNA::Internal::Renderers::Bgfx::BgfxRenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, renderer);
    CNA::Internal::Renderers::GpuDrawParams params;
    params.startIndex = 3;
    params.baseVertex = 3;
    params.minVertexIndex = 3;
    params.numVertices = 3;
    const auto identity = Microsoft::Xna::Framework::Matrix::getIdentityProperty();

    device.Clear(Color::Black);
    renderer->DrawIndexedPrimitivesEx(
        vertexBuffer,
        indexBuffer,
        identity,
        identity,
        identity,
        PrimitiveType::TriangleList,
        1,
        params);

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(0.25f), Color::Red, "native Uint32 selected range");
    ExpectExactColor(pixels.AtNdc(-0.75f), Color::Black, "native Uint32 prefix");
    ExpectExactColor(pixels.AtNdc(-0.25f), Color::Black, "native Uint32 based prefix");
    ExpectExactColor(pixels.AtNdc(0.75f), Color::Black, "native Uint32 suffix");
}

TEST_F(IndexedDrawDeferredTest, BgfxBufferVersionsSurviveRenderTargetTransition)
{
    RequireIndexedRendering();

    auto sourceA = CenterTriangle(Color::Red);
    auto sourceB = CenterTriangle(Color::Lime);
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 3, BufferUsage::None);
    vertexBuffer.SetData(sourceA.data(), 3);
    RenderTarget2D target(
        device,
        96,
        96,
        false,
        SurfaceFormat::Color,
        DepthFormat::None,
        0,
        RenderTargetUsage::PreserveContents);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    device.SetVertexBuffer(&vertexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);

    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(-0.48f, 0.0f, 0.0f));
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

    vertexBuffer.SetData(sourceB.data(), 3);
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(0.48f, 0.0f, 0.0f));
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

    // Switching targets creates an ordered view segment but must not submit, recycle, or mutate
    // either version that the target's two queued draws already reference.
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    device.Clear(Color::Black);
    vertexBuffer.SetData(sourceA.data(), 3);
    effect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::CreateTranslation(0.58f, 0.0f, 0.0f));
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

    sourceA.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    sourceB.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    device.SetVertexBuffer(nullptr);
    vertexBuffer.Dispose();

    // One readback submits the complete target-A -> target-B -> backbuffer-A frame. Verify the
    // restored A draw now, before using the target as a texture in the following frame.
    const BackbufferSnapshot directBackbuffer = ReadBackbufferOnce(device);
    ExpectExactColor(
        directBackbuffer.AtNdc(0.58f), Color::Red,
        "backbuffer segment retained restored vertex A");

    // Sample the RenderTarget2D through the normal BasicEffect texture path into the left half of
    // the next backbuffer. This verifies the target's exact queued pixels without relying on
    // Texture2D's upload shadow (rendered target pixels exist only on the GPU).
    const std::array<Microsoft::Xna::Framework::Graphics::VertexPositionTexture, 6>
        targetQuad{
            Microsoft::Xna::Framework::Graphics::VertexPositionTexture(
                Vector3(-1.0f, 1.0f, 0.0f),
                Microsoft::Xna::Framework::Vector2(0.0f, 0.0f)),
            Microsoft::Xna::Framework::Graphics::VertexPositionTexture(
                Vector3(-1.0f, -1.0f, 0.0f),
                Microsoft::Xna::Framework::Vector2(0.0f, 1.0f)),
            Microsoft::Xna::Framework::Graphics::VertexPositionTexture(
                Vector3(0.0f, -1.0f, 0.0f),
                Microsoft::Xna::Framework::Vector2(1.0f, 1.0f)),
            Microsoft::Xna::Framework::Graphics::VertexPositionTexture(
                Vector3(-1.0f, 1.0f, 0.0f),
                Microsoft::Xna::Framework::Vector2(0.0f, 0.0f)),
            Microsoft::Xna::Framework::Graphics::VertexPositionTexture(
                Vector3(0.0f, -1.0f, 0.0f),
                Microsoft::Xna::Framework::Vector2(1.0f, 1.0f)),
            Microsoft::Xna::Framework::Graphics::VertexPositionTexture(
                Vector3(0.0f, 1.0f, 0.0f),
                Microsoft::Xna::Framework::Vector2(1.0f, 0.0f)),
        };
    BasicEffect sampleEffect(device);
    sampleEffect.setWorldProperty(
        Microsoft::Xna::Framework::Matrix::getIdentityProperty());
    sampleEffect.setViewProperty(
        Microsoft::Xna::Framework::Matrix::getIdentityProperty());
    sampleEffect.setProjectionProperty(
        Microsoft::Xna::Framework::Matrix::getIdentityProperty());
    sampleEffect.setTextureEnabledProperty(true);
    sampleEffect.setTextureProperty(&target);
    device.Clear(Color::Black);
    sampleEffect.Apply();
    device.DrawUserPrimitives(
        PrimitiveType::TriangleList, targetQuad.data(), 0, 2);

    const BackbufferSnapshot backbufferPixels = ReadBackbufferOnce(device);
    ExpectExactColor(
        backbufferPixels.AtNdc(-0.74f), Color::Red,
        "target segment retained vertex A");
    ExpectExactColor(
        backbufferPixels.AtNdc(-0.26f), Color::Lime,
        "target segment retained vertex B");
}

TEST_F(IndexedDrawDeferredTest, BgfxPublicThirtyTwoBitRangesSurviveTargetSegmentation)
{
    RequireIndexedRendering();

    std::vector<VertexPositionColor> vertices;
    AppendVertices(vertices, TriangleAt(-0.75f, Color::Lime));
    AppendVertices(vertices, TriangleAt(-0.25f, Color::Yellow));
    AppendVertices(vertices, TriangleAt(0.25f, Color::Red));
    AppendVertices(vertices, TriangleAt(0.75f, Color::Blue));
    auto targetFirst = std::array<std::uint32_t, 9>{
        0, 1, 2,
        3, 4, 5,
        6, 7, 8,
    };
    auto backbuffer = std::array<std::uint32_t, 9>{
        9, 10, 11,
        0, 0, 0,
        0, 0, 0,
    };
    auto targetSecond = std::array<std::uint32_t, 9>{
        3, 4, 5,
        0, 0, 0,
        0, 0, 0,
    };
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(vertices.size()), BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::ThirtyTwoBits, 9, BufferUsage::None);
    RenderTarget2D target(
        device,
        96,
        96,
        false,
        SurfaceFormat::Color,
        DepthFormat::None,
        0,
        RenderTargetUsage::PreserveContents);
    vertexBuffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
    indexBuffer.SetData(targetFirst.data(), 9);
    auto* native = GetBgfxIndexRenderer(indexBuffer);
    ASSERT_NE(nullptr, native);
    ExpectExactBgfxIndexFlags(indexBuffer, true);
    const std::uint16_t targetFirstVersion = native->handle.idx;

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 3, 3, 3, 3, 1);

    indexBuffer.SetData(backbuffer.data(), 9);
    const std::uint16_t backbufferVersion = native->handle.idx;
    EXPECT_NE(targetFirstVersion, backbufferVersion);
    ExpectExactBgfxIndexFlags(indexBuffer, true);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 9, 3, 0, 1);

    indexBuffer.SetData(targetSecond.data(), 9);
    const std::uint16_t targetSecondVersion = native->handle.idx;
    EXPECT_NE(targetFirstVersion, targetSecondVersion);
    EXPECT_NE(backbufferVersion, targetSecondVersion);
    ExpectExactBgfxIndexFlags(indexBuffer, true);
    device.SetRenderTarget(&target);
    effect.Apply();
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 3, 3, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    targetFirst.fill(0);
    backbuffer.fill(0);
    targetSecond.fill(0);
    vertices.assign(
        vertices.size(),
        VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    device.SetIndexBuffer(nullptr);
    device.SetVertexBuffer(nullptr);
    indexBuffer.Dispose();
    vertexBuffer.Dispose();

    const BackbufferSnapshot directPixels = ReadBackbufferOnce(device);
    ExpectExactColor(
        directPixels.AtNdc(0.75f), Color::Blue,
        "backbuffer retained indexed middle segment");
    ExpectExactColor(
        directPixels.AtNdc(0.25f), Color::Black,
        "target indexed range did not leak to backbuffer");

    SampleRenderTargetToBackbuffer(device, target);
    const BackbufferSnapshot targetPixels = ReadBackbufferOnce(device);
    ExpectExactColor(
        targetPixels.AtNdc(-0.25f), Color::Yellow,
        "target retained second indexed segment");
    ExpectExactColor(
        targetPixels.AtNdc(0.25f), Color::Red,
        "target retained first indexed segment");
    ExpectExactColor(
        targetPixels.AtNdc(-0.75f), Color::Black,
        "target excluded prefix geometry");
    ExpectExactColor(
        targetPixels.AtNdc(0.75f), Color::Black,
        "target excluded suffix geometry");
}

TEST_F(IndexedDrawDeferredTest, BgfxDrawUserBuffersOwnCopiedSourceBytes)
{
    RequireIndexedRendering();

    auto left = TriangleAt(-0.65f, Color::Red);
    auto center = TriangleAt(0.0f, Color::Lime);
    auto right = TriangleAt(0.65f, Color::Blue);
    std::array<std::uint16_t, 4> centerIndices{99, 0, 1, 2};
    std::array<std::uint16_t, 4> rightIndices{99, 0, 1, 2};
    struct CompactPositionColor
    {
        float x;
        float y;
        float z;
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        std::uint8_t a;
    };
    static_assert(sizeof(CompactPositionColor) == 16);
    std::array<CompactPositionColor, 3> compactRight{};
    for (std::size_t i = 0; i < right.size(); ++i)
    {
        compactRight[i] = {
            right[i].Position.X,
            right[i].Position.Y,
            right[i].Position.Z,
            right[i].Color.getRProperty(),
            right[i].Color.getGProperty(),
            right[i].Color.getBProperty(),
            right[i].Color.getAProperty(),
        };
    }

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.DrawUserPrimitives(
        PrimitiveType::TriangleList,
        left.data(), 0, 1);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList,
        center.data(), 0, 3,
        centerIndices.data(), 1, 1);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList,
        compactRight.data(), 0, 3,
        rightIndices.data(), 1, 1,
        PositionColorDeclaration());

    left.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    center.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    right.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    centerIndices.fill(0);
    rightIndices.fill(0);
    compactRight.fill({});

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(
        pixels.AtNdc(-0.65f), Color::Red,
        "non-indexed typed DrawUser copy");
    ExpectExactColor(
        pixels.AtNdc(0.0f), Color::Lime,
        "indexed typed DrawUser copy");
    ExpectExactColor(
        pixels.AtNdc(0.65f), Color::Blue,
        "explicit-declaration DrawUser copy");
}

TEST_F(IndexedDrawDeferredTest, BgfxNativeBufferVersionCountsRemainBounded)
{
    RequireIndexedRendering();

    // Let initialization-time deferred destroys settle before recording the process-wide bgfx
    // handle allocator's baseline.
    device.Present();
    device.Present();
    const bgfx::Stats* stats = bgfx::getStats();
    ASSERT_NE(nullptr, stats);
    const std::uint16_t processVertexBaseline = stats->numDynamicVertexBuffers;
    const std::uint16_t processIndexBaseline = stats->numDynamicIndexBuffers;

    auto verticesA = CenterTriangle(Color::White);
    auto verticesB = CenterTriangle(Color::Lime);
    const auto indicesA = std::array<std::uint32_t, 3>{0, 1, 2};
    const auto indicesB = std::array<std::uint32_t, 3>{0, 2, 1};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 3, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
    vertexBuffer.SetData(verticesA.data(), 3);
    indexBuffer.SetData(indicesA.data(), 3);
    ExpectExactBgfxIndexFlags(indexBuffer, true);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    // Retire the vertex buffer's constructor-layout allocation before measuring its steady
    // one-object baseline.
    device.Present();
    device.Present();
    stats = bgfx::getStats();
    ASSERT_NE(nullptr, stats);
    const std::uint16_t liveVertexBaseline = stats->numDynamicVertexBuffers;
    const std::uint16_t liveIndexBaseline = stats->numDynamicIndexBuffers;
    EXPECT_EQ(processVertexBaseline + 1u, liveVertexBaseline);
    EXPECT_EQ(processIndexBaseline + 1u, liveIndexBaseline);

    std::uint16_t warmVertexHighWater = liveVertexBaseline;
    std::uint16_t warmIndexHighWater = liveIndexBaseline;
    std::uint16_t finalVertexCount = liveVertexBaseline;
    std::uint16_t finalIndexCount = liveIndexBaseline;
    for (int frame = 0; frame < 32; ++frame)
    {
        device.Clear(Color::Black);
        vertexBuffer.SetData(verticesA.data(), 3);
        indexBuffer.SetData(indicesA.data(), 3);
        ExpectExactBgfxIndexFlags(indexBuffer, true);
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 0, 0, 3, 0, 1);

        vertexBuffer.SetData(verticesB.data(), 3);
        indexBuffer.SetData(indicesB.data(), 3);
        ExpectExactBgfxIndexFlags(indexBuffer, true);
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 0, 0, 3, 0, 1);

        vertexBuffer.SetData(verticesA.data(), 3);
        indexBuffer.SetData(indicesA.data(), 3);
        ExpectExactBgfxIndexFlags(indexBuffer, true);
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
        device.Present();

        stats = bgfx::getStats();
        ASSERT_NE(nullptr, stats);
        finalVertexCount = stats->numDynamicVertexBuffers;
        finalIndexCount = stats->numDynamicIndexBuffers;
        if (frame < 4)
        {
            warmVertexHighWater = std::max(
                warmVertexHighWater, finalVertexCount);
            warmIndexHighWater = std::max(
                warmIndexHighWater, finalIndexCount);
        }
        else
        {
            EXPECT_LE(finalVertexCount, warmVertexHighWater);
            EXPECT_LE(finalIndexCount, warmIndexHighWater);
        }
    }

    // The current object versions are the only survivors after bgfx's normal frame-fence
    // retirement window; repeated A -> B -> A updates do not accumulate permanent allocations.
    device.Present();
    device.Present();
    device.Present();
    stats = bgfx::getStats();
    ASSERT_NE(nullptr, stats);
    EXPECT_EQ(liveVertexBaseline, stats->numDynamicVertexBuffers);
    EXPECT_EQ(liveIndexBaseline, stats->numDynamicIndexBuffers);
    EXPECT_LE(finalVertexCount, warmVertexHighWater);
    EXPECT_LE(finalIndexCount, warmIndexHighWater);
    const std::uint16_t retiredVertexCount = stats->numDynamicVertexBuffers;
    const std::uint16_t retiredIndexCount = stats->numDynamicIndexBuffers;

    device.SetIndexBuffer(nullptr);
    device.SetVertexBuffer(nullptr);
    indexBuffer.Dispose();
    vertexBuffer.Dispose();
    device.Present();
    device.Present();
    device.Present();
    stats = bgfx::getStats();
    ASSERT_NE(nullptr, stats);
    EXPECT_EQ(processVertexBaseline, stats->numDynamicVertexBuffers);
    EXPECT_EQ(processIndexBaseline, stats->numDynamicIndexBuffers);
    std::cout
        << "REMED-GFX-108/109 public Uint32 bgfx dynamic-handle cardinality:"
        << " process baseline V/I="
        << processVertexBaseline << "/" << processIndexBaseline
        << ", two live public buffers=" << liveVertexBaseline << "/"
        << liveIndexBaseline
        << ", 32-frame warm high-water=" << warmVertexHighWater << "/"
        << warmIndexHighWater
        << ", post-fence live=" << retiredVertexCount << "/"
        << retiredIndexCount
        << ", post-dispose=" << stats->numDynamicVertexBuffers << "/"
        << stats->numDynamicIndexBuffers << "\n";
}
#endif

TEST_F(IndexedDrawDeferredTest, DrawUserIndexedCapturesOddOffsetsWidthsAndDeclaration)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(WebGPU, Vulkan, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, DirectX9, DirectX11, Software);
    RequireIndexedRendering();

    auto left = TriangleAt(-0.65f, Color::Red);
    auto center = TriangleAt(0.0f, Color::Lime);
    auto right = TriangleAt(0.65f, Color::Blue);
    std::array<std::uint16_t, 5> indices16A{99, 99, 0, 1, 2};
    std::array<std::uint16_t, 4> indices16B{99, 0, 1, 2};
    std::array<std::uint32_t, 5> indices32{99, 99, 0, 1, 2};

    struct CompactPositionColor
    {
        float x;
        float y;
        float z;
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        std::uint8_t a;
    };
    static_assert(sizeof(CompactPositionColor) == 16);
    std::array<CompactPositionColor, 3> compactCenter{};
    for (std::size_t i = 0; i < center.size(); ++i)
    {
        compactCenter[i] = {
            center[i].Position.X,
            center[i].Position.Y,
            center[i].Position.Z,
            center[i].Color.getRProperty(),
            center[i].Color.getGProperty(),
            center[i].Color.getBProperty(),
            center[i].Color.getAProperty(),
        };
    }

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList,
        left.data(), 0, 3,
        indices16A.data(), 2, 1);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList,
        compactCenter.data(), 0, 3,
        indices16B.data(), 1, 1,
        PositionColorDeclaration());
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList,
        right.data(), 0, 3,
        indices32.data(), 2, 1);

    // The temporary public arrays and temporary renderer buffers are not live at replay time.
    left = CenterTriangle(Color::Black);
    center = CenterTriangle(Color::Black);
    right = CenterTriangle(Color::Black);
    indices16A.fill(0);
    indices16B.fill(0);
    indices32.fill(0);
    compactCenter.fill({});

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.65f), Color::Red, "DrawUser Uint16 offset A");
    ExpectExactColor(pixels.AtNdc(0.0f), Color::Lime, "DrawUser declaration offset B");
    ExpectExactColor(pixels.AtNdc(0.65f), Color::Blue, "DrawUser Uint32 offset C");
}

TEST_F(IndexedDrawDeferredTest, DrawUserIndexedTriangleStripsPreserveWidthsOffsetsAndSources)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(WebGPU, Vulkan, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, DirectX9, DirectX11);
    RequireIndexedRendering();

#ifdef CNA_TEST_VULKAN_AVAILABLE
    auto* vulkanRenderer =
        dynamic_cast<CNA::Internal::Renderers::Vulkan::VulkanRenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, vulkanRenderer);
    const std::size_t validationMessageStart =
        vulkanRenderer->GetValidationMessagesEXT().size();
#endif

    auto left = StripTriangleAt(-0.5f, Color::Red);
    const auto right = StripQuadAt(0.5f, Color::Blue);
    struct CompactPositionColor
    {
        float x;
        float y;
        float z;
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        std::uint8_t a;
    };
    static_assert(sizeof(CompactPositionColor) == 16);
    std::array<CompactPositionColor, 4> compactRight{};
    for (std::size_t i = 0; i < right.size(); ++i)
    {
        compactRight[i] = {
            right[i].Position.X,
            right[i].Position.Y,
            right[i].Position.Z,
            right[i].Color.getRProperty(),
            right[i].Color.getGProperty(),
            right[i].Color.getBProperty(),
            right[i].Color.getAProperty(),
        };
    }
    std::array<std::uint16_t, 4> indices16{99, 0, 1, 2};
    std::array<std::uint32_t, 6> indices32{99, 99, 0, 1, 2, 3};

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleStrip,
        left.data(), 0, 3,
        indices16.data(), 1, 1);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleStrip,
        static_cast<const void*>(compactRight.data()), 0, 4,
        indices32.data(), 2, 2,
        PositionColorDeclaration());

    left.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    compactRight.fill({});
    indices16.fill(0);
    indices32.fill(0);

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.5f), Color::Red, "DrawUser Uint16 strip");
    ExpectExactColor(pixels.AtNdc(0.5f), Color::Blue, "DrawUser Uint32 strip");
    ExpectExactColor(pixels.AtNdc(0.0f), Color::Black, "DrawUser strip padding/background");

#ifdef CNA_TEST_VULKAN_AVAILABLE
    AssertNoNewVulkanValidationMessages(*vulkanRenderer, validationMessageStart);
#endif
}

TEST_F(IndexedDrawDeferredTest, IndexedTriangleStripAtoBtoAPreservesWidthsRangesAndPixels)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, WebGPU, Vulkan, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, DirectX9, DirectX11);
    RequireIndexedRendering();

#ifdef CNA_TEST_WEBGPU_AVAILABLE
    auto* renderer =
        dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPURenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, renderer);
    EXPECT_EQ(0u, renderer->GetColoredPipelineCacheSizeEXT());
    const std::size_t uncapturedBefore = renderer->GetUncapturedErrorCountEXT();
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_Validation);
#endif
#ifdef CNA_TEST_VULKAN_AVAILABLE
    auto* vulkanRenderer =
        dynamic_cast<CNA::Internal::Renderers::Vulkan::VulkanRenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, vulkanRenderer);
    const std::size_t validationMessageStart =
        vulkanRenderer->GetValidationMessagesEXT().size();
#endif

    std::vector<VertexPositionColor> vertices;
    AppendVertices(vertices, StripTriangleAt(-0.65f, Color::Red));
    AppendVertices(vertices, StripQuadAt(0.0f, Color::Lime));
    AppendVertices(vertices, FiveVertexStripAt(0.65f, Color::Blue));

    // Odd 16-bit count with a nonzero firstIndex; even dynamic 32-bit count; then another odd
    // 16-bit count from a different buffer object with a positive baseVertex.
    const std::array<std::uint16_t, 5> indices16A{99, 99, 0, 1, 2};
    const std::array<std::uint32_t, 4> indices32B{3, 4, 5, 6};
    const std::array<std::uint16_t, 5> indices16C{0, 1, 2, 3, 4};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(vertices.size()), BufferUsage::None);
    IndexBuffer static16A(
        device, IndexElementSize::SixteenBits, 5, BufferUsage::None);
    DynamicIndexBuffer dynamic32B(
        device, IndexElementSize::ThirtyTwoBits, 4, BufferUsage::None);
    IndexBuffer static16C(
        device, IndexElementSize::SixteenBits, 5, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
    static16A.SetData(indices16A.data(), 5);
    dynamic32B.SetData(
        indices32B.data(), 0, 4, SetDataOptions::Discard);
    static16C.SetData(indices16C.data(), 5);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    device.SetIndexBuffer(&static16A);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleStrip, 0, 0, 3, 2, 1);
    device.SetIndexBuffer(&dynamic32B);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleStrip, 0, 3, 4, 0, 2);
    device.SetIndexBuffer(&static16C);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleStrip, 7, 0, 5, 0, 3);

    // Replay owns the bytes, width, range, and base vertex from each command. Ending on Uint16
    // must not retroactively select that format for the queued Uint32 draw.
    const std::array<std::uint16_t, 5> degenerate16{0, 0, 0, 0, 0};
    const std::array<std::uint32_t, 4> degenerate32{0, 0, 0, 0};
    static16A.SetData(degenerate16.data(), 5);
    dynamic32B.SetData(
        degenerate32.data(), 0, 4, SetDataOptions::Discard);
    static16C.SetData(degenerate16.data(), 5);
    device.SetIndexBuffer(nullptr);
    device.SetVertexBuffer(nullptr);
    static16A.Dispose();
    dynamic32B.Dispose();
    static16C.Dispose();
    vertexBuffer.Dispose();

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.65f), Color::Red, "odd Uint16/startIndex strip");
    ExpectExactColor(pixels.AtNdc(0.0f), Color::Lime, "even dynamic Uint32 strip");
    ExpectExactColor(pixels.AtNdc(0.65f), Color::Blue, "odd Uint16/baseVertex strip");
    ExpectExactColor(pixels.AtNdc(-0.98f), Color::Black, "strip padding/background");

#ifdef CNA_TEST_WEBGPU_AVAILABLE
    // Two format-compatible Uint16 buffer objects reuse one pipeline; the intervening Uint32
    // command creates the sole required additional variant.
    EXPECT_EQ(2u, renderer->GetColoredPipelineCacheSizeEXT());
    PopAndExpectClean(*renderer);
    PopAndExpectClean(*renderer);
    EXPECT_EQ(uncapturedBefore, renderer->GetUncapturedErrorCountEXT());
#endif
#ifdef CNA_TEST_VULKAN_AVAILABLE
    AssertNoNewVulkanValidationMessages(*vulkanRenderer, validationMessageStart);
#endif
}

TEST_F(IndexedDrawDeferredTest, IndexedTopologiesRenderExactDistinctGeometry)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, Vulkan);
    RequireIndexedRendering();

#ifdef CNA_TEST_VULKAN_AVAILABLE
    auto* vulkanRenderer =
        dynamic_cast<CNA::Internal::Renderers::Vulkan::VulkanRenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, vulkanRenderer);
    const std::size_t validationMessageStart =
        vulkanRenderer->GetValidationMessagesEXT().size();
#endif

    std::vector<VertexPositionColor> vertices;
    AppendVertices(vertices, TriangleAt(-0.75f, Color::Red));
    AppendVertices(vertices, StripQuadAt(-0.25f, Color::Lime));
    vertices.emplace_back(Vector3(0.25f, -0.45f, 0.5f), Color::Blue);
    vertices.emplace_back(Vector3(0.25f,  0.45f, 0.5f), Color::Blue);
    vertices.emplace_back(Vector3(0.55f, 0.0f, 0.5f), Color::Yellow);
    vertices.emplace_back(Vector3(0.75f, 0.0f, 0.5f), Color::Yellow);
    vertices.emplace_back(Vector3(0.95f, 0.0f, 0.5f), Color::Yellow);

    const std::array<std::uint16_t, 3> triangleList{0, 1, 2};
    const std::array<std::uint16_t, 4> triangleStrip{3, 4, 5, 6};
    const std::array<std::uint16_t, 2> lineList{7, 8};
    const std::array<std::uint16_t, 3> lineStrip{9, 10, 11};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(vertices.size()), BufferUsage::None);
    IndexBuffer triangleListBuffer(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    IndexBuffer triangleStripBuffer(
        device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    IndexBuffer lineListBuffer(
        device, IndexElementSize::SixteenBits, 2, BufferUsage::None);
    IndexBuffer lineStripBuffer(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
    triangleListBuffer.SetData(triangleList.data(), 3);
    triangleStripBuffer.SetData(triangleStrip.data(), 4);
    lineListBuffer.SetData(lineList.data(), 2);
    lineStripBuffer.SetData(lineStrip.data(), 3);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    device.SetIndexBuffer(&triangleListBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
    device.SetIndexBuffer(&triangleStripBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleStrip, 0, 3, 4, 0, 2);
    device.SetIndexBuffer(&lineListBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::LineList, 0, 7, 2, 0, 1);
    device.SetIndexBuffer(&lineStripBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::LineStrip, 0, 9, 3, 0, 2);

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.75f), Color::Red, "indexed triangle list");
    ExpectExactColor(pixels.AtNdc(-0.25f), Color::Lime, "indexed triangle strip");
    ExpectExactColor(pixels.AtNdc(0.25f), Color::Blue, "indexed line list");
    ExpectExactColorNear(
        pixels, 0.65f, 0.0f, Color::Yellow, "indexed line strip first segment");
    ExpectExactColorNear(
        pixels, 0.85f, 0.0f, Color::Yellow, "indexed line strip second segment");

#ifdef CNA_TEST_VULKAN_AVAILABLE
    AssertNoNewVulkanValidationMessages(*vulkanRenderer, validationMessageStart);
#endif
}

TEST_F(IndexedDrawDeferredTest, PublicThirtyTwoBitTopologiesRenderExactDistinctGeometry)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, Vulkan);
    RequireIndexedRendering();

#ifdef CNA_TEST_VULKAN_AVAILABLE
    auto* vulkanRenderer =
        dynamic_cast<CNA::Internal::Renderers::Vulkan::VulkanRenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, vulkanRenderer);
    const std::size_t validationMessageStart =
        vulkanRenderer->GetValidationMessagesEXT().size();
#endif

    std::vector<VertexPositionColor> vertices;
    AppendVertices(vertices, TriangleAt(-0.75f, Color::Red));
    AppendVertices(vertices, StripQuadAt(-0.25f, Color::Lime));
    vertices.emplace_back(Vector3(0.25f, -0.45f, 0.5f), Color::Blue);
    vertices.emplace_back(Vector3(0.25f,  0.45f, 0.5f), Color::Blue);
    vertices.emplace_back(Vector3(0.55f, 0.0f, 0.5f), Color::Yellow);
    vertices.emplace_back(Vector3(0.75f, 0.0f, 0.5f), Color::Yellow);
    vertices.emplace_back(Vector3(0.95f, 0.0f, 0.5f), Color::Yellow);

    const std::array<std::uint32_t, 3> triangleList{0, 1, 2};
    const std::array<std::uint32_t, 4> triangleStrip{3, 4, 5, 6};
    const std::array<std::uint32_t, 2> lineList{7, 8};
    const std::array<std::uint32_t, 3> lineStrip{9, 10, 11};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(vertices.size()), BufferUsage::None);
    IndexBuffer triangleListBuffer(
        device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
    DynamicIndexBuffer triangleStripBuffer(
        device, IndexElementSize::ThirtyTwoBits, 4, BufferUsage::None);
    IndexBuffer lineListBuffer(
        device, IndexElementSize::ThirtyTwoBits, 2, BufferUsage::None);
    DynamicIndexBuffer lineStripBuffer(
        device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
    triangleListBuffer.SetData(triangleList.data(), 3);
    triangleStripBuffer.SetData(
        triangleStrip.data(), 0, 4, SetDataOptions::Discard);
    lineListBuffer.SetData(lineList.data(), 2);
    lineStripBuffer.SetData(
        lineStrip.data(), 0, 3, SetDataOptions::NoOverwrite);

#ifdef CNA_TEST_BGFX_AVAILABLE
    ExpectExactBgfxIndexFlags(triangleListBuffer, true);
    ExpectExactBgfxIndexFlags(triangleStripBuffer, true);
    ExpectExactBgfxIndexFlags(lineListBuffer, true);
    ExpectExactBgfxIndexFlags(lineStripBuffer, true);
#endif

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    device.SetIndexBuffer(&triangleListBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
    device.SetIndexBuffer(&triangleStripBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleStrip, 0, 3, 4, 0, 2);
    device.SetIndexBuffer(&lineListBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::LineList, 0, 7, 2, 0, 1);
    device.SetIndexBuffer(&lineStripBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::LineStrip, 0, 9, 3, 0, 2);

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.75f), Color::Red, "Uint32 triangle list");
    ExpectExactColor(pixels.AtNdc(-0.25f), Color::Lime, "Uint32 triangle strip");
    ExpectExactColor(pixels.AtNdc(0.25f), Color::Blue, "Uint32 line list");
    ExpectExactColorNear(
        pixels, 0.65f, 0.0f, Color::Yellow, "Uint32 line strip first segment");
    ExpectExactColorNear(
        pixels, 0.85f, 0.0f, Color::Yellow, "Uint32 line strip second segment");

#ifdef CNA_TEST_VULKAN_AVAILABLE
    AssertNoNewVulkanValidationMessages(*vulkanRenderer, validationMessageStart);
#endif
}


TEST_F(IndexedDrawDeferredTest, SoftwareExplicitlyRejectsUnsupportedIndexedTopologies)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software);
    RequireIndexedRendering();

    const auto vertices = StripQuadAt(0.0f, Color::White);
    const std::array<std::uint16_t, 4> indices{0, 1, 2, 3};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 4, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 4);
    indexBuffer.SetData(indices.data(), 4);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleStrip, 0, 0, 4, 0, 2),
        std::runtime_error);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::LineList, 0, 0, 4, 0, 2),
        std::runtime_error);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::LineStrip, 0, 0, 4, 0, 3),
        std::runtime_error);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::PointListEXT, 0, 0, 4, 0, 4),
        std::runtime_error);
}

// REMED-GFX-110: the CPU raster paths address real host storage, so a decoded index that leaves
// the bound vertex buffer must be rejected deterministically instead of forming an out-of-range
// pointer. The public arguments below are all individually legal; only the decoded address is not.
TEST_F(IndexedDrawDeferredTest, SoftwareRejectsDecodedVertexAddressesOutsideTheBoundBuffer)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software);
    RequireIndexedRendering();

    const auto triangle = CenterTriangle(Color::White);
    const std::array<VertexPositionColor, 3> vertices{
        triangle[0], triangle[1], triangle[2],
    };
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 3);

    const std::array<std::uint16_t, 3> pastEnd{0, 1, 7};
    IndexBuffer pastEndBuffer(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    pastEndBuffer.SetData(pastEnd.data(), 3);

    const std::array<std::uint16_t, 3> inRange{0, 1, 2};
    IndexBuffer basedBuffer(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    basedBuffer.SetData(inRange.data(), 3);

    const std::array<std::uint32_t, 3> wrapping{
        0, 1, std::numeric_limits<std::uint32_t>::max()};
    IndexBuffer wrappingBuffer(
        device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
    wrappingBuffer.SetData(wrapping.data(), 3);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    device.SetIndexBuffer(&pastEndBuffer);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1),
        System::ArgumentOutOfRangeException);

    device.SetIndexBuffer(&basedBuffer);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 1, 0, 2, 0, 1),
        System::ArgumentOutOfRangeException);

    device.SetIndexBuffer(&wrappingBuffer);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 1, 0, 2, 0, 1),
        System::ArgumentOutOfRangeException);

    // No rejected draw may have written a pixel.
    ExpectExactColor(
        ReadCenter(device), Color::Black,
        "rejected Software indexed draws write nothing");
}

TEST_F(IndexedDrawDeferredTest, PublicContractValidatesEveryIndexedRangeBeforeSubmission)
{
    RequireIndexedRendering();

    EXPECT_EQ(6, GraphicsDevice::PrimitiveVerts(PrimitiveType::TriangleList, 2));
    EXPECT_EQ(4, GraphicsDevice::PrimitiveVerts(PrimitiveType::TriangleStrip, 2));
    EXPECT_EQ(4, GraphicsDevice::PrimitiveVerts(PrimitiveType::LineList, 2));
    EXPECT_EQ(3, GraphicsDevice::PrimitiveVerts(PrimitiveType::LineStrip, 2));
    // PointListEXT's element count is part of the indexed range contract. Its separate Bgfx
    // topology mapping remains REMED-GFX-111 and is intentionally not asserted as pixels here.
    EXPECT_EQ(2, GraphicsDevice::PrimitiveVerts(PrimitiveType::PointListEXT, 2));

    const auto triangle = CenterTriangle(Color::White);
    const std::array<VertexPositionColor, 9> vertices{
        triangle[0], triangle[1], triangle[2],
        triangle[0], triangle[1], triangle[2],
        triangle[0], triangle[1], triangle[2],
    };
    const std::array<std::uint16_t, 9> indices{};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 9, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 9, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 9);
    indexBuffer.SetData(indices.data(), 9);
    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    EXPECT_NO_THROW(device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 9, 0, 1));
    EXPECT_NO_THROW(device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 9, 3, 1));
    EXPECT_NO_THROW(device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 9, 6, 1));

    struct CountCase
    {
        PrimitiveType primitive;
        int primitiveCount;
        int consumedIndices;
    };
    constexpr std::array<CountCase, 5> countCases{{
        {PrimitiveType::TriangleList, 2, 6},
        {PrimitiveType::TriangleStrip, 2, 4},
        {PrimitiveType::LineList, 2, 4},
        {PrimitiveType::LineStrip, 2, 3},
        {PrimitiveType::PointListEXT, 2, 2},
    }};
    for (const auto& countCase : countCases)
    {
        EXPECT_THROW(
            device.DrawIndexedPrimitives(
                countCase.primitive,
                0,
                0,
                9,
                9 - countCase.consumedIndices,
                countCase.primitiveCount + 1),
            System::ArgumentOutOfRangeException);
    }

    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 0, 0, 9, -1, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 0, 0, 9, 7, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 0, 0, 9, 10, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 0, 0, 9, 0, 4),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList,
            0,
            0,
            9,
            0,
            std::numeric_limits<int>::max()),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 0, 0, 9, 0, 0),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, -1, 0, 9, 0, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 0, -1, 9, 0, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 0, 0, -1, 0, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 0, 0, 0, 0, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 10, 0, 1, 0, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 3, 7, 1, 0, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 3, 3, 4, 0, 1),
        System::ArgumentOutOfRangeException);
}

TEST_F(IndexedDrawDeferredTest, PublicContractRejectsNegativeIndexedBaseVertex)
{
    if (!device.SupportsCapability(GraphicsCapability::ThreeD))
        GTEST_SKIP() << "Renderer explicitly does not support indexed rendering";

    const auto vertices = StripTriangleAt(0.0f, Color::White);
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 3, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 3);
    indexBuffer.SetData(indices.data(), 3);
    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    // CNA's current public contract rejects every negative baseVertex before renderer dispatch;
    // positive baseVertex behavior is exercised by the rendering test above.
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleStrip, -1, 0, 3, 0, 1),
        System::ArgumentOutOfRangeException);
}

#ifdef CNA_TEST_WEBGPU_AVAILABLE
TEST_F(IndexedDrawDeferredTest, WebGpuIndexedTriangleStripMatchesBoundIndexFormat)
{
    RequireIndexedRendering();

    auto* renderer =
        dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPURenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, renderer);
    EXPECT_EQ(0u, renderer->GetColoredPipelineCacheSizeEXT());
    const std::size_t uncapturedBefore = renderer->GetUncapturedErrorCountEXT();
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_Validation);

    const std::array<VertexPositionColor, 4> vertices{
        VertexPositionColor(Vector3(-0.6f, -0.6f, 0.5f), Color::Lime),
        VertexPositionColor(Vector3(-0.6f,  0.6f, 0.5f), Color::Lime),
        VertexPositionColor(Vector3( 0.6f, -0.6f, 0.5f), Color::Lime),
        VertexPositionColor(Vector3( 0.6f,  0.6f, 0.5f), Color::Lime),
    };
    const std::array<std::uint16_t, 4> indices{0, 1, 2, 3};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 4, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 4);
    indexBuffer.SetData(indices.data(), 4);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleStrip, 0, 0, 4, 0, 2);

    ExpectExactColor(ReadCenter(device), Color::Lime, "minimal Uint16 indexed strip");
    EXPECT_EQ(1u, renderer->GetColoredPipelineCacheSizeEXT());
    PopAndExpectClean(*renderer);
    PopAndExpectClean(*renderer);
    EXPECT_EQ(uncapturedBefore, renderer->GetUncapturedErrorCountEXT());
}

TEST_F(IndexedDrawDeferredTest, WebGpuUserAndNonIndexedStripsUseExactPipelineVariants)
{
    RequireIndexedRendering();

    auto* renderer =
        dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPURenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, renderer);
    const std::size_t uncapturedBefore = renderer->GetUncapturedErrorCountEXT();
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_Validation);

    auto listVertices = TriangleAt(-0.75f, Color::Red);
    auto nonIndexedStrip = StripQuadAt(-0.25f, Color::Yellow);
    auto strip16 = StripQuadAt(0.25f, Color::Lime);
    auto strip32 = StripQuadAt(0.75f, Color::Blue);
    struct CompactPositionColor
    {
        float x;
        float y;
        float z;
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        std::uint8_t a;
    };
    static_assert(sizeof(CompactPositionColor) == 16);
    std::array<CompactPositionColor, 4> compactStrip32{};
    for (std::size_t i = 0; i < strip32.size(); ++i)
    {
        compactStrip32[i] = {
            strip32[i].Position.X,
            strip32[i].Position.Y,
            strip32[i].Position.Z,
            strip32[i].Color.getRProperty(),
            strip32[i].Color.getGProperty(),
            strip32[i].Color.getBProperty(),
            strip32[i].Color.getAProperty(),
        };
    }
    std::array<std::uint16_t, 5> indices16{99, 0, 1, 2, 3};
    std::array<std::uint32_t, 6> indices32{99, 99, 0, 1, 2, 3};

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);

    // TriangleList -> non-indexed TriangleStrip -> TriangleList: the list pipeline is reused and
    // the non-indexed strip declares Undefined, not a remembered index format.
    device.DrawUserPrimitives(
        PrimitiveType::TriangleList, listVertices.data(), 0, 1);
    device.DrawUserPrimitives(
        PrimitiveType::TriangleStrip, nonIndexedStrip.data(), 0, 2);
    listVertices = TriangleAt(-0.75f, Color::White);
    device.DrawUserPrimitives(
        PrimitiveType::TriangleList, listVertices.data(), 0, 1);

    // Both typed widths take DrawUser's function-local transient-buffer path. The explicit
    // declaration on the Uint32 draw also preserves the completed GFX-043 transport contract.
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleStrip,
        strip16.data(), 0, 4,
        indices16.data(), 1, 2);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleStrip,
        static_cast<const void*>(compactStrip32.data()), 0, 4,
        indices32.data(), 2, 2,
        PositionColorDeclaration());

    listVertices.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    nonIndexedStrip.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    strip16.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    strip32.fill(VertexPositionColor(Vector3(4, 4, 0.5f), Color::Black));
    compactStrip32.fill({});
    indices16.fill(0);
    indices32.fill(0);

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.75f), Color::White, "list after non-indexed strip");
    ExpectExactColor(pixels.AtNdc(-0.25f), Color::Yellow, "non-indexed strip");
    ExpectExactColor(pixels.AtNdc(0.25f), Color::Lime, "DrawUser Uint16 strip");
    ExpectExactColor(pixels.AtNdc(0.75f), Color::Blue, "DrawUser Uint32 strip");

    // One list/Undefined pipeline plus TriangleStrip Undefined, Uint16, and Uint32.
    EXPECT_EQ(4u, renderer->GetColoredPipelineCacheSizeEXT());
    PopAndExpectClean(*renderer);
    PopAndExpectClean(*renderer);
    EXPECT_EQ(uncapturedBefore, renderer->GetUncapturedErrorCountEXT());
}

TEST_F(IndexedDrawDeferredTest, WebGpuIndexedListPipelinesIgnoreIndexWidth)
{
    RequireIndexedRendering();

    auto* renderer =
        dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPURenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, renderer);
    const std::size_t uncapturedBefore = renderer->GetUncapturedErrorCountEXT();
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_Validation);

    const auto left = TriangleAt(-0.5f, Color::Red);
    const auto right = TriangleAt(0.5f, Color::Blue);
    const std::array<VertexPositionColor, 6> vertices{
        left[0], left[1], left[2], right[0], right[1], right[2],
    };
    const std::array<std::uint16_t, 3> triangle16{0, 1, 2};
    const std::array<std::uint32_t, 3> triangle32{3, 4, 5};
    const std::array<std::uint16_t, 2> line16{0, 3};
    const std::array<std::uint32_t, 2> line32{2, 5};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 6, BufferUsage::None);
    IndexBuffer triangleBuffer16(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    IndexBuffer triangleBuffer32(
        device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
    IndexBuffer lineBuffer16(
        device, IndexElementSize::SixteenBits, 2, BufferUsage::None);
    IndexBuffer lineBuffer32(
        device, IndexElementSize::ThirtyTwoBits, 2, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    triangleBuffer16.SetData(triangle16.data(), 3);
    triangleBuffer32.SetData(triangle32.data(), 3);
    lineBuffer16.SetData(line16.data(), 2);
    lineBuffer32.SetData(line32.data(), 2);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    device.SetIndexBuffer(&triangleBuffer16);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
    device.SetIndexBuffer(&triangleBuffer32);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 3, 3, 0, 1);
    device.SetIndexBuffer(&lineBuffer16);
    device.DrawIndexedPrimitives(
        PrimitiveType::LineList, 0, 0, 6, 0, 1);
    device.SetIndexBuffer(&lineBuffer32);
    device.DrawIndexedPrimitives(
        PrimitiveType::LineList, 0, 0, 6, 0, 1);

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(pixels.AtNdc(-0.5f), Color::Red, "Uint16 triangle list");
    ExpectExactColor(pixels.AtNdc(0.5f), Color::Blue, "Uint32 triangle list");

    // Width is irrelevant to list topology: one TriangleList and one LineList pipeline.
    EXPECT_EQ(2u, renderer->GetColoredPipelineCacheSizeEXT());
    PopAndExpectClean(*renderer);
    PopAndExpectClean(*renderer);
    EXPECT_EQ(uncapturedBefore, renderer->GetUncapturedErrorCountEXT());
}

TEST_F(IndexedDrawDeferredTest, WebGpuTriangleStripAlternatesWindingBeforeCulling)
{
    RequireIndexedRendering();

    auto* renderer =
        dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPURenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, renderer);
    const std::size_t uncapturedBefore = renderer->GetUncapturedErrorCountEXT();
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_Validation);

    std::vector<VertexPositionColor> vertices;
    AppendVertices(vertices, StripQuadAt(-0.5f, Color::Lime));
    AppendVertices(vertices, StripQuadAt(0.5f, Color::Red));
    const std::array<std::uint16_t, 4> leftIndices{0, 1, 2, 3};
    const std::array<std::uint16_t, 4> rightIndices{4, 5, 6, 7};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 8, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 8);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    indexBuffer.SetData(leftIndices.data(), 4);
    device.SetIndexBuffer(&indexBuffer);
    device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleStrip, 0, 0, 4, 0, 2);

    indexBuffer.SetData(rightIndices.data(), 4);
    device.setRasterizerStateProperty(RasterizerState::CullClockwise);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleStrip, 0, 4, 4, 0, 2);

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    // Sample one interior point from each alternating triangle in each strip.
    ExpectExactColor(
        pixels.AtNdc(-0.58f, 0.20f), Color::Lime,
        "front-facing strip first triangle");
    ExpectExactColor(
        pixels.AtNdc(-0.42f, -0.20f), Color::Lime,
        "front-facing strip second triangle");
    ExpectExactColor(
        pixels.AtNdc(0.42f, 0.20f), Color::Black,
        "clockwise-cull strip first triangle");
    ExpectExactColor(
        pixels.AtNdc(0.58f, -0.20f), Color::Black,
        "clockwise-cull strip second triangle");

    PopAndExpectClean(*renderer);
    PopAndExpectClean(*renderer);
    EXPECT_EQ(uncapturedBefore, renderer->GetUncapturedErrorCountEXT());
}

TEST_F(IndexedDrawDeferredTest, WebGpuIndexedStripsRenderToTargetAndBackbuffer)
{
    RequireIndexedRendering();

    auto* renderer =
        dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPURenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, renderer);
    const std::size_t uncapturedBefore = renderer->GetUncapturedErrorCountEXT();
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_Validation);

    auto vertices = StripQuadAt(0.0f, Color::Red);
    const std::array<std::uint16_t, 4> indices16{0, 1, 2, 3};
    const std::array<std::uint32_t, 4> indices32{0, 1, 2, 3};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 4, BufferUsage::None);
    IndexBuffer buffer16(
        device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    IndexBuffer buffer32(
        device, IndexElementSize::ThirtyTwoBits, 4, BufferUsage::None);
    RenderTarget2D target(
        device, 64, 64, false, SurfaceFormat::Color,
        DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    vertexBuffer.SetData(vertices.data(), 4);
    buffer16.SetData(indices16.data(), 4);
    buffer32.SetData(indices32.data(), 4);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.SetVertexBuffer(&vertexBuffer);

    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    device.SetIndexBuffer(&buffer16);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleStrip, 0, 0, 4, 0, 2);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    Color targetPixel = Color::Transparent;
    const Rectangle targetRegion(32, 32, 1, 1);
    target.GetData(0, &targetRegion, &targetPixel, 0, 1);
    ExpectExactColor(targetPixel, Color::Red, "RenderTarget2D Uint16 strip");

    vertices = StripQuadAt(0.0f, Color::Blue);
    vertexBuffer.SetData(vertices.data(), 4);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&buffer32);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleStrip, 0, 0, 4, 0, 2);

    const BackbufferSnapshot pixels = ReadBackbufferOnce(device);
    ExpectExactColor(
        pixels.AtNdc(0.0f), Color::Blue,
        "backbuffer Uint32 strip");
    EXPECT_EQ(2u, renderer->GetColoredPipelineCacheSizeEXT());

    PopAndExpectClean(*renderer);
    PopAndExpectClean(*renderer);
    EXPECT_EQ(uncapturedBefore, renderer->GetUncapturedErrorCountEXT());
}

TEST_F(IndexedDrawDeferredTest, WebGpuNativeScopesCoverLogicalCountsAndInternalPadding)
{
    RequireIndexedRendering();

    auto* renderer =
        dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPURenderer*>(
            &device.GetRenderer());
    ASSERT_NE(nullptr, renderer);
    const std::size_t uncapturedBefore = renderer->GetUncapturedErrorCountEXT();
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(renderer->Device(), WGPUErrorFilter_Validation);

    const auto vertices = CenterTriangle(Color::White);
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 3);
    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    const auto queue16 = [&](const std::vector<std::uint16_t>& indices,
                             PrimitiveType primitive,
                             int primitiveCount)
    {
        IndexBuffer indexBuffer(
            device, IndexElementSize::SixteenBits,
            static_cast<int>(indices.size()), BufferUsage::None);
        indexBuffer.SetData(indices.data(), static_cast<int>(indices.size()));
        ExpectExactWebGpuIndexShadow(indexBuffer, indices);
        device.SetIndexBuffer(&indexBuffer);
        device.DrawIndexedPrimitives(
            primitive, 0, 0, 3, 0, primitiveCount);
        device.SetIndexBuffer(nullptr);
        indexBuffer.Dispose();
    };

    queue16({0}, PrimitiveType::PointListEXT, 1);
    queue16({0, 1}, PrimitiveType::PointListEXT, 2);
    queue16({0, 1, 2}, PrimitiveType::TriangleList, 1);
    queue16({0, 1, 2, 0}, PrimitiveType::PointListEXT, 4);
    queue16({0, 1, 2, 0, 1}, PrimitiveType::PointListEXT, 5);
    queue16({0, 1, 2, 0, 1, 2}, PrimitiveType::TriangleList, 2);
    queue16({0, 1, 2, 0, 1, 2, 0, 1, 2}, PrimitiveType::TriangleList, 3);

    const std::vector<std::uint16_t> dynamicIndices{0, 1, 2, 0, 1};
    DynamicIndexBuffer dynamicBuffer(
        device, IndexElementSize::SixteenBits, 5, BufferUsage::None);
    dynamicBuffer.SetData(
        dynamicIndices.data(), 0, 5, SetDataOptions::NoOverwrite);
    ExpectExactWebGpuIndexShadow(dynamicBuffer, dynamicIndices);
    device.SetIndexBuffer(&dynamicBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::PointListEXT, 0, 0, 3, 0, 5);
    device.SetIndexBuffer(nullptr);
    dynamicBuffer.Dispose();

    const std::vector<std::uint32_t> indices32{0, 1, 2};
    IndexBuffer buffer32(
        device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
    buffer32.SetData(indices32.data(), 3);
    ExpectExactWebGpuIndexShadow(buffer32, indices32);
    device.SetIndexBuffer(&buffer32);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
    device.SetIndexBuffer(nullptr);
    buffer32.Dispose();

    // DrawUser's function-local temporary buffers take the same deferred snapshot path.
    const std::array<std::uint16_t, 3> userIndices{0, 1, 2};
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList,
        vertices.data(), 0, 3,
        userIndices.data(), 0, 1);

    device.SetVertexBuffer(nullptr);
    vertexBuffer.Dispose();
    device.Present();

    PopAndExpectClean(*renderer);
    PopAndExpectClean(*renderer);
    EXPECT_EQ(uncapturedBefore, renderer->GetUncapturedErrorCountEXT());
}
#endif
